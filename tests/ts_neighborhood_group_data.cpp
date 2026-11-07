// Differential tests for cTSNeighborhoodGroupData (src/ts/cTSNeighborhoodGroupData.h).
//
// A world is one group in a guarded buffer whose vtable is a table of recording fakes for the slots its own members call
// through `this` (slots 9, 10), filled by the test with std containers like the game's: neighbourhoods keyed by id with a
// dirty byte (+0xc), a second table from another key to those entries (+0x2c), the ordered map of held objects with its
// cursor, and the two counted pointers. The neighbourhoods, their key objects, the source, the persist system and its record
// table are generic recording fakes; the big refresh 0x476a40 (an original callee) is replaced on both sides by a recording
// stand-in. Every call a fake receives is appended to `events` (pointers to fakes become ids, pointers into the group their
// offset, stack addresses a marker; keys by content). Two worlds built from one seed start the same; one runs the original,
// the other ours, and the result, the group's canonical state (the tables' list order, bucket vectors and masks), the fakes'
// reference counts, the events and the sizes of the freed blocks are compared. Fakes that give back 8-bit answers fill the
// rest of eax with junk.
#include "game_allocator.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "hook/hooks.h"
#include "rz/GeneralAllocator.h"
#include "shape.h"
#include "simantics/ServiceRefVector.h"
#include "ts/cTSNeighborhoodGroupData.h"

#include <windows.h>

#include <cstring>
#include <initializer_list>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {
using Grp = cTSNeighborhoodGroupData;
using Blob = std::vector<uint32_t>;
constexpr unsigned kSlots = 64;
constexpr unsigned kNbCount = 6;

struct SObj;
using Custom = uint32_t (*)(SObj *self, uint32_t *args);
struct SObj {
    void **vt;
    uint32_t id;
    int32_t refs;
    uint32_t value[8];
    uint32_t ret[kSlots];
    Custom custom[kSlots];
    void *table[kSlots];
};

constexpr unsigned kInfoCount = 5;
enum Fake : unsigned {
    kSource, kSourceKey, kPersist, kRecords, kNb0, kKey0 = kNb0 + kNbCount, kMem0 = kKey0 + kNbCount,
    // LoadNeighborhoods: the globals (0x10cc288), the neighbourhood manager, a file prefix string, infos and their persist faces
    kGlobals = kMem0 + kNbCount, kManager, kPrefix, kInfo0, kFace0 = kInfo0 + kInfoCount, kFakeCount = kFace0 + kInfoCount
};

struct Guarded {
    uint8_t before[16];
    alignas(8) uint8_t bytes[sizeof(Grp)];
    uint8_t after[16];
};

struct World {
    Guarded work;
    SObj fakes[kFakeCount];
    Blob events;
    uint32_t junk = 0;
    uint32_t refreshAnswer = 0;
};
World gW;
Grp *G() { return reinterpret_cast<Grp *>(gW.work.bytes); }
SObj &S(unsigned f) { return gW.fakes[f]; }

enum : uint32_t { evCall = 0x60, evGroup = 0x61, evKey = 0x62, evRefresh = 0x63 };
void Ev(std::initializer_list<uint32_t> items) { for (uint32_t v : items) gW.events.push_back(v); }

uint32_t Norm(uint32_t value)
{
    if (!value) return 0;
    for (unsigned i = 0; i < kFakeCount; ++i)
        if (value == uint32_t(uintptr_t(&gW.fakes[i]))) return 0x1000 + i;
    const uint32_t base = uint32_t(uintptr_t(gW.work.bytes));
    if (value >= base && value < base + sizeof(Grp)) return 0x2000 + (value - base);
    uint32_t local = 0;
    const uint32_t sp = uint32_t(uintptr_t(&local));
    if (value - sp < 0x10000 || sp - value < 0x10000) return 0x57ac;
    return value;
}
uint32_t P(const void *p) { return Norm(uint32_t(uintptr_t(p))); }

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
void Set(SObj &o, unsigned slot, unsigned args, const void *result) { Set(o, slot, args, uint32_t(uintptr_t(result))); }

void ResetFake(SObj &o, uint32_t id)
{
    o.vt = o.table;
    o.id = id;
    o.refs = 1000;
    for (auto &v : o.value) v = 0;
    for (unsigned i = 0; i < kSlots; ++i) o.ret[i] = 0, o.custom[i] = nullptr, o.table[i] = nullptr;
    Set(o, 1, 0);
    Set(o, 2, 0);
}

uint32_t Junk8(uint32_t low8) { return (low8 & 0xff) | (gW.junk << 8); }

// ---- custom handlers ----
uint32_t AnswerValue0(SObj *self, uint32_t *) { return Junk8(self->value[0]); }
uint32_t AnswerValue1(SObj *self, uint32_t *) { return Junk8(self->value[1]); }
uint32_t AnswerValue2(SObj *self, uint32_t *) { return Junk8(self->value[2]); }
uint32_t AnswerValue3(SObj *self, uint32_t *) { return Junk8(self->value[3]); }
uint32_t AnswerWide(SObj *self, uint32_t *args) { return self->value[4] ^ args[0]; }
uint32_t AnswerWide2(SObj *self, uint32_t *args) { return self->value[5] + args[0]; }
uint32_t IdAnswer(SObj *self, uint32_t *) { return self->value[0]; }
// A member's slot 18 answers a different number on each call (the id plus 100 times the call number modulo 3), so the three
// calls the rebuild makes per member can be told apart; none ever equals the skipped ids used by the test.
uint32_t MemberId(SObj *self, uint32_t *) { return self->value[0] + 100 * (self->value[1]++ % 3); }
// Slot 54 of a neighbourhood starts a walk over its members (ret[54] of them, taken from the member fakes by position) and
// gives the first; slot 55 gives the next (value[6] is the cursor). Slot 57: a member as an object (value[7] set) or a plain
// wide answer.
uint32_t MemberAt(SObj *self)
{
    return self->value[6] < self->ret[54] ? uint32_t(uintptr_t(&S(kMem0 + (self->id + self->value[6]) % kNbCount))) : 0;
}
uint32_t FirstMember(SObj *self, uint32_t *)
{
    self->value[6] = 0;
    return MemberAt(self);
}
uint32_t NextMember(SObj *self, uint32_t *)
{
    ++self->value[6];
    return MemberAt(self);
}
uint32_t Slot57(SObj *self, uint32_t *args)
{
    if (!self->value[7]) return self->value[5] + args[0];
    return args[0] % 3 == 0 ? 0 : uint32_t(uintptr_t(&S(kMem0 + args[0] % kNbCount)));
}
uint32_t KeyWriter(SObj *self, uint32_t *args)
{
    uint32_t *out = reinterpret_cast<uint32_t *>(args[0]);
    for (unsigned i = 0; i < 4; ++i) out[i] = self->value[i];
    return args[0];
}
uint32_t RecordStore(SObj *self, uint32_t *args)
{
    const uint32_t *key = reinterpret_cast<const uint32_t *>(args[0]);
    Ev({evKey, key[0], key[1], key[2], key[3]});
    return Junk8(self->value[0]);
}

