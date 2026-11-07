// Differential tests for cTSFamilyManager (src/ts/cTSFamilyManager.h): construction and destruction, the lookups, loading,
// saving, creating and removing families, moving neighbours and families, the lot checks and the message handler.
//
// A world is one manager in a guarded buffer, populated by our own code (the containers are the library's, the game's
// layout) with recording fakes: families, the singleton (cache 0x10cc288) and what it hands out, the persist system
// (0x10faf6c), the framework (through the director at 0x10c9390), the message server (0x10f519c), the allocator service
// (0x10c95d4), messages. Every call a fake receives is appended to `events` (pointers to fakes become ids, pointers into the
// manager its offset, stack addresses a marker). Two worlds built from one seed start the same; one runs the original, the
// other ours, and the results, the manager's bytes, the containers' contents, the fakes' counts and the event logs are
// compared.
#include "game_allocator.h"
#include "difftest.h"
#include "game/game.h"
#include "gz/cGZDBSegmentPackedFile.h"
#include "rz/GeneralAllocator.h"
#include "rz/RZMemory.h"
#include "misc/UtilitiesC.h"
#include "rz/cRZString.h"
#include "shape.h"
#include "ts/cTSFamilyManager.h"

#include <Windows.h>
#include <cstring>
#include <initializer_list>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {
using Mgr = cTSFamilyManager;
constexpr unsigned kSlots = 80;
constexpr unsigned kFamilies = 10;

struct SObj;
using Custom = uint32_t (*)(SObj *self, uint32_t *args);
struct SObj {
    void **vt;
    uint32_t id;
    int32_t refs;
    uint32_t value[8];
    uint32_t members[6];
    uint32_t memberCount;
    uint32_t ret[kSlots];
    Custom custom[kSlots];
    void *table[kSlots];
};

enum Fake : unsigned {
    kFam0, kFamLast = kFam0 + kFamilies - 1, kNewFam, kApp, kLotMgr, kNeighborhood, kLots, kLotObj, kLotType, kPersist,
    kCache, kHolder, kRecord, kFramework, kLotState, kLotStateInner, kSingC, kSingCInner, kServer, kMessage, kPayload, kText,
    kNeighbor, kNbMgr, kNbRecord, kNbLot, kNbLotInner, kLotObjects, kNeighbors, kSubNeighbor, kHousehold, kService,
    kRegServer, kAllocator, kContainer, kCtx, kLotCtx, kSegment, kSegRecord, kWriter, kReader, kStore, kLotData, kHood,
    kLotSource, kLotSourceB, kMap, kCatalog, kEntry0, kEntry1, kEntry2, kEntry3, kEntry4, kEntry5, kStatX, kStatCreated, kSink, kFakeCount
};

struct Guarded {
    uint8_t before[16];
    alignas(8) uint8_t bytes[sizeof(Mgr)];
    uint8_t after[16];
};

struct World {
    Guarded work;
    SObj fakes[kFakeCount];
    std::vector<uint32_t> events;
    uint32_t keyCounter = 0;
    bool groupMatch = false;
    char text[64];
    uint32_t slot23Result = 0, slot25Result = 0;
    uint32_t ioSeed = 0;
    alignas(8) uint8_t nameStorage[sizeof(cRZString)];
    bool nameBuilt = false;
    void *entryList[6];
};
World gW;
Mgr *M() { return reinterpret_cast<Mgr *>(gW.work.bytes); }
SObj &S(Fake f) { return gW.fakes[f]; }
SObj &F(unsigned i) { return gW.fakes[kFam0 + i]; }

enum : uint32_t { evCall = 0x60, evMgr = 0x61, evAlloc = 0x62, evFree = 0x63 };
void Ev(std::initializer_list<uint32_t> items) { for (uint32_t v : items) gW.events.push_back(v); }

uint32_t Norm(uint32_t value)
{
    if (!value) return 0;
    for (unsigned i = 0; i < kFakeCount; ++i)
        if (value == uint32_t(uintptr_t(&gW.fakes[i]))) return 0x1000 + i;
    const uint32_t base = uint32_t(uintptr_t(gW.work.bytes));
    if (value >= base && value < base + sizeof(Mgr)) return 0x2000 + (value - base);
    uint32_t local = 0;
    const uint32_t sp = uint32_t(uintptr_t(&local));
    if (value - sp < 0x10000 || sp - value < 0x10000) return 0x57ac;
    return value;
}
uint32_t P(const void *p) { return uint32_t(uintptr_t(p)); }

// ---- the generic recording fakes ----
uint32_t Dispatch(SObj *self, unsigned slot, uint32_t *args, unsigned count)
{
    Ev({evCall, self->id, slot, count});

    for (unsigned i = 0; i < count; ++i) Ev({Norm(args[i])});
    if (self->custom[slot]) return self->custom[slot](self, args);
    if (slot == 1) return uint32_t(++self->refs);
    if (slot == 2) return uint32_t(--self->refs);
    return self->ret[slot];
}
template <size_t> using Word = uint32_t;
void *gTable[kSlots][6];
template <unsigned Slot, class Seq> struct Gen;
template <unsigned Slot, size_t... I> struct Gen<Slot, std::index_sequence<I...>> {
    static uint32_t __fastcall Fn(SObj *self, void *, Word<I>... a)
    {
        uint32_t args[] = {a..., 0u};
        return Dispatch(self, Slot, args, unsigned(sizeof...(I)));
    }
};
template <unsigned Slot> void FillSlot()
{
    gTable[Slot][0] = reinterpret_cast<void *>(&Gen<Slot, std::make_index_sequence<0>>::Fn);
    gTable[Slot][1] = reinterpret_cast<void *>(&Gen<Slot, std::make_index_sequence<1>>::Fn);
    gTable[Slot][2] = reinterpret_cast<void *>(&Gen<Slot, std::make_index_sequence<2>>::Fn);
    gTable[Slot][3] = reinterpret_cast<void *>(&Gen<Slot, std::make_index_sequence<3>>::Fn);
    gTable[Slot][4] = reinterpret_cast<void *>(&Gen<Slot, std::make_index_sequence<4>>::Fn);
    gTable[Slot][5] = reinterpret_cast<void *>(&Gen<Slot, std::make_index_sequence<5>>::Fn);
}
template <size_t... S> void FillAll(std::index_sequence<S...>) { (FillSlot<unsigned(S)>(), ...); }

void Set(SObj &o, unsigned slot, unsigned args, uint32_t result = 0, Custom fn = nullptr)
{
    o.table[slot] = gTable[slot][args];
    o.ret[slot] = result;
    o.custom[slot] = fn;
}
void Set(SObj &o, unsigned slot, unsigned args, const void *result) { Set(o, slot, args, P(result)); }

void ResetFake(SObj &o, uint32_t id)
{
    o.vt = o.table;
    o.id = id;
    o.refs = 1000;
    for (auto &v : o.value) v = 0;
    o.memberCount = 0;
    for (unsigned i = 0; i < kSlots; ++i) o.ret[i] = 0, o.custom[i] = nullptr, o.table[i] = nullptr;
    Set(o, 1, 0);
    Set(o, 2, 0);
}

void LogKey(const void *p)
{
    const uint32_t *key = static_cast<const uint32_t *>(p);
    Ev({0xce1, key[0], key[1], key[2], key[3]});
}
void Give(void *out, Fake f)
{
    *static_cast<void **>(out) = &S(f);
    ++S(f).refs;
}

// Family fakes: value[0] id, [1] home lot, [2] slot 44, [3] sort key, [4] base member count, [5] kind-1 count; members.
uint32_t FamId(SObj *s, uint32_t *) { return s->value[0]; }
uint32_t FamSetId(SObj *s, uint32_t *a) { s->value[0] = a[0]; return 0; }
uint32_t FamLot(SObj *s, uint32_t *) { return s->value[1]; }
uint32_t FamSetLot(SObj *s, uint32_t *a) { s->value[1] = a[0]; return 0; }
uint32_t Fam44(SObj *s, uint32_t *) { return s->value[2]; }
uint32_t FamSortKey(SObj *s, uint32_t *) { return s->value[3]; }
uint32_t FamCount(SObj *s, uint32_t *a)
{
    if (a[0] != 3) return s->value[5];
    uint32_t unknown = 0;
    for (unsigned i = 0; i < s->memberCount; ++i) unknown += s->members[i] & 1;
    return gW.groupMatch ? unknown : unknown + s->value[4];
}
uint32_t FamMemberAt(SObj *s, uint32_t *a)
{
    if (a[0] >= s->memberCount) return 0;
    if (s->members[a[0]] % 10 == 4) return 0; // some members have no slot
    return P(&s->members[a[0]]);
}
uint32_t FamRemoveMember(SObj *s, uint32_t *a)
{
    for (unsigned i = 0; i < s->memberCount; ++i)
        if (s->members[i] == a[0]) {
            for (unsigned j = i + 1; j < s->memberCount; ++j) s->members[j - 1] = s->members[j];
            --s->memberCount;
            break;
        }
    return 0;
}
uint32_t FamSetName(SObj *, uint32_t *a)
{
    const char *text = reinterpret_cast<const cRZString *>(a[0])->ToChar();
    Ev({0x7e7, uint32_t(std::strlen(text))});
    for (const char *c = text; *c; ++c) Ev({uint32_t(uint8_t(*c))});
    return 0;
}
uint32_t RecordKey(SObj *, uint32_t *a)
{
    uint32_t *key = reinterpret_cast<uint32_t *>(a[0]);
    key[0] = 0x46414d49, key[1] = 7, key[2] = ++gW.keyCounter, key[3] = 0;
    return a[0];
}
uint32_t RecordSetKey(SObj *, uint32_t *a) { LogKey(reinterpret_cast<void *>(a[0])); return 0; }

