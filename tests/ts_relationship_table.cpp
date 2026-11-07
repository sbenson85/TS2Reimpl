// cTSRelationshipTable (0x40 bytes, 2 vtables): differential tests.
//
// Standalone shape (docs/standalone-shape.md): the original side's table is built in place by the original constructor 0x55ab10
// (game vtables) and its code is called at its RVA; ours is built by our constructor through the constructor hook's entry
// (cTSRelationshipTableAdapters::Construct) and called through its own vtable slot (CallSlot), as the game's callers reach it.
// The non-virtual members LoadEntry and SetEntry are called directly on each side's object. Fakes that patch the object's vtables
// patch a copy of the side's own vtables (`primary`, `secondary`), so the slot under test is still the side's own. The vtable
// words are the only bytes that differ by design: Code() maps each to "the side's own" (1, 2) or "the patched copy" (3, 4), and
// entries' vtable words to a flag (the game's on the original side, ours on our side). RelationshipTable_Slots covers the slots
// no other test calls; RelationshipTable_Adapters the RVA hooks' entry points on a table carrying the game's vtables.
#include "difftest.h"
#include "free_recorder.h"
#include "free_scope.h"
#include "ts/cTSRelationshipEntry.h"
#include "ts/cTSRelationshipTable.h"
#include "game/game.h"
#include "shape.h"
#include <Windows.h>
#include <array>
#include <cstring>
#include <memory>
#include <random>
#include <vector>
#include <cstdlib>
#include <new>

namespace {
using Table = cTSRelationshipTable;
using TA = cTSRelationshipTableAdapters;
struct HashLayout {
    float load;
    struct Node { Node *next, *previous; uint32_t person; void *entry; } *head;
    uint32_t count;
    Node **begin, **end, **capacity;
    uint32_t mask, buckets;
};
static_assert(sizeof(HashLayout) == sizeof(RelationshipMap));
struct Guarded { uint8_t before[16]; alignas(4) uint8_t object[64]; uint8_t after[16]; } work;
Table *Object() { return reinterpret_cast<Table *>(work.object); }
uint8_t *Face4() { return work.object + reltable::kSerialFace; }
HashLayout &Hash() { return *reinterpret_cast<HashLayout *>(&Object()->mEntries); }
// The block HeapFree receives for the bucket array: allocations of 4096 bytes or more are aligned by the allocator, which
// keeps the heap block's address in the word before the array. Called while the array is live: after the free that
// word is freed memory.
const void *BucketHeapBlock(const HashLayout &hash)
{
    if (!hash.begin) return nullptr;
    const size_t bytes = size_t(reinterpret_cast<const uint8_t *>(hash.capacity) - reinterpret_cast<const uint8_t *>(hash.begin));
    return bytes >= 4096 ? reinterpret_cast<void *const *>(hash.begin)[-1] : hash.begin;
}
// The modified flag's raw byte (a bool member; the original moves the byte as it is).
uint8_t &ModifiedByte() { return work.object[0x18]; }

// Slot `slot` of the vtable at `face` (the object's own), called with `this` = face.
template <class R, class... A> R CallSlot(const void *face, int slot, A... args)
{
    using Fn = R(__thiscall *)(const void *, A...);
    return (*static_cast<Fn *const *>(face))[slot](face, args...);
}

// ---- the sides ----
int gSide; // 0: the original's object and code, 1: ours
const void *OurTableVT(int i)
{
    static const void *ours[2];
    if (!ours[0]) {
        alignas(4) uint8_t block[sizeof(Table)];
        Table *table = TA::Construct(block, nullptr);
        std::memcpy(ours, block, sizeof(ours));
        std::destroy_at(&table->mEntries);
    }
    return ours[i];
}
const void *OurEntryVT(int i)
{
    static const void *ours[3];
    if (!ours[0]) {
        auto *entry = new cTSRelationshipEntry(cGZPersistResourceKey{});
        std::memcpy(ours, entry, sizeof(ours));
        entry->Release(); // count 0: destroyed
    }
    return ours[i];
}
const void *OwnTableVT(int i)
{
    return gSide ? OurTableVT(i) : GameAddress<void>(i ? Table::kOriginalSerialVTable : Table::kOriginalVTable);
}
const uint32_t kGameEntryVT[3] = {cTSRelationshipEntry::kOriginalVTable, cTSRelationshipEntry::kOriginalPersistVTable,
    cTSRelationshipEntry::kOriginalSerialVTable};
const void *OwnEntryVT(int i) { return gSide ? OurEntryVT(i) : GameAddress<void>(kGameEntryVT[i]); }

// The side's table in work.object: the original constructor, or ours through the constructor hook's entry.
void Build(difftest::Context &ctx)
{
    if (gSide) TA::Construct(work.object, nullptr);
    else ctx.Original<Table *(__thiscall *)(void *)>(0x55ab10)(work.object);
}
// Frees the map (and so the entries' references) without either side's destructor (tested on its own).
void Teardown() { std::destroy_at(&Object()->mEntries); }

void *primary[18]{}, *secondary[6]{}, *entryVT[3]{};
// Copies of the side's own vtables (the caller patches the slots it fakes), installed in the object.
void InstallCopies(bool primaryCopy = true)
{
    std::memcpy(primary, OwnTableVT(0), sizeof(primary));
    std::memcpy(secondary, OwnTableVT(1), sizeof(secondary));
    if (primaryCopy) *reinterpret_cast<void **>(work.object) = primary;
    *reinterpret_cast<void **>(Face4()) = secondary;
}
// A table vtable word: 1 / 2 the side's own (and ours are not the game's), 3 / 4 the patched copies, otherwise the raw word.
uint32_t Code(const void *word)
{
    const bool distinct = OurTableVT(0) != GameAddress<void>(Table::kOriginalVTable) &&
        OurTableVT(1) != GameAddress<void>(Table::kOriginalSerialVTable);
    if (distinct && word == OwnTableVT(0)) return 1;
    if (distinct && word == OwnTableVT(1)) return 2;
    if (word == primary) return 3;
    if (word == secondary) return 4;
    return uint32_t(reinterpret_cast<uintptr_t>(word));
}
void NormalizeVTableWords(Guarded &g)
{
    const void *words[2];
    std::memcpy(words, g.object, sizeof(words));
    const uint32_t codes[2] = {Code(words[0]), Code(words[1])};
    std::memcpy(g.object, codes, sizeof(codes));
}

struct Fake { void **vt; uint32_t id; uint32_t calls; } entries[4], storage, storage2;
std::array<uint32_t, 2048> trace{};
size_t traceSize;
int throwAt, ticks;
uint32_t streamToken, forwardResult;
uint64_t injected;
void Record(uint32_t a, uint32_t b, uint32_t c = 0, uint32_t d = 0) {
    trace[traceSize++] = a; trace[traceSize++] = b; trace[traceSize++] = c; trace[traceSize++] = d;
}
void Tick() { if (ticks++ == throwAt) { ++injected; throw 71; } }
uint32_t __fastcall Add(Table *self, void *) {
    Record(1, self->mRefCount); Tick(); return ++self->mRefCount;
}
void *__fastcall Delete(Table *self, void *, uint8_t flags) {
    Record(14, flags, self->mRefCount); Tick(); ModifiedByte() ^= 0xa5; return self;
}
uint32_t Forward(void *face, void *stream, uint32_t slot) {
    Record(slot, uint32_t(face == Face4()), uint32_t(stream == &streamToken), Object()->mOwnerPerson);
    Tick(); ModifiedByte() ^= 0x6e; ++streamToken; return forwardResult;
}
uint32_t __fastcall ForwardWrite(void *face, void *, void *stream) { return Forward(face, stream, 3); }
uint32_t __fastcall ForwardRead(void *face, void *, void *stream) { return Forward(face, stream, 4); }
uint32_t __fastcall FakeAdd(Fake *self, void *) { Record(92, self->id); ++self->calls; return 17; }
uint32_t __fastcall FakeRelease(Fake *self, void *) {
    Record(2, self->id, Object()->mOwnerPerson, Hash().count);
    // The table's vtable words at the time (the destructor stores them back before it releases).
    Record(91, Code(*reinterpret_cast<void **>(work.object)), Code(*reinterpret_cast<void **>(Face4())), uint32_t(Hash().begin != nullptr));
    ++self->calls; return 23;
}
void Setup() {
    entryVT[1] = reinterpret_cast<void *>(&FakeAdd);
    entryVT[2] = reinterpret_cast<void *>(&FakeRelease);
    for (uint32_t i = 0; i < 4; ++i) entries[i] = {entryVT, 100 + i, 0};
    storage = {entryVT, 200, 0};
    storage2 = {entryVT, 201, 0};
}
// The PlainCases fakes: AddRef and the deleting destructor in the primary copy, Write / Read in the serializable copy.
void InstallPlainFakes()
{
    InstallCopies();
    primary[1] = reinterpret_cast<void *>(&Add);
    primary[14] = reinterpret_cast<void *>(&Delete);
    secondary[3] = reinterpret_cast<void *>(&ForwardWrite);
    secondary[4] = reinterpret_cast<void *>(&ForwardRead);
}
Guarded Seed(uint32_t seed) {
    Guarded value; std::mt19937 random(seed);
    for (auto &byte : value.before) byte = uint8_t(random());
    for (auto &byte : value.object) byte = uint8_t(random());
    for (auto &byte : value.after) byte = uint8_t(random());
    return value;
}
struct Result {
    Guarded bytes;
    std::array<uint32_t, 2048> calls;
    size_t count;
    std::array<uint32_t, 6> fakeCalls;
    uint32_t stream, returned;
    bool threw;
};
// Address-normalize only owned allocation pointers, after comparing every defined
// byte of their contents. Sentinel payload (+8,+12) is never constructed by either
// allocator and is genuinely uninitialized; it is deliberately not compared.
Guarded NormalizeObject(Guarded value, const HashLayout &layout) {
    auto *object = reinterpret_cast<Table *>(value.object);
    auto &hash = *reinterpret_cast<HashLayout *>(&object->mEntries);
    hash.head = reinterpret_cast<HashLayout::Node *>(0x100);
    hash.begin = layout.begin ? reinterpret_cast<HashLayout::Node **>(0x200) : nullptr;
    hash.end = layout.end ? reinterpret_cast<HashLayout::Node **>(0x200 + (layout.end - layout.begin) * 4) : nullptr;
    hash.capacity = layout.capacity ? reinterpret_cast<HashLayout::Node **>(0x200 + (layout.capacity - layout.begin) * 4) : nullptr;
    NormalizeVTableWords(value);
    return value;
}
Result Capture(uint32_t returned, bool threw, const HashLayout &layout) {
    return {NormalizeObject(work, layout), trace, traceSize,
        {entries[0].calls, entries[1].calls, entries[2].calls, entries[3].calls, storage.calls, storage2.calls},
        streamToken, returned, threw};
}
void Compare(difftest::Context &ctx, const Result &a, const Result &b) {
    if (std::memcmp(&a.bytes, &b.bytes, sizeof(Guarded)) || a.count != b.count ||
        a.calls != b.calls || a.fakeCalls != b.fakeCalls || a.stream != b.stream ||
        a.returned != b.returned || a.threw != b.threw) ctx.Fail("case %llu: bytes/return/trace/output/exception differ", ctx.cases);
    ++ctx.cases;
}
void Reset(const Guarded &seed) {
    work = seed; Setup(); trace.fill(0); traceSize = 0; ticks = 0;
    streamToken = 0x11223344;
}
void Populate(uint32_t count) {
    for (uint32_t i = 0; i < count; ++i)
        Object()->mEntries.try_emplace(i * 0x12345u + 7, i % 3 ? &entries[i % 4] : nullptr);
}
// A table built the side's way with seeded scalar fields (reference count .. padding) and fake vtable copies.
void BuildPlain(difftest::Context &ctx, const Guarded &seed)
{
    Reset(seed);
    Build(ctx);
    std::memcpy(work.object + 8, seed.object + 8, 0x14); // +0x08 .. +0x1b
    InstallPlainFakes();
}
template<class Call> void PlainCases(difftest::Context &ctx, Call call) {
    uint32_t zero = 0, nonzero = 0, exceptions = 0;
    for (uint32_t i = 0; i < 128; ++i) {
        const auto seed = Seed(0xabc000 + i);
        Result results[2];
        for (gSide = 0; gSide < 2; ++gSide) {
            BuildPlain(ctx, seed);
            throwAt = i % 4 == 0 ? 0 : -1;
            uint32_t returned = 0; bool threw = false;
            try { returned = call(gSide != 0, i); } catch (int) { threw = true; }
            if (!gSide) { if (threw) ++exceptions; else if (returned) ++nonzero; else ++zero; }
            results[gSide] = Capture(returned, threw, Hash());
            Teardown();
        }
        Compare(ctx, results[0], results[1]);
    }
    std::printf("  Branch coverage: zero=%u nonzero=%u injected=%u\n", zero, nonzero, exceptions);
}
}

