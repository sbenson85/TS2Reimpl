#include "rz/cRZCriticalSection.h"

#include "game/game.h"

#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <new>

namespace {

CRITICAL_SECTION *Cs(cRZCriticalSection::Block *block)
{
    return reinterpret_cast<CRITICAL_SECTION *>(block);
}

} // namespace

cRZCriticalSection *cRZCriticalSection::Construct()
{
    mBlock = nullptr;
    mVTable = VTable();
    auto *block = static_cast<Block *>(std::malloc(sizeof(Block)));
    if (block) std::memset(block, 0, sizeof(Block));
    mBlock = block;
    InitializeCriticalSection(Cs(block));
    return this;
}

void cRZCriticalSection::Destruct()
{
    Block *block = mBlock;
    mVTable = VTable();
    DeleteCriticalSection(Cs(block));
    if (block) std::free(block);
}

cRZCriticalSection *cRZCriticalSection::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(*this));
    return this;
}

int32_t cRZCriticalSection::Release()
{
    if (this) {
        // Through the vtable, slot 6 (deleting destructor), like the original.
        using DeletingDtor = void *(__thiscall *)(cRZCriticalSection *, uint32_t);
        (*reinterpret_cast<DeletingDtor *const *>(this))[6](this, 1);
    }
    return 0;
}

int32_t cRZCriticalSection::Lock()
{
    Block *block = mBlock;
    EnterCriticalSection(Cs(block));
    return ++block->lockCount;
}

int32_t cRZCriticalSection::Unlock()
{
    Block *block = mBlock;
    int32_t count = --block->lockCount;
    LeaveCriticalSection(Cs(block));
    return count;
}

int32_t cRZCriticalSection::TryLock()
{
    using TryEnter = BOOL(WINAPI *)(CRITICAL_SECTION *);
    TryEnter tryEnter = reinterpret_cast<TryEnter>(ts2_rva_10caea0);
    Block *block = mBlock;
    if (tryEnter) {
        if (!tryEnter(Cs(block))) return 0;
    } else {
        EnterCriticalSection(Cs(block));
    }
    return ++block->lockCount;
}

int32_t cRZCriticalSection::IsLocked()
{
    return mBlock->lockCount;
}

// Our vtable's slots (rzface::cRZCriticalSection, the header): each runs the member, inlined.
namespace rzface {

namespace {
::cRZCriticalSection *Self(cRZCriticalSection *face) { return reinterpret_cast<::cRZCriticalSection *>(face); }
} // namespace

int32_t cRZCriticalSection::Release() { [[msvc::forceinline_calls]] return Self(this)->Release(); }
int32_t cRZCriticalSection::Lock() { [[msvc::forceinline_calls]] return Self(this)->Lock(); }
int32_t cRZCriticalSection::Unlock() { [[msvc::forceinline_calls]] return Self(this)->Unlock(); }
int32_t cRZCriticalSection::TryLock() { [[msvc::forceinline_calls]] return Self(this)->TryLock(); }
int32_t cRZCriticalSection::IsValid() { return Self(this)->IsValid(); }
int32_t cRZCriticalSection::IsLocked() { [[msvc::forceinline_calls]] return Self(this)->IsLocked(); }
void *cRZCriticalSection::DeletingDestructor(uint32_t flags)
{
    [[msvc::forceinline_calls]] return Self(this)->DeletingDestructor(flags);
}

} // namespace rzface
