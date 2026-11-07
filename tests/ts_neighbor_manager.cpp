// Differential tests for cTSNeighborManager (src/ts/cTSNeighborManager.h): construction and destruction, Read/Write against
// recording fake streams, the neighbour and key lookups, and the members that walk the neighbours.
//
// A world is one manager in a guarded buffer, populated by our own code (the containers are the library's, the game's layout)
// with recording fakes: neighbours, the singleton (cache 0x10cc288) and what it hands out, the message service (0x10c9378),
// the allocator service (0x10c95d4), streams, messages. Every call a fake receives is appended to `events` (pointers to fakes
// become ids, pointers into the manager its offset, stack addresses a marker). Two worlds built from one seed start the same;
// one runs the original, the other ours, and the results, the manager's bytes (heap pointers normalised), the containers'
// contents, the fakes' counts and the event logs are compared.
//
// Standalone shape (tests/ts_neighbor_manager_shape.h): every case runs three routes (the original, our vtable, the adapter with a
// trap) and route 0 is compared with routes 1 and 2. The manager's own slots that the members call through `this` are fakes
// patched over a copy of the route's own vtable.
#include "difftest.h"
#include "ts_neighbor_manager_shape.h"

#include <Windows.h>
#include <cstring>
#include <initializer_list>
#include <random>
#include <utility>
#include <vector>