void SetupFamily(SObj &f, std::mt19937 &r)
{
    f.value[0] = r() % 6 == 0 ? (r() % 2 ? 0 : 31990 + r() % 20) : 1 + r() % 12;
    f.value[1] = r() % 3 ? 0 : r() % 4;
    f.value[2] = r() % 5;
    f.value[3] = r() % 4;
    f.value[4] = r() % 3;
    f.value[5] = r() % 3;
    f.memberCount = r() % 5;
    for (unsigned i = 0; i < f.memberCount; ++i) f.members[i] = r() % 30;
    Set(f, 41, 0, 0, FamId);
    Set(f, 6, 1, 0, FamSetId);
    Set(f, 42, 0, 0, FamLot);
    Set(f, 43, 1, 0, FamSetLot);
    Set(f, 44, 0, 0, Fam44);
    Set(f, 51, 0, 0, FamSortKey);
    Set(f, 24, 1, 0, FamCount);
    Set(f, 26, 1, 0, FamMemberAt);
    Set(f, 8, 1, 0, FamRemoveMember);
    Set(f, 38, 1, 0, FamSetName);
    Set(f, 31, 0, &S(kRecord));
    for (unsigned slot : {7u, 9u, 23u, 35u, 45u, 52u, 54u, 56u, 58u, 72u}) Set(f, slot, 1, r() % 2);
    for (unsigned slot : {36u, 61u, 75u}) Set(f, slot, 0);
    Set(f, 25, 1, r() % 11);
    Set(f, 28, 1, r() % 3 ? (r() % 2 ? 1 : 0x100) : 0);
    Set(f, 29, 1, r() % 3 == 0 ? (r() % 2 ? 1 : 0x100) : 0);
    Set(f, 32, 2);
    Set(f, 34, 1, r() % 2 ? 1 : 0x100);
    Set(f, 53, 0, (r() % 4) * 100);
    Set(f, 59, 0, r() % 2000);
}

// Other custom handlers.
uint32_t LotKnows(SObj *, uint32_t *a) { return (a[0] & 1) ? 0 : a[0] % 4 == 0 ? 2 : 1 + (a[0] & 0x100); }
uint32_t PersistKeys(SObj *s, uint32_t *a)
{
    // Fills the caller's vector (allocator-service storage) with family keys; returns the count.
    const uint32_t count = s->value[0];
    auto *vec = reinterpret_cast<uint32_t *>(a[0]);
    if (count) {
        auto *block = static_cast<uint32_t *>(RZMemory::Malloc(count * 16));
        for (uint32_t i = 0; i < count; ++i) {
            block[i * 4] = 0x46414d49, block[i * 4 + 1] = a[1];
            block[i * 4 + 2] = i % 4 == 3 ? 0xffff0000u | (i * 7) : 0x10000u * i + i % kFamilies;
            block[i * 4 + 3] = 0;
        }
        vec[0] = P(block), vec[1] = P(block + count * 4), vec[2] = P(block + count * 4);
    }
    return count;
}
uint32_t PersistGet(SObj *s, uint32_t *a)
{
    LogKey(reinterpret_cast<void *>(a[0]));
    const uint32_t *key = reinterpret_cast<const uint32_t *>(a[0]);
    if (key[0] == 0x46414d49) {
        Give(reinterpret_cast<void *>(a[2]), Fake(kFam0 + (key[2] & 0xffff) % kFamilies));
        return 1;
    }
    if (s->value[1] || s->value[2]) Give(reinterpret_cast<void *>(a[2]), kLotObjects); // success gives the object
    return s->value[2];
}
uint32_t CacheFind(SObj *s, uint32_t *a)
{
    LogKey(reinterpret_cast<void *>(a[0]));
    // A found record always comes with a holder (the original uses it unchecked); a miss sometimes gives one too.
    const bool found = s->value[1] & 1;
    if (found || (s->value[0] & 1)) Give(reinterpret_cast<void *>(a[1]), kHolder);
    s->value[0] >>= 1;
    s->value[1] >>= 1;
    return found ? 1u : 0x200u;
}
uint32_t LogKeyArg(SObj *, uint32_t *a) { LogKey(reinterpret_cast<void *>(a[0])); return 0; }
uint32_t FrameworkMake(SObj *, uint32_t *a) { Give(reinterpret_cast<void *>(a[2]), kNewFam); return 1; }
uint32_t SingCValue(SObj *s, uint32_t *a) { *reinterpret_cast<uint32_t *>(a[1]) = s->value[0]; return 1; }
uint32_t ServerTake(SObj *, uint32_t *a)
{
    const auto *m = reinterpret_cast<const uint8_t *>(a[0]);
    Ev({0x5e5, m[8], *reinterpret_cast<const uint32_t *>(m + 0xc), *reinterpret_cast<const uint32_t *>(m + 0x10),
        *reinterpret_cast<const uint32_t *>(m + 0x14), *reinterpret_cast<const uint32_t *>(m + 4)});
    return 1;
}
uint32_t MessageQuery(SObj *s, uint32_t *a)
{
    if (s->value[1]) Give(reinterpret_cast<void *>(a[1]), kPayload);
    return s->value[0];
}
uint32_t PayloadText(SObj *s, uint32_t *a)
{
    if (s->value[1]) Give(reinterpret_cast<void *>(a[1]), kText);
    return s->value[0];
}
uint32_t TextChars(SObj *, uint32_t *) { return P(gW.text); }
uint32_t LotsPrice(SObj *s, uint32_t *a)
{
    *reinterpret_cast<int32_t *>(a[1]) = int32_t(s->value[0]);
    *reinterpret_cast<uint8_t *>(a[2]) = uint8_t(s->value[1]);
    *reinterpret_cast<uint8_t *>(a[3]) = uint8_t(s->value[2]);
    return s->value[3];
}
uint32_t NeighborsFamily(SObj *s, uint32_t *a)
{
    for (unsigned i = 0; i < kFamilies; ++i)
        if (F(i).value[0] == a[0]) return P(&F(i));
    return s->value[0];
}
uint32_t ServiceQuery(SObj *s, uint32_t *a)
{
    *reinterpret_cast<void **>(a[2]) = s->value[0] ? &S(kRegServer) : nullptr;
    return 1;
}
// The allocator service forwards to the game's allocator (as the real one does); blocks are not logged, since the
// original's block-vector free goes through the service and ours (ServiceBlockVector::FreeBlock) to the allocator itself.
void *__fastcall AllocatorAlloc(SObj *, void *, uint32_t bytes) { return RZMemory::Malloc(bytes); }
void __fastcall AllocatorFree(SObj *, void *, void *block) { if (block) RZMemory::Free(block); }

// The manager's own slots that members call through `this`: fakes patched over each side's copy of its own primary vtable
// (InstallVtables); the slot under test is called through the side's pristine vtable, the original RVA or the adapter.
void *gMgrPatch[27];
uint32_t __fastcall MgrAddRef(Mgr *m, void *) { Ev({evMgr, 1}); return ++m->mRefCount; }
void *__fastcall MgrDelete(Mgr *m, void *, uint32_t flags) { Ev({evMgr, 26, flags}); return m; }
void *__fastcall MgrFindByLot(Mgr *m, void *, int32_t lot)
{
    Ev({evMgr, 10, uint32_t(lot)});
    for (void **it = m->mFamilies.mFirst; it != m->mFamilies.mLast; ++it)
        if (*it && int32_t(static_cast<SObj *>(*it)->value[1]) == lot) return *it;
    return nullptr;
}
void *__fastcall MgrGetFamily(Mgr *m, void *, int32_t id)
{
    Ev({evMgr, 11, uint32_t(id)});
    for (void **it = m->mFamilies.mFirst; it != m->mFamilies.mLast; ++it)
        if (*it && int32_t(static_cast<SObj *>(*it)->value[0]) == id) return *it;
    return nullptr;
}
int32_t __fastcall MgrRemove(Mgr *m, void *, void *family)
{
    Ev({evMgr, 14, Norm(P(family))});
    for (void **it = m->mFamilies.mFirst; it != m->mFamilies.mLast; ++it)
        if (*it == family) {
            for (void **at = it; at + 1 != m->mFamilies.mLast; ++at) *at = at[1];
            --m->mFamilies.mLast;
            --static_cast<SObj *>(family)->refs;
            return 0;
        }
    return -1;
}
void __fastcall MgrSlot15(Mgr *, void *) { Ev({evMgr, 15}); }
int32_t __fastcall MgrSlot23(Mgr *, void *, int32_t id, int32_t lot)
{
    Ev({evMgr, 23, uint32_t(id), uint32_t(lot)});
    const uint32_t r = gW.slot23Result & 3;
    gW.slot23Result >>= 2;
    return int32_t(r);
}
int32_t __fastcall MgrSlot25(Mgr *, void *, int32_t id, int32_t lot)
{
    Ev({evMgr, 25, uint32_t(id), uint32_t(lot)});
    return int32_t(gW.slot25Result);
}

// ---- the routes (docs/standalone-shape.md) ----
// Route 0: the original side (an object with the game's vtables, the original RVA called). Route 1: our object from our
// constructor, the slot called through our pristine vtable. Route 2: an object with the game's vtables (what the adapters
// serve when the Create hook is off or the game calls an old entry), the adapter called with an arity-matched trap in the
// slot it forwards to, so an adapter that dispatched through the vtable is caught. Every object carries copies of its side's
// own vtables (the primary one patched with the fakes above); only the vtable words are normalised.
int gRoute = 0;
unsigned gTrapHits = 0;
// Route 2 of a slot whose adapter was dropped (lane GJ: nothing reaches the original while the Create hook is in; its status
// entry is "in_effect": "callers"): the original runs on the game-shaped object, as on route 0 (NO_ADAPTER).
bool gNoAdapter = false;
#define NO_ADAPTER nullptr
constexpr unsigned kFaceSlots[3] = {27, 6, 4};
constexpr uint32_t kGameVt[3] = {Mgr::kVTable, Mgr::kContainerVTable, Mgr::kMessageVTable};
// Stack arguments per slot (the trap must return with the slot's `ret N`).
constexpr uint8_t kArity[3][27] = {{2, 0, 0, 0, 0, 1, 0, 0, 1, 0, 1, 1, 1, 2, 1, 0, 0, 3, 2, 1, 1, 2, 2, 2, 2, 2, 1},
                                   {2, 0, 0, 1, 1, 1},
                                   {2, 0, 0, 1}};