DIFF_TEST(RelationshipTable_Construct, 0x55ab10) {
    for (uint32_t i = 0; i < 128; ++i) {
        const auto seed = Seed(i + 0x445500); Guarded results[2];
        for (gSide = 0; gSide < 2; ++gSide) {
            Reset(seed);
            void *returned = gSide ? TA::Construct(work.object, nullptr) :
                ctx.Original<Table *(__thiscall *)(void *)>(0x55ab10)(work.object);
            const auto hash = Hash();
            if (returned != Object() || hash.head->next != hash.head || hash.head->previous != hash.head ||
                hash.end - hash.begin != 16 || hash.capacity != hash.end || hash.mask != 7 || hash.buckets != 8 ||
                hash.load != 1 || hash.count) ctx.Fail("bad initial container");
            for (uint32_t bucket = 0; bucket < 16; ++bucket)
                if (hash.begin[bucket] != hash.head) ctx.Fail("bucket %u not sentinel", bucket);
            results[gSide] = NormalizeObject(work, hash);
            const void *bucketBlock = BucketHeapBlock(hash); // read while the array is live (the word before it)
            difftest::BeginFrees();
            Teardown();
            const bool headFreed = difftest::FreedSinceBegin(hash.head), bucketsFreed = difftest::FreedSinceBegin(bucketBlock);
            if (!headFreed || !bucketsFreed) ctx.Fail("constructor cleanup leaked");
        }
        if (std::memcmp(&results[0], &results[1], sizeof(Guarded))) ctx.Fail("constructor object/padding/guards differ");
        ++ctx.cases;
    }
}
namespace {
// Redirect only allocation entries, for the duration of a constructor call.
// Fixed records and malloc avoid allocating recursively through the redirect.
int allocationFailure, allocations;
void *allocatedBlocks[4];
uint32_t allocationSizes[4];
void *__cdecl AllocateForConstructor(size_t bytes) {
    const int index = allocations++;
    allocationSizes[index] = uint32_t(bytes);
    if (index == allocationFailure) throw std::bad_alloc();
    void *block = std::malloc(bytes);
    if (!block) throw std::bad_alloc();
    std::memset(block, 0x7b, bytes);
    allocatedBlocks[index] = block;
    if (index == 0) difftest::BeginFrees(); // from the first block on: does the failed constructor free them
    return block;
}
// The code stays writable after the restore: putting back the old protection VirtualProtect reported (the first page's)
// narrows both pages when the five bytes straddle a page boundary (lane MW, tests/app_fixture.h).
struct AllocationRedirect {
    uint8_t *site, saved[5];
    explicit AllocationRedirect(void *address) : site(static_cast<uint8_t *>(address)) {
        std::memcpy(saved, site, 5);
        DWORD old; VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t displacement = int32_t(reinterpret_cast<uintptr_t>(&AllocateForConstructor) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xe9; std::memcpy(site + 1, &displacement, 4);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    ~AllocationRedirect() {
        DWORD old; VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(site, saved, 5);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
};
}
DIFF_TEST(RelationshipTable_ConstructAllocationFailure, 0x55ab10) {
    (void)OurTableVT(0); // built before the redirect
    for (allocationFailure = 0; allocationFailure < 2; ++allocationFailure) {
        Guarded results[2]; std::array<uint32_t, 4> sizes[2];
        for (gSide = 0; gSide < 2; ++gSide) {
            Reset(Seed(0x667788)); allocations = 0;
            std::memset(allocatedBlocks, 0, sizeof(allocatedBlocks)); std::memset(allocationSizes, 0, sizeof(allocationSizes));
            bool threw = false;
            {
                using New = void *(__cdecl *)(size_t);
                AllocationRedirect original(ctx.image.At(0x824632));
                AllocationRedirect replacement(reinterpret_cast<void *>(static_cast<New>(&::operator new)));
                try {
                    if (gSide) TA::Construct(work.object, nullptr);
                    else ctx.Original<Table *(__thiscall *)(void *)>(0x55ab10)(work.object);
                } catch (const std::bad_alloc &) { threw = true; }
            }
            if (!threw || allocations != allocationFailure + 1) ctx.Fail("allocation failure was not reached");
            for (int i = 0; i < allocations; ++i)
                if (allocatedBlocks[i] && !difftest::FreedSinceBegin(allocatedBlocks[i])) ctx.Fail("failed constructor leaked");
            results[gSide] = work;
            if (allocatedBlocks[0]) reinterpret_cast<HashLayout *>(results[gSide].object + 0x1c)->head = reinterpret_cast<HashLayout::Node *>(0x100);
            NormalizeVTableWords(results[gSide]);
            std::copy(std::begin(allocationSizes), std::end(allocationSizes), sizes[gSide].begin());
        }
        if (std::memcmp(&results[0], &results[1], sizeof(Guarded)) || sizes[0] != sizes[1]) ctx.Fail("failed constructor state/allocations differ");
        ++ctx.cases;
    }
}
DIFF_TEST(RelationshipTable_QueryInterface, 0x55b310) {
    const uint32_t ids[] = {1, 0xcc364c23, 0xe4fda3d4, 0, 0xcc364c2a, 0xffffffff, 2, 0xe56b8f03};
    PlainCases(ctx, [&](bool ours, uint32_t i) {
        struct Output { uint32_t before; void *value; uint32_t after; } out{0x12340000 + i, reinterpret_cast<void *>(0x99887766), 0xaabb0000 + i};
        uint8_t result;
        const auto note = [&] {
            const uint32_t value = out.value == Object() ? 1 : out.value == Face4() ? 2 : uint32_t(reinterpret_cast<uintptr_t>(out.value));
            Record(90, out.before, value, out.after);
        };
        try {
            result = ours ? CallSlot<uint8_t>(Object(), 0, ids[i % 8], &out.value) :
                ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(0x55b310)(Object(), ids[i % 8], &out.value);
        } catch (...) {
            note();
            throw;
        }
        note();
        return uint32_t(result);
    });
}
DIFF_TEST(RelationshipTable_Release, 0x55baf0) {
    const uint32_t counts[] = {0, 1, 2, 3, 0x7fffffff, 0x80000000, 0xfffffffe, 0xffffffff};
    PlainCases(ctx, [&](bool ours, uint32_t i) {
        Object()->mRefCount = counts[i % 8];
        return ours ? CallSlot<uint32_t>(Object(), 2) : ctx.Original<uint32_t(__thiscall *)(void *)>(0x55baf0)(Object());
    });
}
DIFF_TEST(RelationshipTable_GetGZCLSID, 0x55b000) {
    PlainCases(ctx, [&](bool ours, uint32_t) {
        // The entry is on the +4 face, even though its body does not inspect this.
        return ours ? CallSlot<uint32_t>(Face4(), 5) : ctx.Original<uint32_t(__thiscall *)(void *)>(0x55b000)(Face4());
    });
}
DIFF_TEST(RelationshipTable_WriteForPerson, 0x55bec0) {
    PlainCases(ctx, [&](bool ours, uint32_t i) {
        forwardResult = 0x98765400 + i; const uint32_t person = i * 0xdead0123;
        void *stream = &streamToken;
        return ours ? CallSlot<uint32_t>(Object(), 3, stream, person) :
            ctx.Original<uint32_t(__thiscall *)(void *, void *, uint32_t)>(0x55bec0)(Object(), stream, person);
    });
}
DIFF_TEST(RelationshipTable_ReadForPerson, 0x55b2a0) {
    PlainCases(ctx, [&](bool ours, uint32_t i) {
        forwardResult = 0xfedcba00 + i; const uint32_t person = i * 0xbeef0123;
        void *stream = &streamToken;
        return ours ? CallSlot<uint32_t>(Object(), 4, stream, person) :
            ctx.Original<uint32_t(__thiscall *)(void *, void *, uint32_t)>(0x55b2a0)(Object(), stream, person);
    });
}

namespace {
void IteratorCases(difftest::Context &ctx, bool next) {
    uint32_t empty = 0, missing = 0, last = 0, successor = 0, first = 0;
    for (uint32_t count : {0u, 1u, 2u, 7u, 8u, 9u, 32u, 256u}) {
        std::vector<uint32_t> results[2];
        for (gSide = 0; gSide < 2; ++gSide) {
            Reset(Seed(count)); Build(ctx); Populate(count);
            for (uint32_t i = 0; i < count + 3; ++i) {
                const Guarded before = work; const uint32_t person = i * 0x12345u + 7;
                std::vector<uint8_t> buckets(reinterpret_cast<uint8_t *>(Hash().begin), reinterpret_cast<uint8_t *>(Hash().end));
                std::vector<std::pair<HashLayout::Node *, HashLayout::Node>> nodes;
                for (auto *node = Hash().head->next; node != Hash().head; node = node->next) nodes.push_back({node, *node});
                const auto links = std::make_pair(Hash().head->next, Hash().head->previous);
                const uint32_t r = next ?
                    (gSide ? CallSlot<uint32_t>(Object(), 13, person) : ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x55b010)(Object(), person)) :
                    (gSide ? CallSlot<uint32_t>(Object(), 12) : ctx.Original<uint32_t(__thiscall *)(void *)>(0x55afd0)(Object()));
                results[gSide].push_back(r);
                if (!gSide) {
                    if (!count) ++empty;
                    if (next) {
                        if (i >= count) ++missing;
                        else if (r == UINT32_MAX) ++last;
                        else ++successor;
                    } else if (count) ++first;
                }
                if (std::memcmp(&before, &work, sizeof(work))) ctx.Fail("iterator changed the object count=%u key=%u", count, person);
                if (std::memcmp(Hash().begin, buckets.data(), buckets.size()) || Hash().head->next != links.first || Hash().head->previous != links.second)
                    ctx.Fail("iterator mutated bucket/sentinel links");
                for (const auto &node : nodes) if (std::memcmp(node.first, &node.second, 16)) ctx.Fail("iterator changed node");
            }
            Teardown();
        }
        if (results[0] != results[1]) ctx.Fail("iterator results differ count=%u", count);
        ++ctx.cases;
    }
    std::printf("  Iterator coverage: empty=%u missing=%u last=%u successor=%u first=%u\n", empty, missing, last, successor, first);
}
}
DIFF_TEST(RelationshipTable_GetFirstPerson, 0x55afd0) { IteratorCases(ctx, false); }
DIFF_TEST(RelationshipTable_GetNextPerson, 0x55b010) { IteratorCases(ctx, true); }
// Slot 14: the original's deleting destructor at its RVA, ours through our vtable's slot 14 (the compiler's scalar deleting
// destructor). The primary word is an unpatched copy of the side's own vtable and the serializable word a junk copy, so the
// destructor's stores of both are visible (also at each entry release: FakeRelease records the words).
DIFF_TEST(RelationshipTable_DeletingDestructor, 0x55abf0) {
    for (uint32_t i = 0; i < 64; ++i) {
        const auto seed = Seed(0x887700 + i); Result results[2];
        for (gSide = 0; gSide < 2; ++gSide) {
            Reset(seed); Build(ctx); Populate(i < 60 ? i % 12 : 256);
            Object()->mStorage = i & 1 ? &storage : nullptr;
            Object()->mOwnerPerson = 0xaabb0000 + i;
            InstallCopies();
            std::memset(secondary, 0, sizeof(secondary));
            const HashLayout layout = Hash();
            HashLayout::Node *nodes[256]{}; uint32_t n = 0;
            for (auto *node = layout.head->next; node != layout.head; node = node->next) nodes[n++] = node;
            trace.fill(0); traceSize = 0;
            for (auto &entry : entries) entry.calls = 0;
            const uint8_t flags = uint8_t((i * 2) & 0xfe);
            const void *bucketBlock = BucketHeapBlock(layout); // read while the array is live (the word before it)
            difftest::BeginFrees(); // the blocks are judged by a FreeRecorder across the call
            void *returned = gSide ? CallSlot<void *>(Object(), 14, uint32_t(flags)) :
                ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x55abf0)(Object(), flags);
            bool leaked = !difftest::FreedSinceBegin(layout.head) || !difftest::FreedSinceBegin(bucketBlock);
            for (uint32_t j = 0; j < n; ++j) leaked |= !difftest::FreedSinceBegin(nodes[j]);
            if (leaked) ctx.Fail("destructor leaked an allocation");
            if (returned != Object()) ctx.Fail("destructor return differs");
            results[gSide] = Capture(0, false, Hash());
        }
        Compare(ctx, results[0], results[1]);
    }
    for (uint32_t i = 0; i < 32; ++i) {
        for (gSide = 0; gSide < 2; ++gSide) {
            Reset(Seed(i));
            auto *self = static_cast<uint8_t *>(::operator new(sizeof(Table)));
            std::memcpy(self, work.object, 64);
            if (gSide) TA::Construct(self, nullptr);
            else ctx.Original<Table *(__thiscall *)(void *)>(0x55ab10)(self);
            difftest::FreeRecorder rec;
            void *returned = gSide ? CallSlot<void *>(self, 14, uint32_t(i * 2 + 1)) :
                ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x55abf0)(self, uint8_t(i * 2 + 1));
            rec.Stop();
            if (returned != self || !rec.Freed(self)) ctx.Fail("deleting branch did not free self");
        }
        ++ctx.cases;
    }
}

