// Differential tests for cTSAudioTSObjectUpdateDemon, tMonitorEntry and tPlayingSound (lanes LD, LE;
// src/audio/cTSAudioTSObjectUpdateDemon.h) in the standalone shape.
//
// AudioObjectUpdateDemon_Run: a demon built by the original constructor (0x22910e, game vtables 0xdaa970 / 0xdaa988 /
// 0xdaa99c / 0xdaa9d4) or factory (0x229c42) against one built by ours (the Construct adapter / Create), each driven
// through its own vtables by the same random operations, then cleared (message 0xb2) and destroyed by the last Release
// (cRZSystemService's Release reaching the +0xc face's deleting destructor thunk 0x2293d1, the deleting destructor 0x2293d9,
// the destructor 0x2292a0), or, every fifth seed, by the +0xc face's slot 3 with flags 0 (destroyed, not freed).
// Operations: Init (0x229d1e, also through the service face's thunk 0x229dc0), Shutdown (0x229fbc, thunk 0x22a007), OnTick
// (0x229e45: UpdateMonitors 0x22a593, StepNextMonitor 0x22995a, RotateMonitors 0x22a05e, UpdateSounds 0x22ab39, with the
// clock's step around the 500 cap: 100, 250, 499, 500, 501, 1000), DoMessage (0x2297c2: the sound start 0xaedbce8b,
// OnSoundEvent 0x229c85 for 0xcc773dd7, ClearAll 0x229d02 / ClearMonitors 0x229b64 / ClearSounds 0x229bd3 for 0xb2 /
// 0xb5, the mode 0xece20be8, 0x2e04ed78, an unknown type, a null message), and through them AddMonitor 0x229436, AddSound
// 0x22955b, RemoveSound 0x229ed2, FindMonitor 0x229aff, FindSound 0x229b2c, Seconds 0x22a07f; QueryInterface (0x229e98) /
// AddRef / Release on every face (the thunks 0x229eba / 0x229ec2 / 0x229eca) and the service accessors.
// The monitors (tMonitorEntry: constructor 0x2291ac, Init 0x229dc8, Update 0x22a091 with the placement update 0x22a5ad,
// Step 0x2299b7 with AimRay 0x2296d2, View 0x22ab53, LotScene 0x229f1d, Detach 0x22a00f, destructor 0x2292e9, deleting
// destructor 0x2293f8) and playing sounds (tPlayingSound: constructor 0x229228, Update 0x22a119 with the monitor's
// Position 0x229e85 / Facing 0x2297af / Hidden 0x229d15, SetMonitor 0x229f6e, Detach 0x22a044, destructor 0x229374,
// deleting destructor 0x229417) each side makes are its own (game vtables 0xdaa9f0 / 0xdaaa0c on the original's).
//
// AudioObjectUpdateDemon_Members: one monitor and one playing sound per case, built by the original constructors
// (0x2291ac, 0x229228) on 0xc5-filled memory against ours, with every field set by the case (the points a chosen number
// of ulps from what the update will compare them with: 0, 1, 31, 32, -32, -33 and far, on either sign), then one member
// called directly: the placement update 0x22a5ad, Step 0x2299b7, AimRay 0x2296d2 (into a 0xc5-filled ray), the sound's
// Update 0x22a119, or the helpers 0x229e85 / 0x2297af / 0x229d15 / 0x22ab53 / 0x229f1d; then both destroyed by their
// destructors (0x2292e9, 0x229374).
//
// Every object the code reaches is a recording fake: tests/ts_services_fixture.h's (up to slot 79) and this file's own
// (the world's objects, the view, the lot scene, the scene node, the model, the posted audio event), which log their
// calls and hand out what the case chose. Stood in for on both sides: the message-id walks (0x1c0458 / 0x1c081e), the
// stopwatch's Elapsed (0x1c9256, a clock that steps by the op's amount on every read), the scene graph's named-node search
// 0xae7490 and bounding box 0xae7d00; on the image: the stopwatch restart 0x1c95ec, game new / delete. The ray's
// constructor / assignment / destructor (0x18e720 / 0x18e880 / 0x18e860), the point transform 0x19c900 and the
// quaternion rotation 0x1a6c40 run as the image's (ours is simgeom's, verified). Compared after every operation: the
// result, the event log, every demon field (the never-written +0x64 and the stopwatch's +4 too, when both blocks start as
// 0xc5), every monitor and sound in list order (their vtables as ours / the game's, the sound's monitor as its list
// position), and every fake's count. Left out: the bytes the constructors never write, which come from the allocator on
// both sides (the monitor's +0x2e / +0x2f, +0x45..+0x47, +0x6d..+0x6f: 0x2291ac writes the word +0x2c, the byte +0x44 and
// the byte +0x6c only; the sound's +0x12 / +0x13 and +0x41..+0x43: 0x229228 writes the word +0x10 and the byte +0x40
// only); in Run, a monitor's points (+0x48..+0x6b) until it has been enabled, since 0x2291ac doesn't write them and
// 0x22a5ad first writes them on the enabled path; and the ray's origin and direction when Step casts a ray AimRay didn't
// aim (no model: the original builds that ray inline at 0x2299f5..0x2299fc, the vtable and +0x1c only, so they are its
// frame's leftovers).
#include "code_patch.h"
#include "compgeom/Linear3.h"
#include "difftest.h"
#include "free_recorder.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include "audio/cTSAudioTSObjectUpdateDemon.h"
#include "sg/SGUtilities.h"
#include "game/game.h"
#include "misc/UtilitiesC.h"
#include "rz/MiscHelpersB.h"
#include "rz/cRZStopwatch.h"
#include "simantics/SimGeometry.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <set>
#include <vector>

