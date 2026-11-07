// nTSSG::cTSSGSystem: the message handler and the helpers it runs (lane IR). See src/sg/cTSSGSystem.h.
#include "sg/DetailObject.h"
#include "rz/RZMessageServer.h"
#include "sg/cTSSGSystem.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"
#include "gz/cIGZPropertySet.h"
#include "misc/UtilitiesE.h"
#include "rz/RZMemory.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/StaticGetters2.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZString.h"

namespace {

template <int kSlot, typename R = uint32_t, typename... A> __forceinline R Call(const void *object, A... args)
{
    return VirtualCall<R(__thiscall *)(void *, A...), kSlot>(object, args...);
}

void ReleaseRef(void *object) { Call<2>(object); }

// The message's data interface (QueryInterface 0x65297976): slot 8 whether it has a value, slot 9 the value, slot 13
// whether it has an any, slot 15 the any (its value at +4), slot 29 a query (iid, out), slot 32 an object.
constexpr uint32_t kDataIid = 0x65297976;
constexpr int kMessageType = 4;
constexpr int kDataHasValue = 8;
constexpr int kDataValue = 9;
constexpr int kDataHasAny = 13;
constexpr int kDataAny = 15;
constexpr int kDataQuery = 29;
constexpr int kDataObject = 32;

// The settings service's property set (0x1cbaf3: GetServiceObject slot 5 with 0x0ac1887e / 0x4bb0d269) and its slot 9,
// which reads a property into an any.
constexpr uint32_t kSettingsClass = 0x0ac1887e;
constexpr uint32_t kSettingsIid = 0x4bb0d269;
constexpr int kSettingsGet = 9;
constexpr uint32_t kEnableLotImpostersName = 0xdae4f4;
constexpr uint32_t kEnableOccupantsName = 0xdae4d0;

// The neighbourhood occupant manager (class 0x6bd49281, interface 0xebd4927b).
constexpr uint32_t kOccupantClass = 0x6bd49281;
constexpr uint32_t kOccupantIid = 0xebd4927b;

// The world the lot options apply to: GetCachedSlot23's slot 5 says which (0x0bea28c2 a lot), slot 9 the lot, whose
// slot 18 is its id.
constexpr uint32_t kLotWorld = 0x0bea28c2;

// The primary slots DoMessage calls on the object itself.
constexpr int kSelfSlot19 = 19, kSelfSlot20 = 20, kSelfSlot89 = 89, kSelfSlot90 = 90, kSelfSlot91 = 91, kSelfSlot92 = 92,
              kSelfSlot93 = 93, kSelfSlot94 = 94, kSelfSlot95 = 95;

// A reference released at the end of its scope (the original's local references: slot 2, not cleared).
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

// The any the data carries when it has one (its value at +4). Null when the data, or its any, is missing.
const uint8_t *AnyOf(void *data)
{
    if (!data || !Call<kDataHasAny, uint8_t>(data)) return nullptr;
    return Call<kDataAny, const uint8_t *>(data);
}

// A boolean setting (default true) read through the settings service's property set; the set is held until the end of
// the scope (ScopedRef::ReleaseObject, as the original).
struct Setting {
    ScopedRef set{nullptr};
    bool on;
    explicit Setting(uint32_t name)
    {
        AnyBoolean value{GameAddress<const void>(gzany::kBooleanVTable), 1, {}};
        if (void *service = GetServiceObject()) Call<5>(service, kSettingsClass, kSettingsIid, &set.mObject);
        {
            GZServiceString key(GameAddress<const char>(name));
            Call<kSettingsGet>(set.mObject, &key, &value);
        }
        on = value.mValue != 0;
    }
    Setting(const Setting &) = delete;
    Setting &operator=(const Setting &) = delete;
    ~Setting() { set.ReleaseObject(); }
};

// The current lot's id when the world is a lot that has one (the original asks slot 9 again for every use).
bool CurrentLotId(uint32_t &id, bool wantId)
{
    void *world = GetCachedSlot23();
    if (Call<5>(world) != kLotWorld) return false;
    if (!Call<9, void *>(world)) return false;
    if (!Call<18>(Call<9, void *>(world))) return false;
    if (wantId) id = Call<18>(Call<9, void *>(world));
    return true;
}

// The framework gives `ref` the occupant manager again (the old one released first), which is initialised and gets its
// slot 5(0).
void RecreateOccupants(nTSSG::SGRef &ref)
{
    void *framework = GetDirectorFrameworkObject();
    if (void *old = ref.mObject) {
        ref.mObject = nullptr;
        ReleaseRef(old);
    }
    Call<3>(framework, kOccupantClass, kOccupantIid, &ref.mObject);
    Call<3>(ref.mObject);
    Call<5>(ref.mObject, 0u);
}

} // namespace