namespace {
using namespace nbrmgrshape;
constexpr unsigned kSlots = 100;
constexpr unsigned kNeighbors = 12;

struct SObj;
using Custom = uint32_t (*)(SObj *self, uint32_t *args);
struct SObj {
    void **vt;
    uint32_t id;
    int32_t refs;
    uint32_t value[4];
    uint32_t ret[kSlots];
    Custom custom[kSlots];
    void *table[kSlots];
};

enum Fake : unsigned {
    kNb0, kNbLast = kNb0 + kNeighbors - 1, kInnerA, kInnerB, kApp, kFamilies, kFound, kSink, kStream, kMessage, kPayload,
    kKeyObj0, kKeyObj1, kKeyObj2, kTarget, kService, kServer, kAllocator, kPersist, kStore, kRecord, kExtra, kHolder, kFakeCount
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
    uint32_t removeZeroes = 0; // slot 21 fake: how many calls return 0 (after erasing the neighbour) before one returns 1
    uint32_t collect = 0;      // slot 27 fake: how many ids it appends
    uint32_t allocations = 0;
};
World gW;
Mgr *M() { return reinterpret_cast<Mgr *>(gW.work.bytes); }
SObj &S(Fake f) { return gW.fakes[f]; }

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
void *gTable[kSlots][4];
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

// Custom handlers.
uint32_t NbDataValue(SObj *self, uint32_t *args) { return args[0] == 0x43 ? self->value[0] : args[0] == 0x3d ? self->value[1] : self->value[2]; }
uint32_t StreamGet32(SObj *self, uint32_t *args)
{
    *reinterpret_cast<uint32_t *>(args[0]) = 0xabc00000 + self->value[0]++;
    return 1;
}
uint32_t StreamGet16(SObj *self, uint32_t *args)
{
    // value[2] set: a failed read, which leaves the destination as it was (a real stream does; DT). Read 0x4bd55b reads
    // straight into the manager's +0x18, so a copy through a local would show.
    if (self->value[2] == 0) *reinterpret_cast<uint16_t *>(args[0]) = uint16_t(self->value[1]);
    return 1;
}
uint32_t StoreTakeKey(SObj *, uint32_t *args)
{
    const uint32_t *key = reinterpret_cast<const uint32_t *>(args[0]);
    Ev({0xce1, key[0], key[1], key[2], key[3]});
    return 1;
}
uint32_t PersistOpen(SObj *self, uint32_t *args)
{
    StoreTakeKey(self, args);
    if (self->value[0]) *reinterpret_cast<void **>(args[2]) = &S(kHolder);
    return self->value[1];
}
uint32_t FamiliesFind(SObj *self, uint32_t *args) { return int32_t(args[0]) == int32_t(self->value[0]) ? uint32_t(uintptr_t(&S(kFound))) : 0; }
uint32_t MessageQuery(SObj *self, uint32_t *args)
{
    if (self->value[1]) *reinterpret_cast<void **>(args[1]) = &S(kPayload);
    return self->value[0];
}
uint32_t ServiceQuery(SObj *self, uint32_t *args)
{
    *reinterpret_cast<void **>(args[2]) = self->value[0] ? &S(kServer) : nullptr;
    return 1;
}
// The allocator service: blocks from the process heap; the log gets sizes and free markers, not the (side-specific) addresses.
void *__fastcall AllocatorAlloc(SObj *, void *, uint32_t bytes)
{
    ++gW.allocations;
    Ev({evAlloc, bytes});
    return HeapAlloc(GetProcessHeap(), 0, bytes ? bytes : 1);
}
void __fastcall AllocatorFree(SObj *, void *, void *block)
{
    Ev({evFree, block != nullptr});
    if (block) HeapFree(GetProcessHeap(), 0, block);
}

// The manager's own slots that the members call through `this`.
void *gMgrVT[40];
uint32_t __fastcall MgrAddRef(Mgr *m, void *) { Ev({evMgr, 1}); return ++m->mRefCount; }
void *__fastcall MgrDelete(Mgr *m, void *, uint32_t flags) { Ev({evMgr, 34, flags}); return m; }
uint32_t __fastcall MgrRemove(Mgr *m, void *, void *neighbor)
{
    Ev({evMgr, 21, Norm(uint32_t(uintptr_t(neighbor)))});
    if (gW.removeZeroes == 0) return 1;
    --gW.removeZeroes;
    for (auto it = m->mNeighbors.begin(); it != m->mNeighbors.end(); ++it)
        if (it->second.Get() == neighbor) { m->mNeighbors.erase(it); break; }
    return 0;
}
uint32_t __fastcall MgrCollect(Mgr *, void *, void *target, NeighborIdVector *ids, uint32_t third)
{
    Ev({evMgr, 27, Norm(uint32_t(uintptr_t(target))), third, uint32_t(ids->size())});
    for (uint32_t i = 0; i < gW.collect; ++i) ids->push_back(0x70 + i * 3);
    return uint32_t(ids->size());
}

// Slots 16 / 17 of the manager: GetNeighbor(id, includeAll) and FindByKey(key) answered from the fakes.
void *__fastcall MgrGet(Mgr *, void *, int32_t id, uint32_t all)
{
    Ev({evMgr, 16, uint32_t(id), all});
    return id >= 1 && id <= 20 ? &S(Fake(kNb0 + id % kNeighbors)) : nullptr;
}
void *__fastcall MgrFind(Mgr *, void *, uint32_t key)
{
    Ev({evMgr, 17, key});
    return key % 5 == 0 ? nullptr : &S(Fake(kNb0 + key % kNeighbors));
}

enum Fill : uint32_t { fNeighbors = 1, fKeys = 2, fNullNeighbor = 4 };

void Build(uint32_t seed, uint32_t fill)
{
    static bool filled = false;
    if (!filled) FillAll(std::make_index_sequence<kSlots>()), filled = true;
    World &w = gW;
    std::mt19937 r(seed * 2654435761u + 3);
    w.events.clear();
    w.allocations = 0;
    for (auto &b : w.work.before) b = uint8_t(r());
    for (auto &b : w.work.after) b = uint8_t(r());
    for (auto &b : w.work.bytes) b = uint8_t(r());
    for (unsigned i = 0; i < kFakeCount; ++i) ResetFake(w.fakes[i], 0x100 + i);
    for (unsigned i = kNb0; i <= kNbLast; ++i) {
        SObj &n = w.fakes[i];
        n.value[0] = r() % 3;                 // data value 0x43
        n.value[1] = r() % 3 ? r() % 3 : 0xff00u | (r() % 3); // data value 0x3d (sometimes negative as a 16-bit value)
        n.value[2] = r() % 4 ? r() % 40 : r(); // data value 0xba (a bit index, sometimes a full byte)
        Set(n, 8, 0, r() % 3 ? r() & 0xffff : r());
        Set(n, 10, 0, r() % 3 ? &S(kInnerA) : nullptr);
        Set(n, 16, 1, 0, NbDataValue);
        Set(n, 34, 0, r() % 3 == 0 ? 1 + r() % 0xff : (r() & 0xffffff00));
        Set(n, 35, 0, r() % 2 ? 1 : (r() & 0xffffff00));
        Set(n, 37, 1);
        Set(n, 7, 0);
    }
    Set(S(kPersist), 3, 0, &S(kStore));
    Set(S(kStore), 10, 1, 0, StoreTakeKey);
    // Save: the record and second-record fakes, the persist system's slots.
    for (unsigned i = kNb0; i <= kNbLast; ++i) {
        SObj &n = w.fakes[i];
        Set(n, 48, 0, &S(kRecord));
        Set(n, 38, 0, r() % 2 ? 1 : (r() & 0xffffff00));
        Set(n, 39, 1);
        Set(n, 14, 1, r() % 3 ? &S(kExtra) : nullptr);
    }
    Set(S(kRecord), 4, 1, 0, StoreTakeKey);
    Set(S(kExtra), 17, 0, r() % 3 == 0);
    Set(S(kExtra), 14, 0, r() % 2 ? 1 : (r() & 0xffffff00));
    Set(S(kExtra), 4, 1, 0, StoreTakeKey);
    Set(S(kExtra), 15, 1);
    S(kPersist).value[0] = r() % 5 != 0; // the holder (always given when the open succeeds: the original uses it unchecked)
    S(kPersist).value[1] = r() % 4 ? 1 : (r() % 2 ? 0 : r() & 0xffffff00);
    if (S(kPersist).value[1]) S(kPersist).value[0] = 1;
    Set(S(kPersist), 34, 3, 0, PersistOpen);
    Set(S(kPersist), 16, 2, r() % 4 ? 1 : (r() % 2 ? 0 : 0x100));
    Set(S(kPersist), 17, 1, 0, StoreTakeKey);
    Set(S(kHolder), 10, 0);
    Set(S(kInnerA), 4, 0, r() % 3 ? &S(kInnerB) : nullptr);
    Set(S(kInnerB), 3, 1);
    Set(S(kApp), 9, 0, &S(kFamilies));
    Set(S(kApp), 6, 0, &S(kSink));
    S(kFamilies).value[0] = r() % 3 ? r() % 3 : uint32_t(int32_t(int16_t(0xff00u | (r() % 3))));
    Set(S(kFamilies), 11, 1, 0, FamiliesFind);
    Set(S(kFound), 23, 1, r());
    Set(S(kSink), 39, 1);
    for (unsigned k = kKeyObj0; k <= kKeyObj2; ++k) Set(w.fakes[k], 11, 1);
    Set(S(kTarget), 62, 1);
    Set(S(kServer), 5, 2);
    Set(S(kServer), 6, 2);
    S(kService).value[0] = 1;
    Set(S(kService), 5, 3, 0, ServiceQuery);
    S(kAllocator).table[3] = reinterpret_cast<void *>(&AllocatorAlloc);
    S(kAllocator).table[4] = reinterpret_cast<void *>(&AllocatorFree);

    Mgr *m = Construct(w.work.bytes);
    m->mRefCount = r() % 4;
    m->mUserFileCounter = r();
    m->mNextNeighborId = uint16_t(r());
    m->mDirty = uint8_t(r() % 3 ? r() % 2 : r());
    for (uint32_t *k : {&m->mResourceKey.type, &m->mResourceKey.group, &m->mResourceKey.instance, &m->mResourceKey.resource})
        *k = r();
    if (fill & fNeighbors) {
        const unsigned count = r() % 9;
        for (unsigned i = 0; i < count; ++i) {
            const int32_t key = r() % 5 == 0 ? int32_t(r() % 7) - 3 : int32_t(r() % 30);
            m->mNeighbors[key].Assign(&w.fakes[kNb0 + r() % kNeighbors]);
        }
        if ((fill & fNullNeighbor) && !m->mNeighbors.empty()) m->mNeighbors.begin()->second.Assign(nullptr);
    }
    if (fill & fKeys) {
        const unsigned count = r() % 6;
        for (unsigned i = 0; i < count; ++i) m->mByPersonDataId[r() % 4 ? r() % 16 : r()].Assign(&w.fakes[kKeyObj0 + r() % 3]);
    }
    if (r() % 3 == 0) m->mMessageIds.insert(r() % 0x200);
    // The fakes over a copy of the route's own vtables, last, so the constructor above ran with the real ones.
    ClearPatches();
    for (unsigned s = 0; s < kFaceSlots[0]; ++s) gPatch[0][s] = gMgrVT[s];
    Install(w.work.bytes);
    gTrapHits = 0;
    w.events.clear();
}

using Blob = std::vector<uint32_t>;

void Capture(Blob &b, bool alive = true)
{
    World &w = gW;
    b.push_back(0xb10b);
    for (auto v : w.work.before) b.push_back(v);
    for (auto v : w.work.after) b.push_back(v);
    if (alive) {
        uint8_t copy[sizeof(Mgr)];
        std::memcpy(copy, w.work.bytes, sizeof(copy));
        auto word = [&](unsigned off) -> uint32_t & { return *reinterpret_cast<uint32_t *>(copy + off); };
        for (unsigned off : {0x1cu, 0x28u, 0x30u, 0x34u, 0x38u, 0x54u}) word(off) = 0; // heap blocks (contents below)
        CodeVtableWords(copy);
        for (unsigned i = 0; i < sizeof(copy) / 4; ++i) b.push_back(reinterpret_cast<uint32_t *>(copy)[i]);
        Mgr *m = M();
        b.push_back(0xc0de);
        for (auto &e : m->mNeighbors) { b.push_back(uint32_t(e.first)); b.push_back(Norm(uint32_t(uintptr_t(e.second.Get())))); }
        b.push_back(0xc0df);
        b.push_back(uint32_t(m->mByPersonDataId.bucket_count()));
        for (auto &e : m->mByPersonDataId) { b.push_back(e.first); b.push_back(Norm(uint32_t(uintptr_t(e.second.Get())))); }
        b.push_back(0xc0e0);
        for (auto v : m->mMessageIds) b.push_back(v);
    } else {
        for (unsigned i = 0; i < 4; ++i) b.push_back(VtCode(i, reinterpret_cast<uint32_t *>(w.work.bytes)[i]));
    }
    b.push_back(gTrapHits);
    for (auto &o : w.fakes) b.push_back(uint32_t(o.refs));
    b.push_back(0xe7e7);
    for (uint32_t e : w.events) b.push_back(e);
}

bool Same(difftest::Context &ctx, const Blob &a, const Blob &b, const char *what, uint32_t seed, int route = 1)
{
    ++ctx.cases;
    size_t at = 0;
    while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
    if (at == a.size() && at == b.size()) return true;
    ctx.Fail("%s seed %u route %d differs at word %zu (original %x, replacement %x; sizes %zu/%zu)", what, seed, route, at,
        at < a.size() ? a[at] : 0xdeadbeef, at < b.size() ? b[at] : 0xdeadbeef, a.size(), b.size());
    return false;
}
// Route 0 compared with routes 1 and 2.
void SameRoutes(difftest::Context &ctx, const Blob (&blobs)[kRoutes], const char *what, uint32_t seed)
{
    for (int route = 1; route < kRoutes; ++route) Same(ctx, blobs[0], blobs[route], what, seed, route);
}

// Points the game's globals at the fakes for the scope.
struct Globals {
    difftest::Context &ctx;
    static constexpr uint32_t kRvas[] = {0x10cc288, 0x10c9378, 0x10c95d4, 0x10c95d8, 0x10faf6c};
    uint32_t saved[5];
    explicit Globals(difftest::Context &c) : ctx(c)
    {
        for (unsigned i = 0; i < 5; ++i) saved[i] = *ctx.image.At<uint32_t>(kRvas[i]);
        gMgrVT[1] = reinterpret_cast<void *>(&MgrAddRef);
        gMgrVT[16] = reinterpret_cast<void *>(&MgrGet);
        gMgrVT[17] = reinterpret_cast<void *>(&MgrFind);
        gMgrVT[21] = reinterpret_cast<void *>(&MgrRemove);
        gMgrVT[27] = reinterpret_cast<void *>(&MgrCollect);
        gMgrVT[34] = reinterpret_cast<void *>(&MgrDelete);
        Point();
    }
    void Point()
    {
        *ctx.image.At<void *>(0x10cc288) = &S(kApp);
        *ctx.image.At<void *>(0x10c9378) = &S(kService);
        *ctx.image.At<void *>(0x10c95d4) = &S(kAllocator);
        *ctx.image.At<int32_t>(0x10c95d8) = INT32_MIN;
        *ctx.image.At<void *>(0x10faf6c) = &S(kPersist);
    }
    ~Globals()
    {
        for (unsigned i = 0; i < 5; ++i) *ctx.image.At<uint32_t>(kRvas[i]) = saved[i];
    }
};

template <class Setup, class Run>
void Pair(difftest::Context &ctx, const char *what, uint32_t seed, uint32_t fill, Setup setup, Run run, bool alive = true)
{
    Blob blobs[kRoutes];
    for (int route = 0; route < kRoutes; ++route) {
        gRoute = route;
        Build(seed, fill);
        std::mt19937 rng(seed * 7919u + 5);
        setup(rng);
        const uint32_t result = uint32_t(run(route == 0));
        blobs[route].push_back(result);
        Capture(blobs[route], alive);
    }
    gRoute = 0;
    SameRoutes(ctx, blobs, what, seed);
}
auto NoSetup = [](std::mt19937 &) {};

constexpr uint32_t kSeeds = 120;
#define ORIG(Fn, rva) ctx.Original<Fn>(rva)
// The function at `rva` on the route's way (face 0..3 of its slot, kNoFace for a member that is no slot).
#define ROUTE(Fn, rva, face, name, ...) Call<Fn>(ctx, M(), rva, face, NBRMGR_ADAPTER(name), __VA_ARGS__)
#define ROUTE_NA(Fn, rva, face, name, ...) Call<Fn>(ctx, M(), rva, face, NBRMGR_NO_ADAPTER, __VA_ARGS__)
#define ROUTE0(Fn, rva, face, name) Call<Fn>(ctx, M(), rva, face, NBRMGR_ADAPTER(name))
#define ROUTE0_NA(Fn, rva, face, name) Call<Fn>(ctx, M(), rva, face, NBRMGR_NO_ADAPTER)
} // namespace

