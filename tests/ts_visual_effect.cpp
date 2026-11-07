// cTSVisualEffect (src/ts/cTSVisualEffect.h), lane XD: original vs ours.
//
// Mode 0 builds the effect with the original constructor 0xbe86a0, mode 1 with ours; every slot then runs through the object's
// own vtables (face 0, +4 and +8), so mode 0 runs the image's bodies and thunks, and mode 1 ours. After each step: the result, the
// log of every fake's calls (every call with the ids and the words it was given: floats as bits), the frees by size, all three
// vtable words (as codes) and every field of the object (the vectors by their sizes, capacities and contents).
//
// The effect's own state is built the way the original's other slots (not ours yet) leave it: the description (a flag word and the
// two distance limits), the child records (component effects with a placement, a weight, flags and an index vector), the
// resources, the camera, the blend and the name are written into the object between steps, alike on both sides. Update 0xbe99c0,
// SetResource 0xbea700, the camera follow 0xbead40 and the event index walk 0xbe9590 are not stood in: mode 0 runs the image's
// bodies, mode 1 ours, over fakes with the real layouts. The owner is a manager-shaped block (the effect table at +0x30, the
// description tables at +0x3c, GetParams and GetCamera) with real vectors of components and descriptions; the camera and every
// resource is one kind of node object (QueryInterface for the camera and string interfaces, the world placement through slots 54
// and 140 written to the argument and returned elsewhere as a different array, the matrix object through slot 139); the
// texture handle's name constructor, assignment and destructor (0xae8280, 0xae82e0, 0xae82b0) are stood in on both sides. The
// children log the two blocks they are updated with (the unused byte of a block's flags word, a stack leftover in the original's
// composed copies, is cleared in the log and in the fingerprint). Start's message (0x1c033d and its slots, the real ones on both
// sides) goes to a fake message server, put in the cached cell 0x10f519c, that logs the message's fields. The game allocator is
// plain memory.
//
// Covered RVAs: 0xbe86a0 (constructor), 0xbe8a40 / 0xbe8be0 / 0xbe8bcf (destructor, deleting destructor, its +8 thunk), 0xbe9530,
// 0xbe9560 and their thunks, 0x146a40, 0xbeab90 (Start), 0xbeacd0 (Stop), 0xbe92c0, 0x78e3d0, 0xbe9910, 0xbe98e0, 0xbe96e0,
// 0xbe99a0, 0x332750, 0xbea9e0, 0xbe9700 (event), 0xbe90e0, 0xbe9010, 0xbe90b0, 0xbe8fe0, 0xbe9070, 0xbe92b0, 0xbe92a0, 0xbe9260,
// 0xbe9640, 0xbe9150 (Attach), 0xbeaa00 (Detach), 0xbe8c10 (AddToScene), 0xbe9930 (SetFlagged), 0xbea9d8 (Query thunk), 0xbeacc5,
// 0xbead2d, 0xbe92c4, 0xbe98cb, 0xbe9578, 0xbe9580, and the four that forwarded: 0xbe99c0 (Update, with its helpers 0xbe8f20,
// 0xbe8f90, 0xbe92d0, 0xbe93f0 done inside it), 0xbea700 (SetResource), 0xbead40 (the camera follow), 0xbe9590 (the event index
// walk).
#include "ts_effects_collection.h"

#include "free_recorder.h"
#include "game_allocator.h"
#include "rz/GeneralAllocator.h"
#include "rz/RZMemory.h"
#include "game/game.h"
#include "shape.h"
#include "rz/cRZMessage2Standard.h"
#include "sg/cTextureManager.h"
#include "ts/cTSVisualEffect.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace fxtest;
using fx::EffectsTransform;

constexpr uint32_t kGameTables[3] = {0xe8f7e0, 0xe8f844, 0xe8f880};
using ConstructFn = void *(__thiscall *)(void *);

constexpr uint32_t kCameraIid = 0xe51c10f5, kStringIid = 0x89b7dc8;

enum : uint32_t {
    evChild = 0xc1000000,   // | the slot; the child's id follows
    evSoundService = 0xc6000000,
    evSound = 0xc8000000,   // | the slot; the id follows
    evBad = 0xc9000000,
    evQuery = 0xca000000,   // | the object's id: the interface asked for, the id given
    evObj = 0xcb000000,     // | the slot; the node object's id follows
    evStr = 0xcc000000,     // | the slot; the string's id follows
    evOwnerGet = 0xcd000000, // | the slot; the owner's id follows
    evTex = 0xce000000,     // | 0 name, 1 assign, 2 destroy
};

enum Hit : unsigned {
    hUpdate,
    hUpdFrozen,
    hUpdBlend,
    hUpdFollow,
    hUpdClamp,
    hUpdFlatten,
    hUpdCompose,
    hUpdTurn,
    hUpdAim,
    hUpdUnit,
    hChildUpdate,
    hResource,
    hResCamera,
    hResString,
    hResNoString,
    hResKind4,
    hResTaken,
    hFollow,
    hFolNoCamera,
    hFolNeeds,
    hFolNoParent,
    hIndex,
    hIndexStep,
    hIndexStepRunning,
    hSoundService,
    hSoundNone,
    hChildEvent,
    hChildTaken,
    hAttach,
    hStart,
    hStop,
    hEvent,
    hAddToScene,
    hReplay,
    hReplayIndex,
    hReplayRes,
    hFlatZero,
    hAimNear,
    hCount
};
uint32_t gHit[hCount];

// ---- the world ----

struct Child {
    void *const *vt;
    uint32_t id, refs, running;
};
struct Snd {
    void *const *vt;
    uint32_t id, refs;
};
// A string object (cIGZString's slots 1, 2 and 5 are all the effect uses).
struct Str {
    void *const *vt;
    uint32_t id, refs;
    const char *text;
};
// A node object: the camera, and every resource the effect is given. The first four words are a FakeNode's.
struct Obj {
    void *const *vt;
    uint32_t id, refs;
    FakeNode *parent;
    uint32_t hasParent; // slot 34's answer
    uint32_t qiWeird;   // QueryInterface's answer is the opposite of what it did
    Obj *qiCam;         // what QueryInterface gives for the camera interface (null: nothing)
    Str *qiStr;         // ... and for the string interface
    float world[7], worldAlt[7]; // slot 54: written to the argument / the array it returns
    float rigid[7], rigidAlt[7]; // slot 140
    uint8_t matrixObject[0x74 + 64]; // slot 139: the object whose 4x4 matrix is at +0x74
};
// The effect manager as the effect sees it (the owner on its +4 face): the base is a FakeOwner, the vectors the manager's.
struct Owner {
    FakeOwner base;
    uint32_t pad[7];
    uint32_t effects[3]; // +0x30: the effect table (a vector)
    uint32_t tables[3];  // +0x3c: the description tables (a vector)
    float params[4];     // GetParams' block: the progress at +0xc
    Obj *camera;
};
static_assert(offsetof(Owner, effects) == 0x30 && offsetof(Owner, tables) == 0x3c);
struct Manager {
    uint8_t effects[3][0x28];     // EffectEntry: the components vector at +0x1c
    uint8_t comps[3][4][0x88];    // EffectComponent: the type at +0x18, the index at +0x84
    uint8_t tables[3][0x2c];      // EffectDescriptionTable: the descriptions vector at +0x20
    uint32_t descs[3][5];
    FakeDescription descriptions[8];
};

