// cTSNHoodOccupantManager (0xd0 bytes, five vtables): differential tests of the core, the grid and the persistence (lane DI),
// removal, queries and roads (lane FY), on three routes (lane FY, the standalone shape: docs/standalone-shape.md, "Tests").
// Every case builds the same manager three times from one seed, runs the original (route 0), our vtable's slot (route 1) and the
// adapter with a trap in the slot it forwards to (route 2), and compares the return, the whole object (fields, the four lists
// with their capacities, every grid cell, the pending vertices, the id set, the vtable words coded), every fake occupant's
// reference count and the log of every fake call in order: the occupants, the stream, the neighbourhood / service chain, the
// persist service, the notifier and the message server.
// The vtables the objects carry are copies of the route's own (the game's on routes 0 and 2, ours on route 1) whose manager-face
// slots 10, 12, 14, 16, 18 (FindOccupantsInRect and the four Create functions) are fakes, so the occupants the Read creates are
// scripted fake occupants, not the game's occupant classes.
#include "difftest.h"
#include "game/game.h"
#include "heap_poison.h"
#include "shape.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "rz/RZMemory.h"
#include "ts/cTSNHoodOccupantManager.h"
#include "ts_nhood_fakes.h"
#include "ts_services_fixture.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <new>
#include <random>
#include <vector>

#include <windows.h>

