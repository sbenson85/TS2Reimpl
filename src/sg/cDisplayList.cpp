#include "sg/cDisplayList.h"

#include "game/game.h"
#include "gz/GraphicServices.h" // GetCurrentDeviceCached
#include "gz/cDeviceDX.h"
#include "gz/cDeviceStateDX.h" // cIDeviceState
#include "gz/cGZPropertyManager.h"
#include "gz/cIGZCOM.h"
#include "gz/cIGZMessage2Standard.h"
#include "gz/cGZPropertySet.h"  // gzany:: the value vtables
#include "gz/cIGZPropertySet.h" // GZServiceString
#include "gz/cVertexArrayDX.h"
#include "ifaces/cIGZFrameWork.h"
#include "misc/UtilitiesE.h"     // GetCachedQueriedObject
#include "rz/RZAutoRef.h"
#include "rz/RZFileStreams.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject, ScopedRef
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject
#include "sg/cDefaultDisplayListRenderStrategy.h"
#include "sg/cDisplayListLog.h"
#include "sg/cDynamicGeometryRect.h"
#include "sg/cIGeometryRender.h"
#include "sg/cMaterialManager.h" // cIMaterialManager
#include "sg/cShapeRefNode.h"    // cIShapeRefNode

#include <algorithm>
#include <cstring>
#include <new>

namespace nGZSceneGraph {

static_assert(sizeof(cIDisplayList) == 4, "the behaviour face at +0");

namespace {

constexpr uint32_t kVertexArrayClass = 0xbbfffce5; // cDynamicVertexComponentArrayDX
constexpr uint32_t kIndexArrayClass = 0x9c043807;  // cDynamicVertexIndexArrayDX
constexpr uint32_t kNewDevice = 1;                 // the device event's code for a new device
constexpr uint32_t kDefaultPasses = 2;
constexpr uint32_t kDefaultVertexKB = 600;
constexpr uint32_t kDefaultIndexKB = 150;
constexpr uint32_t kEndSceneStateReset = 8;       // EndScene's device state reset flags
constexpr uint32_t kNewPlacementWord = 0x1f;      // a new pass's viewer record: the placement's first word

// The five switches' setting names (the game's strings), in the order of kSwitchMessages.
constexpr uint32_t kSwitchNames[5] = {0xe752c8, 0xe752d8, 0xe752e8, 0xe752fc, 0xe75310};

ifaces::cIGZFrameWork *Framework() { return static_cast<ifaces::cIGZFrameWork *>(GetServiceObject()); }

// The settings service (the framework's 0x0ac1887e / 0x6bb22f38), a counted local.
ScopedRef Settings()
{
    ScopedRef settings{nullptr};
    if (ifaces::cIGZFrameWork *const framework = Framework())
        framework->GetSystemService(cDisplayList::kSettingsId, cDisplayList::kSettingsIid, &settings.mObject);
    return settings;
}

// The settings service's boolean default value (cAnyBoolean): the destructor stores the base's vtable.
struct AnyBoolean {
    const void *mVTable;
    bool mValue;
    explicit AnyBoolean(bool value) : mVTable(gzany::BooleanVTable()), mValue(value) {}
    AnyBoolean(const AnyBoolean &) = delete;
    AnyBoolean &operator=(const AnyBoolean &) = delete;
    ~AnyBoolean() { *static_cast<const void *volatile *>(&mVTable) = gzany::AnyVTable(); }
};

// A new dynamic array of class `classId` in `ref`, the old one released first.
void MakeArray(cDisplayList::Ref<nGZGraphic4::cIDynamicVertexArray> &ref, uint32_t classId, uint32_t iid)
{
    cIGZCOM *const com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    ref.Reset();
    com->GetClassObject(classId, iid, reinterpret_cast<void **>(&ref.p));
}


} // namespace

// ---- passes -------------------------------------------------------------------------------------------------------

Pass::Pass()
    : mOwner(nullptr), mFrame(0), mTime(0.0f), mViewer(nullptr), mRenderType(0), mLayerState{}, mLayerTagMatch(0),
      mPad1d{}, mTextureMode(0), mFrustum{}, mViewportMin{}, mViewportMax{}, mDepthRange{}, mColorSurface(nullptr),
      mDepthSurface(nullptr), mTextureCount(0), mTextures{}, mTextureRef94(nullptr), mHandleTexture(nullptr),
      mWidth(0), mHeight(0), mScale(0.0f), mPixelRect{}, mView{}, mViewInverse{}, mMatrices{}, mPrevious(nullptr),
      mRecord(nullptr), mLastShader(nullptr), mWords290{}, mDrawStats{}, mDevice(nullptr), mDeviceState(nullptr),
      mVertices1(nullptr), mVertices2(nullptr), mIndices1(nullptr), mIndices2(nullptr), mFrameCache(nullptr),
      mEntryCountA(0), mEntryCountB(0), mNodeCount(0), mViewerRecord{}
{
    for (int i = 0; i < 3; ++i) mView.mMatrix[i * 4] = mViewInverse.mMatrix[i * 4] = 1.0f;
    for (float(&matrix)[16] : mMatrices)
        for (int i = 0; i < 4; ++i) matrix[i * 5] = 1.0f;
    mViewerRecord.mPlacement.mUnknown0 = kNewPlacementWord;
}

// ---- construction -------------------------------------------------------------------------------------------------

cDisplayList::cDisplayList()
{
    mMatrix[0] = mMatrix[5] = mMatrix[10] = mMatrix[15] = 1.0f;
    if (nGZGraphic4::GetCurrentDeviceCached()) {
        MakeArray(mVertices1, kVertexArrayClass, nGZGraphic4::kIidDynamicVertexComponentArray);
        MakeArray(mVertices2, kVertexArrayClass, nGZGraphic4::kIidDynamicVertexComponentArray);
        MakeArray(mIndices1, kIndexArrayClass, nGZGraphic4::kIidDynamicVertexIndexArray);
        MakeArray(mIndices2, kIndexArrayClass, nGZGraphic4::kIidDynamicVertexIndexArray);
    }
    cDisplayList::SetRenderStrategy(nullptr);
    cDisplayList::SetPassCount(kDefaultPasses);
    cDisplayList::SetBufferSizes(kDefaultVertexKB, kDefaultIndexKB);
    uint32_t stats[kStats];
    cDisplayList::TakeStats(stats, 1);

    // The graphics system's device event (no null check on the system in the original).
    RZAutoRef graphics{nullptr};
    if (ifaces::cIGZFrameWork *const framework = Framework())
        framework->GetSystemService(kGraphicSystemId, kGraphicSystemIid, &graphics.mObject);
    static_cast<nGZEvent::cIAbstractBehavior *>(graphics.mObject)->Connect(kDeviceEvent, static_cast<cIDisplayList *>(this), kDeviceEvent);
    graphics.ReleaseObject();
}

// The members go in reverse order (the log, the strategy, the lists, the passes, the rect, the arrays).
cDisplayList::~cDisplayList() = default;

bool cDisplayList::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    switch (riid) {
    case cIDisplayList::kIid:
    case 1:
    case kIidBehavior2:
        *out = static_cast<cIDisplayList *>(this);
        AddRef();
        return true;
    case kIidBehaviorType:
        *out = static_cast<cIDisplayList *>(this);
        return true;
    }
    return false;
}

