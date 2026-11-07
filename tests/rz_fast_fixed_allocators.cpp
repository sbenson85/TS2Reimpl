// Differential tests for the per-size instances of cRZFastFixedAllocator2 (src/rz/RZFastFixedAllocators.*).
//
// Free (vf1) runs on every instance. Each side has a pool over six 2^Log2-byte pages (aligned), with
// random use counts (biased to the full threshold so ReturnFullPage and ReleaseEmptyPage run), random
// rings and random free-list order; the bytes outside the headers and free-list links are identical
// on both sides. Random allocated blocks are freed on both sides, and after each free the rings,
// every page's links, free list and count, the freed block's first word and the slot-3 calls are
// compared (pages as indices, pointers as offsets).
//
// NewPage (vf2) runs on every instance, and FreePage (vf3) on the two instances whose copies aren't
// folded (0x19c, 0x1b0). Each side has its own block pools (as in RZBlockPool_Globals: VirtualAlloc
// arenas, the pool lock, our region slots on our side), so the pages come from the real allocator.
// Every new page's bytes are compared word by word (pointers as arena offsets), together with the
// returned offsets and the offsets reused after FreePage.
#include "difftest.h"
#include "rz_block_pool_sides.h"
#include "hook/hooks.h"
#include "rz/GeneralAllocator.h"
#include "rz/RZBlockPool.h"
#include "rz/RZFastFixedAllocators.h"
#include "rz/cRZCriticalSection.h"

#include <malloc.h>
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int kPages = 6;

// ----- Free ------------------------------------------------------------------------------------

struct FreeSide {
    uint8_t *pages[kPages] = {};
    std::vector<int> calls; // slot 3: the index of each page handed back
};
FreeSide gFree[2];
int gFreeSide = 0;
uint32_t gPageBytes = 0x800;

int PageIndex(int side, uintptr_t value)
{
    for (int i = 0; i < kPages; ++i) {
        uintptr_t base = reinterpret_cast<uintptr_t>(gFree[side].pages[i]);
        if (value >= base && value < base + gPageBytes) return i;
    }
    return -1;
}

// A pointer as page index and offset ("p3+40"), "0" for null, "?" for anything off the pages.
std::string Norm(int side, uintptr_t value)
{
    if (!value) return "0";
    int i = PageIndex(side, value);
    if (i < 0) return "?";
    return "p" + std::to_string(i) + "+" + std::to_string(value - reinterpret_cast<uintptr_t>(gFree[side].pages[i]));
}

std::string Ring(int side, RZFixedPage *start)
{
    if (!start) return "null";
    std::string out;
    RZFixedPage *p = start;
    for (int n = 0; n <= kPages; ++n) {
        out += Norm(side, reinterpret_cast<uintptr_t>(p));
        if (p->next->prev != p) out += "!";
        p = p->next;
        if (p == start) return out;
        out += ",";
    }
    return out + "...";
}

std::string Describe(int side, const RZFixedAllocatorBase &pool)
{
    std::string out = "cur " + Ring(side, pool.mCurrent) + " full " + Ring(side, pool.mFull) + " pages";
    for (int i = 0; i < kPages; ++i) {
        auto *p = reinterpret_cast<RZFixedPage *>(gFree[side].pages[i]);
        char used[16];
        std::snprintf(used, sizeof(used), "%x", p->used);
        out += " [" + Norm(side, reinterpret_cast<uintptr_t>(p->next)) + " " + Norm(side, reinterpret_cast<uintptr_t>(p->prev)) +
               " " + Norm(side, reinterpret_cast<uintptr_t>(p->freeList)) + " " + used + "]";
    }
    return out;
}

void __fastcall FakeFreePage(void *, void *, RZFixedPage *page)
{
    gFree[gFreeSide].calls.push_back(PageIndex(gFreeSide, reinterpret_cast<uintptr_t>(page)));
}

void *gFreeVTable[4] = {nullptr, nullptr, nullptr, reinterpret_cast<void *>(&FakeFreePage)};

using FreeFn = void(__thiscall *)(void *, void *);

// What the random generator decides for one page; both sides are built from the same plan.
struct PagePlan {
    bool onFull = false;
    uint32_t count = 0;               // blocks in use
    std::vector<uint32_t> blocks;     // every block index, the first `count` of them in use
    std::vector<uint8_t> fill;        // the page's bytes before the pool links it up
};

template <class A> uint32_t BlockOffset(uint32_t index) { return A::kHeader + index * A::kBlockSize; }

