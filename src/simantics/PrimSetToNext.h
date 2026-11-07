#pragma once
#include <cstddef>
#include <cstdint>

// SimAntics "Set To Next" primitive (opcode 0x1f of cTSObject::vf332, 1.18.0.187): handler 0x5a12c0,
// `handler(this = the object, frame, param)` (__thiscall, ret 8), 15.8 KB, one switch over the mode byte
// of the node's operand block. It walks object lists (all objects, people, tree entries, tile
// neighbours, ...) with the current position kept in a 16-bit destination operand, so a BHAV can ask for
// "the next one": the destination holds the id/index of the current item (0 to start) and receives the
// next accepted one. Signals bad operands by throwing a 16-bit error code (`throw (short)code`).
//
// Mode table and the operand layout: see PrimSetToNext.cpp.

// The object as this handler sees it: only the fields it touches. Not a complete cTSObject.
struct SetToNextObject {
    void **mVTable;                    // +0x00 primary vtable (cTSObject, 0xdd2f0c)
    void **mTreeSimVTable;             // +0x04 cTSTreeSim subobject: slot 7 reads an operand into a destination
    uint8_t mPad08[0x40 - 0x08];       // +0x08..+0x3f
    void *mEnvironment;                // +0x40 holder of the object that vends the lists and lookups (slots 6, 10, 24, 26, 29)
    uint8_t mPad44[0x96 - 0x44];       // +0x44..+0x95
    int16_t mField96;                  // +0x96 id of a related object, read by the stack-object modes
    int16_t mField98;                  // +0x98
    uint8_t mPad9a[0x9e - 0x9a];       // +0x9a..+0x9d
    int16_t mField9e;                  // +0x9e written by mode 0x0d
    int16_t mFieldA0;                  // +0xa0 written by mode 0x0d
    uint8_t mPadA2[0x1c4 - 0xa2];      // +0xa2..+0x1c3
    void *mObjectListOwner;            // +0x1c4 object whose slot 31 returns the std::list of all objects

    // 0x5a12c0 (ret 8). Returns 1 when an item was found (or the mode's own success condition holds), else 0.
    uint32_t SetToNext(void *frame, const void *param);
};
static_assert(offsetof(SetToNextObject, mEnvironment) == 0x40, "SetToNextObject layout");
static_assert(offsetof(SetToNextObject, mField96) == 0x96, "SetToNextObject layout");
static_assert(offsetof(SetToNextObject, mFieldA0) == 0xa0, "SetToNextObject layout");
static_assert(offsetof(SetToNextObject, mObjectListOwner) == 0x1c4, "SetToNextObject layout");
