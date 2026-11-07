// Differential tests for nTSSG::cTSSGSystem's message handler and its helpers (src/sg/cTSSGSystemMessages.cpp):
// DoMessage 0x249130 (on the cIGZMessageTarget2 face at +8), SetLevelRange 0x254dc0, ReloadMaterials 0x2556a0,
// RefreshMaterials 0x24b160 and TouchEntry 0x247fd0 (hook shape: the original runs on the same object image as ours).
//
// The world (tests/ts_services_fixture.h): a message fake (QueryInterface 0x65297976 gives the data fake or null; slot 4
// the type) and its data fake (slots 8, 9, 13, 15 the any, 29 a query, 32 a source object whose QueryInterface gives a
// fake or null); the service object (slot 5: the settings property set, whose slot 9 reads a boolean the seed draws, and
// the holder of the material and detail objects); the cached cells the original's getters read (GetCachedSingleton,
// GetCachedSlot23 and its lot world, the placement service 0x20e0b2, the cars object 0xb39000, the material object
// 0xae8900, the detail object 0xaead80, the message server 0x9c5a89, which logs posted messages by content); the framework
// (slot 3: the occupant manager); and fakes in every manager field the handler uses. The object's own vtable is a table of
// logging functions for the primary slots the handler calls (19, 20, 89..95).
//
// DoMessage's callees that are ours (CreateManagers, ReleaseManagers, SetLevelRange, ReloadMaterials, RefreshMaterials,
// TouchEntry) are stood in on both sides and tested on their own; the originals it calls (0x24ebd0, 0x24c980, 0x254490)
// are stood in over the image (both sides call the image's).
//
// Compared after each run: the result, the event log (pointers normalised to fake ids, object offsets, the any buffer or
// "a stack address"), the object's 0x3b8 bytes, every fake's reference count and the getters' cache cells.
// Two arguments are logged by their low byte only: the original pushes a register whose upper bytes are left over from
// earlier code (mRefC0 slot 8 at 0x249d89 `mov dl, [eax+4]; ... push edx`; primary slot 91's flag at 0x24934b
// `setne byte [ebp-0x14]; push dword [ebp-0x14]`), and the callees take a bool.
#include "code_patch.h"
#include "difftest.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/cGZAny.h"
#include "gz/cIGZPropertySet.h"
#include "rz/cRZString.h"
#include "sg/cTSSGSystem.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

namespace {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{3, "d"}};

using namespace svcfx;
using nTSSG::cTSSGSystem;

constexpr uint32_t kObjectSize = 0x3b8;
constexpr uint32_t kObjectTag = 0x5e000000;
constexpr uint32_t kAnyTag = 0x6a000000;
enum : uint32_t { evSelf = 0x80, evStand, evSetting, evText };

enum : unsigned {
    fFramework = 0,
    fService,
    fMessage,
    fData,
    fSource,
    fQueried,
    fSettings,
    fWorld,
    fLot,
    fPlacement,
    fServer,
    fCars,
    fMaterials,
    fDetail,
    fShadow,
    fAnim,
    fRef84,
    fTerrain,
    fLotSkirt,
    fRef90,
    fOccupants,
    fRefC0,
    fNewOccupants,
    fHolder,
};
static_assert(fHolder < kFakes);

// Cells the original's getters read.
constexpr uint32_t kQueriedCell = 0x10f8fcc; // GetCachedQueriedObject
constexpr uint32_t kDetailCell = 0x10f9028;  // 0xaead80
constexpr uint32_t kPlacementCell = 0x10cc298; // 0x20e0b2
constexpr uint32_t kCarsCell = 0x10faf84;    // 0xb39000
constexpr uint32_t kServerCell = 0x10f519c;  // 0x9c5a89
constexpr uint32_t kCells[] = {kAppCell, kSlot23Cache, kQueriedCell, kDetailCell, kPlacementCell, kCarsCell, kServerCell};
constexpr char kCellKinds[] = "ppppppp"; // the cells' words as the outcome logs them (svcfx::DumpCells)


uint32_t TextHash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ uint8_t(*s)) * 16777619u;
    return h;
}

// ---- the seed's world ----
struct World {
    uint32_t type;
    bool dataNull;
    bool queriedNull, sourceQueriedNull;
    uint8_t setting;
    bool detailNull;
    bool lotWorld, lotNull;
    uint32_t lotId;
};
World gWorld;
alignas(8) uint8_t gAny[8];

