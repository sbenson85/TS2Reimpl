// Differential tests for nTSSG::cTSSGSystem's start-up members (src/sg/cTSSGSystemInit.cpp): CreateManagers 0x24d260,
// CreateOverlay 0x248c80 and ReadLotTunables 0x248720 (hook shape: the original runs on the same object image as ours).
//
// The world (tests/ts_services_fixture.h): recording fakes behind the director's framework (slot 3, GetClassObject: a fake
// by class id, or null where the seed says so and the original tests for null), the service object 0x10c9378 (slot 9
// AddSystemService, slot 5 the property notifier and the app, slot 21 the command line: a real cRZCmdLine over a text the
// seed draws, with or without the asset-server switch), the property notifier (slot 4 watch, slot 3 its property set), the
// app singleton 0x10cc288 (slot 31, debug mode), the scene's loader (mScene slot 12), the +0xb4 object, the command parser
// and the asset collector. Each manager fake's Init (slot 3) logs.
//
// Stood in on both sides: cRZFile::PathExists 0x1c1242 (a name exists unless it starts with 'x'), cTSAppCommonImpl slot 5
// 0x1d0c60 (empties the list, then gives the seed's names for the option), cRZStopwatch::Start 0x1c982b (logs; the real one
// reads the performance counter). Stood in over the image (both sides call the image's): 0x3a18b0, 0xb39000, 0x3a3c30 (ours since lane YA: a StandIn on both sides),
// 0xb38ef0, the parser target's constructor 0x3a1ae0 (lays out a virtual-base table in the block whose two entries
// reach fakes), the float reader 0xaf7190 (writes a value from the seed and the name) and the game's operator new / delete.
//
// Compared after each run: the event log (every fake call with its arguments; pointers normalised to fake ids, object
// offsets, the parser target block, or "a stack address"; the watches' names by text hash and their values by vtable and
// value), the object's 0x3b8 bytes (the parser target pointer normalised) and every fake's reference count.
// Not compared: allocations through the allocator service and operator new (strings and list nodes, freed in the same call
// on both sides; the parser target block is kept and normalised).
#include "code_patch.h"
#include "difftest.h"
#include "ts_services_fixture.h"

#include "app/AppInit.h"
#include "game/game.h"
#include "gz/cGZAny.h"
#include "rz/cRZCmdLine.h"
#include "rz/cRZFile.h"
#include "rz/cRZStopwatch.h"
#include "rz/cRZString.h"
#include "sg/SGUtilities.h"
#include "sg/cTSSGSystem.h"
#include "ts/cTSCatalog.h"
#include "world/WorldColliders.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace svcfx;
using nTSSG::cTSSGSystem;

constexpr uint32_t kObjectSize = 0x3b8;
constexpr uint32_t kObjectTag = 0x5e000000;
constexpr uint32_t kTargetTag = 0x7a000000;
enum : uint32_t { evExists = 0x70, evList, evWatch, evSetProperty, evStart, evOriginal, evReadFloat, evText };

// Fakes.
enum : unsigned {
    fFramework = 0,
    fService,
    fNotifier,
    fPropertySet,
    fApp,
    fOverlay,
    fScene,
    fLoader,
    fB4,
    fParser,
    fCollector,
    fLightRig,
    fLighting,
    fOldTargetBase,
    fTargetBase1,
    fTargetBase2,
    fProperties,
    fManagers, // 17..23, shared in turn by the other managers
};
constexpr unsigned kManagerFakes = kFakes - fManagers;

uint32_t TextHash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ uint8_t(*s)) * 16777619u;
    return h;
}

// ---- the world a seed draws ----
struct World {
    bool overlayNull[9];
    bool queryNull;
    bool collectorNull;
    bool loaderNull;
    bool appCellSet;
    bool appFromService;
    uint8_t debug;
    float floats[3];
    std::map<uint32_t, std::vector<std::string>> lists;
    std::string commandLine;
};
World gWorld;

