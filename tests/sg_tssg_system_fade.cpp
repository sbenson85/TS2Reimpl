// Differential test for nTSSG::cTSSGSystem::UpdateCinematicFade (slot 99, src/sg/cTSSGSystemFrame.cpp). Hook shape: the
// original runs on the same object image as ours.
//
// The world: the object starts as random bytes; the scene manager (+0x68) is a wide fake whose slot 18 is the camera
// (slot 140 fills the transform, slot 139 gives the view whose +0x3c is the field of view) and slot 17 the scene; the
// cinematic actors vector (+0x12c) points into a test array of 12-byte entries (actors null or wide fakes: slot 120 their
// object, which answers slots 188 and 98; slot 26 their name). Cells: GetCachedSlot23's 0x10cc2a8, the app 0x10cc288.
// The actor bounds query 0xae7d00, the day/night test 0x7e2180 and the car manager getter 0xb39000 are stood in over the
// image; the box union 0x19d430, the dot product 0x18fbf0, 0xae7900 and the quaternion rotation 0x1a6c40 run as they are
// on the original side (ours calls our RotateByQuaternion).
//
// Compared after each run: the event log, the object's 0x3b8 bytes (normalised), every fake's reference count and
// value words, the cells.
#include "code_patch.h"
#include "difftest.h"
#include "sg_tssg_wide.h"
#include "stack_out.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "sg/cTSSGSystem.h"

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>

