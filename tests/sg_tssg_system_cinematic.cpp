// Differential test for nTSSG::cTSSGSystem::EndCinematic (src/sg/cTSSGSystemCinematic.cpp).
// Hook shape: the original runs on the same object image as ours.
//
// The world (tests/ts_services_fixture.h): the object starts as random bytes. Objects the code calls directly and also
// releases through their virtual base (the controller mRef124, mRefB0, the actors) are "duals": a logging vtable of their
// own and a virtual-base table whose entries reach recording fakes. References only released (the cinematic, +0x128, the
// third vector's elements) are blocks whose virtual-base table reaches fakes. The three vectors point into test arrays
// (clearing never frees). The getters' cells (sound probe 0x10f51b4, GetCachedSingletonB 0x10cc28c) hold fakes or are
// empty and filled through the service object (slot 5); the app (0x10cc288) and the director's framework are fakes.
// cRZStopwatch Stop and Elapsed (ours) are stood in on both sides; 0xaeb0e0 (not replaced) over the image.
//
// Compared: the event log, the object's 0x3b8 bytes (normalised), every fake's reference count, the cells.
#include "code_patch.h"
#include "difftest.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "rz/StopwatchMore.h"
#include "rz/cRZStopwatch.h"
#include "sg/cTSSGSystem.h"

#include <cstdint>
#include <cstring>
#include <random>