constexpr uint32_t kOverlayClass = 0xac22fa8d;
constexpr uint32_t kQueryClass = 0x4988a36a;
constexpr uint32_t kCollectorClass = 0xeda217dc;
constexpr uint32_t kManagerClasses[] = {0x896d0177, 0x0bf9c04f, 0x6b3a33c7, 0xe9945f8d, 0xe989cf03, 0xeab6b59e, 0xac7486ae,
                                        0xec439394, 0xef5e4e99, 0xd16de942, 0x31884107, 0x504d1938, 0xf24e3352, 0x69512c6d,
                                        0x5224dfdf, 0x2b940c24, 0x0bc4a38d, 0x6c4f4a1a, 0x2c9a6255};
constexpr uint32_t kLists[] = {0x4c9f79fd, 0x8c9f7a34, 0x2ea751e4, 0xac9f7a5a, 0xcc9f7985};

// The overlay the next GetClassObject for the overlay class gives (CreateOverlay test: by index).
int gOverlayIndex = -1;

uint32_t ClassObject(uint32_t clsid)
{
    if (clsid == kOverlayClass) return gOverlayIndex >= 0 && gWorld.overlayNull[gOverlayIndex] ? 0 : P(&F(fOverlay));
    if (clsid == kQueryClass) return gWorld.queryNull ? 0 : P(&F(fManagers));
    if (clsid == kCollectorClass) return gWorld.collectorNull ? 0 : P(&F(fCollector));
    if (clsid == 0x696d3daf) return P(&F(fLightRig));
    if (clsid == 0x0b390bf7) return P(&F(fLighting));
    if (clsid == 0x5cf9459a) return P(&F(fParser));
    for (unsigned i = 0; i < std::size(kManagerClasses); ++i)
        if (kManagerClasses[i] == clsid) return P(&F(fManagers + i % kManagerFakes));
    return 0;
}

// Framework slot 3 (clsid, iid, out): the object in *out, the byte result whether there is one.
uint32_t GetClassObject(Obj *, uint32_t *args)
{
    const uint32_t object = ClassObject(args[0]);
    *reinterpret_cast<uint32_t *>(uintptr_t(args[2])) = object;
    return object ? 1 : 0;
}

// Service slot 5 (clsid, iid, out): the property notifier, or the app (GetCachedSingleton's request).
uint32_t GetSystemService(Obj *, uint32_t *args)
{
    uint32_t object = 0;
    if (args[0] == 0x0ac1887e) object = P(&F(fNotifier));
    if (args[0] == 0x7a3e667d && gWorld.appFromService) object = P(&F(fApp));
    *reinterpret_cast<uint32_t *>(uintptr_t(args[2])) = object;
    return object ? 1 : 0;
}

void LogValue(uint32_t pointer)
{
    if (!pointer) return Ev(0);
    const auto *words = reinterpret_cast<const uint32_t *>(uintptr_t(pointer));
    Ev(words[0]);
    Ev(words[0] == P(GameAddress<const void>(gzany::kBooleanVTable)) ? words[1] & 0xff : words[1]);
}
const char *ServiceText(uint32_t pointer) { return reinterpret_cast<const GZServiceString *>(uintptr_t(pointer))->c_str(); }
const char *RZText(uint32_t pointer) { return reinterpret_cast<const cRZString *>(uintptr_t(pointer))->mString.c_str(); }

// Notifier slot 4 (target, message, name, default or null).
uint32_t Watch(Obj *, uint32_t *args)
{
    Ev(evWatch);
    Ev(TextHash(ServiceText(args[2])));
    LogValue(args[3]);
    return 1;
}
// Property set slot 3 (name, value).
uint32_t SetProperty(Obj *, uint32_t *args)
{
    Ev(evSetProperty);
    Ev(TextHash(ServiceText(args[0])));
    LogValue(args[1]);
    return 1;
}
// Slots that take a char pointer (first argument) or a cRZString.
uint32_t CharText(Obj *, uint32_t *args)
{
    Ev(evText);
    Ev(TextHash(reinterpret_cast<const char *>(uintptr_t(args[0]))));
    return 1;
}
uint32_t StringText(Obj *, uint32_t *args)
{
    Ev(evText);
    Ev(TextHash(RZText(args[0])));
    return 1;
}

