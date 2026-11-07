// Differential tests for nTSSG::cTSSGSystem's after-load slots (src/sg/cTSSGSystemLoad.cpp): PostLoadLot (slot 20) and
// NhoodEntered (slot 91). Hook shape: the original runs on the same object image as ours.
//
// The world: the object starts as random bytes; its manager references hold recording fakes (svcfx::Obj for slots under
// 80, sgwide::Wide for the placement service's slots 85 / 86 and the camera service's slot 100, and for float getters),
// or null where the code tests for null. Cached cells: the placement service 0x10cc298, the cheat holder 0x10faf68, the
// camera service 0x10cc45c (or empty: then filled through GetServiceObject's slot 5), GetCachedSlot23's 0x10cc2a8, the
// app 0x10cc288. The car manager getter 0xb39000 and the callees nobody has replaced (0x254eb0, 0x252490, 0x248cd0,
// 0x254bd0, 0xae39c0, CheatParserExecuteCommand 0x24a1e0) are stood in over the image, so both sides reach the same
// logging stand-ins. The settings service's property set answers slot 9 by the key's text.
//
// Compared after each run: the event log, the object's 0x3b8 bytes (normalised), every fake's reference count and
// value words, the cells.
#include "code_patch.h"
#include "difftest.h"
#include "sg_tssg_wide.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/cIGZPropertySet.h"
#include "sg/cTSSGSystem.h"

#include <cstdint>
#include <cstring>
#include <random>

