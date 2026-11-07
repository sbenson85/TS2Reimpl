#include "gz/cGZWin.h"
#include "game/vtable.h"
#include "rz/RZAutoRef.h"

#include <cstring>
#include <list>
#include <map>

namespace {

template <typename T> T &At(cGZWin *self, uintptr_t offset)
{
    return *reinterpret_cast<T *>(reinterpret_cast<uint8_t *>(self) + offset);
}

// The containers at +0x98 (a map, the library's _Tree with _Myhead and _Mysize) and +0xa0/+0xa8 (lists) are
// used in place: the same MSVC 14.40 STL, so the same layout and the same clear/iterator code as the library.
// The lists at +0xa0 and +0xa8 hold reference holders (RZAutoRef, 4 bytes; the element destructor 0x142196 is
// its ReleaseUnguarded). The library's list clear (0x1444b1) destroys each element (a Release) and then frees
// its node, so the clear here releases each element first and then clears the list, in the same order.
static_assert(sizeof(std::map<uint32_t, void *>) == 8, "map header must be the 8 bytes the original uses");
static_assert(sizeof(std::list<RZAutoRef>) == 8, "list header must be the 8 bytes the original uses");

void ClearRefList(std::list<RZAutoRef> &list)
{
    for (RZAutoRef &ref : list) ref.ReleaseUnguarded();
    list.clear();
}

} // namespace

// 0x931862 (8 bytes): mov byte ptr [ecx+0x8c], 1; ret
void cGZWin::SetFlag8C()
{
    At<uint8_t>(this, 0x8c) = 1;
}

// 0x931727 (12 bytes): mov eax, [ecx+0xf4]; or eax, 0xff000000; ret
uint32_t cGZWin::GetOpaqueF4()
{
    return At<uint32_t>(this, 0xf4) | 0xff000000u;
}

// 0x9317b7 (13 bytes): mov eax, [ecx+0xd0]; sub eax, [ecx+0xc8]; ret
int32_t cGZWin::GetWidth()
{
    // Wrapping subtraction, as the machine code does (signed overflow would be undefined in C++).
    return static_cast<int32_t>(At<uint32_t>(this, 0xd0) - At<uint32_t>(this, 0xc8));
}

// 0x93174e (13 bytes): mov eax, [ecx+0xd4]; sub eax, [ecx+0xcc]; ret
int32_t cGZWin::GetHeight()
{
    return static_cast<int32_t>(At<uint32_t>(this, 0xd4) - At<uint32_t>(this, 0xcc));
}

// 0x933038 (13 bytes): mov eax, [esp+4]; mov [ecx+0xf0], eax; ret 4
void cGZWin::SetF0(uint32_t value)
{
    At<uint32_t>(this, 0xf0) = value;
}

// 0x931792 (15 bytes): mov eax, [esp+4]; mov ecx, [ecx+0xf0]; mov [eax], ecx; ret 4
void cGZWin::GetF0(uint32_t *out)
{
    *out = At<uint32_t>(this, 0xf0);
}

// 0x931733 (16 bytes): mov eax, [esp+4]; test [ecx+0xe8], eax; setne al; ret 4
// The original leaves the upper bits of eax as the mask's; only al is the result.
bool cGZWin::TestFlagsE8(uint32_t mask)
{
    return (At<uint32_t>(this, 0xe8) & mask) != 0;
}

// 0x931716 (17 bytes): mov eax, [esp+4]; mov ecx, [ecx+0xf4]; mov [eax], ecx; mov al, 1; ret 4
bool cGZWin::GetF4(uint32_t *out)
{
    *out = At<uint32_t>(this, 0xf4);
    return true;
}

// 0x9311ac (18 bytes): copies four dwords from +0x24 to the output, with movsd (no uninitialised reads)
void cGZWin::CopyRectA(int32_t *out)
{
    std::memcpy(out, reinterpret_cast<uint8_t *>(this) + 0x24, 16);
}

// 0x931197 (21 bytes): copies four dwords from +0xc8 to the output, with movsd
void cGZWin::CopyRectB(int32_t *out)
{
    std::memcpy(out, reinterpret_cast<uint8_t *>(this) + 0xc8, 16);
}

// 0x933327 (21 bytes): mov eax, [esp+4]; mov edx, [ecx+0x24]; add [eax], edx;
//                      mov eax, [esp+8]; mov ecx, [ecx+0x28]; add [eax], ecx; ret 8
void cGZWin::AddOffset(int32_t *x, int32_t *y)
{
    *x = static_cast<int32_t>(static_cast<uint32_t>(*x) + At<uint32_t>(this, 0x24));
    *y = static_cast<int32_t>(static_cast<uint32_t>(*y) + At<uint32_t>(this, 0x28));
}

// 0x931c61 (17 bytes): mov eax, [esp+8]; movzx ecx, word ptr [esp+4]; shl eax, 16; or eax, ecx; ret 8
uint32_t __stdcall cGZWin::PackWords(uint16_t lo, uint32_t hi)
{
    return (hi << 16) | lo;
}

// 0x933f35 (20 bytes): push [esp+4]; mov eax, [ecx]; push 0x4000; call [eax+0x14c]; ret 4
void cGZWin::SetFlag4000(uint32_t value)
{
    VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t), 83>(this, 0x4000u, value);
}

// 0x933b29 (14 bytes): mov eax, [ecx]; push 0x4000; call [eax+0x148]; ret
bool cGZWin::TestFlag4000()
{
    return VirtualCall<bool(__thiscall *)(void *, uint32_t), 82>(this, 0x4000u);
}

