// cRZLock (lane FD): src/rz/cRZLock.h. The deleting destructor, size-built as the image's (the constructor is in
// cRZLock.cpp, speed-built as the image's); and our table.
#include "rz/cRZLock.h"

#include <cstring>
#include <new>

cRZLock *cRZLock::DeletingDestructor(uint32_t flags)
{
    // The destructor's work written out: /O1 calls an explicit `this->~cRZLock()` out of line.
    mVTable = &ts2_rva_d9b644;
    VirtualCall<int32_t(__thiscall *)(void *), 2>(mLockable); // unlock
    if (flags & 1) ::operator delete(this, sizeof(cRZLock));
    return this;
}

// The image-free program's table (standalone/owned_statics.cpp aliases ts2_rva_d9b644 to it), filled by a dynamic
// initialiser before any code of the program runs.
#pragma warning(disable : 4073) // initializers put in library initialization area: intended
#pragma init_seg(lib)

namespace {
template <typename T> const void *Member(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance members only");
    const void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}
} // namespace

extern "C" const void *ts2_own_vt_d9b644[1] = {
    Member(&cRZLock::DeletingDestructor), // 0 0x142248
};
