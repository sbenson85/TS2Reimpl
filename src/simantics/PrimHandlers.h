#pragma once
#include <cstddef>
#include <cstdint>

// SimAntics primitive handlers (1.18.0.187): the functions cTSObject::vf332 (0x534650) calls, one per
// opcode, as `handler(this = the object, frame, param)` (__thiscall, ret 8). They return 1 (done, take
// the true branch) or 2/3-style codes to the interpreter, and signal bad operands by throwing a 16-bit
// error code (`throw (short)code`, a ThrowInfo of size 2 whose type is `short`).
//
// Opcode -> handler map: docs/subsystems.md ("Established by the host trace").

// A destination for a 16-bit result, built by the operand reader (0x531700) in the caller's frame:
// 12 bytes, constructed by 0x57c8d0 (zero, zero, range -32768..32767).
struct PrimShortDest {
    uint32_t mUnknown0;  // +0x00 zeroed by the constructor and by the reader; not read by the helpers
    int16_t *mTarget;    // +0x04 where the value goes (null: the operand cannot be written)
    int16_t mMin;        // +0x08 lowest value Set accepts
    int16_t mMax;        // +0x0a highest value Set accepts

    PrimShortDest *Construct();    // 0x57c8d0: zero, zero, min -32768, max 32767; returns this
    bool HasTarget() const;        // 0x57cc90: target != null (only al is set)
    bool Set(int32_t value);       // 0x57ed80 (ret 4): stores the low 16 bits when target is set and in range
};
static_assert(sizeof(PrimShortDest) == 12, "PrimShortDest layout");

// The operand block of a node as the handlers read it: XPrimParam (SimAnticsParams.h) bytes +4...
struct PrimParamView {
    const void *mVTable;  // +0x00
    uint16_t mWord4;      // +0x04
    uint16_t mWord6;      // +0x06
    uint16_t mWord8;      // +0x08
    uint16_t mWordA;      // +0x0a
    uint8_t mRest[0x0c];  // +0x0c..+0x17
};
static_assert(sizeof(PrimParamView) == 0x18, "PrimParamView layout");

// The object as the handlers see it: two vtables and the field at +0x48. Not a complete cTSObject.
struct PrimObject {
    void **mVTable;         // +0x00 primary vtable (cTSObject, 0xdd2f0c)
    void **mTreeSimVTable;  // +0x04 vtable of the cTSTreeSim subobject (0xdd34ac): slot 7 reads an operand
    uint8_t mPad[0x38];     // +0x08..+0x3f
    void *mSubobject40;     // +0x40 object whose slot 26 returns a lookup object (Prim07)
    uint8_t mPad2[4];       // +0x44
    void *mRandomOwner;    // +0x48 object whose slot 55 returns the cRZRandom to draw from

    // Slot 7 of the subobject at +4 (cTSObject::vf4_7, 0x531700, __thiscall ret 0x1c): reads the operand
    // (scope, id) and returns its value as a 16-bit word (in ax). With a destination it also fills
    // `dest` (and `floatDest`, which it zeroes first); the three trailing arguments are 0 for these callers.
    int32_t ReadOperand(uint32_t scope, uint32_t id, PrimShortDest *dest, float **floatDest);

    // Opcode 0x08, 0x5a1060 (ret 8): Random. Operand 1 (scope word +6, id word +4) is the destination,
    // operand 2 (scope +0xa, id +8) the bound. Bound 0 stores 0; otherwise a uniform value in [0, bound)
    // goes to the destination: a 16-bit destination takes the low 16 bits, a float destination the whole
    // 32-bit draw as a float. Throws 0x14 when a 16-bit store is out of range, 0x13 with no destination.
    // Returns 1.
    uint32_t Random(void *frame, const PrimParamView *param);

    // Opcode 0x07, 0x5a86b0 (ret 8). Picks a target object by the param word +4: 0 the object itself; 1 the
    // object that slot 29 of (slot 26 of the object at +0x40) returns for the frame's slot 12 word (sign-
    // extended), throwing 0x15 when that is null; anything else throws 3. Then the param word +6 must be
    // 0, 1 or 2, else it throws 3. Returns 1.
    uint32_t Prim07(void *frame, const PrimParamView *param);

    // Opcode 0x0f, 0x5a7bb0 (ret 8). Reads operand (scope word +6, id word +4); unless bit 0 of the byte at
    // param +8 is set, a nonzero 16-bit value throws 0x3e. Returns 1.
    uint32_t Prim0F(void *frame, const PrimParamView *param);
};
static_assert(offsetof(PrimObject, mRandomOwner) == 0x48, "PrimObject layout");
