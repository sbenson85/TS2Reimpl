// Differential test for nTSSG::cTSSGSystem's per-frame update (src/sg/cTSSGSystemFrame.cpp): Update (slot 17). Hook
// shape: the original runs on the same object image as ours.
//
// The world: the object starts as random bytes; its manager references hold recording fakes (svcfx::Obj, or
// sgwide::Wide where a slot returns a float or the slot is 80 or more), or null where the code tests for null. Its own
// slots 29, 51, 52 and 99 go to a table of logging stand-ins (slot 52's clock reaches its timer through its virtual base).
// Cached cells: the live household 0x10cc2ac, the detail object 0x10f9028, the material object 0x10f8fcc,
// GetCachedSlot23's 0x10cc2a8 and the app 0x10cc288, so the original getters return the fakes on both sides. The
// stopwatch's Elapsed (ours and 0x1c9256) and ReloadMaterials (ours and 0x2556a0) are stood in on both sides; the restart
// 0x1c95ec, 0x24ab50, 0x24b870, the day/night test 0x7e2180 and the car manager getter 0xb39000 over the image.
//
// Compared after each run: the event log, the object's 0x3b8 bytes (normalised), every fake's reference count and
// value words, the cells and the detail-level global 0xff81d8.
#include "code_patch.h"
#include "difftest.h"
#include "sg_tssg_wide.h"
#include "stack_out.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/cIGZPropertySet.h"
#include "rz/cRZStopwatch.h"
#include "sg/cTSSGSystem.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>

