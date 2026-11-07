// Differential tests for cGZDBPFIndexCompressed in the standalone shape (src/gz/cGZDBPFIndexCompressed.h).
//
// Every case runs three routes on the same state (docs/standalone-shape.md, "Tests"; tests/shape.h):
// - original: an object built by the original constructor 0x9a6ca0 (game vtable), the function called at its RVA;
// - vtable:   an object built by our constructor through the constructor hook's adapter, called through its own vtable, in
//             the slot where the game's vtable holds the RVA (a slot-order mistake calls the wrong function);
// - adapter:  an object built by the original constructor, the RVA hook's adapter called, with an arity-matched trap in the
//             slot under test (an adapter that dispatched virtually would land there; in game, its own hook).
// The state (the three vectors' contents and capacities, the block count, the found record) is installed into each object
// with our std::vector (the same STL as the game's) and compared after the call; only the vtable word is normalised.
// States come from our SetEntries on random input (SetEntries is tested against the original below), then perturbed.
#include "difftest.h"
#include "free_recorder.h"
#include "gz/cGZDBPFIndexCompressed.h"
#include "shape.h"
#include "ts_services_fixture.h"

#include <algorithm>
#include <cstring>
#include <random>
#include <set>
#include <type_traits>
#include <vector>

namespace {

using Self = cGZDBPFIndexCompressed;
using Entry = Self::Entry;
using Record = Self::Record;
using Block = Self::Block;
using A = cGZDBPFIndexCompressedAdapters;
using shape::P;

constexpr uint32_t kVTable = 0xe5fe78;      // cGZDBPFIndexCompressed
constexpr uint32_t kIfaceVTable = 0xe5fe14; // cIGZDBPFIndex (what the original destructor leaves)
constexpr unsigned kSlots = 24;
constexpr uint32_t kCtor = 0x9a6ca0;
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
    o->mRecords.~vector();
    o->mBlocks.~vector();
    ::operator delete(mem, sizeof(Self));
}

struct State {
    std::vector<Entry> entries;
    std::vector<Record> records;
    std::vector<Block> blocks;
    uint32_t blockCount = 0;
    cGZDBPFIndexRecord found{};
};