namespace {
namespace Writer {
struct Key { uint32_t words[4]; };
struct Resource { void **vt; uint32_t id; Key key; };
struct Entry { void **vt; uint32_t id, calls; Resource resource; };
void *streamVT[10]{}, *serviceVT[35]{}, *storageVT[25]{}, *writerEntryVT[6]{}, *resourceVT[5]{};
Fake stream{streamVT, 300, 0}, keyed{streamVT, 301, 0}, service{serviceVT, 302, 0};
Fake store{storageVT, 303, 0}, output{entryVT, 304, 0};
Entry values[7];
Key context{{0x12345678, 0x98765432, 0xaabbccdd, 0x77665544}};
uint32_t mode;
uint64_t querySuccess, queryFailure, noStorage, nullEntries, openSuccess, openFailure,
    createSuccess, createFailure, closeCalls, writeCalls, exceptions;
uint8_t __fastcall Query(Fake *self, void *, uint32_t iid, void **out) {
    Record(0, self->id, iid); *out = mode & 1 ? &keyed : nullptr; Tick();
    if (mode & 1) ++querySuccess; else ++queryFailure;
    return mode & 1 ? 0x80 : 0;
}
uint32_t __fastcall ReleaseFake(Fake *self, void *) { Record(2, self->id); return ++self->calls; }
void *__fastcall GetContext(Fake *self, void *) { Record(30, self->id); Tick(); return &context; }
uint32_t __fastcall ClassId(void *face, void *) {
    Record(5, uint32_t(face == Face4())); Tick(); return 0xfedcba98;
}
uint8_t __fastcall GetStorage(Fake *self, void *, Key *key, uint32_t iid, void **out) {
    Record(34, self->id, iid); for (auto word : key->words) Record(100, word);
    *out = mode & 2 ? &store : nullptr; if (!*out) ++noStorage; Tick(); return 0;
}
uint8_t __fastcall Put8(Fake *self, void *, uint32_t value) { Record(8, self->id, value); Tick(); return uint8_t(mode & 1); }
uint8_t __fastcall Put9(Fake *self, void *, uint32_t value) { Record(9, self->id, value); Tick(); return uint8_t(mode & 2); }
uint32_t __fastcall EntryAdd(Entry *self, void *) { Record(1, self->id); Tick(); return ++self->calls; }
uint32_t __fastcall EntryRelease(Entry *self, void *) { Record(2, self->id); return ++self->calls; }
void *__fastcall GetResource(Entry *self, void *) { Record(3, self->id); Tick(); return &self->resource; }
void __fastcall GetKey(Resource *self, void *, Key *out) { Record(13, self->id); *out = self->key; Tick(); }
void __fastcall SetKey(Resource *self, void *, Key *key) {
    Record(14, self->id); for (auto word : key->words) Record(100, word);
    self->key = *key; Tick();
}
uint8_t __fastcall Open(Fake *self, void *, Key *key, void **out, uint32_t access) {
    Record(22, self->id, access); for (auto word : key->words) Record(100, word);
    *out = mode & 8 ? &output : nullptr; Tick();
    if (mode & 4) ++openSuccess; else ++openFailure;
    return mode & 4 ? 0xfe : 0;
}
uint8_t __fastcall Create(Fake *self, void *, Key *key, void **out) {
    Record(23, self->id); for (auto word : key->words) Record(100, word);
    *out = mode & 32 ? &output : nullptr; Tick();
    if (mode & 16) ++createSuccess; else ++createFailure;
    return mode & 16 ? 0x7f : 0;
}
uint8_t __fastcall WriteEntry(Entry *self, void *, void *target) {
    Record(5, self->id, uint32_t(target == &output), uint32_t(target == nullptr));
    ++writeCalls; Tick(); return 0;
}
void __fastcall Close(Fake *self, void *, void *target) { Record(24, self->id, uint32_t(target == &output)); ++closeCalls; Tick(); }
void __fastcall Flush(Fake *self, void *) { Record(10, self->id); Tick(); }
void SetupWriter() {
    streamVT[0] = reinterpret_cast<void *>(&Query);
    streamVT[2] = reinterpret_cast<void *>(&ReleaseFake);
    streamVT[3] = reinterpret_cast<void *>(&GetContext);
    streamVT[8] = reinterpret_cast<void *>(&Put8);
    streamVT[9] = reinterpret_cast<void *>(&Put9);
    serviceVT[34] = reinterpret_cast<void *>(&GetStorage);
    storageVT[2] = reinterpret_cast<void *>(&ReleaseFake);
    storageVT[10] = reinterpret_cast<void *>(&Flush);
    storageVT[22] = reinterpret_cast<void *>(&Open);
    storageVT[23] = reinterpret_cast<void *>(&Create);
    storageVT[24] = reinterpret_cast<void *>(&Close);
    output.vt = streamVT;
    writerEntryVT[1] = reinterpret_cast<void *>(&EntryAdd);
    writerEntryVT[2] = reinterpret_cast<void *>(&EntryRelease);
    writerEntryVT[3] = reinterpret_cast<void *>(&GetResource);
    writerEntryVT[5] = reinterpret_cast<void *>(&WriteEntry);
    resourceVT[3] = reinterpret_cast<void *>(&GetKey);
    resourceVT[4] = reinterpret_cast<void *>(&SetKey);
    for (uint32_t i = 0; i < 7; ++i)
        values[i] = {writerEntryVT, 400 + i, 0, {resourceVT, 500 + i,
            {{0xaaaa0000 + i, 0xbbbb0000 + i, 0xcccc0000 + i, 0xdddd0000 + i}}}};
    stream.calls = keyed.calls = service.calls = store.calls = output.calls = 0;
}
struct Snapshot { Result result; std::array<Entry, 7> entries; std::array<uint32_t, 5> calls; };
}
}
DIFF_TEST(RelationshipTable_Write, 0x55c170) {
    using namespace Writer;
    void **cache = GameAddress<void *>(0x10faf6c); void *oldCache = *cache; *cache = &service;
    for (mode = 0; mode < 64; ++mode) for (uint32_t count : {0u, 1u, 7u}) {
        const Guarded seed = Seed(0x456700 + mode * 7 + count);
        int totalTicks = 0;
        for (int fail = -1; fail < totalTicks; ++fail) {
            Snapshot results[2];
            for (gSide = 0; gSide < 2; ++gSide) {
                Reset(seed); Build(ctx); SetupWriter(); throwAt = -1;
                for (uint32_t i = 0; i < count; ++i) {
                    if (i % 3 == 2 && fail == -1 && !gSide) ++nullEntries;
                    Object()->mEntries.try_emplace(i * 0x12345 + 7, i % 3 == 2 ? nullptr : &values[i]);
                }
                InstallCopies(false);
                secondary[5] = reinterpret_cast<void *>(&ClassId);
                // Write never changes the container's heap: every node and bucket byte stays.
                std::vector<std::pair<void *, std::vector<uint8_t>>> heap;
                heap.push_back({Hash().begin, std::vector<uint8_t>(reinterpret_cast<uint8_t *>(Hash().begin), reinterpret_cast<uint8_t *>(Hash().end))});
                for (auto *node = Hash().head->next; node != Hash().head; node = node->next)
                    heap.push_back({node, std::vector<uint8_t>(reinterpret_cast<uint8_t *>(node), reinterpret_cast<uint8_t *>(node) + 16)});
                for (auto &value : values) value.calls = 0;
                trace.fill(0); traceSize = 0; ticks = 0; throwAt = fail;
                uint8_t returned = 0; bool threw = false;
                try {
                    void *target = &stream;
                    returned = gSide ? CallSlot<uint8_t>(Face4(), 3, target) :
                        ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x55c170)(Face4(), target);
                } catch (int) { threw = true; if (!gSide) ++exceptions; }
                throwAt = -1;
                if (fail == -1 && !gSide) totalTicks = ticks;
                results[gSide].result = Capture(returned, threw, Hash());
                std::copy(std::begin(values), std::end(values), results[gSide].entries.begin());
                results[gSide].calls = {stream.calls, keyed.calls, service.calls, store.calls, output.calls};
                for (const auto &block : heap)
                    if (std::memcmp(block.first, block.second.data(), block.second.size())) ctx.Fail("Write changed container heap");
                Teardown();
            }
            Compare(ctx, results[0].result, results[1].result);
            if (std::memcmp(results[0].entries.data(), results[1].entries.data(), sizeof(values)) || results[0].calls != results[1].calls)
                ctx.Fail("Write resource keys or ref effects differ");
        }
    }
    *cache = oldCache;
    std::printf("  Write coverage (original side): query=%llu/%llu noStorage=%llu nullEntries=%llu open=%llu/%llu create=%llu/%llu close=%llu write=%llu injected=%llu\n",
        querySuccess, queryFailure, noStorage, nullEntries, openSuccess, openFailure, createSuccess, createFailure, closeCalls, writeCalls, exceptions);
}

