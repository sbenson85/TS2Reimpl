// Differential test for nTSSG::cTSSGSystem::SetUpCinematic (slot 27, src/sg/cTSSGSystemCinematicSetup.cpp). Hook shape:
// the original runs on the same object image as ours.
//
// The world is made of wide fakes (tests/sg_tssg_wide.h): the app (cell 0x10cc288; slot 3's object answers the feature
// query, slot 26 the lot, slot 24 the selection), the service (cell 0x10c9378; its slot 5 hands out the resources, whose
// slot 5 logs the parsed key and writes the cinematic), the cinematic (parts, actors, per-actor flags, requirement text
// and value, the selection; its virtual base answers the name), the lot (slot 29 objects by id, slot 16 the selected
// id), the objects (owner redirection, actor interface, pieces), mRefB4's animated entry and the scene manager's camera.
// The object's own slots 26 (find animation), 28 (play) and 29 (end) are a test vtable that logs and answers. The check
// 0xae1ed0 (nGZSceneGraph::AnimatedFaceOf) is stood in on both sides. The three vectors start with 0..2 old entries and spare capacity 0..2, so pushes
// take both the in-place and the reallocating path.
//
// Compared after each run: the event log (every call with its arguments, AddRef / Release), the result and *error, the
// three vectors (size, capacity, entries), the object's other bytes (normalised), the fakes and the cells, and the frees
// (count and sorted sizes).
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "sg_tssg_wide.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "sg/SGUtilities.h"
#include "sg/cTSSGSystem.h"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace svcfx;
using nTSSG::cTSSGCinematicActor;
using nTSSG::cTSSGSystem;
using nTSSG::SGVirtualRef;
using sgwide::W;

constexpr uint32_t kObjectSize = 0x3b8;
constexpr uint32_t kObjectTag = 0x5e000000;
constexpr uint32_t kServiceCell = 0x10c9378;
constexpr uint32_t kCells[] = {kAppCell, kServiceCell};
constexpr char kCellKinds[] = "pp"; // the cells' words as the outcome logs them (svcfx::DumpCells)

constexpr uint32_t kServiceClass = 0x056b906e;
enum : uint32_t { evStand = 0x81, evSelf = 0x82 };

constexpr unsigned kObjects = 5; // the last is the owner redirection's target
constexpr unsigned kPieces = 3;
constexpr unsigned kParts = 3;
constexpr unsigned kOld = 6;
enum : unsigned {
    wApp = 0,
    wAppObject,
    wService,
    wResources,
    wCinematic,
    wCinematicBase,
    wLot,
    wSelection,
    wSelected,
    wScene,
    wCamera,
    wB4,
    wEntry,
    wEntryBase,
    wAnimated,
    wOwner,
    wOwnerInner,
    wObject,                      // kObjects
    wRef = wObject + kObjects,    // the actor interface of each object
    wPiece = wRef + kObjects,     // kPieces
    wExtra = wPiece + kPieces,    // the actor interface of each piece
    wAnim = wExtra + kPieces,     // kParts animations
    wOld = wAnim + kParts,        // kOld references already in the vectors and +0x128
    wEnd = wOld + kOld,
};
static_assert(wEnd <= sgwide::kWide);

struct ObjectDraw {
    uint32_t owned;      // slot 104 (al)
    uint32_t actor;      // slot 9 (al)
    bool writes;         // slot 9 writes the reference even when it answers false
    uint32_t pieces;     // slot 66
    bool pieceThere[kPieces];
};
struct Draw {
    bool appMissing, appObject;
    uint32_t feature;            // slot 15 (ax)
    bool cinematicSet;           // mCinematic already set
    bool found;                  // the resources write the cinematic
    uint32_t parts, failPart;    // part `failPart` (1-based) is not found; 0: none fails
    uint32_t actors;
    uint32_t restore[6], required[6], value[6];
    bool requirement[6];
    uint32_t selected;
    int objectOf[8];             // the lot's slot 29 by id & 7: an object index or -1
    uint32_t selectedId;         // the lot's slot 16 (ax)
    bool selection;              // the selection's slot 14 answers
    ObjectDraw objects[kObjects];
    uint32_t pieceActor[kPieces];
    uint32_t accept[kObjects];   // 0xae1ed0 for each actor reference
    uint32_t acceptCamera;
    bool b4, camera, animatedWritten;
    int32_t index;
    uint32_t qi;
    int nameKind;                // the cinematic's name: the hiding name, its upper case, another
    uint8_t field163, playNow, hiding, field161;
    uint32_t play;
    int32_t error;
    int32_t endError;            // written to *error by slot 29 (as a callee holding the same cell would); -1 none
    unsigned oldCount[3], spare[3];
    int ref128;                  // -1 null, 0..: an old reference, 10..: an actor reference
};
Draw gD;