cRZCmdLine gCommandLine;
bool gCommandLineBuilt = false;
void BuildCommandLine(const char *text)
{
    if (gCommandLineBuilt) gCommandLine.Destruct();
    gCommandLineBuilt = text != nullptr;
    if (text) gCommandLine.Construct(text);
}

// ---- stand-ins ----
bool __cdecl StandPathExists(const cIGZString &path)
{
    const std::string &s = reinterpret_cast<const cRZString &>(path).mString;
    Ev(evExists);
    Ev(TextHash(s.c_str()));
    return !s.empty() && s[0] != 'x';
}
uint32_t __cdecl StandCommonImpl5(uint32_t option, uint32_t list, uint32_t c, uint32_t d, uint32_t e)
{
    Ev(evList);
    Ev(option);
    Ev(c);
    Ev(d);
    Ev(e);
    auto *files = reinterpret_cast<GZStringList *>(uintptr_t(list));
    files->clear();
    for (const auto &name : gWorld.lists[option]) files->push_back(GZString(name.c_str()));
    return 1;
}
void __fastcall StandStart(void *self, void *)
{
    Ev(evStart);
    Ev(Norm(P(self)));
}
uint8_t gEffects[4];
void __cdecl Stand3a18b0()
{
    Ev(evOriginal);
    Ev(0x3a18b0);
}
void *__cdecl StandB39000()
{
    Ev(evOriginal);
    Ev(0xb39000);
    return gEffects;
}
void __cdecl Stand3a3c30(cITSEffectsManager *effects)
{
    Ev(evOriginal);
    Ev(0x3a3c30);
    Ev(static_cast<void *>(effects) == gEffects);
}
void *__cdecl StandB38ef0()
{
    Ev(evOriginal);
    Ev(0xb38ef0);
    return &F(fB4);
}
void __fastcall Stand248f90(void *self, void *)
{
    Ev(evOriginal);
    Ev(0x248f90);
    Ev(Norm(P(self)));
}
// A block with a virtual-base table at +8 whose entries 1 and 2 reach `base1` / `base2` (offsets from +8), the table at +0x10.
void LayOutTarget(uint8_t *block, Obj &base1, Obj &base2)
{
    auto *table = reinterpret_cast<int32_t *>(block + 0x10);
    table[0] = -8;
    table[1] = int32_t(P(&base1) - P(block + 8));
    table[2] = int32_t(P(&base2) - P(block + 8));
    *reinterpret_cast<int32_t **>(block + 8) = table;
}
void *__fastcall StandTargetCtor(void *memory, void *, uint32_t arg)
{
    Ev(evOriginal);
    Ev(0x3a1ae0);
    Ev(arg);
    std::memset(memory, 0, 500);
    LayOutTarget(static_cast<uint8_t *>(memory), F(fTargetBase1), F(fTargetBase2));
    AddRange(memory, 500, kTargetTag);
    return memory;
}
uint32_t __cdecl StandReadFloat(void *properties, const char *name, float *out, void *d)
{
    Ev(evReadFloat);
    Ev(Norm(P(properties)));
    Ev(TextHash(name));
    Ev(Norm(P(out)));
    Ev(P(d));
    const int which = TextHash(name) % 3;
    if (gWorld.floats[which] >= 0) *out = gWorld.floats[which] + float(TextHash(name) & 7);
    return 1;
}
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

alignas(16) uint8_t gOldTarget[0x40];

// ---- the fixture ----
struct Fixture {
    AllocatorScope allocator;
    Globals globals;
    std::vector<std::unique_ptr<shape::CodePatch>> image;
    std::vector<std::unique_ptr<shape::StandIn>> both;

