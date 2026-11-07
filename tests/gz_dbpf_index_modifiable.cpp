// Differential tests for cGZDBPFIndexModifiable in the standalone shape (src/gz/cGZDBPFIndexModifiable.h).
//
// Routes (docs/standalone-shape.md, "Tests"; tests/shape.h):
// - original: an object built by the original constructor 0x9a6cf0 (game vtable), the function called at its RVA;
// - vtable:   an object built by our constructor through the constructor hook's adapter, called through its own vtable, in
//             the slot where the game's vtable holds the RVA (a slot-order mistake calls the wrong function);
// - adapter:  only for the slots that keep an RVA hook (the byte-matched ones): an object built by the original constructor,
//             the adapter called, with an arity-matched trap in the slot under test.
// The map's nodes and buckets come from RZMalloc: both the image's RZMalloc / RZFree and our RZMemory use one test
// allocator (AllocScope). The state (the records in list order, the buckets as list indices, the size, mask, bucket count
// and maximum load factor) is installed into each object by inserting the records in order with our std::unordered_map
// (the same STL as the game's) and compared by walking the map the same way after the call; only the vtable word is
// normalised. The key map and filter fakes are the compressed index test's (tests/gz_dbpf_index_compressed.cpp).
#include "difftest.h"
#include "free_recorder.h"
#include "code_patch.h"
#include "game_allocator.h"
#include "gz/cGZDBPFIndexModifiable.h"
#include "rz/GeneralAllocator.h"
#include "rz/RZMemory.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <random>
#include <set>
#include <type_traits>
#include <vector>

