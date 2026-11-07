#pragma once
// The fixed-size pool statics (lane HE, 50 XC entries; docs/subsystems/startup.md "Done in lane HE"): one
// cRZFastFixedAllocator2<size> or cRZFastAllocator<T> object per entry (src/rz/RZFastFixedAllocators.h), the pools
// behind the game's class operator news. The image's entry builds it (the out-of-line constructor, or the same code
// inlined, the vtable then constant-initialised in .data; a cRZFastAllocator<T> stores its own vtable after the
// base's constructor): the first page from the block pool, the current ring of one, no full ring. Its thunk is the
// base class's destructor (both rings back to the block pool through slot 3), out of line or inlined. Ours constructs
// our class in place (our vtable) and registers our thunk, which runs our destructor, through BoundAtExit() in the
// same slot. Standalone, each becomes a static of its class next to the pooled class's operator new.
#include "app/WinMainStart.h"

#include <cstddef>
#include <cstdint>

namespace staticinit {

struct AllocatorStatic {
    uint32_t slot;
    uint32_t imageRva;  // the image's initialiser
    uint32_t storage;   // the object (RVA, 12 bytes)
    uint32_t thunk;     // the image's destructor thunk (RVA): what its entry registers
    uint32_t blockSize; // the pool's block size (the base class)
    uint32_t vtable;    // the game's vtable of the class (RVA): the base's, or a cRZFastAllocator<T>'s own
    void(__cdecl *build)(void *);   // our constructor at the storage
    void(__cdecl *destroy)(void *); // our destructor
};

void __cdecl InitAllocator(const void *allocatorStatic);
const AllocatorStatic *AllocatorStatics(size_t *count);
const winmainstart::OwnedInitialiser *OwnedAllocators(size_t *count);

} // namespace staticinit