// ---- the group's own vtables: routes, copies, traps (tests/shape.h) ----
//
// Route 0 is the original side (the game's shape built by hand, as the original Create's inline constructor builds it, with
// copies of the game's vtables), route 1 our object from our constructor carrying copies of our vtables (the function under
// test called through our pristine vtable's slot), route 2 the game's shape with copies of the game's vtables and the RVA hook's
// adapter called, an arity-matched trap in the slot it forwards to. The fakes below (slots 1, 9, 10) are patched over each
// route's own copy of the primary vtable.
int gRoute = 0;
unsigned gTrapHits = 0;
constexpr unsigned kFaces = 2;
constexpr uint32_t kFaceOffset[kFaces] = {0, nbgroup::kUnknownFace};
constexpr uint32_t kGameVt[kFaces] = {Grp::kVTable, Grp::kUnknownVTable};
constexpr unsigned kFaceSlots[kFaces] = {22, 6};
constexpr uint32_t kGameRZUnknownVTable = 0xda2fd0; // what cRZUnknown's destructor 0x1b9096 leaves at +4
// Stack words per slot (a trap must return with the slot's `ret N`).
constexpr uint8_t kArity[kFaces][22] = {{2, 0, 0, 2, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1}, {2, 0, 0, 1, 0, 0}};
void *gCopy[kFaces][22];
void *gGroupPatch[22];
void *const *gOurs[kFaces];
void *gOurRZUnknown; // the +4 word our destructor leaves (rz::cRZUnknown's vtable)

void *Face(unsigned face) { return gW.work.bytes + kFaceOffset[face]; }
void *const *GameVt(unsigned face) { return shape::GameVTable(kGameVt[face]); }

void LearnOurs()
{
    if (gOurs[0]) return;
    for (unsigned f = 0; f < kFaces; ++f) gOurs[f] = shape::Ours<Grp>(kFaceOffset).face[f];
    alignas(8) static uint8_t scratch[sizeof(Grp)];
    Grp *g = ::new (scratch) Grp;
    g->Grp::~Grp();
    gOurRZUnknown = shape::VTableAt(scratch, nbgroup::kUnknownFace);
}

// Each face's word points at a copy of the route's own vtable of that face, the fakes patched over the primary one.
void InstallVtables(void *object)
{
    for (unsigned f = 0; f < kFaces; ++f) {
        void *const *own = gRoute == 1 ? gOurs[f] : GameVt(f);
        for (unsigned s = 0; s < kFaceSlots[f]; ++s) gCopy[f][s] = own[s];
        shape::VTableAt(object, kFaceOffset[f]) = gCopy[f];
    }
    for (unsigned s = 0; s < 22; ++s)
        if (gGroupPatch[s]) gCopy[0][s] = gGroupPatch[s];
}

// A vtable word as a code: the installed copy of face f, a vtable of face f the code stores (the game's or ours: a destructor's
// stores; the adapter runs our destructor on a game-built object), cRZUnknown's (game or ours, what the destructor leaves at
// +4), or the raw value.
uint32_t VtCode(unsigned face, const void *word)
{
    if (word == gCopy[face]) return 0x7c00 + face;
    if (word == GameVt(face) || word == gOurs[face]) return 0x7d00 + face;
    if (word == GameAddress<void>(kGameRZUnknownVTable) || word == gOurRZUnknown) return 0x7e00;
    return uint32_t(uintptr_t(word));
}
uint32_t VtCode(const void *object, unsigned face) { return VtCode(face, shape::VTableAt(const_cast<void *>(object), kFaceOffset[face])); }

uint32_t TrapHit(void *, unsigned)
{
    ++gTrapHits;
    gW.events.push_back(0x7eaf);
    return 0;
}
void *TrapFor(unsigned words) { return shape::TrapFor<TrapHit>(words); }
unsigned SlotOf(unsigned face, uint32_t rva) { return shape::SlotOf(kGameVt[face], kFaceSlots[face], rva); }

// Runs the function at `rva` the route's way on the world's group (`face` is the face of `this`): the original RVA, our
// vtable's slot holding it (the slot where the game's vtable of that face holds the RVA), or the adapter with a trap in that
// slot. A function that is no slot (LoadNeighborhoods) runs its adapter on both of our routes.
template <class Fn, class... A>
auto Call(difftest::Context &ctx, bool original, uint32_t rva, unsigned face, void *adapter, A... args)
{
    void *self = Face(face);
    if (original) return ctx.Original<Fn>(rva)(self, args...);
    const unsigned slot = SlotOf(face, rva);
    if (slot == ~0u) return reinterpret_cast<Fn>(adapter)(self, args...);
    if (gRoute == 1) return reinterpret_cast<Fn>(gOurs[face][slot])(self, args...);
    gCopy[face][slot] = TrapFor(kArity[face][slot]);
    return reinterpret_cast<Fn>(adapter)(self, args...);
}
#define ADAPTER(name) reinterpret_cast<void *>(&cTSNeighborhoodGroupDataAdapters::name)

uint32_t __fastcall GroupAddRef(Grp *g, void *)
{
    Ev({evGroup, 1});
    return ++g->mRefCount;
}
void __fastcall GroupSlot9(Grp *, void *) { Ev({evGroup, 9}); }
// Slot 20 (Next, which BeginIteration calls through `this`): logged, then the route's own Next (the game's on the original side
// and the adapter route, ours on the vtable route), so a devirtualised self-call shows as a missing event.
uint32_t __fastcall GroupSlot20(Grp *g, void *)
{
    Ev({evGroup, 20});
    void *const own = gRoute == 1 ? gOurs[0][20] : GameVt(0)[20];
    return reinterpret_cast<uint32_t(__thiscall *)(Grp *)>(own)(g);
}
void __fastcall GroupSlot10(Grp *, void *, int32_t arg) { Ev({evGroup, 10, uint32_t(arg)}); }

