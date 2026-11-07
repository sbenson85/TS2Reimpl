// Differential tests for the cTSPersistSystem functions that walk the factory table (field map in
// src/ts/cTSPersistSystem.h): vf34, vf15 (primary), vf168_48, vf168_30, vf168_34, vf168_35, vf168_36, vf168_14,
// vf168_27 and vf168_18 (cITSPersistSystem).
//
// Each run gets a world built from a seed: a random primary object (0x300 bytes) whose lock at +0x68 is an embedded
// lock object with recording slots, the factory table at +0x114 (an identity-hashed StdHashTable of 0..6 nodes, each
// with a vector of factory pointers), the id table at +0x134 (FNV hashed, nodes pointing at dword ranges), and
// twelve fake objects (factories 0..7, a local object 10 and 11) whose slots record their calls. The original runs on
// one world and ours on another built from the same seed; the recorded calls (kind, object, arguments), the return
// value (as the caller reads it) and every word of the world afterwards must match, with addresses inside the world
// normalised to offsets.
//
// The fill-in function 0x1e95c0 (gathers and sorts the factories for an absent key) and the id fill-in 0x1e97b0 are
// replaced by stand-ins in the image for these tests; both sides call them (ours through OriginalFunction). The stand-in
// for 0x1e95c0 adds a node to the table when the world says so, so the second lookup of vf34 and vf15 finds it. The
// vector insert 0x1d6520 is library code: the image's runs on one side and the toolchain's on ours, counting the fakes.
// Its growth meets the game's heap in tests/ts_want_simulator_gf.cpp (the same helper, rz/CountedRefVector.h).
#include "difftest.h"
#include "gz/cGZPersistResourceManager.h"
#include "rz/cRZFileName.h"
#include "rz/cRZString.h"
#include "std/StdHash.h"
#include "ts/cTSPersistSystem.h"
#include "ts_persist_system_shape.h"
#include "shape.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <unordered_map>
#include <utility>
#include <vector>

#include <windows.h>

namespace {

constexpr int kObjects = 12;
constexpr int kSlots = 80;
constexpr int kNodes = 6;
constexpr int kItemCap = 6;
constexpr uint32_t kSubOffset = 0xa8;
constexpr uint32_t kUseA3 = 0x1c0532fa;

struct Node {
    StdHashNode link; // next, prev
    uint32_t key;     // +8
    void *value[3];   // +0xc: the factory vector (first, last, end), or the id range pointer in value[0]
};
static_assert(sizeof(Node) == 0x18, "table node");

struct PtrVec {
    void **first;
    void **last;
    void **end;
};
struct ByteVec {
    uint8_t *first;
    uint8_t *last;
    uint8_t *end;
};
struct DwordRange {
    uint32_t *first;
    uint32_t *last;
};

struct Table {
    Node nodes[kNodes + 1]; // [0] is the sentinel
    StdHashBucket buckets[8];
    int count;
    uint32_t pad;
};

struct World {
    alignas(8) uint8_t primary[0x300];
    void *vtLock[4];
    void *vtP[kSlots];
    void *vtI[kSlots];
    alignas(8) uint8_t obj[kObjects][0x40];
    void *vt[kObjects][kSlots];
    Table t1; // factory table
    Table t2; // id table
    void *items[kNodes + 2][kItemCap];
    uint32_t ids[kNodes + 2][5];
    DwordRange ranges[kNodes + 2];
    PtrVec empty;
    Node late1;       // the node the 0x1e95c0 stand-in adds
    DwordRange lateRange;
    uint32_t populate; // whether the stand-ins add anything
    void *outStore[12];
    PtrVec outVec;
    alignas(8) uint8_t out16Store[16 * 8];
    ByteVec out16;
    uint32_t keyBlock[4];
    uint32_t args[4];
    char text[64];
    uint32_t retObj[kObjects][kSlots];
    uint32_t retSlot[kSlots];
    uint32_t seq[8];
    void *segs[8];      // the segment vector's storage (+0xd4)
    void *copyStore[8]; // the copy 0x1d89b0's stand-in makes
    void *listeners[6]; // the listener vector's storage (+0x230)
};

World *gWorld = nullptr;
std::vector<uint32_t> gTrace;
bool gWriteOnFail = false;
bool gSkipFilter10 = false;
int gSeqPos = 0;

uint32_t Map(uint32_t v)
{
    if (gWorld) {
        uint32_t base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gWorld));
        if (v >= base && v < base + sizeof(World)) return 0xa5000000u | (v - base);
    }
    return v;
}

uint32_t Map(const void *p)
{
    return Map(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)));
}

void Note(int kind, const void *self)
{
    gTrace.push_back(static_cast<uint32_t>(kind));
    gTrace.push_back(Map(self));
}

void Rec(uint32_t v)
{
    gTrace.push_back(Map(v));
}

void RecPtr(const void *p)
{
    gTrace.push_back(Map(p));
}

template <typename T> void Put(void *base, size_t off, T v)
{
    std::memcpy(static_cast<uint8_t *>(base) + off, &v, sizeof(v));
}