template <class A> void Build(int side, const std::vector<PagePlan> &plan, const std::vector<int> &current,
                              const std::vector<int> &full, RZFixedAllocatorBase &pool)
{
    for (int i = 0; i < kPages; ++i) {
        uint8_t *page = gFree[side].pages[i];
        const PagePlan &pl = plan[i];
        std::memcpy(page, pl.fill.data(), gPageBytes);
        auto *header = reinterpret_cast<RZFixedPage *>(page);
        // Free list: the blocks past `count`, chained in plan order.
        uint8_t *head = nullptr;
        uint8_t *next = nullptr;
        for (size_t k = pl.blocks.size(); k-- > pl.count;) {
            uint8_t *block = page + BlockOffset<A>(pl.blocks[k]);
            *reinterpret_cast<uint8_t **>(block) = next;
            next = block;
            head = block;
        }
        header->freeList = head;
        header->used = (pl.onFull ? 0x40000000u : 0u) + pl.count;
    }
    auto link = [&](const std::vector<int> &order) -> RZFixedPage * {
        if (order.empty()) return nullptr;
        size_t n = order.size();
        for (size_t k = 0; k < n; ++k) {
            auto *p = reinterpret_cast<RZFixedPage *>(gFree[side].pages[order[k]]);
            p->next = reinterpret_cast<RZFixedPage *>(gFree[side].pages[order[(k + 1) % n]]);
            p->prev = reinterpret_cast<RZFixedPage *>(gFree[side].pages[order[(k + n - 1) % n]]);
        }
        return reinterpret_cast<RZFixedPage *>(gFree[side].pages[order[0]]);
    };
    pool.mVTable = gFreeVTable;
    pool.mCurrent = link(current);
    pool.mFull = link(full);
}

template <class A> void CheckFree(difftest::Context &ctx, uint32_t origRva)
{
    auto origFree = ctx.Original<FreeFn>(origRva);
    gPageBytes = A::kPageSize;
    for (auto &side : gFree)
        for (auto &p : side.pages) p = static_cast<uint8_t *>(_aligned_malloc(A::kPageSize, A::kPageSize));
    const uint32_t threshold = A::kFullMark - 0x40000000u;
    std::mt19937 rng(0xF4EE + A::kBlockSize * 977u + A::kLog2);
    for (int run = 0; run < 40; ++run) {
        std::vector<PagePlan> plan(kPages);
        for (int i = 0; i < kPages; ++i) {
            PagePlan &pl = plan[static_cast<size_t>(i)];
            pl.onFull = i > 0 && (rng() & 1); // page 0 stays on the current ring
            uint32_t r = rng() % 4;
            if (r == 0) pl.count = 1;
            else if (r == 1 && threshold + 1 <= A::kBlocks) pl.count = threshold + 1;
            else pl.count = 1 + rng() % A::kBlocks;
            pl.blocks.resize(A::kBlocks);
            for (uint32_t k = 0; k < A::kBlocks; ++k) pl.blocks[k] = k;
            for (uint32_t k = A::kBlocks; k > 1; --k) std::swap(pl.blocks[k - 1], pl.blocks[rng() % k]);
            pl.fill.resize(A::kPageSize);
            for (auto &b : pl.fill) b = static_cast<uint8_t>(rng());
        }
        std::vector<int> current, full;
        for (int i = 0; i < kPages; ++i) (plan[static_cast<size_t>(i)].onFull ? full : current).push_back(i);
        std::shuffle(current.begin(), current.end(), rng);
        std::shuffle(full.begin(), full.end(), rng);

        RZFixedAllocatorBase pool[2];
        for (int side = 0; side < 2; ++side) {
            gFree[side].calls.clear();
            gFreeSide = side;
            Build<A>(side, plan, current, full, pool[side]);
        }
        std::vector<std::vector<uint32_t>> allocs(kPages);
        std::vector<bool> alive(kPages, true);
        for (int i = 0; i < kPages; ++i)
            for (uint32_t k = 0; k < plan[static_cast<size_t>(i)].count; ++k) allocs[static_cast<size_t>(i)].push_back(plan[static_cast<size_t>(i)].blocks[k]);
        std::string before = Describe(0, pool[0]), beforeOurs = Describe(1, pool[1]);
        if (before != beforeOurs) ctx.Fail("run %d: initial state differs:\n      %s\n      %s", run, before.c_str(), beforeOurs.c_str());

        for (int step = 0; step < 80; ++step) {
            std::vector<int> candidates;
            for (int i = 0; i < kPages; ++i)
                if (alive[static_cast<size_t>(i)] && !allocs[static_cast<size_t>(i)].empty()) candidates.push_back(i);
            if (candidates.empty()) break;
            int p = candidates[rng() % candidates.size()];
            auto &list = allocs[static_cast<size_t>(p)];
            size_t j = rng() % list.size();
            uint32_t index = list[j];
            list[j] = list.back();
            list.pop_back();

            uintptr_t block[2];
            gFreeSide = 0;
            block[0] = reinterpret_cast<uintptr_t>(gFree[0].pages[p]) + BlockOffset<A>(index);
            origFree(&pool[0], reinterpret_cast<void *>(block[0]));
            gFreeSide = 1;
            block[1] = reinterpret_cast<uintptr_t>(gFree[1].pages[p]) + BlockOffset<A>(index);
            reinterpret_cast<A *>(&pool[1])->Free(reinterpret_cast<void *>(block[1]));
            ++ctx.cases;

            if (gFree[0].calls != gFree[1].calls) {
                ctx.Fail("run %d step %d (page %d block %u): slot-3 calls differ", run, step, p, index);
                break;
            }
            if (!gFree[0].calls.empty() && gFree[0].calls.back() == p) alive[static_cast<size_t>(p)] = false;
            std::string blockA = Norm(0, *reinterpret_cast<uintptr_t *>(block[0])), blockB = Norm(1, *reinterpret_cast<uintptr_t *>(block[1]));
            std::string da = Describe(0, pool[0]), db = Describe(1, pool[1]);
            if (blockA != blockB || da != db) {
                ctx.Fail("run %d step %d (page %d block %u): freed block %s vs %s\n      %s\n      %s", run, step, p, index, blockA.c_str(),
                         blockB.c_str(), da.c_str(), db.c_str());
                break;
            }
        }
    }
    for (auto &side : gFree)
        for (auto &p : side.pages) _aligned_free(p);
}

