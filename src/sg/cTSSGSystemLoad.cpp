// nTSSG::cTSSGSystem: after a lot or a neighbourhood has loaded (lane JD; slots 20 and 91). See src/sg/cTSSGSystem.h.
#include "sg/cTSSGSystem.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"
#include "gz/cGZPropertySet.h"
#include "gz/cIGZPropertySet.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/StaticGetters2.h"
#include "sg/SGUtilities.h"

#include <string>

namespace {

template <int kSlot, typename R = uint32_t, typename... A> __forceinline R Call(const void *object, A... args)
{
    return VirtualCall<R(__thiscall *)(void *, A...), kSlot>(object, args...);
}

using nTSSG::SGRef;

// The framework's slot 3 (class id, interface id, out).
constexpr int kFrameworkGetClassObject = 3;
// Slots the lot and neighbourhood objects share: 3 initialises, 5 starts (with the object's own arguments).
constexpr int kObjectInit = 3;
constexpr int kObjectStart = 5;

// The objects the two slots make on first use (class, interface).
constexpr uint32_t kRoadsClass = 0x8c360cf7, kRoadsIid = 0x6c360cec;         // mRef90
constexpr uint32_t kOccupantClass = 0x6bd49281, kOccupantIid = 0xebd4927b;   // mNHoodOccupants
constexpr uint32_t kRef84Class = 0x0bcc8149, kRef84Iid = 0x8bcc813e;         // mRef84
constexpr uint32_t kRef94Class = 0x6c9d07cd, kRef94Iid = 0x4c9d07be;         // mRef94
// The interface the car manager's slot 24 object takes from the terrain (lot) or mRef84 (neighbourhood).
constexpr uint32_t kCarTerrainIid = 0x2c2a7106;

// The settings service's property set (GetServiceObject slot 5 with these ids) and its slot 9, a property into an any.
constexpr uint32_t kSettingsClass = 0x0ac1887e;
constexpr uint32_t kSettingsIid = 0x4bb0d269;
constexpr int kSettingsGet = 9;

// Game text.
constexpr uint32_t kEnableLotImpostersName = 0xdae4f4;  // the lot impostor setting's name
constexpr uint32_t kEnableOccupantsName = 0xdae4d0;     // the lot occupants setting's name
constexpr uint32_t kLightingCasName = 0xda88f4;         // the lighting set in the CAS world
constexpr uint32_t kLightingLotName = 0xdaefa0;         // the lighting set on a lot
constexpr uint32_t kLightingNhoodDay = 0xdae884;        // the neighbourhood lighting set by day
constexpr uint32_t kLightingNhoodNight = 0xdae894;      // ... and by night
constexpr uint32_t kCasEffects = 0xdaefa8;              // the CAS world's effects
constexpr uint32_t kLotEffects = 0xdaefb0;              // a lot's effects
constexpr uint32_t kNhoodEffects = 0xdaeecc;            // the neighbourhood's day/night effects
constexpr uint32_t kNhoodEffectsSuffix = 0xdaeee4;      // the suffix of a neighbourhood's own effects
constexpr uint32_t kNhoodEffectsSuffixLength = 8;
constexpr uint32_t kOnLotLoaded = 0xdaefb8;             // the lot-loaded cheat hook
constexpr uint32_t kOnLotLoadedCommand = 0xdaefc4;      // the command creating it (format, the lot id)
constexpr uint32_t kOnNhoodLoaded = 0xdaef68;           // the neighbourhood-loaded cheat hook
constexpr uint32_t kOnNhoodLoadedCommand = 0xdaef80;    // the command creating it
constexpr uint32_t kNeighborhoodName = 0xdaef58;        // the cheat holder's neighbourhood argument
// The road paths of the neighbourhood kinds (the neighbourhood's slot 18): 3, 7, 6, 5.
struct RoadPath {
    uint32_t kind;
    uint32_t name;
};
constexpr RoadPath kRoadPaths[] = {{3, 0xdaeef0}, {7, 0xdaef04}, {6, 0xdaef20}, {5, 0xdaef3c}};

// The car manager's speed (slot 17), as the float 5.
constexpr float kCarSpeed = 5.0f;

// The camera service (cached at 0x10cc45c; GetServiceObject slot 5 with these ids, the local reference released).
constexpr uint32_t kCameraCell = 0x10cc45c;
constexpr uint32_t kCameraClass = 0x4a037ee3;
constexpr uint32_t kCameraIid = 0x49ff7d6f;

// Callees nobody has replaced yet.
void *PlacementService() { return OriginalFunction<void *(__cdecl *)(), 0x20e0b2>()(); }
void *CarManager() { return OriginalFunction<void *(__cdecl *)(), 0xb39000>()(); }
void *CheatHolder() { return OriginalFunction<void *(__cdecl *)(), 0xb38f10>()(); }
void UpdateSceneObject(void *object, void *items)
{
    nGZSceneGraph::AttachChoreoQueue(static_cast<nGZSceneGraph::cICompositionTreeNode *>(object), items); // 0xae39c0
}
void SetLotSetting(uint32_t id, uint32_t value) { OriginalFunction<void(__cdecl *)(uint32_t, uint32_t), 0x254bd0>()(id, value); }
template <typename... A> void ExecuteCheat(void *parser, const char *format, A... args)
{
    OriginalFunction<void(__cdecl *)(void *, const char *, ...), 0x24a1e0>()(parser, format, args...);
}

void ReleaseRef(void *object) { Call<2>(object); }

// Clears `ref` and releases what it held.
void Drop(SGRef &ref)
{
    if (void *old = ref.mObject) {
        ref.mObject = nullptr;
        ReleaseRef(old);
    }
}

// `ref` made from the framework when it is empty (the framework fetched first, then the old reference dropped), then
// initialised.
void Ensure(SGRef &ref, uint32_t clsid, uint32_t iid)
{
    if (!ref.mObject) {
        void *framework = GetDirectorFrameworkObject();
        Drop(ref);
        Call<kFrameworkGetClassObject>(framework, clsid, iid, &ref.mObject);
    }
    Call<kObjectInit>(ref.mObject);
}

// The car manager's named effect `name`: when it has one (slot 12), `ref` is dropped, given it (slot 14) and started
// (its slot 3 with 0).
void LoadEffect(void *cars, const char *name, SGRef &ref)
{
    if (!Call<12, uint8_t>(cars, name)) return;
    Drop(ref);
    if (Call<14, uint8_t>(cars, name, &ref.mObject)) Call<3>(ref.mObject, 0u);
}

// The car manager's speed, scene (slot 17 of the scene manager) and level object (slot 18).
void SetUpCars(void *cars, void *scene)
{
    Call<17>(cars, kCarSpeed);
    Call<39>(cars, Call<17, void *>(scene));
    Call<40>(cars, Call<18, void *>(scene));
}

// A local reference released (slot 2, not cleared) when it goes.
struct LocalRef {
    void *p = nullptr;
    LocalRef() = default;
    LocalRef(const LocalRef &) = delete;
    LocalRef &operator=(const LocalRef &) = delete;
    ~LocalRef()
    {
        if (p) ReleaseRef(p);
    }
};

// A reference AddRef'd when taken and released when it goes (null allowed).
struct CountedRef {
    void *p;
    explicit CountedRef(void *object) : p(object)
    {
        if (p) Call<1>(p);
    }
    CountedRef(const CountedRef &) = delete;
    CountedRef &operator=(const CountedRef &) = delete;
    ~CountedRef()
    {
        if (p) ReleaseRef(p);
    }
};

// A boolean setting (default true) read through `set` (the property set's slot 9 with the name as a service string).
bool ReadSetting(void *set, uint32_t name)
{
    AnyBoolean value{GameAddress<const void>(gzany::kBooleanVTable), 1, {}};
    {
        GZServiceString key(GameAddress<const char>(name));
        Call<kSettingsGet>(set, &key, &value);
    }
    return value.mValue != 0;
}

void *CameraService()
{
    void *&cell = *GameAddress<void *>(kCameraCell);
    if (!cell) {
        ScopedRef local{nullptr};
        if (void *service = GetServiceObject()) Call<5>(service, kCameraClass, kCameraIid, &local.mObject);
        cell = local.mObject;
        local.ReleaseObject();
    }
    return cell;
}

} // namespace