namespace audiodemon_test {
// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{10, "dd"}};

using svcfx::Blob;
using svcfx::Ev;
using svcfx::F;
using svcfx::Obj;
using svcfx::P;
using Demon = cTSAudioTSObjectUpdateDemon;

constexpr uint32_t kTag = 0x7e000000;      // the demon under test
constexpr uint32_t kWorldTag = 0x7b000000; // this file's fakes (gW)

enum : unsigned {
    kService = 0, kApp, kWorld, kObj0, kObj1, kObj2, kState, kLotHolder, kLot, kProbe, kMsg, kEvent, kMode, kModel,
    kScene, kRenderer, kCom, kBounds, kPlaced, kHit,
};

std::mt19937 gRng;
uint32_t Roll(uint32_t n) { return uint32_t(gRng() % n); }

uint32_t Bits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}
float FromBits(uint32_t bits)
{
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
// `value` moved by `ulps` units of its bit pattern (kept when that leaves the finite floats).
float Ulps(float value, int32_t ulps)
{
    const float moved = FromBits(Bits(value) + uint32_t(ulps));
    return std::isfinite(moved) ? moved : value;
}

// ---- the world for a seed ----
struct World {
    uint32_t objects; // bit (id % 8): the world has an object of that id
    bool probe;       // the framework hands out the sound renderer
    uint32_t clock;   // the stopwatch's count
    uint32_t step;    // added on every read
};
World gWorld;
struct EventValues {
    uint32_t object, sound, start;
    float volume;
};
EventValues gEvent;

// ---- this file's fakes: objects with slots past the fixture's 80 ----

struct BigObj {
    void **vt;
    uint32_t id;
    int32_t refs;
};
// A product (the monitor's model, a virtual-base reference): its own vtable at +0 (slot 54, the transform), its virtual
// base table at +4; its count is kept on face 1 at +4 + vbtable[1], the fixture's kModel.
struct FakeProduct {
    void **vt;
    const int32_t *vb;
};
struct FakeWorld {
    BigObj objects[3]; // the world's objects (its slot 29)
    BigObj view;       // the state's scene holder's slot 18
    BigObj scene;      // its slot 17 (the ray cast)
    BigObj node;       // the "root_rot" node
    BigObj event;      // the audio event the director's framework makes
    FakeProduct product;
    int32_t productVb[2];
    AudioTransform placeResult; // what a transform call hands back instead of its out argument, sometimes
    AudioTransform viewResult;
};
FakeWorld gW;
void *gObjVt[132], *gViewVt[141], *gSceneVt[93], *gNodeVt[55], *gProductVt[55], *gEventVt[19];

// What the transforms answer: in Run they drift (a coordinate by a few ulps, a new rotation, a new point) on every call;
// in Members they are the case's.
const float kQuats[6][4] = {{0, 0, 0, 1},          {0, 0, 0.30901699f, 0.95105652f}, {0, 0, -0.30901699f, 0.95105652f},
                            {1, 0, 0, 0},          {0, 0, 0.70710677f, 0.70710677f}, {0, 1, 0, 0}};
const int32_t kUlpSteps[] = {0, 1, 31, 32, -32, -33, 4096};
struct Script {
    bool on;
    bool node;   // the "root_rot" search finds the node
    bool bounds; // the model answers 0x7d14eb66
    bool aimed;  // the view's slot 140 has been asked since the last cast: the ray was aimed (AimRay 0x2296d2)
    AudioTransform place, eye;
};
Script gScript;
AudioTransform gPlace, gEye;

float RollFloat() { return float(int32_t(Roll(2001)) - 1000) / 8.0f; }
void RollPoint(AudioPoint &p)
{
    p.x = RollFloat();
    p.y = RollFloat();
    p.z = RollFloat();
}
float &Coordinate(AudioPoint &p, unsigned i) { return i == 0 ? p.x : i == 1 ? p.y : p.z; }

void Drift(AudioTransform &t)
{
    switch (Roll(12)) {
    case 0:
    case 1:
    case 2: Coordinate(t.position, Roll(3)) = Ulps(Coordinate(t.position, Roll(3)), kUlpSteps[Roll(7)]); break;
    case 3: std::memcpy(t.rotation, kQuats[Roll(6)], sizeof(t.rotation)); break;
    case 4: {
        const unsigned i = Roll(4);
        t.rotation[i] = Ulps(t.rotation[i], kUlpSteps[Roll(7)]);
        break;
    }
    case 5: RollPoint(t.position); break;
    default: break;
    }
}

// The transform `t` given back: written to `out`, or (one call in four) a scrambled `out` and a pointer to `kept` holding
// `t` (the code must use what it is told to).
uint32_t GiveTransform(const AudioTransform &t, AudioTransform *out, AudioTransform &kept)
{
    if (Roll(4) == 0) {
        kept = t;
        *out = t;
        out->position.x += 1000.0f;
        out->rotation[2] = 0.5f;
        return P(&kept);
    }
    *out = t;
    return P(out);
}

uint32_t __fastcall BigAddRef(BigObj *self, void *)
{
    Ev(0xb1);
    Ev(self->id);
    return uint32_t(++self->refs);
}
uint32_t __fastcall BigRelease(BigObj *self, void *)
{
    Ev(0xb2);
    Ev(self->id);
    return uint32_t(--self->refs);
}

// The world's objects: slot 0 QueryInterface (0x5a69d3d5 answered by kPlaced, one call in two), 12 the model
// (0xfa1c39f7, 0xfa1c39ed, out), 13 flags, 36 the level, 43 the direction, 131 the room; the 16-bit answers carry junk in
// their high word.
uint32_t HandOut(uint32_t out, Obj &obj)
{
    ++obj.refs;
    *reinterpret_cast<void **>(uintptr_t(out)) = &obj;
    return 1;
}
uint32_t __fastcall ObjQuery(BigObj *self, void *, uint32_t iid, void **out)
{
    Ev(0xb0);
    Ev(self->id);
    Ev(iid);
    if (iid == 0x5a69d3d5 && Roll(2)) return HandOut(P(out), F(kPlaced));
    return 0;
}
uint32_t __fastcall ObjModel(BigObj *self, void *, uint32_t a, uint32_t b, void **out)
{
    Ev(0xbc);
    Ev(self->id);
    Ev(a);
    Ev(b);
    Ev(svcfx::Norm(P(*out)));
    if (Roll(4)) {
        ++F(kModel).refs;
        *out = &gW.product;
    }
    return 0x5555;
}
uint32_t __fastcall ObjFlags(BigObj *self, void *, uint32_t arg)
{
    static const uint32_t kFlags[] = {0, 2, 3, 0xabcdfffd, 0x0102, 0x200, 0x12340002};
    Ev(0xbd);
    Ev(self->id);
    Ev(arg);
    return kFlags[Roll(7)];
}
uint32_t __fastcall ObjLevel(BigObj *self, void *)
{
    static const int16_t kLevels[] = {0, 1, 2, 3, -5, 1};
    Ev(0xbe);
    Ev(self->id);
    return 0xabcd0000u | uint16_t(kLevels[Roll(6)]);
}
uint32_t __fastcall ObjDirection(BigObj *self, void *)
{
    static const int32_t kDirections[] = {0, 1, 2, 3, 4, 5, 6, 7, -1, 9};
    Ev(0xbf);
    Ev(self->id);
    return uint32_t(kDirections[Roll(10)]);
}
uint32_t __fastcall ObjRoom(BigObj *self, void *)
{
    static const int16_t kRooms[] = {1, 0, -1, 7, -32768, 3};
    Ev(0xc0);
    Ev(self->id);
    return 0x5a5a0000u | uint16_t(kRooms[Roll(6)]);
}

// The view: slot 53 (out) and slot 140 (out, 0), each the eye's transform.
uint32_t EyeTransform(AudioTransform *out)
{
    if (!gScript.on && Roll(3) == 0) RollPoint(gEye.position);
    return GiveTransform(gScript.on ? gScript.eye : gEye, out, gW.viewResult);
}
uint32_t __fastcall ViewEye(BigObj *self, void *, AudioTransform *out)
{
    Ev(0x53);
    Ev(self->id);
    return EyeTransform(out);
}
uint32_t __fastcall ViewEyeAt(BigObj *self, void *, AudioTransform *out, uint32_t arg)
{
    Ev(0x140);
    Ev(self->id);
    Ev(arg);
    gScript.aimed = true;
    return EyeTransform(out);
}

// The lot scene's slot 92 (range, lot, ray): the ray it was given (its origin and direction only when AimRay aimed it),
// a hit (kHit, not counted) one call in three.
uint32_t __fastcall CastRay(BigObj *self, void *, float *range, uint32_t lot, const AudioRay *ray)
{
    Ev(0x92);
    Ev(self->id);
    Ev(Bits(*range));
    Ev(svcfx::Norm(lot));
    // The game's cRay3 vtable (the original's constructor) or ours (the ray's constructor is ours now, 0x18e720).
    static const void *const oursVTable = [] {
        nGZCompGeom::cRay3 probe;
        return *reinterpret_cast<const void *const *>(&probe);
    }();
    Ev(ray->mVTable == GameAddress<void>(0xda139c) || ray->mVTable == oursVTable);
    Ev(ray->mWord1c);
    if (gScript.aimed) {
        for (const AudioPoint *p : {&ray->mOrigin, &ray->mDirection})
            for (float v : {p->x, p->y, p->z}) Ev(Bits(v));
        gScript.aimed = false;
    }
    return Roll(3) == 0 ? P(&F(kHit)) : 0;
}

// The node's and the model's slot 54 (out): the place.
uint32_t PlaceTransform(AudioTransform *out)
{
    if (!gScript.on) Drift(gPlace);
    return GiveTransform(gScript.on ? gScript.place : gPlace, out, gW.placeResult);
}
uint32_t __fastcall NodeTransform(BigObj *self, void *, AudioTransform *out)
{
    Ev(0x54);
    Ev(self->id);
    return PlaceTransform(out);
}
uint32_t __fastcall ProductTransform(FakeProduct *, void *, AudioTransform *out)
{
    Ev(0x55);
    return PlaceTransform(out);
}

// The posted audio event's setters (cITSAudioEvent slots 5, 9, 12, 15, 18).
uint32_t __fastcall EventSetId(BigObj *self, void *, uint32_t id)
{
    Ev(0xe5);
    Ev(self->refs);
    Ev(id);
    return id;
}
void __fastcall EventSetWord(BigObj *, void *, uint32_t key, uint32_t value) // SetFloat / SetUint32 / SetSint32
{
    Ev(0xe9);
    Ev(key);
    Ev(value);
}
void __fastcall EventSetVector(BigObj *, void *, uint32_t key, const float *value)
{
    Ev(0xe18);
    Ev(key);
    for (int i = 0; i < 3; ++i) Ev(Bits(value[i]));
}
uint32_t __fastcall EventSetFloat(BigObj *self, void *, uint32_t key, uint32_t bits)
{
    Ev(0xe90);
    EventSetWord(self, nullptr, key, bits);
    return 0;
}
uint32_t __fastcall EventSetUint(BigObj *self, void *, uint32_t key, uint32_t value)
{
    Ev(0xe12);
    EventSetWord(self, nullptr, key, value);
    return 0;
}
uint32_t __fastcall EventSetSint(BigObj *self, void *, uint32_t key, uint32_t value)
{
    Ev(0xe15);
    EventSetWord(self, nullptr, key, value);
    return 0;
}

template <class F> void *Code(F f) { return reinterpret_cast<void *>(f); }

void BuildVtables()
{
    gObjVt[0] = Code(&ObjQuery);
    gObjVt[1] = gViewVt[1] = gSceneVt[1] = gNodeVt[1] = gEventVt[1] = Code(&BigAddRef);
    gObjVt[2] = gViewVt[2] = gSceneVt[2] = gNodeVt[2] = gEventVt[2] = Code(&BigRelease);
    gObjVt[12] = Code(&ObjModel);
    gObjVt[13] = Code(&ObjFlags);
    gObjVt[36] = Code(&ObjLevel);
    gObjVt[43] = Code(&ObjDirection);
    gObjVt[131] = Code(&ObjRoom);
    gViewVt[53] = Code(&ViewEye);
    gViewVt[140] = Code(&ViewEyeAt);
    gSceneVt[92] = Code(&CastRay);
    gNodeVt[54] = Code(&NodeTransform);
    gProductVt[54] = Code(&ProductTransform);
    gEventVt[5] = Code(&EventSetId);
    gEventVt[9] = Code(&EventSetFloat);
    gEventVt[12] = Code(&EventSetUint);
    gEventVt[15] = Code(&EventSetSint);
    gEventVt[18] = Code(&EventSetVector);
}

void BigInit(BigObj &o, void **vt, uint32_t id)
{
    o.vt = vt;
    o.id = id;
    o.refs = 1000;
}

// ---- stand-ins ----

// Filled, so what the constructors leave unwritten (the monitor's points, which the first placement update compares
// with) is the same on both sides.
void *__cdecl GameNew(size_t n)
{
    void *memory = ::operator new(n);
    std::memset(memory, 0xc5, n);
    return memory;
}
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

// Ours allocates the same way while a Fixture lives: our monitors come from operator new (this exe's heap), and the first
// placement update reads their unwritten points as the original reads GameNew's 0xc5. Unfilled, a monitor built in a block
// an earlier monitor of ours freed compares the place with that one's points, and whether the heap hands that block back
// depends on what ran before: the moved branch then differs, the rolls shift, and a later word differs (lane LU: seed 1471,
// word 42, the monitor's room, in the full suite only). ReleaseMonitors' 0xc5 covers only the monitors this file releases.
using HeapAllocFn = void *(WINAPI *)(HANDLE, DWORD, SIZE_T);
HeapAllocFn gRealHeapAlloc;
void *WINAPI FilledHeapAlloc(HANDLE heap, DWORD flags, SIZE_T size)
{
    void *memory = gRealHeapAlloc(heap, flags, size);
    if (memory && !(flags & HEAP_ZERO_MEMORY)) std::memset(memory, 0xc5, size);
    return memory;
}
struct FilledHeap {
    FilledHeap()
    {
        gRealHeapAlloc = reinterpret_cast<HeapAllocFn>(
            difftest::SwapOwnImport("HeapAlloc", reinterpret_cast<void *>(&FilledHeapAlloc)));
    }
    ~FilledHeap()
    {
        if (gRealHeapAlloc) difftest::SwapOwnImport("HeapAlloc", reinterpret_cast<void *>(gRealHeapAlloc));
    }
    FilledHeap(const FilledHeap &) = delete;
    FilledHeap &operator=(const FilledHeap &) = delete;
};

uint32_t __fastcall StandWalk5(void *self, void *, uint32_t arg, void *obj)
{
    Ev(0x1c0458);
    Ev(svcfx::Norm(P(self)));
    Ev(svcfx::Norm(arg));
    Ev(svcfx::Norm(P(obj)));
    for (uint32_t id : *reinterpret_cast<std::set<uint32_t> *>(self)) Ev(id);
    return 0;
}
void __fastcall StandWalk6(void *self, void *, uint32_t arg, void *obj)
{
    Ev(0x1c081e);
    Ev(svcfx::Norm(P(self)));
    Ev(svcfx::Norm(arg));
    Ev(svcfx::Norm(P(obj)));
    for (uint32_t id : *reinterpret_cast<std::set<uint32_t> *>(self)) Ev(id);
}
uint32_t __fastcall StandElapsed(const cRZStopwatch *self, void *)
{
    Ev(0x1c9256);
    Ev(svcfx::Norm(P(self)));
    gScript.aimed = false; // Step reads the clock last: a ray aimed but not cast is not the next step's
    const uint32_t now = gWorld.clock;
    gWorld.clock += gWorld.step;
    return now;
}
void __fastcall StandRestart(cRZStopwatch *self, void *)
{
    Ev(0x1c95ec);
    Ev(svcfx::Norm(P(self)));
    self->mAccumulated = 7;
}
// 0xae7490 (name, node, flags): the "root_rot" node, or none.
void *__cdecl StandFindNode(const char *name, void *node, uint32_t flags)
{
    Ev(0xae7490);
    Ev(std::strcmp(name, "root_rot") == 0);
    Ev(svcfx::Norm(P(node)));
    Ev(flags);
    const bool found = gScript.on ? gScript.node : Roll(3) != 0;
    return found ? &gW.node : nullptr;
}
// 0xae7d00 (box, flags, node, node): logs the box it is given, then grows it to a box of the case's.
void __cdecl StandBounds(float *box, int32_t flags, void *a, void *b)
{
    Ev(0xae7d00);
    for (int i = 0; i < 6; ++i) Ev(Bits(box[i]));
    Ev(uint32_t(flags));
    Ev(svcfx::Norm(P(a)));
    Ev(svcfx::Norm(P(b)));
    // Values with full mantissas, so the centre's rounding ((max - min) * 0.5 + min, not (max + min) * 0.5) shows.
    for (int i = 0; i < 6; ++i) box[i] = RollFloat() * 0.37f + float(Roll(1000)) * 0.0013f;
}

// ---- fake handlers (the fixture's objects) ----

// The framework's slot 5 GetSystemService(class, iid, out): the sound renderer (0x20db8e) when the world has one.
uint32_t GetSystemService(Obj *, uint32_t *a)
{
    if (a[0] == 0x23cecba0 && gWorld.probe) return HandOut(a[2], F(kProbe));
    return 0;
}
// QueryInterface on the message: value[0] the iid answered, value[1] the fake handed out (1 + index).
uint32_t Query(Obj *self, uint32_t *a)
{
    if (a[0] != self->value[0]) return 0x3300;
    return HandOut(a[1], F(self->value[1] - 1)) | 0x3300;
}
// The model's virtual base: 0x7d14eb66 answered by kBounds (always in Run, so a monitor's first centre is the box's,
// never its unwritten place; by the case in Members).
uint32_t ModelQuery(Obj *, uint32_t *a)
{
    if (a[0] != 0x7d14eb66 || (gScript.on && !gScript.bounds)) return 0x3300;
    return HandOut(a[1], F(kBounds)) | 0x3300;
}
// The world's slot 29 (object by id): the world's objects, not counted.
uint32_t ObjectById(Obj *, uint32_t *a)
{
    if (!a[0] || !((gWorld.objects >> (a[0] % 8)) & 1)) return 0;
    return P(&gW.objects[a[0] % 3]);
}
// The audio event's slot 11 GetUint32(key).
uint32_t EventUint32(Obj *, uint32_t *a)
{
    switch (a[0]) {
    case 0xac5b5b4f: return gEvent.object;
    case 0x6bd63081: return gEvent.sound;
    case 0x8bd63096: return gEvent.start;
    default: return 0x77;
    }
}
// Its slot 8 GetFloat(key) (st0).
float __fastcall EventFloat(Obj *self, void *, uint32_t key)
{
    Ev(0xf10a);
    Ev(self->id);
    Ev(key);
    return key == 0xec488a56 ? gEvent.volume : -1.0f;
}
// The director's framework slot 3 (class, iid, out): the audio event, four calls in five.
uint32_t CreateEvent(Obj *, uint32_t *a)
{
    if (Roll(5) == 0) return 0;
    ++gW.event.refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &gW.event;
    return 1;
}
// The sound renderer's slot 54 (object id, key, AnyFloat32): the value and the any's type (its slot 4).
uint32_t RendererAudible(Obj *, uint32_t *a)
{
    void *const any = reinterpret_cast<void *>(uintptr_t(a[2]));
    Ev(reinterpret_cast<const uint32_t *>(any)[1]);
    Ev((*static_cast<uint32_t(__thiscall ***)(void *)>(any))[4](any));
    return 0;
}

void BuildFakes(uint32_t seed)
{
    svcfx::ResetAll();
    std::mt19937 rng(seed);
    gWorld.objects = rng() % 4 ? 0xff & ~(1u << (rng() % 8)) : rng() % 256;
    gWorld.probe = rng() % 6 != 0;
    gWorld.clock = 100000 + rng() % 100000;
    gWorld.step = 250;

    Obj &service = F(kService);
    for (unsigned s : {3u, 4u, 9u, 10u}) service.Set(s, "p", 0x300 + s);
    service.Set(5, "ddp", 0, &GetSystemService);
    F(kApp).Set(26, 0, rng() % 6 ? &F(kWorld) : nullptr);
    F(kWorld).Set(29, "d", 0, &ObjectById);
    F(kState).Set(46, 0, rng() % 7 ? &F(kLotHolder) : nullptr);
    F(kState).Set(33, 0, &F(kScene));
    F(kLotHolder).Set(4, 0, rng() % 7 ? &F(kLot) : nullptr);
    F(kMsg).Set(0, "dp", 0, &Query);
    F(kMsg).Set(4, 0, 0u);
    F(kEvent).Set(11, "d", 0, &EventUint32);
    F(kEvent).Set(8, 1);
    F(kEvent).table[8] = reinterpret_cast<void *>(&EventFloat);
    F(kMode).Set(9, 0, 0u);
    F(kMode).Set(19, 0, 0u);
    F(kModel).Set(0, "dp", 0, &ModelQuery);
    F(kScene).Set(17, 0, rng() % 5 ? &gW.scene : nullptr);
    F(kScene).Set(18, 0, &gW.view);
    F(kRenderer).Set(17, "p");
    F(kRenderer).Set(29, "d-");
    F(kRenderer).Set(54, "ddp", 0, &RendererAudible);
    F(kCom).Set(3, "ddp", 0, &CreateEvent);

    for (unsigned i = 0; i < 3; ++i) BigInit(gW.objects[i], gObjVt, 300 + i);
    BigInit(gW.view, gViewVt, 310);
    BigInit(gW.scene, gSceneVt, 311);
    BigInit(gW.node, gNodeVt, 312);
    BigInit(gW.event, gEventVt, 313);
    gW.productVb[0] = 0;
    gW.productVb[1] = int32_t(P(&F(kModel)) - (P(&gW.product) + 4));
    gW.product.vt = gProductVt;
    gW.product.vb = gW.productVb;
    svcfx::AddRange(&gW, sizeof(gW), kWorldTag);

    gScript.on = false;
    gScript.aimed = false;
    gRng.seed(seed * 7 + 3);
    RollPoint(gPlace.position);
    std::memcpy(gPlace.rotation, kQuats[Roll(6)], sizeof(gPlace.rotation));
    RollPoint(gEye.position);
    std::memcpy(gEye.rotation, kQuats[0], sizeof(gEye.rotation));

    *GameAddress<void *>(0x10c9378) = rng() % 9 ? &F(kService) : nullptr;
    *GameAddress<void *>(0x10f5194) = rng() % 2 ? *GameAddress<void *>(0x10c9378) : nullptr;
    *GameAddress<void *>(svcfx::kAppCell) = &F(kApp);
    *GameAddress<void *>(0x10cc458) = &F(kState);
    *GameAddress<void *>(0x10f51b4) = rng() % 3 ? &F(kProbe) : nullptr;
    *GameAddress<void *>(0x10cc648) = &F(kRenderer);
    // The point the playing sound's constructor copies (0x10cc5a0).
    const float origin[3] = {float(rng() % 100), -2.5f, 0.125f};
    std::memcpy(GameAddress<void>(0x10cc5a0), origin, sizeof(origin));
}

// ---- driving a demon ----

template <class R, class... A> R Slot(void *face, unsigned slot, A... a)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, A...)>((*static_cast<void ***>(face))[slot]);
    return fn(face, a...);
}
uint8_t *Face(void *object, uint32_t offset) { return static_cast<uint8_t *>(object) + offset; }