// ----- NewPage / FreePage ----------------------------------------------------------------------

using namespace blockpools;


using NewFn = RZFixedPage *(__thiscall *)(void *);
using PageFreeFn = void(__thiscall *)(void *, void *);

template <class A> void CheckNewPage(difftest::Context &ctx, uint32_t origNewRva, uint32_t origFreeRva)
{
    ArenaLease lease;
    auto origNew = ctx.Original<NewFn>(origNewRva);
    auto origPageFree = origFreeRva ? ctx.Original<PageFreeFn>(origFreeRva) : nullptr;
    void **largeGlobal = ctx.image.At<void *>(0x10cb088), **smallGlobal = ctx.image.At<void *>(0x10cb08c);
    void *savedLarge = *largeGlobal, *savedSmall = *smallGlobal;
    // The pools' lock: a cRZCriticalSection the game's static initialisers would have built.
    auto *lock = ctx.image.At<cRZCriticalSection>(0x10cb090);
    alignas(4) uint8_t savedLock[sizeof(cRZCriticalSection)];
    std::memcpy(savedLock, lock, sizeof(savedLock));
    lock->Construct();
    OurVTables vtables(ctx);
    std::mt19937 rng(0x2A6E + A::kBlockSize * 31u + A::kLog2);
    for (int run = 0; run < 8; ++run) {
        ResetArenas();
        OsRedirect redirect(ctx);
        std::vector<uint32_t> results[2];
        std::vector<std::vector<uint32_t>> pageWords[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 local(static_cast<uint32_t>(run) * 7919u + 17u);
            *largeGlobal = nullptr;
            *smallGlobal = nullptr;
            if (side) {
                RZBlockPoolsCreate();
                static_cast<RZBlockPool *>(*largeGlobal)->mVTable = vtables.largeVt;
                static_cast<RZBlockPool *>(*smallGlobal)->mVTable = vtables.smallVt;
            } else {
                auto create = ctx.Original<void(__cdecl *)()>(0x1be449);
                create();
            }
            RZFixedAllocatorBase dummy;
            dummy.mVTable = nullptr;
            std::vector<uint32_t> live;
            for (int step = 0; step < 24; ++step) {
                if (origPageFree && !live.empty() && local() % 3 == 0) {
                    size_t i = local() % live.size();
                    uint32_t page = live[i];
                    live.erase(live.begin() + static_cast<ptrdiff_t>(i));
                    if (side) reinterpret_cast<A *>(&dummy)->FreePage(reinterpret_cast<RZFixedPage *>(page));
                    else origPageFree(&dummy, reinterpret_cast<void *>(page));
                    results[side].push_back(0xF0000000u);
                    continue;
                }
                RZFixedPage *page = side ? reinterpret_cast<A *>(&dummy)->NewPage() : origNew(&dummy);
                uint32_t address = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(page));
                results[side].push_back(Offset(side, address));
                if (address & (A::kPageSize - 1)) ctx.Fail("run %d: page %x not aligned to %x", run, address, A::kPageSize);
                // The whole page, with pointers as arena offsets. Fresh pages are zero, so the bytes
                // the function doesn't write compare equal too.
                std::vector<uint32_t> words(A::kPageSize / 4);
                std::memcpy(words.data(), page, A::kPageSize);
                for (auto &w : words) w = Offset(side, w);
                pageWords[side].push_back(words);
                live.push_back(address);
            }
            *largeGlobal = savedLarge;
            *smallGlobal = savedSmall;
        }
        ++ctx.cases;
        if (results[0] != results[1]) ctx.Fail("run %d: results differ", run);
        if (pageWords[0] != pageWords[1]) ctx.Fail("run %d: page contents differ", run);
    }
    std::memcpy(lock, savedLock, sizeof(savedLock));
}

} // namespace

// ----- Free (vf1) ------------------------------------------------------------------------------
// The 0x14 instances' vf1 stubs at 0x20febe, 0xb2867e and 0x22edd1 are 5-byte jumps to the
// functions tested here (0x2102b7, 0xb2883d, 0x22f908); the stubs themselves are not hooked.

