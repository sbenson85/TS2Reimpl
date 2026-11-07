// Differential tests for the cTSPersistSystem functions reimplemented so far (field map in
// src/ts/cTSPersistSystem.h). Each run gets its own copy of a fake primary object built from the same
// seed: its primary bytes are random. Object A is embedded at +0x68; objects B, C (and D, which slot 11
// of C can swap in) are fake objects with recording vtable slots. Return values (only al for the byte and bool results,
// as the callers read), the recorded slot calls with their arguments, and the primary-object bytes
// that changed must all match.
#include "difftest.h"
#include "std/StdHash.h"
#include "ts/cTSPersistSystem.h"
#include "ts_persist_system_shape.h"

#include <cstring>
#include <memory>
#include <random>
#include <vector>

namespace {

using NoArgRet32 = uint32_t(__thiscall *)(void *);
using NoArgRet8 = uint8_t(__thiscall *)(void *);
using NoArgVoid = void(__thiscall *)(void *);
using OneArgRet8 = uint8_t(__thiscall *)(void *, uint32_t);
using FiveArgRet8 = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using PtrNoArg = void *(__thiscall *)(void *);

// Recorded calls: kind, object id, then the arguments.
std::vector<uint32_t> gTrace;
std::vector<std::pair<const void *, uint32_t>> gIds;
uint32_t gRetA1 = 0, gRetA2 = 0, gRetB = 0, gRetB11 = 0, gRetC11 = 0, gRetD8 = 0;
// When set, slot 11 of object C points field +0x13c (in the primary object) at object D.
uint8_t *gSwapField = nullptr;
void *gSwapTo = nullptr;

uint32_t IdOf(const void *p)
{
    for (const auto &e : gIds)
        if (e.first == p) return e.second;
    return 0xffff;
}

void Note(uint32_t kind, const void *self)
{
    gTrace.push_back(kind);
    gTrace.push_back(IdOf(self));
}

// Slots are recorded with __fastcall so they pop their stack arguments like the game's callees do.
uint32_t __fastcall SlotA1(void *self, void *)
{
    Note(1, self);
    return gRetA1;
}

uint32_t __fastcall SlotA2(void *self, void *)
{
    Note(2, self);
    return gRetA2;
}

uint32_t __fastcall SlotB5(void *self, void *, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
    Note(5, self);
    for (uint32_t a : {a1, a2, a3, a4, a5, a6}) gTrace.push_back(a);
    return gRetB;
}

uint32_t __fastcall SlotB7(void *self, void *, uint32_t a)
{
    Note(7, self);
    gTrace.push_back(a);
    return gRetB;
}

uint32_t __fastcall SlotB8(void *self, void *, uint32_t a)
{
    Note(8, self);
    gTrace.push_back(a);
    return gRetB;
}

uint32_t __fastcall SlotB11(void *self, void *)
{
    Note(11, self);
    return gRetB11;
}

uint32_t __fastcall SlotC11(void *self, void *)
{
    Note(111, self);
    if (gSwapField) {
        *reinterpret_cast<void **>(gSwapField) = gSwapTo;
        gTrace.push_back(IdOf(gSwapTo));
    }
    return gRetC11;
}

uint32_t __fastcall SlotC8(void *self, void *, uint32_t a)
{
    Note(108, self);
    gTrace.push_back(a);
    return 0;
}

uint32_t __fastcall SlotD8(void *self, void *, uint32_t a)
{
    Note(208, self);
    gTrace.push_back(a);
    return gRetD8;
}

// Generic recording slots for the later functions. K identifies the slot (its object's base plus the
// slot number, see the vtables in Build); gRet[K] is what it returns. Each records K, the object, and its
// stack arguments, so the recorded calls must match in order and values. Stack arguments are popped by the
// callee (__fastcall, as with the earlier slots), so each template has the arity of the slot it stands for.
constexpr int kKinds = 1100;
uint32_t gRet[kKinds];
// Out objects handed back by the QueryInterface-like slots: G (locals and out1), H (out2).
void *gTargetG = nullptr;
void *gTargetH = nullptr;
// IIDs of vf168_31's two queries.
constexpr uint32_t kIid1 = 0x286d4a6a;
constexpr uint32_t kIid2 = 0x856c8cf7;
// Whether the QueryInterface-like slots also write their out pointer when they fail (the original reads
// the out pointer back and releases it either way).
bool gWriteOnFail = false;

template <int K> uint32_t __fastcall Slot0(void *self, void *)
{
    Note(K, self);
    return gRet[K];
}

template <int K> uint32_t __fastcall Slot1(void *self, void *, uint32_t a)
{
    Note(K, self);
    gTrace.push_back(a);
    return gRet[K];
}

template <int K> uint32_t __fastcall Slot2(void *self, void *, uint32_t a, uint32_t b)
{
    Note(K, self);
    gTrace.push_back(a);
    gTrace.push_back(b);
    return gRet[K];
}

template <int K> uint32_t __fastcall Slot5(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    Note(K, self);
    for (uint32_t v : {a, b, c, d, e}) gTrace.push_back(v);
    return gRet[K];
}

template <int K>
uint32_t __fastcall Slot6(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f)
{
    Note(K, self);
    for (uint32_t v : {a, b, c, d, e, f}) gTrace.push_back(v);
    return gRet[K];
}

template <int K>
uint32_t __fastcall Slot7(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, uint32_t g)
{
    Note(K, self);
    for (uint32_t v : {a, b, c, d, e, f, g}) gTrace.push_back(v);
    return gRet[K];
}

// Returns al only (the callers read al).
template <int K> uint8_t __fastcall Byte0(void *self, void *)
{
    Note(K, self);
    return static_cast<uint8_t>(gRet[K]);
}

// (object, a, out): ok is gRet[K] & 1; writes gTargetG to *out when ok (or when gWriteOnFail).
template <int K> uint8_t __fastcall Ppv(void *self, void *, uint32_t a, void **out)
{
    Note(K, self);
    gTrace.push_back(a);
    uint8_t ok = gRet[K] & 1;
    if (ok || gWriteOnFail) *out = gTargetG;
    return ok;
}

// (object, iid, out, a): ok is gRet[K] & 1; writes gTargetG to *out when ok (or when gWriteOnFail).
template <int K> uint8_t __fastcall Iid3(void *self, void *, uint32_t iid, void **out, uint32_t a)
{
    Note(K, self);
    gTrace.push_back(iid);
    gTrace.push_back(a);
    uint8_t ok = gRet[K] & 1;
    if (ok || gWriteOnFail) *out = gTargetG;
    return ok;
}

// QueryInterface shape: kIid1 succeeds per gRet[K] & 1 and hands out gTargetG; kIid2 per gRet[K + 1] & 1
// and hands out gTargetH. Any other IID fails.
template <int K> uint8_t __fastcall Qi(void *self, void *, uint32_t iid, void **out)
{
    Note(K, self);
    gTrace.push_back(iid);
    bool ok = false;
    void *target = nullptr;
    if (iid == kIid1) {
        ok = gRet[K] & 1;
        target = gTargetG;
    } else if (iid == kIid2) {
        ok = gRet[K + 1] & 1;
        target = gTargetH;
    }
    if ((ok || gWriteOnFail) && target) *out = target;
    return ok;
}

// Primary slot 4, called by cGZPersistResourceManager::vf3 with its five arguments and a zero. The first
// argument points at the caller's local block, so its contents are recorded, not its address.
uint32_t __fastcall Vf4Slot(void *self, void *, uint32_t block, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
    Note(4, self);
    const auto *words = reinterpret_cast<const uint32_t *>(block);
    for (int i = 0; i < 4; ++i) gTrace.push_back(words[i]);
    for (uint32_t v : {a2, a3, a4, a5, a6}) gTrace.push_back(v);
    return gRet[4];
}

// Primary slot 15, called with a pointer to the local block; records the block's four dwords.
uint32_t __fastcall Slot15(void *self, void *, uint32_t block)
{
    Note(15, self);
    const auto *words = reinterpret_cast<const uint32_t *>(block);
    for (int i = 0; i < 4; ++i) gTrace.push_back(words[i]);
    return gRet[15];
}

constexpr size_t kPrimaryBytes = 0x300;
constexpr size_t kObjBytes = 0x20;
constexpr uint32_t kSubOffset = 0xa8;
constexpr int kHashItems = 24;

// A list node of a StdHashTable (0x10 bytes): the link at +0, the key at +8 and the value at +0xc.
struct HashTestNode {
    StdHashNode link;
    uint32_t key;
    uint32_t value;
};
static_assert(sizeof(HashTestNode) == 0x10, "hash test node layout");

struct World {
    alignas(8) uint8_t primary[kPrimaryBytes];
    alignas(8) uint8_t objB[kObjBytes];
    alignas(8) uint8_t objC[kObjBytes];
    alignas(8) uint8_t objD[kObjBytes];
    void *vtA[3];
    void *vtB[12];
    void *vtC[12];
    void *vtD[12];
    // Objects for the later functions: E and F are argument objects, G is a local or out object, H the
    // second out object. P is the primary's vtable and I the cITSPersistSystem vtable (at +0xa8).
    alignas(8) uint8_t objE[kObjBytes];
    alignas(8) uint8_t objF[kObjBytes];
    alignas(8) uint8_t objG[kObjBytes];
    alignas(8) uint8_t objH[kObjBytes];
    void *vtE[64];
    void *vtF[64];
    void *vtG[64];
    void *vtH[64];
    void *vtP[64];
    void *vtI[64];
    // The StdHashTable at primary +0x248 (sub +0x1a0), built by BuildHash: sentinel and nodes, the bucket
    // array, the keys stored in the table, and the keys' count.
    alignas(8) uint8_t hashMem[(kHashItems + 1) * 0x10];
    StdHashBucket hashBuckets[8];
    uint32_t hashKeys[kHashItems];
};

template <typename T> void Put(uint8_t *base, size_t off, T v)
{
    std::memcpy(base + off, &v, sizeof(v));
}

uintptr_t Addr(const void *p)
{
    return reinterpret_cast<uintptr_t>(p);
}

// The hash of vf168_64: FNV-1a over the four bytes of the key, least significant first.
uint32_t HashFnv(uint32_t key)
{
    uint32_t h = 0x811c9dc5u;
    for (int i = 0; i < 4; ++i) {
        h ^= (key >> (8 * i)) & 0xffu;
        h *= 0x01000193u;
    }
    return h;
}

// Builds a table of StdHashTable's layout (see src/std/StdHash.h) in w, at primary +0x248: nodes linked
// bucket by bucket (buckets in order, items in key order within each), and empty buckets with last == head.
// Keys and values are random; the mask is 0, 1, 3 or 7, so several keys share each bucket in most worlds.
void BuildHash(World &w, std::mt19937 &rng)
{
    const uint32_t mask = (1u << (rng() % 4)) - 1;
    auto *nodes = reinterpret_cast<HashTestNode *>(w.hashMem);
    StdHashNode *head = &nodes[0].link;
    head->next = head->prev = head;
    for (int i = 0; i < kHashItems; ++i) {
        w.hashKeys[i] = rng();
        nodes[i + 1].key = w.hashKeys[i];
        nodes[i + 1].value = rng();
    }
    StdHashNode *prev = head;
    for (uint32_t b = 0; b <= mask; ++b) {
        StdHashNode *first = nullptr, *last = nullptr;
        for (int i = 0; i < kHashItems; ++i) {
            if ((HashFnv(w.hashKeys[i]) & mask) != b) continue;
            StdHashNode *n = &nodes[i + 1].link;
            prev->next = n;
            n->prev = prev;
            prev = n;
            if (!first) first = n;
            last = n;
        }
        w.hashBuckets[b] = first ? StdHashBucket{first, last} : StdHashBucket{head, head};
    }
    prev->next = head;
    head->prev = prev;
    Put(w.primary, 0x248 + 0x04, Addr(head));
    Put(w.primary, 0x248 + 0x0c, Addr(w.hashBuckets));
    Put(w.primary, 0x248 + 0x18, mask);
}

// Fields (primary-object offsets): object A at +0x68, B at +0x1c0, C at +0x1e4. Each is null in a
// quarter of the cases. Their bytes are random otherwise.
void Build(World &w, uint32_t seed)
{
    std::mt19937 rng(seed);
    auto fill = [&rng](void *p, size_t n) {
        auto *b = static_cast<uint8_t *>(p);
        for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(rng());
    };
    fill(w.primary, sizeof(w.primary));
    fill(w.objB, sizeof(w.objB));
    fill(w.objC, sizeof(w.objC));
    fill(w.objD, sizeof(w.objD));

    for (auto &s : w.vtA) s = nullptr;
    w.vtA[1] = reinterpret_cast<void *>(&SlotA1);
    w.vtA[2] = reinterpret_cast<void *>(&SlotA2);
    for (auto &s : w.vtB) s = nullptr;
    w.vtB[5] = reinterpret_cast<void *>(&SlotB5);
    w.vtB[7] = reinterpret_cast<void *>(&SlotB7);
    w.vtB[8] = reinterpret_cast<void *>(&SlotB8);
    w.vtB[11] = reinterpret_cast<void *>(&SlotB11);
    for (auto &s : w.vtC) s = nullptr;
    w.vtC[8] = reinterpret_cast<void *>(&SlotC8);
    w.vtC[11] = reinterpret_cast<void *>(&SlotC11);
    for (auto &s : w.vtD) s = nullptr;
    w.vtD[8] = reinterpret_cast<void *>(&SlotD8);

    Put(w.objB, 0x00, Addr(w.vtB));
    Put(w.objC, 0x00, Addr(w.vtC));
    Put(w.objD, 0x00, Addr(w.vtD));

    // Object A is embedded at +0x68: its vtable pointer is the dword there (no null case exists).
    Put(w.primary, 0x68, Addr(w.vtA));
    Put(w.primary, 0x1c0, rng() % 4 ? Addr(w.objB) : uintptr_t(0));
    Put(w.primary, 0x1e4, rng() % 4 ? Addr(w.objC) : uintptr_t(0));

    // The later functions: fake vtables for the objects they call through (only the slots they use).
    fill(w.objE, sizeof(w.objE));
    fill(w.objF, sizeof(w.objF));
    fill(w.objG, sizeof(w.objG));
    fill(w.objH, sizeof(w.objH));
    w.vtB[9] = reinterpret_cast<void *>(&Slot6<209>);
    for (auto *vt : {w.vtE, w.vtF, w.vtG, w.vtH, w.vtP, w.vtI})
        for (int i = 0; i < 64; ++i) vt[i] = nullptr;
    w.vtE[0] = reinterpret_cast<void *>(&Qi<700>);
    w.vtE[5] = reinterpret_cast<void *>(&Slot0<705>);
    w.vtF[5] = reinterpret_cast<void *>(&Slot0<805>);
    w.vtG[2] = reinterpret_cast<void *>(&Slot0<902>);
    w.vtG[8] = reinterpret_cast<void *>(&Slot1<908>);
    w.vtG[10] = reinterpret_cast<void *>(&Byte0<910>);
    w.vtG[26] = reinterpret_cast<void *>(&Slot1<926>);
    w.vtG[30] = reinterpret_cast<void *>(&Slot2<930>);
    w.vtH[2] = reinterpret_cast<void *>(&Slot0<1002>);
    w.vtH[8] = reinterpret_cast<void *>(&Slot1<1008>);
    w.vtP[5] = reinterpret_cast<void *>(&Slot5<505>);
    w.vtP[10] = reinterpret_cast<void *>(&Slot1<510>);
    w.vtP[34] = reinterpret_cast<void *>(&Ppv<534>);
    w.vtI[10] = reinterpret_cast<void *>(&Slot1<610>);
    w.vtI[20] = reinterpret_cast<void *>(&Iid3<620>);
    w.vtI[21] = reinterpret_cast<void *>(&Slot7<621>);
    w.vtI[30] = reinterpret_cast<void *>(&Slot2<630>);
    w.vtI[45] = reinterpret_cast<void *>(&Slot1<645>);
    w.vtE[11] = reinterpret_cast<void *>(&Slot0<711>);
    w.vtF[21] = reinterpret_cast<void *>(&Slot0<821>);
    w.vtP[1] = reinterpret_cast<void *>(&Slot0<1>);
    w.vtP[4] = reinterpret_cast<void *>(&Vf4Slot);
    w.vtP[15] = reinterpret_cast<void *>(&Slot15);
    BuildHash(w, rng);
    Put(w.objE, 0x00, Addr(w.vtE));
    Put(w.objF, 0x00, Addr(w.vtF));
    Put(w.objG, 0x00, Addr(w.vtG));
    Put(w.objH, 0x00, Addr(w.vtH));
    // The primary's vtable pointer (+0) and the cITSPersistSystem subobject's (+0xa8).
    Put(w.primary, 0x00, Addr(w.vtP));
    Put(w.primary, 0xa8, Addr(w.vtI));
}

struct Outcome {
    uint32_t ret = 0;
    std::vector<uint32_t> trace;
    // Primary-object bytes that changed, with their new values. The field at +0x1e4 is left out: its
    // bytes are an address, which differs between the two worlds. `fieldC` says which object it names.
    std::vector<std::pair<size_t, uint8_t>> changes;
    uint32_t fieldC = 0;
};

constexpr size_t kFieldCOffset = 0x1e4;

void SetIds(const World &w)
{
    gIds = {{w.primary, 0},      {w.primary + 0x68, 1}, {w.objB, 2},   {w.objC, 3},  {w.objD, 4},
            {w.objE, 5},         {w.objF, 6},           {w.objG, 7},   {w.objH, 8},  {w.primary + kSubOffset, 9}};
}

// Where the world lives. Original and ours run on this one world, rebuilt from the seed before each, so they see it
// at the same address and the id map is the same function on both sides: equal words stay equal and different
// words stay different, whatever the call wrote. Two worlds at two addresses made the map depend on the heap
// layout (a random word equal to one world's address was renamed in that world only; lane NB,
// tests/gz_persist_db_segment_multi_packed_files.cpp).
World &SharedWorld()
{
    static World *const w = new World;
    return *w;
}

template <typename Call> Outcome Run(World &w, bool swap, Call call)
{
    SetIds(w);
    gTargetG = w.objG;
    gTargetH = w.objH;
    gSwapField = swap ? w.primary + 0x1e4 : nullptr;
    gSwapTo = w.objD;
    uint8_t before[kPrimaryBytes];
    std::memcpy(before, w.primary, kPrimaryBytes);

    gTrace.clear();
    Outcome out;
    out.ret = call(w);
    out.trace = gTrace;
    for (size_t i = 0; i < kPrimaryBytes; ++i)
        if (before[i] != w.primary[i] && (i < kFieldCOffset || i >= kFieldCOffset + 4)) out.changes.push_back({i, w.primary[i]});
    out.fieldC = IdOf(*reinterpret_cast<void *const *>(w.primary + kFieldCOffset));
    gSwapField = nullptr;
    return out;
}

// Runs both sides on identical worlds and compares them. `ret` is compared as the caller reads it.
template <typename OrigCall, typename OursCall>
void Compare(difftest::Context &ctx, const char *name, uint32_t seed, bool swap, OrigCall origCall, OursCall oursCall)
{
    World &sw = SharedWorld();
    Build(sw, seed);
    Outcome oa = Run(sw, swap, [&](World &w) { return origCall(w); });
    Build(sw, seed);
    Outcome ob = Run(sw, swap, [&](World &w) { return oursCall(w); });
    ++ctx.cases;
    if (oa.ret != ob.ret || oa.trace != ob.trace) {
        ctx.Fail("%s seed %u: original returned %u (%zu trace words), ours %u (%zu)", name, seed, oa.ret, oa.trace.size(), ob.ret,
                 ob.trace.size());
    }
    if (oa.fieldC != ob.fieldC) {
        ctx.Fail("%s seed %u: field +0x1e4 names object %u in the original, %u in ours", name, seed, oa.fieldC, ob.fieldC);
    }
    if (oa.changes.size() != ob.changes.size()) {
        ctx.Fail("%s seed %u: original changed %zu primary bytes, ours %zu", name, seed, oa.changes.size(), ob.changes.size());
    } else {
        for (size_t i = 0; i < oa.changes.size(); ++i) {
            if (oa.changes[i] != ob.changes[i]) {
                ctx.Fail("%s seed %u: primary byte +0x%zx differs (original %u, ours %u)", name, seed, oa.changes[i].first,
                         oa.changes[i].second, ob.changes[i].second);
                break;
            }
        }
    }
}

uint32_t RandomOrZero(std::mt19937 &rng)
{
    return rng() % 3 ? rng() : 0;
}

} // namespace