uint32_t cDisplayList::AddRef() { return Behavior::AddRef(); }

uint32_t cDisplayList::Release() { return Behavior::Release(); }

// ---- the device ---------------------------------------------------------------------------------------------------

void cDisplayList::OnEvent(uint32_t type, uint32_t arg1, void *data, uint32_t arg3)
{
    Behavior::OnEvent(type, arg1, data, arg3);
    if (type == kDeviceEvent) DeviceChanged(static_cast<const uint32_t *>(data));
}

void cDisplayList::DeviceChanged(const uint32_t *event)
{
    if (event[1] != kNewDevice) return;
    RecreateBuffers();
    mScreenRect.Assign(new cDynamicGeometryRect);
}

void cDisplayList::RecreateBuffers()
{
    mBuffersReady = 0;
    nGZGraphic4::cIDeviceDX *const device = nGZGraphic4::GetCurrentDeviceCached();
    if (!device || !device->IsDeviceReady()) return;
    // No null checks: the arrays exist whenever there is a device.
    mVertices1.p->ReleaseResource();
    mVertices2.p->ReleaseResource();
    mIndices1.p->ReleaseResource();
    mIndices2.p->ReleaseResource();
    if (mVertices1.p->Create(device, mVertexBytes, 1) && mVertices2.p->Create(device, mVertexBytes, 2) &&
        mIndices1.p->Create(device, mIndexBytes, 1) && mIndices2.p->Create(device, mIndexBytes, 2))
        mBuffersReady = 1;
}

// ---- the settings -------------------------------------------------------------------------------------------------

void cDisplayList::Init()
{
    mMaterialManager = static_cast<cIMaterialManager *>(GetCachedQueriedObject());
    ScopedRef settings = Settings();
    auto *const manager = static_cast<cIGZPropertyManagerString *>(settings.mObject);
    cIGZMessageTarget2 *const target = this;
    for (int i = 0; i < 5; ++i) {
        GZServiceString key(GameAddress<const char>(kSwitchNames[i]));
        AnyBoolean value(true);
        // ORIGINAL-BUG: the settings service is used without a null check.
        manager->AddNotification(target, kSwitchMessages[i], &key, &value);
    }
    settings.ReleaseObject();
}

