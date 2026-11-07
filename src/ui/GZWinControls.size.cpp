#include "ui/GZWinControls.h"

#include "game/vtable.h"
#include "rz/QueryInterfaces.h"

namespace {

// The subobject is at +0xf8 of its object; the primary object is at the subobject's this - 0xf8.
constexpr uint32_t kSubobjectOffset = 0xf8;

// Slot N of a thiscall vtable entry, with `this` as the first parameter (the stack arguments follow).
using ThisFn0 = uint32_t(__thiscall *)(void *);
using ThisFn1 = uint32_t(__thiscall *)(void *, uint32_t);
using ThisFn2 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);

} // namespace

// ---------------------------------------------------------------------------------------------------------
// cGZWinListBoxAt248 (subobject of cGZWinListBox)

cGZWinListBox *cGZWinListBoxAt248::Primary()
{
    return reinterpret_cast<cGZWinListBox *>(reinterpret_cast<uint8_t *>(this) - kSubobjectOffset);
}

// 0x95428e (ret 8): stores both bytes, then al = 1.
bool cGZWinListBoxAt248::Vf36(uint8_t first, uint8_t second)
{
    Byte(0x60) = first;
    Byte(0x61) = second;
    return true;
}

// 0x9541ca (ret 8): reads both bytes out through the pointers, then al = 1.
bool cGZWinListBoxAt248::Vf37(uint8_t *first, uint8_t *second)
{
    *first = Byte(0x60);
    *second = Byte(0x61);
    return true;
}

// 0x954276 (ret 4): this->vf248_65(index, 0xdeadbeef). The original's `push ecx` slot becomes the second argument.
uint32_t cGZWinListBoxAt248::Vf44(uint32_t index)
{
    return VirtualCall<ThisFn2, 65>(this, index, 0xdeadbeef);
}

// 0x955ed0 (ret 4): +0x45 = value, +0x46 = 0, then the primary's slot 105.
uint32_t cGZWinListBoxAt248::Vf52(uint8_t value)
{
    Byte(0x45) = value;
    Byte(0x46) = 0;
    cGZWinListBox *primary = Primary();
    return VirtualCall<ThisFn0, 105>(primary);
}

// 0x955c39 (ret 4): +0x46 = value, +0x45 = 0, then the primary's slot 105.
uint32_t cGZWinListBoxAt248::Vf54(uint8_t value)
{
    Byte(0x46) = value;
    Byte(0x45) = 0;
    cGZWinListBox *primary = Primary();
    return VirtualCall<ThisFn0, 105>(primary);
}

// ---------------------------------------------------------------------------------------------------------
// cGZWinListBox (primary)

// 0x9555e8 (ret 8). For the subobject's riid, a null ppv gives false with no write and no AddRef. The stored
// pointer is this + 0xf8, or null when this is null (the original masks it with `neg`/`sbb`).
bool cGZWinListBox::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != 0x4132242b) return qi::GZWinQueryInterface(this, nullptr, riid, ppv);
    if (!ppv) return false;
    *ppv = reinterpret_cast<uintptr_t>(this) ? reinterpret_cast<uint8_t *>(this) + kSubobjectOffset : nullptr;
    VirtualCall<ThisFn0, 1>(this);
    return true;
}

// ---------------------------------------------------------------------------------------------------------
// cGZWinTextEditAt248 (subobject of cGZWinTextEdit)

cGZWinTextEdit *cGZWinTextEditAt248::Primary()
{
    return reinterpret_cast<cGZWinTextEdit *>(reinterpret_cast<uint8_t *>(this) - kSubobjectOffset);
}

// 0x94f344 (ret 8). The local is the callee's output for the slot-16 call; its starting value is the original's.
bool cGZWinTextEditAt248::Vf14(uint32_t value, uint32_t *out)
{
    uint32_t local = 0xff000000;
    if (!VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t *), 16>(this, value, &local)) return false;
    cGZWinTextEdit *primary = Primary();
    *out = VirtualCall<ThisFn1, 143>(primary, local);
    return true;
}

// 0x94f12d (ret 4).
bool cGZWinTextEditAt248::Vf19(uint32_t *out)
{
    uint32_t value = Dword(0x138);
    cGZWinTextEdit *primary = Primary();
    *out = VirtualCall<ThisFn1, 143>(primary, value);
    return true;
}

// 0x94f84f (ret 8): the byte is written before the call, and eax is the call's result.
uint32_t cGZWinTextEditAt248::Vf40(uint8_t *flag, uint32_t *out)
{
    *flag = static_cast<uint8_t>((Dword(0x68) >> 4) & 1);
    uint32_t value = Dword(0x140);
    cGZWinTextEdit *primary = Primary();
    uint32_t result = VirtualCall<ThisFn1, 143>(primary, value);
    *out = result;
    return result;
}

// 0x9522e6 (ret 4).
void cGZWinTextEditAt248::Vf44(uint32_t value)
{
    if (Dword(0xd4) == value) return;
    Dword(0xd4) = value;
    cGZWinTextEdit *primary = Primary();
    if (primary->TestFlag4000()) VirtualCall<ThisFn0, 105>(primary);
}

// 0x94f14e (ret 4).
bool cGZWinTextEditAt248::Vf79(uint32_t *out)
{
    *out = Dword(0x19c);
    return true;
}

// 0x95151c (ret 4): a set of the flags in mask that are not all already set goes to slot 83 of this subobject.
void cGZWinTextEditAt248::Vf82(uint32_t mask)
{
    uint32_t flags = Dword(0x68);
    if (mask & flags) return;
    VirtualCall<void(__thiscall *)(void *, uint32_t), 83>(this, flags | mask);
}

// 0x94d3d7 (ret 4): the flags in mask that are set are cleared through slot 83 of this subobject.
void cGZWinTextEditAt248::Vf84(uint32_t mask)
{
    uint32_t flags = Dword(0x68);
    if (!(flags & mask)) return;
    VirtualCall<void(__thiscall *)(void *, uint32_t), 83>(this, ~mask & flags);
}

// ---------------------------------------------------------------------------------------------------------
// cGZWinTextEdit (primary)

// 0x95079a (ret 8). No null check on ppv for the subobject's riid (the original writes through it).
bool cGZWinTextEdit::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != 0x231a1c57) return qi::GZWinQueryInterface(this, nullptr, riid, ppv);
    *ppv = reinterpret_cast<uintptr_t>(this) ? reinterpret_cast<uint8_t *>(this) + kSubobjectOffset : nullptr;
    VirtualCall<ThisFn0, 1>(this);
    return true;
}

// 0x95149c (ret 4): the base SetF4Ok result is kept in bl; slot 105 runs afterwards when bit 0x20 of +0x160 is set.
bool cGZWinTextEdit::Vf118(uint32_t value)
{
    bool result = SetF4Ok(value);
    if (reinterpret_cast<uint8_t *>(this)[0x160] & 0x20) VirtualCall<ThisFn0, 105>(this);
    return result;
}