bool gFactory; // this seed builds through the factories (no 0xc5 fill)

void *Build(difftest::Context &ctx, bool ours)
{
    if (gFactory) {
        auto *service = ours ? reinterpret_cast<uint8_t *>(Demon::Create())
                             : reinterpret_cast<uint8_t *>(ctx.Original<void *(__cdecl *)()>(0x229c42)());
        return service - 8;
    }
    void *memory = ::operator new(Demon::kSize);
    std::memset(memory, 0xc5, Demon::kSize);
    if (ours) cTSAudioTSObjectUpdateDemonAdapters::Construct(memory, nullptr);
    else ctx.Original<void *(__thiscall *)(void *)>(0x22910e)(memory);
    return memory;
}

uint32_t Word(const uint8_t *m, uint32_t off) { return *reinterpret_cast<const uint32_t *>(m + off); }
uint32_t VtableFlag(const void *object, uint32_t vtable, bool ours)
{
    const bool game = Word(static_cast<const uint8_t *>(object), 0) == P(GameAddress<void>(vtable));
    return ours ? !game : game;
}
void DumpPoint(Blob &b, const AudioPoint &p)
{
    b.push_back(Bits(p.x));
    b.push_back(Bits(p.y));
    b.push_back(Bits(p.z));
}

void DumpMonitor(Blob &b, const tMonitorEntry &m, bool ours, bool points)
{
    b.push_back(VtableFlag(&m, 0xdaa9f0, ours));
    b.push_back(m.mRefCount);
    b.push_back(svcfx::Norm(P(m.mDemon.Get())));
    b.push_back(svcfx::Norm(P(m.mLot.Get())));
    b.push_back(svcfx::Norm(P(m.mObject.Get())));
    b.push_back(svcfx::Norm(P(m.mModel.Get())));
    b.push_back(svcfx::Norm(P(m.mRef18.Get())));
    for (uint32_t v : {m.mRegistered, m.mUpdatedAt, m.mStepAt, m.mInterval, uint32_t(m.mObjectId), uint32_t(m.mRoom),
                       uint32_t(m.mFloor), Bits(m.mAudibility), m.mSoundCount, Bits(m.mDistance), uint32_t(m.mEnabled)})
        b.push_back(v);
    b.push_back(points);
    if (points) {
        DumpPoint(b, m.mCentre);
        DumpPoint(b, m.mPosition);
        DumpPoint(b, m.mFacing);
    }
    b.push_back(m.mHidden);
}