void cDisplayList::Shutdown()
{
    mMaterialManager = nullptr;
    ScopedRef settings = Settings();
    if (auto *const manager = static_cast<cIGZPropertyManagerString *>(settings.mObject))
        manager->RemoveAllNotifications(this);
    settings.ReleaseObject();
}

bool cDisplayList::DoMessage(void *message)
{
    if (!message) return false;
    cIGZMessage2Standard *standard = nullptr;
    static_cast<cIGZUnknown *>(message)->QueryInterface(kMessageIid, reinterpret_cast<void **>(&standard));
    bool handled = false;
    // ORIGINAL-BUG: the message is used without checking that it answered the interface.
    if (standard->HasData2()) {
        const uint32_t type = standard->GetType();
        for (int i = 0; i < 5; ++i) {
            if (type != kSwitchMessages[i]) continue;
            if (const uint8_t *const value = static_cast<const uint8_t *>(standard->GetVoid2())) mSwitches[i] = value[4];
            handled = true;
            break;
        }
    }
    if (standard) standard->Release();
    return handled;
}

// ---- the frame and the slots --------------------------------------------------------------------------------------

void cDisplayList::BeginFrame(float time)
{
    ++mFrame;
    mTime = time;
    std::swap(mFreedThisFrame, mFreedLastFrame);
    // The list freed two frames ago (now in mFreedThisFrame) becomes free; the list is left empty.
    for (int32_t index; (index = mFreedThisFrame) >= 0;) {
        mFreedThisFrame = mSlots[index];
        mSlots[index] = kSlotFree;
    }
}

int32_t cDisplayList::AllocSlots(uint32_t count)
{
    // The first run of `count` free slots; else the table grows (a free run at its end is extended).
    const uint32_t size = static_cast<uint32_t>(mSlots.size());
    uint32_t scanned = 0, run = 0;
    while (scanned < size) {
        if (mSlots[scanned++] == kSlotFree) {
            if (++run >= count) break;
        } else {
            run = 0;
        }
    }
    if (run < count) mSlots.resize(size - run + count);
    const uint32_t start = scanned - run;
    for (uint32_t i = 0; i < count; ++i) mSlots[start + i] = kSlotUsed;
    return static_cast<int32_t>(start);
}

int32_t cDisplayList::ReallocSlots(int32_t start, uint32_t count, uint32_t newCount)
{
    if (count) FreeSlots(start, count);
    return newCount ? AllocSlots(newCount) : 0;
}

void cDisplayList::FreeSlots(int32_t start, uint32_t count)
{
    if (!count) return;
    const uint32_t size = static_cast<uint32_t>(mSlots.size());
    const uint32_t first = static_cast<uint32_t>(start);
    if (first >= size || size - first < count) return;
    for (uint32_t i = first; i < first + count; ++i) {
        mSlots[i] = mFreedThisFrame;
        mFreedThisFrame = static_cast<int32_t>(i);
    }
}

// ---- drawing ------------------------------------------------------------------------------------------------------

void cDisplayList::BeginScene()
{
    nGZGraphic4::GetCurrentDeviceCached()->BeginScene();
    mDrawing = 1;
    mPassStack.clear();
    if (!mLog.p) return;
    cIDisplayListLog *const log = static_cast<cIDisplayListLog *>(mLog.p);
    log->SetRZCharStr("-----Scene-----\n");
    log->SetRZCharStr("Frame number: ");
    log->SetUint32(mFrame);
    log->SetRZCharStr("\n");
}

void cDisplayList::EndScene()
{
    mPassStack.clear();
    mDrawing = 0;
    nGZGraphic4::cIDeviceDX *const device = nGZGraphic4::GetCurrentDeviceCached();
    static_cast<nGZGraphic4::cIDeviceState *>(device->GetStateConst())->Reset(kEndSceneStateReset);
    device->EndScene();
    // The log closes after its last frame.
    if (mLogFrames != 0 && --mLogFrames == 0) mLog.Reset();
}

void cDisplayList::DrawNode(cICompositionTreeNode *node, const AffineTransform3 *placement,
                            const nGZCompGeom::BoundBox *bounds, uint32_t planeMask)
{
    Pass *const pass = mPassStack.back();
    // The list only grows: records past the count are kept for reuse.
    if (pass->mNodeCount == pass->mNodes.size()) pass->mNodes.resize(pass->mNodeCount + 1);
    // The shape node's slots (cShapeRefNode's draw is the caller).
    cIShapeRefNode *const shape = static_cast<cIShapeRefNode *>(node);
    uint32_t layerState[2];
    shape->GetLayerState(layerState);
    const uint32_t lastFrame = shape->GetDetailLimit();
    shape->SetWord6c(mFrame);
    NodeRecord &record = pass->mNodes[pass->mNodeCount];
    record.mNode = shape;
    record.mLayerState[0] = layerState[0];
    record.mLayerState[1] = layerState[1];
    record.mCurrent = lastFrame == mFrame;
    record.mPlaneMask = static_cast<uint8_t>(planeMask);
    record.mPlacement = *placement;
    record.mBounds = *bounds;
    ++pass->mNodeCount;
}

