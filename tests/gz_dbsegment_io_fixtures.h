#pragma once
// Fixtures shared by the gz_dbsegment_packed_file_io_*.cpp tests (they were one 7000-line file).

#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZDBPFIndexCompressed.h"
#include "gz/cGZDBPFIndexStatic.h"
#include "gz/cGZDBPFIndexModifiable.h"
#include "gz/cGZDBSegmentPackedFile.h"
#include "gz/cGZDBWriteRecordPackedFile.h"
#include "rz/PathFileOps.h"
#include "rz/GeneralAllocator.h"
#include "rz/RZMemory.h"
#include "rz/cRZFastCompression3.h"
#include "rz/cRZString.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <new>
#include <optional>
#include <random>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma warning(push)
#pragma warning(disable : 4505) // each test file uses only some of these

namespace {

constexpr size_t kBytes = 0x800;
constexpr size_t kStore = 0x300; // fake record store (vtable at +0)
constexpr size_t kHit = 0x320;   // the record the store answers with (16 bytes)
constexpr size_t kRecIn = 0x340; // the record the caller passes (16 bytes)
constexpr size_t kDst = 0x360;   // output dwords
constexpr size_t kFileObj = 0x380; // not used by the object itself: the file object is embedded at +0x58

alignas(16) uint8_t gArena[kBytes];

enum Kind : int64_t { kLockCall = 1, kUnlockCall = 2, kFind11 = 3, kFileLength = 4 };
std::vector<int64_t> gLog;
int gKindHits[256]; // how often each kind was pushed since a test last cleared it

void Push(int64_t kind, int64_t a, int64_t b)
{
    if (kind >= 0 && kind < 256) ++gKindHits[kind];
    gLog.push_back(kind);
    gLog.push_back(a);
    gLog.push_back(b);
}

int64_t Off(const void *p)
{
    return static_cast<const uint8_t *>(p) - gArena;
}

void __fastcall RecLock(void *self, void *)
{
    Push(kLockCall, Off(self), 0);
}

void __fastcall RecUnlock(void *self, void *)
{
    Push(kUnlockCall, Off(self), 0);
}

// Per-trial answers.
void *gFindRet = nullptr;
std::vector<void *> gFindRets; // when not empty: the lookup answers by call number (cycling) instead of gFindRet
size_t gFindIdx = 0;
std::vector<uint32_t> gLengths; // answers of the file's slot 7, in call order (the last repeats)
size_t gLengthIdx = 0;

void *gLockTab[4];
void *gStoreTab[64];
void *gFileTab[64];

void *__fastcall RecFind11(void *self, void *, const uint32_t *copy, uint32_t b1, uint32_t b2)
{
    Push(kFind11, Off(self), 0);
    for (int i = 0; i < 4; ++i) gLog.push_back(copy[i]);
    gLog.push_back(b1);
    gLog.push_back(b2);
    if (!gFindRets.empty()) return gFindRets[gFindIdx++ % gFindRets.size()];
    return gFindRet;
}

uint32_t __fastcall RecLength(void *self, void *)
{
    Push(kFileLength, Off(self), 0);
    uint32_t v = gLengths[(std::min)(gLengthIdx, gLengths.size() - 1)];
    ++gLengthIdx;
    return v;
}

void InitFakes()
{
    gLockTab[1] = reinterpret_cast<void *>(&RecLock);
    gLockTab[2] = reinterpret_cast<void *>(&RecUnlock);
    gStoreTab[11] = reinterpret_cast<void *>(&RecFind11);
    gFileTab[7] = reinterpret_cast<void *>(&RecLength);
    gFindRets.clear();
    gFindIdx = 0;
}

struct Tables {
    void *primary[59];
    void *sub[50];
};

Tables MakeTables(const difftest::Context &ctx)
{
    Tables t{};
    std::memcpy(t.primary, ctx.image.At(cGZDBSegmentPackedFile::kVTable), sizeof(t.primary));
    std::memcpy(t.sub, ctx.image.At(cGZDBSegmentPackedFile::kSubVTable), sizeof(t.sub));
    return t;
}

cGZDBSegmentPackedFile *Obj(void *p)
{
    return static_cast<cGZDBSegmentPackedFile *>(p);
}

using Image = std::vector<uint8_t>;

Image MakeImage(std::mt19937 &rng)
{
    Image s(kBytes);
    for (auto &b : s) b = static_cast<uint8_t>(rng());
    return s;
}

void SetWord(Image &s, size_t at, uint32_t v)
{
    std::memcpy(s.data() + at, &v, sizeof(v));
}

void SetPointer(Image &s, size_t at, const void *p)
{
    SetWord(s, at, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)));
}

uint32_t PickWord(std::mt19937 &rng)
{
    static const uint32_t edges[] = {0, 1, 2, 0x7fffffff, 0x80000000, 0xffffffff, 0xffff, 0x10000};
    if (rng() % 3 == 0) return edges[rng() % 8];
    return static_cast<uint32_t>(rng());
}

uint8_t PickFlag(std::mt19937 &rng)
{
    static const uint8_t flags[] = {0, 0, 1, 2, 3, 4, 0x80, 0xff};
    return flags[rng() % 8];
}

// The record store wired to +0x30 and the file object's fake vtable wired to +0x58.
void Wire(Image &s)
{
    SetPointer(s, kStore, gStoreTab);
    SetPointer(s, 0x30, gArena + kStore);
    SetPointer(s, 0x58, gFileTab);
    SetPointer(s, 0x0c, gLockTab);
}

struct Outcome {
    uint64_t result = 0;
    std::vector<int64_t> log;
    std::vector<uint8_t> bytes;
};

template <typename Call> Outcome Run(const Tables &t, const Image &s, Call call)
{
    std::memcpy(gArena, s.data(), kBytes);
    const void *primary = t.primary;
    const void *sub = t.sub;
    std::memcpy(gArena + 0, &primary, sizeof(primary));
    std::memcpy(gArena + 4, &sub, sizeof(sub));
    gLog.clear();
    gLengthIdx = 0;
    Outcome out;
    out.result = call(gArena);
    out.log = gLog;
    std::memset(gArena, 0, 8); // the vtable pointers differ between the plain and routed tables
    out.bytes.assign(gArena, gArena + kBytes);
    return out;
}

void Compare(difftest::Context &ctx, const char *name, const Outcome &a, const Outcome &b, const Outcome &c)
{
    ++ctx.cases;
    if (a.result != b.result || a.result != c.result) {
        ctx.Fail("%s: result original %llu, ours %llu, via slot %llu", name, static_cast<unsigned long long>(a.result),
                 static_cast<unsigned long long>(b.result), static_cast<unsigned long long>(c.result));
    }
    if (a.log != b.log || a.log != c.log) {
        ctx.Fail("%s: calls made differ (%zu / %zu / %zu values)", name, a.log.size(), b.log.size(), c.log.size());
    }
    if (a.bytes != b.bytes || a.bytes != c.bytes) ctx.Fail("%s: object state differs", name);
}

using U32RecFn = uint32_t(__thiscall *)(void *, const void *);
using U32RecMember = uint32_t (cGZDBSegmentPackedFile::*)(const uint32_t *);
using BoolPtrsFn = bool(__thiscall *)(void *, uint32_t *, uint32_t *);
using BoolPtrsMember = bool (cGZDBSegmentPackedFile::*)(uint32_t *, uint32_t *);
using BoolFn = bool(__thiscall *)(void *);
using BoolMember = bool (cGZDBSegmentPackedFile::*)();

template <typename T> void *Addr(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance member pointers only");
    void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

} // namespace

