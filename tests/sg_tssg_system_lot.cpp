// Differential tests for nTSSG::cTSSGSystem's lot clearing (src/sg/cTSSGSystemLot.cpp).
// Hook shape: the original runs on the same object image as ours.
//
// The world (tests/ts_services_fixture.h): the object starts as random bytes; its manager references hold recording
// fakes (the twelve lot managers ClearLotForLoad calls are a second array of fakes, ids 300 + i, whose reference counts
// are compared too), or null where the code tests for null. The cached cells the getters read (resource manager
// 0x10cc2d8, scene cache 0x10cc2cc, car manager 0x10faf84, the app 0x10cc288) hold fakes; GetCachedSlot23's cell
// 0x10cc2a8 holds one or is empty (then filled from the app's slot 23). 0xae3c00 and
// 0xb3c2b0 (not replaced) are stood in over the image, so both sides reach the same logging stand-ins.
//
// Compared after each run: the event log, the object's 0x3b8 bytes (normalised), every fake's reference count and the
// cells.
#include "code_patch.h"
#include "difftest.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "sg/SGUtilities.h"
#include "sg/cTSSGSystem.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

namespace {

using namespace svcfx;
using nTSSG::cTSSGSystem;

constexpr uint32_t kObjectSize = 0x3b8;
constexpr uint32_t kObjectTag = 0x5e000000;
constexpr uint32_t kMoreTag = 0x6c000000;
enum : uint32_t { evStand = 0x81 };

enum : unsigned {
    fFramework = 0,
    fRig,
    fRigObject,
    fLighting,
    fScene,
    fSceneObject,
    fItems,
    fRef110,
    fRef114,
    fRef118,
    fRef84,
    fNHood,
    fRef94,
    fRef90,
    fCars,
    fCarsObject,
    fResources,
    fSceneCache,
    fSlot23,
    fLightMap,
    fApp,
};

constexpr uint32_t kResourceCell = 0x10cc2d8;
constexpr uint32_t kSceneCacheCell = 0x10cc2cc;
constexpr uint32_t kCarCell = 0x10faf84;
constexpr uint32_t kSlot23Cell = 0x10cc2a8;
constexpr uint32_t kCells[] = {kResourceCell, kSceneCacheCell, kCarCell, kSlot23Cell, kAppCell};
constexpr char kCellKinds[] = "ppppp"; // the cells' words as the outcome logs them (svcfx::DumpCells)


// The lot managers ClearLotForLoad calls, with the slot each is called through.
struct LotManager {
    uint32_t offset;
    unsigned slot;
};
constexpr LotManager kLotManagers[] = {{0x78, 6},  {0x70, 5},  {0x7c, 12}, {0x6c, 11}, {0x80, 6}, {0xb8, 5},
                                       {0x74, 6},  {0xe8, 5},  {0xec, 5},  {0xfc, 7},  {0xf8, 5}, {0xf0, 5}};
constexpr unsigned kLotCount = sizeof(kLotManagers) / sizeof(kLotManagers[0]);
Obj gMore[kLotCount];

void __cdecl StandSceneObject(void *object) { Ev(evStand), Ev(0xae3c00), Ev(Norm(P(object))); }
void __cdecl StandCars(void *object) { Ev(evStand), Ev(0xb3c2b0), Ev(Norm(P(object))); }

uint32_t gItemCount;
uint32_t ItemAt(Obj *, uint32_t *args) { return 0x500 + args[0]; }

struct Fixture {
    AllocatorScope allocator;
    Globals globals;
    shape::StandIn scene{0xae3c00, &nGZSceneGraph::DetachChoreoQueue, &StandSceneObject};
    shape::CodePatch cars{GameAddress<void>(0xb3c2b0), shape::CodeOf(&StandCars)};
    Fixture()
    {
        for (uint32_t cell : kCells) globals.SetWord(cell, 0);
        gFrameworkIndex = fFramework;
    }
};

alignas(16) uint8_t gObject[kObjectSize];
// gObject's words as the outcome logs them (svcfx::DumpWords): 'p' pointer, 'd' data, 'x' not compared.
constexpr char kObjectKinds[] =
    "dddddddd dddddddd dddddddd ddpppppp pp-d-pdp ppdpddpd dddddddd ddpppdpp "
    "ddddp--d dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddd";

struct Draw {
    bool rigGives, sceneGives;
    uint32_t items;
    bool present[8]; // fItems, fRef110, fRef114, fRef118, fRef84, fNHood, fRef94, fRef90
    bool slot23Cached; // else GetCachedSlot23 fills its cell from the app object's slot 23
};

void Wire(const Draw &d)
{
    F(fRig).Set(12, 0, d.rigGives ? P(&F(fRigObject)) : 0u);
    F(fRig).Set(11, "pp");
    F(fLighting).Set(7, "p");
    F(fLighting).Set(9, "p");
    F(fScene).Set(18, 0, d.sceneGives ? P(&F(fSceneObject)) : 0u);
    F(fItems).Set(12, 0);
    F(fItems).Set(16, 0, d.items);
    F(fItems).Set(5, "d", 0u, &ItemAt);
    F(fItems).Set(7, "d");
    for (unsigned f : {fRef110, fRef114, fRef118}) F(f).Set(4, "d");
    for (unsigned f : {fRef84, fNHood, fRef94, fRef90}) F(f).Set(4, 0);
    F(fCars).Set(43, 0, &F(fCarsObject));
    F(fCars).Set(39, 1);
    F(fCars).Set(40, 1);
    F(fResources).Set(45, 0);
    F(fSceneCache).Set(25, "-d");
    F(fLightMap).Set(6, 0);
    F(fApp).Set(23, 0, &F(fSlot23));
    for (unsigned i = 0; i < kLotCount; ++i) {
        gMore[i].Reset(300 + i);
        gMore[i].Set(kLotManagers[i].slot, 0);
    }
}

void Start(const Draw &d, std::mt19937 &gen, uint8_t *start)
{
    for (uint32_t i = 0; i < kObjectSize; ++i) start[i] = uint8_t(gen());
    auto put = [&](uint32_t offset, const void *p) { std::memcpy(start + offset, &p, 4); };
    put(0x9c, &F(fRig));
    put(0xa4, &F(fLighting));
    put(0x68, &F(fScene));
    put(0xa0, &F(fLightMap));
    const uint32_t offsets[] = {0xac, 0x110, 0x114, 0x118, 0x84, 0x88, 0x94, 0x90};
    const unsigned fakes[] = {fItems, fRef110, fRef114, fRef118, fRef84, fNHood, fRef94, fRef90};
    for (unsigned i = 0; i < 8; ++i) put(offsets[i], d.present[i] ? &F(fakes[i]) : nullptr);
    for (unsigned i = 0; i < kLotCount; ++i) put(kLotManagers[i].offset, &gMore[i]);
}

Blob Outcome()
{
    Blob b = gEv;
    b.push_back(0xeeee);
    svcfx::DumpWords(b, gObject, sizeof(gObject), kObjectKinds);
    CaptureFakes(b);
    for (const Obj &o : gMore) b.push_back(uint32_t(o.refs));
    svcfx::DumpCells(b, kCells, kCellKinds);
    return b;
}

template <class Run> void Compare(difftest::Context &ctx, const char *what, uint32_t seed, const Draw &d, const uint8_t *start, Run run)
{
    Blob outcome[2];
    for (int side = 0; side < 2; ++side) {
        ResetAll();
        Wire(d);
        *GameAddress<void *>(kResourceCell) = &F(fResources);
        *GameAddress<void *>(kSceneCacheCell) = &F(fSceneCache);
        *GameAddress<void *>(kCarCell) = &F(fCars);
        *GameAddress<void *>(kSlot23Cell) = d.slot23Cached ? &F(fSlot23) : nullptr;
        *GameAddress<void *>(kAppCell) = &F(fApp);
        std::memcpy(gObject, start, kObjectSize);
        AddRange(gObject, kObjectSize, kObjectTag);
        AddRange(gMore, sizeof(gMore), kMoreTag);
        ctx.Mark(side ? "ours" : "original");
        run(side == 0);
        outcome[side] = Outcome();
    }
    Same(ctx, outcome[0], outcome[1], what, seed);
}

Draw DrawCase(std::mt19937 &gen)
{
    Draw d{};
    d.rigGives = gen() % 2, d.sceneGives = gen() % 2;
    d.items = gen() % 4;
    d.slot23Cached = gen() % 2;
    for (bool &p : d.present) p = gen() % 4 != 0;
    return d;
}

cTSSGSystem *Ours() { return reinterpret_cast<cTSSGSystem *>(gObject); }

} // namespace

// 0x24c610 ClearLot: the light rig's object and the scene manager's object there or not, 0..3 items, each dropped
// reference present or not.
DIFF_TEST(sg_tssg_system_clear_lot, 0x24c610)
{
    Fixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x24c610);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x24c610u);
        const Draw d = DrawCase(gen);
        uint8_t start[kObjectSize];
        Start(d, gen, start);
        Compare(ctx, "clear_lot", seed, d, start, [&](bool useOriginal) {
            if (useOriginal) original(gObject);
            else Ours()->ClearLot();
        });
    }
}

// 0x2521c0 ClearLotForLoad: as ClearLot, with mRef110 and the lot managers.
DIFF_TEST(sg_tssg_system_clear_lot_for_load, 0x2521c0)
{
    Fixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x2521c0);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 gen(seed * 104729u + 0x2521c0u);
        const Draw d = DrawCase(gen);
        uint8_t start[kObjectSize];
        Start(d, gen, start);
        Compare(ctx, "clear_lot_for_load", seed, d, start, [&](bool useOriginal) {
            if (useOriginal) original(gObject);
            else Ours()->ClearLotForLoad();
        });
    }
}
