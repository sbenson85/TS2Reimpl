// nGZEvent::cVectorEvent<cVector<4, float>> (lane FD): src/gz/cVector4fEvent.h.
#include "gz/cVector4fEvent.h"

#include <cstring>
#include <new>

namespace nGZEvent {

cVector4fEvent *cVector4fEvent::DeletingDestructor(uint32_t flags)
{
    mVTable = &ts2_rva_d9ca58;
    if (flags & 1) ::operator delete(this, sizeof(cVector4fEvent));
    return this;
}

cVector4fEvent *cVector4fEvent::Assign(const cVector4fEvent *other)
{
    std::memcpy(mValue, other->mValue, sizeof(mValue));
    return this;
}

uint32_t cVector4fEvent::GetEventType() const { return kType; }
uint32_t cVector4fEvent::GetSize() const { return sizeof(cVector4fEvent); }
uint32_t cVector4fEvent::GetBodySize() const { return sizeof(cVector4fEvent) - 4; }

cVector4fEvent *cVector4fEvent::CloneInto(void *memory) const
{
    return ::new (memory) cVector4fEvent(mValue[0], mValue[1], mValue[2], mValue[3]);
}

} // namespace nGZEvent

// The image-free program's table (standalone/owned_statics.cpp aliases ts2_rva_db07d4 to it): the image's slot order,
// filled by a dynamic initialiser before any code of the program runs.
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
using nGZEvent::cVector4fEvent;
} // namespace

extern "C" const void *ts2_own_vt_db07d4[7] = {
    Member(&cVector4fEvent::DeletingDestructor), // 0 0x154eb0 (folded)
    Member(&cVector4fEvent::Assign),             // 1 0x258ca0
    Member(&cVector4fEvent::Assign),             // 2 0x258ca0
    Member(&cVector4fEvent::GetEventType),       // 3 0x269ba0
    Member(&cVector4fEvent::GetSize),            // 4 0x1559e0 (folded)
    Member(&cVector4fEvent::GetBodySize),        // 5 0x1561d0 (folded)
    Member(&cVector4fEvent::CloneInto),          // 6 0x25b130
};

// cIEventHeader's table (0xd9ca58), what the destructors of its bodies leave: its deleting destructor (0x154e80, ours in
// src/misc/DeletingDestructors.cpp), then six pure slots (the image's 0x8261ba, our toolchain's _purecall).
extern "C" int __cdecl _purecall();
extern "C" const void *ts2_own_vt_d9ca58[7] = {
    Member(&nGZEvent::cIEventHeader::DeletingDestructor),
    reinterpret_cast<const void *>(&_purecall), reinterpret_cast<const void *>(&_purecall),
    reinterpret_cast<const void *>(&_purecall), reinterpret_cast<const void *>(&_purecall),
    reinterpret_cast<const void *>(&_purecall), reinterpret_cast<const void *>(&_purecall),
};