std::string gName;               // the name argument
std::string gPartName;           // the cinematic's slot 7
std::string gCinematicName;      // its virtual base's slot 17
std::vector<int16_t> gIds;
int32_t gError;
unsigned gPartCalls;
const char kRequirement[] = "req"; // test text
const char kEmpty[] = "";

uint32_t Al(uint32_t v) { return 0xabcd0000u | v; }

// The feature query and the singleton acquire both go through the service's slot 5; only the cinematic's gets the resources.
uint32_t ServiceGet(sgwide::Wide *, uint32_t *args)
{
    sgwide::Out(args, 2, args[0] == kServiceClass ? P(&W(wResources)) : 0u);
    return 1;
}
uint32_t ResourcesGet(sgwide::Wide *, uint32_t *args) // (key, type, out, 0, 0)
{
    const auto *key = reinterpret_cast<const uint32_t *>(uintptr_t(args[0]));
    for (int i = 0; i < 4; ++i) Ev(key[i]);
    sgwide::Out(args, 2, gD.found ? P(&W(wCinematic)) : 0u);
    return 1;
}
uint32_t PerActor(const uint32_t *values, uint32_t *args) { return args[0] < 6 ? values[args[0]] : 0x5a5a5a5au; }
uint32_t Restore(sgwide::Wide *, uint32_t *args) { return PerActor(gD.restore, args); }
uint32_t Required(sgwide::Wide *, uint32_t *args) { return PerActor(gD.required, args); }
uint32_t Value(sgwide::Wide *, uint32_t *args) { return PerActor(gD.value, args); }
uint32_t Requirement(sgwide::Wide *, uint32_t *args)
{
    return P(args[0] < 6 && gD.requirement[args[0]] ? kRequirement : kEmpty);
}
uint32_t LotObject(sgwide::Wide *, uint32_t *args)
{
    const int k = gD.objectOf[args[0] & 7];
    return k < 0 ? 0u : P(&W(wObject + unsigned(k)));
}
uint32_t ObjectOwner(sgwide::Wide *, uint32_t *args)
{
    sgwide::Out(args, 1, P(&W(wOwner)));
    return 1;
}
uint32_t ObjectActor(sgwide::Wide *self, uint32_t *args) // (class, iid, out)
{
    const unsigned k = self->id - 500 - wObject;
    const ObjectDraw &o = gD.objects[k];
    if (o.actor & 0xff || o.writes) sgwide::Out(args, 2, P(&W(wRef + k)));
    return Al(o.actor);
}
uint32_t ObjectPiece(sgwide::Wide *self, uint32_t *args)
{
    const ObjectDraw &o = gD.objects[self->id - 500 - wObject];
    return args[0] < kPieces && o.pieceThere[args[0]] ? P(&W(wPiece + args[0])) : 0u;
}
uint32_t PieceActor(sgwide::Wide *self, uint32_t *args)
{
    const unsigned k = self->id - 500 - wPiece;
    if (gD.pieceActor[k] & 0xff) sgwide::Out(args, 2, P(&W(wExtra + k)));
    return Al(gD.pieceActor[k]);
}
uint32_t EntryQuery(sgwide::Wide *, uint32_t *args) // (iid, out): the answer points at the vbtable pointer (+4)
{
    if (gD.animatedWritten) sgwide::Out(args, 1, P(&W(wAnimated)) + 4);
    return Al(gD.qi);
}