// A stand-in for the refresh 0x476a40 (__thiscall, one argument): logs and answers a byte with junk above it.
uint32_t __fastcall FakeRefresh(Grp *self, void *, void *source)
{
    Ev({evRefresh, P(self), P(source)});
    return Junk8(gW.refreshAnswer);
}

// Redirects a function of the mapped image to a stand-in (a jmp over its first five bytes) while it lives.
struct CodePatch {
    uint8_t *site;
    uint8_t saved[5];
    CodePatch(difftest::Context &ctx, uint32_t rva, void *stand) : CodePatch(ctx.image.At<uint8_t>(rva), stand) {}
    CodePatch(uint8_t *where, void *stand) : site(where)
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stand) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    CodePatch(const CodePatch &) = delete;
    ~CodePatch() { std::memcpy(site, saved, 5); }
};

// Our LoadNeighborhoods' code (Add and Refresh call the member directly, as the originals call 0x476a40): the function word of
// its member pointer (8 bytes for a class with two bases; the second word is the `this` adjustment, 0 here).
uint8_t *OurLoadNeighborhoods()
{
    auto member = &Grp::LoadNeighborhoods;
    static_assert(sizeof(member) == 8);
    void *code;
    std::memcpy(&code, &member, sizeof(code));
    return static_cast<uint8_t *>(code);
}

struct Globals {
    difftest::Context &ctx;
    uint32_t saved;
    explicit Globals(difftest::Context &c) : ctx(c), saved(*c.image.At<uint32_t>(0x10faf6c))
    {
        *ctx.image.At<void *>(0x10faf6c) = &S(kPersist);
    }
    ~Globals() { *ctx.image.At<uint32_t>(0x10faf6c) = saved; }
};

// Builds the world for a seed: the fakes, then the group with random contents.
void Build(uint32_t seed)
{
    static bool tables = false;
    if (!tables) {
        FillAll(std::make_index_sequence<kSlots>());
        tables = true;
    }
    World &w = gW;
    std::mt19937 r(seed * 2654435761u + 29);
    w.events.clear();
    w.events.reserve(8192);
    w.junk = r() & 0xffffff;
    w.refreshAnswer = r() % 3 ? 1 : r();
    for (unsigned i = 0; i < kFakeCount; ++i) ResetFake(w.fakes[i], i);

    Set(S(kSource), 24, 0, &S(kSourceKey));
    Set(S(kSourceKey), 6, 0, 0, IdAnswer);
    S(kSourceKey).value[0] = r() % 3 ? r() % 12 : r();
    Set(S(kPersist), 3, 0, &S(kRecords));
    Set(S(kRecords), 10, 1, 0, RecordStore);
    S(kRecords).value[0] = r() % 3 != 0;
    Set(S(kSource), 6, 1);
    for (unsigned i = 0; i < kNbCount; ++i) {
        SObj &nb = S(kNb0 + i);
        SObj &key = S(kKey0 + i);
        Set(nb, 6, 1);
        Set(nb, 7, 0, 0, AnswerValue0);
        Set(nb, 8, 0, 0, AnswerValue1);
        Set(nb, 12, 1);
        Set(nb, 14, 1);
        Set(nb, 16, 0, &key);
        Set(nb, 18, 0, 0, AnswerValue2);
        Set(nb, 19, 0);
        Set(nb, 20, 0, 0, AnswerValue3);
        Set(nb, 37, 1, 0, AnswerWide);
        Set(nb, 57, 1, 0, Slot57);
        Set(nb, 54, 0, r() % 5, FirstMember);
        Set(nb, 55, 0, 0, NextMember);
        for (unsigned k = 0; k < 4; ++k) nb.value[k] = r() % 4 != 0;
        nb.value[4] = r();
        nb.value[5] = r();
        nb.value[7] = 0;
        SObj &member = S(kMem0 + i);
        Set(member, 18, 0, 0, MemberId);
        member.value[0] = r() % 10;
        Set(key, 6, 0, 0, IdAnswer);
        Set(key, 3, 1, 0, KeyWriter);
        key.value[0] = r() % 3 ? r() % 12 : r();
        key.value[1] = r() % 5;
        key.value[2] = r() % 5;
        key.value[3] = r() % 5;
    }

    LearnOurs();
    std::memset(gGroupPatch, 0, sizeof(gGroupPatch));
    gGroupPatch[1] = reinterpret_cast<void *>(&GroupAddRef);
    gGroupPatch[9] = reinterpret_cast<void *>(&GroupSlot9);
    gGroupPatch[10] = reinterpret_cast<void *>(&GroupSlot10);
    gGroupPatch[20] = reinterpret_cast<void *>(&GroupSlot20);

    std::memset(w.work.before, 0xa5, sizeof(w.work.before));
    std::memset(w.work.after, 0x5a, sizeof(w.work.after));
    std::memset(w.work.bytes, 0xcd, sizeof(w.work.bytes));
    // The route's object: ours from our constructor, else the game's shape as the original Create's inline constructor builds it.
    Grp *g = gRoute == 1 ? ::new (w.work.bytes) Grp : G();
    if (gRoute != 1) {
        new (&g->mById) NeighborhoodGroupById();
        new (&g->mByKey) NeighborhoodGroupByKey();
        g->mCurrent = nullptr;
        g->mFirst = nullptr;
        new (&g->mOrdered) NeighborhoodGroupOrdered();
        g->mCursor = NeighborhoodGroupOrdered::iterator();
    }
    InstallVtables(g);
    g->mRefCount = r() % 4;
    auto anyNb = [&]() -> void * { return &S(kNb0 + r() % kNbCount); };
    const unsigned entries = r() % 8 == 0 ? 0 : r() % 9;
    std::vector<NeighborhoodGroupEntry *> made;
    for (unsigned i = 0; i < entries; ++i) {
        const uint32_t key = r() % 3 ? r() % 14 : r();
        NeighborhoodGroupEntry &e = g->mById[key];
        e.mNeighborhood = anyNb();
        e.mDirty = uint8_t(r() % 3 == 0);
        made.push_back(&e);
    }
    if (!made.empty())
        for (unsigned j = r() % 7; j > 0; --j) g->mByKey[r() % 3 ? r() % 14 : r()] = made[r() % made.size()];
    g->mCurrent = r() % 3 ? anyNb() : nullptr;
    g->mFirst = r() % 3 ? (r() % 2 ? g->mCurrent : anyNb()) : nullptr;
    const unsigned held = r() % 6;
    for (unsigned j = 0; j < held; ++j) g->mOrdered[r() % 3 ? int(r() % 10) - 2 : int(r())].mObject = r() % 5 ? anyNb() : nullptr;
    switch (r() % 4) {
    case 0: g->mCursor = g->mOrdered.begin(); break;
    case 1: g->mCursor = g->mOrdered.end(); break;
    case 2: {
        auto it = g->mOrdered.begin();
        for (unsigned k = r() % 4; k > 0 && it != g->mOrdered.end(); --k) ++it;
        g->mCursor = it;
        break;
    }
    default: g->mCursor = g->mOrdered.begin(); break;
    }
    w.events.clear();
}

