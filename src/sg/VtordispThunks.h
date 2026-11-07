#pragma once
#include "gz/cSGResource.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/StaticGetters2.h"
#include "sg/SGResourceHelpers.h"
#include "sg/SGShapeGeometry.h"
#include "ui/UIWinProcs.h"

#include <cstdint>

// Bulk forwarders for vtordisp thunks. A thunk in a vtable of a class with a virtual base has the form
//   sub ecx,[ecx-4]; sub ecx,N; jmp body          (14 bytes)
// so the body gets this' = this - vtordisp - N, where vtordisp is the dword just before `this` in the object.
// Each forwarder below is the replacement for one thunk: it computes this' the same way and calls our
// reimplementation of the body on it. The thunk's N is a template parameter, so every thunk has its own
// member function address to hook (see src/hook/tables/VtordispThunks.cpp). Table of thunks and bodies:
// tests/vtordisp_thunks.cpp (generated from tools/vtordisp.py output).
namespace VtordispThunks {

// The body's `this` for a thunk with adjustment n, given the `this` the thunk was entered with.
inline void *Adjusted(const void *self, uint32_t n)
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(self);
    const uint32_t displacement = *reinterpret_cast<const uint32_t *>(address - 4);
    return reinterpret_cast<void *>(address - displacement - n);
}

// Body 0x9e8850 (RZEnclosedFields::GetDwordMinus12, reads the dword at this-12): 21 thunks.
template <uint32_t N> struct EnclosedDwordThunk {
    uint32_t Call() const
    {
        return reinterpret_cast<const RZEnclosedFields *>(Adjusted(this, N))->GetDwordMinus12();
    }
};

// Body 0x9d9570 (RZEnclosedFields::GetByteMinus8, the byte at this-8; only al is defined): 15 thunks.
template <uint32_t N> struct EnclosedByteThunk {
    uint8_t Call() const
    {
        return reinterpret_cast<const RZEnclosedFields *>(Adjusted(this, N))->GetByteMinus8();
    }
};

// Body 0xa3bdb0 (cSignBitBeforeThis::SignBit, bit 31 of the dword at this-8): 9 thunks.
template <uint32_t N> struct SignBitThunk {
    uint32_t Call()
    {
        return reinterpret_cast<SGResourceHelpers::cSignBitBeforeThis *>(Adjusted(this, N))->SignBit();
    }
};

// Body 0x9529e5 (cGZWinMsgBoxAt252::ForwardToSlot5, the object at this-0xfc, its slot 5): 6 thunks.
template <uint32_t N> struct ForwardToSlot5Thunk {
    uint32_t Call()
    {
        return reinterpret_cast<cGZWinMsgBoxAt252 *>(Adjusted(this, N))->ForwardToSlot5();
    }
};

// The bodies below take `this` as the cSGResource secondary subobject (object + 0x30), or, for 0xa431c0, as
// cGeometryNodeSecondary8 (object + 0x08). Each thunk passes the body's this as Adjusted(this, N); the stack
// arguments and the return are the same as the body's, since the thunk only adjusts ecx and jumps.

// Bodies 0xa3c3c0 (GetOwner, 14 thunks), 0xa3c3d0 (CompareIdentity), 0xa3c4a0 (GetFlag), 0xa3c6d0 (ForwardKey),
// 0xa3c720 (GetVirtualBase), 0xa3c770 (CopyKeyTo), 0xa3c870 (SetFlag), 0xa3cdd0 (SetResource): 14 thunks each.
template <uint32_t N> struct SubobjectOwnerThunk {
    void *Call() const
    {
        return reinterpret_cast<nGZSceneGraph::cSGResourceSubobject *>(Adjusted(this, N))->GetOwner();
    }
};

template <uint32_t N> struct SubobjectCompareIdentityThunk {
    int32_t Call(void *other) const
    {
        return reinterpret_cast<nGZSceneGraph::cSGResourceSubobject *>(Adjusted(this, N))->CompareIdentity(other);
    }
};

template <uint32_t N> struct SubobjectGetFlagThunk {
    uint8_t Call() const
    {
        return reinterpret_cast<nGZSceneGraph::cSGResourceSubobject *>(Adjusted(this, N))->GetFlag();
    }
};

