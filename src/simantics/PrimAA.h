#pragma once
#include "simantics/PrimHandlers.h"

#include <cstddef>
#include <cstdint>

// SimAntics primitive handlers, lane AA (1.18.0.187): opcode 0x70 (Effect, 0x5805c0) and opcode 0x6d (MaterialChange,
// 0x5827e0), each `handler(this = the object, frame, param)` (__thiscall, ret 8) called from cTSObject::vf332
// (0x534650). They return the branch code the interpreter takes and report bad operands by throwing a 16-bit code
// (`throw (short)n`). Everything outside the object is reached through vtable slots (numbers are vtable offset / 4).
// Notes: docs/subsystems/simantics.md ("Lane AA").
struct PrimAAObject {
    void **mVTable;       // +0x00 primary vtable (cTSObject, 0xdd2f0c)
    void **mSecondary;    // +0x04 vtable of the secondary subobject (this + 4); slot 7 reads an operand
    uint8_t mPad[0x38];   // +0x08..+0x3f
    void *mContext;       // +0x40 the context holder (slots 3, 10, 14, 24, 26, 31 ... hand out the tables)
    uint8_t mPad2[0x148]; // +0x44..+0x18b
    void *mField18C;      // +0x18c object whose slot 6 hands out an object (MaterialChange)
    uint8_t mPad3[0x30];  // +0x190..+0x1bf
    void *mField1C0;      // +0x1c0 object whose slot 6 gives an id (Effect, param bit 8)

    // 0x5805c0 (opcode 0x70, Effect; 8,635 bytes with the tail Ghidra cut). See PrimAA70.cpp for the cases.
    uint32_t Prim70(void *frame, const PrimParamView *param);

    // 0x5827e0 (opcode 0x6d, MaterialChange; 11,686 bytes).
    uint32_t Prim6D(void *frame, const PrimParamView *param);
};
static_assert(offsetof(PrimAAObject, mContext) == 0x40, "PrimAAObject layout");
static_assert(offsetof(PrimAAObject, mField18C) == 0x18c, "PrimAAObject layout");
static_assert(offsetof(PrimAAObject, mField1C0) == 0x1c0, "PrimAAObject layout");