void *const *gOurs[3];
void *gCopy[3][27];

constexpr uint32_t kFaceOffset[3] = {0, 4, 8};

void *const *GameVt(unsigned face) { return shape::GameVTable(kGameVt[face]); }
void *Face(unsigned face) { return gW.work.bytes + kFaceOffset[face]; }

// Our three vtables, read from an object our constructor built (then destroyed).
void LearnOurs()
{
    if (gOurs[0]) return;
    for (unsigned f = 0; f < 3; ++f) gOurs[f] = shape::Ours<Mgr>(kFaceOffset).face[f];
}

void InstallVtables()
{
    gNoAdapter = false;
    auto **words = reinterpret_cast<void **>(gW.work.bytes);
    for (unsigned f = 0; f < 3; ++f) {
        void *const *own = gRoute == 1 ? gOurs[f] : GameVt(f);
        for (unsigned s = 0; s < kFaceSlots[f]; ++s) gCopy[f][s] = own[s];
        words[f] = gCopy[f];
    }
    for (unsigned s = 0; s < 27; ++s)
        if (gMgrPatch[s]) gCopy[0][s] = gMgrPatch[s];
}

// A vtable word as a code: the installed copy of face f, the class vtable of face f that the route's code stores (the game's
// on the original side, ours on both other routes: a destructor's stores; the adapter destroys with our destructor), or the
// raw value.
uint32_t VtCode(unsigned face, const void *word)
{
    if (word == gCopy[face]) return 0x7c00 + face;
    if (word == (gRoute == 0 || gNoAdapter ? GameVt(face) : gOurs[face])) return 0x7d00 + face;
    return Norm(P(word));
}
uint32_t VtCode(unsigned face) { return VtCode(face, reinterpret_cast<void *const *>(gW.work.bytes)[face]); }

uint32_t TrapHit(void *, unsigned)
{
    ++gTrapHits;
    Ev({0x7eaf});
    return 0;
}
void *TrapFor(unsigned arity) { return shape::TrapFor<TrapHit>(arity); }

// The slot of face `face` that holds the function at `rva` in the game's vtable.
unsigned SlotOf(unsigned face, uint32_t rva) { return shape::SlotOf(kGameVt[face], kFaceSlots[face], rva); }

// Runs the function at `rva` (a slot of face `face`) the route's way on the world's manager: the original RVA, our vtable's
// slot, or the adapter with a trap in the slot.
template <class Fn, class... A>
auto Call(difftest::Context &ctx, bool original, uint32_t rva, unsigned face, void *adapter, A... args)
{
    void *self = Face(face);
    if (!original && gRoute == 2 && !adapter) gNoAdapter = true;
    if (original || gNoAdapter) return ctx.Original<Fn>(rva)(self, args...);
    unsigned slot = SlotOf(face, rva);
    if (slot == ~0u) ctx.Fail("rva %x is not in face %u's vtable", rva, face), slot = 0;
    if (gRoute == 1) return reinterpret_cast<Fn>(gOurs[face][slot])(self, args...);
    gCopy[face][slot] = TrapFor(kArity[face][slot]);
    return reinterpret_cast<Fn>(adapter)(self, args...);
}
#define ADAPTER(name) reinterpret_cast<void *>(&cTSFamilyManagerAdapters::name)

// Points the game's globals at the fakes for the scope.
void *gDirectorVT[16];
void *__fastcall DirectorFramework(void *, void *) { return &S(kFramework); }
struct Globals {
    difftest::Context &ctx;
    static constexpr uint32_t kRvas[] = {0x10cc288, 0x10c9378, 0x10c95d4, 0x10c95d8, 0x10faf6c, 0x10cc290,
                                         0x10cc2ac, 0x10f519c, 0x10c9390, 0x10c93f4, 0x10cc2a8, 0x10faf78};
    static constexpr unsigned kCount = sizeof(kRvas) / sizeof(kRvas[0]);
    uint32_t saved[kCount];
    bool builtAllocator = false;
    explicit Globals(difftest::Context &c) : ctx(c)
    {
        for (unsigned i = 0; i < kCount; ++i) saved[i] = *ctx.image.At<uint32_t>(kRvas[i]);
        auto *ga = GameAddress<GeneralAllocator>(0x10cb098);
        const auto *bytes = reinterpret_cast<const uint8_t *>(ga);
        bool zero = true;
        for (size_t i = 0; i < sizeof(GeneralAllocator); ++i) zero = zero && bytes[i] == 0;
        if (zero) ga->Construct(nullptr, 0, 0, 0, nullptr, nullptr), builtAllocator = true;
        gMgrPatch[1] = reinterpret_cast<void *>(&MgrAddRef);
        gMgrPatch[10] = reinterpret_cast<void *>(&MgrFindByLot);
        gMgrPatch[11] = reinterpret_cast<void *>(&MgrGetFamily);
        gMgrPatch[14] = reinterpret_cast<void *>(&MgrRemove);
        gMgrPatch[15] = reinterpret_cast<void *>(&MgrSlot15);
        gMgrPatch[23] = reinterpret_cast<void *>(&MgrSlot23);
        gMgrPatch[25] = reinterpret_cast<void *>(&MgrSlot25);
        gMgrPatch[26] = reinterpret_cast<void *>(&MgrDelete);
        LearnOurs();
        gDirectorVT[12] = reinterpret_cast<void *>(&DirectorFramework);
        Point(true);
    }
    void Point(bool singletonC)
    {
        *ctx.image.At<void *>(0x10cc288) = &S(kApp);
        *ctx.image.At<void *>(0x10c9378) = &S(kService);
        *ctx.image.At<void *>(0x10c95d4) = &S(kAllocator);
        *ctx.image.At<int32_t>(0x10c95d8) = INT32_MIN;
        *ctx.image.At<void *>(0x10faf6c) = &S(kPersist);
        *ctx.image.At<void *>(0x10faf78) = &S(kStore);
        *ctx.image.At<void *>(0x10cc290) = singletonC ? &S(kSingC) : nullptr;
        *ctx.image.At<void *>(0x10cc2ac) = &S(kHousehold);
        *ctx.image.At<void *>(0x10f519c) = &S(kServer);
        *ctx.image.At<void *>(0x10c9390) = gDirectorVT;
        *ctx.image.At<int32_t>(0x10c93f4) = INT32_MIN;
    }
    ~Globals()
    {
        for (unsigned i = 0; i < kCount; ++i) *ctx.image.At<uint32_t>(kRvas[i]) = saved[i];
        if (builtAllocator) {
            auto *ga = GameAddress<GeneralAllocator>(0x10cb098);
            difftest_alloc::ShutdownAndRelease(ga);
        }
    }
};

