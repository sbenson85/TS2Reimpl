#include "rz/RZAutoRef.h"

#include "game/vtable.h"

#include <new>

namespace {

using AddRefFn = uint32_t(__thiscall *)(void *);
using ReleaseFn = uint32_t(__thiscall *)(void *);

// Results of AddRef and Release are not used by any of these functions.
void AddRefTo(void *object)
{
    VirtualCall<AddRefFn, 1>(object);
}

void ReleaseTo(void *object)
{
    VirtualCall<ReleaseFn, 2>(object);
}

// Releases the base subobject of object that sits at the virtual base offset. The vbptr is at
// object+4, and entry 1 of its table (at vbptr+4) is the displacement from object+4 to the base.
void ReleaseVirtualBaseOf(void *object)
{
    char *base = static_cast<char *>(object);
    char *vbptr = *reinterpret_cast<char **>(base + 4);
    int32_t displacement = *reinterpret_cast<int32_t *>(vbptr + 4);
    void *subobject = base + 4 + displacement;
    ReleaseTo(subobject);
}

} // namespace

void RZAutoRef::ReleaseObject() noexcept
{
    // The original is `try { p->Release(); } catch (...) {}` compiled with /EHs (FuncInfo flags 5,
    // one try block whose catch(...) is C++ only). Our /EHsc catch(...) does the same.
    if (!mObject) return;
    try {
        ReleaseTo(mObject);
    } catch (...) {
    }
}

void RZAutoRef::ReleaseUnguarded() noexcept
{
    if (mObject) ReleaseTo(mObject);
}

RZAutoRef *RZAutoRef::DeletingDestructor(uint32_t flags)
{
    ReleaseUnguarded();
    if (flags & 1) ::operator delete(this, sizeof(RZAutoRef));
    return this;
}

RZAutoRef *RZAutoRef::Reset()
{
    void *object = mObject;
    mObject = nullptr;
    if (object) ReleaseTo(object);
    return this;
}

void RZAutoRef::ReleaseVirtualBase() noexcept
{
    if (mObject) ReleaseVirtualBaseOf(mObject);
}

RZAutoRef *RZAutoRef::ResetVirtualBase()
{
    void *object = mObject;
    mObject = nullptr;
    if (object) ReleaseVirtualBaseOf(object);
    return this;
}

template <uint32_t Offset> void RZRefField<Offset>::ReleaseRef() noexcept
{
    if (mRef) ReleaseTo(mRef);
}

template <uint32_t Offset> void RZRefField<Offset>::AssignRef(void *p)
{
    if (p == mRef) return;
    if (p) AddRefTo(p);
    void *old = mRef;
    if (old) ReleaseTo(old);
    mRef = p;
}

template <uint32_t Offset> void RZRefField<Offset>::AssignRefAlways(void *p)
{
    if (p) AddRefTo(p);
    void *old = mRef;
    if (old) ReleaseTo(old);
    mRef = p;
}

template struct RZRefField<0x10>;
template struct RZRefField<0x28>;
