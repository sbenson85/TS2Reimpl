// nTSSG::cNHoodOccupantManager (lane GO): creation, destruction, the interfaces, initialisation and the scene detach, with the adapters of those.
// Split from one file along these concerns; the shared helpers are in nTSSGNHoodOccupantManagerInternal.h.
#include "sg/DetailObject.h"
#include "std/StdInstances.h"
#include "ts/nTSSGNHoodOccupantManager.h"
#include "ts/nTSSGNHoodOccupantManagerInternal.h"

namespace nTSSG {

using namespace occupant_detail;

namespace {

// The ids the manager listens for (Initialize registers the message face for each, Shutdown unregisters).
constexpr uint32_t kMessageIds[] = {0x0d6429da, 0x4c095286, 0x2c7812db, 0x4d501e65, 0x6d501e6e,
                                    0xed5c6df0, 0x2d5c6e19, 0xb23b7d66, 0xb23b7d6f, 0x522ced95};

constexpr unsigned kWatchPropertySlot = 4;  // property notifier: (target, watch id, name, 0)
constexpr unsigned kUnwatchSlot = 6;        // property notifier: (target)

constexpr unsigned kRemoveFromSceneSlot = 9;

constexpr unsigned kAppSlot12DetachSlot = 21;

} // namespace

cNHoodOccupantManager::cNHoodOccupantManager()
    : mFadeDistanceSq(2500), mInitialized(0), mDirty(0), mPending{}, mMergeFlora(1), mBuildAll(1), mLotByte(0),
      mFloraPending(0), mOptionBit7(0)
{
    for (uint32_t id : kMessageIds) mMessageIds.insert(id);
    for (uint8_t &visible : mLayerVisible) visible = 1;
    HeldService properties(kPropertyClass, kPropertySetIid);
    AnyBooleanLocal dontMerge;
    {
        PropertyName name("dontMergeNHFlora");
        using Get = uint32_t(__thiscall *)(void *, const as2::RawString *, AnyBooleanLocal *);
        VirtualCall<Get, kGetPropertySlot>(properties.mRef.mObject, &name.mText, &dontMerge);
    }
    if (dontMerge.mValue) mMergeFlora = 0;
}

// 0x2a7b70 (cdecl): the +8 face, null when the allocation fails.
cINHoodOccupantManager *__cdecl cNHoodOccupantManager::Create()
{
    return new (std::nothrow) cNHoodOccupantManager;
}

// 0x2a50f0: Shutdown on the +8 face (not through the vtable), then the members in reverse order: the pending list, the
// reference arrays, the flora node, the two lists, +0x78, the root, the layer nodes, the name, the id set, +0x30, +0x2c, +0x28.
cNHoodOccupantManager::~cNHoodOccupantManager()
{
    cNHoodOccupantManager::Shutdown();
    mPending.Destroy();
}

// 0x2a8da0: the +0xc face for 0x452294aa, the +8 face for 0xebd4927b (counted through slot 1), cRZUnknown's answer for the
// rest; false without touching anything for a null out.
bool cNHoodOccupantManager::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    void *face;
    if (riid == kMessageTargetIid) {
        face = static_cast<cIGZMessageTarget2 *>(this);
    } else if (riid == kSceneIid) {
        face = static_cast<cINHoodOccupantManager *>(this);
    } else {
        return rz::cRZUnknown::QueryInterface(riid, out);
    }
    *out = face;
    AddRef();
    return true;
}

uint32_t cNHoodOccupantManager::AddRef()
{
    return rz::cRZUnknown::AddRef();
}

uint32_t cNHoodOccupantManager::Release()
{
    return rz::cRZUnknown::Release();
}

// 0x2a8100.
bool cNHoodOccupantManager::Initialize()
{
    // The message face through the base conversion (null-tested: the original's compare-and-select).
    reinterpret_cast<KeyMapSlot5Walker *>(&mMessageIds)
        ->CallSlot5ForEachKey(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<cIGZMessageTarget2 *>(this))),
            nullptr);
    HeldService notifier(kPropertyClass, kPropertyNotifierIid);
    {
        PropertyName name("neighborhoodPropFadeDistance");
        using Watch = uint32_t(__thiscall *)(void *, cIGZMessageTarget2 *, uint32_t, const as2::RawString *, uint32_t);
        VirtualCall<Watch, kWatchPropertySlot>(notifier.mRef.mObject, static_cast<cIGZMessageTarget2 *>(this),
            kFadeDistanceWatchId, &name.mText, 0);
    }
    mOptionBit7 = static_cast<uint8_t>(appinit::HasOptionBits(1u << kOptionBit));
    mInitialized = 1;
    return true;
}

