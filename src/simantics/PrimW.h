#pragma once
#include "simantics/PrimExpression.h"
#include "simantics/PrimHandlers.h"

#include <cstddef>
#include <cstdint>

// SimAntics primitive 0x33, ManageInventory (1.18.0.187): the handler 0x598ac0 that cTSObject::vf332 (0x534650) runs
// for opcode 0x33, `handler(this = the object, frame, param)` (__thiscall, ret 8), lane W. Bad operands are reported
// by throwing a 16-bit code (`throw (short)code`). Everything is reached through vtable slots (numbers are vtable
// offsets / 4).
//
// The operand block is a ManageInventoryParam (an XPrimParam): byte +4 holds the target selector (bits 0..2 the
// kind, bit 3 the "mode" flag, bits 4..5 and 6/7 pieces of two small numbers, bit 7 a flag), +5/+6 the first operand
// (scope byte, id word), +8 the operation, +9 a dword token (0: take it from the stack object's owner), +0xa the
// second target selector (kind in bits 0..2, bit 3 a mode flag), +0xd a category byte whose low bits and bits 2..6
// complete the two small numbers, +0xe/+0xf the second operand, +0x11/+0x12 the third, and +0xa/+0xb a fourth.
//
// The inventory: the target selector names a person object (kind 0 the stack object's context, 1..3 an id looked up
// in the context's lists, 4 the context's own), whose inventory object (class cTSInventory, vtable 0xdcdb70) the
// handler works on. Mode 0 (bit 3 clear) works on the inventory's tokens-by-position list (slots 28/29 begin/end,
// 16..31); mode 1 on its second list (slots 42/43, 33..39). The handler keeps a "current entry" in the object
// (+0x1b0..+0x1be, a cursor object at +0x1c0).
struct PrimWObject {
    void **mVTable;           // +0x00 primary vtable (cTSObject, 0xdd2f0c)
    void **mSecondary;        // +0x04 vtable of the secondary subobject (this + 4); slot 7 reads an operand
    uint8_t mPad[0x38];       // +0x08..+0x3f
    void *mContextHolder;     // +0x40 the context holder: slot 6 gives the owner list object, slot 17 the person
    uint8_t mPad2[0x1b0 - 0x44];
    uint16_t mSelKind;        // +0x1b0 the selector remembered for opcode 9 (kind)
    uint16_t mSelId;          // +0x1b2 its id
    uint16_t mSelFlag;        // +0x1b4 its mode flag
    uint16_t mSelIndex;       // +0x1b6 the remembered entry's index
    uint16_t mEntryLow;       // +0x1b8 low word of the entry's slot 6 value
    uint16_t mEntryHigh;      // +0x1ba high word of it
    uint16_t mEntryWord;      // +0x1bc the entry's slot 8 value
    uint16_t mEntryWord2;     // +0x1be the entry's slot 18 value
    void *mCursor;            // +0x1c0 the cursor object (slots 22, 24, 26, 27, 29)

    // 0x598ac0 (16,341 bytes). Reads the first operand (scope byte +5, id word +6) as the target id unless the
    // selector bit 3 is clear and the operation is 9 (then the remembered selector is used), resolves the person
    // (error 0x57 when none), and takes the token (dword +9, else the owner's id of the stack object's table entry:
    // error 0x15 when none; only for the operations that need one). Then, mode 0 (bit 3 clear), by operation:
    //   0 remove-by-token test (slot 16 < 0: false); 1 slot 20 of the position; 2 slot 18 (token, A, B); 3 find
    //   (slot 25) and store the 1-based position; 4 entry at the position gets slot 20; 5 entry's slot 19 value stored;
    //   6 remember the entry (cursor slot 29 and the +0x1b0 block) ; 7 read a word from the cursor list; 8 write a
    //   word into the cursor list; 9 apply the remembered entry (cursor/entry slot 27 checks: errors 0x9c, 0x9d);
    //   0xa store the inventory's slot 31 value; 0xb ask the director for a helper and push the cursor into the
    //   inventory (slot 15); 0xc find (slot 24) and store the 1-based position; 0xd store slot 30's value;
    //   0xe entry slot 22; 0xf store entry slot 21's value; 0x10/0x13 create an entry from the owner's data and
    //   add it to the inventory; 0x11 move an entry out; 0x12 transfer to another person (slot 48).
    // Mode 1 (second list): 0..0xb, see the source. Returns 1 (true branch) or 0.
    // Errors: 0x57 no person, 0x15 no owner, 0x13/0x14 no storage/refused store, 0x59 operand without storage, 0x78
    // missing pair, 0x74 negative position, 0x9c/0x9d.
    uint32_t Prim33(void *frame, const PrimParamView *param);
};
static_assert(offsetof(PrimWObject, mContextHolder) == 0x40, "PrimWObject layout");
static_assert(offsetof(PrimWObject, mSelKind) == 0x1b0, "PrimWObject layout");
static_assert(offsetof(PrimWObject, mEntryWord2) == 0x1be, "PrimWObject layout");
static_assert(offsetof(PrimWObject, mCursor) == 0x1c0, "PrimWObject layout");
