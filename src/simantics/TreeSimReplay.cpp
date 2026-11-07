#include "simantics/TreeReplay.h"
#include "simantics/TreeSimInternal.h"

using namespace treesim;

// The sim's two uses of the record/replay table (cTSTreeSim, TreeSim.h): RecordResult when a replayable tree returns
// (0x575ed0) and ReplayResult before one is called (0x576e70). Written from the disassembly; the virtual calls are made
// in the original's order.

namespace {

// Flag bits of a tree (cTSBehaviorTree::GetFlags).
constexpr uint32_t kTreeFlagAnyCaller = 0x10; // the key does not include the calling object (bit 3, 0x08, marks a
                                              // replayable tree: the interpreter step tests it before calling in here)

// The interface the sim hands out for the object's own argument words (the record keeps eight of them for trees whose
// cache flags say they change them). The object class behind it is the cTSObject.
constexpr uint32_t kIidObjectArgs = 0x9b25d761;
constexpr uint32_t kObjectArgCount = 8;

using ObjectArgs = ifaces::cTSObject;

// The id of the object a frame runs on, as the key sees it: 0 for trees that are shared between objects.
uint32_t KeyObjectId(Frame *frame, uint32_t treeFlags)
{
    if (treeFlags & kTreeFlagAnyCaller) return 0;
    return frame->GetStackObjectId();
}

// (opcode << 16) | object id, each half sign-extended first (the original uses the sign-extending conversions).
uint32_t CallSiteWord(uint32_t opcode, uint32_t objectId)
{
    const int32_t high = static_cast<int16_t>(opcode);
    const int32_t low = static_cast<int16_t>(objectId);
    return static_cast<uint32_t>(high * 0x10000) | static_cast<uint32_t>(low);
}

} // namespace

void cTSTreeSim::RecordResult(uint32_t treeFlags, uint32_t cacheMask, uint32_t opcode, void *frame)
{
    Frame *callee = As<Frame>(frame);
    Owner *owner = callee->GetOwner();
    const uint32_t objectId = KeyObjectId(callee, treeFlags);
    TreeReplayKey key;
    key.mTreeIdentity = owner->GetTreeIdentity(opcode);

    TreeReplayRow row;
    row.mResult = mLastBranch != 0;
    const uint32_t argCount = callee->GetArgCount();
    row.mArgs.resize(argCount);
    for (uint8_t i = 0; i < argCount; ++i) row.mArgs[i] = static_cast<uint16_t>(callee->GetArg(i));

    if (static_cast<uint8_t>(cacheMask) != 0) {
        TreeHeld<ObjectArgs> object;
        if (AsSim(this)->QueryInterface(kIidObjectArgs, reinterpret_cast<void **>(object.Receive()))) {
            row.mTemps.resize(kObjectArgCount);
            for (uint32_t i = 0; i < kObjectArgCount; ++i) row.mTemps[i] = static_cast<uint16_t>(object->GetArgument(i));
        }
    }

    key.mCallSite = CallSiteWord(opcode, objectId);
    if (TreeReplayNode *node = mReplayMap.Find(key)) {
        node->mRows.push_back(row);
    } else {
        std::vector<TreeReplayRow> rows;
        rows.push_back(row);
        TreeReplayMap::InsertResult inserted;
        mReplayMap.Insert(&inserted, &key);
        inserted.mNode->mRows = rows;
    }
}

bool cTSTreeSim::ReplayResult(uint32_t treeFlags, uint32_t cacheMask, uint32_t opcode, uint32_t argCount, const uint16_t *args,
                              uint32_t *out, void *, void *)
{
    // The calling frame is the top of the stack.
    const size_t depth = mStack.mFrames.size();
    if (depth == 0) return false;
    Frame *caller = As<Frame>(mStack.mFrames[depth - 1].get())->Self();
    if (!caller) return false;

    Owner *owner = caller->GetOwner(); // ORIGINAL-BUG: a frame without an owner is dereferenced below
    const uint32_t objectId = KeyObjectId(caller, treeFlags);
    TreeReplayKey key;
    key.mTreeIdentity = owner->GetTreeIdentity(opcode);
    key.mCallSite = CallSiteWord(opcode, objectId);
    TreeReplayNode *node = mReplayMap.Find(key);
    if (!node) return false;

    // The first recorded call made with the same arguments.
    const int32_t compared = static_cast<int16_t>(argCount);
    for (const TreeReplayRow &row : node->mRows) {
        bool same = true;
        for (uint8_t i = 0; same && static_cast<int32_t>(i) < compared; ++i) same = args != nullptr && row.mArgs.data()[i] == args[i];
        if (!same) continue;

        *out = row.mResult;
        if (static_cast<uint8_t>(cacheMask) == 0) return true;

        // Put the object's argument words back the way the call left them.
        TreeHeld<ObjectArgs> object;
        if (AsSim(this)->QueryInterface(kIidObjectArgs, reinterpret_cast<void **>(object.Receive()))) {
            uint8_t remaining = static_cast<uint8_t>(cacheMask);
            for (uint8_t i = 0; i < kObjectArgCount;) {
                const uint8_t bit = static_cast<uint8_t>(1u << i);
                if (static_cast<uint8_t>(cacheMask) & bit) {
                    remaining &= static_cast<uint8_t>(~bit);
                    object->SetArgument(i, row.mTemps.data()[i]); // ORIGINAL-BUG: a row recorded without words has none to read
                }
                ++i;
                if (remaining == 0) break;
            }
        }
        return true;
    }
    return false;
}
