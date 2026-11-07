// cRZRefCount's own vtable (lane RW): what an image-free program's reference-counted objects store in place of the
// image's 0xda2fa8 (standalone/owned_statics.cpp aliases ts2_rva_da2fa8 to it). The image's 5 slots in order
// (db/classes.jsonl): slots 0 and 1 are the functions the image's entries are hooked to (src/hook/tables/cRZRefCount.cpp),
// slots 2-4 the members doing what the image's folded bodies do (tests/rz_refcount_table.cpp). RVAs are 1.18.0.187.
#include "rz/cRZRefCount.h"

#include <cstring>

uint32_t cRZRefCount::AddRef() { return ++mRefCount; }

uint32_t cRZRefCount::RemoveRef()
{
    if (mRefCount != 0) --mRefCount;
    return mRefCount;
}

uint32_t cRZRefCount::RefCount() const { return mRefCount; }

// Filled by a dynamic initialiser (a member function's address is no constant expression) before any code of the
// program runs, as the image's table is there before its code.
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

extern "C" const void *ts2_own_vt_da2fa8[5] = {
    Member(&cRZRefCount::DeletingDestructor), // 0 0x1b90b2 (hooked)
    Member(&cRZRefCount::Release),            // 1 0x1b94af (hooked)
    Member(&cRZRefCount::AddRef),             // 2 0x148fb0 (folded)
    Member(&cRZRefCount::RemoveRef),          // 3 0x1b9538 (folded)
    Member(&cRZRefCount::RefCount),           // 4 0x147a30 (folded)
};
