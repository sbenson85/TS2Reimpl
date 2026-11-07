// Differential tests for the game's general allocator (src/rz/GeneralAllocator.*).
//
// Each side gets its own arena: a large reservation whose first 64 KB holds the allocator object and
// from which every VirtualAlloc(NULL, ...) the allocator makes is carved, in order. The original's
// VirtualAlloc/VirtualFree imports are pointed at arena A, ours (GeneralAllocatorOs) at arena B, so
// both sides lay memory out at the same offsets. After each operation, everything both sides wrote
// must match: the allocator objects and every committed page of the arenas, where a dword that
// points into arena A must point to the same offset in arena B and everything else must be equal.
// The lock's CRITICAL_SECTION (OS-owned contents) is skipped.
//
// Operations are random sequences of malloc/free/realloc over all size classes (fast, small, large
// bins, separately allocated large blocks), options, explicit cores, trimming, consolidation,
// growth failures with and without the out-of-memory callback, and shutdown/re-init.
#include "difftest.h"
#include "rz/GeneralAllocator.h"
#include "rz/RZMemory.h"
#include "rz/cGZAllocatorService.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr size_t kArenaSize = 96u << 20;
constexpr uint32_t kStateArea = 0x10000;

struct Arena {
    uint8_t *base = nullptr;
    size_t next = kStateArea;
    std::map<size_t, size_t> regions; // offset -> reserved size
    int reservesLeft = -1;             // when >= 0, reservations beyond this many fail
    int commitsLeft = -1;              // when >= 0, commits beyond this many fail
};
Arena gArena[2];

template <int S> void *__stdcall ArenaAlloc(void *address, size_t size, uint32_t type, uint32_t protect)
{
    Arena &a = gArena[S];
    if (!address) {
        if (a.reservesLeft == 0) return nullptr;
        if (a.reservesLeft > 0) --a.reservesLeft;
        size_t reserve = (size + 0xffff) & ~static_cast<size_t>(0xffff);
        if (a.next + reserve > kArenaSize) return nullptr;
        uint8_t *p = a.base + a.next;
        a.regions[a.next] = reserve;
        a.next += reserve;
        if ((type & MEM_COMMIT) && !VirtualAlloc(p, size, MEM_COMMIT, protect)) return nullptr;
        return p;
    }
    if (a.commitsLeft == 0) return nullptr;
    if (a.commitsLeft > 0) --a.commitsLeft;
    return VirtualAlloc(address, size, MEM_COMMIT, protect);
}

template <int S> int __stdcall ArenaFree(void *address, size_t size, uint32_t type)
{
    Arena &a = gArena[S];
    if (type == MEM_RELEASE) {
        if (size != 0) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        auto it = a.regions.find(static_cast<uint8_t *>(address) - a.base);
        if (it == a.regions.end()) {
            SetLastError(ERROR_INVALID_ADDRESS);
            return FALSE;
        }
        VirtualFree(address, it->second, MEM_DECOMMIT);
        a.regions.erase(it);
        return TRUE;
    }
    return VirtualFree(address, size, type);
}

size_t __stdcall RealQuery(const void *address, void *info, size_t length)
{
    return VirtualQuery(address, static_cast<MEMORY_BASIC_INFORMATION *>(info), length);
}

void ResetArenas()
{
    for (Arena &a : gArena) {
        if (!a.base) a.base = static_cast<uint8_t *>(VirtualAlloc(nullptr, kArenaSize, MEM_RESERVE, PAGE_READWRITE));
        VirtualFree(a.base, kArenaSize, MEM_DECOMMIT);
        VirtualAlloc(a.base, kStateArea, MEM_COMMIT, PAGE_READWRITE);
        a.next = kStateArea;
        a.regions.clear();
        a.reservesLeft = -1;
        a.commitsLeft = -1;
    }
}

// Routes the original's imports to arena A and ours to arena B (restored by the destructor).
struct OsRedirect {
    difftest::Context &ctx;
    void *saved[3];
    static constexpr uint32_t kSlots[3] = {0xd9a1e4, 0xd9a1e8, 0xd9a1cc}; // VirtualAlloc, VirtualFree, VirtualQuery
    explicit OsRedirect(difftest::Context &c) : ctx(c)
    {
        void *mine[3] = {reinterpret_cast<void *>(&ArenaAlloc<0>), reinterpret_cast<void *>(&ArenaFree<0>), reinterpret_cast<void *>(&RealQuery)};
        for (int i = 0; i < 3; ++i) {
            saved[i] = *ctx.image.At<void *>(kSlots[i]);
            *ctx.image.At<void *>(kSlots[i]) = mine[i];
        }
        GeneralAllocatorOs::virtualAlloc = &ArenaAlloc<1>;
        GeneralAllocatorOs::virtualFree = &ArenaFree<1>;
        GeneralAllocatorOs::virtualQuery = &RealQuery;
    }
    ~OsRedirect()
    {
        for (int i = 0; i < 3; ++i) *ctx.image.At<void *>(kSlots[i]) = saved[i];
        GeneralAllocatorOs::virtualAlloc = reinterpret_cast<GeneralAllocatorOs::VirtualAllocFn>(&::VirtualAlloc);
        GeneralAllocatorOs::virtualFree = reinterpret_cast<GeneralAllocatorOs::VirtualFreeFn>(&::VirtualFree);
        GeneralAllocatorOs::virtualQuery = reinterpret_cast<GeneralAllocatorOs::VirtualQueryFn>(&::VirtualQuery);
    }
};