// The object's own slots.
uint32_t __fastcall SelfFind(void *self, void *, const char *text, void **out, uint32_t force)
{
    Ev(evSelf), Ev(26), Ev(Norm(P(self)));
    for (const char *c = text; *c; ++c) Ev(uint8_t(*c));
    Ev(Norm(P(out))), Ev(force);
    const unsigned k = gPartCalls++;
    const bool found = gD.failPart != k + 1;
    if (found && k < kParts) *out = &W(wAnim + k);
    return Al(found);
}
uint32_t __fastcall SelfPlay(void *self, void *, uint32_t a, int32_t *error)
{
    Ev(evSelf), Ev(28), Ev(Norm(P(self))), Ev(a), Ev(P(error)), Ev(uint32_t(*error));
    *error = 0x1234;
    return gD.play;
}
void __fastcall SelfEnd(void *self, void *, uint32_t abort)
{
    Ev(evSelf), Ev(29), Ev(Norm(P(self))), Ev(abort);
    if (gD.endError != -1) gError = gD.endError;
}
void *gSelfVTable[100];

int __cdecl StandAccepts(void *object)
{
    Ev(evStand), Ev(0xae1ed0), Ev(Norm(P(object)));
    if (object == &W(wCamera)) return int(gD.acceptCamera);
    for (unsigned k = 0; k < kObjects; ++k)
        if (object == &W(wRef + k)) return int(gD.accept[k]);
    return 0x77;
}

struct Fixture {
    Globals globals;
    shape::StandIn accepts{0xae1ed0, &nGZSceneGraph::AnimatedFaceOf, &StandAccepts};
    Fixture()
    {
        for (uint32_t cell : kCells) globals.SetWord(cell, 0);
        gSelfVTable[26] = reinterpret_cast<void *>(&SelfFind);
        gSelfVTable[28] = reinterpret_cast<void *>(&SelfPlay);
        gSelfVTable[29] = reinterpret_cast<void *>(&SelfEnd);
    }
};

void Wire()
{
    W(wApp).Set(3, 0, gD.appObject ? P(&W(wAppObject)) : 0u);
    W(wApp).Set(26, 0, &W(wLot));
    W(wApp).Set(24, 0, &W(wSelection));
    W(wAppObject).Set(15, "d", gD.feature);
    W(wService).Set(5, "ddp", 0u, &ServiceGet);
    W(wResources).Set(5, "pdp--", 0u, &ResourcesGet);
    W(wCinematic).Base(1, &W(wCinematicBase));
    W(wCinematic).Set(6, 0, gD.parts);
    W(wCinematic).Set(7, 0, gPartName.c_str());
    W(wCinematic).Set(16, 0, gD.actors);
    W(wCinematic).Set(12, "d", 0u, &Restore);
    W(wCinematic).Set(11, "d", 0u, &Required);
    W(wCinematic).Set(15, "d", 0u, &Requirement);
    W(wCinematic).Set(10, "d", 0u, &Value);
    W(wCinematic).Set(17, 0, gD.selected);
    W(wCinematicBase).Set(17, 0, gCinematicName.c_str());
    W(wLot).Set(29, "d", 0u, &LotObject);
    W(wLot).Set(16, "p---", gD.selectedId);
    W(wSelection).Set(14, "d", gD.selection ? P(&W(wSelected)) : 0u);
    W(wScene).Set(18, 0, gD.camera ? P(&W(wCamera)) : 0u);
    W(wB4).Set(35, 1, uint32_t(gD.index));
    W(wB4).text[35] = 1;
    W(wB4).Set(36, "d", &W(wEntry));
    W(wEntry).Base(1, &W(wEntryBase));
    W(wEntryBase).Set(0, "dp", 0u, &EntryQuery);
    W(wAnimated).Base(1, &W(wAnimated));
    W(wOwner).Set(6, 0, &W(wOwnerInner));
    W(wOwnerInner).Set(11, 0, &W(wObject + kObjects - 1));
    for (unsigned k = 0; k < kObjects; ++k) {
        const ObjectDraw &o = gD.objects[k];
        W(wObject + k).Set(104, 0, Al(o.owned));
        W(wObject + k).Set(0, "dp", 0u, &ObjectOwner);
        W(wObject + k).Set(9, "ddp", 0u, &ObjectActor);
        W(wObject + k).Set(66, 0, o.pieces);
        W(wObject + k).Set(69, "d", 0u, &ObjectPiece);
        W(wRef + k).Base(1, &W(wRef + k));
        W(wRef + k).Set(97, "d");
    }
    for (unsigned k = 0; k < kPieces; ++k) {
        W(wPiece + k).Set(9, "ddp", 0u, &PieceActor);
        W(wExtra + k).Base(1, &W(wExtra + k));
        W(wExtra + k).Set(97, 1);
    }
    for (unsigned k = 0; k < kParts; ++k) W(wAnim + k).Base(1, &W(wAnim + k));
    for (unsigned k = 0; k < kOld; ++k) {
        W(wOld + k).Base(1, &W(wOld + k));
        W(wOld + k).Set(97, "d");
    }
}