PS_TEST(TSPersistSystem_GetServicePriority, 0x1e0930)
{
    auto orig = ctx.Original<NoArgRet32>(0x1e0930);
    for (uint32_t seed = 0; seed < 200; ++seed) {
        // The service view is at primary + 4; the constant doesn't depend on any field.
        Compare(ctx, "GetServicePriority", seed, false, [&](World &w) { return orig(w.primary + 4); },
                [&](World &w) { return psshape::Call(PS(GetServicePriority), w.primary + 4); });
    }
}

PS_TEST(TSPersistSystem_vf168_3, 0x1dae50)
{
    auto orig = ctx.Original<PtrNoArg>(0x1dae50);
    for (uint32_t seed = 0; seed < 200; ++seed) {
        // The result is the primary object's address: compare it relative to each world's copy.
        Compare(ctx, "vf168_3", seed, false,
                [&](World &w) { return static_cast<uint32_t>(Addr(orig(w.primary + kSubOffset)) - Addr(w.primary)); },
                [&](World &w) {
                    void *its = static_cast<void *>(w.primary + kSubOffset);
                    return static_cast<uint32_t>(Addr(psshape::Call(PS(GetResourceManager), its)) - Addr(w.primary));
                });
    }
}

PS_TEST(TSPersistSystem_vf168_6, 0x1e3ae0)
{
    auto orig = ctx.Original<FiveArgRet8>(0x1e3ae0);
    std::mt19937 rng(6);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRetB = RandomOrZero(rng);
        uint32_t a1 = rng(), a2 = rng(), a3 = rng(), a4 = rng(), a5 = rng();
        Compare(ctx, "vf168_6", seed, false,
                [&](World &w) { return static_cast<uint32_t>(orig(w.primary + kSubOffset, a1, a2, a3, a4, a5)); },
                [&](World &w) {
                    void *its = static_cast<void *>(w.primary + kSubOffset);
                    return static_cast<uint32_t>(psshape::Call(PSX(QueueResourceLoad), its, a1, a2, a3, a4, a5));
                });
    }
}