DIFF_TEST(RZFastFixedAllocators_Free0x0c, 0x17aa00) { CheckFree<RZFastFixed0xc>(ctx, 0x17aa00); }
DIFF_TEST(RZFastFixedAllocators_Free0x10, 0x177030) { CheckFree<RZFastFixed0x10>(ctx, 0x177030); }
DIFF_TEST(RZFastFixedAllocators_Free0x14, 0x15a9f0) { CheckFree<RZFastFixed0x14>(ctx, 0x15a9f0); }
DIFF_TEST(RZFastFixedAllocators_Free0x18, 0x1607c0) { CheckFree<RZFastFixed0x18>(ctx, 0x1607c0); }
DIFF_TEST(RZFastFixedAllocators_Free0x1c, 0x160740) { CheckFree<RZFastFixed0x1c>(ctx, 0x160740); }
DIFF_TEST(RZFastFixedAllocators_Free0x20, 0x14a300) { CheckFree<RZFastFixed0x20>(ctx, 0x14a300); }
DIFF_TEST(RZFastFixedAllocators_Free0x28, 0x1657f0) { CheckFree<RZFastFixed0x28>(ctx, 0x1657f0); }
DIFF_TEST(RZFastFixedAllocators_Free0x34, 0x2102b7) { CheckFree<RZFastFixed0x34>(ctx, 0x2102b7); }
DIFF_TEST(RZFastFixedAllocators_Free0x38, 0xb2883d) { CheckFree<RZFastFixed0x38>(ctx, 0xb2883d); }
DIFF_TEST(RZFastFixedAllocators_Free0x3c, 0x54e1d0) { CheckFree<RZFastFixed0x3c>(ctx, 0x54e1d0); }
DIFF_TEST(RZFastFixedAllocators_Free0x54, 0x8c44f0) { CheckFree<RZFastFixed0x54>(ctx, 0x8c44f0); }
DIFF_TEST(RZFastFixedAllocators_Free0x58, 0x16ddd0) { CheckFree<RZFastFixed0x58>(ctx, 0x16ddd0); }
DIFF_TEST(RZFastFixedAllocators_Free0x5c, 0x1630f0) { CheckFree<RZFastFixed0x5c>(ctx, 0x1630f0); }
DIFF_TEST(RZFastFixedAllocators_Free0x74, 0x205150) { CheckFree<RZFastFixed0x74>(ctx, 0x205150); }
DIFF_TEST(RZFastFixedAllocators_Free0x98, 0xaa3ca0) { CheckFree<RZFastFixed0x98>(ctx, 0xaa3ca0); }
DIFF_TEST(RZFastFixedAllocators_Free0x9c, 0xa34fd0) { CheckFree<RZFastFixed0x9c>(ctx, 0xa34fd0); }
DIFF_TEST(RZFastFixedAllocators_Free0x19c, 0x1645b0) { CheckFree<RZFastFixed0x19c>(ctx, 0x1645b0); }
DIFF_TEST(RZFastFixedAllocators_Free0x1b0, 0x22f908) { CheckFree<RZFastFixed0x1b0>(ctx, 0x22f908); }

// ----- NewPage (vf2) and FreePage (vf3) --------------------------------------------------------

DIFF_TEST(RZFastFixedAllocators_NewPage0x0c, 0x17aa40) { CheckNewPage<RZFastFixed0xc>(ctx, 0x17aa40, 0); }
DIFF_TEST(RZFastFixedAllocators_NewPage0x10, 0x177070) { CheckNewPage<RZFastFixed0x10>(ctx, 0x177070, 0); }
DIFF_TEST(RZFastFixedAllocators_NewPage0x14, 0x15aa30) { CheckNewPage<RZFastFixed0x14>(ctx, 0x15aa30, 0); }
DIFF_TEST(RZFastFixedAllocators_NewPage0x18, 0x160800) { CheckNewPage<RZFastFixed0x18>(ctx, 0x160800, 0); }
DIFF_TEST(RZFastFixedAllocators_NewPage0x1c, 0x160780) { CheckNewPage<RZFastFixed0x1c>(ctx, 0x160780, 0); }
DIFF_TEST(RZFastFixedAllocators_NewPage0x20, 0x14a340) { CheckNewPage<RZFastFixed0x20>(ctx, 0x14a340, 0); }
DIFF_TEST(RZFastFixedAllocators_NewPage0x28, 0x165830) { CheckNewPage<RZFastFixed0x28>(ctx, 0x165830, 0); }
DIFF_TEST(RZFastFixedAllocators_NewPage0x34, 0x20fec3) { CheckNewPage<RZFastFixed0x34>(ctx, 0x20fec3, 0); }
DIFF_TEST(RZFastFixedAllocators_NewPage0x38, 0xb28683) { CheckNewPage<RZFastFixed0x38>(ctx, 0xb28683, 0); }
DIFF_TEST(RZFastFixedAllocators_NewPage0x3c, 0x54e210) { CheckNewPage<RZFastFixed0x3c>(ctx, 0x54e210, 0); }
DIFF_TEST(RZFastFixedAllocators_NewPage0x54, 0x8c4530) { CheckNewPage<RZFastFixed0x54>(ctx, 0x8c4530, 0); }
DIFF_TEST(RZFastFixedAllocators_NewPage0x58, 0x16de10) { CheckNewPage<RZFastFixed0x58>(ctx, 0x16de10, 0); }
DIFF_TEST(RZFastFixedAllocators_NewPage0x5c, 0x163130) { CheckNewPage<RZFastFixed0x5c>(ctx, 0x163130, 0); }
DIFF_TEST(RZFastFixedAllocators_NewPage0x74, 0x205190) { CheckNewPage<RZFastFixed0x74>(ctx, 0x205190, 0); }
DIFF_TEST(RZFastFixedAllocators_NewPage0x98, 0xaa3ce0) { CheckNewPage<RZFastFixed0x98>(ctx, 0xaa3ce0, 0); }
DIFF_TEST(RZFastFixedAllocators_NewPage0x9c, 0xa35010) { CheckNewPage<RZFastFixed0x9c>(ctx, 0xa35010, 0); }
DIFF_TEST(RZFastFixedAllocators_NewPage0x19c, 0x1645f0) { CheckNewPage<RZFastFixed0x19c>(ctx, 0x1645f0, 0x164630); }
DIFF_TEST(RZFastFixedAllocators_NewPage0x1b0, 0x22edd6) { CheckNewPage<RZFastFixed0x1b0>(ctx, 0x22edd6, 0x22ee06); }