// ---- A world with a fixed-address allocator ----
//
// The functions below allocate and free tree nodes through RZMalloc and RZFree. The original's two wrappers are
// redirected to one GeneralAllocator whose core is a static array, and our RZMemory uses the same allocator. The
// whole world (the allocator, its core and the object) is saved before a trial and restored before each side
// runs, so the two sides see identical memory; they must end with identical memory too. Equal addresses
// everywhere then mean equal allocation and free sequences.
namespace {

constexpr size_t kCoreBytes = 0x20000;
alignas(4096) uint8_t gCore[kCoreBytes];
alignas(16) uint8_t gAllocMem[sizeof(GeneralAllocator)];
uint8_t *gAllocOverride = nullptr; // the image's own global allocator (RVA 0x10cb098), for code that calls it directly
uint8_t *AllocBytes() { return gAllocOverride ? gAllocOverride : gAllocMem; }
uint8_t *gCoreOverride = nullptr; // a larger core for the tests that need megabytes
size_t gCoreOverrideSize = 0;
uint8_t *CoreBytes() { return gCoreOverride ? gCoreOverride : gCore; }
size_t CoreSize() { return gCoreOverride ? gCoreOverrideSize : kCoreBytes; }

GeneralAllocator *WorldAlloc()
{
    return reinterpret_cast<GeneralAllocator *>(AllocBytes());
}

void *__cdecl WorldMalloc(uint32_t bytes)
{
    return WorldAlloc()->Malloc(bytes, 0);
}

void __cdecl WorldFree(void *mem)
{
    WorldAlloc()->Free(mem);
}

// Redirects an image function (at least five bytes long) to a replacement while alive.
struct Redirect {
    uint8_t *site;
    uint8_t saved[5];
    Redirect(difftest::Context &ctx, uint32_t rva, void *target) : Redirect(ctx.image.At<uint8_t>(rva), target) {}
    // Our own code (a member we replaced the original with), redirected the same way.
    Redirect(void *code, void *target) : site(static_cast<uint8_t *>(code))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    ~Redirect() { std::memcpy(site, saved, 5); }
};

// Patches a slot of a vtable in the mapped image while alive.
struct SlotPatch {
    void **slot;
    void *saved;
    SlotPatch(difftest::Context &ctx, uint32_t vtableRva, int index, void *fn) : slot(ctx.image.At<void *>(vtableRva) + index), saved(*slot)
    {
        DWORD old;
        VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old);
        *slot = fn;
    }
    ~SlotPatch() { *slot = saved; }
};

// Patches a slot of our cGZDBPFIndexModifiable's vtable while alive: the record store our code builds carries it (the
// game's builds carry the game's vtable, patched with SlotPatch). Its address is read once from a probe object, built and
// destroyed at once with a private allocator on a static core (the world's allocators stay untouched).
void **OurStoreVTable()
{
    static void **vtable = nullptr;
    if (vtable == nullptr) {
        alignas(4096) static uint8_t core[0x8000];
        alignas(16) static uint8_t allocator[sizeof(GeneralAllocator)];
        auto *probeAlloc = reinterpret_cast<GeneralAllocator *>(allocator);
        probeAlloc->Construct(core, sizeof(core), 0, 0, nullptr, nullptr);
        GeneralAllocator *saved = RZMemory::testGlobalAllocator;
        RZMemory::testGlobalAllocator = probeAlloc;
        {
            cGZDBPFIndexModifiable probe;
            std::memcpy(&vtable, &probe, sizeof(vtable));
        }
        RZMemory::testGlobalAllocator = saved;
    }
    return vtable;
}

struct OurStoreSlotPatch {
    void **slot;
    void *saved;
    OurStoreSlotPatch(int index, void *fn)
    {
        slot = OurStoreVTable() + index;
        saved = *slot;
        DWORD old;
        VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old);
        *slot = fn;
    }
    ~OurStoreSlotPatch() { *slot = saved; }
};

// While alive, RZMalloc and RZFree (the original's and ours) use the world's allocator.
struct WorldScope {
    Redirect malloc_, free_;
    explicit WorldScope(difftest::Context &ctx)
        : malloc_(ctx, 0x1be3b0, reinterpret_cast<void *>(&WorldMalloc)), free_(ctx, 0x1be3a1, reinterpret_cast<void *>(&WorldFree))
    {
        RZMemory::testGlobalAllocator = WorldAlloc();
    }
    ~WorldScope() { RZMemory::testGlobalAllocator = nullptr; }
};

void ResetWorld()
{
    std::memset(CoreBytes(), 0, CoreSize());
    std::memset(AllocBytes(), 0, sizeof(gAllocMem));
    WorldAlloc()->Construct(CoreBytes(), static_cast<uint32_t>(CoreSize()), 0, 0, nullptr, nullptr);
}

struct Snapshot {
    std::vector<uint8_t> core, alloc, arena;
    void Take()
    {
        core.assign(CoreBytes(), CoreBytes() + CoreSize());
        alloc.assign(AllocBytes(), AllocBytes() + sizeof(gAllocMem));
        arena.assign(gArena, gArena + kBytes);
    }
    void Restore() const
    {
        std::memcpy(CoreBytes(), core.data(), CoreSize());
        std::memcpy(AllocBytes(), alloc.data(), sizeof(gAllocMem));
        std::memcpy(gArena, arena.data(), kBytes);
    }
};

struct WorldOutcome {
    uint64_t result = 0;
    std::vector<int64_t> log;
    std::vector<uint8_t> bytes; // the arena (without the vtable pointers), the allocator and its core
};

template <typename Call> WorldOutcome WorldRun(const Snapshot &snap, const Tables &t, Call call)
{
    snap.Restore();
    const void *primary = t.primary;
    const void *sub = t.sub;
    std::memcpy(gArena + 0, &primary, sizeof(primary));
    std::memcpy(gArena + 4, &sub, sizeof(sub));
    gLog.clear();
    WorldOutcome out;
    out.result = call(gArena);
    out.log = gLog;
    std::memset(gArena, 0, 8);
    out.bytes.assign(gArena, gArena + kBytes);
    out.bytes.insert(out.bytes.end(), AllocBytes(), AllocBytes() + sizeof(gAllocMem));
    out.bytes.insert(out.bytes.end(), CoreBytes(), CoreBytes() + CoreSize());
    return out;
}

void WorldCompare(difftest::Context &ctx, const char *name, const WorldOutcome &a, const WorldOutcome &b, const WorldOutcome &c)
{
    ++ctx.cases;
    if (a.result != b.result || a.result != c.result) {
        ctx.Fail("%s: result original %llu, ours %llu, via slot %llu", name, static_cast<unsigned long long>(a.result),
                 static_cast<unsigned long long>(b.result), static_cast<unsigned long long>(c.result));
    }
    if (a.log != b.log || a.log != c.log) ctx.Fail("%s: calls made differ", name);
    if (a.bytes != b.bytes || a.bytes != c.bytes) {
        size_t at = 0;
        const WorldOutcome &bad = (a.bytes != b.bytes) ? b : c;
        while (at < a.bytes.size() && a.bytes[at] == bad.bytes[at]) ++at;
        ctx.Fail("%s: world differs (first difference at byte %zu of %zu: object %zu, allocator %zu)", name, at, a.bytes.size(), kBytes,
                 sizeof(gAllocMem));
    }
}

// The trees' own type in the test: nodes from RZMalloc, so that they live in the world's core.
template <typename T> struct TestAllocator {
    using value_type = T;
    TestAllocator() noexcept = default;
    template <typename U> TestAllocator(const TestAllocator<U> &) noexcept {}
    T *allocate(size_t count) { return static_cast<T *>(RZMemory::Malloc(static_cast<uint32_t>(count * sizeof(T)))); }
    void deallocate(T *p, size_t) noexcept { RZMemory::Free(p); }
    template <typename U> bool operator==(const TestAllocator<U> &) const noexcept { return true; }
    template <typename U> bool operator!=(const TestAllocator<U> &) const noexcept { return false; }
};
using TestMap = std::map<uint32_t, uint32_t, std::less<uint32_t>, TestAllocator<std::pair<const uint32_t, uint32_t>>>;
static_assert(sizeof(TestMap) == 8, "TestMap: head and size");

