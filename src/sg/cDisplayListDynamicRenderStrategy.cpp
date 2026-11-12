// nTSSG::cDisplayListDynamicRenderStrategy (lanes 261108g, 261108k). See src/sg/cDisplayListDynamicRenderStrategy.h.
#include "sg/cDisplayListDynamicRenderStrategy.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/GraphicServices.h" // GetCurrentDeviceCached, GetPixelFormatsManagerCached
#include "gz/cBrushes.h"        // cIColorBrush
#include "gz/cDeviceDX.h"
#include "gz/cDeviceEnum.h"
#include "gz/cDeviceStateDX.h"
#include "gz/cGZAny.h"
#include "gz/cGZRect.h" // GZRectIntersect
#include "gz/cGZWinMgrW32.h" // cIGZWinMgr
#include "gz/cIGZCOM.h"
#include "gz/cIGZPropertySet.h" // GZServiceString
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZPropertySetString.h"
#include "ifaces/cIPixelFormatDepthStencil.h"
#include "ifaces/cIPixelFormatsManager.h"
#include "gz/cDraw2D.h"          // cIDraw2D
#include "rz/Singleton_20ddfb.h" // GetServiceObject, ScopedRef
#include "rz/cRZVariant.h"
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject
#include "compgeom/Frames.h"  // AsFrame (ToMatrix4)
#include "compgeom/Matrix4.h" // Matrix4Multiply
#include "sg/SGUtilities.h"      // ProjectBox
#include "sg/cDisplayList.h"     // Pass
#include "sg/cIGeometryRender.h" // GeometrySubset
#include "sg/cIViewerRefNode.h"
#include "sg/cTSRenderSurface.h"

#include <algorithm>
#include <bit>
#include <climits>
#include <cstring>
#include <new>

namespace nTSSG {

namespace {

using nGZGraphic4::cIDeviceDX;
using nGZGraphic4::cIDeviceEnum;
using nGZGraphic4::cIPixelFormat;

constexpr uint32_t kStrategyIid = 0x0bf077fd; // nGZSceneGraph::cIDisplayListRenderStrategy

// The COM classes the constructor asks for, with their interfaces.
constexpr uint32_t kDirector10Class = 0xdc012582, kDirector10Iid = 0x9c012573;
constexpr uint32_t kDirector14Class = 0x9c1d3c77, kDirector14Iid = 0xfc1d3c6b;

// The settings service (class, interface) and the scene-graph system's cache (0x211da3's cell and ids).
constexpr uint32_t kSettingsId = 0x0ac1887e, kSettingsIid = 0x4bb0d269;
constexpr uint32_t kSgSystemCell = 0x10cc458, kSgSystemId = 0x9a353ce8, kSgSystemIid = 0xfa353cdb;

// The render states' ids and the 1.0 their right / bottom (width / height) words hold.
constexpr uint8_t kScissorId = 0x20, kViewportId = 1;
constexpr uint32_t kOneBits = 0x3f800000u;
constexpr uint32_t kScissorVTable = 0xd9ff60;

// antialiasingSupport's values: which surfaces the antialiasing quality goes to.
constexpr uint32_t kSupportBackbuffer = 1;  // the back buffer's multisample count
constexpr uint32_t kSupportTargets = 2;     // the dirty-rect targets' (2 and 3)
constexpr uint32_t kSupportTargetsTurbo = 3;
constexpr uint32_t kSupportSupersample = 4; // a debugRenderRes default of 2
constexpr uint32_t kDynamicStrategyDirty = 2; // dynamicRenderStrategy: the dirty-rect surfaces

// The censor texture's size against the view's.
constexpr float kCensorScale = 0.1f;

// The turbo rect test: depth-stencil format D24S8 (32 bits, 24 depth, 8 stencil), colour ARGB 8888, usage 0xb (depth),
// surfaces of 16 x 16.
constexpr uint32_t kDepthUsage = 0xb;
constexpr uint32_t kTurboSide = 16;

cIGZCOM *Com() { return static_cast<cIGZCOM *>(GetDirectorFrameworkObject()); }

// One settings read: the key's value, `fallback` when the service leaves it.
uint32_t ReadUint32(void *settings, const char *name, uint32_t fallback)
{
    GZServiceString key(name);
    gz::AnyUint32 value(fallback);
    As<ifaces::cIGZPropertySetString>(settings)->GetPropertyValue(&key, static_cast<cIGZAny *>(&value));
    return value.mValue;
}
uint8_t ReadBool(void *settings, const char *name, uint8_t fallback = 0)
{
    GZServiceString key(name);
    gz::AnyBoolean value(fallback);
    As<ifaces::cIGZPropertySetString>(settings)->GetPropertyValue(&key, static_cast<cIGZAny *>(&value));
    return value.mValue;
}
float ReadFloat(void *settings, const char *name, float fallback)
{
    GZServiceString key(name);
    gz::AnyFloat32 value(fallback);
    As<ifaces::cIGZPropertySetString>(settings)->GetPropertyValue(&key, static_cast<cIGZAny *>(&value));
    return value.mValue;
}

// The settings service, counted in `holder` (none without the framework's service object).
void AcquireSettings(ScopedRef &holder)
{
    holder.mObject = nullptr;
    if (void *const framework = GetServiceObject())
        As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kSettingsId, kSettingsIid, &holder.mObject);
}

// 0x211da3's logic: the scene-graph system, cached in its game cell (asked for once, not kept counted).
void *SgSystem()
{
    void **const cell = GameAddress<void *>(kSgSystemCell);
    if (!*cell) {
        ScopedRef held{nullptr};
        if (void *const framework = GetServiceObject())
            As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kSgSystemId, kSgSystemIid, &held.mObject);
        *cell = held.mObject;
        held.ReleaseObject();
    }
    return *cell;
}

// Whether the pool and ocean reflections are in use: the scene-graph system's slot 36 (0x251bf0, the pool manager at
// +0x74), the pool manager's slot 9 (0x30c0d0, its byte at +0x42); both take no arguments (ret 0), both unchecked.
bool WaterReflections() { return RawSlot<9, bool>(RawSlot<36, void *>(SgSystem())); }

// The quality the settings give each kind of surface: the back buffer's (a), the dirty-rect targets' (b) and the
// supersampling one's (c); one of them at most is non-zero.
struct Antialiasing {
    uint32_t backbuffer, targets, supersample;
    Antialiasing(uint32_t support, uint32_t quality)
        : backbuffer(support == kSupportBackbuffer ? quality : 0),
          targets(support == kSupportTargets || support == kSupportTargetsTurbo ? quality : 0),
          supersample(support == kSupportSupersample ? quality : 0)
    {
    }
};

// The game's float-to-unsigned conversion (through a 64-bit integer, truncating).
uint32_t ToUnsigned(float value) { return static_cast<uint32_t>(static_cast<int64_t>(value)); }

void SizeAndScale(cTSRenderTextureSurface *surface, cIDeviceDX *device, uint32_t width, uint32_t height, float scale)
{
    surface->Configure(device, width, height);
    surface->SetScale(scale);
}

template <class Surface> void CreateIf(Surface *surface, bool create)
{
    if (create) surface->Create();
    else surface->Destroy();
}

// ---- the draw path's constants and helpers (lane 261108k) ----

// The settings service, counted for a scope (the draw steps release it on the way out, also when something throws).
class HeldSettings {
public:
    HeldSettings() { AcquireSettings(mRef); }
    ~HeldSettings() { mRef.ReleaseObject(); }
    HeldSettings(const HeldSettings &) = delete;
    HeldSettings &operator=(const HeldSettings &) = delete;
    void *Get() const { return mRef.mObject; }

private:
    ScopedRef mRef{nullptr};
};

// The frame state's class (COM, asked with the unknown interface) when the viewer has none yet.
constexpr uint32_t kStateClass = 0x6d548978, kUnknownIid = 1;

// Pass types (Pass::mRenderType): the reflection passes (screen 2 and 3, pool 7, ocean 9); 3 has its own drawing path.
constexpr uint32_t kTypeMain = 0, kTypeScreen = 2, kTypeScreenOwn = 3, kTypePool = 7, kTypeOcean = 9;
bool IsReflection(uint32_t type)
{
    return type == kTypeScreen || type == kTypeScreenOwn || type == kTypePool || type == kTypeOcean;
}

// The settings' defaults Prepare reads with.
constexpr uint32_t kDefaultMergeThreshold = 0xe1;
// mClearMode's values.
constexpr uint32_t kClearSplit = 0, kClearWhole = 1, kClearNoTurbo = 2;
// dirtyRectsDebug from which SetUpTarget stops clearing the dirty-rect frames.
constexpr uint32_t kDebugModeClears = 3;
// debugRenderBuffer's values (0 none): the target, the turbo target, the post-effects texture, the screen reflection,
// the censor texture.
constexpr uint32_t kShowTarget = 1, kShowSecond = 2, kShowPostEffects = 3, kShowReflection = 4, kShowCensor = 5;

// The device's clear flags (D3DCLEAR_TARGET; D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL) and the clear info's default colour.
constexpr uint32_t kClearTarget = 1, kClearDepthStencil = 6;
constexpr float kClearColor[4] = {1.0f, 0.0f, 0.0f, 0.0f};
constexpr uint8_t kDepthRangeId = 2;

// The empty rectangle the frame's bounds start from (a game global).
constexpr uint32_t kEmptyRectCell = 0x10091b4;

// The main window's children walk Clear's callback goes through (cGZWin slot 37, with this window id filter).
constexpr uint32_t kWinMgrId = 0xa417445e;
constexpr uint32_t kOpaqueUIWindowId = 0x22ba0121;

template <class State> State Command(uint8_t id, const void *vtable)
{
    State state{};
    state.mVTable = vtable;
    state.mId = id;
    state.mSize = static_cast<uint16_t>(sizeof(State));
    return state;
}

// Through the interface: the pass's device state may be any implementation.
void Apply(nGZGraphic4::cIDeviceState *state, const void *command)
{
    state->ApplyStateCommand(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(command)));
}

// A colour channel as a byte: adding 1.5 * 2^15 leaves round(value * 256) in the mantissa's low bits (their unit is
// 2^-8 there), taken out by subtracting 49152.0's bits; then clamped to 0..255 without branches.
uint32_t ColorByte(float value)
{
    const float biased = value + 49152.0f;
    uint32_t bits;
    std::memcpy(&bits, &biased, sizeof bits);
    const int32_t v = static_cast<int32_t>(bits + 0xb8c00000u);
    const int32_t over = static_cast<int32_t>(0xffu - static_cast<uint32_t>(v)) >> 31;
    return static_cast<uint8_t>((~(v >> 31) & v) | over);
}

void CopyRect(int32_t *to, const int32_t *from) { std::memcpy(to, from, 4 * sizeof(int32_t)); }

// The frame state, counted for Render's length.
struct HeldState {
    DynamicStrategyState *mState;
    ~HeldState()
    {
        if (mState) mState->Unknown()->Release();
    }
};

} // namespace

// ---- members ----------------------------------------------------------------------------------------------------------

DynamicStrategyHelper::DynamicStrategyHelper()
{
    mMap10 = new Map(kMapBuckets);
    mMap14 = new Map(kMapBuckets);
}

DynamicStrategyHelper::~DynamicStrategyHelper()
{
    delete mMap10;
    delete mMap14;
}

void DynamicStrategyHelper::FrameStep(uint32_t frame)
{
    if (mFrame == frame) return;
    std::swap(mMap10, mMap14);
    mMap14->clear();
    if (mFrame + 1 != frame) Reset();
    mFrame = frame;
}

void DynamicStrategyHelper::Reset()
{
    mMap14->clear();
    mMap10->clear();
    mByte24 = 0;
}

void DynamicStrategyTable::FrameStep(uint32_t frame)
{
    if (mFrame == frame) return;
    if (mFrame + 1 != frame) Reset();
    mFrame = frame;
    mVisible.swap(mLastVisible);
}

void DynamicStrategyTable::Reset()
{
    mRecords.clear();
    mVisible.clear();
    mLastVisible.clear();
    mFilled = 0;
}

DynamicStrategyState::DynamicStrategyState() : mVTable(GameAddress<void>(kVTable))
{
    mTable = new (std::nothrow) DynamicStrategyTable;
}

DynamicStrategyState::~DynamicStrategyState()
{
    mVTable = GameAddress<void>(kVTable);
    delete mTable;
}

DynamicStrategyState *DynamicStrategyState::Create() { return new (std::nothrow) DynamicStrategyState; }

// Each element's float bits are mapped to a signed order (a negative one's magnitude bits flipped, so that one step is
// one representable float either side of zero) and the two compared.
bool MatricesDiffer(const uint32_t *a, const uint32_t *b)
{
    for (int i = 0; i < 16; ++i) {
        const uint32_t signA = a[i] >> 31, signB = b[i] >> 31;
        const uint32_t keyA = a[i] ^ ((0u - signA) >> 1), keyB = b[i] ^ ((0u - signB) >> 1);
        const int32_t distance = static_cast<int32_t>(keyA - keyB - signB + signA);
        if ((distance ^ (distance >> 26)) & ~0x1f) return true;
    }
    return false;
}

DynamicStrategyTrees::DynamicStrategyTrees()
{
    mXTree.Assign(new (std::nothrow) cIntervalTree(false));
    mYTree.Assign(new (std::nothrow) cIntervalTree(false));
}

void DynamicStrategyTrees::Build(const std::list<DynamicWords16> *rects)
{
    mXTree->Clear();
    mYTree->Clear();
    mNodeCount = 0;
    mNodes.resize(rects->size() * 2);
    int32_t number = 0;
    for (const DynamicWords16 &rect : *rects) {
        ++number;
        Node *const x = &mNodes[mNodeCount++];
        *x = Node(static_cast<int32_t>(rect.w[0]), static_cast<int32_t>(rect.w[2]), number);
        mXTree->Insert(x);
        Node *const y = &mNodes[mNodeCount++];
        *y = Node(static_cast<int32_t>(rect.w[1]), static_cast<int32_t>(rect.w[3]), number);
        mYTree->Insert(y);
    }
}

bool DynamicStrategyTrees::Overlaps(const int32_t *rect)
{
    mXHits.clear();
    mYHits.clear();
    mXTree->Query(mXTree->Root(), rect[0], rect[2], &mXHits);
    if (mXHits.empty()) return false;
    mYTree->Query(mYTree->Root(), rect[1], rect[3], &mYHits);
    if (mYHits.empty()) return false;
    return HitsShareRectangle(mXHits.begin(), mXHits.end(), mYHits.begin(), mYHits.end());
}

bool HitsShareRectangle(cIntervalTree::NodeSet::const_iterator first1, cIntervalTree::NodeSet::const_iterator last1,
                        cIntervalTree::NodeSet::const_iterator first2, cIntervalTree::NodeSet::const_iterator last2)
{
    while (first1 != last1 && first2 != last2) {
        if ((*first1)->mData < (*first2)->mData)
            ++first1;
        else if ((*first2)->mData < (*first1)->mData)
            ++first2;
        else
            return true;
    }
    return false;
}

DynamicScissor::DynamicScissor()
{
    mVTable = GameAddress<void>(kScissorVTable);
    mId = kScissorId;
    mSize = sizeof(nGZGraphic4::cScissorTestState);
    mLeft = 0;
    mTop = 0;
    mRight = static_cast<int32_t>(kOneBits);
    mBottom = static_cast<int32_t>(kOneBits);
    mEnabled = 0;
}

DynamicViewport::DynamicViewport()
{
    mVTable = VTable();
    mId = kViewportId;
    mSize = sizeof(nGZGraphic4::cViewportState);
    mX = 0;
    mY = 0;
    mWidth = kOneBits;
    mHeight = kOneBits;
}

// ---- construction -----------------------------------------------------------------------------------------------------

// 0x36e400: the members (no surfaces, the handles -1, empty containers, the two states over the whole view), then the
// two COM objects (the first one's slot 5 called with 1, unchecked), the helper and a new censor strategy (counted).
cDisplayListDynamicRenderStrategy::cDisplayListDynamicRenderStrategy()
{
    cIGZCOM *com = Com();
    mDirector10.Reset();
    com->GetClassObject(kDirector10Class, kDirector10Iid, reinterpret_cast<void **>(mDirector10.Address()));
    RawSlot<5>(mDirector10.Get(), 1u); // receiver: nGZGraphic4::cColorBrush (Resize 0x153fe0, ret 4)
    com = Com();
    mDirector14.Reset();
    com->GetClassObject(kDirector14Class, kDirector14Iid, reinterpret_cast<void **>(mDirector14.Address()));
    mHelper = new (std::nothrow) DynamicStrategyHelper;
    mCensor.Assign(new (std::nothrow) cDisplayListCensorRenderStrategy);
}

