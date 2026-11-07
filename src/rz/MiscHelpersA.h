#pragma once
#include "game/owned.h"
#include "rz/cRZStd.h"

#include <cstdint>

// QueryServiceValue's fallback text, LogD3DError's format ("%s": the text) and category, and the byte
// cRZStd::Construct tests for modes 4 and 5 (1 at load).
TS2_OWNED_STATIC(const char, da1088)[];
TS2_OWNED_STATIC(const char, da10a0)[];
TS2_OWNED_STATIC(const char, da10b8)[];
TS2_OWNED_STATIC(uint8_t, ff246c);

// Small helpers that each block many functions. RVAs are 1.18.0.187.

// cRZStd: rz/cRZStd.h (its constructor is in MiscHelpersA.cpp).

// The base constructor stub of cTSAudioAmbienceGlobalPrimitiveBase (vtable RVA 0xdaddbc). Only the
// vtable is stored (0x2439f4, __thiscall, no arguments, ret).
struct cTSAudioAmbienceGlobalPrimitiveBase {
    void *mVTable;
    uint32_t mField4; // +4: cleared by the one-argument constructor
    uint32_t mArg;    // +8: the one-argument constructor's argument

    static constexpr uint32_t kVTable = 0xdaddbc;

    // 0x2439f4: stores the vtable at offset 0. Nothing else is touched.
    void Construct();
    // 0x2439de (__thiscall, ret 4; src/rz/MiscHelpersA.size.cpp): clears +4, stores the argument at
    // +8 and the vtable at +0; returns this. Called by the 32 primitive constructors around 0x235209.
    cTSAudioAmbienceGlobalPrimitiveBase *ConstructWithArg(uint32_t arg);
};

// A node of an MSVC std::map/std::set, with 4-byte keys and a 4-byte mapped value at +0x14 (the
// node is 0x18 bytes). The mapped value is an object whose slot 2 (Release) is called on erase.
struct TreeNodeMapped {
    TreeNodeMapped *mLeft;   // +0
    TreeNodeMapped *mParent; // +4
    TreeNodeMapped *mRight;  // +8
    uint8_t mColor;          // +0xc
    uint8_t mIsNil;          // +0xd
    uint8_t mPad[2];         // +0xe
    uint32_t mKey;           // +0x10
    void *mValue;            // +0x14: released on erase when non-null

    static constexpr uint32_t kSize = 0x18;
};

static_assert(sizeof(TreeNodeMapped) == TreeNodeMapped::kSize, "TreeNodeMapped layout");

// The object whose tree is erased (its identity is not known; the original only passes this
// through). 0x1fc790 (__thiscall, ret 8).
struct TreeEraser {
    uint32_t mUnused;

    // Erases the subtree at node: the right subtree first (recursively), then the node's value is
    // released (slot 2, when non-null), the node is deleted (0x18 bytes), and the walk continues
    // with its left child. head is passed along unchanged. Returns this. The frame is noexcept
    // (FuncInfo flags 5) with no try blocks, so it is noexcept here too.
    void *EraseSubtree(void *head, TreeNodeMapped *node) noexcept;
};

// 0x18de70 (__cdecl, one argument, not cleared by the callee): asks the service object for a
// held object (QueryInterface-like slot 5 with identifiers 0x85522803 and 0xc55289ba), calls its
// slot 30 with the argument, releases the held object and returns the result, or the default
// 0xda1088 address when the result is null. The service must exist (the original dereferences
// the held object without checking it).
void *QueryServiceValue(uint32_t arg);

// 0x18def0 (__cdecl, three arguments): reports a bad Direct3D return code. Builds a temporary
// cRZStd(3, first, second) on the stack and calls its slot 2 (cdecl, this first) with the category
// "D3DError", 3, the format "Bad return code : %s" and QueryServiceValue(code) (the code's text).
// first and second are stored in the cRZStd as is (not identified; slot 2 passes them on to 0x1bff41).
void LogD3DError(uint32_t code, uint32_t first, uint32_t second);

// 0xb41750 (__cdecl, two arguments): asks the service object for a held object (slot 5 with
// identifiers 0xa417445e and 0x5a4), calls its slot 7 with (0xf, arg1, arg2), then releases the held
// object. Returns nothing; the original leaves whatever the last call left in eax, which is not
// reproduced.
void ServiceCallWithHeld(uint32_t arg1, uint32_t arg2);