namespace {

using namespace svcfx;
using sgwide::W;
using nTSSG::cTSSGSystem;

constexpr uint32_t kObjectSize = 0x3b8;
constexpr uint32_t kObjectTag = 0x5e000000;
enum : uint32_t { evStand = 0x81 };

// svcfx fakes.
enum : unsigned {
    fFramework = 0,
    fService,
    fSettings,
    fTerrain,
    fLightMap,
    fFloor,
    fRug,
    fAwning,
    fCurved,
    fCutout,
    fWall,
    fFence,
    fRef90,
    fSkirt,
    fOccupants,
    fRefC0,
    fRoof,
    fPool,
    fWorld,
    fLighting,
    fSets,
    fScene,


};
// Wide fakes.
enum : unsigned {
    wPlacement = 0,
    wCamera,      // the camera service (slot 100)
    wView,        // its slot 100 object
    wApp,         // GetCachedSingleton
    wAppObject,   // the app's slot 33 / slot 6 object
    wCheatHolder,
    wCheats,
    wRefB4,
    wRef110,      // the effect objects (+0x110 / +0x114 / +0x118)
    wRef114,
    wRef118,
    wRef84,
    wRef94,
    wTerrainFace, // what the terrain / mRef84 give for 0x2c2a7106
    wNhood,       // GetCachedSlot23's slot 6
    wNhoodName,   // its slot 4 object (slot 5: the name)
    wOverlay5,
    wOverlay7,
    wSceneObject, // the scene manager's slot 18 object
    wNew90,       // the objects the framework makes
    wNewOcc,
    wNew84,
    wNew94,
    wItems,       // mRefAC
    wCars,
    wCarsObject,
};

constexpr uint32_t kPlacementCell = 0x10cc298;
constexpr uint32_t kCheatCell = 0x10faf68;
constexpr uint32_t kCameraCell = 0x10cc45c;
constexpr uint32_t kSlot23Cell = 0x10cc2a8;
constexpr uint32_t kCells[] = {kPlacementCell, kCheatCell, kCameraCell, kSlot23Cell, kAppCell};
constexpr char kCellKinds[] = "ppppp"; // the cells' words as the outcome logs them (svcfx::DumpCells)


// ---- the seed ----
struct Draw {
    uint32_t lotId;
    bool present[8];      // terrain, floor, rug, awning, curved, cutout, skirt (null only without a lot), mRef90
    bool occupantsThere;  // mNHoodOccupants
    bool firstLoadDone;
    uint32_t imposters, occupants; // the settings' bytes
    uint32_t cas;          // the world's slot 19 (al)
    bool sceneObject;
    bool cars;
    uint32_t effect, effectGiven; // the car manager's slots 12 / 14
    bool ref110, ref114, ref118;
    bool terrainGives;
    uint32_t cheat;        // the cheats' slot 48
    bool cameraCached, serviceGives, view, appObject;
    bool slot23Cached;
    // NhoodEntered
    uint32_t night;        // the neighbourhood's slot 20
    uint32_t kind;         // its slot 18
    bool overlay5, overlay7, ref84, ref94;
    const char *nhoodName;
    uint32_t flag;
};
Draw gD;

uint32_t FrameworkMake(Obj *, uint32_t *args) // framework slot 3 (class, iid, &out)
{
    uint32_t object = 0;
    switch (args[0]) {
    case 0x8c360cf7: object = P(&W(wNew90)); break;
    case 0x6bd49281: object = P(&W(wNewOcc)); break;
    case 0x0bcc8149: object = P(&W(wNew84)); break;
    case 0x6c9d07cd: object = P(&W(wNew94)); break;
    }
    sgwide::Out(args, 2, object);
    return object != 0;
}
uint32_t ServiceQuery(Obj *, uint32_t *args) // GetServiceObject slot 5 (class, iid, &out)
{
    uint32_t object = 0;
    if (args[0] == 0x0ac1887e) object = P(&F(fSettings));
    else if (gD.serviceGives) object = P(&W(wCamera));
    sgwide::Out(args, 2, object);
    return object != 0;
}
uint32_t SettingsGet(Obj *, uint32_t *args) // slot 9 (key, any)
{
    const auto *key = reinterpret_cast<const GZServiceString *>(uintptr_t(args[0]));
    for (char c : *key) Ev(uint8_t(c));
    auto *any = reinterpret_cast<uint8_t *>(uintptr_t(args[1]));
    Ev(Norm(*reinterpret_cast<const uint32_t *>(any)));
    Ev(any[4]);
    const bool imposters = key->compare(GameAddress<const char>(0xdae4f4)) == 0; // the lot impostor setting
    any[4] = uint8_t(imposters ? gD.imposters : gD.occupants);
    return 1;
}
uint32_t TerrainQuery(Obj *, uint32_t *args) // slot 0 (iid, &out)
{
    sgwide::Out(args, 1, gD.terrainGives ? P(&W(wTerrainFace)) : 0u);
    return gD.terrainGives;
}
uint32_t TerrainQueryW(sgwide::Wide *, uint32_t *args) { return TerrainQuery(nullptr, args); }
uint32_t EffectName(sgwide::Wide *, uint32_t *args) // car manager slot 12 (name)
{
    for (const char *c = reinterpret_cast<const char *>(uintptr_t(args[0])); *c; ++c) Ev(uint8_t(*c));
    return gD.effect;
}
uint32_t EffectGive(sgwide::Wide *, uint32_t *args) // car manager slot 14 (name, &out)
{
    for (const char *c = reinterpret_cast<const char *>(uintptr_t(args[0])); *c; ++c) Ev(uint8_t(*c));
    const char *name = reinterpret_cast<const char *>(uintptr_t(args[0]));
    uint32_t object = P(&W(wRef110));
    // The day/night effects, and a neighbourhood's own (its name and the suffix).
    if (std::strcmp(name, GameAddress<const char>(0xdaeecc)) == 0) object = P(&W(wRef114));
    else if (std::strstr(name, GameAddress<const char>(0xdaeee4))) object = P(&W(wRef118));
    if (gD.effectGiven) sgwide::Out(args, 1, object);
    return gD.effectGiven;
}
uint32_t CheatName(sgwide::Wide *, uint32_t *args) // slot 48 (name)
{
    for (const char *c = reinterpret_cast<const char *>(uintptr_t(args[0])); *c; ++c) Ev(uint8_t(*c));
    return gD.cheat;
}

// ---- stand-ins ----
void *gCarsResult;
void *__cdecl StandCars()
{
    Ev(evStand), Ev(0xb39000);
    return gCarsResult;
}
void __fastcall StandLevels(void *self, void *, uint32_t a, uint32_t b) { Ev(evStand), Ev(0x254eb0), Ev(Norm(P(self))), Ev(a), Ev(b); }
void __fastcall StandFirstLoad(void *self, void *) { Ev(evStand), Ev(0x252490), Ev(Norm(P(self))); }
void __fastcall StandOverlays(void *self, void *, uint32_t a) { Ev(evStand), Ev(0x248cd0), Ev(Norm(P(self))), Ev(a); }
void __cdecl StandLotSetting(uint32_t id, uint32_t value) { Ev(evStand), Ev(0x254bd0), Ev(id), Ev(value); } // the setting's value: data
void __cdecl StandSceneObject(void *object, void *items) { Ev(evStand), Ev(0xae39c0), Ev(Norm(P(object))), Ev(Norm(P(items))); }
void __cdecl StandCheat(void *parser, const char *format, uint32_t a)
{
    Ev(evStand), Ev(0x24a1e0), Ev(Norm(P(parser)));
    for (const char *c = format; *c; ++c) Ev(uint8_t(*c));
    if (std::strchr(format, '%')) Ev(a);
}

struct Fixture {
    AllocatorScope allocator;
    Globals globals;
    shape::CodePatch cars{GameAddress<void>(0xb39000), shape::CodeOf(&StandCars)};
    shape::CodePatch levels{GameAddress<void>(0x254eb0), shape::CodeOf(&StandLevels)};
    shape::CodePatch firstLoad{GameAddress<void>(0x252490), shape::CodeOf(&StandFirstLoad)};
    shape::CodePatch overlays{GameAddress<void>(0x248cd0), shape::CodeOf(&StandOverlays)};
    shape::CodePatch lotSetting{GameAddress<void>(0x254bd0), shape::CodeOf(&StandLotSetting)};
    shape::CodePatch sceneObject{GameAddress<void>(0xae39c0), shape::CodeOf(&StandSceneObject)};
    shape::CodePatch cheat{GameAddress<void>(0x24a1e0), shape::CodeOf(&StandCheat)};
    Fixture()
    {
        globals.Set(0x10c9378, &F(fService));
        for (uint32_t cell : kCells) globals.SetWord(cell, 0);
        gFrameworkIndex = fFramework;
    }
};

void Wire()
{
    F(fFramework).Set(3, "ddp", 0u, &FrameworkMake);
    F(fService).Set(5, "ddp", 0u, &ServiceQuery);
    F(fSettings).Set(9, "pp", 0u, &SettingsGet);
    F(fTerrain).Set(8, 0);
    F(fTerrain).Set(0, "dp", 0u, &TerrainQuery);
    F(fLightMap).Set(5, 0);
    F(fFloor).Set(11, 0);
    F(fRug).Set(8, 0);
    F(fAwning).Set(8, 0);
    F(fAwning).Set(10, 0);
    F(fCurved).Set(7, 0);
    F(fCurved).Set(8, 0);
    F(fCutout).Set(7, 0);
    F(fWall).Set(10, 0);
    F(fFence).Set(14, 0);
    F(fSkirt).Set(16, 0);
    F(fSkirt).Set(5, 0);
    F(fRefC0).Set(6, 0);
    F(fRoof).Set(6, 0);
    F(fPool).Set(7, 0);
    F(fWorld).Set(19, 0, gD.cas);
    F(fWorld).Set(6, 0, &W(wNhood));
    F(fLighting).Set(18, "d");
    F(fLighting).Set(19, "p");
    F(fLighting).Set(6, "p");
    F(fLighting).Set(8, "p");
    F(fLighting).Set(25, 0, &F(fSets));
    F(fSets).Set(9, "d");
    F(fScene).Set(17, 0, 0x5c17u);
    F(fScene).Set(18, 0, gD.sceneObject ? P(&W(wSceneObject)) : 0u);
    F(fScene).Set(5, 2);
    W(wCars).Set(17, "d");
    W(wCars).Set(39, "d");
    W(wCars).Set(40, "p");
    W(wCars).Set(18, "d");
    W(wCars).Set(25, "dd");
    W(wCars).Set(24, 0, &W(wCarsObject));
    W(wCars).Set(12, 1, 0u, &EffectName);
    W(wCars).Set(14, "-p", 0u, &EffectGive);
    W(wCars).text[12] = W(wCars).text[14] = 1;
    W(wCarsObject).Set(6, "-p");
    for (unsigned f : {fRef90, fOccupants}) {
        F(f).Set(3, 0);
        F(f).Set(5, "d");
        F(f).Set(6, "d");
        F(f).Set(7, "d");
    }
    F(fRef90).Set(5, "dd");

    W(wPlacement).Set(85, 0, 0x85u);
    W(wPlacement).Set(86, 0, 0x86u);
    W(wCamera).Set(100, 0, gD.view ? P(&W(wView)) : 0u);
    W(wView).Set(5, 0);
    W(wApp).Set(33, 0, gD.appObject ? P(&W(wAppObject)) : 0u);
    W(wApp).Set(23, 0, &F(fWorld));
    W(wApp).Set(6, 0, &W(wAppObject));
    W(wApp).Set(11, 0);
    W(wAppObject).Set(25, 0, 0x2525u);
    W(wAppObject).Set(27, 0, 0x2727u);
    W(wAppObject).SetFloat(50, 0, 12.5f);
    W(wAppObject).Set(52, 0, 0x5252u);
    W(wAppObject).Set(53, 0, 0x5353u);
    W(wCheatHolder).Set(3, 0, &W(wCheats));
    W(wCheats).Set(48, "p", 0u, &CheatName);
    W(wRefB4).Set(24, "p");
    W(wRefB4).Set(20, "p");
    for (unsigned w : {wRef110, wRef114, wRef118}) W(w).Set(3, 1);
    for (unsigned w : {wNew90, wNewOcc, wNew84, wNew94, wRef84, wRef94}) {
        W(w).Set(3, 0);
        W(w).Set(5, 0);
        W(w).Set(6, 1);
        W(w).Set(7, "d");
    }
    W(wNew90).Set(5, "dd");
    W(wNewOcc).Set(5, "d");
    W(wNew90).Set(6, "d");
    W(wRef84).Set(0, "dp", 0u, &TerrainQueryW);
    W(wNew84).Set(0, "dp", 0u, &TerrainQueryW);
    W(wNhood).Set(20, 0, gD.night);
    W(wNhood).Set(18, 0, gD.kind);
    W(wNhood).Set(4, 0, &W(wNhoodName));
    W(wNhoodName).Set(5, 0, P(gD.nhoodName));
    for (unsigned w : {wOverlay5, wOverlay7}) W(w).Set(13, "ddd");
}

alignas(16) uint8_t gObject[kObjectSize];
// gObject's words as the outcome logs them (svcfx::DumpWords): 'p' pointer, 'd' data, 'x' not compared.
constexpr char kObjectKinds[] =
    "dddddddd dddddddd dddddddd ddpppppp pppdppdd ppdpdppd pdddddpd pdpppdpd "
    "ddddpppd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddd";

void Start(std::mt19937 &gen, uint8_t *start, bool nhood)
{
    for (uint32_t i = 0; i < kObjectSize; ++i) start[i] = uint8_t(gen());
    auto put = [&](uint32_t offset, const void *p) { std::memcpy(start + offset, &p, 4); };
    const uint32_t offsets[] = {0x80, 0x7c, 0xe8, 0xf0, 0xec, 0xf8, 0x78, 0x90};
    const unsigned fakes[] = {fTerrain, fFloor, fRug, fAwning, fCurved, fCutout, fSkirt, fRef90};
    for (unsigned i = 0; i < 8; ++i) put(offsets[i], gD.present[i] ? &F(fakes[i]) : nullptr);
    put(0x88, gD.occupantsThere ? &F(fOccupants) : nullptr);
    put(0xa0, &F(fLightMap));
    put(0x6c, &F(fWall));
    put(0x70, &F(fFence));
    put(0xc0, &F(fRefC0));
    put(0xb8, &F(fRoof));
    put(0x74, &F(fPool));
    put(0xa4, &F(fLighting));
    put(0x68, &F(fScene));
    put(0xb4, &W(wRefB4));
    put(0xac, &W(wItems));
    put(0x110, gD.ref110 ? &W(wRef110) : nullptr);
    put(0x114, gD.ref114 ? &W(wRef114) : nullptr);
    put(0x118, gD.ref118 ? &W(wRef118) : nullptr);
    put(0x84, gD.ref84 ? &W(wRef84) : nullptr);
    put(0x94, gD.ref94 ? &W(wRef94) : nullptr);
    put(0xd8, gD.overlay5 ? &W(wOverlay5) : nullptr);
    put(0xe0, gD.overlay7 ? &W(wOverlay7) : nullptr);
    start[0x168] = uint8_t(gD.firstLoadDone ? 1 + gen() % 255 : 0);
    (void)nhood;
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

template <class Run> void Compare(difftest::Context &ctx, const char *what, uint32_t seed, const uint8_t *start, Run run)
{
    Blob outcome[2];
    for (int side = 0; side < 2; ++side) {
        ResetAll();
        sgwide::ResetWide();
        Wire();
        gCarsResult = gD.cars ? &W(wCars) : nullptr;
        *GameAddress<void *>(kPlacementCell) = &W(wPlacement);
        *GameAddress<void *>(kCheatCell) = &W(wCheatHolder);
        *GameAddress<void *>(kCameraCell) = gD.cameraCached ? &W(wCamera) : nullptr;
        *GameAddress<void *>(kSlot23Cell) = gD.slot23Cached ? &F(fWorld) : nullptr;
        *GameAddress<void *>(kAppCell) = &W(wApp);
        std::memcpy(gObject, start, kObjectSize);
        AddRange(gObject, kObjectSize, kObjectTag);
        ctx.Mark(side ? "ours" : "original");
        run(side == 0);
        outcome[side] = Outcome();
    }
    Same(ctx, outcome[0], outcome[1], what, seed);
}

const char *const kNames[] = {"", "x", "Nhood_A", "N001"};

void DrawCase(std::mt19937 &gen)
{
    gD = Draw{};
    gD.lotId = gen() % 3 ? gen() : 0;
    for (bool &p : gD.present) p = gen() % 4 != 0;
    if (gD.lotId) gD.present[6] = true; // the lot path calls the skirt's slot 5 unconditionally
    gD.present[3] = gD.present[4] = true; // the awning's slot 10 and the curved pool's slot 8 are unconditional too
    gD.occupantsThere = gen() % 2;
    gD.firstLoadDone = gen() % 2;
    const uint32_t bytes[] = {0, 1, 0x80, 0x100};
    gD.imposters = bytes[gen() % 3];
    gD.occupants = bytes[gen() % 3];
    gD.cas = bytes[gen() % 4];
    gD.sceneObject = gen() % 2;
    gD.cars = gen() % 4 != 0;
    if (gD.cars) gD.present[0] = true; // the terrain is asked for its face unconditionally then
    gD.effect = bytes[gen() % 4];
    gD.effectGiven = bytes[gen() % 4];
    gD.ref110 = gen() % 2, gD.ref114 = gen() % 2, gD.ref118 = gen() % 2;
    gD.terrainGives = gen() % 2;
    gD.cheat = bytes[gen() % 4];
    gD.cameraCached = gen() % 2, gD.serviceGives = gen() % 2, gD.view = gen() % 2, gD.appObject = gen() % 2;
    if (!gD.cameraCached && !gD.serviceGives && !(gD.cas & 0xff)) gD.serviceGives = true; // a null camera is not survivable
    gD.slot23Cached = gen() % 2;
    gD.night = gen() % 3 ? 0 : gen() % 2 ? 1 : 0x10000;
    gD.kind = gen() % 9;
    gD.overlay5 = gen() % 3 != 0, gD.overlay7 = gen() % 3 != 0;
    gD.ref84 = gen() % 2, gD.ref94 = gen() % 2;
    gD.nhoodName = kNames[gen() % 4];
    gD.flag = gen() % 2 ? gen() : 0;
}

cTSSGSystem *Ours() { return reinterpret_cast<cTSSGSystem *>(gObject); }

} // namespace

// 0x251c00 PostLoadLot: a lot id or 0; each lot manager there or not; the first load done or not; both settings (0, 1,
// 0x80); the world CAS or not (al only); the scene object, the car manager, its effect (12 / 14 answers), the effect
// references there or not; the cheat hook; the camera service cached, made or (in CAS) absent, its view; the app object.
DIFF_TEST(sg_tssg_system_post_load_lot, 0x251c00)
{
    Fixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x251c00);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x251c00u);
        DrawCase(gen);
        uint8_t start[kObjectSize];
        Start(gen, start, false);
        Compare(ctx, "post_load_lot", seed, start, [&](bool useOriginal) {
            if (useOriginal) original(gObject, gD.lotId);
            else Ours()->PostLoadLot(gD.lotId);
        });
    }
}

// 0x24c030 NhoodEntered: the car manager there or not (the camera's float, overlays 5 / 7), mRef84 / mNHoodOccupants /
// mRef90 / mRef94 there or made, night or day, the effects, the neighbourhood's name and kind (every road path), the
// cheat hook, the name and flag handed on.
DIFF_TEST(sg_tssg_system_nhood_entered, 0x24c030)
{
    Fixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *, void *, uint32_t)>(0x24c030);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 gen(seed * 104729u + 0x24c030u);
        DrawCase(gen);
        uint8_t start[kObjectSize];
        Start(gen, start, true);
        void *name = reinterpret_cast<void *>(uintptr_t(0x3a3e0000u + seed));
        Compare(ctx, "nhood_entered", seed, start, [&](bool useOriginal) {
            if (useOriginal) original(gObject, name, gD.flag);
            else Ours()->NhoodEntered(name, gD.flag);
        });
    }
}