uint32_t MessageQuery(Obj *, uint32_t *args)
{
    *reinterpret_cast<uint32_t *>(uintptr_t(args[1])) = gWorld.dataNull ? 0 : P(&F(fData));
    return gWorld.dataNull ? 0 : 1;
}
uint32_t DataQuery(Obj *self, uint32_t *args)
{
    *reinterpret_cast<uint32_t *>(uintptr_t(args[1])) = gWorld.queriedNull ? 0 : P(&F(fQueried));
    return self->value[0];
}
uint32_t SourceQuery(Obj *, uint32_t *args)
{
    *reinterpret_cast<uint32_t *>(uintptr_t(args[1])) = gWorld.sourceQueriedNull ? 0 : P(&F(fQueried));
    return gWorld.sourceQueriedNull ? 0 : 1;
}
uint32_t ServiceQuery(Obj *, uint32_t *args)
{
    uint32_t object = 0;
    if (args[0] == 0x0ac1887e) object = P(&F(fSettings));
    if (args[0] == 0xe5074b6b) object = P(&F(fHolder));
    *reinterpret_cast<uint32_t *>(uintptr_t(args[2])) = object;
    return object ? 1 : 0;
}
uint32_t FrameworkClass(Obj *, uint32_t *args)
{
    const uint32_t object = args[0] == 0x6bd49281 ? P(&F(fNewOccupants)) : 0;
    *reinterpret_cast<uint32_t *>(uintptr_t(args[2])) = object;
    return object ? 1 : 0;
}
// Settings slot 9 (key, any): logs the key and the any's state, then sets the seed's value.
uint32_t ReadSetting(Obj *, uint32_t *args)
{
    auto *any = reinterpret_cast<uint32_t *>(uintptr_t(args[1]));
    Ev(evSetting);
    Ev(TextHash(reinterpret_cast<const GZServiceString *>(uintptr_t(args[0]))->c_str()));
    Ev(any[0]);
    Ev(any[1] & 0xff);
    reinterpret_cast<uint8_t *>(any)[4] = gWorld.setting;
    return 1;
}
// mRefC0 slot 8 (one argument, a bool): logged by its low byte (see the top of the file), in place of the generic slot.
uint32_t __fastcall RefC0Slot8(Obj *self, void *, uint32_t on)
{
    Ev(evCall);
    Ev(self->id);
    Ev(8);
    Ev(on & 0xff);
    return 1;
}

// The placement service (0x20e0b2): slots past the fakes' 80, so a table of its own. Slots 85 / 86 log, 88 / 89 log and
// answer the seed's level range.
int32_t gFirstLevel, gLastLevel;
struct Placement {
    void **vt;
};
template <int S> int32_t __fastcall PlacementSlot(void *, void *)
{
    Ev(evCall);
    Ev(0x9900);
    Ev(S);
    return S == 88 ? gFirstLevel : S == 89 ? gLastLevel : 0;
}
void *gPlacementVTable[100];
Placement gPlacement{gPlacementVTable};
void FillPlacement()
{
    gPlacementVTable[85] = reinterpret_cast<void *>(&PlacementSlot<85>);
    gPlacementVTable[86] = reinterpret_cast<void *>(&PlacementSlot<86>);
    gPlacementVTable[88] = reinterpret_cast<void *>(&PlacementSlot<88>);
    gPlacementVTable[89] = reinterpret_cast<void *>(&PlacementSlot<89>);
}

// The object's primary vtable: slots 19, 20, 89..95 log; the rest are never called.
template <int S> uint32_t __fastcall Self0(void *self, void *)
{
    Ev(evSelf);
    Ev(S);
    Ev(Norm(P(self)));
    return 0;
}
// Self1<S>'s argument by slot S (svcfx::Field, word S of the string).
constexpr char kSelfArgKinds[] =
    "-------- -------- ---dd--- -------- -------- -------- -------- -------- "
    "-------- -------- -------- -pp";