template <uint32_t N> struct SubobjectForwardKeyThunk {
    uint32_t Call() const
    {
        return reinterpret_cast<nGZSceneGraph::cSGResourceSubobject *>(Adjusted(this, N))->ForwardKey();
    }
};

template <uint32_t N> struct SubobjectGetVirtualBaseThunk {
    void *Call(void *arg) const
    {
        return reinterpret_cast<nGZSceneGraph::cSGResourceSubobject *>(Adjusted(this, N))->GetVirtualBase(arg);
    }
};

template <uint32_t N> struct SubobjectCopyKeyToThunk {
    void *Call(void *out) const
    {
        return reinterpret_cast<nGZSceneGraph::cSGResourceSubobject *>(Adjusted(this, N))->CopyKeyTo(out);
    }
};

template <uint32_t N> struct SubobjectSetFlagThunk {
    void Call(uint32_t value) const
    {
        reinterpret_cast<nGZSceneGraph::cSGResourceSubobject *>(Adjusted(this, N))->SetFlag(static_cast<uint8_t>(value));
    }
};

template <uint32_t N> struct SubobjectSetResourceThunk {
    uint8_t Call(void *resource) const
    {
        return reinterpret_cast<nGZSceneGraph::cSGResourceSubobject *>(Adjusted(this, N))->SetResource(resource) ? 1 : 0;
    }
};

// Bodies 0xa3c790 (SubmitKeyToService, 14 thunks, plus the cAnimResource thunk 0xa101d2), 0xa3c750
// (VirtualBaseIfObject, 1 thunk), 0xa3c890 (ResetAndSubmit, 14 thunks, plus the cAnimResource thunk 0xa102a9),
// 0xa3ca00 (OpenAssignAndSubmit, 15 thunks) and 0xa3cbf0 (AssignAndSubmit, 15 thunks) on cSGSecondary.
template <uint32_t N> struct SecondarySubmitThunk {
    uint8_t Call() const
    {
        return reinterpret_cast<SGResourceHelpers::cSGSecondary *>(Adjusted(this, N))->SubmitKeyToService();
    }
};

template <uint32_t N> struct SecondaryVirtualBaseThunk {
    void *Call() const
    {
        return reinterpret_cast<SGResourceHelpers::cSGSecondary *>(Adjusted(this, N))->VirtualBaseIfObject();
    }
};

template <uint32_t N> struct SecondaryResetAndSubmitThunk {
    uint8_t Call(void *a1, void *a2) const
    {
        return reinterpret_cast<SGResourceHelpers::cSGSecondary *>(Adjusted(this, N))->ResetAndSubmit(a1, a2);
    }
};

template <uint32_t N> struct SecondaryOpenAssignAndSubmitThunk {
    uint8_t Call(uint32_t a1, void *a2, void *a3) const
    {
        return reinterpret_cast<SGResourceHelpers::cSGSecondary *>(Adjusted(this, N))->OpenAssignAndSubmit(a1, a2, a3);
    }
};

template <uint32_t N> struct SecondaryAssignAndSubmitThunk {
    uint8_t Call(uint32_t a1, void *a2) const
    {
        return reinterpret_cast<SGResourceHelpers::cSGSecondary *>(Adjusted(this, N))->AssignAndSubmit(a1, a2);
    }
};

// Body 0xa3c4b0 (Dispatch8Adjusted, 15 thunks): this - 0x10 then Dispatch8, reached with the body's this.
template <uint32_t N> struct Dispatch8AdjustedThunk {
    uint32_t Call() const
    {
        return Dispatch8Adjusted(Adjusted(this, N));
    }
};

// Body 0xa431c0 (cGeometryNodeSecondary8::VirtualBaseOfResource, 11 thunks): add ecx,0x3c, then VirtualBaseIfObject.
template <uint32_t N> struct VirtualBaseOfResourceThunk {
    void *Call() const
    {
        return reinterpret_cast<nGZSceneGraph::cGeometryNodeSecondary8 *>(Adjusted(this, N))->VirtualBaseOfResource();
    }
};

} // namespace VtordispThunks