namespace {

using Self = cGZDBPFIndexModifiable;
using Key = cGZPersistResourceKey;
using Record = cGZDBPFIndexRecord;
using A = cGZDBPFIndexModifiableAdapters;
using shape::P;

constexpr uint32_t kVTable = 0xe5ff40;      // cGZDBPFIndexModifiable
constexpr uint32_t kIfaceVTable = 0xe5fe14; // cIGZDBPFIndex (what the original destructor leaves)
constexpr unsigned kSlots = 24;
constexpr uint32_t kCtor = 0x9a6cf0;
constexpr uint32_t kFlag = 0x80000000u;

// ---- the allocator behind RZMalloc on both sides ----

GeneralAllocator *gAlloc = nullptr;
int gRzFrees = 0;

void *__cdecl TestMalloc(uint32_t bytes)
{
    return gAlloc->Malloc(bytes, 0);
}

void __cdecl TestFree(void *mem)
{
    ++gRzFrees;
    gAlloc->Free(mem);
}

struct AllocScope {
    shape::CodePatch malloc_{0x1be3b0, reinterpret_cast<const void *>(&TestMalloc)};
    shape::CodePatch free_{0x1be3a1, reinterpret_cast<const void *>(&TestFree)};
    alignas(16) uint8_t memory[sizeof(GeneralAllocator)] = {};
    AllocScope()
    {
        gAlloc = reinterpret_cast<GeneralAllocator *>(memory);
        gAlloc->Construct(nullptr, 0, 0, 0, nullptr, nullptr);
        RZMemory::testGlobalAllocator = gAlloc;
    }
    AllocScope(const AllocScope &) = delete;
    ~AllocScope()
    {
        RZMemory::testGlobalAllocator = nullptr;
        difftest_alloc::ShutdownAndRelease(gAlloc);
        gAlloc = nullptr;
    }
};

enum class Route { Original, VTable, Adapter };
const char *RouteName(Route r) { return r == Route::Original ? "original" : r == Route::VTable ? "vtable" : "adapter"; }

// ---- objects ----

void **OurVTable() { return shape::Ours<Self, 1>({0}).face[0]; }
void **OurDestroyed() { return shape::Ours<Self, 1>({0}).destroyed; }

uint8_t *Build(difftest::Context &ctx, Route route, uint32_t fill)
{
    auto *mem = static_cast<uint8_t *>(::operator new(sizeof(Self)));
    for (size_t i = 0; i < sizeof(Self); ++i) mem[i] = static_cast<uint8_t>(fill >> (8 * (i & 3)));
    if (route == Route::VTable)
        A::Construct(reinterpret_cast<Self *>(mem), nullptr);
    else
        ctx.Original<void *(__thiscall *)(void *)>(kCtor)(mem);
    return mem;
}

Self *AsSelf(uint8_t *mem) { return reinterpret_cast<Self *>(mem); }

// Frees the map without either side's destructor (only the destructor's own test runs it).
void Release(uint8_t *mem)
{
    AsSelf(mem)->mMap.~unordered_map();
    ::operator delete(mem, sizeof(Self));
}

struct State {
    std::vector<std::pair<Key, Record>> records; // inserted in this order
    float maxLoad = 1.0f;
    uint32_t rehash = 0; // 0: none
};

void Install(uint8_t *mem, const State &s)
{
    Self::Map &map = AsSelf(mem)->mMap;
    if (s.maxLoad != 1.0f) map.max_load_factor(s.maxLoad);
    if (s.rehash != 0) map.rehash(s.rehash);
    for (const auto &r : s.records) map.try_emplace(r.first, r.second);
}

// ---- traps for the adapter route ----

int gTrapHits = 0;
void *gTrapTable[kSlots];

uint32_t VtCode(uint32_t word)
{
    if (word == P(shape::GameVTable(kVTable)) || word == P(OurVTable()) || word == P(gTrapTable)) return 0xd0000000u;
    if (word == P(shape::GameVTable(kIfaceVTable)) || word == P(OurDestroyed())) return 0xd0000001u;
    return word;
}

unsigned SlotFor(difftest::Context &ctx, uint32_t rva)
{
    const unsigned slot = shape::SlotOf(kVTable, kSlots, rva);
    if (slot == ~0u) ctx.Fail("0x%x is not in the vtable", rva);
    return slot;
}

// Calls the function at `rva` on `mem` by the route: the original at its RVA, our vtable's slot (`slot`, or where the game's
// vtable first holds the RVA), or the adapter.
template <class Fn, class... Args>
auto Invoke(difftest::Context &ctx, Route route, uint8_t *mem, uint32_t rva, void *adapter, Args... args)
{
    using R = typename shape::Sig<Fn>::Result;
    if (route == Route::Original) return ctx.Original<Fn>(rva)(mem, args...);
    const unsigned slot = SlotFor(ctx, rva);
    if (route == Route::VTable) return reinterpret_cast<Fn>(shape::VTableAt(mem, 0)[slot])(mem, args...);
    std::memcpy(gTrapTable, shape::GameVTable(kVTable), sizeof(gTrapTable));
    gTrapTable[slot] = shape::TrapFor<shape::Count<gTrapHits>>(shape::Sig<Fn>::kWords);
    shape::VTableAt(mem, 0) = gTrapTable;
    gTrapHits = 0;
    auto after = [&] {
        if (shape::VTableAt(mem, 0) == gTrapTable) shape::VTableAt(mem, 0) = shape::GameVTable(kVTable);
        if (gTrapHits != 0) ctx.Fail("0x%x: the adapter dispatched through the vtable (%d trap hits)", rva, gTrapHits);
    };
    if constexpr (std::is_void_v<R>) {
        reinterpret_cast<Fn>(adapter)(mem, args...);
        after();
    } else {
        R r = reinterpret_cast<Fn>(adapter)(mem, args...);
        after();
        return r;
    }
}

// ---- what is compared ----

// The map's words at +4: max load factor, list head, size, bucket vector (begin, end, capacity end), mask, bucket count.
struct MapWords {
    uint32_t maxLoad;
    uint8_t *head;
    uint32_t size;
    uint8_t **bucketsBegin, **bucketsEnd, **bucketsCap;
    uint32_t mask, bucketCount;
};
static_assert(sizeof(MapWords) == 0x20, "map words");

const MapWords &Words(uint8_t *mem) { return *reinterpret_cast<const MapWords *>(mem + 4); }

struct Snap {
    uint32_t vt = 0;
    std::vector<uint32_t> nodes;   // each node's 7 value words, in list order
    std::vector<uint32_t> buckets; // each bucket word as a list index (0xffffffff: the head)
    uint32_t header[5] = {};       // max load, size, bucket capacity, mask, bucket count
    bool operator==(const Snap &o) const
    {
        return vt == o.vt && nodes == o.nodes && buckets == o.buckets && std::memcmp(header, o.header, sizeof(header)) == 0;
    }
};

// Each node's place in the list, the head as 0xffffffff.
std::map<const uint8_t *, uint32_t> Places(uint8_t *mem)
{
    std::map<const uint8_t *, uint32_t> at;
    const MapWords &w = Words(mem);
    at[w.head] = 0xffffffffu;
    uint32_t i = 0;
    for (uint8_t *n = *reinterpret_cast<uint8_t **>(w.head); n != w.head && i < 100000; n = *reinterpret_cast<uint8_t **>(n)) at[n] = i++;
    return at;
}

Snap TakeSnap(uint8_t *mem)
{
    Snap s;
    s.vt = VtCode(shape::Word(mem, 0));
    const MapWords &w = Words(mem);
    const auto at = Places(mem);
    for (uint8_t *n = *reinterpret_cast<uint8_t **>(w.head); n != w.head; n = *reinterpret_cast<uint8_t **>(n)) {
        const auto *v = reinterpret_cast<const uint32_t *>(n + 8);
        s.nodes.insert(s.nodes.end(), v, v + 7);
        // The back link, as a place.
        const auto prev = at.find(*reinterpret_cast<uint8_t **>(n + 4));
        s.nodes.push_back(prev == at.end() ? 0xeeeeeeeeu : prev->second);
    }
    for (uint8_t **b = w.bucketsBegin; b != w.bucketsEnd; ++b) {
        const auto it = at.find(*b);
        s.buckets.push_back(it == at.end() ? 0xeeeeeeeeu : it->second);
    }
    s.header[0] = w.maxLoad;
    s.header[1] = w.size;
    s.header[2] = static_cast<uint32_t>(w.bucketsCap - w.bucketsBegin);
    s.header[3] = w.mask;
    s.header[4] = w.bucketCount;
    return s;
}

// A result word: a pointer into a node as (place, offset), into the object as an offset, everything else as is.
uint32_t NormPtr(uint8_t *mem, uint32_t value)
{
    if (value == 0) return 0;
    if (value >= P(mem) && value < P(mem) + sizeof(Self)) return 0xa0000000u | (value - P(mem));
    for (const auto &[node, place] : Places(mem))
        if (value >= P(node) && value < P(node) + 0x24) return 0xb0000000u | (place << 8) | (value - P(node));
    return value;
}

// ---- fakes: the key map (slots 3, 4) and the key filter (slot 3), recording into gLog ----

std::vector<uint32_t> gLog;

struct Fake {
    void **vt;
};

void __fastcall MapToCaller(Fake *, void *, uint32_t *in, uint32_t *out)
{
    const uint32_t k[4] = {in[0], in[1], in[2], in[3]};
    gLog.insert(gLog.end(), {3u, P(in) == P(out) ? 1u : 0u, k[0], k[1], k[2], k[3]});
    out[0] = k[0];
    out[1] = k[1] ^ 0x10u;
    out[2] = k[2] + 0x100u;
    out[3] = k[3] ^ 0x55u;
}

void __fastcall MapToFile(Fake *, void *, uint32_t *in, uint32_t *out)
{
    const uint32_t k[4] = {in[0], in[1], in[2], in[3]};
    gLog.insert(gLog.end(), {4u, P(in) == P(out) ? 1u : 0u, k[0], k[1], k[2], k[3]});
    out[0] = k[0];
    out[1] = k[1] & 0x7u; // merges groups (keys collide: the second is not inserted)
    out[2] = k[2] ^ 1u;
    out[3] = k[3];
}

// Answers in eax; only al counts (0x100 is false, 0x101 true).
uint32_t __fastcall Include(Fake *, void *, const uint32_t *key)
{
    gLog.insert(gLog.end(), {5u, key[0], key[1], key[2], key[3]});
    const uint32_t h = (key[0] * 31u + key[1] * 17u + key[2] * 2654435761u + key[3]) >> 7;
    static const uint32_t answers[4] = {0, 1, 0x100, 0x101};
    return answers[h & 3];
}

void *gKeyMapTable[5] = {nullptr, nullptr, nullptr, reinterpret_cast<void *>(&MapToCaller), reinterpret_cast<void *>(&MapToFile)};
void *gFilterTable[4] = {nullptr, nullptr, nullptr, reinterpret_cast<void *>(&Include)};
Fake gKeyMap{gKeyMapTable};
Fake gFilter{gFilterTable};

// ---- inputs ----

using Rng = std::mt19937;

uint32_t Pick(Rng &rng, std::initializer_list<uint32_t> values)
{
    return values.begin()[rng() % values.size()];
}

// Index entries: few types, groups and instances (so instances share buckets and keys differ only in their resource),
// duplicate keys, resources mostly 0, sizes and memory sizes around each other.
std::vector<cGZDBPFIndexEntry> MakeInput(Rng &rng)
{
    const uint32_t n = rng() % 8 == 0 ? rng() % 3 : (rng() % 4 == 0 ? 40 + rng() % 160 : rng() % 40);
    const uint32_t nTypes = 1 + rng() % 4, nGroups = 1 + rng() % 4, nInstances = 1 + rng() % (rng() % 2 == 0 ? 6 : 100);
    std::vector<uint32_t> types(nTypes), groups(nGroups), instances(nInstances);
    for (auto &t : types) t = rng() % 5 == 0 ? Self::kDirectoryType : (rng() % 3 == 0 ? rng() % 16 : static_cast<uint32_t>(rng()));
    for (auto &g : groups) g = rng() % 3 == 0 ? rng() % 16 : static_cast<uint32_t>(rng());
    for (auto &i : instances) i = rng() % 3 == 0 ? rng() % 64 : static_cast<uint32_t>(rng());
    std::vector<cGZDBPFIndexEntry> in(n);
    for (auto &e : in) {
        e.type = types[rng() % nTypes];
        e.group = groups[rng() % nGroups];
        e.instance = instances[rng() % nInstances];
        e.resource = rng() % 2 == 0 ? 0 : rng() % 4;
        e.offset = rng() % 4 == 0 ? static_cast<uint32_t>(rng()) : rng() % 0x100000;
        e.size = rng() % 6 == 0 ? 0 : (rng() % 5 == 0 ? static_cast<uint32_t>(rng()) : rng() % 0x4000);
        switch (rng() % 5) {
        case 0: e.memSize = e.size; break;
        case 1: e.memSize = e.size | kFlag; break;
        case 2: e.memSize = static_cast<uint32_t>(rng()); break;
        default: e.memSize = e.size + rng() % 0x8000 - (rng() % 4 == 0 ? 0x10 : 0) + (rng() % 2 == 0 ? kFlag : 0); break;
        }
    }
    return in;
}

State MakeState(Rng &rng)
{
    State s;
    for (const cGZDBPFIndexEntry &e : MakeInput(rng))
        s.records.push_back({Key{e.type, e.group, e.instance, e.resource}, Record{e.offset, e.size, rng() % 3 == 0 ? e.memSize & ~kFlag : e.memSize}});
    if (rng() % 4 == 0) s.maxLoad = Pick(rng, {0, 1, 2}) == 0 ? 0.5f : 4.0f;
    if (rng() % 4 == 0) s.rehash = Pick(rng, {1, 8, 64, 100, 512});
    return s;
}

// ---- the route runner ----

struct Outcome {
    uint64_t result = 0;
    Snap snap;
    std::vector<uint32_t> log;
    std::vector<uint32_t> extra; // the call's out-parameters, as the test reads them
};

void Compare(difftest::Context &ctx, const char *name, Route route, const Outcome &orig, const Outcome &ours)
{
    ++ctx.cases;
    if (orig.result != ours.result)
        ctx.Fail("%s (%s): result original 0x%llx, ours 0x%llx", name, RouteName(route),
                 static_cast<unsigned long long>(orig.result), static_cast<unsigned long long>(ours.result));
    if (!(orig.snap == ours.snap)) ctx.Fail("%s (%s): object state differs", name, RouteName(route));
    if (orig.log != ours.log) ctx.Fail("%s (%s): key map / filter calls differ (%zu vs %zu words)", name, RouteName(route), orig.log.size(), ours.log.size());
    if (orig.extra != ours.extra) ctx.Fail("%s (%s): out-parameters differ", name, RouteName(route));
}

// Runs `call(route, mem, extra)` on a fresh object of each route holding `state` (the adapter route only when `adapter`).
template <class Call> void RunRoutes(difftest::Context &ctx, const char *name, const State &state, bool adapter, Call call)
{
    const Route routes[] = {Route::Original, Route::VTable, Route::Adapter};
    Outcome out[3];
    const int n = adapter ? 3 : 2;
    for (int i = 0; i < n; ++i) {
        uint8_t *mem = Build(ctx, routes[i], 0x3c3c3c3cu);
        Install(mem, state);
        gLog.clear();
        out[i].result = call(routes[i], mem, out[i].extra);
        out[i].log = gLog;
        out[i].snap = TakeSnap(mem);
        Release(mem);
    }
    for (int i = 1; i < n; ++i) Compare(ctx, name, routes[i], out[0], out[i]);
}

using U32Fn = uint32_t(__thiscall *)(void *);
using BoolArgsFn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t);
using SetFn = void(__thiscall *)(void *, std::set<uint32_t> *);
using KeyListFn = uint32_t(__thiscall *)(void *, cGZDBPFResourceKeyList *, void *);
using FindFn = uint32_t(__thiscall *)(void *, const Key *, uint32_t, uint32_t);
using AddFn = void(__thiscall *)(void *, const Key *, const Record *);
using RemoveFn = uint8_t(__thiscall *)(void *, const Key *, Record *, uint32_t, uint32_t);
using SetEntriesFn = uint8_t(__thiscall *)(void *, const cGZDBPFIndexEntry *, uint32_t, void *);
using CopyFn = void(__thiscall *)(void *, cGZDBPFIndexEntry *, void *);
using FromIndexFn = uint8_t(__thiscall *)(void *, const uint32_t *, uint32_t, void *, uint32_t);
using WriteFn = void(__thiscall *)(void *, std::vector<uint32_t> *, void *, uint32_t);
using DtorFn = void *(__thiscall *)(void *, uint32_t);