std::mt19937 gAnswers;
const uint8_t *gBase;
const void *gExt[2];
const FakeDescription *gDescs;

uint32_t Off(const void *p)
{
    if (!p) return 0;
    if (p >= gBase && p < gBase + sizeof(cTSVisualEffect)) return uint32_t(static_cast<const uint8_t *>(p) - gBase);
    if (p == gExt[0]) return 0x1000;
    if (p == gExt[1]) return 0x1001;
    return 0xbadbad;
}

uint32_t HashBytes(const void *p, size_t n)
{
    uint32_t h = 2166136261u;
    if (!p) return h;
    for (size_t i = 0; i < n; ++i) h = (h ^ static_cast<const uint8_t *>(p)[i]) * 16777619u;
    return h;
}
// A placement block's bytes without its flags word's unused byte (+1).
uint32_t HashBlock(const void *p)
{
    uint8_t copy[0x38];
    std::memcpy(copy, p, sizeof(copy));
    copy[1] = 0;
    return HashBytes(copy, sizeof(copy));
}
uint32_t DescId(uint32_t word)
{
    const auto *d = reinterpret_cast<const FakeDescription *>(uintptr_t(word));
    return d >= gDescs && d < gDescs + 8 ? d->id : 0xbadbadu;
}

constexpr uint32_t kTrue = 0xabcdef01u, kFalse = 0xabcdef00u; // only al counts in a bool

// The child component effects (cITSComponentEffect's 14 slots).
uint32_t __fastcall ChildAddRef(Child *c, void *)
{
    Ev({evChild | 12, c->id});
    return ++c->refs;
}
uint32_t __fastcall ChildRelease(Child *c, void *)
{
    Ev({evChild | 13, c->id});
    return --c->refs;
}
void __fastcall ChildAttach(Child *c, void *, void *owner) { Ev({evChild | 0, c->id, IdOf(owner)}); }
void __fastcall ChildDetach(Child *c, void *) { Ev({evChild | 1, c->id}); }
void __fastcall ChildStart(Child *c, void *, uint32_t a)
{
    Ev({evChild | 2, c->id, a});
    c->running = gAnswers() % 3 != 0;
}
void __fastcall ChildStop(Child *c, void *, uint32_t a)
{
    Ev({evChild | 3, c->id, a});
    if (a != 0) c->running = 0;
}
uint32_t __fastcall ChildIsRunning(Child *c, void *)
{
    Ev({evChild | 4, c->id});
    return c->running ? kTrue : kFalse;
}
void __fastcall ChildAddToScene(Child *c, void *, void *time, void *counters)
{
    Ev({evChild | 5, c->id, uint32_t(reinterpret_cast<uintptr_t>(time)), HashBytes(counters, 8)});
}
void __fastcall ChildUpdate(Child *c, void *, const void *local, const void *parent)
{
    Ev({evChild | 6, c->id, Off(local), HashBlock(local), Off(parent), HashBlock(parent)});
    ++gHit[hChildUpdate];
}
uint32_t __fastcall ChildHandleEvent(Child *c, void *, uint32_t kind, void *values, void *count)
{
    const uint32_t n = uint32_t(reinterpret_cast<uintptr_t>(count));
    Ev({evChild | 7, c->id, kind, n,n && n < 100 ? HashBytes(values, n * 4) : 0});
    ++gHit[hChildEvent];
    const bool taken = gAnswers() % 4 == 0;
    gHit[hChildTaken] += taken;
    return taken ? kTrue : kFalse;
}
uint32_t __fastcall ChildQuery(Child *c, void *, void *kind, void *resource)
{
    Ev({evChild | 8, c->id, uint32_t(reinterpret_cast<uintptr_t>(kind)), IdOf(resource)});
    const bool taken = gAnswers() % 3 == 0;
    gHit[hResTaken] += taken;
    return taken ? kTrue : kFalse;
}
void __fastcall ChildSetFlagged(Child *c, void *, uint32_t on) { Ev({evChild | 9, c->id, on & 0xff}); }
void __fastcall ChildOther10(Child *c, void *, uint32_t a) { Ev({evChild | 10, c->id, a}); }
// Slot 11 is given a description (the index walk).
void __fastcall ChildOther11(Child *c, void *, uint32_t a)
{
    Ev({evChild | 11, c->id, DescId(a)});
    ++gHit[hIndex];
}

void *gChildVT[14];

// The sound service and the sound object (slots called through our Start).
void __fastcall SvcSlot4(Snd *s, void *, void *object, uint32_t b)
{
    const auto *m = static_cast<const cRZMessage2Standard *>(object);
    Ev({evSound | 4, s->id, b, m->GetId(), m->HasValue0(), m->GetValue0(), m->HasValue1(), m->GetValue1(), m->HasValue2(), m->HasValue3(),
        m->HasPtr20(), m->HasPtr24(), m->HasExtra()});
    ++gHit[hSoundService];
}
void __fastcall SndBad(Snd *s, void *) { Ev({evBad, s->id}); }

void *gServiceVT[40];
Snd gService{gServiceVT, 0x90, 0};

// The node objects (the camera and the resources): QueryInterface at slot 0, the counts at 1 and 2, the parent test at 34, the
// world placement at 54 and 140 and the matrix object at 139; the other slots log and answer 0.
uint32_t __fastcall ObjQuery(Obj *o, void *, uint32_t iid, void **out)
{
    void *answer = iid == kCameraIid ? static_cast<void *>(o->qiCam) : iid == kStringIid ? static_cast<void *>(o->qiStr) : nullptr;
    Ev({evQuery | o->id, iid, IdOf(answer)});
    if (answer) {
        ++static_cast<FakeNode *>(answer)->refs;
        *out = answer;
        if (iid == kCameraIid) ++gHit[hResCamera];
    }
    return (answer != nullptr) != (o->qiWeird != 0) ? kTrue : kFalse;
}
uint32_t __fastcall ObjHasParent(Obj *o, void *)
{
    Ev({evObj | 34, o->id});
    if (!o->hasParent) ++gHit[hFolNoParent];
    return o->hasParent;
}
float *__fastcall ObjWorld(Obj *o, void *, float *out)
{
    Ev({evObj | 54, o->id});
    std::memcpy(out, o->world, sizeof(o->world));
    return o->worldAlt;
}
float *__fastcall ObjRigid(Obj *o, void *, float *out, int zero)
{
    Ev({evObj | 140, o->id, uint32_t(zero)});
    std::memcpy(out, o->rigid, sizeof(o->rigid));
    return o->rigidAlt;
}
uint8_t *__fastcall ObjMatrix(Obj *o, void *, int zero)
{
    Ev({evObj | 139, o->id, uint32_t(zero)});
    return o->matrixObject;
}
constexpr unsigned kObjSlots = 141;
void *gObjVT[kObjSlots];
template <size_t... I> void FillObjOthers(std::index_sequence<I...>)
{
    ((gObjVT[I] = reinterpret_cast<void *>(&NodeOther<unsigned(I)>)), ...);
}

// The strings.
uint32_t __fastcall StrAddRef(Str *s, void *)
{
    Ev({evStr | 1, s->id});
    return ++s->refs;
}
uint32_t __fastcall StrRelease(Str *s, void *)
{
    Ev({evStr | 2, s->id});
    return --s->refs;
}
const char *__fastcall StrToChar(Str *s, void *)
{
    Ev({evStr | 5, s->id});
    ++gHit[hResString];
    return s->text;
}
uint32_t __fastcall StrBad(Str *s, void *)
{
    Ev({evBad, s->id});
    return 0;
}
void *gStrVT[16];

