// Differential tests for nTSSG::cTSSGSystem's start and stop (src/sg/cTSSGSystemLifecycle.cpp): ReleaseManagers 0x24f4e0
// and Init 0x24afe0 (on the cIGZSystemService face at +0xc). Hook shape: the original runs on the same object image as ours.
//
// ReleaseManagers' world: every reference field holds its own recording fake (48 of them past the fixture's 24, ids 300+),
// or null where the original tests for null; the virtual-base references (+0xac, +0xb0, +0xc0 through the table at +4,
// the parser target through the table at +8) are small blocks whose table reaches a fake; the two vectors point into
// static arrays (registered ranges, so their addresses log as offsets). The notifier comes from the service object (slot
// 5), the app's slot 22 object from GetCachedSingleton, the animation system's slot 31 gives the index the seed draws. The
// originals it calls (0xb3a740 on the +0x3b0 object, 0x298070) are stood in over the image on both sides (ours calls the
// image's). The entry handles' destructor 0xae82b0 runs for real on both (the image's and ours): the texture manager is
// a fake in 0xae8750's cache 0x10f8f94 whose slot 19 (ReleaseId) logs; a handle is an id or -1.
//
// Init's world: the message ids are a real std::set (its head and size copied into the object), the walker's message
// server, the holder (service 0x85522803) and the three cached getters' cells; the three originals it calls (0x3a5de0,
// 0x253f80, 0x253ec0) are stood in over the image.
//
// Compared after each run: the result, the event log, the object's 0x3b8 bytes (normalised), the reference counts of every
// fake and the cells.
#include "code_patch.h"
#include "difftest.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "sg/cTSSGSystem.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <random>
#include <set>
#include <vector>

