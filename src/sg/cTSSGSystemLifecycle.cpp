// nTSSG::cTSSGSystem: the service's Init and the managers' release (lane IR). See src/sg/cTSSGSystem.h.
#include "sg/cTSSGSystem.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "misc/UtilitiesC.h"
#include "rz/MiscHelpersB.h"
#include "rz/RZAutoRef.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"
#include "sg/cTextureManager.h"
#include "misc/UtilitiesE.h"

namespace {

template <int kSlot, typename R = uint32_t, typename... A> __forceinline R Call(const void *object, A... args)
{
    return VirtualCall<R(__thiscall *)(void *, A...), kSlot>(object, args...);
}

// The property notifier (GetServiceObject slot 5, class 0xac1887e as 0x6bb22f38); slot 6 forgets a message target.
constexpr uint32_t kPropertyClass = 0x0ac1887e;
constexpr uint32_t kPropertyNotifierIid = 0x6bb22f38;
constexpr int kNotifierForget = 6;
// The message-server holder Init registers with (service 0x85522803 as 0xa55227bb); slot 8 (id, system, id).
constexpr uint32_t kHolderClass = 0x85522803;
constexpr uint32_t kHolderIid = 0xa55227bb;
constexpr int kHolderRegister = 8;
constexpr int kHolderUnregister = 9;

// The expression-function / command registry (0xae8980): its virtual base (table at +4, entry 2) unregisters a name (the
// game's name objects, read in place) through slot 5. The eight command names, in UnregisterCommands' order.
constexpr uint32_t kCommandNames[] = {0xdae9f4, 0xdaed90, 0xdaea68, 0xdaebec, 0xdaec58, 0xdaeb30, 0xdaeb9c, 0xdaead4};
// Slot 4 shuts a manager down (5 on the video-broadcast manager).
constexpr int kShutdown = 4;

void ReleaseRef(void *object) { Call<2>(object); }

// A virtual base of an object built with virtual inheritance: the virtual-base table pointer at +vbptr, entry `entry`.
void *VirtualBase(void *object, uint32_t vbptr, int entry)
{
    auto *at = static_cast<uint8_t *>(object) + vbptr;
    const int32_t *table = *reinterpret_cast<int32_t *const *>(at);
    return at + table[entry];
}

void Unregister(uint32_t nameRva)
{
    void *registry = GetMaterialParserCached();
    Call<5>(VirtualBase(registry, 4, 2), GameAddress<const void>(nameRva));
}

// Clears the reference, then releases the old object (directly, or through its virtual base at +vbptr entry 1).
void Drop(void *&ref)
{
    if (void *old = ref) {
        ref = nullptr;
        ReleaseRef(old);
    }
}
void Drop(nTSSG::SGRef &ref) { Drop(ref.mObject); }
void DropVirtual(void *&ref, uint32_t vbptr)
{
    if (void *old = ref) {
        ref = nullptr;
        ReleaseRef(VirtualBase(old, vbptr, 1));
    }
}

// Slot `slot` (shutdown), then the drop. `tested`: the original skips both for a null reference; untested, it calls the
// slot on whatever is there.
void ShutDown(nTSSG::SGRef &ref, bool tested, int slot = kShutdown)
{
    if (tested && !ref.mObject) return;
    if (slot == kShutdown) Call<kShutdown>(ref.mObject);
    else Call<5>(ref.mObject);
    Drop(ref);
}

// Assigns `object` to `ref` (AddRef on the new one, Release on the old, nothing when they are the same).
void Assign(nTSSG::SGRef &ref, void *object)
{
    void *old = ref.mObject;
    if (object == old) return;
    if (object) Call<1>(object);
    ref.mObject = object;
    if (old) ReleaseRef(old);
}

} // namespace

