#pragma once
#include <cstdint>

// cTSPerson: a Sim. Layout facts from the disassembly of 1.18.0.187 (RVAs). Offsets are from the primary
// object (its cTSObject base at +0; primary vtable 0xdced48, 368 slots) unless marked sub-relative.
//
// Subobjects, from the vtable pointers the code loads:
//   +0x000  cTSObject / cITSObject / cIGZSerializable (primary vtable 0xdced48)
//   +0x004  cEdithObject / cTSTreeSim (vtable 0xdcf30c, 50 slots)
//   +0x008  cRZUnknown (vtable 0xdcf3d8, 6 slots)
//   +0x074  cIEdithObject (vtable 0xdcf3f4, 14 slots)
//   +0x2f0  cITSPerson (vtable 0xdcf430, 172 slots). Its slots are bodies of cTSPerson (the thunk at
//           0x519486 for GetGZCLSID adds -0x2f0 and jumps to the primary body at 0x519480), so `this` is this
//           subobject and the fields below are sub-relative: primary offset = sub-relative + 0x2f0.
//           Evidence: primary slot 341 (0x51dd10) passes primary+0x2f0 as an argument; primary slot 362
//           (0x522300) loads the vtable pointer stored at +0x2f0 and calls that subobject's slot 137.
//   The primary also holds the cTSObject subobjects used by src/ts/cTSObject.h (+0x040 and +0x1c4).
//
// Sub-relative fields read or written by the functions in cTSPersonITS below:
//   0x2b4  word (read by slot 126)
//   0x2ec  byte (read by slot 139)
//   0x356  array of words; slot 10 indexes it by its argument, slot 50 reads element 0
//   0x396  word, tested against zero by slot 30
//   0x3ea  byte; bit 0 read by slot 33, bit 1 read by slot 34 (after an arithmetic shift right)
//   0x492  word, sign-extended and shifted right by one by slot 32
//   0x524  pointer to another object. Slot 17 returns it; slots 14 and 15 and others call through it
//   0x5b2, 0x5b4, 0x5b6  words written by slots 119, 120 and 121
//   0x5f4  address returned by slot 148 (a field inside the subobject)
//   0x614, 0x618  dwords cleared by slot 169
//
// Calls through this subobject's own vtable (`this` = the subobject): slot 10 (0x28) and slot 11 (0x2c) with the
// argument lists used by slots 57, 58 and 159; slots 123 (0x1ec) and 125 (0x1f4) used by slot 124.
//
// Method names are cITSPerson slot numbers: vfN is slot N of the table above. Signatures come from `ret N` and
// from the argument pushes at each call site.

class cTSPersonITS {
public:
    // Slot 10, 0x51aa40 (ret 8). The first argument is a dword index; returns the word at sub+0x356 + index*2
    // (32-bit wrap) in ax. eax keeps the index's high word (`mov ax, ...` only changes the low word). The second
    // argument is popped and not read. Callers must pass an index whose address is inside the subobject.
    uint32_t vf10(uint32_t index, uint32_t unused);

    // Slot 30, 0x51c210 (ret). al = (word at +0x396 != 0). The rest of eax is the caller's.
    uint8_t vf30();

    // Slot 32, 0x51c160 (ret). Sign-extends the word at +0x492 and shifts it right by one (logical). al is then
    // ANDed with 1. Returns the full eax: (v & ~0xff) | (v & 1), where v is the shifted value.
    uint32_t vf32();

    // Slot 33, 0x51c0a0 (ret). al = byte at +0x3ea & 1. The rest of eax is the caller's.
    uint8_t vf33();

    // Slot 34, 0x51c080 (ret). al = (byte at +0x3ea >> 1) & 1 (sar, then and). The rest of eax is the caller's.
    uint8_t vf34();

    // Slot 50, 0x5196e0 (ret). ax = word at +0x356. The high word of eax is the caller's.
    uint16_t vf50();

    // Slot 126, 0x512e40 (ret). ax = word at +0x2b4. The high word of eax is the caller's.
    uint16_t vf126();

    // Slot 139, 0x519790 (ret). al = byte at +0x2ec. The rest of eax is the caller's.
    uint8_t vf139();

    // Slot 148, 0x519c30 (ret). Returns the address of the field at +0x5f4 (`lea eax, [ecx+0x5f4]`).
    void *vf148();

    // Slot 57, 0x519d80 (ret). Calls slot 10 of this with (0x1f, 1) and returns its full eax.
    uint32_t vf57();

    // Slot 58, 0x520a50 (ret 4). Calls slot 11 of this with (0x1f, a, 1) and returns its full eax.
    uint32_t vf58(uint32_t a);

    // Slot 121, 0x520a60 (ret 4). Stores the low word of the argument at +0x5b6. eax is left as the caller's.
    void vf121(uint32_t w);

    // Slot 124, 0x512c00 (ret 4). Calls slot 125 of this with a. If its al is nonzero, returns slot 123's full eax
    // for the same a. Otherwise clears al and returns the rest of slot 125's eax.
    uint32_t vf124(uint32_t a);

    // Slot 159, 0x51e840 (ret). Calls slot 10 of this with (0xbd, 1). If the result's word is positive, calls slot 11
    // with (0xbd, word - 1, 1). Void here: callers do not read eax.
    void vf159();

    // Slot 169, 0x512d50 (ret 4). Clears the dwords at +0x614 and +0x618. The argument is popped and not read.
    void vf169(uint32_t unused);
};