namespace {
using namespace nhoodfx;
using Blob = std::vector<uint32_t>;
using Mgr = cTSNHoodOccupantManager;

enum : uint32_t { kFlora, kRoad, kModelRoad, kProp };
constexpr uint32_t kIids[4] = {0x4bd0e45b, 0xabd4c3f4, 0x6bd4d6ce, 0x2c0c10c8};
constexpr uint32_t kNoCreate = 0xffffffff;

// ---- fake occupants ----

struct Occ {
    void **vt;
    int32_t refs;
    uint32_t id;
    Occ *next;       // slot 3
    uint32_t layer;  // slot 7
    uint32_t flag12; // slot 12
    int32_t bounds[4];
    uint32_t iidMask; // bit k: QI answers kIids[k]
    uint32_t readOk;  // slots 10 / 22
    // lane FY: the grid / road vtable (gRoadVT)
    Occ *qiAs;      // what QI hands out (null: itself)
    uint32_t type3; // grid object slot 3: the type byte FindOccupantsInRect tests (upper bits junk)
    uint32_t s19;   // road slot 19 (a flag in al)
    uint32_t mark;  // road slot 23 stores it
    uint32_t s25;   // road slot 25 (a byte in al)
    uint32_t trace; // tiles the stood-in tracer 0x4a8930 adds for this road
};
std::deque<Occ> gOccs;
uint32_t gNextId;
void *gOccVT[40]{};
Mgr *gBase; // the object under test: pointers into it are logged as offsets

struct Fake { void **vt; uint32_t id; };
void *gAppVT[16]{}, *gNbhdVT[64]{}, *gServiceVT[8]{}, *gNotifierVT[8]{}, *gPersistVT[40]{}, *gSegVT[16]{}, *gServerVT[8]{},
    *gMsgVT[8]{}, *gDataVT[24]{};
Fake gApp{gAppVT, 10}, gNbhd{gNbhdVT, 11}, gService{gServiceVT, 12}, gNotifier{gNotifierVT, 14}, gPersist{gPersistVT, 15},
    gSeg{gSegVT, 16}, gServer{gServerVT, 17}, gMsg{gMsgVT, 18}, gData{gDataVT, 19};
uint32_t gGridW = 12, gGridH = 8;
float gCellSize = 10.0f;
uint8_t gSaveRet, gFindRet, gFlushRet;
uint32_t gMsgType, gMsgX, gMsgY;
bool gMsgHasData;
uint32_t gReadIndex;
std::vector<uint8_t> gReadOk;
uint32_t gCreateBounds;     // varies the footprints of created occupants
uint32_t gCreateMasks[4];   // iid mask of the grid object of each kind
uint32_t gCreateFlag12;     // bit i: the i-th created occupant's slot 12 answers non-zero
uint32_t gCreated;

void Ev(uint32_t a, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0) { Note(evMisc, a, b); gLog.push_back(c); gLog.push_back(d); }

uint32_t Norm(const void *p)
{
    const auto u = reinterpret_cast<uintptr_t>(p);
    if (!p) return 0;
    for (Fake *f : {&gApp, &gNbhd, &gService, &gNotifier, &gPersist, &gSeg, &gServer, &gMsg, &gData}) if (p == f) return 0x100 + f->id;
    for (const Occ &o : gOccs) if (p == &o) return 0x2000 + o.id;
    if (gBase && u >= reinterpret_cast<uintptr_t>(gBase) && u < reinterpret_cast<uintptr_t>(gBase) + sizeof(Mgr)) return 0x1000 + uint32_t(u - reinterpret_cast<uintptr_t>(gBase));
    return 0xbad00000;
}
uint32_t OccId(const void *p)
{
    if (!p) return 0;
    for (const Occ &o : gOccs) if (p == &o) return o.id;
    return 0xbad;
}

// slot 0 QueryInterface(iid, out): answers with a counted pointer to itself when the iid is in its mask
uint8_t __fastcall OccQI(Occ *self, void *, uint32_t iid, void **out)
{
    Ev(0x31, self->id, iid);
    for (int k = 0; k < 4; ++k) {
        if ((self->iidMask >> k & 1) && iid == kIids[k]) {
            Occ *answer = self->qiAs ? self->qiAs : self;
            *out = answer;
            ++answer->refs;
            return 1;
        }
    }
    return 0;
}
uint32_t __fastcall OccAddRef(Occ *self, void *) { Ev(0x32, self->id, uint32_t(self->refs + 1)); return uint32_t(++self->refs); }
uint32_t __fastcall OccRelease(Occ *self, void *) { Ev(0x33, self->id, uint32_t(self->refs - 1)); return uint32_t(--self->refs); }
Occ *__fastcall OccFace(Occ *self, void *) { Ev(0x34, self->id); return self->next; }
uint32_t __fastcall OccLayer(Occ *self, void *) { Ev(0x35, self->id); return self->layer; }
void __fastcall OccBounds(Occ *self, void *, int32_t *out)
{
    Ev(0x36, self->id);
    std::memcpy(out, self->bounds, 16);
}
uint32_t __fastcall OccFlag12(Occ *self, void *) { Ev(0x3a, self->id); return self->flag12; }
template <int Slot> void __fastcall OccWrite(Occ *self, void *, void *stream)
{
    Ev(0x37, self->id, Slot);
    reinterpret_cast<ifaces::cIGZOStream *>(stream)->SetUint32(self->id);
}
template <int Slot> uint8_t __fastcall OccRead(Occ *self, void *, void *stream, uint32_t sub)
{
    uint32_t payload = 0;
    reinterpret_cast<ifaces::cIGZIStream *>(stream)->GetUint32(&payload);
    // The sub-version is a 16-bit read into a 32-bit stack slot of the original, pushed whole: the upper half is whatever the
    // stack held, so only the low half is compared (ours passes it zero-extended).
    Ev(0x38, self->id, Slot, sub & 0xffff);
    Ev(0x39, payload);
    return self->readOk ? 0x41 : 0;
}

Occ *NewOcc(uint32_t mask, uint32_t cells = 12)
{
    gOccs.emplace_back();
    Occ &o = gOccs.back();
    o.vt = gOccVT;
    o.refs = 1;
    o.id = ++gNextId;
    o.next = nullptr;
    o.layer = o.id % 2;
    o.flag12 = 1;
    const int32_t span = int32_t(cells * 4 + 12);
    const int32_t x0 = int32_t((o.id * 37u) % uint32_t(span)) - 8, y0 = int32_t((o.id * 53u) % uint32_t(span)) - 8;
    const int32_t w = int32_t((o.id * 11u) % 13u) + (gCreateBounds & 1 ? 5 : 0), h = int32_t((o.id * 7u) % 9u);
    o.bounds[0] = x0; o.bounds[1] = y0; o.bounds[2] = x0 + w; o.bounds[3] = y0 + h;
    o.iidMask = mask;
    o.readOk = 1;
    o.type3 = (o.id % 8) | (o.id % 3) << 8;
    return &o;
}

// The occupant the manager's Create function hands out: the type face, whose slot 3 gives the grid object (for a road with a
// model one more step); the grid object answers the kind's iid (and whatever else the plan adds).
void *CreateOf(uint32_t kind)
{
    Occ *type = NewOcc(0);
    Occ *face = NewOcc(gCreateMasks[kind]);
    type->next = face;
    face->flag12 = (gCreateFlag12 >> (gCreated & 31)) & 1;
    ++gCreated;
    type->readOk = gReadIndex < gReadOk.size() ? gReadOk[gReadIndex] : 1;
    ++gReadIndex;
    if (kind == kModelRoad) {
        Occ *inner = NewOcc(gCreateMasks[kind]);
        face->next = inner;
        face->iidMask = 0;
        inner->bounds[2] += 2;
    }
    Ev(0x40, kind, type->id, Norm(type));
    return type;
}
void *__fastcall MakeFlora(void *, void *) { return CreateOf(kFlora); }
void *__fastcall MakeRoad(void *, void *) { return CreateOf(kRoad); }
void *__fastcall MakeModelRoad(void *, void *) { return CreateOf(kModelRoad); }
void *__fastcall MakeProp(void *, void *) { return CreateOf(kProp); }
uint8_t __fastcall FaceFindInRect(void *, void *, const int32_t *rect, uint32_t mask, void *out)
{
    Ev(0x41, uint32_t(rect[0]), uint32_t(rect[1]), uint32_t(rect[2]));
    Ev(0x42, uint32_t(rect[3]), mask, Norm(out));
    return 0;
}

void *__fastcall AppSlot6(Fake *, void *) { Ev(6); return &gNbhd; }
float __fastcall NbhdCell(Fake *, void *) { Ev(50); return gCellSize; }
uint32_t __fastcall NbhdW(Fake *, void *) { Ev(52); return gGridW; }
uint32_t __fastcall NbhdH(Fake *, void *) { Ev(53); return gGridH; }
uint32_t __fastcall ServiceQuery(Fake *, void *, uint32_t a, uint32_t b, void **out)
{
    Ev(5, a, b);
    *out = &gNotifier;
    return 1;
}
uint32_t __fastcall FakeRelease(Fake *self, void *) { Ev(2, self->id); return 1; }
uint32_t __fastcall NotifierAdd(Fake *, void *, const void *arg, uint32_t key) { Ev(5, Norm(arg), key); return 1; }
uint32_t __fastcall NotifierRemove(Fake *, void *, const void *arg, uint32_t key) { Ev(6, Norm(arg), key); return 1; }
uint8_t __fastcall PersistSave(Fake *, void *, const void *resource, const void *segment) { Ev(16, Norm(resource), Norm(segment)); return gSaveRet; }
uint8_t __fastcall PersistFind(Fake *, void *, const uint32_t *key, uint32_t iid, void **out)
{
    Ev(34, key[0], key[1], key[2]);
    Ev(35, key[3], iid);
    if (gFindRet) *out = &gSeg;
    return gFindRet;
}
uint8_t __fastcall SegFlush(Fake *, void *) { Ev(10); return gFlushRet; }
uint8_t __fastcall ServerPost(Fake *, void *, const uint8_t *m, uint32_t flag)
{
    Ev(4, flag, m[8], *reinterpret_cast<const uint32_t *>(m + 0xc));
    Ev(*reinterpret_cast<const uint32_t *>(m + 0x10), *reinterpret_cast<const uint32_t *>(m + 0x14), *reinterpret_cast<const uint32_t *>(m + 0x18), *reinterpret_cast<const uint32_t *>(m + 4));
    Ev(Norm(*reinterpret_cast<void *const *>(m + 0x1c)), Norm(*reinterpret_cast<void *const *>(m + 0x20)), Norm(*reinterpret_cast<void *const *>(m + 0x24)), Norm(*reinterpret_cast<void *const *>(m + 0x28)));
    return 1;
}
uint8_t __fastcall ServerSend(Fake *, void *, const uint8_t *m)
{
    Ev(3, m[8], *reinterpret_cast<const uint32_t *>(m + 0xc));
    Ev(*reinterpret_cast<const uint32_t *>(m + 0x10), *reinterpret_cast<const uint32_t *>(m + 0x14), *reinterpret_cast<const uint32_t *>(m + 0x18), *reinterpret_cast<const uint32_t *>(m + 4));
    Ev(Norm(*reinterpret_cast<void *const *>(m + 0x1c)), Norm(*reinterpret_cast<void *const *>(m + 0x20)), Norm(*reinterpret_cast<void *const *>(m + 0x24)), Norm(*reinterpret_cast<void *const *>(m + 0x28)));
    return 1;
}
// lane FY: the grid / road slots
void *gRoadVT[40]{};
uint32_t __fastcall OccType3(Occ *self, void *) { Ev(0x43, self->id); return self->type3; }
void __fastcall OccReset(Occ *self, void *, uint32_t arg) { Ev(0x44, self->id, arg); }
uint32_t __fastcall OccS19(Occ *self, void *) { Ev(0x45, self->id); return self->s19; }
void __fastcall OccMark(Occ *self, void *, uint32_t value) { Ev(0x46, self->id, value); self->mark = value; }
uint32_t __fastcall OccS25(Occ *self, void *) { Ev(0x47, self->id); return self->s25; }
// the incoming message of DoMessage and its data interface
uint8_t __fastcall MsgQI(Fake *, void *, uint32_t iid, void **out)
{
    Ev(0x51, iid);
    if (!gMsgHasData) return 0;
    *out = &gData;
    return 1;
}
uint32_t __fastcall DataRelease(Fake *, void *) { Ev(0x52); return 1; }
uint32_t __fastcall DataType(Fake *, void *) { Ev(0x53); return gMsgType; }
uint32_t __fastcall DataX(Fake *, void *) { Ev(0x54); return gMsgX; }
uint32_t __fastcall DataY(Fake *, void *) { Ev(0x55); return gMsgY; }

// ---- the routes (docs/standalone-shape.md, "Tests"; the model is FR's tests/ts_neighbor_manager_shape.h) ----
// Route 0: the original RVA, on an object carrying copies of the game's vtables. Route 1: our vtable's slot (the slot where the
// game's vtable of that face holds the RVA), on an object carrying copies of our vtables. Route 2: the adapter, on the game's
// vtables, with an arity-matched trap in the slot it forwards to (an adapter that dispatched through the vtable is caught). A
// member that is no slot (kNoFace) runs its adapter on routes 1 and 2. The fixture's fakes (gPatch) are patched over every
// route's copies; only the vtable words are normalised (VtCode).
int gRoute = 0;
unsigned gTrapHits = 0;
constexpr unsigned kFaces = 5, kMaxSlots = 26, kNoFace = ~0u;
constexpr uint32_t kFaceOffset[kFaces] = {0, nhoodocc::kManagerFace, nhoodocc::kSerialFace, nhoodocc::kPersistFace, nhoodocc::kMessageFace};
constexpr unsigned kFaceSlots[kFaces] = {6, 26, 6, 12, 4};
constexpr uint32_t kGameVt[kFaces] = {Mgr::kVTable, Mgr::kManagerVTable, Mgr::kSerialVTable, Mgr::kPersistVTable, Mgr::kMessageVTable};
constexpr uint32_t kGameBaseVTable = 0xda2fd0; // what the original's destructor leaves at +0 (~cRZUnknown)
// Stack words per slot (a trap returns with the slot's `ret N`).
constexpr uint8_t kArity[kFaces][kMaxSlots] = {
    {2, 0, 0, 1, 0, 0},
    {2, 0, 0, 0, 0, 0, 1, 1, 1, 1, 3, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 2, 1, 0},
    {2, 0, 0, 1, 1, 0},
    {2, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0},
    {2, 0, 0, 1}};
void *gCopy[kFaces][kMaxSlots]; // the vtables the object carries
void *gPatch[kFaces][kMaxSlots]; // the fixture's fakes (null: the route's own function)

void *const *GameVt(unsigned face) { return shape::GameVTable(kGameVt[face]); }
void *const *OurVt(unsigned face) { return shape::Ours<Mgr>(kFaceOffset).face[face]; }
// The vtables a route's object carries (before the fakes): ours on route 1, the game's on the others.
void *const *OwnVt(unsigned face) { return gRoute == 1 ? OurVt(face) : GameVt(face); }
// The class vtable the route's code stores (the constructor's and destructor's stores): the game's on route 0, ours on the
// others (the adapters construct and destroy with our class).
void *const *RouteVt(unsigned face) { return gRoute == 0 ? GameVt(face) : OurVt(face); }

void Install(void *object)
{
    for (unsigned f = 0; f < kFaces; ++f) {
        void *const *own = OwnVt(f);
        for (unsigned s = 0; s < kFaceSlots[f]; ++s) gCopy[f][s] = gPatch[f][s] ? gPatch[f][s] : own[s];
        shape::VTableAt(object, kFaceOffset[f]) = gCopy[f];
    }
}

// A vtable word as a code: the installed copy of face f (0x7c00 + f), the route's class vtable of face f (0x7d00 + f), the base
// vtable a destructor leaves at +0 (0x7e00), or raw.
uint32_t VtCode(unsigned face, uint32_t word)
{
    if (word == shape::P(gCopy[face])) return 0x7c00 + face;
    if (word == shape::P(RouteVt(face))) return 0x7d00 + face;
    if (face == 0 && (word == shape::P(shape::GameVTable(kGameBaseVTable)) || word == shape::P(shape::Ours<Mgr>(kFaceOffset).destroyed)))
        return 0x7e00;
    return word;
}

uint32_t TrapHit(void *, unsigned)
{
    ++gTrapHits;
    return 0;
}

// Runs the function at `rva` (a slot of face `face`, or kNoFace) the route's way on the manager `m`: the original RVA, our
// vtable's slot, or the adapter with a trap in the slot (restored after the call). kNoFace: the adapter on routes 1 and 2.
template <class Fn, class... A>
auto OccCall(difftest::Context &ctx, void *m, uint32_t rva, unsigned face, void *adapter, A... args)
{
    auto self = shape::SelfAt<Fn>(m, face == kNoFace ? 0 : kFaceOffset[face]);
    if (gRoute == 0) return ctx.Original<Fn>(rva)(self, args...);
    if (face == kNoFace) return reinterpret_cast<Fn>(adapter)(self, args...);
    unsigned slot = shape::SlotOf(kGameVt[face], kFaceSlots[face], rva);
    if (slot == ~0u) ctx.Fail("rva %x is not in face %u's vtable", rva, face), slot = 0;
    if (gRoute == 1) return reinterpret_cast<Fn>(OurVt(face)[slot])(self, args...);
    void *const saved = gCopy[face][slot];
    gCopy[face][slot] = shape::TrapFor<TrapHit>(kArity[face][slot]);
    return shape::ThenAfter([&] { return reinterpret_cast<Fn>(adapter)(self, args...); }, [&] { gCopy[face][slot] = saved; });
}
#define OCC_ADAPTER(name) reinterpret_cast<void *>(&cTSNHoodOccupantManagerAdapters::name)

void SetupFakes()
{
    std::memset(gPatch, 0, sizeof gPatch);
    gOccVT[0] = reinterpret_cast<void *>(&OccQI);
    gOccVT[1] = reinterpret_cast<void *>(&OccAddRef);
    gOccVT[2] = reinterpret_cast<void *>(&OccRelease);
    gOccVT[3] = reinterpret_cast<void *>(&OccFace);
    gOccVT[7] = reinterpret_cast<void *>(&OccLayer);
    gOccVT[8] = reinterpret_cast<void *>(&OccBounds);
    gOccVT[9] = reinterpret_cast<void *>(&OccWrite<9>);
    gOccVT[21] = reinterpret_cast<void *>(&OccWrite<21>);
    gOccVT[10] = reinterpret_cast<void *>(&OccRead<10>);
    gOccVT[22] = reinterpret_cast<void *>(&OccRead<22>);
    gOccVT[12] = reinterpret_cast<void *>(&OccFlag12);
    gAppVT[6] = reinterpret_cast<void *>(&AppSlot6);
    gNbhdVT[50] = reinterpret_cast<void *>(&NbhdCell);
    gNbhdVT[52] = reinterpret_cast<void *>(&NbhdW);
    gNbhdVT[53] = reinterpret_cast<void *>(&NbhdH);
    gServiceVT[5] = reinterpret_cast<void *>(&ServiceQuery);
    gNotifierVT[2] = gSegVT[2] = reinterpret_cast<void *>(&FakeRelease);
    gNotifierVT[5] = reinterpret_cast<void *>(&NotifierAdd);
    gNotifierVT[6] = reinterpret_cast<void *>(&NotifierRemove);
    gPersistVT[16] = reinterpret_cast<void *>(&PersistSave);
    gPersistVT[34] = reinterpret_cast<void *>(&PersistFind);
    gSegVT[10] = reinterpret_cast<void *>(&SegFlush);
    gServerVT[4] = reinterpret_cast<void *>(&ServerPost);
    gServerVT[3] = reinterpret_cast<void *>(&ServerSend);
    std::memcpy(gRoadVT, gOccVT, sizeof gRoadVT);
    gRoadVT[3] = reinterpret_cast<void *>(&OccType3);
    gRoadVT[10] = reinterpret_cast<void *>(&OccReset);
    gRoadVT[19] = reinterpret_cast<void *>(&OccS19);
    gRoadVT[23] = reinterpret_cast<void *>(&OccMark);
    gRoadVT[25] = reinterpret_cast<void *>(&OccS25);
    gMsgVT[0] = reinterpret_cast<void *>(&MsgQI);
    gDataVT[2] = reinterpret_cast<void *>(&DataRelease);
    gDataVT[4] = reinterpret_cast<void *>(&DataType);
    gDataVT[9] = reinterpret_cast<void *>(&DataX);
    gDataVT[14] = reinterpret_cast<void *>(&DataY);
    gPatch[1][10] = reinterpret_cast<void *>(&FaceFindInRect);
    gPatch[1][12] = reinterpret_cast<void *>(&MakeFlora);
    gPatch[1][14] = reinterpret_cast<void *>(&MakeRoad);
    gPatch[1][16] = reinterpret_cast<void *>(&MakeModelRoad);
    gPatch[1][18] = reinterpret_cast<void *>(&MakeProp);
}

// The game cells the code under test reads, saved and restored around a test.
struct Cells {
    uint32_t app, persist, service, server;
    Cells()
    {
        app = *GameAddress<uint32_t>(0x10cc288);
        persist = *GameAddress<uint32_t>(0x10faf6c);
        service = *GameAddress<uint32_t>(0x10c9378);
        server = *GameAddress<uint32_t>(0x10f519c);
        SetupFakes();
        *GameAddress<void *>(0x10cc288) = &gApp;
        *GameAddress<void *>(0x10faf6c) = &gPersist;
        *GameAddress<void *>(0x10c9378) = &gService;
        *GameAddress<void *>(0x10f519c) = &gServer;
    }
    ~Cells()
    {
        *GameAddress<uint32_t>(0x10cc288) = app;
        *GameAddress<uint32_t>(0x10faf6c) = persist;
        *GameAddress<uint32_t>(0x10c9378) = service;
        *GameAddress<uint32_t>(0x10f519c) = server;
    }
};

// ---- the manager under test ----

struct Plan {
    uint32_t pre[4] = {0, 0, 0, 0}; // entries already in the four lists
    uint32_t initialized = 0;
    uint32_t cx = 3, cy = 2;        // grid cells each way (the terrain is four times that)
    uint32_t seedCells = 4;         // pre-existing occupants spread over the cells
};

uint32_t Bits(float f) { uint32_t b; std::memcpy(&b, &f, 4); return b; }

void FillList(RefPtrVector &list, uint32_t count, std::mt19937 &rng, uint32_t kind, uint32_t cells)
{
    const uint32_t capacity = count + rng() % 3;
    list.mFirst = capacity ? static_cast<void **>(::operator new(capacity * 4)) : nullptr;
    list.mLast = list.mFirst;
    list.mEnd = list.mFirst + capacity;
    for (uint32_t i = 0; i < count; ++i) {
        Occ *o = NewOcc(1u << kind, cells);
        ++o->refs; // the list's count
        *list.mLast++ = o;
    }
}

uint32_t gSkip;
uint32_t gExtraGroups; // road groups added to the seeded 0-2
Mgr *MakeManager(uint32_t seed, const Plan &plan)
{
    std::mt19937 rng(seed);
    auto *raw = static_cast<uint8_t *>(::operator new(sizeof(Mgr)));
    for (size_t i = 0; i < sizeof(Mgr); ++i) raw[i] = uint8_t(rng());
    auto *m = reinterpret_cast<Mgr *>(raw);
    Install(m);
    m->mRefCount = 1 + seed % 3;
    m->mInitialized = uint8_t(plan.initialized);
    m->mDeferVertexUpdates = uint8_t(rng() & 1);
    m->mGridCellSize = 40.0f;
    m->mTerrainWidth = plan.cx * 4;
    m->mTerrainHeight = plan.cy * 4;
    m->mGridCellsX = plan.cx;
    m->mGridCellsY = plan.cy;
    const uint32_t cells = plan.cx;
    FillList(m->mFlora, plan.pre[0], rng, kFlora, cells);
    FillList(m->mRoads, plan.pre[1], rng, kRoad, cells);
    FillList(m->mModelRoads, plan.pre[2], rng, kModelRoad, cells);
    FillList(m->mProps, plan.pre[3], rng, kProp, cells);
    new (&m->mGrid) std::vector<RefPtrVector>(plan.cx * plan.cy);
    for (RefPtrVector &cell : m->mGrid) cell = RefPtrVector{};
    for (uint32_t i = 0; i < ((gSkip & 2) ? 0 : plan.seedCells) && !m->mGrid.empty(); ++i) {
        Occ *o = NewOcc(0, cells);
        o->bounds[0] = o->bounds[2] = int32_t(rng() % (plan.cx * 4));
        o->bounds[1] = o->bounds[3] = int32_t(rng() % (plan.cy * 4));
        RefPtrVector &cell = m->mGrid[rng() % m->mGrid.size()];
        const uint32_t capacity = 1 + rng() % 3;
        cell.mFirst = static_cast<void **>(RZMemory::Malloc(capacity * 4)); // the cells live in the allocator service
        cell.mLast = cell.mFirst;
        cell.mEnd = cell.mFirst + capacity;
        ++o->refs;
        *cell.mLast++ = o;
    }
    new (&m->mPendingVertices) std::unordered_set<uint32_t>();
    for (uint32_t i = 0, n = (gSkip & 4) ? 0 : rng() % 4; i < n; ++i) m->mPendingVertices.insert((rng() % 9 << 16) + rng() % 9);
    new (&m->mMessageIds) std::set<uint32_t>{Mgr::kMessageHeightChanged, Mgr::kMessageBeginDefer, Mgr::kMessageEndDefer};
    for (auto &k : m->mKey) k = rng();
    new (&m->mRoadGroups) std::vector<cTSNHoodRoadGroup>();
    for (uint32_t i = 0, n = (gSkip & 1) ? 0 : rng() % 3 + gExtraGroups; i < n; ++i) {
        // the groups' own storage comes from the game's allocator service, as the destructor frees it
        const uint32_t count = 1 + rng() % 3;
        cTSNHoodRoadGroup group;
        for (uint32_t k = 0; k < count; ++k) group.mTiles.push_back({int32_t(rng() % 1000), int32_t(rng() % 1000)});
        m->mRoadGroups.push_back(group);
    }
    return m;
}

void CaptureList(Blob &b, const RefPtrVector &v)
{
    b.insert(b.end(), {uint32_t(v.mLast - v.mFirst), uint32_t(v.mEnd - v.mFirst)});
    for (void **p = v.mFirst; p != v.mLast; ++p) b.push_back(OccId(*p));
}

Blob Capture(Mgr *m)
{
    Blob b;
    for (unsigned f = 0; f < kFaces; ++f) b.push_back(VtCode(f, shape::Word(m, kFaceOffset[f])));
    b.insert(b.end(), {m->mRefCount, m->mInitialized, m->mDeferVertexUpdates, Bits(m->mGridCellSize), m->mTerrainWidth,
        m->mTerrainHeight, m->mGridCellsX, m->mGridCellsY});
    CaptureList(b, m->mFlora);
    CaptureList(b, m->mRoads);
    CaptureList(b, m->mModelRoads);
    CaptureList(b, m->mProps);
    b.insert(b.end(), {uint32_t(m->mGrid.size()), uint32_t(m->mGrid.capacity())});
    for (const RefPtrVector &cell : m->mGrid) CaptureList(b, cell);
    std::vector<uint32_t> pending(m->mPendingVertices.begin(), m->mPendingVertices.end());
    std::sort(pending.begin(), pending.end());
    b.push_back(uint32_t(pending.size()));
    b.insert(b.end(), pending.begin(), pending.end());
    b.push_back(uint32_t(m->mMessageIds.size()));
    for (uint32_t id : m->mMessageIds) b.push_back(id);
    b.insert(b.end(), std::begin(m->mKey), std::end(m->mKey));
    // The groups and their tiles (slot 22 builds them, slot 23 copies one out). Capacities are compared too: the group vector's
    // growth and the copies' exact sizes are the original's.
    b.insert(b.end(), {uint32_t(m->mRoadGroups.size()), uint32_t(m->mRoadGroups.capacity())});
    for (const cTSNHoodRoadGroup &g : m->mRoadGroups) {
        b.insert(b.end(), {uint32_t(g.mTiles.size()), uint32_t(g.mTiles.capacity())});
        for (const cTSNHoodRoadTile &t : g.mTiles) b.insert(b.end(), {uint32_t(t.x), uint32_t(t.y)});
    }
    for (const Occ &o : gOccs) b.insert(b.end(), {o.id, uint32_t(o.refs)});
    return b;
}

void Destroy(difftest::Context &ctx, Mgr *m) { ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x4a84a0)(m, 1); }

