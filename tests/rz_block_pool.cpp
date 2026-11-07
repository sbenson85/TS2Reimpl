// Differential tests for the power-of-two block pools (src/rz/RZBlockPool.*).
//
// Each side gets a VirtualAlloc arena (the original's imports point at arena A, ours at arena B), so
// regions land at the same offsets. Random allocations (512 B to 512 KB) and frees run on a large and
// a small pool per side; after each step the returned addresses (as arena offsets) and both pools'
// state are compared: the next-unit counter, the current node (as its in-order position) and both
// region maps node by node (key as an arena offset, bitmap, tree shape and colours). The global
// entry points are compared over whole sequences on the image's globals.
#include "difftest.h"
#include "rz/GeneralAllocator.h"
#include "rz/RZBlockPool.h"
#include "rz/cRZCriticalSection.h"
#include "hook/hooks.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr size_t kArenaSize = 256u << 20;

struct Arena {
    uint8_t *base = nullptr;
    size_t next = 0;
};
Arena gArena[2];

template <int S> void *__stdcall ArenaAlloc(void *address, size_t size, uint32_t type, uint32_t protect)
{
    Arena &a = gArena[S];
    if (address) return VirtualAlloc(address, size, type, protect);
    size_t reserve = (size + 0xffff) & ~static_cast<size_t>(0xffff);
    if (a.next + reserve > kArenaSize) return nullptr;
    uint8_t *p = a.base + a.next;
    a.next += reserve;
    return VirtualAlloc(p, size, MEM_COMMIT, protect);
}

template <int S> int __stdcall ArenaFree(void *address, size_t size, uint32_t type)
{
    if (type == MEM_RELEASE && size == 0) return VirtualFree(address, 0x80000, MEM_DECOMMIT); // regions are 512 KB
    return VirtualFree(address, size, type);
}

void ResetArenas()
{
    for (Arena &a : gArena) {
        if (!a.base) a.base = static_cast<uint8_t *>(VirtualAlloc(nullptr, kArenaSize, MEM_RESERVE, PAGE_READWRITE));
        VirtualFree(a.base, kArenaSize, MEM_DECOMMIT);
        a.next = 0;
    }
}

// Releases the arenas when a test ends: each reserves 256 MB, and kept for the whole run they took a quarter of the
// 32-bit address space (two files, two sides).
struct ArenaLease {
    ArenaLease() = default;
    ArenaLease(const ArenaLease &) = delete;
    ~ArenaLease()
    {
        for (Arena &a : gArena) {
            if (a.base) VirtualFree(a.base, 0, MEM_RELEASE);
            a.base = nullptr;
            a.next = 0;
        }
    }
};

// An address as an arena offset (or the value itself when it isn't in that side's arena).
uint32_t Offset(int side, uint32_t value)
{
    uintptr_t base = reinterpret_cast<uintptr_t>(gArena[side].base);
    return value >= base && value < base + kArenaSize ? static_cast<uint32_t>(value - base) | 0x80000000u : value;
}

struct OsRedirect {
    difftest::Context &ctx;
    void *saved[2];
    static constexpr uint32_t kSlots[2] = {0xd9a1e4, 0xd9a1e8}; // VirtualAlloc, VirtualFree
    explicit OsRedirect(difftest::Context &c) : ctx(c)
    {
        void *mine[2] = {reinterpret_cast<void *>(&ArenaAlloc<0>), reinterpret_cast<void *>(&ArenaFree<0>)};
        for (int i = 0; i < 2; ++i) {
            saved[i] = *ctx.image.At<void *>(kSlots[i]);
            *ctx.image.At<void *>(kSlots[i]) = mine[i];
        }
        GeneralAllocatorOs::virtualAlloc = &ArenaAlloc<1>;
        GeneralAllocatorOs::virtualFree = &ArenaFree<1>;
    }
    ~OsRedirect()
    {
        for (int i = 0; i < 2; ++i) *ctx.image.At<void *>(kSlots[i]) = saved[i];
        GeneralAllocatorOs::virtualAlloc = reinterpret_cast<GeneralAllocatorOs::VirtualAllocFn>(&::VirtualAlloc);
        GeneralAllocatorOs::virtualFree = reinterpret_cast<GeneralAllocatorOs::VirtualFreeFn>(&::VirtualFree);
    }
};

