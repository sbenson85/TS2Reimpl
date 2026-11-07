#include "sg/cAnimResourceConst.h"

#include "gz/cSGResource.h"
#include "sg/SGResourceHelpers.h"

#include <cstdint>

namespace nGZSceneDBAnim {

namespace {

// Bytes at a fixed offset from the subobject (positive offsets only: these bodies read the subobject and the
// record it points to).
template <typename T> T &At(cAnimResourceConstSubobject *self, int offset)
{
    return *reinterpret_cast<T *>(reinterpret_cast<uint8_t *>(self) + offset);
}

// The animation record at S+0x18 (object +0x44).
uint8_t *RecordOf(cAnimResourceConstSubobject *self)
{
    return At<uint8_t *>(self, 0x18);
}

// The record's field at a fixed offset.
template <typename T> T &RecordAt(cAnimResourceConstSubobject *self, int offset)
{
    return *reinterpret_cast<T *>(RecordOf(self) + offset);
}

} // namespace

// 0x57e910 (6 bytes): mov al,[ecx+8]; and al,1; ret. Only al is defined; the caller reads al alone.
uint8_t cAnimResourceConstSubobject::FlagBit0()
{
    return At<uint8_t>(this, 8) & 1;
}

// 0xa111b0 (7 bytes): mov eax,[ecx+0x18]; add eax,0xc; ret
void *cAnimResourceConstSubobject::RecordFieldC()
{
    return RecordOf(this) + 0xc;
}

// 0xa10da0 (22 bytes): ret 4. Returns entries + 28 * index, computed in 32 bits (lea edx,[eax*8]; sub edx,eax;
// lea eax,[eax+edx*4]), so a large index wraps like the original.
void *cAnimResourceConstSubobject::EntryAt(uint32_t index)
{
    uint32_t base = At<uint32_t>(this, 0x1c);
    return reinterpret_cast<void *>(static_cast<uintptr_t>(base + index * 28u));
}

// 0xa15e90 (8 bytes): test byte [ecx+8],3; setne al; ret. Only al is defined.
bool cAnimResourceConstSubobject::HasFlagBits0To1()
{
    return (At<uint8_t>(this, 8) & 3) != 0;
}

// 0xa15eb0 (9 bytes): mov eax,[ecx+0x18]; mov al,[eax+8]; and al,1; ret. Only al is defined.
uint8_t cAnimResourceConstSubobject::RecordFlagBit0()
{
    return RecordAt<uint8_t>(this, 8) & 1;
}

// 0xa16380 (11 bytes): mov eax,[ecx+0x18]; mov eax,[eax+8]; shr eax,1; and al,1; ret. Only al is defined.
uint8_t cAnimResourceConstSubobject::RecordFlagBit1()
{
    uint32_t word = RecordAt<uint32_t>(this, 8);
    return static_cast<uint8_t>((word >> 1) & 1);
}

// 0xa16490 (15 bytes): mov eax,[ecx+0x18]; mov eax,[eax+8]; shr eax,0xc; and eax,0xfff; ret
uint32_t cAnimResourceConstSubobject::RecordField8Bits12To23()
{
    return (RecordAt<uint32_t>(this, 8) >> 12) & 0xfff;
}

// 0xa16530 (8 bytes): mov eax,[ecx+0x18]; movzx eax,word [eax+2]; ret. Zero-extended into the whole eax.
uint32_t cAnimResourceConstSubobject::RecordWord2()
{
    return RecordAt<uint16_t>(this, 2);
}

// 0xa16590 (13 bytes): mov eax,[ecx+0x18]; mov eax,[eax+8]; shr eax,8; and eax,0xf; ret
uint32_t cAnimResourceConstSubobject::RecordField8Bits8To11()
{
    return (RecordAt<uint32_t>(this, 8) >> 8) & 0xf;
}

// 0xa17770 (7 bytes): mov eax,[ecx+0x18]; mov eax,[eax+0x14]; ret
uint32_t cAnimResourceConstSubobject::RecordDword14()
{
    return RecordAt<uint32_t>(this, 0x14);
}

// 0xa17780 (7 bytes): mov eax,[ecx+0x18]; mov eax,[eax+0x18]; ret
uint32_t cAnimResourceConstSubobject::RecordDword18()
{
    return RecordAt<uint32_t>(this, 0x18);
}

// 0xa175c0 (41 bytes): ret 8. The flag word loses bits 0-1 first. Then, if the first argument's low byte is
// non-zero, bit 1 is set when the second's low byte is non-zero and bit 0 otherwise. The word is stored
// whether or not the first argument is set (the original stores the cleared word, then possibly sets bits).
void cAnimResourceConstSubobject::SetFlagBits(uint8_t a, uint8_t b)
{
    uint32_t word = At<uint32_t>(this, 8) & ~3u;
    if (a != 0) word |= (b != 0) ? 2u : 1u;
    At<uint32_t>(this, 8) = word;
}

// 0xa125f0 (7 bytes): mov eax,[ecx+0x18]; fld dword [eax+0x1c]; ret. The result is in st(0).
float cAnimResourceConstSubobject::RecordFloat1C()
{
    return RecordAt<float>(this, 0x1c);
}

// 0xa163a0 (7 bytes): mov eax,[ecx+0x18]; fld dword [eax+0x20]; ret
float cAnimResourceConstSubobject::RecordFloat20()
{
    return RecordAt<float>(this, 0x20);
}

// 0xa16390 (7 bytes): mov eax,[ecx+0x18]; fld dword [eax+0x24]; ret
float cAnimResourceConstSubobject::RecordFloat24()
{
    return RecordAt<float>(this, 0x24);
}

// 0xa16470 (7 bytes): mov eax,[ecx+0x18]; fld dword [eax+0x28]; ret
float cAnimResourceConstSubobject::RecordFloat28()
{
    return RecordAt<float>(this, 0x28);
}

// 0xa16480 (7 bytes): mov eax,[ecx+0x18]; fld dword [eax+0x2c]; ret
float cAnimResourceConstSubobject::RecordFloat2C()
{
    return RecordAt<float>(this, 0x2c);
}

// 0xa17870 (7 bytes): mov eax,[ecx+0x18]; fld dword [eax+0x30]; ret
float cAnimResourceConstSubobject::RecordFloat30()
{
    return RecordAt<float>(this, 0x30);
}

// 0xa17790 (7 bytes): mov eax,[ecx+0x18]; fld dword [eax+0x34]; ret
float cAnimResourceConstSubobject::RecordFloat34()
{
    return RecordAt<float>(this, 0x34);
}

// 0xa16580 (7 bytes): mov eax,[ecx+0x18]; fld dword [eax+0x38]; ret
float cAnimResourceConstSubobject::RecordFloat38()
{
    return RecordAt<float>(this, 0x38);
}

// 0xa17100 (7 bytes): mov eax,[ecx+0x18]; fld dword [eax+0x3c]; ret
float cAnimResourceConstSubobject::RecordFloat3C()
{
    return RecordAt<float>(this, 0x3c);
}

namespace {

// The thunks' adjustment: this - vtordisp (the dword at this - 4) - 0x28, which is the cSGResource secondary
// subobject S (object + 0x30). Done in uintptr_t so the subtraction wraps the way the original's sub does.
uintptr_t SecondaryOf(const cAnimResourceConstSecondary *self)
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(self);
    const uint32_t displacement = *reinterpret_cast<const uint32_t *>(address - 4);
    return address - displacement - 0x28;
}

nGZSceneGraph::cSGResourceSubobject *Secondary(const cAnimResourceConstSecondary *self)
{
    return reinterpret_cast<nGZSceneGraph::cSGResourceSubobject *>(SecondaryOf(self));
}

} // namespace