void ResetWorld()
{
    gOccs.clear();
    gNextId = 0;
    gReadIndex = 0;
    gCreated = 0;
}

using Call = uint32_t (*)(difftest::Context &, bool ours, Mgr *);
bool gCatchAll;
Blob Run(difftest::Context &ctx, Call call, int route, uint32_t seed, const Plan &plan, const std::vector<Item> &script, int fail, int *total)
{
    gRoute = route;
    gTrapHits = 0;
    Begin(script, fail);
    ResetWorld();
    Mgr *m = MakeManager(seed, plan);
    gBase = m;
    uint32_t returned = 0;
    uint32_t threw = 0;
    try { returned = call(ctx, route != 0, m); } catch (int) { threw = 1; } catch (...) { threw = 2; }
    gFailAt = -1;
    if (total) *total = gTick;
    Blob blob{returned, threw, uint32_t(gCursor), gTrapHits};
    blob.insert(blob.end(), gLog.begin(), gLog.end());
    Blob state = Capture(m);
    blob.insert(blob.end(), state.begin(), state.end());
    gBase = nullptr;
    Destroy(ctx, m);
    gRoute = 0;
    return blob;
}

void Compare(difftest::Context &ctx, const char *name, uint32_t index, const Blob &original, const Blob &ours)
{
    ++ctx.cases;
    if (original == ours) return;
    size_t at = 0;
    while (at < original.size() && at < ours.size() && original[at] == ours[at]) ++at;
    auto w = [&](const Blob &b, size_t i) { return i < b.size() && i + 1 > 0 ? b[i] : 0xffffffffu; };
    ctx.Fail("%s case %u: result / state / fake calls differ at word %zu of %zu/%zu: orig %08x %08x [%08x] %08x vs ours %08x %08x [%08x] %08x", name,
        index, at, original.size(), ours.size(), at >= 2 ? w(original, at - 2) : 0, at >= 1 ? w(original, at - 1) : 0, w(original, at), w(original, at + 1),
        at >= 2 ? w(ours, at - 2) : 0, at >= 1 ? w(ours, at - 1) : 0, w(ours, at), w(ours, at + 1));
}

// Both sides over every plan and script (and, with `inject`, a throw at every stream call).
void Sweep(difftest::Context &ctx, Call call, const std::vector<Plan> &plans, const std::vector<std::vector<Item>> &scripts, const char *name, bool inject = true)
{
    uint64_t before = gThrown;
    uint32_t index = 0;
    for (const Plan &plan : plans) {
        for (const auto &script : scripts) {
            int total = 0;
            for (int fail = -1; fail < (inject ? total : 0); fail += (fail < 0 || total <= 16) ? 1 : total / 16) { // a throw at each call; past 16 calls, at 16 spread over them (a run is slow here)
                const uint32_t seed = index * 31 + 7;
                Blob original = Run(ctx, call, 0, seed, plan, script, fail, fail < 0 ? &total : nullptr); // only the clean run counts the calls
                for (int route = 1; route < 3; ++route) {
                    char label[96];
                    std::snprintf(label, sizeof label, "%s (route %d)", name, route);
                    Compare(ctx, label, index, original, Run(ctx, call, route, seed, plan, script, fail, nullptr));
                }
            }
            ++index;
        }
    }
    if (inject) std::printf("  %s: exceptions injected %llu\n", name, gThrown - before);
}

// Our class's vtables are read once from an object our constructor builds (shape::Ours), whose generator is seeded from the
// clock: the clock seed is fixed while a test runs.
uint32_t ScratchClockSeed(int, uint32_t) { return 777; }
struct ClockScope {
    uint32_t (*saved)(int, uint32_t) = gRZRandomClockSeedOverride;
    ClockScope() { gRZRandomClockSeedOverride = &ScratchClockSeed; }
    ~ClockScope() { gRZRandomClockSeedOverride = saved; }
};