// ----- The classes in the standalone shape -------------------------------------------------------
// RZFastFixedAllocators_Shape*: each class's whole life on real block pools (as NewPage above), the original on one side,
// ours on the other, every slot called through the object's own vtable:
//   route A (the shape): the image's object from its constructor (the out-of-line copy where there is one, else the
//     inlined form by hand: the game's vtable, the original NewPage through it, the ring of one), with the game's
//     vtable; ours from our constructor (the hooked constructor's adapter where the image has one), our vtable.
//   route B (the adapters): our side's object is the image's, with a copy of the game's vtable whose slots point at
//     the hooks' adapters (the view's members), as a game-built object reaches them in game.
// Then the same random operations on both: Alloc (slot 0: 0x14a2d0, or the jump 0x20feb9 in the 0x34 / 0x38 / 0x1b0
// vtables, so the jump is run too), a junk word written into the block, Free (slot 1) of a random live block, in four
// phases (fill over two pages, free most, fill one page, free all: TakeFreePage, NewPage, ReturnFullPage,
// ReleaseEmptyPage and FreePage all run; the third run fills another page instead of the last phase, so its destructor meets full pages). After each step: the block handed out (arena offset) and both rings (each
// page's offset, free-list head and count). Before destruction every page's bytes (pointers as offsets). Then the
// destructor (the image's out-of-line copy, else its inlined form: the base class's game vtable stored and 0x1be23b on
// both rings; ours: the hooked destructor's adapter, else our destructor), and the next 12 RZBlockPoolAlloc results of
// the page size (which pages the destructor gave back). The vtable word is checked by class: the game's vtable
// (built) and its base's (destroyed) on the image's side, ours by our RTTI name.
namespace {

template <class R, class... A> R CallSlot(void *object, int slot, A... args)
{
    void *fn = (*static_cast<void ***>(object))[slot];
    return reinterpret_cast<R(__thiscall *)(void *, A...)>(fn)(object, args...);
}

struct ShapeSpec {
    uint32_t gameVTable; // the class's
    uint32_t baseVTable; // cRZFastFixedAllocator2<size>'s (the same for a size's own class)
    uint32_t ctorRva;    // the base's out-of-line constructor, 0: inlined only
    uint32_t dtorRva;    // the base's out-of-line destructor, 0: inlined only
};

uint32_t Addr(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

std::string OurClassName(const void *vtable)
{
    auto *col = static_cast<const uint32_t *const *>(vtable)[-1];
    return std::string(reinterpret_cast<const char *>(static_cast<uintptr_t>(col[3])) + 8);
}

template <class X> std::string DescribeShape(int side, void *object)
{
    auto *pool = static_cast<rz::cRZFastFixedAllocator2Common *>(static_cast<X *>(object));
    std::string out;
    auto ring = [&](RZFixedPage *start) {
        if (!start) return std::string("null");
        std::string r;
        RZFixedPage *p = start;
        for (int n = 0; n < 64; ++n) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%x/%x/%x ", Offset(side, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p))),
                          Offset(side, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p->freeList))), p->used);
            r += buf;
            if (p->next->prev != p) r += "! ";
            p = p->next;
            if (p == start) return r;
        }
        return r + "...";
    };
    return "cur " + ring(pool->mCurrent) + "full " + ring(pool->mFull);
}

template <class X> std::vector<uint32_t> PageWords(int side, void *object)
{
    auto *pool = static_cast<rz::cRZFastFixedAllocator2Common *>(static_cast<X *>(object));
    std::vector<uint32_t> out;
    for (RZFixedPage *start : {pool->mCurrent, pool->mFull}) {
        if (!start) continue;
        RZFixedPage *p = start;
        do {
            const auto *w = reinterpret_cast<const uint32_t *>(p);
            // Word 3 is the page's count (0x40000000 added on the full ring), not a pointer: kept raw, since an arena
            // can sit at 0x40000000 and one side's normalisation would rewrite it.
            for (uint32_t i = 0; i < X::kPageSize / 4; ++i) out.push_back(i == 3 ? w[i] : Offset(side, w[i]));
            p = p->next;
        } while (p != start);
    }
    return out;
}