// Translates a side-A dword to what side B must hold. Data that merely looks like an arena-A address
// (bitmaps, sizes) is also accepted unchanged: side B never holds arena-A addresses, so an exact match
// can't hide a wrong pointer.
// Side A's allocator object: arena A's base, or the image's global instance (wrapper tests).
uint8_t *gObjA = nullptr;

uint32_t Expected(uint32_t valueA)
{
    uintptr_t obj = reinterpret_cast<uintptr_t>(gObjA);
    if (obj && valueA >= obj && valueA < obj + sizeof(GeneralAllocator))
        return valueA - static_cast<uint32_t>(obj) + static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gArena[1].base));
    uintptr_t a = gArena[0].base ? reinterpret_cast<uintptr_t>(gArena[0].base) : 0;
    if (valueA >= a && valueA < a + kArenaSize) return valueA - static_cast<uint32_t>(a) + static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gArena[1].base));
    return valueA;
}

bool Committed(const uint8_t *p)
{
    MEMORY_BASIC_INFORMATION info;
    return VirtualQuery(p, &info, sizeof(info)) && info.State == MEM_COMMIT;
}

// Compares the allocator objects and (when `memory`) every committed page; returns a description of
// the first difference, or empty.
std::string Compare(bool memory)
{
    auto *a = gArena[0].base, *b = gArena[1].base;
    const uint8_t *objA = gObjA ? gObjA : a;
    char buf[160];
    for (uint32_t off = 0; off < sizeof(GeneralAllocator); off += 4) {
        if (off >= 0x4f0 && off < 0x508) continue; // CRITICAL_SECTION
        uint32_t va, vb;
        std::memcpy(&va, objA + off, 4);
        std::memcpy(&vb, b + off, 4);
        if (vb != Expected(va) && vb != va) {
            std::snprintf(buf, sizeof(buf), "allocator +0x%x: original %08x, ours %08x", off, va, vb);
            return buf;
        }
    }
    if (!memory) return {};
    for (size_t page = kStateArea; page < gArena[0].next || page < gArena[1].next; page += 0x1000) {
        bool ca = page < kArenaSize && Committed(a + page), cb = page < kArenaSize && Committed(b + page);
        if (ca != cb) {
            std::snprintf(buf, sizeof(buf), "arena +0x%zx: committed %d vs %d", page, ca, cb);
            return buf;
        }
        if (!ca) continue;
        for (uint32_t off = 0; off < 0x1000; off += 4) {
            uint32_t va, vb;
            std::memcpy(&va, a + page + off, 4);
            std::memcpy(&vb, b + page + off, 4);
            if (vb != Expected(va) && vb != va) {
                std::snprintf(buf, sizeof(buf), "arena +0x%zx: original %08x, ours %08x", page + off, va, vb);
                return buf;
            }
        }
    }
    return {};
}

// Originals.
struct Orig {
    void *(__thiscall *construct)(void *, void *, uint32_t, uint32_t, uint32_t, void *, void *);
    void *(__thiscall *malloc)(void *, uint32_t, uint32_t);
    void(__thiscall *free)(void *, void *);
    void *(__thiscall *realloc)(void *, void *, uint32_t, uint32_t);
    bool(__thiscall *init)(void *, void *, uint32_t, uint32_t, uint32_t, void *, void *);
    bool(__thiscall *addCore)(void *, void *, uint32_t, uint32_t, uint32_t, void *, void *);
    void(__thiscall *setOption)(void *, uint32_t, int32_t);
    uint32_t(__thiscall *trim)(void *, uint32_t);
    void(__thiscall *consolidate)(void *);
    bool(__thiscall *shutdown)(void *);
    explicit Orig(const difftest::Context &ctx)
    {
        construct = ctx.Original<decltype(construct)>(0x1c655d);
        malloc = ctx.Original<decltype(malloc)>(0x1c70cc);
        free = ctx.Original<decltype(free)>(0x1c6c5a);
        realloc = ctx.Original<decltype(realloc)>(0x1c7693);
        init = ctx.Original<decltype(init)>(0x1c6ece);
        addCore = ctx.Original<decltype(addCore)>(0x1c66ce);
        setOption = ctx.Original<decltype(setOption)>(0x1c79b8);
        trim = ctx.Original<decltype(trim)>(0x1c7cb7);
        consolidate = ctx.Original<decltype(consolidate)>(0x1c6981);
        shutdown = ctx.Original<decltype(shutdown)>(0x1c7b90);
    }
};