#define OCC_TEST(name, rva)                                                                                             \
    static void name##Body(difftest::Context &ctx);                                                                      \
    DIFF_TEST(name, rva)                                                                                                 \
    {                                                                                                                    \
        svcfx::AllocatorScope allocator;                                                                                 \
        ClockScope clock;                                                                                                \
        (void)OurVt(0); /* our vtables read before anything is compared (the scratch object allocates) */               \
        Cells cells;                                                                                                     \
        gSaveRet = gFindRet = gFlushRet = 1;                                                                             \
        gGridW = 12; gGridH = 8; gCellSize = 10.0f;                                                                      \
        gCreateMasks[0] = 1; gCreateMasks[1] = 2; gCreateMasks[2] = 4; gCreateMasks[3] = 8;                              \
        gCreateFlag12 = 0xffffffffu; gCreateBounds = 0; gReadOk.clear(); gMsgHasData = true; gExtraGroups = 0;           \
        name##Body(ctx);                                                                                                 \
    }                                                                                                                    \
    static void name##Body(difftest::Context &ctx)

// A decorations record: the version, then four lists of (sub-version, count, one payload word per entry).
std::vector<Item> Record(uint32_t version, const uint32_t counts[4], bool withLists = true)
{
    std::vector<Item> s{U(version)};
    if (!withLists) return s;
    const uint32_t subs[4] = {8, 3, 3, 8};
    for (int k = 0; k < 4; ++k) {
        s.push_back(U(subs[k]));
        s.push_back(U(counts[k]));
        for (uint32_t i = 0; i < (std::min)(counts[k], 16u); ++i) s.push_back(U(0x1000 * (k + 1) + i));
    }
    return s;
}
} // namespace

OCC_TEST(NHoodOcc_Read, 0x4aa110)
{
    std::vector<std::vector<Item>> scripts{{}, {U(0)}, {U(0x6c)}, {U(0x6d)}};
    const uint32_t shapes[][4] = {{0, 0, 0, 0}, {1, 1, 1, 1}, {3, 2, 4, 5}, {7, 0, 3, 0}, {0, 6, 0, 2}};
    for (uint32_t version : {0x6du, 0x6fu, 0xcbu, 0x100u})
        for (const auto &counts : shapes) scripts.push_back(Record(version, counts));
    {
        const uint32_t counts[4] = {2, 2, 2, 2};
        for (size_t cut = 1; cut < 24; cut += 1) {
            auto s = Record(0x6f, counts);
            s.resize((std::min)(cut, s.size()));
            scripts.push_back(s);
        }
        const uint32_t huge[4] = {0x40000000u, 0, 0, 0};
        scripts.push_back(Record(0x6f, huge));                              // flora count over max_size: length_error
        const uint32_t huge2[4] = {1, 0x40000000u, 0, 0};
        scripts.push_back(Record(0x6f, huge2));
        const uint32_t huge3[4] = {0, 0, 0x40000000u, 0};
        scripts.push_back(Record(0x6f, huge3));
        const uint32_t huge4[4] = {0, 0, 0, 0x40000000u};
        scripts.push_back(Record(0x6f, huge4));
    }
    std::vector<Plan> plans(4);
    plans[1].initialized = 1;
    plans[1].pre[0] = 3; plans[1].pre[1] = 2; plans[1].pre[2] = 4; plans[1].pre[3] = 1; // existing entries: the model roads stay
    plans[2].cx = 1; plans[2].cy = 1;
    plans[3].cx = 4; plans[3].cy = 3; plans[3].pre[2] = 1;
    // Occupant Reads: all succeed; then one per list fails; flags of slot 12 vary; the grid objects answer the kind's iid.
    struct Variant { std::vector<uint8_t> readOk; uint32_t flag12; uint32_t masks[4]; uint32_t bounds; };
    const Variant variants[] = {
        {{}, 0xffffffffu, {1, 2, 4, 8}, 0},
        {{1, 1, 0, 1, 1}, 0xffffffffu, {1, 2, 4, 8}, 1},
        {{1, 1, 1, 1, 1, 1, 0}, 0xaaaaaaaau, {1, 2, 4, 8}, 0},
        {{0, 1, 1, 1}, 0x00000000u, {1, 2, 4, 8}, 0},
        {{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0}, 0x55555555u, {0xf, 0xe, 0xc, 8}, 1},
    };
    for (const Variant &v : variants) {
        gReadOk = v.readOk;
        gCreateFlag12 = v.flag12;
        std::memcpy(gCreateMasks, v.masks, sizeof gCreateMasks);
        gCreateBounds = v.bounds;
        Sweep(ctx, [](difftest::Context &c, bool ours, Mgr *m) -> uint32_t {
            (void)ours;
            auto *stream = reinterpret_cast<ifaces::cIGZIStream *>(&gIn);
            return OccCall<uint8_t(__thiscall *)(void *, void *)>(c, m, 0x4aa110, 2, OCC_ADAPTER(Read), stream);
        }, plans, scripts, "Read", &v == &variants[0] || &v == &variants[2]);
    }
}

OCC_TEST(NHoodOcc_Write, 0x4ab6f0)
{
    std::vector<Plan> plans(4);
    plans[0].pre[0] = 0;
    plans[1].pre[0] = 1; plans[1].pre[1] = 1; plans[1].pre[2] = 1; plans[1].pre[3] = 1;
    plans[2].pre[0] = 4; plans[2].pre[1] = 2; plans[2].pre[2] = 3; plans[2].pre[3] = 5;
    plans[3].pre[1] = 3;
    Sweep(ctx, [](difftest::Context &c, bool ours, Mgr *m) -> uint32_t {
        (void)ours;
        auto *stream = reinterpret_cast<ifaces::cIGZOStream *>(&gOut);
        return OccCall<uint8_t(__thiscall *)(void *, void *)>(c, m, 0x4ab6f0, 2, OCC_ADAPTER(Write), stream);
    }, plans, {{}}, "Write");
}

OCC_TEST(NHoodOcc_GetGZCLSID, 0x4a9250)
{
    uint32_t results[3];
    for (int route = 0; route < 3; ++route) {
        gRoute = route;
        gTrapHits = 0;
        Mgr *m = MakeManager(1, Plan{});
        results[route] = OccCall<uint32_t(__thiscall *)(void *)>(ctx, m, 0x4a9250, 2, OCC_ADAPTER(GetGZCLSID)) + gTrapHits;
        Destroy(ctx, m);
    }
    gRoute = 0;
    if (results[1] != results[0] || results[2] != results[0]) ctx.Fail("GetGZCLSID differs");
    ++ctx.cases;
}


// ---- the rest: construction, grid, lists, messages ----

namespace {
uint32_t gShift;    // extra occupants made before the one a case uses (varies its footprint)
uint32_t gArgMask;  // iid mask of the occupant a case adds
uint32_t gOutMode;  // Get functions: how full the caller's vector is
uint32_t gDtorFlags;

uint32_t Fold(uint32_t acc, uint32_t v) { return (acc ^ v) * 16777619u + 0x9e3779b9u; }
Occ *CaseOcc(uint32_t mask)
{
    for (uint32_t i = 0; i < gShift; ++i) NewOcc(0);
    Occ *o = NewOcc(mask);
    o->next = o;
    return o;
}
std::vector<Plan> GridPlans()
{
    std::vector<Plan> plans(5);
    plans[1].pre[0] = 3; plans[1].pre[1] = 2; plans[1].pre[2] = 4; plans[1].pre[3] = 1; plans[1].seedCells = 9;
    plans[2].cx = 1; plans[2].cy = 1; plans[2].seedCells = 2;
    plans[3].cx = 5; plans[3].cy = 4; plans[3].seedCells = 0;
    plans[4].cx = 2; plans[4].cy = 3; plans[4].pre[3] = 2; plans[4].initialized = 1;
    return plans;
}
#define CASE_LAMBDA(...) [](difftest::Context &c, bool ours, Mgr *m) -> uint32_t { (void)c; (void)ours; __VA_ARGS__ }
} // namespace

OCC_TEST(NHoodOcc_InitGrid, 0x4a9b10)
{
    const uint32_t sizes[][2] = {{12, 8}, {0, 0}, {3, 3}, {16, 16}, {4, 100}, {1000, 8}};
    for (auto &size : sizes) for (float cell : {10.0f, 0.25f}) {
        gGridW = size[0]; gGridH = size[1]; gCellSize = cell;
        Sweep(ctx, CASE_LAMBDA(
            OccCall<void(__thiscall *)(void *)>(c, m, 0x4a9b10, kNoFace, OCC_ADAPTER(InitGrid));
            return 0;), GridPlans(), {{}}, "InitGrid", false);
    }
}

OCC_TEST(NHoodOcc_BoundsToCellRect, 0x4a9370)
{
    std::vector<Plan> plans;
    for (uint32_t cx : {0u, 1u, 3u, 7u}) for (uint32_t cy : {0u, 2u, 5u}) { Plan p; p.cx = cx; p.cy = cy; p.seedCells = 0; plans.push_back(p); }
    Sweep(ctx, CASE_LAMBDA(
        uint32_t acc = 0;
        const int32_t values[] = {-1000, -9, -5, -4, -3, -1, 0, 1, 3, 4, 5, 7, 8, 11, 12, 13, 27, 100, 5000};
        for (int32_t a : values) for (int32_t b : {-7, 0, 6, 33, 4000}) {
            const int32_t bounds[4] = {a, b, b + a, a - b};
            int32_t out[4] = {0x55, 0x55, 0x55, 0x55};
            OccCall<void(__thiscall *)(void *, const int32_t *, int32_t *)>(c, m, 0x4a9370, kNoFace, OCC_ADAPTER(BoundsToCellRect), bounds, out);
            for (int32_t v : out) acc = Fold(acc, uint32_t(v));
        }
        return acc;), plans, {{}}, "BoundsToCellRect", false);
}

OCC_TEST(NHoodOcc_InsertIntoGrid, 0x4a9c10)
{
    for (gShift = 0; gShift < 14; ++gShift) {
        gCreateBounds = gShift;
        Sweep(ctx, CASE_LAMBDA(
            Occ *a = CaseOcc(0), *b = CaseOcc(0);
            std::memcpy(b->bounds, a->bounds, 16);
            for (Occ *o : {a, a, b}) { OccCall<void(__thiscall *)(void *, void *)>(c, m, 0x4a9c10, kNoFace, OCC_ADAPTER(InsertIntoGrid), o); }
            a->bounds[0] += 9; a->bounds[3] += 5;
            OccCall<void(__thiscall *)(void *, void *)>(c, m, 0x4a9c10, kNoFace, OCC_ADAPTER(InsertIntoGrid), a);
            return 0;), GridPlans(), {{}}, "InsertIntoGrid", false);
    }
    gShift = 0;
}

OCC_TEST(NHoodOcc_RemoveFromGrid, 0x4aa550)
{
    for (gShift = 0; gShift < 14; ++gShift) {
        Sweep(ctx, CASE_LAMBDA(
            Occ *o[3] = {CaseOcc(0), CaseOcc(0), CaseOcc(0)};
            for (Occ *x : o) { std::memcpy(x->bounds, o[0]->bounds, 16); c.Original<void(__thiscall *)(void *, void *)>(0x4a9c10)(m, x); }
            for (Occ *x : {o[1], o[0], o[0], o[2]}) { OccCall<void(__thiscall *)(void *, void *)>(c, m, 0x4aa550, kNoFace, OCC_ADAPTER(RemoveFromGrid), x); }
            return 0;), GridPlans(), {{}}, "RemoveFromGrid", false);
    }
    gShift = 0;
}