namespace {

using namespace svcfx;
using sgwide::W;
using nTSSG::cTSSGSystem;

constexpr uint32_t kObjectSize = 0x3b8;
constexpr uint32_t kObjectTag = 0x5e000000;
constexpr uint32_t kActorTag = 0x5d000000;
enum : uint32_t { evStand = 0x81 };
constexpr unsigned kActors = 5;

enum : unsigned { fWorld = 0 };
enum : unsigned {
    wSceneManager = 0,
    wCamera,
    wScene,
    wCars,
    wApp,
    wAppObject,
    wActor,                 // kActors actors
    wActorObject = wActor + kActors, // and their slot 120 objects
};

constexpr uint32_t kSlot23Cell = 0x10cc2a8;
constexpr uint32_t kCells[] = {kSlot23Cell, kAppCell};
constexpr char kCellKinds[] = "pp"; // the cells' words as the outcome logs them (svcfx::DumpCells)


struct ActorDraw {
    bool there, object;
    uint32_t live;   // the object's slot 188 (al)
    uint32_t kind;   // its slot 98
    float box[6];
    bool named;
};
struct Draw {
    bool lot;
    uint32_t night;
    bool appObject;
    float transform[7];
    float fov;
    unsigned count;
    ActorDraw actors[kActors];
};
Draw gD;

struct Entry {
    void *actor;
    uint32_t value;
    uint32_t restore;
};
Entry gEntries[kActors];
alignas(16) uint8_t gView[0x40];
const char kName[] = "actor"; // test text

uint32_t CameraTransform(sgwide::Wide *, uint32_t *args) // slot 140 (out, 0)
{
    auto *out = reinterpret_cast<float *>(uintptr_t(args[0]));
    std::memcpy(out, gD.transform, sizeof(gD.transform));
    difftest::FillOut(out, sizeof(gD.transform));
    return args[0];
}

unsigned ActorIndex(const void *actor)
{
    for (unsigned i = 0; i < kActors; ++i)
        if (actor == &W(wActor + i)) return i;
    return 0;
}

void __cdecl StandBounds(float *box, uint32_t flags, void *scene, void *actor)
{
    Ev(evStand), Ev(0xae7d00), Ev(Norm(P(box))), Ev(flags), Ev(Norm(P(scene))), Ev(Norm(P(actor)));
    for (int i = 0; i < 6; ++i) {
        uint32_t bits;
        std::memcpy(&bits, box + i, 4);
        Ev(bits);
    }
    std::memcpy(box, gD.actors[ActorIndex(actor)].box, 24);
    difftest::FillOut(box, 24);
}
uint32_t __cdecl StandIsNight(const float *position)
{
    Ev(evStand), Ev(0x7e2180), Ev(Norm(P(position)));
    return 0xabcd0000u | gD.night;
}
void *__cdecl StandCars()
{
    Ev(evStand), Ev(0xb39000);
    return &W(wCars);
}

struct Fixture {
    Globals globals;
    shape::CodePatch bounds{GameAddress<void>(0xae7d00), shape::CodeOf(&StandBounds)};
    shape::CodePatch night{GameAddress<void>(0x7e2180), shape::CodeOf(&StandIsNight)};
    shape::CodePatch cars{GameAddress<void>(0xb39000), shape::CodeOf(&StandCars)};
    Fixture()
    {
        for (uint32_t cell : kCells) globals.SetWord(cell, 0);
    }
};

void Wire()
{
    F(fWorld).Set(5, 0, gD.lot ? 0x0bea28c2u : 0x0bea28c3u);
    W(wSceneManager).Set(18, 0, &W(wCamera));
    W(wSceneManager).Set(17, 0, &W(wScene));
    W(wCamera).Set(140, "p-", 0u, &CameraTransform);
    W(wCamera).Set(139, 1, gView);
    W(wCars).Set(31, "d-");
    W(wCars).low[31] = 2; // the original pushes a stack word of which it wrote only the low byte
    W(wApp).Set(33, 0, gD.appObject ? P(&W(wAppObject)) : 0u);
    W(wAppObject).Set(40, "d");
    for (unsigned i = 0; i < kActors; ++i) {
        const ActorDraw &a = gD.actors[i];
        W(wActor + i).Set(120, 0, a.object ? P(&W(wActorObject + i)) : 0u);
        W(wActor + i).Set(26, 0, a.named ? P(kName) : 0u);
        W(wActorObject + i).Set(188, 0, a.live);
        W(wActorObject + i).Set(98, 0, a.kind);
    }
}

alignas(16) uint8_t gObject[kObjectSize];
// gObject's words as the outcome logs them (svcfx::DumpWords): 'p' pointer, 'd' data, 'x' not compared.
constexpr char kObjectKinds[] =
    "dddddddd dddddddd dddddddd ddpddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddpppdd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddd";

void Start(std::mt19937 &gen, uint8_t *start)
{
    for (uint32_t i = 0; i < kObjectSize; ++i) start[i] = uint8_t(gen());
    auto put = [&](uint32_t offset, const void *p) { std::memcpy(start + offset, &p, 4); };
    put(0x68, &W(wSceneManager));
    put(0x12c, gEntries);
    put(0x130, gEntries + gD.count);
    put(0x134, gEntries + kActors);
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

float Pick(std::mt19937 &gen, std::initializer_list<float> values) { return values.begin()[gen() % values.size()]; }

void DrawCase(std::mt19937 &gen)
{
    gD = Draw{};
    gD.lot = gen() % 2;
    gD.night = gen() % 2;
    gD.appObject = gen() % 2;
    for (int i = 0; i < 3; ++i) gD.transform[i] = Pick(gen, {0.0f, 10.0f, -4.5f, 123.25f});
    // A rotation: the identity, a quarter turn about each axis, or a random unit quaternion.
    const float h = std::sqrt(0.5f);
    const float rotations[5][4] = {{0, 0, 0, 1}, {h, 0, 0, h}, {0, h, 0, h}, {0, 0, h, h}, {0.5f, -0.5f, 0.5f, 0.5f}};
    std::memcpy(gD.transform + 3, rotations[gen() % 5], 16);
    gD.fov = Pick(gen, {1.0f, 0.75f, 0.0f, 3.14159f, std::numeric_limits<float>::quiet_NaN()});
    gD.count = gen() % (kActors + 1);
    for (ActorDraw &a : gD.actors) {
        a.there = gen() % 6 != 0;
        a.object = gen() % 6 != 0;
        a.live = gen() % 4 == 0 ? 0 : gen() % 2 ? 1 : 0x100;
        a.kind = gen() % 2 ? 2 : gen() % 2 ? 1 : 3;
        a.named = gen() % 2;
        // Boxes: centred near the camera's view, of sizes around the 0.5 half-diagonal limit; sometimes empty on one axis
        // (a minimum above its maximum), NaN, or the untouched empty box.
        const float size = Pick(gen, {0.1f, 0.5f, 0.6f, 0.9f, 1.2f, 2.0f, 10.0f});
        for (int i = 0; i < 3; ++i) {
            const float centre = Pick(gen, {0.0f, 5.0f, -3.0f, 20.0f});
            a.box[i] = centre - size * Pick(gen, {0.5f, 0.25f, 1.0f});
            a.box[i + 3] = centre + size * Pick(gen, {0.5f, 0.25f, 1.0f});
        }
        switch (gen() % 8) {
        case 0: std::swap(a.box[gen() % 3], a.box[3 + gen() % 3]); break;
        case 1: a.box[gen() % 6] = std::numeric_limits<float>::quiet_NaN(); break;
        case 2:
            for (int i = 0; i < 3; ++i) a.box[i] = FLT_MAX, a.box[i + 3] = -FLT_MAX;
            break;
        }
    }
}

} // namespace

// 0x248100 UpdateCinematicFade: the camera's position and rotation (identity, quarter turns, a mixed one); a lot world or
// not, day or night, the app object; 0..5 actors, each null or not, its object null or not, live or not (al only), kind
// 2 or not, bounds valid, empty on an axis, NaN or untouched, of sizes around the 0.5 limit, named or not; the field of
// view 0, ordinary, pi or NaN (the ratio's clamp to 1, a zero or NaN distance).
DIFF_TEST(sg_tssg_system_update_cinematic_fade, 0x248100)
{
    Fixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x248100);
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x248100u);
        DrawCase(gen);
        uint8_t start[kObjectSize];
        Start(gen, start);
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            ResetAll();
            sgwide::ResetWide();
            Wire();
            for (unsigned i = 0; i < kActors; ++i)
                gEntries[i] = Entry{gD.actors[i].there ? &W(wActor + i) : nullptr, 0x77u, 1u};
            std::memset(gView, 0, sizeof(gView));
            std::memcpy(gView + 0x3c, &gD.fov, 4);
            *GameAddress<void *>(kSlot23Cell) = &F(fWorld);
            *GameAddress<void *>(kAppCell) = &W(wApp);
            std::memcpy(gObject, start, kObjectSize);
            AddRange(gObject, kObjectSize, kObjectTag);
            AddRange(gEntries, sizeof(gEntries), kActorTag);
            ctx.Mark(side ? "ours" : "original");
            if (side == 0) original(gObject);
            else reinterpret_cast<cTSSGSystem *>(gObject)->UpdateCinematicFade();
            outcome[side] = Outcome();
        }
        Same(ctx, outcome[0], outcome[1], "update_cinematic_fade", seed);
    }
}
