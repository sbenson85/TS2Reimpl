// nTSSG::cLightingManager, the light builders' leaves (lane 261109j, scenegraph LG4a): the largest / smallest vector
// predicates, the light removal and lerp evaluation the Rebuild functions and the lifecycle slots call, the sun height
// map rebuild and the portal sampler. See sg/cLightingManager.h.
#include "sg/cLightingManager.h"
#include "sg/cLightingManagerDetail.h"

#include "compgeom/ConvexMath.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "rz/StopwatchMore.h"
#include "sg/cLerpLightSet.h"
#include "sg/cLightMapManager.h" // LightMapStopwatch

#include <algorithm>
#include <cmath>

namespace nTSSG {

namespace {

constexpr uint32_t kFadeSwitch = 0xffde34;    // the byte that makes the removers clear the lerp sets, not fade them
constexpr uint32_t kObjectsFilterVTable = 0xdb608c; // nTSSG::cTSSGObjectsFilter
constexpr uint32_t kIidPortalObject = 0x8a561e23;   // asked of the portal records' objects (class not identified)
constexpr uint32_t kSceneObjectFlag = 0xfa1c39ed;   // what the scene objects are asked for in 0x2e4b50

using nGZCompGeom::convex::AddS;
using nGZCompGeom::convex::DivS;
using nGZCompGeom::convex::MulS;
using nGZCompGeom::convex::Negated;
using nGZCompGeom::convex::SubS;

// The cRZStopwatch restart (0x1c95ec, still the game's).
void Restart(cRZStopwatch *watch) { OriginalFunction<void(__thiscall *)(cRZStopwatch *), 0x1c95ec>()(watch); }

void RemoveFromScene(void *sceneManager, void *light)
{
    RawSlot<9, void>(sceneManager, light); // receiver: nTSSG::cSceneManager vf9 0x2bfae0
}

} // namespace

cBuiltRef::cBuiltRef(const cBuiltRef &other) : mObject(other.mObject)
{
    if (mObject) RawSlot<1, void>(mObject); // receiver: the light or portal object (cIGZUnknown AddRef)
}

cBuiltRef &cBuiltRef::operator=(const cBuiltRef &other)
{
    void *const old = mObject;
    if (other.mObject != old) {
        if (other.mObject) RawSlot<1, void>(other.mObject); // receiver: as the copy constructor
        mObject = other.mObject;
        if (old) RawSlot<2, void>(old); // receiver: as the destructor
    }
    return *this;
}

cBuiltRef::~cBuiltRef()
{
    if (mObject) RawSlot<2, void>(mObject); // receiver: the light or portal object (cIGZUnknown Release)
}

cLerpRef::cLerpRef(const cLerpRef &other) : mSet(other.mSet)
{
    if (mSet) RawSlot<2, void>(mSet); // receiver: nTSSG::cLerpLightSet (cRZRefCount AddRef, slot 2)
}

cLerpRef::~cLerpRef()
{
    if (mSet) RawSlot<1, void>(mSet); // receiver: nTSSG::cLerpLightSet (cRZRefCount Release, slot 1)
}

cSkySample::cSkySample() : mWeights{}, mTileCount(0), mWallCount(0), mPortalCount(0)
{
    const float *const zero = GameAddress<const float>(kZeroVector);
    for (int32_t i = 0; i < 6; ++i) {
        mVectors[i][0] = zero[0];
        mVectors[i][1] = zero[1];
        mVectors[i][2] = zero[2];
    }
}

int32_t __cdecl cLightingManager::LargestOfSix(const float *vectors)
{
    const float n0 = SizeOf(vectors);
    const float n1 = SizeOf(vectors + 3);
    float best = n0;
    if (n1 > n0) best = n1;
    int32_t index = n1 > n0;
    for (int32_t i = 2; i < 6; ++i) {
        const float n = SizeOf(vectors + 3 * i);
        if (n > best) {
            index = i;
            best = n;
        }
    }
    return index;
}

int32_t __cdecl cLightingManager::LargestOfFour(const float *vectors)
{
    const float n0 = SizeOf(vectors);
    const float n1 = SizeOf(vectors + 3);
    float best = n0;
    if (n1 > n0) best = n1;
    int32_t index = n1 > n0;
    for (int32_t i = 2; i < 4; ++i) {
        const float n = SizeOf(vectors + 3 * i);
        if (n > best) {
            index = i;
            best = n;
        }
    }
    return index;
}

int32_t __cdecl cLightingManager::SmallestOfSix(const float *vectors)
{
    const float n0 = SizeOf(vectors);
    const float n1 = SizeOf(vectors + 3);
    float best = n0;
    if (best > n1) best = n1;
    int32_t index = n0 > n1;
    for (int32_t i = 2; i < 6; ++i) {
        const float n = SizeOf(vectors + 3 * i);
        if (best > n) {
            index = i;
            best = n;
        }
    }
    return index;
}

void cLightingManager::RemoveBuiltLights()
{
    for (uint32_t i = 0; i < mBuiltLights.size(); ++i) RemoveFromScene(mSceneManager, mBuiltLights[i].mObject);
    mBuiltLights.clear();
    if (!Switch(kRefreshSwitch) || !Switch(kFadeSwitch)) mLerp228->Clear();
}

void cLightingManager::RemoveSampleLights()
{
    for (uint32_t s = 0; s < SkySampleCount(); ++s) {
        cSkySample &sample = SkySample(s);
        for (uint32_t i = 0; i < sample.mLights.size(); ++i) RemoveFromScene(mSceneManager, sample.mLights[i].mObject);
        if (!Switch(kFadeSwitch)) {
            if (sample.mLerp78) {
                sample.mLerp78->ClearActive();
                sample.mLerp78->FadeOutUnused();
            }
            if (sample.mLerp88) {
                sample.mLerp88->ClearActive();
                sample.mLerp88->FadeOutUnused();
            }
        } else if (!Switch(kRefreshSwitch)) {
            if (sample.mLerp78) sample.mLerp78->Clear();
            if (sample.mLerp88) sample.mLerp88->Clear();
        }
        for (uint32_t i = 0; i < sample.mPortals.size(); ++i) {
            void *const other = sample.mPortals[i].mOther.mObject;
            if (other) {
                void *const owner = RawSlot<34, void *>(other); // receiver: the portal record's object (class not identified)
                RawSlot<46, void>(owner, sample.mPortals[i].mOther.mObject); // receiver: its slot 34 result (class not identified)
            }
        }
        sample.mLights.clear();
        sample.mPortals.clear();
    }
}

void cLightingManager::EvaluateLerpLights(float t)
{
    mLerp228->Evaluate(t);
    mLerp22C->Evaluate(t);
    mLerp230->Evaluate(t);
    for (uint32_t s = 0; s < SkySampleCount(); ++s) {
        cSkySample &sample = SkySample(s);
        if (sample.mLerp78) sample.mLerp78->Evaluate(t);
        if (sample.mLerp88) sample.mLerp88->Evaluate(t);
    }
    RawSlot<11, void>(mRef24, t); // receiver: nTSSG::cLightMapManager (the lightmaps)
}

void cLightingManager::RefreshPortalObjects()
{
    if (Switch(kRefreshSwitch)) return;
    void *held = nullptr;
    auto refresh = [&held](void *object) {
        if (held) {
            void *const old = held;
            held = nullptr;
            ReleaseThroughBase(old);
        }
        // receiver: the portal record's object (class not identified): slot 24 finds an interface
        if (RawSlot<24, bool>(object, kIidPortalObject, &held, 0))
            RawSlot<3, void>(held, 0); // receiver: the interface found (class not identified)
    };
    for (uint32_t s = 0; s < SkySampleCount(); ++s) {
        cSkySample &sample = SkySample(s);
        for (uint32_t i = 0; i < sample.mPortals.size(); ++i) {
            void *const first = sample.mPortals[i].mObject.mObject;
            void *const second = sample.mPortals[i].mOther.mObject;
            if (first) refresh(first);
            if (second) refresh(second);
        }
    }
    if (held) ReleaseThroughBase(held);
}

void cLightingManager::RelightSceneObjects()
{
    struct Filter {
        const void *mVTable;
    } filter = {GameAddress<const void>(kObjectsFilterVTable)};
    struct Iterator {
        void *mIterator;
        void *mField4;
        void *mField8;
    } it;
    nGZSceneGraph::cICompositionTreeNode *const scene = SceneOf(mSceneManager);
    scene->MakeIterator44(&it, 0, reinterpret_cast<uint32_t>(&filter)); // receiver: the scene (a cICompositionTreeNode face)
    while (!RawSlot<4, bool>(it.mIterator, it.mField8)) {     // receiver: the scene's iterator (class not identified)
        void *const object = RawSlot<5, void *>(it.mIterator);
        if (RawSlot<25, bool>(object, kSceneObjectFlag)) // receiver: the scene object (class not identified)
            RawSlot<13, void>(this, RawSlot<5, void *>(it.mIterator), 0); // receiver: nTSSG::cLightingManager
        RawSlot<6, void>(it.mIterator);
    }
    if (it.mField8) RawSlot<2, void>(it.mField8);
    if (it.mField4) RawSlot<2, void>(it.mField4);
    if (it.mIterator) RawSlot<2, void>(it.mIterator);
}

void cLightingManager::RebuildSunHeights()
{
    const cLightVector sun = CurrentSet().mLights[0].mDirection;
    if (mShadeTiles || mDrawSunHeights) {
        LightMapStopwatch watch;
        if (mSunMapDirty || mFlag82) {
            Restart(&watch.value);
            auto *const db = static_cast<nTSWorld::cIWorldDB *>(mTerrain);
            mSunHeights.mBlendScale = mBlendScale240;
            cSunHeightMap::BuildGround(&mSunHeights, db, 8);
            if (mShadeWalls) {
                cSunHeightMap::AddWalls(&mSunHeights, db, 8, &sun.x);
                cSunHeightMap::AddRoofs(&mSunHeights, db, 8);
                const float away[3] = {Negated(sun.x), Negated(sun.y), Negated(sun.z)};
                cSunHeightMap::AddAwnings(&mSunHeights, db, 8, away);
            }
            mSunMapDirty = 0;
            stopwatch_more::Stop(&watch.value, nullptr);
        }
        Restart(&watch.value);
        mSunHeights.BuildShadow(&sun.x);
        stopwatch_more::Stop(&watch.value, nullptr);
    }
    mSunShadows = mShadeTiles;
}

void cSkySample::Clear()
{
    const float *const zero = GameAddress<const float>(kZeroVector);
    mTileCount = 0;
    mWallCount = 0;
    mPortalCount = 0;
    for (int32_t i = 0; i < 6; ++i) {
        mVectors[i][0] = zero[0];
        mVectors[i][1] = zero[1];
        mVectors[i][2] = zero[2];
        mWeights[i] = 0.0f;
    }
}

void cLightingManager::SamplePortals()
{
    for (uint32_t s = 0; s < SkySampleCount(); ++s) SkySample(s).Clear();
    LightMapStopwatch watch;
    Restart(&watch.value);
    SampleRoomTiles();
    Restart(&watch.value);
    SampleWallPortals();
    Restart(&watch.value);
    AddPortalRecords();
    if (mSkySamples.empty()) return;
    for (uint32_t s = 0; s < SkySampleCount(); ++s) {
        cSkySample &sample = SkySample(s);
        for (int32_t i = 0; i < 6; ++i) {
            const float w = sample.mWeights[i];
            if (w != 0.0f) {
                sample.mVectors[i][0] = DivS(sample.mVectors[i][0], w);
                sample.mVectors[i][1] = DivS(sample.mVectors[i][1], w);
                sample.mVectors[i][2] = DivS(sample.mVectors[i][2], w);
            }
        }
    }
}

void cLightingManager::AddPortalRecords()
{
    for (uint32_t s = 0; s < SkySampleCount(); ++s) {
        for (uint32_t i = 0; i < SkySample(s).mPortals.size(); ++i) {
            const cPortalRecord &record = SkySample(s).mPortals[i];
            const int32_t index = record.mSample;
            if (index == -1 || index == 0 || static_cast<uint32_t>(index) >= SkySampleCount()) continue;
            cSkySample &target = SkySample(static_cast<uint32_t>(index));
            const float scale = mPortalScale;
            const float c0 = MulS(record.mColor[0], scale);
            const float c1 = MulS(record.mColor[1], scale);
            const float c2 = MulS(record.mColor[2], scale);
            const float front = record.mNormal[0]; // the normal's x weighs the front side by its sign
            const float back = record.mNormal[1]; // and its y the back
            const float f0 = MulS(front, c0), f1 = MulS(front, c1), f2 = MulS(front, c2);
            if (front > 0.0f) {
                target.mVectors[0][0] = AddS(f0, target.mVectors[0][0]);
                target.mVectors[0][1] = AddS(f1, target.mVectors[0][1]);
                target.mVectors[0][2] = AddS(f2, target.mVectors[0][2]);
                target.mWeights[0] = AddS(front, target.mWeights[0]);
            } else {
                target.mVectors[1][0] = SubS(target.mVectors[1][0], f0);
                target.mVectors[1][1] = SubS(target.mVectors[1][1], f1);
                target.mVectors[1][2] = SubS(target.mVectors[1][2], f2);
                target.mWeights[1] = SubS(target.mWeights[1], front);
            }
            const float b0 = MulS(back, c0), b1 = MulS(c1, back), b2 = MulS(c2, back);
            if (back > 0.0f) {
                target.mVectors[2][0] = AddS(b0, target.mVectors[2][0]);
                target.mVectors[2][1] = AddS(b1, target.mVectors[2][1]);
                target.mVectors[2][2] = AddS(b2, target.mVectors[2][2]);
                target.mWeights[2] = AddS(back, target.mWeights[2]);
            } else {
                target.mVectors[3][0] = SubS(target.mVectors[3][0], b0);
                target.mVectors[3][1] = SubS(target.mVectors[3][1], b1);
                target.mVectors[3][2] = SubS(target.mVectors[3][2], b2);
                target.mWeights[3] = SubS(target.mWeights[3], back);
            }
            ++target.mPortalCount;
        }
    }
}

} // namespace nTSSG
