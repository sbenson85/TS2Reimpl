#pragma once
#include "simantics/PrimExpression.h"
#include "simantics/TreeStack.h"

#include <cstddef>
#include <cstdint>

// The SimAntics operand reader (1.18.0.187): cTSObject::vf4_7 at 0x531700, slot 7 of the secondary subobject
// (this = the cTSObject + 4), which every primitive handler calls to read one operand. The node's operand is a
// pair (owner, id): the owner says where the value lives (literal, temps, the stack frame's parameters or
// locals, the stack object's attributes, a neighbour or relationship table...) and the id picks the value there.
//
// The object is not modelled: the reader reaches everything through the offsets below (counted from the
// subobject, `this`) and through vtable slots of the objects those fields lead to.
//   +0x18  embedded stack counter (its vtable pointer is the first dword; slot 10 gives the stack depth)
//   +0x1c, +0x20  begin/end of the vector of stack frames (4-byte pointers; slot 41 of a frame gives the element)
//   +0x3c  the context object (object + 0x40); slot 26 is called on every read
//   +0x84/+0x88  pointer/length (16-bit) of owner 0's table;  +0x8c/+0x90  owner 2's
//   +0x92  the eight 16-bit "temps" (owner 8), also read as a data index by several owners
//   +0xa2  114 16-bit attribute values (owner 3)
//   +0x194/+0x198  begin/end of a vector of 12-byte {begin, end, cap} ranges (owners 43 and 45); +0x1a0/+0x1a4 a
//          vector of 16-bit values (owners 41 and 43)
//   +0x1ac  ten 16-bit values (owner 39);  +0x1c0  the object table owner (+0x1a24/+0x1a28: begin/end of a vector of
//          object pointers, indexed by the stack object id);  +0x1c8  a 16-bit value (read with attribute 0x19)
// The stack object is found from the top frame's element (the 16-bit id at element +0xa): table[id - 1], whose
// slot 280 gives the "person data" many owners read (referred to as P).

// The game's own exception type for a missing stack frame is an unnamed enum of cTSTreeStack
// (`cTSTreeStack::<unnamed-enum-kStackErrorBase>`, ThrowInfo 0xf803fc, size 4): see TreeStack.h, where the class lives.

struct SimOperandSource {
    // 0x531700 (__thiscall, ret 0x1c). The seven stack arguments:
    //   owner      16-bit owner (low word; the table is indexed owner + 1, -1 and 0..0x33)
    //   id         the id (low word for most owners; some pass the whole dword on to slot calls)
    //   dest       a SimDataRef the operand is bound to (reset first; null: a private one is used)
    //   floatDest  a dword that is zeroed first and gets the float bits of owners 14, 15 and 17 (null: skipped)
    //   zeroed     a dword zeroed first when non-null (the slot is then overwritten by the context's slot-26 result)
    //   clearFlag  low byte: when non-zero and the owner is not a writable one (8, 9, 10, 11, 25), the dest and
    //              float destination are cleared again at the end, so the caller gets the value only
    //   arg7       passed to the object's slot 18 by owner 32
    // Returns the value in ax (the upper half of eax is not defined by the original). Bad operands throw a
    // 16-bit error code (`throw (short)n`); a missing stack frame throws the 32-bit
    // cTSTreeStack::kStackErrorNoFrame (0x3ea) of the game's own exception type.
    // Owners: -1 error 0x5b; 0 object table at +0x84; 1 and 5 table in P (+0x88, +0x90); 2 table at +0x8c; 3 attribute
    // array (+0xa2) with a notification when id 0x19 is read; 4 P's attribute array (+0xa6); 6 context slot-3 object's
    // 60-value row; 7 the literal id; 8 temps; 9 and 25 the frame element's parameters and locals; 10 the stack
    // top's object; 11 temps indirect; 13 P's +0x96; 14, 15 and 17 a float attribute of the interface 0x5a69d3d5 of
    // the object, of P, of P indexed by temps; 16, 20, 48, 49 a 16-bit read from the object P or the object returns;
    // 19, 31, 32 a 231-value table; 21 and 51 P's interface; 22 and 50 a parameter looked up through the element;
    // 23 derived values (id 0..7: time/skill-like calls); 24 a table of eleven; 26 and 47 a list of the interface
    // 0x5b267c89; 34 a context value; 35 a flag of P; 36, 37, 42, 44, 46 tables P or the object returns; 38 the
    // context's slot-16 object; 39 and 40 ten values; 41 a vector of values; 43 and 45 a vector of ranges.
    uint32_t Read(int32_t owner, uint32_t id, SimDataRef *dest, uint32_t *floatDest, uint32_t *zeroed, uint32_t clearFlag,
                  uint32_t arg7);
};

// The small helpers the reader uses, all private to it. Each binds a SimDataRef to a run of 16-bit values.
struct OperandRef : SimDataRef {
    // 0x53d9c0 (__thiscall, ret 0xc): length = ptr and length > 0 ? length : 0; index = index when 0 < index < length,
    // else 0; mPtr = ptr (the element pointer, not the run's start); range = full.
    void Bind(int16_t *ptr, int32_t index, int32_t length);
    // 0x52ccc0 (__thiscall, ret 0xc; returns this): the same, but mPtr = ptr + index when the index is valid
    // (ptr is the run's start).
    SimDataRef *MakeFromBase(int16_t *ptr, int32_t index, int32_t length);
};
static_assert(sizeof(OperandRef) == 12, "OperandRef layout");

// 0x53f7a0 (__cdecl): a float rounded to the nearest integer, halves away from zero (through double), truncating
// conversion; NaN and out-of-range values give 0x80000000.
int32_t __cdecl RoundHalfAway(float value);

// 0x53f7d0 (__cdecl, three stack arguments, caller cleans up): binds `dest` to the value run the object returns from
// its slot 7. id >= 0xc9: length 2, index 0. Otherwise, for the ids in a fixed set (1, 15, 22, 24, 30, 53, 65, 71, 73,
// 93) the slot is asked for id - 1, else for id; length 0x6c.
void __cdecl BindObjectData(void *object, int32_t id, SimDataRef *dest);
