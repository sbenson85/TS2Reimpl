#pragma once
#include "simantics/PrimHandlers.h"

#include <cstddef>
#include <cstdint>

// SimAntics primitive handlers, lane X (1.18.0.187): opcode 0x73 (Debug, 0x586560) and opcode 0x2e (Snap, 0x5a5210),
// each `handler(this = the object, frame, param)` (__thiscall, ret 8) called from cTSObject::vf332 (0x534650). They
// return the branch code the interpreter takes and report bad operands by throwing a 16-bit code (`throw (short)n`).
// Docs: docs/subsystems.md ("Established by the host trace"). The objects are reached through vtable slots only (the
// slot numbers are vtable offsets / 4).
struct PrimXObject {
    void **mVTable;       // +0x00 primary vtable (cTSObject, 0xdd2f0c)
    void **mSecondary;    // +0x04 vtable of the secondary subobject (this + 4); slot 7 reads an operand
    uint8_t mPad[0x38];   // +0x08..+0x3f
    void *mContext;       // +0x40 the context holder: slot 3 the settings object, 6 the person list, 17, 24, 26 lists
    uint32_t mField44;    // +0x44 non-zero while the object is driven by the debug console (Debug only prints then)

    // 0x586560 (opcode 0x73, Debug; 4,987 bytes). Does nothing (returns 1) unless the cached singleton (0x20ddfb) says
    // slot 31 yes. Builds the object's text (its slot 61 object's name + ", ID n") and the person's (found in the
    // context's slot-26 list by the frame's id), then, when this+0x44 is set and the settings object's slot 15 (0x21)
    // has bit 3, runs the sub-command in param byte +0x11: 0 and 6 format a string-table text with four operands (0:
    // built, not sent; 6: sent to the debug console, the cached service 0xb38f10, slot 7); 1 registers a log window
    // (the log service holder 0x1bf8fb: slot 12 lookup, "logFilter"/"logWindow" console commands, slot 9 group
    // calls); 2 toggles the person's flag 0x8000 and prints "displayAnimBillboard on/off"; 4 "showbones <id>"; 5
    // toggles the person's flag 0x4000; 7 (also when the gate fails) "dumpHappy "<name>"". Always returns 1.
    uint32_t Prim73(void *frame, const PrimParamView *param);

    // 0x5a5210 (opcode 0x2e, Snap; 5,466 bytes).
    uint32_t Prim2E(void *frame, const PrimParamView *param);
};
static_assert(offsetof(PrimXObject, mContext) == 0x40, "PrimXObject layout");
static_assert(offsetof(PrimXObject, mField44) == 0x44, "PrimXObject layout");
