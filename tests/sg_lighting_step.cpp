// Differential tests for nTSSG::cLightingManager's rebuild drivers (lane 261110a, scenegraph LG4h;
// src/sg/cLightingManagerRebuild.cpp): RebuildStep 0x2d3a50 and RebuildAll 0x2d4010. Hook shape: the original runs on
// the same manager as ours.
//
// The world: the manager is 0x548 bytes of junk with the lit object (+0x18), the lightmaps (+0x24) and the scene manager
// (+0x28; null now and then for RebuildAll) as logging fakes, the sun mode (+0x40: 2, near it, junk above the low byte),
// the flag bytes +0x80..+0x87 (0, 1, 2, 0x80, 0xff), the next step (+0x88: every phase 0..23, past it, junk), the
// portal lights (+0x8c: null, or a static array with an end anywhere around it: whole, partial, before the start), the
// light sets (+0x98, zero to two; the current index +0xd0 in and out of range; the default set +0x2d0) with drawn
// refresh delays (+0x200: zeros, NaN, infinities, denormals) and mField500 (zeros, NaN, values). The game's switch
// bytes (0xffde88, 0xffde34, 0xffde40, 0xffde94, 0xffde58, 0xffde70, 0xffde64) are drawn 0, 1 or another non-zero byte
// and restored. RebuildAll's argument is drawn with junk above the low byte. In half the draws every fake callee (the
// builders, the lit object's, the lightmaps' and the manager's slots) now and then toggles a switch byte or mFlag83
// after it runs, so each re-read of them must come after the same call as in the original.
//
// The stopwatches are fakes (tests/sg_lighting_fakes.h, Stopwatches): construction numbered, Elapsed answering scripted
// readings: under 10 (0, 1, 5, 9) until a drawn round, then 10 or more (10, 11, 1000, 2^31, 2^32 - 1) to end the
// budget. RebuildStep's first draws sweep every starting phase with every exit round from 0, so each phase runs and
// each phase's budget exit is reached; the rest draw the start and exit round, the hundred-round limit included.
//
// Fakes on both sides: the manager's builders (Builders), the stopwatch's Construct, Start and Elapsed, the scene manager
// slot 17, the lit object's slots 8, 9 (fills an empty vector: one to three portal lights from the heap, its end drawn),
// 13, 14, 15 and 21, the lightmaps' slots 8, 9 and 10, the manager's slots 39 to 45, the scene flag setter 0xaeeea0
// (ours: RebuildLightBounds); on the image's side the stopwatch's Restart 0x1c95ec (called by ours as the original). Compared: the log,
// the manager's bytes (the portal vector as ids and spans), the sets' bytes, the switch bytes, the readings taken and
// the heap blocks each side made or freed; at the x87's 53-bit and 24-bit precision.
#include "sg_lighting_fakes.h"

#include <memory>