struct Node {
    Node *left, *parent, *right;
    char color, isNil;
    uint16_t pad;
    uint32_t key, value;
};

// Serialises a tree (pre-order: key offset, bitmap, colour; '-' for nil) and the in-order position of
// `mark` (-1 for the head).
void Describe(int side, Node *node, Node *mark, std::string &out, int &position, int &markPosition)
{
    if (node->isNil) {
        out += '-';
        return;
    }
    char buf[48];
    std::snprintf(buf, sizeof(buf), "(%x:%x:%d", Offset(side, node->key), node->value, node->color);
    out += buf;
    Describe(side, node->left, mark, out, position, markPosition);
    if (node == mark) markPosition = position;
    ++position;
    Describe(side, node->right, mark, out, position, markPosition);
    out += ')';
}

std::string DescribePool(int side, const RZBlockPool *pool)
{
    std::string out;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "next %x; ", pool->mNextUnit);
    out += buf;
    for (const uint32_t *map : {pool->mPartial, pool->mFull}) {
        auto *head = reinterpret_cast<Node *>(static_cast<uintptr_t>(map[0]));
        int position = 0, markPosition = -1;
        out += "size ";
        out += std::to_string(map[1]);
        out += ' ';
        Describe(side, head->parent, static_cast<Node *>(pool->mCurrent), out, position, markPosition);
        if (map == pool->mPartial) out += "; current " + std::to_string(pool->mCurrent == head ? -1 : markPosition);
        out += "; ";
    }
    return out;
}

struct Orig {
    void *(__thiscall *constructBase)(void *);
    void *(__thiscall *constructSmall)(void *, void *);
    uint32_t(__thiscall *allocSmall)(void *, uint32_t);
    uint32_t(__thiscall *allocLarge)(void *, uint32_t);
    void(__thiscall *freeSmall)(void *, uint32_t, uint32_t);
    void(__thiscall *freeLarge)(void *, uint32_t, uint32_t);
    explicit Orig(const difftest::Context &ctx)
    {
        constructBase = ctx.Original<decltype(constructBase)>(0x1bdd0e);
        constructSmall = ctx.Original<decltype(constructSmall)>(0x1bdd4e);
        allocSmall = ctx.Original<decltype(allocSmall)>(0x1bdd69);
        allocLarge = ctx.Original<decltype(allocLarge)>(0x1bdeb8);
        freeSmall = ctx.Original<decltype(freeSmall)>(0x1be050);
        freeLarge = ctx.Original<decltype(freeLarge)>(0x1be133);
    }
};

// Our pools' vtables in the operation test: copies of the game's with the region slots (0 and 1)
// pointing at our implementations, so our side never reaches the original's region code. (The
// slots are also compared directly in RZBlockPool_Slots.)
struct OurVTables {
    void *largeVt[5], *smallVt[5];
    explicit OurVTables(const difftest::Context &ctx)
    {
        std::memcpy(largeVt, ctx.image.At(RZBlockPool::kLargeVTable), sizeof(largeVt));
        std::memcpy(smallVt, ctx.image.At(RZBlockPool::kSmallVTable), sizeof(smallVt));
        largeVt[0] = MemberAddress(&RZBlockPool::LargeNewRegion);
        largeVt[1] = MemberAddress(&RZBlockPool::LargeReleaseRegion);
        smallVt[0] = MemberAddress(&RZBlockPool::SmallNewRegion);
        smallVt[1] = MemberAddress(&RZBlockPool::SmallReleaseRegion);
    }
};

// A block size: mostly small blocks, some 16 KB-plus, rarely a whole region (512 KB; the original's
// whole-region bug is reproduced, so it's compared like the rest).
uint32_t PickLog2(std::mt19937 &rng)
{
    uint32_t r = rng() % 100;
    if (r < 60) return 9 + rng() % 5;   // 512 B .. 8 KB
    if (r < 98) return 14 + rng() % 5;  // 16 KB .. 256 KB
    return 19;
}

