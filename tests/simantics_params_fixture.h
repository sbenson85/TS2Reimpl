#pragma once
// Shared test fixtures for the SimAntics data-layer tests (tests/simantics_params*.cpp).
//
// Pool fixtures: the image's global pool allocators (the XPrimParam pool at 0x10fd4c4 with 0x18-byte blocks and
// the cTSBehaviorNode pool at 0x1012908 with 0x10-byte blocks) are pointed at a 16-page region of our own
// whose NewPage/FreePage slots are fakes. A pool is put into a random state by running random allocations and
// frees through the original, then Take() snapshots every active pool (region bytes, allocator words,
// fake-page counter, freed pages). Each side runs from the same snapshot (Restore()), so the returned blocks,
// every byte of the regions and the allocators compare exactly.
#include "difftest.h"
#include "game/game.h"
#include "rz/RZFixedAllocator.h"

#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace simfix {

constexpr int kPages = 16;
constexpr uint32_t kPageBytes = 0x800;

struct PoolBase;

inline std::vector<PoolBase *> &ActivePools()
{
    static std::vector<PoolBase *> v;
    return v;
}

struct PoolBase {
    difftest::Context &ctx;
    uint32_t rva;
    uint32_t blockBytes;
    uint32_t blocks;
    uint32_t freeRva;
    uint8_t *region = nullptr;
    int nextPage = 0;
    std::vector<uint32_t> freed;
    void *vt[4] = {};
    uint32_t saved[3] = {};
    std::vector<void *> held;

    uint32_t *Words() const { return static_cast<uint32_t *>(ctx.image.At(rva)); }
    void *Self() const { return ctx.image.At(rva); }
    RZFixedPage *PageAt(int i) const { return reinterpret_cast<RZFixedPage *>(region + i * kPageBytes); }

    void InitPage(RZFixedPage *p) const
    {
        p->next = p;
        p->prev = p;
        p->used = 0;
        uint8_t *base = reinterpret_cast<uint8_t *>(p);
        for (uint32_t k = 0; k < blocks; ++k) {
            void **block = reinterpret_cast<void **>(base + 0x10 + k * blockBytes);
            *block = k + 1 < blocks ? base + 0x10 + (k + 1) * blockBytes : nullptr;
        }
        p->freeList = base + 0x10;
    }

    static RZFixedPage *__fastcall FakeNewPage(void *self, void *)
    {
        PoolBase *pool = Find(self);
        RZFixedPage *p = pool->PageAt(pool->nextPage++);
        pool->InitPage(p);
        return p;
    }

    static void __fastcall FakeFreePage(void *self, void *, RZFixedPage *p)
    {
        PoolBase *pool = Find(self);
        pool->freed.push_back(static_cast<uint32_t>(reinterpret_cast<uint8_t *>(p) - pool->region));
    }

    static PoolBase *Find(void *self)
    {
        for (PoolBase *p : ActivePools())
            if (p->Self() == self) return p;
        return nullptr;
    }