PS_TEST(TSPersistSystem_vf168_8, 0x1e2170)
{
    auto orig = ctx.Original<OneArgRet8>(0x1e2170);
    std::mt19937 rng(8);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRetB = RandomOrZero(rng);
        uint32_t a = rng();
        Compare(ctx, "vf168_8", seed, false, [&](World &w) { return static_cast<uint32_t>(orig(w.primary + kSubOffset, a)); },
                [&](World &w) {
                    void *its = static_cast<void *>(w.primary + kSubOffset);
                    return static_cast<uint32_t>(psshape::Call(PSX(IsResourceAvailable), its, a));
                });
    }
}

PS_TEST(TSPersistSystem_vf168_9, 0x1e2190)
{
    auto orig = ctx.Original<OneArgRet8>(0x1e2190);
    std::mt19937 rng(9);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRetB = RandomOrZero(rng);
        uint32_t a = rng();
        Compare(ctx, "vf168_9", seed, false, [&](World &w) { return static_cast<uint32_t>(orig(w.primary + kSubOffset, a)); },
                [&](World &w) {
                    void *its = static_cast<void *>(w.primary + kSubOffset);
                    return static_cast<uint32_t>(psshape::Call(PSX(IsResourcePending), its, a));
                });
    }
}

PS_TEST(TSPersistSystem_vf168_41, 0x1dea20)
{
    auto orig = ctx.Original<NoArgVoid>(0x1dea20);
    for (uint32_t seed = 0; seed < 2000; ++seed) {
        Compare(ctx, "vf168_41", seed, false, [&](World &w) { orig(w.primary + kSubOffset); return 0u; },
                [&](World &w) {
                    psshape::Call(PS(ClearDeferWrites), w.primary + kSubOffset);
                    return 0u;
                });
    }
}