namespace {

using namespace lightfx;
using nTSSG::cPortalLight;

constexpr int kSets = 2;
constexpr int kPortalArray = 4;
constexpr uint32_t kPortalSize = sizeof(cPortalLight);
static_assert(kPortalSize == 0x6c, "cPortalLight");

alignas(16) uint8_t gManager[0x548];
alignas(16) uint8_t gSets[kSets][0x214];
cPortalLight gPortalArray[kPortalArray];
Plain gLitObject, gLightMaps, gScene, gStepSceneManager;
void *gLitVT[24], *gLightMapsVT[12];

constexpr uint32_t kSwitches[] = {0xffde88, 0xffde34, 0xffde40, 0xffde94, 0xffde58, 0xffde70, 0xffde64};
constexpr uint32_t kFixedColour = 0xffd9a0;

uint32_t gReadingValues[128];
uint32_t gFillCount;  // the lit object's slot 9 fills an empty vector with this many portal lights (0: never)
int32_t gFillEnd;     // and puts its end this many bytes after its start
uint32_t gFilled;     // the block it made (this side's)
uint32_t gFilledBytes;

uint32_t Id(const void *p)
{
    if (!p) return 0;
    const uint32_t a = P(p);
    if (a >= P(gManager) && a < P(gManager) + sizeof gManager) return 0x10000 + (a - P(gManager));
    if (a >= P(gPortalArray) && a < P(gPortalArray) + sizeof gPortalArray) return 0x20000 + (a - P(gPortalArray));
    if (gFilled && a >= gFilled && a <= gFilled + gFilledBytes) return 0x30000 + (a - gFilled);
    if (a >= P(gSets) && a < P(gSets) + sizeof gSets) return 0x40000 + (a - P(gSets));
    if (p == &gLitObject) return 0x11;
    if (p == &gLightMaps) return 0x24;
    if (p == &gStepSceneManager) return 0x28;
    if (p == &gScene) return 0x5ce;
    if (p == GameAddress<const void>(kFixedColour)) return 0xd9a0;
    const uint32_t here = P(&a);
    if (a - here + 0x10000u < 0x20000u) return 0x57ac; // the caller's own vector on the stack
    return 0xbad00000u | (a & 0xfffff);
}

// After a builder now and then (by gFlipSalt; 0: never): a switch byte or mFlag83 toggled, so that each read of them is
// placed after the right call.
uint32_t gFlipSalt, gFlipCalls;
void AfterBuilder(uint32_t tag)
{
    if (!gFlipSalt) return;
    const uint32_t h = Mix2(gFlipSalt, gFlipCalls++);
    if (h % 3) return;
    const uint32_t pick = (h >> 4) % (std::size(kSwitches) + 1);
    uint8_t &b = pick < std::size(kSwitches) ? *GameAddress<uint8_t>(kSwitches[pick]) : gManager[0x83];
    b = b ? 0 : 1;
    L(0xf119), L(tag), L(pick);
}

// A vector's three words as the begin's id, the end's and the capacity's byte offsets from it.
void LogVector(const void *vector)
{
    const auto *const w = static_cast<const uint32_t *>(vector);
    L(Id(reinterpret_cast<const void *>(uintptr_t(w[0])))), L(w[1] - w[0]), L(w[2] - w[0]);
}

// ---- the lit object, the lightmaps, the manager's own slots ----

void LitVector(uint32_t slot, void *self, void *vector)
{
    L(0x1100 + slot), L(Id(self)), L(Id(vector)), LogVector(vector);
    AfterBuilder(0x1100 + slot);
}
void __fastcall Lit8(Plain *self, void *, void *vector) { LitVector(8, self, vector); }
void __fastcall Lit13(Plain *self, void *, void *vector) { LitVector(13, self, vector); }
void __fastcall Lit9(Plain *self, void *, void *vector)
{
    LitVector(9, self, vector);
    auto *const w = static_cast<uint32_t *>(vector);
    if (w[0] || !gFillCount || gFilled) return;
    cPortalLight *const block = std::allocator<cPortalLight>().allocate(gFillCount);
    std::memset(block, 0x6c, gFillCount * kPortalSize);
    gFilled = P(block), gFilledBytes = gFillCount * kPortalSize;
    w[0] = gFilled, w[1] = gFilled + uint32_t(gFillEnd), w[2] = gFilled + gFilledBytes;
}
void __fastcall Lit14(Plain *self, void *) { L(0x110e), L(Id(self)), AfterBuilder(0x110e); }
void __fastcall Lit15(Plain *self, void *) { L(0x110f), L(Id(self)), AfterBuilder(0x110f); }
void __fastcall Lit21(Plain *self, void *) { L(0x1115), L(Id(self)), AfterBuilder(0x1115); }

void __fastcall Maps8(Plain *self, void *, const float *colour)
{
    L(0x2408), L(Id(self)), L(Id(colour)), LF(colour, 3), AfterBuilder(0x2408);
}
void __fastcall Maps9(Plain *self, void *, uint32_t lerp) { L(0x2409), L(Id(self)), L(lerp), AfterBuilder(0x2409); }
void __fastcall Maps10(Plain *self, void *) { L(0x240a), L(Id(self)), AfterBuilder(0x240a); }

#define STEP_MANAGER_SLOT(n) \
    void __fastcall Manager##n(void *self, void *) { L(0x3300 + n), L(Id(self)), AfterBuilder(0x3300 + n); }
STEP_MANAGER_SLOT(39)
STEP_MANAGER_SLOT(40)
STEP_MANAGER_SLOT(41)
STEP_MANAGER_SLOT(42)
#undef STEP_MANAGER_SLOT
#define STEP_MANAGER_LIGHTS(n) \
    void __fastcall Manager##n(void *self, void *, const void *lights, int32_t count) \
    { \
        L(0x3300 + n), L(Id(self)), L(Id(lights)), L(uint32_t(count)), AfterBuilder(0x3300 + n); \
    }
STEP_MANAGER_LIGHTS(43)
STEP_MANAGER_LIGHTS(44)
STEP_MANAGER_LIGHTS(45)
#undef STEP_MANAGER_LIGHTS

void FillStepFakes()
{
    FillShared();
    gLitVT[8] = reinterpret_cast<void *>(&Lit8);
    gLitVT[9] = reinterpret_cast<void *>(&Lit9);
    gLitVT[13] = reinterpret_cast<void *>(&Lit13);
    gLitVT[14] = reinterpret_cast<void *>(&Lit14);
    gLitVT[15] = reinterpret_cast<void *>(&Lit15);
    gLitVT[21] = reinterpret_cast<void *>(&Lit21);
    gLightMapsVT[8] = reinterpret_cast<void *>(&Maps8);
    gLightMapsVT[9] = reinterpret_cast<void *>(&Maps9);
    gLightMapsVT[10] = reinterpret_cast<void *>(&Maps10);
    gManagerVT[39] = reinterpret_cast<void *>(&Manager39);
    gManagerVT[40] = reinterpret_cast<void *>(&Manager40);
    gManagerVT[41] = reinterpret_cast<void *>(&Manager41);
    gManagerVT[42] = reinterpret_cast<void *>(&Manager42);
    gManagerVT[43] = reinterpret_cast<void *>(&Manager43);
    gManagerVT[44] = reinterpret_cast<void *>(&Manager44);
    gManagerVT[45] = reinterpret_cast<void *>(&Manager45);
    gLitObject.vt = gLitVT, gLightMaps.vt = gLightMapsVT, gStepSceneManager.vt = gSceneManagerVT;
}

struct Globals : SavedGlobals {
    Globals()
    {
        for (uint32_t s : kSwitches) Save(s, 1);
        Save(kFixedColour, 12);
    }
};

// ---- the drawn world ----

uint8_t Flag(std::mt19937 &gen)
{
    static const uint8_t kFlags[] = {0, 1, 0, 1, 2, 0x80, 0xff};
    return kFlags[gen() % std::size(kFlags)];
}

float Delay(std::mt19937 &gen)
{
    static const float kDelays[] = {0.0f, -0.0f, 1.0f, 0.5f, 0.001f, -1.0f, 2.5f, 1e30f, 1e-40f};
    if (gen() % 2) return kDelays[gen() % std::size(kDelays)];
    return Wide(gen, 8.0f);
}

void DrawSet(std::mt19937 &gen, uint8_t *set)
{
    for (int i = 0; i < 0x214; ++i) set[i] = uint8_t(gen());
    for (uint32_t o = 0x19c; o < 0x1a8; o += 4) PutFloat(set, o, Nice(gen));
    PutFloat(set, 0x200, Delay(gen));
}

// The readings: under the budget until round `exitAt`, then 10 or more.
void DrawReadings(std::mt19937 &gen, uint32_t exitAt)
{
    static const uint32_t kUnder[] = {0, 1, 5, 9, 9};
    static const uint32_t kOver[] = {10, 10, 11, 1000, 0x80000000u, 0xffffffffu};
    for (uint32_t i = 0; i < std::size(gReadingValues); ++i)
        gReadingValues[i] = i < exitAt ? kUnder[gen() % std::size(kUnder)] : kOver[gen() % std::size(kOver)];
    Readings(gReadingValues, std::size(gReadingValues));
}

// Common to both tests: the manager, the sets, the switches, the scene, the fill.
void DrawCommon(std::mt19937 &gen)
{
    for (auto &b : gManager) b = uint8_t(gen());
    PutWord(gManager, 0, P(gManagerVT));
    PutWord(gManager, 0x18, P(&gLitObject)), PutWord(gManager, 0x24, P(&gLightMaps));
    PutWord(gManager, 0x28, P(&gStepSceneManager));
    static const uint32_t kModes[] = {0, 1, 2, 2, 2, 3, 0x102, 0x80000002u};
    PutWord(gManager, 0x40, gen() % 5 ? kModes[gen() % std::size(kModes)] : gen());
    for (uint32_t o = 0x80; o < 0x88; ++o) gManager[o] = Flag(gen);
    // the portal lights: null, or the static array with its end around it
    if (gen() % 3 == 0) {
        PutWord(gManager, 0x8c, 0), PutWord(gManager, 0x90, 0), PutWord(gManager, 0x94, 0);
    } else {
        static const int32_t kEnds[] = {0, 1, 2, 3, 4, -1, -2};
        int32_t end = int32_t(kPortalSize) * kEnds[gen() % std::size(kEnds)];
        if (gen() % 5 == 0) end += int32_t(gen() % 0x6c) - 0x36; // not a whole light
        PutWord(gManager, 0x8c, P(gPortalArray)), PutWord(gManager, 0x90, P(gPortalArray) + uint32_t(end));
        PutWord(gManager, 0x94, P(gPortalArray) + kPortalArray * kPortalSize);
    }
    // the light sets and the current one
    for (auto &set : gSets) DrawSet(gen, set);
    DrawSet(gen, gManager + 0x2d0);
    const uint32_t sets = gen() % (kSets + 1);
    PutWord(gManager, 0x98, P(gSets)), PutWord(gManager, 0x9c, P(gSets) + sets * 0x214);
    PutWord(gManager, 0xa0, P(gSets) + kSets * 0x214);
    static const uint32_t kCurrent[] = {0, 1, 2, 0xffffffffu, 0x80000000u};
    PutWord(gManager, 0xd0, gen() % 4 ? gen() % 3 : kCurrent[gen() % std::size(kCurrent)]);
    static const float kField500[] = {0.0f, 0.0f, -0.0f, 1.0f, 500.0f, -3.0f};
    const uint32_t f500 = gen() % 8;
    PutFloat(gManager, 0x500, f500 < 6 ? kField500[f500] : f500 == 6 ? NaN(gen) : Wide(gen, 1000.0f));
    // the switches and the fixed colour
    for (uint32_t s : kSwitches) *GameAddress<uint8_t>(s) = Flag(gen);
    for (int k = 0; k < 3; ++k) GameAddress<float>(kFixedColour)[k] = Nice(gen);
    // the scene and the fill
    static Plain *const kScenes[] = {&gScene, &gScene, &gScene, nullptr};
    gSceneAnswer = kScenes[gen() % std::size(kScenes)];
    gFillCount = gen() % 3 == 0 ? 0 : 1 + gen() % 3;
    static const int32_t kFillEnds[] = {0, 1, 2, 3, -1, -2};
    gFillEnd = int32_t(kPortalSize) * kFillEnds[gen() % std::size(kFillEnds)];
    if (gen() % 5 == 0) gFillEnd += int32_t(gen() % 0x6c) - 0x36;
    gFilled = 0, gFilledBytes = 0;
    gFlipSalt = gen() % 2 ? gen() | 1 : 0, gFlipCalls = 0;
    gWatchCount = 0, gWatchHome = gManager, gWatchHomeSize = sizeof gManager;
}

// RebuildStep's world: the sweep (seed below 25 * 30: every start 0..24 with every exit round 0..29), then drawn.
void DrawStep(uint32_t seed)
{
    std::mt19937 gen(seed * 2654435761u + 0x2d3a50u);
    DrawCommon(gen);
    const uint32_t plain = seed & 0xfffff;
    uint32_t start, exitAt;
    if (plain < 25 * 30) {
        start = plain % 25, exitAt = plain / 25;
    } else {
        static const uint32_t kStarts[] = {0, 1, 1, 1, 6, 11, 22, 23, 24, 25, 100, 0x80000000u, 0xffffffffu};
        start = gen() % 3 ? gen() % 24 : kStarts[gen() % std::size(kStarts)];
        static const uint32_t kExits[] = {0, 1, 2, 23, 24, 98, 99, 100, 101, 128};
        exitAt = gen() % 2 ? gen() % 40 : kExits[gen() % std::size(kExits)];
    }
    PutWord(gManager, 0x88, start);
    DrawReadings(gen, exitAt);
}

uint32_t gLerp;
void DrawAll(uint32_t seed)
{
    std::mt19937 gen(seed * 2654435761u + 0x2d4010u);
    DrawCommon(gen);
    if (gen() % 10 == 0) PutWord(gManager, 0x28, 0);
    static const uint32_t kLerps[] = {0, 1, 0x100, 0x101, 0xffffff00u, 0x80, 0xff};
    gLerp = gen() % 4 ? kLerps[gen() % std::size(kLerps)] : gen();
    DrawReadings(gen, gen() % 4);
}

Blob Snapshot(const Globals &globals, long long blocks, long long bytes)
{
    Blob o = gLog;
    o.push_back(0xeeee);
    for (uint32_t i = 0; i < sizeof gManager; i += 4)
        if (i < 0x8c || i >= 0x98) o.push_back(Word<uint32_t>(gManager, i));
    gLog.clear();
    LogVector(gManager + 0x8c);
    o.insert(o.end(), gLog.begin(), gLog.end());
    for (const auto &set : gSets)
        for (uint32_t i = 0; i < sizeof set; i += 4) o.push_back(Word<uint32_t>(set, i));
    globals.Append(o);
    o.push_back(uint32_t(gNextReading)), o.push_back(gWatchCount);
    o.push_back(uint32_t(blocks)), o.push_back(uint32_t(bytes));
    return o;
}

// The block the lit object's slot 9 put into the manager's portal vector, if it is still there.
void FreeAll()
{
    if (gFilled && Word<uint32_t>(gManager, 0x8c) == gFilled)
        std::allocator<cPortalLight>().deallocate(reinterpret_cast<cPortalLight *>(uintptr_t(gFilled)), gFillCount);
}

struct Scope {
    svcfx::AllocatorScope allocator;
    Globals globals;
    Builders builders;
    Stopwatches watches;
    shape::StandIn sceneFlags{0xaeeea0u, &nGZSceneGraph::RebuildLightBounds, &StandSceneFlags};
    Scope()
    {
        FillStepFakes();
        gIdOf = &Id, gAfterBuilder = &AfterBuilder;
        gLog.reserve(1 << 14);
    }
    ~Scope() { gAfterBuilder = nullptr; }
};

} // namespace