template <class X, class View> void CheckShape(difftest::Context &ctx, const ShapeSpec &spec, bool adapterRoute)
{
    ArenaLease lease;
    void **largeGlobal = ctx.image.At<void *>(0x10cb088), **smallGlobal = ctx.image.At<void *>(0x10cb08c);
    void *savedLarge = *largeGlobal, *savedSmall = *smallGlobal;
    auto *lock = ctx.image.At<cRZCriticalSection>(0x10cb090);
    alignas(4) uint8_t savedLock[sizeof(cRZCriticalSection)];
    std::memcpy(savedLock, lock, sizeof(savedLock));
    lock->Construct();
    OurVTables vtables(ctx);
    void *const gameVt = ctx.image.At<void>(spec.gameVTable);
    void *const baseVt = ctx.image.At<void>(spec.baseVTable);
    // Route B's vtable: the game's, its slots on the hooks' adapters.
    void *hooked[4] = {MemberAddress(&RZFixedAllocatorBase::Alloc), MemberAddress(&View::Free), MemberAddress(&View::NewPage),
                       MemberAddress(&View::FreePage)};
    auto imageBuild = [&](void *object) {
        if (spec.ctorRva) {
            ctx.Original<void *(__thiscall *)(void *)>(spec.ctorRva)(object);
        } else { // the inlined constructor
            *static_cast<void **>(object) = baseVt;
            auto *page = CallSlot<RZFixedPage *>(object, 2);
            auto *words = static_cast<RZFixedPage **>(object);
            words[1] = page;
            words[2] = nullptr;
            page->prev = page;
            page->next = page->prev;
        }
        *static_cast<void **>(object) = gameVt; // a cRZFastAllocator<T>: its own vtable after the base's
    };
    auto imageDestroy = [&](void *object) {
        if (spec.dtorRva) {
            ctx.Original<void(__thiscall *)(void *)>(spec.dtorRva)(object);
        } else { // the inlined destructor
            *static_cast<void **>(object) = baseVt;
            auto visit = ctx.Original<void(__thiscall *)(void *, void *)>(0x1be23b);
            visit(object, static_cast<void **>(object)[1]);
            visit(object, static_cast<void **>(object)[2]);
        }
    };
    using Adapters = rz::FastAllocatorAdapters<X>;
    std::mt19937 seeds(0x5A9E + X::kBlockSize * 131u + (adapterRoute ? 7u : 0u) + spec.gameVTable);
    for (int run = 0; run < 3; ++run) {
        ResetArenas();
        OsRedirect redirect(ctx);
        const uint32_t seed = seeds();
        std::vector<std::string> trace[2];
        std::vector<uint32_t> words[2], after[2];
        int builtCode[2] = {-1, -1}, destroyedCode[2] = {-1, -1};
        for (int side = 0; side < 2; ++side) {
            std::mt19937 rng(seed);
            *largeGlobal = nullptr;
            *smallGlobal = nullptr;
            if (side) {
                RZBlockPoolsCreate();
                static_cast<RZBlockPool *>(*largeGlobal)->mVTable = vtables.largeVt;
                static_cast<RZBlockPool *>(*smallGlobal)->mVTable = vtables.smallVt;
            } else {
                ctx.Original<void(__cdecl *)()>(0x1be449)();
            }
            alignas(8) uint8_t storage[sizeof(X) + 8];
            std::memset(storage, 0xcd, sizeof(storage));
            void *object = storage;
            void *copy[4];
            if (side == 0 || adapterRoute) {
                imageBuild(object);
                if (side) {
                    std::memcpy(copy, hooked, sizeof(copy));
                    *static_cast<void **>(object) = copy;
                }
            } else if (spec.ctorRva && spec.gameVTable == spec.baseVTable) {
                Adapters::Construct(static_cast<X *>(object));
            } else {
                ::new (object) X;
            }
            void *vt = *static_cast<void **>(object);
            builtCode[side] = vt == gameVt ? 1 : vt == copy ? 3 : OurClassName(vt).rfind(".?AV?$cRZFast", 0) == 0 ? 2 : 0;
            trace[side].push_back(DescribeShape<X>(side, object));
            {
                // Slots 2 and 3 by their numbers (the game calls them so): a page made and given back.
                auto *extra = CallSlot<RZFixedPage *>(object, 2);
                char buf[48];
                std::snprintf(buf, sizeof(buf), "new %x %x ", Offset(side, Addr(extra)), Offset(side, Addr(extra->freeList)));
                CallSlot<void>(object, 3, extra);
                trace[side].push_back(buf + DescribeShape<X>(side, object));
            }
            std::vector<uint32_t> live;
            auto alloc = [&](uint32_t n) {
                for (uint32_t i = 0; i < n; ++i) {
                    void *block = CallSlot<void *>(object, 0);
                    *static_cast<uint32_t *>(block) = rng() & 0xffff; // below every arena: not taken for an offset
                    uint32_t address = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(block));
                    live.push_back(address);
                    char buf[24];
                    std::snprintf(buf, sizeof(buf), "a %x ", Offset(side, address));
                    trace[side].push_back(buf + DescribeShape<X>(side, object));
                }
            };
            auto release = [&](size_t keep) {
                while (live.size() > keep) {
                    size_t i = rng() % live.size();
                    uint32_t address = live[i];
                    live[i] = live.back();
                    live.pop_back();
                    CallSlot<void>(object, 1, reinterpret_cast<void *>(static_cast<uintptr_t>(address)));
                    char buf[24];
                    std::snprintf(buf, sizeof(buf), "f %x ", Offset(side, address));
                    trace[side].push_back(buf + DescribeShape<X>(side, object));
                }
            };
            alloc(2 * X::kBlocks + rng() % X::kBlocks);
            release(live.size() / 4);
            alloc(X::kBlocks);
            words[side] = PageWords<X>(side, object);
            if (run < 2) {
                release(rng() % 3);
            } else {
                // Run 2 destroys the pool with its blocks live: one more page's worth exhausts the current page, so
                // the full ring isn't empty and the destructor gives it back too.
                alloc(X::kBlocks);
                if (!static_cast<rz::cRZFastFixedAllocator2Common *>(static_cast<X *>(object))->mFull)
                    ctx.Fail("run 2 side %d: no page on the full ring before the destructor", side);
            }
            if (side == 0 || adapterRoute) imageDestroy(object);
            else if (spec.dtorRva && spec.gameVTable == spec.baseVTable) Adapters::Destruct(static_cast<X *>(object));
            else static_cast<X *>(object)->~X();
            vt = *static_cast<void **>(object);
            destroyedCode[side] = vt == baseVt ? 1 : OurClassName(vt).rfind(".?AV?$cRZFastFixedAllocator2@", 0) == 0 ? 2 : 0;
            for (int k = 0; k < 12; ++k) {
                uint32_t page = side ? RZBlockPoolAlloc(X::kLog2) : ctx.Original<uint32_t(__cdecl *)(uint32_t)>(0x1be354)(X::kLog2);
                after[side].push_back(Offset(side, page));
            }
            *largeGlobal = savedLarge;
            *smallGlobal = savedSmall;
        }
        ++ctx.cases;
        const int ourBuilt = adapterRoute ? 1 : 2, ourDestroyed = adapterRoute ? 1 : 2;
        if (builtCode[0] != 1 || builtCode[1] != (adapterRoute ? 3 : ourBuilt))
            ctx.Fail("run %d: built vtable codes %d / %d", run, builtCode[0], builtCode[1]);
        if (destroyedCode[0] != 1 || destroyedCode[1] != ourDestroyed)
            ctx.Fail("run %d: destroyed vtable codes %d / %d", run, destroyedCode[0], destroyedCode[1]);
        if (trace[0].size() != trace[1].size()) ctx.Fail("run %d: %u steps vs %u", run, unsigned(trace[0].size()), unsigned(trace[1].size()));
        for (size_t i = 0; i < trace[0].size() && i < trace[1].size(); ++i) {
            if (trace[0][i] != trace[1][i]) {
                ctx.Fail("run %d step %u:\n      %s\n      %s", run, unsigned(i), trace[0][i].c_str(), trace[1][i].c_str());
                break;
            }
        }
        if (words[0] != words[1]) {
            size_t i = 0;
            while (i < words[0].size() && i < words[1].size() && words[0][i] == words[1][i]) ++i;
            ctx.Fail("run %d: page contents differ at word %u of %u/%u: %08x vs %08x", run, unsigned(i), unsigned(words[0].size()),
                     unsigned(words[1].size()), i < words[0].size() ? words[0][i] : 0, i < words[1].size() ? words[1][i] : 0);
        }
        if (after[0] != after[1]) ctx.Fail("run %d: the pools after the destructor differ", run);
    }
    std::memcpy(lock, savedLock, sizeof(savedLock));
}

