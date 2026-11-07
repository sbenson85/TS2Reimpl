#pragma once
#include "game/owned.h"
#include "rz/cRZCriticalSection.h"

#include <windows.h>

#include <cstdint>
#include <map>

// The game's power-of-two block pools (1.18.0.187; RTTI cRZSlabAllocatorLarge/cRZSlabAllocatorSmall),
// used for blocks of 512 bytes to 512 KB:
//   - the large pool (global at RVA 0x10cb088) reserves 512 KB regions with VirtualAlloc and hands
//     out 16 KB-granular blocks;
//   - the small pool (global at RVA 0x10cb08c) splits 16 KB blocks taken from the large pool into
//     512-byte-granular blocks.
// A region's 32 units are tracked by a bitmap. Regions with free units are in `partial`, full ones in
// `full`: std::map<region address, bitmap> with a process-heap allocator (MSVC 14.40's tree, the same
// library code as the game's). A block of 2^n bytes takes 2^(n - granularity) consecutive units,
// aligned to its size; the search starts after the last block handed out and rotates through each
// region's bitmap.

template <class T> struct ProcessHeapAllocator {
    using value_type = T;
    ProcessHeapAllocator() = default;
    template <class U> ProcessHeapAllocator(const ProcessHeapAllocator<U> &) {}
    T *allocate(size_t n) { return static_cast<T *>(HeapAlloc(GetProcessHeap(), 0, n * sizeof(T))); } // 0x1be421
    void deallocate(T *p, size_t) { HeapFree(GetProcessHeap(), 0, p); }                                  // 0x1be435
    template <class U> bool operator==(const ProcessHeapAllocator<U> &) const { return true; }
    template <class U> bool operator!=(const ProcessHeapAllocator<U> &) const { return false; }
};

using RZRegionMap = std::map<uint32_t, uint32_t, std::less<uint32_t>, ProcessHeapAllocator<std::pair<const uint32_t, uint32_t>>>;
static_assert(sizeof(RZRegionMap) == 8, "region map layout: head, size");

class RZBlockPool;

// The four tables (owned.h, OwnedVTable; 2 slots each): the game's in hooked builds, ours in an image-free program
// (ts2_own_vt_<rva> in RZBlockPool.size.cpp, aliased in standalone/owned_statics.cpp). The bases' slots are _purecall.
TS2_OWNED_STATIC(const OwnedVTable, da3c68); // cRZSlabAllocatorLarge's base (abstract)
TS2_OWNED_STATIC(const OwnedVTable, da3c74); // cRZSlabAllocatorLarge: LargeNewRegion, LargeReleaseRegion
TS2_OWNED_STATIC(const OwnedVTable, da3c80); // cRZSlabAllocatorSmall's base (abstract)
TS2_OWNED_STATIC(const OwnedVTable, da3c8c); // cRZSlabAllocatorSmall: SmallNewRegion, SmallReleaseRegion
// The pools (created on first use by RZBlockPoolsCreate; null until then) and the lock the global entry points take (a
// cRZCriticalSection built by an XC static initialiser in game, by standalone/owned_statics.cpp in an image-free one).
TS2_OWNED_STATIC(RZBlockPool *, 10cb088);   // the large pool
TS2_OWNED_STATIC(RZBlockPool *, 10cb08c);   // the small pool
TS2_OWNED_STATIC(cRZCriticalSection, 10cb090);

class RZBlockPool {
public:
    // The tables' RVAs, for the tests; our code stores the owned statics above.
    static constexpr uint32_t kBaseVTable = 0xda3c68;      // large pool's base (abstract)
    static constexpr uint32_t kLargeVTable = 0xda3c74;     // large pool
    static constexpr uint32_t kSmallBaseVTable = 0xda3c80; // small pool's base (abstract)
    static constexpr uint32_t kSmallVTable = 0xda3c8c;     // small pool

    RZBlockPool *ConstructBase();                    // 0x1bdd0e (vtable 0xda3c68)
    RZBlockPool *ConstructSmallBase();               // 0x1bdce5 (vtable 0xda3c80)
    RZBlockPool *ConstructSmall(RZBlockPool *parent); // 0x1bdd4e

    uint32_t AllocSmall(uint32_t log2Size);              // 0x1bdd69: granularity 512 B, regions of 16 KB
    uint32_t AllocLarge(uint32_t log2Size);              // 0x1bdeb8: granularity 16 KB, regions of 512 KB
    void FreeSmall(uint32_t address, uint32_t log2Size); // 0x1be050
    void FreeLarge(uint32_t address, uint32_t log2Size); // 0x1be133

    // Virtual slots 0 and 1 (region source): the large pool's take regions from the OS, the small
    // pool's from its parent.
    uint32_t LargeNewRegion(uint32_t *unused);              // 0x1be007
    void LargeReleaseRegion(uint32_t *bitmap, uint32_t region); // 0x1be216
    uint32_t SmallNewRegion(uint32_t *unused);              // 0x1be01e
    void SmallReleaseRegion(uint32_t *bitmap, uint32_t region); // 0x1be22a

    // Clear a block's units in a bitmap (`this` unused): 0x1be26e (512 B units), 0x1be295 (16 KB).
    void ClearSmallBits(uint32_t *bitmap, uint32_t offset, uint32_t log2Size);
    void ClearLargeBits(uint32_t *bitmap, uint32_t offset, uint32_t log2Size);

    RZRegionMap &Partial() { return *reinterpret_cast<RZRegionMap *>(&mPartial); }
    RZRegionMap &Full() { return *reinterpret_cast<RZRegionMap *>(&mFull); }

    const void *mVTable;     // +0x00
    uint32_t mPartial[2];    // +0x04 RZRegionMap: regions with free units
    uint32_t mFull[2];       // +0x0c RZRegionMap: full regions
    uint32_t mNextUnit;      // +0x14 where the next search starts (unit index, any value; masked)
    void *mCurrent;          // +0x18 node of `partial` the next search starts at
    RZBlockPool *mParent;    // +0x1c small pool only

private:
    template <uint32_t Granularity> uint32_t Alloc(uint32_t log2Size);
    template <uint32_t Granularity> void Free(uint32_t address, uint32_t log2Size);
};

// The global entry points (__cdecl): 0x1be449 creates both pools; 0x1be354 allocates a block of
// 2^log2Size bytes (small pool below 16 KB), 0x1be3d6 frees one; both under the critical section at
// RVA 0x10cb090.
void __cdecl RZBlockPoolsCreate();
uint32_t __cdecl RZBlockPoolAlloc(uint32_t log2Size);
void __cdecl RZBlockPoolFree(uint32_t address, uint32_t log2Size);
