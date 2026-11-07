// nTSSG::cTSSGSystem: the per-frame update (lane JD; slot 17). See src/sg/cTSSGSystem.h.
#include "sg/DetailObject.h"
#include "sg/cTSSGSystem.h"

#include "compgeom/CompGeom.h"
#include "compgeom/Rigid.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"
#include "gz/cGZPropertySet.h"
#include "gz/cIGZPropertySet.h"
#include "misc/UtilitiesE.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/StaticGetters2.h"
#include "sg/SGLocalString.h"
#include "simantics/SimGeometry.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace {

template <int kSlot, typename R = uint32_t, typename... A> __forceinline R Call(const void *object, A... args)
{
    return VirtualCall<R(__thiscall *)(void *, A...), kSlot>(object, args...);
}

// A virtual base of an object built with virtual inheritance: the virtual-base table pointer at +vbptr, entry `entry`.
void *VirtualBase(void *object, uint32_t vbptr, int entry)
{
    auto *at = static_cast<uint8_t *>(object) + vbptr;
    const int32_t *table = *reinterpret_cast<int32_t *const *>(at);
    return at + table[entry];
}

// A tick or counter that has not been set.
constexpr int32_t kNoTick = int32_t(0x80000100);
// The statistics buckets (one a second of ticks, ten of them).
constexpr uint32_t kStatBuckets = 10;
constexpr int32_t kStatBucketTicks = 1000;
// Simulation time running backwards by more than this many ticks resets the reference.
constexpr int32_t kSimRewind = 10000;
// GetCachedSlot23's slot 5 for a lot.
constexpr uint32_t kLotWorld = 0x0bea28c2;

// Game constants (read at run time): 0.001 as a float and as a double, the clock's wrap (3600 seconds), pi, 1, 0.5.
constexpr uint32_t kMilliF = 0xda20dc;
constexpr uint32_t kMilliD = 0xdaf070;
constexpr uint32_t kClockWrap = 0xdaf078;
constexpr uint32_t kPi = 0xda77d8;
constexpr uint32_t kOne = 0xd9c698;
constexpr uint32_t kHalf32 = 0xd9c694;
constexpr uint32_t kHalf = 0xda1870;
float ConstF(uint32_t rva) { return *GameAddress<const float>(rva); }
double ConstD(uint32_t rva) { return *GameAddress<const double>(rva); }

// Game text: the settings' shader switch, the object fade properties.
constexpr uint32_t kReparseShaders = 0xdae768;
constexpr uint32_t kFadeDistance = 0xdae830;
constexpr uint32_t kFadeFalloff = 0xdae844;
constexpr uint32_t kFadeAllObjects = 0xdae858;
constexpr uint32_t kFadeCineCam = 0xdae870;

// The settings service's property set (GetServiceObject slot 5 with these ids): slot 9 reads a property into an any,
// slot 3 sets one.
constexpr uint32_t kSettingsClass = 0x0ac1887e;
constexpr uint32_t kSettingsIid = 0x4bb0d269;
constexpr int kSettingsGet = 9;
constexpr int kSettingsSet = 3;

// The global the detail object's slot 43 value goes to every frame.
constexpr uint32_t kDetailLevelCell = 0xff81d8;

// Callees nobody has replaced yet.
void RestartWatch(cRZStopwatch *watch) { OriginalFunction<void(__thiscall *)(cRZStopwatch *), 0x1c95ec>()(watch); }
void *LiveHousehold() { return OriginalFunction<void *(__cdecl *)(), 0x20df9a>()(); }
void *DetailObject() { return nGZSceneGraph::GetDetailObject(); }
void *CarManager() { return OriginalFunction<void *(__cdecl *)(), 0xb39000>()(); }
uint8_t IsNight(void *time) { return OriginalFunction<uint8_t(__cdecl *)(void *), 0x7e2180>()(time); }
void RefreshNhoodShaders() { OriginalFunction<void(__cdecl *)(), 0x24b870>()(); }

void ReleaseRef(void *object) { Call<2>(object); }

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

// A stopwatch count in milliseconds as seconds, the way the original converts it (unsigned to double, to float, times
// the float 0.001).
float Seconds(uint32_t milliseconds) { return static_cast<float>(static_cast<double>(milliseconds)) * ConstF(kMilliF); }


// The forward axis the camera's rotation turns (a game global, three floats).
constexpr uint32_t kForwardAxis = 0xff78f4;
// Game constants: the share of an actor's height cut from its top and from its bottom (the head and the feet), the share
// of the bounds' half-diagonal that counts.
constexpr uint32_t kTopCut = 0xdaf068;
constexpr uint32_t kBottomCut = 0xdadf5c;
constexpr uint32_t kRadiusShare = 0xdaf06c;
// An actor whose trimmed bounds' half-diagonal is under this counts only when its slot 120 object's slot 98 is this kind.
constexpr uint32_t kSmallActorKind = 2;
// The bounds query's flags (0xae7d00).
constexpr uint32_t kBoundsFlags = 0xf;

