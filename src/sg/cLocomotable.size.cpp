#include "sg/cLocomotable.h"

namespace nTSSG {

uintptr_t cLocomotableSubobject::AdjustedBy(uint32_t n) const
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(this);
    const uint32_t displacement = *reinterpret_cast<const uint32_t *>(address - 4);
    return address - displacement - n;
}

// Thunk 0x280d35 (14 bytes): sub ecx,[ecx-4]; sub ecx,0x2e0; jmp 0x259af0 (mov eax,[ecx-0x50]; ret).
uint32_t cLocomotableSubobject::Vf860_55()
{
    return *reinterpret_cast<const uint32_t *>(AdjustedBy(0x2e0) - 0x50);
}

// Thunk 0x28270c: sub ecx,[ecx-4]; sub ecx,0x1ac; jmp 0x26fbf0 (mov eax,[ecx-0x74]; ret).
uint32_t cLocomotableSubobject::Vf872_25()
{
    return *reinterpret_cast<const uint32_t *>(AdjustedBy(0x1ac) - 0x74);
}

// Thunk 0x282a1c: sub ecx,[ecx-4]; sub ecx,0x1ac; jmp 0x26fc00 (mov eax,[ecx-0x78]; ret).
uint32_t cLocomotableSubobject::Vf872_24()
{
    return *reinterpret_cast<const uint32_t *>(AdjustedBy(0x1ac) - 0x78);
}

// Thunk 0x2837ec: sub ecx,[ecx-4]; sub ecx,0x2e0; jmp 0x25b7d0 (mov eax,[ecx-0x6c]; ret).
uint32_t cLocomotableSubobject::Vf860_20()
{
    return *reinterpret_cast<const uint32_t *>(AdjustedBy(0x2e0) - 0x6c);
}

// Thunk 0x2837fa: sub ecx,[ecx-4]; sub ecx,0x218; jmp 0x25b7e0 (fld dword [ecx-0x4c]; ret). The float comes back in st0.
float cLocomotableSubobject::Vf860_126()
{
    return *reinterpret_cast<const float *>(AdjustedBy(0x218) - 0x4c);
}

// Thunk 0x283808: sub ecx,[ecx-4]; sub ecx,0x1ac; jmp 0x26fdc0 (fld dword [ecx-0x70]; ret).
float cLocomotableSubobject::Vf872_26()
{
    return *reinterpret_cast<const float *>(AdjustedBy(0x1ac) - 0x70);
}

// Thunk 0x2839d3: sub ecx,[ecx-4]; sub ecx,0x2e0; jmp 0x25b800 (mov eax,[ecx-0x58]; ret).
uint32_t cLocomotableSubobject::Vf860_39()
{
    return *reinterpret_cast<const uint32_t *>(AdjustedBy(0x2e0) - 0x58);
}

// Thunk 0x28651d: sub ecx,[ecx-4]; sub ecx,0x218; jmp 0x25dd60 (mov al,[ecx-0x2d]; ret). Only al is written.
uint8_t cLocomotableSubobject::Vf860_134()
{
    return *reinterpret_cast<const uint8_t *>(AdjustedBy(0x218) - 0x2d);
}

// Thunk 0x286fd9: sub ecx,[ecx-4]; sub ecx,0x2e0; jmp 0x25f090 (mov eax,[ecx-0x54]; ret).
uint32_t cLocomotableSubobject::Vf860_49()
{
    return *reinterpret_cast<const uint32_t *>(AdjustedBy(0x2e0) - 0x54);
}

// Thunk 0x286fe7: sub ecx,[ecx-4]; sub ecx,0x2e0; jmp 0x25f0b0 (mov eax,[ecx-0x5c]; ret).
uint32_t cLocomotableSubobject::Vf860_37()
{
    return *reinterpret_cast<const uint32_t *>(AdjustedBy(0x2e0) - 0x5c);
}

// Thunk 0x2874e8: sub ecx,[ecx-4]; sub ecx,0x2e0; jmp 0x262570 (mov eax,[ecx-0x64]; ret).
uint32_t cLocomotableSubobject::Vf860_33()
{
    return *reinterpret_cast<const uint32_t *>(AdjustedBy(0x2e0) - 0x64);
}

// Thunk 0x2874f6: sub ecx,[ecx-4]; sub ecx,0x2e0; jmp 0x262980 (mov eax,[ecx-0x30]; ret).
uint32_t cLocomotableSubobject::Vf860_68()
{
    return *reinterpret_cast<const uint32_t *>(AdjustedBy(0x2e0) - 0x30);
}

// Thunk 0x287504: sub ecx,[ecx-4]; sub ecx,0x2e0; jmp 0x2629a0 (mov eax,[ecx-0x2c]; ret).
uint32_t cLocomotableSubobject::Vf860_76()
{
    return *reinterpret_cast<const uint32_t *>(AdjustedBy(0x2e0) - 0x2c);
}

// Thunk 0x287683: sub ecx,[ecx-4]; sub ecx,0x2e0; jmp 0x263690 (mov eax,[ecx-0x60]; ret).
uint32_t cLocomotableSubobject::Vf860_35()
{
    return *reinterpret_cast<const uint32_t *>(AdjustedBy(0x2e0) - 0x60);
}

// Thunk 0x287691: sub ecx,[ecx-4]; sub ecx,0x1ac; jmp 0x274080 (lea eax,[ecx-0x6c]; ret). Returns the address, not a read.
void *cLocomotableSubobject::Vf872_27()
{
    return reinterpret_cast<void *>(AdjustedBy(0x1ac) - 0x6c);
}

} // namespace nTSSG