    template <class Stand> void Image(uint32_t rva, Stand stand)
    {
        image.emplace_back(new shape::CodePatch(GameAddress<void>(rva), shape::CodeOf(stand)));
    }
    Fixture()
    {
        Image(0x824632, &GameNew);
        Image(0x824624, &GameDelete);
        Image(0x3a18b0, &Stand3a18b0);
        Image(0xb39000, &StandB39000);
        both.emplace_back(new shape::StandIn(0x3a3c30, &world_colliders::RegisterWorldObjects, &Stand3a3c30)); // ours since lane YA
        Image(0xb38ef0, &StandB38ef0);
        Image(0x3a1ae0, &StandTargetCtor);
        both.emplace_back(new shape::StandIn(0xaf7190, &nGZSceneGraph::ReadFloatProperty, &StandReadFloat));
        both.emplace_back(new shape::StandIn(0x1c1242, &cRZFile::PathExists, &StandPathExists));
        both.emplace_back(new shape::StandIn(0x1d0c60, &appinit::CommonImplSlot5, &StandCommonImpl5));
        both.emplace_back(new shape::StandIn(0x1c982b, &cRZStopwatch::Start, &StandStart));
        globals.Set(0x10c9378, &F(fService));
        globals.SetWord(kAppCell, 0);
        gFrameworkIndex = fFramework;
    }
    ~Fixture() { BuildCommandLine(nullptr); }
};

// The seed's world, then the fakes' slots for it.
void DrawWorld(std::mt19937 &gen)
{
    World &w = gWorld;
    for (bool &b : w.overlayNull) b = gen() % 4 == 0;
    w.queryNull = gen() % 3 == 0;
    w.collectorNull = gen() % 3 == 0;
    w.loaderNull = gen() % 4 == 0;
    w.appCellSet = gen() % 2 == 0;
    w.appFromService = gen() % 3 != 0;
    w.debug = gen() % 3 == 0 ? 0 : uint8_t(1 + gen() % 255);
    for (float &f : w.floats) f = gen() % 4 == 0 ? -1.0f : float(gen() % 50) / 4.0f;
    w.lists.clear();
    for (uint32_t option : kLists) {
        auto &names = w.lists[option];
        const unsigned count = gen() % 4;
        for (unsigned i = 0; i < count; ++i) {
            std::string name = gen() % 3 == 0 ? "x" : "f";
            name += std::to_string(option & 0xfff) + "_" + std::to_string(gen() % 1000) + ".txt";
            names.push_back(name);
        }
    }
    const std::string sw(GameAddress<const char>(0xdaee9c), 19);
    w.commandLine = "Sims2EP9.exe -w";
    switch (gen() % 5) {
    case 0: break;
    case 1: w.commandLine += " -" + sw + ":server" + std::to_string(gen() % 100); break;
    case 2: w.commandLine += " /" + sw + ":\"quoted host\""; break;
    case 3: w.commandLine += " -" + sw; break; // no value: not present
    default: w.commandLine += " -" + sw + ":h -other:1"; break;
    }
}

void SetUpFakes()
{
    ResetAll();
    F(fFramework).Set(3, "ddp", 0u, &GetClassObject);
    F(fService).Set(9, "p", 1u);
    F(fService).Set(5, "ddp", 0u, &GetSystemService);
    F(fService).Set(21, 0, &gCommandLine);
    F(fNotifier).Set(4, "pdpp", 0u, &Watch);
    F(fNotifier).Set(3, 0, &F(fPropertySet));
    F(fPropertySet).Set(3, "pp", 0u, &SetProperty);
    F(fApp).Set(31, 0, gWorld.debug);
    F(fScene).Set(12, 0, gWorld.loaderNull ? nullptr : &F(fLoader));
    F(fLoader).Set(7, "p", 0u, &StringText);
    F(fB4).Set(17, "p", 0u, &StringText);
    F(fLightRig).Set(5, "p", 0u, &CharText);
    F(fLighting).Set(5, 0, 1u);
    F(fParser).Set(5, "pd", 1u);
    F(fParser).Set(6, "pd", 1u);
    F(fParser).Set(7, 0, 1u);
    F(fParser).Set(9, 0, &F(fProperties));
    F(fCollector).Set(7, "p", 0u, &CharText);
    for (unsigned i : {unsigned(fOverlay), unsigned(fLightRig), unsigned(fLighting), unsigned(fParser), unsigned(fScene)})
        F(i).Set(3, 0, 1u);
    for (unsigned i = fManagers; i < kFakes; ++i) F(i).Set(3, 0, 1u);
    LayOutTarget(gOldTarget, F(fOldTargetBase), F(fOldTargetBase));
}