void DumpSound(Blob &b, const tPlayingSound &s, const MonitorList &monitors, bool ours)
{
    b.push_back(VtableFlag(&s, 0xdaaa0c, ours));
    b.push_back(s.mRefCount);
    b.push_back(svcfx::Norm(P(s.mDemon.Get())));
    uint32_t monitor = 0;
    if (s.mMonitor) {
        monitor = 0xdd000000 | s.mMonitor->mObjectId; // held but no longer listed
        uint32_t index = 0;
        for (const auto &m : monitors) {
            if (m.Get() == s.mMonitor.Get()) monitor = 0x1000 + index;
            ++index;
        }
    }
    b.push_back(monitor);
    for (uint32_t v : {uint32_t(s.mObjectId), s.mSoundId, Bits(s.mVolume)}) b.push_back(v);
    DumpPoint(b, s.mPosition);
    DumpPoint(b, s.mFacing);
    for (uint32_t v : {uint32_t(s.mRoom), uint32_t(s.mFloor), Bits(s.mAudibility), uint32_t(s.mHidden)}) b.push_back(v);
}

// The monitors each side has seen enabled, by address: their points are written (0x22a5ad writes them on the enabled path
// only, 0x2291ac never). Pruned to the listed ones at every dump.
std::set<const void *> gPlaced[2];

