// nTSSG::cLightingManager, the two rebuild drivers (lane 261110a, scenegraph LG4h): RebuildStep 0x2d3a50 (one phase at a
// time under a 10 ms budget) and RebuildAll 0x2d4010 (every phase at once). See sg/cLightingManager.h.
//
// Still the game's, called through OriginalFunction: the stopwatch's Restart 0x1c95ec. The scene flag setter 0xaeeea0
// is ours (nGZSceneGraph::RebuildLightBounds: the light bound shapes rebuilt under the scene).
#include "sg/cLightingManager.h"
#include "sg/cLightingManagerDetail.h"

#include "compgeom/ConvexMath.h"
#include "game/original.h"
#include "sg/SGUtilities.h"

#include <vector>

namespace nTSSG {

namespace {

using nGZCompGeom::convex::MulS;

constexpr uint32_t kSwitch88 = 0xffde88;     // g_Switch88: the room lights go to the lerp sets (mFlag84)
constexpr uint32_t kFadeSwitch = 0xffde34;   // the byte that keeps the sun height-map and portal phases
constexpr uint32_t kPortalSwitch = 0xffde40; // the portal lights built / applied
constexpr uint32_t kFixedSwitch = 0xffde94;  // with both switches above set: mRef24 takes the fixed bounce colour
constexpr uint32_t kFixedColour = 0xffd9a0;  // g_FixedBounceColor0
constexpr uint32_t kRef24Switch = 0xffde58;  // mRef24's slot 10 runs
constexpr uint32_t kKeepDirty = 0xffde70;    // RebuildAll leaves mDirty set
constexpr uint32_t kThousand = 0xda4568;     // 1000.0f
constexpr uint32_t kSceneFlags = 0xc;        // what the scene gets with mSceneFlagWanted set

constexpr uint32_t kStepBudget = 10; // RebuildStep's milliseconds (mode 4 stopwatch units)
constexpr int32_t kStepRounds = 100; // RebuildStep's phases at most per call

// A stopwatch on the stack, built as the original builds it (0x1c9030; its destructor 0x140fe0 does nothing).
struct LocalStopwatch {
    union {
        cRZStopwatch value;
    };
    explicit LocalStopwatch(int32_t mode) { value.Construct(mode); }
    LocalStopwatch(const LocalStopwatch &) = delete;
    LocalStopwatch &operator=(const LocalStopwatch &) = delete;
    ~LocalStopwatch() {}
};

// The cRZStopwatch restart (0x1c95ec, still the game's).
void Restart(cRZStopwatch *watch) { OriginalFunction<void(__thiscall *)(cRZStopwatch *), 0x1c95ec>()(watch); }

// 0xaeeea0 (ours): the scene's light bound shapes rebuilt for the flags.
void SetSceneFlags(void *scene, uint32_t flags)
{
    nGZSceneGraph::RebuildLightBounds(static_cast<nGZSceneGraph::cICompositionTreeNode *>(scene), flags);
}

// The manager's slots 43, 44 and 45 take portal lights as (begin, count: a signed division of the byte span), or
// (0, 0) when the vector is empty.
template <int kSlot> void PassPortalLights(cLightingManager *manager, const std::vector<cPortalLight> &lights)
{
    if (lights.empty()) {
        Call<kSlot, void>(manager, static_cast<const cPortalLight *>(nullptr), 0);
        return;
    }
    const auto *const begin = reinterpret_cast<const uint8_t *>(lights.data());
    const auto *const end = reinterpret_cast<const uint8_t *>(lights.data() + lights.size());
    Call<kSlot, void>(manager, lights.data(), static_cast<int32_t>(end - begin) / static_cast<int32_t>(sizeof(cPortalLight)));
}

} // namespace

// The current set's bounce colour (+0x19c) to mRef24's slot 8, unless both g_Switch88 and the byte 0xffde34 are set:
// then the fixed colour, and only when 0xffde94 is set.
void cLightingManager::PassBounceColour()
{
    if (!Switch(kSwitch88) || !Switch(kFadeSwitch))
        Call<8, void>(mRef24, &CurrentSet().mVector19C); // receiver: the lightmaps (mRef24), slot 8 (colour)
    else if (Switch(kFixedSwitch))
        Call<8, void>(mRef24, GameAddress<const float>(kFixedColour)); // receiver: the lightmaps (mRef24), slot 8
}

// mFlag84 from g_Switch88 and mFlag83 (then cleared); the scene's flags given or taken back; under g_RefreshSwitch
// (`refresh`) mRefreshWatch restarted, mField504 cleared and, when mField500 is 0, mField500 raised to the current
// set's mRefreshDelay * 1000.
void cLightingManager::SettleScene()
{
    mFlag84 = Switch(kSwitch88) && !mFlag83;
    mFlag83 = 0;
    if (mSceneFlagWanted) {
        // receiver: nTSSG::cSceneManager (mSceneManager), slot 17 (the scene)
        SetSceneFlags(SceneOf(mSceneManager), kSceneFlags);
        mSceneFlagSet = 1;
    } else if (mSceneFlagSet) {
        SetSceneFlags(SceneOf(mSceneManager), 0); // receiver: nTSSG::cSceneManager, slot 17 (the scene)
        mSceneFlagSet = 0;
    }
}

void cLightingManager::RestartRefresh()
{
    Restart(&mRefreshWatch);
    const float last = mField500;
    mField504 = 0;
    if (last == 0.0f) {
        const float wanted = MulS(CurrentSet().mRefreshDelay, *GameAddress<const float>(kThousand));
        mField500 = wanted > last ? wanted : last; // maxss: a NaN keeps the old value
    }
}

void cLightingManager::RebuildStep()
{
    LocalStopwatch phase(4);
    LocalStopwatch budget(4);
    budget.value.Start();
    int32_t rounds = kStepRounds;
    // the rooms' lights go to the lerp sets: read where used (after the phase's builder, in phase 20)
    const auto LerpRooms = [this] { return Switch(kSwitch88) && !mFlag83; };
    for (;;) {
        --rounds;
        if (budget.value.Elapsed() >= kStepBudget) break;
        Restart(&phase.value);
        switch (mRebuildStep) {
        case 0:
            return;
        case 1:
            mRebuildStep = (mSunMode != 2) + 2;
            break;
        case 2: // the sun mode 2 rebuild
            RemoveBuiltLights();
            BuildSunHeightMapLights();
            BuildSunShadowTexture();
            EvaluateLerpLights(1.0f);
            mRebuildStep = 0;
            break;
        case 3:
            RelightSceneObjects();
            ++mRebuildStep;
            break;
        case 4:
            PassBounceColour();
            mPortalLights.clear();
            ++mRebuildStep;
            break;
        case 5:
            RemoveBuiltLights();
            RemoveSampleLights();
            ++mRebuildStep;
            break;
        case 6:
            BuildFlatOutdoorLights(1);
            if (Switch(kFadeSwitch)) {
                ++mRebuildStep;
            } else {
                mFlag83 = 1;
                mRebuildStep = 22;
            }
            break;
        case 7:
            BuildSunHeightMapLights();
            ++mRebuildStep;
            break;
        case 8:
            BuildRoomPortals();
            ++mRebuildStep;
            break;
        case 9:
            RefreshPortalObjects();
            ++mRebuildStep;
            break;
        case 10:
            Call<9, void>(mLitObject, &mPortalLights); // receiver: the lit object (mLitObject), slot 9 (the portals)
            ++mRebuildStep;
            break;
        case 11:
            Call<13, void>(mLitObject, &mPortalLights); // receiver: the lit object (mLitObject), slot 13
            if (Switch(kPortalSwitch)) ++mRebuildStep;
            else mRebuildStep = 15;
            break;
        case 12:
            if (Switch(kRefreshSwitch)) BuildPortalLights(&mPortalLights);
            else ApplyPortalLights();
            ++mRebuildStep;
            break;
        case 13:
            if (!Switch(kRefreshSwitch))
                Call<8, void>(mLitObject, &mPortalLights); // receiver: the lit object (mLitObject), slot 8
            ++mRebuildStep;
            break;
        case 14:
            Call<13, void>(mLitObject, &mPortalLights); // receiver: the lit object (mLitObject), slot 13
            ++mRebuildStep;
            break;
        case 15:
            SamplePortals();
            ++mRebuildStep;
            break;
        case 16:
            mRebuildStep = 17;
            PassPortalLights<45>(this, mPortalLights); // receiver: this manager, slot 45
            break;
        case 17:
            BuildRoomLights();
            ++mRebuildStep;
            break;
        case 18:
            if (LerpRooms()) Call<15, void>(mLitObject); // receiver: the lit object (mLitObject), slot 15
            else Call<14, void>(mLitObject);           // receiver: the lit object (mLitObject), slot 14
            ++mRebuildStep;
            break;
        case 19:
            mRebuildStep = 20;
            if (Switch(kRef24Switch)) Call<10, void>(mRef24); // receiver: the lightmaps (mRef24), slot 10
            break;
        case 20:
            RebuildSunHeights();
            if (LerpRooms()) { // read after RebuildSunHeights
                Call<41, void>(this); // receiver: this manager, slot 41
                Call<42, void>(this); // receiver: this manager, slot 42
            } else {
                Call<39, void>(this); // receiver: this manager, slot 39
                Call<40, void>(this); // receiver: this manager, slot 40
            }
            ++mRebuildStep;
            break;
        case 21:
            BlendLotEdges();
            ++mRebuildStep;
            break;
        case 22:
            if (!Switch(kRefreshSwitch)) Call<9, void>(mRef24, 1); // receiver: the lightmaps (mRef24), slot 9
            ++mRebuildStep;
            break;
        case 23:
            SettleScene();
            mRebuildStep = 0;
            mFlag81 = 1;
            mFlag82 = 0;
            if (Switch(kRefreshSwitch)) RestartRefresh();
            break;
        default:
            break;
        }
        if (rounds <= 0) {
            --rounds;
            break;
        }
    }
    if (rounds > 0) budget.value.Elapsed();
    else mRebuildStep = 0;
}

void cLightingManager::RebuildAll(uint32_t lerp)
{
    if (!mSceneManager) return;
    const bool refresh = Switch(kRefreshSwitch) && static_cast<uint8_t>(lerp);
    LocalStopwatch total(5);
    total.value.Start();
    LocalStopwatch phase(5);
    Restart(&phase.value);
    RelightSceneObjects();
    Restart(&phase.value);
    std::vector<cPortalLight> portals;
    PassBounceColour();
    RemoveSampleLights();
    RemoveBuiltLights();
    if (mSunMode == 2) {
        BuildSunHeightMapLights();
        BuildSunShadowTexture();
        mDirty = 0;
        return;
    }
    if (Switch(kFadeSwitch)) {
        BuildSunHeightMapLights();
        if (Switch(kFadeSwitch)) {
            Restart(&phase.value);
            BuildRoomPortals();
            RefreshPortalObjects();
            Restart(&phase.value);
            Call<9, void>(mLitObject, &portals);  // receiver: the lit object (mLitObject), slot 9 (the portals)
            Call<13, void>(mLitObject, &portals); // receiver: the lit object (mLitObject), slot 13
            if (Switch(kPortalSwitch)) {
                Restart(&phase.value);
                if (Switch(kRefreshSwitch)) BuildPortalLights(&portals);
                else ApplyPortalLights();
                Restart(&phase.value);
                if (!Switch(kRefreshSwitch))
                    Call<8, void>(mLitObject, &portals); // receiver: the lit object (mLitObject), slot 8
                Call<13, void>(mLitObject, &portals);    // receiver: the lit object (mLitObject), slot 13
            }
            Restart(&phase.value);
            if (Switch(kSwitch88) && !mFlag83) Call<15, void>(mLitObject); // receiver: the lit object, slot 15
            else Call<14, void>(mLitObject);                                // receiver: the lit object, slot 14
            if (!refresh && Switch(kRef24Switch)) Call<10, void>(mRef24);    // receiver: the lightmaps (mRef24), slot 10
            Restart(&phase.value);
            RebuildSunHeights();
            if (Switch(kSwitch88) && !mFlag83) {
                Call<41, void>(this); // receiver: this manager, slot 41
                Call<42, void>(this); // receiver: this manager, slot 42
            } else {
                Call<39, void>(this); // receiver: this manager, slot 39
                Call<40, void>(this); // receiver: this manager, slot 40
            }
            Restart(&phase.value);
            SamplePortals();
            if (Switch(kSwitch88) && !mFlag83) {
                Restart(&phase.value);
                PassPortalLights<44>(this, portals); // receiver: this manager, slot 44
            } else {
                Restart(&phase.value);
                PassPortalLights<43>(this, portals); // receiver: this manager, slot 43
            }
            Restart(&phase.value);
            BuildRoomLights();
        }
    }
    BuildFlatOutdoorLights(static_cast<uint8_t>(lerp));
    BlendLotEdges();
    Restart(&phase.value);
    if (!refresh) Call<9, void>(mRef24, lerp); // receiver: the lightmaps (mRef24), slot 9 (the whole argument word)
    if (!Switch(kKeepDirty)) mDirty = 0;
    mFlag82 = 0;
    if (refresh) RestartRefresh();
    else if (Switch(kSwitch88) && Switch(kFadeSwitch))
        Call<21, void>(mLitObject); // receiver: the lit object (mLitObject), slot 21
    SettleScene();
}

} // namespace nTSSG