// The object a seed starts from: random bytes, the references CreateManagers reads set to null or a fake.
void DrawObject(std::mt19937 &gen, uint8_t *object, bool early)
{
    for (uint32_t i = 0; i < kObjectSize; ++i) object[i] = uint8_t(gen());
    auto *sys = reinterpret_cast<cTSSGSystem *>(object);
    sys->mManagersCreated = early ? uint8_t(1 + gen() % 255) : 0;
    auto pick = [&](nTSSG::SGRef &ref, unsigned fake) { ref.mObject = gen() % 2 ? &F(fake) : nullptr; };
    for (int i : {0, 1, 5, 6, 8}) pick(sys->mOverlays[i], fOverlay);
    nTSSG::SGRef *managers[] = {&sys->mLightMapManager, &sys->mShadowManager, &sys->mTerrain, &sys->mFloorManager,
                                &sys->mWallManager, &sys->mFenceManager, &sys->mPoolManager, &sys->mLotSkirt,
                                &sys->mRugManager, &sys->mCurvedPoolManager, &sys->mTerrainCutoutManager,
                                &sys->mAwningManager, &sys->mSeasonManager, &sys->mQueryStrategyManager,
                                &sys->mSceneManager, &sys->mTerrainSnowManager, &sys->mRoofManager,
                                &sys->mLotImposterManager, &sys->mVideoBroadcastManager, &sys->m3DUIManager};
    for (unsigned i = 0; i < std::size(managers); ++i) pick(*managers[i], fManagers + (i + 3) % kManagerFakes);
    pick(sys->mLightRigManager, fLightRig);
    pick(sys->mLightingManager, fLighting);
    pick(sys->mCommandParser, fParser);
    pick(sys->mRefB4, gen() % 2 ? fB4 : fManagers);
    pick(sys->mAssetCollector, fCollector);
    sys->mScene.mObject = &F(fScene);
    sys->mParserTarget = gen() % 2 ? gOldTarget : nullptr;
}

alignas(16) uint8_t gObject[kObjectSize];

// One side's outcome: the log, the object (parser target normalised), the fakes.
Blob Outcome()
{
    Blob b = gEv;
    b.push_back(0xeeee);
    for (uint32_t i = 0; i < kObjectSize; i += 4) {
        uint32_t word;
        std::memcpy(&word, gObject + i, 4);
        b.push_back(i == 0x10c ? Norm(word) : word);
    }
    CaptureFakes(b);
    return b;
}

template <class Run> void Compare(difftest::Context &ctx, const char *what, uint32_t seed, const uint8_t *start, Run run)
{
    Blob outcome[2];
    for (int side = 0; side < 2; ++side) {
        SetUpFakes();
        std::memcpy(gObject, start, kObjectSize);
        AddRange(gObject, kObjectSize, kObjectTag);
        AddRange(gOldTarget, sizeof(gOldTarget), 0x7b000000);
        *GameAddress<void *>(kAppCell) = gWorld.appCellSet ? &F(fApp) : nullptr;
        ctx.Mark(side ? "ours" : "original");
        run(side == 0);
        outcome[side] = Outcome();
    }
    Same(ctx, outcome[0], outcome[1], what, seed);
}