// The placement update compares the place with the monitor's last, which a new monitor never wrote: the original's
// allocation is filled (GameNew), ours too (FilledHeap; before lane LU ours was the heap's, which this guard alone did not
// cover: a monitor the demon frees itself kept its points). So no monitor's memory is reused within a seed while it may still
// hold a place: every monitor listed or held by a sound gets a reference before an operation (through its vtable, both
// sides alike), and after it those only this file still holds have their points set to 0xc5 before the last release
// destroys them (in the dump's step, so their destructors' calls are still compared).
std::vector<void *> HoldMonitors(void *object)
{
    const auto &demon = *static_cast<const Demon *>(object);
    std::vector<void *> held;
    for (const auto &mon : demon.mMonitors) held.push_back(mon.Get());
    for (const auto &s : demon.mSounds)
        if (s->mMonitor) held.push_back(s->mMonitor.Get());
    for (void *m : held) Slot<uint32_t>(m, 1);
    return held;
}
void ReleaseMonitors(const std::vector<void *> &held)
{
    for (void *m : held) {
        auto *monitor = static_cast<tMonitorEntry *>(m);
        if (monitor->mRefCount == 1) {
            std::memset(&monitor->mCentre, 0xc5, 3 * sizeof(AudioPoint));
            gPlaced[0].erase(m);
            gPlaced[1].erase(m);
        }
        Slot<uint32_t>(m, 2);
    }
}

void CaptureWorld(Blob &b)
{
    svcfx::CaptureFakes(b, kValueKinds);
    for (const BigObj *o : {&gW.objects[0], &gW.objects[1], &gW.objects[2], &gW.view, &gW.scene, &gW.node, &gW.event})
        b.push_back(uint32_t(o->refs));
}

void Dump(Blob &b, void *object, int side)
{
    const bool ours = side == 1;
    const auto *m = static_cast<const uint8_t *>(object);
    const auto &demon = *static_cast<const Demon *>(object);
    // The game's vtables of each face (kept here, not in src/: our code must not hold them, tools/callers.py).
    static const uint32_t kFaces[4][2] = {{0, 0xdaa970}, {4, 0xdaa988}, {8, 0xdaa99c}, {0xc, 0xdaa9d4}};
    for (const auto &f : kFaces) b.push_back(VtableFlag(m + f[0], f[1], ours));
    for (uint32_t off = 0x10; off < 0x20; off += 4) b.push_back(Word(m, off));
    b.push_back(gFactory ? m[0x20] : Word(m, 0x20)); // the running flag; its padding is the allocator's from a factory
    b.push_back(uint32_t(demon.mMessageIds.size()));
    for (uint32_t id : demon.mMessageIds) b.push_back(id);
    b.push_back(uint32_t(demon.mSounds.size()));
    for (const auto &s : demon.mSounds) DumpSound(b, *s.Get(), demon.mMonitors, ours);
    b.push_back(uint32_t(demon.mMonitors.size()));
    std::set<const void *> placed;
    for (const auto &mon : demon.mMonitors) {
        const bool written = mon->mEnabled || gPlaced[side].count(mon.Get());
        if (written) placed.insert(mon.Get());
        DumpMonitor(b, *mon.Get(), ours, written);
    }
    gPlaced[side] = std::move(placed);
    b.push_back(Word(m, 0x3c));
    b.push_back(Word(m, 0x40));
    b.push_back(gFactory ? 0 : Word(m, 0x44)); // the stopwatch's +4: never written (0x1c9030)
    for (uint32_t off = 0x48; off < 0x64; off += 4) b.push_back(Word(m, off));
    b.push_back(gFactory ? 0 : Word(m, 0x64)); // never written (0x22910e)
    // The cached framework, sound renderer and state service cells.
    for (uint32_t cell : {0x10f5194u, 0x10c9378u, 0x10f51b4u, 0x10cc458u}) b.push_back(svcfx::Norm(*GameAddress<uint32_t>(cell)));
}

struct Op {
    unsigned kind;
    uint32_t arg, arg2;
};

std::vector<Op> MakeOps(uint32_t seed)
{
    std::mt19937 rng(seed * 31 + 5);
    std::vector<Op> ops;
    const unsigned n = 4 + rng() % 40;
    if (rng() % 6) ops.push_back({0, rng() | 1, rng()});
    for (unsigned i = 0; i < n; ++i) {
        unsigned kind = rng() % 12;
        if (kind >= 8 && kind <= 9 && rng() % 3) kind = 3; // more sound messages
        ops.push_back({kind, rng(), rng()});
    }
    return ops;
}

const uint32_t kIids[] = {1, Demon::kIid, 0x287fb697, 0x0c1e612a, 0x12345678, 0x4d58135d};
const uint32_t kObjectIds[] = {0, 1, 2, 3, 5, 0x10001, 0x20003, 0xffff, 7, 0x10000, 6, 4};
const uint32_t kSoundIds[] = {1, 2, 3, 0x80000001, 0, 0x10001};
const uint32_t kSteps[] = {100, 250, 499, 500, 501, 1000};
const uint32_t kTypes[] = {0xaedbce8b, 0xcc773dd7, 0xb2, 0xb5, 0xece20be8, 0x2e04ed78, 0x1234, 0xcc773dd7, 0xaedbce8b};
const float kVolumes[] = {0.0f, 1.0f, 0.5f, -3.25f, 100.0f};

// A message of `type` with the event's values from `a`; `c` picks whether the message answers the event interface.
uint32_t SendMessage(void *target, uint32_t type, uint32_t a, uint32_t c)
{
    F(kMsg).ret[4] = type;
    gEvent.object = kObjectIds[a % 12];
    gEvent.sound = kSoundIds[(a >> 4) % 6];
    gEvent.start = (a >> 8) % 3 ? (a >> 10) % 2 + 1 : 0u;
    gEvent.volume = kVolumes[(a >> 12) % 5];
    if (type == 0xece20be8) {
        // The mode's face is used unchecked (DoMessage's ORIGINAL-BUG): always answered.
        F(kMsg).value[0] = 0x65297976;
        F(kMsg).value[1] = 1 + kMode;
        F(kMode).ret[9] = c % 7;
        F(kMode).ret[19] = (c >> 3) % 5 - 1;
    } else {
        F(kMsg).value[0] = c % 6 ? 0x0bc7957du : 0x1234u;
        F(kMsg).value[1] = 1 + kEvent;
    }
    if (type == 0x1234 && c % 4 == 0) return Slot<uint32_t>(target, 3, static_cast<void *>(nullptr)) & 0xff;
    return Slot<uint32_t>(target, 3, static_cast<void *>(&F(kMsg))) & 0xff;
}

