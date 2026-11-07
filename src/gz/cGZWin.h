#pragma once
#include <cstddef>
#include <cstdint>

// cGZWin: the base class of the game's UI windows and widgets (a tree of windows with flags, rectangles,
// children and message handling). Its primary interfaces are cIGZWin, cIGZMessageTarget and cIGZUnknown,
// all at +0; the primary vtable is 0xe52f54 (173 slots). Slot N is at byte offset 4N.
//
// Constructor: 0x92ff2a. It stores the vtable at +0x00, zeroes +0x0c..+0x40, creates a small block (0xc
// bytes) and stores it at +0x54, constructs the member subobjects at +0x60, +0x98, +0xa0 and +0xa8, then sets
// +0x80..+0x94 to zero, +0x8c to 1 (word) and +0x8e to 1 (byte), +0xb0 to 0, +0xb4 and +0xb8 to 500, +0xbc..+0xcc
// to zero, and +0xf0 to -1. The object is larger than 0xf8; the full size and the rest of the constructor are
// not mapped yet.
//
// Fields touched by the functions below (offsets from the window, `this`):
//   +0x04  pointer to another interface object; the thunks in slots 10, 21 and 46 forward to it
//   +0x24  rectangle A (four dwords): CopyRectA copies it out; +0x24 and +0x28 are added to point coordinates
//   +0x54  pointer to a small block (created by the constructor, freed elsewhere)
//   +0x58  pointer to a child interface; slots 28 and 29 call through its vtable
//   +0x8c  byte flag; SetFlag8C sets it to 1
//   +0xc8  rectangle B, left/top/right/bottom: CopyRectB copies it out; width = +0xd0 - +0xc8, height = +0xd4 - +0xcc
//   +0xe8  flags mask; TestFlagsE8 tests it
//   +0xf0  dword; SetF0 stores it, GetF0 reads it (initially -1)
//   +0xf4  dword; GetF4 reads it, GetOpaqueF4 reads it with the top byte forced to 0xff
//
// Slot numbers are for the primary vtable 0xe52f54. RVAs are 1.18.0.187. Each member below is `this` = the
// window; the original bodies are non-virtual code reached through the vtable.
class cGZWin {
public:
    static constexpr uint32_t kVTable = 0xe52f54;
    static constexpr std::size_t kMinimumSize = 0xf8; // largest field offset used so far is +0xf4

    void SetFlag8C();                       // slot 104, 0x931862: byte at +0x8c = 1; 8 bytes
    uint32_t GetOpaqueF4();                 // slot 142, 0x931727: (+0xf4) | 0xff000000; 12 bytes
    int32_t GetWidth();                     // slot 49, 0x9317b7: +0xd0 - +0xc8; 13 bytes
    int32_t GetHeight();                    // slot 50, 0x93174e: +0xd4 - +0xcc; 13 bytes
    void SetF0(uint32_t value);             // slot 124, 0x933038: +0xf0 = arg; ret 4; 13 bytes
    void GetF0(uint32_t *out);              // slot 125, 0x931792: *out = +0xf0; ret 4; 15 bytes
    bool TestFlagsE8(uint32_t mask);        // slot 82, 0x931733: (+0xe8 & mask) != 0; ret 4; 16 bytes
    bool GetF4(uint32_t *out);              // slot 115, 0x931716: *out = +0xf4, returns true; ret 4; 17 bytes
    void CopyRectA(int32_t *out);           // slot 58, 0x9311ac: four dwords from +0x24 to out; ret 4; 18 bytes
    void CopyRectB(int32_t *out);           // slot 55, 0x931197: four dwords from +0xc8 to out; ret 4; 21 bytes
    void AddOffset(int32_t *x, int32_t *y); // slot 75, 0x933327: *x += +0x24, *y += +0x28; ret 8; 21 bytes

    // Slot 94, 0x931c61: no `this` use, takes a word and a dword, returns (hi << 16) | lo. The original is a
    // stdcall (ret 8) and is called through the vtable with a this that it ignores.
    static uint32_t __stdcall PackWords(uint16_t lo, uint32_t hi);

    // Direct helpers (not vtable slots), reached by direct calls in the original.
    // 0x933f35 (20 bytes, ret 4): this->vf83(0x4000, value), through the vtable (slot 83).
    void SetFlag4000(uint32_t value);
    // 0x933b29 (14 bytes): return this->vf82(0x4000), through the vtable (slot 82, TestFlagsE8).
    // The original's eax is 0x4000 with the low byte set to the result; only al is reproduced here.
    bool TestFlag4000();

    // Slots with small bodies (the slot numbers are the vtable slots of the primary vtable).
    // Slot 4, 0x9317d9 (64 bytes): unless flag 0x4000 is set, sets it (SetFlag4000(1)), calls slot 113 with 1 when
    // (+0xe8 & 0x10001) == 0x10001, then calls slot 109. Returns true (al = 1).
    bool Activate();
    // Slot 84, 0x9330b0 (17 bytes, ret 4): this->vf83(1, value).
    void SetFlag1(uint32_t value);
    // Slot 85, 0x9317cc (13 bytes): this->vf83(1, 0).
    void ClearFlag1();
    // Slot 98, 0x932be5 (28 bytes, ret 8): +0x90 = value; if the low byte of notify is set, calls slot 97.
    void SetF90Notify(uint32_t value, uint32_t notify);
    // Slot 140, 0x5c8be0 (13 bytes, ret 4): +0xf4 = value.
    void SetF4(uint32_t value);
    // Slot 118, 0x932c79 (15 bytes, ret 4): +0xf4 = value; returns true (al = 1).
    bool SetF4Ok(uint32_t value);
    // Slot 117, 0x9316ef (39 bytes, ret 0xc): *top = +0xf6, *middle = +0xf5, *bottom = +0xf4 (bytes). eax is left
    // as the last pointer; the function returns nothing.
    void GetF4Bytes(uint8_t *top, uint8_t *middle, uint8_t *bottom);
    // Slot 120, 0x932c18 (34 bytes, ret 0xc): +0xf4 = top << 16 | middle << 8 | bottom, from the low bytes of the
    // three stack arguments (the high bits of each dword are ignored).
    void SetF4Bytes(uint8_t top, uint8_t middle, uint8_t bottom);

    // Slot 5, 0x9330c1 (195 bytes). Tears the window down: clears flag 0x4000 (returns true at once if it is
    // not set), clears flag 0x2000 through the parent, releases the owner at +0x94, clears the two lists at
    // +0xa0 and +0xa8, calls slots 85, 23 and 113, then releases every value of the map at +0x98 and clears
    // it. Always returns true (al = 1).
    bool Teardown();

    // Never constructed here: `this` always points into an existing window.
};

// 0x9352d8 (81 bytes, cdecl, not a cGZWin member): `window` is any object with the window's vtable layout.
// Gets the child for `key` (slot 39), and if the child's flag 1 (slot 82) differs from the low byte of
// `value`, sets it (slot 83, with the full dword) and, when the low byte of `notify` is set, calls slot 105.
// Returns false when there is no child, true otherwise. The original returns al only.
bool SetChildFlagNotify(void *window, uint32_t key, uint32_t value, uint32_t notify);