// Thunk 0xa170e0 (11 bytes): sub ecx,[ecx-4]; sub ecx,0x28; jmp 0xa3c750. Body: no arguments, ret 0.
void *cAnimResourceConstSecondary::VirtualBaseIfObject()
{
    return reinterpret_cast<SGResourceHelpers::cSGSecondary *>(SecondaryOf(this))->VirtualBaseIfObject();
}

// Thunk 0xa10e60: sub ecx,[ecx-4]; sub ecx,0x28; jmp 0xa3c3c0. Body: the object, S - 0x30.
void *cAnimResourceConstSecondary::GetOwner()
{
    return Secondary(this)->GetOwner();
}

// Thunk 0xa170d5: sub ecx,[ecx-4]; sub ecx,0x28; jmp 0xa3c720. Body: ret 4.
void *cAnimResourceConstSecondary::GetVirtualBase(void *arg)
{
    return Secondary(this)->GetVirtualBase(arg);
}

// Thunk 0xa15e76: sub ecx,[ecx-4]; sub ecx,0x28; jmp 0xa3c3d0. Body: ret 4.
int32_t cAnimResourceConstSecondary::CompareIdentity(void *other)
{
    return Secondary(this)->CompareIdentity(other);
}

// Thunk 0xa175ff: sub ecx,[ecx-4]; sub ecx,0x28; jmp 0xa3cdd0. Body: ret 4, returns the body's al.
bool cAnimResourceConstSecondary::SetResource(void *resource)
{
    return Secondary(this)->SetResource(resource);
}

// Thunk 0xa1759c: sub ecx,[ecx-4]; sub ecx,0x28; jmp 0xa3c870. Body: ret 4, stores the low byte of the argument.
void cAnimResourceConstSecondary::SetFlag(uint32_t value)
{
    Secondary(this)->SetFlag(static_cast<uint8_t>(value));
}

// Thunk 0xa170eb: sub ecx,[ecx-4]; sub ecx,0x28; jmp 0xa3c770. Body: ret 4, returns out.
void *cAnimResourceConstSecondary::CopyKeyTo(void *out)
{
    return Secondary(this)->CopyKeyTo(out);
}

// Thunk 0xa15e98: sub ecx,[ecx-4]; sub ecx,0x28; jmp 0xa3c4a0. Body: mov al,[ecx-7]; the caller reads al only.
uint8_t cAnimResourceConstSecondary::GetFlag()
{
    return Secondary(this)->GetFlag();
}

// Thunk 0xa170af: sub ecx,[ecx-4]; sub ecx,0x28; jmp 0xa3c6d0. Body: the resource's slot 21 result, or -1.
uint32_t cAnimResourceConstSecondary::ForwardKey()
{
    return Secondary(this)->ForwardKey();
}

} // namespace nGZSceneDBAnim