namespace nTSSG {

// The texture handle destructor (0xae82b0, ours: sg/cTextureManager.h).
cTSSGEntryHandle::~cTSSGEntryHandle() { reinterpret_cast<nGZSceneGraph::cTextureHandle *>(this)->Destroy(); }

void cTSSGSystem::ReleaseManagers()
{
    if (!mManagersCreated) return;
    mManagersCreated = 0;

    // The notifier is held to the end and released there (ScopedRef::ReleaseObject).
    struct HeldRef {
        ScopedRef ref{nullptr};
        ~HeldRef() { ref.ReleaseObject(); }
    } notifier;
    if (void *framework = GetServiceObject())
        Call<5>(framework, kPropertyClass, kPropertyNotifierIid, &notifier.ref.mObject);
    // ORIGINAL-BUG: the notifier is used without a null test.
    Call<kNotifierForget>(notifier.ref.mObject, static_cast<void *>(&mMessageVTable));

    if (mRef3B0.mObject) {
        OriginalFunction<void(__thiscall *)(void *), 0xb3a740>()(mRef3B0.mObject);
        Drop(mRef3B0);
    }
    for (void *object : mHeldObjects)
        if (object) ReleaseRef(object);
    mHeldObjects.clear();
    mEntryHandles.clear();

    if (mRefAC.mObject) {
        const int32_t index = Call<31, int32_t>(mAnimSystem.mObject, mRefAC.mObject);
        if (index >= 0) Call<11>(mAnimSystem.mObject, index, 1u);
        DropVirtual(mRefAC.mObject, 4);
    }
    DropVirtual(mRefB0.mObject, 4);
    Drop(mRef358);
    Drop(mRef104);
    DropVirtual(mRefC0.mObject, 4);
    ShutDown(mAssetCollector, true);
    ShutDown(mCommandParser, false);
    DropVirtual(mParserTarget, 8);
    Drop(mRefB4);
    ShutDown(mLightingManager, false);
    ShutDown(mTerrain, true);
    ShutDown(mRef84, true);
    ShutDown(mVideoBroadcastManager, true, 5);
    ShutDown(mNHoodOccupants, true);
    ShutDown(mRef90, true);
    ShutDown(mRef94, true);

    OriginalFunction<uint8_t(__cdecl *)(), 0x298070>()();
    Call<19>(Call<22, void *>(GetCachedSingleton()), 0u);

    ShutDown(mSceneManager, false);
    ShutDown(mLightRigManager, false);
    Drop(mQueryStrategyManager);
    ShutDown(mLotSkirt, true);
    ShutDown(mPoolManager, true);
    ShutDown(mFenceManager, false);
    ShutDown(mWallManager, false);
    ShutDown(mFloorManager, false);
    ShutDown(mLightMapManager, false);
    ShutDown(mShadowManager, false);
    ShutDown(mRoofManager, false);
    ShutDown(mLotImposterManager, false);
    ShutDown(m3DUIManager, false);
    ShutDown(mRugManager, false);
    ShutDown(mCurvedPoolManager, false);
    ShutDown(mTerrainSnowManager, false);
    ShutDown(mTerrainCutoutManager, false);
    ShutDown(mAwningManager, false);
    ShutDown(mSeasonManager, false);
    for (SGRef &overlay : mOverlays) ShutDown(overlay, true);
    Drop(mRef64);

    mFlag16A = 0;
    *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(this) + 0x3ac) = 0;
}

bool cTSSGSystem::Init()
{
    // The message server registers the message face for every id (the walker gets the server itself).
    reinterpret_cast<KeyMapSlot5Walker *>(&mMessageIds)
        ->CallSlot5ForEachKey(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&mMessageVTable)), nullptr);
    Assign(mScene, OriginalFunction<void *(__cdecl *)(), 0x20df26>()());
    Assign(mAnimSystem, OriginalFunction<void *(__cdecl *)(), 0x20dd94>()());
    Assign(mPersistService, CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>());

    // The holder is released at the end (RZAutoRef::ReleaseObject), also when a call unwinds.
    struct Held {
        RZAutoRef ref{nullptr};
        ~Held() { ref.ReleaseObject(); }
    } holder;
    if (void *framework = GetServiceObject()) Call<5>(framework, kHolderClass, kHolderIid, &holder.ref.mObject);
    // ORIGINAL-BUG: the holder is used without a null test.
    Call<kHolderRegister>(holder.ref.mObject, 0xca7ccbbcu, static_cast<void *>(this), 0xca7ccbbcu);
    Call<kHolderRegister>(holder.ref.mObject, 0x2b5745f8u, static_cast<void *>(this), 0x2b5745f8u);
    OriginalFunction<void(__cdecl *)(), 0x3a5de0>()();
    OriginalFunction<void(__cdecl *)(), 0x253f80>()();
    OriginalFunction<void(__cdecl *)(), 0x253ec0>()();
    return true;
}

void cTSSGSystem::UnregisterExpressionFunction() { Unregister(0xdaedf4); }

void cTSSGSystem::UnregisterCommands()
{
    for (uint32_t name : kCommandNames) Unregister(name);
}

bool cTSSGSystem::Shutdown()
{
    UnregisterExpressionFunction();
    UnregisterCommands();
    OriginalFunction<void(__cdecl *)(), 0x3a65a0>()();
    Drop(mRef2C8);
    Drop(mRef2CC);
    Drop(mScene);
    Drop(mAnimSystem);
    Drop(mPersistService);
    reinterpret_cast<KeyMapWalker *>(&mMessageIds)
        ->CallSlot6ForEachKey(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&mMessageVTable)), nullptr);

    struct Held {
        RZAutoRef ref{nullptr};
        ~Held() { ref.ReleaseObject(); }
    } holder;
    if (void *framework = GetServiceObject()) Call<5>(framework, kHolderClass, kHolderIid, &holder.ref.mObject);
    // ORIGINAL-BUG: the holder is used without a null test.
    Call<kHolderUnregister>(holder.ref.mObject, 0xca7ccbbcu, static_cast<void *>(this), 0xca7ccbbcu);
    Call<kHolderUnregister>(holder.ref.mObject, 0x2b5745f8u, static_cast<void *>(this), 0x2b5745f8u);
    return true;
}

bool cTSSGSystemServiceSub::Shutdown()
{
    return reinterpret_cast<cTSSGSystem *>(reinterpret_cast<uint8_t *>(this) - 0xc)->Shutdown();
}

bool cTSSGSystemServiceSub::Init()
{
    return reinterpret_cast<cTSSGSystem *>(reinterpret_cast<uint8_t *>(this) - 0xc)->Init();
}

} // namespace nTSSG