// The slots that keep an RVA hook (byte matches); null for the others.
void *Adapter(uint32_t rva)
{
    switch (rva) {
    case 0x9acc10: return reinterpret_cast<void *>(&A::GetTotalSize);
    case 0x9ac720: return reinterpret_cast<void *>(&A::GetSerializedSize);
    default: return nullptr;
    }
}

// Keys to look up: a record's own key, the same with the resource changed or zeroed, the instance changed, random keys.
Key PickKey(Rng &rng, const State &s)
{
    Key k{static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng()), 0};
    if (s.records.empty() || rng() % 8 == 0) return k;
    k = s.records[rng() % s.records.size()].first;
    switch (rng() % 6) {
    case 0: k.resource = 0; break;
    case 1: k.resource = rng() % 2 == 0 ? k.resource + 1 : rng() % 4; break;
    case 2: k.instance += 1; break;
    default: break;
    }
    return k;
}

uint32_t PickFlag(Rng &rng)
{
    return Pick(rng, {0, 1, 0x100, 0x101, 0xffffff00u, 0x80});
}

uint32_t PickBound(Rng &rng, const State &s)
{
    const uint32_t kind = rng() % 9;
    if (!s.records.empty() && kind < 4) {
        const Record &r = s.records[rng() % s.records.size()].second;
        const uint32_t base = kind < 2 ? r.offset : r.offset + r.size;
        return base + static_cast<uint32_t>(static_cast<int32_t>(rng() % 3) - 1);
    }
    return Pick(rng, {0, 0xffffffffu, 0x80000000u, rng() % 0x300, static_cast<uint32_t>(rng())});
}

} // namespace

