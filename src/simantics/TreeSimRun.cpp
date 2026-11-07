#include "simantics/TreeSim.h"

#include "rz/cRZString.h"
#include "simantics/TreeSimInternal.h"

using namespace treesim;

// Pushing and running trees: PushTree (slot 12, 0x577460), CallTree (slot 15, 0x577e80) and RunToCompletion (slot 36,
// 0x577890). See TreeSimStep.cpp for the step they drive.

namespace {

// A cRZString on the stack, built and destroyed the way the original does (inline constructor, 0x141193 destructor).
struct LocalString {
    cRZString mString;
    LocalString() { mString.Construct(); }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
    ~LocalString() { mString.Destruct(); }
    const char *c_str() const { return mString.mString.c_str(); }
};

int32_t MaxDepth()
{
    return *GameAddress<int32_t>(cTSTreeSim::kMaxDepthGlobal);
}

} // namespace

uint8_t cTSTreeSim::PushTree(void *ownerArg, uint16_t *args, uint32_t treeId, uint32_t flag16, uint32_t word18,
                             uint32_t word1a, uint32_t word1c)
{
    Sim *sim = AsSim(this);

    // The current top frame, asked for without a throw (null when the stack is empty).
    Frame *top = nullptr;
    {
        const int32_t index = static_cast<int32_t>(mStack.mFrames.size()) - 1;
        if (index > -1 && static_cast<uint32_t>(index) < mStack.mFrames.size())
            top = As<Frame>(mStack.mFrames[static_cast<uint32_t>(index)].get())->Self();
    }
    // The object table is asked for the object word18 names (the answer is not used).
    // ORIGINAL-BUG: a context without an object table (null) is dereferenced here.
    As<ifaces::GscObjectTable>(As<Context>(mContext)->GetObjectTable())->FindObject(static_cast<int16_t>(word18));

    TreeHeld<Frame> frame;
    sim->NewFrame(cTSTreeStackElem::kIidFrame, reinterpret_cast<void **>(frame.Receive()));

    Owner *owner = static_cast<Owner *>(ownerArg);
    if (!owner) {
        owner = top->GetOwner(); // ORIGINAL-BUG: a null top frame (empty stack) is dereferenced here
        if (!owner) return 0;
    }
    TreeHeld<Tree> tree(owner->GetTree(treeId));
    if (!tree) return 0;

    if (AsStack(this)->Count() > MaxDepth()) {
        mErrorCode = static_cast<uint16_t>(kErrorStackDepth);
        return 0;
    }

    frame->SetOwner(owner);
    frame->SetResumeState(0);
    frame->SetTreeId(treeId);
    const uint32_t stackObject = top ? static_cast<uint16_t>(top->GetStackObjectId()) : 0;
    frame->SetStackObjectId(stackObject);
    frame->SetFlag16(flag16);
    frame->SetWord18(word18);
    frame->SetWord1a(word1a);
    frame->SetWord1c(word1c);
    uint16_t *arguments = args ? args : mDefaultArgs;

    if (tree->GetTreeType() == 4) {
        // A type-4 tree starts at the node index its first argument names, clamped to its last node.
        frame->SetNodeIndex(*arguments);
        const int16_t nodeCount = tree->GetNodeCount();
        if (static_cast<int32_t>(frame->GetNodeIndex()) >= nodeCount) {
            frame->SetNodeIndex(static_cast<uint16_t>(tree->GetNodeCount() - 1));
        }
    } else {
        frame->SetNodeIndex(0);
    }
    frame->GetNodeIndex(); // asked once more by the original, result unused
    {
        const int16_t nodeCount = tree->GetNodeCount();
        if (static_cast<int32_t>(frame->GetNodeIndex()) >= nodeCount) return 0; // nothing to run
    }

    try {
        frame->SetLocalCount(tree->GetLocalCount());
        frame->SetArgCount(tree->GetArgCount());
        frame->SetArgs(static_cast<uint8_t>(tree->GetArgCount()), arguments);
        SimTraceLog *log = SimTraceLog::Get();
        if (log->IsActive()) {
            LocalString name;
            owner->GetTreeName(treeId, &name.mString);
            log->LogCall(static_cast<int16_t>(treeId), static_cast<int16_t>(stackObject), name.c_str());
        }
        AsStack(this)->Push(frame.get(), mDebugService != nullptr, mContext);
    } catch (int16_t code) {
        sim->HandleError(ErrorArgument(code));
        mErrorCode = static_cast<uint16_t>(code);
        return 0;
    }
    return 1;
}

