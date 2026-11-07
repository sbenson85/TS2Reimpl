#include "sg/cAnimRequest.h"

#include <cstring>

namespace nTSSG {

namespace {

// The thunks' adjustment: sub ecx,[ecx-4] is a 32-bit subtraction of the vtordisp stored at subobject - 4. The
// bodies then read and write at offsets from that pointer, so every access below is `adjusted + offset`.
uint8_t *AdjustedThis(const cAnimRequestSubobject *self)
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(self);
    const uint32_t displacement = *reinterpret_cast<const uint32_t *>(address - 4);
    return reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(static_cast<uint32_t>(address - displacement)));
}

template <typename T> void Store(uint8_t *base, int32_t offset, T value)
{
    std::memcpy(base + offset, &value, sizeof(T));
}

template <typename T> T Load(const uint8_t *base, int32_t offset)
{
    T value;
    std::memcpy(&value, base + offset, sizeof(T));
    return value;
}

} // namespace

// Thunk 0x2b6c31 (5 bytes): sub ecx,[ecx-4]; jmp 0x2b6c40. Body: mov [ecx-0xec],eax; ret 4.
void cAnimRequestSubobject::SetDwordEc(uint32_t value)
{
    Store<uint32_t>(AdjustedThis(this), -0xec, value);
}

// Thunk 0x2b6e7d: sub ecx,[ecx-4]; jmp 0x2b6e90. Body: mov [ecx-0xa8],eax; ret 4.
void cAnimRequestSubobject::SetDwordA8(uint32_t value)
{
    Store<uint32_t>(AdjustedThis(this), -0xa8, value);
}

// Thunk 0x2b6daa: sub ecx,[ecx-4]; jmp 0x2b6dc0. Body: mov [ecx-0x98],ax; ret 4 (low word only).
void cAnimRequestSubobject::SetWord98(uint16_t value)
{
    Store<uint16_t>(AdjustedThis(this), -0x98, value);
}

// Thunk 0x2b6bef: sub ecx,[ecx-4]; jmp 0x2b6c00. Body: mov [ecx-0x96],ax; ret 4 (low word only).
void cAnimRequestSubobject::SetWord96(uint16_t value)
{
    Store<uint16_t>(AdjustedThis(this), -0x96, value);
}

// Thunk 0x2b6c4d: sub ecx,[ecx-4]; jmp 0x2b6c60. Body: mov [ecx-0x94],al; ret 4.
void cAnimRequestSubobject::SetByte94(uint8_t value)
{
    Store<uint8_t>(AdjustedThis(this), -0x94, value);
}

// Thunk 0x2b6e3e: sub ecx,[ecx-4]; jmp 0x2b6e50. Body: mov [ecx-0x84],al; ret 4.
void cAnimRequestSubobject::SetByte84(uint8_t value)
{
    Store<uint8_t>(AdjustedThis(this), -0x84, value);
}

// Thunk 0x2b6e5d: sub ecx,[ecx-4]; jmp 0x2b6e70. Body: mov [ecx-0x83],al; ret 4.
void cAnimRequestSubobject::SetByte83(uint8_t value)
{
    Store<uint8_t>(AdjustedThis(this), -0x83, value);
}

// Thunk 0x2b6a6d: sub ecx,[ecx-4]; jmp 0x2b6a80. Body: mov [ecx-0x80],eax; ret 4.
void cAnimRequestSubobject::SetDword80(uint32_t value)
{
    Store<uint32_t>(AdjustedThis(this), -0x80, value);
}

// Thunk 0x2b6d78: sub ecx,[ecx-4]; jmp 0x2b6d80. Body: mov [ecx-0x58],al; ret 4.
void cAnimRequestSubobject::SetByte58(uint8_t value)
{
    Store<uint8_t>(AdjustedThis(this), -0x58, value);
}

// Thunk 0x2b6c6d: sub ecx,[ecx-4]; jmp 0x2b6c80. Body: mov [ecx-0x4c],eax; ret 4.
void cAnimRequestSubobject::SetDword4C(uint32_t value)
{
    Store<uint32_t>(AdjustedThis(this), -0x4c, value);
}

// Thunk 0x2b6d0a: sub ecx,[ecx-4]; jmp 0x2b6d20. Body: mov [ecx-0x24],eax (first); mov [ecx-0x20],eax (second); ret 8.
void cAnimRequestSubobject::SetDwords24And20(uint32_t first, uint32_t second)
{
    uint8_t *self = AdjustedThis(this);
    Store<uint32_t>(self, -0x24, first);
    Store<uint32_t>(self, -0x20, second);
}

// Thunk 0x2b6447: sub ecx,[ecx-4]; jmp 0x2b6450. Body: mov eax,[ecx-0xec]; ret.
uint32_t cAnimRequestSubobject::GetDwordEc()
{
    return Load<uint32_t>(AdjustedThis(this), -0xec);
}

// Thunk 0x2b6ed4: sub ecx,[ecx-4]; jmp 0x2b6ee0. Body: mov eax,[ecx-0xa8]; ret.
uint32_t cAnimRequestSubobject::GetDwordA8()
{
    return Load<uint32_t>(AdjustedThis(this), -0xa8);
}

// Thunk 0x2b6283: sub ecx,[ecx-4]; jmp 0x2b6290. Body: mov eax,[ecx-0x80]; ret.
uint32_t cAnimRequestSubobject::GetDword80()
{
    return Load<uint32_t>(AdjustedThis(this), -0x80);
}

// Thunk 0x2b6d4f: sub ecx,[ecx-4]; jmp 0x2b6d60. Body: [ecx-0x18]=a, [ecx-0x14]=b, [ecx-0x10]=c; ret 0xc.
void cAnimRequestSubobject::SetDwords18_14_10(uint32_t a, uint32_t b, uint32_t c)
{
    uint8_t *self = AdjustedThis(this);
    Store<uint32_t>(self, -0x18, a);
    Store<uint32_t>(self, -0x14, b);
    Store<uint32_t>(self, -0x10, c);
}

} // namespace nTSSG