template <int S> uint32_t __fastcall Self1(void *self, void *, uint32_t a)
{
    Ev(evSelf);
    Ev(S);
    Ev(Norm(P(self)));
    svcfx::Field(svcfx::gEv, a, kSelfArgKinds, S * 4);
    return 0;
}
uint32_t __fastcall Self91(void *self, void *, uint32_t object, uint32_t flag)
{
    Ev(evSelf);
    Ev(91);
    Ev(Norm(P(self)));
    Ev(Norm(object));
    Ev(flag & 0xff);
    return 0;
}
void *gSelfVTable[100];
void FillSelfVTable()
{
    gSelfVTable[19] = reinterpret_cast<void *>(&Self1<19>);
    gSelfVTable[20] = reinterpret_cast<void *>(&Self1<20>);
    gSelfVTable[89] = reinterpret_cast<void *>(&Self1<89>);
    gSelfVTable[90] = reinterpret_cast<void *>(&Self1<90>);
    gSelfVTable[91] = reinterpret_cast<void *>(&Self91);
    gSelfVTable[92] = reinterpret_cast<void *>(&Self0<92>);
    gSelfVTable[93] = reinterpret_cast<void *>(&Self0<93>);
    gSelfVTable[94] = reinterpret_cast<void *>(&Self0<94>);
    gSelfVTable[95] = reinterpret_cast<void *>(&Self0<95>);
}

// ---- stand-ins ----
void __fastcall StandMember0(void *self, void *)
{
    Ev(evStand);
    Ev(Norm(P(self)));
}
template <uint32_t Rva> void __fastcall StandImage0(void *self, void *)
{
    Ev(evStand);
    Ev(Rva);
    Ev(Norm(P(self)));
}
void __fastcall StandSelected(void *self, void *, uint32_t on)
{
    Ev(evStand);
    Ev(0x254490);
    Ev(Norm(P(self)));
    Ev(on & 0xff);
}
void __fastcall StandLevels(void *self, void *, int32_t first, int32_t last)
{
    Ev(evStand);
    Ev(0x254dc0);
    Ev(Norm(P(self)));
    Ev(uint32_t(first));
    Ev(uint32_t(last));
}
void __fastcall StandTouch(void *self, void *, uint32_t id)
{
    Ev(evStand);
    Ev(0x247fd0);
    Ev(Norm(P(self)));
    Ev(id);
}
uint32_t gReloadTag = 0x2556a0, gRefreshTag = 0x24b160, gCreateTag = 0x24d260;
void __fastcall StandReload(void *self, void *) { Ev(evStand), Ev(gReloadTag), Ev(Norm(P(self))); }
void __fastcall StandRefresh(void *self, void *) { Ev(evStand), Ev(gRefreshTag), Ev(Norm(P(self))); }
void __fastcall StandCreate(void *self, void *) { Ev(evStand), Ev(gCreateTag), Ev(Norm(P(self))); }
uint32_t gReleaseTag = 0x24f4e0;
void __fastcall StandRelease(void *self, void *) { Ev(evStand), Ev(gReleaseTag), Ev(Norm(P(self))); }

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
        FillSelfVTable();
        FillPlacement();
        globals.Set(0x10c9378, &F(fService));
        for (uint32_t cell : kCells) globals.SetWord(cell, 0);
        gFrameworkIndex = fFramework;
    }
};

void SetUpFakes()
{
    ResetAll();
    F(fFramework).Set(3, "ddp", 0u, &FrameworkClass);
    F(fService).Set(5, "ddp", 0u, &ServiceQuery);
    F(fMessage).Set(0, "dp", 0u, &MessageQuery);
    F(fMessage).Set(4, 0, gWorld.type);
    F(fData).Set(29, "pp", 0u, &DataQuery);
    F(fSource).Set(0, "dp", 0u, &SourceQuery);
    F(fSettings).Set(9, "pp", 0u, &ReadSetting);
    F(fHolder).Set(5, 0, &F(fMaterials));
    F(fHolder).Set(9, 0, gWorld.detailNull ? nullptr : &F(fDetail));
    F(fWorld).Set(5, 0, gWorld.lotWorld ? 0x0bea28c2u : 0x0bea28c3u);
    F(fWorld).Set(9, 0, gWorld.lotNull ? nullptr : &F(fLot));
    F(fLot).Set(18, 0, gWorld.lotId);
    UseAsMessageServer(F(fServer));
    F(fRefC0).Set(8, 1, 0u);
    F(fRefC0).table[8] = reinterpret_cast<void *>(&RefC0Slot8);
    F(fNewOccupants).Set(3, 0, 1u);
    F(fNewOccupants).Set(5, 1, 1u);
    // Every other slot the handler can call on the managers, with its argument count.
    F(fShadow).Set(6, "dd", 1u);
    F(fAnim).Set(12, "d", 1u);
    F(fAnim).Set(20, "d", 1u);
    F(fRef84).Set(18, "d", 1u);
    F(fTerrain).Set(24, "dd", 1u);
    F(fTerrain).Set(25, "d", 1u);
    F(fLotSkirt).Set(16, 0, 1u);
    F(fLotSkirt).Set(7, "d", 1u);
    F(fRef90).Set(6, "d", 1u);
    F(fRef90).Set(7, "d", 1u);
    F(fOccupants).Set(4, 0, 1u);
    F(fOccupants).Set(6, "dd", 1u);
    F(fCars).Set(31, "dd", 1u);
    F(fMaterials).Set(30, "p", 1u);
    F(fQueried).Set(6, 0, 1u);
}