OCC_TEST(NHoodOcc_UpdateOccupantPosition, 0x4aa070)
{
    for (gShift = 0; gShift < 8; ++gShift) {
        Sweep(ctx, CASE_LAMBDA(
            Occ *a = CaseOcc(0);
            c.Original<void(__thiscall *)(void *, void *)>(0x4a9c10)(m, a);
            a->bounds[0] += 13; a->bounds[1] -= 6; a->bounds[2] += 20;
            OccCall<void(__thiscall *)(void *, void *)>(c, m, 0x4aa070, 1, OCC_ADAPTER(UpdateOccupantPosition), a);
            return 0;), GridPlans(), {{}}, "UpdateOccupantPosition", false);
    }
    gShift = 0;
}

OCC_TEST(NHoodOcc_AddOccupantInternal, 0x4a9e10)
{
    for (gArgMask = 0; gArgMask < 16; ++gArgMask) for (gShift = 0; gShift < 3; ++gShift) {
        Sweep(ctx, CASE_LAMBDA(
            Occ *a = CaseOcc(gArgMask);
            uint32_t acc = 0;
            for (int i = 0; i < 3; ++i) acc = Fold(acc, OccCall<uint8_t(__thiscall *)(void *, void *)>(c, m, 0x4a9e10, kNoFace, OCC_ADAPTER(AddOccupantInternal), a));
            return acc;), GridPlans(), {{}}, "AddOccupantInternal", false);
    }
    gArgMask = 0; gShift = 0;
}

OCC_TEST(NHoodOcc_AddOccupant, 0x4a9d20)
{
    for (uint32_t argMask : {0u, 1u, 6u, 8u}) {
        gArgMask = argMask;
        Sweep(ctx, CASE_LAMBDA(
            Occ *a = CaseOcc(gArgMask);
            uint32_t acc = 0;
            for (Occ *o : {a, static_cast<Occ *>(nullptr), a})
                acc = Fold(acc, OccCall<uint8_t(__thiscall *)(void *, void *)>(c, m, 0x4a9d20, 1, OCC_ADAPTER(AddOccupant), o));
            return acc;), GridPlans(), {{}}, "AddOccupant", false);
    }
    gArgMask = 0;
}

OCC_TEST(NHoodOcc_Initialize, 0x4a9ad0)
{
    Sweep(ctx, CASE_LAMBDA(
        return OccCall<uint8_t(__thiscall *)(void *)>(c, m, 0x4a9ad0, 1, OCC_ADAPTER(Initialize));), GridPlans(), {{}}, "Initialize", false);
}

OCC_TEST(NHoodOcc_Shutdown, 0x4aad00)
{
    Sweep(ctx, CASE_LAMBDA(
        // the notifier is asked through the service object when the manager was initialised
        return OccCall<uint8_t(__thiscall *)(void *)>(c, m, 0x4aad00, 1, OCC_ADAPTER(Shutdown));), GridPlans(), {{}}, "Shutdown", false);
}

OCC_TEST(NHoodOcc_Save, 0x4aac50)
{
    for (int save : {0, 1, 0x41}) for (int find : {0, 1}) for (int flush : {0, 1, 0x40, 0x80}) {
        gSaveRet = uint8_t(save); gFindRet = uint8_t(find); gFlushRet = uint8_t(flush);
        Sweep(ctx, CASE_LAMBDA(
            return OccCall<uint8_t(__thiscall *)(void *)>(c, m, 0x4aac50, 1, OCC_ADAPTER(Save));), {Plan{}}, {{}}, "Save", false);
    }
}

OCC_TEST(NHoodOcc_GetLayerMask, 0x4a9430)
{
    for (uint32_t layer : {0u, 1u, 2u, 0x100u, 0x80000000u}) {
        gArgMask = layer;
        Sweep(ctx, CASE_LAMBDA(
            Occ *a = CaseOcc(0);
            a->layer = gArgMask;
            return OccCall<uint32_t(__thiscall *)(void *, void *)>(c, m, 0x4a9430, 1, OCC_ADAPTER(GetOccupantLayerMask), a);), {Plan{}}, {{}}, "GetOccupantLayerMask", false);
    }
    gArgMask = 0;
}

namespace {
// The four Get functions: the caller's vector starts empty, partly full or with room to spare (storage from the allocator service).
uint32_t GetList(difftest::Context &c, bool ours, Mgr *m, int which, uint32_t rva)
{
    RefPtrVector out{};
    if (gOutMode) {
        const uint32_t n = gOutMode == 1 ? 1 : 3, cap = gOutMode == 3 ? 8 : n;
        out.mFirst = static_cast<void **>(RZMemory::Malloc(cap * 4));
        out.mLast = out.mFirst;
        out.mEnd = out.mFirst + cap;
        for (uint32_t i = 0; i < n; ++i) *out.mLast++ = NewOcc(0);
    }
    (void)ours;
    void *const adapters[4] = {OCC_ADAPTER(GetFloraOccupants), OCC_ADAPTER(GetRoadOccupants), OCC_ADAPTER(GetModelRoadOccupants),
        OCC_ADAPTER(GetPropOccupants)};
    OccCall<void(__thiscall *)(void *, void *)>(c, m, rva, 1, adapters[which], &out);
    uint32_t acc = Fold(out.Size(), uint32_t(out.mEnd - out.mFirst));
    for (void **p = out.mFirst; p != out.mLast; ++p) acc = Fold(acc, OccId(*p));
    return acc;
}
} // namespace

#define GET_TEST(name, which, rva)                                                                                       \
    OCC_TEST(name, rva)                                                                                                  \
    {                                                                                                                    \
        std::vector<Plan> plans(4);                                                                                      \
        plans[1].pre[0] = plans[1].pre[1] = plans[1].pre[2] = plans[1].pre[3] = 1;                                       \
        plans[2].pre[0] = plans[2].pre[1] = plans[2].pre[2] = plans[2].pre[3] = 4;                                       \
        plans[3].pre[0] = plans[3].pre[1] = plans[3].pre[2] = plans[3].pre[3] = 9;                                       \
        for (gOutMode = 0; gOutMode < 4; ++gOutMode)                                                                     \
            Sweep(ctx, CASE_LAMBDA(return GetList(c, ours, m, which, rva);), plans, {{}}, #name, false);                 \
        gOutMode = 0;                                                                                                    \
    }
GET_TEST(NHoodOcc_GetFloraOccupants, 0, 0x4a92c0)
GET_TEST(NHoodOcc_GetRoadOccupants, 1, 0x4a96f0)
GET_TEST(NHoodOcc_GetModelRoadOccupants, 2, 0x4a97a0)
GET_TEST(NHoodOcc_GetPropOccupants, 3, 0x4a9630)

OCC_TEST(NHoodOcc_DoMessage, 0x4a9070)
{
    const uint32_t types[] = {Mgr::kMessageBeginDefer, Mgr::kMessageHeightChanged, Mgr::kMessageEndDefer, 0x1234, 0};
    const uint32_t points[][2] = {{0, 0}, {5, 7}, {0xffff, 3}, {1, 0x7fff}, {3, 2}};
    std::vector<Plan> plans = GridPlans();
    for (bool hasData : {true, false}) for (uint32_t type : types) for (auto &p : points) {
        gMsgHasData = hasData; gMsgType = type; gMsgX = p[0]; gMsgY = p[1];
        Sweep(ctx, CASE_LAMBDA(
            uint32_t acc = 0;
            for (void *message : {static_cast<void *>(&gMsg), static_cast<void *>(nullptr), static_cast<void *>(&gMsg)})
                acc = Fold(acc, OccCall<uint8_t(__thiscall *)(void *, void *)>(c, m, 0x4a9070, 4, OCC_ADAPTER(DoMessage), message));
            return acc;), plans, {{}}, "DoMessage", false);
    }
    gMsgHasData = true;
}

// The constructor and the factory: the object is compared word by word after the allocator's leftovers are made equal (the block
// is poisoned and freed first, so both sides get the same bytes where the constructor stores nothing); heap pointers inside the
// standard containers are compared as sizes.
namespace {
// The constructor seeds its random generator from the clock (seed 0xffffffff): the original's clock reads are traps in the
// harness, so for these tests the original's cRZRandom constructor is redirected to ours with a fixed seed, and ours is given
// the same seed through the clock-seed hook.
cRZRandom *__fastcall RandomStub(cRZRandom *self, void *, uint32_t seed) { return self->Construct(seed == UINT32_MAX ? 12345u : seed); }
uint32_t FixedClockSeed(int, uint32_t) { return 12345; }
struct RandomRedirect {
    uint8_t *site;
    uint8_t saved[5];
    uint32_t (*savedHook)(int, uint32_t);
    explicit RandomRedirect(difftest::Context &ctx) : site(ctx.image.At<uint8_t>(0x1b8bc8)), savedHook(gRZRandomClockSeedOverride)
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&RandomStub) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
        gRZRandomClockSeedOverride = &FixedClockSeed;
    }
    ~RandomRedirect()
    {
        std::memcpy(site, saved, 5);
        gRZRandomClockSeedOverride = savedHook;
    }
};
uint8_t gPoison = 0xcd; // what freed blocks are filled with: untouched words of a new object show it, stored words do not
struct Image { std::vector<uint32_t> words; uint32_t faceOffset; };
Image MakeImage(difftest::Context &ctx, bool ours, bool viaCreate)
{
    // ours: Create, or the Construct adapter (no slot: routes 1 and 2 are the same call)
    gRoute = ours ? 1 : 0;
    void *poison = ::operator new(sizeof(Mgr));
    std::memset(poison, gPoison, sizeof(Mgr));
    ::operator delete(poison, sizeof(Mgr));
    Mgr *m;
    Image image;
    if (viaCreate) {
        void *face = ours ? static_cast<void *>(Mgr::Create()) : ctx.Original<void *(__cdecl *)()>(0x4a91f0)();
        m = reinterpret_cast<Mgr *>(static_cast<uint8_t *>(face) - 8);
        image.faceOffset = 8;
    } else {
        m = static_cast<Mgr *>(::operator new(sizeof(Mgr)));
        std::memset(m, 0xcd, sizeof(Mgr));
        OccCall<void *(__thiscall *)(void *)>(ctx, m, 0x4a7ff0, kNoFace, OCC_ADAPTER(Construct));
    }
    const auto *w = reinterpret_cast<const uint32_t *>(m);
    for (uint32_t i = 0; i < sizeof(Mgr) / 4; ++i) {
        const uint32_t offset = i * 4;
        const bool heapWord = offset == 0x70 || offset == 0x78 || offset == 0x7c || offset == 0x80 || offset == 0x90;
        uint32_t word = heapWord ? 0 : w[i];
        for (unsigned f = 0; f < kFaces; ++f)
            if (offset == kFaceOffset[f]) word = VtCode(f, word); // the class vtables: the game's on route 0, ours on route 1
        image.words.push_back(word);
    }
    gRoute = 0;
    image.words.push_back(uint32_t(m->mPendingVertices.size()));
    image.words.push_back(uint32_t(m->mPendingVertices.bucket_count()));
    image.words.push_back(Bits(m->mPendingVertices.max_load_factor()));
    image.words.push_back(uint32_t(m->mMessageIds.size()));
    for (uint32_t id : m->mMessageIds) image.words.push_back(id);
    image.words.push_back(uint32_t(reinterpret_cast<const uint32_t *>(m)[0x7c / 4] - reinterpret_cast<const uint32_t *>(m)[0x78 / 4]));
    image.words.push_back(uint32_t(reinterpret_cast<const uint32_t *>(m)[0x80 / 4] - reinterpret_cast<const uint32_t *>(m)[0x78 / 4]));
    ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x4a84a0)(m, 1);
    return image;
}
} // namespace