    PoolBase(difftest::Context &c, uint32_t allocatorRva, uint32_t bytes, uint32_t nBlocks, uint32_t freeFunctionRva)
        : ctx(c), rva(allocatorRva), blockBytes(bytes), blocks(nBlocks), freeRva(freeFunctionRva)
    {
        std::memcpy(saved, Words(), sizeof(saved));
        region = static_cast<uint8_t *>(VirtualAlloc(nullptr, kPages * kPageBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        vt[2] = reinterpret_cast<void *>(&FakeNewPage);
        vt[3] = reinterpret_cast<void *>(&FakeFreePage);
        ActivePools().push_back(this);
    }

    ~PoolBase()
    {
        std::memcpy(Words(), saved, sizeof(saved));
        VirtualFree(region, 0, MEM_RELEASE);
        auto &v = ActivePools();
        for (size_t i = 0; i < v.size(); ++i)
            if (v[i] == this) {
                v.erase(v.begin() + static_cast<long>(i));
                break;
            }
    }

    PoolBase(const PoolBase &) = delete;
    PoolBase &operator=(const PoolBase &) = delete;

    // A random pool state: garbage in every byte, then `steps` allocations (with random frees of held blocks
    // unless allocOnly), then postFrees frees. Allocations and frees run through the original: the pool's
    // Allocate slot (0x14a2d0, shared) and the free function.
    void Randomize(uint32_t seed, int steps, bool allocOnly, int postFrees = 0)
    {
        std::mt19937 rng(seed);
        for (uint32_t i = 0; i < kPages * kPageBytes; ++i) region[i] = static_cast<uint8_t>(rng());
        nextPage = 1;
        freed.clear();
        held.clear();
        InitPage(PageAt(0));
        uint32_t *a = Words();
        a[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(vt));
        a[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(PageAt(0)));
        a[2] = 0;
        auto alloc = ctx.Original<void *(__thiscall *)(void *)>(0x14a2d0);
        auto free = ctx.Original<void(__thiscall *)(void *, void *)>(freeRva);
        for (int i = 0; i < steps; ++i) {
            if (allocOnly || held.empty() || rng() % 3 != 0) {
                held.push_back(alloc(a));
            } else {
                size_t k = rng() % held.size();
                free(a, held[k]);
                held.erase(held.begin() + static_cast<long>(k));
            }
        }
        for (int i = 0; i < postFrees && !held.empty(); ++i) {
            size_t k = rng() % held.size();
            free(a, held[k]);
            held.erase(held.begin() + static_cast<long>(k));
        }
    }
};

// The XPrimParam pool: 84 blocks of 0x18 per page, freed by 0xc0aacf.
struct PoolFixture : PoolBase {
    explicit PoolFixture(difftest::Context &c) : PoolBase(c, 0x10fd4c4, 0x18, 84, 0xc0aacf) {}
};

// The cTSBehaviorNode pool: 127 blocks of 0x10 per page, freed by cRZFastFixedAllocator2<0x10>::Free.
struct NodePoolFixture : PoolBase {
    explicit NodePoolFixture(difftest::Context &c) : PoolBase(c, 0x1012908, 0x10, 127, 0x177030) {}
};

struct PoolSnap {
    std::vector<uint8_t> region;
    uint32_t alloc[3] = {};
    int nextPage = 0;
    std::vector<uint32_t> freed;
    bool operator==(const PoolSnap &o) const
    {
        return region == o.region && std::memcmp(alloc, o.alloc, sizeof(alloc)) == 0 && nextPage == o.nextPage && freed == o.freed;
    }
};

using Snapshot = std::vector<PoolSnap>;

inline Snapshot Take()
{
    Snapshot s;
    for (PoolBase *p : ActivePools()) {
        PoolSnap ps;
        ps.region.assign(p->region, p->region + kPages * kPageBytes);
        std::memcpy(ps.alloc, p->Words(), sizeof(ps.alloc));
        ps.nextPage = p->nextPage;
        ps.freed = p->freed;
        s.push_back(std::move(ps));
    }
    return s;
}

inline void Restore(const Snapshot &s)
{
    for (size_t i = 0; i < s.size(); ++i) {
        PoolBase *p = ActivePools()[i];
        std::memcpy(p->region, s[i].region.data(), s[i].region.size());
        std::memcpy(p->Words(), s[i].alloc, sizeof(s[i].alloc));
        p->nextPage = s[i].nextPage;
        p->freed = s[i].freed;
    }
}

struct PoolCase {
    uint32_t seed;
    int steps;
    bool allocOnly;
    int postFrees;
};

// Random states plus the boundaries: pages exactly full, one short and one over (for the 0x18 pool: 84
// blocks; the 0x10 pool: 127), with and without frees afterwards. `perPage` is the pool's blocks per page.
inline std::vector<PoolCase> PoolCases(int random, int perPage = 84)
{
    std::vector<PoolCase> v;
    for (int pages = 0; pages < 4; ++pages)
        for (int delta : {-2, -1, 0, 1})
            if (pages * perPage + delta >= 0 && (pages > 0 || delta >= 0))
                v.push_back({static_cast<uint32_t>(pages * perPage + delta) * 7 + 1, pages * perPage + delta, true, 0});
    for (int pages = 2; pages < 4; ++pages)
        for (int delta : {0, 1})
            for (int frees : {1, 2, 3, 5})
                v.push_back({static_cast<uint32_t>(pages * perPage + delta) * 11 + static_cast<uint32_t>(frees), pages * perPage + delta, true, frees});
    std::mt19937 rng(0x5173);
    for (int i = 0; i < random; ++i) v.push_back({static_cast<uint32_t>(rng()), static_cast<int>(rng() % (3 * perPage + 8)), false, 0});
    return v;
}

} // namespace simfix