// Lane CX: the +4 face's thunks, Read, and the value accessors (slots 5-10 and their helpers).
// Entries are real cTSRelationshipEntry objects, built by each side's own code (the original's with the game's vtables,
// ours with ours); the streams, records, segment, resource manager and persist system are recording fakes.
DIFF_TEST(RelationshipTable_FaceQueryInterface, 0x55b359) {
    PlainCases(ctx, [&](bool ours, uint32_t i) -> uint32_t {
        static const uint32_t ids[] = {1, 0xcc364c23, 0xe4fda3d4, 0x12345, 0};
        void *out = reinterpret_cast<void *>(0x5a5a5a5a);
        const uint8_t returned = ours ? CallSlot<uint8_t>(Face4(), 0, ids[i % 5], &out) :
            ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(0x55b359)(Face4(), ids[i % 5], &out);
        Record(77, uint32_t(out == Object()) | uint32_t(out == Face4()) << 1 |
            uint32_t(out == reinterpret_cast<void *>(0x5a5a5a5a)) << 2);
        return returned;
    });
}
DIFF_TEST(RelationshipTable_FaceRelease, 0x55bb07) {
    PlainCases(ctx, [&](bool ours, uint32_t i) -> uint32_t {
        if (i % 3 == 0) Object()->mRefCount = i % 2;
        return ours ? CallSlot<uint32_t>(Face4(), 2) : ctx.Original<uint32_t(__thiscall *)(void *)>(0x55bb07)(Face4());
    });
}

