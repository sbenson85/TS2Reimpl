#include "app/WinMainInfra.h"
#include "hook/hooks.h"

namespace {

// Status `replaced`: our own implementations held to the contracts in docs/contracts/. The handler
// 0xcac170 is not hooked: only the installer (0xcac1f0) refers to it, and ours installs
// winmaininfra::ReportAndReturn instead.
const HookSpec kHooks[] = {
    {"WinMainInfra_StartCrashReporter", 0xc2b206, 32, 0xd256e94a, reinterpret_cast<void *>(&winmaininfra::StartCrashReporter)},
    {"WinMainInfra_SendAnalyticsEvent", 0xc2df67, 32, 0x8ae05172, reinterpret_cast<void *>(&winmaininfra::SendAnalyticsEvent)},
    {"WinMainInfra_InstallHandlers", 0xcac1f0, 24, 0xeefa6818, reinterpret_cast<void *>(&winmaininfra::InstallTerminateAndPurecallHandlers)},
};
REGISTER_HOOKS(kHooks);

} // namespace