namespace {

using namespace svcfx;
using nTSSG::cTSSGSystem;
using nTSSG::SGRef;

constexpr uint32_t kObjectSize = 0x3b8;
constexpr uint32_t kObjectTag = 0x5e000000;
constexpr uint32_t kHeldTag = 0x6b000000;
constexpr uint32_t kHandleTag = 0x6c000000;
constexpr uint32_t kBlockTag = 0x6d000000;
enum : uint32_t { evStand = 0x81 };

enum : unsigned {
    fFramework = 0,
    fService,
    fNotifier,
    fApp,
    fAppObject,
    fAnim,
    fHolder,
    fServer,
    fScene,
    fPersist,
    fSceneOld,
    fAnimOld,
    fPersistOld,
    fRegistryBase,
    fRef2C8,
    fRef2CC,
    fTexMgr, // the texture manager, in 0xae8750's cache 0x10f8f94
};

constexpr uint32_t kSceneCell = 0x10cc2c8; // 0x20df26
constexpr uint32_t kAnimCell = 0x10cc2c4;  // 0x20dd94
constexpr uint32_t kCells[] = {kAppCell, kPersistCell, kSceneCell, kAnimCell, 0x10f8fd0};
constexpr char kCellKinds[] = "ppppp"; // the cells' words as the outcome logs them (svcfx::DumpCells)


// The extra fakes the reference fields hold.
constexpr unsigned kExtras = 52;
Obj gExtras[kExtras];

// A block with virtual-base tables at +4 and +8 whose entry 1 reaches a fake.
struct VBlock {
    uint32_t words[3];
    int32_t table4[2];
    int32_t table8[2];
    // Each table reaches its own fake, so a release through the wrong table shows.
    void Point(Obj *target4, Obj *target8)
    {
        words[0] = 0x0b1ec700;
        words[1] = P(table4);
        words[2] = P(table8);
        table4[0] = table8[0] = 0;
        table4[1] = int32_t(P(target4) - P(&words[1]));
        table8[1] = int32_t(P(target8) - P(&words[2]));
    }
};
VBlock gBlocks[4];

void *gHeld[6];
uint32_t gHandles[6];

uint32_t ServiceQuery(Obj *, uint32_t *args)
{
    uint32_t object = P(&F(fServer));
    if (args[0] == 0x0ac1887e) object = P(&F(fNotifier));
    if (args[0] == 0x85522803) object = P(&F(fHolder));
    *reinterpret_cast<uint32_t *>(uintptr_t(args[2])) = object;
    return 1;
}

// ---- stand-ins over the image ----
void __fastcall StandUnregister(void *self, void *) { Ev(evStand), Ev(0xb3a740), Ev(Norm(P(self))); }
uint8_t __cdecl StandRegistrations() { return Ev(evStand), Ev(0x298070), uint8_t(1); }
void __cdecl Stand3a5de0() { Ev(evStand), Ev(0x3a5de0); }
void __cdecl Stand253f80() { Ev(evStand), Ev(0x253f80); }
void __cdecl Stand253ec0() { Ev(evStand), Ev(0x253ec0); }
void __cdecl Stand3a65a0() { Ev(evStand), Ev(0x3a65a0); }
uint32_t gUnregisterTag = 0x255150, gCommandsTag = 0x255170;
void __cdecl StandUnregisterFunction() { Ev(evStand), Ev(gUnregisterTag); }
void __cdecl StandUnregisterCommands() { Ev(evStand), Ev(gCommandsTag); }

// The registry 0xae8980 returns (cell 0x10f8fd0): its table at +4, entry 2, reaches fake fRegistryBase.
constexpr uint32_t kRegistryCell = 0x10f8fd0;
struct Registry {
    uint32_t words[2];
    int32_t table[3];
};
Registry gRegistry;

struct Fixture {
    AllocatorScope allocator;
    Globals globals;
    std::vector<std::unique_ptr<shape::CodePatch>> image;
    template <class Stand> void Image(uint32_t rva, Stand stand)
    {
        image.emplace_back(new shape::CodePatch(GameAddress<void>(rva), shape::CodeOf(stand)));
    }
    Fixture()
    {
        globals.Set(0x10c9378, &F(fService));
        globals.Set(0x10f8f94, &F(fTexMgr));
        for (uint32_t cell : kCells) globals.SetWord(cell, 0);
        gFrameworkIndex = fFramework;
    }
};

alignas(16) uint8_t gObject[kObjectSize];
// gObject's words as the outcome logs them (svcfx::DumpWords): 'p' pointer, 'd' data, 'x' not compared.
constexpr char kObjectKinds[] =
    "dddddddd dddddddd dddddddp dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddd";

Blob Outcome(uint32_t result)
{
    Blob b = gEv;
    b.push_back(0xeeee);
    b.push_back(result);
    svcfx::DumpWords(b, gObject, sizeof(gObject), kObjectKinds);
    CaptureFakes(b);
    for (const Obj &o : gExtras) b.push_back(uint32_t(o.refs));
    svcfx::DumpCells(b, kCells, kCellKinds);
    return b;
}

template <class Prepare, class Run>
void Compare(difftest::Context &ctx, const char *what, uint32_t seed, const uint8_t *start, Prepare prepare, Run run)
{
    Blob outcome[2];
    for (int side = 0; side < 2; ++side) {
        ResetAll();
        for (unsigned i = 0; i < kExtras; ++i) gExtras[i].Reset(300 + i);
        F(fService).Set(5, "ddp", 0u, &ServiceQuery);
        prepare();
        std::memcpy(gObject, start, kObjectSize);
        AddRange(gObject, kObjectSize, kObjectTag);
        AddRange(gHeld, sizeof(gHeld), kHeldTag);
        AddRange(gHandles, sizeof(gHandles), kHandleTag);
        AddRange(gBlocks, sizeof(gBlocks), kBlockTag);
        ctx.Mark(side ? "ours" : "original");
        const uint32_t result = run(side == 0);
        outcome[side] = Outcome(result);
    }
    Same(ctx, outcome[0], outcome[1], what, seed);
}

// The reference fields ReleaseManagers drops, in field order, and whether the original tests each for null (an untested
// one always holds a fake). Each gets an extra fake whose slots 4 and 5 log.
struct Field {
    SGRef cTSSGSystem::*ref;
    bool tested;
};
const Field kFields[] = {
    {&cTSSGSystem::mRef64, true},           {&cTSSGSystem::mSceneManager, false},       {&cTSSGSystem::mWallManager, false},
    {&cTSSGSystem::mFenceManager, false},   {&cTSSGSystem::mPoolManager, true},         {&cTSSGSystem::mLotSkirt, true},
    {&cTSSGSystem::mFloorManager, false},   {&cTSSGSystem::mTerrain, true},             {&cTSSGSystem::mRef84, true},
    {&cTSSGSystem::mNHoodOccupants, true},  {&cTSSGSystem::mVideoBroadcastManager, true}, {&cTSSGSystem::mRef90, true},
    {&cTSSGSystem::mRef94, true},           {&cTSSGSystem::mQueryStrategyManager, true}, {&cTSSGSystem::mLightRigManager, false},
    {&cTSSGSystem::mLightMapManager, false}, {&cTSSGSystem::mLightingManager, false},   {&cTSSGSystem::mShadowManager, false},
    {&cTSSGSystem::mRefB4, true},           {&cTSSGSystem::mRoofManager, false},        {&cTSSGSystem::mLotImposterManager, false},
    {&cTSSGSystem::mRugManager, false},     {&cTSSGSystem::mCurvedPoolManager, false},  {&cTSSGSystem::mAwningManager, false},
    {&cTSSGSystem::mSeasonManager, false},  {&cTSSGSystem::mTerrainCutoutManager, false}, {&cTSSGSystem::mTerrainSnowManager, false},
    {&cTSSGSystem::m3DUIManager, false},    {&cTSSGSystem::mRef104, true},              {&cTSSGSystem::mCommandParser, false},
    {&cTSSGSystem::mRef358, true},          {&cTSSGSystem::mRef3B0, true},              {&cTSSGSystem::mAssetCollector, true},
};
constexpr unsigned kOverlayExtra = 33; // overlays use extras 33..41
static_assert(std::size(kFields) == kOverlayExtra);
// The four virtual-base blocks reach extras 42..45 through their table at +4 and 48..51 through the one at +8; the held
// objects are extras 46 and 47.
constexpr unsigned kVirtualExtra = 42;

} // namespace

