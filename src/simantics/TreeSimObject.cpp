#include "simantics/TreeSimObject.h"

#include "ifaces/TreeRunListener.h"
#include "simantics/TreeSimInternal.h"

using namespace treesim;

// cTSObject's versions of the sim's CallTree and RunToCompletion slots (TreeSimObject.h). Written from the
// disassembly; the calls are made in the original's order.

namespace {

// The object the sim subobject belongs to: the sim sits at object + 4.
ifaces::cTSObject *ObjectOf(TreeSimObject *sim)
{
    return As<ifaces::cTSObject>(reinterpret_cast<uint8_t *>(sim) - 4);
}

// The frame at an index of the sim's stack the way the end-of-run scan asks for it: the index is a 16-bit value, a
// negative or too large one finds nothing, and the frame is the one the pointer's own Self reports.
Frame *FrameAtShortIndex(cTSTreeSim *sim, int32_t index)
{
    const int32_t narrowed = static_cast<int16_t>(index);
    if (narrowed <= -1 || static_cast<uint32_t>(narrowed) >= sim->mStack.mFrames.size()) return nullptr;
    return As<Frame>(sim->mStack.mFrames[static_cast<uint32_t>(narrowed)].get())->Self();
}

constexpr uint16_t kNoTreeWord = 0xffff; // a frame's tree word when it runs no tree (the bottom frame of CallTree)

} // namespace

uint8_t TreeSimObject::CallTree(void *owner, uint32_t word1a, uint32_t treeId, uint32_t word18, uint32_t word1c,
                                uint16_t *args, uint32_t result, uint32_t flag16)
{
    const uint8_t finished = mSim.CallTree(owner, word1a, treeId, word18, word1c, args, result, flag16);
    if (mActionPending && mSim.mCallDepth == 0) {
        mActionPending = 0;
        ObjectOf(this)->TryStartQueuedAction(nullptr, 0);
    }
    return finished;
}

uint8_t TreeSimObject::RunToCompletion(uint32_t actor, uint32_t reportSuccess, uint32_t announceStart)
{
    cTSTreeSim *sim = &mSim;
    if (mAnnounceRuns && static_cast<uint8_t>(announceStart)) As<ifaces::TreeRunListener>(&mListener)->OnRunStarting(actor);

    uint8_t completed = 1;
    // The last frame of the leading run of frames that run a tree: the scan stops at the first frame that is missing,
    // has no tree word or the "no tree" word.
    const int32_t frameCount = AsStack(sim)->Count();
    int32_t lastTreeFrame = frameCount - 1;
    if (lastTreeFrame >= 0) {
        int32_t index = 0;
        do {
            Frame *frame = FrameAtShortIndex(sim, index);
            if (!frame) {
                lastTreeFrame = index - 1;
                break;
            }
            // The original asks for the frame a second time, with the bounds test throwing this time (the first could have
            // changed the stack).
            const int32_t narrowed = static_cast<int16_t>(index);
            if (static_cast<uint32_t>(narrowed) >= sim->mStack.mFrames.size()) throw cTSTreeStack::kStackErrorNoFrame;
            frame = As<Frame>(sim->mStack.mFrames[static_cast<uint32_t>(narrowed)].get())->Self();
            if (Fields(frame)->mTreeWord == kNoTreeWord) {
                lastTreeFrame = index - 1;
                break;
            }
            ++index;
        } while (index <= lastTreeFrame);
    }

    if (lastTreeFrame != -1 && FrameAtShortIndex(sim, lastTreeFrame) != nullptr) {
        completed = sim->RunToCompletion(actor, reportSuccess, announceStart);
        const bool report = completed != 0 && static_cast<uint8_t>(reportSuccess) != 0;
        ObjectOf(this)->OnRunFinished(actor, report ? 1 : 0);
        if (mCountdownB != 0) --mCountdownB;
        if (mCountdownA != 0) --mCountdownA;
    }
    return completed;
}