uint32_t Addr(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

template <typename F> void *Fn(F f)
{
    return reinterpret_cast<void *>(f);
}

int ObjIndex(const void *self)
{
    if (!gWorld) return -1;
    auto d = reinterpret_cast<const uint8_t *>(self) - reinterpret_cast<const uint8_t *>(gWorld->obj);
    if (d < 0 || d >= static_cast<ptrdiff_t>(sizeof(gWorld->obj))) return -1;
    return static_cast<int>(d / 0x40);
}

// What a slot returns: per fake object when self is one, else per slot.
uint32_t RetOf(const void *self, int slot)
{
    int i = ObjIndex(self);
    return i >= 0 ? gWorld->retObj[i][slot] : gWorld->retSlot[slot];
}

// Recording slots (stack arguments popped by the callee, as the game's __thiscall callees do).
template <int Slot> uint32_t __fastcall R0(void *self, void *)
{
    Note(Slot, self);
    return RetOf(self, Slot);
}
template <int Slot> uint32_t __fastcall R1(void *self, void *, uint32_t a)
{
    Note(Slot, self);
    Rec(a);
    return RetOf(self, Slot);
}
template <int Slot> uint32_t __fastcall R2(void *self, void *, uint32_t a, uint32_t b)
{
    Note(Slot, self);
    Rec(a);
    Rec(b);
    return RetOf(self, Slot);
}

void __fastcall LockEnter(void *self, void *)
{
    Note(2301, self);
}
void __fastcall LockLeave(void *self, void *)
{
    Note(2302, self);
}

// Slot 0 (query): records the id, hands out object 10 when it works (low byte of the return value), and object 11
// when it fails but this world writes failures.
uint32_t __fastcall Query(void *self, void *, uint32_t iid, void **out)
{
    Note(0, self);
    Rec(iid);
    uint32_t r = RetOf(self, 0);
    if (r & 0xff) *out = gWorld->obj[10];
    else if (gWriteOnFail) *out = gWorld->obj[11];
    return r;
}

// Slot 20 (accept a key): records the 16-byte key it is given.
uint32_t __fastcall AcceptKey(void *self, void *, const uint32_t *key)
{
    Note(20, self);
    for (int i = 0; i < 4; ++i) Rec(key[i]);
    return RetOf(self, 20);
}

// Slot 25 of a local object: the next value of the world's sequence.
uint32_t __fastcall SeqValue(void *self, void *)
{
    Note(25, self);
    return gWorld->seq[gSeqPos++ & 7];
}

// Slot 19 (fill `out` with the filter): records the filter's fields by value, and appends 0..2 elements to out.
uint32_t __fastcall FillOut(void *self, void *, ByteVec *out, const uint8_t *filter)
{
    Note(19, self);
    RecPtr(out);
    uint32_t w[7];
    std::memcpy(w, filter, sizeof(w));
    Rec(w[0]); // the game's vtable
    Rec(w[1]); // reference count
    const auto *head = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(w[2]));
    Rec(head[0] == w[2] && head[1] == w[2] && head[2] == w[2]);
    Rec(reinterpret_cast<const uint8_t *>(head)[0xc]);
    Rec(reinterpret_cast<const uint8_t *>(head)[0xd]);
    Rec(w[3]);
    if (!gSkipFilter10) Rec(w[4]); // +0x10 is not written by the constructor: only checked where the function sets it
    Rec(w[5]);
    Rec(w[6] & 0xffff);
    uint32_t n = RetOf(self, 19) % 3;
    if (out->last + 16 * n <= out->end) out->last += 16 * n;
    return 0;
}

// ITS slot 32 (score of a factory for a key): records the key, returns a small score per factory.
uint32_t __fastcall Score(void *self, void *, void *factory, const uint32_t *key)
{
    Note(32, self);
    RecPtr(factory);
    for (int i = 0; i < 4; ++i) Rec(key[i]);
    int i = ObjIndex(factory);
    return i >= 0 ? gWorld->retObj[i][32] % 4 : 0;
}

// Stand-ins for the unreplaced callees.
PtrVec *__fastcall FillInStandIn(void *primary, void *, uint32_t key)
{
    World &w = *gWorld;
    Note(3001, primary);
    Rec(key);
    if (!w.populate) return &w.empty;
    Node *n = &w.late1;
    n->key = key;
    Node *head = &w.t1.nodes[0];
    uint32_t mask = 0;
    std::memcpy(&mask, w.primary + 0x114 + 0x18, 4);
    StdHashBucket *buckets = nullptr;
    std::memcpy(&buckets, w.primary + 0x114 + 0xc, 4);
    StdHashBucket &b = buckets[key & mask];
    if (b.first == &head->link) {
        StdHashNode *prev = head->link.prev;
        n->link.next = &head->link;
        n->link.prev = prev;
        prev->next = &n->link;
        head->link.prev = &n->link;
        b.first = b.last = &n->link;
    } else {
        StdHashNode *last = b.last;
        n->link.next = last->next;
        n->link.prev = last;
        last->next->prev = &n->link;
        last->next = &n->link;
        b.last = &n->link;
    }
    return reinterpret_cast<PtrVec *>(&n->value);
}

DwordRange *__fastcall IdFillStandIn(void *primary, void *, void *obj)
{
    Note(3003, primary);
    RecPtr(obj);
    return gWorld->populate ? &gWorld->lateRange : nullptr;
}

struct CodePatch {
    uint8_t *site;
    uint8_t saved[5];
    CodePatch(void *at, void *stand) : site(static_cast<uint8_t *>(at))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stand) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    CodePatch(const CodePatch &) = delete;
    CodePatch &operator=(const CodePatch &) = delete;
    ~CodePatch() { std::memcpy(site, saved, 5); }
};

uint32_t Fnv(uint32_t key)
{
    uint32_t h = 0x811c9dc5u;
    for (int i = 0; i < 4; ++i) {
        h ^= (key >> (8 * i)) & 0xffu;
        h *= 0x01000193u;
    }
    return h;
}

// Builds a table of StdHashTable's layout in the primary at `tableOffset`: nodes (0..kNodes, random distinct keys)
// linked bucket by bucket, empty buckets with last == head. `fnv` selects the hash.
void BuildTable(World &w, Table &t, uint32_t tableOffset, bool fnv, std::mt19937 &rng)
{
    const uint32_t mask = (1u << (rng() % 4)) - 1;
    t.count = static_cast<int>(rng() % (kNodes + 1));
    Node *head = &t.nodes[0];
    head->link.next = head->link.prev = &head->link;
    for (int i = 1; i <= t.count; ++i) {
        uint32_t key;
        bool dup;
        do {
            key = rng() % 5 ? rng() : rng() % 32;
            dup = false;
            for (int j = 1; j < i; ++j) dup |= t.nodes[j].key == key;
        } while (dup || key == kUseA3);
        t.nodes[i].key = key;
    }
    StdHashNode *prev = &head->link;
    for (uint32_t b = 0; b <= mask; ++b) {
        StdHashNode *first = nullptr, *last = nullptr;
        for (int i = 1; i <= t.count; ++i) {
            uint32_t h = fnv ? Fnv(t.nodes[i].key) : t.nodes[i].key;
            if ((h & mask) != b) continue;
            StdHashNode *n = &t.nodes[i].link;
            prev->next = n;
            n->prev = prev;
            prev = n;
            if (!first) first = n;
            last = n;
        }
        t.buckets[b] = first ? StdHashBucket{first, last} : StdHashBucket{&head->link, &head->link};
    }
    prev->next = &head->link;
    head->link.prev = prev;
    Put(w.primary, tableOffset + 4, Addr(head));
    Put(w.primary, tableOffset + 0xc, Addr(t.buckets));
    Put(w.primary, tableOffset + 0x18, mask);
    (void)w;
}

