#include "app/ReportBlockers.h"
#include "hook/hooks.h"

namespace {

using namespace appbase;

const HookSpec kHooks[] = {
    {"ReportBlockers_FindModuleOfAddress", 0x1bbe01, 32, 0x96dba65b, reinterpret_cast<void *>(&FindModuleOfAddress)},
    {"ReportBlockers_CreateDirectories", 0x1c4caf, 32, 0x38a5f635, reinterpret_cast<void *>(&CreateDirectories)},
    {"ReportBlockers_NameFiles", 0x1bd49b, 32, 0xf38aa758, reinterpret_cast<void *>(&NameFiles)},
    {"ReportBlockers_AssignText", 0x1acbfb, 17, 0x4159d6dc, reinterpret_cast<void *>(&AssignText)},
    {"ReportBlockers_AssignNarrowed", 0x1ad84c, 32, 0x059cf07e, reinterpret_cast<void *>(&AssignNarrowed)},
    {"ReportBlockers_ReadLineToString", 0x1c408f, 32, 0xfb375ebf, reinterpret_cast<void *>(&ReadLineToString)},
    {"ReportBlockers_ReadLineToBuffer", 0x1c42bf, 32, 0x3307a07b, reinterpret_cast<void *>(&ReadLineToBuffer)},
    {"ReportBlockers_ReadFileLine", 0x1bbd40, 32, 0x2ab64891, reinterpret_cast<void *>(&ReadFileLine)},
};
REGISTER_HOOKS(kHooks);

} // namespace