// Builds a tree in the arena at `where` from disjoint ranges. Returns the ranges (start, length).
std::vector<std::pair<uint32_t, uint32_t>> BuildRanges(std::mt19937 &rng, size_t where, unsigned maxNodes)
{
    TestMap *map = new (gArena + where) TestMap();
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    static const uint32_t gaps[] = {0, 1, 1, 2, 3, 10};
    uint32_t cursor = 1 + rng() % 4;
    for (unsigned n = rng() % (maxNodes + 1); n > 0; --n) {
        uint32_t start = cursor + gaps[rng() % 6];
        uint32_t length = 1 + rng() % 8;
        map->emplace(start, length);
        ranges.emplace_back(start, length);
        cursor = start + length;
    }
    return ranges;
}

using AddRangeFn = void(__cdecl *)(void *, uint32_t, uint32_t);

} // namespace

namespace {

// A new range for the given tree contents: mostly touching an existing range, sometimes not.
void PickNewRange(std::mt19937 &rng, const std::vector<std::pair<uint32_t, uint32_t>> &ranges, uint32_t &offset, uint32_t &length)
{
    length = 1 + rng() % 6;
    if (ranges.empty() || rng() % 5 == 0) {
        offset = static_cast<uint32_t>(rng() % 60);
        return;
    }
    auto pick = ranges[rng() % ranges.size()];
    switch (rng() % 4) {
    case 0: offset = pick.first + pick.second; break;
    case 1: offset = pick.first - length; break;
    case 2: offset = pick.first; break;
    default:
        offset = pick.first + pick.second;
        for (auto &r : ranges) if (r.first > pick.first) { length = r.first - offset; break; }
        break;
    }
}

void *NodePointer(TestMap::iterator it)
{
    void *node;
    std::memcpy(&node, &it, sizeof(node));
    return node;
}

} // namespace

// ---- The reference-count maps (unordered_map<unsigned, unsigned> at +0x1a0 and +0x1a4) and the record store ----

namespace {

using RefMap = std::unordered_map<uint32_t, uint32_t>;

RefMap *BuildRefMap(std::mt19937 &rng)
{
    auto *map = new RefMap();
    static const uint32_t counts[] = {0, 1, 1, 2, 3, 0x80000000u, 0xffffffffu, 0x7fffffffu};
    // Enough keys sometimes to cross a growth threshold of the table.
    static const unsigned sizes[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 8, 9, 16, 17, 32, 33};
    unsigned n = sizes[rng() % 15];
    bool wide = n > 8;
    for (; n > 0; --n) {
        uint32_t key = (wide || rng() % 3 == 0) ? static_cast<uint32_t>(rng()) : static_cast<uint32_t>(rng() % 24);
        (*map)[key] = rng() % 2 ? counts[rng() % 8] : static_cast<uint32_t>(rng() % 6);
    }
    return map;
}

void DescribeMap(const RefMap *map, std::vector<int64_t> &out)
{
    if (map == nullptr) {
        out.push_back(-1);
        return;
    }
    out.push_back(static_cast<int64_t>(map->size()));
    out.push_back(static_cast<int64_t>(map->bucket_count()));
    out.push_back(static_cast<int64_t>(map->max_load_factor() * 1000));
    for (const auto &entry : *map) {
        out.push_back(entry.first);
        out.push_back(entry.second);
    }
}

RefMap *MapAt(const uint8_t *arena, size_t at)
{
    uint32_t v;
    std::memcpy(&v, arena + at, sizeof(v));
    return reinterpret_cast<RefMap *>(static_cast<uintptr_t>(v));
}

// The fakes for the maps' tests: the record store's slot 10 fills a vector with the trial's entries (a heap block
// that the allocator service's fake frees), and the lock's Unlock returns a recognisable value.
constexpr int64_t kFill10 = 40, kSvcFree = 41, kSvcAlloc = 42;
std::vector<std::array<uint32_t, 4>> gEntries;
uint32_t gExtraCapacity = 0;
void *gFilledBlock = nullptr;
void *gSvcTab[8];
uint8_t gSvcObject[16];
void *gStoreTab10[64];

void __fastcall RecFill10(void *self, void *, uint32_t **vec, uint32_t zero)
{
    Push(kFill10, Off(self), zero);
    if (gEntries.empty() && gExtraCapacity == 0) return;
    size_t bytes = (gEntries.size() + gExtraCapacity) * 16;
    auto *block = static_cast<uint32_t *>(HeapAlloc(GetProcessHeap(), 0, bytes ? bytes : 16));
    gFilledBlock = block;
    for (size_t i = 0; i < gEntries.size(); ++i) std::memcpy(block + 4 * i, gEntries[i].data(), 16);
    vec[0] = block;
    vec[1] = block + 4 * gEntries.size();
    vec[2] = block + 4 * (gEntries.size() + gExtraCapacity);
}

void *__fastcall RecSvcAlloc(void *, void *, uint32_t bytes)
{
    Push(kSvcAlloc, bytes, 0);
    return HeapAlloc(GetProcessHeap(), 0, bytes ? bytes : 1);
}

void __fastcall RecSvcFree(void *, void *, void *mem)
{
    // The block's identity differs between runs: log whether it is the one slot 10 handed out.
    Push(kSvcFree, mem == gFilledBlock ? 1 : 0, 0);
    if (mem != nullptr) HeapFree(GetProcessHeap(), 0, mem);
}

uint32_t gUnlockResult = 0;

uint32_t __fastcall RecUnlockResult(void *self, void *)
{
    Push(kUnlockCall, Off(self), 0);
    return gUnlockResult;
}

// While alive, the allocator service cache (0x10c95d4, guard 0x10c95d8) holds the fake above.
struct ServiceScope {
    void **cache;
    int *guard;
    void *savedCache;
    int savedGuard;
    explicit ServiceScope(difftest::Context &ctx) : cache(ctx.image.At<void *>(0x10c95d4)), guard(ctx.image.At<int>(0x10c95d8))
    {
        savedCache = *cache;
        savedGuard = *guard;
        gSvcTab[3] = reinterpret_cast<void *>(&RecSvcAlloc);
        gSvcTab[4] = reinterpret_cast<void *>(&RecSvcFree);
        void *tab = gSvcTab;
        std::memcpy(gSvcObject, &tab, sizeof(tab));
        *cache = gSvcObject;
        *guard = INT32_MIN;
    }
    ~ServiceScope()
    {
        *cache = savedCache;
        *guard = savedGuard;
    }
};

struct MapState {
    bool tracking; // +0x19f
    bool hasMaps;
    uint32_t seed;
};

// Runs one call with two freshly built maps (the same for a seed) and logs their state afterwards.
template <typename Call> Outcome RunMaps(const Tables &t, const Image &s, const MapState &f, Call call)
{
    std::memcpy(gArena, s.data(), kBytes);
    const void *primary = t.primary;
    const void *sub = t.sub;
    std::memcpy(gArena + 0, &primary, sizeof(primary));
    std::memcpy(gArena + 4, &sub, sizeof(sub));
    gArena[0x19f] = f.tracking ? 1 : 0;
    std::mt19937 build(f.seed);
    RefMap *first = f.hasMaps ? BuildRefMap(build) : nullptr;
    RefMap *second = f.hasMaps ? BuildRefMap(build) : nullptr;
    std::memcpy(gArena + 0x1a0, &first, 4);
    std::memcpy(gArena + 0x1a4, &second, 4);
    gLog.clear();
    gFilledBlock = nullptr;
    Outcome out;
    difftest::FreeRecorder rec;
    out.result = call(gArena);
    rec.Stop();
    out.log = gLog;
    out.log.push_back(-2);
    RefMap *firstAfter = MapAt(gArena, 0x1a0);
    RefMap *secondAfter = MapAt(gArena, 0x1a4);
    // A map the call dropped (pointer nulled) must have been freed (FreeRecorder across the call).
    auto freed = [&rec](RefMap *before, RefMap *after) -> int64_t {
        if (before == nullptr || after != nullptr) return -3;
        return rec.Freed(before) ? 1 : 0;
    };
    out.log.push_back(freed(first, firstAfter));
    out.log.push_back(freed(second, secondAfter));
    DescribeMap(firstAfter, out.log);
    DescribeMap(secondAfter, out.log);
    delete firstAfter;
    delete secondAfter;
    std::memset(gArena, 0, 8);
    std::memset(gArena + 0x1a0, 0, 8);
    out.bytes.assign(gArena, gArena + kBytes);
    return out;
}

void InitMapFakes()
{
    InitFakes();
    gLockTab[2] = reinterpret_cast<void *>(&RecUnlockResult);
    gStoreTab[10] = reinterpret_cast<void *>(&RecFill10);
}

} // namespace

