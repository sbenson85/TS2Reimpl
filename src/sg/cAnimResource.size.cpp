#include "sg/cAnimResource.h"

#include "gz/cSGResource.h"
#include "sg/SGResourceHelpers.h"

#include <cstdint>

namespace nGZSceneDBAnim {

namespace {

// The thunks' adjustment: this - vtordisp (the dword at this - 4) - 0x94, which is the cSGResource secondary
// subobject S (object + 0x30). Done in uintptr_t so the subtraction wraps the way the original's sub does.
uintptr_t SecondaryOf(const cAnimResourceSubobject *self)
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(self);
    const uint32_t displacement = *reinterpret_cast<const uint32_t *>(address - 4);
    return address - displacement - 0x94;
}

nGZSceneGraph::cSGResourceSubobject *Secondary(const cAnimResourceSubobject *self)
{
    return reinterpret_cast<nGZSceneGraph::cSGResourceSubobject *>(SecondaryOf(self));
}

} // namespace

// Thunk 0xa10195 (14 bytes): sub ecx,[ecx-4]; sub ecx,0x94; jmp 0xa3c750. Body: no arguments, ret 0.
void *cAnimResourceSubobject::VirtualBaseIfObject()
{
    return reinterpret_cast<SGResourceHelpers::cSGSecondary *>(SecondaryOf(this))->VirtualBaseIfObject();
}

// Thunk 0xa0f3ae: sub ecx,[ecx-4]; sub ecx,0x94; jmp 0xa3c3c0. Body: the object, S - 0x30.
void *cAnimResourceSubobject::GetOwner()
{
    return Secondary(this)->GetOwner();
}

// Thunk 0xa10187: sub ecx,[ecx-4]; sub ecx,0x94; jmp 0xa3c720. Body: ret 4.
void *cAnimResourceSubobject::GetVirtualBase(void *arg)
{
    return Secondary(this)->GetVirtualBase(arg);
}

// Thunk 0xa0f554: sub ecx,[ecx-4]; sub ecx,0x94; jmp 0xa3c3d0. Body: ret 4.
int32_t cAnimResourceSubobject::CompareIdentity(void *other)
{
    return Secondary(this)->CompareIdentity(other);
}

// Thunk 0xa103ce: sub ecx,[ecx-4]; sub ecx,0x94; jmp 0xa3cdd0. Body: ret 4, always returns true (al = 1).
bool cAnimResourceSubobject::SetResource(void *resource)
{
    return Secondary(this)->SetResource(resource);
}

// Thunk 0xa101e0: sub ecx,[ecx-4]; sub ecx,0x94; jmp 0xa3c870. Body: ret 4, stores the low byte of the argument.
void cAnimResourceSubobject::SetFlag(uint32_t value)
{
    Secondary(this)->SetFlag(static_cast<uint8_t>(value));
}

// Thunk 0xa101a3: sub ecx,[ecx-4]; sub ecx,0x94; jmp 0xa3c770. Body: ret 4, returns out.
void *cAnimResourceSubobject::CopyKeyTo(void *out)
{
    return Secondary(this)->CopyKeyTo(out);
}

// Thunk 0xa0f57b: sub ecx,[ecx-4]; sub ecx,0x94; jmp 0xa3c4a0. Body: mov al,[ecx-7]; the caller reads al only.
// The thunk itself never writes eax, so the upper bits of eax are whatever the caller left there; this returns
// the flag in al and leaves the rest to the compiler.
uint8_t cAnimResourceSubobject::GetFlag()
{
    return Secondary(this)->GetFlag();
}

// Thunk 0xa0fb19: sub ecx,[ecx-4]; sub ecx,0x94; jmp 0xa3c6d0. Body: the resource's slot 21 result, or -1.
uint32_t cAnimResourceSubobject::ForwardKey()
{
    return Secondary(this)->ForwardKey();
}

} // namespace nGZSceneDBAnim