// ---- constructor and destructor ----

DIFF_TEST(cGZDBPFIndexModifiable_Construct, 0x9a6cf0)
{
    // An empty map (head node, 8 buckets, load factor 1.0): compared through the snapshot (the byte-level comparison in a
    // fixed-address world is cGZDBSegmentPackedFile_RecordStoreConstruct, tests/gz_dbsegment_packed_file_io_ranges.cpp).
    AllocScope alloc;
    for (int i = 0; i < 16; ++i) {
        uint8_t *orig = Build(ctx, Route::Original, 0x01010101u * i);
        uint8_t *ours = Build(ctx, Route::VTable, 0x01010101u * i);
        ++ctx.cases;
        if (VtCode(shape::Word(orig, 0)) != 0xd0000000u || VtCode(shape::Word(ours, 0)) != 0xd0000000u) ctx.Fail("Construct: vtable word");
        if (shape::Word(ours, 0) == shape::Word(orig, 0)) ctx.Fail("Construct: our object carries the game's vtable");
        if (!(TakeSnap(orig) == TakeSnap(ours))) ctx.Fail("Construct: map differs");
        Release(orig);
        Release(ours);
    }
}

DIFF_TEST(cGZDBPFIndexModifiable_DeletingDestructor, 0x9a7830)
{
    // Slot 0 with flags 0 (the object stays: its first word compared, normalised) and 1 (freed): the result, the number of
    // RZFree calls (nodes, buckets, head; ours counted by the same stand-in patched over our RZMemory::Free) and the heap
    // frees (the object).
    AllocScope alloc;
    shape::CodePatch ourFree(reinterpret_cast<void *>(&RZMemory::Free), reinterpret_cast<const void *>(&TestFree));
    Rng rng(0x0dbf2100u);
    for (int i = 0; i < 300; ++i) {
        const State s = MakeState(rng);
        const uint32_t flags = Pick(rng, {0, 1, 2, 3, 0x100});
        size_t sizes[2][4] = {};
        size_t counts[2] = {};
        int rzFrees[2] = {};
        uint32_t results[2] = {}, vts[2] = {};
        for (int r = 0; r < 2; ++r) {
            uint8_t *mem = Build(ctx, r == 0 ? Route::Original : Route::VTable, 0x5a5a5a5au);
            Install(mem, s);
            gRzFrees = 0;
            void *result;
            {
                difftest::FreeRecorder rec;
                result = Invoke<DtorFn>(ctx, r == 0 ? Route::Original : Route::VTable, mem, 0x9a7830, nullptr, flags);
                rec.Stop();
                counts[r] = rec.SortedSizes(sizes[r], 4);
            }
            rzFrees[r] = gRzFrees;
            results[r] = P(result) == P(mem) ? 1u : P(result);
            if ((flags & 1) == 0) {
                vts[r] = VtCode(shape::Word(mem, 0));
                ::operator delete(mem, sizeof(Self));
            }
        }
        ++ctx.cases;
        if (results[1] != results[0]) ctx.Fail("DeletingDestructor: result");
        if (rzFrees[1] != rzFrees[0]) ctx.Fail("DeletingDestructor: %d RZFree calls, ours %d", rzFrees[0], rzFrees[1]);
        if (counts[1] != counts[0] || std::memcmp(sizes[1], sizes[0], sizeof(sizes[0])) != 0) ctx.Fail("DeletingDestructor (flags %u): heap frees differ", flags);
        if (vts[1] != vts[0]) ctx.Fail("DeletingDestructor (flags %u): vtable word 0x%x, ours 0x%x", flags, vts[0], vts[1]);
    }
}