template <class X, class View> void CheckShapeBoth(difftest::Context &ctx, const ShapeSpec &spec)
{
    CheckShape<X, View>(ctx, spec, false);
    CheckShape<X, View>(ctx, spec, true);
}

template <uint32_t N> using FF = rz::cRZFastFixedAllocator2<N>;
template <class T> using FA = rz::cRZFastAllocator<T>;
namespace pooled = rz::pooled;

} // namespace

// The classes' own vtables; slot 0 0x14a2d0 / 0x20feb9 and the folded slot 3 copies run on the image's side.
DIFF_TEST(RZFastFixedAllocators_Shape0x0c, 0x17acf0) { CheckShapeBoth<FF<0x0c>, RZFastFixed0xc>(ctx, {0xd9f9bc, 0xd9f9bc, 0x17acf0, 0x17ae20}); }
DIFF_TEST(RZFastFixedAllocators_Shape0x10, 0x177350) { CheckShapeBoth<FF<0x10>, RZFastFixed0x10>(ctx, {0xd9f520, 0xd9f520, 0x177350, 0x177710}); }
DIFF_TEST(RZFastFixedAllocators_Shape0x14, 0x15b7e0) { CheckShapeBoth<FF<0x14>, RZFastFixed0x14>(ctx, {0xd9d05c, 0xd9d05c, 0x15b7e0, 0x15dad0}); }
DIFF_TEST(RZFastFixedAllocators_Shape0x18, 0x160840) { CheckShapeBoth<FF<0x18>, RZFastFixed0x18>(ctx, {0xd9d578, 0xd9d578, 0x160840, 0x160950}); }
DIFF_TEST(RZFastFixedAllocators_Shape0x1c, 0x1608a0) { CheckShapeBoth<FF<0x1c>, RZFastFixed0x1c>(ctx, {0xd9d564, 0xd9d564, 0x1608a0, 0}); }
DIFF_TEST(RZFastFixedAllocators_Shape0x20, 0x14ba20) { CheckShapeBoth<FF<0x20>, RZFastFixed0x20>(ctx, {0xd9c2d4, 0xd9c2d4, 0x14ba20, 0x14be30}); }
DIFF_TEST(RZFastFixedAllocators_Shape0x28, 0x165870) { CheckShapeBoth<FF<0x28>, RZFastFixed0x28>(ctx, {0xd9ded4, 0xd9ded4, 0x165870, 0}); }
DIFF_TEST(RZFastFixedAllocators_Shape0x34, 0x20ff00) { CheckShapeBoth<FF<0x34>, RZFastFixed0x34>(ctx, {0xda8148, 0xda8148, 0x20ff00, 0x20ffea}); }
DIFF_TEST(RZFastFixedAllocators_Shape0x38, 0xb286b0) { CheckShapeBoth<FF<0x38>, RZFastFixed0x38>(ctx, {0xe813c4, 0xe813c4, 0xb286b0, 0xb2875e}); }
DIFF_TEST(RZFastFixedAllocators_Shape0x3c, 0x54e250) { CheckShapeBoth<FF<0x3c>, RZFastFixed0x3c>(ctx, {0xdd3fec, 0xdd3fec, 0x54e250, 0x54e520}); }
DIFF_TEST(RZFastFixedAllocators_Shape0x54, 0x163170) { CheckShapeBoth<FF<0x54>, RZFastFixed0x54>(ctx, {0xe4f1fc, 0xe4f1fc, 0, 0}); }
DIFF_TEST(RZFastFixedAllocators_Shape0x58, 0x16de90) { CheckShapeBoth<FF<0x58>, RZFastFixed0x58>(ctx, {0xd9e99c, 0xd9e99c, 0x16de90, 0x16def0}); }
DIFF_TEST(RZFastFixedAllocators_Shape0x5c, 0x14a2d0) { CheckShapeBoth<FF<0x5c>, RZFastFixed0x5c>(ctx, {0xd9db38, 0xd9db38, 0, 0}); }
DIFF_TEST(RZFastFixedAllocators_Shape0x74, 0x2051d0) { CheckShapeBoth<FF<0x74>, RZFastFixed0x74>(ctx, {0xda75ec, 0xda75ec, 0x2051d0, 0x2052b0}); }
DIFF_TEST(RZFastFixedAllocators_Shape0x98, 0xaa4930) { CheckShapeBoth<FF<0x98>, RZFastFixed0x98>(ctx, {0xe7588c, 0xe7588c, 0xaa4930, 0xaa4c80}); }
DIFF_TEST(RZFastFixedAllocators_Shape0x9c, 0xa368d0) { CheckShapeBoth<FF<0x9c>, RZFastFixed0x9c>(ctx, {0xe687ac, 0xe687ac, 0xa368d0, 0xa36b50}); }
DIFF_TEST(RZFastFixedAllocators_Shape0x19c, 0x15aa70) { CheckShapeBoth<FF<0x19c>, RZFastFixed0x19c>(ctx, {0xd9dd48, 0xd9dd48, 0, 0}); }
DIFF_TEST(RZFastFixedAllocators_Shape0x1b0, 0x22ee16) { CheckShapeBoth<FF<0x1b0>, RZFastFixed0x1b0>(ctx, {0xdab48c, 0xdab48c, 0x22ee16, 0x22f1ae}); }
// cRZFastAllocator<T>: the base's constructor, then the class's vtable (as the image's XC entries build them).
DIFF_TEST(RZFastFixedAllocators_ShapeAudioEvent, 0x20feb9) { CheckShapeBoth<FA<pooled::cTSAudioEvent>, RZFastFixed0x34>(ctx, {0xda815c, 0xda8148, 0x20ff00, 0x20ffea}); }
DIFF_TEST(RZFastFixedAllocators_ShapeAudioSoundInstance, 0x20febe) { CheckShapeBoth<FA<pooled::cTSAudioSoundInstance>, RZFastFixed0x1b0>(ctx, {0xdab4a0, 0xdab48c, 0x22ee16, 0x22f1ae}); }
DIFF_TEST(RZFastFixedAllocators_ShapeBehaviorNode, 0xb2867e) { CheckShapeBoth<FA<pooled::cTSBehaviorNode>, RZFastFixed0x10>(ctx, {0xdc8fa0, 0xd9f520, 0, 0}); }
DIFF_TEST(RZFastFixedAllocators_ShapeWantInstance, 0x22edd1) { CheckShapeBoth<FA<pooled::cTSWantInstance>, RZFastFixed0x3c>(ctx, {0xdd4000, 0xdd3fec, 0, 0}); }
DIFF_TEST(RZFastFixedAllocators_ShapeTreeStackElem, 0x14a380) { CheckShapeBoth<FA<pooled::cTSTreeStackElem>, RZFastFixed0x3c>(ctx, {0xdd7674, 0xdd3fec, 0x54e250, 0x54e520}); }
DIFF_TEST(RZFastFixedAllocators_ShapeSndEventStd, 0x20fef0) { CheckShapeBoth<FA<pooled::cGZSndEventStd>, RZFastFixed0x38>(ctx, {0xe813d8, 0xe813c4, 0xb286b0, 0xb2875e}); }
DIFF_TEST(RZFastFixedAllocators_ShapeDataTableContainer, 0xa35050) { CheckShapeBoth<FA<pooled::cGenericDataTableContainer>, RZFastFixed0x20>(ctx, {0xe88294, 0xd9c2d4, 0x14ba20, 0x14be30}); }
DIFF_TEST(RZFastFixedAllocators_ShapeXPrimParam, 0x160950) { CheckShapeBoth<FA<pooled::XPrimParam>, RZFastFixed0x18>(ctx, {0xe91974, 0xd9d578, 0x160840, 0x160950}); }
