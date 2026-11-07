#include "simantics/SimTraceLog.h"

#include "game/game.h"
#include "ifaces/TraceDebugService.h"
#include "ifaces/TraceSink.h"
#include "ifaces/TreeOwner.h"
#include "ifaces/cTSBehaviorTree.h"
#include "ifaces/cTSTreeStackElem.h"
#include "simantics/TreeHeld.h"

#include <cstdio>

namespace {

// The text the trace writes; the literals are the game's (in .rdata, read here as constants).
constexpr const char *kTab = "\t";
constexpr const char *kNewline = "\n";
constexpr const char *kExitFalse = "Exit Node False";
constexpr const char *kExitTrue = "Exit Node True";
constexpr const char *kNodeTextFormat = "    Node text: %s\n";
constexpr const char *kNode = "Node";
constexpr const char *kResult = "result";
constexpr const char *kTrue = "true";
constexpr const char *kFalse = "false";
constexpr const char *kGosub = "Gosub tree id";
constexpr const char *kTreeName = "tree Name";
constexpr const char *kWithStackObject = "with stack obj";
constexpr const char *kErrored = "Tree Errored Out.";

constexpr uint32_t kDebugServiceGlobal = 0x10cc294;

ifaces::TraceSink *Sink(void *sink)
{
    return As<ifaces::TraceSink>(sink);
}

} // namespace

SimTraceLog *SimTraceLog::Get()
{
    return GameAddress<SimTraceLog>(kGlobal);
}

bool SimTraceLog::IsActive() const
{
    return mEnabled != 0 && mActive != 0;
}

void SimTraceLog::WriteIndent()
{
    for (int32_t i = 0; i < mDepth; ++i) Sink(mSink)->WriteText(kTab);
}

void SimTraceLog::LogCall(int32_t treeId, int32_t stackObjectId, const char *name)
{
    ++mDepth;
    if (!mSink) return;
    WriteIndent();
    ifaces::TraceSink *sink = Sink(mSink);
    sink->WriteText(kGosub);
    sink->WriteText(kTab);
    sink->WriteInt(static_cast<uint32_t>(treeId));
    sink->WriteText(kTab);
    sink->WriteText(kTreeName);
    sink->WriteText(kTab);
    sink->WriteText(name);
    sink->WriteText(kTab);
    sink->WriteText(kWithStackObject);
    sink->WriteText(kTab);
    sink->WriteInt(static_cast<uint32_t>(stackObjectId));
    sink->WriteText(kNewline);
}

void SimTraceLog::LogErrored()
{
    if (!mSink) return;
    WriteIndent();
    ifaces::TraceSink *sink = Sink(mSink);
    sink->WriteText(kErrored);
    sink->WriteText(kNewline);
}

void SimTraceLog::LogNode(void *framePtr, uint32_t isTrue)
{
    ifaces::cTSTreeStackElem *frame = As<ifaces::cTSTreeStackElem>(framePtr);
    if (!frame || !mSink) return;
    // Everything the log does after those checks sits in a catch (...) in the original: a failing sink or lookup
    // never reaches the interpreter.
    try {
        WriteNode(frame, isTrue);
    } catch (...) {
    }
}

void SimTraceLog::WriteNode(void *framePtr, uint32_t isTrue)
{
    ifaces::cTSTreeStackElem *frame = As<ifaces::cTSTreeStackElem>(framePtr);
    if (frame->GetNodeIndex() == 0xfffe) {
        WriteIndent();
        Sink(mSink)->WriteText(kExitFalse);
        Sink(mSink)->WriteText(kNewline);
        --mDepth;
        return;
    }
    if (frame->GetNodeIndex() == 0xfffd) {
        WriteIndent();
        Sink(mSink)->WriteText(kExitTrue);
        Sink(mSink)->WriteText(kNewline);
        --mDepth;
        return;
    }
    bool printPlain = true;
    if (mShowNodeText && frame->GetOwner()) {
        // The owner and the tree are held while the node's text is looked up.
        TreeHeld<ifaces::TreeOwner> owner(frame->GetOwner());
        owner->PrepareTree(static_cast<uint16_t>(frame->GetTreeId()));
        TreeHeld<ifaces::cTSBehaviorTree> tree(owner->GetTree(static_cast<uint16_t>(frame->GetTreeId())));
        if (tree) {
            void *service = *GameAddress<void *>(kDebugServiceGlobal);
            if (service) {
                const uint32_t nodeIndex = frame->GetNodeIndex();
                const int32_t treeId = static_cast<int16_t>(static_cast<uint16_t>(frame->GetTreeId()));
                const char *text = As<ifaces::TraceDebugService>(service)->GetNodeText(owner.get(), treeId, nodeIndex);
                char line[0x100];
                // _snprintf into the 256-byte buffer: the original passes the size and the text as-is.
                _snprintf(line, sizeof(line), kNodeTextFormat, text);
                WriteIndent();
                ifaces::TraceSink *sink = Sink(mSink);
                sink->WriteText(kNode);
                sink->WriteText(kTab);
                sink->WriteWord(frame->GetNodeIndex());
                sink->WriteText(kTab);
                sink->WriteText(kResult);
                sink->WriteText(kTab);
                sink->WriteText(isTrue ? kTrue : kFalse);
                sink->WriteText(kTab);
                sink->WriteText(line);
                printPlain = false;
            }
        }
    }
    if (!printPlain) return;
    WriteIndent();
    ifaces::TraceSink *sink = Sink(mSink);
    sink->WriteText(kNode);
    sink->WriteText(kTab);
    sink->WriteWord(frame->GetNodeIndex());
    sink->WriteText(kTab);
    sink->WriteText(kResult);
    sink->WriteText(kTab);
    sink->WriteText(isTrue ? kTrue : kFalse);
    sink->WriteText(kNewline);
}
