#include "ts/cTSPerson.h"
#include "game/vtable.h"

#include <cstdint>

namespace {

// Field access relative to the subobject (`this`), as the original code uses it.
template <typename T> T &At(const void *base, intptr_t offset)
{
    return *reinterpret_cast<T *>(reinterpret_cast<uintptr_t>(base) + offset);
}

using NoArgRet32 = uint32_t(__thiscall *)(void *);
using OneArgRet32 = uint32_t(__thiscall *)(void *, uint32_t);
using TwoArgRet32 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
using ThreeArgRet32 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);

} // namespace

uint32_t cTSPersonITS::vf10(uint32_t index, uint32_t)
{
    // `mov eax, [esp+4]` then `mov ax, [ecx+eax*2+0x356]`: the index is a full dword, and the address wraps at 32 bits.
    uint32_t offset = 0x356u + index * 2u; // 32-bit wrap, as the original's address arithmetic
    uint16_t word = *reinterpret_cast<uint16_t *>(reinterpret_cast<uintptr_t>(this) + offset);
    return (index & 0xffff0000u) | word;
}

uint8_t cTSPersonITS::vf30()
{
    return At<uint16_t>(this, 0x396) != 0;
}

uint32_t cTSPersonITS::vf32()
{
    uint32_t v = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(At<uint16_t>(this, 0x492))));
    v >>= 1;
    return (v & ~0xffu) | (v & 1u);
}

uint8_t cTSPersonITS::vf33()
{
    return At<uint8_t>(this, 0x3ea) & 1;
}

uint8_t cTSPersonITS::vf34()
{
    return static_cast<uint8_t>(static_cast<int8_t>(At<uint8_t>(this, 0x3ea)) >> 1) & 1;
}

uint16_t cTSPersonITS::vf50()
{
    return At<uint16_t>(this, 0x356);
}

uint16_t cTSPersonITS::vf126()
{
    return At<uint16_t>(this, 0x2b4);
}

uint8_t cTSPersonITS::vf139()
{
    return At<uint8_t>(this, 0x2ec);
}

void *cTSPersonITS::vf148()
{
    return reinterpret_cast<uint8_t *>(this) + 0x5f4;
}

uint32_t cTSPersonITS::vf57()
{
    // push 1; push 0x1f; call [eax+0x28] (slot 10): the callee's eax is returned.
    return VirtualCall<TwoArgRet32, 10>(this, 0x1f, 1);
}

uint32_t cTSPersonITS::vf58(uint32_t a)
{
    // push 1; push [esp+8] (a); push 0x1f; call [eax+0x2c] (slot 11): arguments are (0x1f, a, 1).
    return VirtualCall<ThreeArgRet32, 11>(this, 0x1f, a, 1);
}

void cTSPersonITS::vf121(uint32_t w)
{
    At<uint16_t>(this, 0x5b6) = static_cast<uint16_t>(w);
}

uint32_t cTSPersonITS::vf124(uint32_t a)
{
    uint32_t r = VirtualCall<OneArgRet32, 125>(this, a);
    if (static_cast<uint8_t>(r) != 0) return VirtualCall<OneArgRet32, 123>(this, a);
    // `xor al, al`: only the low byte is cleared; the rest of eax is slot 125's.
    return r & ~0xffu;
}

void cTSPersonITS::vf159()
{
    // push 1; push 0xbd; call [eax+0x28] (slot 10); movzx eax, ax; test ax, ax; jle; dec eax; push 1; push eax;
    // push 0xbd; call [edx+0x2c] (slot 11).
    uint32_t r = VirtualCall<TwoArgRet32, 10>(this, 0xbd, 1);
    uint16_t word = static_cast<uint16_t>(r);
    if (static_cast<int16_t>(word) > 0) {
        uint32_t n = static_cast<uint32_t>(word) - 1;
        VirtualCall<ThreeArgRet32, 11>(this, 0xbd, n, 1);
    }
}

void cTSPersonITS::vf169(uint32_t)
{
    At<uint32_t>(this, 0x614) = 0;
    At<uint32_t>(this, 0x618) = 0;
}