namespace {

// The record store at +0x30 whose slot 10 hands out the trial's entries.
void FillTrial(std::mt19937 &rng)
{
    gEntries.clear();
    for (unsigned n = rng() % 13; n > 0; --n) {
        std::array<uint32_t, 4> e;
        e[0] = rng() % 4 == 0 ? static_cast<uint32_t>(rng()) : static_cast<uint32_t>(rng() % 10);
        e[1] = rng() % 4 == 0 ? static_cast<uint32_t>(rng()) : static_cast<uint32_t>(rng() % 10);
        e[2] = static_cast<uint32_t>(rng());
        e[3] = static_cast<uint32_t>(rng());
        gEntries.push_back(e);
    }
    gExtraCapacity = rng() % 3;
}

} // namespace

// ---- Pending records: the tree at +0x110 and the record objects it points at ----

namespace {

constexpr size_t kRecBase = 0x400, kRecStride = 0x20;
constexpr int64_t kRecAddRef = 50, kRecSubRelease = 51, kRecKey = 52, kRecRelease = 53, kRec52 = 54, kRec62 = 55, kClose24 = 56,
                  kSet8 = 57, kCompare41 = 58;

// A record object in the arena: +0 the primary's vtable (gRecPrim), +4 the secondary's (gRecSub), key words at +8.
void *gRecSub[8];
void *gRecPrim[64];
std::vector<uint32_t> gRecSizes;     // slot 52's answer per record index
std::vector<uint32_t> gSetFill;      // what the store's slots 8 and 9 put into the set
bool gCompare41 = false;             // the primary's slot 41

size_t RecIndex(const void *self)
{
    return (static_cast<const uint8_t *>(self) - gArena - kRecBase) / kRecStride;
}

uint32_t __fastcall RecSubAddRef(void *self, void *)
{
    Push(kRecAddRef, Off(self), 0);
    return 1;
}

uint32_t __fastcall RecSubRelease(void *self, void *)
{
    Push(kRecSubRelease, Off(self), 0);
    return 1;
}

extern void *gKeyOverrideSelf;
extern uint32_t gKeyOverrideKey[4];

const uint32_t *__fastcall RecSubKey(void *self, void *)
{
    Push(kRecKey, Off(self), 0);
    if (self == gKeyOverrideSelf) return gKeyOverrideKey;
    return reinterpret_cast<const uint32_t *>(static_cast<uint8_t *>(self) + 4);
}

uint32_t __fastcall RecPrimRelease(void *self, void *)
{
    Push(kRecRelease, Off(self), 0);
    return 1;
}

uint32_t __fastcall RecPrim52(void *self, void *)
{
    Push(kRec52, Off(self), 0);
    return gRecSizes[RecIndex(self) % gRecSizes.size()];
}

void __fastcall RecPrim62(void *self, void *)
{
    Push(kRec62, Off(self), 0);
}

void __fastcall RecClose24(void *self, void *, void *record)
{
    Push(kClose24, Off(self), record == nullptr ? -1 : Off(record));
}

void __fastcall RecSetFill(void *self, void *, void *set)
{
    Push(kSet8, Off(self), 0);
    for (uint32_t v : gSetFill) static_cast<std::set<uint32_t> *>(set)->insert(v);
}

// Open (subobject slot 5) for the EnsureLoaded in DoGetRecordSize: logs the low bytes of its arguments and, when the
// trial says so, opens the file (sets state bit 0); answers 1.
constexpr int64_t kOpen5 = 59;
bool gOpensFile = false;
uint8_t gOpenMask = 1;

uint8_t __fastcall RecOpen5(void *self, void *, uint32_t a, uint32_t b)
{
    Push(kOpen5, Off(self), a & 0xff);
    gLog.push_back(b & 0xff);
    if (gOpensFile) gArena[0x24] |= gOpenMask;
    return 1;
}

// A handle whose key getter answers another record's key (the pointer in gKeyOverrideKey), for the one handle named.
void *gKeyOverrideSelf = nullptr;
uint32_t gKeyOverrideKey[4];
constexpr int64_t kSet9 = 60;
std::vector<uint32_t> gSetFill2; // what store slot 9 puts into the set

void __fastcall RecSetFill9(void *self, void *, void *set)
{
    Push(kSet9, Off(self), 0);
    for (uint32_t v : gSetFill2) static_cast<std::set<uint32_t> *>(set)->insert(v);
}

bool __fastcall RecCompare41(void *self, void *, void *record)
{
    Push(kCompare41, Off(self), Off(record));
    return gCompare41;
}

void InitRecordFakes()
{
    InitFakes();
    gRecSub[1] = reinterpret_cast<void *>(&RecSubAddRef);
    gRecSub[2] = reinterpret_cast<void *>(&RecSubRelease);
    gRecSub[3] = reinterpret_cast<void *>(&RecSubKey);
    gRecPrim[2] = reinterpret_cast<void *>(&RecPrimRelease);
    gRecPrim[52] = reinterpret_cast<void *>(&RecPrim52);
    gRecPrim[62] = reinterpret_cast<void *>(&RecPrim62);
    gStoreTab[8] = reinterpret_cast<void *>(&RecSetFill);
    gStoreTab[9] = reinterpret_cast<void *>(&RecSetFill9);
}

// The tree's key order: (word 0, 1, 3, 2).
struct TestOrder {
    bool operator()(const std::array<uint32_t, 4> &a, const std::array<uint32_t, 4> &b) const
    {
        if (a[0] != b[0]) return a[0] < b[0];
        if (a[1] != b[1]) return a[1] < b[1];
        if (a[3] != b[3]) return a[3] < b[3];
        return a[2] < b[2];
    }
};
template <typename Alloc> using PendingTree = std::map<std::array<uint32_t, 4>, void *, TestOrder, Alloc>;
using LocalPending = PendingTree<std::allocator<std::pair<const std::array<uint32_t, 4>, void *>>>;
using WorldPending = PendingTree<TestAllocator<std::pair<const std::array<uint32_t, 4>, void *>>>;
static_assert(sizeof(LocalPending) == 8 && sizeof(WorldPending) == 8, "pending tree: head and size");

// Writes record object `index` into the arena image with the given key; returns its address.
void *MakeRecord(Image &s, size_t index, const std::array<uint32_t, 4> &key)
{
    size_t at = kRecBase + index * kRecStride;
    SetPointer(s, at, gRecPrim);
    SetPointer(s, at + 4, gRecSub);
    std::memcpy(s.data() + at + 8, key.data(), 16);
    return gArena + at;
}

// Keys with small words so that different keys share words (the distinct counts need that).
std::array<uint32_t, 4> SmallKey(std::mt19937 &rng)
{
    return {static_cast<uint32_t>(rng() % 4), static_cast<uint32_t>(rng() % 4), static_cast<uint32_t>(rng() % 4), static_cast<uint32_t>(rng() % 4)};
}

struct PendingBuild {
    std::vector<std::array<uint32_t, 4>> keys; // in record order
};

// Fills the image with `n` records and returns their keys (distinct).
PendingBuild MakeRecords(Image &s, std::mt19937 &rng, unsigned n, std::vector<void *> &addresses)
{
    PendingBuild b;
    for (unsigned guard = 0; b.keys.size() < n && guard < 200; ++guard) {
        auto key = SmallKey(rng);
        if (std::find(b.keys.begin(), b.keys.end(), key) != b.keys.end()) continue;
        addresses.push_back(MakeRecord(s, b.keys.size(), key));
        b.keys.push_back(key);
    }
    return b;
}

void InstallSub(Tables &t, int slot, void *fn)
{
    t.sub[slot] = fn;
}

} // namespace

