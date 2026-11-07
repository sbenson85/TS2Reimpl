#include "gz/FrameworkStartup.h"
#include "hook/hooks.h"
#include "rz/StopwatchMore.h"

namespace {

// Not hooked: 0x1420af, the vector assignment 0x1420d2 calls (a library match), and the two small
// wrappers 0x143660 and 0x1427a7 (other groups' records; their bodies are inside Startup here).
const HookSpec kHooks[] = {
    {"cRZCmdLine_Assign", 0x1420d2, 32, 0x13599a83, reinterpret_cast<void *>(&fw_startup::AssignCmdLine)},
    {"cRZCmdLine_ParseProgramPath", 0x1aeb57, 32, 0xe74a9fc1, reinterpret_cast<void *>(&fw_startup::ParseCommandLine)},
    {"GZFrameWork_Startup", 0x144514, 32, 0x52078f92, reinterpret_cast<void *>(&fw_startup::GZFrameWork_Startup)},
    {"cRZStopwatch_ElapsedAsFloat", 0x1c9368, 32, 0x46a02d23, reinterpret_cast<void *>(&stopwatch_more::ElapsedAsFloat)},
    {"cRZStopwatch_ElapsedAs64", 0x1c948d, 32, 0xd562b365, reinterpret_cast<void *>(&stopwatch_more::ElapsedAs64)},
    {"cRZStopwatch_Stop", 0x1c9842, 32, 0xfba15d73, reinterpret_cast<void *>(&stopwatch_more::Stop)},
};
REGISTER_HOOKS(kHooks);

} // namespace