// ---- getters, bounds, sets ----

namespace {

void RunGetter(difftest::Context &ctx, const char *name, uint32_t rva, int trials)
{
    Rng rng(0x0dbf2200u + rva);
    for (int i = 0; i < trials; ++i) {
        const State s = MakeState(rng);
        RunRoutes(ctx, name, s, Adapter(rva) != nullptr, [&](Route route, uint8_t *mem, std::vector<uint32_t> &) {
            return uint64_t(Invoke<U32Fn>(ctx, route, mem, rva, Adapter(rva)));
        });
    }
}

void RunBounds(difftest::Context &ctx, const char *name, uint32_t rva, bool pairEnd)
{
    Rng rng(0x0dbf2300u + rva);
    for (int i = 0; i < 800; ++i) {
        const State s = MakeState(rng);
        for (int j = 0; j < 4; ++j) {
            uint32_t a = PickBound(rng, s), b = PickBound(rng, s);
            if (!s.records.empty() && rng() % 3 == 0) {
                const Record &r = s.records[rng() % s.records.size()].second;
                const uint32_t d = static_cast<uint32_t>(static_cast<int32_t>(rng() % 3) - 1);
                b = pairEnd ? r.offset + r.size + d : r.offset - a + d;
            }
            RunRoutes(ctx, name, s, Adapter(rva) != nullptr, [&](Route route, uint8_t *mem, std::vector<uint32_t> &) {
                return uint64_t(Invoke<BoolArgsFn>(ctx, route, mem, rva, Adapter(rva), a, b));
            });
        }
    }
}

void RunCollect(difftest::Context &ctx, const char *name, uint32_t rva)
{
    Rng rng(0x0dbf2400u + rva);
    for (int i = 0; i < 500; ++i) {
        const State s = MakeState(rng);
        std::set<uint32_t> before;
        for (uint32_t n = rng() % 4; n != 0; --n) before.insert(rng() % 3 == 0 ? rng() % 16 : static_cast<uint32_t>(rng()));
        RunRoutes(ctx, name, s, false, [&](Route route, uint8_t *mem, std::vector<uint32_t> &extra) {
            std::set<uint32_t> set = before;
            Invoke<SetFn>(ctx, route, mem, rva, nullptr, &set);
            extra.assign(set.begin(), set.end());
            extra.push_back(static_cast<uint32_t>(set.size()));
            return uint64_t(0);
        });
    }
}

} // namespace