PS_TEST(TSPersistSystem_vf168_52, 0x1dae80)
{
    auto orig = ctx.Original<NoArgRet8>(0x1dae80);
    std::mt19937 rng(52);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRetA1 = RandomOrZero(rng);
        Compare(ctx, "vf168_52", seed, false, [&](World &w) { return static_cast<uint32_t>(orig(w.primary + kSubOffset)); },
                [&](World &w) {
                    void *its = static_cast<void *>(w.primary + kSubOffset);
                    return static_cast<uint32_t>(psshape::Call(PS(LockSystem), its));
                });
    }
}

PS_TEST(TSPersistSystem_vf168_53, 0x1dea10)
{
    auto orig = ctx.Original<NoArgRet8>(0x1dea10);
    std::mt19937 rng(53);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRetA2 = RandomOrZero(rng);
        Compare(ctx, "vf168_53", seed, false, [&](World &w) { return static_cast<uint32_t>(orig(w.primary + kSubOffset)); },
                [&](World &w) {
                    void *its = static_cast<void *>(w.primary + kSubOffset);
                    return static_cast<uint32_t>(psshape::Call(PS(UnlockSystem), its));
                });
    }
}

PS_TEST(TSPersistSystem_vf168_68, 0x1e5e40)
{
    auto orig = ctx.Original<NoArgVoid>(0x1e5e40);
    std::mt19937 rng(68);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRetC11 = RandomOrZero(rng);
        gRetD8 = rng();
        // Half the cases have slot 11 repoint field +0x13c at object D, so the second call's receiver
        // shows whether the field is read again.
        bool swap = rng() % 2;
        Compare(ctx, "vf168_68", seed, swap, [&](World &w) { orig(w.primary + kSubOffset); return 0u; },
                [&](World &w) {
                    psshape::Call(PS(StopBackgroundOpen), w.primary + kSubOffset);
                    return 0u;
                });
    }
}