// ---- fakes for the container face (Export and Import) ----
// The container (kContainer) hands out the family context, the neighbourhood context and the segment; the segment hands out
// records; a record the stream writer or reader. Entries (kEntry0..5) are what a lot source lists and the lot data takes.
cRZString *NameString()
{
    if (!gW.nameBuilt) reinterpret_cast<cRZString *>(gW.nameStorage)->Construct("household"), gW.nameBuilt = true;
    return reinterpret_cast<cRZString *>(gW.nameStorage);
}
uint32_t FamGetName(SObj *, uint32_t *) { return P(NameString()); }
uint32_t FamCollect(SObj *s, uint32_t *a)
{
    auto *vec = reinterpret_cast<void **>(a[0]);
    const unsigned count = s->value[7] % 4;
    if (count) {
        auto *block = static_cast<void **>(RZMemory::Malloc(count * 4));
        for (unsigned i = 0; i < count; ++i) {
            SObj *o = &S(Fake(kEntry0 + (s->value[7] + i) % 6));
            ++o->refs;
            block[i] = o;
        }
        vec[0] = block, vec[1] = block + count, vec[2] = block + count;
    }
    return 0;
}
uint32_t ContainerQuery(SObj *s, uint32_t *a)
{
    if (a[0] == 0x6db6f3ff && (s->value[0] & 1)) return Give(reinterpret_cast<void *>(a[1]), kCtx), 1;
    if (a[0] == 0x4e9c59c7 && (s->value[0] & 2)) return Give(reinterpret_cast<void *>(a[1]), kLotCtx), 1;
    return a[0] % 2 ? 0 : 0x100;
}
uint32_t ContainerSegment(SObj *s, uint32_t *a)
{
    // The answer can be "no" and still hand a segment out (the original releases it all the same).
    if (s->value[0] & 12) Give(reinterpret_cast<void *>(a[1]), kSegment);
    return (s->value[0] & 4) ? 1 : 0;
}
uint32_t ContainerGroup(SObj *s, uint32_t *) { return s->value[1]; }
uint32_t CtxCopyIds(SObj *s, uint32_t *a)
{
    const uint32_t count = s->value[0];
    auto *vec = reinterpret_cast<uint32_t *>(a[0]);
    if (count) {
        auto *block = static_cast<uint32_t *>(RZMemory::Malloc(count * 4));
        for (uint32_t i = 0; i < count; ++i) block[i] = (s->value[1] + i * 5) % 13;
        vec[0] = P(block), vec[1] = P(block + count), vec[2] = P(block + count);
    }
    return 0;
}
uint32_t CtxMapped(SObj *s, uint32_t *a)
{
    if (a[0] % 5 == 4 && (s->value[2] & 1)) return 0;
    *reinterpret_cast<uint32_t *>(a[1]) = (a[0] + s->value[2]) % 14;
    return 1;
}
uint32_t CtxObjectMap(SObj *s, uint32_t *a) { return s->value[3] ? (Give(reinterpret_cast<void *>(a[1]), kMap), 1u) : 0u; }
uint32_t LotCtxMapped(SObj *, uint32_t *a)
{
    if (a[0] % 3 == 0) return 0x100;
    *reinterpret_cast<uint32_t *>(a[1]) = a[0] + 100;
    return 1;
}
uint32_t SegmentOpen(SObj *s, uint32_t *a)
{
    LogKey(reinterpret_cast<void *>(a[0]));
    if (!s->value[0]) return 0;
    Give(reinterpret_cast<void *>(a[1]), kSegRecord);
    return 1;
}
uint32_t SegRecordQuery(SObj *s, uint32_t *a)
{
    if (a[0] == 0xc51b11cb && (s->value[0] & 1)) return Give(reinterpret_cast<void *>(a[1]), kWriter), 1;
    if (a[0] == 0x51b11bd && (s->value[0] & 2)) return Give(reinterpret_cast<void *>(a[1]), kReader), 1;
    return 0;
}
uint32_t ReaderGet(SObj *s, uint32_t *a) { *reinterpret_cast<uint32_t *>(a[0]) = s->value[0]; return 1; }
// The metadata object is new heap memory: log its version only.
void *__fastcall MetaSlot(SObj *self, void *, void *metadata)
{
    Ev({evCall, self->id, 19, 1, metadata ? static_cast<const UtilObjectB *>(metadata)->mArg : 0xdead});
    return nullptr;
}
uint32_t LotsLotData(SObj *s, uint32_t *) { return s->value[4] % 5 ? P(&S(kLotData)) : 0; }
uint32_t HoodFind(SObj *s, uint32_t *) { return s->value[0] % 3 ? P(&S(kLotSource)) : 0; }
uint32_t SourceFirst(SObj *, uint32_t *a) { *reinterpret_cast<void ***>(a[0]) = &gW.entryList[0]; return 0; }
uint32_t SourceLast(SObj *s, uint32_t *a) { *reinterpret_cast<void ***>(a[0]) = &gW.entryList[s->value[0]]; return 0; }
uint32_t SourceExtraFirst(SObj *, uint32_t *a) { *reinterpret_cast<void ***>(a[0]) = &gW.entryList[4]; return 0; }
uint32_t SourceExtraLast(SObj *s, uint32_t *a) { *reinterpret_cast<void ***>(a[0]) = &gW.entryList[4 + s->value[1]]; return 0; }
uint32_t EntryTag(SObj *s, uint32_t *) { return s->value[0]; }
uint32_t EntrySetTag(SObj *s, uint32_t *a) { s->value[0] = a[0]; return 0; }
uint32_t EntryCount(SObj *s, uint32_t *) { return s->value[1]; }
uint32_t EntryValue(SObj *s, uint32_t *a) { return s->value[2] ^ (a[0] * 0x10001); }
uint32_t MapTag(SObj *, uint32_t *a)
{
    if (a[0] % 3 == 1) return 0;
    *reinterpret_cast<uint32_t *>(a[1]) = a[0] ^ 0x55;
    return 1;
}
uint32_t MapValue(SObj *, uint32_t *a) { *reinterpret_cast<uint32_t *>(a[1]) = a[0] * 3; return 0; }
uint32_t CatalogLot(SObj *s, uint32_t *a) { return a[0] ? (a[0] + s->value[0]) % 3 : s->value[0] & 1; }
uint32_t PersistGetIo(SObj *s, uint32_t *a)
{
    LogKey(reinterpret_cast<void *>(a[0]));
    const uint32_t *key = reinterpret_cast<const uint32_t *>(a[0]);
    if (key[0] == 0x4e474248) {
        if (s->value[3]) Give(reinterpret_cast<void *>(a[2]), kHood);
        return 1;
    }
    Give(reinterpret_cast<void *>(a[2]), Fake(kFam0 + (key[2] & 0xffff) % kFamilies));
    return 1;
}
uint32_t FrameworkLot(SObj *, uint32_t *a) { Give(reinterpret_cast<void *>(a[2]), kLotSourceB); return 1; }

void BuildIo(uint32_t seed)
{
    World &w = gW;
    std::mt19937 r(seed * 977u + 13);
    if (seed % 3) for (unsigned i = 0; i < kFamilies; ++i) F(i).value[7] = r();
    for (unsigned i = 0; i < kFamilies; ++i) {
        SObj &f = F(i);
        Set(f, 33, 0);
        Set(f, 37, 0, 0, FamGetName);
        Set(f, 39, 0, 0, FamGetName);
        Set(f, 40, 1);
        Set(f, 49, 1, r() % 5);
        Set(f, 50, 1);
        for (unsigned slot : {66u, 68u, 70u}) Set(f, slot, 1);
        for (unsigned slot : {67u, 69u, 71u}) Set(f, slot, 0, r() % 3 ? r() % 2 : 0x301);
        Set(f, 11, 1);
        Set(f, 14, 1, 0, FamCollect);
        f.value[7] = r();
    }
    Set(S(kPersist), 16, 2);
    Set(S(kStore), 4, 2);
    Set(S(kStore), 5, 1);
    Set(S(kApp), 24, 0, &S(kCatalog));
    Set(S(kLots), 38, 1, 0, LotsLotData);
    S(kLots).value[4] = r();
    Set(S(kContainer), 0, 2, 0, ContainerQuery);
    Set(S(kContainer), 12, 2, 0, ContainerSegment);
    Set(S(kContainer), 14, 0, 0, ContainerGroup);
    S(kContainer).value[0] = (r() % 8 ? 1 : 0) | (r() % 2 ? 2 : 0) | (r() % 8 ? 4 : 0) | (r() % 4 == 0 ? 8 : 0);
    S(kContainer).value[1] = r() % 3 ? 7 : r() % 9;
    Set(S(kCtx), 23, 1, 0, CtxCopyIds);
    Set(S(kCtx), 25, 2, 0, CtxMapped);
    Set(S(kCtx), 27, 2, 0, CtxObjectMap);
    S(kCtx).value[0] = r() % 7, S(kCtx).value[1] = r() % 13, S(kCtx).value[2] = r() % 14, S(kCtx).value[3] = r() % 4 != 0;
    Set(S(kLotCtx), 25, 2, 0, LotCtxMapped);
    Set(S(kLotCtx), 29, 2, 0, LotCtxMapped);
    Set(S(kSegment), 22, 3, 0, SegmentOpen);
    Set(S(kSegment), 24, 1);
    S(kSegment).value[0] = r() % 5 != 0;
    Set(S(kSegRecord), 0, 2, 0, SegRecordQuery);
    S(kSegRecord).value[0] = r() % 4;
    Set(S(kWriter), 9, 1);
    Set(S(kReader), 9, 1, 0, ReaderGet);
    S(kReader).value[0] = r() % 3 ? 0xcb : r();
    for (SObj *o : {&S(kWriter), &S(kReader)}) Set(*o, 19, 1), o->table[19] = reinterpret_cast<void *>(&MetaSlot);
    Set(S(kLotData), 3, 1, 1);
    Set(S(kLotData), 9, 1);
    Set(S(kLotData), 10, 0);
    Set(S(kLotData), 15, 1);
    Set(S(kLotData), 32, 1);
    Set(S(kHood), 38, 1, 0, HoodFind);
    S(kHood).value[0] = r();
    for (SObj *source : {&S(kLotSource), &S(kLotSourceB)}) {
        Set(*source, 28, 1, 0, SourceFirst);
        Set(*source, 29, 1, 0, SourceLast);
        Set(*source, 42, 1, 0, SourceExtraFirst);
        Set(*source, 43, 1, 0, SourceExtraLast);
        Set(*source, 4, 1, r() % 4 ? (r() % 2 ? 1 : 3) : (r() % 2 ? 2 : 0x100));
        source->value[0] = r() % 5, source->value[1] = r() % 3;
    }
    const uint32_t tags[] = {0, 0, 0x4dd398a5, 0xedd39930, 7, 0x11, 0x20, 0x4dd398a5};
    const uint32_t counts[] = {0, 1, 2, 3, 0x10002, 0x20003, 4};
    for (unsigned i = 0; i < 6; ++i) {
        SObj &e = S(Fake(kEntry0 + i));
        gW.entryList[i] = &e;
        Set(e, 6, 0, 0, EntryTag);
        Set(e, 7, 1, 0, EntrySetTag);
        Set(e, 18, 0, 0, EntryCount);
        Set(e, 21, 1, 0, EntryValue);
        Set(e, 22, 2);
        e.value[0] = tags[r() % 8], e.value[1] = counts[r() % 7], e.value[2] = r();
    }
    // The original asks the object map for the ids of a special entry without checking that it exists (ORIGINAL-BUG in
    // ImportFamily): without a map, the special entries have no ids.
    if (!S(kCtx).value[3])
        for (unsigned i = 0; i < 6; ++i)
            if (S(Fake(kEntry0 + i)).value[0] == 0x4dd398a5 || S(Fake(kEntry0 + i)).value[0] == 0xedd39930) S(Fake(kEntry0 + i)).value[1] = 0;
    Set(S(kMap), 36, 2, 0, MapTag);
    Set(S(kMap), 32, 2, 0, MapValue);
    Set(S(kCatalog), 14, 1, 0, CatalogLot);
    S(kCatalog).value[0] = r();
    S(kPersist).value[3] = r() % 4 != 0;
    w.events.clear();
}

enum Fill : uint32_t { fFamilies = 1, fRemoved = 2, fNullFamily = 4, fLog = 8 };

void FillVector(RefPtrVector &v, unsigned count, unsigned spare, std::mt19937 &r, bool allowNull)
{
    v = RefPtrVector{};
    if (!count && !spare) return;
    auto **block = static_cast<void **>(::operator new((count + spare) * sizeof(void *)));
    for (unsigned i = 0; i < count; ++i) {
        SObj *f = &F(r() % kFamilies);
        if (allowNull && r() % 6 == 0) f = nullptr;
        if (f) ++f->refs;
        block[i] = f;
    }
    v.mFirst = block, v.mLast = block + count, v.mEnd = block + count + spare;
}

