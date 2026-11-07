// cRZStd's own vtable (lane FA): what an image-free program's cRZStd objects store in place of the image's 0xda3e6c
// (standalone/owned_statics.cpp aliases ts2_rva_da3e6c to it). The image's three slots in order, each the function the
// image's entry is hooked to (src/hook/tables/cRZStd.cpp): slot 0 Printf (0x1bfca9, cdecl, this first), slot 1 Assert
// (0x1bf9c4, thiscall), slot 2 Report (0x1bfb92, cdecl, this first). RVAs are 1.18.0.187.
#include "rz/cRZStd.h"

#include <cstring>

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

extern "C" const void *ts2_own_vt_da3e6c[3] = {
    reinterpret_cast<const void *>(&cRZStd::Printf), // 0 0x1bfca9
    Member(&cRZStd::Assert),                         // 1 0x1bf9c4
    reinterpret_cast<const void *>(&cRZStd::Report), // 2 0x1bfb92
};