// The data fake's answers for the seed.
void DrawData(std::mt19937 &gen, bool anyMustExist)
{
    Obj &data = F(fData);
    data.Set(8, 0, gen() % 3 ? 1u : 0u);
    data.Set(9, 0, gen() % 4 ? 0x1000u + gen() % 0x10000 : 0u);
    data.Set(13, 0, anyMustExist || gen() % 4 ? 1u : 0u);
    data.Set(15, 0, anyMustExist || gen() % 5 ? static_cast<void *>(gAny) : nullptr);
    data.Set(32, 0, &F(fSource));
    data.value[0] = gen() % 3 ? 1 : 0; // slot 29's result
    gAny[0] = 0x11;
    const uint32_t value = gen() % 2 ? gen() % 2 : gen();
    std::memcpy(gAny + 4, &value, 4);
}

// Message types: every handled one, and some the handler does not handle. `untested`: the handler uses the data (or its
// any) without a null test, so the seed always gives them.
struct Type {
    uint32_t id;
    bool untested;
};
constexpr Type kTypes[] = {
    {0x8d, false},       {0x8e, false},       {0x9a, false},       {0x9d, true},        {0xb2, true},        {0xb3, true},
    {0xb4, false},       {0xb5, false},       {0xb8, true},        {0xb9, false},       {0xbb, true},        {0xbc, true},
    {0xcf, false},       {0xd0, false},       {0xd3, false},       {0xd4, false},       {0xd5, false},       {0xd7, false},
    {0xd8, false},       {0x0be33684, false}, {0x8be6f77b, false}, {0x8be6f77c, false}, {0x0cdc679b, false}, {0xabfc20da, false},
    {0x8c1827f2, false}, {0xac1827fc, false}, {0x8d9a22c3, false}, {0x2cea9fb2, false}, {0xccdc9639, false}, {0x4ef8bd75, false},
    {0x2d8812c4, false}, {0x8dc9d24a, false}, {0x0c7bfea6, false}, {0x0cdc6794, false}, {0x2beb0d4c, false}, {0x2cae6756, true},
    {0x4c60901a, true},  {0x4c7ab209, true},  {0xac730900, true},  {0xac730901, true},  {0xac730902, true},  {0x6c618b86, true},
    {0x4c618b90, true},  {0xac618b98, true},  {0x0c618ba0, true},  {0xec3f494d, false}, {0x2b4d18fb, true},  {0x925df53f, false},
    {0x522ced95, false}, {0xb23b7d66, false}, {0xb23b7d6f, false}, {0xd23b7d7c, false}, {0x2b4d1900, false}, {0x2f9dc7f9, false},
    {0x4f9dc80b, false}, {0x4f9dc81a, false}, {0x12345678, false}, {0x0be33685, false}, {0xac730903, false}, {0xffffffff, false},
};

alignas(16) uint8_t gObject[kObjectSize];
// gObject's words as the outcome logs them (svcfx::DumpWords): 'p' pointer, 'd' data, 'x' not compared.
constexpr char kObjectKinds[] =
    "pddddddd dddddddd dddddddp ddddddpd pppdpddd ddpddddd pddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddd";

