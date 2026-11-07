#pragma once
#include "simantics/TreeSim.h"

#include <cstddef>
#include <cstdint>

// The cTSObject end of the interpreter: the object's own versions of two slots of the sim subobject, which run the
// generic cTSTreeSim code and then tell the object about it. `this` is the sim subobject (object + 4); the fields below
// are the object's, listed by their offset from the sim.
//
//   slot 15, CallTree: cTSObject::vf4_15 (0x53d2d0, __thiscall ret 0x20). Runs the tree; when the object left an action
//     pending while a tree was running (the byte at +0x298) and no tree call is active any more, clears the byte and lets
//     the object start it (object slot 204).
//   slot 36, RunToCompletion: cTSObject::vf4_36 (0x5315a0, __thiscall ret 0xc). Ends a run of the stack: announces it to
//     the listener subobject at +0x70, finds the last frame below the first one that has no tree, runs it to completion
//     and tells the object (slot 343) how it went, then counts the object's two countdown words down.
struct TreeRunListener {
    const void *const *mVTable; // only its slot 11 is used here
};

struct TreeSimObject {
    // 0x53d2d0: CallTree, then the pending action.
    uint8_t CallTree(void *owner, uint32_t word1a, uint32_t treeId, uint32_t word18, uint32_t word1c, uint16_t *args,
                     uint32_t result, uint32_t flag16);
    // 0x5315a0: ends a run. `actor` and `reportSuccess` are passed on to the object (the base RunToCompletion ignores its
    // arguments); `announceStart` also gates the call to the listener. Returns whether the run completed (1 when there was
    // nothing to run).
    uint8_t RunToCompletion(uint32_t actor, uint32_t reportSuccess, uint32_t announceStart);

    cTSTreeSim mSim;                  // +0x000
    TreeRunListener mListener;        // +0x070 (object + 0x74): a subobject, only its vtable is used
    uint8_t mPad74[0xd4 - 0x74];
    uint16_t mCountdownA;             // +0x0d4 counted down by RunToCompletion while nonzero
    uint8_t mPadD6[0x150 - 0xd6];
    uint16_t mCountdownB;             // +0x150 likewise
    uint8_t mPad152[0x28c - 0x152];
    uint8_t mAnnounceRuns;            // +0x28c the listener is told about runs while this is set
    uint8_t mPad28d[0x298 - 0x28d];
    uint8_t mActionPending;           // +0x298 an action waits for the outermost tree call to end
};

static_assert(offsetof(TreeSimObject, mListener) == 0x70, "TreeSimObject layout");
static_assert(offsetof(TreeSimObject, mCountdownA) == 0xd4, "TreeSimObject layout");
static_assert(offsetof(TreeSimObject, mCountdownB) == 0x150, "TreeSimObject layout");
static_assert(offsetof(TreeSimObject, mAnnounceRuns) == 0x28c, "TreeSimObject layout");
static_assert(offsetof(TreeSimObject, mActionPending) == 0x298, "TreeSimObject layout");
