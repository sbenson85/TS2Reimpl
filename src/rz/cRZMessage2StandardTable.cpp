// cRZMessage2Standard's own vtable (lane RU): what an image-free program's messages store in place of the image's
// 0xda4064 (standalone/owned_statics.cpp aliases ts2_rva_da4064 to it). The image's 38 slots in order: an entry whose
// image body is hooked is the function the hook installs (src/hook/tables/cRZMessage2Standard.cpp), a folded getter
// (shared with other classes, never hooked) the member doing its work (tests/rz_stream_message_tables.cpp). RVAs
// are 1.18.0.187.
#include "rz/cRZMessage2Standard.h"

#include <cstring>

uint32_t cRZMessage2Standard::AddRef() { return ++mRefCount; }
uint32_t cRZMessage2Standard::GetId() const { return mId; }
uint32_t cRZMessage2Standard::GetValue0() const { return mValue[0]; }
uint32_t cRZMessage2Standard::GetValue1() const { return mValue[1]; }
uint32_t cRZMessage2Standard::GetValue2() const { return mValue[2]; }
uint32_t cRZMessage2Standard::GetValue3() const { return mValue[3]; }
void *cRZMessage2Standard::GetObjectField() const { return mPtr24; }

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

using M = cRZMessage2Standard;

} // namespace

extern "C" const void *ts2_own_vt_da4064[38] = {
    Member(&M::QueryInterface),     //  0 0x1c0660 (hooked)
    Member(&M::AddRef),             //  1 0x148fb0 (folded)
    Member(&M::Release),            //  2 0x1c0804 (hooked)
    Member(&M::Create),             //  3 0x1c04ea (hooked)
    Member(&M::GetId),              //  4 0x145770 (folded)
    Member(&M::SetId),              //  5 0x1c0915 (hooked)
    Member(&M::IdEquals),           //  6 0x1c03c8 (hooked)
    Member(&M::IdLess),             //  7 0x1c03de (hooked)
    Member(&M::HasValue0),          //  8 0x1c05a0 (hooked)
    Member(&M::GetValue0),          //  9 0x149cf0 (folded)
    Member(&M::GetValue0),          // 10 0x149cf0 (folded)
    Member(&M::SetValue0),          // 11 0x1c0874 (hooked)
    Member(&M::SetValue0),          // 12 0x1c0874
    Member(&M::HasValue1),          // 13 0x1c05a8 (hooked)
    Member(&M::GetValue1),          // 14 0x149d00 (folded)
    Member(&M::GetValue1),          // 15 0x149d00 (folded)
    Member(&M::SetValue1),          // 16 0x1c0882 (hooked)
    Member(&M::SetValue1),          // 17 0x1c0882
    Member(&M::HasValue2),          // 18 0x1c05b1 (hooked)
    Member(&M::GetValue2),          // 19 0x148ba0 (folded)
    Member(&M::GetValue2),          // 20 0x148ba0 (folded)
    Member(&M::SetValue2),          // 21 0x1c0890 (hooked)
    Member(&M::SetValue2),          // 22 0x1c0890
    Member(&M::HasValue3),          // 23 0x1c05ba (hooked)
    Member(&M::GetValue3),          // 24 0x147d00 (folded)
    Member(&M::GetValue3),          // 25 0x147d00 (folded)
    Member(&M::SetValue3),          // 26 0x1c089e (hooked)
    Member(&M::SetValue3),          // 27 0x1c089e
    Member(&M::HasPtr20),           // 28 0x1c05e0 (hooked)
    Member(&M::GetString),          // 29 0x1c05f4 (hooked)
    Member(&M::SetString),          // 30 0x1c08f2 (hooked)
    Member(&M::HasPtr24),           // 31 0x1c05cc (hooked)
    Member(&M::GetObjectField),     // 32 0x15e970 (folded)
    Member(&M::SetObject),          // 33 0x1c08cf (hooked)
    Member(&M::HasExtra),           // 34 0x1c05c3 (hooked)
    Member(&M::GetExtra),           // 35 0x1c0582 (hooked)
    Member(&M::SetExtra),           // 36 0x1c08ac (hooked)
    Member(&M::DeletingDestructor), // 37 0x1c042a (hooked)
};