namespace nTSSG {

void cTSSGSystem::PostLoadLot(uint32_t lotId)
{
    void *placement = PlacementService();
    const uint32_t first = Call<85>(placement);
    const uint32_t last = Call<86>(placement);
    OriginalFunction<void(__thiscall *)(cTSSGSystem *, uint32_t, uint32_t), 0x254eb0>()(this, first, last);
    if (mTerrain.mObject) Call<8>(mTerrain.mObject);
    Call<5>(mLightMapManager.mObject);
    if (mFloorManager.mObject) Call<11>(mFloorManager.mObject);
    if (mRugManager.mObject) Call<8>(mRugManager.mObject);
    if (mAwningManager.mObject) Call<8>(mAwningManager.mObject);
    if (mCurvedPoolManager.mObject) Call<7>(mCurvedPoolManager.mObject);
    if (mTerrainCutoutManager.mObject) Call<7>(mTerrainCutoutManager.mObject);
    if (!mLotFirstLoadDone) {
        OriginalFunction<void(__thiscall *)(cTSSGSystem *), 0x252490>()(this);
        mLotFirstLoadDone = 1;
    }
    Call<10>(mWallManager.mObject);
    Call<14>(mFenceManager.mObject);
    Call<10>(mAwningManager.mObject);

    if (lotId) {
        Ensure(mRef90, kRoadsClass, kRoadsIid);
        if (mLotSkirt.mObject) Call<16>(mLotSkirt.mObject);
        ScopedRef set{nullptr};
        if (void *service = GetServiceObject()) Call<5>(service, kSettingsClass, kSettingsIid, &set.mObject);
        if (ReadSetting(set.mObject, kEnableLotImpostersName)) {
            Call<6>(mRef90.mObject, lotId);
            Call<7>(mRef90.mObject, 1u);
        } else {
            Call<7>(mRef90.mObject, 0u);
        }
        Call<5>(mLotSkirt.mObject);
        if (ReadSetting(set.mObject, kEnableOccupantsName)) {
            Ensure(mNHoodOccupants, kOccupantClass, kOccupantIid);
            Call<kObjectStart>(mNHoodOccupants.mObject, 0u);
        }
        set.ReleaseObject();
    }
    Call<6>(mRefC0.mObject);
    Call<6>(mRoofManager.mObject);
    Call<7>(mPoolManager.mObject);
    mLotRefreshPending = 1;

    void *world = GetCachedSlot23();
    Call<18>(mLightingManager.mObject, 1u);
    Call<19>(mLightingManager.mObject,
             GameAddress<const char>(Call<19, uint8_t>(world) ? kLightingCasName : kLightingLotName));
    Call<6>(mLightingManager.mObject, mSceneManager.mObject);
    if (void *scene = mSceneManager.mObject) {
        void *sets = Call<25, void *>(mLightingManager.mObject);
        Call<9>(sets, Call<17, void *>(scene));
    }
    Call<8>(mCurvedPoolManager.mObject);
    void *cars = CarManager();
    if (cars) {
        SetUpCars(cars, mSceneManager.mObject);
        Call<18>(cars, 1u);
        Call<25>(cars, 0u, mOverlays[0].mObject);
        Call<25>(cars, 1u, mOverlays[2].mObject);
    }
    Call<24>(mRefB4.mObject, Call<18, void *>(mSceneManager.mObject));
    if (Call<18, void *>(mSceneManager.mObject))
        UpdateSceneObject(Call<18, void *>(mSceneManager.mObject), mRefAC.mObject);
    if (cars) {
        LocalRef terrain;
        Call<0>(mTerrain.mObject, kCarTerrainIid, &terrain.p);
        Call<6>(Call<24, void *>(cars), 0u, terrain.p);
        LoadEffect(cars, GameAddress<const char>(Call<19, uint8_t>(world) ? kCasEffects : kLotEffects), mRef110);
    }
    void *cheats = Call<3, void *>(CheatHolder());
    if (Call<48, uint8_t>(cheats, GameAddress<const char>(kOnLotLoaded)))
        ExecuteCheat(cheats, GameAddress<const char>(kOnLotLoadedCommand), lotId);
    if (!Call<19, uint8_t>(world)) {
        if (void *view = Call<100, void *>(CameraService())) Call<5>(view);
        CountedRef app(Call<33, void *>(GetCachedSingleton()));
        if (app.p) {
            SetLotSetting(0x122ced69, Call<25>(app.p));
            SetLotSetting(0x322ced70, Call<27>(app.p));
        }
    }
}

void cTSSGSystem::NhoodEntered(void *name, uint32_t flag)
{
    void *nhood = Call<6, void *>(GetCachedSlot23());
    Call<5>(mSceneManager.mObject, 0u, 0u);
    OriginalFunction<void(__thiscall *)(cTSSGSystem *, uint32_t), 0x248cd0>()(this, 1);
    void *cars = CarManager();
    if (cars) {
        SetUpCars(cars, mSceneManager.mObject);
        CountedRef camera(Call<6, void *>(GetCachedSingleton()));
        Call<11>(GetCachedSingleton());
        const float height = Call<50, float>(camera.p);
        const uint32_t a = Call<52>(camera.p);
        const uint32_t b = Call<53>(camera.p);
        Call<18>(cars, 0u);
        Call<25>(cars, 0u, mOverlays[5].mObject);
        Call<25>(cars, 1u, mOverlays[7].mObject);
        if (mOverlays[5].mObject) Call<13>(mOverlays[5].mObject, height, a, b);
        if (mOverlays[7].mObject) Call<13>(mOverlays[7].mObject, height, a, b);
    }
    Ensure(mRef84, kRef84Class, kRef84Iid);
    Call<5>(mRef84.mObject);
    Call<18>(mLightingManager.mObject, 1u);
    Call<19>(mLightingManager.mObject,
             GameAddress<const char>(Call<20>(nhood) ? kLightingNhoodNight : kLightingNhoodDay));
    Call<8>(mLightingManager.mObject, mSceneManager.mObject);
    LocalRef terrain;
    if (cars) {
        Call<0>(mRef84.mObject, kCarTerrainIid, &terrain.p);
        Call<6>(Call<24, void *>(cars), 0u, terrain.p);
        LoadEffect(cars, GameAddress<const char>(kNhoodEffects), mRef114);
        std::string effects(Call<5, const char *>(Call<4, void *>(nhood)));
        effects.append(GameAddress<const char>(kNhoodEffectsSuffix), kNhoodEffectsSuffixLength);
        LoadEffect(cars, effects.c_str(), mRef118);
    }
    Ensure(mNHoodOccupants, kOccupantClass, kOccupantIid);
    Call<kObjectStart>(mNHoodOccupants.mObject, 1u);
    Ensure(mRef90, kRoadsClass, kRoadsIid);
    Call<kObjectStart>(mRef90.mObject, name, flag);
    Ensure(mRef94, kRef94Class, kRef94Iid);
    for (const RoadPath &path : kRoadPaths)
        if (Call<18>(nhood) == path.kind) Call<7>(mRef94.mObject, GameAddress<const char>(path.name));
    void *scene = mSceneManager.mObject;
    void *sets = Call<25, void *>(mLightingManager.mObject);
    Call<9>(sets, Call<17, void *>(scene));
    Call<24>(mRefB4.mObject, Call<18, void *>(mSceneManager.mObject));
    Call<20>(mRefB4.mObject, GameAddress<const char>(kNeighborhoodName));
    if (Call<18, void *>(mSceneManager.mObject))
        UpdateSceneObject(Call<18, void *>(mSceneManager.mObject), mRefAC.mObject);
    void *cheats = Call<3, void *>(CheatHolder());
    if (Call<48, uint8_t>(cheats, GameAddress<const char>(kOnNhoodLoaded)))
        ExecuteCheat(cheats, GameAddress<const char>(kOnNhoodLoadedCommand));
    mFlag16B = 1;
    mNhoodRefreshPending = 1;
}

} // namespace nTSSG
