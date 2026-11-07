// cGZDefaultAllocator in the standalone shape (src/gz/cGZDefaultAllocator.h). Built for size (/O1), as the game's
// memory module.
#include "gz/cGZDefaultAllocator.h"

#include "rz/RZMemory.h"

#include <new>

// 0x1be4fa: its own interface id or cIGZUnknown's (1) give this, with a reference (AddRef through the vtable).
bool cGZDefaultAllocator::QueryInterface(uint32_t riid, void **ppvObj)
{
    if (riid == kIid) {
        *ppvObj = this;
        AddRef();
        return true;
    }
    if (riid == 1) {
        *ppvObj = this;
        AddRef();
        return true;
    }
    return false;
}

// 0x148fb0 (folded with about 600 classes): the new count, not interlocked.
uint32_t cGZDefaultAllocator::AddRef()
{
    return ++mRefCount;
}

// 0x1be540 (folded with three other classes' Release): a count of 0 or 1 deletes the object (the count is left as
// it was) and gives 0; a higher one is decremented.
uint32_t cGZDefaultAllocator::Release()
{
    if (mRefCount > 1) return --mRefCount;
    delete this;
    return 0;
}

// 0x1be4c7 (folded): RZMemory::Malloc.
void *cGZDefaultAllocator::Allocate(uint32_t bytes)
{
    return RZMemory::Malloc(bytes);
}

// 0x1be4d4: frees only a non-null block.
void cGZDefaultAllocator::Deallocate(void *mem)
{
    if (mem) RZMemory::Free(mem);
}

// 0x1be4e8: RZMemory::Realloc.
void *cGZDefaultAllocator::Reallocate(void *mem, uint32_t bytes)
{
    return RZMemory::Realloc(mem, bytes);
}

// 0x1be557 (the deleting destructor): the body only stores the vtable.
cGZDefaultAllocator::~cGZDefaultAllocator() {}

bool __fastcall cGZDefaultAllocatorAdapters::QueryInterface(cGZDefaultAllocator *self, void *, uint32_t riid, void **ppvObj)
{
    [[msvc::forceinline_calls]] return self->cGZDefaultAllocator::QueryInterface(riid, ppvObj);
}

cGZDefaultAllocator *__fastcall cGZDefaultAllocatorAdapters::DeletingDestructor(cGZDefaultAllocator *self, void *, uint32_t flags)
{
    self->cGZDefaultAllocator::~cGZDefaultAllocator();
    if (flags & 1) ::operator delete(self, sizeof(cGZDefaultAllocator));
    return self;
}
