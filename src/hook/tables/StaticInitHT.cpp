#include "hook/hooks.h"
#include "app/ReporterDump.h"
#include "app/StaticInitHT.h"
#include "rz/cRZThread.h"

// Lane HT: the members its XC entries brought in (db/status/StaticInitHT.jsonl).

namespace {

const HookSpec kHooks[] = {
    {"cRZCallbackThread_Destruct", 0x9cfac2, 32, 0x68b91e4a, MemberAddress(&cRZCallbackThread::Destruct)},
    {"cRZCallbackThread_DeletingDestruct", 0x9cfb3e, 31, 0x93d19c67, MemberAddress(&cRZCallbackThread::DeletingDestruct)},
    {"DbgHelpTable_Unload", 0x1bd611, 32, 0xd08e02a4, MemberAddress(&appbase::DbgHelpTable::Unload)},
    {"SimDoNodeActionLog_Flush", 0x526c30, 32, 0x08981902, MemberAddress(&staticinit::ActionLog::Flush)},
    {"WorkerThreads_Shutdown", 0x99936e, 32, 0xfda0bf07, reinterpret_cast<void *>(&staticinit::WorkerThreads::Shutdown)},
};
REGISTER_HOOKS(kHooks);

} // namespace