void Build(uint32_t seed, uint32_t fill)
{
    static bool filled = false;
    if (!filled) FillAll(std::make_index_sequence<kSlots>()), filled = true;
    World &w = gW;
    std::mt19937 r(seed * 2654435761u + 7);
    w.events.clear();
    w.keyCounter = 0;
    w.groupMatch = false;
    w.slot23Result = r();
    w.slot25Result = r() % 4 == 0 ? 13 : r() % 3 == 0 ? 9 : r() % 15;
    for (auto &b : w.work.before) b = uint8_t(r());
    for (auto &b : w.work.after) b = uint8_t(r());
    for (auto &b : w.work.bytes) b = uint8_t(r());
    for (unsigned i = 0; i < kFakeCount; ++i) ResetFake(w.fakes[i], 0x100 + i);
    for (unsigned i = 0; i < kFamilies; ++i) SetupFamily(F(i), r);
    SetupFamily(S(kNewFam), r);
    // Distinct ids for most seeds (ties in the sort are then only between equal pointers).
    if (seed % 4) for (unsigned i = 0; i < kFamilies; ++i) F(i).value[0] = i == 0 && seed % 3 == 0 ? 0 : 1 + i * 3 + seed % 3;
    // The id bounds the saves check (0 < id < 32000), exactly at the edge.
    if (seed % 8 == 4) F(1).value[0] = 32000, F(2).value[0] = 31999;

    Set(S(kRecord), 3, 1, 0, RecordKey);
    Set(S(kRecord), 4, 1, 0, RecordSetKey);
    Set(S(kApp), 10, 0, r() % 4 ? &S(kLotMgr) : nullptr);
    Set(S(kApp), 6, 0, r() % 6 ? &S(kLots) : nullptr);
    Set(S(kApp), 9, 0, &S(kNeighbors));
    Set(S(kApp), 23, 0, &S(kLotState));
    Set(S(kApp), 26, 0, &S(kNbMgr));
    Set(S(kLotMgr), 33, 0, &S(kNeighborhood));
    Set(S(kLotMgr), 17, 1, 0, LotKnows);
    Set(S(kLotMgr), 16, 2, r() % 4 ? &S(kNeighbor) : nullptr);
    Set(S(kNeighborhood), 6, 0, 7);
    Set(S(kLots), 26, 0);
    Set(S(kLots), 57, 1, r() % 6 ? &S(kLotObj) : nullptr);
    S(kLots).value[0] = (r() % 4) * 100, S(kLots).value[1] = r(), S(kLots).value[2] = r() % 3 ? 1 : 0;
    S(kLots).value[3] = r() % 4 ? 1 : (r() % 2 ? 0 : 0x300);
    Set(S(kLots), 23, 5, 0, LotsPrice);
    Set(S(kLotObj), 4, 0, &S(kLotType));
    Set(S(kLotObj), 32, 0, r() % 3 ? 8 : r() % 4);
    Set(S(kLotObj), 42, 0, r() % 4);
    Set(S(kLotObj), 43, 1, 50 + r() % 5);
    Set(S(kLotObj), 47, 0, r() % 3 ? r() % 5 : 0xffffff80u | r() % 4);
    Set(S(kLotType), 8, 0, r() % 12);
    Set(S(kLotType), 24, 1, r() % 2 ? 1 : 0x100);
    Set(S(kNeighbors), 10, 1, &S(kSubNeighbor));
    S(kNeighbors).value[0] = r() % 2 ? P(&F(0)) : 0;
    Set(S(kNeighbors), 11, 1, 0, NeighborsFamily);
    Set(S(kSubNeighbor), 24, 1, r() % 12);
    Set(S(kLotState), 6, 0, r() % 3 ? &S(kLotStateInner) : nullptr);
    Set(S(kLotStateInner), 18, 0, r() % 4);
    Set(S(kSingC), 64, 0, r() % 3 ? &S(kSingCInner) : nullptr);
    S(kSingCInner).value[0] = r();
    Set(S(kSingCInner), 6, 2, 0, SingCValue);
    Set(S(kFramework), 3, 3, 0, FrameworkMake);
    S(kPersist).value[0] = r() % 7;
    S(kPersist).value[1] = r() % 4 != 0;
    S(kPersist).value[2] = r() % 3 ? 1 : (r() % 2 ? 0 : 0x100);
    Set(S(kPersist), 3, 0, &S(kCache));
    Set(S(kPersist), 36, 3, 0, PersistKeys);
    Set(S(kPersist), 5, 5, 0, PersistGet);
    Set(S(kCache), 10, 1, 0, LogKeyArg);
    S(kCache).value[0] = r(), S(kCache).value[1] = r();
    Set(S(kCache), 34, 2, 0, CacheFind);
    Set(S(kHolder), 26, 1, 0, LogKeyArg);
    for (unsigned slot : {22u, 23u, 24u}) Set(S(kLotObjects), slot, 0);
    Set(S(kServer), 3, 1, 0, ServerTake);
    Set(S(kServer), 4, 2, 0, ServerTake);
    S(kMessage).value[0] = r() % 5 ? 1 : (r() % 2 ? 0 : 0x100);
    S(kMessage).value[1] = S(kMessage).value[0] ? 1 : r() % 2;
    Set(S(kMessage), 0, 2, 0, MessageQuery);
    Set(S(kPayload), 8, 0, r() % 3 ? 1 : (r() % 2 ? 0 : 0x100));
    Set(S(kPayload), 9, 0, r() % 5);
    S(kPayload).value[0] = r() % 4 ? 1 : (r() % 2 ? 0 : 0x100);
    S(kPayload).value[1] = S(kPayload).value[0] ? 1 : r() % 2;
    Set(S(kPayload), 29, 2, 0, PayloadText);
    Set(S(kText), 5, 0, 0, TextChars);
    {
        const unsigned length = r() % 3 ? r() % 12 : 16 + r() % 40;
        for (unsigned i = 0; i < length; ++i) w.text[i] = char('a' + r() % 26);
        w.text[length] = 0;
    }
    Set(S(kNeighbor), 15, 0, 20 + r() % 6);
    Set(S(kNeighbor), 16, 1, r() % 4 ? r() % 14 : (r() % 2 ? 0 : 0x1fffbu));
    Set(S(kNeighbor), 17, 2);
    Set(S(kNeighbor), 10, 0, &S(kNbLot));
    Set(S(kNeighbor), 33, 0, r() % 40);
    Set(S(kNbLot), 67, 0, &S(kNbLotInner));
    Set(S(kNbLotInner), 11, 0, r() % 9);
    Set(S(kNbMgr), 43, 1, r() % 3 ? &S(kNbRecord) : nullptr);
    Set(S(kNbRecord), 11, 3);
    Set(S(kHousehold), 15, 1, r() % 3 ? r() % 14 : 0xabcd0000u | (r() % 14));
    Set(S(kHousehold), 16, 2);
    Set(S(kRegServer), 5, 2);
    Set(S(kRegServer), 6, 2);
    S(kService).value[0] = 1;
    Set(S(kService), 5, 3, 0, ServiceQuery);
    S(kAllocator).table[3] = reinterpret_cast<void *>(&AllocatorAlloc);
    S(kAllocator).table[4] = reinterpret_cast<void *>(&AllocatorFree);

    // The side's object: on the vtable route ours from our constructor (vtables, count, empty vectors, recording, set and
    // log built), else the game's shape as the original Create builds it inline (no constructor function to call).
    Mgr *m = gRoute == 1 ? ::new (gW.work.bytes) Mgr : M();
    const uint32_t group = r() % 3 ? 7 : r() % 9;
    m->mRefCount = r() % 4;
    m->mGroup = group;
    FillVector(m->mFamilies, (fill & fFamilies) ? r() % 9 : 0, r() % 3, r, (fill & fNullFamily) != 0);
    FillVector(m->mRemovedFamilies, (fill & fRemoved) ? r() % 4 : 0, r() % 2, r, false);
    m->mRecording = uint8_t(r() % 2);
    if (gRoute != 1) new (&m->mMessageIds) std::set<uint32_t>();
    if (r() % 3 == 0) m->mMessageIds.insert(r() % 0x200);
    if (gRoute != 1) new (&m->mLog) FamilyLog();
    if (fill & fLog) {
        const unsigned count = r() % 3 ? r() % 6 : 24 + r() % 3;
        for (unsigned i = 0; i < count; ++i) m->mLog.emplace_back(i % 2 ? "abc" : "a longer entry text 0123456789");
    }
    InstallVtables();
    BuildIo(seed);
    w.events.clear();
}

using Blob = std::vector<uint32_t>;

void CaptureVector(Blob &b, const RefPtrVector &v)
{
    b.push_back(0xc0de);
    b.push_back(uint32_t(v.mEnd - v.mFirst));
    for (void **it = v.mFirst; it != v.mLast; ++it) b.push_back(Norm(P(*it)));
}

void Capture(Blob &b, bool alive = true)
{
    World &w = gW;
    b.push_back(0xb10b);
    for (auto v : w.work.before) b.push_back(v);
    for (auto v : w.work.after) b.push_back(v);
    Mgr *m = M();
    if (alive) {
        for (unsigned f = 0; f < 3; ++f) b.push_back(VtCode(f));
        b.push_back(m->mRefCount);
        b.push_back(m->mGroup);
        b.push_back(m->mRecording);
        for (auto pad : m->mPad2D) b.push_back(pad);
        CaptureVector(b, m->mFamilies);
        CaptureVector(b, m->mRemovedFamilies);
        b.push_back(0xc0e0);
        for (auto v : m->mMessageIds) b.push_back(v);
        b.push_back(0xc0e1);
        for (auto &e : m->mLog) {
            const auto *s = reinterpret_cast<const cRZString *>(e.mString);
            b.push_back(P(s->mVTable));
            b.push_back(s->mRefCount);
            b.push_back(uint32_t(std::hash<std::string>()(s->mString)));
        }
    }
    for (auto &o : w.fakes) {
        b.push_back(uint32_t(o.refs));
        for (auto v : o.value) b.push_back(v);
        b.push_back(o.memberCount);
        for (unsigned i = 0; i < o.memberCount; ++i) b.push_back(o.members[i]);
    }
    b.push_back(0xe7e7);
    for (uint32_t e : w.events) b.push_back(e);
}