cDisplayListDynamicRenderStrategy::~cDisplayListDynamicRenderStrategy()
{
    ReleaseSurfaceResources();
    delete mDirtyTarget1;
    delete mDirtyTarget2;
    delete mDirtyTexture1;
    delete mDirtyTexture2;
    delete mScreenReflection;
    delete mPoolReflection;
    delete mOceanReflection;
    delete mCensorTexture;
    delete mPostEffects;
    delete mTexture130;
    delete mBackbuffer;
    delete mHelper;
    mCensor.Reset();
}

void cDisplayListDynamicRenderStrategy::ReleaseSurfaceResources()
{
    mDirtyTarget1->ReleaseSurfaces();
    mDirtyTarget2->ReleaseSurfaces();
    mDirtyTexture1->ResetTexture();
    mDirtyTexture2->ResetTexture();
    mCensorTexture->ResetTexture();
    mScreenReflection->ResetTexture();
    mPoolReflection->ResetTexture();
    mOceanReflection->ResetTexture();
    mPostEffects->ResetTexture();
}

// ---- the COM part -----------------------------------------------------------------------------------------------------

bool cDisplayListDynamicRenderStrategy::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == kStrategyIid || iid == cITSDisplayListRenderStrategy::kIid) {
        cITSDisplayListRenderStrategy *const face = this;
        *out = face;
        AddRef();
        return true;
    }
    return rz::cRZUnknown::QueryInterface(iid, out);
}

// 0x374dd0: the viewer's clear info (colour 1, 0, 0, 0 unless it says otherwise) gives the flags; the frame state may
// change them: a type 3 reflection clears only the colour, only for the screen-reflection debug buffer and only while
// the frame is drawn whole (depth 1, stencil 0); the other reflections clear depth and stencil (to 0 on dirty-rect
// frames, with the colour too for that debug buffer otherwise); the main view's dirty-rect frames (frames 2 on, below
// dirtyRectsDebug 3) clear depth and stencil to 0, nothing with the turbo rect. The colour also goes to the brush (bytes
// A R G B from the clear colour's four floats). Then the surfaces: a reflection draws into its texture throughout; a
// view with a handle texture into the last texture surface (that texture made external), ending in the back buffer
// only with post-processing; the dirty-rect surfaces' mode into the current surface (the turbo partner for the second
// target on dirty-rect frames) ending in the back buffer; anything else into the current surface when the dirty-rect
// drawing may run and there is one, else the back buffer. When the target is valid: applied (forced), the projection,
// the pass's viewport and depth range set, and the clear when there are flags.
void cDisplayListDynamicRenderStrategy::SetUpTarget(nGZSceneGraph::Pass *pass)
{
    nGZSceneGraph::cViewerClearInfo clear{};
    std::memcpy(clear.mColor, kClearColor, sizeof(kClearColor));
    pass->mViewer->GetClearInfo(&clear);
    uint32_t flags = ((clear.mFlags & 2) ? kClearDepthStencil : 0) | (clear.mFlags & kClearTarget);
    const uint32_t type = pass->mRenderType;
    if (type == kTypeScreenOwn) {
        flags = mState->mFrames < 2 && mDebugBuffer == kShowReflection ? kClearTarget : 0;
        clear.mDepth = 1.0f;
        clear.mStencil = 0;
    } else if (type == kTypeScreen || type == kTypePool || type == kTypeOcean) {
        if (mState->mFrames >= 2) {
            flags = kClearDepthStencil;
            clear.mDepth = 0.0f;
            clear.mStencil = 0;
        } else {
            flags = mDebugBuffer == kShowReflection ? kClearDepthStencil | kClearTarget : kClearDepthStencil;
        }
    } else if (mDebugMode < kDebugModeClears && mState->mFrames >= 2) {
        clear.mDepth = 0.0f;
        clear.mStencil = 0;
        flags = mTurbo ? 0 : kClearDepthStencil;
    }

    const uint32_t packed = ColorByte(clear.mColor[0]) << 24 | ColorByte(clear.mColor[1]) << 16 |
                            ColorByte(clear.mColor[2]) << 8 | ColorByte(clear.mColor[3]);
    static_cast<nGZGraphic4::cIColorBrush *>(mDirector10.Get())->SetColor(0, &packed);

    cITSRenderSurface *final;
    if (IsReflection(type)) {
        cITSRenderSurface *const texture = type == kTypePool    ? mPoolReflection
                                         : type == kTypeOcean ? mOceanReflection
                                                              : static_cast<cITSRenderSurface *>(mScreenReflection);
        mTarget = mTargetSecond = mTargetPost = final = texture;
    } else if (pass->mHandleTexture) {
        mTexture130->SetExternal(reinterpret_cast<nGZSceneGraph::cIRenderTargetTexture *>(pass->mHandleTexture));
        mTarget = mTargetSecond = mTargetPost = mTexture130;
        final = mPostProcessing ? static_cast<cITSRenderSurface *>(mBackbuffer) : mTexture130;
    } else if (mState->mMode == DynamicStrategyState::kDirtySurfaces) {
        mTarget = mCurrent;
        mTargetSecond = mState->mFrames != 0 && mTurbo ? mCurrentSecond : mCurrent;
        mTargetPost = mPostProcessing ? mPostEffects : nullptr;
        final = mBackbuffer;
    } else {
        cITSRenderSurface *const target = mDirtyRects && mCurrent ? mCurrent : mBackbuffer;
        mTarget = mTargetSecond = target;
        mTargetPost = mPostProcessing ? mPostEffects : nullptr;
        final = mBackbuffer;
    }
    mFinal = final;

    if (!mTarget->IsValid()) return;
    mTarget->Apply(pass, true);
    nGZGraphic4::cIDeviceState *const state = pass->mDeviceState;
    state->SetProjection(reinterpret_cast<const D3DMATRIX *>(pass->mMatrices[0]));
    nGZGraphic4::cViewportState viewport =
        Command<nGZGraphic4::cViewportState>(kViewportId, nGZGraphic4::cViewportState::VTable());
    std::memcpy(&viewport.mX, pass->mViewportMin, sizeof(pass->mViewportMin));
    std::memcpy(&viewport.mWidth, pass->mViewportMax, sizeof(pass->mViewportMax));
    Apply(state, &viewport);
    nGZGraphic4::cDepthRangeState depth =
        Command<nGZGraphic4::cDepthRangeState>(kDepthRangeId, nGZGraphic4::cDepthRangeState::VTable());
    depth.mMin = pass->mDepthRange[0];
    depth.mMax = pass->mDepthRange[1];
    Apply(state, &depth);
    if (flags) pass->mDevice->Clear(flags, clear.mColor, clear.mDepth, clear.mStencil);
}

// ---- the strategy interface -------------------------------------------------------------------------------------------

void cDisplayListDynamicRenderStrategy::BeginPass(nGZSceneGraph::Pass *pass)
{
    static_cast<cITSDisplayListRenderStrategy *>(mCensor.Get())->BeginPass(pass);
}

// 0x374930: the viewer's frame state (made through COM and handed to the viewer the first time), counted while the pass
// is drawn and held at mState; whether the projection moved since it last drew (then the new one kept); Prepare, then
// Draw. Anything thrown on the way leaves the frame state's count of dirty-rect frames at 0 and the scissor test off.
void cDisplayListDynamicRenderStrategy::Render(nGZSceneGraph::Pass *pass, nGZSceneGraph::DrawItem *items, uint32_t count)
{
    nGZSceneGraph::cIViewerRefNodeBase *const viewer = pass->mViewer;
    cIGZUnknown *made = viewer->Slot116();
    if (made) {
        made->AddRef();
    } else {
        Com()->GetClassObject(kStateClass, kUnknownIid, reinterpret_cast<void **>(&made));
        viewer->Slot99(made);
    }
    made->AddRef(); // unchecked, as the original
    HeldState held{reinterpret_cast<DynamicStrategyState *>(made)};
    if (made) made->Release();

    DynamicStrategyState *const state = held.mState;
    mState = state;
    const float *const projection = pass->mMatrices[4];
    const bool changed = MatricesDiffer(reinterpret_cast<const uint32_t *>(projection), state->mProjection);
    if (changed) std::memcpy(state->mProjection, projection, sizeof(state->mProjection));
    try {
        Prepare(pass, changed);
        Draw(pass, items, count);
    } catch (...) {
        if (mState) mState->mFrames = 0;
        const uint8_t scissoring = mUseScissoring;
        mState = nullptr;
        if (scissoring) {
            mUseScissoring = 0;
            DisableScissor(pass);
        }
        throw;
    }
    mState = nullptr;
}

// 0x373920: a new frame: the censor strategy's Clear; a pending invalidation older than one frame dropped; the frame
// counted; debugRenderBuffer and renderOpaqueUI read, the latter handed to the main window's children.
void cDisplayListDynamicRenderStrategy::Clear()
{
    static_cast<cITSDisplayListRenderStrategy *>(mCensor.Get())->Clear();
    if (mInvalidated > 1) mInvalidated = 0;
    ++mFrame;
    const HeldSettings settings;
    mDebugBuffer = ReadUint32(settings.Get(), "debugRenderBuffer", 0);
    mRenderOpaqueUI = ReadBool(settings.Get(), "renderOpaqueUI", 0);
    uint8_t opaque;
    MarkOpaqueUI(&opaque, mRenderOpaqueUI);
}

// ---- the draw steps (lane 261108k) ------------------------------------------------------------------------------------

// 0x371b10: the settings into the members (dirtyRectsDebug, dirtyRectsDisableIntervalTrees, dirtyRectsMergeThreshold,
// useScissoring, useTurboRect, enablePostProcessing; useSplitRect, mirrorDirtyRects and dynamicRenderStrategy used
// here). The mode: a reflection's is the dirty-rect surfaces' when mirrorDirtyRects is on, a view with a handle texture
// draws whole, the main view (type 0) takes dynamicRenderStrategy (1 or 2, else whole) and is the only one the dirty-rect
// drawing may run for; any other type draws whole. Where it may run, a multisampled back buffer makes it whole,
// post-processing needs a created and valid post-effects texture, and the dirty-rect surfaces' mode needs a current
// surface (and the turbo rect its partner); elsewhere no turbo rect, no debug drawing and post-processing only with a
// handle texture. Then the table and helper step to the frame, the debug flag goes to both, and the dirty-rect frames
// are counted: one more when the mode is a dirty-rect one, the same as last time, the view still and nothing
// invalidated (and the table filled; else the count restarts at 1); 0 otherwise. The turbo rect and scissoring only run
// on counted frames of the dirty-rect surfaces' mode. Last the redraw flag, the pixel rectangle and the clear mode.
void cDisplayListDynamicRenderStrategy::Prepare(nGZSceneGraph::Pass *pass, bool viewChanged)
{
    const HeldSettings settings;
    void *const values = settings.Get();
    mDebugMode = ReadUint32(values, "dirtyRectsDebug", 0);
    mDisableIntervalTrees = ReadBool(values, "dirtyRectsDisableIntervalTrees", 0);
    mMergeThreshold = ReadUint32(values, "dirtyRectsMergeThreshold", kDefaultMergeThreshold);
    mUseScissoring = ReadBool(values, "useScissoring", 1);
    mByte79 = 0;
    mTurbo = ReadBool(values, "useTurboRect", 0);
    const uint8_t split = ReadBool(values, "useSplitRect", 1);
    mPostProcessing = ReadBool(values, "enablePostProcessing", 0);
    const uint8_t mirror = ReadBool(values, "mirrorDirtyRects", 1);
    const uint32_t strategy = ReadUint32(values, "dynamicRenderStrategy", 0);

    DynamicStrategyState *const state = mState;
    mDirtyRects = 0;
    const uint32_t type = pass->mRenderType;
    if (IsReflection(type)) {
        state->mMode = mirror ? DynamicStrategyState::kDirtySurfaces : DynamicStrategyState::kFull;
    } else if (pass->mHandleTexture) {
        state->mMode = DynamicStrategyState::kFull;
    } else {
        const bool known = strategy == DynamicStrategyState::kDirtyRects || strategy == DynamicStrategyState::kDirtySurfaces;
        state->mMode = known ? strategy : DynamicStrategyState::kFull;
        if (type != kTypeMain) state->mMode = DynamicStrategyState::kFull;
        else mDirtyRects = 1;
    }

    if (mDirtyRects) {
        // Through the interface (as the original's virtual calls; the classes are final, which would bind them).
        cITSRenderSurface *const backbuffer = mBackbuffer, *const post = mPostEffects;
        if (backbuffer->IsMultisampled()) state->mMode = DynamicStrategyState::kFull;
        if (mPostProcessing && (!post->IsCreated() || !post->IsValid())) mPostProcessing = 0;
        if (state->mMode == DynamicStrategyState::kDirtySurfaces) {
            if (!mCurrent) state->mMode = DynamicStrategyState::kFull;
            if (!mCurrentSecond) mTurbo = 0;
        }
    } else {
        mTurbo = 0;
        mPostProcessing = pass->mHandleTexture ? mPostProcessing : 0;
        mDebugMode = 0;
    }

    state->mTable->FrameStep(mFrame);
    mHelper->FrameStep(mFrame);
    const uint8_t debug = state->mMode == DynamicStrategyState::kDirtyRects && mDebugMode >= 1;
    state->mTable->mDebug = debug;
    mHelper->mDebug = debug;

    const uint32_t mode = state->mMode, lastMode = state->mLastMode;
    state->mLastMode = mode;
    uint32_t invalidated = mInvalidated;
    if (invalidated) mInvalidated = ++invalidated;
    const bool dirtyMode = mode == DynamicStrategyState::kDirtyRects || mode == DynamicStrategyState::kDirtySurfaces;
    if (mode == lastMode && dirtyMode && !viewChanged)
        state->mFrames = invalidated == 0 && state->mTable->mFilled ? state->mFrames + 1 : 1;
    else
        state->mFrames = 0;

    uint8_t turbo;
    if (state->mFrames != 0 && state->mMode == DynamicStrategyState::kDirtySurfaces) {
        turbo = mTurbo;
    } else {
        mTurbo = 0;
        turbo = 0;
        mUseScissoring = 0;
    }
    mRedraw = IsReflection(type) ? !viewChanged : !viewChanged && !pass->mHandleTexture;
    CopyRect(mPixelRect, pass->mPixelRect);
    mClearMode = turbo ? (split ? kClearSplit : kClearWhole) : kClearNoTurbo;
}

// 0x373e20: the frame's two word lists and both rectangle lists emptied (their bounds the empty rectangle), the items
// drawn by the type 3 reflection's path or the general one, then Finish.
void cDisplayListDynamicRenderStrategy::Draw(nGZSceneGraph::Pass *pass, nGZSceneGraph::DrawItem *items, uint32_t count)
{
    mItems.clear();
    mItemRecords.clear();
    mRects.clear();
    CopyRect(mBounds, GameAddress<int32_t>(kEmptyRectCell));
    mTurboRects.clear();
    CopyRect(mTurboBounds, GameAddress<int32_t>(kEmptyRectCell));
    if (pass->mRenderType == kTypeScreenOwn) DrawOwnPath(pass, items, count);
    else DrawPath(pass, items, count);
    Finish(pass);
}

