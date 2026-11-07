#include "hook/hooks.h"
#include "rz/cRZStopwatchCalibration.h"

namespace {

const HookSpec kHooks[] = {
    {"cRZStopwatchCalibration_Construct", 0x1c8d8e, 32, 0xc1a6a3d4, MemberAddress(&cRZStopwatchCalibration::Construct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
