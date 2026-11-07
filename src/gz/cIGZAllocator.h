#pragma once
#include "gz/cIGZUnknown.h"

#include <cstdint>

// cIGZAllocator (interface id 0x3ae4beaa): a memory allocator object. Slot order is ABI (cGZDefaultAllocator's
// vtable 0xda3c98). Implementers append their deleting destructor as slot 6.
class cIGZAllocator : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x3ae4beaa;

    virtual void *Allocate(uint32_t bytes) = 0;              // slot 3 (ret 4)
    virtual void Deallocate(void *mem) = 0;                  // slot 4 (ret 4)
    virtual void *Reallocate(void *mem, uint32_t bytes) = 0; // slot 5 (ret 8)
};