// The shape of an unordered_map: its words, the list in order (payload per node), the bucket vector as list positions.
std::vector<uint32_t> CaptureTable(Blob &b, const void *table, void (*payload)(Blob &, const uint8_t *node, const std::vector<uint32_t> *byNode),
                                   const std::vector<uint32_t> *other = nullptr)
{
    const uint32_t *w = static_cast<const uint32_t *>(table);
    for (unsigned i : {0u, 2u, 6u, 7u}) b.push_back(w[i]);
    const uint32_t head = w[1];
    std::vector<uint32_t> nodes;
    for (uint32_t n = *reinterpret_cast<const uint32_t *>(head); n != head && nodes.size() < 64; n = *reinterpret_cast<const uint32_t *>(n))
        nodes.push_back(n);
    b.push_back(uint32_t(nodes.size()));
    for (uint32_t n : nodes) payload(b, reinterpret_cast<const uint8_t *>(n), other);
    const uint32_t first = w[3], last = w[4], end = w[5];
    b.push_back((last - first) / 4);
    b.push_back((end - first) / 4);
    for (uint32_t p = first; p < last; p += 4) {
        const uint32_t v = *reinterpret_cast<const uint32_t *>(p);
        uint32_t pos = 0xffff;
        if (v != head)
            for (size_t i = 0; i < nodes.size(); ++i)
                if (nodes[i] == v) pos = uint32_t(i);
        b.push_back(pos);
    }
    return nodes;
}
void ByIdPayload(Blob &b, const uint8_t *node, const std::vector<uint32_t> *)
{
    b.push_back(*reinterpret_cast<const uint32_t *>(node + 8));
    b.push_back(Norm(*reinterpret_cast<const uint32_t *>(node + 0xc)));
    b.push_back(node[0x10]);
}
void ByKeyPayload(Blob &b, const uint8_t *node, const std::vector<uint32_t> *byIdNodes)
{
    b.push_back(*reinterpret_cast<const uint32_t *>(node + 8));
    const uint32_t mapped = *reinterpret_cast<const uint32_t *>(node + 0xc); // an entry's mapped part: its node + 0xc
    uint32_t pos = 0xffff;
    for (size_t i = 0; i < byIdNodes->size(); ++i)
        if ((*byIdNodes)[i] + 0xc == mapped) pos = uint32_t(i);
    b.push_back(pos);
}

void CaptureGroup(Blob &b, Grp *g)
{
    b.push_back(0xb10b);
    const uint32_t *w = reinterpret_cast<const uint32_t *>(g);
    b.push_back(VtCode(g, 0));
    b.push_back(VtCode(g, 1));
    b.push_back(w[2]);
    const std::vector<uint32_t> byId = CaptureTable(b, &g->mById, ByIdPayload);
    CaptureTable(b, &g->mByKey, ByKeyPayload, &byId);
    b.push_back(Norm(w[0x4c / 4]));
    b.push_back(Norm(w[0x50 / 4]));
    b.push_back(uint32_t(g->mOrdered.size()));
    uint32_t cursorPos = 0xffff, idx = 0;
    for (auto it = g->mOrdered.begin(); it != g->mOrdered.end(); ++it, ++idx) {
        b.push_back(uint32_t(it->first));
        b.push_back(Norm(uint32_t(uintptr_t(it->second.mObject))));
        if (it == g->mCursor) cursorPos = idx;
    }
    b.push_back(cursorPos);
}

void Capture(Blob &b, bool alive = true)
{
    World &w = gW;
    b.push_back(0xc0de);
    for (auto v : w.work.before) b.push_back(v);
    for (auto v : w.work.after) b.push_back(v);
    if (alive) CaptureGroup(b, G());
    for (auto &o : w.fakes) b.push_back(uint32_t(o.refs));
    b.push_back(0xe7e7);
    for (uint32_t e : w.events) b.push_back(e);
}

bool Same(difftest::Context &ctx, const Blob &a, const Blob &b, const char *what, uint32_t seed)
{
    ++ctx.cases;
    size_t at = 0;
    while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
    if (at == a.size() && at == b.size()) return true;
    ctx.Fail("%s seed %u differs at word %zu (original %x, replacement %x; sizes %zu/%zu)", what, seed, at,
        at < a.size() ? a[at] : 0xdeadbeef, at < b.size() ? b[at] : 0xdeadbeef, a.size(), b.size());
    return false;
}

// Runs `run` on the original side and on both of our routes, and compares each route with the original.
template <class Run>
void Pair(difftest::Context &ctx, const char *what, uint32_t seed, Run run, bool alive = true, int routes = 3)
{
    Blob blobs[3];
    for (int route = 0; route < routes; ++route) {
        gRoute = route;
        gTrapHits = 0;
        Build(seed);
        difftest::FreeRecorder rec;
        const uint32_t result = uint32_t(run(route == 0));
        rec.Stop();
        blobs[route].push_back(result);
        size_t sizes[64];
        const size_t count = rec.SortedSizes(sizes, 64);
        blobs[route].push_back(uint32_t(rec.Count()));
        for (size_t i = 0; i < count; ++i) blobs[route].push_back(uint32_t(sizes[i]));
        Capture(blobs[route], alive);
        if (gTrapHits) ctx.Fail("%s seed %u route %d: %u trap hits", what, seed, route, gTrapHits);
    }
    gRoute = 0;
    Same(ctx, blobs[0], blobs[1], (std::string(what) + " (vtable route)").c_str(), seed);
    if (routes > 2) Same(ctx, blobs[0], blobs[2], (std::string(what) + " (adapter route)").c_str(), seed);
}