// 0x376460: without the dirty-rect drawing, a reflection's rectangles, bounds, pixel rectangle and frame state are kept
// for the screen-reflection debug buffer, and nothing else. Otherwise the debug buffer (debugRenderBuffer) is copied
// into the final surface when created (stretched with a reduced resolution; the censor texture never), the target ones
// and the reflection with the overlay (DrawDebugOverlay) and the final surface applied after; the final surface is
// applied in any case. Then the outlines (DrawOutlines) of this frame's rectangles (the turbo rect's when it ran and the
// buffer is none or the turbo target; the kept reflection's for the reflection buffer; none for the post-effects or
// censor buffers), without the helper in the dirty-rect surfaces' mode; and the kept reflection dropped.
void cDisplayListDynamicRenderStrategy::Finish(nGZSceneGraph::Pass *pass)
{
    if (!mDirtyRects) {
        if (IsReflection(pass->mRenderType)) {
            mLastState = mState;
            mLastRects = mRects;
            CopyRect(mLastPixelRect, mPixelRect);
            CopyRect(mLastBounds, mBounds);
        }
        return;
    }

    const auto show = [this, pass](cITSRenderSurface *surface) {
        if (!surface->IsCreated()) return;
        mFinal->CopyColor(surface, mReducedResolution != 0);
        DrawDebugOverlay(pass, surface);
        mFinal->Apply(pass, false);
    };
    // Through the interface (the classes are final, which would bind the calls).
    cITSRenderSurface *const post = mPostEffects, *const censor = mCensorTexture;
    switch (mDebugBuffer) {
    case kShowTarget: show(mTarget); break;
    case kShowSecond: show(mTargetSecond); break;
    case kShowPostEffects:
        if (post->IsCreated()) mFinal->CopyColor(post, mReducedResolution != 0);
        break;
    case kShowReflection: show(mScreenReflection); break;
    case kShowCensor:
        if (censor->IsCreated()) mFinal->CopyColor(censor, false);
        break;
    default: break;
    }
    mFinal->Apply(pass, false);

    DynamicStrategyState *state = mState;
    DynamicStrategyTable *table = state->mTable;
    DynamicStrategyHelper *helper = state->mMode == DynamicStrategyState::kDirtySurfaces ? nullptr : mHelper;
    int32_t bounds[4], pixels[4];
    CopyRect(bounds, mBounds);
    CopyRect(pixels, mPixelRect);
    std::list<DynamicWords16> *rects = &mRects;
    bool second = false;
    bool outlines = true;
    switch (mDebugBuffer) {
    case 0:
    case kShowSecond:
        if (mTurbo) {
            CopyRect(bounds, mTurboBounds);
            rects = &mTurboRects;
            second = true;
        }
        break;
    case kShowPostEffects:
    case kShowCensor: outlines = false; break;
    case kShowReflection:
        state = mLastState;
        table = state ? state->mTable : nullptr;
        CopyRect(bounds, mLastBounds);
        helper = nullptr;
        rects = &mLastRects;
        CopyRect(pixels, mLastPixelRect);
        break;
    default: break;
    }
    if (outlines && state) DrawOutlines(state, table, helper, rects, bounds, pixels, second);
    mLastState = nullptr;
    mLastRects.clear();
}

// 0x3714b0: the kept scissor state's enable byte cleared and the state applied.
void cDisplayListDynamicRenderStrategy::DisableScissor(nGZSceneGraph::Pass *pass)
{
    mScissor.mEnabled = 0;
    Apply(pass->mDeviceState, &mScissor);
}

// ---- the drawing paths' per-frame helpers (row TS1e) ----

namespace {
constexpr uint32_t kUnboundedRectCell = 0x10091cc; // ComputeBounds' start: a rectangle every real one widens

// 0x373520 (src/gz/cGZRect.h, ours and hooked): `*out` = a & b when they overlap (true), else all zero (false);
// `out` may be `a`.
bool IntersectRect(int32_t *out, const int32_t *a, const int32_t *b)
{
    return GZRectIntersect(reinterpret_cast<cGZRect *>(out), reinterpret_cast<const cGZRect *>(a),
                           reinterpret_cast<const cGZRect *>(b));
}

// A uint32 pass size as a float, through a double as the original converts it (exact, so rounded once).
float SizeAsFloat(uint32_t value) { return static_cast<float>(static_cast<double>(value)); }

void StoreFloat(int32_t *word, float value) { std::memcpy(word, &value, sizeof value); }
} // namespace

// 0x3724a0: each table record's +0x1d flag set for this frame. Without `which` the flags are first reset (cleared
// with `turbo`, else copied from the drawn flag). With `test` and any rectangles, each drawn record is then tested
// against the frame's rectangles: with `turbo` only the records of the same turbo-rect side, clipped to that side's
// bounds first (cleared when outside them); the test is the interval trees' (built here) unless
// dirtyRectsDisableIntervalTrees, then a plain walk with strict edges. `pass` is not read. The callers pass 0 or 1
// for `which`.
void cDisplayListDynamicRenderStrategy::MarkRecords(const std::list<DynamicWords16> *rects, nGZSceneGraph::Pass *pass,
                                                    bool turbo, bool test, uint8_t which)
{
    (void)pass;
    // The record count is the +0x30 vector's, the records the +0x3c vector's pointers.
    const uint32_t count = static_cast<uint32_t>(mItems.size());
    DynamicRectRecord *const *const records = mItemRecords.data();
    if (!which) {
        for (uint32_t i = 0; i < count; ++i) records[i]->mFlag1d = turbo ? 0 : records[i]->mDrawn;
    }
    if (!test || rects->empty()) return;
    if (!mDisableIntervalTrees) mTrees.Build(rects);
    const int32_t *const bounds = which ? mTurboBounds : mBounds;
    for (uint32_t i = 0; i < count; ++i) {
        DynamicRectRecord *const record = records[i];
        int32_t rect[4];
        std::memcpy(rect, record->mRect, sizeof rect);
        if (!record->mDrawn) continue;
        if (turbo) {
            if (record->mSecond != which) continue;
            if (!IntersectRect(rect, rect, bounds)) {
                record->mFlag1d = 0;
                continue;
            }
        }
        bool hit = false;
        if (!mDisableIntervalTrees) {
            hit = mTrees.Overlaps(rect);
        } else {
            for (const DynamicWords16 &r : *rects) {
                const int32_t *const w = reinterpret_cast<const int32_t *>(r.w);
                if (rect[2] > w[0] && rect[0] < w[2] && rect[3] > w[1] && rect[1] < w[3]) {
                    hit = true;
                    break;
                }
            }
        }
        record->mFlag1d = hit;
    }
}

// 0x370eb0: the side's bounds (`which` 0 mBounds, 1 mTurboBounds) as the union of the non-empty rectangles, clipped to
// the pass's pixels; the empty rectangle when there are none or the union misses the pixels.
void cDisplayListDynamicRenderStrategy::ComputeBounds(const std::list<DynamicWords16> *rects, uint8_t which)
{
    int32_t *const bounds = which ? mTurboBounds : mBounds;
    CopyRect(bounds, GameAddress<int32_t>(kUnboundedRectCell));
    for (const DynamicWords16 &r : *rects) {
        const int32_t *const w = reinterpret_cast<const int32_t *>(r.w);
        if (w[2] <= w[0] || w[3] <= w[1]) continue;
        bounds[0] = std::min(bounds[0], w[0]);
        bounds[1] = std::min(bounds[1], w[1]);
        bounds[2] = std::max(bounds[2], w[2]);
        bounds[3] = std::max(bounds[3], w[3]);
    }
    cGZRect *const out = reinterpret_cast<cGZRect *>(bounds);
    if (bounds[0] <= bounds[2] &&
        nGZGraphic4::cDraw2D::Intersect(out, nullptr, out, reinterpret_cast<const cGZRect *>(mPixelRect)))
        return;
    CopyRect(bounds, GameAddress<int32_t>(kEmptyRectCell));
}

// 0x3719a0: with useScissoring, the side's bounds non-empty and dirtyRectsDebug not 3, the kept scissor state set to
// the bounds as fractions of the pass's size (floats in its rectangle words) and enabled; applied (and noted in +0x79)
// when the device state accepts it. Otherwise false, with the state's enable byte cleared.
bool cDisplayListDynamicRenderStrategy::SetUpScissor(nGZSceneGraph::Pass *pass, uint8_t which)
{
    const int32_t *const bounds = which ? mTurboBounds : mBounds;
    if (mUseScissoring && bounds[2] > bounds[0] && bounds[3] > bounds[1] && mDebugMode != kDebugModeClears) {
        StoreFloat(&mScissor.mLeft, static_cast<float>(bounds[0]) / SizeAsFloat(pass->mWidth));
        StoreFloat(&mScissor.mTop, static_cast<float>(bounds[1]) / SizeAsFloat(pass->mHeight));
        StoreFloat(&mScissor.mRight, static_cast<float>(bounds[2]) / SizeAsFloat(pass->mWidth));
        StoreFloat(&mScissor.mBottom, static_cast<float>(bounds[3]) / SizeAsFloat(pass->mHeight));
        mScissor.mEnabled = 1;
        nGZGraphic4::cIDeviceState *const state = pass->mDeviceState;
        if (state->ValidateCommand(&mScissor, true, false)) { // receiver: cIDeviceState (slot 29)
            Apply(state, &mScissor); // receiver: cIDeviceState (slot 12, ApplyStateCommand)
            mByte79 = 1;
            return true;
        }
    }
    mScissor.mEnabled = 0;
    return false;
}

// ---- the drawing paths' item drawers and clears (row TS1g, lane 261108r) ----

namespace {

// The byte DrawOneItem tests with useScissoring: in .data's zero-filled tail, and nothing in the image refers to it but
// that read (tools/xrefs.py), so in the game the kept scissor state is never applied per item.
constexpr uint32_t kItemScissorCell = 0x10d9d24;
// Sort keys from 11000 on are drawn late (DrawItemRun's `late`, DrawItemsBy's mode 7); -9998 is the screen
// reflection's (DrawReflectionItems).
constexpr int32_t kLateSortKey = 11000;
constexpr int32_t kReflectionSortKey = -9998;
// The frame-state modes in which a dirty-rect frame draws only the marked records (mFlag1d): DrawReflectionItems' 2,
// DrawItemsBy's 1 and 2; either way only from the second dirty-rect frame in a row.
constexpr uint32_t kStateModeMarked = 2, kStateModeMarkedToo = 1;
constexpr uint32_t kMarkedFrames = 2;

// An item's material variant (the material manager's): slot 11 draws the item in the pass.
class cIDrawItemVariant : public cIGZUnknown {
public:
    virtual void Slot3() = 0;
    virtual void Slot4() = 0;
    virtual void Slot5() = 0;
    virtual void Slot6() = 0;
    virtual void Slot7() = 0;
    virtual void Slot8() = 0;
    virtual void Slot9() = 0;
    virtual void Slot10() = 0;
    virtual void Draw(nGZSceneGraph::Pass *pass, nGZSceneGraph::DrawItem *item) = 0; // 11
};

} // namespace

// 0x3743a0 (ret 8): the kept scissor state applied when the cell above and useScissoring are set, the item drawn by its
// variant, the pass's draw count 0 up.
void cDisplayListDynamicRenderStrategy::DrawOneItem(nGZSceneGraph::Pass *pass, nGZSceneGraph::DrawItem *item)
{
    if (*GameAddress<const uint8_t>(kItemScissorCell) && mUseScissoring)
        Apply(pass->mDeviceState, &mScissor); // receiver: cIDeviceState (slot 12, ApplyStateCommand)
    static_cast<cIDrawItemVariant *>(item->mMaterial)->Draw(pass, item); // receiver: nGZSceneGraph::cShader (0xaa69e0, ret 8)
    ++pass->mDrawStats[0];
}

// 0x374350 (ret 0x10): of `count` items in a row, those whose lateness (sort key 11000 or more) is `late`, compared as
// bytes: a `late` of 2 or more draws none.
void cDisplayListDynamicRenderStrategy::DrawItemRun(nGZSceneGraph::Pass *pass, nGZSceneGraph::DrawItem *items,
                                                    uint32_t count, uint8_t late)
{
    for (; count != 0; --count, ++items)
        if (late == static_cast<uint8_t>(items->mSortKey >= kLateSortKey)) DrawOneItem(pass, items);
}

// 0x374270 (ret 8): the drawn records' items whose being the reflection's (sort key -9998) is `reflection` (as bytes);
// without `reflection`, in mode 2 from the second dirty-rect frame, only the marked ones.
void cDisplayListDynamicRenderStrategy::DrawReflectionItems(nGZSceneGraph::Pass *pass, uint8_t reflection)
{
    const int32_t count = static_cast<int32_t>(mItems.size());
    const bool markedOnly = reflection == 0 && mState->mMode == kStateModeMarked && mState->mFrames >= kMarkedFrames;
    for (int32_t i = 0; i < count; ++i) {
        nGZSceneGraph::DrawItem *const item = mItems[i];
        if (reflection != static_cast<uint8_t>(item->mSortKey == kReflectionSortKey)) continue;
        const DynamicRectRecord *const record = mItemRecords[i];
        if (!record->mDrawn) continue;
        if (markedOnly && !record->mFlag1d) continue;
        DrawOneItem(pass, item);
    }
}

// 0x374100 (ret 8): the drawn records' items `mode` picks: 0 every early one (sort key below 11000), 1 / 2 the early
// ones of turbo-rect side 0 / the other side, 3 / 4 the early ones without / with +0x21, 5 the early ones with +0x20
// and +0x1f, 6 the early ones without +0x20, 7 the late ones; any other mode none. In modes 1 and 2 from the second
// dirty-rect frame, an early pick only when marked.
void cDisplayListDynamicRenderStrategy::DrawItemsBy(nGZSceneGraph::Pass *pass, uint32_t mode)
{
    const uint32_t stateMode = mState->mMode;
    const bool marked =
        (stateMode == kStateModeMarked || stateMode == kStateModeMarkedToo) && mState->mFrames >= kMarkedFrames;
    const uint32_t count = static_cast<uint32_t>(mItems.size());
    for (uint32_t i = 0; i < count; ++i) {
        nGZSceneGraph::DrawItem *const item = mItems[i];
        const DynamicRectRecord *const record = mItemRecords[i];
        if (!record->mDrawn) continue;
        const bool early = item->mSortKey < kLateSortKey;
        bool picked;
        switch (mode) {
        case 0: picked = early; break;
        case 1: picked = early && record->mSecond == 0; break;
        case 2: picked = early && record->mSecond != 0; break;
        case 3: picked = early && record->mUnknown21 == 0; break;
        case 4: picked = early && record->mUnknown21 != 0; break;
        case 5: picked = early && record->mFlag20 != 0 && record->mFlag1f != 0; break;
        case 6: picked = early && record->mFlag20 == 0; break;
        case 7: picked = !early; break;
        default: picked = false; break;
        }
        if (marked && early) picked = picked && record->mFlag1d != 0;
        if (picked) DrawOneItem(pass, item);
    }
}

// 0x374ad0 (ret 4): the censor's box rectangles listed; with any, the records under them marked and the censor's
// texture drawn with the frame's items, otherwise the frame state's kept rectangles emptied.
void cDisplayListDynamicRenderStrategy::DrawCensor(nGZSceneGraph::Pass *pass)
{
    std::list<DynamicWords16> rects;
    if (mCensor->ListBoxRects(&rects) > 0) {
        MarkRecords(&rects, pass, false, true, 0);
        mCensor->RenderTexture(pass, &mItems, &mItemRecords);
    } else {
        mState->mRects54.clear();
    }
}

// 0x373b70 (ret 0xc): below the debug modes past the clears' (4 and on: nothing), each rectangle cleared in the side's
// target (flags 6 for the first side, 4 for the second), scaled by the target's scale times its aspect unless that is
// exactly 1 (each coordinate then the truncation of coordinate * scale + 0.5, in floats).
void cDisplayListDynamicRenderStrategy::ClearRects(nGZSceneGraph::Pass *pass, const std::list<DynamicWords16> *rects,
                                                   uint8_t second)
{
    if (mDebugMode > kDebugModeClears) return;
    const uint32_t flags = (static_cast<uint32_t>(second) ^ 1u) * 2u + 4u;
    cITSRenderSurface *const target = second ? mTargetSecond : mTarget;
    const float aspect = target->GetAspect();     // receiver: cITSRenderSurface (folded 0x18c7a0 / 0x372660)
    const float targetScale = target->GetScale(); // receiver: cITSRenderSurface (folded 0x153f90)
    // One rounding: the product of two floats is exact in a double.
    const float scale = static_cast<float>(static_cast<double>(targetScale) * static_cast<double>(aspect));
    if (scale == 1.0f) {
        for (const DynamicWords16 &r : *rects)
            target->ClearRect(pass, reinterpret_cast<const int32_t *>(r.w), flags); // receiver: cTSRenderSurfaceImpl (0x370fe0, ret 0xc)
        return;
    }
    for (const DynamicWords16 &r : *rects) {
        int32_t scaled[4];
        for (unsigned k = 0; k < 4; ++k)
            scaled[k] = static_cast<int32_t>(static_cast<float>(static_cast<int32_t>(r.w[k])) * scale + 0.5f);
        target->ClearRect(pass, scaled, flags); // receiver: cTSRenderSurfaceImpl (0x370fe0, ret 0xc)
    }
}

