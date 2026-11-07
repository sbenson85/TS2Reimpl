#include "hook/hooks.h"
#include "gz/cDeviceEnumDX.h"

// cDeviceEnumDX's classes are in effect through callers (cDeviceDX::Init constructs ours); this table holds the
// free helper the original's CanStretchRect reaches.

namespace {

const HookSpec kHooks[] = {
    {"nGZGraphic4_HoldGraphicService", 0x1773b0, 32, 0x605e4337, reinterpret_cast<void *>(&nGZGraphic4::HoldGraphicService)},
};
REGISTER_HOOKS(kHooks);

} // namespace