constexpr uint32_t kTable1 = 0x114;
constexpr uint32_t kTable2 = 0x134;

const void *kTraps[kSlots];

template <int N> void __fastcall Trap(void *self, void *)
{
    std::printf("unexpected call: slot %d of object 0x%x\n", N, Map(self));
    std::fflush(stdout);
    ExitProcess(3);
}

template <size_t... I> void FillTraps(std::index_sequence<I...>)
{
    ((kTraps[I] = reinterpret_cast<const void *>(&Trap<static_cast<int>(I)>)), ...);
}

void Build(World &w, uint32_t seed)
{
    std::memset(&w, 0, sizeof(w));
    static bool trapsReady = false;
    if (!trapsReady) {
        FillTraps(std::make_index_sequence<kSlots>());
        trapsReady = true;
    }
    std::mt19937 rng(seed);
    auto fill = [&rng](void *p, size_t n) {
        auto *b = static_cast<uint8_t *>(p);
        for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(rng());
    };
    fill(w.primary, sizeof(w.primary));
    fill(w.obj, sizeof(w.obj));
    for (int i = 0; i < kSlots; ++i) {
        w.vtP[i] = w.vtI[i] = const_cast<void *>(kTraps[i]);
        for (auto &v : w.vt) v[i] = const_cast<void *>(kTraps[i]);
    }
    for (int o = 0; o < kObjects; ++o) {
        Put(w.obj[o], 0, Addr(w.vt[o]));
        for (int s = 0; s < kSlots; ++s) w.retObj[o][s] = rng() % 2 ? (static_cast<uint32_t>(rng()) | 1u) : (static_cast<uint32_t>(rng()) & ~0xffu);
    }
    for (int s = 0; s < kSlots; ++s) w.retSlot[s] = rng() % 2 ? (static_cast<uint32_t>(rng()) | 1u) : (static_cast<uint32_t>(rng()) & ~0xffu);
    gWriteOnFail = rng() % 4 == 0;
    gSkipFilter10 = false;
    gSeqPos = 0;
    // The lock at +0x68 (the primary's own lock object): slots 1 and 2.
    w.vtLock[1] = Fn(&LockEnter);
    w.vtLock[2] = Fn(&LockLeave);
    Put(w.primary, 0x68, Addr(w.vtLock));
    Put(w.primary, 0, Addr(w.vtP));
    Put(w.primary, 0xa8, Addr(w.vtI));

    // The factory table: node i holds items[i-1], 0..kItemCap factories (objects 0..7).
    BuildTable(w, w.t1, kTable1, false, rng);
    for (int i = 1; i <= w.t1.count; ++i) {
        int n = static_cast<int>(rng() % (kItemCap + 1));
        for (int k = 0; k < kItemCap; ++k) w.items[i - 1][k] = w.obj[rng() % 8];
        w.t1.nodes[i].value[0] = w.items[i - 1];
        w.t1.nodes[i].value[1] = w.items[i - 1] + n;
        w.t1.nodes[i].value[2] = w.items[i - 1] + kItemCap;
    }
    // The node the stand-in adds: 0..3 factories.
    for (int k = 0; k < kItemCap; ++k) w.items[kNodes][k] = w.obj[rng() % 8];
    int lateCount = static_cast<int>(rng() % 4);
    w.late1.value[0] = w.items[kNodes];
    w.late1.value[1] = w.items[kNodes] + lateCount;
    w.late1.value[2] = w.items[kNodes] + kItemCap;
    // The id table: node i holds a range of 0..5 ids.
    BuildTable(w, w.t2, kTable2, true, rng);
    for (int i = 1; i <= w.t2.count; ++i) {
        int n = static_cast<int>(rng() % 6);
        for (int k = 0; k < 5; ++k) w.ids[i - 1][k] = rng() % 3 ? static_cast<uint32_t>(rng()) : rng() % 8;
        w.ranges[i - 1] = {w.ids[i - 1], w.ids[i - 1] + (n > 5 ? 5 : n)};
        w.t2.nodes[i].value[0] = &w.ranges[i - 1];
    }
    for (int k = 0; k < 5; ++k) w.ids[kNodes][k] = rng() % 3 ? static_cast<uint32_t>(rng()) : rng() % 8;
    w.lateRange = {w.ids[kNodes], w.ids[kNodes] + rng() % 6};
    w.populate = rng() % 2;
    // The out vectors.
    int used = static_cast<int>(rng() % 4), cap = used + static_cast<int>(rng() % 5);
    for (auto &p : w.outStore) p = w.obj[rng() % 8];
    w.outVec = {w.outStore, w.outStore + used, w.outStore + cap};
    int used16 = static_cast<int>(rng() % 3), cap16 = used16 + static_cast<int>(rng() % 6);
    fill(w.out16Store, sizeof(w.out16Store));
    w.out16 = {w.out16Store, w.out16Store + 16 * used16, w.out16Store + 16 * cap16};
    for (auto &k : w.keyBlock) k = rng();
    for (auto &a : w.args) a = rng();
    for (auto &s : w.seq) s = (rng() % 2 ? 0x10000u : 0u) | (rng() % 3 == 0 ? 0xffffu : (rng() % 3));
}

void Snapshot(World &w, std::vector<uint32_t> &t)
{
    const auto *b = reinterpret_cast<const uint8_t *>(&w);
    for (size_t i = 0; i + 4 <= sizeof(World); i += 4) {
        uint32_t v;
        std::memcpy(&v, b + i, 4);
        t.push_back(Map(v));
    }
}

struct Outcome {
    uint32_t ret = 0;
    std::vector<uint32_t> trace;
    std::vector<uint32_t> after;
};

using Customise = void (*)(World &, std::mt19937 &);

// Where the world lives. Original and ours run on this one world, rebuilt from the seed before each (Compare), so
// they see it at the same address and Map (which renames words inside the world) is the same function on both
// sides: equal words stay equal and different words stay different, whatever the call wrote. Two worlds at two
// addresses made Map depend on the heap layout (a random word equal to an address inside one world was renamed in
// that world only; lane NB, tests/gz_persist_db_segment_multi_packed_files.cpp).
World &SharedWorld()
{
    static World *const w = new World;
    return *w;
}