DIFF_TEST(cGZDBPFIndexModifiable_GetTotalSize, 0x9acc10)
{
    AllocScope alloc;
    RunGetter(ctx, "GetTotalSize", 0x9acc10, 500);
}

DIFF_TEST(cGZDBPFIndexModifiable_GetTotalMemSize, 0x9acca0)
{
    AllocScope alloc;
    RunGetter(ctx, "GetTotalMemSize", 0x9acca0, 500);
}

DIFF_TEST(cGZDBPFIndexModifiable_GetDataEnd, 0x9abc10)
{
    AllocScope alloc;
    RunGetter(ctx, "GetDataEnd", 0x9abc10, 500);
}

DIFF_TEST(cGZDBPFIndexModifiable_GetSerializedSize, 0x9ac720)
{
    AllocScope alloc;
    RunGetter(ctx, "GetSerializedSize", 0x9ac720, 300);
}

DIFF_TEST(cGZDBPFIndexModifiable_AllEntriesWithin, 0x9b0ac0)
{
    AllocScope alloc;
    RunBounds(ctx, "AllEntriesWithin", 0x9b0ac0, true);
}

DIFF_TEST(cGZDBPFIndexModifiable_IsRangeFree, 0x9b0bd0)
{
    AllocScope alloc;
    RunBounds(ctx, "IsRangeFree", 0x9b0bd0, false);
}

DIFF_TEST(cGZDBPFIndexModifiable_CollectTypes, 0x9acd90)
{
    AllocScope alloc;
    RunCollect(ctx, "CollectTypes", 0x9acd90);
}

DIFF_TEST(cGZDBPFIndexModifiable_CollectGroups, 0x9abf10)
{
    AllocScope alloc;
    RunCollect(ctx, "CollectGroups", 0x9abf10);
}

// ---- slot 10: the key list ----

DIFF_TEST(cGZDBPFIndexModifiable_GetResourceKeyList, 0x9ac300)
{
    AllocScope alloc;
    svcfx::AllocatorScope allocator;
    Rng rng(0x0dbf2500u);
    for (int i = 0; i < 600; ++i) {
        State s = MakeState(rng);
        if (rng() % 3 == 0)
            for (auto &r : s.records) r.second.offset = rng() % 8;
        const uint32_t listKind = rng() % 3;
        const uint32_t preset = rng() % 5;
        const bool filter = rng() % 2 == 0;
        RunRoutes(ctx, "GetResourceKeyList", s, false, [&](Route route, uint8_t *mem, std::vector<uint32_t> &extra) {
            cGZDBPFResourceKeyList list;
            for (uint32_t k = 0; k < preset; ++k) list.push_back({k, k + 1, k + 2, k + 3});
            const uint32_t r = Invoke<KeyListFn>(ctx, route, mem, 0x9ac300, nullptr, listKind == 0 ? nullptr : &list,
                                                 filter ? &gFilter : nullptr);
            if (listKind != 0) {
                const auto *w = reinterpret_cast<const uint32_t *>(list.data());
                extra.assign(w, w + list.size() * 4);
                extra.push_back(static_cast<uint32_t>(list.capacity()));
            }
            return uint64_t(r);
        });
    }
}

// ---- slots 11 and 12 (one function), 13, 14 ----

DIFF_TEST(cGZDBPFIndexModifiable_FindRecord, 0x9ab220)
{
    // The game's vtable holds 0x9ab220 in slots 11 and 12: our side is called through both.
    AllocScope alloc;
    Rng rng(0x0dbf2600u);
    for (int i = 0; i < 1000; ++i) {
        const State s = MakeState(rng);
        for (int j = 0; j < 4; ++j) {
            const Key key = PickKey(rng, s);
            const uint32_t a = PickFlag(rng), b = PickFlag(rng);
            for (unsigned slot : {11u, 12u}) {
                RunRoutes(ctx, slot == 11 ? "FindRecord" : "FindRecordForWrite", s, false, [&](Route route, uint8_t *mem, std::vector<uint32_t> &) {
                    if (route == Route::Original) return uint64_t(NormPtr(mem, ctx.Original<FindFn>(0x9ab220)(mem, &key, a, b)));
                    return uint64_t(NormPtr(mem, reinterpret_cast<FindFn>(shape::VTableAt(mem, 0)[slot])(mem, &key, a, b)));
                });
            }
        }
    }
}