namespace {
namespace Lane {
struct Key { uint32_t words[4]; };
struct Stub { void **vt; uint32_t id; uint32_t releases; };
struct GameEntry { void *vt[3]; uint32_t refCount; Key key; uint32_t *begin, *end, *capacity; };
static_assert(sizeof(GameEntry) == 0x2c);
std::vector<uint32_t> callLog;
int failAt = -1, tick;
uint64_t thrown;
std::vector<uint32_t> script;
size_t cursor;
uint32_t mode; // 1 keyed record, 2 segment found, 4 record opens, 8 old storage, 16 old entries
Key recordKey{{0x5eed0001, 0x6a0b1234, 0x00070000, 0x99887766}};
void *ioVT[10]{}, *managerVT[35]{}, *segmentVT[25]{}, *persistVT[18]{}, *oldVT[3]{};
Stub stream{ioVT, 1, 0}, keyRecord{ioVT, 2, 0}, entryRecord{ioVT, 3, 0}, manager{managerVT, 4, 0};
Stub segment{segmentVT, 5, 0}, oldStorage{segmentVT, 6, 0}, persist{persistVT, 7, 0}, olds[3];
void Note(uint32_t a, uint32_t b = 0, uint32_t c = 0) { callLog.push_back(a); callLog.push_back(b); callLog.push_back(c); }
void NoteKey(const Key *key) { for (auto word : key->words) callLog.push_back(word); }
void Step() { if (tick++ == failAt) { ++thrown; throw 72; } }
uint8_t __fastcall Query(Stub *self, void *, uint32_t iid, void **out) {
    Note(0, self->id, iid); Step();
    if (iid == 0x51b11bd) { *out = self; return 1; }
    *out = mode & 1 ? &keyRecord : nullptr;
    return mode & 1 ? 0x41 : 0;
}
uint32_t __fastcall Release(Stub *self, void *) { Note(2, self->id); return ++self->releases; }
uint32_t __fastcall AddRef(Stub *self, void *) { Note(1, self->id); return 9; }
void *__fastcall GetKey(Stub *self, void *) { Note(3, self->id); Step(); return &recordKey; }
uint8_t __fastcall Close(Stub *self, void *) { Note(6, self->id); Step(); return 1; }
template <uint32_t Slot> uint8_t __fastcall Get(Stub *self, void *, uint32_t *out) {
    Note(Slot, self->id, uint32_t(cursor)); Step();
    if (cursor < script.size()) { *out = script[cursor++]; return 1; }
    *out = 0; return 0; // a short stream reads zeros
}
uint8_t __fastcall FindSegmentWithKey(Stub *self, void *, Key *key, void **out) {
    Note(34, self->id); NoteKey(key); Step();
    *out = mode & 2 ? &segment : nullptr;
    return mode & 2 ? 1 : 0;
}
uint8_t __fastcall OpenRecord(Stub *self, void *, Key *key, void **out, uint32_t access) {
    Note(22, self->id, access); NoteKey(key); Step();
    *out = mode & 4 ? &entryRecord : nullptr;
    return mode & 4 ? 0x33 : 0;
}
uint8_t __fastcall DeleteResource(Stub *self, void *, Key *key) { Note(17, self->id); NoteKey(key); Step(); return 0; }
uint32_t __fastcall ClassId(void *face, void *) { Note(5, uint32_t(face == Face4())); Step(); return 0x13572468; }
void SetupFakes() {
    ioVT[0] = reinterpret_cast<void *>(&Query);
    ioVT[1] = reinterpret_cast<void *>(&AddRef);
    ioVT[2] = reinterpret_cast<void *>(&Release);
    ioVT[3] = reinterpret_cast<void *>(&GetKey);
    ioVT[6] = reinterpret_cast<void *>(&Close);
    ioVT[8] = reinterpret_cast<void *>(&Get<8>);
    ioVT[9] = reinterpret_cast<void *>(&Get<9>);
    managerVT[34] = reinterpret_cast<void *>(&FindSegmentWithKey);
    segmentVT[2] = reinterpret_cast<void *>(&Release);
    segmentVT[22] = reinterpret_cast<void *>(&OpenRecord);
    persistVT[17] = reinterpret_cast<void *>(&DeleteResource);
    oldVT[1] = reinterpret_cast<void *>(&AddRef);
    oldVT[2] = reinterpret_cast<void *>(&Release);
    for (uint32_t i = 0; i < 3; ++i) olds[i] = {oldVT, 40 + i, 0};
    for (Stub *stub : {&stream, &keyRecord, &entryRecord, &manager, &segment, &oldStorage, &persist}) stub->releases = 0;
}
// Everything observable about the table: fields, padding, guards, the vtable words (as codes), the list in order with each
// entry's contents (its vtable words as flags: the side's own entry vtables), the bucket vector as node indexes.
std::vector<uint32_t> State() {
    std::vector<uint32_t> s;
    auto *table = Object(); auto &hash = Hash();
    uint32_t load; std::memcpy(&load, &hash.load, 4);
    s.insert(s.end(), {table->mRefCount, table->mResourceType, table->mResourceGroup, table->mOwnerPerson,
        ModifiedByte(), table->mPadding[0], table->mPadding[1], table->mPadding[2],
        uint32_t(reinterpret_cast<uintptr_t>(table->mStorage)), Code(*reinterpret_cast<void **>(work.object)),
        Code(*reinterpret_cast<void **>(Face4())), hash.count, hash.mask, hash.buckets, load,
        uint32_t(hash.end - hash.begin), uint32_t(hash.capacity - hash.begin)});
    std::vector<HashLayout::Node *> order;
    for (auto *node = hash.head->next; node != hash.head && order.size() < 4096; node = node->next) {
        s.push_back(uint32_t(node->next->previous == node));
        order.push_back(node);
    }
    s.push_back(uint32_t(order.size()));
    for (auto *node : order) {
        s.push_back(node->person);
        auto *entry = static_cast<GameEntry *>(node->entry);
        if (!entry) { s.push_back(0); continue; }
        if (entry->vt[0] != GameAddress(kGameEntryVT[0]) && entry->vt[0] != OurEntryVT(0)) {
            s.push_back(1); s.push_back(static_cast<Stub *>(node->entry)->id); continue;
        }
        s.insert(s.end(), {2, uint32_t(entry->vt[0] == OwnEntryVT(0)), uint32_t(entry->vt[1] == OwnEntryVT(1)),
            uint32_t(entry->vt[2] == OwnEntryVT(2)), entry->refCount, entry->key.words[0], entry->key.words[1],
            entry->key.words[2], entry->key.words[3], uint32_t(entry->end - entry->begin), uint32_t(entry->capacity - entry->begin)});
        for (auto *value = entry->begin; value != entry->end; ++value) s.push_back(*value);
    }
    for (auto **bucket = hash.begin; bucket != hash.end; ++bucket) {
        uint32_t index = *bucket == hash.head ? 0xffffffff : 0xfffffffe;
        for (uint32_t i = 0; i < order.size(); ++i) if (order[i] == *bucket) index = i;
        s.push_back(index);
    }
    uint32_t guards[8]; std::memcpy(guards, work.before, 16); std::memcpy(guards + 4, work.after, 16);
    s.insert(s.end(), std::begin(guards), std::end(guards));
    return s;
}
struct Outcome {
    std::vector<uint32_t> state, calls;
    uint32_t returned; bool threw; size_t cursor;
    bool operator!=(const Outcome &o) const {
        return state != o.state || calls != o.calls || returned != o.returned || threw != o.threw || cursor != o.cursor;
    }
};
// A fresh table built the side's way with seeded fields, its serializable vtable a copy of its own with GetGZCLSID faked; old
// entries are fakes, old storage a fake segment.
void Prepare(difftest::Context &ctx, uint32_t seed) {
    work = Seed(seed); Build(ctx); SetupFakes();
    std::mt19937 random(seed);
    InstallCopies(false);
    secondary[5] = reinterpret_cast<void *>(&ClassId);
    Object()->mResourceType = random() % 3 == 0 ? 0 : random();
    Object()->mResourceGroup = random() % 3 == 0 ? 0 : random();
    Object()->mOwnerPerson = random() & 0x1ffff;
    ModifiedByte() = uint8_t(random());
    if (mode & 8) Object()->mStorage = &oldStorage;
    if (mode & 16) for (uint32_t i = 0; i < 3; ++i) Object()->mEntries.try_emplace(i * 11 + 6, &olds[i]);
}
void Finish() {
    failAt = -1;
    if (Object()->mStorage == &segment || Object()->mStorage == &oldStorage) Object()->mStorage = nullptr;
    Teardown();
}
std::vector<std::vector<uint32_t>> ReadScripts() {
    const uint32_t m1 = 0xffffffff, m2 = 0xfffffffe;
    std::vector<std::vector<uint32_t>> scripts = {
        {}, {3, 0}, {3, 3, 7, 0x10, 0x2000}, {3, 4, 6, 6, 17, 99}, {2, 2, 1, 2}, {3, 6, 1}, {2},
        {1}, {4, 1, 2}, {0x7fffffff}, {0, 3}, {0x80000000, 0},
        {m1, 0}, {m1, 3, 1, 42, 2, 10, 20, 0, 1, 43, 0}, {m1, 2, 1, 42, 1, 5, 1, 42, 2, 6, 7},
        {m1, 1, 1, 9, m1}, {0xfffffffb, m2}, {m2, 4, 1, 50, 3, 1}, {m1, 7, 0, 0, 0, 0, 0, 0, 0},
        {m1, 3, 0, 1, 17, 1, 0, 0}};
    std::vector<uint32_t> many{3, 20}, inlineMany{m1, 12};
    for (uint32_t i = 0; i < 20; ++i) many.push_back(i * 37 + 1);
    for (uint32_t i = 0; i < 12; ++i) inlineMany.insert(inlineMany.end(), {1, i * 0x1001 + 3, 1, i});
    scripts.push_back(many); scripts.push_back(inlineMany);
    return scripts;
}
}
}
DIFF_TEST(RelationshipTable_Read, 0x55b420) {
    using namespace Lane;
    void **managerCache = GameAddress<void *>(0x10cc2d8), *oldManager = *managerCache;
    void **persistCache = GameAddress<void *>(0x10faf6c), *oldPersist = *persistCache;
    *managerCache = &manager; *persistCache = &persist;
    uint64_t versions[3]{}, failures = 0; thrown = 0;
    const auto scripts = ReadScripts();
    for (size_t s = 0; s < scripts.size(); ++s) for (mode = 0; mode < 32; ++mode) {
        if (mode & 4) continue; // record opening is not used by Read
        int total = 0;
        for (int fail = -1; fail < total; ++fail) {
            Outcome results[2];
            for (gSide = 0; gSide < 2; ++gSide) {
                script = scripts[s]; Prepare(ctx, uint32_t(s * 64 + mode));
                callLog.clear(); cursor = 0; tick = 0; failAt = fail;
                uint8_t returned = 0; bool threw = false;
                try {
                    void *target = &stream;
                    returned = gSide ? CallSlot<uint8_t>(Face4(), 4, target) :
                        ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x55b420)(Face4(), target);
                } catch (int) { threw = true; }
                failAt = -1;
                if (fail == -1 && !gSide) total = tick;
                results[gSide] = {State(), callLog, returned, threw, cursor};
                Finish();
            }
            if (results[0] != results[1]) ctx.Fail("Read script %zu mode %u fail %d: state/calls/return differ", s, mode, fail);
            if (fail == -1) {
                const int32_t version = scripts[s].empty() ? 0 : int32_t(scripts[s][0]);
                ++versions[version < 0 ? 0 : (version == 2 || version == 3) ? 1 : 2];
                if (!results[0].returned) ++failures;
            }
            ++ctx.cases;
        }
    }
    *managerCache = oldManager; *persistCache = oldPersist;
    std::printf("  Read coverage: inline=%llu keys=%llu badVersion=%llu returned0=%llu injected=%llu\n",
        versions[0], versions[1], versions[2], failures, thrown);
}

