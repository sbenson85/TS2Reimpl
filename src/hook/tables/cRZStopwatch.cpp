#include "hook/hooks.h"
#include "rz/cRZStopwatch.h"

namespace {

const HookSpec kHooks[] = {
    {"cRZStopwatch_Construct", 0x1c9030, 25, 0xe601ca38, MemberAddress(&cRZStopwatch::Construct)},
    {"cRZStopwatch_Start", 0x1c982b, 23, 0x0c83f50f, MemberAddress(&cRZStopwatch::Start)},
    {"cRZStopwatch_Elapsed", 0x1c9256, 32, 0x46a02d23, MemberAddress(&cRZStopwatch::Elapsed)},
    {"cRZStopwatch_SetCount", 0x1c960f, 32, 0xbd2cfb4d, MemberAddress(&cRZStopwatch::SetCount)},
    {"cRZStopwatch_MeasureFrequency", 0x1c906a, 32, 0x5e15f2c4, reinterpret_cast<void *>(&cRZStopwatch::MeasureFrequency)},
    {"cRZStopwatch_ReadTsc", 0x1c9244, 18, 0x300a0644, reinterpret_cast<void *>(&cRZStopwatch::ReadTsc)},
    {"cRZStopwatch_ReadPerformanceCounter", 0x1c95c4, 11, 0x40374673, reinterpret_cast<void *>(&cRZStopwatch::ReadPerformanceCounter)},
};
REGISTER_HOOKS(kHooks);

} // namespace
