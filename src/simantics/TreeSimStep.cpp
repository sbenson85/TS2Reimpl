#include "simantics/TreeSim.h"

#include "simantics/StepObserver.h"
#include "simantics/TreeSimInternal.h"

using namespace treesim;

// The interpreter step, cTSTreeSim slot 13 (0x5762d0, 2,847 bytes): runs the node a frame is on and moves the frame to
// the node the result selects. Written from the disassembly; every virtual call is made in the order the original
// makes it (the objects behind the slots are game objects), including the repeated reads of the same property.
//
// What a step does with the frame it is given:
//  - no frame, or a frame without an owner: pops the top frame (stack of at least two) and reports 0;
//  - a paused frame: ResumeFrame (clears the pause bit and steps again);
//  - the frame's node index is one of the special targets: 0xfffd / 0xfffe finish the tree (see below), anything
//    else in 0xfffb..0xffff is the error 0x44c;
//  - an empty tree: pops, marks the caller done and reports 0 (or reports 1 when only the last frame remains);
//  - a node with its flag (bit 15 of the opcode) set: ResumeFrame again with the node;
//  - otherwise the node runs: opcodes below 0x100 through RunNode (slot 6), opcodes from 0x100 up push the tree
//    they name (arguments built by one of three modes depending on the tree format and the node's flag byte).
// RunNode's result picks the next node: -1 and anything unlisted yield (2), 0 takes the false target, 1 the true
// target (both report 0), 2 reports 1 (the tree ended), 3 reports 0 without moving.
//
// The result of the step: 0 keep going, 1 the stack is down to its last frame, 2 yield.

namespace {

constexpr uint32_t kStepReturned = 1;

// How the arguments of a tree call are built, chosen by the node's parameter flag byte (+0x10 of the parameter block).
constexpr uint8_t kParamArgsFromOperands = 1; // bit 0: operand descriptors (kind byte + id word) read through ReadOperand
constexpr uint8_t kParamArgsFromFrame = 2;    // bit 1: the calling frame's own arguments, passed on
constexpr uint8_t kParamFlagsNone = 0xff;     // unset block
constexpr int16_t kFormatWithParamFlags = static_cast<int16_t>(0x8003); // trees of this format or newer carry the flag byte

// Operand descriptors of a node's parameter block: argument i is (kind byte at +4+3i, id word at +5+3i).
uint16_t ReadArgumentOperand(cTSTreeSim *sim, const uint8_t *param, uint32_t i)
{
    const uint8_t kind = param[4 + 3 * i];
    uint16_t id;
    std::memcpy(&id, param + 5 + 3 * i, sizeof(id));
    return static_cast<uint16_t>(AsSim(sim)->ReadOperand(kind, id, nullptr, nullptr, nullptr, 0, 0));
}

} // namespace

