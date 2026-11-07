#pragma once
#include "simantics/PrimExpression.h"
#include "simantics/PrimHandlers.h"

#include <cstddef>
#include <cstdint>

// SimAntics primitive handlers, lane T (1.18.0.187): the functions cTSObject::vf332 (0x534650) calls for opcodes
// 0x17, 0x24, 0x71 and 0x76, each `handler(this = the object, frame, param)` (__thiscall, ret 8). Opcode -> handler
// map: docs/subsystems.md ("Established by the host trace"). They return the branch code the interpreter takes
// and report bad operands by throwing a 16-bit code (`throw (short)code`). Everything is reached through vtable
// slots (the numbers are vtable offsets / 4).

// The object as these handlers see it. Same prefix as PrimMidObject (PrimMid.h).
struct PrimTObject {
    void **mVTable;         // +0x00 primary vtable (cTSObject, 0xdd2f0c)
    void **mSecondary;      // +0x04 vtable of the secondary subobject (this + 4 is that subobject); slot 7 reads an operand
    uint8_t mPad[0x38];     // +0x08..+0x3f
    void *mContext;         // +0x40 the context object (slot 26 gives the object table, slot 29 of that looks objects up)
    uint8_t mPad2[4];       // +0x44
    void *mOwner;           // +0x48

    // 0x57f2b0 (opcode 0x76, ArrayOperation). Works on an array of 16-bit values that an object keeps: the
    // object is this (param byte +6 == 0) or the one the table finds for the id the secondary slot 20 object's
    // slot 12 gives (error 8 when none); the array is number (signed word at param +7) of its slot 265, checked
    // against its slot 266 count (error 0x6c for a bad number or a missing array). The first operand (scope byte
    // +9, id word +0xa) is read (always, with a destination) as the value V. Then the operation (param byte +5):
    //   0 clear; 1 store the length into V's destination (0x13 no storage, 0x14 refused); 2 resize to V (zeroes
    //   when growing; V negative: length_error); 3 fill with V; 4 insert V at the front; 5 append V; 6 insert V
    //   before the index the second operand (scope byte +0xc, id word +0xd) names; 7 erase the front; 8 erase the
    //   back; 9 erase at the index; 10 find V from the index, store the position into the second operand's
    //   destination (returns 0 when the index is out of range or V is absent, 1 when found); 11 swap the elements
    //   at V and the second operand; 12 sort descending; 13 sort ascending; others throw 1. Errors for a bad
    //   index: 0x6d (also erasing from an empty array). Returns 1 except for the two 0 answers of operation 10.
    uint32_t Prim76(void *frame, const PrimParamView *param);

    // 0x5a6e40 (opcode 0x71, SnapInto). Two operands name objects in the context's slot-26 table (slot 30 looks
    // one up; error 0x17 when missing): A (scope byte +7, id word +8) and B (scope byte +4, id word +5). The slot
    // number is param byte +0xa, or this's slot 19(0) when bit 0 of byte +0xf is set. It clears this's slot-14
    // entries 0x34 and 0x36, builds a snap-test state (0x7f02f0) and asks the snap check 0x7e8aa0(B, A, slot,
    // state). On success: unless bit 1 of byte +0xf, B's slot 174 (A, slot, 0) and, with bit 2, a query on B's slot 9
    // (0xfa1c39f7, 0xfa1c39ed) whose result's slot 85 is called; then when B's slot 98 says 0x13 a message
    // (0xaf6129b0, value B) goes to the message server (0x9c5a89, slot 3). Returns 1. On failure it reports the
    // error through this's slot 14 (0x34, code + 2) and returns 0; when the context's slot 31 says yes it first
    // walks the objects involved (interface 0x5a69d3d5 names and ids, whose text it formats and throws away).
    uint32_t Prim71(void *frame, const PrimParamView *param);

    // 0x5879a0 (opcode 0x24, Dialog). Param byte +9 picks the action. 9: asks the dialog service's (0x20e031, slot 3)
    // dialog list whether the dialog numbered this's slot 19(selector) is up; 8, 0x0a and 0x1c create a dialog:
    // the text comes from string table 0x12d (the entry from the stack object's slot 19(0) + 1 or param word +0x11;
    // the group object from the locale or the context list), the stack object's slot 262 fills in an extra,
    // mode bits (param byte +0xb bits 1..3) pick the object the dialog is about (error 0x37 when mode 2 finds none),
    // and kind 8 builds the dialog through the list's slot 3 (-1 answer: result 0) and sets its two flags while
    // 0x0a / 0x1c ask this's slot 19 to make it. 0x39 opens a two-text popup through the registry (0xb39020),
    // 0x16 / 0x19 show a message built from three texts (the third parsed as a number) or, when this's slot 19(1)
    // is 0, close the dialog. Every other opcode is handed to the object table's slot 78 (with error 0x6c for some
    // opcodes when the person object has no entries) or, when a pending entry exists, answered by its state
    // (slot 7). Results: 1, 0 and 2; 2 also when the state service (0x211da3) says something is running, or
    // (0x39, 0x16, 0x19) when the frame's slot 18 is non-zero.
    uint32_t Prim24(void *frame, const PrimParamView *param);

    // 0x59f370 (opcode 0x17, PlaySound). The sound number is param word +4 (negative: the operand at scope byte +0xa,
    // id word +0xb instead; 10000.. and 20000.. pick the frame's kind object slot 14 / 15, else slot 16, and are
    // subtracted). The owner id is this's slot 47, or the frame's slot 12 when bit 1 of param byte +8 is set. The
    // framework object (director slot 12) hands out a sound lookup object (clsid 0x1c03c383) that is asked whether the
    // sound exists (slot 3) and for its name (slot 5, lower-cased); when the sound system is up (0x9c5b7e) a message
    // (clsid 0xcbc79579) is filled: the owner object's flags (slot 13(0x22): bit 1 sends nothing, bit 4 with a name
    // starting "vox" sends nothing), its attributes, and a position and direction (from its interface's skeleton
    // node and a rotation, or from its cell and heading), then the name, owner id and a flag-selected id, and the
    // message goes to the server (0x9c5a89, slot 3). Always returns 1.
    uint32_t Prim17(void *frame, const PrimParamView *param);
};
static_assert(offsetof(PrimTObject, mContext) == 0x40, "PrimTObject layout");
static_assert(offsetof(PrimTObject, mOwner) == 0x48, "PrimTObject layout");