namespace {

using namespace svcfx;
using sgwide::W;
using nTSSG::cTSSGSystem;

constexpr uint32_t kObjectSize = 0x3b8;
constexpr uint32_t kObjectTag = 0x5e000000;
enum : uint32_t { evStand = 0x81, evSelf = 0x82 };

// svcfx fakes.
enum : unsigned {
    fService = 0,
    fSettings,
    fScene,       // mScene (+0x58)
    fSceneLoader, // its slot 12 object
    fDetail,
    fWorld,       // GetCachedSlot23
    fHousehold,   // the live household (0x20df9a)
    fLightRig,
    fOccupants,
    fRef84,
    fRefC0,
    fSceneManager,
    fWall,
    fFloor,
    fTerrain,
    fRug,
    fCurved,
    fCutout,
    fSnow,
    fAwning,
    fRef90,
    fLightMap,
    fFence,
    fPool,
};
// Wide fakes.
enum : unsigned {
    wClock = 0,  // the object's slot 52
    wTimer,      // the clock's virtual base (slots 11 / 12: the tick, the simulation tick)
    wSelection,  // the object's slot 51
    wMaterials,  // GetCachedQueriedObject
    wRefB4,
    wProps,      // its slot 37
    wTime,       // the properties' slot 14
    wCars,
    wApp,        // GetCachedSingleton
    wAppObject,  // its slot 33
    wHolder,     // its slot 22
    wView,       // the holder's slot 20
    wRef358,
    wLighting,
    wAsset,
    wShadow,
    wRef94,
    wSceneObject, // the scene manager's slot 18
};

constexpr uint32_t kHouseholdCell = 0x10cc2ac;
constexpr uint32_t kDetailCell = 0x10f9028;
constexpr uint32_t kMaterialCell = 0x10f8fcc;
constexpr uint32_t kSlot23Cell = 0x10cc2a8;
constexpr uint32_t kDetailLevel = 0xff81d8;
constexpr uint32_t kCells[] = {kHouseholdCell, kDetailCell, kMaterialCell, kSlot23Cell, kAppCell, kDetailLevel};
constexpr char kCellKinds[] = "pppppd"; // the cells' words as the outcome logs them (svcfx::DumpCells)

constexpr int32_t kNoTick = int32_t(0x80000100);

// ---- the seed ----
struct Draw {
    uint32_t frame, startup;   // the two stopwatches' Elapsed
    uint32_t paused;           // the household's slot 26 (al)
    bool settings;             // the settings service there
    uint32_t reparse;          // its byte
    uint32_t lot;              // GetCachedSlot23's slot 5
    int32_t tick, simTick;
    bool props;
    uint32_t has[4], writes[4]; // the four fade properties: slot 17's answer, whether it writes
    float values[4];
    uint32_t night;
    bool appObject, view, cars;
    bool present[14];          // occupants, mRef84, terrain, rug, curved, cutout, snow, awning, mRef90, mRef358, pool,
                               // mRef94, asset collector, (unused)
    float current;             // mRef358's slot 52
};
Draw gD;

uint32_t SettingsGet(Obj *, uint32_t *args) // slot 9 (key, any)
{
    const auto *key = reinterpret_cast<const GZServiceString *>(uintptr_t(args[0]));
    for (char c : *key) Ev(uint8_t(c));
    auto *any = reinterpret_cast<uint8_t *>(uintptr_t(args[1]));
    Ev(Norm(*reinterpret_cast<const uint32_t *>(any)));
    Ev(any[4]);
    any[4] = uint8_t(gD.reparse);
    return 1;
}
uint32_t SettingsSet(Obj *, uint32_t *args) // slot 3 (key, any)
{
    const auto *key = reinterpret_cast<const GZServiceString *>(uintptr_t(args[0]));
    for (char c : *key) Ev(uint8_t(c));
    const auto *any = reinterpret_cast<const uint8_t *>(uintptr_t(args[1]));
    Ev(Norm(*reinterpret_cast<const uint32_t *>(any)));
    Ev(any[4]);
    return 1;
}
uint32_t ServiceQuery(Obj *, uint32_t *args) // GetServiceObject slot 5 (class, iid, &out)
{
    const uint32_t object = args[0] == 0x0ac1887e && gD.settings ? P(&F(fSettings)) : 0u;
    sgwide::Out(args, 2, object);
    return object != 0;
}
// The properties' slot 17 (name, float *out): which of the four fade properties (by name), its answer, maybe written.
constexpr uint32_t kFadeNames[] = {0xdae830, 0xdae844, 0xdae858, 0xdae870};
uint32_t PropertyGet(sgwide::Wide *, uint32_t *args)
{
    const char *name = reinterpret_cast<const char *>(uintptr_t(args[0]));
    for (unsigned i = 0; i < 4; ++i) {
        if (std::strcmp(name, GameAddress<const char>(kFadeNames[i])) != 0) continue;
        if (gD.writes[i]) {
            std::memcpy(reinterpret_cast<void *>(uintptr_t(args[1])), &gD.values[i], 4);
            difftest::FillOut(reinterpret_cast<void *>(uintptr_t(args[1])), 4);
        }
        return gD.has[i];
    }
    return 0;
}
// The properties' slot 14 (out): fills the caller's time word and returns it.
uint32_t PropertyTime(sgwide::Wide *, uint32_t *args)
{
    auto *out = reinterpret_cast<uint32_t *>(uintptr_t(args[0]));
    *out = 0x71e0u;
    difftest::FillOut(out, 4);
    return args[0];
}

// ---- the object's own slots ----
void *gSelfVTable[100];
uint32_t __fastcall Self52(void *self, void *) { return Ev(evSelf), Ev(52), Ev(Norm(P(self))), P(&W(wClock)); }
uint32_t __fastcall Self51(void *self, void *) { return Ev(evSelf), Ev(51), Ev(Norm(P(self))), P(&W(wSelection)); }
void __fastcall Self29(void *self, void *, uint32_t a) { Ev(evSelf), Ev(29), Ev(Norm(P(self))), Ev(a); }
void __fastcall Self99(void *self, void *) { Ev(evSelf), Ev(99), Ev(Norm(P(self))); }

// ---- stand-ins ----
alignas(16) uint8_t gObject[kObjectSize];
// gObject's words as the outcome logs them (svcfx::DumpWords): 'p' pointer, 'd' data, 'x' not compared.
constexpr char kObjectKinds[] =
    "pddddddd dddddddd ddddddpd ddppppdp pppdppdp pppddpdd pddddddd ddpppdpp "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd ddddddpd dddddddd dddddddd dddddp";
uint32_t __fastcall StandElapsed(const cRZStopwatch *watch, void *)
{
    Ev(evStand), Ev(0x1c9256), Ev(Norm(P(watch)));
    return P(watch) == P(gObject + 0x20) ? gD.frame : gD.startup;
}
void __fastcall StandRestart(cRZStopwatch *watch, void *) { Ev(evStand), Ev(0x1c95ec), Ev(Norm(P(watch))); }
void __fastcall StandPrepare(void *self, void *) { Ev(evStand), Ev(0x24ab50), Ev(Norm(P(self))); }
void __cdecl StandNhoodShaders() { Ev(evStand), Ev(0x24b870); }
uint32_t __cdecl StandIsNight(void *time)
{
    Ev(evStand), Ev(0x7e2180), Ev(Norm(P(time)));
    return 0xabcd0000u | gD.night; // only al is the answer
}
void *__cdecl StandCars()
{
    Ev(evStand), Ev(0xb39000);
    return gD.cars ? &W(wCars) : nullptr;
}
void __fastcall StandReload(cTSSGSystem *self, void *) { Ev(evStand), Ev(0x2556a0), Ev(Norm(P(self))); }

struct Fixture {
    AllocatorScope allocator;
    Globals globals;
    shape::StandIn elapsed{0x1c9256, &cRZStopwatch::Elapsed, &StandElapsed};
    shape::StandIn reload{0x2556a0, &cTSSGSystem::ReloadMaterials, &StandReload};
    shape::CodePatch restart{GameAddress<void>(0x1c95ec), shape::CodeOf(&StandRestart)};
    shape::CodePatch prepare{GameAddress<void>(0x24ab50), shape::CodeOf(&StandPrepare)};
    shape::CodePatch shaders{GameAddress<void>(0x24b870), shape::CodeOf(&StandNhoodShaders)};
    shape::CodePatch night{GameAddress<void>(0x7e2180), shape::CodeOf(&StandIsNight)};
    shape::CodePatch cars{GameAddress<void>(0xb39000), shape::CodeOf(&StandCars)};
    Fixture()
    {
        globals.Set(0x10c9378, &F(fService));
        for (uint32_t cell : kCells) globals.SetWord(cell, 0);
        gSelfVTable[29] = reinterpret_cast<void *>(&Self29);
        gSelfVTable[51] = reinterpret_cast<void *>(&Self51);
        gSelfVTable[52] = reinterpret_cast<void *>(&Self52);
        gSelfVTable[99] = reinterpret_cast<void *>(&Self99);
    }
};

void Wire()
{
    F(fService).Set(5, "ddp", 0u, &ServiceQuery);
    F(fSettings).Set(9, "pp", 0u, &SettingsGet);
    F(fSettings).Set(3, "pp", 0u, &SettingsSet);
    F(fScene).Set(12, 0, &F(fSceneLoader));
    F(fSceneLoader).Set(8, 0);
    F(fDetail).Set(43, 0, 0x4343u);
    F(fWorld).Set(5, 0, gD.lot);
    F(fHousehold).Set(26, 0, gD.paused);
    F(fLightRig).Set(7, 0);
    F(fOccupants).Set(10, 0);
    F(fRef84).Set(17, 0);
    F(fRefC0).Set(5, 0);
    F(fSceneManager).Set(15, 0);
    F(fSceneManager).Set(18, 0, &W(wSceneObject));
    F(fWall).Set(23, 0);
    F(fFloor).Set(15, 0);
    F(fTerrain).Set(7, 0);
    for (unsigned f : {fRug, fCurved, fCutout, fAwning}) F(f).Set(6, 0);
    F(fSnow).Set(8, 0);
    F(fRef90).Set(8, 0);
    F(fLightMap).Set(7, 0);
    F(fFence).Set(17, 0);
    F(fPool).Set(5, "d");

    W(wClock).Base(2, &W(wTimer));
    W(wTimer).Set(11, 0, uint32_t(gD.tick));
    W(wTimer).Set(12, 0, uint32_t(gD.simTick));
    W(wSelection).Set(2, "dd");
    W(wMaterials).Set(24, "-d");
    W(wRefB4).Set(25, "d");
    W(wRefB4).Set(37, 0, gD.props ? P(&W(wProps)) : 0u);
    W(wProps).Set(17, "-p", 0u, &PropertyGet);
    W(wProps).text[17] = 1;
    W(wProps).Set(14, "p", 0u, &PropertyTime);
    W(wCars).Set(31, "dd");
    W(wCars).Set(40, "p");
    W(wCars).Set(20, "dd");
    W(wApp).Set(33, 0, gD.appObject ? P(&W(wAppObject)) : 0u);
    W(wApp).Set(22, 0, &W(wHolder));
    W(wHolder).Set(20, 0, gD.view ? P(&W(wView)) : 0u);
    W(wView).Set(5, 0);
    W(wAppObject).Set(40, "d");
    W(wRef358).SetFloat(52, 0, gD.current);
    W(wRef358).Set(42, "d");
    W(wLighting).Set(10, 0);
    W(wAsset).Set(8, 0);
    W(wShadow).Set(5, 0);
    W(wRef94).Set(5, 0);
}

void Start(std::mt19937 &gen, uint8_t *start)
{
    for (uint32_t i = 0; i < kObjectSize; ++i) start[i] = uint8_t(gen());
    auto put = [&](uint32_t offset, const void *p) { std::memcpy(start + offset, &p, 4); };
    auto word = [&](uint32_t offset, uint32_t v) { std::memcpy(start + offset, &v, 4); };
    auto real = [&](uint32_t offset, float v) { std::memcpy(start + offset, &v, 4); };
    put(0, gSelfVTable);
    put(0x58, &F(fScene));
    put(0x9c, &F(fLightRig));
    put(0xb4, &W(wRefB4));
    put(0xc0, &F(fRefC0));
    put(0x68, &F(fSceneManager));
    put(0x6c, &F(fWall));
    put(0x7c, &F(fFloor));
    put(0xa0, &F(fLightMap));
    put(0x70, &F(fFence));
    put(0xa4, &W(wLighting));
    put(0xa8, &W(wShadow));
    const uint32_t offsets[] = {0x88, 0x84, 0x80, 0xe8, 0xec, 0xf8, 0xfc, 0xf0, 0x90, 0x358, 0x74, 0x94, 0x3b4};
    const void *fakes[] = {&F(fOccupants), &F(fRef84), &F(fTerrain), &F(fRug), &F(fCurved), &F(fCutout), &F(fSnow),
                           &F(fAwning), &F(fRef90), &W(wRef358), &F(fPool), &W(wRef94), &W(wAsset)};
    for (unsigned i = 0; i < 13; ++i) put(offsets[i], gD.present[i] ? fakes[i] : nullptr);

    // The gate.
    start[0x16a] = uint8_t(gen() % 8 ? 1 + gen() % 255 : 0);
    start[0x16b] = uint8_t(gen() % 8 ? 1 + gen() % 255 : 0);
    // The least frame time: none, below, at or above the frame's.
    const uint32_t least[] = {0, gD.frame - 1, gD.frame, gD.frame + 1, gen()};
    word(0x38, least[gen() % 5]);
    // The animation clock, near its wrap or not.
    const float clocks[] = {0.0f, 3599.9f, 3600.0f, 3599.0f, 100.25f};
    real(0x3c, clocks[gen() % 5]);
    // The tick: unchanged, never set, or another.
    const int32_t ticks[] = {gD.tick, kNoTick, gD.tick - 33, int32_t(gen())};
    word(0x194, uint32_t(ticks[gen() % 4]));
    start[0x18d] = uint8_t(gen() % 2 ? 1 + gen() % 255 : 0);
    // The simulation reference: behind, equal, ahead by up to and past the rewind limit.
    const int32_t sims[] = {gD.simTick - 5, gD.simTick, gD.simTick + 10000, gD.simTick + 10001, gD.simTick + 3,
                            int32_t(gen())};
    word(0x198, uint32_t(sims[gen() % 6]));
    // The statistics: the bucket 0..10 (10 is never stored, but read), the next one never started or not, the current
    // one started 999 / 1000 / 1001 ticks ago or at random.
    const uint32_t bucket = gen() % 11;
    word(0x2b4, bucket);
    const uint32_t next = bucket < 9 ? bucket + 1 : 0;
    for (uint32_t i = 0; i < 10; ++i) word(0x19c + 4 * i, gen() % 4 ? gen() : uint32_t(kNoTick));
    if (gen() % 3 == 0) word(0x19c + 4 * next, uint32_t(kNoTick));
    else if (gen() % 2) word(0x19c + 4 * next, uint32_t(gD.tick));
    const int32_t ages[] = {999, 1000, 1001, 0, -5, int32_t(gen())};
    word(0x19c + 4 * bucket, uint32_t(gD.tick - ages[gen() % 6]));
    start[0x161] = uint8_t(gen() % 3 ? 0 : 1 + gen() % 255);
    start[0x369] = uint8_t(gen() % 2 ? 0 : 1 + gen() % 255);
    start[0x169] = uint8_t(gen() % 2 ? 0 : 1 + gen() % 255);
    start[0x3a8] = uint8_t(gen() % 2 ? 0 : 1 + gen() % 255);
    const int32_t delays[] = {0, 1, 2, -1, int32_t(0x80000000), int32_t(gen() % 100)};
    word(0x3ac, uint32_t(delays[gen() % 6]));
    // The pulse: depth and rate.
    const float depths[] = {0.0f, 0.5f, 1.0f, 0.25f};
    const float rates[] = {0.0f, 1.0f, 2.5f, 0.125f};
    real(0x350, depths[gen() % 4]);
    real(0x354, rates[gen() % 4]);
}

Blob Outcome()
{
    Blob b = gEv;
    b.push_back(0xeeee);
    svcfx::DumpWords(b, gObject, sizeof(gObject), kObjectKinds);
    CaptureFakes(b);
    sgwide::CaptureWide(b);
    svcfx::DumpCells(b, kCells, kCellKinds);
    return b;
}

void DrawCase(std::mt19937 &gen)
{
    gD = Draw{};
    const uint32_t spans[] = {0, 1, 16, 33, 1000, 0x7fffffff, 0x80000000, 0xffffffff};
    gD.frame = gen() % 3 ? spans[gen() % 8] : gen() % 100;
    gD.startup = gen() % 4 ? gen() % 200000 : 0;
    gD.paused = gen() % 2 ? 0 : gen() % 2 ? 1 : 0x100;
    gD.settings = gen() % 4 != 0;
    gD.reparse = gen() % 2 ? 0 : 1;
    gD.lot = gen() % 2 ? 0x0bea28c2 : gen() % 2 ? 0x0bea28c3 : 0;
    gD.tick = int32_t(gen() % 4 ? gen() % 1000000 : gen());
    gD.simTick = int32_t(gen() % 4 ? gen() % 1000000 : gen());
    gD.props = gen() % 4 != 0;
    const float values[] = {0.0f, 1.0f, -1.0f, 25.5f, std::nanf("")};
    for (unsigned i = 0; i < 4; ++i) {
        gD.has[i] = gen() % 3 ? (gen() % 2 ? 1 : 0x100) : 0;
        gD.writes[i] = gen() % 3 != 0;
        gD.values[i] = values[gen() % 5];
    }
    gD.night = gen() % 2;
    gD.appObject = gen() % 2;
    gD.view = gen() % 2;
    gD.cars = gen() % 4 != 0;
    if (gD.props && gD.lot == 0x0bea28c2) gD.cars = true; // the day/night goes to it without a null test
    for (bool &p : gD.present) p = gen() % 4 != 0;
    gD.current = 0.0f;
}

// mRef358's current value: the pulse the frame computes (so slot 42 is skipped), or another.
float Pulse(const uint8_t *start)
{
    float depth, rate;
    std::memcpy(&depth, start + 0x350, 4);
    std::memcpy(&rate, start + 0x354, 4);
    float phase = static_cast<float>(static_cast<double>(gD.startup)) * *GameAddress<const float>(0xda20dc);
    phase = phase + phase;
    phase = phase * *GameAddress<const float>(0xda77d8);
    phase = phase * rate;
    const float one = *GameAddress<const float>(0xd9c698);
    const float wave = static_cast<float>(std::sin(static_cast<double>(phase))) + one;
    return static_cast<float>(static_cast<double>(wave) * (static_cast<double>(depth) * *GameAddress<const double>(0xda1870)) +
                              static_cast<double>(one - depth));
}

cTSSGSystem *Ours() { return reinterpret_cast<cTSSGSystem *>(gObject); }

} // namespace