// ---- Tree erase and clear, file space allocation, the commit slot ----

namespace {

constexpr int64_t kSetLength = 61, kWrite58 = 62, kVisit37 = 63, kObj6 = 64;
std::vector<uint8_t> gSetLenAnswers{1};
size_t gSetLenIdx = 0;
const void *gMagicAddress = nullptr;

uint8_t __fastcall RecSetLength(void *self, void *, uint32_t newLength)
{
    Push(kSetLength, Off(self), newLength);
    return gSetLenAnswers[gSetLenIdx++ % gSetLenAnswers.size()];
}

uint8_t __fastcall RecWrite58(void *self, void *, const void *src, uint32_t offset, uint32_t length)
{
    Push(kWrite58, Off(self), src == gMagicAddress ? 1 : 0);
    gLog.push_back(offset);
    gLog.push_back(length);
    return 1;
}

void InitSpaceFakes()
{
    InitFakes();
    gFileTab[8] = reinterpret_cast<void *>(&RecSetLength);
}

} // namespace

// ---- Writing a record's bytes (vf29) ----

namespace {

constexpr int64_t kStore13 = 100, kStore14 = 101, kAlloc44 = 102, kWrite58b = 103;
bool gRemoveOk = false;
uint32_t gRemoved[3];
bool gAllocOk = true;
uint32_t gAllocOffset = 0;
uint8_t gWriteOk = 1;

// A fake called on the arena's store, or on a real store made by EnsureStore (a heap object).
int64_t SelfId(const void *self)
{
    auto p = static_cast<const uint8_t *>(self);
    return (p >= gArena && p < gArena + kBytes) ? p - gArena : -1;
}

uint8_t __fastcall RecStore14(void *self, void *, const uint32_t *key, uint32_t *out, uint32_t b1, uint32_t b2)
{
    Push(kStore14, SelfId(self), 0);
    for (int i = 0; i < 4; ++i) gLog.push_back(key[i]);
    gLog.push_back(b1);
    gLog.push_back(b2);
    if (gRemoveOk) std::memcpy(out, gRemoved, sizeof(gRemoved));
    return gRemoveOk;
}

void __fastcall RecStore13(void *self, void *, const uint32_t *key, const uint32_t *record)
{
    Push(kStore13, SelfId(self), 0);
    for (int i = 0; i < 4; ++i) gLog.push_back(key[i]);
    for (int i = 0; i < 3; ++i) gLog.push_back(record[i]);
}

uint8_t __fastcall RecAlloc44(void *self, void *, uint32_t *out, uint32_t length)
{
    Push(kAlloc44, Off(self), length);
    *out = gAllocOffset;
    return gAllocOk;
}

uint8_t __fastcall RecWrite58b(void *self, void *, const void *src, uint32_t offset, uint32_t length)
{
    Push(kWrite58b, Off(self), src == nullptr ? -1 : Off(src));
    gLog.push_back(offset);
    gLog.push_back(length);
    return gWriteOk;
}

uint8_t __fastcall RecStoreCurrent(void *, void *)
{
    return 1;
}

} // namespace

// ---- Close, ReopenIfDirty, Compact, CopyRecordsTo and the slots that load the store ----
//
// The large helpers these call stay original code (0x9ae5c0 the index load, 0x9b0670 the commit write, 0x9a83d0 the
// compaction, 0x9a8be0 the single-record copy). They are not under test here: each is redirected, on both sides, to a
// recording fake with the same calling convention, so the tests compare which helper is called, with what, and what
// is done with its answer.
namespace {

constexpr int64_t kStoreDtor = 80, kFileClose5 = 81, kFlush10Call = 82, kLoadIndexCall = 83, kCommitCall = 84, kCompactCall = 85,
                  kCopyOneCall = 86, kReopenOpen = 87, kMySubLock = 88, kMySubUnlock = 89, kOtherLock = 90, kOtherUnlock = 91,
                  kList45 = 92, kList19 = 93, kStore3 = 94, kStore10 = 95;
constexpr size_t kOther = 0x7a0; // a fake object (vtable word first) standing for another file's subobject

uint8_t gFlushAnswer = 1, gLoadIndexAnswer = 0, gCompactAnswer = 0, gReopenOpenAnswer = 1;
bool gLoadCreatesStore = false;
std::vector<uint8_t> gCopyAnswers{0};
size_t gCopyIdx = 0;
uint32_t gCountAnswer = 0, gStore10Answer = 0;
void *gOtherTab[64];

void __fastcall StoreDtor(void *self, void *, uint32_t flags)
{
    Push(kStoreDtor, Off(self), flags);
}

void __fastcall FileClose5(void *self, void *)
{
    Push(kFileClose5, Off(self), gArena[0x24]);
}

uint8_t __fastcall Flush10(void *self, void *)
{
    Push(kFlush10Call, Off(self), 0);
    return gFlushAnswer;
}

uint8_t __fastcall FakeLoadIndex(void *self, void *)
{
    Push(kLoadIndexCall, Off(self), 0);
    if (gLoadCreatesStore) {
        uint32_t store = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gArena + kStore));
        std::memcpy(gArena + 0x30, &store, sizeof(store));
    }
    return gLoadIndexAnswer;
}

uint8_t __fastcall FakeCommit(void *self, void *)
{
    Push(kCommitCall, Off(self), 0);
    return 0x5a;
}

uint8_t __fastcall FakeCompact(void *self, void *)
{
    Push(kCompactCall, Off(self), gArena[0x19e]);
    return gCompactAnswer;
}

uint8_t __fastcall FakeCopyOne(void *self, void *, void *other, const uint32_t *entry, uint32_t a, uint32_t b)
{
    Push(kCopyOneCall, Off(self), Off(other));
    for (int i = 0; i < 4; ++i) gLog.push_back(entry[i]);
    gLog.push_back(a);
    gLog.push_back(b);
    return gCopyAnswers[gCopyIdx++ % gCopyAnswers.size()];
}

uint8_t __fastcall ReopenOpen(void *self, void *, uint32_t a, uint32_t b)
{
    Push(kReopenOpen, Off(self), a);
    gLog.push_back(b);
    return gReopenOpenAnswer;
}

void __fastcall SubLock13(void *self, void *)
{
    Push(kMySubLock, Off(self), 0);
}