// ---- construction and destruction ----
DIFF_TEST(NeighborManager_Create, 0x4bc570)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < 24; ++seed) {
        Build(seed, 0);
        using Fn = Mgr *(__cdecl *)();
        Mgr *a = ORIG(Fn, 0x4bc570)();
        Mgr *b = Mgr::Create();
        ++ctx.cases;
        if (!a || !b) { ctx.Fail("create returned null"); continue; }
        if (HeapSize(GetProcessHeap(), 0, a) < sizeof(Mgr) || HeapSize(GetProcessHeap(), 0, b) < sizeof(Mgr)) ctx.Fail("create block size");
        for (unsigned f = 0; f < kFaces; ++f) {
            if (shape::Word(a, kFaceOffset[f]) != shape::P(GameVt(f))) ctx.Fail("original create: face %u vtable", f);
            // Ours carries our vtables (the class's switch), not the game's.
            if (shape::Word(b, kFaceOffset[f]) != shape::P(OurVt(f)) || OurVt(f) == GameVt(f)) ctx.Fail("our create: face %u vtable", f);
        }
        Blob blobs[2];
        for (Mgr *m : {a, b}) {
            Blob &out = blobs[m == b];
            uint8_t copy[sizeof(Mgr)];
            std::memcpy(copy, m, sizeof(copy));
            for (unsigned off : {0x1cu, 0x28u, 0x30u, 0x34u, 0x38u, 0x54u}) *reinterpret_cast<uint32_t *>(copy + off) = 0;
            for (unsigned f = 0; f < kFaces; ++f) { // vtable words: the side's class vtable of face f as one code
                uint32_t &word = *reinterpret_cast<uint32_t *>(copy + kFaceOffset[f]);
                if (word == shape::P(GameVt(f)) || word == shape::P(OurVt(f))) word = 0x7d00 + f;
            }
            for (unsigned i = 0; i < 0x1b; ++i) out.push_back(copy[i]); // byte +0x1b is padding the constructor leaves
            for (unsigned i = 0x1c; i < sizeof(Mgr); ++i) out.push_back(copy[i]);
            out.push_back(uint32_t(m->mNeighbors.size()));
            out.push_back(m->mNeighbors.begin() == m->mNeighbors.end());
            out.push_back(uint32_t(m->mByPersonDataId.size()));
            out.push_back(uint32_t(m->mByPersonDataId.bucket_count()));
            for (auto v : m->mMessageIds) out.push_back(v);
            // Every bucket of the empty map points at the list head.
            const uint32_t head = *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(m) + 0x28);
            const uint32_t *first = *reinterpret_cast<uint32_t **>(reinterpret_cast<uint8_t *>(m) + 0x30);
            const uint32_t *last = *reinterpret_cast<uint32_t **>(reinterpret_cast<uint8_t *>(m) + 0x34);
            out.push_back(uint32_t(last - first));
            for (const uint32_t *p = first; p != last; ++p) out.push_back(*p == head);
        }
        Same(ctx, blobs[0], blobs[1], "Create", seed);
        // Bit 0 of the flags frees the block: the original's destructor, ours through our vtable (slot 34) and the original again
        // on an object of the game's shape (its adapter was dropped, lane GP: only the game's vtable reached it) each free the
        // same blocks.
        Mgr *c = Mgr::Create();
        for (unsigned f = 0; f < kFaces; ++f) shape::VTableAt(c, kFaceOffset[f]) = const_cast<void **>(GameVt(f));
        using Dtor = Mgr *(__thiscall *)(Mgr *, uint32_t);
        size_t sizes[kRoutes][8] = {};
        size_t counts[kRoutes];
        for (int route = 0; route < kRoutes; ++route) {
            Mgr *m = route == 0 ? a : route == 1 ? b : c;
            difftest::FreeRecorder rec;
            Mgr *r;
            if (route == 1) r = reinterpret_cast<Dtor>(shape::VTableAt(m, 0)[34])(m, 1);
            else r = ORIG(Dtor, 0x4bb7d0)(m, 1);
            rec.Stop();
            ++ctx.cases;
            if (r != m) ctx.Fail("DeletingDestructor(1) seed %u route %d returned %p", seed, route, static_cast<void *>(r));
            if (!rec.Freed(m)) ctx.Fail("DeletingDestructor(1) seed %u route %d kept the block", seed, route);
            counts[route] = rec.SortedSizes(sizes[route], 8);
        }
        for (int route = 1; route < kRoutes; ++route)
            if (counts[0] != counts[route] || std::memcmp(sizes[0], sizes[route], sizeof(sizes[0])) != 0)
                ctx.Fail("DeletingDestructor(1) frees differ on route %d", route);
    }
}