namespace {
namespace Lane {
// One operation of the value interface on either side; returns what the caller sees. Slots through the side's own vtable;
// LoadEntry and SetEntry (not slots) are called directly on our side.
uint32_t Operate(difftest::Context &ctx, bool ours, uint32_t op, uint32_t person, uint32_t a, uint32_t b) {
    auto *table = Object();
    switch (op) {
    case 0: return ours ? CallSlot<uint32_t>(table, 5, person) :
        ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x55aef0)(table, person);
    case 1: return ours ? CallSlot<uint8_t>(table, 6, a, person, b) :
        ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(0x55bf70)(table, a, person, b);
    case 2: return ours ? CallSlot<uint32_t>(table, 9, person, a) :
        ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, uint32_t)>(0x55b1b0)(table, person, a);
    case 3: return ours ? CallSlot<uint8_t>(table, 10, person, a, b) :
        ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(0x55c080)(table, person, a, b);
    case 4:
        if (ours) CallSlot<void>(table, 7, person);
        else ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x55bbc0)(table, person);
        return 0;
    case 5:
        if (ours) CallSlot<void>(table, 8, uint32_t(a));
        else ctx.Original<void(__thiscall *)(void *, uint8_t)>(0x55bb10)(table, uint8_t(a));
        return 0;
    case 6: {
        auto position = table->mEntries.find(person);
        void *node = *reinterpret_cast<void **>(&position);
        void *entry = ours ? table->LoadEntry(position) :
            ctx.Original<void *(__thiscall *)(void *, void *)>(0x55b090)(table, node);
        auto after = table->mEntries.find(person);
        return entry == nullptr ? 0 : after != table->mEntries.end() && after->second.value == entry ? 1 : 2;
    }
    default: {
        RelationshipMap::iterator out = table->mEntries.end();
        if (ours) table->SetEntry(a, person, &out);
        else ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t, void *)>(0x55ac60)(table, a, person, &out);
        return out == table->mEntries.end() ? 0xffffffff : out->first;
    }
    }
}
}
}
DIFF_TEST(RelationshipTable_Values, 0x55aef0) {
    // Also 0x55bf70 SetValueCount, 0x55b1b0 GetValue, 0x55c080 SetValue, 0x55bbc0 RemovePerson,
    // 0x55bb10 Clear, 0x55b090 LoadEntry, 0x55ac60 SetEntry (all through Operate).
    using namespace Lane;
    void **persistCache = GameAddress<void *>(0x10faf6c), *oldPersist = *persistCache;
    *persistCache = &persist;
    static const uint32_t persons[] = {3, 4, 5, 0x10003, 0xffff, 7};
    uint64_t opCount[8]{}, nonzero = 0; thrown = 0;
    for (uint32_t c = 0; c < 768; ++c) {
        std::mt19937 plan(0xc0ffee + c);
        struct Op { uint32_t op, person, a, b; };
        std::vector<Op> ops(10);
        for (auto &op : ops) {
            op.op = c < 512 ? plan() % 8 : (c % 8);
            op.person = persons[plan() % 6];
            op.a = plan() % 5; op.b = plan() % 6 + (plan() % 4 == 0 ? 0x10000 : 0);
            if (op.op == 1 || op.op == 7) op.a = plan() & 0x1ffff;
            if (op.op == 5) op.a = plan() % 3 ? 1 : 0;
        }
        const int fail = c % 2 ? int(plan() % 16) : -1;
        Outcome results[2];
        for (gSide = 0; gSide < 2; ++gSide) {
            mode = c & 15; mode = (mode & ~uint32_t(1)) | 16; // no keyed record; old entries present
            script = {2, 2, 11, 22, 2, 1, 33, 2, 3, 1, 2, 3, 5, 9};
            Prepare(ctx, c ^ 0x5150);
            if (!(c & 8)) Object()->mStorage = c & 2 ? &segment : nullptr;
            // Lazy entries as Read leaves them, plus one loaded entry made by the side's own SetEntry.
            Object()->mEntries.try_emplace(3); Object()->mEntries.try_emplace(0x10003);
            RelationshipMap::iterator made;
            if (gSide) Object()->SetEntry(0x123, 4, &made);
            else ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t, void *)>(0x55ac60)(Object(), 0x123, 4, &made);
            callLog.clear(); cursor = 0; tick = 0; failAt = fail;
            std::vector<uint32_t> returns; bool threw = false;
            try {
                for (const auto &op : ops) {
                    returns.push_back(Operate(ctx, gSide != 0, op.op, op.person, op.a, op.b));
                    if (!gSide) { ++opCount[op.op]; if (returns.back()) ++nonzero; }
                    auto state = State(); returns.insert(returns.end(), state.begin(), state.end());
                }
            } catch (int) { threw = true; }
            failAt = -1;
            results[gSide] = {State(), callLog, 0, threw, cursor};
            results[gSide].calls.insert(results[gSide].calls.end(), returns.begin(), returns.end());
            Finish();
        }
        if (results[0] != results[1]) ctx.Fail("value case %u: state/calls/returns differ", c);
        ++ctx.cases;
    }
    *persistCache = oldPersist;
    std::printf("  Values coverage: get=%llu setCount=%llu getValue=%llu setValue=%llu remove=%llu clear=%llu load=%llu setEntry=%llu nonzero=%llu injected=%llu\n",
        opCount[0], opCount[1], opCount[2], opCount[3], opCount[4], opCount[5], opCount[6], opCount[7], nonzero, thrown);
}