// 0x24fb30 Update: the gate (both flags); the frame time against the least time (none, below, at, above); the household
// paused (al only); the settings service there, the reparse switch; a lot world or not; the animation clock near its wrap;
// the tick unchanged, never set or new; render option 5 and the simulation reference behind, equal or ahead up to and
// past 10000; every statistics bucket 0..10 with the next one never started and the current one 999 / 1000 / 1001 ticks
// old; the fade properties (each answered, written or not, NaN included); the cinematic camera byte and +0x161; day or
// night, the app object; the view; every optional manager; the pulse equal or not; the car manager; the neighbourhood
// refresh; the material countdown at 0, 1, 2, -1, INT_MIN or more.
DIFF_TEST(sg_tssg_system_update, 0x24fb30)
{
    Fixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x24fb30);
    for (uint32_t seed = 0; seed < 2000; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x24fb30u);
        DrawCase(gen);
        uint8_t start[kObjectSize];
        Start(gen, start);
        const uint32_t pick = gen() % 3;
        gD.current = pick == 0 ? Pulse(start) : pick == 1 ? 1.0f : std::nanf("");
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            ResetAll();
            sgwide::ResetWide();
            Wire();
            *GameAddress<void *>(kHouseholdCell) = &F(fHousehold);
            *GameAddress<void *>(kDetailCell) = &F(fDetail);
            *GameAddress<void *>(kMaterialCell) = &W(wMaterials);
            *GameAddress<void *>(kSlot23Cell) = &F(fWorld);
            *GameAddress<void *>(kAppCell) = &W(wApp);
            *GameAddress<uint32_t>(kDetailLevel) = 0x5555;
            std::memcpy(gObject, start, kObjectSize);
            AddRange(gObject, kObjectSize, kObjectTag);
            ctx.Mark(side ? "ours" : "original");
            if (side == 0) original(gObject);
            else Ours()->Update();
            outcome[side] = Outcome();
        }
        Same(ctx, outcome[0], outcome[1], "update", seed);
    }
}