DIFF_TEST(cGZDBPFIndexModifiable_AddRecord, 0x9a7cd0)
{
    // Present and absent keys, several adds in a row (rehashes as the map grows).
    AllocScope alloc;
    Rng rng(0x0dbf2700u);
    for (int i = 0; i < 600; ++i) {
        const State s = MakeState(rng);
        std::vector<std::pair<Key, Record>> adds(1 + rng() % 12);
        for (auto &add : adds) {
            add.first = PickKey(rng, s);
            add.second = Record{static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng())};
        }
        RunRoutes(ctx, "AddRecord", s, false, [&](Route route, uint8_t *mem, std::vector<uint32_t> &) {
            for (const auto &add : adds) Invoke<AddFn>(ctx, route, mem, 0x9a7cd0, nullptr, &add.first, &add.second);
            return uint64_t(0);
        });
    }
}

DIFF_TEST(cGZDBPFIndexModifiable_RemoveRecord, 0x9a9320)
{
    AllocScope alloc;
    Rng rng(0x0dbf2800u);
    for (int i = 0; i < 1000; ++i) {
        const State s = MakeState(rng);
        std::vector<Key> keys(1 + rng() % 4);
        for (Key &k : keys) k = PickKey(rng, s);
        const uint32_t a = PickFlag(rng), b = PickFlag(rng);
        RunRoutes(ctx, "RemoveRecord", s, false, [&](Route route, uint8_t *mem, std::vector<uint32_t> &extra) {
            uint64_t results = 0;
            for (const Key &k : keys) {
                Record removed{0x11111111u, 0x22222222u, 0x33333333u};
                results = results << 8 | Invoke<RemoveFn>(ctx, route, mem, 0x9a9320, nullptr, &k, &removed, a, b);
                extra.insert(extra.end(), {removed.offset, removed.size, removed.memSize});
            }
            return results;
        });
    }
}

// ---- slots 18 and 20: adding entries ----

DIFF_TEST(cGZDBPFIndexModifiable_SetEntries, 0x9ae1d0)
{
    // On an empty or filled map (not cleared), with and without the key map (its slot 4 merges groups: keys collide).
    AllocScope alloc;
    Rng rng(0x0dbf2900u);
    for (int i = 0; i < 500; ++i) {
        const std::vector<cGZDBPFIndexEntry> in = MakeInput(rng);
        const bool map = rng() % 2 == 0;
        const State s = rng() % 2 == 0 ? State{} : MakeState(rng);
        RunRoutes(ctx, "SetEntries", s, false, [&](Route route, uint8_t *mem, std::vector<uint32_t> &) {
            return uint64_t(Invoke<SetEntriesFn>(ctx, route, mem, 0x9ae1d0, nullptr, in.empty() ? nullptr : in.data(),
                                                 static_cast<uint32_t>(in.size()), map ? &gKeyMap : nullptr));
        });
    }
}

DIFF_TEST(cGZDBPFIndexModifiable_SetEntriesFromIndex, 0x9aece0)
{
    AllocScope alloc;
    Rng rng(0x0dbf2a00u);
    for (int i = 0; i < 500; ++i) {
        const std::vector<cGZDBPFIndexEntry> in = MakeInput(rng);
        const uint32_t version = Pick(rng, {0, 1, 2, 2, 3});
        std::vector<uint32_t> words;
        for (const auto &e : in) {
            words.insert(words.end(), {e.type, e.group, e.instance});
            if (version == 2) words.push_back(e.resource);
            words.insert(words.end(), {e.offset, e.size});
        }
        const bool map = rng() % 2 == 0;
        const State s = rng() % 2 == 0 ? State{} : MakeState(rng);
        RunRoutes(ctx, "SetEntriesFromIndex", s, false, [&](Route route, uint8_t *mem, std::vector<uint32_t> &) {
            return uint64_t(Invoke<FromIndexFn>(ctx, route, mem, 0x9aece0, nullptr, words.empty() ? nullptr : words.data(),
                                                static_cast<uint32_t>(in.size()), map ? &gKeyMap : nullptr, version));
        });
    }
}

// ---- slots 19, 21, 22, 23 ----

DIFF_TEST(cGZDBPFIndexModifiable_CopyEntries, 0x9b1030)
{
    AllocScope alloc;
    Rng rng(0x0dbf2b00u);
    for (int i = 0; i < 600; ++i) {
        const State s = MakeState(rng);
        const bool map = rng() % 2 == 0;
        const uint32_t fill = static_cast<uint32_t>(rng());
        RunRoutes(ctx, "CopyEntries", s, false, [&](Route route, uint8_t *mem, std::vector<uint32_t> &extra) {
            std::vector<cGZDBPFIndexEntry> out(s.records.size() + 2);
            for (auto &e : out) std::memset(&e, static_cast<int>(fill & 0xff), sizeof(e));
            Invoke<CopyFn>(ctx, route, mem, 0x9b1030, nullptr, out.data(), map ? &gKeyMap : nullptr);
            const auto *w = reinterpret_cast<const uint32_t *>(out.data());
            extra.assign(w, w + out.size() * 7);
            return uint64_t(0);
        });
    }
}