OCC_TEST(NHoodOcc_Construct, 0x4a7ff0)
{
    RandomRedirect redirect(ctx);
    for (int round = 0; round < 3; ++round) {
        Image original = MakeImage(ctx, false, false), ours = MakeImage(ctx, true, false);
        ++ctx.cases;
        if (original.words != ours.words) {
            size_t at = 0;
            while (at < original.words.size() && original.words[at] == ours.words[at]) ++at;
            ctx.Fail("Construct round %d differs at word %zu (offset %zx): orig %08x ours %08x", round, at, at * 4, original.words[at], ours.words[at]);
        }
    }
}

OCC_TEST(NHoodOcc_Create, 0x4a91f0)
{
    RandomRedirect redirect(ctx);
    for (int round = 0; round < 4; ++round) {
        // Every heap block either side allocates is poisoned (heap_poison.h, DT), so the words the constructor leaves (the
        // three bytes after the flag at +0x18, +0x4c set only by Initialize) hold the pattern on both sides: the whole
        // non-heap object is compared exactly, under two patterns. (Before, a word that differed between two poisoned
        // frees was masked: the heap does not hand back the block just freed, so it held leftovers of earlier managers.)
        Image original, ours;
        {
            heappoison::Scope poison(ctx, round % 2 ? 0x5a : 0xcd);
            original = MakeImage(ctx, false, true);
            ours = MakeImage(ctx, true, true);
        }
        ++ctx.cases;
        if (original.words != ours.words || original.faceOffset != ours.faceOffset) {
            size_t at = 0;
            while (at < original.words.size() && at < ours.words.size() && original.words[at] == ours.words[at]) ++at;
            ctx.Fail("Create round %d differs at word %zu (offset %zx): orig %08x ours %08x", round, at, at * 4, at < original.words.size() ? original.words[at] : 0, at < ours.words.size() ? ours.words[at] : 0);
        }
    }
}

namespace {
// Whether a deleting destructor freed the object: KERNEL32!HeapFree is wrapped in the original image's import table (its CRT's
// free) and in this executable's (our CRT's), and a free of the watched block is counted.
void **ImportSlot(uint8_t *base, const char *name)
{
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (auto *d = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress); d->Name != 0; ++d) {
        if (_stricmp(reinterpret_cast<const char *>(base + d->Name), "KERNEL32.dll") != 0) continue;
        const auto *names = reinterpret_cast<const IMAGE_THUNK_DATA32 *>(base + d->OriginalFirstThunk);
        auto *slots = reinterpret_cast<IMAGE_THUNK_DATA32 *>(base + d->FirstThunk);
        for (; names->u1.AddressOfData != 0; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal)) continue;
            const auto *byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME *>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char *>(byName->Name), name) == 0) return reinterpret_cast<void **>(&slots->u1.Function);
        }
    }
    return nullptr;
}
const void *gWatched;
uint32_t gFreed;
decltype(&HeapFree) gRealFree;
BOOL WINAPI WatchingHeapFree(HANDLE heap, DWORD flags, LPVOID block)
{
    if (block && block == gWatched) ++gFreed;
    return gRealFree(heap, flags, block);
}
struct FreeWatch {
    void **slots[2]{};
    void *saved[2]{};
    explicit FreeWatch(difftest::Context &ctx)
    {
        slots[0] = ImportSlot(ctx.image.At<uint8_t>(0), "HeapFree");
        slots[1] = ImportSlot(reinterpret_cast<uint8_t *>(GetModuleHandleW(nullptr)), "HeapFree");
        if (!slots[0] || !slots[1]) ctx.Fail("free watch: no HeapFree import slot (image %d, ours %d)", slots[0] != nullptr, slots[1] != nullptr);
        gRealFree = reinterpret_cast<decltype(&HeapFree)>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "HeapFree"));
        for (int i = 0; i < 2; ++i) {
            if (!slots[i]) continue;
            saved[i] = *slots[i];
            Write(slots[i], reinterpret_cast<void *>(&WatchingHeapFree));
        }
    }
    ~FreeWatch()
    {
        for (int i = 1; i >= 0; --i)
            if (slots[i]) Write(slots[i], saved[i]);
    }
    FreeWatch(const FreeWatch &) = delete;
    FreeWatch &operator=(const FreeWatch &) = delete;
    static void Write(void **slot, void *value)
    {
        DWORD old;
        VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old);
        *slot = value;
        VirtualProtect(slot, sizeof(void *), old, &old);
    }
};
} // namespace

OCC_TEST(NHoodOcc_DeletingDestructor, 0x4a84a0)
{
    FreeWatch watch(ctx);
    std::vector<Plan> plans = GridPlans();
    plans.push_back(Plan{{2, 3, 0, 4}, 1, 4, 4, 12});
    for (uint32_t flags : {1u, 0u}) {
        gDtorFlags = flags;
        // the object is consumed: only the log and the occupants' counts are compared
        uint32_t index = 0;
        for (const Plan &plan : plans) {
            Blob blobs[3];
            for (int route = 0; route < 3; ++route) {
                gRoute = route;
                gTrapHits = 0;
                Begin({}, -1);
                ResetWorld();
                Mgr *m = MakeManager(index * 17 + 3, plan);
                gBase = m;
                gWatched = m;
                gFreed = 0;
                const uint32_t returned = uint32_t(reinterpret_cast<uintptr_t>(OccCall<void *(__thiscall *)(void *, uint32_t)>(ctx, m,
                    0x4a84a0, 0, OCC_ADAPTER(DeletingDestructor), gDtorFlags))) - uint32_t(reinterpret_cast<uintptr_t>(m));
                gWatched = nullptr;
                Blob b{returned, gTrapHits, gFreed}; // freed with bit 0, by either CRT
                b.insert(b.end(), gLog.begin(), gLog.end());
                for (const Occ &o : gOccs) b.insert(b.end(), {o.id, uint32_t(o.refs)});
                const auto *words = reinterpret_cast<const uint32_t *>(m);
                if (gDtorFlags == 0) {
                    for (uint32_t offset : {0x0u, 0x8u, 0xcu, 0x10u, 0x14u, 0x18u}) b.push_back(words[offset / 4] == 0 ? 0 : 1);
                    b.push_back(VtCode(0, words[0])); // the base vtable the destructor leaves (the game's or ours)
                    ::operator delete(m, sizeof(Mgr));
                }
                gBase = nullptr;
                blobs[route] = b;
            }
            gRoute = 0;
            Compare(ctx, gDtorFlags ? "DeletingDestructor(1) route 1" : "DeletingDestructor(0) route 1", index, blobs[0], blobs[1]);
            Compare(ctx, gDtorFlags ? "DeletingDestructor(1) route 2" : "DeletingDestructor(0) route 2", index++, blobs[0], blobs[2]);
        }
    }
}

OCC_TEST(NHoodOcc_QueryInterface, 0x4aa090)
{
    const uint32_t ids[] = {1, 0x6bd0dc57, 0x452294aa, 0x456b8f1d, 0xe4fda3d4, 0, 2, 0xffffffff, 0x6bd0dc56, 0x6bd0dc58, 0x452294ab, 0xe4fda3d3};
    struct Entry { uint32_t rva; int face; };
    for (Entry e : {Entry{0x4aa090, 0}, Entry{0x4aa0e7, 1}, Entry{0x4aa0ff, 2}, Entry{0x4aa0ef, 3}, Entry{0x4aa0f7, 4}}) for (uint32_t id : ids) {
        // routes: the original (the primary or a thunk), our vtable's slot 0 of that face (the compiler's adjustor thunk on the
        // secondary faces), the adapter with a trap in that slot 0
        void *const adapters[kFaces] = {OCC_ADAPTER(QueryInterface), OCC_ADAPTER(ManagerQueryInterface), OCC_ADAPTER(SerialQueryInterface),
            OCC_ADAPTER(PersistQueryInterface), OCC_ADAPTER(MessageQueryInterface)};
        Blob blobs[3];
        for (int route = 0; route < 3; ++route) {
            gRoute = route;
            gTrapHits = 0;
            Mgr *m = MakeManager(id, Plan{});
            void *out = reinterpret_cast<void *>(0x5a5a5a5a);
            const uint8_t r = OccCall<uint8_t(__thiscall *)(void *, uint32_t, void **)>(ctx, m, e.rva, unsigned(e.face), adapters[e.face], id, &out);
            blobs[route] = {r, out == reinterpret_cast<void *>(0x5a5a5a5a) ? 0xfffffff0u : uint32_t(static_cast<uint8_t *>(out) - reinterpret_cast<uint8_t *>(m)), m->mRefCount, gTrapHits};
            Destroy(ctx, m);
        }
        gRoute = 0;
        if (blobs[0] != blobs[1] || blobs[0] != blobs[2]) ctx.Fail("QueryInterface %x id %08x: differs", e.rva, id);
        ++ctx.cases;
    }
}

// Create* on the manager's own face (not the fake vtable): the game's occupant constructor runs on fresh memory in both; the
// returned face and the new object's bytes are compared (poisoned allocation, as above).
namespace {
uint32_t CreateOccupant(difftest::Context &ctx, int route, int which, uint32_t rva, uint32_t size, uint32_t faceOffset, std::vector<uint32_t> &image)
{
    gRoute = route;
    gTrapHits = 0;
    Mgr *m = MakeManager(9, Plan{});
    void *poison = ::operator new(size);
    std::memset(poison, gPoison, size);
    ::operator delete(poison, size);
    void *const adapters[4] = {OCC_ADAPTER(CreateFloraOccupant), OCC_ADAPTER(CreateRoadOccupant), OCC_ADAPTER(CreateModelRoadOccupant),
        OCC_ADAPTER(CreatePropOccupant)};
    void *face = OccCall<void *(__thiscall *)(void *)>(ctx, m, rva, 1, adapters[which]);
    Destroy(ctx, m);
    gRoute = 0;
    if (gTrapHits) return 0xfffffffe;
    if (!face) return 0xffffffff;
    const auto *object = reinterpret_cast<const uint32_t *>(static_cast<uint8_t *>(face) - faceOffset);
    image.assign(object, object + size / 4);
    return 0;
}
} // namespace