constexpr uint32_t kSeeds = 300;
#define ORIG(Fn, rva) ctx.Original<Fn>(rva)
using Fn0 = uint32_t(__thiscall *)(void *);
using Fn1 = uint32_t(__thiscall *)(void *, uint32_t);
using Fn2 = uint32_t(__thiscall *)(void *, void *, void *);

// A key near the ones the tables hold (or one of their ids, or a wild one).
uint32_t PickKey(std::mt19937 &r) { return r() % 4 ? r() % 14 : r(); }
} // namespace

DIFF_TEST(NeighborhoodGroup_Create, 0x473c40)
{
    for (uint32_t seed = 0; seed < 8; ++seed) {
        Blob blobs[2];
        Grp *made[2];
        for (int side = 0; side < 2; ++side) {
            Build(seed);
            // Ours: the Create hook's entry (our class, our constructor).
            Grp *g = side == 0 ? ORIG(Grp *(__cdecl *)(), 0x473c40)() : Grp::Create();
            made[side] = g;
            ++ctx.cases;
            if (!g) { ctx.Fail("create returned null"); return; }
            if (HeapSize(GetProcessHeap(), 0, g) < sizeof(Grp)) ctx.Fail("create block size");
            // The shape check reads the group through the fixed buffer's address; copy the plain words by hand here. Each side's
            // own vtables: the game's in the original's object, ours in ours.
            const uint32_t *w = reinterpret_cast<const uint32_t *>(g);
            for (unsigned f = 0; f < kFaces; ++f)
                blobs[side].push_back(shape::VTableAt(g, kFaceOffset[f]) == (side == 0 ? GameVt(f) : gOurs[f]) ? 0x7d00 + f : w[f]);
            blobs[side].push_back(w[2]);
            const std::vector<uint32_t> byId = CaptureTable(blobs[side], &g->mById, ByIdPayload);
            CaptureTable(blobs[side], &g->mByKey, ByKeyPayload, &byId);
            blobs[side].push_back(w[0x4c / 4]);
            blobs[side].push_back(w[0x50 / 4]);
            blobs[side].push_back(uint32_t(g->mOrdered.size()));
            blobs[side].push_back(g->mOrdered.begin() == g->mOrdered.end());
            blobs[side].push_back(w[0x5c / 4] == 0);
            // The head nodes: self-linked, with the colour / nil word of the map's.
            const uint32_t head = w[0x54 / 4];
            const uint32_t *h = reinterpret_cast<const uint32_t *>(head);
            blobs[side].insert(blobs[side].end(), {h[0] == head, h[1] == head, h[2] == head, *reinterpret_cast<const uint16_t *>(&h[3])});
        }
        Same(ctx, blobs[0], blobs[1], "Create", seed);
        // The original's group by the adapter (as a game-built object reaches 0x4729f0 in game), ours through slot 3 of its +4
        // face; both blocks freed.
        using DeleteFn = Grp *(__thiscall *)(void *, uint32_t);
        for (int side = 0; side < 2; ++side) {
            Grp *g = made[side];
            void *face = reinterpret_cast<uint8_t *>(g) + nbgroup::kUnknownFace;
            difftest::FreeRecorder rec;
            Grp *r = side == 0 ? cTSNeighborhoodGroupDataAdapters::DeletingDestructor(g, nullptr, 1)
                               : reinterpret_cast<DeleteFn>(gOurs[1][3])(face, 1);
            rec.Stop();
            ++ctx.cases;
            if (!rec.Freed(g) || r != g) ctx.Fail("Create seed %u side %d: deleting destructor kept the block", seed, side);
        }
    }
}

DIFF_TEST(NeighborhoodGroup_DeletingDestructor, 0x4729f0)
{
    using DeleteFn = Grp *(__thiscall *)(void *, uint32_t);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Blob blobs[3];
        for (int route = 0; route < 3; ++route) {
            gRoute = route;
            gTrapHits = 0;
            Build(seed);
            const uint32_t flags = seed % 3;
            // The group is moved to a heap block so the free of the block itself can be seen: the members' pointers stay valid
            // (nodes, buffers), the tables keep their addresses only through the copy of their words (the vtable words still
            // point at the installed copies).
            Grp *h = static_cast<Grp *>(::operator new(sizeof(Grp)));
            std::memcpy(static_cast<void *>(h), G(), sizeof(Grp));
            void *face = reinterpret_cast<uint8_t *>(h) + nbgroup::kUnknownFace;
            // Odd seeds through the +4 face's slot 3 (the thunk 0x4728af), even ones at 0x4729f0 itself (no slot: the adapter on
            // the adapter route, our deleting destructor through the +4 slot on the vtable route).
            const bool viaThunk = seed % 2;
            gCopy[1][3] = TrapFor(1);
            difftest::FreeRecorder rec;
            Grp *r;
            if (route == 0) r = ORIG(DeleteFn, viaThunk ? 0x4728af : 0x4729f0)(viaThunk ? face : static_cast<void *>(h), flags);
            else if (route == 1) r = reinterpret_cast<DeleteFn>(gOurs[1][3])(face, flags);
            else if (viaThunk) r = cTSNeighborhoodGroupDataAdapters::UnknownDeletingDestructor(static_cast<rz::cRZUnknown *>(h), nullptr, flags);
            else r = cTSNeighborhoodGroupDataAdapters::DeletingDestructor(h, nullptr, flags);
            rec.Stop();
            const bool freed = rec.Freed(h);
            Blob &b = blobs[route];
            b.push_back(r == h);
            b.push_back(freed);
            b.push_back(uint32_t(rec.Count()));
            size_t sizes[64];
            const size_t count = rec.SortedSizes(sizes, 64);
            for (size_t i = 0; i < count; ++i) b.push_back(uint32_t(sizes[i]));
            for (auto &o : gW.fakes) b.push_back(uint32_t(o.refs));
            if (!freed) { // a freed block holds heap bookkeeping
                const uint32_t *w = reinterpret_cast<const uint32_t *>(h);
                b.insert(b.end(), {VtCode(h, 0), VtCode(h, 1), w[2]});
            }
            for (uint32_t e : gW.events) b.push_back(e);
            if (!freed) ::operator delete(h, sizeof(Grp));
            if (gTrapHits) ctx.Fail("DeletingDestructor seed %u route %d: trap hit", seed, route);
        }
        gRoute = 0;
        Same(ctx, blobs[0], blobs[1], "DeletingDestructor (vtable route)", seed);
        Same(ctx, blobs[0], blobs[2], "DeletingDestructor (adapter route)", seed);
    }
}