void RunOp(Blob &b, void *object, const Op &op)
{
    void *const primary = object;
    void *const target = Face(object, 4);
    void *const service = Face(object, 8);
    void *const unknown = Face(object, 0xc);
    const uint32_t a = op.arg, c = op.arg2;
    void *out = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
    gWorld.clock += (c >> 24) % 5 * 3001; // time passes between ops: seconds for the monitors' update intervals
    switch (op.kind) {
    case 0: b.push_back((a % 3 ? Slot<uint32_t>(primary, 3) : Slot<uint32_t>(service, 8)) & 0xff); break;
    case 1: b.push_back((a % 3 ? Slot<uint32_t>(primary, 4) : Slot<uint32_t>(service, 9)) & 0xff); break;
    case 2: { // OnTick, a few times
        const unsigned times = 1 + a % 3;
        for (unsigned t = 0; t < times; ++t) {
            gWorld.step = kSteps[(c >> (4 * t)) % 6];
            b.push_back(Slot<uint32_t>(service, 10, c) & 0xff);
        }
        break;
    }
    case 3: // a sound start or event
        b.push_back(SendMessage(target, a % 2 ? 0xaedbce8bu : 0xcc773dd7u, a >> 1, c));
        break;
    case 4: b.push_back(SendMessage(target, kTypes[a % 9], a >> 4, c)); break;
    case 5: { // QueryInterface on a face, AddRef / Release through others
        void *faces[] = {primary, target, service, unknown};
        void *q = out;
        const uint32_t ok = Slot<uint32_t>(faces[a % 4], 0, kIids[(a >> 4) % 6], &q) & 0xff;
        b.push_back(ok);
        b.push_back(svcfx::NormOut(P(q)));
        b.push_back(Slot<uint32_t>(faces[(a >> 8) % 4], 1));
        b.push_back(Slot<uint32_t>(faces[(a >> 12) % 4], 2));
        if (ok) b.push_back(Slot<uint32_t>(q, 2));
        break;
    }
    case 6: // the cIGZSystemService accessors and cRZUnknownMT's
        b.push_back(Slot<uint32_t>(service, 3));
        b.push_back(Slot<uint32_t>(service, 5));
        b.push_back(Slot<uint32_t>(service, 6) & 0xff);
        b.push_back(Slot<uint32_t>(service, 11, c) & 0xff);
        b.push_back(Slot<uint32_t>(service, 12));
        Slot<uint32_t>(service, 7, a % 2);
        b.push_back(Slot<uint32_t>(unknown, 5));
        break;
    case 7: // the world changes
        gWorld.objects = a % 4 ? gWorld.objects ^ (1u << ((a >> 2) % 8)) : (a >> 8) % 256;
        gWorld.probe = (a >> 16) % 4 != 0;
        F(kApp).ret[26] = (a >> 18) % 6 ? P(&F(kWorld)) : 0;
        F(kState).ret[46] = (a >> 21) % 7 ? P(&F(kLotHolder)) : 0;
        F(kLotHolder).ret[4] = (a >> 24) % 7 ? P(&F(kLot)) : 0;
        if ((a >> 27) % 4 == 0) *GameAddress<void *>(0x10f51b4) = nullptr;
        if ((c % 16) == 0) {
            *GameAddress<void *>(0x10f5194) = nullptr;
            *GameAddress<void *>(0x10c9378) = c % 32 ? nullptr : &F(kService);
        }
        F(kScene).ret[17] = (c >> 8) % 5 ? P(&gW.scene) : 0;
        break;
    default: // more ticks between the messages
        gWorld.step = kSteps[a % 6];
        b.push_back(Slot<uint32_t>(service, 10, c) & 0xff);
        b.push_back(SendMessage(target, kTypes[(a >> 4) % 9], a >> 8, c));
        break;
    }
}

// ---- Members: one monitor and one sound ----

// A point `ulps` units from `p` in coordinate `which` (3: none).
AudioPoint Nudged(AudioPoint p, unsigned which, int32_t ulps)
{
    if (which < 3) Coordinate(p, which) = Ulps(Coordinate(p, which), ulps);
    return p;
}
float RollMember(std::mt19937 &rng)
{
    static const float kValues[] = {0.0f, -0.0f, 1.0f, -2.5f, 3.75f, 100.125f, -0.001f, 12345.0f};
    return rng() % 3 ? kValues[rng() % 8] : float(int32_t(rng() % 4001) - 2000) / 16.0f;
}
AudioPoint RollMemberPoint(std::mt19937 &rng) { return {RollMember(rng), RollMember(rng), RollMember(rng)}; }

struct MemberCase {
    unsigned op; // 0 placement, 1 step, 2 aim, 3 sound update, 4 helpers
    uint16_t objectId;
    bool object, model, lot, enabled, sound, worldApp;
    int32_t room, floor, demonFloor;
    float audibility;
    uint8_t hidden;
    AudioPoint centre, position, facing;
    // the sound's
    AudioPoint soundPosition, soundFacing;
    int32_t soundRoom, soundFloor;
    float soundAudibility;
    uint8_t soundHidden;
    bool view, scene;
};

MemberCase MakeCase(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 11);
    MemberCase c{};
    c.op = rng() % 5;
    c.objectId = uint16_t(rng() % 5 ? rng() % 8 : 0x8001);
    c.object = rng() % 4 != 0;
    c.model = rng() % 5 != 0;
    c.lot = rng() % 4 != 0;
    c.enabled = rng() % 6 != 0;
    c.sound = rng() % 8 != 0;
    c.worldApp = rng() % 8 != 0;
    static const int32_t kRooms[] = {0, 1, -1, 0x10000, 0x20001, 7};
    static const int32_t kFloors[] = {-1, 0, 1, 2, 3};
    c.room = kRooms[rng() % 6];
    c.floor = kFloors[rng() % 5];
    c.demonFloor = kFloors[rng() % 5];
    static const float kAudible[] = {0.0f, 1.0f, -0.0f, 0.5f};
    c.audibility = kAudible[rng() % 4];
    c.hidden = uint8_t(rng() % 3);
    c.centre = RollMemberPoint(rng);
    c.position = RollMemberPoint(rng);
    c.facing = RollMemberPoint(rng);
    c.view = c.op != 1 || rng() % 6 != 0; // the placement and AimRay use the view unchecked
    c.scene = rng() % 5 != 0;

    // The place the node's (or the model's) transform gives, the rotation, the search result: the monitor's points a
    // chosen number of ulps from them.
    gScript.node = rng() % 4 != 0;
    gScript.bounds = rng() % 2 != 0;
    gScript.place.position = RollMemberPoint(rng);
    std::memcpy(gScript.place.rotation, kQuats[rng() % 6], sizeof(gScript.place.rotation));
    if (rng() % 3 == 0) gScript.place.rotation[rng() % 4] = Ulps(gScript.place.rotation[0], kUlpSteps[rng() % 7]);
    gScript.eye.position = RollMemberPoint(rng);
    std::memcpy(gScript.eye.rotation, kQuats[rng() % 6], sizeof(gScript.eye.rotation));
    AudioPoint facing = gScript.node ? AudioPoint{0, 0, 1} : AudioPoint{0, 1, 0};
    simgeom::RotateByQuaternion(&facing.x, gScript.place.rotation);
    if (c.op == 0 && rng() % 4 != 0) {
        c.position = Nudged(gScript.place.position, rng() % 4, kUlpSteps[rng() % 7]);
        c.facing = Nudged(facing, rng() % 4, kUlpSteps[rng() % 7]);
        if (rng() % 2) c.position = gScript.place.position;
    }

    // The sound: the monitor's values, or a chosen number of ulps from them, or other values.
    c.soundPosition = rng() % 5 ? Nudged(c.position, rng() % 4, kUlpSteps[rng() % 7]) : RollMemberPoint(rng);
    c.soundFacing = rng() % 5 ? Nudged(c.facing, rng() % 4, kUlpSteps[rng() % 7]) : RollMemberPoint(rng);
    c.soundRoom = rng() % 6 ? c.room : c.room + 1;
    c.soundFloor = rng() % 6 ? c.floor : c.floor - 1;
    static const float kSoundAudible[] = {0.0f, 1.0f, -0.0f, 0.5f, FromBits(0x7fc00000)};
    c.soundAudibility = rng() % 4 ? c.audibility : kSoundAudible[rng() % 5];
    if (c.soundAudibility != c.soundAudibility) c.audibility = c.soundAudibility; // NaN on both
    c.soundHidden = uint8_t(rng() % 3);
    if (c.op == 3 && rng() % 2) {
        // mostly unchanged but for one thing, so each comparison decides alone
        c.soundPosition = Nudged(c.position, rng() % 4, kUlpSteps[rng() % 7]);
        c.soundFacing = rng() % 2 ? c.facing : Nudged(c.facing, rng() % 3, kUlpSteps[rng() % 7]);
        c.soundRoom = c.room;
        c.soundFloor = c.floor;
        c.soundAudibility = c.audibility;
        switch (rng() % 4) {
        case 0: c.soundRoom = c.room ^ 1; break;
        case 1: c.soundFloor = c.floor + 1; break;
        case 2: c.soundAudibility = c.audibility == 0.0f ? 1.0f : 0.0f; break;
        default: break;
        }
    }
    return c;
}