// Out-of-memory callback: records calls; when asked, makes room by freeing the oldest live block
// (through the same side's allocator) and asks for a retry.
struct OomState {
    int calls[2] = {0, 0};
    bool freeOnOom = false;
    void *victim[2] = {nullptr, nullptr};
    void *freed = nullptr; // side A's victim once the callback has freed it (the test drops the block)
    const Orig *orig = nullptr;
} gOom;

bool __cdecl OnOutOfMemory(GeneralAllocator *allocator, uint32_t bytes, uint32_t, void *)
{
    int side = reinterpret_cast<uint8_t *>(allocator) == gArena[0].base ? 0 : 1;
    ++gOom.calls[side];
    (void)bytes;
    if (!gOom.freeOnOom || !gOom.victim[side]) return false;
    void *v = gOom.victim[side];
    gOom.victim[side] = nullptr;
    if (side == 0) {
        gOom.orig->free(allocator, v);
        gOom.freed = v;
    }
    else allocator->Free(v);
    return true;
}

int gCoreFrees[2];
bool __cdecl OnFreeCore(GeneralAllocator *allocator, void *, uint32_t, void *)
{
    ++gCoreFrees[reinterpret_cast<uint8_t *>(allocator) == gArena[0].base ? 0 : 1];
    return true;
}

// GENERAL_ALLOCATOR_TRACE=1 prints every operation before it runs (to find crashes).
bool Trace()
{
    static const bool on = std::getenv("GENERAL_ALLOCATOR_TRACE") != nullptr;
    return on;
}
void TraceStep(int run, int step, const char *what, uint32_t x, uint32_t y)
{
    if (Trace()) {
        std::fprintf(stderr, "run %d step %d: %s %u %u\n", run, step, what, x, y);
        std::fflush(stderr);
    }
}

struct Block {
    uint8_t *a, *b;
    uint32_t size;
};

// A realloc size at one of Realloc's boundaries for this block, read from our side's chunk headers
// (the two sides' layouts are identical): shrinking to the split limit, keeping a large block in
// place, growing into the top chunk or into a free next chunk, each +-16 bytes.
uint32_t BoundarySize(const GeneralAllocator *g, const uint8_t *mem, std::mt19937 &rng)
{
    const uint8_t *chunk = mem - 8;
    uint32_t head = *reinterpret_cast<const uint32_t *>(chunk + 4), oldSize = head & ~7u;
    uint32_t nb;
    if (head & 2) {
        nb = oldSize - 8; // stays in place while nb + 4 <= the block size
    } else if (rng() % 2) {
        nb = oldSize - 0x10; // the remainder is split off from 0x10 bytes
    } else {
        const uint8_t *next = chunk + oldSize;
        uint32_t nextSize = *reinterpret_cast<const uint32_t *>(next + 4) & ~7u;
        nb = next == g->mTop ? oldSize + nextSize - 0x10 : oldSize + nextSize;
    }
    nb += (static_cast<uint32_t>(rng() % 5) - 2) * 8;
    return nb > 0x20 && nb < 0x1000000 ? nb - 8 : 1; // RequestToSize(nb - 8) == nb
}

// A TrimCore pad that puts the excess of a trimmable core's free tail exactly on a multiple of the
// decommit unit (+-8), read from our side; a random pad when no core qualifies.
uint32_t BoundaryPad(const GeneralAllocator *g, std::mt19937 &rng)
{
    uint32_t unit = ((g->mCoreIncrement >> 1) + 0xffff) & 0xffff0000;
    for (const GeneralAllocatorCore *core = g->mCores.next; core != &g->mCores; core = core->next) {
        if (!core->canFree || !core->canTrim) continue;
        const uint8_t *fence = reinterpret_cast<const uint8_t *>(core) + core->committed - 0x10;
        if (*reinterpret_cast<const uint32_t *>(fence + 4) & 1) continue;
        uint32_t size = *reinterpret_cast<const uint32_t *>(fence - *reinterpret_cast<const uint32_t *>(fence) + 4) & ~7u;
        uint32_t k = 1 + rng() % 3;
        if (size <= 0x40 + k * unit) continue;
        return size - 0x40 - k * unit + (static_cast<uint32_t>(rng() % 3) - 1) * 8;
    }
    return rng() % 0x20000;
}

// A top pad that puts the trim Free starts for this block (once merged with free neighbours) on a
// multiple of the decommit unit (+-8), read from our side.
uint32_t BoundaryTopPad(const GeneralAllocator *g, const uint8_t *mem, std::mt19937 &rng)
{
    const uint8_t *chunk = mem - 8;
    uint32_t head = *reinterpret_cast<const uint32_t *>(chunk + 4), size = head & ~7u;
    if (!(head & 1)) size += *reinterpret_cast<const uint32_t *>(chunk);
    const uint8_t *next = chunk + (head & ~7u);
    uint32_t nextSize = *reinterpret_cast<const uint32_t *>(next + 4) & ~7u;
    if (nextSize > 0x10 && !(*reinterpret_cast<const uint32_t *>(next + nextSize + 4) & 1)) size += nextSize;
    uint32_t unit = ((g->mCoreIncrement >> 1) + 0xffff) & 0xffff0000;
    uint32_t k = 1 + rng() % 2;
    if (size <= 0x40 + k * unit) return rng() % 0x30000;
    return size - 0x40 - k * unit + (static_cast<uint32_t>(rng() % 3) - 1) * 8;
}