void __fastcall SubUnlock14(void *self, void *)
{
    Push(kMySubUnlock, Off(self), 0);
}

void __fastcall OtherLock13(void *self, void *)
{
    Push(kOtherLock, Off(self), 0);
}

void __fastcall OtherUnlock14(void *self, void *)
{
    Push(kOtherUnlock, Off(self), 0);
}

// The entry lists: heap blocks the allocator service's fake frees (see RecFill10).
void FillList(uint32_t **vec)
{
    if (gEntries.empty() && gExtraCapacity == 0) return;
    size_t bytes = (gEntries.size() + gExtraCapacity) * 16;
    auto *block = static_cast<uint32_t *>(HeapAlloc(GetProcessHeap(), 0, bytes ? bytes : 16));
    gFilledBlock = block;
    for (size_t i = 0; i < gEntries.size(); ++i) std::memcpy(block + 4 * i, gEntries[i].data(), 16);
    vec[0] = block;
    vec[1] = block + 4 * gEntries.size();
    vec[2] = block + 4 * (gEntries.size() + gExtraCapacity);
}

uint32_t __fastcall SubList45(void *self, void *, uint32_t **vec, uint32_t filter)
{
    Push(kList45, Off(self), filter);
    FillList(vec);
    return 0x45;
}

uint32_t __fastcall OtherList19(void *self, void *, uint32_t **vec, uint32_t filter)
{
    Push(kList19, Off(self), filter);
    FillList(vec);
    return 0x19;
}

uint32_t __fastcall Store3(void *self, void *)
{
    Push(kStore3, Off(self), 0);
    return gCountAnswer;
}

uint32_t __fastcall Store10(void *self, void *, uint32_t a, uint32_t b)
{
    Push(kStore10, Off(self), a);
    gLog.push_back(b);
    return gStore10Answer;
}

void InitCloseFakes()
{
    InitFakes();
    gStoreTab[0] = reinterpret_cast<void *>(&StoreDtor);
    gStoreTab[3] = reinterpret_cast<void *>(&Store3);
    gStoreTab[10] = reinterpret_cast<void *>(&Store10);
    gFileTab[5] = reinterpret_cast<void *>(&FileClose5);
    gOtherTab[13] = reinterpret_cast<void *>(&OtherLock13);
    gOtherTab[14] = reinterpret_cast<void *>(&OtherUnlock14);
    gOtherTab[19] = reinterpret_cast<void *>(&OtherList19);
    gFlushAnswer = 1;
    gLoadIndexAnswer = 0;
    gLoadCreatesStore = false;
    gCompactAnswer = 0;
    gReopenOpenAnswer = 1;
    gCopyAnswers.assign(1, 0);
    gCopyIdx = 0;
    gCountAnswer = 0;
    gStore10Answer = 0;
    gEntries.clear();
    gExtraCapacity = 0;
    gFilledBlock = nullptr;
}

// The path string at +0x3c kept harmless for anything that reads it, the pending tree empty.
void WireClose(Image &s)
{
    Wire(s);
    SetPointer(s, kOther, gOtherTab);
    SetWord(s, 0x114, 0);
    SetWord(s, 0x50, 15);
    s[0x4b] = 0;
}

struct HelperFakes {
    Redirect loadIndex, loadIndexOurs, commit, commitOurs, compact, compactOurs, copyOne, copyOneOurs;
    explicit HelperFakes(difftest::Context &ctx)
        : loadIndex(ctx, 0x9ae5c0, reinterpret_cast<void *>(&FakeLoadIndex)),
          loadIndexOurs(Addr(&cGZDBSegmentPackedFile::LoadCachedIndex), reinterpret_cast<void *>(&FakeLoadIndex)), commit(ctx, 0x9b0670, reinterpret_cast<void *>(&FakeCommit)),
          commitOurs(Addr(&cGZDBSegmentPackedFile::SaveCachedIndex), reinterpret_cast<void *>(&FakeCommit)),
          compact(ctx, 0x9a83d0, reinterpret_cast<void *>(&FakeCompact)),
          compactOurs(Addr(&cGZDBSegmentPackedFile::CompactBody), reinterpret_cast<void *>(&FakeCompact)), copyOne(ctx, 0x9a8be0, reinterpret_cast<void *>(&FakeCopyOne)),
          copyOneOurs(Addr(&cGZDBSegmentPackedFile::CopyOneRecord), reinterpret_cast<void *>(&FakeCopyOne))
    {
    }
};

// Runs `fn` with two fresh reference maps (or none) at +0x1a0 and +0x1a4 and logs whether each was deleted.
template <typename Fn> uint64_t WithMaps(uint8_t *self, bool has, uint32_t seed, Fn fn)
{
    std::mt19937 build(seed);
    RefMap *m1 = has ? BuildRefMap(build) : nullptr;
    RefMap *m2 = has ? BuildRefMap(build) : nullptr;
    std::memcpy(self + 0x1a0, &m1, 4);
    std::memcpy(self + 0x1a4, &m2, 4);
    difftest::FreeRecorder rec;
    uint64_t r = fn(self);
    rec.Stop();
    RefMap *a1 = MapAt(self, 0x1a0);
    RefMap *a2 = MapAt(self, 0x1a4);
    gLog.push_back(-2);
    auto freed = [&rec](RefMap *before, RefMap *after) -> int64_t {
        if (before == nullptr || after != nullptr) return -3;
        return rec.Freed(before) ? 1 : 0;
    };
    gLog.push_back(freed(m1, a1));
    gLog.push_back(freed(m2, a2));
    DescribeMap(a1, gLog);
    DescribeMap(a2, gLog);
    delete a1;
    delete a2;
    std::memset(self + 0x1a0, 0, 8);
    return r;
}

} // namespace

// ---- Open, Flush, the header writers and the iterator decrement ----

namespace {

constexpr int64_t kFile4 = 120, kHdrSeek12 = 121, kWrite16 = 122, kFile18 = 123, kFile19 = 124, kSetPath29 = 125, kOpen3 = 126, kStore6 = 127;
uint8_t gFile4Answer = 1, gFile18Answer = 1, gFile19Answer = 1, gWriteAnswer = 1, gOpen3Answer = 1;
bool gSeekLands = true;
uint32_t gStore6Answer = 0;

uint8_t __fastcall File4(void *self, void *)
{
    Push(kFile4, Off(self), 0);
    return gFile4Answer;
}

uint32_t __fastcall Seek12(void *self, void *, uint32_t where)
{
    Push(kHdrSeek12, Off(self), where);
    return gSeekLands ? where : where + 1;
}

uint8_t __fastcall Write16(void *self, void *, const uint32_t *buffer, uint32_t length)
{
    Push(kWrite16, Off(self), length);
    for (int i = 0; i < 0x18; ++i) gLog.push_back(buffer[i]);
    return gWriteAnswer;
}

uint8_t __fastcall File18(void *self, void *)
{
    Push(kFile18, Off(self), 0);
    return gFile18Answer;
}

uint8_t __fastcall File19(void *self, void *)
{
    Push(kFile19, Off(self), 0);
    return gFile19Answer;
}

void __fastcall SetPath29(void *self, void *, void *path)
{
    Push(kSetPath29, Off(self), Off(path));
}

uint8_t __fastcall Open3(void *self, void *, uint32_t flags, uint32_t mode, uint32_t third)
{
    Push(kOpen3, Off(self), flags);
    gLog.push_back(mode);
    gLog.push_back(third);
    return gOpen3Answer;
}

uint32_t __fastcall Store6(void *self, void *)
{
    Push(kStore6, Off(self), 0);
    return gStore6Answer;
}

void InitHeaderFakes()
{
    InitCloseFakes();
    InitSpaceFakes();
    gFileTab[4] = reinterpret_cast<void *>(&File4);
    gFileTab[12] = reinterpret_cast<void *>(&Seek12);
    gFileTab[16] = reinterpret_cast<void *>(&Write16);
    gFileTab[18] = reinterpret_cast<void *>(&File18);
    gFileTab[19] = reinterpret_cast<void *>(&File19);
    gFileTab[29] = reinterpret_cast<void *>(&SetPath29);
    gFileTab[3] = reinterpret_cast<void *>(&Open3);
    gStoreTab[6] = reinterpret_cast<void *>(&Store6);
    gFile4Answer = gFile18Answer = gFile19Answer = gWriteAnswer = gOpen3Answer = 1;
    gSeekLands = true;
    gStore6Answer = 0;
    gSetLenAnswers.assign(1, 1);
    gSetLenIdx = 0;
}

// Sets the game's header global for a trial and restores it afterwards.
struct HeaderGlobal {
    uint32_t *global;
    uint32_t saved;
    explicit HeaderGlobal(difftest::Context &ctx) : global(ctx.image.At<uint32_t>(0x10f5030)), saved(*global) {}
    ~HeaderGlobal() { *global = saved; }
};

} // namespace