// 0x2a92c0.
bool cNHoodOccupantManager::Shutdown()
{
    if (mInitialized) {
        reinterpret_cast<KeyMapWalker *>(&mMessageIds)
            ->CallSlot6ForEachKey(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<cIGZMessageTarget2 *>(this))),
                nullptr);
        HeldService notifier(kPropertyClass, kPropertyNotifierIid);
        using Unwatch = uint32_t(__thiscall *)(void *, cIGZMessageTarget2 *);
        VirtualCall<Unwatch, kUnwatchSlot>(notifier.mRef.mObject, static_cast<cIGZMessageTarget2 *>(this));
        DetachScene();
        mRef78.Reset();
        mSgSystem.Reset();
        mInitialized = 0;
    }
    return true;
}

// 0x2a7af0.
void cNHoodOccupantManager::SetLayerVisible(uint32_t index, uint8_t visible)
{
    if (mLayerVisible[index] == visible) return;
    mLayerVisible[index] = visible;
    cIGZUnknown *node = mLayerNodes[index].mObject;
    if (!node) return;
    using Child = uint32_t(__thiscall *)(void *, cIGZUnknown *);
    if (visible) {
        VirtualCall<Child, kAddChildSlot>(mRoot.mObject, node);
    } else {
        VirtualCall<Child, kRemoveChildSlot>(mRoot.mObject, node);
    }
}

void cNHoodOccupantManager::MarkDirty()
{
    mDirty = 1;
}

void cNHoodOccupantManager::ClearDirty()
{
    mDirty = 0;
}

// 0x2a56d0.
void cNHoodOccupantManager::DetachScene()
{
    if (!mRoot.mObject) return;
    void *system = CachedServiceGetter<kSgSystemCache, kSgSystemIdA, kSgSystemIdB>();
    auto *scene = VirtualCall<cIGZUnknown *(__thiscall *)(void *), kSceneOfSystemSlot>(system);
    if (scene) scene->AddRef();
    using Child = uint32_t(__thiscall *)(void *, cIGZUnknown *);
    VirtualCall<Child, kRemoveFromSceneSlot>(scene, mRoot.mObject);
    if (mAppSlot12.mObject) {
        VirtualCall<uint32_t(__thiscall *)(void *), kAppSlot12DetachSlot>(mAppSlot12.mObject);
        mAppSlot12.Reset();
    }
    mNodes.clear();
    for (int i = 0; i < 4; ++i) {
        if (mLayerNodes[i].mObject) {
            if (mLayerVisible[i]) VirtualCall<Child, kRemoveChildSlot>(mRoot.mObject, mLayerNodes[i].mObject);
            mLayerNodes[i].Reset();
        }
    }
    if (mMergeFlora && mFloraNode.mObject) VirtualCall<Child, kRemoveChildSlot>(mRoot.mObject, mFloraNode.mObject);
    mRoot.Reset();
    if (scene) scene->Release();
}

// ---- adapters ----

using Adapters = cNHoodOccupantManagerAdapters;
using Self = cNHoodOccupantManager;

Self *__fastcall Adapters::Construct(Self *memory, void *)
{
    return ::new (memory) Self;
}

void __fastcall Adapters::Destruct(Self *self, void *)
{
    [[msvc::forceinline_calls]] self->Self::~Self();
}

// 0x2a5310: the destructor and, with bit 0, the sized delete (the compiler's scalar deleting destructor, by hand).
Self *__fastcall Adapters::DeletingDestructor(Self *self, void *, uint32_t flags)
{
    [[msvc::forceinline_calls]] self->Self::~Self();
    if (flags & 1) ::operator delete(self, sizeof(Self));
    return self;
}

bool __fastcall Adapters::QueryInterface(Self *self, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::QueryInterface(riid, out);
}

bool __fastcall Adapters::Initialize(cINHoodOccupantManager *face, void *)
{
    __assume(face != nullptr);
    [[msvc::forceinline_calls]] return static_cast<Self &>(*face).Self::Initialize();
}

bool __fastcall Adapters::Shutdown(cINHoodOccupantManager *face, void *)
{
    __assume(face != nullptr);
    [[msvc::forceinline_calls]] return static_cast<Self &>(*face).Self::Shutdown();
}

void __fastcall Adapters::SetLayerVisible(cINHoodOccupantManager *face, void *, uint32_t index, uint8_t visible)
{
    __assume(face != nullptr);
    [[msvc::forceinline_calls]] return static_cast<Self &>(*face).Self::SetLayerVisible(index, visible);
}

void __fastcall Adapters::MarkDirty(cINHoodOccupantManager *face, void *)
{
    __assume(face != nullptr);
    [[msvc::forceinline_calls]] return static_cast<Self &>(*face).Self::MarkDirty();
}

void __fastcall Adapters::ClearDirty(cINHoodOccupantManager *face, void *)
{
    __assume(face != nullptr);
    [[msvc::forceinline_calls]] return static_cast<Self &>(*face).Self::ClearDirty();
}

} // namespace nTSSG