// The owner (the effect manager): GetParams (slot 21) and GetCamera (slot 43).
void *__fastcall OwnerGetParams(Owner *o, void *)
{
    Ev({evOwnerGet | 21, o->base.id});
    return o->params;
}
Obj *__fastcall OwnerGetCamera(Owner *o, void *)
{
    Ev({evOwnerGet | 43, o->base.id});
    return o->camera;
}
void *gOwnerVT[kFakeOwnerSlots];

// The texture handle (0xae8280 from a name, 0xae82e0 assignment, 0xae82b0 destructor): no texture manager, ids from the name.
void *__fastcall StandHandleFromName(uint32_t *self, void *, const char *name)
{
    const uint32_t hash = HashBytes(name, std::strlen(name) + 1);
    Ev({evTex | 0, Off(self), hash});
    *self = 0x7e000000u | (hash & 0xffffff);
    return self;
}
void *__fastcall StandHandleAssign(uint32_t *self, void *, const uint32_t *other)
{
    Ev({evTex | 1, Off(self), *other});
    *self = *other;
    return self;
}
void __fastcall StandHandleDestroy(uint32_t *self, void *)
{
    Ev({evTex | 2, Off(self), *self});
    *self = 0xdddddddd;
}

// The game's allocator (RZMemory's Malloc 0x1be3b0 and Free 0x1be3a1, which the message is made and freed by) is uninitialised in
// the harness: one test GeneralAllocator behind the image's functions and our RZMemory (as tests/rz_message2_standard_rest.cpp).
GeneralAllocator *gAlloc = nullptr;
void *__cdecl StandMalloc(uint32_t bytes) { return gAlloc->Malloc(bytes, 0); }
void __cdecl StandFree(void *p) { gAlloc->Free(p); }

struct Stands {
    shape::CodePatch malloc{0x1be3b0u, shape::CodeOf(&StandMalloc)};
    shape::CodePatch free{0x1be3a1u, shape::CodeOf(&StandFree)};
    alignas(16) uint8_t allocatorMemory[sizeof(GeneralAllocator)] = {};
    Stands()
    {
        gAlloc = reinterpret_cast<GeneralAllocator *>(allocatorMemory);
        gAlloc->Construct(nullptr, 0, 0, 0, nullptr, nullptr);
        RZMemory::testGlobalAllocator = gAlloc;
    }
    Stands(const Stands &) = delete;
    ~Stands()
    {
        RZMemory::testGlobalAllocator = nullptr;
        difftest_alloc::ShutdownAndRelease(gAlloc);
        gAlloc = nullptr;
    }
    shape::StandIn texName{0xae8280u, &nGZSceneGraph::cTextureHandle::InitFromName, &StandHandleFromName};
    shape::StandIn texAssign{0xae82e0u, &nGZSceneGraph::cTextureHandle::Assign, &StandHandleAssign};
    shape::StandIn texDestroy{0xae82b0u, &nGZSceneGraph::cTextureHandle::Destroy, &StandHandleDestroy};
};

struct World {
    Owner owners[3];
    Obj nodes[8];
    Str strs[3];
    Child children[6];
    uint32_t description[16];
    Manager manager;
    EffectsTransform transforms[2];
};

void FillTables()
{
    (void)gFakeTablesFilled;
    static bool done = false;
    if (done) return;
    done = true;
    gChildVT[0] = reinterpret_cast<void *>(&ChildAttach);
    gChildVT[1] = reinterpret_cast<void *>(&ChildDetach);
    gChildVT[2] = reinterpret_cast<void *>(&ChildStart);
    gChildVT[3] = reinterpret_cast<void *>(&ChildStop);
    gChildVT[4] = reinterpret_cast<void *>(&ChildIsRunning);
    gChildVT[5] = reinterpret_cast<void *>(&ChildAddToScene);
    gChildVT[6] = reinterpret_cast<void *>(&ChildUpdate);
    gChildVT[7] = reinterpret_cast<void *>(&ChildHandleEvent);
    gChildVT[8] = reinterpret_cast<void *>(&ChildQuery);
    gChildVT[9] = reinterpret_cast<void *>(&ChildSetFlagged);
    gChildVT[10] = reinterpret_cast<void *>(&ChildOther10);
    gChildVT[11] = reinterpret_cast<void *>(&ChildOther11);
    gChildVT[12] = reinterpret_cast<void *>(&ChildAddRef);
    gChildVT[13] = reinterpret_cast<void *>(&ChildRelease);
    for (void *&slot : gServiceVT) slot = reinterpret_cast<void *>(&SndBad);
    gServiceVT[4] = reinterpret_cast<void *>(&SvcSlot4);
    FillObjOthers(std::make_index_sequence<kObjSlots>());
    gObjVT[0] = reinterpret_cast<void *>(&ObjQuery);
    gObjVT[1] = reinterpret_cast<void *>(&NodeAddRef);
    gObjVT[2] = reinterpret_cast<void *>(&NodeRelease);
    gObjVT[34] = reinterpret_cast<void *>(&ObjHasParent);
    gObjVT[54] = reinterpret_cast<void *>(&ObjWorld);
    gObjVT[139] = reinterpret_cast<void *>(&ObjMatrix);
    gObjVT[140] = reinterpret_cast<void *>(&ObjRigid);
    for (void *&slot : gStrVT) slot = reinterpret_cast<void *>(&StrBad);
    gStrVT[1] = reinterpret_cast<void *>(&StrAddRef);
    gStrVT[2] = reinterpret_cast<void *>(&StrRelease);
    gStrVT[5] = reinterpret_cast<void *>(&StrToChar);
    std::memcpy(gOwnerVT, gFakeOwnerVT, sizeof(gOwnerVT));
    gOwnerVT[21] = reinterpret_cast<void *>(&OwnerGetParams);
    gOwnerVT[43] = reinterpret_cast<void *>(&OwnerGetCamera);
}

float Value(std::mt19937 &r)
{
    switch (r() % 6) {
    case 0: return r() % 3 == 0 ? -0.0f : 0.0f; // a negative zero matters where a sum starts from nothing
    case 1: return 1.0f;
    case 2: return 0.5f;
    case 3: return float(int(r() % 9) - 4) + (r() % 2 ? 0.5f : 0.0f);
    default: return testhelp::FewOrUniform(r, -3.0f, 3.0f);
    }
}

uint32_t Word(float f) { return Bits(f); }

