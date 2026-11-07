#pragma once
#include <cstdint>

// Reference holders: a field that holds a pointer to a reference-counted object. AddRef is vtable
// slot 1 and Release is slot 2 (cRZRefCount, cIGZUnknown). The holders' owners are not identified
// yet, so the layouts below are only what the code touches. RVAs are 1.18.0.187.

// A holder whose only field is the object pointer (4 bytes).
struct RZAutoRef {
    void *mObject;

    // 0x1466e0 (__thiscall): if mObject is non-null, calls its Release (slot 2) inside try/catch(...)
    // built with /EHs: C++ exceptions from Release are swallowed, SEH exceptions propagate. mObject
    // is not cleared. The frame is also flagged noexcept (FuncInfo flags 5).
    void ReleaseObject() noexcept;

    // 0x1cbd45 and 0x145d50 (__thiscall): the same release without a handler. The frame is flagged
    // noexcept (FuncInfo flags 5), so a C++ exception from Release terminates the process, as it does
    // in the original. SEH exceptions propagate. mObject is not cleared.
    void ReleaseUnguarded() noexcept;

    // 0x142196 (__thiscall, ret 4): ReleaseUnguarded, then with flags & 1 deletes this (sized
    // operator delete, size 4). Returns this.
    RZAutoRef *DeletingDestructor(uint32_t flags);

    // 0x1dae60 (__thiscall): clears mObject, then releases the old object if it was non-null.
    // Returns this.
    RZAutoRef *Reset();

    // 0x210cfc and 0x1fcd10 (__thiscall): mObject is an object whose virtual base is at the offset
    // in its virtual base table, found through the vbptr at +4. Releases that base subobject
    // through its slot 2. mObject is not cleared, and nothing is caught. Noexcept, as the original's
    // frame is.
    void ReleaseVirtualBase() noexcept;

    // 0x2296b4 and 0x258c70 (__thiscall): clears mObject, then ReleaseVirtualBase on the old object.
    // Returns this.
    RZAutoRef *ResetVirtualBase();
};

static_assert(sizeof(RZAutoRef) == 4, "RZAutoRef layout");

// An owner whose reference is the field at Offset (0x10 or 0x28 in the code). The owning class is
// not identified, so the fields before the reference are opaque.
template <uint32_t Offset> struct RZRefField {
    uint8_t mOwnerFields[Offset];
    void *mRef;

    // 0x416c90 (__thiscall, Offset 0x10): if mRef is non-null, calls its Release (slot 2). Not
    // cleared, nothing is caught, and noexcept as the original's frame is.
    void ReleaseRef() noexcept;

    // 0x9981d0 (Offset 0x10) and 0x9d0c32 (Offset 0x28) (__thiscall, ret 4): assigns p to mRef. Does
    // nothing when p already is mRef; otherwise AddRef (slot 1) on p if non-null, Release (slot 2)
    // on the old reference if non-null, then stores p.
    void AssignRef(void *p);

    // 0x991253 (Offset 0x10, __thiscall, ret 4): assigns p to mRef without the comparison: AddRef on
    // p if non-null, Release on the old reference if non-null, then stores p.
    void AssignRefAlways(void *p);
};

static_assert(sizeof(RZRefField<0x10>) == 0x14, "RZRefField<0x10> layout");
static_assert(sizeof(RZRefField<0x28>) == 0x2c, "RZRefField<0x28> layout");
