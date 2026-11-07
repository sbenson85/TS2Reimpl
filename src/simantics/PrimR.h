#pragma once
#include "simantics/PrimExpression.h"
#include "simantics/PrimHandlers.h"

#include <cstddef>
#include <cstdint>

// SimAntics primitive handlers, lane R (1.18.0.187): functions cTSObject::vf332 (0x534650) calls for opcodes 0x0d,
// 0x0e, 0x10, 0x12, 0x1a and 0x1c, each `handler(this = the object, frame, param)` (__thiscall, ret 8). Opcode ->
// handler map: docs/subsystems.md ("Established by the host trace"). They return the branch code the interpreter
// takes (0 false, 1 true, 2 yield, 3) and report bad operands by throwing a 16-bit code (`throw (short)code`).
//
// The objects are reached only through vtable slots (slot = vtable offset / 4); what most slots do is unknown, so
// the code names the role each plays in the handler.

// The object as these handlers see it (same prefix as PrimMidObject): the secondary subobject at +4 (its vtable
// slot 7 reads an operand), the holder at +0x40 and the field at +0x48.
struct PrimRObject {
    void **mVTable;         // +0x00 primary vtable (cTSObject, 0xdd2f0c)
    void **mSecondary;      // +0x04 vtable of the secondary subobject (this + 4 is that subobject)
    uint8_t mPad[0x38];     // +0x08..+0x3f
    void *mContext;         // +0x40 a holder: the pointer in it (0x57cc80 reads it) is the context object
    uint8_t mPad2[4];       // +0x44
    void *mOwner;           // +0x48

    // 0x59e6e0 (opcode 0x1a). Two operands name object ids A and B (param byte +6 / word +7, byte +9 / word
    // +0xa). The objects they stand for are found by param byte +5: bit 1 asks the context's slot-10 manager for
    // them directly (slot 16, error 0x1c when either is missing); bit 3 looks A up in this's table (slot 63's slot
    // 29, error 0x17) and asks its slot 109; otherwise both are looked up (0x17) and, when both report state 2
    // (slot 98), each one's slot-61 object names (slot 28) a manager object whose slot 8 gives the new A / B and
    // slot 12 the target (else the first one's slot 109 is the target). The target (error 0x68 when none) is asked
    // for the count at B (slot 5): when it is above param byte +4 it is stored into the operand at byte +0xc /
    // word +0xd (slot 9 into a SimDataRef, error 0x13 when it has no storage, 0x14 when refused; with bit 2 slot 10
    // instead, 0x14 when it says no) and the result is 1. Otherwise the result is 0, after a slot-6 call that
    // raises the count to byte +4 + 1 (and a second look) unless bit 0 is set.
    uint32_t Prim1A(void *frame, const PrimParamView *param);

    // 0x53de40 (opcode 0x1c, looks like "run a tree by name"). When the frame's slot 18 says 1 it returns whether
    // this's secondary slot 29 says yes. Otherwise it loads a string from the string table (0x1d0df0: group 0x12f,
    // an id from param byte +8 (| 0x100 under bit 6 of the word at +6), a key that depends on bits 0/1 of that
    // word), trims trailing blanks (empty: result 0), picks the objects by param byte +9 (0/1/2: the registry
    // entry the frame's slot 12 numbers, its slot 280 object; 3: the tree-set entry keyed by this's slot 19 words;
    // else error 0x2c; 0x15 for missing objects, 0x7a for the missing entry), asks the tree object (slot 31) for
    // the id of the tree of that name, gathers up to four arguments (word bit 5: the frame's; bit 4: three operands)
    // and starts it: through an interface of the caller (modes 0 and 1, slots 47 and the interface's slot 15), or
    // through this's slots 247/248. Returns 3 when started (and tells the frame, slot 19), else 0.
    uint32_t Prim1C(void *frame, const PrimParamView *param);