struct Live {
    uint32_t a, b, log2;
};

} // namespace

DIFF_TEST(RZBlockPool_Operations, 0x1bdd69)
{
    ArenaLease lease;
    Orig orig(ctx);
    OurVTables vtables(ctx);
    std::mt19937 rng(0xB10C);
    for (int run = 0; run < 40; ++run) {
        ResetArenas();
        OsRedirect redirect(ctx);
        alignas(4) uint8_t largeA[0x20] = {}, smallA[0x20] = {}, largeB[0x20] = {}, smallB[0x20] = {};
        orig.constructBase(largeA);
        *reinterpret_cast<const void **>(largeA) = ctx.image.At(RZBlockPool::kLargeVTable);
        orig.constructSmall(smallA, largeA);
        auto *lb = reinterpret_cast<RZBlockPool *>(largeB), *sb = reinterpret_cast<RZBlockPool *>(smallB);
        lb->ConstructBase();
        sb->ConstructSmall(lb);
        lb->mVTable = vtables.largeVt;
        sb->mVTable = vtables.smallVt;
        std::vector<Live> live;
        std::string diff;
        for (int step = 0; step < 300 && diff.empty(); ++step) {
            if (live.empty() || rng() % 100 < 55) {
                uint32_t log2 = PickLog2(rng);
                uint32_t ra = log2 < 14 ? orig.allocSmall(smallA, log2) : orig.allocLarge(largeA, log2);
                uint32_t rb = log2 < 14 ? sb->AllocSmall(log2) : lb->AllocLarge(log2);
                if (Offset(0, ra) != Offset(1, rb)) diff = "alloc result (log2 " + std::to_string(log2) + ")";
                else if (ra) live.push_back({ra, rb, log2});
            } else {
                size_t i = rng() % live.size();
                Live blk = live[i];
                live.erase(live.begin() + static_cast<ptrdiff_t>(i));
                if (blk.log2 < 14) {
                    orig.freeSmall(smallA, blk.a, blk.log2);
                    sb->FreeSmall(blk.b, blk.log2);
                } else {
                    orig.freeLarge(largeA, blk.a, blk.log2);
                    lb->FreeLarge(blk.b, blk.log2);
                }
            }
            ++ctx.cases;
            if (diff.empty()) {
                for (int p = 0; p < 2 && diff.empty(); ++p) {
                    std::string da = DescribePool(0, reinterpret_cast<RZBlockPool *>(p ? smallA : largeA));
                    std::string db = DescribePool(1, p ? sb : lb);
                    if (da != db) diff = std::string(p ? "small" : "large") + " pool:\n      " + da + "\n      " + db;
                }
            }
        }
        if (!diff.empty()) ctx.Fail("run %d: %s", run, diff.c_str());
    }
}

// The bit-clearing helpers on random bitmaps, offsets and sizes.
DIFF_TEST(RZBlockPool_ClearBits, 0x1be26e)
{
    auto clearSmall = ctx.Original<void(__thiscall *)(void *, uint32_t *, uint32_t, uint32_t)>(0x1be26e);
    auto clearLarge = ctx.Original<void(__thiscall *)(void *, uint32_t *, uint32_t, uint32_t)>(0x1be295);
    std::mt19937 rng(0xC1EA);
    RZBlockPool pool{};
    for (int i = 0; i < 50000; ++i) {
        bool large = i & 1;
        uint32_t bits = rng(), a = bits, b = bits;
        uint32_t offset = rng() % 0x100000, log2 = (large ? 14 : 9) + rng() % 7;
        (large ? clearLarge : clearSmall)(nullptr, &a, offset, log2);
        if (large) pool.ClearLargeBits(&b, offset, log2);
        else pool.ClearSmallBits(&b, offset, log2);
        ++ctx.cases;
        if (a != b) ctx.Fail("Clear%sBits(%08x, %x, %u): %08x / %08x", large ? "Large" : "Small", bits, offset, log2, a, b);
    }
}