// 0x9330c1 (195 bytes). The original's sequence, with the same virtual calls in the same order.
bool cGZWin::Teardown()
{
    if (!TestFlag4000()) return true;
    SetFlag4000(0);

    if (At<uint32_t>(this, 0xe8) & 0x2000u) {
        // Parent at +4; slot 28 is called with this as its single stack argument (the callee pops it).
        void *parent = At<void *>(this, 0x04);
        VirtualCall<void(__thiscall *)(void *, void *), 28>(parent, this);
        At<uint32_t>(this, 0xe8) &= ~0x2000u;
    }

    void *owner = At<void *>(this, 0x94);
    if (owner) {
        VirtualCall<uint32_t(__thiscall *)(void *), 2>(owner); // Release
        At<void *>(this, 0x94) = nullptr;
    }

    // The original calls the library list clear (0x1444b1) for both lists.
    ClearRefList(At<std::list<RZAutoRef>>(this, 0xa0));
    ClearRefList(At<std::list<RZAutoRef>>(this, 0xa8));

    VirtualCall<void(__thiscall *)(void *), 85>(this);
    VirtualCall<void(__thiscall *)(void *), 23>(this);
    VirtualCall<void(__thiscall *)(void *, uint32_t), 113>(this, 0u);

    // Release each value of the map at +0x98 in key order (null values are skipped), then clear the map.
    auto &map = At<std::map<uint32_t, void *>>(this, 0x98);
    for (auto it = map.begin(); it != map.end(); ++it) {
        void *value = it->second;
        if (value) VirtualCall<uint32_t(__thiscall *)(void *), 2>(value); // Release
    }
    map.clear();
    return true;
}

// 0x9352d8 (81 bytes): push ebp; mov ecx, [ebp+8]; push [ebp+0xc]; mov eax, [ecx]; call [eax+0x9c] (child);
// test; child->vf82(1) (al); cmp al, byte [ebp+0x10]; if different: child->vf83(1, [ebp+0x10]), and if
// byte [ebp+0x14] is set, child->vf105(). Returns al = 1 when a child was found, else 0.
bool SetChildFlagNotify(void *window, uint32_t key, uint32_t value, uint32_t notify)
{
    void *child = VirtualCall<void *(__thiscall *)(void *, uint32_t), 39>(window, key);
    if (!child) return false;
    bool flag = VirtualCall<bool(__thiscall *)(void *, uint32_t), 82>(child, 1u);
    if (static_cast<uint8_t>(flag) != static_cast<uint8_t>(value)) {
        VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t), 83>(child, 1u, value);
        if (static_cast<uint8_t>(notify) != 0) VirtualCall<void(__thiscall *)(void *), 105>(child);
    }
    return true;
}

// 0x9317d9 (64 bytes): slot 4
bool cGZWin::Activate()
{
    if (!TestFlag4000()) {
        SetFlag4000(1);
        if ((At<uint32_t>(this, 0xe8) & 0x10001u) == 0x10001u)
            VirtualCall<void(__thiscall *)(void *, uint32_t), 113>(this, 1u);
        VirtualCall<void(__thiscall *)(void *), 109>(this);
    }
    return true;
}

// 0x9330b0 (17 bytes): push [esp+4]; mov eax, [ecx]; push 1; call [eax+0x14c]; ret 4
void cGZWin::SetFlag1(uint32_t value)
{
    VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t), 83>(this, 1u, value);
}

// 0x9317cc (13 bytes): mov eax, [ecx]; push 0; push 1; call [eax+0x14c]; ret
void cGZWin::ClearFlag1()
{
    VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t), 83>(this, 1u, 0u);
}

// 0x932be5 (28 bytes): cmp byte [esp+8], 0; mov eax, [esp+4]; mov [ecx+0x90], eax; je; mov eax, [ecx]; call [eax+0x184]; ret 8
void cGZWin::SetF90Notify(uint32_t value, uint32_t notify)
{
    At<uint32_t>(this, 0x90) = value;
    if (static_cast<uint8_t>(notify) != 0) VirtualCall<void(__thiscall *)(void *), 97>(this);
}

// 0x5c8be0 (13 bytes): mov eax, [esp+4]; mov [ecx+0xf4], eax; ret 4
void cGZWin::SetF4(uint32_t value)
{
    At<uint32_t>(this, 0xf4) = value;
}

// 0x932c79 (15 bytes): mov eax, [esp+4]; mov [ecx+0xf4], eax; mov al, 1; ret 4
bool cGZWin::SetF4Ok(uint32_t value)
{
    At<uint32_t>(this, 0xf4) = value;
    return true;
}

// 0x9316ef (39 bytes): byte copies from +0xf6, +0xf5 and +0xf4 to the three pointers, in that order
void cGZWin::GetF4Bytes(uint8_t *top, uint8_t *middle, uint8_t *bottom)
{
    *top = At<uint8_t>(this, 0xf6);
    *middle = At<uint8_t>(this, 0xf5);
    *bottom = At<uint8_t>(this, 0xf4);
}

// 0x932c18 (34 bytes): movzx of each low byte, packed as top << 16 | middle << 8 | bottom
void cGZWin::SetF4Bytes(uint8_t top, uint8_t middle, uint8_t bottom)
{
    At<uint32_t>(this, 0xf4) = (static_cast<uint32_t>(top) << 16) | (static_cast<uint32_t>(middle) << 8) | bottom;
}