// ===== Every slot no other test calls, through each side's own vtable =====
// Primary: AddRef 0x145260 (slot 1), GetPersonCount 0x15e970 (11), SetStorage 0x46e120 (15), IsModified 0x156110 (16),
// SetModified 0x1b9690 (17); the serializable face's AddRef thunk 0x1448e9. Same seeded table on both sides (fields, entries,
// old storage); compared: the result (raw al for byte results), the object (vtable words as codes), the storage fakes' calls
// and the trace.
DIFF_TEST(RelationshipTable_Slots, 0x55b000) {
    constexpr int kCalls[][2] = {{0, 1}, {0, 11}, {0, 15}, {0, 16}, {0, 17}, {4, 1}};
    for (uint32_t i = 0; i < 64; ++i) {
        for (const auto &call : kCalls) {
            const auto seed = Seed(0x5107 + i);
            Result results[2];
            std::mt19937 rng(i);
            const uint32_t size = rng() % 12, oldStorage = rng() % 3, newStorage = rng() % 4, arg = rng();
            for (gSide = 0; gSide < 2; ++gSide) {
                Reset(seed); Build(ctx);
                std::memcpy(work.object + 8, seed.object + 8, 0x14);
                Populate(size);
                Object()->mStorage = oldStorage == 0 ? nullptr : oldStorage == 1 ? &storage : &storage2;
                trace.fill(0); traceSize = 0;
                for (auto *fake : {&storage, &storage2}) fake->calls = 0;
                uint8_t *face = call[0] ? Face4() : work.object;
                uint32_t r = 0;
                if (call[1] == 1 || call[1] == 11) r = CallSlot<uint32_t>(face, call[1]);
                else if (call[1] == 16) r = CallSlot<uint8_t>(face, 16);
                else if (call[1] == 17) CallSlot<void>(face, 17, arg);
                else {
                    void *target = newStorage == 0 ? nullptr : newStorage == 1 ? &storage : newStorage == 2 ? &storage2 : Object()->mStorage;
                    CallSlot<void>(face, 15, target);
                }
                Record(93, uint32_t(Object()->mStorage == &storage) | uint32_t(Object()->mStorage == &storage2) << 1 |
                    uint32_t(Object()->mStorage == nullptr) << 2);
                results[gSide] = Capture(r, false, Hash());
                Object()->mStorage = nullptr;
                Teardown();
            }
            Compare(ctx, results[0], results[1]);
        }
    }
}