DIFF_TEST(NeighborhoodGroup_QueryInterface, 0x4772e0)
{
    const uint32_t ids[] = {0, 1, 2, 0xf05f6b5a, 0xf05f6b5b, 0xf05f6b59, 0xffffffff};
    for (unsigned t = 0; t < 2; ++t)
        for (uint32_t id : ids)
            Pair(ctx, "QueryInterface", id * 2 + t, [&](bool original) {
                void *out = reinterpret_cast<void *>(0x1234);
                using QIFn = uint8_t(__thiscall *)(void *, uint32_t, void **);
                const uint8_t r = t == 0 ? Call<QIFn>(ctx, original, 0x4772e0, 0, ADAPTER(QueryInterface), id, &out)
                                         : Call<QIFn>(ctx, original, 0x477307, 1, ADAPTER(UnknownQueryInterface), id, &out);
                gW.events.push_back(P(out));
                return r;
            });
}

DIFF_TEST(NeighborhoodGroup_Add, 0x476080)
{
    Globals globals(ctx);
    CodePatch patch(ctx, 0x476a40, reinterpret_cast<void *>(&FakeRefresh));
    // Ours calls our LoadNeighborhoods (the replacement of 0x476a40): the same stand-in there.
    CodePatch patchOurs(OurLoadNeighborhoods(), reinterpret_cast<void *>(&FakeRefresh));
    for (uint32_t seed = 0; seed < 2 * kSeeds; ++seed)
        Pair(ctx, "Add", seed, [&](bool original) {
            std::mt19937 r(seed + 5);
            // The neighbourhood is never null here: the original calls through it unchecked (a null one crashes).
            void *neighborhood = static_cast<void *>(&S(kNb0 + r() % kNbCount));
            if (r() % 4 == 0 && G()->mCurrent) neighborhood = G()->mCurrent;
            using AddFn = uint32_t(__thiscall *)(void *, void *, void *);
            return uint8_t(Call<AddFn>(ctx, original, 0x476080, 0, ADAPTER(Add), static_cast<void *>(&S(kSource)), neighborhood));
        });
}

DIFF_TEST(NeighborhoodGroup_Refresh, 0x476340)
{
    Globals globals(ctx);
    CodePatch patch(ctx, 0x476a40, reinterpret_cast<void *>(&FakeRefresh));
    // Ours calls our LoadNeighborhoods (the replacement of 0x476a40): the same stand-in there.
    CodePatch patchOurs(OurLoadNeighborhoods(), reinterpret_cast<void *>(&FakeRefresh));
    for (uint32_t seed = 0; seed < kSeeds; ++seed)
        Pair(ctx, "Refresh", seed, [&](bool original) {
            return uint8_t(Call<Fn1>(ctx, original, 0x476340, 0, ADAPTER(Refresh), uint32_t(uintptr_t(&S(kSource)))));
        });
}

DIFF_TEST(NeighborhoodGroup_SaveAll, 0x47a4b0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < 2 * kSeeds; ++seed)
        Pair(ctx, "SaveAll", seed, [&](bool original) { return uint8_t(Call<Fn0>(ctx, original, 0x47a4b0, 0, ADAPTER(SaveAll))); });
}

DIFF_TEST(NeighborhoodGroup_Flush, 0x479590)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < 2 * kSeeds; ++seed)
        Pair(ctx, "Flush", seed, [&](bool original) { return uint8_t(Call<Fn0>(ctx, original, 0x479590, 0, ADAPTER(Flush))); });
}

DIFF_TEST(NeighborhoodGroup_Rebuild, 0x47aae0)
{
    // A member whose id equals the skipped one is never stepped past (an infinite loop, ORIGINAL-BUG in src): the member ids
    // are 0..9, the skipped ones never in that range.
    for (uint32_t seed = 0; seed < 2 * kSeeds; ++seed)
        Pair(ctx, "Rebuild", seed, [&](bool original) {
            std::mt19937 r(seed + 37);
            const int32_t skip = r() % 2 ? 50 : -3;
            // The map is emptied under the cursor, which then dangles (its position would depend on node reuse): park it.
            G()->mCursor = G()->mOrdered.end();
            Call<Fn1>(ctx, original, 0x47aae0, 0, ADAPTER(Rebuild), uint32_t(skip));
            return 0;
        });
}

DIFF_TEST(NeighborhoodGroup_TrackCurrent, 0x47a8a0)
{
    for (uint32_t seed = 0; seed < 2 * kSeeds; ++seed) {
        Pair(ctx, "TrackCurrent", seed, [&](bool original) {
            std::mt19937 r(seed + 41);
            for (unsigned i = 0; i < kNbCount; ++i) S(kNb0 + i).value[7] = 1; // slot 57 gives member objects
            const int32_t id = r() % 4 ? int32_t(r() % 12) - 1 : int32_t(r());
            Call<Fn1>(ctx, original, 0x47a8a0, 0, ADAPTER(TrackCurrent), uint32_t(id));
            return 0;
        });
    }
}

DIFF_TEST(NeighborhoodGroup_Reset, 0x47a620)
{
    for (uint32_t seed = 0; seed < 8; ++seed)
        Pair(ctx, "Reset", seed, [&](bool original) {
            Call<Fn0>(ctx, original, 0x47a620, 0, ADAPTER(Reset));
            return 0;
        });
}

DIFF_TEST(NeighborhoodGroup_Remove, 0x47adb0)
{
    for (uint32_t seed = 0; seed < 2 * kSeeds; ++seed)
        Pair(ctx, "Remove", seed, [&](bool original) {
            std::mt19937 r(seed + 7);
            uint32_t id = PickKey(r);
            if (!G()->mById.empty() && r() % 2) {
                auto it = G()->mById.begin();
                std::advance(it, r() % G()->mById.size());
                id = it->first; // an id present (the table is walked before the run; no frees yet)
            }
            Call<Fn1>(ctx, original, 0x47adb0, 0, ADAPTER(Remove), id);
            return 0;
        });
}

DIFF_TEST(NeighborhoodGroup_MarkDirtyById, 0x476db0)
{
    for (uint32_t seed = 0; seed < 2 * kSeeds; ++seed)
        Pair(ctx, "MarkDirtyById", seed, [&](bool original) {
            std::mt19937 r(seed + 11);
            uint32_t id = PickKey(r);
            if (r() % 2) id = S(kKey0 + r() % kNbCount).value[0];
            Call<Fn1>(ctx, original, 0x476db0, 0, ADAPTER(MarkDirtyById), id);
            return 0;
        });
}

