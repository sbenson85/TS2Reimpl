// Differential tests for cGZDBPFIndexStatic in the standalone shape (src/gz/cGZDBPFIndexStatic.h).
//
// Every case runs three routes on the same state (docs/standalone-shape.md, "Tests"; tests/shape.h):
// - original: an object built by the original constructor 0x9a6da0 (game vtable), the function called at its RVA;
// - vtable:   an object built by our constructor through the constructor hook's adapter, called through its own vtable, in
//             the slot where the game's vtable holds the RVA (a slot-order mistake calls the wrong function);
// - adapter:  an object built by the original constructor, the RVA hook's adapter called, with an arity-matched trap in the
//             slot under test (an adapter that dispatched virtually would land there; in game, its own hook).
// The state (the entries, the bucket pointers as entry indices, both vectors' capacities, the bucket count) is installed into
// each object with our std::vector (the same STL as the game's) and compared after the call; only the vtable word is
// normalised. States come from our SetEntries on random input (SetEntries is tested against the original below), then
// perturbed. The key map and filter fakes are the compressed index test's (tests/gz_dbpf_index_compressed.cpp).
#include "difftest.h"
#include "free_recorder.h"
#include "gz/cGZDBPFIndexStatic.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include <algorithm>
#include <cstring>
#include <random>
#include <set>
#include <type_traits>
#include <vector>