uint32_t PickSize(std::mt19937 &rng)
{
    switch (rng() % 10) {
    case 0: return rng() % 8;
    case 1: case 2: return rng() % 0x50;          // fast bins
    case 3: case 4: return 0x40 + rng() % 0x1d0;  // small bins
    case 5: case 6: return 0x1f0 + rng() % 0x3000; // large bins
    case 7: return 0x3000 + rng() % 0x20000;
    case 8: return 0x18000 + rng() % 0x60000;     // around the large-block threshold
    default: return rng() % 0x200;
    }
}

void Fill(Block &blk, uint32_t seed)
{
    for (uint32_t i = 0; i + 4 <= blk.size; i += 4) {
        uint32_t v = 0xA0000000u | ((seed * 2654435761u + i) & 0x0fffffff);
        std::memcpy(blk.a + i, &v, 4);
        std::memcpy(blk.b + i, &v, 4);
    }
}

} // namespace

DIFF_TEST(GeneralAllocator_Operations, 0x1c7104)
{
    Orig orig(ctx);
    gOom.orig = &orig;
    std::mt19937 rng(0xA110C);
    for (int run = 0; run < 60; ++run) {
        ResetArenas();
        OsRedirect redirect(ctx);
        auto *a = reinterpret_cast<GeneralAllocator *>(gArena[0].base);
        auto *b = reinterpret_cast<GeneralAllocator *>(gArena[1].base);
        orig.construct(a, nullptr, 0, 0, 0, nullptr, nullptr);
        b->Construct(nullptr, 0, 0, 0, nullptr, nullptr);
        // Small cores so the arenas stay small and growth paths run often. The increment stays above
        // the largest request: when ExtendCore commits a request bigger than the increment it commits
        // that unaligned size, and a later trim decommits whole pages including the one its own
        // fencepost write lands in, so the original faults (seen in run 3 with a 0x8000 increment;
        // the game's default increment is 0x400000).
        int32_t minimum = 0x100000 << (rng() % 2), increment = 0x80000 << (rng() % 2);
        orig.setOption(a, 3, minimum);
        b->SetOption(3, minimum);
        orig.setOption(a, 4, increment);
        b->SetOption(4, increment);
        int32_t threshold = 0x8000 + static_cast<int32_t>(rng() % 0x40000);
        orig.setOption(a, 8, threshold);
        b->SetOption(8, threshold);
        bool withOom = run % 3 == 1;
        if (withOom) {
            a->mOutOfMemory = b->mOutOfMemory = reinterpret_cast<void *>(&OnOutOfMemory);
            a->mOutOfMemoryRetries = b->mOutOfMemoryRetries = 1 + rng() % 4;
            gOom.freeOnOom = rng() % 2;
        }
        gOom.calls[0] = gOom.calls[1] = 0;
        gOom.victim[0] = gOom.victim[1] = nullptr;
        gOom.freed = nullptr;
        std::vector<Block> live;
        std::string history;
        std::string diff = Compare(true);
        for (int step = 0; step < 400 && diff.empty(); ++step) {
            int op = rng() % 100;
            char line[160];
            if (op < 40) {
                uint32_t size = PickSize(rng);
                if (rng() % 16 == 0 && b->mLargeBlockThreshold > 0x40) // around the large-block threshold
                    size = b->mLargeBlockThreshold - 8 + (static_cast<uint32_t>(rng() % 5) - 2) * 8;
                // Growth failures, only with the out-of-memory callback installed: without it the
                // original doesn't check for a failed new core (ExtendCore's SetTop(null) faults).
                if (withOom && rng() % 50 == 0) {
                    int n = static_cast<int>(rng() % 3);
                    gArena[0].reservesLeft = gArena[1].reservesLeft = n;
                    gArena[0].commitsLeft = gArena[1].commitsLeft = static_cast<int>(rng() % 4);
                }
                TraceStep(run, step, "malloc A", size, 0);
                auto *pa = static_cast<uint8_t *>(orig.malloc(a, size, 0));
                TraceStep(run, step, "malloc B", size, 0);
                auto *pb = static_cast<uint8_t *>(b->Malloc(size, 0));
                std::snprintf(line, sizeof(line), "malloc(%u) -> %p / %p", size, pa, pb);
                if (Expected(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pa))) != reinterpret_cast<uintptr_t>(pb)) diff = "malloc result";
                else if (pa) {
                    Block blk{pa, pb, size};
                    Fill(blk, static_cast<uint32_t>(step));
                    live.push_back(blk);
                    if (withOom && !gOom.victim[0] && rng() % 2) {
                        gOom.victim[0] = pa;
                        gOom.victim[1] = pb;
                    }
                }
            } else if (op < 70) {
                if (live.empty() || rng() % 20 == 0) {
                    orig.free(a, nullptr);
                    b->Free(nullptr);
                    std::snprintf(line, sizeof(line), "free(null)");
                } else {
                    size_t i = rng() % live.size();
                    Block blk = live[i];
                    live.erase(live.begin() + static_cast<ptrdiff_t>(i));
                    if (gOom.victim[0] == blk.a) gOom.victim[0] = gOom.victim[1] = nullptr;
                    if (blk.size >= 0xf000 && rng() % 2 && !(blk.b[-4] & 2)) {
                        int32_t pad = static_cast<int32_t>(BoundaryTopPad(b, blk.b, rng));
                        orig.setOption(a, 7, pad);
                        b->SetOption(7, pad);
                    }
                    TraceStep(run, step, "free A", blk.size, 0);
                    orig.free(a, blk.a);
                    TraceStep(run, step, "free B", blk.size, 0);
                    b->Free(blk.b);
                    std::snprintf(line, sizeof(line), "free(%u bytes)", blk.size);
                }
            } else if (op < 85) {
                uint32_t size = PickSize(rng);
                if (live.empty() || rng() % 10 == 0) {
                    auto *pa = static_cast<uint8_t *>(orig.realloc(a, nullptr, size, 0));
                    auto *pb = static_cast<uint8_t *>(b->Realloc(nullptr, size, 0));
                    std::snprintf(line, sizeof(line), "realloc(null, %u)", size);
                    if (Expected(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pa))) != reinterpret_cast<uintptr_t>(pb)) diff = "realloc(null) result";
                    else if (pa) live.push_back({pa, pb, size});
                } else {
                    size_t i = rng() % live.size();
                    Block &blk = live[i];
                    if (rng() % 3 == 0) size = BoundarySize(b, blk.b, rng);
                    if (gOom.victim[0] == blk.a) gOom.victim[0] = gOom.victim[1] = nullptr;
                    TraceStep(run, step, "realloc A", blk.size, size);
                    auto *pa = static_cast<uint8_t *>(orig.realloc(a, blk.a, size, 0));
                    TraceStep(run, step, "realloc B", blk.size, size);
                    auto *pb = static_cast<uint8_t *>(b->Realloc(blk.b, size, 0));
                    std::snprintf(line, sizeof(line), "realloc(%u -> %u) -> %p / %p", blk.size, size, pa, pb);
                    if (Expected(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pa))) != reinterpret_cast<uintptr_t>(pb)) diff = "realloc result";
                    else if (pa) {
                        blk = {pa, pb, size};
                        Fill(blk, static_cast<uint32_t>(step) ^ 0x55);
                    }
                }
            } else if (op < 90) {
                uint32_t option = rng() % 13;
                if (option == 0 || option == 3 || option == 4) option = 5; // keep locking and core sizes
                // Max-fast 0 isn't used: it sets the limit to 8, below the first fast bin, so
                // MallocConsolidate's bin walk never meets its end and the original runs into the bins.
                int32_t value = option == 5 ? static_cast<int32_t>(1 + rng() % 0x5f)
                              : option == 6 || option == 7 ? static_cast<int32_t>(rng() % 0x30000)
                              : option == 8 ? static_cast<int32_t>((0x4000 + rng() % 0x40000) & (rng() % 2 ? ~7u : ~0u))
                              : option == 9 ? static_cast<int32_t>(rng() % 4)
                              : option == 12 ? static_cast<int32_t>(1 + rng() % 3)
                              : static_cast<int32_t>(rng() % 2);
                TraceStep(run, step, "option", option, static_cast<uint32_t>(value));
                orig.setOption(a, option, value);
                b->SetOption(option, value);
                std::snprintf(line, sizeof(line), "SetOption(%u, %d)", option, value);
            } else if (op < 94) {
                uint32_t pad = rng() % 2 ? BoundaryPad(b, rng) : rng() % 0x20000;
                TraceStep(run, step, "trim", pad, 0);
                uint32_t ra = orig.trim(a, pad), rb = b->TrimCore(pad);
                std::snprintf(line, sizeof(line), "TrimCore(%u) -> %u / %u", pad, ra, rb);
                if (ra != rb) diff = "TrimCore result";
            } else if (op < 97) {
                TraceStep(run, step, "consolidate", 0, 0);
                orig.consolidate(a);
                b->MallocConsolidate();
                std::snprintf(line, sizeof(line), "MallocConsolidate");
            } else {
                // An explicit core: caller-provided committed memory, with or without a free
                // callback. (Reserved-only memory isn't usable: AddCore writes the core header and
                // fenceposts before it asks VirtualQuery, so the original faults on it.)
                uint32_t size = 0x10000 * (1 + rng() % 4) + (rng() % 3 == 0 ? rng() % 0x3000 : 0);
                bool reserveOnly = false;
                uint32_t type = reserveOnly ? MEM_RESERVE : (MEM_RESERVE | MEM_COMMIT);
                void *ma = ArenaAlloc<0>(nullptr, size, type, PAGE_READWRITE);
                void *mb = ArenaAlloc<1>(nullptr, size, type, PAGE_READWRITE);
                uint32_t canFree = rng() % 2, canTrim = rng() % 2;
                void *fn = rng() % 2 ? reinterpret_cast<void *>(&OnFreeCore) : nullptr;
                if (reserveOnly) fn = nullptr;
                TraceStep(run, step, "addcore", size, reserveOnly);
                bool ra = orig.addCore(a, ma, size, canFree, canTrim, fn, nullptr);
                bool rb = b->AddCore(mb, size, canFree, canTrim, fn, nullptr);
                std::snprintf(line, sizeof(line), "AddCore(%u, %s) -> %d / %d", size, reserveOnly ? "reserved" : "committed", ra, rb);
                if (ra != rb) diff = "AddCore result";
            }
            if (gOom.freed) {
                // The out-of-memory callback freed the victim block: it's no longer live.
                for (size_t i = 0; i < live.size(); ++i)
                    if (live[i].a == gOom.freed) {
                        live.erase(live.begin() + static_cast<ptrdiff_t>(i));
                        break;
                    }
                gOom.freed = nullptr;
            }
            history += "\n    ";
            history += line;
            if (history.size() > 1200) history.erase(0, history.size() - 1200);
            ++ctx.cases;
            TraceStep(run, step, "compare", 0, 0);
            if (diff.empty()) diff = Compare(step % 8 == 7);
            if (diff.empty() && gOom.calls[0] != gOom.calls[1]) diff = "out-of-memory callback calls differ";
        }
        for (Arena &ar : gArena) ar.reservesLeft = ar.commitsLeft = -1;
        if (diff.empty()) {
            gCoreFrees[0] = gCoreFrees[1] = 0;
            TraceStep(run, -1, "shutdown A", 0, 0);
            bool ra = orig.shutdown(a);
            TraceStep(run, -1, "shutdown B", 0, 0);
            bool rb = b->Shutdown();
            TraceStep(run, -1, "shutdown done", 0, 0);
            ++ctx.cases;
            if (ra != rb || gCoreFrees[0] != gCoreFrees[1]) diff = "Shutdown";
            else diff = Compare(true);
            TraceStep(run, -1, "compared", 0, 0);
            if (diff.empty()) { // re-initialise after shutdown, and allocate once more
                TraceStep(run, -2, "init A", 0, 0);
                orig.init(a, nullptr, 0, 0, 0, nullptr, nullptr);
                TraceStep(run, -2, "init B", 0, 0);
                b->Init(nullptr, 0, 0, 0, nullptr, nullptr);
                TraceStep(run, -2, "malloc A", 0, 0);
                void *pa = orig.malloc(a, 100, 0);
                TraceStep(run, -2, "malloc B", 0, 0);
                void *pb = b->Malloc(100, 0);
                TraceStep(run, -2, "done", 0, 0);
                ++ctx.cases;
                if (Expected(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pa))) != reinterpret_cast<uintptr_t>(pb)) diff = "malloc after re-init";
                else diff = Compare(true);
                orig.shutdown(a);
                b->Shutdown();
            }
        }
        if (!diff.empty()) ctx.Fail("run %d: %s; last steps:%s", run, diff.c_str(), history.c_str());
    }
}