// 0x24f4e0 ReleaseManagers: managers created or not, each tested reference present or not, the vectors empty or not, the
// animation index negative or not.
DIFF_TEST(sg_tssg_system_release_managers, 0x24f4e0)
{
    Fixture fixture;
    fixture.Image(0xb3a740, &StandUnregister);
    fixture.Image(0x298070, &StandRegistrations);
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x24f4e0);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 gen(seed * 2246822519u + 0x24f4e0u);
        uint8_t start[kObjectSize];
        for (uint8_t &b : start) b = uint8_t(gen());
        auto *sys = reinterpret_cast<cTSSGSystem *>(start);
        sys->mManagersCreated = gen() % 8 ? 1 : 0;
        sys->mAnimSystem.mObject = &F(fAnim);
        for (unsigned i = 0; i < std::size(kFields); ++i)
            (sys->*kFields[i].ref).mObject = !kFields[i].tested || gen() % 4 ? &gExtras[i] : nullptr;
        for (unsigned i = 0; i < 9; ++i) sys->mOverlays[i].mObject = gen() % 4 ? &gExtras[kOverlayExtra + i] : nullptr;
        void **virtuals[] = {&sys->mRefAC.mObject, &sys->mRefB0.mObject, &sys->mRefC0.mObject, &sys->mParserTarget};
        for (unsigned i = 0; i < 4; ++i) *virtuals[i] = gen() % 4 ? &gBlocks[i] : nullptr;
        // The vectors: begin, end, capacity words into the static arrays.
        const uint32_t held = gen() % 4 ? gen() % 7 : 0;
        const uint32_t handles = gen() % 4 ? gen() % 7 : 0;
        uint32_t *vectors = reinterpret_cast<uint32_t *>(start + 0x170);
        vectors[0] = P(gHeld), vectors[1] = P(gHeld + held), vectors[2] = P(gHeld + 6);
        vectors[3] = P(gHandles), vectors[4] = P(gHandles + handles), vectors[5] = P(gHandles + 6);
        void *heldValues[6];
        for (unsigned i = 0; i < 6; ++i) heldValues[i] = gen() % 4 ? &gExtras[kVirtualExtra + 4 + (i % 2)] : nullptr;
        const int32_t index = gen() % 3 ? int32_t(gen() % 40) : -int32_t(1 + gen() % 3);
        auto prepare = [&] {
            *GameAddress<void *>(kAppCell) = &F(fApp);
            F(fApp).Set(22, 0, &F(fAppObject));
            F(fAppObject).Set(19, 1, 0u);
            F(fNotifier).Set(6, "p", 0u);
            F(fAnim).Set(31, "p", uint32_t(index));
            F(fAnim).Set(11, "dd", 0u);
            for (Obj &o : gExtras) o.Set(4, 0, 0u), o.Set(5, 0, 0u);
            for (unsigned i = 0; i < 4; ++i) gBlocks[i].Point(&gExtras[kVirtualExtra + i], &gExtras[kVirtualExtra + 6 + i]);
            std::memcpy(gHeld, heldValues, sizeof(gHeld));
            F(fTexMgr).Set(19, "d"); // ReleaseId
            for (unsigned i = 0; i < 6; ++i) gHandles[i] = (seed + i) % 5 == 0 ? 0xffffffffu : 0x4a000000u + i;
        };
        Compare(ctx, "release_managers", seed, start, prepare, [&](bool useOriginal) -> uint32_t {
            if (useOriginal) original(gObject);
            else reinterpret_cast<cTSSGSystem *>(gObject)->ReleaseManagers();
            return 0;
        });
    }
}