// Builds the world from the seed and lets the test customise it (deterministic in the seed, so each side gets the
// same). The world is cleared first: Snapshot reads every word of it, and whatever Build leaves unwritten (padding)
// must not carry over from the previous run.
void Prepare(World &w, uint32_t seed, Customise customise)
{
    static_assert(std::is_trivially_copyable_v<World>, "cleared with memset");
    std::memset(&w, 0, sizeof(w));
    Build(w, seed);
    std::mt19937 rng(seed ^ 0x5eed);
    if (customise) customise(w, rng);
}

template <typename Call> Outcome Run(World &w, Call call)
{
    gWorld = &w;
    gTrace.clear();
    gSeqPos = 0;
    Outcome o;
    o.ret = call(w);
    o.trace = gTrace;
    Snapshot(w, o.after);
    gWorld = nullptr;
    return o;
}

template <typename OrigCall, typename OursCall>
void Compare(difftest::Context &ctx, const char *name, uint32_t seed, Customise customise, OrigCall orig, OursCall ours)
{
    World &w = SharedWorld();
    gTrace.reserve(1 << 16);
    Prepare(w, seed, customise);
    Outcome a = Run(w, orig);
    Prepare(w, seed, customise);
    Outcome b = Run(w, ours);
    ++ctx.cases;
    if (a.ret != b.ret) ctx.Fail("%s seed %u: original returned 0x%x, ours 0x%x", name, seed, a.ret, b.ret);
    if (a.trace != b.trace) {
        size_t i = 0;
        while (i < a.trace.size() && i < b.trace.size() && a.trace[i] == b.trace[i]) ++i;
        ctx.Fail("%s seed %u: calls differ at word %zu (original %zu words, ours %zu)", name, seed, i, a.trace.size(), b.trace.size());
    }
    if (a.after != b.after) {
        size_t i = 0;
        while (i < a.after.size() && a.after[i] == b.after[i]) ++i;
        ctx.Fail("%s seed %u: world word %zu differs afterwards (original 0x%x, ours 0x%x)", name, seed, i, a.after[i], b.after[i]);
    }
}

constexpr int kSeeds = 400;

// Slots shared by the tests: the factories 0..7 (query, AddRef, Release), the local objects 10 and 11.
void SetupObjects(World &w)
{
    for (int i = 0; i < 8; ++i) {
        w.vt[i][0] = Fn(&Query);
        w.vt[i][1] = Fn(&R0<1>);
        w.vt[i][2] = Fn(&R0<2>);
    }
    for (int i : {10, 11}) {
        w.vt[i][1] = Fn(&R0<1>);
        w.vt[i][2] = Fn(&R0<2>);
    }
}

// Picks a key: an existing node's key (2 in 3 when there is one), else a random one, into args[0].
uint32_t PickKey(World &w, Table &t, std::mt19937 &rng)
{
    if (t.count > 0 && rng() % 3) return t.nodes[1 + rng() % t.count].key;
    return rng() % 3 ? static_cast<uint32_t>(rng()) : rng() % 32;
}

// The 16-byte key block of vf34 / vf15 / vf168_34: key[1] = the table key, or the marker with the key in key[0].
void PickKeyBlock(World &w, std::mt19937 &rng, bool allowMarker)
{
    uint32_t key = PickKey(w, w.t1, rng);
    if (allowMarker && rng() % 4 == 0) {
        w.keyBlock[1] = kUseA3;
        w.keyBlock[0] = key;
    } else {
        w.keyBlock[1] = key;
    }
}

} // namespace

// ---------------------------------------------------------------------------------------------------------
// Primary slots 34 and 15.

namespace {

void Vf34Setup(World &w, std::mt19937 &rng)
{
    SetupObjects(w);
    for (int i = 0; i < 8; ++i) w.vt[i][20] = Fn(&AcceptKey);
    PickKeyBlock(w, rng, true);
}

} // namespace

PS_TEST(TSPersistSystem3_vf34, 0x1dea30)
{
    using F = uint8_t(__thiscall *)(void *, const uint32_t *, void **);
    auto orig = ctx.Original<F>(0x1dea30);
    CodePatch fill(ctx.image.At(0x1e95c0), reinterpret_cast<void *>(&FillInStandIn));
    CodePatch fillOurs(shape::MemberCode(&cTSPersistSystem::FillInSegments), reinterpret_cast<void *>(&FillInStandIn));
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "vf34", seed, &Vf34Setup,
                [&](World &w) {
                    void *out = w.obj[11];
                    uint32_t r = orig(w.primary, w.keyBlock, &out);
                    RecPtr(out);
                    return r & 0xff;
                },
                [&](World &w) {
                    void *out = w.obj[11];
                    uint32_t r = psshape::Call(PSX(vf34), w.primary, w.keyBlock, &out);
                    RecPtr(out);
                    return r & 0xff;
                });
    }
}

PS_TEST(TSPersistSystem3_vf15, 0x1e5670)
{
    using F = uint8_t(__thiscall *)(void *, const uint32_t *);
    auto orig = ctx.Original<F>(0x1e5670);
    CodePatch fill(ctx.image.At(0x1e95c0), reinterpret_cast<void *>(&FillInStandIn));
    CodePatch fillOurs(shape::MemberCode(&cTSPersistSystem::FillInSegments), reinterpret_cast<void *>(&FillInStandIn));
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "vf15", seed, &Vf34Setup,
                [&](World &w) { return uint32_t(orig(w.primary, w.keyBlock)) & 0xff; },
                [&](World &w) { return uint32_t(psshape::Call(PSX(vf15), w.primary, w.keyBlock)) & 0xff; });
    }
}

// ---------------------------------------------------------------------------------------------------------
// cITSPersistSystem slots 48, 30, 34.

namespace {

void Vf48Setup(World &w, std::mt19937 &rng)
{
    SetupObjects(w);
    w.args[0] = PickKey(w, w.t1, rng);
    // The insert fills the whole store's room (at most 3 used + kItemCap): it stays in place, this store is not a heap block.
    static_assert(3 + kItemCap <= sizeof(w.outStore) / sizeof(w.outStore[0]));
    w.outVec.end = w.outStore + sizeof(w.outStore) / sizeof(w.outStore[0]);
}

} // namespace

