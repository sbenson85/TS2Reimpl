#include "app/ReporterDump.h"
#include "hook/hooks.h"

namespace {

const HookSpec kHooks[] = {
    {"ReporterDump_QueryModuleVersion", 0x1bc0c2, 32, 0x4cd086db, reinterpret_cast<void *>(&appbase::QueryModuleVersion)},
    {"ReporterDump_LoadDbgHelp", 0x1bc8a7, 32, 0x608b91f7, reinterpret_cast<void *>(&appbase::LoadDbgHelp)},
    {"ReporterDump_WriteMiniDump", 0x1bd87a, 32, 0x72c3b87b, reinterpret_cast<void *>(&appbase::WriteMiniDump)},
};
REGISTER_HOOKS(kHooks);

} // namespace