DIFF_TEST(NeighborhoodGroup_ByKey, 0x476df0)
{
    for (uint32_t seed = 0; seed < 4 * kSeeds; ++seed) {
        auto pick = [&](std::mt19937 &r) {
            uint32_t key = PickKey(r);
            if (!G()->mByKey.empty() && r() % 2) {
                auto it = G()->mByKey.begin();
                std::advance(it, r() % G()->mByKey.size());
                key = it->first;
            }
            return key;
        };
        Pair(ctx, "MarkDirtyByKey", seed, [&](bool original) {
            std::mt19937 r(seed + 13);
            const uint32_t key = pick(r);
            Call<Fn1>(ctx, original, 0x476df0, 0, ADAPTER(MarkDirtyByKey), key);
            return 0;
        });
        Pair(ctx, "GetByKey", seed, [&](bool original) {
            std::mt19937 r(seed + 17);
            const uint32_t key = pick(r);
            return Call<Fn1>(ctx, original, 0x4753c0, 0, ADAPTER(GetByKey), key);
        });
        Pair(ctx, "MarkAndQuery", seed, [&](bool original) {
            std::mt19937 r(seed + 19);
            const uint32_t key = pick(r);
            return Call<Fn1>(ctx, original, 0x474790, 0, ADAPTER(MarkAndQuery), key);
        });
        Pair(ctx, "QueryByKey", seed, [&](bool original) {
            std::mt19937 r(seed + 23);
            const uint32_t key = pick(r);
            return Call<Fn1>(ctx, original, 0x475050, 0, ADAPTER(QueryByKey), key);
        });
    }
}

DIFF_TEST(NeighborhoodGroup_GetById, 0x475340)
{
    for (uint32_t seed = 0; seed < 2 * kSeeds; ++seed)
        Pair(ctx, "GetById", seed, [&](bool original) {
            std::mt19937 r(seed + 29);
            uint32_t id = PickKey(r);
            if (!G()->mById.empty() && r() % 2) {
                auto it = G()->mById.begin();
                std::advance(it, r() % G()->mById.size());
                id = it->first;
            }
            return Call<Fn1>(ctx, original, 0x475340, 0, ADAPTER(GetById), id);
        });
}

DIFF_TEST(NeighborhoodGroup_Iteration, 0x473b70)
{
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Pair(ctx, "BeginIteration", seed, [&](bool original) {
            return Call<Fn0>(ctx, original, 0x473b70, 0, ADAPTER(BeginIteration));
        });
        Pair(ctx, "Next", seed, [&](bool original) {
            uint32_t acc = 0;
            for (int i = 0; i < 8; ++i) acc = acc * 31 + Call<Fn0>(ctx, original, 0x476f70, 0, ADAPTER(Next));
            return acc;
        });
        Pair(ctx, "NextKey", seed, [&](bool original) {
            std::mt19937 r(seed + 31);
            uint32_t acc = 0;
            for (int i = 0; i < 6; ++i) {
                uint32_t key = r() % 3 ? uint32_t(int(r() % 10) - 2) : r();
                if (r() % 4 == 0) key = 0;
                if (!G()->mOrdered.empty() && r() % 2) {
                    auto it = G()->mOrdered.begin();
                    std::advance(it, r() % G()->mOrdered.size());
                    key = uint32_t(it->first);
                }
                acc = acc * 31 + Call<Fn1>(ctx, original, 0x475460, 0, ADAPTER(NextKey), key);
            }
            return acc;
        });
    }
}