struct MemberObjects {
    void *demon;
    tMonitorEntry *monitor;
    tPlayingSound *sound;
};

MemberObjects BuildMembers(difftest::Context &ctx, const MemberCase &c, bool ours)
{
    MemberObjects o{};
    gFactory = false;
    o.demon = Build(ctx, ours);
    Slot<uint32_t>(o.demon, 1);
    auto *const demon = static_cast<Demon *>(o.demon);
    demon->mModeWord5c = c.demonFloor;

    void *memory = ::operator new(tMonitorEntry::kSize);
    std::memset(memory, 0xc5, tMonitorEntry::kSize);
    if (ours) o.monitor = ::new (memory) tMonitorEntry(demon, c.objectId);
    else
        o.monitor = static_cast<tMonitorEntry *>(ctx.Original<void *(__thiscall *)(void *, void *, uint32_t)>(0x2291ac)(
            memory, demon, c.objectId));
    tMonitorEntry &m = *o.monitor;
    m.mRefCount = 1; // the case's own reference: the sound's release never destroys it
    if (c.object) m.mObject.Assign(reinterpret_cast<cIGZUnknown *>(&gW.objects[c.objectId % 3]));
    if (c.model) m.mModel.Assign(&gW.product);
    if (c.lot) m.mLot.Assign(reinterpret_cast<cIGZUnknown *>(&F(kLot)));
    m.mEnabled = c.enabled;
    m.mRoom = c.room;
    m.mFloor = c.floor;
    m.mAudibility = c.audibility;
    m.mHidden = c.hidden;
    m.mCentre = c.centre;
    m.mPosition = c.position;
    m.mFacing = c.facing;
    m.mUpdatedAt = 0x1111;
    m.mDistance = 2.5f;

    memory = ::operator new(tPlayingSound::kSize);
    std::memset(memory, 0xc5, tPlayingSound::kSize);
    if (ours) o.sound = ::new (memory) tPlayingSound(demon);
    else o.sound = static_cast<tPlayingSound *>(ctx.Original<void *(__thiscall *)(void *, void *)>(0x229228)(memory, demon));
    tPlayingSound &s = *o.sound;
    s.mSoundId = 0x600 + c.objectId;
    if (c.sound) s.SetMonitor(o.monitor);
    s.mPosition = c.soundPosition;
    s.mFacing = c.soundFacing;
    s.mRoom = c.soundRoom;
    s.mFloor = c.soundFloor;
    s.mAudibility = c.soundAudibility;
    s.mHidden = c.soundHidden;

    F(kApp).ret[26] = c.worldApp ? P(&F(kWorld)) : 0;
    F(kScene).ret[17] = c.scene ? P(&gW.scene) : 0;
    F(kScene).ret[18] = c.view ? P(&gW.view) : 0;
    return o;
}

void RunMember(difftest::Context &ctx, Blob &b, const MemberCase &c, MemberObjects &o, bool ours)
{
    tMonitorEntry *const m = o.monitor;
    switch (c.op) {
    case 0:
        if (ours) m->UpdatePlacement();
        else ctx.Original<void(__thiscall *)(void *)>(0x22a5ad)(m);
        break;
    case 1:
        if (ours) m->Step();
        else ctx.Original<void(__thiscall *)(void *)>(0x2299b7)(m);
        break;
    case 2: {
        AudioRay ray;
        std::memset(&ray, 0xc5, sizeof(ray));
        const bool aimed =
            ours ? m->AimRay(&ray) : ctx.Original<bool(__thiscall *)(void *, AudioRay *)>(0x2296d2)(m, &ray);
        b.push_back(aimed);
        for (uint32_t i = 0; i < 8; ++i) b.push_back(Word(reinterpret_cast<const uint8_t *>(&ray), 4 * i));
        break;
    }
    case 3:
        if (ours) o.sound->Update();
        else ctx.Original<void(__thiscall *)(void *)>(0x22a119)(o.sound);
        break;
    default: {
        AudioPoint p[2];
        std::memset(p, 0xc5, sizeof(p));
        if (ours) {
            p[0] = m->Position();
            p[1] = m->Facing();
            b.push_back(m->Hidden());
            b.push_back(svcfx::Norm(P(tMonitorEntry::View())));
            b.push_back(svcfx::Norm(P(tMonitorEntry::LotScene())));
        } else {
            b.push_back(svcfx::Norm(P(ctx.Original<AudioPoint *(__thiscall *)(void *, AudioPoint *)>(0x229e85)(m, &p[0]))));
            b.push_back(svcfx::Norm(P(ctx.Original<AudioPoint *(__thiscall *)(void *, AudioPoint *)>(0x2297af)(m, &p[1]))));
            b.push_back(ctx.Original<uint32_t(__thiscall *)(void *)>(0x229d15)(m));
            b.push_back(svcfx::Norm(ctx.Original<uint32_t(__thiscall *)(void *)>(0x22ab53)(m)));
            b.push_back(svcfx::Norm(ctx.Original<uint32_t(__thiscall *)(void *)>(0x229f1d)(m)));
            b.erase(b.end() - 5, b.end() - 3); // the out pointers they return (their frame's): checked against p below
        }
        DumpPoint(b, p[0]);
        DumpPoint(b, p[1]);
        break;
    }
    }
}

void DestroyMembers(difftest::Context &ctx, Blob &b, MemberObjects &o, bool ours)
{
    if (ours) {
        o.sound->~tPlayingSound();
        o.monitor->~tMonitorEntry();
    } else {
        ctx.Original<void(__thiscall *)(void *)>(0x229374)(o.sound);
        ctx.Original<void(__thiscall *)(void *)>(0x2292e9)(o.monitor);
    }
    ::operator delete(static_cast<void *>(o.sound));
    ::operator delete(static_cast<void *>(o.monitor));
    b.push_back(Slot<uint32_t>(o.demon, 2)); // the last release destroys it
}

} // namespace audiodemon_test

using namespace audiodemon_test;

