#include "simantics/SimTraceLog.h"

#include "game/vtable.h"
#include "ifaces/TraceSink.h"

// The interaction check-tree part of the SimAntics trace log (the other members are in SimTraceLog.cpp). The literals are
// the game's, read here as constants.

namespace {

constexpr const char *kTab = "\t";
constexpr const char *kNewline = "\n";
constexpr const char *kNewCheck = "New Interaction checkTree.";
constexpr const char *kIndex = "Index";
constexpr const char *kStackObjectId = "inside of Stack Object ID";
constexpr const char *kStackObjectName = "Stack Object Name";
constexpr const char *kCheckComplete = "Interaction Check Tree Complete.";

ifaces::TraceSink *Sink(void *sink)
{
    return As<ifaces::TraceSink>(sink);
}

} // namespace

void SimTraceLog::LogInteractionCheckStart(int32_t, int32_t stackObjectId, const char *stackObjectName, int32_t interactionIndex)
{
    if (mSink) {
        ifaces::TraceSink *sink = Sink(mSink);
        sink->WriteText(kNewline);
        sink->WriteText(kNewCheck);
        sink->WriteText(kNewline);
        sink->WriteText(kIndex);
        sink->WriteText(kTab);
        sink->WriteInt(static_cast<uint32_t>(interactionIndex));
        sink->WriteText(kTab);
        sink->WriteText(kStackObjectId);
        sink->WriteText(kTab);
        sink->WriteInt(static_cast<uint32_t>(stackObjectId));
        sink->WriteText(kTab);
        sink->WriteText(kStackObjectName);
        sink->WriteText(kTab);
        sink->WriteText(stackObjectName);
        sink->WriteText(kNewline);
        sink->WriteText(kNewline);
    }
    mDepth = -1;
}

void SimTraceLog::LogInteractionCheckComplete()
{
    if (!mSink) return;
    ifaces::TraceSink *sink = Sink(mSink);
    sink->WriteText(kNewline);
    sink->WriteText(kCheckComplete);
    sink->WriteText(kNewline);
}
