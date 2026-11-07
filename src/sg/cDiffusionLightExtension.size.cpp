#include "sg/cDiffusionLightExtension.h"

#include <cstdint>
#include <cstring>

namespace nTSSG {

namespace {

// The full object for this subobject: the displacement is the dword before the subobject's vtable pointer.
uintptr_t FullOf(const cIDiffusionLightExtensionSub *self)
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(self);
    const uint32_t displacement = *reinterpret_cast<const uint32_t *>(address - 4);
    return address - displacement;
}

// A field of the full object at a (negative) offset. Done in uintptr_t so the wrap matches the original's add.
template <typename T> T &At(const cIDiffusionLightExtensionSub *self, int32_t offset)
{
    return *reinterpret_cast<T *>(FullOf(self) + static_cast<uintptr_t>(offset));
}

uintptr_t AddressOf(const cIDiffusionLightExtensionSub *self, int32_t offset)
{
    return FullOf(self) + static_cast<uintptr_t>(offset);
}

} // namespace

// 0x2ea27b: the flags only, in the order the original clears them.
void cIDiffusionLightExtensionSub::ResetFlags()
{
    At<uint8_t>(this, -0x5c) = 0;
    At<uint8_t>(this, -0x50) = 0;
    At<uint8_t>(this, -0x48) = 0;
    At<uint8_t>(this, -0x38) = 0;
    At<uint8_t>(this, -0x36) = 0;
    At<uint8_t>(this, -0x30) = 0;
    At<uint8_t>(this, -0x28) = 0;
    At<uint8_t>(this, -0x20) = 0;
    At<uint8_t>(this, -0x18) = 0;
    At<uint8_t>(this, -0x10) = 0;
}

// 0x2ea948: both dwords are loaded before either is stored, as the original does (the value may alias the object).
void cIDiffusionLightExtensionSub::SetPairP1(const void *value)
{
    uint32_t lo, hi;
    std::memcpy(&lo, value, 4);
    std::memcpy(&hi, static_cast<const uint8_t *>(value) + 4, 4);
    At<uint32_t>(this, -0x64) = lo;
    At<uint32_t>(this, -0x60) = hi;
    At<uint8_t>(this, -0x5c) = 1;
}

// 0x2ea842
void cIDiffusionLightExtensionSub::SetFloatF3(uint32_t value)
{
    At<uint32_t>(this, -0x4c) = value;
    At<uint8_t>(this, -0x48) = 1;
}

// 0x2ea749: the 8-byte head is loaded and stored before the dword at +8 is read, as in the original.
void cIDiffusionLightExtensionSub::SetVec3V4(const void *value)
{
    uint64_t head;
    std::memcpy(&head, value, 8);
    std::memcpy(&At<uint8_t>(this, -0x44), &head, 8);
    uint32_t tail;
    std::memcpy(&tail, static_cast<const uint8_t *>(value) + 8, 4);
    At<uint32_t>(this, -0x3c) = tail;
    At<uint8_t>(this, -0x38) = 1;
}

// 0x2ea8ba: the thunk and body read only the low byte of the argument (mov al, [esp+4]).
void cIDiffusionLightExtensionSub::SetByteB5(uint32_t value)
{
    At<uint8_t>(this, -0x37) = static_cast<uint8_t>(value);
    At<uint8_t>(this, -0x36) = 1;
}

// 0x2ea966
void cIDiffusionLightExtensionSub::SetFloatF8(uint32_t value)
{
    At<uint32_t>(this, -0x24) = value;
    At<uint8_t>(this, -0x20) = 1;
}

// 0x2ea87e
void cIDiffusionLightExtensionSub::SetFloatF10(uint32_t value)
{
    At<uint32_t>(this, -0x14) = value;
    At<uint8_t>(this, -0x10) = 1;
}

// 0x2ea926: the default's dwords are loaded before either is stored.
void cIDiffusionLightExtensionSub::SetPairD11(const void *value)
{
    uint32_t lo, hi;
    std::memcpy(&lo, value, 4);
    std::memcpy(&hi, static_cast<const uint8_t *>(value) + 4, 4);
    At<uint32_t>(this, -0x9c) = lo;
    At<uint32_t>(this, -0x98) = hi;
}

// 0x2ea8de
void cIDiffusionLightExtensionSub::SetPairD12(const void *value)
{
    uint32_t lo, hi;
    std::memcpy(&lo, value, 4);
    std::memcpy(&hi, static_cast<const uint8_t *>(value) + 4, 4);
    At<uint32_t>(this, -0x94) = lo;
    At<uint32_t>(this, -0x90) = hi;
}

// 0x2ea728: same copy order as SetVec3V4, without a flag.
void cIDiffusionLightExtensionSub::SetVec3D14(const void *value)
{
    uint64_t head;
    std::memcpy(&head, value, 8);
    std::memcpy(&At<uint8_t>(this, -0x88), &head, 8);
    uint32_t tail;
    std::memcpy(&tail, static_cast<const uint8_t *>(value) + 8, 4);
    At<uint32_t>(this, -0x80) = tail;
}

// 0x2ea8a2: stores the low byte, no flag.
void cIDiffusionLightExtensionSub::SetByteD15(uint32_t value)
{
    At<uint8_t>(this, -0x7c) = static_cast<uint8_t>(value);
}

// 0x2ea531: cmovne picks &P1 when the flag is nonzero, &D11 otherwise.
void *cIDiffusionLightExtensionSub::GetPointerP1OrD11()
{
    return reinterpret_cast<void *>(At<uint8_t>(this, -0x5c) != 0 ? AddressOf(this, -0x64) : AddressOf(this, -0x9c));
}

// 0x2ea4ae
float cIDiffusionLightExtensionSub::GetFloatF3OrDefault()
{
    return At<uint8_t>(this, -0x48) != 0 ? At<float>(this, -0x4c) : At<float>(this, -0x8c);
}

// 0x2ea4ee: the original writes only al on both paths, so the caller's upper eax bits stay as they were.
uint8_t cIDiffusionLightExtensionSub::GetByteB5OrD15()
{
    return At<uint8_t>(this, -0x36) != 0 ? At<uint8_t>(this, -0x37) : At<uint8_t>(this, -0x7c);
}

// 0x2ea436
void *cIDiffusionLightExtensionSub::GetVec3V4OrD14()
{
    return reinterpret_cast<void *>(At<uint8_t>(this, -0x38) != 0 ? AddressOf(this, -0x44) : AddressOf(this, -0x88));
}

} // namespace nTSSG