void Install(uint8_t *mem, const State &s)
{
    Self *o = AsSelf(mem);
    o->mEntries = s.entries;
    o->mRecords = s.records;
    o->mBlocks = s.blocks;
    o->mBlockCount = s.blockCount;
    o->mFound = s.found;
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

template <class T> std::vector<uint8_t> Bytes(const std::vector<T> &v)
{
    const auto *p = reinterpret_cast<const uint8_t *>(v.data());
    return std::vector<uint8_t>(p, p + v.size() * sizeof(T));
}

struct Snap {
    uint32_t vt = 0;
    std::vector<uint8_t> entries, records, blocks;
    size_t entriesCap = 0, recordsCap = 0, blocksCap = 0;
    uint32_t tail[4] = {}; // +0x28 block count, +0x2c found record
    bool operator==(const Snap &o) const
    {
        return vt == o.vt && entries == o.entries && records == o.records && blocks == o.blocks &&
               entriesCap == o.entriesCap && recordsCap == o.recordsCap && blocksCap == o.blocksCap &&
               std::memcmp(tail, o.tail, sizeof(tail)) == 0;
    }
};

Snap TakeSnap(uint8_t *mem)
{
    Snap s;
    Self *o = AsSelf(mem);
    s.vt = VtCode(shape::Word(mem, 0));
    s.entries = Bytes(o->mEntries);
    s.records = Bytes(o->mRecords);
    s.blocks = Bytes(o->mBlocks);
    s.entriesCap = o->mEntries.capacity();
    s.recordsCap = o->mRecords.capacity();
    s.blocksCap = o->mBlocks.capacity();
    std::memcpy(s.tail, mem + 0x28, sizeof(s.tail));
    return s;
}

// A result word: pointers into the object or its entries as tagged offsets, everything else as is.
uint32_t NormPtr(uint8_t *mem, uint32_t value)
{
    if (value == 0) return 0;
    if (value >= P(mem) && value < P(mem) + sizeof(Self)) return 0xa0000000u | (value - P(mem));
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

// Index entries as SetEntries takes them: few types and groups (so blocks have several entries and hash buckets
// collide), duplicate instances, resources mostly 0, sizes that sometimes need records.
std::vector<cGZDBPFIndexEntry> MakeInput(Rng &rng)
{
    const uint32_t n = rng() % 8 == 0 ? rng() % 3 : (rng() % 4 == 0 ? 100 + rng() % 400 : rng() % 80);
    const uint32_t nTypes = 1 + rng() % 12, nGroups = 1 + rng() % (rng() % 3 == 0 ? 60 : 6);
    std::vector<uint32_t> types(nTypes), groups(nGroups);
    for (auto &t : types) t = rng() % 5 == 0 ? Self::kDirectoryType : (rng() % 3 == 0 ? rng() % 16 : static_cast<uint32_t>(rng()));
    for (auto &g : groups) g = rng() % 3 == 0 ? rng() % 16 : static_cast<uint32_t>(rng());
    std::vector<cGZDBPFIndexEntry> in(n);
    for (auto &e : in) {
        e.type = types[rng() % nTypes];
        e.group = groups[rng() % nGroups];
        e.instance = rng() % 3 == 0 ? rng() % 8 : static_cast<uint32_t>(rng());
        e.resource = rng() % 2 == 0 ? 0 : (rng() % 2 == 0 ? rng() % 4 : static_cast<uint32_t>(rng()));
        e.offset = rng() % 4 == 0 ? static_cast<uint32_t>(rng()) : rng() % 0x100000;
        switch (rng() % 6) {
        case 0: e.size = 0; break;
        case 1: e.size = Pick(rng, {0xfffe, 0xffff, 0x10000, static_cast<uint32_t>(rng())}); break;
        default: e.size = rng() % 0x4000; break;
        }
        switch (rng() % 6) {
        case 0: e.memSize = e.size; break;
        case 1: e.memSize = e.size | kFlag; break;
        case 2: e.memSize = Pick(rng, {0xfffe, 0xffff, 0x8000fffe, 0x8000ffff, 0x10000, static_cast<uint32_t>(rng())}); break;
        default: e.memSize = e.size + rng() % 0x8000 + (rng() % 2 == 0 ? kFlag : 0); break;
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
    s.records = scratch.mRecords;
    s.blocks = scratch.mBlocks;
    s.blockCount = scratch.mBlockCount;
    s.found = {0x11111111u, 0x22222222u, 0x33333333u};
    return s;
}

// A state, sometimes perturbed: sizes moved to and from records, zero sizes, record values with and without the flag.
State MakeState(Rng &rng)
{
    State s = StateFrom(MakeInput(rng));
    if (rng() % 3 == 0) {
        for (Entry &e : s.entries) {
            switch (rng() % 8) {
            case 0: e.mSize = 0; break;
            case 1:
                if (!s.records.empty()) {
                    e.mSize = Self::kLargeSize;
                    e.mExtra = static_cast<uint16_t>(rng() % s.records.size());
                }
                break;
            case 2:
                if (e.mSize == Self::kLargeSize) e.mSize = static_cast<uint16_t>(rng() % 0x100);
                break;
            default: break;
            }
        }
        for (Record &r : s.records) {
            if (rng() % 3 == 0) r.mSize = rng() % 2 == 0 ? 0 : static_cast<uint32_t>(rng());
            if (rng() % 3 == 0) r.mMemSize = static_cast<uint32_t>(rng());
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
using SetMemFn = void(__thiscall *)(void *, const cGZPersistResourceKey *, uint32_t);
using SetEntriesFn = uint8_t(__thiscall *)(void *, const cGZDBPFIndexEntry *, uint32_t, void *);
using CopyFn = void(__thiscall *)(void *, cGZDBPFIndexEntry *, void *);
using FromIndexFn = uint8_t(__thiscall *)(void *, const uint32_t *, uint32_t, void *, uint32_t);
using WriteFn = void(__thiscall *)(void *, std::vector<uint32_t> *, void *, uint32_t);
using DtorFn = void *(__thiscall *)(void *, uint32_t);

void *Adapter(uint32_t rva)
{
    switch (rva) {
    case 0x9acb40: return reinterpret_cast<void *>(&A::GetCount);
    case 0x9acbc0: return reinterpret_cast<void *>(&A::GetTotalSize);
    case 0x9acc50: return reinterpret_cast<void *>(&A::GetTotalMemSize);
    case 0x9abbc0: return reinterpret_cast<void *>(&A::GetDataEnd);
    case 0x9ac6f0: return reinterpret_cast<void *>(&A::GetSerializedSize);
    case 0x9acd00: return reinterpret_cast<void *>(&A::CollectTypes);
    case 0x9abe70: return reinterpret_cast<void *>(&A::CollectGroups);
    case 0x9ac040: return reinterpret_cast<void *>(&A::GetResourceKeyList);
    case 0x9ab1a0: return reinterpret_cast<void *>(&A::FindRecord);
    case 0x9afd00: return reinterpret_cast<void *>(&A::SetRecordMemSize);
    case 0x9b0a40: return reinterpret_cast<void *>(&A::AllEntriesWithin);
    case 0x9b0b60: return reinterpret_cast<void *>(&A::IsRangeFree);
    case 0x9adb20: return reinterpret_cast<void *>(&A::SetEntries);
    case 0x9b0e70: return reinterpret_cast<void *>(&A::CopyEntries);
    case 0x9aec00: return reinterpret_cast<void *>(&A::SetEntriesFromIndex);
    case 0x9b1540: return reinterpret_cast<void *>(&A::WriteIndexEntries);
    case 0x9b11a0: return reinterpret_cast<void *>(&A::WriteCompressionDirectory);
    case 0x9a7720: return reinterpret_cast<void *>(&A::DeletingDestructor);
    default: return nullptr;
    }
}

// Keys to look up: an entry's own key, the same with the resource changed or zeroed, a key of a missing instance in
// an existing block, and random keys.
cGZPersistResourceKey PickKey(Rng &rng, const State &s)
{
    cGZPersistResourceKey k{static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng()), 0};
    if (s.blockCount == 0 || rng() % 8 == 0) return k;
    const uint32_t b = rng() % s.blockCount;
    const Block &block = s.blocks[b];
    k.type = block.mType;
    k.group = block.mGroup;
    const uint32_t first = block.mFirstEntry, end = s.blocks[b + 1].mFirstEntry;
    if (end > first) {
        const Entry &e = s.entries[first + rng() % (end - first)];
        k.instance = e.mInstance;
        k.resource = e.mResource;
    }
    switch (rng() % 6) {
    case 0: k.resource = 0; break;
    case 1: k.resource = rng() % 2 == 0 ? k.resource + 1 : static_cast<uint32_t>(rng()); break;
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
        const uint32_t size = e.mSize == Self::kLargeSize ? s.records[e.mExtra].mSize : e.mSize;
        const uint32_t base = kind < 2 ? e.mStart : e.mStart + size;
        return base + static_cast<uint32_t>(static_cast<int32_t>(rng() % 3) - 1);
    }
    return Pick(rng, {0, 0xffffffffu, 0x80000000u, rng() % 0x300, static_cast<uint32_t>(rng())});
}

} // namespace

// ---- constructor and destructor ----

DIFF_TEST(cGZDBPFIndexCompressed_Construct, 0x9a6ca0)
{
    // The constructor writes the vtable and the three vectors (nine zero words); +0x28..+0x37 keep the buffer's bytes.
    Rng rng(0x0dbf0100u);
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

DIFF_TEST(cGZDBPFIndexCompressed_DeletingDestructor, 0x9a7720)
{
    // Slot 0 with flags 0 (the object stays: its words compared, the destroyed vtable normalised) and 1 (freed). The frees
    // are compared by their sorted sizes (the three vectors, then the object).
    Rng rng(0x0dbf0200u);
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
                result = Invoke<DtorFn>(ctx, kRoutes[r], mem, 0x9a7720, Adapter(0x9a7720), flags);
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
    Rng rng(0x0dbf0300u + rva);
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
    Rng rng(0x0dbf0400u + rva);
    for (int i = 0; i < 2000; ++i) {
        uint32_t words[9];
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

DIFF_TEST(cGZDBPFIndexCompressed_GetCount, 0x9acb40)
{
    RunGetter(ctx, "GetCount", 0x9acb40, 300);
    RunRawGetter(ctx, "GetCount", 0x9acb40);
}

DIFF_TEST(cGZDBPFIndexCompressed_GetTotalSize, 0x9acbc0)
{
    RunGetter(ctx, "GetTotalSize", 0x9acbc0, 1000);
}

DIFF_TEST(cGZDBPFIndexCompressed_GetTotalMemSize, 0x9acc50)
{
    RunGetter(ctx, "GetTotalMemSize", 0x9acc50, 1000);
}

DIFF_TEST(cGZDBPFIndexCompressed_GetDataEnd, 0x9abbc0)
{
    RunGetter(ctx, "GetDataEnd", 0x9abbc0, 1000);
}

DIFF_TEST(cGZDBPFIndexCompressed_GetSerializedSize, 0x9ac6f0)
{
    RunGetter(ctx, "GetSerializedSize", 0x9ac6f0, 300);
    RunRawGetter(ctx, "GetSerializedSize", 0x9ac6f0);
}

// ---- slots 16 and 17 ----

namespace {

void RunBounds(difftest::Context &ctx, const char *name, uint32_t rva, bool pairEnd)
{
    Rng rng(0x0dbf0500u + rva);
    for (int i = 0; i < 1500; ++i) {
        const State s = MakeState(rng);
        for (int j = 0; j < 4; ++j) {
            uint32_t a = PickBound(rng, s), b = PickBound(rng, s);
            if (!s.entries.empty() && rng() % 3 == 0) {
                // (a, b) with a + b on an entry's start (IsRangeFree takes a length) or b on an entry's end.
                const Entry &e = s.entries[rng() % s.entries.size()];
                const uint32_t d = static_cast<uint32_t>(static_cast<int32_t>(rng() % 3) - 1);
                if (pairEnd)
                    b = e.mStart + e.mSize + d;
                else
                    b = e.mStart - a + d;
            }
            RunRoutes(ctx, name, s, 0x77777777u, [&](Route route, uint8_t *mem, std::vector<uint32_t> &) {
                return uint64_t(Invoke<BoolArgsFn>(ctx, route, mem, rva, Adapter(rva), a, b));
            });
        }
    }
}

} // namespace

DIFF_TEST(cGZDBPFIndexCompressed_AllEntriesWithin, 0x9b0a40)
{
    RunBounds(ctx, "AllEntriesWithin", 0x9b0a40, true);
}

DIFF_TEST(cGZDBPFIndexCompressed_IsRangeFree, 0x9b0b60)
{
    RunBounds(ctx, "IsRangeFree", 0x9b0b60, false);
}

// ---- slots 8 and 9: the type and group sets ----

namespace {

void RunCollect(difftest::Context &ctx, const char *name, uint32_t rva)
{
    Rng rng(0x0dbf0600u + rva);
    for (int i = 0; i < 600; ++i) {
        const State s = MakeState(rng);
        std::set<uint32_t> before;
        for (uint32_t n = rng() % 4; n != 0; --n) before.insert(rng() % 3 == 0 ? rng() % 16 : static_cast<uint32_t>(rng()));
        if (!s.blocks.empty() && rng() % 2 == 0) before.insert(s.blocks[rng() % s.blocks.size()].mType);
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

DIFF_TEST(cGZDBPFIndexCompressed_CollectTypes, 0x9acd00)
{
    RunCollect(ctx, "CollectTypes", 0x9acd00);
}

DIFF_TEST(cGZDBPFIndexCompressed_CollectGroups, 0x9abe70)
{
    RunCollect(ctx, "CollectGroups", 0x9abe70);
}

// ---- slot 10: the key list ----

DIFF_TEST(cGZDBPFIndexCompressed_GetResourceKeyList, 0x9ac040)
{
    // The list's blocks come from the allocator service (both sides grow it through the service). Lists: none, empty, or
    // holding keys already; filters: none or the recording fake (al answers, 0x100 is false).
    svcfx::AllocatorScope allocator;
    Rng rng(0x0dbf0700u);
    for (int i = 0; i < 800; ++i) {
        const State s = MakeState(rng);
        const uint32_t listKind = rng() % 3;
        const uint32_t preset = rng() % 5;
        const bool filter = rng() % 2 == 0;
        RunRoutes(ctx, "GetResourceKeyList", s, 0x0f0f0f0fu, [&](Route route, uint8_t *mem, std::vector<uint32_t> &extra) {
            cGZDBPFResourceKeyList list;
            for (uint32_t k = 0; k < preset; ++k) list.push_back({k, k + 1, k + 2, k + 3});
            const uint32_t r = Invoke<KeyListFn>(ctx, route, mem, 0x9ac040, Adapter(0x9ac040), listKind == 0 ? nullptr : &list,
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

// ---- slot 11 and FindEntry 0x9ab2e0 ----

DIFF_TEST(cGZDBPFIndexCompressed_FindRecord, 0x9ab1a0)
{
    // The result is &mFound (+0x2c) or null; mFound's words are in the compared state.
    Rng rng(0x0dbf0800u);
    for (int i = 0; i < 1500; ++i) {
        const State s = MakeState(rng);
        for (int j = 0; j < 4; ++j) {
            const cGZPersistResourceKey key = PickKey(rng, s);
            const uint32_t a = PickFlag(rng), b = PickFlag(rng);
            RunRoutes(ctx, "FindRecord", s, 0x44444444u, [&](Route route, uint8_t *mem, std::vector<uint32_t> &) {
                return uint64_t(NormPtr(mem, Invoke<FindFn>(ctx, route, mem, 0x9ab1a0, Adapter(0x9ab1a0), &key, a, b)));
            });
        }
    }
}

DIFF_TEST(cGZDBPFIndexCompressed_FindEntry, 0x9ab2e0)
{
    // A non-virtual member: the original at its RVA against ours (its hook points at the member itself). The flags are
    // dwords whose low bytes count.
    Rng rng(0x0dbf0900u);
    for (int i = 0; i < 1500; ++i) {
        const State s = MakeState(rng);
        for (int j = 0; j < 6; ++j) {
            const cGZPersistResourceKey key = PickKey(rng, s);
            const uint32_t a = PickFlag(rng), b = PickFlag(rng);
            uint32_t results[2];
            Snap snaps[2];
            for (int r = 0; r < 2; ++r) {
                uint8_t *mem = Build(ctx, r == 0 ? Route::Original : Route::VTable, 0x66666666u);
                Install(mem, s);
                const uint32_t p = r == 0 ? ctx.Original<FindFn>(0x9ab2e0)(mem, &key, a, b)
                                          : P(AsSelf(mem)->FindEntry(&key, static_cast<uint8_t>(a), static_cast<uint8_t>(b)));
                results[r] = NormPtr(mem, p);
                snaps[r] = TakeSnap(mem);
                Release(mem);
            }
            ++ctx.cases;
            if (results[0] != results[1]) ctx.Fail("FindEntry: original 0x%x, ours 0x%x", results[0], results[1]);
            if (!(snaps[0] == snaps[1])) ctx.Fail("FindEntry: state differs");
        }
    }
}

// ---- slot 15 ----

DIFF_TEST(cGZDBPFIndexCompressed_SetRecordMemSize, 0x9afd00)
{
    Rng rng(0x0dbf0a00u);
    for (int i = 0; i < 1500; ++i) {
        const State s = MakeState(rng);
        for (int j = 0; j < 3; ++j) {
            const cGZPersistResourceKey key = PickKey(rng, s);
            const uint32_t size = Pick(rng, {0, 0xfffe, 0xffff, 0x10000, 0x8000fffeu, rng() % 0x1000, static_cast<uint32_t>(rng())});
            RunRoutes(ctx, "SetRecordMemSize", s, 0x55555555u, [&](Route route, uint8_t *mem, std::vector<uint32_t> &) {
                Invoke<SetMemFn>(ctx, route, mem, 0x9afd00, Adapter(0x9afd00), &key, size);
                return uint64_t(0);
            });
        }
    }
}

// ---- slots 18 and 20: building the index ----

DIFF_TEST(cGZDBPFIndexCompressed_SetEntries, 0x9adb20)
{
    // Two calls on one object (the second must clear the first's records and blocks and resize its entries), with and
    // without the key map (its slot 4 merges groups). The resulting vectors (contents and capacities), block count, the
    // key map's calls and the result are compared.
    Rng rng(0x0dbf0b00u);
    for (int i = 0; i < 600; ++i) {
        const std::vector<cGZDBPFIndexEntry> first = MakeInput(rng), second = MakeInput(rng);
        const bool map1 = rng() % 2 == 0, map2 = rng() % 2 == 0;
        const bool twice = rng() % 2 == 0;
        State empty;
        empty.blockCount = 0x99999999u;
        empty.found = {1, 2, 3};
        RunRoutes(ctx, "SetEntries", empty, 0x21212121u, [&](Route route, uint8_t *mem, std::vector<uint32_t> &extra) {
            uint32_t r = Invoke<SetEntriesFn>(ctx, route, mem, 0x9adb20, Adapter(0x9adb20), first.empty() ? nullptr : first.data(),
                                              static_cast<uint32_t>(first.size()), map1 ? &gKeyMap : nullptr);
            extra.push_back(r);
            if (twice)
                r = Invoke<SetEntriesFn>(ctx, route, mem, 0x9adb20, Adapter(0x9adb20), second.data(),
                                         static_cast<uint32_t>(second.size()), map2 ? &gKeyMap : nullptr);
            return uint64_t(r);
        });
    }
}

DIFF_TEST(cGZDBPFIndexCompressed_SetEntriesFromIndex, 0x9aec00)
{
    // The file's index words (6 per entry for minor version 2, else 5) on a fresh or a built index.
    Rng rng(0x0dbf0c00u);
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
            return uint64_t(Invoke<FromIndexFn>(ctx, route, mem, 0x9aec00, Adapter(0x9aec00), words.empty() ? nullptr : words.data(),
                                                static_cast<uint32_t>(in.size()), map ? &gKeyMap : nullptr, version));
        });
    }
}

// ---- slots 19, 21, 22: handing the index out ----

DIFF_TEST(cGZDBPFIndexCompressed_CopyEntries, 0x9b0e70)
{
    // Into a buffer with room for two more entries than the index has, prefilled (the flag word is read before it is
    // written); the whole buffer is compared.
    Rng rng(0x0dbf0d00u);
    for (int i = 0; i < 800; ++i) {
        const State s = MakeState(rng);
        const bool map = rng() % 2 == 0;
        const uint32_t fill = static_cast<uint32_t>(rng());
        RunRoutes(ctx, "CopyEntries", s, 0x41414141u, [&](Route route, uint8_t *mem, std::vector<uint32_t> &extra) {
            std::vector<cGZDBPFIndexEntry> out(s.entries.size() + 2);
            for (auto &e : out) std::memset(&e, static_cast<int>(fill & 0xff), sizeof(e));
            for (size_t k = 0; k < out.size(); k += 3) out[k].memSize = fill;
            Invoke<CopyFn>(ctx, route, mem, 0x9b0e70, Adapter(0x9b0e70), out.data(), map ? &gKeyMap : nullptr);
            const auto *w = reinterpret_cast<const uint32_t *>(out.data());
            extra.assign(w, w + out.size() * 7);
            return uint64_t(0);
        });
    }
}

namespace {

void RunWrite(difftest::Context &ctx, const char *name, uint32_t rva, bool presizeOther)
{
    Rng rng(0x0dbf0e00u + rva);
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

DIFF_TEST(cGZDBPFIndexCompressed_WriteIndexEntries, 0x9b1540)
{
    RunWrite(ctx, "WriteIndexEntries", 0x9b1540, true);
}

DIFF_TEST(cGZDBPFIndexCompressed_WriteCompressionDirectory, 0x9b11a0)
{
    RunWrite(ctx, "WriteCompressionDirectory", 0x9b11a0, false);
}

// ---- the slots no test above calls: the folded ones (shared with the other indexes) through our vtable ----

DIFF_TEST(cGZDBPFIndexCompressed_Slots, 0x147480)
{
    // Slot 1 0x147480 IsModifiable, 2 0x9ad640 IsEmpty, 12 0x18a5c0 FindRecordForWrite, 13 0x1b9a50 AddRecord, 14 0x147110
    // RemoveRecord, 23 0x140fe0 SetFlagOnAll: the original folded function against our vtable's slot on our object. Results
    // read whole eax only where the original sets it (12); al for the bools.
    Rng rng(0x0dbf0f00u);
    for (int i = 0; i < 300; ++i) {
        const State s = rng() % 4 == 0 ? State{} : MakeState(rng);
        const cGZPersistResourceKey key = PickKey(rng, s);
        cGZDBPFIndexRecord record{1, 2, 3};
        struct Case {
            uint32_t rva;
            unsigned slot;
        };
        const Case cases[] = {{0x147480, 1}, {0x9ad640, 2}, {0x18a5c0, 12}, {0x1b9a50, 13}, {0x147110, 14}, {0x140fe0, 23}};
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
                case 12: results[r] = reinterpret_cast<uint32_t(__thiscall *)(void *, const void *, uint32_t, uint32_t)>(fn)(mem, &key, 1, 1); break;
                case 13: reinterpret_cast<void(__thiscall *)(void *, const void *, const void *)>(fn)(mem, &key, &record); break;
                case 14: results[r] = reinterpret_cast<uint8_t(__thiscall *)(void *, const void *, void *, uint32_t, uint32_t)>(fn)(mem, &key, &record, 1, 1); break;
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