namespace {

using namespace svcfx;
using nTSSG::cTSSGSystem;

constexpr uint32_t kObjectSize = 0x3b8;
constexpr uint32_t kObjectTag = 0x5e000000;
constexpr uint32_t kBlockTag = 0x6d000000;
constexpr uint32_t kDualTag = 0x6b000000;
constexpr uint32_t kArrayTag = 0x6a000000;
enum : uint32_t { evDual = 0x82, evStand };

enum : unsigned {
    fFramework = 0,
    fService,
    fApp,
    fAppObject,
    fProperties,
    fProbe,
    fSettings,
    fMessage,
    fCamera,
    fView,
    fWall,
    fSlot120,
    fRelease0, // the duals' and blocks' virtual bases: fRelease0 + index
};
constexpr unsigned kReleaseFakes = 24 - fRelease0;

constexpr uint32_t kProbeCell = 0x10f51b4;
constexpr uint32_t kSettingsCell = 0x10cc28c;
constexpr uint32_t kCells[] = {kProbeCell, kSettingsCell, kAppCell};
constexpr char kCellKinds[] = "ppp"; // the cells' words as the outcome logs them (svcfx::DumpCells)


// ---- duals ----
struct Dual {
    void **vt;
    int32_t *vbt;
    int32_t table[3];
    uint32_t id;
    uint32_t ret[128];
};
template <int S> uint32_t __fastcall Dual0(Dual *self, void *)
{
    Ev(evDual), Ev(self->id), Ev(S);
    return self->ret[S];
}
// Dual1<S>'s argument by slot S (svcfx::Field, word S of the string).
constexpr char kDualArgKinds[] = "-dd";
template <int S> uint32_t __fastcall Dual1(Dual *self, void *, uint32_t a)
{
    Ev(evDual), Ev(self->id), Ev(S), svcfx::Field(svcfx::gEv, a, kDualArgKinds, S * 4);
    return self->ret[S];
}
void *gDualVT[128];
// 0: controller, 1: mRefB0, 2..4: actors, 5..7: extras.
Dual gDuals[8];
void PointDual(Dual &d, unsigned index, Obj *entry1, Obj *entry2)
{
    d.vt = gDualVT;
    d.vbt = d.table;
    d.table[0] = 0;
    d.table[1] = int32_t(P(entry1) - P(&d.vbt));
    d.table[2] = int32_t(P(entry2) - P(&d.vbt));
    d.id = 400 + index;
}

struct VBlock {
    uint32_t words[2];
    int32_t table[3];
    void Point(Obj *entry1)
    {
        words[0] = 0x0b1ec700;
        words[1] = P(table);
        table[0] = 0;
        table[1] = int32_t(P(entry1) - P(&words[1]));
        table[2] = 0;
    }
};
VBlock gBlocks[5]; // 0: cinematic, 1: +0x128, 2..4: the third vector's references

alignas(16) nTSSG::cTSSGCinematicActor gActors[3];
alignas(16) nTSSG::cTSSGCinematicActor gExtras[3];
alignas(16) uint32_t gRefs[3];

alignas(16) uint8_t gObject[kObjectSize];
// gObject's words as the outcome logs them (svcfx::DumpWords): 'p' pointer, 'd' data, 'x' not compared.
constexpr char kObjectKinds[] =
    "dddddddd dddddddd dddddddd dddpdddd dddddddd ddddpddd dddddddd dddddddd "
    "dddddddd pppppppp ppppdddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddd";

// ---- stand-ins ----
void __fastcall StandStop(cRZStopwatch *watch, void *) { Ev(evStand), Ev(0x1c9842), Ev(Norm(P(watch))); }
uint32_t __fastcall StandElapsed(const cRZStopwatch *watch, void *)
{
    Ev(evStand), Ev(0x1c9256), Ev(Norm(P(watch)));
    return 77;
}
void __cdecl StandCleanUp(void *object, uint32_t a, uint32_t b, const char *name, uint32_t flags)
{
    Ev(evStand), Ev(0xaeb0e0), Ev(Norm(P(object))), Ev(a), Ev(b), Ev(flags);
    for (const char *c = name; *c; ++c) Ev(uint8_t(*c));
}

// ---- the seed ----
struct Draw {
    bool cinematic, controller, camera, held, abort, probeCached, settingsCached, serviceGives, messageGiven;
    uint32_t finished, properties, slot120;
    unsigned actors, extras, refs;
    bool actorPresent[3], restore[3];
};
Draw gD;

uint32_t ServiceQuery(Obj *, uint32_t *args) // service slot 5 (id, iid, &out)
{
    uint32_t object = 0;
    if (gD.serviceGives) object = args[0] == 0xe92db716 ? P(&F(fSettings)) : P(&F(fProbe));
    *reinterpret_cast<uint32_t *>(uintptr_t(args[2])) = object;
    return object ? 1 : 0;
}
uint32_t FrameworkMessage(Obj *, uint32_t *args) // framework slot 3 (class, iid, &out)
{
    if (gD.messageGiven) *reinterpret_cast<uint32_t *>(uintptr_t(args[2])) = P(&F(fMessage));
    return gD.messageGiven;
}

struct Fixture {
    AllocatorScope allocator;
    Globals globals;
    shape::StandIn stop{0x1c9842, &stopwatch_more::Stop, &StandStop};
    shape::StandIn elapsed{0x1c9256, &cRZStopwatch::Elapsed, &StandElapsed};
    shape::CodePatch cleanUp{GameAddress<void>(0xaeb0e0), shape::CodeOf(&StandCleanUp)};
    Fixture()
    {
        gDualVT[1] = reinterpret_cast<void *>(&Dual1<1>);
        gDualVT[2] = reinterpret_cast<void *>(&Dual1<2>);
        gDualVT[7] = reinterpret_cast<void *>(&Dual1<7>);
        gDualVT[33] = reinterpret_cast<void *>(&Dual0<33>);
        gDualVT[97] = reinterpret_cast<void *>(&Dual1<97>);
        gDualVT[120] = reinterpret_cast<void *>(&Dual0<120>);
        globals.Set(0x10c9378, &F(fService));
        for (uint32_t cell : kCells) globals.SetWord(cell, 0);
        gFrameworkIndex = fFramework;
    }
};

void Wire()
{
    F(fService).Set(5, "ddp", 0u, &ServiceQuery);
    F(fFramework).Set(3, "ddp", 0u, &FrameworkMessage);
    F(fMessage).Set(5, "d");
    F(fMessage).Set(12, "d-");
    F(fProbe).Set(12, "p");
    F(fSettings).Set(27, "d");
    F(fSettings).Set(29, "d");
    F(fSettings).Set(25, "dd");
    F(fApp).Set(21, 0, gD.properties ? P(&F(fProperties)) : 0u);
    F(fApp).Set(26, 0, &F(fAppObject));
    F(fAppObject).Set(18, "d");
    F(fProperties).Set(13, "pd");
    F(fCamera).Set(60, 0, gD.finished);
    F(fCamera).Set(7, "d");
    F(fCamera).Set(6, 0);
    F(fView).Set(11, "d-", 0x7e7e);
    F(fWall).Set(21, 1);
    F(fWall).Set(16, 0);
    F(fSlot120).Set(47, "d", 0xabcd1234u);

    for (unsigned i = 0; i < 8; ++i) PointDual(gDuals[i], i, &F(fRelease0 + i % kReleaseFakes), &F(fView));
    std::memset(gDuals[0].ret, 0, sizeof(gDuals[0].ret));
    gDuals[0].ret[33] = gD.camera ? P(&F(fCamera)) : 0;
    for (unsigned i = 2; i < 8; ++i) gDuals[i].ret[120] = gD.slot120 ? P(&F(fSlot120)) : 0;
    for (unsigned i = 0; i < 5; ++i) gBlocks[i].Point(&F(fRelease0 + (8 + i) % kReleaseFakes));
}

void Start(std::mt19937 &gen, uint8_t *start)
{
    for (uint32_t i = 0; i < kObjectSize; ++i) start[i] = uint8_t(gen());
    auto *sys = reinterpret_cast<cTSSGSystem *>(start);
    auto put = [&](uint32_t offset, const void *p) { std::memcpy(start + offset, &p, 4); };
    put(0x120, gD.cinematic ? static_cast<void *>(&gBlocks[0]) : nullptr);
    put(0x124, gD.controller ? static_cast<void *>(&gDuals[0]) : nullptr);
    put(0x128, gen() % 4 ? static_cast<void *>(&gBlocks[1]) : nullptr);
    put(0xb0, &gDuals[1]);
    put(0x6c, gen() % 4 ? static_cast<void *>(&F(fWall)) : nullptr);
    start[0x160] = uint8_t(gen() % 2 ? 0 : 1 + gen() % 255);
    start[0x162] = uint8_t(gD.held ? 1 + gen() % 255 : 0);
    // The vectors: first, last, end.
    auto vector = [&](uint32_t offset, const void *first, size_t bytes) {
        put(offset, first);
        put(offset + 4, static_cast<const uint8_t *>(first) + bytes);
        put(offset + 8, static_cast<const uint8_t *>(first) + bytes);
    };
    vector(0x12c, gActors, gD.actors * sizeof(gActors[0]));
    vector(0x138, gExtras, gD.extras * sizeof(gExtras[0]));
    vector(0x144, gRefs, gD.refs * sizeof(gRefs[0]));
    (void)sys;
}

void FillArrays()
{
    for (unsigned i = 0; i < 3; ++i) {
        gActors[i].mObject.mObject = gD.actorPresent[i] ? &gDuals[2 + i] : nullptr;
        gActors[i].mValue = 0x1000 + i;
        gActors[i].mRestore = uint8_t(gD.restore[i] ? 1 + i : 0);
        gExtras[i].mObject.mObject = &gDuals[5 + i];
        gExtras[i].mValue = 0x2000 + i;
        gExtras[i].mRestore = 1;
        gRefs[i] = i % 2 ? P(&gBlocks[2 + i]) : 0;
    }
}

Blob Outcome()
{
    Blob b = gEv;
    b.push_back(0xeeee);
    svcfx::DumpWords(b, gObject, sizeof(gObject), kObjectKinds);
    CaptureFakes(b);
    svcfx::DumpCells(b, kCells, kCellKinds);
    return b;
}

cTSSGSystem *Ours() { return reinterpret_cast<cTSSGSystem *>(gObject); }

} // namespace

