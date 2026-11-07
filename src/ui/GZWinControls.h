#pragma once
#include <cstddef>
#include <cstdint>

#include "gz/cGZWin.h"

// cGZWinListBox and cGZWinTextEdit: the UI list box and text edit windows (RVAs 1.18.0.187). Both derive from
// cGZWin at +0 and have a second interface subobject at +0xf8 (248), with its own vtable. cGZWinListBox has its
// primary vtable at 0xe56b70 (190 slots) and the subobject's at 0xe56e6c (72 slots); cGZWinTextEdit has 0xe56138
// (178 slots) and 0xe56404 (100 slots). Slot N is at byte offset 4N.
//
// A function of the subobject is entered with `this` = object + 0xf8 (the subobject's own ecx), and its field
// offsets are relative to that. Where the original subtracts 0xf8 before a call, the call goes to the object's
// primary side (the primary vtable), which is what Primary() gives here.
//
// Names: vf248_N is slot N of the subobject's vtable; vfN is slot N of the primary vtable. RVAs are 1.18.0.187.

class cGZWinListBox;
class cGZWinTextEdit;

// The cIGZWinListBox subobject of cGZWinListBox, at +0xf8. Its fields are relative to the subobject.
class cGZWinListBoxAt248 {
public:
    // Slot 36, 0x95428e (19 bytes, ret 8): +0x60 = first argument (byte), +0x61 = second (byte); returns true.
    bool Vf36(uint8_t first, uint8_t second);
    // Slot 37, 0x9541ca (23 bytes, ret 8): *first = +0x60, *second = +0x61 (bytes); returns true.
    bool Vf37(uint8_t *first, uint8_t *second);
    // Slot 44, 0x954276 (24 bytes, ret 4): calls this->vf248_65 (subobject slot 65, ret 8) with the argument as its
    // first argument and 0xdeadbeef as its second. The original pushes ecx into the second argument's stack slot
    // and then overwrites that slot with 0xdeadbeef before the call. Returns the call's eax unchanged.
    uint32_t Vf44(uint32_t index);
    // Slot 52, 0x955ed0 (28 bytes, ret 4): +0x45 = value (byte), +0x46 = 0; then the primary's vf105 (no arguments).
    // Returns the call's eax unchanged.
    uint32_t Vf52(uint8_t value);
    // Slot 54, 0x955c39 (28 bytes, ret 4): +0x46 = value (byte), +0x45 = 0; then the primary's vf105. Returns the
    // call's eax unchanged.
    uint32_t Vf54(uint8_t value);

    // The primary object (this - 0xf8).
    cGZWinListBox *Primary();
    uint32_t &Dword(uint32_t offset) { return *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(this) + offset); }
    uint8_t &Byte(uint32_t offset) { return *(reinterpret_cast<uint8_t *>(this) + offset); }
};

// cGZWinListBox: the primary object. Its subobject is at +0xf8.
class cGZWinListBox : public cGZWin {
public:
    static constexpr uint32_t kPrimaryVTable = 0xe56b70;
    static constexpr uint32_t kSubobjectVTable = 0xe56e6c;

    // Slot 0, 0x9555e8 (58 bytes, ret 8). For riid 0x4132242b: if ppv is null, returns false; otherwise stores
    // this + 0xf8 (or null when this is null) in *ppv, AddRefs through the primary's slot 1 and returns true. Any
    // other riid goes to the base cGZWin::QueryInterface (0x9325d8, src/rz/QueryInterfaces.cpp).
    bool QueryInterface(uint32_t riid, void **ppv);
};

// The cIGZWinTextEdit subobject of cGZWinTextEdit, at +0xf8. Its fields are relative to the subobject.
class cGZWinTextEditAt248 {
public:
    // Slot 14, 0x94f344 (65 bytes, ret 8). Calls the subobject's slot 16 with (value, &local), where the local starts
    // at 0xff000000. When that returns true, the primary's slot 143 is called with the local; its result goes to *out
    // and the function returns true. Otherwise it returns false.
    bool Vf14(uint32_t value, uint32_t *out);
    // Slot 19, 0x94f12d (33 bytes, ret 4): *out = the primary's slot 143 called with the dword at +0x138; returns true.
    bool Vf19(uint32_t *out);
    // Slot 40, 0x94f84f (48 bytes, ret 8): *flag = bit 4 of the dword at +0x68 (byte); *out = the primary's slot 143
    // called with the dword at +0x140. Returns the call's eax (the same value as *out).
    uint32_t Vf40(uint8_t *flag, uint32_t *out);
    // Slot 44, 0x9522e6 (50 bytes, ret 4): if +0xd4 already equals value, does nothing; otherwise stores it and,
    // when the primary's cGZWin::TestFlag4000 returns true, calls the primary's slot 105 (no arguments).
    void Vf44(uint32_t value);
    // Slot 79, 0x94f14e (17 bytes, ret 4): *out = the dword at +0x19c; returns true.
    bool Vf79(uint32_t *out);
    // Slot 82, 0x95151c (27 bytes, ret 4): if (+0x68 & mask) is zero, calls the subobject's slot 83 with
    // (+0x68 | mask). Returns nothing; on the other path the original's eax is not set, so none is compared.
    void Vf82(uint32_t mask);
    // Slot 84, 0x94d3d7 (26 bytes, ret 4): if (+0x68 & mask) is nonzero, calls the subobject's slot 83 with
    // (+0x68 & ~mask). Returns nothing.
    void Vf84(uint32_t mask);

    // The primary object (this - 0xf8).
    cGZWinTextEdit *Primary();
    uint32_t &Dword(uint32_t offset) { return *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(this) + offset); }
};

// cGZWinTextEdit: the primary object. Its subobject is at +0xf8.
class cGZWinTextEdit : public cGZWin {
public:
    static constexpr uint32_t kPrimaryVTable = 0xe56138;
    static constexpr uint32_t kSubobjectVTable = 0xe56404;

    // Slot 0, 0x95079a (45 bytes, ret 8). For riid 0x231a1c57: stores this + 0xf8 (or null when this is null) in *ppv
    // with no null check on ppv, AddRefs through the primary's slot 1 and returns true. Any other riid goes to the base
    // cGZWin::QueryInterface (0x9325d8).
    bool QueryInterface(uint32_t riid, void **ppv);
    // Slot 118, 0x95149c (46 bytes, ret 4): returns the base cGZWin::SetF4Ok(value) (0x932c79, verified). If the byte
    // at +0x160 has bit 0x20 set, calls slot 105 (no arguments) afterwards.
    bool Vf118(uint32_t value);
};