// A node's placements: the world one (translation, then the rotation's x y z w) and what slot 54 returns instead of the
// argument: the same, a moved translation, the negated rotation or one ulp off (the follow's cache tests); the rigid one for slot
// 140 and its returned copy; the matrix object's 4x4.
void Place(Obj &n, std::mt19937 &r)
{
    const auto unit = [&r](float *q) {
        float len = 0;
        for (int i = 0; i < 4; ++i) {
            q[i] = testhelp::FewOrUniform(r, -1.0f, 1.0f);
            len += q[i] * q[i];
        }
        if (len == 0.0f) q[3] = 1.0f, len = 1.0f;
        const float k = 1.0f / std::sqrt(len);
        for (int i = 0; i < 4; ++i) q[i] *= k;
    };
    for (int i = 0; i < 3; ++i) n.world[i] = Value(r) * 2.0f;
    unit(n.world + 3);
    std::memcpy(n.worldAlt, n.world, sizeof(n.world));
    switch (r() % 4) {
    case 1: n.worldAlt[0] += 0.5f; break;
    case 2:
        for (int i = 3; i < 7; ++i) n.worldAlt[i] = -n.worldAlt[i];
        break;
    case 3:
        for (int i = 0; i < 7; ++i) n.worldAlt[i] = std::bit_cast<float>(std::bit_cast<uint32_t>(n.worldAlt[i]) + 1);
        break;
    default: break;
    }
    for (int i = 0; i < 3; ++i) n.rigid[i] = Value(r) * 2.0f;
    if (r() % 4 == 0) n.rigid[0] = n.rigid[1] = n.rigid[2] = 0.0f; // at the origin: an aim from there has no sideways direction
    unit(n.rigid + 3);
    if (r() % 2) {
        std::memcpy(n.rigidAlt, n.rigid, sizeof(n.rigid));
    } else {
        for (int i = 0; i < 3; ++i) n.rigidAlt[i] = Value(r) * 2.0f;
        unit(n.rigidAlt + 3);
    }
    float matrix[16];
    for (int i = 0; i < 16; ++i) matrix[i] = Value(r);
    for (int i = 0; i < 4; ++i) matrix[i * 5] += 1.0f;
    std::memset(n.matrixObject, 0x5a, sizeof(n.matrixObject));
    std::memcpy(n.matrixObject + 0x74, matrix, sizeof(matrix));
}

void Reset(World &w, uint32_t seed)
{
    FillTables();
    std::mt19937 r(seed * 977u + 1);
    for (uint32_t &x : w.description) x = r();
    gAnswers.seed(seed * 13u + 7);
    gEv.clear();
    gEv.reserve(1 << 20);
    std::memset(gHit, 0, sizeof(gHit));
    for (uint32_t i = 0; i < 3; ++i) {
        w.strs[i] = Str{gStrVT, 0xa0 + i, 0, i == 0 ? "smoke_texture" : i == 1 ? "" : "a/much/longer/texture/name/that/is/not/short"};
    }
    for (uint32_t i = 0; i < 8; ++i) {
        Obj &n = w.nodes[i];
        std::memset(&n, 0, sizeof(n));
        n.vt = gObjVT;
        n.id = 0x50 + i;
        static const uint32_t kParent[] = {0, 1, 1, 1, 1, kTrue, kFalse};
        n.hasParent = kParent[r() % 7];
        n.qiWeird = r() % 7 == 0;
        n.qiCam = r() % 4 ? &w.nodes[r() % 8] : nullptr;
        n.qiStr = r() % 4 ? &w.strs[r() % 3] : nullptr;
        Place(n, r);
    }
    std::memset(&w.manager, 0, sizeof(w.manager));
    for (uint32_t i = 0; i < 8; ++i) {
        w.manager.descriptions[i] = FakeDescription{kFakeDescriptionVT, 0x60 + i, int32_t(i), 0};
    }
    gDescs = w.manager.descriptions;
    for (uint32_t e = 0; e < 3; ++e) {
        auto *entry = reinterpret_cast<uint32_t *>(w.manager.effects[e]);
        entry[7] = uint32_t(reinterpret_cast<uintptr_t>(w.manager.comps[e][0]));
        entry[8] = uint32_t(reinterpret_cast<uintptr_t>(w.manager.comps[e][4]));
        entry[9] = entry[8];
        for (uint32_t k = 0; k < 4; ++k) {
            auto *comp = reinterpret_cast<uint32_t *>(w.manager.comps[e][k]);
            comp[6] = 1 + r() % 3;
            comp[0x84 / 4] = r() % 5;
        }
        auto *table = reinterpret_cast<uint32_t *>(w.manager.tables[e]);
        table[8] = uint32_t(reinterpret_cast<uintptr_t>(w.manager.descs[e]));
        table[9] = uint32_t(reinterpret_cast<uintptr_t>(w.manager.descs[e] + 5));
        table[10] = table[9];
        for (uint32_t k = 0; k < 5; ++k) {
            w.manager.descs[e][k] = r() % 6 ? uint32_t(reinterpret_cast<uintptr_t>(&w.manager.descriptions[r() % 8])) : 0;
        }
    }
    for (uint32_t i = 0; i < 3; ++i) {
        Owner &o = w.owners[i];
        std::memset(&o, 0, sizeof(o));
        o.base = FakeOwner{gOwnerVT, 0x20 + i, 0, nullptr, 0};
        o.effects[0] = uint32_t(reinterpret_cast<uintptr_t>(w.manager.effects[0]));
        o.effects[1] = uint32_t(reinterpret_cast<uintptr_t>(w.manager.effects[3 - 0])); // one past the table
        o.effects[2] = o.effects[1];
        o.tables[0] = uint32_t(reinterpret_cast<uintptr_t>(w.manager.tables[0]));
        o.tables[1] = uint32_t(reinterpret_cast<uintptr_t>(w.manager.tables[3 - 0]));
        o.tables[2] = o.tables[1];
        for (float &p : o.params) p = Value(r);
        o.camera = r() % 4 ? &w.nodes[r() % 8] : nullptr;
    }
    for (uint32_t i = 0; i < 6; ++i) w.children[i] = Child{gChildVT, 0x70 + i, 0, 0};
    gService = Snd{gServiceVT, 0x90, 0};
}

// ---- the object's fingerprint ----

struct OurTables {
    void **face[3];
};
uint32_t TableCode(uint32_t word)
{
    static const uint32_t kOffsets[3] = {0, 4, 8};
    const shape::OurTables<3> &ours = shape::Ours<cTSVisualEffect, 3>(kOffsets);
    return shape::VtCode(word, 3, shape::GameTables{kGameTables}, ours.face);
}

const uint32_t *Ptr(uint32_t word) { return reinterpret_cast<const uint32_t *>(uintptr_t(word)); }