// ===== LoadNeighborhoods 0x476a40 =====
//
// The world above, plus: the globals (game global 0x10cc288) whose slot 8 gives the neighbourhood manager (or null); the
// manager's slot 10 (GetByName) answers one of the infos and slot 12 (GetNamedList) fills the service-allocator vector with
// a scripted list of infos (counted by PushBack, repeats allowed); every info's slot 8 is the prefix string (slot 5 its text)
// and slot 24 its persist face, whose slot 6 is the group. The persist system's slot 5 (GetResource) logs the key and, unless
// the group's bit in a failure mask is set, hands out the neighbourhood fake of that group counted; on failure it sometimes
// writes the pointer anyway (the holder releases it). Neighbourhood slot 6 (Initialize) answers a scripted byte with junk
// above it; slots 12 and 14 log. The allocator service cache (0x10c95d4) holds the game's service so both sides' vectors
// allocate and free alike.
namespace {
const char gPrefixText[] = "TEST";
struct LoadScript {
    uint32_t failMask;
    bool writeOnFail;
} gLoad;

uint32_t LoadGetResource(SObj *, uint32_t *args)
{
    const uint32_t *key = reinterpret_cast<const uint32_t *>(uintptr_t(args[0]));
    Ev({evKey, key[0], key[1], key[2], key[3]});
    const bool ok = !((gLoad.failMask >> (key[1] % 16)) & 1);
    if (ok || gLoad.writeOnFail) {
        SObj &hood = S(kNb0 + key[1] % kNbCount);
        ++hood.refs;
        *reinterpret_cast<void **>(uintptr_t(args[2])) = &hood;
    }
    return Junk8(ok);
}
uint32_t LoadNamedList(SObj *self, uint32_t *args)
{
    auto *out = reinterpret_cast<ServiceRefVector *>(uintptr_t(args[1]));
    for (unsigned i = 0; i < self->value[0]; ++i) out->PushBack(&S(kInfo0 + self->value[1 + i]));
    return Junk8(self->value[0] != 0);
}

// The game's global allocator (the image's object at 0x10cb098, unconstructed in the harness) built for the length of a test, so
// the original's RZMemory::Malloc / Free and ours (the lists' storage) work on the same blocks.
struct GameAllocatorFixture {
    GeneralAllocator *allocator;
    bool built = false;
    GameAllocatorFixture() : allocator(GameAddress<GeneralAllocator>(0x10cb098))
    {
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(allocator);
        bool zero = true;
        for (size_t i = 0; i < sizeof(GeneralAllocator); ++i) zero = zero && bytes[i] == 0;
        if (zero) {
            allocator->Construct(nullptr, 0, 0, 0, nullptr, nullptr);
            built = true;
        }
    }
    ~GameAllocatorFixture()
    {
        if (built) {
            difftest_alloc::ShutdownAndRelease(allocator);
        }
    }
};

struct AllocatorServiceScope {
    void **cache;
    int *guard;
    void *savedCache;
    int savedGuard;
    alignas(8) uint8_t object[16];
    explicit AllocatorServiceScope(difftest::Context &ctx) : cache(ctx.image.At<void *>(0x10c95d4)), guard(ctx.image.At<int>(0x10c95d8))
    {
        savedCache = *cache;
        savedGuard = *guard;
        std::memset(object, 0, sizeof(object));
        const void *vtable = ctx.image.At<void>(0xda3cd4);
        std::memcpy(object, &vtable, sizeof(vtable));
        *cache = object;
        *guard = INT32_MIN;
    }
    AllocatorServiceScope(const AllocatorServiceScope &) = delete;
    ~AllocatorServiceScope()
    {
        *cache = savedCache;
        *guard = savedGuard;
    }
};

struct GlobalsSingletonScope {
    uint32_t *slot;
    uint32_t saved;
    explicit GlobalsSingletonScope(difftest::Context &ctx) : slot(ctx.image.At<uint32_t>(0x10cc288)), saved(*slot)
    {
        *slot = uint32_t(uintptr_t(&S(kGlobals)));
    }
    GlobalsSingletonScope(const GlobalsSingletonScope &) = delete;
    ~GlobalsSingletonScope() { *slot = saved; }
};

// Sets up the load's fakes for a seed (after Build) and picks the source.
void *SetupLoad(uint32_t seed)
{
    std::mt19937 r(seed * 7 + 3);
    if (r() % 12 == 0) Set(S(kGlobals), 8, 0, nullptr);
    else Set(S(kGlobals), 8, 0, &S(kManager));
    Set(S(kManager), 10, 1, &S(kInfo0 + r() % 2));
    Set(S(kManager), 12, 2, 0, LoadNamedList);
    SObj &manager = S(kManager);
    manager.value[0] = r() % 5 == 0 ? 0 : r() % 5 == 0 ? 1 : r() % 7;
    for (unsigned i = 1; i < 8; ++i) manager.value[i] = r() % kInfoCount;
    Set(S(kPrefix), 5, 0, gPrefixText);
    Set(S(kSource), 8, 0, &S(kPrefix));
    for (unsigned i = 0; i < kInfoCount; ++i) {
        Set(S(kInfo0 + i), 8, 0, &S(kPrefix));
        Set(S(kInfo0 + i), 24, 0, &S(kFace0 + i));
        Set(S(kFace0 + i), 6, 0, 0, IdAnswer);
        S(kFace0 + i).value[0] = r() % 4 ? r() % 8 : r();
    }
    Set(S(kPersist), 5, 5, 0, LoadGetResource);
    switch (r() % 4) {
    case 0: gLoad.failMask = 0; break;
    case 1: gLoad.failMask = 0xffffffff; break; // nothing loads (the main one is missing)
    default: gLoad.failMask = r() & r(); break;
    }
    gLoad.writeOnFail = r() % 4 == 0;
    // Initialize answers 0 / 1 mostly, and now and then a byte with other bits (the main answer is masked to bit 0, the
    // children's are and-ed in whole).
    for (unsigned i = 0; i < kNbCount; ++i) {
        Set(S(kNb0 + i), 6, 1, 0, AnswerValue0);
        S(kNb0 + i).value[0] = r() % 3 ? r() % 2 : r() & 0xff;
    }
    switch (r() % 4) {
    case 0: return &S(kSource);
    case 1: return &S(kInfo0);
    case 2: return &S(kInfo0 + 1);
    default: return &S(kInfo0 + r() % kInfoCount);
    }
}
} // namespace

DIFF_TEST(NeighborhoodGroup_LoadNeighborhoods, 0x476a40)
{
    Globals globals(ctx);
    GlobalsSingletonScope singleton(ctx);
    GameAllocatorFixture allocator;
    AllocatorServiceScope service(ctx);
    for (uint32_t seed = 0; seed < 2 * kSeeds; ++seed)
        Pair(ctx, "LoadNeighborhoods", seed, [&](bool original) {
            void *source = SetupLoad(seed);
            // The second table (+0x2c) is not the load's: 0x476a40 touches only +0xc (clear 0x47b570, try_emplace 0x471980), +0x4c and
            // +0x50. Its mapped pointers would dangle into the entries the clear frees, and where they then point depends on heap
            // reuse, so it starts empty here (and must stay so).
            G()->mByKey.clear();
            return uint8_t(Call<Fn1>(ctx, original, 0x476a40, 0, ADAPTER(LoadNeighborhoods), uint32_t(uintptr_t(source))));
        });
}

// ===== the slots no other test calls, through each side's own vtable =====
//
// The primary face's AddRef / Release (the game's folded 0x1dab20 / 0x1e3970, jumps into cRZUnknown's), the folded getters of
// slots 7 / 8 (0x169f60 / 0x1e0950), and the +4 face's AddRef / Release thunks (0x1dab28 / 0x1e3978, folded) and cRZUnknown's
// own slots 4 / 5 (0x1b9538 RemoveRef, 0x147a30 the count). The original side calls the game's vtable slot on a game-shaped group,
// ours our vtable's slot on our group; Release's deletion goes through slot 3 of the +4 copy, a recording fake on both sides.
namespace {
Grp *__fastcall GroupDelete(void *face, void *, uint32_t flags)
{
    Ev({evGroup, 3, P(face), flags});
    return nullptr;
}
} // namespace

DIFF_TEST(NeighborhoodGroup_Slots, 0x169f60)
{
    struct Case {
        unsigned face, slot;
    };
    const Case cases[] = {{0, 1}, {0, 2}, {0, 7}, {0, 8}, {1, 1}, {1, 2}, {1, 4}, {1, 5}};
    for (const Case &c : cases)
        for (uint32_t seed = 0; seed < 60; ++seed)
            Pair(
                ctx, "Slots", seed * 64 + c.face * 32 + c.slot,
                [&](bool original) {
                    gCopy[1][3] = reinterpret_cast<void *>(&GroupDelete);
                    void *const fn = original ? GameVt(c.face)[c.slot] : gOurs[c.face][c.slot];
                    return Norm(reinterpret_cast<uint32_t(__thiscall *)(void *)>(fn)(Face(c.face)));
                },
                true, 2);
}
