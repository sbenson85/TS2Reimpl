#pragma once
#include "rz/RZMemory.h"

#include <cstddef>
#include <cstdint>

// The two allocators of the game's STL containers. Both end on the same heap: the allocator service's slots 3 / 4
// (cGZAllocatorService, src/rz/cGZAllocatorService.h) forward to RZMemory::Malloc / Free, the global GeneralAllocator.
// They are not the same behaviour, so they stay two:
//
// - GameAllocator: RZMemory::Malloc / Free called directly. For containers whose original code calls the game's memory
//   functions (RZMemory::Malloc 0x1be3b0 / Free 0x1be3a1), with no service in between.
// - GZServiceAllocator: through the allocator service the game caches in a function-local static (0x10c95d4, guard
//   0x10c95d8): slot 3 allocates, slot 4 frees (AllocateViaAllocatorService 0x1bf795, FreeViaAllocatorService 0x1bf84e,
//   src/app/AppServices2.h). The first use fetches the service (GetAllocatorService) and every call is a virtual call
//   on it, so a container whose original allocates through the service (0x197100, 0x1bf795, 0x194dc0 and their kin)
//   uses this one: what reaches the service, and when the cache is filled, stay the original's. The 0x197100 family
//   (FreeThroughAllocatorService and simantics' AllocateThroughAllocatorService) differs from 0x1bf795 / 0x1bf84e only
//   when the first fetch throws (it puts the guard back); containers do not depend on that.
//
// Either way, a container that original code fills, grows or frees must use one of these, never std::allocator:
// operator new's blocks are on the process heap, and the general allocator crashes freeing them (docs/lane/write.md,
// "Heap"). Both are stateless: the library code instantiated over them is the same bytes whichever one it is.
template <typename T> struct GameAllocator {
    using value_type = T;
    GameAllocator() noexcept = default;
    template <typename U> GameAllocator(const GameAllocator<U> &) noexcept {}
    T *allocate(size_t count) { return static_cast<T *>(RZMemory::Malloc(static_cast<uint32_t>(count * sizeof(T)))); }
    void deallocate(T *block, size_t) noexcept { RZMemory::Free(block); }
    template <typename U> bool operator==(const GameAllocator<U> &) const noexcept { return true; }
    template <typename U> bool operator!=(const GameAllocator<U> &) const noexcept { return false; }
};

namespace as2 {
void *__stdcall AllocateViaAllocatorService(uint32_t size, uint32_t unused); // 0x1bf795 (src/app/AppServices2.h)
void __stdcall FreeViaAllocatorService(void *mem, uint32_t unused);         // 0x1bf84e
} // namespace as2

template <typename T> struct GZServiceAllocator {
    using value_type = T;
    GZServiceAllocator() noexcept = default;
    template <typename U> GZServiceAllocator(const GZServiceAllocator<U> &) noexcept {}
    T *allocate(size_t count) { return static_cast<T *>(as2::AllocateViaAllocatorService(static_cast<uint32_t>(count * sizeof(T)), 0)); }
    void deallocate(T *block, size_t) noexcept { as2::FreeViaAllocatorService(block, 0); }
    template <typename U> bool operator==(const GZServiceAllocator<U> &) const noexcept { return true; }
    template <typename U> bool operator!=(const GZServiceAllocator<U> &) const noexcept { return false; }
};