alignas(16) uint8_t gObject[kObjectSize];
// gObject's words as the outcome logs them (svcfx::DumpWords): 'p' pointer, 'd' data, 'x' not compared.
constexpr char kObjectKinds[] =
    "pddddddd dddddddd dddddddd ddpddddd dddddddd dddddpdd dddddddd dddddddd "
    "dddddddd pdp----- ----dddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddd";
cTSSGSystem *Ours() { return reinterpret_cast<cTSSGSystem *>(gObject); }
template <class T> std::vector<T> *VectorAt(uint32_t offset) { return reinterpret_cast<std::vector<T> *>(gObject + offset); }

void *OldRef(unsigned k) { return &W(wOld + k % kOld); }

// The object: random bytes, then the references and vectors the function reads.
void Build(const uint8_t *start)
{
    std::memcpy(gObject, start, kObjectSize);
    auto put = [](uint32_t offset, const void *p) { std::memcpy(gObject + offset, &p, 4); };
    put(0, gSelfVTable);
    put(0x68, &W(wScene));
    put(0xb4, gD.b4 ? &W(wB4) : nullptr);
    put(0x120, gD.cinematicSet ? &W(wCinematic) : nullptr);
    void *ref128 = gD.ref128 < 0 ? nullptr : gD.ref128 >= 10 ? &W(wRef + unsigned(gD.ref128 - 10)) : OldRef(unsigned(gD.ref128));
    if (ref128) ++static_cast<sgwide::Wide *>(ref128)->refs;
    put(0x128, ref128);
    auto *actors = ::new (gObject + 0x12c) std::vector<cTSSGCinematicActor>();
    auto *extras = ::new (gObject + 0x138) std::vector<cTSSGCinematicActor>();
    auto *refs = ::new (gObject + 0x144) std::vector<SGVirtualRef>();
    actors->reserve(gD.oldCount[0] + gD.spare[0]);
    extras->reserve(gD.oldCount[1] + gD.spare[1]);
    refs->reserve(gD.oldCount[2] + gD.spare[2]);
    for (unsigned i = 0; i < gD.oldCount[0]; ++i) {
        cTSSGCinematicActor a{};
        a.mObject.mObject = OldRef(i);
        a.mValue = 0x100 + i;
        a.mRestore = uint8_t(i);
        ++W(wOld + i % kOld).refs; // the entry's own reference (the local's destructor releases one)
        actors->push_back(a);
    }
    for (unsigned i = 0; i < gD.oldCount[1]; ++i) {
        cTSSGCinematicActor a{};
        a.mObject.mObject = OldRef(i + 2);
        a.mValue = 0x200 + i;
        ++W(wOld + (i + 2) % kOld).refs;
        extras->push_back(a);
    }
    for (unsigned i = 0; i < gD.oldCount[2]; ++i) {
        SGVirtualRef r;
        r.mObject = OldRef(i + 4);
        ++W(wOld + (i + 4) % kOld).refs;
        refs->push_back(r);
    }
}

// The vectors described (size, capacity, entries), then destroyed and their words cleared.
void DescribeVectors(Blob &b)
{
    for (uint32_t offset : {0x12cu, 0x138u}) {
        auto *v = VectorAt<cTSSGCinematicActor>(offset);
        b.push_back(uint32_t(v->size()));
        b.push_back(uint32_t(v->capacity()));
        for (const cTSSGCinematicActor &a : *v) {
            b.push_back(Norm(P(a.mObject.mObject)));
            b.push_back(a.mValue);
            b.push_back(a.mRestore);
        }
        std::destroy_at(v);
        std::memset(gObject + offset, 0, 12);
    }
    auto *refs = VectorAt<SGVirtualRef>(0x144);
    b.push_back(uint32_t(refs->size()));
    b.push_back(uint32_t(refs->capacity()));
    for (const SGVirtualRef &r : *refs) b.push_back(Norm(P(r.mObject)));
    std::destroy_at(refs);
    std::memset(gObject + 0x144, 0, 12);
}