namespace {

void RunWrite(difftest::Context &ctx, const char *name, uint32_t rva, bool presizeOther)
{
    Rng rng(0x0dbf2c00u + rva);
    for (int i = 0; i < 600; ++i) {
        const State s = MakeState(rng);
        const bool map = rng() % 2 == 0;
        const uint32_t version = Pick(rng, {1, 1, 2, 2, 0, 3});
        uint32_t preset = rng() % 3 == 0 ? rng() % 0x50 : 0;
        if (presizeOther && version != 1 && version != 2) preset = static_cast<uint32_t>(s.records.size() * 5 + 3);
        RunRoutes(ctx, name, s, false, [&](Route route, uint8_t *mem, std::vector<uint32_t> &extra) {
            std::vector<uint32_t> out;
            for (uint32_t k = 0; k < preset; ++k) out.push_back(0xee000000u + k);
            Invoke<WriteFn>(ctx, route, mem, rva, nullptr, &out, map ? &gKeyMap : nullptr, version);
            extra = out;
            extra.push_back(static_cast<uint32_t>(out.capacity()));
            return uint64_t(0);
        });
    }
}

} // namespace

DIFF_TEST(cGZDBPFIndexModifiable_WriteIndexEntries, 0x9b1630)
{
    AllocScope alloc;
    RunWrite(ctx, "WriteIndexEntries", 0x9b1630, true);
}

DIFF_TEST(cGZDBPFIndexModifiable_WriteCompressionDirectory, 0x9b1320)
{
    AllocScope alloc;
    RunWrite(ctx, "WriteCompressionDirectory", 0x9b1320, false);
}

DIFF_TEST(cGZDBPFIndexModifiable_SetFlagOnAll, 0x9ad810)
{
    AllocScope alloc;
    Rng rng(0x0dbf2d00u);
    for (int i = 0; i < 300; ++i) {
        const State s = MakeState(rng);
        RunRoutes(ctx, "SetFlagOnAll", s, false, [&](Route route, uint8_t *mem, std::vector<uint32_t> &) {
            Invoke<void(__thiscall *)(void *)>(ctx, route, mem, 0x9ad810, nullptr);
            return uint64_t(0);
        });
    }
}

// ---- the folded slots, through our vtable ----

DIFF_TEST(cGZDBPFIndexModifiable_Slots, 0x144910)
{
    // Slot 1 0x144910 IsModifiable, 2 0x744eb0 IsEmpty, 3 0x145770 GetCount, 15 0x9afdb0 SetRecordMemSize (slot 12, then the
    // memory size's low 31 bits): the original folded function on the original's object against our vtable's slot on ours.
    AllocScope alloc;
    Rng rng(0x0dbf2e00u);
    for (int i = 0; i < 500; ++i) {
        const State s = rng() % 6 == 0 ? State{} : MakeState(rng);
        const Key key = PickKey(rng, s);
        const uint32_t memSize = Pick(rng, {0, 0x7fffffffu, 0x80000000u, 0xffffffffu, static_cast<uint32_t>(rng())});
        struct Case {
            uint32_t rva;
            unsigned slot;
        };
        const Case cases[] = {{0x144910, 1}, {0x744eb0, 2}, {0x145770, 3}, {0x9afdb0, 15}};
        for (const Case &c : cases) {
            uint32_t results[2] = {};
            Snap snaps[2];
            for (int r = 0; r < 2; ++r) {
                uint8_t *mem = Build(ctx, r == 0 ? Route::Original : Route::VTable, 0x13131313u);
                Install(mem, s);
                void *fn = r == 0 ? ctx.Original<void *>(c.rva) : shape::VTableAt(mem, 0)[c.slot];
                if (r == 0 && shape::SlotOf(kVTable, kSlots, c.rva) != c.slot) ctx.Fail("Slots: 0x%x is not in slot %u", c.rva, c.slot);
                switch (c.slot) {
                case 1:
                case 2: results[r] = reinterpret_cast<uint8_t(__thiscall *)(void *)>(fn)(mem); break;
                case 3: results[r] = reinterpret_cast<uint32_t(__thiscall *)(void *)>(fn)(mem); break;
                default: reinterpret_cast<void(__thiscall *)(void *, const void *, uint32_t)>(fn)(mem, &key, memSize); break;
                }
                snaps[r] = TakeSnap(mem);
                Release(mem);
            }
            ++ctx.cases;
            if (results[0] != results[1]) ctx.Fail("Slots: slot %u result original 0x%x, ours 0x%x", c.slot, results[0], results[1]);
            if (!(snaps[0] == snaps[1])) ctx.Fail("Slots: slot %u state differs", c.slot);
        }
    }
}
