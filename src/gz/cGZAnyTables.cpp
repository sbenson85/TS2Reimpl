// The cIGZAny family's own tables (lane VX): what the owned vtables (gz/cGZPropertySet.h, gzany::*VTable()) stand for
// in an image-free program, in the image's slot order (0 deleting destructor, 1 Assign, 2 Compare, 3 Clone, 4 GetType,
// 5 GetTypeName). Each slot is the function the hooked game runs for that image slot (src/hook/tables/cGZAny.cpp), so
// an object behaves the same whichever table it carries. The folded slots are shared as in the image: 0x14c5f0
// (AnyUint32, AnyBoolean), 0x1bedcb and 0x1bf068 (AnySint32, AnyFloat32, AnyAny).
#include "gz/cGZAny.h"

#include <cstring>

namespace {
template <typename T> const void *Member(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance members only");
    const void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

template <typename F> const void *Static(F *fn) { return reinterpret_cast<const void *>(fn); }
} // namespace

extern "C" int __cdecl _purecall();

using gz::AnyAdapters;

extern "C" const void *ts2_own_vt_d9c654[6] = {
    Member(&cGZAny::DeletingDestructor), // 0x14c840
    Static(&_purecall), Static(&_purecall), Static(&_purecall), Static(&_purecall), Static(&_purecall),
};
extern "C" const void *ts2_own_vt_d9f534[6] = {
    Member(&cGZAny::ValueDeletingDestructor),             // 0x14c5f0
    Static(&AnyAdapters<gz::AnyUint32>::Assign),          // 0x177b90
    Static(&AnyAdapters<gz::AnyUint32>::Compare),         // 0x177b70
    Static(&AnyAdapters<gz::AnyUint32>::Clone),           // 0x177ab0
    Static(&AnyAdapters<gz::AnyUint32>::GetType),         // 0x179d00
    Static(&AnyAdapters<gz::AnyUint32>::GetTypeName),     // 0x179d10
};
extern "C" const void *ts2_own_vt_d9c670[6] = {
    Member(&cGZAny::ValueDeletingDestructor),             // 0x14c5f0
    Static(&AnyAdapters<gz::AnyBoolean>::Assign),         // 0x14e760
    Static(&AnyAdapters<gz::AnyBoolean>::Compare),        // 0x14e5b0
    Static(&AnyAdapters<gz::AnyBoolean>::Clone),          // 0x14df70
    Static(&AnyAdapters<gz::AnyBoolean>::GetType),        // 0x1522b0
    Static(&AnyAdapters<gz::AnyBoolean>::GetTypeName),    // 0x1522c0
};
extern "C" const void *ts2_own_vt_da3d48[6] = {
    Member(&AnySint32::DeletingDestructor), // 0x1bedcb
    Member(&AnySint32::Assign),             // 0x1bf068
    Member(&AnySint32::Compare),            // 0x1befcf
    Member(&AnySint32::Clone),              // 0x1bef56
    Member(&AnySint32::GetType),            // 0x1bf5c5
    Member(&AnySint32::GetTypeName),        // 0x1bf5dd
};
extern "C" const void *ts2_own_vt_da3d70[6] = {
    Member(&AnySint32::DeletingDestructor),            // 0x1bedcb
    Member(&AnySint32::Assign),                        // 0x1bf068
    Static(&AnyAdapters<gz::AnyFloat32>::Compare),     // 0x1befac
    Static(&AnyAdapters<gz::AnyFloat32>::Clone),       // 0x1bef35
    Static(&AnyAdapters<gz::AnyFloat32>::GetType),     // 0x1bf5bf
    Static(&AnyAdapters<gz::AnyFloat32>::GetTypeName), // 0x1bf5d7
};
extern "C" const void *ts2_own_vt_da3d98[6] = {
    Member(&AnyString::DeletingDestructor), // 0x1bedeb
    Member(&AnyString::Assign),             // 0x1bf097
    Member(&AnyString::Compare),            // 0x1befeb
    Member(&AnyString::Clone),              // 0x1bef77
    Member(&AnyString::GetType),            // 0x1bf5cb
    Member(&AnyString::GetTypeName),        // 0x1bf5e3
};
extern "C" const void *ts2_own_vt_da3e00[6] = {
    Member(&AnySint32::DeletingDestructor), // 0x1bedcb
    Member(&AnySint32::Assign),             // 0x1bf068
    Member(&AnyAny::Compare),               // 0x1bef94
    Member(&AnyAny::Clone),                 // 0x1bef14
    Member(&AnyAny::GetType),               // 0x1bf5b9
    Member(&AnyAny::GetTypeName),           // 0x1bf5d1
};