// 0x375240: the window manager (from the framework) asked for its main window, whose slot 37 walks its children with
// the window id filter, the callback (OpaqueUICallback, 0x3752d0) and the flag.
void cDisplayListDynamicRenderStrategy::MarkOpaqueUI(uint8_t *flag, uint8_t value)
{
    *flag = value;
    struct HeldManager {
        ScopedRef mRef{nullptr};
        ~HeldManager() { mRef.ReleaseObject(); }
    } manager;
    if (void *const framework = GetServiceObject())
        As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kWinMgrId, cIGZWinMgr::kIid, &manager.mRef.mObject);
    cIGZUnknown *const window = static_cast<cIGZWinMgr *>(manager.mRef.mObject)->GetMainWindow();
    RawSlot<37, bool>(window, kOpaqueUIWindowId, reinterpret_cast<void *>(&OpaqueUICallback), flag); // receiver: cGZWin::vf37 0x930cff (ret 0xc)
}

// ---- the window callback and the window's uint32 properties (row TS1d) ----

namespace {

// A window property's variant, released on the way out (also when something throws).
struct HeldVariant {
    cIGZVariant *mVariant = nullptr;
    HeldVariant() = default;
    HeldVariant(const HeldVariant &) = delete;
    HeldVariant &operator=(const HeldVariant &) = delete;
    ~HeldVariant()
    {
        if (mVariant) mVariant->Release();
    }
};

// The window property the callback keeps its state in (bit 4: the window was opaque when the callback cleared its
// flag 0x20) and the window flags it reads (the cGZWin's slot 82) and sets (slot 83).
constexpr uint32_t kOpaqueProperty = 0x300d422;
constexpr uint32_t kFlagVisible = 1, kFlagTracked = 4, kFlagOpaque = 0x20, kFlagOpaqueUI = 0x100000;

// The window's slots (cGZWin; the callback's windows are the main window's children): 15 the child count, 37 the
// children walk, 82 a flag test, 83 a flag set, 126 a property get (the variant counted for the caller, ret 8), 127 a
// property set (ret 8). The flag test's byte is used as the window returns it.
uint8_t WindowFlag(cIGZUnknown *window, uint32_t mask) { return RawSlot<82, uint8_t>(window, mask); }

} // namespace

// 0x372670: the window's property `id` as a uint32 (the variant's GetValUint32), 0 when the window has none.
uint32_t WindowPropertyUint32(cIGZUnknown *window, uint32_t id)
{
    uint32_t value = 0;
    HeldVariant held;
    if (RawSlot<126, bool>(window, id, &held.mVariant)) held.mVariant->GetValUint32(&value);
    return value;
}

// 0x375660: the window's property `id` set to `value`: the window's own variant when it has one, else a new one handed
// to the window (slot 127). A variant the get left with a failed get is replaced (released) by the new one.
void SetWindowPropertyUint32(cIGZUnknown *window, uint32_t id, uint32_t value)
{
    HeldVariant held;
    if (!RawSlot<126, bool>(window, id, &held.mVariant)) {
        cIGZVariant *const made = new (std::nothrow) cRZVariant;
        if (made != held.mVariant) {
            if (made) made->AddRef();
            cIGZVariant *const old = held.mVariant;
            held.mVariant = made;
            if (old) old->Release();
        }
        // ORIGINAL-BUG: a failed allocation leaves no variant, and the store goes through the null pointer.
        held.mVariant->SetValUint32(value);
        RawSlot<127>(window, id, held.mVariant);
    } else {
        held.mVariant->SetValUint32(value);
    }
}

// 0x3752d0 (cdecl, held as data by MarkOpaqueUI and itself): for a visible window, its flag 0x100000 set to the flag
// when the window is tracked (flag 4 and the property's bit 0); an opaque window (flag 0x20) loses it while the flag is
// set, remembered in the property's bit 4; a window remembered so gets it back while the flag is clear. Then the
// window's children walked the same way; false (stop the walk) when that walk says so. The flag byte is read once,
// before anything (the walk below sees the pointer). The first two arguments are unused.
bool __cdecl cDisplayListDynamicRenderStrategy::OpaqueUICallback(cIGZUnknown *, uint32_t, cIGZUnknown *window,
                                                                 uint8_t *flagPtr)
{
    const uint8_t flag = *flagPtr;
    if (!WindowFlag(window, kFlagVisible)) return true;
    const uint32_t state = WindowPropertyUint32(window, kOpaqueProperty);
    if (WindowFlag(window, kFlagTracked) & static_cast<uint8_t>(state) & 1u)
        RawSlot<83>(window, kFlagOpaqueUI, static_cast<uint32_t>(flag)); // the flag's byte as the bool
    if (WindowFlag(window, kFlagOpaque)) {
        if (flag) {
            RawSlot<83>(window, kFlagOpaque, 0u);
            SetWindowPropertyUint32(window, kOpaqueProperty, state | 4u);
        }
    } else if (!flag && (state & 4u)) {
        RawSlot<83>(window, kFlagOpaque, 1u);
    }
    if (RawSlot<15, uint32_t>(window) != 0 &&
        !RawSlot<37, uint8_t>(window, kOpaqueUIWindowId, reinterpret_cast<void *>(&OpaqueUICallback), flagPtr))
        return false;
    return true;
}

// ---- the frame's dirty rectangles (row TS1f) ----

namespace {

// The game's float-to-integer rounding (the censor strategy's too): x moved half a unit down (floor) or up (ceil) and
// added to 1.5 * 2^23, whose low mantissa bits are then the rounded integer, corrected by one when that integer's
// neighbour equals x. Single precision throughout.
constexpr float kRoundHalf = 0.5f;
constexpr float kRoundMagic = 12582912.0f; // 1.5 * 2^23
constexpr uint32_t kRoundMagicBits = 0x4b400000u;

int32_t RoundedBits(float t) { return static_cast<int32_t>(std::bit_cast<uint32_t>(t) - kRoundMagicBits); }

int32_t Floor(float x)
{
    const float t = (x - kRoundHalf) + kRoundMagic;
    const int32_t r = RoundedBits(t);
    const int32_t up = static_cast<int32_t>(uint32_t(r) + 1);
    return static_cast<float>(up) == x ? up : r;
}

int32_t Ceil(float x)
{
    const float t = (x + kRoundHalf) + kRoundMagic;
    const int32_t r = RoundedBits(t);
    const int32_t down = static_cast<int32_t>(uint32_t(r) - 1);
    return static_cast<float>(down) == x ? down : r;
}

// A rectangle's area (height times width) in the game's wrapping 32-bit arithmetic.
uint32_t RectArea(const int32_t *r) { return (uint32_t(r[3]) - uint32_t(r[1])) * (uint32_t(r[2]) - uint32_t(r[0])); }

bool SameRect(const int32_t *a, const int32_t *b) { return std::memcmp(a, b, 4 * sizeof(int32_t)) == 0; }

const int32_t *Words(const DynamicWords16 &r) { return reinterpret_cast<const int32_t *>(r.w); }
int32_t *Words(DynamicWords16 &r) { return reinterpret_cast<int32_t *>(r.w); }

DynamicWords16 AsWords(const int32_t *rect)
{
    DynamicWords16 words;
    std::memcpy(words.w, rect, sizeof words.w);
    return words;
}

// The window flag (slot 82) of a window the helper does not record (its children are still walked), the property bits
// (kOpaqueProperty) it reads: bit 0 an unchanged transparent window redrawn with renderOpaqueUI, bit 1 always redrawn;
// and the colour a window's slot 125 is asked with (it writes its fill colour over it).
constexpr uint32_t kFlagUnrecorded = 0x80;
constexpr uint32_t kPropertyRedrawTransparent = 1, kPropertyRedraw = 2;
constexpr uint32_t kOpaqueBlack = 0xff000000u;

// An item's byte +0x16 (its sub key's bits 16..23): bit 0 changed since last frame, bit 2 covers the whole pass.
constexpr uint8_t kItemChanged = 1, kItemWholePass = 4;


} // namespace

// 0x372270: by the item's state word 2 (bit 1 or 2: drawn from its node record), the box and placement are the node
// record's, else the geometry subset's bounds (the geometry's slot 5 for the sub key's low 16 bits) and the item's
// placement. The box projected by placement x the pass's matrix 3; in front of the eye (the far corner's depth not
// below 0, NaN counting as behind) the corners rounded out (lo down, hi up) and clipped to `pixelRect` (IntersectRect:
// all zero and false when they miss); behind it the empty rectangle and false.
bool DrawItemPixelRect(int32_t *out, const int32_t *pixelRect, nGZSceneGraph::Pass *pass,
                       const nGZSceneGraph::DrawItem *item)
{
    const nGZCompGeom::BoundBox *box;
    const AffineTransform3 *placement;
    const uint32_t state = item->mState[2];
    if (((state >> 2) | (state >> 1)) & 1u) {
        const nGZSceneGraph::NodeRecord *const record = static_cast<const nGZSceneGraph::NodeRecord *>(item->mRecord);
        box = &record->mBounds;
        placement = &record->mPlacement;
    } else {
        auto *const geometry = static_cast<nGZSceneGraph::cIGeometryRender *>(item->mGeometry);
        const uint32_t *const info = geometry->GetDrawInfo(static_cast<uint16_t>(item->mSubKey)); // receiver: cIGeometryRender
        box = &reinterpret_cast<const nGZSceneGraph::GeometrySubset *>(info)->mBounds;
        placement = static_cast<const AffineTransform3 *>(item->mPlacement);
    }
    float frame[16], matrix[16], lo[3], hi[3];
    nGZCompGeom::Matrix4Multiply(matrix, nGZCompGeom::AsFrame(*placement).ToMatrix4(frame), pass->mMatrices[3]);
    // The near corner `lo` and the far one `hi` in pixels and depth (0xaf22d0, the censor's too).
    nGZSceneGraph::ProjectBox(lo, hi, box, matrix);
    if (hi[2] >= 0.0f) {
        out[0] = Floor(lo[0]);
        out[1] = Floor(lo[1]);
        out[2] = Ceil(hi[0]);
        out[3] = Ceil(hi[1]);
        return IntersectRect(out, out, pixelRect);
    }
    CopyRect(out, GameAddress<int32_t>(kEmptyRectCell));
    return false;
}

// 0x3703f0.
void DynamicStrategyTable::Push(const int32_t *rect)
{
    mDirtyArea += RectArea(rect);
    mDirtyRects.push_back(AsWords(rect));
}

// 0x372430.
std::vector<DynamicWords16> *DynamicStrategyTable::Update(nGZSceneGraph::Pass *pass,
                                                          const nGZSceneGraph::DrawItem *items, uint32_t count,
                                                          bool gather, uint32_t clearMode, const int32_t *pixelRect,
                                                          bool useRecords)
{
    mFilled = 1;
    mDirtyRects.clear();
    mDirtyTurbo.clear();
    Collect(pass, items, count, gather, clearMode, pixelRect, useRecords);
    return &mDirtyRects;
}

// 0x36fc40 (reached only through Update, from UpdateDirtyRects). The records sized to the display list's slot count
// (the owner's slot 41) and this frame's visible bitset to cover them (never shorter than last frame's), all clear.
// Then per item, by its record (the item's mUserData):
// - its rectangle: with `useRecords` and a record from the last frame, an unchanged item (byte +0x16 bit 0 clear)
//   keeps the record's rectangle and drawn flag; any other item is changed and gets DrawItemPixelRect's (the record's
//   drawn flag cleared first when the record is stale). On screen sets the item's bit in mVisible. An item covering
//   the whole pass (bit 2) is then visible with the pass's rectangle.
// - its side (the turbo rect's, mClearMode 0 and 1): mode 0 a positive sort key, or 0 and changed; mode 1 an odd key
//   or changed; mode 2 none.
// - gathering, the rectangles pushed: modes 0 and 1, for a changed item the record's old rectangle (when drawn) with
//   the record's side and the new one (when visible) with the new side, a side-0 rectangle pushed a second time on
//   side 1; for an unchanged one the rectangle when it moved side (visible, drawn and the side differs). Mode 2: a
//   changed item's old and new rectangles, an unchanged one's old one only with the debug drawing on (mDebug) and the
//   record flagged changed last frame.
// - the record rewritten for this frame and its area summed.
// Gathering, the records visible last frame but not this one are pushed as well (PushVanished).
void DynamicStrategyTable::Collect(nGZSceneGraph::Pass *pass, const nGZSceneGraph::DrawItem *items, uint32_t count,
                                   bool gather, uint32_t clearMode, const int32_t *pixelRect, bool useRecords)
{
    const uint32_t last = mFrame - 1;
    const uint32_t slots = RawSlot<41, uint32_t>(pass->mOwner); // receiver: cIDisplayList (GetSlotCount)
    mRecords.resize(slots);
    mVisible.clear();
    mVisible.resize(std::max<size_t>(mLastVisible.size(), (slots + 31u) >> 5), 0u);
    uint32_t *const visibleBits = mVisible.data();

    // A rectangle on a side: side 0 is pushed twice, the second time on side 1.
    const auto pushSided = [this](const int32_t *rect, uint8_t side) {
        Push(rect);
        mDirtyTurbo.push_back(side != 0);
        if (!side) {
            Push(rect);
            mDirtyTurbo.push_back(true);
        }
    };

    // ORIGINAL-BUG: a clear mode other than 0..2 keeps the previous item's side (the first item's is an uninitialised
    // stack byte in the game; Prepare only sets 0..2, so it never happens).
    uint8_t side = 0;
    for (const nGZSceneGraph::DrawItem *item = items; item != items + count; ++item) {
        const uint32_t index = item->mUserData;
        // ORIGINAL-BUG: the item's index is not checked against the record count.
        DynamicRectRecord &record = mRecords.data()[index];
        const uint8_t even = !(item->mSortKey & 1);
        const uint8_t flags = static_cast<uint8_t>(item->mSubKey >> 16);
        uint8_t changed = flags & kItemChanged;
        int32_t rect[4];
        uint8_t visible;
        if (useRecords && record.mFrame == last) {
            if (!changed) {
                CopyRect(rect, record.mRect);
                visible = record.mDrawn;
            } else {
                visible = DrawItemPixelRect(rect, pixelRect, pass, item);
            }
        } else {
            changed = 1;
            record.mDrawn = 0;
            visible = DrawItemPixelRect(rect, pixelRect, pass, item);
        }
        if (visible) visibleBits[index >> 5] |= 0x80000000u >> (index & 31);
        if (flags & kItemWholePass) {
            visible = 1;
            CopyRect(rect, pixelRect);
        }

        switch (clearMode) {
        case 0: side = item->mSortKey > 0 || (item->mSortKey == 0 && changed); break;
        case 1: side = !even || changed; break;
        case 2: side = 0; break;
        default: break;
        }

        if (gather) {
            if (clearMode == 0 || clearMode == 1) {
                if (changed) {
                    if (record.mDrawn) pushSided(record.mRect, record.mSecond);
                    if (visible) pushSided(rect, side);
                } else if (visible && record.mDrawn && side != record.mSecond) {
                    pushSided(rect, side);
                }
            } else if (clearMode == 2) {
                if (changed) {
                    if (record.mDrawn) Push(record.mRect);
                    if (visible) Push(rect);
                } else if (record.mDrawn && mDebug && record.mFlag1c) {
                    Push(record.mRect);
                }
            }
        }

        CopyRect(record.mRect, rect);
        record.mSortKey = item->mSortKey;
        record.mDrawn = visible;
        record.mFrame = mFrame;
        record.mFlag1c = changed;
        record.mFlag1d = 0;
        record.mFlag20 = even;
        record.mFlag1f = changed;
        record.mUnknown21 = even && !changed;
        record.mSecond = side;
        mRecordArea += RectArea(rect);
    }
    if (gather) PushVanished(clearMode);
}