DIFF_TEST(NeighborManager_DeletingDestructor, 0x4bb7d0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed)
        Pair(ctx, "DeletingDestructor", seed, seed % 8, NoSetup, [&](bool original) {
            using Fn = Mgr *(__thiscall *)(Mgr *, uint32_t);
            const uint32_t flags = (seed % 3) * 2; // bit 0 (freeing) is covered by Create
            (void)original;
            Mgr *r = ROUTE_NA(Fn, 0x4bb7d0, 0, DeletingDestructor, flags);
            return Norm(uint32_t(uintptr_t(r)));
        }, false);
}

DIFF_TEST(NeighborManager_ReleaseAndQuery, 0x4be130)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        Pair(ctx, "Release", seed, seed % 8, [&](std::mt19937 &rng) { M()->mRefCount = rng() % 4 == 0 ? rng() : rng() % 4; },
            [&](bool) { return ROUTE0(uint32_t(__thiscall *)(Mgr *), 0x4be130, 0, Release); });
        constexpr uint32_t ids[] = {1, 0xcc2a6a30, 0xe4fda3d4, 0x456b8f1d, 0x8db5bf48, 0, 2, 0x456b8f1c, 0x456b8f1e, 0x8db5bf47,
            0xcc2a6a34, 0xffffffff, 0xe4fda3d5};
        const uint32_t id = ids[seed % (sizeof(ids) / 4)];
        Pair(ctx, "QueryInterface", seed, 0, NoSetup, [&](bool) {
            void *out = reinterpret_cast<void *>(0x5a5a5a5a);
            const uint8_t r = ROUTE_NA(uint8_t(__thiscall *)(Mgr *, uint32_t, void **), 0x4bd480, 0, QueryInterface, id, &out);
            return uint32_t(r) | (Norm(uint32_t(uintptr_t(out))) << 8);
        });
        Pair(ctx, "GetGZCLSID", seed, 0, NoSetup, [&](bool) { return ROUTE0(uint32_t(__thiscall *)(Mgr *), 0x4bc6f0, 0, GetGZCLSID); });
    }
}