PS_TEST(TSPersistSystem3_vf168_48, 0x1e0140)
{
    using F = void(__thiscall *)(void *, uint32_t, void *);
    auto orig = ctx.Original<F>(0x1e0140);
    CodePatch fill(ctx.image.At(0x1e95c0), reinterpret_cast<void *>(&FillInStandIn));
    CodePatch fillOurs(shape::MemberCode(&cTSPersistSystem::FillInSegments), reinterpret_cast<void *>(&FillInStandIn));
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "vf168_48", seed, &Vf48Setup,
                [&](World &w) {
                    orig(w.primary + kSubOffset, w.args[0], &w.outVec);
                    return 0u;
                },
                [&](World &w) {
                    psshape::Call(PSX(GetSegmentsForGroup), w.primary + kSubOffset, w.args[0], &w.outVec);
                    return 0u;
                });
    }
}

namespace {

// The object whose slot 15 yields the id; the id is a key of the id table 2 times in 3, the value one of its ids.
uint32_t __fastcall IdOfObject(void *self, void *)
{
    Note(15, self);
    return gWorld->args[0];
}

void Vf30Setup(World &w, std::mt19937 &rng)
{
    w.vt[9][15] = Fn(&IdOfObject);
    w.args[0] = PickKey(w, w.t2, rng);
    // The value: an id of the found node's range, of the stand-in's, or random.
    const DwordRange *range = nullptr;
    for (int i = 1; i <= w.t2.count; ++i)
        if (w.t2.nodes[i].key == w.args[0]) range = &w.ranges[i - 1];
    if (!range && w.populate) range = &w.lateRange;
    if (range && range->last != range->first && rng() % 4) w.args[1] = range->first[rng() % (range->last - range->first)];
}

} // namespace

PS_TEST(TSPersistSystem3_vf168_30, 0x1de910)
{
    using F = uint8_t(__thiscall *)(void *, void *, uint32_t);
    auto orig = ctx.Original<F>(0x1de910);
    CodePatch fill(ctx.image.At(0x1e97b0), reinterpret_cast<void *>(&IdFillStandIn));
    CodePatch fillOurs(shape::MemberCode(&cTSPersistSystem::SegmentGroups), reinterpret_cast<void *>(&IdFillStandIn));
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "vf168_30", seed, &Vf30Setup,
                [&](World &w) { return uint32_t(orig(w.primary + kSubOffset, w.obj[9], w.args[1])) & 0xff; },
                [&](World &w) {
                    return uint32_t(psshape::Call(PSX(SegmentHasGroup), w.primary + kSubOffset, w.obj[9], w.args[1])) & 0xff;
                });
    }
}

namespace {

void Vf168_34Setup(World &w, std::mt19937 &rng)
{
    SetupObjects(w);
    w.vtI[32] = Fn(&Score);
    PickKeyBlock(w, rng, false);
    w.args[1] = rng() % 4; // the iid passed to the final query
}

} // namespace

PS_TEST(TSPersistSystem3_vf168_34, 0x1dec80)
{
    using F = uint8_t(__thiscall *)(void *, const uint32_t *, uint32_t, void **);
    auto orig = ctx.Original<F>(0x1dec80);
    CodePatch fill(ctx.image.At(0x1e95c0), reinterpret_cast<void *>(&FillInStandIn));
    CodePatch fillOurs(shape::MemberCode(&cTSPersistSystem::FillInSegments), reinterpret_cast<void *>(&FillInStandIn));
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "vf168_34", seed, &Vf168_34Setup,
                [&](World &w) {
                    void *out = w.obj[9];
                    uint32_t r = orig(w.primary + kSubOffset, w.keyBlock, w.args[1], &out);
                    RecPtr(out);
                    return r & 0xff;
                },
                [&](World &w) {
                    void *out = w.obj[9];
                    uint32_t r = psshape::Call(PSX(FindSegment), w.primary + kSubOffset, w.keyBlock, w.args[1], &out);
                    RecPtr(out);
                    return r & 0xff;
                });
    }
}

// ---------------------------------------------------------------------------------------------------------
// Slots 35 and 36: the filter handed to every factory, released afterwards.

namespace {

void Vf35Setup(World &w, std::mt19937 &rng)
{
    SetupObjects(w);
    for (int i = 0; i < 8; ++i) w.vt[i][19] = Fn(&FillOut);
    w.args[0] = PickKey(w, w.t1, rng);
    w.args[1] = rng() % 4 == 0 ? kUseA3 : w.args[0];
    w.args[2] = PickKey(w, w.t1, rng);
}

// Runs one call with the frees recorded: how many blocks went away and their sizes.
template <typename Call> uint32_t RecordFrees(Call call)
{
    difftest::FreeRecorder rec;
    uint32_t r = call();
    rec.Stop();
    size_t sizes[16] = {};
    Rec(static_cast<uint32_t>(rec.Count()));
    rec.SortedSizes(sizes, 16);
    for (size_t i = 0; i < 16 && i < rec.Count(); ++i) Rec(static_cast<uint32_t>(sizes[i]));
    return r;
}

} // namespace

PS_TEST(TSPersistSystem3_vf168_35, 0x1df880)
{
    using F = uint32_t(__thiscall *)(void *, void *, uint32_t);
    auto orig = ctx.Original<F>(0x1df880);
    CodePatch fill(ctx.image.At(0x1e95c0), reinterpret_cast<void *>(&FillInStandIn));
    CodePatch fillOurs(shape::MemberCode(&cTSPersistSystem::FillInSegments), reinterpret_cast<void *>(&FillInStandIn));
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        // The constructor leaves the filter's +0x10 unset in this function: its value is not compared.
        // Checked (DL): the filter constructor 0x1d8b10 stores +0, +4, +8, +0xc, +0x18 only; 0x1df880 then sets +0x14, +0x19.
        Compare(ctx, "vf168_35",
                seed, [](World &w, std::mt19937 &rng) { Vf35Setup(w, rng); gSkipFilter10 = true; },
                [&](World &w) { return RecordFrees([&] { return orig(w.primary + kSubOffset, &w.out16, w.args[0]); }); },
                [&](World &w) {
                    return RecordFrees([&] {
                        return psshape::Call(PSX(ListKeysOfType), w.primary + kSubOffset, &w.out16, w.args[0]);
                    });
                });
    }
}