bool Same(difftest::Context &ctx, const Blob &a, const Blob &b, const char *what, uint32_t seed)
{
    ++ctx.cases;
    size_t at = 0;
    while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
    if (at == a.size() && at == b.size()) return true;
    if (std::getenv("FAMGR_DUMP")) {
        for (const Blob *blob : {&a, &b}) {
            std::printf("--");
            for (size_t i = 0; i < blob->size(); ++i) std::printf("%s%x", i % 16 ? " " : "\n", (*blob)[i]);
            std::printf("\n");
        }
    }
    ctx.Fail("%s seed %u differs at word %zu (original %x, replacement %x; sizes %zu/%zu)", what, seed, at,
        at < a.size() ? a[at] : 0xdeadbeef, at < b.size() ? b[at] : 0xdeadbeef, a.size(), b.size());
    return false;
}

template <class Setup, class Run>
void Pair(difftest::Context &ctx, const char *what, uint32_t seed, uint32_t fill, Setup setup, Run run, bool alive = true)
{
    Blob blobs[3];
    for (int route = 0; route < 3; ++route) {
        gRoute = route;
        gTrapHits = 0;
        Build(seed, fill);
        std::mt19937 rng(seed * 7919u + 5);
        setup(rng);
        const uint32_t result = uint32_t(run(route == 0));
        blobs[route].push_back(result);
        Capture(blobs[route], alive);
        if (gTrapHits) ctx.Fail("%s seed %u route %d: %u trap hits", what, seed, route, gTrapHits);
    }
    gRoute = 0;
    Same(ctx, blobs[0], blobs[1], (std::string(what) + " (vtable route)").c_str(), seed);
    Same(ctx, blobs[0], blobs[2], (std::string(what) + " (adapter route)").c_str(), seed);
}
auto NoSetup = [](std::mt19937 &) {};

constexpr uint32_t kSeeds = 150;
#define ORIG(Fn, rva) ctx.Original<Fn>(rva)
} // namespace

// ---- construction and destruction ----
DIFF_TEST(FamilyManager_Create, 0x4b5440)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < 16; ++seed) {
        Build(seed, 0);
        // b: the Create hook's entry (our class, our constructor); c: a game-built manager for the adapter's destruction.
        Mgr *a = ORIG(Mgr *(__cdecl *)(), 0x4b5440)();
        Mgr *b = Mgr::Create();
        Mgr *c = ORIG(Mgr *(__cdecl *)(), 0x4b5440)();
        ++ctx.cases;
        if (!a || !b || !c) { ctx.Fail("create returned null"); continue; }
        Blob blobs[2];
        for (Mgr *m : {a, b}) {
            Blob &out = blobs[m == b];
            // Each side's own vtables: the game's in the original's object, ours in ours.
            for (unsigned f = 0; f < 3; ++f) {
                const void *word = reinterpret_cast<void *const *>(m)[f];
                out.push_back(word == (m == a ? GameVt(f) : gOurs[f]) ? 0x7d00 + f : P(word));
            }
            out.push_back(m->mRefCount); // +0x10 is left unset
            for (const RefPtrVector *v : {&m->mFamilies, &m->mRemovedFamilies})
                out.push_back(P(v->mFirst)), out.push_back(P(v->mLast)), out.push_back(P(v->mEnd));
            out.push_back(m->mRecording);
            out.push_back(uint32_t(m->mMessageIds.size()));
            out.push_back(uint32_t(m->mLog.size()));
            out.push_back(m->mLog.begin() == m->mLog.end());
            out.push_back(HeapSize(GetProcessHeap(), 0, m) >= sizeof(Mgr));
        }
        Same(ctx, blobs[0], blobs[1], "Create", seed);
        // a by the original deleting destructor, b through slot 26 of its own vtable, c by the original again (its adapter was
        // dropped, lane GJ: only the game's vtable reached it).
        using DeleteFn = Mgr *(__thiscall *)(Mgr *, uint32_t);
        for (int side = 0; side < 3; ++side) {
            Mgr *m = side == 0 ? a : side == 1 ? b : c;
            difftest::FreeRecorder rec;
            Mgr *r = side == 1 ? reinterpret_cast<DeleteFn>(gOurs[0][26])(m, 1) : ORIG(DeleteFn, 0x4b4550)(m, 1);
            rec.Stop();
            ++ctx.cases;
            if (!rec.Freed(m) || r != m) ctx.Fail("DeletingDestructor(1) seed %u side %d kept the block", seed, side);
        }
    }
}

DIFF_TEST(FamilyManager_Destructor, 0x4b4550)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Blob blobs[3];
        for (int side = 0; side < 3; ++side) {
            gRoute = side;
            gTrapHits = 0;
            Build(seed, fFamilies | fRemoved | fLog | (seed % 3 ? fNullFamily : 0));
            // The blocks the manager owns: both vectors' storage, the set's head and nodes, the log's head, nodes and long
            // texts. Each must be freed.
            Mgr *m = M();
            std::vector<const void *> blocks = {m->mFamilies.mFirst, m->mRemovedFamilies.mFirst,
                *reinterpret_cast<void *const *>(&m->mMessageIds), *reinterpret_cast<void *const *>(&m->mLog)};
            for (auto &id : m->mMessageIds) blocks.push_back(reinterpret_cast<const uint8_t *>(&id) - 0x10);
            for (auto &e : m->mLog) {
                blocks.push_back(reinterpret_cast<const uint8_t *>(&e) - 8);
                const auto *s = reinterpret_cast<const cRZString *>(e.mString);
                if (s->mString.capacity() > 15) blocks.push_back(s->mString.data());
            }
            difftest::FreeRecorder rec;
            Mgr *r = Call<Mgr *(__thiscall *)(void *, uint32_t)>(ctx, side == 0, 0x4b4550, 0, NO_ADAPTER,
                uint32_t(0));
            rec.Stop();
            blobs[side].push_back(Norm(P(r)));
            for (unsigned f = 0; f < 3; ++f) blobs[side].push_back(VtCode(f));
            for (const void *block : blocks) blobs[side].push_back(block ? rec.Freed(block) : 2);
            blobs[side].push_back(P(m->mFamilies.mFirst) | P(m->mFamilies.mLast) | P(m->mRemovedFamilies.mEnd));
            Capture(blobs[side], false);
            if (gTrapHits) ctx.Fail("Destructor seed %u route %d: trap hit", seed, side);
        }
        gRoute = 0;
        Same(ctx, blobs[0], blobs[1], "Destructor (vtable route)", seed);
        Same(ctx, blobs[0], blobs[2], "Destructor (adapter route)", seed);
    }
}

// ---- cIGZUnknown and messages ----
DIFF_TEST(FamilyManager_Unknown, 0x4b5f50)
{
    Globals globals(ctx);
    using QiFn = uint8_t(__thiscall *)(void *, uint32_t, void **);
    using ByteFn = uint8_t(__thiscall *)(void *);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        const uint32_t ids[] = {1, 0x0c1e4682, 0x8db5bf48, 0x456b8f1d, seed * 77777};
        const uint32_t id = ids[seed % 5];
        Pair(ctx, "QueryInterface", seed, 0, NoSetup, [&](bool original) {
            void *out = reinterpret_cast<void *>(0x5a5a);
            const uint8_t r = Call<QiFn>(ctx, original, 0x4b5f50, 0, ADAPTER(QueryInterface), id, &out);
            Ev({Norm(P(out))});
            return r;
        });
        Pair(ctx, "Release", seed, 0, NoSetup, [&](bool original) {
            return Call<uint32_t(__thiscall *)(void *)>(ctx, original, 0x4b6880, 0, ADAPTER(Release));
        });
        Pair(ctx, "RegisterMessages", seed, 0, NoSetup, [&](bool original) {
            return Call<ByteFn>(ctx, original, 0x4b5770, 0, NO_ADAPTER);
        });
        Pair(ctx, "UnregisterMessages", seed, 0, [](std::mt19937 &r) { if (r() % 2) M()->mMessageIds.insert({0xb2, 0xaf}); S(kService).value[0] = r() % 4 != 0; },
            [&](bool original) {
                return Call<ByteFn>(ctx, original, 0x4b6e40, 0, ADAPTER(UnregisterMessages));
            });
    }
}

DIFF_TEST(FamilyManager_DoMessage, 0x4b5180)
{
    Globals globals(ctx);
    using Fn = uint8_t(__thiscall *)(void *, void *);
    const uint32_t ids[] = {0xb2, 0xb5, 0xaf, 0xb6, 0xcded7b51, 0xb3, 0xb0, 0xae, 0xb7, 0};
    for (uint32_t seed = 0; seed < kSeeds * 3; ++seed) {
        const uint32_t id = ids[seed % 10];
        const bool none = seed % 23 == 0;
        Pair(ctx, "DoMessage", seed, fFamilies | fLog | (seed % 4 == 0 ? fNullFamily : 0),
            [&](std::mt19937 &) { Set(S(kMessage), 4, 0, id); },
            [&](bool original) {
                void *message = none ? nullptr : &S(kMessage);
                return Call<Fn>(ctx, original, 0x4b5180, 2, NO_ADAPTER, message);
            });
    }
}