namespace nTSSG {

bool cTSSGSystemMessageFace::DoMessage(void *message)
{
    return reinterpret_cast<cTSSGSystem *>(reinterpret_cast<uint8_t *>(this) - 8)->DoMessage(message);
}

bool cTSSGSystem::DoMessage(void *message)
{
    if (!message) return false;
    LocalRef data;
    Call<0>(message, kDataIid, &data.p);
    const uint32_t type = Call<kMessageType>(message);
    switch (type) {
    // ---- start-up steps and commands (ids 0x8d..0xd8) ----
    case 0x8d: mMaterialReloadDelay = 10; return true;
    case 0x9a: mFlag16B = 1; return true;
    case 0xb4: mFlag16B = 0; return true;
    case 0x9d: TouchEntry(Call<kDataValue>(data.p)); return true;
    case 0xb2:
        if (Call<kDataHasValue, uint8_t>(data.p)) Call<kSelfSlot19>(this, Call<kDataValue>(data.p));
        return true;
    case 0xb3:
        if (Call<kDataHasValue, uint8_t>(data.p)) Call<kSelfSlot20>(this, Call<kDataValue>(data.p));
        return true;
    case 0xb5: Call<kSelfSlot93>(this); return true;
    case 0xb9: Call<kSelfSlot92>(this); return true;
    case 0xd7: Call<kSelfSlot95>(this); return true;
    case 0xd8: Call<kSelfSlot94>(this); return true;
    case 0xb8: {
        LocalRef queried;
        if (Call<kDataQuery, uint8_t>(data.p, 0x089b7dc8u, &queried.p))
            Call<kSelfSlot91>(this, queried.p, Call<kDataValue>(data.p) != 0);
        return true;
    }
    case 0xbb:
    case 0xbc: {
        // 0x1d4ab0: the data's object, as interface 0xeb54e473.
        void *source = Call<kDataObject, void *>(data.p);
        LocalRef queried;
        Call<0>(source, 0xeb54e473u, &queried.p);
        if (queried.p) {
            if (type == 0xbb) Call<kSelfSlot90>(this, queried.p);
            else Call<kSelfSlot89>(this, queried.p);
        }
        return true;
    }
    case 0xcf: CreateManagers(); return true;
    case 0xd0: ReleaseManagers(); return true;
    case 0xd3: OriginalFunction<void(__thiscall *)(cTSSGSystem *), 0x24ebd0>()(this); return true;
    case 0xd4: OriginalFunction<void(__thiscall *)(cTSSGSystem *), 0x24c980>()(this); return true;

    // ---- shadows: the shadow manager's slot 6(kind, on) ----
    case 0x0be33684:
    case 0x8be6f77b:
    case 0x8be6f77c: {
        if (!data.p) return true;
        if (!Call<kDataHasAny, uint8_t>(data.p) || !mShadowManager.mObject) return true;
        const uint8_t *any = Call<kDataAny, const uint8_t *>(data.p);
        if (!any) return true;
        const uint32_t kind = type == 0x0be33684 ? 2 : type == 0x8be6f77c ? 1 : 0;
        Call<6>(mShadowManager.mObject, kind, uint32_t(any[4]));
        return true;
    }

    // ---- options stored on the object ----
    case 0x0cdc679b: // animationPermanentCache
    case 0xabfc20da: // animationLOD
    case 0x8c1827f2: // displayPaths
    case 0xac1827fc: // displayLookAtBoxes
    case 0x8d9a22c3: // displayMissingAnimations
    case 0x2cea9fb2: // animationTimerIsMaster
    case 0xccdc9639: // animationPrediction
    case 0x4ef8bd75: // animationBackgroundLoad
    case 0x2d8812c4: // debugCinematics
    case 0x8dc9d24a: // renderInsideVisibleObjects (a dword)
    {
        const uint8_t *any = AnyOf(data.p);
        if (!any) return true;
        switch (type) {
        case 0x0cdc679b: mRenderOptions[0] = any[4]; break;
        case 0xabfc20da:
            mRenderOptions[1] = any[4];
            if (mRefC0.mObject) Call<8>(mRefC0.mObject, uint32_t(any[4]));
            break;
        case 0x8c1827f2: mRenderOptions[2] = any[4]; break;
        case 0xac1827fc: mRenderOptions[3] = any[4]; break;
        case 0x8d9a22c3: mRenderOptions[4] = any[4]; break;
        case 0x2cea9fb2: mRenderOptions[5] = any[4]; break;
        case 0xccdc9639: mRenderOptions[6] = any[4]; break;
        case 0x4ef8bd75: mRenderOptions[7] = any[4]; break;
        case 0x2d8812c4: mRenderOptions[8] = any[4]; break;
        default: mField3A4 = *reinterpret_cast<const uint32_t *>(any + 4); break;
        }
        return true;
    }

    // ---- the animation system ----
    case 0x0c7bfea6: // animationFrameSampling: slot 12(on)
        if (const uint8_t *any = AnyOf(data.p)) Call<12>(mAnimSystem.mObject, uint32_t(any[4]));
        return true;
    case 0x0cdc6794: // animationCacheSize: slot 20(size in KB << 10)
        if (const uint8_t *any = AnyOf(data.p))
            Call<20>(mAnimSystem.mObject, *reinterpret_cast<const uint32_t *>(any + 4) << 10);
        return true;

    // ---- managers told directly ----
    case 0x2beb0d4c: // renderInsideObjectOnlyOnSelectedSimLevel
        if (const uint8_t *any = AnyOf(data.p)) OriginalFunction<void(__thiscall *)(cTSSGSystem *, uint32_t), 0x254490>()(this, any[4]);
        return true;
    case 0x2cae6756: // displayNeighborhoodWater: +0x84 slot 18(on); the any is not tested for null
        if (mRef84.mObject && data.p && Call<kDataHasAny, uint8_t>(data.p))
            Call<18>(mRef84.mObject, uint32_t(Call<kDataAny, const uint8_t *>(data.p)[4]));
        return true;
    case 0x4c60901a: // displayLotImposters: +0x90 slot 7(on); the any is not tested for null
        if (mRef90.mObject && data.p && Call<kDataHasAny, uint8_t>(data.p))
            Call<7>(mRef90.mObject, uint32_t(Call<kDataAny, const uint8_t *>(data.p)[4]));
        return true;
    case 0x4c7ab209: { // lotTerrainLighting: the data and its any are used untested
        const uint8_t *any = Call<kDataAny, const uint8_t *>(data.p);
        if (mTerrain.mObject) Call<25>(mTerrain.mObject, uint32_t(any[4]));
        return true;
    }
    case 0xac730900: // lotTerrainCanvas, lotTerrainPaints, lotWater: the terrain's slot 24(index 0..2, on)
    case 0xac730901:
    case 0xac730902: {
        const uint32_t index = Call<kMessageType>(message) + 0x538cf700u;
        const uint8_t *any = Call<kDataAny, const uint8_t *>(data.p);
        if (mTerrain.mObject) Call<24>(mTerrain.mObject, index, uint32_t(any[4]));
        return true;
    }
    case 0x6c618b86: // displayNeighborhoodFlora / Roads / RoadsWithModel / Props: the occupants' slot 6(kind, on)
    case 0x4c618b90:
    case 0xac618b98:
    case 0x0c618ba0: {
        const uint32_t again = Call<kMessageType>(message);
        uint32_t kind;
        if (again == 0x6c618b86) kind = 0;
        else if (again == 0x4c618b90) kind = 1;
        else if (again == 0xac618b98) kind = 2;
        else if (again == 0x0c618ba0) kind = 3;
        else return false;
        if (mNHoodOccupants.mObject && data.p && Call<kDataHasAny, uint8_t>(data.p))
            Call<6>(mNHoodOccupants.mObject, kind, uint32_t(Call<kDataAny, const uint8_t *>(data.p)[4]));
        return true;
    }
    case 0xec3f494d: // carsCompact: the +0xb39000 object's slot 31(3, on)
        if (const uint8_t *any = AnyOf(data.p)) {
            void *cars = OriginalFunction<void *(__cdecl *)(), 0xb39000>()();
            Call<31>(cars, 3u, uint32_t(any[4]));
        }
        return true;
    case 0x2b4d18fb: Call<kDataValue>(data.p); return true;

    // ---- materials ----
    case 0x925df53f:
        Call<30>(GetCachedQueriedObject(), GameAddress<const char>(0xdae954));
        RefreshMaterials();
        return true;
    case 0x522ced95:
    case 0xb23b7d66:
    case 0xb23b7d6f:
    case 0xd23b7d7c: ReloadMaterials(); return true;

    // ---- the lot's level range, then message 0xd5 ----
    case 0x2b4d1900: {
        void *placement = OriginalFunction<void *(__cdecl *)(), 0x20e0b2>()();
        const int32_t first = Call<88, int32_t>(placement);
        const int32_t last = Call<89, int32_t>(placement);
        SetLevelRange(first, last);
        auto *raw = static_cast<cRZMessage2Standard *>(RZMemory::Malloc(cRZMessage2Standard::kSize));
        if (raw) {
            raw->mVTable = &ts2_rva_da4064;
            raw->mRefCount = 0;
            raw->mFlags = 0;
            raw->mId = 0;
            for (uint32_t &v : raw->mValue) v = 0;
            raw->mPtr20 = raw->mPtr24 = raw->mPtr28 = nullptr;
        }
        LocalRef posted;
        posted.p = raw;
        if (posted.p) Call<1>(posted.p);
        Call<5>(posted.p, 0xd5u); // used untested, as the original
        void *server = GetCachedMessageServer();
        Call<3>(server, posted.p);
        return true;
    }

    // ---- the lot options (lotSkirtSizeIncrease, enableLotImpostersInLot, enableNeighborhoodOccupantsInLot) ----
    case 0x2f9dc7f9: {
        uint32_t lot = 0;
        if (!CurrentLotId(lot, true)) return true;
        if (mLotSkirt.mObject) {
            Call<16>(mLotSkirt.mObject);
            Call<7>(mLotSkirt.mObject, 0u);
        }
        if (mRef90.mObject) {
            Setting imposters(kEnableLotImpostersName);
            if (imposters.on) Call<6>(mRef90.mObject, lot);
        }
        if (mNHoodOccupants.mObject) {
            Setting occupants(kEnableOccupantsName);
            if (occupants.on) {
                Call<4>(mNHoodOccupants.mObject);
                RecreateOccupants(mNHoodOccupants);
            }
        }
        if (mLotSkirt.mObject) Call<7>(mLotSkirt.mObject, 1u);
        return true;
    }
    case 0x4f9dc80b: {
        uint32_t lot = 0;
        if (!CurrentLotId(lot, true)) return true;
        if (!mRef90.mObject) return true;
        Setting imposters(kEnableLotImpostersName);
        if (imposters.on) {
            Call<7>(mRef90.mObject, 1u);
            Call<6>(mRef90.mObject, lot);
        } else {
            Call<7>(mRef90.mObject, 0u);
        }
        return true;
    }
    case 0x4f9dc81a: {
        uint32_t unused = 0;
        if (!CurrentLotId(unused, false)) return true;
        Setting occupants(kEnableOccupantsName);
        if (occupants.on) {
            if (mNHoodOccupants.mObject) Call<4>(mNHoodOccupants.mObject);
            RecreateOccupants(mNHoodOccupants);
        } else if (void *current = mNHoodOccupants.mObject) {
            Call<4>(current);
            if (void *old = mNHoodOccupants.mObject) {
                mNHoodOccupants.mObject = nullptr;
                ReleaseRef(old);
            }
        }
        return true;
    }
    default: return false;
    }
}

void cTSSGSystem::SetLevelRange(int32_t first, int32_t last)
{
    void *placement = OriginalFunction<void *(__cdecl *)(), 0x20e0b2>()();
    Call<85>(placement);
    Call<86>(placement);
    if (void *scene = mSceneManager.mObject) {
        Call<19>(scene);
        int32_t had = Call<20, int32_t>(mSceneManager.mObject);
        Call<5>(mSceneManager.mObject, first, last);
        if (last > had)
            for (int32_t level = had + 1; level <= last; ++level) Call<12>(mSceneManager.mObject, level, 0u);
    }
    if (mFloorManager.mObject) Call<6>(mFloorManager.mObject, first, last);
    if (mWallManager.mObject) Call<9>(mWallManager.mObject, first, last);
    if (mFenceManager.mObject) Call<12>(mFenceManager.mObject, first, last);
    if (mRoofManager.mObject) Call<7>(mRoofManager.mObject, first, last);
    if (mRugManager.mObject) Call<7>(mRugManager.mObject, first, last);
    if (mAwningManager.mObject) Call<7>(mAwningManager.mObject, first, last);
    if (mCurvedPoolManager.mObject) Call<7>(mCurvedPoolManager.mObject);
    if (mTerrainCutoutManager.mObject) Call<7>(mTerrainCutoutManager.mObject);
}

void cTSSGSystem::ReloadMaterials()
{
    // 0x2556a0: the ten material names, in the original's order.
    static constexpr uint32_t kNames[] = {0xdae8a8, 0xdae8c4, 0xdae8e4, 0xdae8fc, 0xdae91c,
                                          0xdae92c, 0xdae948, 0xdae964, 0xdae980, 0xdae994};
    void *materials = GetCachedQueriedObject();
    for (uint32_t name : kNames) Call<30>(materials, GameAddress<const char>(name));
    RefreshMaterials();
}

void cTSSGSystem::RefreshMaterials()
{
    void *detail = nGZSceneGraph::GetDetailObject();
    if (!detail) return;
    void *source = Call<42, void *>(detail);
    LocalRef refresher;
    if (source && Call<0, uint8_t>(source, 0x2ca4c07du, &refresher.p)) Call<6>(refresher.p);
}

void cTSSGSystem::TouchEntry(uint32_t id)
{
    void *registry = Call<10, void *>(GetCachedSingleton());
    void *entry = Call<16, void *>(registry, int32_t(int16_t(id)), 1u);
    if (!entry) return;
    void *owner = Call<10, void *>(entry);
    if (!owner) return;
    void *target = Call<67, void *>(owner);
    struct Name {
        cRZString s;
        Name() { s.Construct(); }
        ~Name() { s.Destruct(); }
    } name;
    Call<12>(target, &name.s);
}

} // namespace nTSSG