PS_TEST(TSPersistSystem3_vf168_36, 0x1df740)
{
    using F = uint32_t(__thiscall *)(void *, void *, uint32_t, uint32_t);
    auto orig = ctx.Original<F>(0x1df740);
    CodePatch fill(ctx.image.At(0x1e95c0), reinterpret_cast<void *>(&FillInStandIn));
    CodePatch fillOurs(shape::MemberCode(&cTSPersistSystem::FillInSegments), reinterpret_cast<void *>(&FillInStandIn));
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "vf168_36", seed, &Vf35Setup,
                [&](World &w) { return RecordFrees([&] { return orig(w.primary + kSubOffset, &w.out16, w.args[1], w.args[2]); }); },
                [&](World &w) {
                    return RecordFrees([&] {
                        return psshape::Call(PSX(ListKeys), w.primary + kSubOffset, &w.out16, w.args[1], w.args[2]);
                    });
                });
    }
}

// ---------------------------------------------------------------------------------------------------------
// Slot 14: the candidate value over the factories (no lock, no fill-in).

namespace {

void Vf14Setup(World &w, std::mt19937 &rng)
{
    SetupObjects(w);
    w.vt[10][25] = Fn(&SeqValue);
    w.vt[11][25] = Fn(&SeqValue);
    w.args[0] = PickKey(w, w.t1, rng);
    // Some factory entries are null.
    for (int i = 0; i < kNodes; ++i)
        for (int k = 0; k < kItemCap; ++k)
            if (rng() % 6 == 0) w.items[i][k] = nullptr;
}

} // namespace

PS_TEST(TSPersistSystem3_vf168_14, 0x1e0550)
{
    using F = uint32_t(__thiscall *)(void *, uint32_t);
    auto orig = ctx.Original<F>(0x1e0550);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        // Only ax is the result: the original leaves stale bits in the upper half of eax.
        Compare(ctx, "vf168_14", seed, &Vf14Setup,
                [&](World &w) { return orig(w.primary + kSubOffset, w.args[0]) & 0xffff; },
                [&](World &w) { return psshape::Call(PSX(GetPackedFileGroup), w.primary + kSubOffset, w.args[0]) & 0xffff; });
    }
}

// ---------------------------------------------------------------------------------------------------------
// Slot 27 (file name stem to factories) and slot 18 (factory file name check).

namespace {

const char *const kPaths[] = {
    "",
    "name",
    "C:\\dir\\sub\\stem.package",
    "dir/other.sims2pack",
    "a.b.c",
    "C:\\x\\",
    "C:\\folder\\a_stem_over_fifteen_chars_long.package",
    "C:\\a\\b.txt",
    "C:\\a\\b",
    "c:\\A\\B\\",
    "C:\\a\\b\\",
    "C:/a/b/",
    "rel\\file.package",
    "C:\\a\\b.txt/",
};
constexpr int kPathCount = sizeof(kPaths) / sizeof(kPaths[0]);

// vf168_18 builds cRZFileNames from its strings, and a relative path reaches GetCurrentDirectoryW (a trapped
// import), so its paths are absolute (the name may also be empty: that ends the function before any is built).
const char *const kAbsPaths[] = {
    "C:\\dir\\sub\\stem.package",
    "C:\\x\\",
    "C:\\folder\\a_stem_over_fifteen_chars_long.package",
    "C:\\a\\b.txt",
    "C:\\a\\b",
    "c:\\A\\B\\",
    "C:\\a\\b\\",
    "C:/a/b/",
    "C:\\a\\b.txt/",
};
constexpr int kAbsCount = sizeof(kAbsPaths) / sizeof(kAbsPaths[0]);

uint32_t __fastcall TextLength(void *self, void *)
{
    Note(7, self);
    return static_cast<uint32_t>(std::strlen(gWorld->text));
}

uint32_t __fastcall TextChars(void *self, void *)
{
    Note(6, self);
    return Addr(gWorld->text);
}

// ITS slot 11 with a cRZString (vf168_27): records its text.
uint32_t __fastcall StemId(void *self, void *, const cRZString *s)
{
    Note(11, self);
    for (const char *p = s->Data(); *p; ++p) gTrace.push_back(static_cast<uint8_t>(*p));
    Rec(static_cast<uint32_t>(s->mString.size()));
    return gWorld->args[0];
}

// ITS slot 11 with an object (vf168_18).
uint32_t __fastcall KeyOfObject(void *self, void *, void *obj)
{
    Note(11, self);
    RecPtr(obj);
    return gWorld->args[0];
}

// ITS slot 31 (vf168_18): writes a path chosen per factory into the string.
uint32_t __fastcall PathOut(void *self, void *, void *factory, cRZString *path)
{
    Note(31, self);
    RecPtr(factory);
    int i = ObjIndex(factory);
    path->mString.assign(kAbsPaths[(i >= 0 ? gWorld->retObj[i][31] : 0) % kAbsCount]);
    return 0;
}

void Vf27Setup(World &w, std::mt19937 &rng)
{
    SetupObjects(w);
    w.vt[9][7] = Fn(&TextLength);
    w.vt[9][6] = Fn(&TextChars);
    w.vtI[11] = Fn(&StemId);
    w.vtI[26] = Fn(&R1<26>);
    w.args[0] = PickKey(w, w.t1, rng);
    std::strcpy(w.text, kPaths[rng() % kPathCount]);
}

} // namespace

PS_TEST(TSPersistSystem3_vf168_27, 0x1e2510)
{
    using F = void(__thiscall *)(void *, void *);
    auto orig = ctx.Original<F>(0x1e2510);
    CodePatch fill(ctx.image.At(0x1e95c0), reinterpret_cast<void *>(&FillInStandIn));
    CodePatch fillOurs(shape::MemberCode(&cTSPersistSystem::FillInSegments), reinterpret_cast<void *>(&FillInStandIn));
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "vf168_27", seed, &Vf27Setup,
                [&](World &w) {
                    orig(w.primary + kSubOffset, w.obj[9]);
                    return 0u;
                },
                [&](World &w) {
                    psshape::Call(PSX(ReopenSegmentsForPath), w.primary + kSubOffset, w.obj[9]);
                    return 0u;
                });
    }
}

