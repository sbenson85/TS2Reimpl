#pragma once
#include "simantics/PrimExpression.h"
#include "simantics/PrimHandlers.h"

#include <cstddef>
#include <cstdint>

// SimAntics primitive handlers, mid-sized ones (1.18.0.187): the functions cTSObject::vf332 (0x534650) calls for
// opcodes 0x00, 0x0b, 0x0c, 0x14, 0x19, 0x20, 0x21, 0x30, 0x7a and 0x7e, each `handler(this = the object, frame,
// param)` (__thiscall, ret 8). Opcode -> handler map: docs/subsystems.md ("Established by the host trace").
// They return the branch code the interpreter takes (0 false, 1 true, 2 yield, 3) and report bad operands by throwing
// a 16-bit code (`throw (short)code`).
//
// The objects are reached only through vtable slots (the slot numbers are the vtable offsets / 4); what each slot
// does is mostly unknown, so the code names the role it plays in the handler.

constexpr int kPrimReadSlot = 7; // secondary slot 7: the operand reader (cTSObject::vf4_7 0x531700, 7 stack arguments)

// The object as these handlers see it. Same prefix as PrimObject (PrimHandlers.h): the secondary subobject at +4 (its
// vtable slot 7 reads an operand), the field at +0x40 and the one at +0x48.
struct PrimMidObject {
    void **mVTable;         // +0x00 primary vtable (cTSObject, 0xdd2f0c)
    void **mSecondary;      // +0x04 vtable of the secondary subobject (this + 4 is that subobject)
    uint8_t mPad[0x38];     // +0x08..+0x3f
    void *mContext;         // +0x40 a holder: the object it points to (0x57cc80 reads the pointer) is the context
    uint8_t mPad2[4];       // +0x44
    void *mOwner;           // +0x48

    // 0x5988a0 (opcode 0x00, Idle). The counter is the frame's 16-bit value number param word +4 (error 8 when that
    // is negative or not below the frame's slot 26 count). Without a ticking context (slot 14 of the context's slot-3
    // object & 6 == 0) it returns 1 once the counter is 0, else 2. With one it counts the counter down once per call
    // through the table's slots 71/73 (id from this slot 47), returning 1 at 0 and 2 otherwise.
    uint32_t Idle(void *frame, const PrimParamView *param);

    // 0x588eb0 (opcode 0x0b, DistanceTo). Reads operand A (scope/id from the param via the 0x57e590/0x57e5d0 rules)
    // as the id of one object and the frame's slot 12 as the other (error 0x17 when either is unknown, 2 for a bad
    // data number), asks the first object's slot 128 for the distance to the second (a float argument from the game
    // global 0x1014404) and stores it in this's data cell word +4: the float truncated, or times 100 when param byte
    // +0xa bit 1 is set. Returns 1.
    uint32_t DistanceTo(void *frame, const PrimParamView *param);

    // 0x588bb0 (opcode 0x0c, DirectionTo). Reads the destination operand, then two object ids (errors 0x17/0x15);
    // stores the direction from the first object's position to the second's: an octant 0..7 (0x460400), or with
    // param byte +0xc bit 1 a bearing in degrees 0..359 from atan2. Error 0x14 when the store is refused. Returns 1.
    uint32_t DirectionTo(void *frame, const PrimParamView *param);

    // 0x580360 (opcode 0x14). Looks up the person object by the frame's slot 12 (error 0x15), maps param word +4
    // (0..0x1b except 0x18) to a motive/need index, asks the object for its value (slots 93/94) and, when that is
    // nonzero, calls this's slot 248 with it. Returns 3 when that call says yes, else 0.
    uint32_t Prim14(void *frame, const PrimParamView *param);

    // 0x5801b0 (opcode 0x19, Budget). Computes an amount (a read operand times a multiplier, or two data cells as
    // hundreds of thousands) and charges (bit 1 clear) or credits it against the household funds the context's
    // slot-3 object keeps. Returns 0 when a charge exceeds the funds, else 1. Error 0x38 when param byte +0xa > 10.
    uint32_t Budget(void *frame, const PrimParamView *param);