void PushObject(Blob &b, const uint8_t *o, const World &w)
{
    const auto *x = reinterpret_cast<const uint32_t *>(o);
    for (int f = 0; f < 3; ++f) b.push_back(TableCode(x[f]));
    for (int i = 3; i < 5; ++i) b.push_back(x[i]);
    // The string: its size and capacity, and the text (inline up to 15 characters, else through the pointer).
    b.push_back(x[9]);
    b.push_back(x[10]);
    {
        const uint8_t *text = x[10] < 16 ? o + 0x14 : reinterpret_cast<const uint8_t *>(uintptr_t(x[5]));
        const uint32_t n = x[9] < 64 ? x[9] : 64;
        for (uint32_t i = 0; i <= n; ++i) b.push_back(text[i]);
    }
    for (int i = 11; i < 16; ++i) b.push_back(x[i]);
    // The description by its place in the world's block.
    b.push_back(x[16] ? uint32_t(x[16] - uint32_t(reinterpret_cast<uintptr_t>(w.description))) : 0xffffffffu);
    // The flags, parameters and the four placement blocks (A +0x50, B +0x88, C +0xc0, D +0xf8; each one's flags word without its
    // unused second byte).
    for (int i = 17; i < 76; ++i) b.push_back(i == 20 || i == 34 || i == 48 || i == 62 ? x[i] & ~0xff00u : x[i]);
    // Children: size, capacity, and each record's fields.
    const uint32_t cb = x[76];
    b.push_back(cb ? (x[77] - cb) / 0x54 : 0xffffffffu);
    b.push_back(cb ? (x[78] - cb) / 0x54 : 0xffffffffu);
    for (uint32_t c = 0; cb && c < (x[77] - cb) / 0x54; ++c) {
        const uint32_t *rec = Ptr(cb + c * 0x54);
        b.push_back(IdOf(Ptr(rec[14])));
        b.push_back(rec[15]);
        b.push_back(rec[16]);
        b.push_back(rec[17]);
        b.push_back(rec[18] ? (rec[19] - rec[18]) / 4 : 0xffffffffu);
    }
    // Entries and values.
    const uint32_t eb = x[79];
    b.push_back(eb ? (x[80] - eb) / 4 : 0xffffffffu);
    b.push_back(eb ? (x[81] - eb) / 4 : 0xffffffffu);
    for (uint32_t i = 0; eb && i < (x[80] - eb) / 4; ++i) b.push_back(Ptr(eb)[i]);
    const uint32_t vb = x[82];
    b.push_back(vb ? (x[83] - vb) / 4 : 0xffffffffu);
    b.push_back(vb ? (x[84] - vb) / 4 : 0xffffffffu);
    for (uint32_t i = 0; vb && i < (x[83] - vb) / 4; ++i) b.push_back(Ptr(vb)[i]);
    const uint32_t rb = x[85];
    b.push_back(rb ? (x[86] - rb) / 4 : 0xffffffffu);
    b.push_back(rb ? (x[87] - rb) / 4 : 0xffffffffu);
    for (uint32_t i = 0; rb && i < (x[86] - rb) / 4; ++i) b.push_back(IdOf(Ptr(Ptr(rb)[i])));
    if (rb && (x[87] - rb) / 4 >= 5) b.push_back(IdOf(Ptr(Ptr(rb)[4]))); // kind 4's slot, past the four
    b.push_back(x[88]);
    b.push_back(IdOf(Ptr(x[89])));
    for (int i = 90; i < 99; ++i) b.push_back(x[i]);
    b.push_back(IdOf(Ptr(x[99])));
}

void PushWorld(Blob &b, const World &w)
{
    for (const Owner &o : w.owners) b.push_back(o.base.refs);
    for (const Obj &n : w.nodes) b.push_back(n.refs);
    for (const Str &s : w.strs) b.push_back(s.refs);
    for (const FakeDescription &d : w.manager.descriptions) b.push_back(d.refs);
    for (const Child &c : w.children) {
        b.push_back(c.refs);
        b.push_back(c.running);
    }
    b.push_back(gService.refs);
}

void Drain(Blob &b)
{
    b.insert(b.end(), gEv.begin(), gEv.end());
    gEv.clear();
}

void PushFrees(Blob &b, const difftest::FreeRecorder &rec)
{
    size_t sizes[24] = {};
    const size_t freed = rec.SortedSizes(sizes, 24);
    b.push_back(uint32_t(freed));
    for (size_t i = 0; i < freed && i < 24; ++i) b.push_back(uint32_t(sizes[i]));
}

void *NewObject(int mode, std::mt19937 &r)
{
    void *memory = ::operator new(sizeof(cTSVisualEffect));
    for (size_t i = 0; i < sizeof(cTSVisualEffect); ++i) static_cast<uint8_t *>(memory)[i] = uint8_t(r());
    return memory;
}

// A finite placement with a random flags byte and version.
void FiniteTransform(EffectsTransform &t, std::mt19937 &r)
{
    t = EffectsTransform{};
    t.flags = uint8_t((r() % 2 ? EffectsTransform::kRotated : 0) | (r() % 3 == 0 ? EffectsTransform::kTranslated : 0));
    t.Version() = uint16_t(r());
    for (auto &row : t.m)
        for (float &c : row) c = testhelp::FewOrUniform(r, -2.0f, 2.0f);
    t.scale = r() % 3 ? testhelp::FewOrUniform(r, -2.0f, 2.0f) : 1.0f;
    t.translation = fx::Vector3{Value(r), Value(r), Value(r)};
}

// ---- state the original's other slots leave ----

void InstallChildren(uint8_t *o, World &w, std::mt19937 &r)
{
    auto *x = reinterpret_cast<uint32_t *>(o);
    if (x[76] != 0 || x[99] == 0) return; // children come with an owner (an index walk reads its tables)
    const uint32_t n = 1 + r() % 4;
    auto *buffer = static_cast<uint32_t *>(::operator new(n * 0x54));
    std::memset(buffer, 0, n * 0x54);
    for (uint32_t c = 0; c < n; ++c) {
        uint32_t *rec = buffer + c * 0x54 / 4;
        Child *child = &w.children[r() % 6];
        ++child->refs;
        rec[14] = uint32_t(reinterpret_cast<uintptr_t>(child));
        rec[15] = r();
        rec[16] = Word(r() % 3 ? 0.5f * float(1 + r() % 4) : 0.0f);
        rec[17] = r() % 4 == 0 ? 0x20 : r() % 2 ? 0x08 : 0x28 * (r() % 2);
        if (r() % 4 != 0) {
            auto *indexes = static_cast<int32_t *>(::operator new(16));
            for (int i = 0; i < 4; ++i) indexes[i] = int(r() % 4); // a component of the effect
            rec[18] = uint32_t(reinterpret_cast<uintptr_t>(indexes));
            rec[19] = rec[18] + 4 * (1 + r() % 4);
            rec[20] = rec[18] + 16;
        }
    }
    x[76] = uint32_t(reinterpret_cast<uintptr_t>(buffer));
    x[77] = x[76] + n * 0x54;
    x[78] = x[77];
    x[13] = r() % 3; // the effect's index in the manager's table (Detach clears it with the children)
}

// The resource table: four of the five slots in use (the original makes it with four; the fifth is kind 4's, written past
// them), or empty with room (SetResource zero-fills), or the five.
void InstallResources(uint8_t *o, World &w, std::mt19937 &r)
{
    auto *x = reinterpret_cast<uint32_t *>(o);
    if (x[85] != 0) return;
    auto *buffer = static_cast<uint32_t *>(::operator new(20));
    std::memset(buffer, 0, 20);
    const uint32_t variant = r() % 4;
    const int used = variant == 3 ? 5 : variant == 2 ? 0 : 4;
    for (int i = 0; i < used; ++i) {
        Obj *node = r() % 5 ? &w.nodes[r() % 8] : nullptr;
        if (node) ++node->refs;
        buffer[i] = uint32_t(reinterpret_cast<uintptr_t>(node));
    }
    x[85] = uint32_t(reinterpret_cast<uintptr_t>(buffer));
    x[86] = x[85] + 4 * used;
    x[87] = x[85] + 20;
}

void InstallCamera(uint8_t *o, World &w, std::mt19937 &r)
{
    auto *x = reinterpret_cast<uint32_t *>(o);
    if (x[89] != 0) return;
    Obj *node = &w.nodes[r() % 8];
    ++node->refs;
    x[89] = uint32_t(reinterpret_cast<uintptr_t>(node));
}

void SetName(uint8_t *o, std::mt19937 &r)
{
    static const char *const kText[] = {"", "smoke", "a_much_longer_effect_name_that_is_on_the_heap", "x", "0123456789abcde", "0123456789abcdef"};
    reinterpret_cast<std::string *>(o + 0x14)->assign(kText[r() % 6]);
}

