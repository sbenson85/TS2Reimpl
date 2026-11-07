#include "simantics/TreeSim.h"

#include "simantics/TreeSimInternal.h"

using namespace treesim;

namespace {

using AddRefFn = uint32_t(__thiscall *)(void *);

constexpr int kAddRefSlot = 1;

int32_t &StepLimit()
{
    return *GameAddress<int32_t>(cTSTreeSim::kStepLimitGlobal);
}

// Takes the reference a service pointer held by the sim carries away: null the field first, then release (the order
// of Detach).
void DropService(void *&field)
{
    void *object = field;
    if (!object) return;
    field = nullptr;
    VirtualCall<AddRefFn, 2>(object);
}

} // namespace

bool TreeNodeParam::HasNoOperands() const
{
    return mWord4 == 0xffffffffu && mWord8 == 0xffffffffu;
}

bool cTSTreeSim::QueryInterface(uint32_t iid, void **out)
{
    if (iid == kIidTreeSim) {
        *out = this;
        VirtualCall<AddRefFn, kAddRefSlot>(this);
        return true;
    }
    // Anything else is the cRZUnknown subobject at +4 (0x1b945c, which only knows iid 1).
    if (iid != 1) return false;
    *out = &mUnknown;
    VirtualCall<AddRefFn, kAddRefSlot>(&mUnknown);
    return true;
}

uint8_t cTSTreeSim::NewFrame(uint32_t iid, void **out)
{
    return AsStack(this)->NewFrame(iid, out);
}

void cTSTreeSim::GetPosition(uint16_t *treeId, uint16_t *nodeIndex)
{
    const int32_t index = AsStack(this)->Count() - 1;
    if (index <= -1 || static_cast<uint32_t>(index) >= mStack.mFrames.size()) throw cTSTreeStack::kStackErrorNoFrame;
    cTSTreeStackElem *frame = Fields(As<Frame>(mStack.mFrames[static_cast<uint32_t>(index)].get())->Self());
    const uint16_t word = frame->mTreeWord;
    *treeId = word == 0xffff ? static_cast<uint16_t>(0xffff) : static_cast<uint16_t>(word & 0x7fff);
    *nodeIndex = frame->mNodeIndex;
}

uint32_t cTSTreeSim::StartTree(void *owner, uint32_t treeId, uint32_t value)
{
    AsStack(this)->Clear();
    // ORIGINAL-BUG: the original passes the address of two zeroed dwords as the argument block (a tree with more than
    // four arguments reads past them; here the block is long enough and zero).
    uint16_t noArguments[8] = {};
    PushTree(owner, noArguments, treeId, 0, value, value, 3);
    mErrorCode = 0;
    return 0;
}

uint32_t cTSTreeSim::ResumeFrame(void *frame, void *node)
{
    if (frame) As<Frame>(frame)->SetPaused(0);
    if (node) As<Node>(node)->SetFlag(0);
    return AsSim(this)->DoNodeAction(As<Frame>(frame), 0);
}

void cTSTreeSim::PrepareStack(uint32_t reserve, uint16_t *defaultArgs)
{
    AsStack(this)->Reset(reserve);
    mDefaultArgs = defaultArgs;
}

void cTSTreeSim::ClearErrorCode()
{
    mErrorCode = 0;
}

void *cTSTreeSim::FindOwnerBaseFrame()
{
    Sim *sim = AsSim(this);
    uint32_t index = static_cast<uint16_t>(AsStack(this)->Count() - 2);
    void *owner = As<Frame>(sim->GetTopFrame())->GetOwner();
    if (!owner) Raise(kErrorNoOwner);
    // Walk down from the frame below the top while the owner stays the same; the frame after the first mismatch
    // is the answer (frame 0 when the owner never changes).
    while (static_cast<int16_t>(index) >= 0) {
        if (As<Frame>(sim->GetFrameAt(static_cast<int32_t>(index)))->GetOwner() != owner)
            return sim->GetFrameAt(static_cast<int32_t>(index + 1));
        --index;
    }
    return sim->GetFrameAt(0);
}

void *cTSTreeSim::GetTopFrame()
{
    const int32_t index = static_cast<int32_t>(mStack.mFrames.size()) - 1;
    return AsStack(this)->GetAt(index);
}

void *cTSTreeSim::FindLastRealFrame()
{
    Sim *sim = AsSim(this);
    int32_t index = 0;
    int32_t last = static_cast<int32_t>(mStack.mFrames.size()) - 1;
    if (last >= 0) {
        for (;;) {
            const uint32_t slot = static_cast<uint16_t>(index);
            // Stop at a missing frame, at a tree id of 0 or at the bottom frame's -1.
            Frame *frame = sim->GetFrameAt(static_cast<int32_t>(slot));
            if (!frame) {
                last = index - 1;
                break;
            }
            if (static_cast<uint16_t>(As<Frame>(sim->GetFrameAt(static_cast<int32_t>(slot)))->GetTreeId()) == 0) {
                last = index - 1;
                break;
            }
            if (static_cast<uint16_t>(As<Frame>(sim->GetFrameAt(static_cast<int32_t>(slot)))->GetTreeId()) == 0xffff) {
                last = index - 1;
                break;
            }
            ++index;
            if (index > last) break;
        }
    }
    if (last == -1) return nullptr;
    return sim->GetFrameAt(last);
}

void *cTSTreeSim::GetFrameAt(int32_t index)
{
    return AsStack(this)->GetAt(static_cast<int16_t>(index));
}

uint32_t cTSTreeSim::GetCurrentOpcode()
{
    Sim *sim = AsSim(this);
    TreeHeld<Frame> top(sim->GetTopFrame());
    uint32_t opcode = 0xffffffffu;
    if (top) {
        TreeHeld<Owner> owner(top->GetOwner());
        if (owner) {
            TreeHeld<Tree> tree(owner->GetTree(static_cast<uint16_t>(top->GetTreeId())));
            if (tree) {
                TreeHeld<Node> node(tree->GetNode(top->GetNodeIndex()));
                if (node) opcode = node->GetOpcode();
            }
        }
    }
    return static_cast<uint16_t>(opcode);
}

uint32_t cTSTreeSim::GetResult()
{
    return mResult;
}

void cTSTreeSim::ClearResult()
{
    mResult = 0;
}

uint8_t cTSTreeSim::ReturnedTrue()
{
    return mNextNode == kTargetTrue;
}

int32_t cTSTreeSim::GetStepLimit()
{
    return StepLimit();
}

void cTSTreeSim::SetStepLimit(int32_t limit)
{
    StepLimit() = limit;
}

uint8_t cTSTreeSim::GetRunFlag()
{
    return *GameAddress<uint8_t>(kRunFlagGlobal);
}

void cTSTreeSim::ClearReplayCache(uint32_t)
{
    mReplayMap.Clear();
}

void cTSTreeSim::Detach()
{
    AsStack(this)->ClearAll();
    mDefaultArgs = nullptr;
    DropService(mDebugService);
    DropService(mGate);
    DropService(mServiceC);
    DropService(mContext);
}
