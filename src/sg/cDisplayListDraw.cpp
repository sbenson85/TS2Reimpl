// cDisplayList's view slots (DLb in docs/subsystems/scenegraph.md): DrawViewer (slot 23) pushes a pass and sets it up
// from a viewer node, DrawPass (slot 24) draws the current pass and pops it; with their helpers, the pass reset, the
// draw item builder, AddGeometry (slot 29) and the strategy call. The frame cache the passes flip is sg/cFrameCache.h.
#include "sg/cDisplayList.h"

#include "compgeom/Frames.h"  // AsFrame(...).ToMatrix4
#include "compgeom/Matrix4.h" // Matrix4Multiply
#include "game/game.h"
#include "game/owned.h"
#include "gz/cParserTexts.h" // the line end 0xd9e644
#include "gz/GraphicServices.h" // GetCurrentDeviceCached
#include "gz/cDeviceDX.h"
#include "sg/DisplayListSort.h"
#include "sg/cDisplayListLog.h"
#include "sg/cFrameCache.h"
#include "sg/cIGeometryRender.h"
#include "sg/cIViewerRefNode.h"
#include "sg/cLightRefNode.h" // cILightRefNode
#include "sg/cMaterialManager.h"
#include "sg/cShader.h" // cShader::GetInfo, ShaderInfo
#include "sg/cViewerRefNodeBaseImpl.h" // cViewerTargetSize

#include <cstring>

// The pass log's texts (the game's texts; standalone/owned_statics.cpp holds them for an image-free build).
TS2_OWNED_STATIC(const char, e75524)[];
TS2_OWNED_STATIC(const char, e7551c)[];
TS2_OWNED_STATIC(const char, e75548)[];

