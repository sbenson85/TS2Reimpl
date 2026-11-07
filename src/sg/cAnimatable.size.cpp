#include "sg/cAnimatable.h"

#include <cstdint>

namespace nTSSG {

namespace {

// The adjusted this of every thunk: this - vtordisp - n, where vtordisp is the dword at this - 4. The arithmetic
// is done in uintptr_t so the subtraction wraps the way the original's sub does.
uintptr_t Adjusted(const void *self, uint32_t n)
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(self);
    const uint32_t displacement = *reinterpret_cast<const uint32_t *>(address - 4);
    return address - displacement - n;
}

// A field of the adjusted this. offset is the body's (negative) displacement, read as the body reads it.
template <typename T> T Field(const void *self, uint32_t n, int offset)
{
    return *reinterpret_cast<const T *>(Adjusted(self, n) + static_cast<uintptr_t>(offset));
}

} // namespace

// Thunk 0x26e018 (14 bytes): sub ecx,[ecx-4]; sub ecx,0x134; jmp 0x259af0 (mov eax,[ecx-0x50]; ret).
uint32_t cAnimatableSubobject::Vf432_55()
{
    return Field<uint32_t>(this, 0x134, -0x50);
}

// Thunk 0x26fbdf (8 bytes): sub ecx,[ecx-4]; jmp 0x26fbf0 (mov eax,[ecx-0x74]; ret).
uint32_t cAnimatableSubobject::Vf444_25()
{
    return Field<uint32_t>(this, 0, -0x74);
}

// Thunk 0x26fc04 (14 bytes): sub ecx,[ecx-4]; sub ecx,0x134; jmp 0x25b7d0 (mov eax,[ecx-0x6c]; ret).
uint32_t cAnimatableSubobject::Vf432_20()
{
    return Field<uint32_t>(this, 0x134, -0x6c);
}

// Thunk 0x26fda5 (11 bytes): sub ecx,[ecx-4]; sub ecx,0x6c; jmp 0x25b7e0 (fld dword [ecx-0x4c]; ret).
float cAnimatableSubobject::Vf432_126()
{
    return Field<float>(this, 0x6c, -0x4c);
}

// Thunk 0x26fdb0 (8 bytes): sub ecx,[ecx-4]; jmp 0x26fdc0 (fld dword [ecx-0x70]; ret).
float cAnimatableSubobject::Vf444_26()
{
    return Field<float>(this, 0, -0x70);
}

// Thunk 0x270558 (14 bytes): sub ecx,[ecx-4]; sub ecx,0x134; jmp 0x25b800 (mov eax,[ecx-0x58]; ret).
uint32_t cAnimatableSubobject::Vf432_39()
{
    return Field<uint32_t>(this, 0x134, -0x58);
}

// Thunk 0x271dac (11 bytes): sub ecx,[ecx-4]; sub ecx,0x6c; jmp 0x25dd60 (mov al,[ecx-0x2d]; ret).
// Only al is written: the upper bits of eax are whatever the caller left there, so the result is a byte.
uint8_t cAnimatableSubobject::Vf432_134()
{
    return Field<uint8_t>(this, 0x6c, -0x2d);
}

// Thunk 0x272197 (14 bytes): sub ecx,[ecx-4]; sub ecx,0x134; jmp 0x25f090 (mov eax,[ecx-0x54]; ret).
uint32_t cAnimatableSubobject::Vf432_49()
{
    return Field<uint32_t>(this, 0x134, -0x54);
}

// Thunk 0x2721a5 (14 bytes): sub ecx,[ecx-4]; sub ecx,0x134; jmp 0x25f0b0 (mov eax,[ecx-0x5c]; ret).
uint32_t cAnimatableSubobject::Vf432_37()
{
    return Field<uint32_t>(this, 0x134, -0x5c);
}

// Thunk 0x27253f (11 bytes): sub ecx,[ecx-4]; sub ecx,0x6c; jmp 0x140fe0 (ret 0, which is a folded no-op). The body
// never writes eax, so there is no result: the caller's eax is left as it was, and nothing else happens.
void cAnimatableSubobject::Vf424_10()
{
}

// Thunk 0x273339 (14 bytes): sub ecx,[ecx-4]; sub ecx,0x134; jmp 0x262570 (mov eax,[ecx-0x64]; ret).
uint32_t cAnimatableSubobject::Vf432_33()
{
    return Field<uint32_t>(this, 0x134, -0x64);
}

// Thunk 0x2737dc (14 bytes): sub ecx,[ecx-4]; sub ecx,0x134; jmp 0x262980 (mov eax,[ecx-0x30]; ret).
uint32_t cAnimatableSubobject::Vf432_68()
{
    return Field<uint32_t>(this, 0x134, -0x30);
}

// Thunk 0x2737ea (14 bytes): sub ecx,[ecx-4]; sub ecx,0x134; jmp 0x2629a0 (mov eax,[ecx-0x2c]; ret).
uint32_t cAnimatableSubobject::Vf432_76()
{
    return Field<uint32_t>(this, 0x134, -0x2c);
}

// Thunk 0x274062 (14 bytes): sub ecx,[ecx-4]; sub ecx,0x134; jmp 0x263690 (mov eax,[ecx-0x60]; ret).
uint32_t cAnimatableSubobject::Vf432_35()
{
    return Field<uint32_t>(this, 0x134, -0x60);
}

// Thunk 0x274070 (8 bytes): sub ecx,[ecx-4]; jmp 0x274080 (lea eax,[ecx-0x6c]; ret). Returns an address.
void *cAnimatableSubobject::Vf444_27()
{
    return reinterpret_cast<void *>(Adjusted(this, 0) - 0x6c);
}

} // namespace nTSSG