// ===== The adapters: our functions behind the old entry points, on a table built by the original constructor =====
// What runs when the game calls an old entry point directly or holds a table with its own vtables (the constructor hook off).
// The table on both sides is the original's; the original side calls the RVA, ours the adapter the hook installs. The game's
// vtables are copied with a trap in every slot an adapter forwards to, so an adapter that dispatched through the object's
// vtable (in game: back into its own hook) logs it. Where the original itself calls a slot (WriteForPerson / ReadForPerson the
// +4 face's Write / Read, Write / Read GetGZCLSID), both sides reach the trap alike. AddRef stays the game's.
namespace {
void *gTrapVT[18];
void *gTrapSerialVT[6];
uint32_t TrapHit(void *, unsigned words) { Lane::Note(0x7a7a7a7au, words); return 0; }
void *TrapFor(int args) { return shape::TrapFor<TrapHit>(unsigned(args)); }
} // namespace

DIFF_TEST(RelationshipTable_Adapters, 0x55bec0) {
    using namespace Lane;
    void **managerCache = GameAddress<void *>(0x10cc2d8), *oldManager = *managerCache;
    void **persistCache = GameAddress<void *>(0x10faf6c), *oldPersist = *persistCache;
    *managerCache = &manager; *persistCache = &persist;
    for (uint32_t c = 0; c < 64; ++c) {
        std::vector<uint32_t> blobs[2];
        for (int side = 0; side < 2; ++side) {
            gSide = 0; // the object is the original's on both sides
            mode = (c & 6) | 16; // no keyed record; old entries (fakes at 6, 17, 28: only removed or cleared here)
            script = {2, 3, 4, 9, 2, 1, 5};
            Prepare(ctx, c ^ 0xada);
            Object()->mStorage = c & 8 ? &segment : nullptr;
            Object()->mEntries.try_emplace(3); Object()->mEntries.try_emplace(0x10003); // lazy entries, as Read leaves them
            Object()->mRefCount = 5;
            std::memcpy(gTrapVT, GameAddress<void>(Table::kOriginalVTable), sizeof(gTrapVT));
            // Each slot's trap takes the slot's stack arguments (ret N), as the slot itself.
            constexpr int kPrimaryArgs[18] = {2, -1, 0, 2, 2, 1, 3, 1, 1, 2, 3, -1, 0, 1, 1, -1, -1, -1};
            for (int slot = 0; slot < 18; ++slot) if (kPrimaryArgs[slot] >= 0) gTrapVT[slot] = TrapFor(kPrimaryArgs[slot]);
            std::memcpy(gTrapSerialVT, GameAddress<void>(Table::kOriginalSerialVTable), sizeof(gTrapSerialVT));
            gTrapSerialVT[3] = TrapFor(1);
            gTrapSerialVT[4] = TrapFor(1);
            gTrapSerialVT[5] = TrapFor(0);
            *reinterpret_cast<void **>(work.object) = gTrapVT;
            *reinterpret_cast<void **>(Face4()) = gTrapSerialVT;
            auto *self = Object();
            auto *serial = reinterpret_cast<cIGZSerializable *>(Face4());
            callLog.clear(); cursor = 0; tick = 0; failAt = -1;
            std::vector<uint32_t> &b = blobs[side];
            std::mt19937 rng(c);
            const uint32_t id = std::vector<uint32_t>{1, 0xcc364c23, 0xe4fda3d4, 7}[rng() % 4];
            void *out = nullptr;
            // Slots whose adapters were dropped (lane GP: only the game's vtables reach them, and no table carries those while the
            // Construct hook is in; "in_effect": "callers") run the original on both sides here, keeping the call sequence the
            // remaining adapters see; their members are compared through our vtable by the tests above.
            b.push_back(ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(0x55b310)(self, id, &out));
            b.push_back(out ? uint32_t(static_cast<uint8_t *>(out) - work.object) : 0xffffffffu);
            b.push_back(side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *)>(0x55baf0)(self) : TA::Release(self, nullptr));
            b.push_back(side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *)>(0x55b000)(serial) : TA::GetGZCLSID(serial, nullptr));
            auto *ostream = reinterpret_cast<ifaces::cIGZOStream *>(&stream);
            auto *istream = reinterpret_cast<ifaces::cIGZIStream *>(&stream);
            const uint32_t person = rng() & 0x1ffff;
            b.push_back(side == 0 ? ctx.Original<uint8_t(__thiscall *)(void *, void *, uint32_t)>(0x55bec0)(self, ostream, person)
                                  : TA::WriteForPerson(self, nullptr, ostream, person));
            b.push_back(side == 0 ? ctx.Original<uint8_t(__thiscall *)(void *, void *, uint32_t)>(0x55b2a0)(self, istream, person)
                                  : TA::ReadForPerson(self, nullptr, istream, person));
            // The value slots over lazy, loaded and absent persons; SetEntry / LoadEntry make real entries (the side's own).
            for (uint32_t k = 0; k < 6; ++k) {
                const uint32_t who = std::vector<uint32_t>{3, 4, 5, 0x10003, 9}[rng() % 5], a = rng() % 4, v = rng();
                const uint32_t op = rng() % 8;
                switch (op) {
                case 0: b.push_back(ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x55aef0)(self, who)); break;
                case 1: b.push_back(ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(0x55bf70)(self, 0x77, who, a)); break;
                case 2: b.push_back(ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, uint32_t)>(0x55b1b0)(self, who, a)); break;
                case 3: b.push_back(ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(0x55c080)(self, who, a, v)); break;
                case 4: ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x55bbc0)(self, who); break;
                case 5: ctx.Original<void(__thiscall *)(void *, uint8_t)>(0x55bb10)(self, uint8_t(a & 1)); break;
                case 6: {
                    auto position = self->mEntries.find(who);
                    void *node = *reinterpret_cast<void **>(&position);
                    void *entry = side == 0 ? ctx.Original<void *(__thiscall *)(void *, void *)>(0x55b090)(self, node)
                                            : TA::LoadEntry(self, nullptr, position);
                    b.push_back(entry ? 1 : 0);
                    break;
                }
                default: {
                    RelationshipMap::iterator made = self->mEntries.end();
                    if (side == 0) ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t, void *)>(0x55ac60)(self, a, who, &made);
                    else TA::SetEntry(self, nullptr, a, who, &made);
                    b.push_back(made == self->mEntries.end() ? 0xffffffff : made->first);
                }
                }
                b.push_back(ctx.Original<uint32_t(__thiscall *)(void *)>(0x55afd0)(self));
                b.push_back(ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x55b010)(self, who));
            }
            script = {3, 2, 6, 17};
            cursor = 0;
            b.push_back(ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x55b420)(serial, istream));
            Writer::SetupWriter();
            Writer::mode = 0; // no keyed record: the keys only (the persist cache holds the Lane fake)
            trace.fill(0); traceSize = 0; ticks = 0; throwAt = -1;
            void *writeStream = &Writer::stream;
            b.push_back(ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x55c170)(serial, writeStream));
            b.insert(b.end(), trace.begin(), trace.begin() + traceSize);
            gSide = side; // the entries the code built are the side's own
            auto state = State();
            b.insert(b.end(), state.begin(), state.end());
            b.insert(b.end(), callLog.begin(), callLog.end());
            Finish();
        }
        if (blobs[0] != blobs[1]) ctx.Fail("Adapters case %u: differs", c);
        ++ctx.cases;
    }
    *managerCache = oldManager; *persistCache = oldPersist;
}