// The description: a flag word of the bits the effect reads (or any word), and the two distance limits.
void SetDescription(uint8_t *o, World &w, std::mt19937 &r, bool force)
{
    static const uint32_t kBits[] = {1, 2, 4, 0x10, 0x20, 0x40, 0x100, 0x200, 0x400, 0x2000, 0x4000, 0x8000, 0x10000};
    auto *x = reinterpret_cast<uint32_t *>(o);
    if (!force && r() % 8 == 0) {
        x[16] = 0;
        return;
    }
    uint32_t flags = 0;
    for (const uint32_t bit : kBits) {
        const unsigned oneIn = bit == 4 || bit == 0x40 || bit == 0x10 || bit == 0x4000 ? 6 : 3;
        if (r() % oneIn == 0) flags |= bit;
    }
    if (r() % 10 == 0) flags = r();
    w.description[0] = flags;
    const float low = 0.75f * float(r() % 4);
    w.description[5] = Word(low);
    w.description[6] = Word(low + 0.5f + 0.75f * float(r() % 4));
    x[16] = uint32_t(reinterpret_cast<uintptr_t>(w.description));
}

uint32_t EventKind(std::mt19937 &r)
{
    static const uint32_t kKinds[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 16, 0x12, 0x100, 0x101, 0x101, 0x200, 0xffffffff};
    return kKinds[r() % (sizeof(kKinds) / sizeof(kKinds[0]))];
}

uint32_t gCounts[0x20];

} // namespace

DIFF_TEST(VisualEffect_Construct, 0xbe86a0)
{
    for (uint32_t seed = 0; seed < 40; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            Blob &b = blobs[mode];
            std::mt19937 r(seed + 1);
            alignas(8) uint8_t block[sizeof(cTSVisualEffect)];
            for (uint8_t &x : block) x = uint8_t(r());
            void *e = mode ? static_cast<void *>(::new (block) cTSVisualEffect) : ctx.Original<ConstructFn>(0xbe86a0)(block);
            b.push_back(uint32_t(e == block));
            for (int f = 0; f < 3; ++f) b.push_back(TableCode(reinterpret_cast<const uint32_t *>(block)[f]));
            for (size_t i = 12; i < sizeof(block); ++i) b.push_back(block[i]);
            difftest::FreeRecorder rec;
            CallSlot<void *>(block + 8, 0, uint32_t(0));
            rec.Stop();
            PushFrees(b, rec);
        }
        Same(ctx, blobs[0], blobs[1], "VisualEffect_Construct", seed);
    }
}