uint8_t cTSTreeSim::CallTree(void *owner, uint32_t word1a, uint32_t treeId, uint32_t word18, uint32_t word1c,
                             uint16_t *args, uint32_t result, uint32_t flag16)
{
    Sim *sim = AsSim(this);
    if (mErrorCode != 0) return 0;
    if (AsStack(this)->Count() > MaxDepth()) {
        mErrorCode = static_cast<uint16_t>(kErrorStackDepth);
        return 0;
    }
    const uint32_t savedDepth = mCallDepth;
    mCallDepth = static_cast<uint16_t>(savedDepth + 1);
    mResult = static_cast<uint16_t>(result);

    // The bottom frame: a frame with no tree (id -1) that only exists to be returned to.
    TreeHeld<Frame> bottom;
    sim->NewFrame(cTSTreeStackElem::kIidFrame, reinterpret_cast<void **>(bottom.Receive()));
    bottom->SetNodeIndex(0);
    bottom->SetResumeState(1);
    bottom->SetOwner(nullptr);
    bottom->SetTreeId(0xffffffffu);
    bottom->SetStackObjectId(0);
    bottom->SetLocalCount(0);
    bottom->SetArgCount(0);
    bottom->SetFlag16(flag16);
    bottom->SetWord18(word18);
    bottom->SetWord1a(word1a);
    bottom->SetWord1c(word1c);
    AsStack(this)->Push(bottom.get(), mDebugService != nullptr, mContext);

    // The run state flag of the context's gate is set while a tree runs and restored afterwards.
    const uint32_t savedRunState = As<Gate>(mGate)->GetRunState();
    As<Gate>(mGate)->SetRunState(1);

    if (!sim->PushTree(static_cast<Owner *>(owner), args, treeId, flag16, word18, word1a, word1c)) {
        AsStack(this)->Pop(mDebugService != nullptr, mContext);
        mCallDepth = static_cast<uint16_t>(savedDepth);
        sim->OnFramePopped();
        As<Gate>(mGate)->SetRunState(savedRunState);
        return 0;
    }

    TopFrameChecked(this)->SetStackObjectId(word1a);
    uint8_t *runFlag = GameAddress<uint8_t>(kRunFlagGlobal);
    const uint8_t savedRunFlag = *runFlag;
    *runFlag = 0;
    const uint32_t savedSteps = mStepCount;
    uint8_t ok = 0;
    mStepCount = 0;
    try {
        for (;;) {
            Frame *top = TopFrameChecked(this);
            if (Fields(top)->mTreeWord == 0xffff) {
                // Back at the bottom frame: the run is over.
                AsStack(this)->Pop(mDebugService != nullptr, mContext);
                sim->OnFramePopped();
                ok = sim->ReturnedTrue();
                break;
            }
            const uint32_t step = sim->DoNodeAction(top, 0);
            if (step == kStepStackEmpty) Raise(kErrorYielded);
            if (step == kStepYield) {
                SimTraceLog *log = SimTraceLog::Get();
                if (log->IsActive()) log->LogErrored();
                Raise(kErrorYielded);
            }
        }
    } catch (int16_t code) {
        mErrorCode = static_cast<uint16_t>(code);
        sim->HandleError(ErrorArgument(code));
        mCallDepth = static_cast<uint16_t>(savedDepth);
        if (savedRunState == 0) {
            // An outermost run that failed: when the service says so, the object it ran on is told to cancel.
            if (As<ifaces::SimServiceC>(mServiceC)->IsEnabled()) {
                if (auto *table = As<Context>(mContext)->GetObjectTable()) {
                    if (auto *object = As<ifaces::cTSObject>(As<ifaces::GscObjectTable>(table)->FindObject(static_cast<int16_t>(word18)))) {
                        const uint32_t flags = object->GetStateFlags();
                        if (As<Gate>(mGate)->GetHaltRequest() != 0 || (flags & 0x71840) != 0) object->CancelAction(1);
                    }
                }
            }
        }
    }
    mStepCount = savedSteps;
    mCallDepth = static_cast<uint16_t>(savedDepth);
    *runFlag = savedRunFlag;
    As<Gate>(mGate)->SetRunState(savedRunState);
    return ok;
}

uint8_t cTSTreeSim::RunToCompletion(uint32_t, uint32_t, uint32_t)
{
    Sim *sim = AsSim(this);
    As<Gate>(mGate)->SetRunState(1);
    mStepCount = 0;
    try {
        for (;;) {
            const uint32_t step = sim->DoNodeAction(TopFrameChecked(this), 0);
            if (step == kStepStackEmpty) {
                As<Gate>(mGate)->SetRunState(0);
                return 1;
            }
            if (step == kStepYield) {
                As<Gate>(mGate)->SetRunState(0);
                return 0;
            }
        }
    } catch (int16_t code) {
        mErrorCode = static_cast<uint16_t>(code);
        sim->HandleError(ErrorArgument(code));
        As<Gate>(mGate)->SetRunState(0);
        return 1;
    }
}