struct Box {
    float min[3];
    float max[3];
};

// An empty box: every minimum the largest float, every maximum its negative.
void Empty(float *box)
{
    for (int i = 0; i < 3; ++i) box[i] = std::numeric_limits<float>::max(), box[i + 3] = -std::numeric_limits<float>::max();
}

void ActorBounds(Box *box, void *scene, void *actor)
{
    OriginalFunction<void(__cdecl *)(Box *, uint32_t, void *, void *), 0xae7d00>()(box, kBoundsFlags, scene, actor);
}
void Grow(float *bounds, const Box *box)
{
    reinterpret_cast<nGZCompGeom::BoundBox *>(bounds)->Grow(*reinterpret_cast<const nGZCompGeom::BoundBox *>(box));
}
float Dot(const float *a, const float *b) { return static_cast<float>(nGZCompGeom::Dot87(a, b)); } // 0x18fbf0, ours
const char *ActorName(void *actor) { return OriginalFunction<const char *(__cdecl *)(void *), 0xae7900>()(actor); }

// The length of (x, y, z), squared in float, the root taken in double.
float Length(float x, float y, float z) { return static_cast<float>(std::sqrt(static_cast<double>(x * x + y * y + z * z))); }

} // namespace

namespace nTSSG {

void cTSSGSystem::Update()
{
    if (!mFlag16A || !mFlag16B) return;
    uint32_t elapsed = mFrameWatch.Elapsed();
    RestartWatch(&mFrameWatch);
    if (mUnknown38 && elapsed < mUnknown38) elapsed = mUnknown38;
    uint32_t simElapsed = Call<26, uint8_t>(LiveHousehold()) ? 0 : elapsed;
    OriginalFunction<void(__thiscall *)(cTSSGSystem *), 0x24ab50>()(this);

    ScopedRef settings{nullptr};
    if (void *service = GetServiceObject()) Call<5>(service, kSettingsClass, kSettingsIid, &settings.mObject);
    if (settings.mObject) {
        AnyBoolean reparse{GameAddress<const void>(gzany::kBooleanVTable), 0, {}};
        {
            GZServiceString key(GameAddress<const char>(kReparseShaders));
            Call<kSettingsGet>(settings.mObject, &key, &reparse);
        }
        if (reparse.mValue) {
            {
                GZServiceString key(GameAddress<const char>(kReparseShaders));
                AnyBoolean off{GameAddress<const void>(gzany::kBooleanVTable), 0, {}};
                Call<kSettingsSet>(settings.mObject, &key, &off);
            }
            Call<24>(GetCachedQueriedObject(), 0u, 1u);
        }
    }
    Call<8>(Call<12, void *>(mScene.mObject));
    *GameAddress<uint32_t>(kDetailLevelCell) = Call<43>(DetailObject());

    const uint32_t clockSpan = Call<5>(GetCachedSlot23()) == kLotWorld ? simElapsed : elapsed;
    mAnimClock = Seconds(clockSpan) + mAnimClock;
    if (mAnimClock > ConstF(kClockWrap)) mAnimClock = mAnimClock - ConstF(kClockWrap);

    void *clock = Call<52, void *>(this);
    const int32_t tick = Call<11, int32_t>(VirtualBase(clock, 4, 2));
    int32_t tickDelta = 0;
    if (tick != mCounter194) {
        Call<2>(Call<51, void *>(this), 1u, 1u);
        const int32_t previous = mCounter194;
        mCounter194 = tick;
        Call<29>(this, 0u);
        tickDelta = previous == kNoTick ? 0 : int32_t(uint32_t(tick) - uint32_t(previous));
    }
    if (mRenderOptions[5]) {
        const int32_t simTick = Call<12, int32_t>(VirtualBase(clock, 4, 2));
        if (simTick > mCounter198) {
            simElapsed = uint32_t(simTick) - uint32_t(mCounter198);
            mCounter198 = simTick;
        } else {
            simElapsed = 0;
            if (int32_t(uint32_t(mCounter198) - uint32_t(simTick)) > kSimRewind) mCounter198 = simTick;
        }
    }

    // Resource statistics: a new bucket every second of ticks; all of them reset when the next one was never started.
    const uint32_t bucket = mStatBucket;
    const uint32_t next = bucket < kStatBuckets - 1 ? bucket + 1 : 0;
    if (mStatBucketStart[next] == kNoTick) {
        for (uint32_t i = 0; i < kStatBuckets; ++i) {
            mStatBucketStart[i] = tick;
            mPersistHitsFlagged[i] = 0;
            mCacheHits[i] = 0;
            mCacheHitsSlot59[i] = 0;
            mPersistHits[i] = 0;
            mPersistTimeFlagged[i] = 0;
            mPersistTime[i] = 0;
        }
    }
    // The stored bucket is not range-checked here (only 0..9 are ever stored).
    if (int32_t(uint32_t(tick) - uint32_t(mStatBucketStart[bucket])) >= kStatBucketTicks) {
        uint32_t fresh = bucket + 1;
        mStatBucket = fresh;
        if (fresh >= kStatBuckets) mStatBucket = fresh = 0;
        mStatBucketStart[fresh] = tick;
        mPersistHitsFlagged[mStatBucket] = 0;
        mCacheHits[mStatBucket] = 0;
        mCacheHitsSlot59[mStatBucket] = 0;
        mPersistHits[mStatBucket] = 0;
        mPersistTimeFlagged[mStatBucket] = 0;
        mPersistTime[mStatBucket] = 0;
    }

    Call<7>(mLightRigManager.mObject);
    Call<25>(mRefB4.mObject, elapsed);
    if (void *properties = Call<37, void *>(mRefB4.mObject)) {
        // The original reads the two flags through the stack slot that still holds the clock pointer.
        uint32_t scratch = uint32_t(reinterpret_cast<uintptr_t>(clock));
        float &value = reinterpret_cast<float &>(scratch);
        mObjectFadeSet = Call<17, uint8_t>(properties, GameAddress<const char>(kFadeDistance), &mObjectFade.distance);
        if (mObjectFadeSet) {
            Call<17, uint8_t>(properties, GameAddress<const char>(kFadeFalloff), &mObjectFade.falloff);
            if (Call<17, uint8_t>(properties, GameAddress<const char>(kFadeAllObjects), &value))
                mObjectFade.allObjects = value > 0.0f;
            if (Call<17, uint8_t>(properties, GameAddress<const char>(kFadeCineCam), &value))
                mObjectFade.cineCam = value > 0.0f && !mField161 ? 1 : 0;
        }
        if (Call<5>(GetCachedSlot23()) == kLotWorld) {
            uint8_t time[16];
            const uint8_t night = IsNight(Call<14, void *>(properties, time));
            // The original pushes the whole register the flag was moved into: the tick's upper 24 bits come along.
            Call<31>(CarManager(), 0xbu, (uint32_t(tick) & 0xffffff00u) | night);
            CountedRef app(Call<33, void *>(GetCachedSingleton()));
            if (app.p) Call<40>(app.p, uint32_t(night ^ 1));
        }
    }
    if (mObjectFade.cineCam) Call<99>(this);
    if (mNHoodOccupants.mObject) Call<10>(mNHoodOccupants.mObject);
    if (mRef84.mObject) Call<17>(mRef84.mObject);
    if (mLotRefreshPending) {
        Call<5>(mRefC0.mObject);
        mLotRefreshPending = 0;
    }
    Call<15>(mSceneManager.mObject);
    if (void *view = Call<20, void *>(Call<22, void *>(GetCachedSingleton()))) Call<5>(view);
    Call<23>(mWallManager.mObject);
    Call<2>(mRefC0.mObject);
    Call<15>(mFloorManager.mObject);
    if (mTerrain.mObject) Call<7>(mTerrain.mObject);
    if (mRugManager.mObject) Call<6>(mRugManager.mObject);
    if (mCurvedPoolManager.mObject) Call<6>(mCurvedPoolManager.mObject);
    if (mTerrainCutoutManager.mObject) Call<6>(mTerrainCutoutManager.mObject);
    if (mTerrainSnowManager.mObject) Call<8>(mTerrainSnowManager.mObject);
    if (mAwningManager.mObject) Call<6>(mAwningManager.mObject);
    if (mRef90.mObject) Call<8>(mRef90.mObject);
    Call<7>(mLightMapManager.mObject);
    Call<17>(mFenceManager.mObject);

    if (mRef358.mObject) {
        // The pulse: 1 - depth + depth * (sin(2 pi rate t) + 1) / 2, partly in double as the original computes it.
        float phase = Seconds(mStartupWatch.Elapsed());
        phase = phase + phase;
        phase = phase * ConstF(kPi);
        phase = phase * mPulseRate;
        const float wave = static_cast<float>(std::sin(static_cast<double>(phase))) + ConstF(kOne);
        const float floor = ConstF(kOne) - mPulseDepth;
        const float pulse = static_cast<float>(static_cast<double>(wave) * (static_cast<double>(mPulseDepth) * ConstD(kHalf)) +
                                               static_cast<double>(floor));
        if (Call<52, float>(mRef358.mObject) != pulse) Call<42>(mRef358.mObject, pulse);
    }
    if (void *cars = CarManager()) {
        Call<40>(cars, Call<18, void *>(mSceneManager.mObject));
        Call<20>(cars, tickDelta, elapsed);
    }
    if (mPoolManager.mObject)
        Call<5>(mPoolManager.mObject, static_cast<float>(static_cast<double>(simElapsed) * ConstD(kMilliD)));
    if (mRef94.mObject) Call<5>(mRef94.mObject);
    Call<10>(mLightingManager.mObject);
    if (mAssetCollector.mObject) Call<8>(mAssetCollector.mObject);
    Call<5>(mShadowManager.mObject);
    if (mNhoodRefreshPending) {
        mNhoodRefreshPending = 0;
        RefreshNhoodShaders();
    }
    if (mMaterialReloadDelay > 0 && --mMaterialReloadDelay == 0) ReloadMaterials();
    settings.ReleaseObject();
}

void cTSSGSystem::UpdateCinematicFade()
{
    void *camera = Call<18, void *>(mSceneManager.mObject);
    // The camera's transform: the position, then the rotation (a quaternion) at +0xc.
    float transform[16];
    Call<140>(camera, transform, 0u);
    std::memcpy(mCameraPosition, transform, sizeof(mCameraPosition));
    float forward[3];
    std::memcpy(forward, GameAddress<const float>(kForwardAxis), sizeof(forward));
    simgeom::RotateByQuaternion(forward, transform + 3);
    std::memcpy(mCameraDirection, forward, sizeof(mCameraDirection));
    if (Call<5>(GetCachedSlot23()) == kLotWorld) {
        // The original pushes a whole stack word whose upper bytes it never wrote; the flag is its low byte.
        const uint8_t night = IsNight(mCameraPosition);
        Call<31>(CarManager(), 0xbu, uint32_t(night));
        CountedRef app(Call<33, void *>(GetCachedSingleton()));
        if (app.p) Call<40>(app.p, uint32_t(night ^ 1));
    }

    void *scene = Call<17, void *>(mSceneManager.mObject);
    Empty(mActorBounds);
    for (cTSSGCinematicActor *it = mCinematicActors.data(), *end = it + mCinematicActors.size(); it != end; ++it) {
        void *actor = it->mObject.mObject;
        if (!actor || !Call<120, void *>(actor) || !Call<188, uint8_t>(Call<120, void *>(actor))) continue;
        Box box;
        Empty(box.min);
        ActorBounds(&box, scene, actor);
        // An empty box (a minimum above its maximum) is left out; NaN bounds are not.
        if (!(box.min[0] > box.max[0]) && !(box.min[1] > box.max[1]) && !(box.min[2] > box.max[2])) {
            const float height = box.max[2] - box.min[2];
            box.max[2] = box.max[2] - height * ConstF(kTopCut);
            box.min[2] = height * ConstF(kBottomCut) + box.min[2];
            const float half = ConstF(kHalf32);
            const float radius = Length((box.max[0] - box.min[0]) * half, (box.max[1] - box.min[1]) * half,
                                        (box.max[2] - box.min[2]) * half);
            if (radius >= half || Call<98>(Call<120, void *>(actor)) == kSmallActorKind) Grow(mActorBounds, &box);
        }
        // The original copies the actor's name into a local string and drops it.
        SGLocalString name;
        if (ActorName(actor)) name.value.mString.assign(ActorName(actor));
    }
    if (mActorBounds[0] > mActorBounds[3] || mActorBounds[1] > mActorBounds[4] || mActorBounds[2] > mActorBounds[5])
        for (float &b : mActorBounds) b = 0.0f;

    const float half = ConstF(kHalf32);
    float toCentre[3];
    for (int i = 0; i < 3; ++i)
        toCentre[i] = ((mActorBounds[i + 3] - mActorBounds[i]) * half + mActorBounds[i]) - mCameraPosition[i];
    mObjectFade.cameraDistance = Dot(toCentre, mCameraDirection);
    if (mObjectFade.cameraDistance <= 0.0f) mObjectFade.cameraDistance = 0.0f;
    const float hx = (mActorBounds[3] - mActorBounds[0]) * half;
    const float hy = (mActorBounds[4] - mActorBounds[1]) * half;
    const float hz = (mActorBounds[5] - mActorBounds[2]) * half;
    const auto *view = Call<139, const uint8_t *>(camera, 0u);
    const float radius = Length(hy, hx, hz) * ConstF(kRadiusShare);
    float fov;
    constexpr size_t kViewFovOffset = 0x3c; // the camera view's field of view (no typed view struct exists yet)
    std::memcpy(&fov, view + kViewFovOffset, sizeof(fov));
    const float spread = static_cast<float>(std::tan(static_cast<double>(fov * half))) * mObjectFade.cameraDistance;
    mObjectFade.cameraRatio = radius / spread;
    if (mObjectFade.cameraRatio > ConstF(kOne)) mObjectFade.cameraRatio = 1.0f;
}

} // namespace nTSSG
