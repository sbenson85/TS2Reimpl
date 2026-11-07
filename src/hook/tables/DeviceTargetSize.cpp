#include "hook/hooks.h"
#include "gz/DeviceTargetSize.h"

namespace {

// The current device's render-target size (lane VU, O-c).
const HookSpec kHooks[] = {
    {"nGZGraphic4_GetDeviceTargetSize", 0x18aea0, 32, 0x61ffedd6, reinterpret_cast<void *>(&nGZGraphic4::GetDeviceTargetSize)},
};
REGISTER_HOOKS(kHooks);

} // namespace