// The object a seed starts from: random bytes, its vtable ours, the manager references the handler reads null or a fake.
void DrawObject(std::mt19937 &gen, uint8_t *object)
{
    for (uint32_t i = 0; i < kObjectSize; ++i) object[i] = uint8_t(gen());
    auto *sys = reinterpret_cast<cTSSGSystem *>(object);
    sys->mVTable = gSelfVTable;
    auto pick = [&](nTSSG::SGRef &ref, unsigned fake) { ref.mObject = gen() % 4 ? &F(fake) : nullptr; };
    pick(sys->mShadowManager, fShadow);
    sys->mAnimSystem.mObject = &F(fAnim);
    pick(sys->mRef84, fRef84);
    pick(sys->mTerrain, fTerrain);
    pick(sys->mLotSkirt, fLotSkirt);
    pick(sys->mRef90, fRef90);
    pick(sys->mNHoodOccupants, fOccupants);
    pick(sys->mRefC0, fRefC0);
}

Blob Outcome(uint32_t result)
{
    Blob b = gEv;
    b.push_back(0xeeee);
    b.push_back(result);
    svcfx::DumpWords(b, gObject, sizeof(gObject), kObjectKinds);
    CaptureFakes(b, kValueKinds);
    svcfx::DumpCells(b, kCells, kCellKinds);
    return b;
}

// Runs both sides from `start`: the fakes reset, `prepare` (the seed's answers and cells), then `run` (returns the result).
template <class Prepare, class Run>
void Compare(difftest::Context &ctx, const char *what, uint32_t seed, const uint8_t *start, Prepare prepare, Run run)
{
    Blob outcome[2];
    for (int side = 0; side < 2; ++side) {
        SetUpFakes();
        prepare();
        std::memcpy(gObject, start, kObjectSize);
        AddRange(gObject, kObjectSize, kObjectTag);
        AddRange(gAny, sizeof(gAny), kAnyTag);
        ctx.Mark(side ? "ours" : "original");
        const uint32_t result = run(side == 0);
        outcome[side] = Outcome(result);
    }
    Same(ctx, outcome[0], outcome[1], what, seed);
}

// The cells for the seed (the getters fill an empty one through the service on both sides alike).
void SetCells(std::mt19937 &gen)
{
    *GameAddress<void *>(kAppCell) = nullptr;
    *GameAddress<void *>(kSlot23Cache) = &F(fWorld);
    *GameAddress<void *>(kQueriedCell) = gen() % 2 ? &F(fMaterials) : nullptr;
    *GameAddress<void *>(kDetailCell) = gen() % 2 && !gWorld.detailNull ? &F(fDetail) : nullptr;
    *GameAddress<void *>(kPlacementCell) = &gPlacement;
    *GameAddress<void *>(kCarsCell) = &F(fCars);
    *GameAddress<void *>(kServerCell) = &F(fServer);
}

} // namespace

// 0x249130 DoMessage (the face at +8): every handled type and some unhandled ones, with the data, its any, the settings,
// the lot world and the manager references drawn per seed.
DIFF_TEST(sg_tssg_system_do_message, 0x249130)
{
    Fixture fixture;
    shape::StandIn create(0x24d260, &cTSSGSystem::CreateManagers, &StandCreate);
    shape::StandIn levels(0x254dc0, &cTSSGSystem::SetLevelRange, &StandLevels);
    shape::StandIn reload(0x2556a0, &cTSSGSystem::ReloadMaterials, &StandReload);
    shape::StandIn refresh(0x24b160, &cTSSGSystem::RefreshMaterials, &StandRefresh);
    shape::StandIn touch(0x247fd0, &cTSSGSystem::TouchEntry, &StandTouch);
    shape::StandIn release(0x24f4e0, &cTSSGSystem::ReleaseManagers, &StandRelease);
    fixture.Image(0x24ebd0, &StandImage0<0x24ebd0>);
    fixture.Image(0x24c980, &StandImage0<0x24c980>);
    fixture.Image(0x254490, &StandSelected);
    auto original = ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x249130);
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        std::mt19937 gen(seed * 2654435761u + 0x249130u);
        const Type &type = kTypes[seed < std::size(kTypes) ? seed : gen() % std::size(kTypes)];
        World &w = gWorld;
        w.type = type.id;
        w.dataNull = !type.untested && gen() % 5 == 0;
        w.queriedNull = gen() % 3 == 0;
        w.sourceQueriedNull = gen() % 3 == 0;
        w.setting = gen() % 3 ? uint8_t(gen() % 2) : uint8_t(gen());
        w.detailNull = gen() % 4 == 0;
        w.lotWorld = gen() % 5 != 0;
        w.lotNull = gen() % 5 == 0;
        w.lotId = gen() % 4 ? 1 + gen() % 50 : 0;
        gFirstLevel = int32_t(gen() % 5) - 1;
        gLastLevel = gFirstLevel + int32_t(gen() % 6) - 1;
        uint8_t start[kObjectSize];
        DrawObject(gen, start);
        const uint32_t dataSeed = gen();
        const uint32_t cellSeed = gen();
        const bool nullMessage = gen() % 50 == 0;
        auto prepare = [&] {
            std::mt19937 dataGen(dataSeed), cellGen(cellSeed);
            DrawData(dataGen, type.untested);
            SetCells(cellGen);
        };
        Compare(ctx, "do_message", seed, start, prepare, [&](bool useOriginal) -> uint32_t {
            void *message = nullMessage ? nullptr : &F(fMessage);
            if (useOriginal) return original(gObject + 8, message);
            return reinterpret_cast<nTSSG::cTSSGSystemMessageFace *>(gObject + 8)->DoMessage(message);
        });
    }
}