uint32_t cTSTreeSim::DoNodeAction(void *frameArg, uint32_t)
{
    Sim *sim = AsSim(this);
    Frame *frame = As<Frame>(frameArg);
    SimTraceLog *log = SimTraceLog::Get();

    if (!frame || !frame->GetOwner()) {
        // Nothing to run in this frame: drop the top one.
        if (AsStack(this)->Count() < 2) Raise(kErrorStackTooSmall);
        AsStack(this)->Pop(mDebugService != nullptr, mContext);
        sim->OnFramePopped();
        return kStepContinue;
    }

    TreeHeld<Frame> hold(frame);
    Frame *self = frame->Self();
    if (self->IsPaused()) return sim->ResumeFrame(frame, nullptr);

    cTSTreeStackElem *selfFields = Fields(self);
    const uint32_t nodeWord = selfFields->mNodeIndex;
    int32_t recorded = -1; // the result a replayed tree call delivers

    if (static_cast<uint16_t>(nodeWord) >= 0xfffb) {
        // The special targets. 0xfffc ("no target") and anything else outside 0xfffd / 0xfffe is an error.
        if (nodeWord != kTargetTrue && nodeWord != kTargetFalse) Raise(kErrorBadTarget);

        // A tree finished (returned true or false): back to the caller.
        if (log->IsActive()) log->LogNode(self, 1);
        TreeHeld<Frame> hold2(frame);
        if (AsStack(this)->Count() < 2) Raise(kErrorStackTooSmall);
        AsStack(this)->Pop(mDebugService != nullptr, mContext);
        sim->OnFramePopped();
        Frame *caller = TopFrameChecked(this);
        mLastBranch = nodeWord == kTargetTrue;
        if (caller->GetResumeState() != 0) return kStepContinue; // the caller is waiting on something else
        Owner *callerOwner = caller->GetOwner();
        if (!callerOwner) Raise(kErrorNoOwner);
        Tree *callerTree = callerOwner->GetTree(static_cast<uint16_t>(caller->GetTreeId()));
        if (!callerTree) Raise(kErrorNoCalledTree);
        if (static_cast<uint16_t>(callerTree->GetNodeCount()) == 0) {
            if (((mStack.mFrames.size() * 4) & ~3u) > 4) {
                caller->SetNodeIndex(kTargetTrue);
                mNextNode = kTargetTrue;
                return kStepContinue;
            }
            return kStepStackEmpty;
        }
        Node *callerNode = callerTree->GetNode(Fields(caller)->mNodeIndex);
        if (!callerNode) Raise(kErrorNoCalledTree);
        Tree *calledTree = callerOwner->GetTree(callerNode->GetOpcode());
        if (mRecording && calledTree && (calledTree->GetFlags() & 8)) {
            const uint32_t opcode = callerNode->GetOpcode();
            const uint32_t cacheFlags = calledTree->GetCacheFlags();
            const uint32_t treeFlags = calledTree->GetFlags();
            RecordResult(treeFlags, cacheFlags, opcode, frame);
        }
        if (nodeWord == kTargetTrue) {
            if (callerNode->GetTrueTarget() == kTargetNone) Raise(kErrorBadTarget);
            caller->SetNodeIndex(callerNode->GetTrueTarget());
            mNextNode = callerNode->GetTrueTarget();
        } else {
            if (callerNode->GetFalseTarget() == kTargetNone) Raise(kErrorBadTarget);
            caller->SetNodeIndex(callerNode->GetFalseTarget());
            mNextNode = callerNode->GetFalseTarget();
        }
        return kStepContinue;
    }

    Owner *owner = self->GetOwner();
    if (!owner) Raise(kErrorNoOwner);
    const uint32_t treeWord = selfFields->mTreeWord;
    Tree *curTree = owner->GetTree(treeWord == 0xffff ? 0xffffffffu : (treeWord & 0x7fff));
    if (!curTree) Raise(kErrorNoNode);

    if (static_cast<uint16_t>(curTree->GetNodeCount()) == 0) {
        // An empty tree ends at once.
        if (((mStack.mFrames.size() * 4) & ~3u) > 4) {
            AsStack(this)->Pop(mDebugService != nullptr, mContext);
            sim->OnFramePopped();
            TopFrameChecked(this)->SetNodeIndex(kTargetTrue);
            mNextNode = kTargetTrue;
            return kStepContinue;
        }
        return kStepStackEmpty;
    }

    Node *node = curTree->GetNode(selfFields->mNodeIndex);
    if (!node) Raise(kErrorNoCalledTree);
    if (node->GetFlag()) return sim->ResumeFrame(frame, node);

    // One more node executed; the limit (and which of the two the context's option selects) is checked first.
    ++mStepCount;
    Gate *gate = As<Gate>(As<Context>(mContext)->GetGate());
    int32_t &limit = *GameAddress<int32_t>(kStepLimitGlobal);
    if (gate->GetWord(kGateOptionSlowLimit) & 1) {
        if (limit < kStepLimitRaised) {
            limit = kStepLimitRaised;
            mStepBase = mStepCount;
        }
    } else if (limit > kStepLimitNormal) {
        limit = kStepLimitNormal;
        mStepCount = mStepBase;
    }
    if (static_cast<int32_t>(mStepCount) >= limit) Raise(kErrorStepLimit);

    int32_t result;
    // The host's trace, when installed, observes the opcode read and the primitive call (StepObserver.h).
    const int16_t kindOpcode = stepobserver::gOpcodeRead ? static_cast<int16_t>(stepobserver::gOpcodeRead(sim, frame, node))
                                                         : static_cast<int16_t>(node->GetOpcode());
    if (kindOpcode < 0x100) {
        result = stepobserver::gRunPrimitive ? stepobserver::gRunPrimitive(sim, frame, node) : sim->RunNode(frame, node);
    } else {
        // A call of another tree: build its arguments and push it.
        uint16_t arguments[8] = {};
        uint16_t *argumentPointer = nullptr;
        Tree *calledTree = owner->GetTree(node->GetOpcode());
        if (!calledTree) Raise(kErrorNoTree);
        const uint16_t opcode = node->GetOpcode();
        const uint32_t argCount = calledTree->GetArgCount();
        const uint8_t *param = node->GetParam();
        const TreeNodeParam *paramBlock = reinterpret_cast<const TreeNodeParam *>(param);
        auto formatHasFlags = [&]() { return curTree->GetFormatVersion() >= kFormatWithParamFlags; };

        if (formatHasFlags() && (param[0x10] & kParamArgsFromOperands) && param[0x10] != kParamFlagsNone) {
            if (!paramBlock->HasNoOperands() && argCount != 0 && argCount - 1 <= 7) {
                // The first four operand descriptors, read from the last one down.
                for (uint32_t i = (argCount < 4 ? argCount : 4); i-- > 0;)
                    arguments[i] = ReadArgumentOperand(this, param, i);
            }
            argumentPointer = arguments;
        } else if (formatHasFlags() && (param[0x10] & kParamArgsFromFrame) && param[0x10] != kParamFlagsNone) {
            if (!paramBlock->HasNoOperands() && argCount != 0 && argCount - 1 <= 7) {
                // The calling frame's own arguments, highest first (each tested against the frame's count).
                for (uint32_t i = argCount - 1; i >= 1; --i) {
                    if (self->GetArgCount() > i) arguments[i] = static_cast<uint16_t>(self->GetArg(static_cast<int32_t>(i)));
                }
                if (self->GetArgCount() != 0) arguments[0] = static_cast<uint16_t>(self->GetArg(0));
            }
            argumentPointer = arguments;
        } else if (!paramBlock->HasNoOperands()) {
            argumentPointer = const_cast<uint16_t *>(reinterpret_cast<const uint16_t *>(param + 4));
        }

        bool replayed = false;
        if (mRecording && (calledTree->GetFlags() & 8)) {
            const uint32_t cacheFlags = calledTree->GetCacheFlags();
            const uint32_t treeFlags = calledTree->GetFlags();
            uint32_t out = 0xffffffffu;
            replayed = ReplayResult(treeFlags, cacheFlags, opcode, argCount, argumentPointer, &out, nullptr, nullptr);
            recorded = static_cast<int32_t>(out);
        }
        if (replayed) {
            result = recorded;
        } else {
            const uint32_t word1c = self->GetWord1c();
            const uint32_t word1a = self->GetWord1a();
            const uint32_t word18 = self->GetWord18();
            const uint32_t flag16 = self->GetFlag16();
            if (!sim->PushTree(nullptr, argumentPointer, opcode, flag16, word18, word1a, word1c)) {
                // The push failed: 0x3e8 when the tree exists, 0x450 when it is gone.
                Owner *failOwner = self->GetOwner();
                if (!failOwner) Raise(kErrorNoOwner);
                Tree *again = failOwner->GetTree(node->GetOpcode());
                Raise(again ? kErrorNoTreeEnd : kErrorNoCalledTree);
            }
            return kStepContinue;
        }
    }

    switch (result) {
    case 1: // true
        if (static_cast<uint16_t>(node->GetTrueTarget()) == kTargetNone) Raise(kErrorBadTarget);
        if (log->IsActive()) log->LogNode(frame, 1);
        self->SetNodeIndex(node->GetTrueTarget());
        mNextNode = node->GetTrueTarget();
        mLastBranch = 1;
        self->SetResumeState(0);
        return kStepContinue;
    case 0: // false
        if (static_cast<uint16_t>(node->GetFalseTarget()) == kTargetNone) Raise(kErrorBadTarget);
        if (log->IsActive()) log->LogNode(frame, 0);
        self->SetNodeIndex(node->GetFalseTarget());
        mNextNode = node->GetFalseTarget();
        mLastBranch = 0;
        self->SetResumeState(0);
        return kStepContinue;
    case 2:
        return kStepReturned;
    case 3:
        return kStepContinue;
    default: // -1 and anything else: yield
        return kStepYield;
    }
}