// ---- lookups ----
DIFF_TEST(FamilyManager_Lookups, 0x4b5540)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds * 2; ++seed) {
        std::mt19937 v(seed + 3);
        const uint32_t fill = fFamilies | (seed % 3 == 0 ? fNullFamily : 0);
        const int32_t id = seed % 4 == 0 ? -2 : seed % 7 == 0 ? int32_t(v()) : int32_t(v() % 34) - 1;
        using FindFn = void *(__thiscall *)(void *, int32_t);
        Pair(ctx, "GetFamily", seed, fill, NoSetup, [&](bool original) {
            return Norm(P(Call<FindFn>(ctx, original, 0x4b5540, 0, NO_ADAPTER, id)));
        });
        const int32_t lot = int32_t(v() % 5) - 1;
        Pair(ctx, "FindFamilyByLot", seed, fill, NoSetup, [&](bool original) {
            return Norm(P(Call<FindFn>(ctx, original, 0x4b5640, 0, NO_ADAPTER, lot)));
        });
        Pair(ctx, "FindFamilyBySlot44", seed, fill, NoSetup, [&](bool original) {
            return Norm(P(Call<FindFn>(ctx, original, 0x4b56b0, 0, NO_ADAPTER, lot + 1)));
        });
        Pair(ctx, "GetFamilyWorth", seed, 0, NoSetup, [&](bool original) {
            using Fn = int32_t(__thiscall *)(void *, void *);
            return Call<Fn>(ctx, original, 0x4b5680, 0, NO_ADAPTER, static_cast<void *>(&F(seed % kFamilies)));
        });
        const int32_t familyId = int32_t(v() % 14);
        Pair(ctx, "MoveFamilyIntoLot", seed, fill, NoSetup, [&](bool original) {
            using Fn = uint8_t(__thiscall *)(void *, int32_t, int32_t);
            return Call<Fn>(ctx, original, 0x4b5ef0, 0, NO_ADAPTER, familyId, lot);
        });
        Pair(ctx, "IsObjectInFamily", seed, 0, NoSetup, [&](bool original) {
            using Fn = uint8_t(__thiscall *)(void *, int32_t, void *);
            return Call<Fn>(ctx, original, 0x4b57d0, 0, NO_ADAPTER, familyId,
                static_cast<void *>(&S(kNeighbor)));
        });
    }
}

DIFF_TEST(FamilyManager_FamilyIds, 0x4b55e0)
{
    Globals globals(ctx);
    using IdVector = std::vector<uint16_t, GZServiceAllocator<uint16_t>>;
    using Fn = void(__thiscall *)(void *, void *);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        for (int which = 0; which < 2; ++which) {
            Blob blobs[3];
            for (int side = 0; side < 3; ++side) {
                gRoute = side;
                gTrapHits = 0;
                Build(seed, fFamilies | (seed % 3 == 0 ? fNullFamily : 0));
                IdVector ids;
                for (unsigned i = 0; i < seed % 4; ++i) ids.push_back(uint16_t(0x900 + i));
                gW.events.clear();
                if (which) Call<Fn>(ctx, side == 0, 0x4b56f0, 0, NO_ADAPTER, static_cast<void *>(&ids));
                else Call<Fn>(ctx, side == 0, 0x4b55e0, 0, NO_ADAPTER, static_cast<void *>(&ids));
                blobs[side].push_back(uint32_t(ids.capacity()));
                for (auto id : ids) blobs[side].push_back(id);
                Capture(blobs[side]);
                if (gTrapHits) ctx.Fail("FamilyIds seed %u route %d: trap hit", seed, side);
            }
            gRoute = 0;
            const char *what = which ? "GetHomelessFamilyIds" : "GetFamilyIds";
            Same(ctx, blobs[0], blobs[1], (std::string(what) + " (vtable route)").c_str(), seed);
            Same(ctx, blobs[0], blobs[2], (std::string(what) + " (adapter route)").c_str(), seed);
        }
    }
}

// ---- loading and saving ----
DIFF_TEST(FamilyManager_Load, 0x4b5860)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds * 2; ++seed) {
        const uint32_t group = seed % 3 ? 7 : seed % 5;
        Pair(ctx, "Load", seed, fFamilies | fRemoved, [&](std::mt19937 &) { gW.groupMatch = group == 7; },
            [&](bool original) {
                using Fn = uint8_t(__thiscall *)(void *, uint32_t);
                return Call<Fn>(ctx, original, 0x4b5860, 0, NO_ADAPTER, group);
            });
    }
}

DIFF_TEST(FamilyManager_UnloadSave, 0x4b6eb0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        const uint32_t fill = fFamilies | fRemoved | (seed % 3 == 0 ? fNullFamily : 0);
        Pair(ctx, "Unload", seed, fill, NoSetup, [&](bool original) {
            Call<void(__thiscall *)(void *)>(ctx, original, 0x4b6eb0, 0, NO_ADAPTER);
            return 0;
        });
        Pair(ctx, "Save", seed, fill, NoSetup, [&](bool original) {
            return Call<uint8_t(__thiscall *)(void *)>(ctx, original, 0x4b6c80, 0, NO_ADAPTER);
        });
    }
}

// ---- creating and removing ----
DIFF_TEST(FamilyManager_CreateFamily, 0x4b5c00)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds * 2; ++seed) {
        std::mt19937 v(seed + 41);
        const int32_t id = seed % 3 == 0 ? 31999 + int32_t(v() % 4) : int32_t(v() % 20) - 2;
        const bool singletonC = true; // a null cache would be filled through the service, differently per side
        Pair(ctx, "CreateFamily", seed, fFamilies | fRemoved, [&](std::mt19937 &) { globals.Point(singletonC); },
            [&](bool original) {
                using Fn = uint8_t(__thiscall *)(void *, void **, int32_t);
                void *out = nullptr;
                const uint8_t r = Call<Fn>(ctx, original, 0x4b5c00, 0, NO_ADAPTER, &out, id);
                Ev({Norm(P(out))});
                return r;
            });
    }
    globals.Point(true);
}

DIFF_TEST(FamilyManager_RemoveFamily, 0x4b6920)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds * 2; ++seed) {
        void *family = seed % 9 == 0 ? nullptr : &F(seed % kFamilies);
        Pair(ctx, "RemoveFamily", seed, fFamilies | fRemoved | (seed % 4 == 0 ? fNullFamily : 0),
            [&](std::mt19937 &r) {
                if (family && r() % 2) F(seed % kFamilies).value[1] = 0;
                // the live household's value 9 is compared as 16 bits
                if (r() % 2) Set(S(kHousehold), 15, 1, (r() % 2 ? 0xabcd0000u : 0) | F(seed % kFamilies).value[0]);
            },
            [&](bool original) {
                using Fn = int32_t(__thiscall *)(void *, void *);
                return Call<Fn>(ctx, original, 0x4b6920, 0, NO_ADAPTER, family);
            });
        Pair(ctx, "RemoveEmptyFamilies", seed, fFamilies | (seed % 4 == 0 ? fNullFamily : 0), NoSetup, [&](bool original) {
            Call<void(__thiscall *)(void *)>(ctx, original, 0x4b68b0, 0, NO_ADAPTER);
            return 0;
        });
    }
}

// ---- neighbours ----
DIFF_TEST(FamilyManager_Neighbors, 0x4b4a40)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds * 3; ++seed) {
        void *family = seed % 11 == 0 ? nullptr : &F(seed % kFamilies);
        void *previous = seed % 5 == 0 ? nullptr : &F((seed / 3) % kFamilies);
        void *neighbor = seed % 13 == 0 ? nullptr : &S(kNeighbor);
        auto lots = [](std::mt19937 &) { Set(S(kApp), 6, 0, &S(kLots)); }; // used unchecked here
        Pair(ctx, "AddNeighborToFamily", seed, fFamilies, lots, [&](bool original) {
            using Fn = int32_t(__thiscall *)(void *, void *, void *, void *);
            return Call<Fn>(ctx, original, 0x4b4a40, 0, NO_ADAPTER, neighbor, family, previous);
        });
        Pair(ctx, "RemoveNeighborFromFamily", seed, fFamilies, lots, [&](bool original) {
            using Fn = int32_t(__thiscall *)(void *, void *, void *);
            return Call<Fn>(ctx, original, 0x4b6b10, 0, NO_ADAPTER, static_cast<void *>(&S(kNeighbor)),
                family);
        });
    }
}

// ---- lot checks ----
DIFF_TEST(FamilyManager_LotChecks, 0x4b4f30)
{
    Globals globals(ctx);
    using Fn = int32_t(__thiscall *)(void *, int32_t, int32_t);
    for (uint32_t seed = 0; seed < kSeeds * 4; ++seed) {
        std::mt19937 v(seed + 5);
        const int32_t id = int32_t(v() % 14);
        const int32_t lot = seed % 7 == 0 ? 4 : int32_t(v() % 5);
        const uint32_t fill = fFamilies | (seed % 5 == 0 ? fNullFamily : 0);
        // Every seventh seed reaches the price check: an empty lot of an ordinary type, a priced answer, funds equal to,
        // above or below the price.
        auto priced = [&](std::mt19937 &r) {
            if (seed % 7) return;
            Set(S(kLotType), 8, 0, 2);
            Set(S(kLotObj), 32, 0, 8);
            S(kLots).value[2] = 1, S(kLots).value[3] = 1, S(kLots).value[0] = 200;
            for (unsigned i = 0; i < kFamilies; ++i) Set(F(i), 53, 0, 100 + (r() % 3) * 100), F(i).value[5] = 1;
        };
        Pair(ctx, "CanMoveFamilyIntoLot", seed, fill, priced, [&](bool original) {
            return Call<Fn>(ctx, original, 0x4b4f30, 0, NO_ADAPTER, id, lot);
        });
        const int32_t count = seed % 4 == 0 ? int32_t(v()) : int32_t(v() % 10) - 2;
        Pair(ctx, "CheckLotAges", seed, fill, NoSetup, [&](bool original) {
            return Call<Fn>(ctx, original, 0x4b4e40, 0, NO_ADAPTER, count, lot);
        });
        Pair(ctx, "CheckLotMembers", seed, fill, NoSetup,
            [&](bool original) {
                // slot 11 must find a family: use the id of the first non-null family, if any.
                int32_t famId = id;
                for (void **it = M()->mFamilies.mFirst; it != M()->mFamilies.mLast; ++it)
                    if (*it) { famId = int32_t(static_cast<SObj *>(*it)->value[0]); break; }
                bool any = false;
                for (void **it = M()->mFamilies.mFirst; it != M()->mFamilies.mLast; ++it) any = any || *it;
                if (!any) return 0x7777;
                return Call<Fn>(ctx, original, 0x4b4d40, 0, NO_ADAPTER, famId, lot);
            });
    }
}