namespace {

// vf168_7's a4 is an address in the null-B case (the original stores a byte there) and a plain value
// otherwise (the original passes it on). The primary's +0x2f0 byte is the store target.
void *Vf7A4(World &w, uint32_t value)
{
    if (*reinterpret_cast<void *const *>(w.primary + 0x1c0)) return reinterpret_cast<void *>(static_cast<uintptr_t>(value));
    return w.primary + 0x2f0;
}

} // namespace

PS_TEST(TSPersistSystem_vf168_7, 0x1e04e0)
{
    using Fn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, void *);
    auto orig = ctx.Original<Fn>(0x1e04e0);
    std::mt19937 rng(7);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRet[505] = rng();
        gRet[209] = rng();
        uint32_t a1 = rng(), a2 = rng(), a3 = rng(), a4 = rng();
        Compare(ctx, "vf168_7", seed, false,
                [&](World &w) { return orig(w.primary + kSubOffset, a1, a2, a3, Vf7A4(w, a4)); },
                [&](World &w) {
                    void *its = static_cast<void *>(w.primary + kSubOffset);
                    return psshape::Call(PSX(GetResourceSpeculative), its, a1, a2, a3, Vf7A4(w, a4));
                });
    }
}

PS_TEST(TSPersistSystem_vf168_11, 0x1dff90)
{
    using Fn = uint32_t(__thiscall *)(void *, void *);
    auto orig = ctx.Original<Fn>(0x1dff90);
    std::mt19937 rng(11);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRet[705] = rng();
        gRet[610] = rng();
        Compare(ctx, "vf168_11", seed, false, [&](World &w) { return orig(w.primary + kSubOffset, w.objE); },
                [&](World &w) {
                    void *its = static_cast<void *>(w.primary + kSubOffset);
                    return psshape::Call(PSX(GetGroupIdForPath), its, w.objE);
                });
    }
}