// 0x255250 EndCinematic: no cinematic; a controller or not, its camera gone, finished or not, held or not, abort or not;
// the probe and settings cached, got through the service or absent; the message given or not; 0..3 actors (present
// or null, restore flag, slot 120 object or not), 0..3 extras, 0..3 references.
DIFF_TEST(sg_tssg_system_end_cinematic, 0x255250)
{
    Fixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *, uint8_t)>(0x255250);
    for (uint32_t seed = 0; seed < 800; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x255250u);
        gD = Draw{};
        gD.cinematic = gen() % 8 != 0, gD.controller = gen() % 4 != 0, gD.camera = gen() % 3 != 0;
        gD.held = gen() % 3 == 0, gD.abort = gen() % 3 == 0;
        gD.probeCached = gen() % 2, gD.settingsCached = gen() % 2, gD.serviceGives = gen() % 2;
        gD.messageGiven = gen() % 3 != 0;
        gD.finished = gen() % 2 ? gen() % 2 : 0x100;
        gD.properties = gen() % 3 != 0, gD.slot120 = gen() % 3 != 0;
        gD.actors = gen() % 4, gD.extras = gen() % 4, gD.refs = gen() % 4;
        for (unsigned i = 0; i < 3; ++i) gD.actorPresent[i] = gen() % 4 != 0, gD.restore[i] = gen() % 2;
        uint8_t start[kObjectSize];
        Start(gen, start);
        const uint8_t abortArg = uint8_t(gD.abort ? 1 + gen() % 255 : 0);
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            ResetAll();
            Wire();
            FillArrays();
            *GameAddress<void *>(kProbeCell) = gD.probeCached ? &F(fProbe) : nullptr;
            *GameAddress<void *>(kSettingsCell) = gD.settingsCached ? &F(fSettings) : nullptr;
            *GameAddress<void *>(kAppCell) = &F(fApp);
            std::memcpy(gObject, start, kObjectSize);
            AddRange(gObject, kObjectSize, kObjectTag);
            AddRange(gBlocks, sizeof(gBlocks), kBlockTag);
            AddRange(gDuals, sizeof(gDuals), kDualTag);
            AddRange(gActors, sizeof(gActors), kArrayTag);
            AddRange(gExtras, sizeof(gExtras), kArrayTag | 0x100);
            AddRange(gRefs, sizeof(gRefs), kArrayTag | 0x200);
            ctx.Mark(side ? "ours" : "original");
            if (side == 0) original(gObject, abortArg);
            else Ours()->EndCinematic(abortArg);
            outcome[side] = Outcome();
        }
        Same(ctx, outcome[0], outcome[1], "end_cinematic", seed);
    }
}