namespace {
// The text a slot gets as a char pointer (slot 30 of the material object), or as a cRZString (TouchEntry's target).
uint32_t CharArg(Obj *, uint32_t *args)
{
    Ev(evText);
    Ev(TextHash(reinterpret_cast<const char *>(uintptr_t(args[0]))));
    return 1;
}
uint32_t StringArg(Obj *, uint32_t *args)
{
    const auto *s = reinterpret_cast<const cRZString *>(uintptr_t(args[0]));
    Ev(evText);
    Ev(*reinterpret_cast<const uint32_t *>(s));
    Ev(TextHash(s->mString.c_str()));
    return 1;
}
uint32_t gSourceAnswer;
uint32_t RefresherQuery(Obj *, uint32_t *args)
{
    // A query that succeeds gives an object (the original uses it untested); one that fails gives null or one.
    *reinterpret_cast<uint32_t *>(uintptr_t(args[1])) = gWorld.sourceQueriedNull && !gSourceAnswer ? 0 : P(&F(fQueried));
    return gSourceAnswer;
}
} // namespace

// 0x254dc0 SetLevelRange(first, last): every manager present or not, the scene manager's old level count below, at and
// above the new last level.
DIFF_TEST(sg_tssg_system_set_level_range, 0x254dc0)
{
    Fixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *, int32_t, int32_t)>(0x254dc0);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 gen(seed * 977u + 0x254dc0u);
        uint8_t start[kObjectSize];
        DrawObject(gen, start);
        auto *sys = reinterpret_cast<cTSSGSystem *>(start);
        struct Field {
            nTSSG::SGRef *ref;
            unsigned fake;
        } fields[] = {{&sys->mSceneManager, fShadow},  {&sys->mFloorManager, fAnim},        {&sys->mWallManager, fRef84},
                      {&sys->mFenceManager, fTerrain}, {&sys->mRoofManager, fLotSkirt},     {&sys->mRugManager, fRef90},
                      {&sys->mAwningManager, fOccupants}, {&sys->mCurvedPoolManager, fRefC0}, {&sys->mTerrainCutoutManager, fNewOccupants}};
        for (Field &f : fields) f.ref->mObject = gen() % 4 ? &F(f.fake) : nullptr;
        const int32_t first = int32_t(gen() % 6) - 2;
        const int32_t last = first + int32_t(gen() % 8) - 2;
        const int32_t had = int32_t(gen() % 9) - 2;
        auto prepare = [&] {
            *GameAddress<void *>(kPlacementCell) = &gPlacement;
            F(fShadow).Set(19, 0, 1u);
            F(fShadow).Set(20, 0, uint32_t(had));
            F(fShadow).Set(5, "dd", 1u);
            F(fShadow).Set(12, "d-", 1u);
            F(fAnim).Set(6, "dd", 1u);
            F(fRef84).Set(9, "dd", 1u);
            F(fTerrain).Set(12, "dd", 1u);
            F(fLotSkirt).Set(7, "dd", 1u);
            F(fRef90).Set(7, "dd", 1u);
            F(fOccupants).Set(7, "dd", 1u);
            F(fRefC0).Set(7, 0, 1u);
            F(fNewOccupants).Set(7, 0, 1u);
        };
        Compare(ctx, "set_level_range", seed, start, prepare, [&](bool useOriginal) -> uint32_t {
            if (useOriginal) original(gObject, first, last);
            else reinterpret_cast<cTSSGSystem *>(gObject)->SetLevelRange(first, last);
            return 0;
        });
    }
}

