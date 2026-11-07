#include "hook/hooks.h"
#include "rz/cRZStd.h"

namespace {

const HookSpec kHooks[] = {
    {"cRZStd_vf0", 0x1bfca9, 32, 0xa587a006, reinterpret_cast<void *>(&cRZStd::Printf)},
    {"cRZStd_vf1", 0x1bf9c4, 32, 0x5e50447e, MemberAddress(&cRZStd::Assert)},
    {"cRZStd_Report", 0x1bfb92, 32, 0x469ec818, reinterpret_cast<void *>(&cRZStd::Report)},
    {"cRZStd_LogLine", 0x1c00a8, 32, 0xbb3599d2, reinterpret_cast<void *>(&rzstd::LogLine)},
    {"cRZStd_Output", 0x1bff41, 32, 0x09899d26, reinterpret_cast<void *>(&rzstd::Output)},
    {"cRZStd_AskBox", 0x1bfce6, 32, 0xf7573786, reinterpret_cast<void *>(&rzstd::AskBox)},
    {"cRZStd_AskWithFramework", 0x1bfe0c, 32, 0x522cc7f4, reinterpret_cast<void *>(&rzstd::AskWithFramework)},
    {"cRZStd_EnvironmentText", 0x1b14b2, 19, 0xc10d3d43, reinterpret_cast<void *>(&rzstd::EnvironmentText)},
    {"cRZStd_DebugText", 0x1bfc9c, 13, 0xf5541977, reinterpret_cast<void *>(&rzstd::DebugText)},
};
REGISTER_HOOKS(kHooks);

} // namespace