    // 0x5a79f0 (opcode 0x20). Compares an id (param dword +4, or a lookup when it equals 0x4c7cab2b) with what the
    // object found by a chain of lookups reports; with param byte +0xb bit 2 it instead sets this's data cells
    // 0/1 from the id. Returns 1/0.
    uint32_t Prim20(void *frame, const PrimParamView *param);

    // 0x58a110 (opcode 0x21). Takes an interface (IID 0x5a69d3d5) from this or from the object the secondary slot 20
    // names (param word +8 mode 0/1; mode 2 none), requires its owner's slot 8/98 to say 2 (error 0xa), param word
    // +0xa in 0..2 (error 0x1e), sorts the eight ids {5..9, 0xd, 0xe, 0xf} by the float table its slot 15 returns
    // and stores the first five into this's data cells 0..4. Returns 1.
    uint32_t Prim21(void *frame, const PrimParamView *param);

    // 0x5a87f0 (opcode 0x23). A two-phase primitive on the frame's state (frame slot 18: 0 not started, 1 running,
    // else done with 1). Not started: begins it (context object slots 15/16, frame slot 19(1)) and yields 2.
    // Running: looks the object up (error 0x17), updates the context state and the cached service
    // (GetCachedSingletonB), asks the object's helper (slot 12, a virtual-base reference) and, when a
    // virtual-base interface of the source object (0x211da3 chain) answers, steps its counter (slot 5) by
    // param byte +7 (1: to 0, 2: half, else one less). Returns 2 (1 when the service refuses, 2 on the
    // short flag-1 path).
    uint32_t Prim23(void *frame, const PrimParamView *param);

    // 0x59d590 (opcode 0x30). Looks up an object by id (param word +4 selects the frame's slot 12 or this's slot
    // 47), asks the COM director (0x1ae18d) for an interface (0xcbc79579, 0xbc7957d) and, if present, notifies it
    // (slots 5 and 12) and the object 0x9c5b7e returns. Returns 1.
    uint32_t Prim30(void *frame, const PrimParamView *param);

    // 0x59efa0 (opcode 0x7a, OnTimer). Looks up the object by the frame's slot 12 (error 0x17) and asks it (slots
    // 301/302/303 by param byte +0x13) to set a timer built from the param: three data values read from operands
    // or the frame, the timer id, flags. Stores the result in this's data cell 0 in the first case. Returns 1.
    uint32_t OnTimer(void *frame, const PrimParamView *param);

    // 0x5a1260 (opcode 0x7e). Forwards to slot 7 of the object slot 67 of the object at +0x48 returns, passing
    // (this, frame, param); returns 0 when there is none.
    uint32_t Prim7E(void *frame, const PrimParamView *param);
};
static_assert(offsetof(PrimMidObject, mContext) == 0x40, "PrimMidObject layout");
static_assert(offsetof(PrimMidObject, mOwner) == 0x48, "PrimMidObject layout");

// A fixed-point position: two 16.16 coordinates.
struct SimFixedPoint {
    int32_t mX;
    int32_t mY;

    // 0x460400 (__thiscall, ret 4): the octant (0..7) of the direction from this position to `to`. Both
    // positions are scaled by 2^-16 to floats; the rest is a comparison of |dy|, |dx| against tan(22.5 degrees).
    int32_t Octant(const SimFixedPoint *to) const;
};
static_assert(sizeof(SimFixedPoint) == 8, "SimFixedPoint layout");

// A 16.16 fixed-point value.
struct SimFixed {
    int32_t mRaw;
    // 0x57f290 (__thiscall, 26 bytes): the value as a float times 2^-16 (the int converted first), returned in st0.
    float ToFloat() const;
};
static_assert(sizeof(SimFixed) == 4, "SimFixed layout");

// 0x275390 (__cdecl): atan2 of two floats through the double-precision CRT function, rounded back to a float
// (returned in st0). atan2(first, second).
float Atan2Float(float y, float x);

// The float table 0x58a110 leaves for the comparator (the game global 0x10dbf50).
// 0x5a9330 (__cdecl): the qsort comparator over ids: -1 if table[*a] - table[*b] < -0.01, 1 if > 0.01, else 0.
int __cdecl RankCompare(const int32_t *a, const int32_t *b);