namespace {

// LoadCachedIndex, 0x9ae5c0. The segment cache (+0x14) is a fake in the arena whose OpenRecord (slot 22) logs the key and
// the mode and hands out the record fake (also in the arena; now and then it answers false, with or without setting the
// out pointer). The record's Read (slot 8) logs the size and serves the trial's stream (a read past its end copies nothing
// and answers 0), Close (slot 6) and Release (slot 2) log. The subobject's GetSegmentChangeID (slot 29) is a fake in the
// tables. The stream is a cache entry as the game writes it: version 3, the stamp, a DBPF header CheckExtents accepts most
// of the time, the entries (fewer or more than 0x40, keys from a few types and groups, now and then sizes past 16 bits),
// the free-range count (never below the header's: the original then reads past its vector) and the ranges (repeated
// starts now and then), with one field broken or the stream cut short in some trials. The namespace (+0x18) is null or a
// fake that maps keys (group ^ 0x5a5a, both directions). The trees at +0x184 and +0x18c start with ranges. Afterwards the
// index the call built (+0x30) is described through its slots (kind: static or compressed; count; every entry through
// CopyEntries), deleted through its slot 0 and replaced by a marker; the old index (the store fake) logs its deletion.
// RebuildRefs runs with or without the reference maps (WithMaps, +0x19f). Compared: the result, the calls, the arena and
// the world's allocator and core (the range trees' nodes).
constexpr size_t kCache = 0x600, kCacheRecord = 0x640, kNs = 0x680;
constexpr int64_t kCacheOpen = 160, kCacheRead = 161, kCacheClose = 162, kCacheRelease = 163, kChangeId = 164, kNsMap = 165,
                  kIndexKind = 166;
void *gCacheTab[32], *gCacheRecordTab[16], *gNsTab[8];
std::vector<uint8_t> gCacheStream;
size_t gCachePos = 0, gCacheLimit = 0;
uint8_t gCacheOpenAnswer = 1;
bool gCacheOpenSetsOut = true;
uint32_t gChangeIdAnswer = 0;
const void *gGameCompressedVTable = nullptr, *gGameStaticVTable = nullptr;

uint8_t __fastcall CacheOpen(void *self, void *, const uint32_t *key, void **out, uint32_t mode)
{
    Push(kCacheOpen, Off(self), mode);
    for (int i = 0; i < 4; ++i) gLog.push_back(key[i]);
    if (gCacheOpenSetsOut) *out = gArena + kCacheRecord;
    return gCacheOpenAnswer;
}

uint8_t __fastcall CacheRead(void *self, void *, void *to, uint32_t size)
{
    Push(kCacheRead, Off(self), size);
    if (size > gCacheLimit - gCachePos) return 0;
    if (size != 0) std::memcpy(to, gCacheStream.data() + gCachePos, size);
    gCachePos += size;
    return 1;
}

uint8_t __fastcall CacheClose(void *self, void *)
{
    Push(kCacheClose, Off(self), 0);
    return 1;
}

uint32_t __fastcall CacheRelease(void *self, void *)
{
    Push(kCacheRelease, Off(self), 0);
    return 0;
}

uint32_t __fastcall ChangeId(void *self, void *)
{
    Push(kChangeId, Off(self), 0);
    return gChangeIdAnswer;
}

void __fastcall NsMap(void *self, void *, const uint32_t *in, uint32_t *out)
{
    Push(kNsMap, Off(self), in[1]);
    uint32_t key[4];
    std::memcpy(key, in, sizeof(key));
    key[1] ^= 0x5a5au;
    std::memcpy(out, key, sizeof(key));
}

template <typename Index> const void *VTableOf()
{
    static const void *vtable = [] {
        Index probe;
        const void *v;
        std::memcpy(&v, &probe, sizeof(v));
        return v;
    }();
    return vtable;
}

void DescribeIndex(uint8_t *self)
{
    void *index;
    std::memcpy(&index, self + 0x30, sizeof(index));
    gLog.push_back(kIndexKind);
    if (index == nullptr || index == gArena + kStore) {
        gLog.push_back(index == nullptr ? 0 : -1);
        return;
    }
    const void *vtable;
    std::memcpy(&vtable, index, sizeof(vtable));
    int64_t kind = 9;
    if (vtable == gGameCompressedVTable || vtable == VTableOf<cGZDBPFIndexCompressed>()) kind = 2;
    if (vtable == gGameStaticVTable || vtable == VTableOf<cGZDBPFIndexStatic>()) kind = 1;
    gLog.push_back(kind);
    uint32_t count = VirtualSlot<uint32_t(__thiscall *)(void *)>(index, 3)(index);
    gLog.push_back(count);
    std::vector<cGZDBPFIndexEntry> entries(count);
    void *ns;
    std::memcpy(&ns, self + 0x18, sizeof(ns));
    if (count != 0) VirtualSlot<void(__thiscall *)(void *, void *, void *)>(index, 19)(index, entries.data(), ns);
    for (const cGZDBPFIndexEntry &e : entries) {
        const uint32_t *w = &e.type;
        for (int k = 0; k < 7; ++k) gLog.push_back(w[k]);
    }
    VirtualSlot<void(__thiscall *)(void *, uint32_t)>(index, 0)(index, 1);
    uint32_t marker = 0xBEEF0001u;
    std::memcpy(self + 0x30, &marker, sizeof(marker));
}

void PutWord(std::vector<uint8_t> &s, uint32_t v)
{
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&v);
    s.insert(s.end(), p, p + 4);
}

} // namespace

// ---- DoCloseRecord (sub slot 39) and DoGetSegmentChangeID (sub slot 46) ----

namespace {

constexpr int64_t kRecFlags5 = 150, kCommit9b1fe0 = 151;
std::vector<uint8_t> gRecFlagAnswers{0};

uint8_t __fastcall RecSub5(void *self, void *)
{
    Push(kRecFlags5, Off(self), 0);
    return gRecFlagAnswers[RecIndex(self) % gRecFlagAnswers.size()];
}

void __fastcall FakeCommitRecord(void *self, void *, void *record, const uint32_t *key)
{
    Push(kCommit9b1fe0, Off(self), Off(record));
    for (int i = 0; i < 4; ++i) gLog.push_back(key[i]);
}

} // namespace

