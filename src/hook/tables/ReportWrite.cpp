#include "app/ReportWrite.h"
#include "hook/hooks.h"

namespace {

using namespace appbase;

const HookSpec kHooks[] = {
    {"ModuleInfo_Destruct", 0x1baabe, 32, 0xece337fe, MemberAddress(&ModuleInfo::Destruct)},
    {"ModuleInfo_CopyConstruct", 0x1ba8df, 32, 0xa40a64be, MemberAddress(&ModuleInfo::CopyConstruct)},
    {"ReportWrite_EnumerateModulesToolhelp", 0x1bb2b6, 32, 0x355b2170, reinterpret_cast<void *>(&EnumerateModulesToolhelp)},
    {"ReportWrite_EnumerateModules", 0x1bb108, 32, 0xad2928b1, reinterpret_cast<void *>(&EnumerateModules)},
    {"ReportWrite_LoadSymbolModules", 0x1bb026, 32, 0xf64f083b, reinterpret_cast<void *>(&LoadSymbolModules)},
    {"ReportWrite_LoadSymbolModulesOnce", 0x1bcf4a, 32, 0xe7ac516f, reinterpret_cast<void *>(&LoadSymbolModulesOnce)},
    {"ReportWrite_WriteModules", 0x1bcdd4, 32, 0x56d6ebc3, reinterpret_cast<void *>(&WriteModules)},
    {"ReportWrite_WriteHexDump", 0x1bd744, 32, 0xda7988f3, reinterpret_cast<void *>(&WriteHexDump)},
    {"ReportWrite_WriteManualCallStack", 0x1bcc6c, 32, 0x8fa0ad12, reinterpret_cast<void *>(&WriteManualCallStack)},
    {"ReportWrite_WriteInstructionData", 0x1bd064, 32, 0xdb7a4c28, reinterpret_cast<void *>(&WriteInstructionData)},
    {"ReportWrite_WriteStackData", 0x1bd1de, 32, 0xea78e5a0, reinterpret_cast<void *>(&WriteStackData)},
    {"ReportWrite_WaitStopwatch", 0x1c9211, 32, 0x17a4e49f, reinterpret_cast<void *>(&WaitStopwatch)},
    {"ReportWrite_WriteReport", 0x1bb410, 32, 0x53c264e0, reinterpret_cast<void *>(&WriteReport)},
    {"ReportWrite_Report", 0x1bd69a, 32, 0x8a2c0060, reinterpret_cast<void *>(&Report)},
};
REGISTER_HOOKS(kHooks);

} // namespace