// ---- the data container face ----
DIFF_TEST(FamilyManager_ExportImport, 0x4b6f90)
{
    Globals globals(ctx);
    using Fn = uint8_t(__thiscall *)(void *, void *);
    for (uint32_t seed = 0; seed < kSeeds * 4; ++seed) {
        const bool noContainer = seed % 29 == 0;
        const uint32_t fill = fFamilies | (seed % 4 == 0 ? fNullFamily : 0);
        auto exportSetup = [](std::mt19937 &r) {
            for (unsigned i = 0; i < kFamilies; ++i) Set(F(i), 34, 1, r() % 5 ? 1 : 0x100), Set(F(i), 35, 1, r() % 5 ? 3 : 0);
            Set(S(kLotData), 3, 1, r() % 3 ? (r() % 2 ? 1 : 3) : (r() % 2 ? 0 : 0x100));
        };
        Pair(ctx, "Export", seed, fill, exportSetup, [&](bool original) {
            void *container = noContainer ? nullptr : &S(kContainer);
            return Call<Fn>(ctx, original, 0x4b6f90, 1, NO_ADAPTER, container);
        });
        auto importSetup = [](std::mt19937 &) {
            Set(S(kPersist), 5, 5, 0, PersistGetIo);
            Set(S(kFramework), 3, 3, 0, FrameworkLot);
        };
        Pair(ctx, "Import", seed, fill, importSetup, [&](bool original) {
            void *container = noContainer ? nullptr : &S(kContainer);
            return Call<Fn>(ctx, original, 0x4b5fb0, 1, NO_ADAPTER, container);
        });
    }
}

// ---- the statistics report (vf9, 0x4b45b0) ----
// The sink logs the report it is handed field by field (the original leaves padding bytes unwritten: skipped).
uint32_t StatNeighborValue(SObj *s, uint32_t *a)
{
    s->value[0] = s->value[0] * 1103515245u + 12345u;
    const uint32_t bits = s->value[0] >> 8;
    switch (a[0]) {
    case 0x41: return bits % 3 == 0 ? 0x10000u : bits % 3 == 1 ? 1 : 0;
    case 0x3a: return bits % 4 == 0 ? 0x11 : bits % 4 == 1 ? 0x12 : bits % 4 == 2 ? 0xfff0 : 0x20 + (bits >> 4 & 0xff);
    case 0x38: return bits % 2;
    default: return (bits >> 3) % 5 == 0 ? 0x7fff : (bits >> 3) % 5 == 1 ? 0x8000 : (bits >> 4) % 90;
    }
}
uint32_t StatNeighbor(SObj *s, uint32_t *a) { return a[0] % 4 == 0 ? 0 : P(&S(kNeighbor)); }
uint32_t StatMemberCount(SObj *s, uint32_t *) { return s->memberCount; }
uint32_t StatMemberAt(SObj *s, uint32_t *a) { return P(&s->members[a[0]]); }
uint32_t StatCreatedValue(SObj *s, uint32_t *a) { return s->value[0] + a[0] * 7; }
uint32_t StatXHolder(SObj *, uint32_t *a) { Give(reinterpret_cast<void *>(a[0]), kHolder); return 0; }
uint32_t StatFrameworkCreate(SObj *, uint32_t *a) { Give(reinterpret_cast<void *>(a[2]), kStatCreated); return 1; }
uint32_t StatLotStateFamily(SObj *s, uint32_t *) { return s->value[0] % 3 ? 0 : P(&F(s->value[1] % kFamilies)); }
uint32_t StatFamily61(SObj *s, uint32_t *) { return s->value[7]; }
uint32_t SinkTake(SObj *, uint32_t *a)
{
    const auto *p = reinterpret_cast<const uint8_t *>(a[0]);
    Ev({0x5747});
    for (unsigned offset = 0; offset < 0x43; ++offset) Ev({p[offset]});
    Ev({p[0x50], p[0x51]});
    const auto *first = *reinterpret_cast<const uint8_t *const *>(p + 0x44);
    const auto *last = *reinterpret_cast<const uint8_t *const *>(p + 0x48);
    const auto *end = *reinterpret_cast<const uint8_t *const *>(p + 0x4c);
    Ev({uint32_t((last - first) / 0x20), uint32_t(end >= last)});
    for (const uint8_t *e = first; e != last; e += 0x20) {
        const auto *s = reinterpret_cast<const cRZString *>(e);
        Ev({P(s->mVTable), s->mRefCount, uint32_t(s->mString.size()), uint32_t(std::hash<std::string>()(s->mString))});
    }
    return 0;
}

DIFF_TEST(FamilyManager_ReportStatistics, 0x4b45b0)
{
    Globals globals(ctx);
    using Fn = void(__thiscall *)(void *);
    for (uint32_t seed = 0; seed < kSeeds * 2; ++seed) {
        auto setup = [seed](std::mt19937 &r) {
            S(kLotState).value[0] = seed % 5, S(kLotState).value[1] = r();
            Set(S(kLotState), 16, 0, 0, StatLotStateFamily);
            Set(S(kLotState), 27, 0, seed % 7 == 0 ? 0 : r() % 4);
            S(kNeighbor).value[0] = r();
            Set(S(kNeighbor), 10, 2, 0, StatNeighborValue);
            Set(S(kNbMgr), 43, 1, 0, StatNeighbor);
            Set(S(kApp), 13, 0, &S(kStatX));
            Set(S(kStatX), 10, 1, 0, StatXHolder);
            for (unsigned slot : {3u, 7u, 8u}) Set(S(kHolder), slot, 0, r() | (r() % 2 ? 0x10000u : 0));
            for (unsigned slot : {29u, 35u, 36u}) Set(S(kHousehold), slot, 0, r() % 5);
            Set(S(kHousehold), 45, 1);
            Set(S(kFramework), 3, 3, 0, StatFrameworkCreate);
            S(kStatCreated).value[0] = r() % 100;
            Set(S(kStatCreated), 4, 1, 0, StatCreatedValue);
            Set(S(kSink), 6, 1, 0, SinkTake);
            for (unsigned i = 0; i < kFamilies; ++i) {
                SObj &f = F(i);
                Set(f, 24, 1, 0, StatMemberCount);
                Set(f, 26, 1, 0, StatMemberAt);
                Set(f, 61, 0, 0, StatFamily61);
                Set(f, 65, 0, seed % 6 == 0 ? 0 : P(&S(kSink)));
                f.value[7] = r();
                f.memberCount = r() % 6;
                for (unsigned k = 0; k < f.memberCount; ++k) f.members[k] = r() % 30;
            }
        };
        Pair(ctx, "ReportStatistics", seed, fFamilies | fLog | (seed % 4 == 0 ? fNullFamily : 0), setup, [&](bool original) {
            *ctx.image.At<void *>(0x10cc2a8) = nullptr; // the current-lot cache starts empty on both sides
            Call<Fn>(ctx, original, 0x4b45b0, 0, NO_ADAPTER);
            return 0;
        });
    }
}

// ---- the slots no other test calls, through each side's own vtable ----
// Primary slot 1 (AddRef, folded 0x15e520) and 16 (the family count, folded 0x3a5a20); the container face's QI / AddRef /
// Release thunks (0x4b5f99, folded 0x1f4daf, 0x4b6897) and slot 3 (folded 0x18a7a0, false); the message face's thunks
// (0x4b5fa1, folded 0x1f4db7, 0x4b689f). The original side calls the game's vtable slot, ours our vtable's.
DIFF_TEST(FamilyManager_Slots, 0x4b5f99)
{
    Globals globals(ctx);
    struct Thunk { unsigned face, slot; uint32_t rva; };
    for (const Thunk &t : {Thunk{1, 0, 0x4b5f99}, Thunk{2, 0, 0x4b5fa1}, Thunk{1, 2, 0x4b6897}, Thunk{2, 2, 0x4b689f}}) {
        ++ctx.cases;
        if (GameVt(t.face)[t.slot] != GameAddress<void>(t.rva)) ctx.Fail("face %u slot %u is not %x", t.face, t.slot, t.rva);
    }
    {
        Mgr *m = ::new (gW.work.bytes) Mgr;
        ++ctx.cases;
        if (static_cast<void *>(static_cast<cITSDataContainer *>(m)) != Face(1) ||
            static_cast<void *>(static_cast<cIGZMessageTarget2 *>(m)) != Face(2))
            ctx.Fail("face offsets");
        m->Mgr::~Mgr();
    }
    struct Case { unsigned face, slot, kind; const char *what; };
    enum { kNone, kQuery, kContext };
    const Case cases[] = {{0, 1, kNone, "AddRef"}, {0, 16, kNone, "GetFamilyCount"}, {1, 0, kQuery, "container QI"},
        {1, 1, kNone, "container AddRef"}, {1, 2, kNone, "container Release"}, {1, 3, kContext, "CollectReferenced"},
        {2, 0, kQuery, "message QI"}, {2, 1, kNone, "message AddRef"}, {2, 2, kNone, "message Release"}};
    for (uint32_t seed = 0; seed < 60; ++seed) {
        const uint32_t ids[] = {1, 0x0c1e4682, 0x8db5bf48, 0x456b8f1d};
        for (const Case &c : cases) {
            Blob blobs[2];
            for (int side = 0; side < 2; ++side) {
                gRoute = side;
                Build(seed, fFamilies | (seed % 3 == 0 ? fNullFamily : 0));
                void *self = Face(c.face);
                void *fn = (side == 0 ? GameVt(c.face) : gOurs[c.face])[c.slot];
                uint32_t result;
                if (c.kind == kQuery) {
                    void *out = reinterpret_cast<void *>(0x5a5a);
                    result = reinterpret_cast<uint8_t(__thiscall *)(void *, uint32_t, void **)>(fn)(self, ids[seed % 4], &out);
                    Ev({Norm(P(out))});
                } else if (c.kind == kContext) {
                    result = reinterpret_cast<uint8_t(__thiscall *)(void *, void *)>(fn)(self, &S(kContainer));
                } else {
                    result = reinterpret_cast<uint32_t(__thiscall *)(void *)>(fn)(self);
                }
                blobs[side].push_back(result);
                Capture(blobs[side]);
            }
            gRoute = 0;
            Same(ctx, blobs[0], blobs[1], c.what, seed);
        }
    }
}