namespace nGZSceneGraph {

namespace {

constexpr uint32_t kNewSubKey = 0x60000;           // the builder's sub key
constexpr uint8_t kSetupLayerTagMatch = 2;         // a reset pass's tag match (tags ignored)

// The log's texts (the game's strings): before the render type, after it, before the item count; the line end.
constexpr const char *kLogViewText = ts2_rva_e75524;
constexpr const char *kLogViewEndText = ts2_rva_e7551c;
constexpr const char *kLogItemsText = ts2_rva_e75548;
constexpr const char *kLogLineEnd = ts2_rva_d9e644;

// The viewport rounding: `dimension` (pixels) times `fraction` to the nearest integer as the original computes it: the
// value less a half, then the half added back with 1.5 * 2^23 so the float's low bits hold the integer (the bias
// subtracted from the bits), one less when that one less converts back to exactly the value less a half (a tie).
int32_t RoundViewport(uint32_t dimension, float fraction)
{
    const float scaled = static_cast<float>(static_cast<double>(dimension)) * fraction;
    const float value = scaled - 0.5f;
    const float biased = (value + 0.5f) + 12582912.0f;
    uint32_t bits;
    std::memcpy(&bits, &biased, sizeof bits);
    const uint32_t rounded = bits + 0xb4c00000u;
    const uint32_t lower = rounded - 1;
    // An ordered equality: a NaN keeps `rounded`.
    return static_cast<int32_t>(static_cast<float>(static_cast<int32_t>(lower)) == value ? lower : rounded);
}

// Copies the 16 floats of `from` (the original copies through a temporary).
void CopyMatrix(float *to, const float *from) { std::memcpy(to, from, 16 * sizeof(float)); }

} // namespace

// ---- the pass reset ---------------------------------------------------------------------------------------------------

void cDisplayList::SetupTopPass()
{
    Pass *const pass = mPassStack.back();
    pass->mOwner = this;
    pass->mFrame = mFrame;
    pass->mTime = mTime;
    pass->mViewer = nullptr;
    pass->mRenderType = 0;
    pass->mLayerState[0] = 0xffffffffu;
    pass->mLayerState[1] = 0;
    pass->mLayerTagMatch = kSetupLayerTagMatch;
    pass->mTextureMode = 0;
    pass->mViewportMin[0] = 0.0f;
    pass->mViewportMin[1] = 0.0f;
    pass->mViewportMax[0] = 1.0f;
    pass->mViewportMax[1] = 1.0f;
    pass->mDepthRange[0] = 0.0f;
    pass->mDepthRange[1] = 1.0f;
    pass->mColorSurface = nullptr;
    pass->mDepthSurface = nullptr;
    pass->mTextureCount = 0;
    pass->mTextureRef94 = nullptr;
    pass->mHandleTexture = nullptr;
    pass->mWidth = 0;
    pass->mHeight = 0;
    pass->mScale = 0.0f;
    for (cIGZUnknown *&texture : pass->mTextures) texture = nullptr;
    pass->mLastShader = nullptr;
    pass->mDrawStats[0] = pass->mDrawStats[1] = pass->mDrawStats[2] = 0;
    pass->mDevice = nGZGraphic4::GetCurrentDeviceCached();
    pass->mDeviceState = pass->mDevice->GetStateConst();
    pass->mVertices1 = mVertices1.p;
    pass->mVertices2 = mVertices2.p;
    pass->mIndices1 = mIndices1.p;
    pass->mIndices2 = mIndices2.p;
    pass->mFrameCache = nullptr;
    pass->mEntryCountA = 0;
    pass->mEntryCountB = 0;
    pass->mNodeCount = 0;
    NodeRecord &record = pass->mViewerRecord;
    record.mNode = nullptr;
    record.mLayerState[0] = 0xffffffffu;
    record.mLayerState[1] = 0;
    record.mCurrent = 1;
    record.mPlaneMask = 0;
    record.mPlacement = ts2_rva_106e3d8;
    record.mBounds = nGZCompGeom::BoundBox{};
    // The render slots' counts.
    mCount54 = 0;
    mCount64 = 0;
    mCount74 = 0;
    mWords84[0] = mWords84[1] = mWords84[2] = mWords84[3] = 0;
    mWordA0 = 0;
    mSortedCount = 0;
}

// ---- the strategy -------------------------------------------------------------------------------------------------------

// No C++ object lives here: the frame is the structured handler's alone.
void cDisplayList::RenderWithStrategy(cIDisplayListRenderStrategy *strategy, Pass *pass)
{
    __try {
        strategy->BeginPass(pass);
        strategy->Render(pass, mItems.data(), mSortedCount);
    } __except (1) {
    }
}

// ---- the draw items ---------------------------------------------------------------------------------------------------

bool cDisplayList::BuildDrawItem(DrawItem *item, cIGeometryRender *geometry, cIMaterialManager *materialManager,
                                 int32_t material, Pass *pass)
{
    if (material == -1 || !pass) return false;
    geometry->GetFlags();
    const auto *const info = reinterpret_cast<const GeometrySubset *>(geometry->GetDrawInfo(0));
    item->mMaterial = nullptr;
    item->mSortKey = 0;
    item->mGeometry = geometry;
    item->mSubKey = kNewSubKey;
    item->mLayer = info->mLayer;
    item->mDepth = 0.0f;
    item->mWords20[0] = item->mWords20[1] = 0;
    item->mPlacement = &ts2_rva_10728c8;
    item->mMaterialIndex = material;
    item->mRecord = pass->mRecord;
    item->mWord3c = 0;
    item->mState[0] = item->mState[1] = item->mState[2] = 0;
    // The state's third word: the format's bits at 4, the pass's render type at 11, the info's flag at 0.
    uint32_t state = (info->mFormat->GetFormatBits() << 4) & 0x7f0;
    state |= (pass->mRenderType << 11) & 0x7800;
    state |= info->mFlag != 0 ? 1u : 0u;
    item->mState[2] = state;
    cIGZUnknown *const variant = materialManager->GetVariant(static_cast<uint32_t>(material), item->mState, 0, 0, 0);
    item->mMaterial = variant;
    const ShaderInfo *const shader = reinterpret_cast<cShader *>(variant)->GetInfo();
    item->mSortKey = static_cast<int32_t>(shader->m84);
    if (shader->mCount == 0) return false;
    return geometry->IsSupported(shader->mFlags[0]);
}

void cDisplayList::AddGeometry(cIGeometryRender *geometry, int32_t material, Pass *pass, uint32_t userData)
{
    DrawItem item{};
    if (!BuildDrawItem(&item, geometry, mMaterialManager, material, pass)) return;
    item.mUserData = userData;
    InsertDrawItem(&item);
}

// ---- slot 24 ------------------------------------------------------------------------------------------------------------

void cDisplayList::DrawPass(uint32_t count, const int32_t *materials, uint32_t userDataBase)
{
    if (!mBuffersReady) {
        SetupTopPass();
        mPassStack.pop_back();
        return;
    }
    Pass *const pass = mPassStack.back();
    // The first entry list is drawn by every pass but the last (the end read again after each draw).
    if (mPassStack.size() != mPasses.size() && pass->mEntryCountA != 0) {
        for (PassEntry *entry = pass->mEntriesA.data(); entry != pass->mEntriesA.data() + pass->mEntryCountA;
             ++entry)
            static_cast<cIRenderableNode *>(entry->mObject)
                ->Slot83(pass, reinterpret_cast<void *>(static_cast<uintptr_t>(entry->mFlag)));
    }
    // The lights (slot 26), counted in mWords84[1] as they draw.
    mWords84[1] = 0;
    if (pass->mEntryCountB != 0) {
        do {
            const PassEntry &entry = pass->mEntriesB.data()[mWords84[1]];
            static_cast<cIRenderableNode *>(entry.mObject)
                ->Slot83(pass, reinterpret_cast<void *>(static_cast<uintptr_t>(entry.mFlag)));
            ++mWords84[1];
        } while (mWords84[1] != pass->mEntryCountB);
    }
    // The nodes (slot 25), counted in mWords84[2]; each sees the sorted count so far.
    const uint32_t nodeCount = pass->mNodeCount;
    if (nodeCount != 0) {
        NodeRecord *record = pass->mNodes.data();
        mWords84[2] = 0;
        do {
            mWordE4 = mSortedCount;
            record->mNode->Slot83(pass, reinterpret_cast<void *>(static_cast<uintptr_t>(record->mPlaneMask)));
            ++mWords84[2];
            ++record;
        } while (mWords84[2] != nodeCount);
    }
    const uint32_t frame = mFrame;
    pass->mFrameCache->Flip(GetSlotCount(), frame);
    // The lights' records forget their index.
    for (int32_t i = 0; i < static_cast<int32_t>(mCount54); ++i)
        mRecords58[i].mNode->SetWord78(-1);
    if (mSortedCount != 0) {
        DrawItem *const items = mItems.data();
        DisplayListSort::SortItems(items, items + mSortedCount, static_cast<ptrdiff_t>(mSortedCount), DrawItemOrder{});
    }
    if (mItems.size() < count + mSortedCount) mItems.resize(count + mSortedCount);
    // The screen rect once per material.
    for (uint32_t i = 0; i < count; ++i) {
        DrawItem item{};
        if (BuildDrawItem(&item, mScreenRect.p, mMaterialManager, materials[i], pass)) {
            item.mUserData = i + userDataBase;
            InsertDrawItem(&item);
        }
    }
    if (mLog.p) {
        cIDisplayListLog *log = static_cast<cIDisplayListLog *>(mLog.p);
        log->SetRZCharStr(kLogViewText);
        log->SetUint32(pass->mViewer->GetRenderType());
        log->SetRZCharStr(kLogViewEndText);
        log = static_cast<cIDisplayListLog *>(mLog.p);
        log->SetRZCharStr(kLogItemsText);
        log->SetUint32(mSortedCount);
        log->SetRZCharStr(kLogLineEnd);
        LogDrawItems(static_cast<cIDisplayListLog *>(mLog.p), mItems.data(), static_cast<int32_t>(mSortedCount));
        static_cast<cIDisplayListLog *>(mLog.p)->SetRZCharStr(kLogLineEnd);
    }
    if (mSwitches[1]) {
        cIDisplayListRenderStrategy *strategy = pass->mViewer->GetRenderStrategy();
        if (!strategy) strategy = mStrategy.p;
        pass->mLastShader = nullptr;
        pass->mDrawStats[0] = pass->mDrawStats[1] = pass->mDrawStats[2] = 0;
        RenderWithStrategy(strategy, pass);
        mStats[0] += pass->mDrawStats[0];
        mStats[4] += pass->mDrawStats[1];
        mStats[5] += pass->mDrawStats[2];
        mStats[1] += mCount54;
    }
    SetupTopPass();
    mPassStack.pop_back();
}

// ---- slot 23 ------------------------------------------------------------------------------------------------------------

Pass *cDisplayList::DrawViewer(cIViewerRefNodeBase *viewer, const float *viewportMin, const float *viewportMax,
                               const float *depthRange, const nGZCompGeom::FramePlacement *view,
                               const float *projection, const float *matrix)
{
    nGZGraphic4::GetCurrentDeviceCached();
    mPassStack.push_back(mPasses.data() + mPassStack.size());
    SetupTopPass();
    Pass *const pass = mPassStack.back();
    pass->mViewer = viewer;
    pass->mRenderType = viewer->GetRenderType();
    uint32_t layerState[2];
    pass->mLayerState[0] = viewer->GetLayerState(layerState)[0];
    pass->mLayerState[1] = viewer->GetLayerState(layerState)[1];
    pass->mLayerTagMatch = viewer->GetLayerTagMatch();
    pass->mTextureMode = viewer->GetTextureMode();
    pass->mViewportMin[1] = viewportMin[1];
    pass->mViewportMin[0] = viewportMin[0];
    pass->mViewportMax[1] = viewportMax[1];
    pass->mViewportMax[0] = viewportMax[0];
    pass->mDepthRange[0] = depthRange[0];
    pass->mDepthRange[1] = depthRange[1];
    pass->mColorSurface = viewer->GetColorSurface();
    pass->mDepthSurface = viewer->GetDepthSurface();
    pass->mTextureCount = viewer->GetTextureCount();
    pass->mTextureRef94 = viewer->GetTextureRef94();
    pass->mHandleTexture = viewer->GetHandleTexture();
    // ORIGINAL-BUG: the count is not bounded by the four texture words: a fifth texture and on overwrite the words after
    // them (mTextureRef94, mHandleTexture, the size...).
    cIGZUnknown **const textures = reinterpret_cast<cIGZUnknown **>(reinterpret_cast<uint8_t *>(pass) +
                                                                     offsetof(Pass, mTextures));
    for (uint32_t i = 0; i < pass->mTextureCount; ++i) textures[i] = viewer->GetTexture(static_cast<int32_t>(i));
    cViewerTargetSize size;
    viewer->GetTargetSize(&size);
    pass->mWidth = size.mWidth;
    pass->mHeight = size.mHeight;
    pass->mScale = size.mScale;
    pass->mPixelRect[0] = RoundViewport(size.mWidth, pass->mViewportMin[0]);
    pass->mPixelRect[1] = RoundViewport(size.mHeight, pass->mViewportMin[1]);
    pass->mPixelRect[2] = RoundViewport(size.mWidth, pass->mViewportMax[0]);
    pass->mPixelRect[3] = RoundViewport(size.mHeight, pass->mViewportMax[1]);
    // The view, the projection ([0]) and the last matrix ([2]); the inverse view; the products.
    pass->mView = *view;
    CopyMatrix(pass->mMatrices[0], projection);
    CopyMatrix(pass->mMatrices[2], matrix);
    nGZCompGeom::FramePlacement inverse = pass->mView;
    pass->mViewInverse = *inverse.Invert();
    float viewMatrix[16];
    float product[16];
    nGZCompGeom::AsFrame(pass->mViewInverse).ToMatrix4(viewMatrix);
    CopyMatrix(pass->mMatrices[1], nGZCompGeom::Matrix4Multiply(product, viewMatrix, pass->mMatrices[0]));
    CopyMatrix(pass->mMatrices[3], nGZCompGeom::Matrix4Multiply(product, pass->mMatrices[0], pass->mMatrices[2]));
    CopyMatrix(pass->mMatrices[4], nGZCompGeom::Matrix4Multiply(product, pass->mMatrices[1], pass->mMatrices[2]));
    pass->mFrustum.FromView(pass->mMatrices[0]);
    // The viewer's frame cache (its holder answers the query; neither is checked for null).
    cIGZUnknown *const holder = viewer->GetFrameCacheHolder();
    cFrameCache *cache = nullptr;
    holder->QueryInterface(cFrameCache::kIid, reinterpret_cast<void **>(&cache));
    pass->mFrameCache = cache;
    // A cache more than a frame behind drops everything it holds.
    if (mFrame - cache->mFrame > 1) {
        cache->Flip(0, 0);
        pass->mFrameCache->Flip(0, 0);
    }
    NodeRecord &record = pass->mViewerRecord;
    record.mLayerState[0] = pass->mLayerState[0];
    record.mLayerState[1] = pass->mLayerState[1];
    record.mNode = viewer;
    record.mPlacement = ts2_rva_106e3d8;
    const cBoundBox bounds = viewer->GetBoundBoxIn(reinterpret_cast<const cTransform34 *>(&pass->mViewInverse));
    static_assert(sizeof(bounds) == sizeof(record.mBounds), "the box");
    std::memcpy(&record.mBounds, &bounds, sizeof bounds);
    if (cache) reinterpret_cast<cIGZUnknown *>(cache)->Release();
    return pass;
}

} // namespace nGZSceneGraph