// ---- procedural animations (0x248f90 / 0x24a280) ----
// Three animation blocks with a virtual-base table at +4 whose entry 1 reaches a fake (the base: slot 12 names it, slot 8
// gives the key object, the properties fake, whose slot 4 takes the parsed key). The builders 0x3a27f0 / 0x3a25e0 are
// stood in over the image (both sides call the image's): each logs, writes its block (or null, or nothing) and answers
// the seed's result.
alignas(16) uint8_t gAnimations[3][0x20];
const unsigned kAnimationBases[3] = {fOldTargetBase, fTargetBase1, fTargetBase2};
struct BuildPlan {
    uint8_t result;
    int write; // 0 nothing, 1 the block, 2 null
};
BuildPlan gBuild[3];
int gBuildCall = 0;
uint32_t Build(void **out, uint32_t flag)
{
    const int i = gBuildCall++ % 3;
    Ev(evOriginal);
    Ev(0x3a25e0 + uint32_t(i));
    Ev(flag);
    Ev(*out == nullptr);
    if (gBuild[i].write == 1) *out = gAnimations[i];
    if (gBuild[i].write == 2) *out = nullptr;
    return gBuild[i].result;
}
uint8_t __cdecl StandClockBuilder(void **out) { return uint8_t(Build(out, 0xff)); }
uint8_t __cdecl StandPlatformBuilder(uint32_t flag, void **out) { return uint8_t(Build(out, flag)); }
uint32_t NameAnimation(Obj *, uint32_t *args)
{
    Ev(evText);
    Ev(TextHash(reinterpret_cast<const char *>(uintptr_t(args[0]))));
    return 1;
}
uint32_t TakeKey(Obj *, uint32_t *args)
{
    const auto *key = reinterpret_cast<const uint32_t *>(uintptr_t(args[0]));
    for (int i = 0; i < 4; ++i) Ev(key[i]);
    return 1;
}
void SetUpAnimations()
{
    for (int i = 0; i < 3; ++i) {
        std::memset(gAnimations[i], 0, sizeof(gAnimations[i]));
        Obj &base = F(kAnimationBases[i]);
        auto *table = reinterpret_cast<int32_t *>(gAnimations[i] + 0x10);
        table[0] = -4;
        table[1] = int32_t(P(&base) - P(gAnimations[i] + 4));
        *reinterpret_cast<int32_t **>(gAnimations[i] + 4) = table;
        AddRange(gAnimations[i], sizeof(gAnimations[i]), 0x7c000000 + (uint32_t(i) << 16));
        base.Set(12, "p-", 0u, &NameAnimation);
        base.Set(8, 0, &F(fProperties));
    }
    F(fProperties).Set(4, "p", 0u, &TakeKey);
    F(fManagers).Set(13, "p", 1u);
    gBuildCall = 0;
}

} // namespace

// 0x24d260 CreateManagers (with 0x248c80 CreateOverlay and 0x248720 ReadLotTunables as its callees on each side).
DIFF_TEST(sg_tssg_system_create_managers, 0x24d260)
{
    Fixture fixture;
    // AddProceduralAnimations (0x248f90, ours on our side) is stood in on both sides; its own test is below.
    shape::StandIn animations(0x248f90, &cTSSGSystem::AddProceduralAnimations, &Stand248f90);
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x24d260);
    for (uint32_t seed = 0; seed < 400; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x24d260u);
        DrawWorld(gen);
        BuildCommandLine(gWorld.commandLine.c_str());
        uint8_t start[kObjectSize];
        DrawObject(gen, start, seed % 23 == 5);
        // The overlay class object is null for all five overlays or for none (the seed's draw for one index); the
        // CreateOverlay test covers each index.
        gOverlayIndex = int(gen() % 9);
        Compare(ctx, "create_managers", seed, start, [&](bool useOriginal) {
            if (useOriginal) original(gObject);
            else reinterpret_cast<cTSSGSystem *>(gObject)->CreateManagers();
        });
    }
}

// 0x248c80 CreateOverlay(index), every index, with and without an old overlay and a new one.
DIFF_TEST(sg_tssg_system_create_overlay, 0x248c80)
{
    Fixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x248c80);
    for (uint32_t seed = 0; seed < 200; ++seed) {
        std::mt19937 gen(seed * 31u + 0x248c80u);
        DrawWorld(gen);
        uint8_t start[kObjectSize];
        DrawObject(gen, start, false);
        const uint32_t index = seed % 9;
        auto *sys = reinterpret_cast<cTSSGSystem *>(start);
        sys->mOverlays[index].mObject = gen() % 2 ? &F(fOverlay) : nullptr;
        gOverlayIndex = int(index);
        Compare(ctx, "create_overlay", seed, start, [&](bool useOriginal) {
            if (useOriginal) original(gObject, index);
            else reinterpret_cast<cTSSGSystem *>(gObject)->CreateOverlay(index);
        });
    }
}