void cDisplayList::AddLightNode(cICompositionTreeNode *node, uint32_t planeMask)
{
    if (!mSwitches[0]) return;
    Pass *const pass = mPassStack.back();
    if (pass->mEntriesB.size() <= pass->mEntryCountB) pass->mEntriesB.resize(pass->mEntryCountB + 1);
    PassEntry &entry = pass->mEntriesB[pass->mEntryCountB];
    entry.mObject = node;
    entry.mFlag = static_cast<uint8_t>(planeMask);
    ++pass->mEntryCountB;
}

// ---- adding to the current pass -----------------------------------------------------------------------------------

void cDisplayList::AddPassEntry(void *object, uint8_t flag)
{
    Pass *const pass = mPassStack.back();
    // The list only grows: entries past the count are kept for reuse.
    if (pass->mEntriesA.size() <= pass->mEntryCountA) pass->mEntriesA.resize(pass->mEntryCountA + 1);
    PassEntry &entry = pass->mEntriesA[pass->mEntryCountA];
    entry.mObject = object;
    entry.mFlag = flag;
    ++pass->mEntryCountA;
}

void cDisplayList::InsertDrawItem(const DrawItem *item)
{
    const auto sortedEnd = mItems.begin() + static_cast<int32_t>(mSortedCount);
    const auto at = std::lower_bound(mItems.begin(), sortedEnd, *item, DrawItemOrder{});
    mItems.insert(at, *item);
    ++mSortedCount;
}

// ---- configuration ------------------------------------------------------------------------------------------------

void cDisplayList::SetPassCount(uint32_t count)
{
    mPasses.resize(count);
    if (count == 0) return;
    mPasses[0].mPrevious = nullptr;
    for (uint32_t i = 1; i < count; ++i) mPasses[i].mPrevious = &mPasses[i - 1];
    for (Pass &pass : mPasses) pass.mRecord = &pass.mViewerRecord;
}

void cDisplayList::SetRenderStrategy(cIDisplayListRenderStrategy *strategy)
{
    mStrategy.Assign(strategy ? strategy : new cDefaultDisplayListRenderStrategy);
}

void cDisplayList::SetBufferSizes(uint32_t vertexKB, uint32_t indexKB)
{
    mVertexBytes = vertexKB << 10;
    mIndexBytes = indexKB << 10;
    RecreateBuffers();
}

void cDisplayList::OpenLog(const char *path, uint32_t frames)
{
    mLogFrames = frames;
    auto *const stream = static_cast<cRZOFStream *>(::operator new(sizeof(cRZOFStream)))->Construct(path, 0, 0, 0, 0);
    // cRZOFStream's slots 1 and 2 are AddRef and Release.
    mLog.Assign(reinterpret_cast<cIGZUnknown *>(stream));
}

uint32_t *cDisplayList::TakeStats(uint32_t *out, uint8_t reset)
{
    std::memcpy(out, mStats, sizeof(mStats));
    if (reset) std::memset(mStats, 0, sizeof(mStats));
    return out;
}

void cDisplayList::SetMatrix(const float *matrix) { std::memcpy(mMatrix, matrix, sizeof(mMatrix)); }

void cDisplayList::SetVector140(const float *v) { std::memcpy(mVector140, v, sizeof(mVector140)); }

void cDisplayList::SetVector14c(const float *v) { std::memcpy(mVector14c, v, sizeof(mVector14c)); }

// ---- getters ------------------------------------------------------------------------------------------------------

bool cDisplayList::IsDrawing() { return mDrawing != 0; }

bool cDisplayList::HasPass() { return !mPassStack.empty(); }

uint32_t cDisplayList::GetSlotCount() { return static_cast<uint32_t>(mSlots.size()); }

cIDisplayListRenderStrategy *cDisplayList::GetRenderStrategy() { return mStrategy.p; }

uint32_t cDisplayList::GetFrame() { return mFrame; }

uint32_t *cDisplayList::GetBufferSizes(uint32_t *vertexKB, uint32_t *indexKB)
{
    *vertexKB = mVertexBytes >> 10;
    *indexKB = mIndexBytes >> 10;
    return indexKB;
}

const float *cDisplayList::GetMatrix() { return mMatrix; }

const float *cDisplayList::GetVector140() { return mVector140; }

const float *cDisplayList::GetVector14c() { return mVector14c; }

// ---- adapters -----------------------------------------------------------------------------------------------------

cIDisplayList *__cdecl cDisplayListAdapters::Create() { return new cDisplayList; }

} // namespace nGZSceneGraph