PS_TEST(TSPersistSystem_vf168_17, 0x1e2d60)
{
    using Fn = uint8_t(__thiscall *)(void *, uint32_t);
    auto orig = ctx.Original<Fn>(0x1e2d60);
    std::mt19937 rng(17);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRet[534] = rng();
        gRet[510] = rng();
        gRet[902] = rng();
        gWriteOnFail = rng() % 2;
        uint32_t a1 = rng();
        Compare(ctx, "vf168_17", seed, false, [&](World &w) { return static_cast<uint32_t>(orig(w.primary + kSubOffset, a1)); },
                [&](World &w) {
                    void *its = static_cast<void *>(w.primary + kSubOffset);
                    return static_cast<uint32_t>(psshape::Call(PSX(DeleteResource), its, a1));
                });
    }
    gWriteOnFail = false;
}

PS_TEST(TSPersistSystem_vf168_22, 0x1dda20)
{
    using Fn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, void *, void *, uint32_t, uint32_t);
    auto orig = ctx.Original<Fn>(0x1dda20);
    std::mt19937 rng(22);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRet[805] = rng();
        gRet[705] = rng();
        gRet[621] = rng();
        uint32_t a1 = rng(), a2 = rng(), a3 = rng(), a6 = rng(), a7 = rng();
        Compare(ctx, "vf168_22", seed, false,
                [&](World &w) { return orig(w.primary + kSubOffset, a1, a2, a3, w.objF, w.objE, a6, a7); },
                [&](World &w) {
                    void *its = static_cast<void *>(w.primary + kSubOffset);
                    return psshape::Call(PSX(OpenSegment), its, a1, a2, a3, w.objF, w.objE, a6, a7);
                });
    }
}