// 0x248720 ReadLotTunables(properties): the three reads, the values the reader writes (or leaves).
DIFF_TEST(sg_tssg_system_read_lot_tunables, 0x248720)
{
    Fixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *, void *)>(0x248720);
    for (uint32_t seed = 0; seed < 100; ++seed) {
        std::mt19937 gen(seed * 13u + 0x248720u);
        DrawWorld(gen);
        uint8_t start[kObjectSize];
        DrawObject(gen, start, false);
        void *properties = gen() % 4 ? static_cast<void *>(&F(fProperties)) : nullptr;
        Compare(ctx, "read_lot_tunables", seed, start, [&](bool useOriginal) {
            if (useOriginal) original(gObject, properties);
            else reinterpret_cast<cTSSGSystem *>(gObject)->ReadLotTunables(properties);
        });
    }
}

// 0x248f90 AddProceduralAnimations (with 0x24a280 AddProceduralAnimation as its callee on each side): the builders' results
// and outputs drawn per seed; the animation system (mAnimSystem) a fake.
DIFF_TEST(sg_tssg_system_add_procedural_animations, 0x248f90)
{
    Fixture fixture;
    shape::CodePatch clock(GameAddress<void>(0x3a27f0), shape::CodeOf(&StandClockBuilder));
    shape::CodePatch platform(GameAddress<void>(0x3a25e0), shape::CodeOf(&StandPlatformBuilder));
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x248f90);
    for (uint32_t seed = 0; seed < 200; ++seed) {
        std::mt19937 gen(seed * 101u + 0x248f90u);
        DrawWorld(gen);
        for (BuildPlan &b : gBuild) {
            b.result = uint8_t(gen() % 3 ? 1 + gen() % 255 : 0);
            // A builder that succeeds gives its block (the original adds the result untested); one that fails gives the
            // block, null or nothing.
            b.write = b.result ? 1 : int(gen() % 3);
        }
        uint8_t start[kObjectSize];
        DrawObject(gen, start, false);
        reinterpret_cast<cTSSGSystem *>(start)->mAnimSystem.mObject = &F(fManagers);
        Compare(ctx, "add_procedural_animations", seed, start, [&](bool useOriginal) {
            SetUpAnimations();
            if (useOriginal) original(gObject);
            else reinterpret_cast<cTSSGSystem *>(gObject)->AddProceduralAnimations();
        });
    }
}

// 0x24a280 AddProceduralAnimation(animation, name) called directly, with the image's names and other texts.
DIFF_TEST(sg_tssg_system_add_procedural_animation, 0x24a280)
{
    Fixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *, void *, const char *)>(0x24a280);
    const char *texts[] = {"plain", "o-anim_text", "", "UPPER_case_anim", "#12!thing", "##7!other"};
    for (uint32_t seed = 0; seed < 60; ++seed) {
        std::mt19937 gen(seed * 7u + 0x24a280u);
        DrawWorld(gen);
        uint8_t start[kObjectSize];
        DrawObject(gen, start, false);
        reinterpret_cast<cTSSGSystem *>(start)->mAnimSystem.mObject = &F(fManagers);
        const char *name = seed < 3 ? GameAddress<const char>(seed == 0 ? 0xdae7c0 : seed == 1 ? 0xdae7e0 : 0xdae808)
                                    : texts[seed % std::size(texts)];
        const int which = int(seed % 3);
        Compare(ctx, "add_procedural_animation", seed, start, [&](bool useOriginal) {
            SetUpAnimations();
            if (useOriginal) original(gObject, gAnimations[which], name);
            else reinterpret_cast<cTSSGSystem *>(gObject)->AddProceduralAnimation(gAnimations[which], name);
        });
    }
}
