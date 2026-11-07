#include "rz/cRZRefCount.h"

#include "game/game.h"

#include <windows.h>

#include <new>

namespace {

// Calls the object's deleting destructor at `slot` with the "delete" flag, like the originals.
void DeleteThroughSlot(void *object, int slot)
{
    using DeletingDtor = void *(__thiscall *)(void *, uint32_t);
    (*reinterpret_cast<DeletingDtor *const *>(object))[slot](object, 1);
}

template <typename T> T *DeletingDestructorOf(T *self, uint32_t flags)
{
    self->Destruct();
    if (flags & 1) ::operator delete(self, sizeof(T));
    return self;
}

} // namespace

int32_t __cdecl RZAtomicAdd(volatile int32_t *value, int32_t delta)
{
    return InterlockedExchangeAdd(reinterpret_cast<volatile LONG *>(value), delta) + delta;
}

uint32_t cRZUnknown::Release()
{
    if (mRefCount > 1) return --mRefCount;
    DeleteThroughSlot(this, 3);
    return 0;
}

void cRZUnknown::Destruct()
{
    mVTable = GameAddress(kVTable);
}

cRZUnknown *cRZUnknown::DeletingDestructor(uint32_t flags)
{
    return DeletingDestructorOf(this, flags);
}

uint32_t cRZRefCount::Release()
{
    if (mRefCount > 1) return --mRefCount;
    DeleteThroughSlot(this, 0);
    return 0;
}

void cRZRefCount::Destruct()
{
    mVTable = &ts2_rva_da2fa8;
}

cRZRefCount *cRZRefCount::DeletingDestructor(uint32_t flags)
{
    return DeletingDestructorOf(this, flags);
}

uint32_t cRZUnknownMT::AddRef()
{
    return static_cast<uint32_t>(RZAtomicAdd(&mRefCount, 1));
}

uint32_t cRZUnknownMT::Release()
{
    // The count is read without synchronisation to choose between decrementing and deleting.
    if (static_cast<uint32_t>(mRefCount) > 1) return static_cast<uint32_t>(RZAtomicAdd(&mRefCount, -1));
    if (this) DeleteThroughSlot(this, 3);
    return 0;
}

uint32_t cRZUnknownMT::RemoveRef()
{
    if (static_cast<uint32_t>(mRefCount) > 0) RZAtomicAdd(&mRefCount, -1);
    return static_cast<uint32_t>(mRefCount);
}

void cRZUnknownMT::Destruct()
{
    mVTable = GameAddress(kVTable);
}

cRZUnknownMT *cRZUnknownMT::DeletingDestructor(uint32_t flags)
{
    return DeletingDestructorOf(this, flags);
}

uint32_t cRZRefCountMT::AddRef()
{
    return static_cast<uint32_t>(RZAtomicAdd(&mRefCount, 1));
}

uint32_t cRZRefCountMT::Release()
{
    int32_t count = RZAtomicAdd(&mRefCount, -1);
    if (count != 0) return static_cast<uint32_t>(count);
    if (this) DeleteThroughSlot(this, 0);
    return 0;
}

void cRZRefCountMT::Destruct()
{
    mVTable = GameAddress(kVTable);
}

cRZRefCountMT *cRZRefCountMT::DeletingDestructor(uint32_t flags)
{
    return DeletingDestructorOf(this, flags);
}
