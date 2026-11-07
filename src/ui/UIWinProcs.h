#pragma once
#include <cstddef>
#include <cstdint>

// UI window-procedure style functions (RVAs 1.18.0.187). `this` is the object the original is called with:
// for a subobject slot, the subobject's address (the vtable slot's own `ecx`), not the start of the most
// derived object. Offsets below are from that `this`.

// cGZWinGrid, primary vtable 0xe557d0 (148 slots); cGZWin is its base at +4.
class cGZWinGrid {
public:
    // Slot 13, 0x936a5c (16 bytes, ret 4): setne al of (+0xfc & mask). The original's eax keeps the mask
    // (it is `mov eax, [esp+4]` before the test); only al is the result, and callers test al.
    bool TestFlagsFC(uint32_t mask);
};

// The cIGZWinMsgBox subobject of cGZWinMsgBox, at +252 (0xfc) of the object (its primary vtable is 0xe56868).
class cGZWinMsgBoxAt252 {
public:
    // Slot 6 of this subobject (named vf252_6), 0x9529e5 (11 bytes): `this - 0xfc` becomes the object, then it
    // tail-jumps to the object's primary vtable slot 5 (at 0x14) with the object as `this`. Nothing is pushed,
    // so the result is whatever that slot returns, in eax, unchanged.
    uint32_t ForwardToSlot5();
};

// cTSWinProcGridThumbnail (its QueryInterface is in src/rz/QueryInterfaces.cpp). Three RZUnknownSlot members
// at +8, +0xc and +0x10.
class cTSWinProcGridThumbnail {
public:
    // Slot 7, 0x75de32 (ret 8): clears the three slots (+0x10, +0xc, +8, in that order) with
    // RZUnknownSlot::Assign(null), then returns true (al = 1). The two stack arguments are ignored. The original's
    // eax is the last slot's address with al set to 1; only al is the result.
    bool Vf7(uint32_t, uint32_t);
};

// cTSWinProcGrid, primary vtable 0xdf6b08 (46 slots). The child interface cITSWinProcGrid is at +8 and its
// vtable is 0xdf6bc4 (4 slots).
class cTSWinProcGrid {
public:
    // Slot 39, 0x748af4 (150 bytes, ret 8). Reads the message id at msg+8 and dispatches on it:
    //   a few ids call slot 26 with the field at +0x20 minus or plus one; others call slot 45 (this, 1, 1) or
    //   slot 45 (this, 0, 1) and then slot 3 of the child at +8. Returns true (al = 1) for the handled ids and
    //   false (al = 0) for the others. The first stack argument is not used.
    bool Vf39(uint32_t, const void *msg);
};

namespace nTSUICAS {

// cTSUICASComponent: the subobject at +0x168 (360) of its owner, so vf360_N is slot N of this subobject. The
// "owner" is the object at this - 0x168; this subobject's +0x68 is a flag byte and +0x70 is the head pointer
// of a std::list of component objects (each node: next, prev, value).
class cTSUICASComponent {
public:
    // Slot 8 of the subobject (vf360_8), 0x7594a3 (ret 4). When the flag at +0x68 is set and the owner's slot 86
    // returns true, walks the component list: for each non-null value whose slot 9 returns true, takes its slot 10
    // result (o2) and, if o2's slot 86 returns true, calls the component's slot 8 with the argument. Returns
    // nothing: the original's eax is only the last call's result on some paths and is not a result.
    void Vf360_8(uint32_t arg);
};

} // namespace nTSUICAS