uint32_t Pick(std::mt19937 &gen, std::initializer_list<uint32_t> values) { return values.begin()[gen() % values.size()]; }

void DrawCase(std::mt19937 &gen)
{
    gD = Draw{};
    gD.appMissing = gen() % 16 == 0;
    gD.appObject = gen() % 16 != 0;
    gD.feature = Pick(gen, {1, 1, 1, 1, 1, 0, 0x10000, 0xffff});
    gD.cinematicSet = gen() % 16 == 0;
    gD.found = gen() % 8 != 0;
    gD.parts = Pick(gen, {0, 1, 1, 2, 3});
    gD.failPart = gen() % 4 == 0 ? 1 + gen() % 3 : 0;
    gD.actors = gen() % 6;
    for (unsigned i = 0; i < 6; ++i) {
        gD.restore[i] = gen() % 4 == 0 ? Pick(gen, {1, 0x100, 0xff}) : 0u;
        gD.required[i] = gen() % 4 == 0 ? Pick(gen, {1, 0x100}) : 0u;
        gD.value[i] = gen();
        gD.requirement[i] = gen() % 3 == 0;
    }
    gD.selected = gen() % 5 == 0 ? Pick(gen, {gD.actors, gD.actors + 1, 0xffffffffu}) : gen() % (gD.actors + 1);
    for (int &k : gD.objectOf) k = gen() % 5 == 0 ? -1 : int(gen() % kObjects);
    gD.selectedId = Pick(gen, {0xabcd0001u, 0xabcd0003u, 0x12340000u, 0xffff8005u, 0x00000007u});
    gD.selection = gen() % 3 != 0;
    for (ObjectDraw &o : gD.objects) {
        o.owned = gen() % 5 == 0 ? Pick(gen, {1, 0x100}) : 0u;
        o.actor = gen() % 5 == 0 ? Pick(gen, {0, 0x100}) : 1u;
        o.writes = gen() % 3 == 0;
        o.pieces = gen() % 3 == 0 ? gen() % (kPieces + 1) : 0;
        for (bool &p : o.pieceThere) p = gen() % 4 != 0;
    }
    for (uint32_t &p : gD.pieceActor) p = Pick(gen, {1, 1, 0, 0x100});
    for (uint32_t &a : gD.accept) a = Pick(gen, {1, 1, 1, 0, 0x100});
    gD.acceptCamera = Pick(gen, {1, 1, 0, 0x100});
    gD.b4 = gen() % 6 != 0;
    gD.camera = gen() % 6 != 0;
    gD.animatedWritten = gen() % 5 != 0;
    gD.index = int32_t(Pick(gen, {0, 0, 0, 3, 0xffffffffu, 0xfffffffeu}));
    gD.qi = Pick(gen, {1, 1, 1, 0, 0x100});
    gD.nameKind = int(gen() % 3);
    gD.field163 = uint8_t(Pick(gen, {0, 1, 0xff}));
    gD.playNow = uint8_t(Pick(gen, {0, 0, 1, 0x80}));
    gD.hiding = uint8_t(Pick(gen, {0, 1, 2}));
    gD.field161 = uint8_t(Pick(gen, {0, 1, 4}));
    gD.play = Pick(gen, {0xabcd0000u, 0xabcd0001u, 0x12345680u});
    gD.error = int32_t(Pick(gen, {0, 5, 0xffffffffu}));
    gD.endError = int32_t(Pick(gen, {0xffffffffu, 0xffffffffu, 0x42}));
    for (unsigned i = 0; i < 3; ++i) gD.oldCount[i] = gen() % 3, gD.spare[i] = gen() % 3;
    gD.ref128 = gen() % 3 == 0 ? -1 : gen() % 2 ? int(gen() % kOld) : 10 + int(gen() % kObjects);

    // The name: empty, blank, short, padded, past the inline buffer, or already ending with the suffix.
    const std::string suffix = GameAddress<const char>(0xdadff0);
    const char *const names[] = {"", "   ", "intro", "  intro\t", "a_much_longer_cinematic_name"}; // test text
    const unsigned pick = gen() % 6;
    gName = pick < 5 ? names[pick] : std::string("intro") + suffix;
    gPartName = gen() % 2 ? "part" : "a_part_name_past_sixteen_characters"; // test text
    const std::string hiding = GameAddress<const char>(0xdaeff8);
    if (gD.nameKind == 0) gCinematicName = hiding;
    else if (gD.nameKind == 1) {
        gCinematicName = hiding;
        for (char &c : gCinematicName) c = char(std::toupper(static_cast<unsigned char>(c)));
    } else gCinematicName = "other"; // test text
    gIds.clear();
    const unsigned idCount = gen() % 7;
    for (unsigned i = 0; i < idCount; ++i)
        gIds.push_back(int16_t(Pick(gen, {0, 1, 2, 3, 4, 0xffff, 0x7fff, 0x8000, 0x8001})));
}

} // namespace