// 0x24afe0 Init (the face at +0xc): message ids none or several, the getters' cells holding the fields' objects or new
// ones (the old ones present or not).
DIFF_TEST(sg_tssg_system_init, 0x24afe0)
{
    Fixture fixture;
    fixture.Image(0x3a5de0, &Stand3a5de0);
    fixture.Image(0x253f80, &Stand253f80);
    fixture.Image(0x253ec0, &Stand253ec0);
    auto original = ctx.Original<uint8_t(__thiscall *)(void *)>(0x24afe0);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 gen(seed * 40503u + 0x24afe0u);
        std::set<uint32_t> ids;
        const uint32_t count = gen() % 3 ? gen() % 30 : 0;
        while (ids.size() < count) ids.insert(gen() % 3 ? 0x80 + gen() % 0x80 : gen());
        uint8_t start[kObjectSize];
        for (uint8_t &b : start) b = uint8_t(gen());
        std::memcpy(start + 0x14, &ids, sizeof(ids));
        auto *sys = reinterpret_cast<cTSSGSystem *>(start);
        const unsigned news[] = {fScene, fAnim, fPersist}, olds[] = {fSceneOld, fAnimOld, fPersistOld};
        SGRef *fields[] = {&sys->mScene, &sys->mAnimSystem, &sys->mPersistService};
        bool same[3];
        for (unsigned i = 0; i < 3; ++i) {
            same[i] = gen() % 4 == 0;
            fields[i]->mObject = same[i] ? &F(news[i]) : gen() % 3 ? &F(olds[i]) : nullptr;
        }
        auto prepare = [&] {
            *GameAddress<void *>(kSceneCell) = &F(fScene);
            *GameAddress<void *>(kAnimCell) = &F(fAnim);
            *GameAddress<void *>(kPersistCell) = &F(fPersist);
            F(fServer).Set(5, "pd", 0u);
            F(fHolder).Set(8, "dpd", 0u);
        };
        Compare(ctx, "init", seed, start, prepare, [&](bool useOriginal) -> uint32_t {
            if (useOriginal) return original(gObject + 0xc);
            return reinterpret_cast<nTSSG::cTSSGSystemServiceSub *>(gObject + 0xc)->Init();
        });
    }
}