#define CREATE_TEST(name, which, rva, bytes, faceOff)                                                                  \
    OCC_TEST(name, rva)                                                                                                  \
    {                                                                                                                    \
        for (int step = 0; step < 8; ++step) {                                                                           \
            const int round = step / 2, route = 1 + step % 2;                                                            \
            std::vector<uint32_t> a, b;                                                                                  \
            uint32_t ra, rb;                                                                                             \
            {   /* every heap block poisoned on both sides (heap_poison.h, DT): the whole object is compared exactly */  \
                heappoison::Scope poison(ctx, round % 2 ? 0x5a : 0xcd);                                                  \
                ra = CreateOccupant(ctx, 0, which, rva, bytes, faceOff, a);                                              \
                rb = CreateOccupant(ctx, route, which, rva, bytes, faceOff, b);                                          \
            }                                                                                                            \
            ++ctx.cases;                                                                                                 \
            size_t at = 0;                                                                                               \
            while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;                                                \
            if (ra != rb || a != b) ctx.Fail(#name " round %d route %d: image differs at word %zu: orig %08x ours %08x", round, route, at, at < a.size() ? a[at] : 0, at < b.size() ? b[at] : 0); \
        }                                                                                                                \
    }
CREATE_TEST(NHoodOcc_CreateFloraOccupant, 0, 0x4a8ef0, 0x34, 0x28)
CREATE_TEST(NHoodOcc_CreateRoadOccupant, 1, 0x4a8fb0, 0x98, 0x28)
CREATE_TEST(NHoodOcc_CreateModelRoadOccupant, 2, 0x4a9010, 0xc4, 0x98)
CREATE_TEST(NHoodOcc_CreatePropOccupant, 3, 0x4a8f50, 0x34, 0x28)

// ---- lane FY: removal, queries and roads (manager face slots 7, 8, 10, 19-23) ----
namespace {
// Two originals the road slots call are stood in on both sides (ours calls them at their RVA through OriginalFunction): the
// road snap 0x4aaf80 (terrain heights, its own lane) and the road-group tracer 0x4a8930 (it searches the grid and draws random
// numbers). The stand-ins log their arguments; the tracer adds `trace` tiles to the group it is handed.
struct CodePatch {
    uint8_t *site;
    uint8_t saved[5];
    CodePatch(void *at, const void *stand) : site(static_cast<uint8_t *>(at))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t rel = int32_t(reinterpret_cast<uintptr_t>(stand) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xe9;
        std::memcpy(site + 1, &rel, 4);
    }
    CodePatch(const CodePatch &) = delete;
    ~CodePatch() { std::memcpy(site, saved, 5); }
};
uint32_t GroupIndex(const void *group)
{
    if (!gBase) return 0xbad;
    for (size_t i = 0; i < gBase->mRoadGroups.size(); ++i)
        if (group == &gBase->mRoadGroups[i] || group == &gBase->mRoadGroups[i].mTiles) return uint32_t(i);
    return 0xbad;
}
void __fastcall StandSnapRoad(Mgr *m, void *, void *road) { Ev(0x60, Norm(m), OccId(road)); }
void __fastcall StandTrace(Mgr *m, void *, Occ *road, cTSNHoodRoadTileVector *group)
{
    Ev(0x61, Norm(m), OccId(road), GroupIndex(group));
    Ev(0x62, uint32_t(group->size()));
    for (uint32_t i = 0; i < road->trace; ++i) group->push_back({int32_t(road->id), int32_t(i)});
}
struct Stands {
    CodePatch snap{GameAddress<void>(0x4aaf80), reinterpret_cast<const void *>(&StandSnapRoad)};
    CodePatch trace{GameAddress<void>(0x4a8930), reinterpret_cast<const void *>(&StandTrace)};
};

// FindOccupantsInRect as a self-call: logged, then the original's body on the same object (both sides).
difftest::Context *gCtx;
uint8_t __fastcall FaceFindForward(void *face, void *, const int32_t *rect, uint32_t mask, void *out)
{
    Ev(0x41, uint32_t(rect[0]), uint32_t(rect[1]), uint32_t(rect[2]));
    Ev(0x42, uint32_t(rect[3]), mask, Norm(out));
    return gCtx->Original<uint8_t(__thiscall *)(void *, const int32_t *, uint32_t, void *)>(0x4a9450)(face, rect, mask, out);
}

// The world of the road tests, built the same on both sides from the manager's seeded key: every list entry (a type face) gets
// a grid object answering its kind's iid with that face, put in the grid by the original InsertIntoGrid; the grid objects'
// slot 12 names a road face (or not); roads get a type (slot 7), flags (19 / 25, junk upper bits) and a trace length; two
// strangers in the grid answer no iid. Every grid object carries the road vtable.
std::vector<Occ *> gGridOf[4], gStrangers;
void Populate(difftest::Context &c, Mgr *m)
{
    std::mt19937 rng(m->mKey[0]);
    for (auto &v : gGridOf) v.clear();
    gStrangers.clear();
    const uint32_t roads = uint32_t(m->mRoads.mLast - m->mRoads.mFirst);
    for (void **at = m->mRoads.mFirst; at != m->mRoads.mLast; ++at) {
        Occ *r = static_cast<Occ *>(*at);
        r->vt = gRoadVT;
        r->layer = rng() % 6;
        r->s19 = (rng() & 1) | (rng() % 2) << 8;
        r->s25 = (rng() % 6) | (rng() % 2) << 9;
        r->trace = (rng() % 3) ? 20 + rng() % 4 : rng() % 20;
    }
    RefPtrVector *lists[4] = {&m->mFlora, &m->mRoads, &m->mModelRoads, &m->mProps};
    for (int k = 0; k < 4; ++k) {
        for (void **at = lists[k]->mFirst; at != lists[k]->mLast; ++at) {
            Occ *face = static_cast<Occ *>(*at);
            Occ *g = NewOcc(1u << k, m->mGridCellsX);
            g->vt = gRoadVT;
            g->qiAs = face;
            g->type3 = (1u << k) | (rng() % 3) << 8;
            g->flag12 = roads && rng() % 3 ? uint32_t(reinterpret_cast<uintptr_t>(m->mRoads.mFirst[rng() % roads])) : rng() % 2;
            if (k != kRoad) face->next = g;
            if (!m->mGrid.empty()) c.Original<void(__thiscall *)(void *, void *)>(0x4a9c10)(m, g);
            gGridOf[k].push_back(g);
        }
    }
    for (int i = 0; i < 2; ++i) {
        Occ *s = NewOcc(0, m->mGridCellsX);
        s->type3 = rng() % 16;
        if (!m->mGrid.empty()) c.Original<void(__thiscall *)(void *, void *)>(0x4a9c10)(m, s);
        gStrangers.push_back(s);
    }
    for (RefPtrVector &cell : m->mGrid)
        for (void **at = cell.mFirst; at != cell.mLast; ++at) static_cast<Occ *>(*at)->vt = gRoadVT;
}

std::vector<Plan> RoadPlans()
{
    std::vector<Plan> plans(6);
    plans[0].pre[0] = 3; plans[0].pre[1] = 4; plans[0].pre[2] = 2; plans[0].pre[3] = 3; plans[0].cx = 4; plans[0].cy = 3;
    plans[1].pre[0] = 1; plans[1].pre[1] = 7; plans[1].pre[3] = 5; plans[1].cx = 5; plans[1].cy = 4; plans[1].seedCells = 9;
    plans[2].cx = 1; plans[2].cy = 1; plans[2].pre[1] = 2; plans[2].pre[0] = 2;
    plans[3].pre[0] = 5; plans[3].pre[1] = 9; plans[3].pre[2] = 1; plans[3].pre[3] = 6; plans[3].cx = 3; plans[3].cy = 5;
    plans[4].pre[1] = 1; plans[4].cx = 2; plans[4].cy = 2; plans[4].seedCells = 0;
    plans[5].cx = 3; plans[5].cy = 3; // nothing in the lists
    return plans;
}

// Each test picks its argument with these.
uint32_t gPick, gArg;
int32_t gRect[4];
uint32_t gOutPlan;
Occ *Pick(Mgr *m)
{
    // 0 null, 1..: walk the grid objects of the four kinds and the strangers
    if (gPick == 0) return nullptr;
    std::vector<Occ *> all;
    for (auto &v : gGridOf) all.insert(all.end(), v.begin(), v.end());
    all.insert(all.end(), gStrangers.begin(), gStrangers.end());
    (void)m;
    return all.empty() ? nullptr : all[(gPick - 1) % all.size()];
}
void LogList(const RefPtrVector &v)
{
    Ev(0x70, uint32_t(v.mLast - v.mFirst), uint32_t(v.mEnd - v.mFirst));
    for (void **p = v.mFirst; p != v.mLast; ++p) Ev(0x71, OccId(*p));
}
// A caller's vector for FindOccupantsInRect / SnapRoadsInRect: over the allocator service; empty, or holding counted entries.
void FillOut(RefPtrVector &out, uint32_t plan)
{
    out = RefPtrVector{};
    if (plan == 0) return;
    const uint32_t capacity = plan == 1 ? 1 : 6;
    out.mFirst = static_cast<void **>(RZMemory::Malloc(capacity * 4));
    out.mLast = out.mFirst;
    out.mEnd = out.mFirst + capacity;
    if (plan >= 2) {
        for (uint32_t i = 0; i < plan - 1 && i < capacity && !gOccs.empty(); ++i) {
            Occ *o = &gOccs[(i * 5) % gOccs.size()];
            ++o->refs;
            *out.mLast++ = o;
        }
    }
}
void FreeOut(difftest::Context &c, RefPtrVector &out) { c.Original<void(__thiscall *)(void *)>(0x22589e)(&out); }
} // namespace

OCC_TEST(NHoodOcc_RemoveOccupant, 0x4aa830)
{
    for (gPick = 0; gPick < 40; ++gPick) {
        Sweep(ctx, CASE_LAMBDA(
            Populate(c, m);
            Occ *target = Pick(m);
            if (gPick % 7 == 3 && target) target->iidMask |= 0xf; // answers every iid: the first in the original's order wins
            if (gPick % 11 == 5 && target && target->qiAs) {      // its face twice in the list: only the first goes
                RefPtrVector *lists[4] = {&m->mFlora, &m->mRoads, &m->mModelRoads, &m->mProps};
                for (RefPtrVector *l : lists)
                    if (l->mLast != l->mFirst && l->mFirst[0] == target->qiAs && l->mLast != l->mEnd) { *l->mLast++ = target->qiAs; ++target->qiAs->refs; }
            }
            OccCall<void(__thiscall *)(void *, void *)>(c, m, 0x4aa830, 1, OCC_ADAPTER(RemoveOccupant), target);
            return 0;), RoadPlans(), {{}}, "RemoveOccupant", false);
    }
}

OCC_TEST(NHoodOcc_FindLinkedOccupants, 0x4aa690)
{
    // the road is a road face from the list (types 0-5), null, or a stranger whose type is forced
    for (gPick = 0; gPick < 24; ++gPick) {
        Sweep(ctx, CASE_LAMBDA(
            Populate(c, m);
            Occ *road = nullptr;
            const uint32_t roads = uint32_t(m->mRoads.mLast - m->mRoads.mFirst);
            if (gPick && roads) road = static_cast<Occ *>(m->mRoads.mFirst[gPick % roads]);
            if (road && gPick >= 12) road->layer = gPick % 6;
            OccCall<void(__thiscall *)(void *, void *)>(c, m, 0x4aa690, 1, OCC_ADAPTER(FindLinkedOccupants), road);
            return 0;), RoadPlans(), {{}}, "FindLinkedOccupants", false);
    }
}

OCC_TEST(NHoodOcc_FindOccupantsInRect, 0x4a9450)
{
    const int32_t rects[][4] = {{0, 0, 100, 100}, {-50, -50, -1, -1}, {3, 2, 9, 7}, {5, 5, 5, 5}, {4, 0, 8, 4}, {12, 9, 40, 30}, {7, 3, 6, 2}, {-8, 4, 300, 5}};
    for (const auto &r : rects) for (uint32_t mask : {0u, 1u, 2u, 4u, 8u, 0xfu, 0x10u, 0xffu}) for (gOutPlan = 0; gOutPlan < 5; ++gOutPlan) {
        std::memcpy(gRect, r, sizeof gRect);
        gArg = mask;
        Sweep(ctx, CASE_LAMBDA(
            Populate(c, m);
            RefPtrVector out;
            FillOut(out, gOutPlan);
            RefPtrVector *arg = gOutPlan == 4 ? nullptr : &out;
            uint8_t r;
            r = OccCall<uint8_t(__thiscall *)(void *, const int32_t *, uint32_t, void *)>(c, m, 0x4a9450, 1, OCC_ADAPTER(FindOccupantsInRect), static_cast<const int32_t *>(gRect), gArg, static_cast<void *>(arg));
            LogList(out);
            FreeOut(c, out);
            return r;), RoadPlans(), {{}}, "FindOccupantsInRect", false);
    }
}

OCC_TEST(NHoodOcc_SnapAllRoads, 0x4aadb0)
{
    Stands stands;
    for (gPick = 0; gPick < 3; ++gPick) {
        Sweep(ctx, CASE_LAMBDA(
            Populate(c, m);
            if (gPick == 1) for (void **at = m->mRoads.mFirst; at != m->mRoads.mLast; ++at) static_cast<Occ *>(*at)->s25 = 0x100;
            if (gPick == 2) for (void **at = m->mRoads.mFirst; at != m->mRoads.mLast; ++at) static_cast<Occ *>(*at)->s19 = 0x200;
            OccCall<void(__thiscall *)(void *)>(c, m, 0x4aadb0, 1, OCC_ADAPTER(SnapAllRoads));
            return 0;), RoadPlans(), {{}}, "SnapAllRoads", false);
    }
}

OCC_TEST(NHoodOcc_SnapRoadsInRect, 0x4ab400)
{
    Stands stands;
    gCtx = &ctx;
    gPatch[1][10] = reinterpret_cast<void *>(&FaceFindForward); // every route: the self-call reaches the original body
    const int32_t rects[][4] = {{0, 0, 100, 100}, {-50, -50, -1, -1}, {3, 2, 9, 7}, {5, 5, 5, 5}, {1, 1, 4, 4}, {0, 0, 0, 0}, {12, 9, 40, 30}, {7, 3, 6, 2}, {-8, 4, 300, 5}, {16, 12, 20, 20}};
    for (gPick = 0; gPick < 2; ++gPick) for (const auto &r : rects) {
        std::memcpy(gRect, r, sizeof gRect);
        Sweep(ctx, CASE_LAMBDA(
            Populate(c, m);
            // pick 1: two roads in three snap (slot 25 low byte 0), half of them flagged (slot 19), junk upper bits
            if (gPick == 1)
                for (void **at = m->mRoads.mFirst; at != m->mRoads.mLast; ++at) {
                    Occ *road = static_cast<Occ *>(*at);
                    road->s25 = road->id % 3 == 0 ? 0x201 : 0x100;
                    road->s19 = (road->id & 1) | 0x200;
                }
            OccCall<void(__thiscall *)(void *, const int32_t *)>(c, m, 0x4ab400, 1, OCC_ADAPTER(SnapRoadsInRect), gRect);
            return 0;), RoadPlans(), {{}}, "SnapRoadsInRect", false);
    }
    gCtx = nullptr;
}

OCC_TEST(NHoodOcc_ResetGridOccupants, 0x4a85f0)
{
    Sweep(ctx, CASE_LAMBDA(
        Populate(c, m);
        OccCall<void(__thiscall *)(void *)>(c, m, 0x4a85f0, 1, OCC_ADAPTER(ResetGridOccupants));
        return 0;), RoadPlans(), {{}}, "ResetGridOccupants", false);
}

OCC_TEST(NHoodOcc_BuildRoadGroups, 0x4a8b70)
{
    Stands stands;
    for (gPick = 0; gPick < 2; ++gPick) for (uint32_t extra : {0u, 3u}) for (uint32_t limit : {0u, 1u, 2u, 3u, 5u, 100u, 0xffffffffu}) {
        gExtraGroups = extra;
        gArg = limit;
        Sweep(ctx, CASE_LAMBDA(
            Populate(c, m);
            // pick 1: every road joins groups and traces 18, 19, 20 or 21 tiles (both sides of the 20-tile bound)
            if (gPick == 1) {
                uint32_t i = 0;
                for (void **at = m->mRoads.mFirst; at != m->mRoads.mLast; ++at, ++i) {
                    static_cast<Occ *>(*at)->trace = 18 + i % 4;
                    static_cast<Occ *>(*at)->s25 = 0x103;
                }
            }
            return uint32_t(OccCall<int32_t(__thiscall *)(void *, uint32_t)>(c, m, 0x4a8b70, 1, OCC_ADAPTER(BuildRoadGroups), gArg));), RoadPlans(), {{}}, "BuildRoadGroups", false);
    }
    gPick = 0;
    gExtraGroups = 0;
}

OCC_TEST(NHoodOcc_GetRoadGroup, 0x4a9260)
{
    gExtraGroups = 3;
    for (gPick = 0; gPick < 7; ++gPick) for (gOutPlan = 0; gOutPlan < 4; ++gOutPlan) {
        Sweep(ctx, CASE_LAMBDA(
            // the caller's vector: empty, or holding 1 / 2 / 9 tiles (capacity below and above the groups' 1-3)
            cTSNHoodRoadTileVector out;
            const uint32_t fill[4] = {0, 1, 2, 9};
            for (uint32_t i = 0; i < fill[gOutPlan]; ++i) out.push_back({int32_t(i), -int32_t(i)});
            uint8_t r;
            r = OccCall<uint8_t(__thiscall *)(void *, uint32_t, void *)>(c, m, 0x4a9260, 1, OCC_ADAPTER(GetRoadGroup), gPick, static_cast<void *>(&out));
            Ev(0x72, uint32_t(out.size()), uint32_t(out.capacity()));
            for (const cTSNHoodRoadTile &t : out) Ev(0x73, uint32_t(t.x), uint32_t(t.y));
            return r;), RoadPlans(), {{}}, "GetRoadGroup", false);
    }
    gExtraGroups = 0;
}

// ---- lane FY: the slots our class inherits or the compiler makes (no RVA hook of their own) ----
// Every slot of our vtables that is not one of the hooked functions: cRZUnknown's AddRef / Release / RemoveRef / RefCount, the
// secondary faces' AddRef / Release adjustor thunks, AsPersistResource (folded 0x4735d0) and the persist face's key accessors
// (all folded game code on +0x98) and GetRefCount. Route 0 calls the game's vtable entry on an object with the game's vtables,
// route 1 our vtable's entry on an object with ours; the return (pointers as offsets), the count, the key and the coded vtable
// words are compared. (Release runs with a count of 2 or more: at 1 it deletes through slot 3, the destructor's own test.)
namespace {
enum SlotKind { kNoArg, kOneArg, kGetKey, kSetKey };
struct SlotCase {
    unsigned face, slot;
    SlotKind kind;
    uint32_t mask;
    bool pointer;
};
constexpr SlotCase kSlotCases[] = {
    {0, 1, kNoArg, ~0u, false}, {0, 2, kNoArg, ~0u, false}, {0, 4, kNoArg, ~0u, false}, {0, 5, kNoArg, ~0u, false},
    {1, 1, kNoArg, ~0u, false}, {1, 2, kNoArg, ~0u, false}, {1, 25, kNoArg, ~0u, true},
    {2, 1, kNoArg, ~0u, false}, {2, 2, kNoArg, ~0u, false},
    {3, 1, kNoArg, ~0u, false}, {3, 2, kNoArg, ~0u, false}, {3, 3, kGetKey, ~0u, false}, {3, 4, kSetKey, 0xff, false},
    {3, 5, kNoArg, ~0u, false}, {3, 6, kNoArg, ~0u, false}, {3, 7, kNoArg, ~0u, false}, {3, 8, kOneArg, 0xff, false},
    {3, 9, kOneArg, 0xff, false}, {3, 10, kOneArg, 0xff, false}, {3, 11, kNoArg, ~0u, false},
    {4, 1, kNoArg, ~0u, false}, {4, 2, kNoArg, ~0u, false},
};

Blob RunSlot(difftest::Context &ctx, const SlotCase &c, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + c.face * 64 + c.slot);
    Mgr *m = MakeManager(seed, Plan{});
    m->mRefCount = 2 + r() % 3;
    for (auto &k : m->mKey) k = r();
    gBase = m;
    void *const fn = gCopy[c.face][c.slot];
    void *const self = reinterpret_cast<uint8_t *>(m) + kFaceOffset[c.face];
    const uint32_t arg = r();
    uint32_t key[4] = {0x5e5e5e5e, 0x5e5e5e5e, 0x5e5e5e5e, 0x5e5e5e5e};
    const uint32_t newKey[4] = {r(), r(), r(), r()};
    uint32_t result = 0;
    switch (c.kind) {
    case kNoArg: result = reinterpret_cast<uint32_t(__thiscall *)(void *)>(fn)(self); break;
    case kOneArg: result = reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t)>(fn)(self, arg); break;
    case kGetKey: result = reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t *)>(fn)(self, key); break;
    case kSetKey: result = reinterpret_cast<uint32_t(__thiscall *)(void *, const uint32_t *)>(fn)(self, newKey); break;
    }
    result &= c.mask;
    if (c.kind == kGetKey) result = result == shape::P(key) ? 0x6e7 : result; // the hidden pointer comes back
    if (c.pointer) result = Norm(reinterpret_cast<void *>(uintptr_t(result)));
    Blob b{result, m->mRefCount};
    b.insert(b.end(), std::begin(key), std::end(key));
    b.insert(b.end(), std::begin(m->mKey), std::end(m->mKey));
    for (unsigned f = 0; f < kFaces; ++f) b.push_back(VtCode(f, shape::Word(m, kFaceOffset[f])));
    gBase = nullptr;
    Destroy(ctx, m);
    return b;
}
} // namespace

OCC_TEST(NHoodOcc_Slots, 0x4735d0)
{
    for (const SlotCase &c : kSlotCases) {
        ++ctx.cases;
        if (OurVt(c.face)[c.slot] == GameVt(c.face)[c.slot]) ctx.Fail("face %u slot %u: our vtable holds the game's", c.face, c.slot);
        for (uint32_t seed = 0; seed < 24; ++seed) {
            Blob blobs[2];
            for (int route = 0; route < 2; ++route) {
                gRoute = route;
                ResetWorld();
                blobs[route] = RunSlot(ctx, c, seed);
            }
            gRoute = 0;
            ++ctx.cases;
            if (blobs[0] != blobs[1]) {
                size_t at = 0;
                while (at < blobs[0].size() && at < blobs[1].size() && blobs[0][at] == blobs[1][at]) ++at;
                ctx.Fail("face %u slot %u seed %u differs at word %zu (%x / %x)", c.face, c.slot, seed, at,
                    at < blobs[0].size() ? blobs[0][at] : 0xdeadbeef, at < blobs[1].size() ? blobs[1][at] : 0xdeadbeef);
            }
        }
    }
}