// 0x370980: each mLastVisible word less this frame's (mVisible), its set bits taken from the top, the record of each
// bit pushed (modes 0 and 1 with its side as Collect pushes a changed record's old rectangle; mode 2 plainly).
void DynamicStrategyTable::PushVanished(uint32_t clearMode)
{
    const DynamicRectRecord *record = mRecords.data();
    const uint32_t *current = mVisible.data();
    const uint32_t *const end = mLastVisible.data() + mLastVisible.size();
    for (const uint32_t *word = mLastVisible.data(); word != end; ++word, ++current) {
        uint32_t bits = *word & ~*current;
        if (!bits) {
            record += 32;
            continue;
        }
        // ORIGINAL-BUG: last frame's bitset may cover more records than this frame's record count (the game reads past
        // them as well); a clear mode other than 0..2 does not advance the record (never set).
        if (clearMode == 0 || clearMode == 1) {
            for (int bit = 0; bit < 32; ++bit, bits <<= 1, ++record) {
                if (!(bits & 0x80000000u)) continue;
                const uint8_t side = record->mSecond;
                Push(record->mRect);
                mDirtyTurbo.push_back(side != 0);
                if (!side) {
                    Push(record->mRect);
                    mDirtyTurbo.push_back(true);
                }
            }
        } else if (clearMode == 2) {
            for (int bit = 0; bit < 32; ++bit, bits <<= 1, ++record)
                if (bits & 0x80000000u) Push(record->mRect);
        }
    }
}

// 0x370430.
void DynamicStrategyHelper::Add(const int32_t *rect)
{
    mAddedArea += RectArea(rect);
    mItems.push_back(AsWords(rect));
}

// 0x375a20: the entry (inserted when new) of this frame's map rewritten whole. The game stores its two pad bytes from
// its stack; ours are zero (nothing reads them).
void DynamicStrategyHelper::Record(uint32_t id, const int32_t *rect, uint8_t dirty, uint8_t opaque)
{
    DynamicWindowEntry entry{};
    CopyRect(entry.mRect, rect);
    entry.mFrame = mFrame;
    entry.mDirty = dirty;
    entry.mOpaque = opaque;
    mRecordedArea += RectArea(rect);
    std::memcpy(&(*mMap14)[id], &entry, sizeof entry);
}

// 0x370cd0: the window's screen rectangle, id and opacity. A window last frame's map does not have is added and
// recorded dirty. One it has gets its entry's frame set to this one (in last frame's map), then is added when its
// property's bit 1 is set or it moved (its old rectangle too when it differs), or else when its opacity changed or it
// is transparent and renderOpaqueUI is off or its property's bit 0 set (with the debug drawing on, its old rectangle
// is first added when that entry was dirty); recorded dirty when added.
void DynamicStrategyHelper::Window(cIGZUnknown *window)
{
    int32_t rect[4];
    WindowScreenRect(window, rect);
    const uint32_t id = WindowId(window);
    const uint8_t opaque = WindowOpaque(window);
    const Map::iterator found = mMap10->find(id);
    if (found == mMap10->end()) {
        Add(rect);
        Record(id, rect, 1, opaque);
        return;
    }
    DynamicWindowEntry &entry = reinterpret_cast<DynamicWindowEntry &>(found->second);
    entry.mFrame = mFrame;
    const uint8_t wasDirty = entry.mDirty;
    const uint8_t wasOpaque = entry.mOpaque;
    const bool moved = !SameRect(rect, entry.mRect);
    const uint32_t property = WindowPropertyUint32(window, kOpaqueProperty);
    const bool redrawTransparent = !mFlagC || (property & kPropertyRedrawTransparent);
    uint8_t dirty = 0;
    if ((property & kPropertyRedraw) || moved) {
        if (!SameRect(entry.mRect, rect)) Add(entry.mRect);
        Add(rect);
        dirty = 1;
    } else {
        if (mDebug && wasDirty) Add(entry.mRect);
        if (opaque != wasOpaque || (!opaque && redrawTransparent)) {
            Add(rect);
            dirty = 1;
        }
    }
    Record(id, rect, dirty, opaque);
}

// 0x3702c0: the area sums zeroed; the window manager (from the framework) asked for its main window, whose slot 37
// walks its children with the window id filter, WindowCallback and this; then the rectangle of each window in last
// frame's map not seen this frame (its entry's frame not set by Window) added, in the map's order.
void DynamicStrategyHelper::CollectWindows()
{
    mRecordedArea = 0;
    mAddedArea = 0;
    struct HeldManager {
        ScopedRef mRef{nullptr};
        ~HeldManager() { mRef.ReleaseObject(); }
    } manager;
    if (void *const framework = GetServiceObject())
        As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kWinMgrId, cIGZWinMgr::kIid, &manager.mRef.mObject);
    // ORIGINAL-BUG: no window manager (no framework, or no such service) is not checked: the call goes through null.
    cIGZUnknown *const window = static_cast<cIGZWinMgr *>(manager.mRef.mObject)->GetMainWindow();
    RawSlot<37, bool>(window, kOpaqueUIWindowId, reinterpret_cast<void *>(&WindowCallback), this); // receiver: cGZWin::vf37 0x930cff (ret 0xc)
    for (const auto &seen : *mMap10) {
        const DynamicWindowEntry &entry = reinterpret_cast<const DynamicWindowEntry &>(seen.second);
        if (entry.mFrame != mFrame) Add(entry.mRect);
    }
}

// 0x372480.
std::vector<DynamicWords16> *DynamicStrategyHelper::Collect()
{
    mByte24 = 1;
    mItems.clear();
    CollectWindows();
    return &mItems;
}

// 0x370e40 (cdecl, held as data by CollectWindows and itself). The first two arguments are unused.
bool __cdecl DynamicStrategyHelper::WindowCallback(cIGZUnknown *, uint32_t, cIGZUnknown *window,
                                                   DynamicStrategyHelper *helper)
{
    if (!WindowFlag(window, kFlagVisible)) return true;
    if (!WindowFlag(window, kFlagUnrecorded)) helper->Window(window);
    if (RawSlot<15, uint32_t>(window) != 0 && // receiver: cGZWin (slot 15, the child count)
        !RawSlot<37, uint8_t>(window, kOpaqueUIWindowId, reinterpret_cast<void *>(&WindowCallback), helper)) // receiver: cGZWin::vf37 0x930cff (ret 0xc)
        return false;
    return true;
}

// 0x372600: the window's rectangle (slot 55), offset by each ancestor's position (slot 11 the parent, 52 its y and 51
// its x, asked in that order).
void WindowScreenRect(cIGZUnknown *window, int32_t *out)
{
    RawSlot<55>(window, out); // receiver: cGZWin (slot 55, the window's rectangle)
    for (cIGZUnknown *parent = RawSlot<11, cIGZUnknown *>(window); parent; // receiver: cGZWin (slot 11, the parent)
         parent = RawSlot<11, cIGZUnknown *>(parent)) {                    // receiver: cGZWin (slot 11, the parent)
        const int32_t dy = RawSlot<52, int32_t>(parent); // receiver: cGZWin (slot 52, y)
        const int32_t dx = RawSlot<51, int32_t>(parent); // receiver: cGZWin (slot 51, x)
        out[0] = static_cast<int32_t>(uint32_t(out[0]) + uint32_t(dx));
        out[2] = static_cast<int32_t>(uint32_t(out[2]) + uint32_t(dx));
        out[1] = static_cast<int32_t>(uint32_t(out[1]) + uint32_t(dy));
        out[3] = static_cast<int32_t>(uint32_t(out[3]) + uint32_t(dy));
    }
}

// 0x3726f0: the game's body is a tail jump to the window's slot 80 (jmp [eax+0x140]).
uint32_t WindowId(cIGZUnknown *window)
{
    return RawSlot<80, uint32_t>(window); // receiver: cGZWin (slot 80, the id)
}

// 0x3738b0: opaque unless flagged 0x20 with a fill colour (slot 125, asked with opaque black) not fully opaque; a
// window not flagged 0x100000 but flagged 4 is never opaque.
bool WindowOpaque(cIGZUnknown *window)
{
    bool opaque = true;
    if (WindowFlag(window, kFlagOpaque)) {
        uint32_t color = kOpaqueBlack;
        RawSlot<125>(window, &color); // receiver: cGZWin (slot 125, the fill colour)
        opaque = (color >> 24) == 0xffu;
    }
    if (WindowFlag(window, kFlagOpaqueUI)) return opaque;
    return WindowFlag(window, kFlagTracked) ? false : opaque;
}

namespace {
// std::list's node (MSVC): AddRect may write the union into the sentinel's value storage, which only the raw layout
// reaches (the storage is allocated with the sentinel and never constructed).
struct RectListNode {
    RectListNode *mNext;
    RectListNode *mPrev;
    DynamicWords16 mValue;
};
static_assert(sizeof(RectListNode) == 0x18, "the game's list node (new 0x18)");

DynamicWords16 *SentinelValue(std::list<DynamicWords16> *list)
{
    return &(*reinterpret_cast<RectListNode **>(list))->mValue;
}
} // namespace

// 0x370470: for each element, the score = the rectangle's area + the element's - their union's (the least wasted
// union scores highest; wrapping 32-bit arithmetic, the first best kept). When the best score is at least
// -mMergeThreshold, the union replaces the element, or, when it is larger than the element and the list not empty,
// the element is erased and the union merged again. Otherwise the rectangle is appended. Empty rectangles are ignored.
void cDisplayListDynamicRenderStrategy::AddRect(std::list<DynamicWords16> *list, const int32_t *rect)
{
    const int32_t x0 = rect[0], y0 = rect[1], x1 = rect[2], y1 = rect[3];
    if (x1 <= x0 || y1 <= y0) return;
    const uint32_t area = (uint32_t(x1) - uint32_t(x0)) * (uint32_t(y1) - uint32_t(y0));
    int32_t best = INT32_MIN;
    uint32_t growth = 0;
    std::list<DynamicWords16>::iterator bestElement = list->end();
    // ORIGINAL-BUG: with no element scoring above INT32_MIN (an empty list, or every union's waste that large) and a
    // threshold of 0x80000000, the "best" is the list's sentinel: its unconstructed value storage is read and written.
    DynamicWords16 *bestValue = SentinelValue(list);
    for (auto it = list->begin(); it != list->end(); ++it) {
        const int32_t *const n = Words(*it);
        const uint32_t own = RectArea(n);
        const uint32_t joined = (uint32_t(std::max(y1, n[3])) - uint32_t(std::min(y0, n[1]))) *
                                (uint32_t(std::max(x1, n[2])) - uint32_t(std::min(x0, n[0])));
        const int32_t score = static_cast<int32_t>(area + (own - joined));
        if (score > best) {
            bestElement = it;
            bestValue = &*it;
            growth = joined - own;
            best = score;
        }
    }
    if (best >= static_cast<int32_t>(0u - mMergeThreshold)) {
        const int32_t *const b = Words(*bestValue);
        const int32_t joined[4] = {std::min(x0, b[0]), std::min(y0, b[1]), std::max(x1, b[2]), std::max(y1, b[3])};
        if (static_cast<int32_t>(growth) > 0 && !list->empty()) {
            list->erase(bestElement);
            AddRect(list, joined);
            return;
        }
        CopyRect(Words(*bestValue), joined);
        return;
    }
    list->push_back(AsWords(rect));
}

// 0x3706a0 (hooked; the drawing path 0x374670 calls it, 0x3743e0 too). Gathering means the frame state has two or
// more dirty-rect frames (mFrames). In the dirty-rect mode with the dirty-rect drawing allowed, the helper's UI
// rectangles are collected (renderOpaqueUI handed to it) and, gathering, each clipped to the pass's pixels merged into
// mRects. The table's rectangles (Update) follow, gathering, by side (modes 0 and 1: the table's bit per rectangle,
// set for mTurboRects; mode 2 all mRects), then the censor's (0x377f50, with the frame state's mRects54) that meet
// the pass's pixels, clipped to them, into mTurboRects in mode 1 and mRects otherwise.
void cDisplayListDynamicRenderStrategy::UpdateDirtyRects(nGZSceneGraph::Pass *pass,
                                                         const nGZSceneGraph::DrawItem *items, uint32_t count,
                                                         bool useRecords)
{
    const DynamicStrategyState *const state = mState;
    const bool gather = state->mFrames >= 2;
    if (mDirtyRects && state->mMode == DynamicStrategyState::kDirtyRects) {
        mHelper->mFlagC = mRenderOpaqueUI;
        const std::vector<DynamicWords16> *const ui = mHelper->Collect();
        if (gather) {
            for (const DynamicWords16 &r : *ui) {
                int32_t clipped[4];
                CopyRect(clipped, Words(r));
                if (IntersectRect(clipped, clipped, mPixelRect)) AddRect(&mRects, clipped);
            }
        }
    }

    const std::vector<DynamicWords16> *const rects =
        mState->mTable->Update(pass, items, count, gather, mClearMode, mPixelRect, useRecords);
    if (gather) {
        uint32_t index = 0;
        for (const DynamicWords16 &r : *rects) {
            int32_t copy[4];
            CopyRect(copy, Words(r));
            switch (mClearMode) {
            case 0:
            case 1: {
                const bool turbo = mState->mTable->mDirtyTurbo[index++];
                AddRect(turbo ? &mTurboRects : &mRects, copy);
                break;
            }
            case 2: AddRect(&mRects, copy); break;
            default: break;
            }
        }
    }

    std::vector<DynamicWords16> censored;
    mCensor->CollectDirtyRects(pass, &censored);
    if (!gather) return;
    for (const DynamicWords16 &r : censored) {
        int32_t c[4];
        CopyRect(c, Words(r));
        const int32_t *const px = mPixelRect;
        if (c[2] > px[0] && c[0] < px[2] && c[3] > px[1] && c[1] < px[3]) {
            c[0] = std::max(c[0], px[0]);
            c[1] = std::max(c[1], px[1]);
            c[2] = std::min(c[2], px[2]);
            c[3] = std::min(c[3], px[3]);
            AddRect(mClearMode == 1 ? &mTurboRects : &mRects, c);
        }
    }
}

// ---- the outlines (0x371670 and its drawer) ----

namespace {

using nGZGraphic4::cIDraw2D;

// The outlines' colours: the brush's outline colour (slot 5) and fill colour (slot 6) per kind, from game cells; the
// kinds 1, 2, 4 and 0x10 build their fill colours from a byte cell (the alpha) and a constant.
constexpr uint32_t kOutlineAllCell = 0x100902c, kOutlineFillCell = 0x10d9cd4;
constexpr uint32_t kOutlineKind2Cell = 0x1009024, kOutlineKind4Cell = 0x1009034;
constexpr uint32_t kOutlineRectsCell = 0x1009014, kOutlineBoundsCell = 0x100901c;
constexpr uint32_t kAlphaKind2Cell = 0x10091dc, kAlphaKind4Cell = 0x10091e0, kAlphaKind1Cell = 0x10091e4,
                   kAlphaKind10Cell = 0x10091e8;
constexpr uint32_t kOutlinePixelsColor = 0xffffff00;
// dirtyRectsDebug 2's flags: when the byte cell is set, the mode's flags are stored in the two cells (first sweep,
// second sweep) the drawing then uses; otherwise whatever they held.
constexpr uint32_t kOutlineCellsEnabled = 0x10091c8, kOutlineFlags0Cell = 0x10d9d1c, kOutlineFlags1Cell = 0x10d9d20;

// The outline kinds (bits of a sweep's flags).
constexpr uint32_t kOutlineLast = 1, kOutlineFlag1c = 2, kOutlineFlag1d = 4, kOutlineFlag20 = 8, kOutlineNew = 0x10;
constexpr uint32_t kOutlineRects = 0x100, kOutlineBounds = 0x200, kOutlinePixels = 0x400, kOutlineAll = 0x40000000;

uint8_t GameByte(uint32_t rva) { return *GameAddress<uint8_t>(rva); }

// The outline drawer (the original's is a stack object): the Draw2D, the brush, the rectangles' scale, the frame
// state's table, the helper's maps (none in the dirty-rect surfaces' mode), the frame's rectangle list, bounds and
// pixel rectangle, the frame and the turbo-rect flag. Pass 1 of a sweep draws what pass 0 leaves out.
struct OutlineDrawer {
    cIDraw2D *mDraw;
    nGZGraphic4::cIOutlineColorBrush *mBrush;
    float mScaleX, mScaleY;
    DynamicStrategyTable *mTable;
    DynamicStrategyHelper *mHelper;
    std::list<DynamicWords16> *mRects;
    int32_t mBounds[4];
    int32_t mPixels[4];
    uint32_t mFrame;
    uint8_t mSecond;

