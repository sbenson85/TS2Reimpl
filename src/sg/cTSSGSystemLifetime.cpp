// nTSSG::cTSSGSystem: the constructor (0x246110) and destructor (0x246f00) (lane KE). See src/sg/cTSSGSystem.h.
#include "sg/cTSSGSystem.h"

#include "game/game.h"
#include "game/vtable.h"
#include "rz/RZAutoRef.h"
#include "simantics/cTSBehaviorTree.h" // RefPtrVector

#include <cstring>
#include <limits>
#include <memory>
#include <new>

namespace nTSSG {

namespace {

// "No tick yet": the start of a statistics bucket that has not started, and the frame counter's frozen value.
constexpr int32_t kNoTick = int32_t(0x80000100u);
constexpr int32_t kStopwatchMode = 4;
// The game's default camera position and its forward axis (three floats each), copied in by the constructor.
constexpr uint32_t kDefaultCameraPosition = 0x10cc860;
constexpr uint32_t kForwardAxis = 0xff78f4;

// The messages the system listens to, in the constructor's order.
constexpr uint32_t kMessageIds[] = {0xbb,       0xbc,       0xb2, 0xb3, 0xb4, 0xb5, 0x9a, 0x8d,
                                    0x2b4d1900, 0x2b4d18fb, 0xb8, 0xb9, 0x9d, 0xcf, 0xd0, 0xd4,
                                    0xd3,       0xd7,       0xd8, 0x522ced95, 0xb23b7d6f, 0x925df53f,
                                    0xb23b7d66, 0xd23b7d7c};

// The eight colours slot 78 hands out (the constructor's final values).
constexpr float kColors[8][4] = {
    {0.9f, 1.0f, 0.1f, 0.1f}, {0.9f, 0.5f, 0.5f, 0.5f}, {1.0f, 0.1f, 0.1f, 0.1f}, {1.0f, 0.5f, 0.5f, 0.5f},
    {1.0f, 1.0f, 1.0f, 0.0f}, {0.3f, 1.0f, 1.0f, 0.0f}, {1.0f, 0.3f, 0.3f, 0.3f}, {0.55f, 1.0f, 1.0f, 0.0f},
};

void ReleaseObject(void *object) { VirtualCall<uint32_t(__thiscall *)(void *), 2>(object); }

// A plain reference: released (slot 2) when set; the field is left as it is.
void ReleaseRef(SGRef &ref)
{
    if (ref.mObject) ReleaseObject(ref.mObject);
}

// A reference to an object built with virtual inheritance: released through its virtual base (the virtual-base table at
// +vbptr, entry 1) when set.
void ReleaseVirtual(void *object, uint32_t vbptr)
{
    if (!object) return;
    auto *at = static_cast<uint8_t *>(object) + vbptr;
    const int32_t *table = *reinterpret_cast<int32_t *const *>(at);
    ReleaseObject(at + table[1]);
}

} // namespace

cTSSGSystem::cTSSGSystem()
    : mVTable(GameAddress<const void>(kVTable)), mRefCount(0), mMessageVTable(GameAddress<const void>(kMessageVTable)),
      mServiceVTable(GameAddress<const void>(kServiceVTable)), mManagersCreated(0), mServiceRunning(0), mMessageIds()
{
    mFrameWatch.Construct(kStopwatchMode);
    mUnknown38 = 0;
    mAnimClock = 0.0f;
    mStartupWatch.Construct(kStopwatchMode);

    for (SGRef *ref : {&mScene, &mAnimSystem, &mPersistService, &mRef64, &mSceneManager, &mWallManager, &mFenceManager,
                       &mPoolManager, &mLotSkirt, &mFloorManager, &mTerrain, &mRef84, &mNHoodOccupants,
                       &mVideoBroadcastManager, &mRef90, &mRef94, &mQueryStrategyManager, &mLightRigManager,
                       &mLightMapManager, &mLightingManager, &mShadowManager, &mRefAC, &mRefB0, &mRefB4, &mRoofManager,
                       &mLotImposterManager, &mRefC0})
        ref->mObject = nullptr;
    for (SGRef &overlay : mOverlays) overlay.mObject = nullptr;
    for (SGRef *ref : {&mRugManager, &mCurvedPoolManager, &mAwningManager, &mSeasonManager, &mTerrainCutoutManager,
                       &mTerrainSnowManager, &m3DUIManager, &mRef104, &mCommandParser})
        ref->mObject = nullptr;
    mParserTarget = nullptr;
    mRef110.mObject = nullptr;
    mRef114.mObject = nullptr;
    mRef118.mObject = nullptr;
    mRef11C.mObject = nullptr;
    mCinematic = nullptr;
    mRef124.mObject = nullptr;
    mCinematicRef128 = nullptr;
    // (The five vectors are built empty as members, before this body.)
    mCinematicSaved[0] = 0;
    mCinematicSaved[1] = 4;
    mCinematicSaved[2] = 0;
    mObjectHidingValue = 0;
    mObjectHiding = 0;
    mField161 = 0;
    mCinematicHeld = 0;
    mField163 = 0;
    mCinematicRestore = 100;
    mLotFirstLoadDone = 0;
    mLotRefreshPending = 0;
    mFlag16A = 0;
    mFlag16B = 0;
    mCaptureSuspended = 0;
    // Render options: the dwords 1 and 0x01010100, then a zero byte (+0x191..+0x193 left alone).
    static constexpr uint8_t kRenderOptions[9] = {1, 0, 0, 0, 0, 1, 1, 1, 0};
    std::memcpy(mRenderOptions, kRenderOptions, sizeof(kRenderOptions));
    mCounter194 = kNoTick;
    mCounter198 = 0;
    for (int bucket = 0; bucket < 10; ++bucket) {
        mStatBucketStart[bucket] = kNoTick;
        mPersistHitsFlagged[bucket] = 0;
        mCacheHits[bucket] = 0;
        mCacheHitsSlot59[bucket] = 0;
        mPersistHits[bucket] = 0;
        mPersistTimeFlagged[bucket] = 0;
        mPersistTime[bucket] = 0;
    }
    mStatBucket = 0;
    mLotTunables[0] = 1.0f;
    mLotTunables[1] = 3.0f;
    mLotTunables[2] = 21.0f;
    mField2C4 = 0;
    mRef2C8.mObject = nullptr;
    mRef2CC.mObject = nullptr;
    std::memcpy(mColors, kColors, sizeof(mColors));
    mPulseDepth = 0.2f;
    mPulseRate = 1.0f;
    mRef358.mObject = nullptr;
    mObjectFadeSet = 1;
    mObjectFade.distance = 10.0f;
    mObjectFade.falloff = 4.0f;
    mObjectFade.allObjects = 0;
    mObjectFade.cineCam = 0;
    mObjectFade.cameraDistance = 10.0f;
    mObjectFade.cameraRatio = 0.25f;
    std::memcpy(mCameraPosition, GameAddress<const float>(kDefaultCameraPosition), sizeof(mCameraPosition));
    std::memcpy(mCameraDirection, GameAddress<const float>(kForwardAxis), sizeof(mCameraDirection));
    for (int axis = 0; axis < 3; ++axis) {
        mActorBounds[axis] = std::numeric_limits<float>::max();
        mActorBounds[axis + 3] = -std::numeric_limits<float>::max();
    }
    mField3A4 = 0;
    mNhoodRefreshPending = 0;
    mMaterialReloadDelay = 0;
    mRef3B0.mObject = nullptr;
    mAssetCollector.mObject = nullptr;

    for (uint32_t id : kMessageIds) mMessageIds.insert(id);
}

void cTSSGSystem::Destruct()
{
    mVTable = GameAddress<const void>(kVTable);
    mMessageVTable = GameAddress<const void>(kMessageVTable);
    mServiceVTable = GameAddress<const void>(kServiceVTable);
    EndCinematic(1);

    ReleaseRef(mAssetCollector);
    ReleaseRef(mRef3B0);
    ReleaseRef(mRef358);
    ReleaseRef(mRef2CC);
    ReleaseRef(mRef2C8);
    std::destroy_at(&mEntryHandles);
    reinterpret_cast<RefPtrVector &>(mHeldObjects).Destroy();
    std::destroy_at(&mCinematicRefs);
    std::destroy_at(&mCinematicExtras);
    std::destroy_at(&mCinematicActors);
    ReleaseVirtual(mCinematicRef128, 4);
    ReleaseVirtual(mRef124.mObject, 4);
    ReleaseVirtual(mCinematic, 4);
    ReleaseRef(mRef11C);
    ReleaseRef(mRef118);
    ReleaseRef(mRef114);
    ReleaseRef(mRef110);
    ReleaseVirtual(mParserTarget, 8);
    for (SGRef *ref : {&mCommandParser, &mRef104, &m3DUIManager, &mTerrainSnowManager, &mTerrainCutoutManager,
                       &mSeasonManager, &mAwningManager, &mCurvedPoolManager, &mRugManager})
        ReleaseRef(*ref);
    for (int i = 8; i >= 0; --i) reinterpret_cast<RZAutoRef &>(mOverlays[i]).ReleaseUnguarded();
    ReleaseVirtual(mRefC0.mObject, 4);
    ReleaseRef(mLotImposterManager);
    ReleaseRef(mRoofManager);
    ReleaseRef(mRefB4);
    ReleaseVirtual(mRefB0.mObject, 4);
    ReleaseVirtual(mRefAC.mObject, 4);
    for (SGRef *ref : {&mShadowManager, &mLightingManager, &mLightMapManager, &mLightRigManager, &mQueryStrategyManager,
                       &mRef94, &mRef90, &mVideoBroadcastManager, &mNHoodOccupants, &mRef84, &mTerrain, &mFloorManager,
                       &mLotSkirt, &mPoolManager, &mFenceManager, &mWallManager, &mSceneManager, &mRef64,
                       &mPersistService, &mAnimSystem, &mScene})
        ReleaseRef(*ref);
    // The stopwatches have nothing to destroy.
    std::destroy_at(&mMessageIds);
    mVTable = GameAddress<const void>(kBaseVTable);
}

cTSSGSystem *__fastcall cTSSGSystemAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cTSSGSystem;
}

} // namespace nTSSG
