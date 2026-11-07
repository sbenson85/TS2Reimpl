#include "simantics/SimTraceLog.h"

#include "game/vtable.h"
#include "rz/RZFileStreams.h"
#include "rz/cRZString.h"
#include "ifaces/TraceSink.h"

// The trace log members the FindBestAction search uses to open and close its check-tree log (lane AF).

void SimTraceLog::LogSearchStart(const char *name, int32_t tick, int32_t objectId)
{
    // The file name lives in a game string for the length of the call.
    struct FileName {
        cRZString mString;
        FileName() { mString.Construct(); }
        ~FileName() { mString.Destruct(); }
    } fileName;
    fileName.mString.Sprintf("ChecktreeLogging_%d_%s.xls", tick, name);

    if (mSink == nullptr) {
        // A text file stream (cRZOFStream, rz/RZFileStreams.h): the name and four zeros.
        void *const block = ::operator new(sizeof(cRZOFStream));
        mSink = block ? static_cast<cRZOFStream *>(block)->Construct(*fileName.mString.AsInterface(), 0, 0, 0, 0) : nullptr;
    }
    ifaces::TraceSink *const sink = As<ifaces::TraceSink>(mSink);
    sink->WriteText("Me Object");
    sink->WriteText("\t");
    sink->WriteText(name);
    sink->WriteText("\t");
    sink->WriteText("Id");
    sink->WriteText("\t");
    sink->WriteInt(static_cast<uint32_t>(objectId));
    sink->WriteText("\t");
    sink->WriteText("On Tick");
    sink->WriteText("\t");
    sink->WriteInt(static_cast<uint32_t>(tick));
    sink->WriteText("\n");
    sink->WriteText("\n");
    mActive = 1;
}

void SimTraceLog::CloseSink()
{
    if (mSink) {
        As<ifaces::TraceSink>(mSink)->Destroy(1);
        mSink = nullptr;
    }
    mActive = 0;
}
