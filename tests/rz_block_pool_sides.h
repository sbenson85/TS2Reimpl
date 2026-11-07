#pragma once
// Real block pools (RZBlockPoolAlloc / Free, 0x1be354 / 0x1be3d6) on each side of a test, for the fixed-size pool
// allocators (tests/rz_fast_fixed_allocators.cpp, tests/static_init_allocators.cpp). As in RZBlockPool_Globals: each
// side's VirtualAlloc goes to its own 256 MB arena (the image's through its IAT slots, ours through
// GeneralAllocatorOs), so pages compare as arena offsets; the pool lock (a cRZCriticalSection the XC initialisers
// would have built) is constructed for the test; the image's side creates its pools with the original
// RZBlockPoolsCreate 0x1be449, ours with ours and our region slots.
#include "difftest.h"
#include "hook/hooks.h"
#include "rz/GeneralAllocator.h"
#include "rz/RZBlockPool.h"
#include "rz/cRZCriticalSection.h"

#include <windows.h>

#include <cstring>
#include <vector>

namespace blockpools {

struct Arena {
    uint8_t *base = nullptr;
    size_t next = 0;
};
constexpr size_t kArenaSize = 256u << 20;
inline Arena gArena[2];

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

inline void ResetArenas()
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
inline uint32_t Offset(int side, uint32_t value)
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

// Our pools' vtables: copies of the game's with the region slots pointing at our implementations.
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

// One test's pools: the lock and the pool globals saved and restored; Begin(side) gives a side fresh pools (call
// ResetArenas and hold an OsRedirect around the run first), End() puts the globals back.
struct Sides {
    difftest::Context &ctx;
    ArenaLease lease;
    void **largePool, **smallPool;
    void *savedLarge, *savedSmall;
    cRZCriticalSection *lock;
    alignas(4) uint8_t savedLock[sizeof(cRZCriticalSection)];
    OurVTables vtables;
    explicit Sides(difftest::Context &c)
        : ctx(c), largePool(c.image.At<void *>(0x10cb088)), smallPool(c.image.At<void *>(0x10cb08c)), savedLarge(*largePool),
          savedSmall(*smallPool), lock(c.image.At<cRZCriticalSection>(0x10cb090)), vtables(c)
    {
        std::memcpy(savedLock, lock, sizeof(savedLock));
        lock->Construct();
    }
    ~Sides()
    {
        End();
        std::memcpy(lock, savedLock, sizeof(savedLock));
    }
    void Begin(int side)
    {
        *largePool = nullptr;
        *smallPool = nullptr;
        if (side) {
            RZBlockPoolsCreate();
            static_cast<RZBlockPool *>(*largePool)->mVTable = vtables.largeVt;
            static_cast<RZBlockPool *>(*smallPool)->mVTable = vtables.smallVt;
        } else {
            ctx.Original<void(__cdecl *)()>(0x1be449)();
        }
    }
    void End()
    {
        *largePool = savedLarge;
        *smallPool = savedSmall;
    }
    // The next n pages of 2^log2 bytes the side's pools hand out, as arena offsets.
    std::vector<uint32_t> Next(int side, uint32_t log2, int n)
    {
        std::vector<uint32_t> out;
        for (int k = 0; k < n; ++k) {
            uint32_t page = side ? RZBlockPoolAlloc(log2) : ctx.Original<uint32_t(__cdecl *)(uint32_t)>(0x1be354)(log2);
            out.push_back(Offset(side, page));
        }
        return out;
    }
};

} // namespace blockpools