PS_TEST(TSPersistSystem_vf168_23, 0x1e5dc0)
{
    using Fn = uint8_t(__thiscall *)(void *, uint32_t);
    auto orig = ctx.Original<Fn>(0x1e5dc0);
    std::mt19937 rng(23);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRet[620] = rng();
        gRet[910] = rng();
        gWriteOnFail = rng() % 2;
        uint32_t a1 = rng();
        Compare(ctx, "vf168_23", seed, false, [&](World &w) { return static_cast<uint32_t>(orig(w.primary + kSubOffset, a1)); },
                [&](World &w) {
                    void *its = static_cast<void *>(w.primary + kSubOffset);
                    return static_cast<uint32_t>(psshape::Call(PSX(FlushSegmentByInfo), its, a1));
                });
    }
    gWriteOnFail = false;
}

PS_TEST(TSPersistSystem_vf168_31, 0x1dfd40)
{
    using Fn = uint8_t(__thiscall *)(void *, void *, uint32_t);
    auto orig = ctx.Original<Fn>(0x1dfd40);
    std::mt19937 rng(31);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        // gRet[700] and gRet[701] decide the two queries; 908 and 1008 are the out objects' slot 8.
        gRet[700] = rng();
        gRet[701] = rng();
        gRet[908] = rng();
        gRet[1008] = rng();
        gWriteOnFail = rng() % 2;
        uint32_t a2 = rng();
        Compare(ctx, "vf168_31", seed, false,
                [&](World &w) { return static_cast<uint32_t>(orig(w.primary + kSubOffset, w.objE, a2)); },
                [&](World &w) {
                    void *its = static_cast<void *>(w.primary + kSubOffset);
                    return static_cast<uint32_t>(psshape::Call(PSX(GetSegmentPath), its, w.objE, a2));
                });
    }
    gWriteOnFail = false;
}

PS_TEST(TSPersistSystem_vf168_57, 0x1e2700)
{
    using Fn = uint32_t(__thiscall *)(void *, uint32_t, void *, uint32_t);
    auto orig = ctx.Original<Fn>(0x1e2700);
    std::mt19937 rng(57);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRet[630] = rng();
        gRet[645] = rng();
        uint32_t a1 = rng();
        // Half the cases pass a3 with the low byte of slot 30's result, so the early return is taken.
        uint32_t a3 = rng();
        if (rng() % 2) a3 = (a3 & ~0xffu) | (gRet[630] & 0xffu);
        // The second argument points at the primary's +0x2e0; the callee reads its +4 dword.
        Compare(ctx, "vf168_57", seed, false,
                [&](World &w) { return orig(w.primary + kSubOffset, a1, w.primary + 0x2e0, a3); },
                [&](World &w) {
                    void *its = static_cast<void *>(w.primary + kSubOffset);
                    return psshape::Call(PSX(RegisterSegmentForKey), its, a1, w.primary + 0x2e0, a3);
                });
    }
}

PS_TEST(TSPersistSystem_vf55, 0x1e0780)
{
    using Fn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t);
    auto orig = ctx.Original<Fn>(0x1e0780);
    std::mt19937 rng(55);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRet[534] = rng();
        gRet[930] = rng();
        gRet[902] = rng();
        gWriteOnFail = rng() % 2;
        uint32_t a1 = rng(), a2 = rng();
        Compare(ctx, "vf55", seed, false, [&](World &w) { return static_cast<uint32_t>(orig(w.primary, a1, a2)); },
                [&](World &w) {
                    void *prim = static_cast<void *>(w.primary);
                    return static_cast<uint32_t>(psshape::Call(PSX(GetRecordChangeID), prim, a1, a2));
                });
    }
    gWriteOnFail = false;
}