namespace {

using Self = cGZDBPFIndexStatic;
using Entry = Self::Entry;
using A = cGZDBPFIndexStaticAdapters;
using shape::P;

constexpr uint32_t kVTable = 0xe5fedc;      // cGZDBPFIndexStatic
constexpr uint32_t kIfaceVTable = 0xe5fe14; // cIGZDBPFIndex (what the original destructor leaves)
constexpr unsigned kSlots = 24;
constexpr uint32_t kCtor = 0x9a6da0;
constexpr uint32_t kFlag = 0x80000000u;

enum class Route { Original, VTable, Adapter };
constexpr Route kRoutes[] = {Route::Original, Route::VTable, Route::Adapter};
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

// Frees the vectors and the block without either side's destructor (only the destructor's own test runs it).
void Release(uint8_t *mem)
{
    Self *o = AsSelf(mem);
    o->mEntries.~vector();
    o->mBuckets.~vector();
    ::operator delete(mem, sizeof(Self));
}

// Bucket pointers as indices into the entries; kOutside marks one that points elsewhere (a perturbed state never has it).
struct State {
    std::vector<Entry> entries;
    std::vector<uint32_t> buckets;
    uint32_t bucketCount = 0;
};

void Install(uint8_t *mem, const State &s)
{
    Self *o = AsSelf(mem);
    o->mEntries = s.entries;
    o->mBuckets.resize(s.buckets.size());
    for (size_t i = 0; i < s.buckets.size(); ++i) o->mBuckets[i] = o->mEntries.data() + s.buckets[i];
    o->mBucketCount = s.bucketCount;
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

// Calls the function at `rva` on `mem` by the route: the original at its RVA, our vtable's slot, or the adapter.
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
        // Put the game's vtable back unless the call replaced the trap table (the destructor's own store).
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

struct Snap {
    uint32_t vt = 0;
    std::vector<uint8_t> entries;
    std::vector<uint32_t> buckets; // entry indices (0x80000000 | raw word when outside the entries)
    size_t entriesCap = 0, bucketsCap = 0;
    uint32_t bucketCount = 0;
    bool operator==(const Snap &o) const
    {
        return vt == o.vt && entries == o.entries && buckets == o.buckets && entriesCap == o.entriesCap &&
               bucketsCap == o.bucketsCap && bucketCount == o.bucketCount;
    }
};

Snap TakeSnap(uint8_t *mem)
{
    Snap s;
    Self *o = AsSelf(mem);
    s.vt = VtCode(shape::Word(mem, 0));
    const auto *p = reinterpret_cast<const uint8_t *>(o->mEntries.data());
    s.entries.assign(p, p + o->mEntries.size() * sizeof(Entry));
    const uint32_t base = P(o->mEntries.data()), end = base + static_cast<uint32_t>(o->mEntries.size() * sizeof(Entry));
    for (Entry *b : o->mBuckets) {
        const uint32_t w = P(b);
        s.buckets.push_back(w >= base && w <= end && (w - base) % sizeof(Entry) == 0 ? (w - base) / sizeof(Entry)
                                                                                     : 0x80000000u | w);
    }
    s.entriesCap = o->mEntries.capacity();
    s.bucketsCap = o->mBuckets.capacity();
    s.bucketCount = o->mBucketCount;
    return s;
}

// A result word: pointers into the entries as tagged offsets, everything else as is.
uint32_t NormPtr(uint8_t *mem, uint32_t value)
{
    if (value == 0) return 0;
    const Self *o = AsSelf(mem);
    const uint32_t b = P(o->mEntries.data());
    if (value >= b && value < b + o->mEntries.size() * sizeof(Entry)) return 0xb0000000u | (value - b);
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
    const uint32_t o[4] = {out[0], out[1], out[2], out[3]};
    gLog.insert(gLog.end(), {3u, P(in) == P(out) ? 1u : 0u, k[0], k[1], k[2], k[3], o[0], o[1], o[2], o[3]});
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
    out[1] = k[1] & 0x7u; // merges groups
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

// Index entries as SetEntries takes them: few types, groups and instances (so buckets hold several entries and keys differ
// only in their resource), resources mostly 0, sizes and memory sizes around each other.
std::vector<cGZDBPFIndexEntry> MakeInput(Rng &rng)
{
    const uint32_t n = rng() % 8 == 0 ? rng() % 3 : (rng() % 4 == 0 ? 40 + rng() % 200 : rng() % 64);
    const uint32_t nTypes = 1 + rng() % 6, nGroups = 1 + rng() % 4, nInstances = 1 + rng() % (rng() % 2 == 0 ? 6 : 200);
    std::vector<uint32_t> types(nTypes), groups(nGroups), instances(nInstances);
    for (auto &t : types) t = rng() % 5 == 0 ? Self::kDirectoryType : (rng() % 3 == 0 ? rng() % 16 : static_cast<uint32_t>(rng()));
    for (auto &g : groups) g = rng() % 3 == 0 ? rng() % 16 : static_cast<uint32_t>(rng());
    for (auto &i : instances) i = rng() % 3 == 0 ? rng() % 8 : static_cast<uint32_t>(rng());
    std::vector<cGZDBPFIndexEntry> in(n);
    for (auto &e : in) {
        e.type = types[rng() % nTypes];
        e.group = groups[rng() % nGroups];
        e.instance = instances[rng() % nInstances];
        e.resource = rng() % 2 == 0 ? 0 : (rng() % 2 == 0 ? rng() % 4 : static_cast<uint32_t>(rng()));
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

// Our SetEntries on a scratch object: a consistent state.
State StateFrom(const std::vector<cGZDBPFIndexEntry> &in)
{
    Self scratch;
    scratch.SetEntries(in.data(), static_cast<uint32_t>(in.size()), nullptr);
    State s;
    s.entries = scratch.mEntries;
    for (Entry *b : scratch.mBuckets) s.buckets.push_back(static_cast<uint32_t>(b - scratch.mEntries.data()));
    s.bucketCount = scratch.mBucketCount;
    return s;
}

// A state, sometimes perturbed: zero sizes, memory sizes with and without the flag (the bucket order is kept).
State MakeState(Rng &rng)
{
    State s = StateFrom(MakeInput(rng));
    if (rng() % 3 == 0) {
        for (Entry &e : s.entries) {
            switch (rng() % 6) {
            case 0: e.size = 0; break;
            case 1: e.memSize ^= kFlag; break;
            case 2: e.memSize = static_cast<uint32_t>(rng()); break;
            default: break;
            }
        }
    }
    return s;
}

// ---- the three-route runner ----

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

// Runs `call(route, mem, extra)` on a fresh object of each route holding `state`; call returns the result word.
template <class Call> void RunRoutes(difftest::Context &ctx, const char *name, const State &state, uint32_t fill, Call call)
{
    Outcome out[3];
    for (int i = 0; i < 3; ++i) {
        const Route route = kRoutes[i];
        uint8_t *mem = Build(ctx, route, fill);
        Install(mem, state);
        gLog.clear();
        out[i].result = call(route, mem, out[i].extra);
        out[i].log = gLog;
        out[i].snap = TakeSnap(mem);
        Release(mem);
    }
    Compare(ctx, name, Route::VTable, out[0], out[1]);
    Compare(ctx, name, Route::Adapter, out[0], out[2]);
}

using U32Fn = uint32_t(__thiscall *)(void *);
using BoolArgsFn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t);
using SetFn = void(__thiscall *)(void *, std::set<uint32_t> *);
using KeyListFn = uint32_t(__thiscall *)(void *, cGZDBPFResourceKeyList *, void *);
using FindFn = uint32_t(__thiscall *)(void *, const cGZPersistResourceKey *, uint32_t, uint32_t);
using SetEntriesFn = uint8_t(__thiscall *)(void *, const cGZDBPFIndexEntry *, uint32_t, void *);
using CopyFn = void(__thiscall *)(void *, cGZDBPFIndexEntry *, void *);
using FromIndexFn = uint8_t(__thiscall *)(void *, const uint32_t *, uint32_t, void *, uint32_t);
using WriteFn = void(__thiscall *)(void *, std::vector<uint32_t> *, void *, uint32_t);
using DtorFn = void *(__thiscall *)(void *, uint32_t);

void *Adapter(uint32_t rva)
{
    switch (rva) {
    case 0x9acb50: return reinterpret_cast<void *>(&A::GetCount);
    case 0x9acc30: return reinterpret_cast<void *>(&A::GetTotalSize);
    case 0x9accd0: return reinterpret_cast<void *>(&A::GetTotalMemSize);
    case 0x9abc40: return reinterpret_cast<void *>(&A::GetDataEnd);
    case 0x9ac730: return reinterpret_cast<void *>(&A::GetSerializedSize);
    case 0x9ace20: return reinterpret_cast<void *>(&A::CollectTypes);
    case 0x9abfa0: return reinterpret_cast<void *>(&A::CollectGroups);
    case 0x9ac4e0: return reinterpret_cast<void *>(&A::GetResourceKeyList);
    case 0x9ab430: return reinterpret_cast<void *>(&A::FindRecordForWrite);
    case 0x9b0b10: return reinterpret_cast<void *>(&A::AllEntriesWithin);
    case 0x9b0c20: return reinterpret_cast<void *>(&A::IsRangeFree);
    case 0x9ae410: return reinterpret_cast<void *>(&A::SetEntries);
    case 0x9b10c0: return reinterpret_cast<void *>(&A::CopyEntries);
    case 0x9aef50: return reinterpret_cast<void *>(&A::SetEntriesFromIndex);
    case 0x9b16f0: return reinterpret_cast<void *>(&A::WriteIndexEntries);
    case 0x9b1430: return reinterpret_cast<void *>(&A::WriteCompressionDirectory);
    case 0x9a7860: return reinterpret_cast<void *>(&A::DeletingDestructor);
    default: return nullptr;
    }
}

// Keys to look up: an entry's own key, the same with the resource changed or zeroed, the instance changed, random keys.
cGZPersistResourceKey PickKey(Rng &rng, const State &s)
{
    cGZPersistResourceKey k{static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng()), 0};
    if (s.entries.empty() || rng() % 8 == 0) return k;
    const Entry &e = s.entries[rng() % s.entries.size()];
    k = {e.type, e.group, e.instance, e.resource};
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

// Bounds for slots 16/17: entry starts and ends (+-1), zero, the top of the range, random values.
uint32_t PickBound(Rng &rng, const State &s)
{
    const uint32_t kind = rng() % 9;
    if (!s.entries.empty() && kind < 4) {
        const Entry &e = s.entries[rng() % s.entries.size()];
        const uint32_t base = kind < 2 ? e.offset : e.offset + e.size;
        return base + static_cast<uint32_t>(static_cast<int32_t>(rng() % 3) - 1);
    }
    return Pick(rng, {0, 0xffffffffu, 0x80000000u, rng() % 0x300, static_cast<uint32_t>(rng())});
}

} // namespace

// ---- constructor and destructor ----

DIFF_TEST(cGZDBPFIndexStatic_Construct, 0x9a6da0)
{
    // The constructor writes the vtable and the two vectors (six zero words); +0x1c keeps the buffer's bytes.
    Rng rng(0x0dbf1100u);
    for (int i = 0; i < 64; ++i) {
        const uint32_t fill = static_cast<uint32_t>(rng());
        uint8_t *orig = Build(ctx, Route::Original, fill);
        uint8_t *ours = Build(ctx, Route::VTable, fill);
        ++ctx.cases;
        if (VtCode(shape::Word(orig, 0)) != 0xd0000000u || VtCode(shape::Word(ours, 0)) != 0xd0000000u)
            ctx.Fail("Construct: vtable word");
        if (shape::Word(ours, 0) == shape::Word(orig, 0)) ctx.Fail("Construct: our object carries the game's vtable");
        if (std::memcmp(orig + 4, ours + 4, sizeof(Self) - 4) != 0) ctx.Fail("Construct: bytes after the vtable differ");
        Release(orig);
        Release(ours);
    }
}

DIFF_TEST(cGZDBPFIndexStatic_DeletingDestructor, 0x9a7860)
{
    // Slot 0 with flags 0 (the object stays: its words compared, the destroyed vtable normalised) and 1 (freed). The frees
    // are compared by their sorted sizes (the two vectors, then the object).
    Rng rng(0x0dbf1200u);
    for (int i = 0; i < 400; ++i) {
        const State s = MakeState(rng);
        const uint32_t flags = Pick(rng, {0, 1, 2, 3, 0x100});
        size_t sizes[3][8] = {};
        size_t counts[3] = {};
        uint32_t words[3][sizeof(Self) / 4] = {};
        uint32_t results[3] = {};
        for (int r = 0; r < 3; ++r) {
            uint8_t *mem = Build(ctx, kRoutes[r], 0x5a5a5a5au);
            Install(mem, s);
            void *result;
            {
                difftest::FreeRecorder rec;
                result = Invoke<DtorFn>(ctx, kRoutes[r], mem, 0x9a7860, Adapter(0x9a7860), flags);
                rec.Stop();
                counts[r] = rec.SortedSizes(sizes[r], 8);
            }
            results[r] = P(result) == P(mem) ? 1u : P(result);
            if ((flags & 1) == 0) {
                std::memcpy(words[r], mem, sizeof(Self));
                words[r][0] = VtCode(words[r][0]);
                ::operator delete(mem, sizeof(Self));
            }
        }
        for (int r = 1; r < 3; ++r) {
            ++ctx.cases;
            if (results[r] != results[0]) ctx.Fail("DeletingDestructor (%s): result", RouteName(kRoutes[r]));
            if (counts[r] != counts[0] || std::memcmp(sizes[r], sizes[0], sizeof(sizes[0])) != 0)
                ctx.Fail("DeletingDestructor (%s, flags %u): frees differ (%zu vs %zu)", RouteName(kRoutes[r]), flags, counts[0], counts[r]);
            for (unsigned w = 0; w < sizeof(Self) / 4; ++w) {
                if (words[r][w] != words[0][w])
                    ctx.Fail("DeletingDestructor (%s, flags %u): word +0x%x original 0x%x, ours 0x%x", RouteName(kRoutes[r]), flags,
                             w * 4, words[0][w], words[r][w]);
            }
        }
    }
}

// ---- the plain getters ----

namespace {

void RunGetter(difftest::Context &ctx, const char *name, uint32_t rva, int trials)
{
    Rng rng(0x0dbf1300u + rva);
    for (int i = 0; i < trials; ++i) {
        const State s = MakeState(rng);
        RunRoutes(ctx, name, s, 0x3c3c3c3cu, [&](Route route, uint8_t *mem, std::vector<uint32_t> &) {
            return uint64_t(Invoke<U32Fn>(ctx, route, mem, rva, Adapter(rva)));
        });
    }
}

// Slots 3 and 7 only subtract the vector pointers: they also run on random pointer words (never dereferenced). The words
// are put back to null before the object is freed.
void RunRawGetter(difftest::Context &ctx, const char *name, uint32_t rva)
{
    Rng rng(0x0dbf1400u + rva);
    for (int i = 0; i < 2000; ++i) {
        uint32_t words[6];
        for (auto &w : words) {
            switch (rng() % 4) {
            case 0: w = rng() % 0x40; break;
            case 1: w = 0xffffffffu - rng() % 0x40; break;
            default: w = static_cast<uint32_t>(rng()); break;
            }
        }
        uint32_t results[3];
        for (int r = 0; r < 3; ++r) {
            uint8_t *mem = Build(ctx, kRoutes[r], 0);
            std::memcpy(mem + 4, words, sizeof(words));
            results[r] = Invoke<U32Fn>(ctx, kRoutes[r], mem, rva, Adapter(rva));
            std::memset(mem + 4, 0, sizeof(words));
            Release(mem);
        }
        ++ctx.cases;
        if (results[1] != results[0] || results[2] != results[0])
            ctx.Fail("%s (raw pointers): original 0x%x, vtable 0x%x, adapter 0x%x", name, results[0], results[1], results[2]);
    }
}

} // namespace

DIFF_TEST(cGZDBPFIndexStatic_GetCount, 0x9acb50)
{
    RunGetter(ctx, "GetCount", 0x9acb50, 300);
    RunRawGetter(ctx, "GetCount", 0x9acb50);
}

DIFF_TEST(cGZDBPFIndexStatic_GetTotalSize, 0x9acc30)
{
    RunGetter(ctx, "GetTotalSize", 0x9acc30, 1000);
}

DIFF_TEST(cGZDBPFIndexStatic_GetTotalMemSize, 0x9accd0)
{
    RunGetter(ctx, "GetTotalMemSize", 0x9accd0, 1000);
}

DIFF_TEST(cGZDBPFIndexStatic_GetDataEnd, 0x9abc40)
{
    RunGetter(ctx, "GetDataEnd", 0x9abc40, 1000);
}

DIFF_TEST(cGZDBPFIndexStatic_GetSerializedSize, 0x9ac730)
{
    RunGetter(ctx, "GetSerializedSize", 0x9ac730, 300);
    RunRawGetter(ctx, "GetSerializedSize", 0x9ac730);
}

// ---- slots 16 and 17 ----

namespace {

void RunBounds(difftest::Context &ctx, const char *name, uint32_t rva, bool pairEnd)
{
    Rng rng(0x0dbf1500u + rva);
    for (int i = 0; i < 1500; ++i) {
        const State s = MakeState(rng);
        for (int j = 0; j < 4; ++j) {
            uint32_t a = PickBound(rng, s), b = PickBound(rng, s);
            if (!s.entries.empty() && rng() % 3 == 0) {
                // (a, b) with a + b on an entry's start (IsRangeFree takes a length) or b on an entry's end.
                const Entry &e = s.entries[rng() % s.entries.size()];
                const uint32_t d = static_cast<uint32_t>(static_cast<int32_t>(rng() % 3) - 1);
                if (pairEnd)
                    b = e.offset + e.size + d;
                else
                    b = e.offset - a + d;
            }
            RunRoutes(ctx, name, s, 0x77777777u, [&](Route route, uint8_t *mem, std::vector<uint32_t> &) {
                return uint64_t(Invoke<BoolArgsFn>(ctx, route, mem, rva, Adapter(rva), a, b));
            });
        }
    }
}

} // namespace

DIFF_TEST(cGZDBPFIndexStatic_AllEntriesWithin, 0x9b0b10)
{
    RunBounds(ctx, "AllEntriesWithin", 0x9b0b10, true);
}

DIFF_TEST(cGZDBPFIndexStatic_IsRangeFree, 0x9b0c20)
{
    RunBounds(ctx, "IsRangeFree", 0x9b0c20, false);
}

// ---- slots 8 and 9: the type and group sets ----

namespace {

void RunCollect(difftest::Context &ctx, const char *name, uint32_t rva)
{
    Rng rng(0x0dbf1600u + rva);
    for (int i = 0; i < 600; ++i) {
        const State s = MakeState(rng);
        std::set<uint32_t> before;
        for (uint32_t n = rng() % 4; n != 0; --n) before.insert(rng() % 3 == 0 ? rng() % 16 : static_cast<uint32_t>(rng()));
        if (!s.entries.empty() && rng() % 2 == 0) before.insert(s.entries[rng() % s.entries.size()].type);
        RunRoutes(ctx, name, s, 0x12345678u, [&](Route route, uint8_t *mem, std::vector<uint32_t> &extra) {
            std::set<uint32_t> set = before;
            Invoke<SetFn>(ctx, route, mem, rva, Adapter(rva), &set);
            extra.assign(set.begin(), set.end());
            extra.push_back(static_cast<uint32_t>(set.size()));
            return uint64_t(0);
        });
    }
}

} // namespace

DIFF_TEST(cGZDBPFIndexStatic_CollectTypes, 0x9ace20)
{
    RunCollect(ctx, "CollectTypes", 0x9ace20);
}

DIFF_TEST(cGZDBPFIndexStatic_CollectGroups, 0x9abfa0)
{
    RunCollect(ctx, "CollectGroups", 0x9abfa0);
}

// ---- slot 10: the key list ----

DIFF_TEST(cGZDBPFIndexStatic_GetResourceKeyList, 0x9ac4e0)
{
    // The list's blocks come from the allocator service (both sides grow it through the service). Lists: none, empty, or
    // holding keys already; filters: none or the recording fake (al answers, 0x100 is false). Offsets repeat (insertion and
    // heap orders show in the result).
    svcfx::AllocatorScope allocator;
    Rng rng(0x0dbf1700u);
    for (int i = 0; i < 800; ++i) {
        State s = MakeState(rng);
        if (rng() % 3 == 0)
            for (Entry &e : s.entries) e.offset = rng() % 8;
        const uint32_t listKind = rng() % 3;
        const uint32_t preset = rng() % 5;
        const bool filter = rng() % 2 == 0;
        RunRoutes(ctx, "GetResourceKeyList", s, 0x0f0f0f0fu, [&](Route route, uint8_t *mem, std::vector<uint32_t> &extra) {
            cGZDBPFResourceKeyList list;
            for (uint32_t k = 0; k < preset; ++k) list.push_back({k, k + 1, k + 2, k + 3});
            const uint32_t r = Invoke<KeyListFn>(ctx, route, mem, 0x9ac4e0, Adapter(0x9ac4e0), listKind == 0 ? nullptr : &list,
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

// ---- slot 12 (and slot 11, which jumps to it) ----

DIFF_TEST(cGZDBPFIndexStatic_FindRecordForWrite, 0x9ab430)
{
    // The result points into the entries (the record words of the entry) or is null.
    Rng rng(0x0dbf1800u);
    for (int i = 0; i < 1500; ++i) {
        const State s = MakeState(rng);
        for (int j = 0; j < 4; ++j) {
            const cGZPersistResourceKey key = PickKey(rng, s);
            const uint32_t a = PickFlag(rng), b = PickFlag(rng);
            RunRoutes(ctx, "FindRecordForWrite", s, 0x44444444u, [&](Route route, uint8_t *mem, std::vector<uint32_t> &) {
                return uint64_t(NormPtr(mem, Invoke<FindFn>(ctx, route, mem, 0x9ab430, Adapter(0x9ab430), &key, a, b)));
            });
        }
    }
}

// ---- slots 18 and 20: building the index, and BuildBuckets 0x9ad220 ----

DIFF_TEST(cGZDBPFIndexStatic_SetEntries, 0x9ae410)
{
    // Two calls on one object (the second resizes the first's vectors), with and without the key map (its slot 4 merges
    // groups). The vectors (contents and capacities), the bucket count, the key map's calls and the result are compared.
    Rng rng(0x0dbf1900u);
    for (int i = 0; i < 600; ++i) {
        const std::vector<cGZDBPFIndexEntry> first = MakeInput(rng), second = MakeInput(rng);
        const bool map1 = rng() % 2 == 0, map2 = rng() % 2 == 0;
        const bool twice = rng() % 2 == 0;
        State empty;
        empty.bucketCount = 0x99999999u;
        RunRoutes(ctx, "SetEntries", empty, 0x21212121u, [&](Route route, uint8_t *mem, std::vector<uint32_t> &extra) {
            uint32_t r = Invoke<SetEntriesFn>(ctx, route, mem, 0x9ae410, Adapter(0x9ae410), first.empty() ? nullptr : first.data(),
                                              static_cast<uint32_t>(first.size()), map1 ? &gKeyMap : nullptr);
            extra.push_back(r);
            if (twice)
                r = Invoke<SetEntriesFn>(ctx, route, mem, 0x9ae410, Adapter(0x9ae410), second.data(),
                                         static_cast<uint32_t>(second.size()), map2 ? &gKeyMap : nullptr);
            return uint64_t(r);
        });
    }
}

DIFF_TEST(cGZDBPFIndexStatic_SetEntriesFromIndex, 0x9aef50)
{
    // The file's index words (6 per entry for minor version 2, else 5) on a fresh or a built index.
    Rng rng(0x0dbf1a00u);
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
        RunRoutes(ctx, "SetEntriesFromIndex", s, 0x31313131u, [&](Route route, uint8_t *mem, std::vector<uint32_t> &) {
            return uint64_t(Invoke<FromIndexFn>(ctx, route, mem, 0x9aef50, Adapter(0x9aef50), words.empty() ? nullptr : words.data(),
                                                static_cast<uint32_t>(in.size()), map ? &gKeyMap : nullptr, version));
        });
    }
}

DIFF_TEST(cGZDBPFIndexStatic_BuildBuckets, 0x9ad220)
{
    // A non-virtual member: the original at its RVA against ours (its hook points at the member itself), on entries in any
    // order and bucket vectors of any length (shrunk, kept, grown within and beyond the capacity).
    Rng rng(0x0dbf1b00u);
    for (int i = 0; i < 800; ++i) {
        State s = StateFrom(MakeInput(rng));
        std::shuffle(s.entries.begin(), s.entries.end(), rng);
        s.buckets.assign(rng() % 3 == 0 ? 0 : rng() % 80, 0);
        for (auto &b : s.buckets) b = s.entries.empty() ? 0 : rng() % s.entries.size();
        const uint32_t reserve = rng() % 2 == 0 ? 0 : rng() % 100;
        Snap snaps[2];
        for (int r = 0; r < 2; ++r) {
            uint8_t *mem = Build(ctx, r == 0 ? Route::Original : Route::VTable, 0x66666666u);
            Install(mem, s);
            AsSelf(mem)->mBuckets.reserve(reserve);
            if (r == 0)
                ctx.Original<void(__thiscall *)(void *)>(0x9ad220)(mem);
            else
                AsSelf(mem)->BuildBuckets();
            snaps[r] = TakeSnap(mem);
            Release(mem);
        }
        ++ctx.cases;
        if (!(snaps[0] == snaps[1])) ctx.Fail("BuildBuckets: state differs (%zu entries)", s.entries.size());
    }
}

// ---- slots 19, 21, 22: handing the index out ----

DIFF_TEST(cGZDBPFIndexStatic_CopyEntries, 0x9b10c0)
{
    // Into a buffer with room for two more entries than the index has, prefilled; the whole buffer is compared. The key map
    // writes its result into a zeroed key here (its log shows the out key it was handed).
    Rng rng(0x0dbf1c00u);
    for (int i = 0; i < 800; ++i) {
        const State s = MakeState(rng);
        const bool map = rng() % 2 == 0;
        const uint32_t fill = static_cast<uint32_t>(rng());
        RunRoutes(ctx, "CopyEntries", s, 0x41414141u, [&](Route route, uint8_t *mem, std::vector<uint32_t> &extra) {
            std::vector<cGZDBPFIndexEntry> out(s.entries.size() + 2);
            for (auto &e : out) std::memset(&e, static_cast<int>(fill & 0xff), sizeof(e));
            Invoke<CopyFn>(ctx, route, mem, 0x9b10c0, Adapter(0x9b10c0), out.data(), map ? &gKeyMap : nullptr);
            const auto *w = reinterpret_cast<const uint32_t *>(out.data());
            extra.assign(w, w + out.size() * 7);
            return uint64_t(0);
        });
    }
}

namespace {

void RunWrite(difftest::Context &ctx, const char *name, uint32_t rva, bool presizeOther)
{
    Rng rng(0x0dbf1d00u + rva);
    for (int i = 0; i < 800; ++i) {
        const State s = MakeState(rng);
        const bool map = rng() % 2 == 0;
        const uint32_t version = Pick(rng, {1, 1, 2, 2, 0, 3});
        uint32_t preset = rng() % 3 == 0 ? rng() % 0x50 : 0;
        // Slot 21 writes five words per entry into what `out` holds for versions other than 1 and 2: give it room.
        if (presizeOther && version != 1 && version != 2) preset = static_cast<uint32_t>(s.entries.size() * 5 + 3);
        RunRoutes(ctx, name, s, 0x51515151u, [&](Route route, uint8_t *mem, std::vector<uint32_t> &extra) {
            std::vector<uint32_t> out;
            for (uint32_t k = 0; k < preset; ++k) out.push_back(0xee000000u + k);
            Invoke<WriteFn>(ctx, route, mem, rva, Adapter(rva), &out, map ? &gKeyMap : nullptr, version);
            extra = out;
            extra.push_back(static_cast<uint32_t>(out.capacity()));
            return uint64_t(0);
        });
    }
}

} // namespace

DIFF_TEST(cGZDBPFIndexStatic_WriteIndexEntries, 0x9b16f0)
{
    RunWrite(ctx, "WriteIndexEntries", 0x9b16f0, true);
}

DIFF_TEST(cGZDBPFIndexStatic_WriteCompressionDirectory, 0x9b1430)
{
    RunWrite(ctx, "WriteCompressionDirectory", 0x9b1430, false);
}

// ---- the slots no test above calls: the folded ones (shared with the other indexes) through our vtable ----

DIFF_TEST(cGZDBPFIndexStatic_Slots, 0x147480)
{
    // Slot 1 0x147480 IsModifiable, 2 0x9ad640 IsEmpty, 11 0x1ad030 FindRecord (jumps to slot 12 of the object's vtable), 13
    // 0x1b9a50 AddRecord, 14 0x147110 RemoveRecord, 15 0x9afdb0 SetRecordMemSize (slot 12, then the memory size's low 31
    // bits), 23 0x140fe0 SetFlagOnAll: the original folded function on the original's object against our vtable's slot on
    // our object. Results in al for the bools.
    Rng rng(0x0dbf1e00u);
    for (int i = 0; i < 600; ++i) {
        const State s = rng() % 8 == 0 ? StateFrom({}) : MakeState(rng);
        const cGZPersistResourceKey key = PickKey(rng, s);
        const uint32_t a = PickFlag(rng), b = PickFlag(rng);
        const uint32_t memSize = Pick(rng, {0, 0x7fffffffu, 0x80000000u, 0xffffffffu, static_cast<uint32_t>(rng())});
        cGZDBPFIndexRecord record{1, 2, 3};
        struct Case {
            uint32_t rva;
            unsigned slot;
        };
        const Case cases[] = {{0x147480, 1}, {0x9ad640, 2}, {0x1ad030, 11}, {0x1b9a50, 13}, {0x147110, 14}, {0x9afdb0, 15}, {0x140fe0, 23}};
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
                case 11: results[r] = NormPtr(mem, reinterpret_cast<uint32_t(__thiscall *)(void *, const void *, uint32_t, uint32_t)>(fn)(mem, &key, a, b)); break;
                case 13: reinterpret_cast<void(__thiscall *)(void *, const void *, const void *)>(fn)(mem, &key, &record); break;
                case 14: results[r] = reinterpret_cast<uint8_t(__thiscall *)(void *, const void *, void *, uint32_t, uint32_t)>(fn)(mem, &key, &record, 1, 1); break;
                case 15: reinterpret_cast<void(__thiscall *)(void *, const void *, uint32_t)>(fn)(mem, &key, memSize); break;
                default: reinterpret_cast<void(__thiscall *)(void *)>(fn)(mem); break;
                }
                snaps[r] = TakeSnap(mem);
                Release(mem);
            }
            ++ctx.cases;
            if (results[0] != results[1]) ctx.Fail("Slots: slot %u result original 0x%x, ours 0x%x", c.slot, results[0], results[1]);
            if (!(snaps[0] == snaps[1])) ctx.Fail("Slots: slot %u state differs", c.slot);
        }
        ++ctx.cases;
        if (record.offset != 1 || record.size != 2 || record.memSize != 3) ctx.Fail("Slots: the record argument changed");
    }
}