    // 0x371600: a rectangle (min x, min y, max x, max y) filled, scaled.
    void Rect(int32_t x0, int32_t y0, int32_t x1, int32_t y1) const
    {
        mDraw->FillRect(static_cast<int32_t>(static_cast<float>(x0) * mScaleX),
                        static_cast<int32_t>(static_cast<float>(y0) * mScaleY),
                        static_cast<int32_t>(static_cast<float>(x1 - x0) * mScaleX),
                        static_cast<int32_t>(static_cast<float>(y1 - y0) * mScaleY));
    }
    void Rect(const int32_t *r) const { Rect(r[0], r[1], r[2], r[3]); }
    void Rect(const uint32_t *w) const
    {
        Rect(static_cast<int32_t>(w[0]), static_cast<int32_t>(w[1]), static_cast<int32_t>(w[2]),
             static_cast<int32_t>(w[3]));
    }

    // 0x3753c0: the brush's colours for a kind (nothing for other kinds).
    void Colour(uint32_t kind) const
    {
        const auto cell = [](uint32_t rva) { return GameAddress<uint32_t>(rva); };
        uint32_t word;
        switch (kind) {
        case kOutlineAll:
            mBrush->SetOutlineColor(cell(kOutlineAllCell));
            mBrush->SetFillColor(cell(kOutlineFillCell));
            break;
        case kOutlineFlag1c:
            mBrush->SetOutlineColor(cell(kOutlineKind2Cell));
            word = static_cast<uint32_t>(GameByte(kAlphaKind2Cell)) << 24 | 0x400000u;
            mBrush->SetFillColor(&word);
            break;
        case kOutlineLast:
            mBrush->SetOutlineColor(cell(kOutlineKind2Cell));
            word = static_cast<uint32_t>(GameByte(kAlphaKind1Cell)) << 24 | 0x400000u;
            mBrush->SetFillColor(&word);
            break;
        case kOutlineFlag1d:
            mBrush->SetOutlineColor(cell(kOutlineKind4Cell));
            word = static_cast<uint32_t>(GameByte(kAlphaKind4Cell)) << 24 | 0x40u;
            mBrush->SetFillColor(&word);
            break;
        case kOutlineRects:
            mBrush->SetOutlineColor(cell(kOutlineRectsCell));
            mBrush->SetFillColor(cell(kOutlineFillCell));
            break;
        case kOutlineBounds:
            mBrush->SetOutlineColor(cell(kOutlineBoundsCell));
            mBrush->SetFillColor(cell(kOutlineFillCell));
            break;
        case kOutlinePixels:
            word = kOutlinePixelsColor;
            mBrush->SetOutlineColor(&word);
            mBrush->SetFillColor(cell(kOutlineFillCell));
            break;
        case kOutlineNew: // the same word for both, its alpha read again for the fill
            word = static_cast<uint32_t>(GameByte(kAlphaKind10Cell)) << 24 | 0x400040u;
            mBrush->SetOutlineColor(&word);
            word = static_cast<uint32_t>(GameByte(kAlphaKind10Cell)) << 24 | 0x400040u;
            mBrush->SetFillColor(&word);
            break;
        default: break;
        }
    }

    static bool Choose(bool value, uint32_t pass) { return pass == 1 ? !value : value; }

    // 0x371520: a drawn record of the drawer's turbo-rect flag, filled when its kind's test (inverted for pass 1)
    // holds. The original also has a kind 0x40000000 (always) that no sweep reaches (Draw emulates it, below).
    void Record(const DynamicRectRecord &record, uint32_t kind, uint32_t pass) const
    {
        if (!record.mDrawn) return;
        bool value;
        switch (kind) {
        case kOutlineFlag20: value = record.mFlag20 != 0; break;
        case kOutlineLast: value = record.mFrame == mFrame - 1; break;
        case kOutlineFlag1c: value = record.mFlag1c != 0; break;
        case kOutlineFlag1d: value = record.mFlag1d != 0; break;
        case kOutlineNew: value = record.mFlag20 && !record.mFlag1f; break;
        default: return;
        }
        if (Choose(value, pass) && record.mSecond == mSecond) Rect(record.mRect);
    }

    // 0x3714e0: the table's records seen this frame or the last.
    void Records(uint32_t kind, uint32_t pass) const
    {
        for (const DynamicRectRecord &record : mTable->mRecords)
            if (mFrame - record.mFrame <= 1) Record(record, kind, pass);
    }

    // 0x3715b0: a helper map's value filled: kind 1 when not seen this frame (its word 4), kind 2 by its byte at +0x14.
    void MapValue(const DynamicWords24 &value, uint32_t kind, uint32_t pass) const
    {
        bool chosen;
        if (kind == kOutlineLast) chosen = value.w[4] != mFrame;
        else if (kind == kOutlineFlag1c) chosen = static_cast<uint8_t>(value.w[5]) != 0;
        else return;
        if (Choose(chosen, pass)) Rect(value.w);
    }

    // 0x371930: kind 1 through the last frame's map, the others through the frame's (in the maps' list order).
    void Map(uint32_t kind, uint32_t pass) const
    {
        const DynamicStrategyHelper::Map &map = *(kind == kOutlineLast ? mHelper->mMap10 : mHelper->mMap14);
        for (const auto &entry : map) MapValue(entry.second, kind, pass);
    }

    // 0x3718f0: a kind's colours, then its records and map values.
    void Kind(uint32_t kind, uint32_t pass) const
    {
        Colour(kind);
        if (mTable) Records(kind, pass);
        if (mHelper) Map(kind, pass);
    }

    // 0x3717e0: a sweep, kind by kind. Kind 0x40000000 (every record) is two kind 2 sweeps of opposite passes.
    void Draw(uint32_t flags, uint32_t pass) const
    {
        if (flags & kOutlineAll) {
            Colour(kOutlineAll);
            if (mTable) {
                Records(kOutlineFlag1c, pass == 0 ? 1u : 0u);
                Records(kOutlineFlag1c, pass);
            }
        }
        for (const uint32_t kind : {kOutlineFlag1c, kOutlineFlag1d, kOutlineLast, kOutlineFlag20, kOutlineNew})
            if (flags & kind) Kind(kind, pass);
        if (flags & kOutlineRects) {
            Colour(kOutlineRects);
            for (const DynamicWords16 &rect : *mRects) Rect(rect.w);
        }
        if (flags & kOutlinePixels) {
            Colour(kOutlinePixels);
            Rect(mPixels);
        }
        if (flags & kOutlineBounds) {
            Colour(kOutlineBounds);
            Rect(mBounds);
        }
    }
};

cIDraw2D *Draw2D() { return static_cast<cIDraw2D *>(nGZGraphic4::GetDraw2DCached()); }

} // namespace

// 0x371670: nothing on the first two dirty-rect frames (the frame state's count 0 or 1). Otherwise, with the outline
// brush (+0x14) at depth 0.5, dirtyRectsDebug 1 outlines this mode's kinds (dirty rects: kind 2; dirty surfaces: kinds
// 1, 2, the frame's rectangles and the pixel rectangle, and the bounds with the byte at +0x79), then nothing in pass 1;
// dirtyRectsDebug 2 sweeps with the flag cells (when enabled, set for the mode first: dirty surfaces adds every record
// and kind 4, pass 1 nothing). Other modes, or another frame-state mode with 2, draw nothing.
void cDisplayListDynamicRenderStrategy::DrawOutlines(DynamicStrategyState *state, DynamicStrategyTable *table,
                                                     DynamicStrategyHelper *helper, std::list<DynamicWords16> *rects,
                                                     const int32_t *bounds, const int32_t *pixels, bool second)
{
    if (state->mFrames == 0 || state->mFrames == 1) return;
    cIDraw2D *const draw = Draw2D();
    draw->PushClipOrigin(0, 0, false, 0.5f);
    auto *const brush = static_cast<nGZGraphic4::cIOutlineColorBrush *>(mDirector14.Get());
    draw->SetMaterial(brush);
    OutlineDrawer drawer{draw, brush, 1.0f, 1.0f, table, helper, rects, {}, {}, mFrame, static_cast<uint8_t>(second)};
    CopyRect(drawer.mBounds, bounds);
    CopyRect(drawer.mPixels, pixels);
    if (mDebugMode == 1) {
        uint32_t flags = 0;
        if (state->mMode == DynamicStrategyState::kDirtyRects) flags = kOutlineFlag1c;
        else if (state->mMode == DynamicStrategyState::kDirtySurfaces)
            flags = mByte79 ? 0x703u : 0x503u; // kinds 1 and 2, the rectangles, pixels (and bounds)
        drawer.Draw(flags, 0);
        drawer.Draw(0, 1);
    } else if (mDebugMode == 2 && (state->mMode == DynamicStrategyState::kDirtyRects ||
                                   state->mMode == DynamicStrategyState::kDirtySurfaces)) {
        uint32_t *const flags0 = GameAddress<uint32_t>(kOutlineFlags0Cell);
        uint32_t *const flags1 = GameAddress<uint32_t>(kOutlineFlags1Cell);
        if (GameByte(kOutlineCellsEnabled)) {
            *flags1 = 0;
            *flags0 = state->mMode == DynamicStrategyState::kDirtyRects ? kOutlineFlag1c
                                                                         : 0x40000507u | (mByte79 ? 0x200u : 0u);
        }
        drawer.Draw(*flags0, 0);
        drawer.Draw(*flags1, 1);
    }
    draw->PopClip();
}

// ---- the debug buffer's overlay (0x376840) ----

namespace {

// 0x374ce0: the states the overlay changed put back: no stencil, keep operations, depth test and writes on, every
// colour channel written.
void RestoreOverlayStates(nGZSceneGraph::Pass *pass)
{
    using namespace nGZGraphic4;
    auto stencil = Command<cStencilState>(0x16, GameAddress<void>(cStencilState::kVTable));
    stencil.mEnabled = 0;
    stencil.mReference = 0xffffffffu;
    Apply(pass->mDeviceState, &stencil);
    auto operation = Command<cStencilOperationState>(0x18, GameAddress<void>(cStencilOperationState::kVTable));
    Apply(pass->mDeviceState, &operation);
    auto depth = Command<cDepthTestState>(0x14, GameAddress<void>(cDepthTestState::kVTable));
    depth.mTestEnabled = 1;
    depth.mWriteEnabled = 1;
    Apply(pass->mDeviceState, &depth);
    auto colour = Command<cColorWriteEnableState>(7, GameAddress<void>(cColorWriteEnableState::kVTable));
    colour.mMask = 0x0f0f0f0fu;
    Apply(pass->mDeviceState, &colour);
}

constexpr uint32_t kOverlayColor = 0x40000080; // the colour brush's colour: blue, a quarter opaque

} // namespace

// 0x376840 (debugStencil, default on): when the buffer shown has the back buffer's size, neither multisampled and the
// resolution not reduced, the stencil marks drawn over it: the back buffer's colour with the buffer's depth-stencil as
// targets (and the pass's textures), the pixel rectangle filled with the colour brush where the stencil is not 0
// (colour writes 0x0f0f0f0f, no depth test), then the states put back.
void cDisplayListDynamicRenderStrategy::DrawDebugOverlay(nGZSceneGraph::Pass *pass, cITSRenderSurface *surface)
{
    using namespace nGZGraphic4;
    HeldSettings settings;
    if (!ReadBool(settings.Get(), "debugStencil", 1)) return;
    cITSRenderSurface *const backbuffer = mBackbuffer; // through the interface (the class is final)
    if (backbuffer->GetWidth() != surface->GetWidth()) return;
    if (backbuffer->GetHeight() != surface->GetHeight()) return;
    if (backbuffer->IsMultisampled() || surface->IsMultisampled() || mReducedResolution) return;

    cIDraw2D *const draw = Draw2D();
    draw->PushClipOrigin(0, 0, false, 1.0f);
    const uint32_t colour = kOverlayColor;
    static_cast<cIColorBrush *>(mDirector10.Get())->SetColor(0, &colour);
    draw->SetMaterial(static_cast<cIColorBrush *>(mDirector10.Get()));

    cISurface *const depthSurface = surface->GetDepthSurface();
    auto targets = Command<cRenderTargetsState>(0, cRenderTargetsState::VTable());
    targets.mTargets.surface[0] = backbuffer->GetColorSurface();
    targets.mDepthTarget = depthSurface;
    Apply(pass->mDeviceState, &targets);
    pass->mTextureCount = 1;
    pass->mTextures[0] = backbuffer->GetColorSurface();
    pass->mTextureRef94 = surface->GetDepthSurface();

    auto write = Command<cColorWriteEnableState>(7, GameAddress<void>(cColorWriteEnableState::kVTable));
    write.mMask = 0x0f0f0f0fu;
    Apply(pass->mDeviceState, &write);
    auto stencil = Command<cStencilState>(0x16, GameAddress<void>(cStencilState::kVTable));
    stencil.mEnabled = 1;
    stencil.mReference = 0xffffffffu;
    Apply(pass->mDeviceState, &stencil);
    auto function = Command<cStencilFunctionState>(0x17, GameAddress<void>(cStencilFunctionState::kVTable));
    function.mFunction = 2;
    function.mReference = 0;
    function.mMask = 0xffffffffu;
    Apply(pass->mDeviceState, &function);
    auto operation = Command<cStencilOperationState>(0x18, GameAddress<void>(cStencilOperationState::kVTable));
    Apply(pass->mDeviceState, &operation);
    auto depth = Command<cDepthTestState>(0x14, GameAddress<void>(cDepthTestState::kVTable));
    Apply(pass->mDeviceState, &depth);
    auto depthFunction = Command<cDepthTestFunction>(0x15, GameAddress<void>(cDepthTestFunction::kVTable));
    depthFunction.mFunction = 7;
    Apply(pass->mDeviceState, &depthFunction);

    draw->FillRect(mPixelRect[0], mPixelRect[1], mPixelRect[2] - mPixelRect[0], mPixelRect[3] - mPixelRect[1]);
    RestoreOverlayStates(pass);
    draw->PopClip();
}

// ---- the drawing paths (row TS1i, lane 261108s) ----

namespace {

// .data bytes (zero-filled; nothing in the image writes them) that send a dirty-rect frame from the third on back to
// the second: 0x374670's and 0x3743e0's.
constexpr uint32_t kPathRestartCell = 0x10d9d18, kOwnPathRestartCell = 0x10d9d19;
// The frame counts the paths test: the first frame (draw everything), the second, and from the third on.
constexpr uint32_t kFirstFrame = 0, kSecondFrame = 1, kDirtyFrames = 2;
// The clears' flags (D3DCLEAR_ZBUFFER, D3DCLEAR_STENCIL; with D3DCLEAR_TARGET's neighbour 2 the colour-less pair).
constexpr uint32_t kClearDepth = 2, kClearStencil = 4;
// The modes of DrawItemsBy Present draws with: the late items (post-processing).
constexpr uint32_t kModeLate = 7;

// The frame's restart: with the cell set, a frame count of two or more set back to one (the second frame).
void RestartFrames(uint32_t cell, DynamicStrategyState *state)
{
    if (*GameAddress<const uint8_t>(cell) && state->mFrames >= kDirtyFrames) state->mFrames = kSecondFrame;
}

} // namespace

// 0x372150 (ret 4): the record at the item's slot, compared unsigned against the records' count.
DynamicRectRecord *DynamicStrategyTable::Record(const nGZSceneGraph::DrawItem *item)
{
    const uint32_t index = item->mUserData;
    return index < mRecords.size() ? &mRecords[index] : nullptr;
}

// 0x373a70 (ret 0xc): both lists sized to `count` (new entries null), then item i and its record (null past the
// table's records) stored at i. The count is compared signed in the loop, as the original's.
void cDisplayListDynamicRenderStrategy::ListItems(nGZSceneGraph::Pass *, nGZSceneGraph::DrawItem *items, uint32_t count)
{
    mItems.resize(count);
    mItemRecords.resize(count);
    for (int32_t i = 0; i < static_cast<int32_t>(count); ++i) {
        mItems[i] = &items[i];
        mItemRecords[i] = mState->mTable->Record(&items[i]);
    }
}