namespace {

// The patches and cells both tests need.
struct Fixture {
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    shape::CodePatch gameNew{0x824632, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624, shape::CodeOf(&GameDelete)};
    FilledHeap filledHeap; // ours' allocations filled as GameNew fills the original's
    shape::CodePatch restart{0x1c95ec, shape::CodeOf(&StandRestart)};
    shape::StandIn findNode{0xae7490, &nGZSceneGraph::FindNodeByName, &StandFindNode};
    shape::StandIn bounds{0xae7d00, &nGZSceneGraph::GrowByShapeBounds, &StandBounds};
    shape::StandIn walk5{0x1c0458, &KeyMapSlot5Walker::CallSlot5ForEachKey, &StandWalk5};
    shape::StandIn walk6{0x1c081e, &KeyMapWalker::CallSlot6ForEachKey, &StandWalk6};
    shape::StandIn elapsed{0x1c9256, &cRZStopwatch::Elapsed, &StandElapsed};

    Fixture()
    {
        char traceFlag[4];
        svcfx::gTrace = GetEnvironmentVariableA("TS2_TEST_TRACE", traceFlag, sizeof(traceFlag)) != 0;
        for (uint32_t cell : {0x10c9378u, 0x10f5194u, svcfx::kAppCell, 0x10cc458u, 0x10f51b4u, 0x10cc5a0u, 0x10cc5a4u,
                              0x10cc5a8u, 0x10cc648u})
            globals.SetWord(cell, 0);
        svcfx::gFrameworkIndex = kCom; // the director's framework (GetDirectorFrameworkObject): the event maker
        BuildVtables();
    }
};

bool Compare(difftest::Context &ctx, const std::vector<Blob> (&results)[2], const char *name, uint32_t seed,
             const char *what)
{
    for (size_t i = 0; i < results[0].size(); ++i) {
        if (!svcfx::Same(ctx, results[0][i], results[1][i], name, seed)) {
            if (svcfx::gTrace)
                for (size_t w = 0; w < results[0][i].size() || w < results[1][i].size(); ++w)
                    fprintf(stderr, "%3u %08x %08x\n", unsigned(w), w < results[0][i].size() ? results[0][i][w] : 0,
                            w < results[1][i].size() ? results[1][i][w] : 0);
            ctx.Fail("  at step %u (%s)", unsigned(i), what);
            return false;
        }
    }
    return true;
}

} // namespace

DIFF_TEST(AudioObjectUpdateDemon_Run, 0x22910e)
{
    // Also reached (see the file comment): 0x229c42 (factory), 0x2292a0, 0x2293d9, 0x2293d1, 0x229e98, 0x229eba, 0x229ec2,
    // 0x229eca, 0x229d1e, 0x229dc0, 0x229fbc, 0x22a007, 0x229e45, 0x2297c2, 0x229436, 0x22955b, 0x229ed2, 0x229c85,
    // 0x229aff, 0x229b2c, 0x229b64, 0x229bd3, 0x229d02, 0x22a593, 0x22995a, 0x22a05e, 0x22ab39, 0x22a07f, 0x2291ac,
    // 0x229dc8, 0x22a091, 0x22a5ad, 0x2299b7, 0x2296d2, 0x22ab53, 0x229f1d, 0x22a00f, 0x2292e9, 0x2293f8, 0x229228,
    // 0x22a119, 0x229e85, 0x2297af, 0x229d15, 0x229f6e (with 0x229f15, the monitor's sound count down, inline there),
    // 0x22a044, 0x229374, 0x229417.
    Fixture fixture;
    if (!gRealHeapAlloc) return ctx.Fail("this exe imports no HeapAlloc to fill ours' allocations through");
    for (uint32_t seed = 1; seed <= 2000; ++seed) {
        const std::vector<Op> ops = MakeOps(seed);
        gFactory = seed % 4 == 0;
        std::vector<Blob> results[2];
        for (int side = 0; side < 2; ++side) {
            BuildFakes(seed);
            gPlaced[side].clear();
            void *object = Build(ctx, side == 1);
            svcfx::AddRange(object, Demon::kSize, kTag);
            Slot<uint32_t>(object, 1); // the owner's reference
            for (size_t i = 0; i <= ops.size(); ++i) {
                Blob b;
                gRng.seed(seed * 1000 + uint32_t(i) + 17);
                if (i < ops.size()) {
                    if (svcfx::gTrace) fprintf(stderr, "seed %u side %d op %u kind %u\n", seed, side, unsigned(i), ops[i].kind);
                    const std::vector<void *> held = HoldMonitors(object);
                    RunOp(b, object, ops[i]);
                    ReleaseMonitors(held);
                    Dump(b, object, side);
                } else {
                    // The monitors hold the demon: cleared first (0xb2), then destroyed.
                    const std::vector<void *> held = HoldMonitors(object);
                    b.push_back(SendMessage(Face(object, 4), 0xb2, 0, 1));
                    ReleaseMonitors(held);
                    Dump(b, object, side);
                    if (seed % 5 == 0) {
                        // The +0xc face's deleting destructor (thunk 0x2293d1) with flags 0: destroyed, not freed.
                        Slot<uint32_t>(Face(object, 0xc), 3, 0u);
                        b.push_back(Word(static_cast<uint8_t *>(object), 0x10));
                        ::operator delete(object);
                    } else {
                        b.push_back(Slot<uint32_t>(object, 2)); // the last release destroys it
                    }
                }
                b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
                svcfx::gEv.clear();
                CaptureWorld(b);
                results[side].push_back(std::move(b));
            }
            svcfx::gRanges.clear();
        }
        if (!Compare(ctx, results, "AudioObjectUpdateDemon_Run", seed, "op")) return;
    }
}

DIFF_TEST(AudioObjectUpdateDemon_Members, 0x22a5ad)
{
    // Reached: 0x22a5ad, 0x2299b7, 0x2296d2, 0x22a119, 0x229e85, 0x2297af, 0x229d15, 0x22ab53, 0x229f1d, with 0x2291ac,
    // 0x229228, 0x2292e9, 0x229374, 0x229f6e and 0x22a07f around them.
    Fixture fixture;
    if (!gRealHeapAlloc) return ctx.Fail("this exe imports no HeapAlloc to fill ours' allocations through");
    unsigned counts[5] = {};
    for (uint32_t seed = 1; seed <= 30000; ++seed) {
        std::vector<Blob> results[2];
        MemberCase c{};
        for (int side = 0; side < 2; ++side) {
            const bool ours = side == 1;
            BuildFakes(seed);
            c = MakeCase(seed);
            gScript.on = true;
            MemberObjects o = BuildMembers(ctx, c, ours);
            svcfx::AddRange(o.demon, Demon::kSize, kTag);
            svcfx::AddRange(o.monitor, tMonitorEntry::kSize, 0x7c000000);
            svcfx::AddRange(o.sound, tPlayingSound::kSize, 0x7d000000);
            svcfx::gEv.clear();
            gRng.seed(seed * 13 + 1);
            Blob b;
            RunMember(ctx, b, c, o, ours);
            DumpMonitor(b, *o.monitor, ours, true);
            MonitorList none;
            DumpSound(b, *o.sound, none, ours);
            b.push_back(Bits(gScript.place.position.x)); // untouched
            b.insert(b.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            svcfx::gEv.clear();
            CaptureWorld(b);
            results[side].push_back(std::move(b));
            Blob end;
            DestroyMembers(ctx, end, o, ours);
            end.insert(end.end(), svcfx::gEv.begin(), svcfx::gEv.end());
            svcfx::gEv.clear();
            CaptureWorld(end);
            results[side].push_back(std::move(end));
            svcfx::gRanges.clear();
        }
        ++counts[c.op];
        if (!Compare(ctx, results, "AudioObjectUpdateDemon_Members", seed, c.op == 0   ? "placement"
                                                                            : c.op == 1 ? "step"
                                                                            : c.op == 2 ? "aim"
                                                                            : c.op == 3 ? "sound update"
                                                                                        : "helpers"))
            return;
    }
    if (svcfx::gTrace) fprintf(stderr, "ops %u %u %u %u %u\n", counts[0], counts[1], counts[2], counts[3], counts[4]);
}