// 0x2d3a50: up to 100 phases of the rebuild from mRebuildStep, until the 10 ms budget runs out.
DIFF_TEST(sg_lighting_step, 0x2d3a50)
{
    Scope scope;
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x2d3a50);
    SidesAtBothPrecisions(ctx, "step", 1500, false, [&](uint32_t draw, int side) {
        DrawStep(draw);
        gLog.clear();
        ctx.Mark(side ? "ours" : "original");
        difftest::AllocationCounter heap;
        if (side == 0) original(gManager);
        else reinterpret_cast<nTSSG::cLightingManager *>(gManager)->RebuildStep();
        heap.Stop();
        Blob outcome = Snapshot(scope.globals, heap.Blocks(), heap.Bytes());
        FreeAll();
        return outcome;
    });
}

// 0x2d4010: every phase at once.
DIFF_TEST(sg_lighting_step_all, 0x2d4010)
{
    Scope scope;
    auto original = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x2d4010);
    SidesAtBothPrecisions(ctx, "all", 1500, false, [&](uint32_t draw, int side) {
        DrawAll(draw);
        gLog.clear();
        ctx.Mark(side ? "ours" : "original");
        difftest::AllocationCounter heap;
        if (side == 0) original(gManager, gLerp);
        else reinterpret_cast<nTSSG::cLightingManager *>(gManager)->RebuildAll(gLerp);
        heap.Stop();
        Blob outcome = Snapshot(scope.globals, heap.Blocks(), heap.Bytes());
        FreeAll();
        return outcome;
    });
}
