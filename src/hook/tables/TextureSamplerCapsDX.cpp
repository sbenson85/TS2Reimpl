#include "hook/hooks.h"
#include "gz/TextureSamplerCapsDX.h"

namespace {

// The sampler state fitting to the device's caps (lane VU, O-c).
const HookSpec kHooks[] = {
    {"nGZGraphic4_FitTextureSamplerStates", 0x18cc40, 32, 0x4bb9e2d1, reinterpret_cast<void *>(&nGZGraphic4::FitTextureSamplerStates)},
    {"nGZGraphic4_FitCubeTextureSamplerStates", 0x18e320, 32, 0xd18275db, reinterpret_cast<void *>(&nGZGraphic4::FitCubeTextureSamplerStates)},
};
REGISTER_HOOKS(kHooks);

} // namespace