namespace {

// Stand-in for 0x1c8151 (thiscall on a cRZFileName, ret 8: other, flag), which reaches GetCurrentDirectoryW: records
// both file names' five parts and the flag; returns args[2] (zero in half the worlds: the names compare equal).
uint32_t __fastcall FileNameCompareStandIn(cRZFileName *self, void *, const cRZFileName *other, uint32_t flag)
{
    Note(3004, nullptr);
    const cRZFileName *files[2] = {self, other};
    for (const cRZFileName *f : files) {
        const cRZString *parts[5] = {&f->mDrive, &f->mDir, &f->mName, &f->mExt, &f->mFull};
        for (const cRZString *part : parts) {
            for (const char *p = part->Data(); *p; ++p) gTrace.push_back(static_cast<uint8_t>(*p));
            Rec(static_cast<uint32_t>(part->mString.size()));
        }
    }
    Rec(flag);
    return gWorld->args[2];
}

void Vf18Setup(World &w, std::mt19937 &rng)
{
    w.args[2] = rng() % 2 ? 0u : static_cast<uint32_t>(rng());
    SetupObjects(w);
    w.vtI[11] = Fn(&KeyOfObject);
    w.vtI[31] = Fn(&PathOut);
    w.args[0] = PickKey(w, w.t1, rng);
    w.args[1] = rng() % 6 == 0 ? 4 + rng() % 3 : rng() % 4; // the mode; above 3 skips every factory
    std::strcpy(w.text, rng() % 6 == 0 ? "" : kAbsPaths[rng() % kAbsCount]);
    // Make the interesting case likely: the name equal to what a factory produces.
    if (rng() % 3 == 0) std::strcpy(w.text, kAbsPaths[w.retObj[rng() % 8][31] % kAbsCount]);
}

} // namespace

PS_TEST(TSPersistSystem3_vf168_18, 0x1df300)
{
    using F = uint8_t(__thiscall *)(void *, void *, void *, uint32_t);
    auto orig = ctx.Original<F>(0x1df300);
    CodePatch fill(ctx.image.At(0x1e95c0), reinterpret_cast<void *>(&FillInStandIn));
    CodePatch fillOurs(shape::MemberCode(&cTSPersistSystem::FillInSegments), reinterpret_cast<void *>(&FillInStandIn));
    CodePatch compare(ctx.image.At(0x1c8151), reinterpret_cast<void *>(&FileNameCompareStandIn));
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "vf168_18", seed, &Vf18Setup,
                [&](World &w) {
                    cRZString name;
                    name.Construct(w.text);
                    uint32_t r = orig(w.primary + kSubOffset, &name, w.obj[9], w.args[1]) & 0xff;
                    name.Destruct();
                    return r;
                },
                [&](World &w) {
                    cRZString name;
                    name.Construct(w.text);
                    uint32_t r = psshape::Call(PSX(HasSegmentForFile), w.primary + kSubOffset, &name, w.obj[9], w.args[1]) & 0xff;
                    name.Destruct();
                    return r;
                });
    }
}

// ---------------------------------------------------------------------------------------------------------
// Primary slots 53 and 57.

namespace {

template <typename M> void *MemberCode(M m)
{
    static_assert(sizeof(m) == sizeof(void *), "plain member pointer");
    void *p;
    std::memcpy(&p, &m, sizeof(p));
    return p;
}

uint32_t __fastcall ListenerSlot(void *self, void *, uint32_t event, uint32_t arg)
{
    Note(3, self);
    Rec(event);
    Rec(arg);
    return 0;
}

// Primary slot 39 (a1, &local, 0): hands out object 10 when it works.
uint32_t __fastcall Slot39(void *self, void *, uint32_t a1, void **out, uint32_t zero)
{
    Note(39, self);
    Rec(a1);
    Rec(zero);
    uint32_t r = gWorld->retSlot[39];
    if (r & 0xff) *out = gWorld->obj[10];
    return r;
}

// a2's slot 3 (local, id, &second, a6, a7): hands out object 11 when it works.
uint32_t __fastcall A2Slot3(void *self, void *, uint32_t local, uint32_t iid, void **second, uint32_t a6, uint32_t a7)
{
    Note(103, self);
    Rec(local);
    Rec(iid);
    Rec(a6);
    Rec(a7);
    uint32_t r = RetOf(self, 3);
    if (r & 0xff) *second = gWorld->obj[11];
    return r;
}

// Slot 0 of object 11 with two arguments.
uint32_t __fastcall TwoArgQuery(void *self, void *, uint32_t a, uint32_t b)
{
    Note(0, self);
    Rec(a);
    Rec(b);
    return RetOf(self, 0);
}

// The base vf53 (0x9b5140; ret 0x1c): both the image's and ours are redirected here.
uint32_t __fastcall BaseVf53StandIn(void *self, void *, uint32_t a1, void *a2, void *a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7)
{
    Note(3053, self);
    Rec(a1);
    RecPtr(a2);
    RecPtr(a3);
    Rec(a4);
    Rec(a5);
    Rec(a6);
    Rec(a7);
    return gWorld->retSlot[53];
}

void SetupPrimaryListeners(World &w, std::mt19937 &rng)
{
    Put(w.primary, 0x98, Addr(w.vtLock));
    int n = static_cast<int>(rng() % 4);
    for (int i = 0; i < 6; ++i) {
        w.listeners[i] = w.obj[i];
        w.vt[i][3] = Fn(&ListenerSlot);
    }
    Put(w.primary, 0x230, Addr(w.listeners));
    Put(w.primary, 0x234, Addr(w.listeners + n));
    Put(w.primary, 0x238, Addr(w.listeners + 6));
}

void Vf53Setup(World &w, std::mt19937 &rng)
{
    SetupObjects(w);
    SetupPrimaryListeners(w, rng);
    w.primary[0x22d] = rng() % 4 ? static_cast<uint8_t>(1 + rng() % 255) : 0;
    Put(w.primary, 0x84, rng() % 4 ? static_cast<uint32_t>(rng()) | 1u : 0u);
    w.vtP[39] = Fn(&Slot39);
    w.vt[9][3] = Fn(&A2Slot3);
    w.vt[10][6] = Fn(&R0<6>);
    w.vt[10][7] = Fn(&R0<7>);
    w.vt[11][0] = Fn(&TwoArgQuery);
    w.vt[11][4] = Fn(&R1<4>);
}

} // namespace

