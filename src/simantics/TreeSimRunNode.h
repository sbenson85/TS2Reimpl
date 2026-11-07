#pragma once
#include <cstddef>
#include <cstdint>

// The part of cTSObject that executes one SimAntics node for the interpreter (cTSTreeSim slot 6, RunNode): the slot is
// implemented by cTSObject::vf246 (0x53e560, reached through the subobject thunk 0x52184d).
//
// `this` is the object itself (primary vtable); the sim subobject is at +4 and the context's gate (the same object as the
// sim's own +0x48 field) is held at +0x2c8.
//
// It asks the sim's slot 38 (the editor's node hook) first, then the object's primitive dispatch (slot 332, vf332); runs
// the after-hooks (sim slot 39, object slot 333); a node nobody handled must be opcode 0x1c (run tree by name, handled by
// PrimRObject::Prim1C) or error 5 is thrown. When the gate's word 0x35 (the editor configuration flags) differs after the
// node ran, the new value is published to a property sink obtained from the service hub (game global 0x10c9378) as
// "edithConfigFlags". Returns the node's result code (-1 when nothing set one).
struct TreeRunNodeObject {
    int32_t RunNode(void *frame, void *node); // 0x53e560, __thiscall ret 8

    void **mVTable;           // +0x00
    uint8_t mPad04[0x2c4];    // +0x04
    void *mGate;              // +0x2c8
};

static_assert(offsetof(TreeRunNodeObject, mGate) == 0x2c8, "TreeRunNodeObject layout");