// 0x2530c0 SetUpCinematic: the app, its object or the feature (ax: 0x10000 counts as none) missing; a cinematic already
// set; names empty, blank, padded, long, already suffixed; the cinematic not found; 0..3 parts (one or several, the
// numbered names), any of them not found; 0..5 actors with 0..6 ids (zero, negative, past the actors), each actor's restore
// and required flags (al), the selected id (ax) or none, the lot's object or none, owned (slot 104) or not, its actor
// interface or not (written or not), requirement text and the check, 0..3 pieces; the selection at, past and below the
// actors' count; +0x128 null, old or already the chosen actor; mRefB4, its entry index (-1, -2), the query, the camera
// and its check; the cinematic's name the hiding one, its upper case, or another; play now or not, slot 28's answer.
DIFF_TEST(sg_tssg_system_set_up_cinematic, 0x2530c0)
{
    Fixture fixture;
    using Fn = uint8_t(__thiscall *)(void *, const char *, const std::vector<int16_t> *, uint32_t, uint32_t, uint32_t,
                                     uint32_t, int32_t *);
    auto original = ctx.Original<Fn>(0x2530c0);
    gEv.reserve(1 << 16); // the log must not reallocate (and free) while frees are recorded
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x2530c0u);
        DrawCase(gen);
        uint8_t start[kObjectSize];
        for (uint8_t &byte : start) byte = uint8_t(gen());
        // The three upper argument bytes of the flags are junk the function must ignore.
        const uint32_t junk = gen() & 0xffffff00u;
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            ResetAll();
            sgwide::ResetWide();
            Wire();
            *GameAddress<void *>(kAppCell) = gD.appMissing ? nullptr : &W(wApp);
            *GameAddress<void *>(kServiceCell) = &W(wService);
            Build(start);
            AddRange(gObject, kObjectSize, kObjectTag);
            gError = gD.error;
            gPartCalls = 0;
            ctx.Mark(side ? "ours" : "original");
            difftest::FreeRecorder frees;
            const uint8_t result =
                side == 0 ? original(gObject, gName.c_str(), &gIds, junk | gD.field163, junk | gD.playNow, junk | gD.hiding,
                                     junk | gD.field161, &gError)
                          : Ours()->SetUpCinematic(gName.c_str(), &gIds, gD.field163, gD.playNow, gD.hiding, gD.field161,
                                                   &gError);
            frees.Stop();
            Blob b = gEv;
            b.push_back(0xeeee);
            b.push_back(result);
            b.push_back(uint32_t(gError));
            b.push_back(uint32_t(frees.Count()));
            size_t sizes[256];
            const size_t n = frees.SortedSizes(sizes, 256);
            for (size_t i = 0; i < n; ++i) b.push_back(uint32_t(sizes[i]));
            b.push_back(frees.Overflowed());
            DescribeVectors(b);
            svcfx::DumpWords(b, gObject, sizeof(gObject), kObjectKinds);
            CaptureFakes(b);
            sgwide::CaptureWide(b);
            svcfx::DumpCells(b, kCells, kCellKinds);
            outcome[side] = b;
        }
        Same(ctx, outcome[0], outcome[1], "set_up_cinematic", seed);
    }
}
