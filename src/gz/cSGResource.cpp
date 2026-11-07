#include "gz/cSGResource.h"

#include "game/vtable.h"

#include <cstdint>

namespace nGZSceneGraph {

namespace {

// The identifier the original pushes before calling QueryInterface (0xa3c3d0). It is an immediate, not a
// pointer to an IID structure, so it is passed through as the value itself.
const void *const kIdentityId = reinterpret_cast<const void *>(static_cast<uintptr_t>(0x9b4bbca3u));

// Bytes at a fixed offset from the subobject: negative offsets reach the object's own fields.
template <typename T> T &At(cSGResourceSubobject *self, int offset)
{
    return *reinterpret_cast<T *>(reinterpret_cast<uint8_t *>(self) + offset);
}

// The virtual base's address: the vbptr at object +0x1c (subobject -0x14), its entry 1 is the displacement
// from there, and the original adds it to the subobject pointer with the -0x14 folded in.
void *VirtualBaseOf(cSGResourceSubobject *self)
{
    const int *vbtable = At<const int *>(self, -0x14);
    return reinterpret_cast<uint8_t *>(self) + (vbtable[1] - 0x14);
}

// QueryInterface is slot 0 of any object; the original reads the result from al only.
bool QueryInterface(void *object, void **out)
{
    return VirtualCall<uint8_t(__thiscall *)(void *, const void *, void **), 0>(object, kIdentityId, out) != 0;
}

// AddRef and Release are slots 1 and 2; the original ignores the counts they return.
void AddRef(void *object)
{
    VirtualCall<uint32_t(__thiscall *)(void *), 1>(object);
}

void Release(void *object)
{
    VirtualCall<uint32_t(__thiscall *)(void *), 2>(object);
}

} // namespace

// 0xa3c3c0 (4 bytes): lea eax,[ecx-0x30]; ret
void *cSGResourceSubobject::GetOwner()
{
    return reinterpret_cast<uint8_t *>(this) - 0x30;
}

// 0xa3c3d0 (191 bytes): ret 4. The argument's QueryInterface gives p; then the virtual base's gives q. The
// result is 0 when both succeed and q equals p, else -1. Each non-null interface is released once.
// The original keeps p and q in a SEH frame whose cleanup releases them on unwind; this version releases
// them on the normal paths only, since nothing called here throws.
int32_t cSGResourceSubobject::CompareIdentity(void *other)
{
    void *p = nullptr;
    int32_t result = -1;
    if (QueryInterface(other, &p)) {
        void *q = nullptr;
        bool baseOk = QueryInterface(VirtualBaseOf(this), &q);
        if (baseOk && q == p) result = 0;
        if (q != nullptr) Release(q);
    }
    if (p != nullptr) Release(p);
    return result;
}

// 0xa3c4a0 (4 bytes): mov al,[ecx-7]; ret
uint8_t cSGResourceSubobject::GetFlag()
{
    return At<uint8_t>(this, -7);
}

// 0xa3c870 (10 bytes): mov al,[esp+4]; mov [ecx-7],al; ret 4. Only the low byte of the argument is stored.
void cSGResourceSubobject::SetFlag(uint8_t value)
{
    At<uint8_t>(this, -7) = value;
}

// 0xa3c720 (32 bytes): ret 4. Returns the virtual base when the argument is null and the object pointer is
// not null (that is, this - 0x30 != 0, checked before the vbptr is read); otherwise null.
void *cSGResourceSubobject::GetVirtualBase(void *arg)
{
    if (arg != nullptr) return nullptr;
    if (reinterpret_cast<uintptr_t>(this) - 0x30 == 0) return nullptr;
    return VirtualBaseOf(this);
}

// 0xa3c770 (20 bytes): ret 4. Primary slot 3 copies the object's 16-byte key (+0x0c) to out; returns out.
void *cSGResourceSubobject::CopyKeyTo(void *out)
{
    void *object = GetOwner();
    VirtualCall<void(__thiscall *)(void *, void *), kPrimaryKeySlot>(object, out);
    return out;
}

// 0xa3c6d0 (32 bytes): returns -1 when the resource pointer (S-0x0c) is null or the flag byte (S-0x08) is
// zero. Otherwise the key is copied (primary slot 3) into a local, and the resource's slot 21 is called
// with the pointer that slot 3 returned. The resource's vtable is read before the key is copied, as the
// original does.
uint32_t cSGResourceSubobject::ForwardKey()
{
    void *resource = At<void *>(this, -0xc);
    if (resource == nullptr || At<uint8_t>(this, -8) == 0) return 0xffffffffu;
    void *const *vtable = *static_cast<void *const *const *>(resource);
    uint32_t key[4];
    void *object = GetOwner();
    void *keyPointer = VirtualCall<void *(__thiscall *)(void *, void *), kPrimaryKeySlot>(object, key);
    return reinterpret_cast<uint32_t (__thiscall *)(void *, void *)>(vtable[kResourceForwardSlot])(resource, keyPointer);
}

// 0xa3cdd0 (32 bytes): ret 4. Same pointer: nothing happens. Otherwise the new resource gains a reference
// (AddRef, slot 1) before it is stored at S-0x0c, and the old one, if any, is released (slot 2). Always
// returns true (al = 1).
bool cSGResourceSubobject::SetResource(void *resource)
{
    void *old = At<void *>(this, -0xc);
    if (resource != old) {
        if (resource != nullptr) AddRef(resource);
        At<void *>(this, -0xc) = resource;
        if (old != nullptr) Release(old);
    }
    return true;
}

} // namespace nGZSceneGraph