// ---- serialisation ----
DIFF_TEST(NeighborManager_Write, 0x4bf6c0)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed)
        Pair(ctx, "Write", seed, seed % 4,
            [&](std::mt19937 &rng) {
                SObj &s = S(kStream);
                for (unsigned slot : {6u, 9u}) Set(s, slot, 1, rng() % 2);
                Set(s, 18, 0, rng() % 2 ? 0 : (rng() % 3 ? 1 : rng() & 0xffffff00));
            },
            [&](bool) {
                using Fn = uint8_t(__thiscall *)(Mgr *, void *);
                return ROUTE(Fn, 0x4bf6c0, 0, Write, static_cast<void *>(&S(kStream)));
            });
}

DIFF_TEST(NeighborManager_Read, 0x4bd500)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed)
        Pair(ctx, "Read", seed, seed % 8,
            [&](std::mt19937 &rng) {
                SObj &s = S(kStream);
                s.value[1] = rng();
                s.value[2] = rng() % 4 == 0;
                Set(s, 9, 1, 0, StreamGet32);
                Set(s, 6, 1, 0, StreamGet16);
            },
            [&](bool) {
                using Fn = uint8_t(__thiscall *)(Mgr *, void *);
                return ROUTE_NA(Fn, 0x4bd500, 0, Read, static_cast<void *>(&S(kStream)));
            });
}

// ---- messages, flags, key ----
DIFF_TEST(NeighborManager_Messages, 0x4bc3a0)
{
    Globals globals(ctx);
    using MsgFn = uint8_t(__thiscall *)(void *, void *);
    for (uint32_t seed = 0; seed < kSeeds * 2; ++seed) {
        Pair(ctx, "DoMessage", seed, fKeys,
            [&](std::mt19937 &rng) {
                SObj &msg = S(kMessage);
                msg.value[0] = rng() % 5 ? 1 : (rng() % 2 ? 0 : rng() & 0xffffff00);
                msg.value[1] = msg.value[0] ? 1 : rng() % 2; // the original uses the payload unchecked when the query succeeds
                Set(msg, 0, 2, 0, MessageQuery);
                Set(msg, 4, 0, rng() % 4 ? 0xa8 : (rng() % 2 ? 0xa9 : 0x1a8));
                Set(S(kPayload), 8, 0, rng() % 4 ? 1 : (rng() % 2 ? 0 : rng() & 0xffffff00));
                // A key that is in the map most of the time.
                uint32_t key = rng() % 16;
                if (rng() % 3 == 0 && !M()->mByPersonDataId.empty()) key = M()->mByPersonDataId.begin()->first;
                Set(S(kPayload), 9, 0, key);
            },
            [&](bool) {
                void *message = seed % 17 == 3 ? nullptr : &S(kMessage);
                return ROUTE_NA(MsgFn, 0x4bc3a0, 3, DoMessage, message);
            });
    }
    for (uint32_t seed = 0; seed < 40; ++seed) {
        Pair(ctx, "RegisterMessages", seed, 0, [&](std::mt19937 &) {}, [&](bool) {
            return ROUTE0(uint8_t(__thiscall *)(Mgr *), 0x4bd270, 0, RegisterMessages);
        });
        Pair(ctx, "UnregisterMessages", seed, 0, [&](std::mt19937 &rng) { S(kService).value[0] = rng() % 4 != 0; },
            [&](bool) { return ROUTE0(uint8_t(__thiscall *)(Mgr *), 0x4be650, 0, UnregisterMessages); });
        Pair(ctx, "SetDirty", seed, 0, NoSetup, [&](bool) {
            ROUTE0(void(__thiscall *)(Mgr *), 0x4bd470, 0, SetDirty);
            return 0;
        });
        Pair(ctx, "SetKey", seed, 0, NoSetup, [&](bool) {
            const uint32_t key[4] = {seed * 3 + 1, ~seed, 0x12345678 ^ seed, seed << 20};
            return ROUTE(MsgFn, 0x4be640, 1, SetKey, static_cast<void *>(const_cast<uint32_t *>(key)));
        });
    }
}