PS_TEST(TSPersistSystem3_vf53, 0x1e2f00)
{
    using F = uint8_t(__thiscall *)(void *, uint32_t, void *, void *, uint32_t, uint32_t, uint32_t, uint32_t);
    auto orig = ctx.Original<F>(0x1e2f00);
    CodePatch base(ctx.image.At(0x9b5140), reinterpret_cast<void *>(&BaseVf53StandIn));
    CodePatch baseOurs(MemberCode(&cGZPersistResourceManager::vf53), reinterpret_cast<void *>(&BaseVf53StandIn));
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "vf53", seed, &Vf53Setup,
                [&](World &w) {
                    return uint32_t(orig(w.primary, w.keyBlock[2], w.obj[9], w.obj[8], w.args[0], w.args[1], w.args[2], w.args[3])) & 0xff;
                },
                [&](World &w) {
                    return uint32_t(psshape::Call(PSX(vf53), w.primary, w.keyBlock[2], w.obj[9], w.obj[8], w.args[0], w.args[1],
                                                                                              w.args[2], w.args[3])) &
                           0xff;
                });
    }
}

namespace {

const char *const kSegmentNames[] = {"Alpha.package", "alphaBeta", "BETA", "gamma", "", "ALPHA", "beta_two", "Al"};
const char *const kPrefixes[] = {"alpha", "al", "", "beta", "ALPHA", "gam", "b", "alphabeta_longer"};

uint32_t __fastcall NameOut(void *self, void *, cRZString *s)
{
    Note(11, self);
    int i = ObjIndex(self);
    s->mString.assign(kSegmentNames[(i >= 0 ? gWorld->retObj[i][11] : 0) % 8]);
    return 0;
}

uint32_t __fastcall PrefixLength(void *self, void *)
{
    Note(7, self);
    return static_cast<uint32_t>(std::strlen(gWorld->text));
}

uint32_t __fastcall PrefixChars(void *self, void *)
{
    Note(5, self);
    return Addr(gWorld->text);
}

// mSegmentsByGroup (+0x114) for vf57. Its clear (0x1e8ce0) is the toolchain's unordered_map::clear, which ours runs
// itself, so both sides get a real table there, built in the run (heap nodes through the game's new / delete, both
// redirected to ours): w.args[3] groups (0..5), group g's vector spanning w.segs[g..g+1] (the value destructor
// 0x1e6a40's stand-in records it, so each node's destruction and its order show). Afterwards the size, the head's links
// and whether every bucket is the head (as a clear leaves them) are recorded, the table destroyed and its bytes zeroed.
struct GroupValue {
    void **first, **last, **end;
};
struct GroupHash {
    size_t operator()(uint32_t key) const { return key; }
};
using GroupTable = std::unordered_map<uint32_t, GroupValue, GroupHash>;
static_assert(sizeof(GroupTable) == 0x20, "unordered_map layout");

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

void MakeGroupTable(World &w)
{
    auto *table = new (w.primary + 0x114) GroupTable();
    for (uint32_t g = 0; g < w.args[3]; ++g)
        table->emplace(0x100u + g * 7, GroupValue{w.segs + g, w.segs + g + 1, w.segs + g + 2});
}

void NoteGroupTable(World &w)
{
    uint8_t *table = w.primary + 0x114;
    uint32_t words[8];
    std::memcpy(words, table, sizeof(words));
    auto *head = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(words[1]));
    gTrace.push_back(3104);
    Rec(words[2]);
    Rec(head[0] == words[1] && head[1] == words[1]);
    uint32_t allHead = 1;
    for (auto *b = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(words[3]));
         b != reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(words[4])); ++b)
        allHead &= *b == words[1];
    Rec(allHead);
    Rec(words[4] - words[3]);
    reinterpret_cast<GroupTable *>(table)->~GroupTable();
    std::memset(table, 0, 0x20);
}

// 0x1d89b0 (thiscall(dst, src), ret 4): the copy lives in the world's copyStore. The destination is a stack local, so
// its address is not recorded.
void __fastcall CopyVectorStandIn(PtrVec *dst, void *, const PtrVec *src)
{
    gTrace.push_back(3102);
    RecPtr(src);
    size_t n = static_cast<size_t>(src->last - src->first);
    for (size_t i = 0; i < n; ++i) gWorld->copyStore[i] = src->first[i];
    *dst = {gWorld->copyStore, gWorld->copyStore + n, gWorld->copyStore + n};
}

void __fastcall DestroyVectorStandIn(PtrVec *vec, void *)
{
    gTrace.push_back(3103);
    for (void **p = vec->first; p != vec->last; ++p) RecPtr(*p);
}

void Vf57Setup(World &w, std::mt19937 &rng)
{
    SetupObjects(w);
    int n = static_cast<int>(rng() % 6);
    for (int i = 0; i < 8; ++i) {
        w.segs[i] = w.obj[rng() % 8];
        w.vt[i][11] = Fn(&NameOut);
    }
    Put(w.primary, 0xd4, Addr(w.segs));
    Put(w.primary, 0xd8, Addr(w.segs + n));
    Put(w.primary, 0xdc, Addr(w.segs + 8));
    w.vtP[30] = Fn(&R1<30>);
    w.vt[9][7] = Fn(&PrefixLength);
    w.vt[9][5] = Fn(&PrefixChars);
    std::strcpy(w.text, kPrefixes[rng() % 8]);
    w.args[3] = rng() % 6;
}

} // namespace

PS_TEST(TSPersistSystem3_vf57, 0x1dcd80)
{
    using F = void(__thiscall *)(void *, void *);
    auto orig = ctx.Original<F>(0x1dcd80);
    CodePatch gameNew(ctx.image.At(0x824632), reinterpret_cast<void *>(&GameNew));
    CodePatch gameDelete(ctx.image.At(0x824624), reinterpret_cast<void *>(&GameDelete));
    CodePatch copy(ctx.image.At(0x1d89b0), reinterpret_cast<void *>(&CopyVectorStandIn));
    CodePatch destroy(ctx.image.At(0x1e6a40), reinterpret_cast<void *>(&DestroyVectorStandIn));
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Compare(ctx, "vf57", seed, &Vf57Setup,
                [&](World &w) {
                    MakeGroupTable(w);
                    orig(w.primary, w.obj[9]);
                    NoteGroupTable(w);
                    return 0u;
                },
                [&](World &w) {
                    MakeGroupTable(w);
                    psshape::Call(PSX(RemoveSegmentsWithPrefix), w.primary, w.obj[9]);
                    NoteGroupTable(w);
                    return 0u;
                });
    }
}