DIFF_TEST(GeneralAllocator_Helpers, 0x1c6e44)
{
    auto largeBin = ctx.Original<uint32_t(__cdecl *)(uint32_t)>(0x1c6e44);
    std::mt19937 rng(0xB1);
    for (int i = 0; i < 200000; ++i) {
        uint32_t size = i < 64 ? (1u << (i % 32)) + (i / 32) : static_cast<uint32_t>(rng()) >> (rng() % 32);
        ++ctx.cases;
        if (largeBin(size) != GeneralAllocator::LargeBinIndex(size)) ctx.Fail("LargeBinIndex(0x%x)", size);
    }
    // Chunk helpers on scratch buffers.
    auto fenceposts = ctx.Original<void(__cdecl *)(uint8_t *, uint32_t)>(0x1c6916);
    auto fencepostsAt = ctx.Original<void(__cdecl *)(uint8_t *, uint32_t)>(0x1c792f);
    auto initChunk = ctx.Original<uint8_t *(__cdecl *)(uint8_t *, uint32_t, uint32_t)>(0x1c70ab);
    auto chunkToMem = ctx.Original<void *(__cdecl *)(uint8_t *)>(0x1c6e31);
    auto largeChunk = ctx.Original<uint8_t *(__cdecl *)(uint8_t *)>(0x1c6eb2);
    auto pageSize = ctx.Original<uint32_t(__cdecl *)()>(0x1c6eb9);
    for (int i = 0; i < 2000; ++i) {
        alignas(8) uint8_t a[0x300], b[0x300];
        for (size_t k = 0; k < sizeof(a); ++k) a[k] = b[k] = static_cast<uint8_t>(rng());
        uint32_t size = 0x20 + 8 * (rng() % 0x50), flags = rng() % 8;
        std::memcpy(a + 4, &size, 4);
        std::memcpy(b + 4, &size, 4);
        a[4] |= flags;
        b[4] |= flags;
        uint32_t which = rng() % 3, arg = rng() % 2;
        if (which == 0) {
            fenceposts(a, arg);
            GeneralAllocator::SetFenceposts(b, arg);
        } else if (which == 1) {
            fencepostsAt(a, size - 0x10);
            GeneralAllocator::SetFencepostsAt(b, size - 0x10);
        } else {
            uint8_t *ra = initChunk(a, size, flags), *rb = GeneralAllocator::InitCoreChunk(b, size, flags);
            if (ra - a != rb - b) ctx.Fail("InitCoreChunk result");
        }
        ++ctx.cases;
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("chunk helper %u (size 0x%x, flags %u, arg %u)", which, size, flags, arg);
        uint32_t back = 8 * (rng() % 8);
        std::memcpy(a + 0x100, &back, 4);
        ++ctx.cases;
        if (static_cast<uint8_t *>(chunkToMem(a)) - a != static_cast<uint8_t *>(GeneralAllocator::ChunkToMem(a)) - a
            || largeChunk(a + 0x100) != GeneralAllocator::LargeBlockChunk(a + 0x100))
            ctx.Fail("ChunkToMem/LargeBlockChunk");
    }
    ++ctx.cases;
    if (pageSize() != GeneralAllocator::PageSize()) ctx.Fail("PageSize");
    // Locks: count bookkeeping (the CRITICAL_SECTION itself is the OS's).
    auto create = ctx.Original<GeneralAllocatorLock *(__cdecl *)(GeneralAllocatorLock *)>(0x1c763d);
    auto destroy = ctx.Original<void(__cdecl *)(GeneralAllocatorLock *)>(0x1c7654);
    auto count = ctx.Original<int32_t(__cdecl *)(GeneralAllocatorLock *)>(0x1c765f);
    auto lock = ctx.Original<int32_t(__cdecl *)(GeneralAllocatorLock *)>(0x1c7667);
    auto unlock = ctx.Original<int32_t(__cdecl *)(GeneralAllocatorLock *)>(0x1c767c);
    GeneralAllocatorLock la{}, lb{};
    la.count = lb.count = 77;
    ++ctx.cases;
    if (create(&la) != &la || GeneralAllocator::CreateLock(&lb) != &lb || la.count != lb.count) ctx.Fail("CreateLock");
    for (int i = 0; i < 20; ++i) {
        bool up = i < 10 || rng() % 2;
        int32_t ra = up ? lock(&la) : unlock(&la), rb = up ? GeneralAllocator::Lock(&lb) : GeneralAllocator::Unlock(&lb);
        ++ctx.cases;
        if (ra != rb || count(&la) != GeneralAllocator::LockCount(&lb)) ctx.Fail("Lock/Unlock %d", i);
    }
    while (la.count > 0) unlock(&la);
    while (lb.count > 0) GeneralAllocator::Unlock(&lb);
    destroy(&la);
    GeneralAllocator::DestroyLock(&lb);
}