// ---- The record compression helpers ----
//
// The helpers allocate straight from the game's global allocator (RVA 0x10cb098). These tests construct that very
// object on the test core (ImageAllocScope), so the original and ours see one allocator whose state and core are
// restored before each side and compared after it.
namespace {

struct ImageAllocScope {
    uint8_t *global;
    uint8_t saved[sizeof(GeneralAllocator)];
    explicit ImageAllocScope(difftest::Context &ctx) : global(ctx.image.At<uint8_t>(0x10cb098))
    {
        std::memcpy(saved, global, sizeof(saved));
        gAllocOverride = global;
    }
    ~ImageAllocScope()
    {
        std::memcpy(global, saved, sizeof(saved));
        gAllocOverride = nullptr;
    }
};

// A big core for the trials that need megabytes (zero pages are not touched until used).
constexpr size_t kBigCore = 96u << 20;
alignas(4096) uint8_t gBigCore[kBigCore];

struct BigCoreScope {
    BigCoreScope()
    {
        gCoreOverride = gBigCore;
        gCoreOverrideSize = kBigCore;
    }
    ~BigCoreScope() { gCoreOverride = nullptr; }
};

constexpr size_t kBufPtrAt = 0x1e0, kSizeAt = 0x1e4;

std::vector<uint8_t> MakeData(std::mt19937 &rng, size_t size, int kind)
{
    std::vector<uint8_t> d(size);
    switch (kind) {
    case 0:
        for (auto &b : d) b = static_cast<uint8_t>(rng());
        break;
    case 1: {
        unsigned alphabet = 2 + rng() % 6;
        for (auto &b : d) b = static_cast<uint8_t>(rng() % alphabet);
        break;
    }
    case 2:
        break;
    default: {
        size_t cut = size == 0 ? 0 : rng() % size;
        for (size_t i = 0; i < cut; ++i) d[i] = static_cast<uint8_t>(rng());
        break;
    }
    }
    return d;
}

std::vector<uint8_t> CompressWithTheGame(const std::vector<uint8_t> &data, bool quick = false)
{
    cRZFastCompression3 c{};
    c.mVTable = GameAddress(cRZFastCompression3::kVTable);
    c.mRefCount = 0;
    c.mFlag = quick;
    std::vector<uint8_t> out(c.GetMaxLengthRequiredForCompressedData(static_cast<uint32_t>(data.size())) + 16);
    uint32_t length = static_cast<uint32_t>(out.size());
    if (!c.CompressData(data.data(), static_cast<uint32_t>(data.size()), out.data(), &length)) return {};
    out.resize(length);
    return out;
}

} // namespace

namespace {

constexpr int64_t kRead56 = 160;
std::vector<uint8_t> gFileImage;
bool gReadOk = true;

bool __fastcall ReadAtFake(void *self, void *, void *destination, uint32_t offset, uint32_t length)
{
    bool inCore = reinterpret_cast<uint8_t *>(destination) >= CoreBytes() && reinterpret_cast<uint8_t *>(destination) < CoreBytes() + CoreSize();
    Push(kRead56, offset, length);
    gLog.push_back(inCore ? 1 : 0);
    if (!gReadOk) return false;
    std::memset(destination, 0, length);
    if (offset < gFileImage.size()) std::memcpy(destination, gFileImage.data() + offset, (std::min<size_t>)(length, gFileImage.size() - offset));
    (void)self;
    return true;
}

} // namespace

// ---- DoReadRecord (sub slot 42) and DoWriteRecord (sub slot 43) ----

namespace {

constexpr int64_t kSeekPos = 170, kReadFile14 = 171, kWrite58h = 172;
uint32_t gSeekPos = 0;
uint8_t gRead14Answer = 1;

uint32_t __fastcall SeekRecording12(void *self, void *, uint32_t where)
{
    Push(kSeekPos, Off(self), where);
    gSeekPos = where;
    return gSeekLands ? where : where + 1;
}

bool __fastcall Read14(void *self, void *, void *destination, uint32_t count)
{
    Push(kReadFile14, Off(self), count);
    if (gRead14Answer == 0) return false;
    std::memset(destination, 0, count);
    if (gSeekPos < gFileImage.size()) std::memcpy(destination, gFileImage.data() + gSeekPos, (std::min<size_t>)(count, gFileImage.size() - gSeekPos));
    return true;
}

uint8_t __fastcall WriteHashed58(void *self, void *, const void *src, uint32_t offset, uint32_t length)
{
    Push(kWrite58h, Off(self), offset);
    gLog.push_back(length);
    uint32_t hash = 17;
    for (uint32_t i = 0; i < length; ++i) hash = hash * 31 + static_cast<const uint8_t *>(src)[i];
    gLog.push_back(hash);
    return gWriteOk;
}

} // namespace

// ---- The readers of the file's header, index, free ranges and compression directory (Open's fallback) ----
//
// The file object (+0x58) seeks (slot 12, SeekRecording12: lands or not) and reads (slot 14, Read14: from gFileImage at
// the position, or refuses); its length (slot 7) is the trial's length. Compared: the result, the calls, the arena and
// the world's allocator and core.
namespace {

constexpr int64_t kFindHeader = 180, kStoreDelete = 181, kDirRead = 182, kMemSize = 183;
std::vector<uint32_t> gFindAnswers;
size_t gFindIndex = 0;

uint32_t __fastcall FakeFindHeader(void *self, void *)
{
    Push(kFindHeader, Off(self), 0);
    return gFindAnswers[(std::min)(gFindIndex++, gFindAnswers.size() - 1)];
}

void *__fastcall StoreDelete(void *self, void *, uint32_t flags)
{
    Push(kStoreDelete, Off(self), flags);
    return self;
}

void __fastcall StoreMemSize(void *self, void *, const uint32_t *key, uint32_t memSize)
{
    Push(kMemSize, Off(self), memSize);
    for (int i = 0; i < 4; ++i) gLog.push_back(key[i]);
}

// The store fake's slots these tests set, put back afterwards (other tests read the table).
struct StoreSlots {
    void *saved[64];
    StoreSlots() { std::memcpy(saved, gStoreTab, sizeof(saved)); }
    ~StoreSlots() { std::memcpy(gStoreTab, saved, sizeof(saved)); }
};

void PutAt(size_t at, uint32_t v)
{
    if (gFileImage.size() < at + 4) gFileImage.resize(at + 4);
    std::memcpy(gFileImage.data() + at, &v, sizeof(v));
}

// A 0x60-byte header that CheckExtents takes (or, `good` false, one with one thing wrong) for a file of `length`.
void PutHeader(std::mt19937 &rng, size_t at, uint32_t length, bool good)
{
    uint32_t h[24];
    for (uint32_t &w : h) w = rng() % 3 == 0 ? static_cast<uint32_t>(rng()) : 0;
    h[0] = 0x46504244;
    h[1] = rng() % 4 == 0 ? 0 : 1;
    h[2] = rng() % 4;
    h[9] = rng() % 0x80;
    h[10] = 0x60 + rng() % (length - 0x60);
    h[11] = rng() % (length - h[10] + 1);
    h[12] = rng() % 8;
    h[13] = rng() % 4 == 0 ? 0 : 0x60 + rng() % (length - 0x60);
    h[14] = rng() % (length - h[13] + 1);
    h[15] = 1 + rng() % 2;
    if (!good) {
        switch (rng() % 6) {
        case 0: h[0] ^= 1u << (rng() % 32); break;
        case 1: h[1] = 2 + rng() % 3; break;
        case 2: h[10] = length + rng() % 0x100; break;
        case 3: h[11] = length - h[10] + 1 + rng() % 0x10; break;
        case 4: h[13] = length + rng() % 0x100; break;
        default: h[15] = 0; h[1] = 1; h[2] = 1 + rng() % 3; break; // a version whose indexMinorVersion stays as read
        }
    }
    for (int k = 0; k < 24; ++k) PutAt(at + 4 * k, h[k]);
}

} // namespace

#pragma warning(pop)