// The global entry points: whole sequences from fresh pools, run by the original and then by ours;
// results and final pool states compared. Each side's creator (0x1be449 / RZBlockPoolsCreate) builds
// the pools first, so our side's vtables can be swapped for the test ones; RZBlockPool_Create
// compares the creators' objects.
DIFF_TEST(RZBlockPool_Globals, 0x1be354)
{
    ArenaLease lease;
    auto alloc = ctx.Original<uint32_t(__cdecl *)(uint32_t)>(0x1be354);
    auto free_ = ctx.Original<void(__cdecl *)(uint32_t, uint32_t)>(0x1be3d6);
    auto create = ctx.Original<void(__cdecl *)()>(0x1be449);
    OurVTables vtables(ctx);
    void **largeGlobal = ctx.image.At<void *>(0x10cb088), **smallGlobal = ctx.image.At<void *>(0x10cb08c);
    void *savedLarge = *largeGlobal, *savedSmall = *smallGlobal;
    // The pools' lock: a cRZCriticalSection the game's static initialisers would have built.
    auto *lock = ctx.image.At<cRZCriticalSection>(0x10cb090);
    alignas(4) uint8_t savedLock[sizeof(cRZCriticalSection)];
    std::memcpy(savedLock, lock, sizeof(savedLock));
    lock->Construct();
    std::mt19937 rng(0x6106);
    for (int run = 0; run < 20; ++run) {
        ResetArenas();
        OsRedirect redirect(ctx);
        std::vector<uint32_t> ops; // log2 for an allocation, or 0x100 + index into the live list to free
        std::vector<uint32_t> resultsA, resultsB;
        void *pools[2][2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 local(static_cast<uint32_t>(run) * 7919u);
            *largeGlobal = nullptr;
            *smallGlobal = nullptr;
            // Create the pools first (each side's creator), then give ours the test vtables.
            if (side) {
                RZBlockPoolsCreate();
                static_cast<RZBlockPool *>(*largeGlobal)->mVTable = vtables.largeVt;
                static_cast<RZBlockPool *>(*smallGlobal)->mVTable = vtables.smallVt;
            } else {
                create();
            }
            std::vector<std::pair<uint32_t, uint32_t>> live;
            auto &results = side ? resultsB : resultsA;
            for (int step = 0; step < 150; ++step) {
                if (live.empty() || local() % 100 < 55) {
                    uint32_t log2 = PickLog2(local);
                    uint32_t r = side ? RZBlockPoolAlloc(log2) : alloc(log2);
                    results.push_back(Offset(side, r));
                    if (r) live.push_back({r, log2});
                } else {
                    size_t i = local() % live.size();
                    auto blk = live[i];
                    live.erase(live.begin() + static_cast<ptrdiff_t>(i));
                    if (side) RZBlockPoolFree(blk.first, blk.second);
                    else free_(blk.first, blk.second);
                }
            }
            pools[side][0] = *largeGlobal;
            pools[side][1] = *smallGlobal;
        }
        ++ctx.cases;
        if (resultsA != resultsB) ctx.Fail("run %d: results differ", run);
        for (int p = 0; p < 2; ++p)
            if (DescribePool(0, static_cast<RZBlockPool *>(pools[0][p])) != DescribePool(1, static_cast<RZBlockPool *>(pools[1][p])))
                ctx.Fail("run %d: %s pool state differs", run, p ? "small" : "large");
        if (*reinterpret_cast<void **>(pools[0][0]) != ctx.image.At(RZBlockPool::kLargeVTable) ||
            *reinterpret_cast<void **>(pools[0][1]) != ctx.image.At(RZBlockPool::kSmallVTable) ||
            reinterpret_cast<RZBlockPool *>(pools[0][1])->mParent != pools[0][0] || static_cast<RZBlockPool *>(pools[1][1])->mParent != pools[1][0])
            ctx.Fail("run %d: pool objects", run);
    }
    *largeGlobal = savedLarge;
    *smallGlobal = savedSmall;
    std::memcpy(lock, savedLock, sizeof(savedLock));
    (void)rng;
}