// The global wrappers (RZMalloc/RZFree/RZRealloc and the memory service's slots). The original ones
// use the image's global allocator (RVA 0x10cb098), set up here on arena A; ours are pointed at an
// allocator on arena B. Random operations through the wrappers; results and the whole allocator state
// compared as in GeneralAllocator_Operations.
DIFF_TEST(RZMemory_Wrappers, 0x1be3b0)
{
    auto rzMalloc = ctx.Original<void *(__cdecl *)(uint32_t)>(0x1be3b0);
    auto rzFree = ctx.Original<void(__cdecl *)(void *)>(0x1be3a1);
    auto rzRealloc = ctx.Original<void *(__cdecl *)(void *, uint32_t)>(0x1be3c1);
    auto svcMalloc = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x1be4c7);
    auto svcFreeIfNotNull = ctx.Original<void(__thiscall *)(void *, void *)>(0x1be4d4);
    auto svcRealloc = ctx.Original<void *(__thiscall *)(void *, void *, uint32_t)>(0x1be4e8);
    auto svcFree = ctx.Original<void(__thiscall *)(void *, void *)>(0x1be654);
    Orig orig(ctx);
    uint8_t *global = ctx.image.At<uint8_t>(0x10cb098);
    uint8_t saved[sizeof(GeneralAllocator)];
    std::memcpy(saved, global, sizeof(saved));
    std::mt19937 rng(0x5EED);
    RZMemory::Service service; // `this` is unused by the slots
    // cGZAllocatorService's slots 3..5 (lane KD, standalone shape): on odd steps our object's own vtable slots
    // (Allocate, Free, Reallocate) stand in for the RZMemory::Service bodies, against the same originals 0x1be4c7,
    // 0x1be654 and 0x1be4e8.
    alignas(4) unsigned char ourServiceMemory[cGZAllocatorService::kSize];
    cGZAllocatorService *ourService = cGZAllocatorServiceAdapters::Construct(ourServiceMemory, nullptr);
    // Called through the object's own vtable by slot number (3, 4, 5), as the game calls them.
    void **const ourSlots = *reinterpret_cast<void **const *>(ourService);
    auto ourAllocate = reinterpret_cast<void *(__thiscall *)(void *, uint32_t)>(ourSlots[3]);
    auto ourFree = reinterpret_cast<void(__thiscall *)(void *, void *)>(ourSlots[4]);
    auto ourReallocate = reinterpret_cast<void *(__thiscall *)(void *, void *, uint32_t)>(ourSlots[5]);
    for (int run = 0; run < 20; ++run) {
        ResetArenas();
        OsRedirect redirect(ctx);
        auto *b = reinterpret_cast<GeneralAllocator *>(gArena[1].base);
        gObjA = global;
        orig.construct(global, nullptr, 0, 0, 0, nullptr, nullptr);
        b->Construct(nullptr, 0, 0, 0, nullptr, nullptr);
        int32_t minimum = 0x100000, increment = 0x80000;
        orig.setOption(global, 3, minimum);
        b->SetOption(3, minimum);
        orig.setOption(global, 4, increment);
        b->SetOption(4, increment);
        RZMemory::testGlobalAllocator = b;
        std::vector<Block> live;
        std::string diff;
        for (int step = 0; step < 300 && diff.empty(); ++step) {
            int op = static_cast<int>(rng() % 8);
            bool viaService = rng() % 2 != 0;
            const bool viaOurs = step % 2 == 1;
            if (op < 3 || live.empty()) {
                uint32_t size = PickSize(rng) % 0x40000;
                auto *pa = static_cast<uint8_t *>(viaService ? svcMalloc(&service, size) : rzMalloc(size));
                auto *pb = static_cast<uint8_t *>(viaService ? (viaOurs ? ourAllocate(ourService, size) : service.ServiceMalloc(size)) : RZMemory::Malloc(size));
                if (Expected(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pa))) != reinterpret_cast<uintptr_t>(pb)) diff = "malloc result";
                else if (pa) live.push_back({pa, pb, size});
            } else if (op < 6) {
                size_t i = rng() % live.size();
                Block blk = live[i];
                live.erase(live.begin() + static_cast<ptrdiff_t>(i));
                int kind = viaService ? 1 + static_cast<int>(rng() % 2) : 0;
                if (kind == 0) { rzFree(blk.a); RZMemory::Free(blk.b); }
                else if (kind == 1) { svcFreeIfNotNull(&service, blk.a); service.ServiceFreeIfNotNull(blk.b); }
                else { svcFree(&service, blk.a); viaOurs ? ourFree(ourService, blk.b) : service.ServiceFree(blk.b); }
            } else if (op == 6) {
                // Null frees: RZFree(null) and the service's checked free are no-ops; the unchecked
                // service free passes null on (also a no-op in Free).
                rzFree(nullptr); RZMemory::Free(nullptr);
                svcFreeIfNotNull(&service, nullptr); service.ServiceFreeIfNotNull(nullptr);
                svcFree(&service, nullptr); service.ServiceFree(nullptr);
                svcFree(&service, nullptr); ourFree(ourService, nullptr);
            } else {
                uint32_t size = PickSize(rng) % 0x40000;
                bool fromNull = rng() % 8 == 0;
                size_t i = rng() % live.size();
                uint8_t *oa = fromNull ? nullptr : live[i].a, *ob = fromNull ? nullptr : live[i].b;
                auto *pa = static_cast<uint8_t *>(viaService ? svcRealloc(&service, oa, size) : rzRealloc(oa, size));
                auto *pb = static_cast<uint8_t *>(viaService ? (viaOurs ? ourReallocate(ourService, ob, size) : service.ServiceRealloc(ob, size)) : RZMemory::Realloc(ob, size));
                if (Expected(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pa))) != reinterpret_cast<uintptr_t>(pb)) diff = "realloc result";
                else if (pa) {
                    if (fromNull) live.push_back({pa, pb, size});
                    else live[i] = {pa, pb, size};
                }
            }
            ++ctx.cases;
            if (diff.empty()) diff = Compare(step % 8 == 7);
        }
        if (diff.empty()) diff = Compare(true);
        orig.shutdown(global);
        b->Shutdown();
        RZMemory::testGlobalAllocator = nullptr;
        gObjA = nullptr;
        if (!diff.empty()) ctx.Fail("run %d: %s", run, diff.c_str());
    }
    ourService->~cGZAllocatorService();
    std::memcpy(global, saved, sizeof(saved));
}