// 0x371150: in the dirty-rect drawing with registered items, mFinal's colour surface (the whole of it, height asked
// first) stretch-copied into each item's surface and rectangle when the surface exists and a copy is pending: done
// (and no longer pending unless kept), or failed.
void cDisplayListDynamicRenderStrategy::PresentRegistered()
{
    if (!mDirtyRects || mRegistered.empty()) return;
    const uint32_t height = mFinal->GetHeight(); // receiver: cTSRenderSurfaceImpl (0x15e550, ret 0)
    const uint32_t width = mFinal->GetWidth();   // receiver: cTSRenderSurfaceImpl (0x147d00, ret 0)
    const RECT whole{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    nGZGraphic4::cISurface *const colour = mFinal->GetColorSurface(); // receiver: cTSRenderSurfaceImpl (0x149cf0, ret 0)
    for (void *const entry : mRegistered) {
        DynamicRegisteredItem *const item = static_cast<DynamicRegisteredItem *>(entry);
        if (!item->mSurface->IsCreated()) continue; // receiver: cISurface (shared_vf_1624f0, slot 6, ret 0)
        if (!item->mPending) continue;
        // receiver: cISurface (shared_vf_161120, slot 17, ret 0xc)
        if (item->mSurface->StretchRect(reinterpret_cast<const RECT *>(item->mRect), colour, &whole)) {
            item->mDone = 1;
            if (!item->mKeep) item->mPending = 0;
        } else {
            item->mFailed = 1;
        }
    }
}

// 0x371240: nothing when both targets are one. Without the turbo rect, or before the dirty-rect drawing's third frame
// (the count compared unsigned), the whole of mTarget copied (CopyFrom). Otherwise its depth, then the colour of each
// turbo rectangle, scaled as ClearRects scales (by the second target's scale times its aspect, unless 1).
void cDisplayListDynamicRenderStrategy::CopyToSecond()
{
    if (mTarget == mTargetSecond) return;
    if (!mTurbo || mState->mFrames < kDirtyFrames) {
        mTargetSecond->CopyFrom(mTarget); // receiver: cTSRenderSurfaceImpl (0x371210, ret 4)
        return;
    }
    mTargetSecond->CopyDepth(mTarget);                // receiver: cTSRenderSurfaceImpl (0x371120, ret 4)
    const float aspect = mTargetSecond->GetAspect();     // receiver: cITSRenderSurface (folded 0x18c7a0 / 0x372660)
    const float targetScale = mTargetSecond->GetScale(); // receiver: cITSRenderSurface (folded 0x153f90)
    const float scale = static_cast<float>(static_cast<double>(targetScale) * static_cast<double>(aspect));
    if (scale == 1.0f) {
        for (const DynamicWords16 &r : mTurboRects) // receiver: cTSRenderSurfaceImpl (0x371010, ret 8)
            mTargetSecond->CopyRect(mTarget, reinterpret_cast<const RECT *>(r.w));
        return;
    }
    for (const DynamicWords16 &r : mTurboRects) {
        int32_t scaled[4];
        for (unsigned k = 0; k < 4; ++k)
            scaled[k] = static_cast<int32_t>(static_cast<float>(static_cast<int32_t>(r.w[k])) * scale + 0.5f);
        mTargetSecond->CopyRect(mTarget, reinterpret_cast<const RECT *>(scaled)); // receiver: cTSRenderSurfaceImpl (0x371010, ret 8)
    }
}

// 0x373f00 (ret 4): with post-processing, the second target's colour copied into the post-processing surface, mFinal's
// colour surface made the pass's only target and the late items drawn into it. Otherwise mFinal applied and the second
// target's colour copied into it; first, in the dirty-rect drawing with the device's presentWorkaround setting and
// mFinal the back buffer, one pixel filled at (0, 0) with a new colour brush (depth 0.5).
void cDisplayListDynamicRenderStrategy::Present(nGZSceneGraph::Pass *pass)
{
    if (mPostProcessing) {
        mTargetPost->CopyColor(mTargetSecond, mReducedResolution != 0); // receiver: cTSRenderSurfaceImpl (0x3710a0, ret 8)
        pass->mColorSurface = mFinal->GetColorSurface(); // receiver: cTSRenderSurfaceImpl (0x149cf0, ret 0)
        pass->mDepthSurface = nullptr;
        mFinal->Apply(pass, false); // receiver: cTSRenderSurfaceImpl (0x3751f0, ret 8)
        DrawItemsBy(pass, kModeLate);
        return;
    }
    mFinal->Apply(pass, false); // receiver: cTSRenderSurfaceImpl (0x3751f0, ret 8)
    if (mDirtyRects) {
        // receiver: cDeviceDX (shared 0x1674b0, slot 16), cDeviceEnumDX (0x177fb0, slot 22), cGZPropertySetString
        // (slot 9, through ReadBool)
        auto *const properties =
            static_cast<cIDeviceEnum *>(nGZGraphic4::GetCurrentDeviceCached()->GetDeviceEnumConst())->GetProperties();
        if (ReadBool(properties, "presentWorkaround", 0) &&
            mFinal == static_cast<cITSRenderSurface *>(mBackbuffer)) {
            cIDraw2D *const draw = Draw2D();
            nGZGraphic4::cIColorBrush *brush = nullptr;
            // receiver: cIGZCOM (slot 3); the result is not checked, as the original's
            Com()->GetClassObject(kDirector10Class, kDirector10Iid, reinterpret_cast<void **>(&brush));
            brush->Resize(1);               // receiver: nGZGraphic4::cColorBrush (0x153fe0, ret 4)
            draw->SetMaterial(brush);       // receiver: nGZGraphic4::cDraw2D (0x151ad0)
            draw->PushClipOrigin(0, 0, false, 0.5f); // receiver: nGZGraphic4::cDraw2D (0x151350)
            draw->FillRect(0, 0, 1, 1);     // receiver: nGZGraphic4::cDraw2D (0x14f0b0)
            draw->PopClip();                // receiver: nGZGraphic4::cDraw2D (0x151180)
            draw->SetMaterial(nullptr);     // receiver: nGZGraphic4::cDraw2D (0x151ad0)
            if (brush) brush->Release();    // receiver: nGZGraphic4::cColorBrush (cIGZUnknown slot 2)
        }
    }
    mFinal->CopyColor(mTargetSecond, mReducedResolution != 0); // receiver: cTSRenderSurfaceImpl (0x3710a0, ret 8)
}

// 0x374bb0 (ret 8): the side's bounds and record marks (tested from the third dirty-rect frame, by the turbo rect).
// The second side with no rectangles is done there when the turbo rect is off from the third frame. Before the third
// frame the side's items are drawn whole. Otherwise: with the turbo rect, the first side clears mTarget's stencil
// (outside the debug modes above 3); then, with rectangles, the side's scissor set up, the turbo second side's target
// stencil cleared in the dirty-rect drawing, the rectangles cleared, the side's items drawn and the scissor turned off.
void cDisplayListDynamicRenderStrategy::DrawSide(nGZSceneGraph::Pass *pass, uint8_t which)
{
    const uint32_t mode = static_cast<uint32_t>(which) + 1;
    std::list<DynamicWords16> *const rects = which ? &mTurboRects : &mRects;
    ComputeBounds(rects, which);
    MarkRecords(rects, pass, mTurbo != 0, mState->mFrames >= kDirtyFrames, which);
    if (which && rects->empty() && mState->mFrames >= kDirtyFrames && !mTurbo) return;
    if (mState->mFrames < kDirtyFrames) {
        DrawItemsBy(pass, mode);
        return;
    }
    if (mTurbo && !which && mDebugMode <= kDebugModeClears)
        mTarget->Clear(pass, kClearStencil, 0.0f, 1); // receiver: cTSRenderSurfaceImpl (0x370f50, ret 0x10)
    if (rects->empty()) return;
    const bool scissor = SetUpScissor(pass, which);
    if (mDirtyRects && which && mTurbo)
        mTargetSecond->Clear(pass, kClearStencil, 0.0f, 1); // receiver: cTSRenderSurfaceImpl (0x370f50, ret 0x10)
    ClearRects(pass, rects, which);
    DrawItemsBy(pass, mode);
    if (scissor) DisableScissor(pass);
}

// 0x374670 (ret 0xc): the frame's restart (cell 0x10d9d18) first. Without the zeroFrameRectUpdate setting and with
// the censor's dirty rectangles gathered (CollectDirtyRects answering 0), the first frame is drawn whole: the target
// set up (nothing more when it is not valid), the early items drawn into it, then either through post-processing (the
// second target's colour copied into the post-processing surface unless they are one, mFinal's colour surface the
// pass's only target, the late items) or Present; the registered items' copies last. Otherwise (and on the second
// frame, which clears mRedraw first) the dirty-rect frame: the rectangles, the item list, the censor, the target set
// up (nothing more when it is not valid), the first side drawn into mTarget, the copy into the second target, the
// second side drawn into it, Present and the registered items' copies.
void cDisplayListDynamicRenderStrategy::DrawPath(nGZSceneGraph::Pass *pass, nGZSceneGraph::DrawItem *items,
                                                 uint32_t count)
{
    RestartFrames(kPathRestartCell, mState);
    HeldSettings settings;
    const bool zeroFrame = ReadBool(settings.Get(), "zeroFrameRectUpdate", 0) != 0;
    bool whole = false;
    if (!zeroFrame) {
        std::vector<DynamicWords16> censorRects;
        whole = mCensor->CollectDirtyRects(pass, &censorRects) == 0;
    }
    if (whole) {
        const uint32_t frames = mState->mFrames;
        if (frames == kFirstFrame) {
            SetUpTarget(pass); // receiver: cDisplayListDynamicRenderStrategy (primary slot 6, 0x374dd0; ours)
            if (!mTarget->IsValid()) return; // receiver: cTSRenderSurfaceImpl (0x156110 / 0x3736e0 / 0x373720, ret 0)
            mTarget->Apply(pass, false);     // receiver: cTSRenderSurfaceImpl (0x3751f0, ret 8)
            DrawItemRun(pass, items, count, 0);
            if (mPostProcessing) {
                if (mTargetPost != mTargetSecond) // receiver: cTSRenderSurfaceImpl (0x3710a0, ret 8)
                    mTargetPost->CopyColor(mTargetSecond, mReducedResolution != 0);
                pass->mColorSurface = mFinal->GetColorSurface(); // receiver: cTSRenderSurfaceImpl (0x149cf0, ret 0)
                pass->mDepthSurface = nullptr;
                mFinal->Apply(pass, false); // receiver: cTSRenderSurfaceImpl (0x3751f0, ret 8)
                DrawItemRun(pass, items, count, 1);
            } else {
                Present(pass);
            }
            PresentRegistered();
            return;
        }
        if (frames == kSecondFrame) mRedraw = 0;
    }
    UpdateDirtyRects(pass, items, count, mRedraw != 0);
    ListItems(pass, items, count);
    DrawCensor(pass);
    SetUpTarget(pass);               // receiver: cDisplayListDynamicRenderStrategy (primary slot 6, 0x374dd0; ours)
    if (!mTarget->IsValid()) return; // receiver: cTSRenderSurfaceImpl (0x156110 / 0x3736e0 / 0x373720, ret 0)
    mTarget->Apply(pass, false);     // receiver: cTSRenderSurfaceImpl (0x3751f0, ret 8)
    DrawSide(pass, 0);
    CopyToSecond();
    mTargetSecond->Apply(pass, false); // receiver: cTSRenderSurfaceImpl (0x3751f0, ret 8)
    DrawSide(pass, 1);
    Present(pass);
    PresentRegistered();
}

// 0x3743e0 (ret 0xc): the type 3 reflection's path. The frame's restart (cell 0x10d9d19); the whole-surface viewport
// (0, 0, 1.0, 1.0) and the pass's kept in mViewport (the pass's corners, the whole one's depth range); the rectangles,
// item list, censor and target as the other path (the target's validity not tested). Before the third frame: every
// record marked, scissoring on, the depth and stencil cleared to 1 / 0, the reflection's own items drawn under the
// whole viewport. From the third: the bounds clipped to the pass's pixels, the records marked in them when they are
// not empty, and nothing more when they are empty or there are no rectangles; else scissoring, the depth and stencil
// cleared to 0, the rectangles cleared, the own items drawn under the whole viewport. Then (both) the pass's viewport,
// the depth cleared to 1, the other items, and the depth and stencil cleared to 1 without a scissor or the scissor
// turned off.
void cDisplayListDynamicRenderStrategy::DrawOwnPath(nGZSceneGraph::Pass *pass, nGZSceneGraph::DrawItem *items,
                                                    uint32_t count)
{
    using nGZGraphic4::cViewportState;
    RestartFrames(kOwnPathRestartCell, mState);
    // ORIGINAL-BUG: the original leaves the whole viewport's depth range (+0x18 / +0x1c) as the stack held it, so the
    // device gets (and mViewport keeps) leftover words there; ours are 0.
    cViewportState whole = Command<cViewportState>(kViewportId, cViewportState::VTable());
    whole.mX = 0;
    whole.mY = 0;
    whole.mWidth = kOneBits;
    whole.mHeight = kOneBits;
    mViewport.mId = kViewportId;
    mViewport.mSize = static_cast<uint16_t>(sizeof(cViewportState));
    std::memcpy(&mViewport.mX, pass->mViewportMin, 4 * sizeof(uint32_t)); // the corners, +0x84 / +0x8c, as one copy
    mViewport.mMinZ = whole.mMinZ;
    mViewport.mMaxZ = whole.mMaxZ;

    UpdateDirtyRects(pass, items, count, mRedraw != 0);
    ListItems(pass, items, count);
    DrawCensor(pass);
    SetUpTarget(pass); // receiver: cDisplayListDynamicRenderStrategy (primary slot 6, 0x374dd0; ours)
    ComputeBounds(&mRects, 0);
    const bool early = mState->mFrames < kDirtyFrames;
    if (early) {
        MarkRecords(&mRects, pass, false, false, 0);
    } else {
        nGZGraphic4::cDraw2D::Intersect(reinterpret_cast<cGZRect *>(mBounds), nullptr,
                                        reinterpret_cast<const cGZRect *>(mBounds),
                                        reinterpret_cast<const cGZRect *>(mPixelRect));
        // The bounds tested again after the marks, as the original reads them.
        const auto some = [this] { return mBounds[2] > mBounds[0] && mBounds[3] > mBounds[1]; };
        if (some()) MarkRecords(&mRects, pass, true, mState->mFrames >= kDirtyFrames, 0);
        if (!some() || mRects.empty()) return;
    }
    mUseScissoring = 1;
    const bool scissor = SetUpScissor(pass, 0);
    // receiver: cTSRenderSurfaceImpl (0x370f50, ret 0x10)
    mTarget->Clear(pass, kClearDepthStencil, early ? 1.0f : 0.0f, 0);
    if (!early) ClearRects(pass, &mRects, 0);
    Apply(pass->mDeviceState, &whole); // receiver: cIDeviceState (slot 12, ApplyStateCommand)
    DrawReflectionItems(pass, 1);
    Apply(pass->mDeviceState, &mViewport); // receiver: cIDeviceState (slot 12, ApplyStateCommand)
    mTarget->Clear(pass, kClearDepth, 1.0f, 0); // receiver: cTSRenderSurfaceImpl (0x370f50, ret 0x10)
    DrawReflectionItems(pass, 0);
    if (!scissor) mTarget->Clear(pass, kClearDepthStencil, 1.0f, 0); // receiver: cTSRenderSurfaceImpl (0x370f50, ret 0x10)
    else DisableScissor(pass);
}

void cDisplayListDynamicRenderStrategy::Invalidate()
{
    mInvalidated = 1;
    static_cast<cITSDisplayListRenderStrategy *>(mCensor.Get())->Invalidate();
}

void cDisplayListDynamicRenderStrategy::AddBox(const nGZCompGeom::BoundBox *box)
{
    static_cast<cITSDisplayListRenderStrategy *>(mCensor.Get())->AddBox(box);
}

void cDisplayListDynamicRenderStrategy::Register(void *item)
{
    if (std::find(mRegistered.begin(), mRegistered.end(), item) == mRegistered.end()) mRegistered.push_back(item);
}

void cDisplayListDynamicRenderStrategy::Unregister(void *item)
{
    const auto found = std::find(mRegistered.begin(), mRegistered.end(), item);
    if (found != mRegistered.end()) mRegistered.erase(found);
}

// 0x375b00: nothing without a ready device. The first time, the surfaces are made and named (the censor strategy is
// handed the censor texture at its +0x28). Nothing more while the back buffer's multisample type is changing. Otherwise
// the back buffer gets the device and its antialiasing quality; when it has a size, the dirty-rect surfaces get it times
// the debugRenderRes scale (1 with any antialiasing of the back buffer or the targets; the default is 2 with
// supersampling) and the targets the antialiasing quality, the reflection and post-effects textures the size times the
// scale when it shrinks, the censor texture a tenth of that, the last texture nothing; then UpdateSurfaces.
void cDisplayListDynamicRenderStrategy::Slot8()
{
    cIDeviceDX *const device = nGZGraphic4::GetCurrentDeviceCached();
    if (!device || !device->IsDeviceReady()) return;

    if (!mDirtyTexture1) {
        mBackbuffer = new cTSRenderBackbufferSurface;
        mBackbuffer->Init("BackbufferTarget", true);
        mDirtyTarget1 = new cTSRenderTargetSurface;
        mDirtyTarget2 = new cTSRenderTargetSurface;
        mDirtyTexture1 = new cTSRenderTextureSurface;
        mDirtyTexture2 = new cTSRenderTextureSurface;
        mDirtyTarget1->Init("DirtyRectTarget1", true);
        mDirtyTarget2->Init("DirtyRectTarget2", true);
        mDirtyTexture1->Init("", "DirtyRectTexture1", false, true, false);
        mDirtyTexture2->Init("", "DirtyRectTexture2", false, true, false);
        mScreenReflection = new cTSRenderTextureSurface;
        mPoolReflection = new cTSRenderTextureSurface;
        mOceanReflection = new cTSRenderTextureSurface;
        mPostEffects = new cTSRenderTextureSurface;
        mCensorTexture = new cTSRenderTextureSurface;
        mScreenReflection->Init("", "ScreenReflection", true, true, false);
        mPoolReflection->Init("", "PoolReflection", true, true, false);
        mOceanReflection->Init("", "OceanReflection", true, true, false);
        mPostEffects->Init("postprocessingmaterial", "@@PostEffectsTexture@@", false, true, true);
        mCensorTexture->Init("CensorMaterial", "CensorTexture", false, true, false);
        mTexture130 = new cTSRenderTextureSurface;
        mCensor->mTexture = mCensorTexture;
    }
    if (mBackbuffer->InMultisampleChange()) return;

    ScopedRef settings{nullptr};
    AcquireSettings(settings);
    const uint32_t support = ReadUint32(settings.mObject, "antialiasingSupport", kSupportBackbuffer);
    const uint32_t quality = ReadUint32(settings.mObject, "antialiasingQuality", 0);
    const Antialiasing aa(support, quality);
    const uint32_t defaultScale = aa.supersample == 0 ? 1 : 2;
    mBackbuffer->Configure(device, aa.backbuffer);

    const uint32_t width = mBackbuffer->GetWidth(), height = mBackbuffer->GetHeight();
    if (width != 0 && height != 0) {
        float scale = ReadFloat(settings.mObject, "debugRenderRes",
                                static_cast<float>(static_cast<double>(defaultScale)));
        if (aa.backbuffer != 0 || aa.targets != 0) {
            scale = 1.0f;
            mReducedResolution = 0;
        } else {
            mReducedResolution = !(scale == 1.0f);
        }
        const float widthF = static_cast<float>(static_cast<int32_t>(width));
        const float heightF = static_cast<float>(static_cast<int32_t>(height));
        const uint32_t scaledWidth = static_cast<uint32_t>(static_cast<int32_t>(widthF * scale));
        const uint32_t scaledHeight = static_cast<uint32_t>(static_cast<int32_t>(heightF * scale));
        mDirtyTarget1->Configure(device, scaledWidth, scaledHeight, aa.targets);
        mDirtyTarget2->Configure(device, scaledWidth, scaledHeight, aa.targets);
        mDirtyTexture1->Configure(device, scaledWidth, scaledHeight);
        mDirtyTexture2->Configure(device, scaledWidth, scaledHeight);
        mDirtyTarget1->SetScale(scale);
        mDirtyTarget2->SetScale(scale);
        mDirtyTexture1->SetScale(scale);
        mDirtyTexture2->SetScale(scale);

        // The other textures only ever shrink.
        const bool shrinks = 1.0f > scale;
        const uint32_t otherWidth = shrinks ? static_cast<uint32_t>(static_cast<int32_t>(widthF * scale)) : width;
        const uint32_t otherHeight = shrinks ? static_cast<uint32_t>(static_cast<int32_t>(heightF * scale)) : height;
        const float otherScale = shrinks ? scale : 1.0f;
        SizeAndScale(mScreenReflection, device, otherWidth, otherHeight, otherScale);
        if (WaterReflections()) {
            SizeAndScale(mPoolReflection, device, otherWidth, otherHeight, otherScale);
            SizeAndScale(mOceanReflection, device, otherWidth, otherHeight, otherScale);
        }
        mPostEffects->Configure(device, otherWidth, otherHeight);
        const uint32_t censorHeight =
            ToUnsigned(static_cast<float>(static_cast<int32_t>(otherHeight)) * kCensorScale);
        const uint32_t censorWidth = ToUnsigned(static_cast<float>(static_cast<int32_t>(otherWidth)) * kCensorScale);
        SizeAndScale(mCensorTexture, device, censorWidth, censorHeight, otherScale * kCensorScale);
        mTexture130->Configure(device, 0, 0);
        UpdateSurfaces(device);
    }
    settings.ReleaseObject();
}

// 0x372a50: the back buffer and the censor texture are always created. With the dirty-rect strategy
// (dynamicRenderStrategy 2, and no multisampled back buffer: that destroys the dirty-rect surfaces and turns the
// strategy off), the render textures (useRenderTextures) or the targets are created, the second one only with the turbo
// rect (useTurboRect, as the antialiasing allows, and when the device can), and they become the current pair (each
// dropped when not created and valid). Otherwise one surface of the kind (the first) is created when something needs a
// copy of the frame (antialiasing quality, no depth on the back buffer, post-processing, a reduced resolution) and the
// back buffer is not antialiased, and it is the current one. The post-effects texture is always created; the reflection
// textures (the pool and ocean ones only when the water reflects) as the reflection settings say.
void cDisplayListDynamicRenderStrategy::UpdateSurfaces(cIDeviceDX *device)
{
    ScopedRef settings{nullptr};
    AcquireSettings(settings);
    const uint32_t quality = ReadUint32(settings.mObject, "antialiasingQuality", 0);
    const uint32_t support = ReadUint32(settings.mObject, "antialiasingSupport", kSupportBackbuffer);
    const bool anyQuality = quality != 0;
    bool dirtyRects = ReadUint32(settings.mObject, "dynamicRenderStrategy", 0) == kDynamicStrategyDirty;
    uint8_t turbo = ReadBool(settings.mObject, "useTurboRect");
    const bool extraViewer = ReadBool(settings.mObject, "reflectionWithExtraViewer") != 0;
    const bool nhoodWater = ReadBool(settings.mObject, "nhoodWaterReflection") != 0;
    const bool reflections = extraViewer || nhoodWater;
    const uint8_t postProcessing = ReadBool(settings.mObject, "enablePostProcessing");
    const uint8_t renderTextures = ReadBool(settings.mObject, "useRenderTextures");
    const Antialiasing aa(support, quality);

    if (turbo && (aa.targets != 0 || aa.supersample != 0)) {
        if (aa.supersample != 0) turbo = 1;
        if (aa.targets == 1 || aa.targets == 2) turbo = support == kSupportTargetsTurbo ? static_cast<uint8_t>(quality) : turbo;
        else if (aa.targets >= 3) turbo = 0;
    }

    if (mBackbuffer->IsMultisampled()) {
        dirtyRects = false;
        mDirtyTexture1->Destroy();
        mDirtyTexture2->Destroy();
        mDirtyTarget1->Destroy();
        mDirtyTarget2->Destroy();
    }
    mBackbuffer->Create();
    mCensorTexture->Create();

    if (dirtyRects) {
        if (turbo) turbo = TurboRectSupported(device);
        if (!renderTextures) {
            mDirtyTexture1->Destroy();
            mDirtyTexture2->Destroy();
            mDirtyTarget1->Create();
            CreateIf(mDirtyTarget2, turbo != 0);
            mCurrent = mDirtyTarget1;
            mCurrentSecond = turbo ? mDirtyTarget2 : nullptr;
        } else {
            mDirtyTarget1->Destroy();
            mDirtyTarget2->Destroy();
            mDirtyTexture1->Create();
            CreateIf(mDirtyTexture2, turbo != 0);
            mCurrent = mDirtyTexture1;
            mCurrentSecond = turbo ? mDirtyTexture2 : nullptr;
        }
        if (turbo && (!mCurrentSecond->IsCreated() || !mCurrentSecond->IsValid())) mCurrentSecond = nullptr;
        // ORIGINAL-BUG: the current surface's validity is read from the second dirty-rect texture (+0x110), whichever
        // surface is current.
        if (!mCurrent->IsCreated() || !static_cast<cITSRenderSurface *>(mDirtyTexture2)->IsValid()) mCurrent = nullptr;
    } else {
        const bool needed = anyQuality || !mBackbuffer->HasDepth() || postProcessing || mReducedResolution;
        const bool use = aa.backbuffer == 0 ? needed : false;
        cITSRenderSurface *current;
        if (!renderTextures) {
            CreateIf(mDirtyTarget1, use);
            mDirtyTarget2->Destroy();
            mDirtyTexture1->Destroy();
            mDirtyTexture2->Destroy();
            current = use ? mDirtyTarget1 : nullptr;
        } else {
            mDirtyTarget1->Destroy();
            mDirtyTarget2->Destroy();
            if (use) {
                mDirtyTexture1->Create();
                mDirtyTarget2->Destroy();
                current = mDirtyTexture1;
            } else {
                mDirtyTexture2->Destroy();
                mDirtyTarget2->Destroy();
                current = nullptr;
            }
        }
        mCurrentSecond = nullptr;
        mCurrent = current;
    }

    mPostEffects->Create();
    const bool water = WaterReflections();
    if (reflections) {
        mScreenReflection->Create();
        if (water) {
            mPoolReflection->Create();
            mOceanReflection->Create();
        }
    } else {
        mScreenReflection->Destroy();
        if (water) {
            mPoolReflection->Destroy();
            mOceanReflection->Destroy();
        }
    }
    settings.ReleaseObject();
}

bool cDisplayListDynamicRenderStrategy::TurboRectSupported(cIDeviceDX *device)
{
    // receiver: nGZGraphic4::cPixelFormatsManagerDX (GetDepthStencil 0x15f720, ret 0x10)
    auto *const depthFormat = static_cast<cIPixelFormat *>(
        RawSlot<11, void *>(nGZGraphic4::GetPixelFormatsManagerCached(), 0x20u, 0x18u, 8u, 0u));
    auto *const colorFormat = static_cast<cIPixelFormat *>(
        static_cast<ifaces::cIPixelFormatsManager *>(nGZGraphic4::GetPixelFormatsManagerCached())->GetARGB(0x20, 0, 8, 8, 8));
    const bool depthOk = static_cast<cIDeviceEnum *>(device->GetDeviceEnumConst())
                             ->IsDepthFormatSupportedForDisplay(depthFormat, kDepthUsage, colorFormat);
    const bool stencil = As<ifaces::cIPixelFormatDepthStencil>(depthFormat)->GetStencilBits() != 0;
    if (!(stencil && depthOk)) return false;
    const nGZGraphic4::cSurfaceExtent size{kTurboSide, kTurboSide};
    const RECT rect{0, 0, static_cast<LONG>(kTurboSide), static_cast<LONG>(kTurboSide)};
    return static_cast<cIDeviceEnum *>(device->GetDeviceEnumConst())
        ->CanStretchRect(kDepthUsage, &size, &rect, depthFormat, kDepthUsage, &size, &rect, depthFormat);
}

// ---- the hooks' entry points ------------------------------------------------------------------------------------------

cDisplayListDynamicRenderStrategy *__fastcall cDisplayListDynamicRenderStrategyAdapters::Construct(Self *memory, void *)
{
    return new (memory) cDisplayListDynamicRenderStrategy;
}

cITSDisplayListRenderStrategy *__cdecl cDisplayListDynamicRenderStrategyAdapters::Create()
{
    cDisplayListDynamicRenderStrategy *const made = new cDisplayListDynamicRenderStrategy;
    return made;
}

void __fastcall cDisplayListDynamicRenderStrategyAdapters::DisableScissor(Self *self, void *, nGZSceneGraph::Pass *pass)
{
    self->DisableScissor(pass);
}

void __fastcall cDisplayListDynamicRenderStrategyAdapters::MarkRecords(Self *self, void *,
                                                                       const std::list<DynamicWords16> *rects,
                                                                       nGZSceneGraph::Pass *pass, uint32_t turbo,
                                                                       uint32_t test, uint32_t which)
{
    self->MarkRecords(rects, pass, static_cast<uint8_t>(turbo) != 0, static_cast<uint8_t>(test) != 0,
                      static_cast<uint8_t>(which));
}

void __fastcall cDisplayListDynamicRenderStrategyAdapters::ComputeBounds(Self *self, void *,
                                                                         const std::list<DynamicWords16> *rects,
                                                                         uint32_t which)
{
    self->ComputeBounds(rects, static_cast<uint8_t>(which));
}

bool __fastcall cDisplayListDynamicRenderStrategyAdapters::SetUpScissor(Self *self, void *, nGZSceneGraph::Pass *pass,
                                                                        uint32_t which)
{
    return self->SetUpScissor(pass, static_cast<uint8_t>(which));
}

void __fastcall cDisplayListDynamicRenderStrategyAdapters::UpdateDirtyRects(Self *self, void *,
                                                                            nGZSceneGraph::Pass *pass,
                                                                            const nGZSceneGraph::DrawItem *items,
                                                                            uint32_t count, uint32_t useRecords)
{
    self->UpdateDirtyRects(pass, items, count, static_cast<uint8_t>(useRecords) != 0);
}

void __fastcall cDisplayListDynamicRenderStrategyAdapters::DrawItemRun(Self *self, void *, nGZSceneGraph::Pass *pass,
                                                                       nGZSceneGraph::DrawItem *items, uint32_t count,
                                                                       uint32_t late)
{
    self->DrawItemRun(pass, items, count, static_cast<uint8_t>(late));
}

void __fastcall cDisplayListDynamicRenderStrategyAdapters::DrawReflectionItems(Self *self, void *,
                                                                               nGZSceneGraph::Pass *pass,
                                                                               uint32_t reflection)
{
    self->DrawReflectionItems(pass, static_cast<uint8_t>(reflection));
}

void __fastcall cDisplayListDynamicRenderStrategyAdapters::ClearRects(Self *self, void *, nGZSceneGraph::Pass *pass,
                                                                      const std::list<DynamicWords16> *rects,
                                                                      uint32_t second)
{
    self->ClearRects(pass, rects, static_cast<uint8_t>(second));
}

DynamicStrategyState *__cdecl cDisplayListDynamicRenderStrategyAdapters::CreateState()
{
    return DynamicStrategyState::Create();
}

void __fastcall cDisplayListDynamicRenderStrategyAdapters::DestroyState(DynamicStrategyState *self, void *)
{
    self->~DynamicStrategyState();
}

// 0x36f5c0's hooked entry (one argument in ecx, the original's thiscall shape), so ClassesRound3's deleting destructor
// 0x36fbb0 (its own type for the same object) calls the hooked replacement directly.
void __fastcall DestroyDynamicStrategyState(void *state)
{
    cDisplayListDynamicRenderStrategyAdapters::DestroyState(static_cast<DynamicStrategyState *>(state), nullptr);
}

} // namespace nTSSG