// ---- lookups ----
DIFF_TEST(NeighborManager_Lookups, 0x4bd130)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds * 2; ++seed) {
        std::mt19937 values(seed + 11);
        const int16_t after = int16_t(values() % 4 == 0 ? int32_t(values()) : int32_t(values() % 34) - 4);
        const uint8_t all = uint8_t(values() % 3 == 0 ? values() % 2 : values() % 4 == 0 ? values() : 0);
        Pair(ctx, "NextNeighborId", seed, fNeighbors, NoSetup, [&](bool) {
            using Fn = int16_t(__thiscall *)(Mgr *, int16_t, uint8_t);
            return uint16_t(ROUTE_NA(Fn, 0x4bd130, 0, NextNeighborId, after, all));
        });
        const int32_t id = values() % 5 == 0 ? int32_t(values()) : int32_t(values() % 34) - 4;
        Pair(ctx, "GetNeighbor", seed, fNeighbors, NoSetup, [&](bool) {
            using Fn = void *(__thiscall *)(Mgr *, int32_t, uint8_t);
            return Norm(uint32_t(uintptr_t(ROUTE_NA(Fn, 0x4bc4c0, 0, GetNeighbor, id, all))));
        });
        const uint32_t key = values() % 4 == 0 ? values() : values() % 18;
        Pair(ctx, "FindByKey", seed, fKeys, NoSetup, [&](bool) {
            using Fn = void *(__thiscall *)(Mgr *, uint32_t);
            return Norm(uint32_t(uintptr_t(ROUTE_NA(Fn, 0x4bc480, 0, FindNeighborByKey, key))));
        });
        Pair(ctx, "HighestNeighborId", seed, seed % 2 ? fNeighbors : 0, NoSetup, [&](bool) {
            return ROUTE0_NA(int32_t(__thiscall *)(Mgr *), 0x4bd200, 0, HighestNeighborId);
        });
        Pair(ctx, "CountNeighborsReporting35", seed, fNeighbors | (seed % 3 == 0 ? fNullNeighbor : 0), NoSetup, [&](bool) {
            return ROUTE0_NA(int32_t(__thiscall *)(Mgr *), 0x4bd1b0, 0, CountFlaggedNeighbors);
        });
    }
}

DIFF_TEST(NeighborManager_GetNeighborIds, 0x4bd010)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds * 2; ++seed) {
        std::mt19937 values(seed + 29);
        const uint32_t mask = values() % 3 == 0 ? 0 : values() % 3 == 0 ? values() : 1u << (values() % 40 % 32);
        const uint8_t all = uint8_t(values() % 2 ? values() % 2 : values() % 3 == 0 ? values() : 0);
        const unsigned start = values() % 4;      // elements already in the vector
        const unsigned capacity = start + values() % 6;
        Blob blobs[kRoutes];
        for (int side = 0; side < kRoutes; ++side) {
            gRoute = side;
            Build(seed, fNeighbors);
            NeighborIdVector *out = new NeighborIdVector();
            out->reserve(capacity);
            for (unsigned i = 0; i < start; ++i) out->push_back(0xeee0 + i);
            gW.events.clear();
            gW.allocations = 0;
            using Fn = void(__thiscall *)(Mgr *, NeighborIdVector *, uint32_t, uint8_t);
            ROUTE_NA(Fn, 0x4bd010, 0, GetNeighborIds, out, mask, all);
            blobs[side].push_back(uint32_t(out->size()));
            blobs[side].push_back(uint32_t(out->capacity()));
            blobs[side].push_back(gW.allocations);
            for (uint32_t v : *out) blobs[side].push_back(v);
            Capture(blobs[side]);
            delete out;
        }
        gRoute = 0;
        SameRoutes(ctx, blobs, "GetNeighborIds", seed);
    }
}

// ---- walks over the neighbours ----
DIFF_TEST(NeighborManager_Walks, 0x4bc340)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds * 2; ++seed) {
        std::mt19937 values(seed + 37);
        const int32_t value = values() % 5 == 0 ? int32_t(values()) : int32_t(values() % 3);
        Pair(ctx, "RemoveNeighborsWithValue43", seed, fNeighbors | (seed % 4 == 0 ? fNullNeighbor : 0),
            [&](std::mt19937 &rng) { gW.removeZeroes = rng() % 4; },
            [&](bool) {
                ROUTE_NA(void(__thiscall *)(Mgr *, int32_t), 0x4bc340, 0, RemoveNeighborsWithAttribute43, value);
                return 0;
            });
        Pair(ctx, "ReportNeighborWords", seed, fNeighbors | (seed % 4 == 0 ? fNullNeighbor : 0), NoSetup, [&](bool) {
            ROUTE0_NA(void(__thiscall *)(Mgr *), 0x4bc090, 0, EnsureSimInventories);
            return 0;
        });
        Pair(ctx, "RefreshFamilyOf", seed, fNeighbors, NoSetup, [&](bool) {
            void *neighbor = &S(Fake(kNb0 + seed % kNeighbors));
            return ROUTE_NA(uint32_t(__thiscall *)(Mgr *, void *), 0x4be0e0, 0, MarkFamilyChanged, neighbor);
        });
        Pair(ctx, "ApplyCollectedIds", seed, 0, [&](std::mt19937 &rng) { gW.collect = rng() % 6; },
            [&](bool) {
                ROUTE_NA(void(__thiscall *)(Mgr *, void *), 0x4bf1f0, 0, AddRelatedIdsToFamily, static_cast<void *>(&S(kTarget)));
                return gW.allocations;
            });
    }
}