// 0x255010 Shutdown (the face at +0xc): the references present or not, message ids none or several; the two unregister
// helpers stood in on both sides (tested below), 0x3a65a0 over the image.
DIFF_TEST(sg_tssg_system_shutdown, 0x255010)
{
    Fixture fixture;
    shape::StandIn functions(0x255150, &cTSSGSystem::UnregisterExpressionFunction, &StandUnregisterFunction);
    shape::StandIn commands(0x255170, &cTSSGSystem::UnregisterCommands, &StandUnregisterCommands);
    fixture.Image(0x3a65a0, &Stand3a65a0);
    auto original = ctx.Original<uint8_t(__thiscall *)(void *)>(0x255010);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 gen(seed * 69069u + 0x255010u);
        std::set<uint32_t> ids;
        const uint32_t count = gen() % 3 ? gen() % 30 : 0;
        while (ids.size() < count) ids.insert(gen());
        uint8_t start[kObjectSize];
        for (uint8_t &b : start) b = uint8_t(gen());
        std::memcpy(start + 0x14, &ids, sizeof(ids));
        auto *sys = reinterpret_cast<cTSSGSystem *>(start);
        const unsigned fakes[] = {fRef2C8, fRef2CC, fScene, fAnim, fPersist};
        SGRef *fields[] = {&sys->mRef2C8, &sys->mRef2CC, &sys->mScene, &sys->mAnimSystem, &sys->mPersistService};
        for (unsigned i = 0; i < 5; ++i) fields[i]->mObject = gen() % 4 ? &F(fakes[i]) : nullptr;
        auto prepare = [&] {
            F(fServer).Set(6, "pd", 0u);
            F(fHolder).Set(9, "dpd", 0u);
        };
        Compare(ctx, "shutdown", seed, start, prepare, [&](bool useOriginal) -> uint32_t {
            if (useOriginal) return original(gObject + 0xc);
            return reinterpret_cast<nTSSG::cTSSGSystemServiceSub *>(gObject + 0xc)->Shutdown();
        });
    }
}

namespace {
// The registry's virtual base logs each unregistered name (slot 5): its address and its first word.
uint32_t NameArg(Obj *, uint32_t *args)
{
    Ev(evStand);
    Ev(args[0] - P(GameAddress<void>(0)));
    Ev(*reinterpret_cast<const uint32_t *>(uintptr_t(args[0])));
    return 0;
}
template <class Run> void CompareUnregister(difftest::Context &ctx, const char *what, Run run)
{
    for (uint32_t seed = 0; seed < 4; ++seed) {
        uint8_t start[kObjectSize] = {};
        auto prepare = [&] {
            gRegistry.words[0] = 0x0e9e0000;
            gRegistry.words[1] = P(gRegistry.table);
            gRegistry.table[0] = gRegistry.table[1] = 0;
            gRegistry.table[2] = int32_t(P(&F(fRegistryBase)) - P(&gRegistry.words[1]));
            *GameAddress<void *>(kRegistryCell) = &gRegistry;
            F(fRegistryBase).Set(5, "p", 0u, &NameArg);
        };
        Compare(ctx, what, seed, start, prepare, run);
    }
}
} // namespace

// 0x255150 UnregisterExpressionFunction: the registry (cached) gets the one name.
DIFF_TEST(sg_tssg_system_unregister_expression_function, 0x255150)
{
    Fixture fixture;
    auto original = ctx.Original<void(__cdecl *)()>(0x255150);
    CompareUnregister(ctx, "unregister_expression_function", [&](bool useOriginal) -> uint32_t {
        if (useOriginal) original();
        else cTSSGSystem::UnregisterExpressionFunction();
        return 0;
    });
}

// 0x255170 UnregisterCommands: the registry gets the eight names, in order.
DIFF_TEST(sg_tssg_system_unregister_commands, 0x255170)
{
    Fixture fixture;
    auto original = ctx.Original<void(__cdecl *)()>(0x255170);
    CompareUnregister(ctx, "unregister_commands", [&](bool useOriginal) -> uint32_t {
        if (useOriginal) original();
        else cTSSGSystem::UnregisterCommands();
        return 0;
    });
}