// The creators (0x1be449 / RZBlockPoolsCreate) and the region slots called directly.
DIFF_TEST(RZBlockPool_Create, 0x1be449)
{
    auto create = ctx.Original<void(__cdecl *)()>(0x1be449);
    void **largeGlobal = ctx.image.At<void *>(0x10cb088), **smallGlobal = ctx.image.At<void *>(0x10cb08c);
    void *savedLarge = *largeGlobal, *savedSmall = *smallGlobal;
    *largeGlobal = *smallGlobal = nullptr;
    create();
    void *la = *largeGlobal, *sa = *smallGlobal;
    *largeGlobal = *smallGlobal = nullptr;
    RZBlockPoolsCreate();
    void *lb = *largeGlobal, *sb = *smallGlobal;
    ++ctx.cases;
    if (DescribePool(0, static_cast<RZBlockPool *>(la)) != DescribePool(1, static_cast<RZBlockPool *>(lb)) ||
        DescribePool(0, static_cast<RZBlockPool *>(sa)) != DescribePool(1, static_cast<RZBlockPool *>(sb)))
        ctx.Fail("created pools differ");
    if (std::memcmp(la, lb, 4) != 0 || std::memcmp(sa, sb, 4) != 0 || static_cast<RZBlockPool *>(sa)->mParent != la ||
        static_cast<RZBlockPool *>(sb)->mParent != lb || static_cast<RZBlockPool *>(la)->mNextUnit != static_cast<RZBlockPool *>(lb)->mNextUnit)
        ctx.Fail("created pool objects differ");
    *largeGlobal = savedLarge;
    *smallGlobal = savedSmall;
}

DIFF_TEST(RZBlockPool_Slots, 0x1be007)
{
    ArenaLease lease;
    auto newLarge = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t *)>(0x1be007);
    auto releaseLarge = ctx.Original<void(__thiscall *)(void *, uint32_t *, uint32_t)>(0x1be216);
    auto newSmall = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t *)>(0x1be01e);
    auto releaseSmall = ctx.Original<void(__thiscall *)(void *, uint32_t *, uint32_t)>(0x1be22a);
    OurVTables vtables(ctx);
    for (int run = 0; run < 10; ++run) {
        ResetArenas();
        OsRedirect redirect(ctx);
        Orig orig(ctx);
        alignas(4) uint8_t largeA[0x20] = {}, smallA[0x20] = {}, largeB[0x20] = {}, smallB[0x20] = {};
        orig.constructBase(largeA);
        *reinterpret_cast<const void **>(largeA) = ctx.image.At(RZBlockPool::kLargeVTable);
        orig.constructSmall(smallA, largeA);
        auto *lb = reinterpret_cast<RZBlockPool *>(largeB), *sb = reinterpret_cast<RZBlockPool *>(smallB);
        lb->ConstructBase();
        sb->ConstructSmall(lb);
        lb->mVTable = vtables.largeVt;
        sb->mVTable = vtables.smallVt;
        uint32_t extraA = 0x77, extraB = 0x77;
        uint32_t ra = newLarge(largeA, &extraA), rb = lb->LargeNewRegion(&extraB);
        uint32_t sa = newSmall(smallA, &extraA), sbr = sb->SmallNewRegion(&extraB);
        ++ctx.cases;
        if (Offset(0, ra) != Offset(1, rb) || Offset(0, sa) != Offset(1, sbr) || extraA != extraB) ctx.Fail("new region");
        releaseSmall(smallA, &extraA, sa);
        sb->SmallReleaseRegion(&extraB, sbr);
        releaseLarge(largeA, &extraA, ra);
        lb->LargeReleaseRegion(&extraB, rb);
        ++ctx.cases;
        for (int p = 0; p < 2; ++p)
            if (DescribePool(0, reinterpret_cast<RZBlockPool *>(p ? smallA : largeA)) != DescribePool(1, p ? sb : lb)) ctx.Fail("release region: pool %d", p);
        MEMORY_BASIC_INFORMATION ia, ib;
        VirtualQuery(reinterpret_cast<void *>(static_cast<uintptr_t>(ra)), &ia, sizeof(ia));
        VirtualQuery(reinterpret_cast<void *>(static_cast<uintptr_t>(rb)), &ib, sizeof(ib));
        if (ia.State != ib.State) ctx.Fail("release region: page state %lx / %lx", ia.State, ib.State);
    }
}
