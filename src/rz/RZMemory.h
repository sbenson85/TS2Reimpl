#pragma once
#include <cstdint>

class GeneralAllocator;

// The game's global memory functions (1.18.0.187): thin wrappers around the global GeneralAllocator
// instance at RVA 0x10cb098, and the memory service's thiscall slots that forward to them.
namespace RZMemory {

// The global allocator. Tests may point it at another instance (nullptr: the game's).
extern GeneralAllocator *testGlobalAllocator;
GeneralAllocator *GlobalAllocator();

void *__cdecl Malloc(uint32_t bytes);                 // 0x1be3b0: Malloc(bytes, 0)
void __cdecl Free(void *mem);                         // 0x1be3a1
void *__cdecl Realloc(void *mem, uint32_t bytes);     // 0x1be3c1: Realloc(mem, bytes, 0)

// Allocator objects' slots that forward to the functions above (thiscall; `this` unused):
// cGZDefaultAllocator and cGZAllocatorService. Not hooked: 0x1be4c7 is folded with unrelated classes,
// and 0x1be4e8 is listed as library code (a shape match under a wrong name); both are still tested.
struct Service {
    void *ServiceMalloc(uint32_t bytes);              // 0x1be4c7
    void ServiceFreeIfNotNull(void *mem);             // 0x1be4d4 cGZDefaultAllocator slot 4: frees only non-null
    void *ServiceRealloc(void *mem, uint32_t bytes);  // 0x1be4e8
    void ServiceFree(void *mem);                      // 0x1be654 cGZAllocatorService slot 4
};

} // namespace RZMemory
