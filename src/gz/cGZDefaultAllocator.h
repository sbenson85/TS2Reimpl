#pragma once
#include "gz/cIGZAllocator.h"

#include <cstdint>

// cGZDefaultAllocator (1.18.0.187), in the standalone shape (docs/standalone-shape.md): the cIGZAllocator over the
// game's global memory functions (RZMemory, src/rz/RZMemory.h). 8 bytes: the vtable (0xda3c98, 7 slots), the
// reference count.
//
// The game has no constructor function: the object is built inline (vtable, zero count) by the two container
// constructors 0x85a19a and 0x85bf91 (`new` of 8 bytes; not ours) and constant-initialised in .data at 0xff2464 by the
// XC static initialiser 0xd90726 (slot 101; owned by the static-initialiser lanes). Those objects carry the game's
// vtable: the slot adapters below serve them.
//
//   0 QueryInterface 0x1be4fa      1 AddRef 0x148fb0 (folded, verified elsewhere)
//   2 Release 0x1be540 (folded)    3 Allocate 0x1be4c7 (folded)   4 Deallocate 0x1be4d4
//   5 Reallocate 0x1be4e8 (listed as library code)                6 deleting destructor 0x1be557
class cGZDefaultAllocator : public cIGZAllocator {
public:
    static constexpr uint32_t kVTable = 0xda3c98;

    cGZDefaultAllocator() : mRefCount(0) {}

    bool QueryInterface(uint32_t riid, void **ppvObj) override;
    uint32_t AddRef() override;
    uint32_t Release() override;
    void *Allocate(uint32_t bytes) override;
    void Deallocate(void *mem) override;
    void *Reallocate(void *mem, uint32_t bytes) override;
    virtual ~cGZDefaultAllocator(); // slot 6

    uint32_t mRefCount; // +4
};

static_assert(sizeof(cGZDefaultAllocator) == 8, "cGZDefaultAllocator layout");

// The RVA hooks' entry points (one-statement forwards, docs/standalone-shape.md step 6) for objects that carry the
// game's vtable.
struct cGZDefaultAllocatorAdapters {
    static bool __fastcall QueryInterface(cGZDefaultAllocator *self, void *, uint32_t riid, void **ppvObj);
    static cGZDefaultAllocator *__fastcall DeletingDestructor(cGZDefaultAllocator *self, void *, uint32_t flags);
};