    // 0x59cba0 (opcode 0x12, looks like "delete object"). Param word +4 (0..2, else error 9) picks the id: this's
    // slot 47, the frame's slot 12, or 0. The context's slot-26 table (slot 29) gives the object (none: result 0).
    // Objects whose type id (slot 61's slot 28) is in the function-local hash set at 0x10dbf54 (seven ids, filled on
    // first use) are left alone (result 0). Otherwise: the object's position is read (slot 41), this's slot-63 manager
    // is told (slot 18: id, !(param byte +6 bit 1)), objects whose slot 116 says yes go through 0x5416a0, the
    // developer log "OETreeDelete.xls" is written when the context's slot 31 says yes, an object with interface
    // 0x5a69d3d5 is handed to the context's slot-6 object (slot 29), and the result is 2 when the id is this object's
    // own and its interface 0x99c7f5a (+4 subobject slot 33) says yes and param byte +6 bit 0 is clear, else 1.
    uint32_t Prim12(void *frame, const PrimParamView *param);

    // 0x5a0560 (opcode 0x0d, looks like "push interaction"). The caller person is the frame's slot-12 object (error
    // 0x15), which must report state 2 (slot 98) and offer interface 0x5a69d3d5 (0x1c). The target object comes from
    // a frame data cell, the interface's slot 10, or a frame argument (param byte +7 bit 1 / byte +0x12 bit 1; errors
    // 0x33, 8, 0x17), the interaction index from an operand, the caller's slot 13 or param byte +4 (the list from the
    // target's slot 87, errors 0x1f, 0x16), the priority from param byte +6 (0: the own interface's slot 10, 1: 100,
    // 2: 20, 3: 50, else 0x2d). A cEdithObjectTestSim and the director's 0xfc5ecc3d service are set up with those,
    // checks run (reason codes 1..6 are stored through this's slot 14 with 0x34 when the push is refused, result 0)
    // and the caller interface's slot 122 queues it (result 1).
    uint32_t Prim0D(void *frame, const PrimParamView *param);

    // 0x58a2c0 (opcode 0x0e, looks like "find best object for a function"). Param word +4 (the primitive's kind,
    // 0..0x1b with gaps, others return 0) picks the object function number and a rating cell. The objects registered
    // for that function (a std::list-like container the registry at this + 0x1c4 returns from its slot 32) are scored
    // one by one: skipped when flagged (slot 25 with 0x10000/0x20000, data cells 0x38/0x6b/0x19, the optional
    // allowed-number set the own interface fills, slots 129/190/135), rated by the cell (this's data cell 0x42 via
    // slots 14/13), checked through the secondary slot 15, selected by the param flags (byte +6 bits 0/1/2/5/6, byte
    // +0xa bit 0), and scored by rating / (distance / 5 + 1) weighted by the own interface's opinion (slots 79/78); a
    // better score than the best so far (and, with byte +6 bit 4, a position the owner service accepts) replaces it.
    // The best candidate's number goes to the frame's slot 13 and the result is 1; none: 0. Errors: 0x17 (operand
    // object), 8 and 0x6c (stack object lookups).
    uint32_t Prim0E(void *frame, const PrimParamView *param);

    // 0x58ae50 (opcode 0x10, looks like "go to / route to"). The walker is the frame's slot-12 object (or an operand's
    // object with byte +6 bit 3; error 0x15 / 0x17) and the destination this, a frame argument (bit 0) or a second
    // operand; the goal is the destination's position (default 1.0, 1.0 on floor 1). Refuses when the walker's slot
    // 25 flags say so, tells the framework the walk started (message 0xaf6129c0) when its slot-64 object says 0x13,
    // and then by param byte +4: 1 stops the current route (slots 188/104/175 and the registry's slot 106); 5..8 walk
    // the waypoints (heading, floor) of a destination list item (frame cell, operand or the destination's own list;
    // errors 8, 0x1b) trying each through the route check 0x7e8c60 and committing the first that passes (slots
    // 215/171/170/216); 3 and 4 plan one route facing the destination's heading (error 0x3d without one); the rest
    // plan a route request through the walker's slot 186, retrying once with a flag cleared (not for kind 2), and
    // announce completion (0xaf6129b0). Result 1 when a route was started, else 0.
    uint32_t Prim10(void *frame, const PrimParamView *param);
};
static_assert(offsetof(PrimRObject, mContext) == 0x40, "PrimRObject layout");
static_assert(offsetof(PrimRObject, mOwner) == 0x48, "PrimRObject layout");