DIFF_TEST(NeighborManager_Save, 0x4be580)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds * 2; ++seed) {
        const uint8_t all = uint8_t(seed % 3 == 0 ? 0 : seed % 3 == 1 ? 1 : 0x40 + seed);
        const uint32_t fill = fNeighbors | (seed % 5 == 0 ? fNullNeighbor : 0);
        Pair(ctx, "Save", seed, fill, NoSetup, [&](bool) {
            return ROUTE_NA(uint8_t(__thiscall *)(Mgr *, uint8_t), 0x4be580, 0, Save, all);
        });
        Pair(ctx, "SaveNeighbors", seed, fill, NoSetup, [&](bool) {
            return ROUTE(uint8_t(__thiscall *)(Mgr *, uint8_t), 0x4bfba0, kNoFace, SaveNeighbors, all);
        });
    }
}

DIFF_TEST(NeighborManager_Reset, 0x4bf130)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed)
        Pair(ctx, "Reset", seed, (seed % 4) | (seed % 5 == 0 ? fNullNeighbor : 0), NoSetup, [&](bool original) {
            (void)original;
            ROUTE0_NA(void(__thiscall *)(Mgr *), 0x4bf130, 0, Reset);
            return 0;
        });
}

// ---- relationships between neighbours, the collection members and the per-neighbour queries ----
namespace {
uint32_t gKeys[4];
uint32_t gRecords[4];
struct PointerRange {
    uint32_t *begin;
    uint32_t *end;
} gRange;

uint32_t TableKnown(SObj *self, uint32_t *args)
{
    const uint32_t h = (args[0] * 2654435761u) ^ self->value[0];
    return (h >> 7) % 5 ? 1 + (h & 0xf00) : 0;
}
uint32_t TableState(SObj *self, uint32_t *args)
{
    const uint32_t h = (args[0] * 40503u + args[1] * 977u + self->value[1]) * 2654435761u;
    if (args[1] == 3) return (h >> 11) % 6 == 0 ? (h & 0xff) | 1 : 0;
    return (h >> 9) & ((h >> 3) & 1 ? 0xffffffffu : 0x10101u);
}
uint32_t TargetKey(SObj *, uint32_t *args) { return uint32_t(uintptr_t(&gKeys[args[0] & 3])); }
uint32_t RecordsRange(SObj *, uint32_t *) { return uint32_t(uintptr_t(&gRange)); }
uint32_t RecordFlag(SObj *self, uint32_t *args) { return args[0] == 0x40 ? self->value[0] : self->value[1]; }

void SetupRelations(std::mt19937 &r)
{
    SObj &table = S(kHolder);
    table.value[0] = r();
    table.value[1] = r();
    Set(table, 5, 1, 0, TableKnown);
    Set(table, 9, 2, 0, TableState);
    for (auto &k : gKeys) k = r() % 3 ? r() % 40 : r();
    auto equip = [&](SObj &o) {
        Set(o, 12, 0, r() % 7 ? &table : nullptr);
        Set(o, 15, 0, r() % 3 ? r() % 40 : r());
        Set(o, 36, 0, r() % 4 ? 1 + (r() & 0xff00) : (r() & 0xffffff00));
        Set(o, 42, 1);
        Set(o, 41, 0, r());
    };
    for (unsigned i = kNb0; i <= kNbLast; ++i) equip(gW.fakes[i]);
    SObj &t = S(kTarget);
    equip(t);
    Set(t, 8, 0, r() % 3 ? r() & 0xffff : r());
    Set(t, 24, 1, r() % 4);
    Set(t, 26, 1, 0, TargetKey);
    Set(t, 29, 1, r() % 3 == 0 ? 1 : (r() & 0xffffff00));
    Set(t, 63, 0);
}

void SetupQueries(std::mt19937 &r, bool nullInner)
{
    SetupRelations(r);
    Set(S(kApp), 26, 0, &S(kMessage));
    Set(S(kApp), 23, 0, &S(kPayload));
    Set(S(kPayload), 14, 0, r() % 4 ? &S(kStream) : nullptr);
    Set(S(kStream), 29, 1, r() % 3 ? r() : (r() & 0xffffff00));
    Set(S(kMessage), 34, 1, 0, RecordsRange);
    for (auto &p : gRecords) p = r() % 4 ? uint32_t(uintptr_t(&S(Fake(kKeyObj0 + r() % 3)))) : 0;
    gRange.begin = gRecords;
    gRange.end = gRecords + r() % 5;
    for (unsigned k = kKeyObj0; k <= kKeyObj2; ++k) {
        SObj &o = gW.fakes[k];
        o.value[0] = r() % 3 == 0 ? r() | 1 : (r() & 0xffffff00);
        o.value[1] = r() % 3 == 0 ? r() | 1 : (r() & 0xffffff00);
        Set(o, 98, 0, r() % 2 ? 2 : r() % 4);
        Set(o, 25, 1, 0, RecordFlag);
        Set(o, 47, 0, r());
    }
    Set(S(kInnerA), 28, 0, r());
    Set(S(kFound), 42, 0, r() % 3 ? r() : 0);
    for (unsigned slot : {61u, 67u, 69u, 71u}) Set(S(kFound), slot, 0, r() % 3 ? (r() % 2 ? 1 + (r() & 0xff00) : r()) : (r() & 0xffffff00));
    for (unsigned i = kNb0; i <= kNbLast; ++i) Set(gW.fakes[i], 10, 0, nullInner ? nullptr : &S(kInnerA));
}

void *RelPick(unsigned k) { return k < 12 ? &S(Fake(kNb0 + k)) : k == 12 ? &S(kTarget) : nullptr; }
} // namespace

DIFF_TEST(NeighborManager_Relations, 0x4be670)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds * 4; ++seed) {
        std::mt19937 values(seed + 53);
        const unsigned ia = values() % 14, ib = values() % 14;
        Pair(ctx, "RelationsAllowBasic", seed, 0, SetupRelations, [&](bool original) {
            using Fn = uint8_t(__cdecl *)(void *, void *);
            return original ? ORIG(Fn, 0x4be670)(RelPick(ia), RelPick(ib)) : uint8_t(Mgr::RelationsAllowBasic(RelPick(ia), RelPick(ib)));
        });
        Pair(ctx, "RelationsAllowExtended", seed, 0, SetupRelations, [&](bool original) {
            using Fn = uint8_t(__cdecl *)(void *, void *);
            return original ? ORIG(Fn, 0x4be7f0)(RelPick(ia), RelPick(ib)) : uint8_t(Mgr::RelationsAllowExtended(RelPick(ia), RelPick(ib)));
        });
    }
}