// 0x2556a0 ReloadMaterials: the ten names to the material object (its cell set, or filled through the service), then
// RefreshMaterials (stood in on both sides; its own test is below).
DIFF_TEST(sg_tssg_system_reload_materials, 0x2556a0)
{
    Fixture fixture;
    shape::StandIn refresh(0x24b160, &cTSSGSystem::RefreshMaterials, &StandRefresh);
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x2556a0);
    for (uint32_t seed = 0; seed < 20; ++seed) {
        std::mt19937 gen(seed * 31u + 0x2556a0u);
        uint8_t start[kObjectSize];
        DrawObject(gen, start);
        const bool cached = seed % 2 == 0;
        auto prepare = [&] {
            *GameAddress<void *>(kQueriedCell) = cached ? &F(fMaterials) : nullptr;
            F(fMaterials).Set(30, "p", 0u, &CharArg);
        };
        Compare(ctx, "reload_materials", seed, start, prepare, [&](bool useOriginal) -> uint32_t {
            if (useOriginal) original(gObject);
            else reinterpret_cast<cTSSGSystem *>(gObject)->ReloadMaterials();
            return 0;
        });
    }
}

// 0x24b160 RefreshMaterials: the detail object cached or through the service (null or not), its slot 42 object null or
// not, the query's result and output drawn per seed.
DIFF_TEST(sg_tssg_system_refresh_materials, 0x24b160)
{
    Fixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x24b160);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 gen(seed * 53u + 0x24b160u);
        uint8_t start[kObjectSize];
        DrawObject(gen, start);
        gWorld.detailNull = gen() % 4 == 0;
        gWorld.sourceQueriedNull = gen() % 3 == 0;
        gSourceAnswer = gen() % 3 ? 1 : 0;
        const bool cached = gen() % 2 == 0;
        const bool sourceNull = gen() % 4 == 0;
        auto prepare = [&] {
            *GameAddress<void *>(kDetailCell) = cached && !gWorld.detailNull ? &F(fDetail) : nullptr;
            F(fDetail).Set(42, 0, sourceNull ? nullptr : &F(fSource));
            F(fSource).Set(0, "dp", 0u, &RefresherQuery);
        };
        Compare(ctx, "refresh_materials", seed, start, prepare, [&](bool useOriginal) -> uint32_t {
            if (useOriginal) original(gObject);
            else reinterpret_cast<cTSSGSystem *>(gObject)->RefreshMaterials();
            return 0;
        });
    }
}

// 0x247fd0 TouchEntry(id): ids with the upper half set or not (the original sign-extends the low 16 bits), the entry and
// its owner null or not.
DIFF_TEST(sg_tssg_system_touch_entry, 0x247fd0)
{
    Fixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x247fd0);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 gen(seed * 59u + 0x247fd0u);
        uint8_t start[kObjectSize];
        DrawObject(gen, start);
        const uint32_t id = seed % 3 == 0 ? gen() : gen() % 3 == 0 ? 0x8000u + gen() % 0x8000 : gen() % 0x8000;
        const bool entryNull = gen() % 4 == 0;
        const bool ownerNull = gen() % 4 == 0;
        auto prepare = [&] {
            // App (fWorld) slot 10: the registry (fLot); its slot 16: the entry (fSource); its slot 10: the owner
            // (fDetail); its slot 67: the target (fQueried), whose slot 12 gets the string.
            *GameAddress<void *>(kAppCell) = &F(fWorld);
            F(fWorld).Set(10, 0, &F(fLot));
            F(fLot).Set(16, "dd", entryNull ? nullptr : &F(fSource));
            F(fSource).Set(10, 0, ownerNull ? nullptr : &F(fDetail));
            F(fDetail).Set(67, 0, &F(fQueried));
            F(fQueried).Set(12, "p", 0u, &StringArg);
        };
        Compare(ctx, "touch_entry", seed, start, prepare, [&](bool useOriginal) -> uint32_t {
            if (useOriginal) original(gObject, id);
            else reinterpret_cast<cTSSGSystem *>(gObject)->TouchEntry(id);
            return 0;
        });
    }
}