// The out pointer of the QueryInterface test as an offset into the primary (0 when not set), with the
// returned byte. Both sides' primary objects sit at different addresses.
uint32_t EncodeQi(const World &w, uint8_t r, const void *out)
{
    uint32_t offset = out ? static_cast<uint32_t>(Addr(out) - Addr(w.primary)) : 0;
    return r | (offset << 8);
}

PS_TEST(TSPersistSystem_QueryInterface, 0x1e2e50)
{
    using Fn = uint8_t(__thiscall *)(void *, uint32_t, void **);
    auto orig = ctx.Original<Fn>(0x1e2e50);
    constexpr uint32_t kIds[] = {0x9af5e924, 0x03fa40bf, 0x2efe1873, 0x656b8efc};
    std::mt19937 rng(0x2e50);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        // Three of four cases ask for one of the ids handled in the primary; the rest are random (the base).
        uint32_t riid = rng() % 4 ? kIds[rng() % 4] : rng();
        Compare(ctx, "QueryInterface", seed, false,
                [&](World &w) {
                    void *out = nullptr;
                    uint8_t r = orig(w.primary, riid, &out);
                    return EncodeQi(w, r, out);
                },
                [&](World &w) {
                    void *out = nullptr;
                    void *prim = static_cast<void *>(w.primary);
                    uint8_t r = psshape::Call(PSX(QueryInterface), prim, riid, &out);
                    return EncodeQi(w, r, out);
                });
    }
}

PS_TEST(TSPersistSystem_vf56, 0x1e2d50)
{
    using Fn = uint8_t(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x1e2d50);
    // The service object is the game global 0x10c9378; each run sets it to a fake with a recording slot 21.
    void *const saved = *ctx.image.At<void *>(0x10c9378);
    std::mt19937 rng(56);
    for (int i = 0; i < 500; ++i) {
        uint32_t seed = rng();
        gRet[821] = rng();
        Compare(ctx, "vf56", seed, false,
                [&](World &w) {
                    *ctx.image.At<void *>(0x10c9378) = w.objF;
                    return static_cast<uint32_t>(orig(w.primary));
                },
                [&](World &w) {
                    *ctx.image.At<void *>(0x10c9378) = w.objF;
                    void *prim = static_cast<void *>(w.primary);
                    return static_cast<uint32_t>(psshape::Call(PS(vf56), prim));
                });
    }
    *ctx.image.At<void *>(0x10c9378) = saved;
}

PS_TEST(TSPersistSystem_vf168_4, 0x1e0800)
{
    using Fn = uint32_t(__thiscall *)(void *, uint32_t, void *, uint32_t, uint32_t, uint32_t);
    auto orig = ctx.Original<Fn>(0x1e0800);
    std::mt19937 rng(4);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        gRet[711] = rng();
        // Half the cases return zero from the primary's vf3 path's slot 4, so slot 15 is consulted.
        gRet[4] = rng() % 2 ? 0 : rng();
        gRet[15] = rng();
        uint32_t a1 = rng(), a3 = rng(), a4 = rng(), a5 = rng();
        Compare(ctx, "vf168_4", seed, false,
                [&](World &w) { return orig(w.primary + kSubOffset, a1, w.objE, a3, a4, a5); },
                [&](World &w) {
                    void *its = static_cast<void *>(w.primary + kSubOffset);
                    return psshape::Call(PSX(GetResourceFromFileRef), its, a1, w.objE, a3, a4, a5);
                });
    }
}

PS_TEST(TSPersistSystem_vf168_64, 0x1e0410)
{
    using Fn = uint32_t(__thiscall *)(void *, uint32_t);
    auto orig = ctx.Original<Fn>(0x1e0410);
    std::mt19937 rng(64);
    for (int i = 0; i < 2000; ++i) {
        uint32_t seed = rng();
        uint32_t pick = rng();
        bool present = rng() % 4 != 0;
        uint32_t fresh = rng();
        // The key: one stored in the table, or a value that is not stored (the table is the same on both sides).
        auto keyOf = [&](World &w) -> uint32_t {
            if (present) return w.hashKeys[pick % kHashItems];
            uint32_t k = fresh;
            for (;;) {
                bool stored = false;
                for (uint32_t x : w.hashKeys) stored = stored || x == k;
                if (!stored) return k;
                ++k;
            }
        };
        Compare(ctx, "vf168_64", seed, false, [&](World &w) { return orig(w.primary + kSubOffset, keyOf(w)); },
                [&](World &w) {
                    void *its = static_cast<void *>(w.primary + kSubOffset);
                    return psshape::Call(PSX(GetKeyedObject), its, keyOf(w));
                });
    }
}