DIFF_TEST(NeighborManager_CountRelated, 0x4bfd00)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds * 3; ++seed) {
        std::mt19937 values(seed + 59);
        const unsigned it = values() % 14, itNonNull = it % 13;
        const uint32_t fill = fNeighbors | (seed % 5 == 0 ? fNullNeighbor : 0);
        Pair(ctx, "CountRelatedNeighbors", seed, fill, SetupRelations, [&](bool) {
            ROUTE(void(__thiscall *)(Mgr *, void *), 0x4bfd00, kNoFace, CountRelatedNeighbors, RelPick(itNonNull));
            return 0;
        });
        Pair(ctx, "CountRelatedNeighborsOf", seed, fill, SetupRelations, [&](bool) {
            ROUTE(void(__thiscall *)(Mgr *, void *), 0x4bf4d0, 0, CountRelatedNeighborsOf, RelPick(it));
            return 0;
        });
    }
}

DIFF_TEST(NeighborManager_CollectRelated, 0x4bcc10)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds * 3; ++seed) {
        std::mt19937 values(seed + 61);
        const uint8_t extended = uint8_t(values() % 3 == 0 ? values() : values() % 2);
        const unsigned start = values() % 4;
        const unsigned capacity = start + values() % 6;
        Blob blobs[kRoutes];
        for (int side = 0; side < kRoutes; ++side) {
            gRoute = side;
            Build(seed, fNeighbors | (seed % 5 == 0 ? fNullNeighbor : 0));
            std::mt19937 rng(seed * 7919u + 5);
            SetupRelations(rng);
            NeighborIdVector *out = new NeighborIdVector();
            out->reserve(capacity);
            for (unsigned i = 0; i < start; ++i) out->push_back(0xeee0 + i);
            gW.events.clear();
            gW.allocations = 0;
            using Fn = int32_t(__thiscall *)(Mgr *, void *, NeighborIdVector *, uint8_t);
            const int32_t size = ROUTE_NA(Fn, 0x4bcc10, 0, CollectRelatedIds, static_cast<void *>(&S(kTarget)), out, extended);
            blobs[side].push_back(uint32_t(size));
            blobs[side].push_back(uint32_t(out->size()));
            blobs[side].push_back(uint32_t(out->capacity()));
            blobs[side].push_back(gW.allocations);
            for (uint32_t v : *out) blobs[side].push_back(v);
            Capture(blobs[side]);
            delete out;
        }
        gRoute = 0;
        SameRoutes(ctx, blobs, "CollectRelatedIds", seed);
    }
}

DIFF_TEST(NeighborManager_QueryNeighbor, 0x4bcd30)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds * 8; ++seed) {
        std::mt19937 values(seed + 67);
        const int32_t id = int32_t(values() % 26) - 3;
        const int32_t query = int32_t(values() % 14) - 1;
        const bool withOut = values() % 4 != 0;
        const bool nullInner = query == 0 && seed % 3 == 0;
        Pair(ctx, "QueryNeighbor", seed, fNeighbors, [&](std::mt19937 &rng) { SetupQueries(rng, nullInner); }, [&](bool) {
            uint32_t out = 0x1234;
            using Fn = uint32_t(__thiscall *)(Mgr *, int32_t, int32_t, uint32_t *);
            const uint32_t result = ROUTE_NA(Fn, 0x4bcd30, 0, QueryNeighbor, id, query, withOut ? &out : nullptr);
            Ev({0x0b7, out});
            return result;
        });
    }
}

// ---- family assignments (slot 25) ----
namespace {
int16_t gFamilyIds[6];
unsigned gFamilyCount;
uint32_t FamilyIdsFill(SObj *, uint32_t *args)
{
    auto *ids = reinterpret_cast<std::vector<int16_t, GZServiceAllocator<int16_t>> *>(args[0]);
    for (unsigned i = 0; i < gFamilyCount; ++i) ids->push_back(gFamilyIds[i]);
    return 0;
}
uint32_t FamiliesFindAny(SObj *, uint32_t *args) { return int32_t(args[0]) % 3 == 0 ? 0u : uint32_t(uintptr_t(&S(kFound))); }

void SetupFamilies(std::mt19937 &r)
{
    SetupRelations(r);
    gFamilyCount = r() % 7;
    for (auto &id : gFamilyIds) id = int16_t(r() % 5 ? int32_t(r() % 20) - 2 : int32_t(r()));
    Set(S(kFamilies), 19, 1, 0, FamilyIdsFill);
    Set(S(kFamilies), 11, 1, 0, FamiliesFindAny);
    Set(S(kFound), 24, 1, r() % 4);
    Set(S(kFound), 26, 1, 0, TargetKey);
    Set(S(kFound), 41, 0, r());
    for (unsigned i = kNb0; i <= kNbLast; ++i) {
        SObj &n = gW.fakes[i];
        Set(n, 17, 2);
        Set(n, 8, 0, r() % 3 ? uint32_t(int32_t(r() % 34) - 3) | (r() % 2 ? 0 : 0xabcd0000u) : r());
    }
}
} // namespace

DIFF_TEST(NeighborManager_RebuildFamilyAssignments, 0x4bf290)
{
    Globals globals(ctx);
    for (uint32_t seed = 0; seed < kSeeds * 4; ++seed)
        Pair(ctx, "RebuildFamilyAssignments", seed, fNeighbors | (seed % 5 == 0 ? fNullNeighbor : 0), SetupFamilies, [&](bool) {
            ROUTE0_NA(void(__thiscall *)(Mgr *), 0x4bf290, 0, RebuildFamilyAssignments);
            return gW.allocations;
        });
}
