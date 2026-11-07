#pragma once
#include "simantics/PrimExpression.h"

#include <cstdint>

// The SimAntics primitive dispatch glue (1.18.0.187): the opcode switches that call the primitive handlers
// (src/simantics/Prim*.cpp, `handler(this = the object, frame, param)`, __thiscall ret 8), and the person's
// override of the operand reader.
//
//   cTSObject::vf332 0x534650   (this, frame, node, result*) __thiscall ret 0xc: the base switch.
//   cTSPerson::vf332 0x51e4c0   same signature: five person opcodes, everything else goes to the base.
//   cTSPerson::vf4_7 0x51b870   the operand reader (secondary slot 7, this = object + 4) for a person.
//
// `frame` is the stack frame (cTSTreeStackElem, a vtable object); `node` is the behaviour node (16 bytes):
// its slot 3 gives the 16-bit opcode (sign-extended), slot 16 the node's param object.

using PrimHandlerFn = uint32_t (*)(void *self, void *frame, void *param);

constexpr int kPrimOpcodeCount = 0x7f; // opcodes 0..0x7e have a switch entry; anything else is the default case

// Opcode -> handler. A null entry has no handler: the dispatch takes the default case (returns 0, result -1) for it,
// except for the opcodes the dispatch itself handles (0x0a and the six forwarded ones, see PrimDispatch.cpp).
struct PrimHandlerTable {
    PrimHandlerFn mByOpcode[kPrimOpcodeCount];
};

// The game's table: our handlers where they exist, OriginalFunction wrappers for the six that nobody has replaced.
const PrimHandlerTable &DefaultPrimHandlers();

using ObjectDispatchFn = uint8_t (*)(void *self, void *frame, void *node, int32_t *result);

// 0x534650 with the handler table passed in (the entry point below passes the game's).
// Returns 1 when the opcode was handled (the handler's eax is in *result), 0 for the default case (result -1).
// The original leaves the upper bits of eax undefined; callers use al.
uint8_t DispatchPrimitive(void *self, void *frame, void *node, int32_t *result, const PrimHandlerTable &table);

// The person's own handlers for opcodes 3, 0x11 and 0x1e selector 0 (lane Y's, src/simantics/PrimPerson*.cpp).
struct PersonPrimHandlers {
    PrimHandlerFn mOp03;
    PrimHandlerFn mOp11;
    PrimHandlerFn mOp1E0;
};
const PersonPrimHandlers &DefaultPersonPrimHandlers();

// 0x51e4c0 with the base dispatch and the person's handlers passed in (the entry point passes
// DispatchPrimitive with the game's table and DefaultPersonPrimHandlers()).
uint8_t DispatchPersonPrimitive(void *self, void *frame, void *node, int32_t *result, ObjectDispatchFn base,
                                const PersonPrimHandlers &handlers);

// 0x534650 and 0x51e4c0 as members of the object (the hook entries).
struct PrimDispatchObject {
    uint8_t Dispatch(void *frame, void *node, int32_t *result);
};
struct PrimDispatchPerson {
    uint8_t Dispatch(void *frame, void *node, int32_t *result);
};

// The base operand reader a person falls back to (SimOperandSource::Read, 0x531700): same arguments.
using OperandReadFn = uint32_t (*)(void *subobject, int32_t owner, uint32_t id, SimDataRef *dest, uint32_t *floatDest,
                                   uint32_t *zeroed, uint32_t clearFlag, uint32_t arg7);

// 0x51b870 (cTSPerson::vf4_7, __thiscall ret 0x1c; this = the person's cTSTreeSim subobject, object + 4). The
// arguments are those of SimOperandSource::Read (OperandReader.h). Owners 0x0c, 0x12, 0x1c, 0x1d and 0x1e are the
// person's own and read from objects the person leads to; every other owner calls the base reader (with the last
// argument 0, not the caller's). Returns the value in ax (as the original: zero-extended for the person's owners).
//   0x0c, 0x1c, 0x1d  a run of 16-bit values the person's interface (field +0x44 of the subobject, slot 63) gives
//                     through slots 53, 55 and 54 for (id, the +0x2ec holder's slot 20 value); 0x1c also bounds
//                     the value to 0..0x16 (an out-of-range stored value is clamped in place and error 0x14 thrown)
//   0x12, 0x1e        element `id` (0x1e: the object's slot 19 of id) of the 0xe7-value table the +0x2ec holder's
//                     slot 12 returns; ids above 0xe6 throw 0x21
// Errors thrown as 16-bit codes: 0x1c (the object's slot 98 is not 2), 0x1a (id above 15, or no run), 0x21, 0x02 (id
// above 7 for 0x1e), 0x14.
uint32_t PersonReadOperand(void *subobject, int32_t owner, uint32_t id, SimDataRef *dest, uint32_t *floatDest, uint32_t *zeroed,
                           uint32_t clearFlag, uint32_t arg7, OperandReadFn base);

struct PersonOperandSource {
    uint32_t Read(int32_t owner, uint32_t id, SimDataRef *dest, uint32_t *floatDest, uint32_t *zeroed, uint32_t clearFlag,
                  uint32_t arg7);
};