DIFF_TEST(VisualEffect_Slots, 0xbe9700)
{
    Stands stands;
    uint32_t *const cell = GameAddress<uint32_t>(0x10f519c); // the cached message server: our fake, for both sides
    const uint32_t savedCell = *cell;
    *cell = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gService));
    uint32_t totals[2][hCount] = {};
    uint32_t steps[2][8] = {};
    constexpr uint32_t kSeeds = 300, kSteps = 60;
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            World world;
            Reset(world, seed);
            World &w = world;
            Blob &b = blobs[mode];
            b.reserve(1 << 20);
            std::mt19937 r(seed * 7919u + 3);
            void *e = NewObject(mode, r);
            auto *o = static_cast<uint8_t *>(e);
            auto *x = reinterpret_cast<uint32_t *>(o);
            gBase = o;
            void *e4 = o + 4;
            void *e8 = o + 8;
            void *made = mode ? static_cast<void *>(::new (e) cTSVisualEffect) : ctx.Original<ConstructFn>(0xbe86a0)(e);
            b.push_back(uint32_t(made == e));
            PushObject(b, o, w);
            // The first fingerprint builds our vtable cache (a throwaway object of ours, whose destructor reaches the texture
            // stand-in): what that logged is dropped.
            gEv.clear();
            b.push_back(CallSlot<uint32_t>(e, 1)); // the caller's reference: the count starts at 0
            for (uint32_t step = 0; step < kSteps; ++step) {
                b.push_back(0x0b000000u | step);
                uint32_t op = r() % 52;
                // The extra range repeats the ops whose branches need the most state: Update, a child's event, the child
                // installs and the blend / camera moves.
                if (op >= 40) {
                    static const uint32_t kRepeat[12] = {19, 19, 19, 52, 5, 5, 21, 29, 36, 34, 11, 52};
                    op = kRepeat[op - 40];
                }
                uint32_t result = 0xfeed;
                gExt[0] = &w.transforms[0];
                gExt[1] = &w.transforms[1];
                // What reaches Update (and so the owner's blend and camera) needs an owner, and a follow needs a description.
                if (op == 11 || op == 12 || op == 19 || op == 20 || (op >= 21 && op <= 25) || op == 31) {
                    if (x[99] == 0) {
                        FakeOwner *owner = &w.owners[r() % 3].base;
                        owner->kind = r() % 5;
                        CallSlot<void>(e4, 0, static_cast<void *>(owner));
                    }
                    if ((op == 31 || (op == 19 && r() % 2)) && x[16] == 0) SetDescription(o, w, r, true);
                }
                difftest::FreeRecorder rec;
                switch (op) {
                case 0:
                case 1:
                case 2: {
                    FakeOwner *owner = &w.owners[r() % 3].base;
                    owner->kind = r() % 5;
                    CallSlot<void>(e4, 0, static_cast<void *>(owner));
                    ++gHit[hAttach];
                    break;
                }
                case 3:
                case 4:
                    CallSlot<void>(e4, 1);
                    break;
                case 5:
                    InstallChildren(o, w, r);
                    break;
                case 6:
                    InstallResources(o, w, r);
                    break;
                case 7:
                    InstallCamera(o, w, r);
                    break;
                case 8:
                    SetName(o, r);
                    break;
                case 9:
                case 10:
                    SetDescription(o, w, r, false);
                    break;
                case 11:
                case 12: {
                    const uint32_t a = r() % 3;
                    b.push_back(a);
                    // Start with the "an index step waits" flag set, the effect running or not.
                    if (r() % 2) x[17] |= 4;
                    if (x[16]) {
                        if (o[0x3b] && (x[17] & 4)) ++gHit[hIndexStepRunning];
                        if (!o[0x3b] && (x[17] & 4)) ++gHit[hIndexStep];
                    }
                    if (r() % 2) CallSlot<void>(e, 3, a);
                    else CallSlot<void>(e4, 2, a);
                    ++gHit[hStart];
                    break;
                }
                case 13:
                case 14: {
                    const uint32_t a = r() % 3;
                    b.push_back(a);
                    if (r() % 2) CallSlot<void>(e, 4, a);
                    else CallSlot<void>(e4, 3, a);
                    ++gHit[hStop];
                    break;
                }
                case 15:
                    result = CallSlot<uint8_t>(e, 5);
                    b.push_back(CallSlot<uint8_t>(e4, 4));
                    b.push_back(CallSlot<uint8_t>(e, 6));
                    break;
                case 16:
                    CallSlot<void>(e, 7, uint32_t(r() % 2));
                    break;
                case 17:
                    CallSlot<void>(e, 8, uint32_t(r() % 2));
                    break;
                case 18:
                    CallSlot<void>(e4, 9, uint32_t(r() % 2));
                    break;
                case 19: {
                    FiniteTransform(w.transforms[0], r);
                    FiniteTransform(w.transforms[1], r);
                    uint32_t which = r() % 3;
                    const uint32_t tweak = r() % 8;
                    Owner *ownerNow = x[99] ? reinterpret_cast<Owner *>(uintptr_t(x[99])) : nullptr;
                    if (x[16]) {
                        const uint32_t dflags = w.description[0];
                        const bool skipped = (dflags & 0x2000) && o[0x39] && o[0x3b];
                        if (tweak == 0) {
                            // A first column with no horizontal part: the flatten keeps the identity (its length is exactly 0).
                            for (auto &t : w.transforms) t.m[0][0] = t.m[1][0] = 0.0f;
                            if ((dflags & 0x400) && !skipped) ++gHit[hFlatZero];
                        } else if (tweak == 1 && ownerNow && ownerNow->camera && (dflags & 3) == 3 && std::bit_cast<float>(x[97]) < 0.0f &&
                                   !(dflags & 0x8000) && !skipped) {
                            // An aimed billboard whose camera is a hair from the point: squared distances either side of 1e-6 and
                            // well between 1e-6 and 1e-4 (the placements are made identity so the point is the local translation).
                            static const float kOffsets[6] = {0.0009f, 0.00099f, 0.00101f, 0.0011f, 0.003f, 0.0085f};
                            const float p[3] = {0.25f, 0.5f, -0.125f};
                            const float d = kOffsets[r() % 6];
                            for (auto &t : w.transforms) {
                                t = EffectsTransform{};
                                t.scale = 1.0f;
                            }
                            w.transforms[0].flags = EffectsTransform::kTranslated;
                            w.transforms[0].translation = fx::Vector3{p[0], p[1], p[2]};
                            w.transforms[1].flags = EffectsTransform::kTranslated;
                            w.transforms[1].translation = fx::Vector3{0.0f, 0.0f, 0.0f};
                            which = 2;
                            Obj *cam = ownerNow->camera;
                            const uint32_t axis = r() % 3;
                            for (int i = 0; i < 3; ++i) cam->rigid[i] = cam->rigidAlt[i] = p[i];
                            cam->rigid[axis] = cam->rigidAlt[axis] = p[axis] + d;
                            ++gHit[hAimNear];
                        }
                    }
                    if (x[16]) {
                        const uint32_t flags = w.description[0];
                        const bool camera = reinterpret_cast<const Owner *>(uintptr_t(x[99]))->camera != nullptr;
                        if (o[0x39] && o[0x3b] && (flags & 0x2000)) {
                            ++gHit[hUpdFrozen];
                        } else {
                            ++gHit[hUpdate];
                            if (std::bit_cast<float>(x[97]) >= 0.0f) ++gHit[hUpdBlend];
                            if ((flags & 0x8000) && camera) ++gHit[hUpdFollow];
                            if ((flags & 0x8000) && camera && (flags & 0x10000)) ++gHit[hUpdClamp];
                            if (flags & 0x400) ++gHit[hUpdFlatten];
                            if (flags & 0x20) ++gHit[hUpdCompose];
                            if ((flags & 3) == 1 && camera) ++gHit[hUpdTurn];
                            if ((flags & 3) == 3 && camera) ++gHit[hUpdAim];
                            if (flags & 0x300) ++gHit[hUpdUnit];
                        }
                    }
                    if (which == 0) CallSlot<void>(e, 9, static_cast<const void *>(&w.transforms[0]));
                    else if (which == 1) CallSlot<void>(e, 10, static_cast<const void *>(&w.transforms[0]));
                    else CallSlot<void>(e4, 6, static_cast<const void *>(&w.transforms[0]), static_cast<const void *>(&w.transforms[1]));
                    break;
                }
                case 20: {
                    const uint32_t a = r() % 3 ? r() % 4 : r(), c = r() % 3 ? 0 : r();
                    CallSlot<void>(e, 11, a, c);
                    break;
                }
                case 21:
                case 22:
                case 23:
                case 24:
                case 25: {
                    const uint32_t kind = EventKind(r);
                    uint32_t count = r() % 10 == 0 ? (r() % 2 ? 0 : 0xffffffffu) : (r() % 5 ? 1 + (kind * 7 + seed) % 4 : 1 + r() % 6);
                    float values[32]; // the alias reads three floats per count
                    for (float &v : values) v = Value(r);
                    b.push_back(kind);
                    b.push_back(count);
                    const uint32_t via = r() % 3;
                    if (via == 0) result = CallSlot<uint8_t>(e, 13, kind, static_cast<const void *>(values), count);
                    else if (via == 1) result = CallSlot<uint8_t>(e, 12, kind, static_cast<const void *>(values), count);
                    else result = CallSlot<uint8_t>(e4, 7, kind, static_cast<void *>(values), reinterpret_cast<void *>(uintptr_t(count)));
                    ++gHit[hEvent];
                    break;
                }
                case 26: {
                    CallSlot<void>(e, 23, uint32_t(r() % 2));
                    result = CallSlot<uint8_t>(e, 22);
                    break;
                }
                case 27: {
                    const uint32_t kind = r() % 2 ? r() % 0x11 : 0x12 + r() % 4; // 0x11: the original reads one past the table
                    uint32_t count = 0xdead;
                    const uint32_t slot = r() % 2 ? 15 : 16;
                    const uint32_t *p = CallSlot<const uint32_t *>(e, slot, kind, static_cast<void *>(nullptr), &count);
                    b.push_back(kind);
                    b.push_back(count);
                    const uint32_t *base = Ptr(reinterpret_cast<const uint32_t *>(o)[82]);
                    b.push_back(p ? uint32_t(p - base) : 0xffffffffu);
                    if (p && count < 20) {
                        for (uint32_t i = 0; i < count * (slot == 15 ? 3 : 1); ++i) b.push_back(p[i]);
                    }
                    break;
                }
                case 28: {
                    // ORIGINAL-BUG: kind 4 reads one past a table of four: asked for only where the table has the fifth.
                    uint32_t kind = r() % 8;
                    if (kind == 4 && !(x[85] != 0 && (x[87] - x[85]) / 4 >= 5)) kind = 3;
                    void *p = CallSlot<void *>(e, 17, kind);
                    result = IdOf(p);
                    break;
                }
                case 29: {
                    // Kinds 0 to 4 are valid (5 returns false at once). The table is made with four; kind 4 only where the
                    // resource table has room for a fifth (the original writes past the end otherwise), and a null resource
                    // is kind 2's reset, not kind 0's (the original calls through it).
                    uint32_t kind = r() % 6;
                    const bool room = x[85] != 0 && (x[87] - x[85]) / 4 >= 5;
                    if (kind == 4 && !room) kind = r() % 4;
                    Obj *node = r() % 6 ? &w.nodes[r() % 8] : nullptr;
                    if (kind == 0 && !node) node = &w.nodes[r() % 8];
                    ++gHit[hResource];
                    if (kind == 4) ++gHit[hResKind4];
                    if (kind == 0 && !node->qiStr) ++gHit[hResNoString];
                    if (r() % 2) result = CallSlot<uint8_t>(e, 14, kind, static_cast<void *>(node));
                    else result = CallSlot<uint8_t>(e4, 8, reinterpret_cast<void *>(uintptr_t(kind)), static_cast<void *>(node));
                    break;
                }
                case 30: {
                    EffectsTransform out;
                    std::memset(&out, 0x5a, sizeof(out));
                    CallSlot<void>(e, r() % 2 ? 18 : 19, &out);
                    b.push_back(HashBytes(&out, sizeof(out)));
                    b.push_back(CallSlot<uint8_t>(e, 20));
                    b.push_back(CallSlot<uint8_t>(e, 21));
                    break;
                }
                case 31: {
                    int32_t counters[2] = {int(r() % 5), int(r() % 5)};
                    const float t = Value(r) * 3.0f;
                    if (!(x[17] & 2)) { // not hidden: the follow runs
                        ++gHit[hFollow];
                        if (!x[89]) ++gHit[hFolNoCamera];
                        if (w.description[0] & 0x8007) ++gHit[hFolNeeds];
                    }
                    CallSlot<void>(e4, 5, reinterpret_cast<void *>(uintptr_t(Word(t))), static_cast<void *>(counters));
                    b.push_back(uint32_t(counters[0]));
                    b.push_back(uint32_t(counters[1]));
                    ++gHit[hAddToScene];
                    break;
                }
                case 32: {
                    // The counts through the three faces; the caller's reference stays.
                    result = CallSlot<uint32_t>(e, 1);
                    b.push_back(CallSlot<uint32_t>(e4, 12));
                    b.push_back(CallSlot<uint32_t>(e, 2));
                    b.push_back(CallSlot<uint32_t>(e4, 13));
                    b.push_back(CallSlot<uint32_t>(e8, 2));
                    b.push_back(CallSlot<uint32_t>(e8, 4));
                    b.push_back(CallSlot<uint32_t>(e8, 3));
                    b.push_back(CallSlot<uint32_t>(e8, 4));
                    break;
                }
                case 33: {
                    void *out = nullptr;
                    static const uint32_t kIds[] = {1, 0x4c7af090, 0, 2, 0x4c7af091, 0xffffffffu};
                    const uint32_t id = kIds[r() % 6];
                    result = CallSlot<uint8_t>(e, 0, id, static_cast<void *>(&out));
                    b.push_back(out == e ? 1 : out == nullptr ? 0 : 2);
                    if (out) CallSlot<uint32_t>(e, 2); // the reference the query took
                    break;
                }
                case 34:
                case 35: {
                    // The camera moves (a node's placements, the owner's camera and progress).
                    Place(w.nodes[r() % 8], r);
                    Owner &owner = w.owners[r() % 3];
                    owner.camera = r() % 5 ? &w.nodes[r() % 8] : nullptr;
                    owner.params[3] = Value(r);
                    break;
                }
                case 36:
                case 37: {
                    // The blend (set by something the original's other classes do): with an owner only (Update reads its progress).
                    if (x[99]) {
                        const uint32_t kind = r() % 8;
                        x[97] = kind == 0 ? Word(-1.0f) : kind == 1 ? Word(0.0f) : Word(Value(r) * 0.5f + 0.5f);
                        x[98] = Word(Value(r));
                    }
                    break;
                }
                case 38: {
                    // The follow's cached camera placement: the camera's own, a few ulps off, the rotation negated, or anything.
                    auto *camera = reinterpret_cast<const Obj *>(uintptr_t(x[89]));
                    if (camera) {
                        float f[7];
                        std::memcpy(f, camera->world, sizeof(f));
                        const uint32_t how = r() % 9;
                        if (how >= 5) f[6] = Value(r); // the last number alone
                        for (int i = 0; i < 7; ++i) {
                            if (how == 1) f[i] = std::bit_cast<float>(std::bit_cast<uint32_t>(f[i]) + (r() % 3));
                            else if (how == 4) f[i] = std::bit_cast<float>(std::bit_cast<uint32_t>(f[i]) + 14 + (r() % 20)); // around the limit of 32 ulps
                            else if (how == 2 && i >= 3) f[i] = -f[i];
                            else if (how == 3) f[i] = Value(r);
                        }
                        std::memcpy(&x[90], f, sizeof(f));
                        if (how >= 5 && x[16] != 0) { // (not without a description: the original does not look)
                            // Followed at once, while only the last number is off: whether the camera counts as moved shows now.
                            int32_t counters[2] = {0, 0};
                            CallSlot<void>(e4, 5, reinterpret_cast<void *>(uintptr_t(Word(0.0f))), static_cast<void *>(counters));
                            b.push_back(uint32_t(counters[0]));
                            b.push_back(uint32_t(counters[1]));
                        }
                    }
                    break;
                }
                case 52: {
                    // The stored events and resources told to the children again (a non-virtual member the manager's rebuild calls).
                    ++gHit[hReplay];
                    const auto *entries = reinterpret_cast<const uint16_t *>(uintptr_t(x[79]));
                    if (entries && (x[80] - x[79]) / 4 > 0xf && entries[0xf * 2 + 1] != 0) ++gHit[hReplayIndex];
                    if (x[85] != x[86]) ++gHit[hReplayRes];
                    if (mode) reinterpret_cast<cTSVisualEffect *>(e)->ReplayEvents();
                    else ctx.Original<ConstructFn>(0xbe8c80)(e);
                    break;
                }
                default: {
                    // Start with the index step flag, and a second start on a running effect.
                    x[17] |= 4;
                    CallSlot<void>(e, 3, uint32_t(r() % 3));
                    ++gHit[hStart];
                    break;
                }
                }
                rec.Stop();
                b.push_back(op);
                b.push_back(result);
                Drain(b);
                PushFrees(b, rec);
                PushObject(b, o, w);
                PushWorld(b, w);
                ++steps[mode][op % 8];
            }
            b.push_back(0x0b0000ffu);
            {
                difftest::FreeRecorder rec;
                if (seed % 2 == 0) {
                    reinterpret_cast<uint32_t *>(o)[4] = 1; // the last reference: Release deletes
                    b.push_back(CallSlot<uint32_t>(e, 2));
                    rec.Stop();
                    b.push_back(uint32_t(rec.Freed(e)));
                } else {
                    b.push_back(uint32_t(CallSlot<void *>(e8, 0, uint32_t(0)) == e));
                    rec.Stop();
                }
                Drain(b);
                PushFrees(b, rec);
            }
            if (seed % 2 != 0) ::operator delete(e);
            PushWorld(b, w);
            for (unsigned i = 0; i < hCount; ++i) totals[mode][i] += gHit[i];
        }
        Same(ctx, blobs[0], blobs[1], "VisualEffect_Slots", seed);
    }
    static const char *const kHitNames[hCount] = {
        "Update", "Update frozen", "Update blend", "Update follow camera", "Update clamp", "Update flatten", "Update compose", "Update turn",
        "Update aim", "Update unit", "child Update", "SetResource", "camera query", "string text", "no string", "kind 4", "resource taken",
        "Follow", "Follow without camera", "Follow needs", "Follow no parent", "index step child", "index step", "index step running",
        "Sound service", "no sound service", "child event", "child took event", "Attach", "Start", "Stop", "Event", "AddToScene", "Replay", "Replay index", "Replay resources", "Flatten zero", "Aim near"};
    static const uint32_t kFloor[hCount] = {700, 15, 150, 200, 60, 200, 250, 100, 60, 400, 700, 300, 30, 40, 15, 30, 20, 120, 90, 100, 5,
                                            60, 110, 60, 10, 0, 250, 60, 500, 650, 350, 1000, 170, 60, 10, 20, 20, 6};
    *cell = savedCell;
    if (std::getenv("TS2_VE_HITS"))
        for (unsigned i = 0; i < hCount; ++i) std::fprintf(stderr, "hit %-24s %8u %8u (floor %u)\n", kHitNames[i], totals[0][i], totals[1][i], kFloor[i]);
    for (int mode = 0; mode < 2; ++mode)
        for (unsigned i = 0; i < hCount; ++i)
            if (totals[mode][i] < kFloor[i]) ctx.Fail("VisualEffect_Slots: side %d reached %s only %u times (floor %u)", mode, kHitNames[i], totals[mode][i], kFloor[i]);
}
