#include "hook/hooks.h"
#include "gz/TextureSupportDX.h"

namespace {

// The texture support checks (lane RJ, O-c2).
const HookSpec kHooks[] = {
    {"nGZGraphic4_TextureFilterSupported", 0x18c7f0, 32, 0xb45f8438, reinterpret_cast<void *>(&nGZGraphic4::TextureFilterSupported)},
    {"nGZGraphic4_CubeTextureFilterSupported", 0x18df30, 32, 0xb45f8438, reinterpret_cast<void *>(&nGZGraphic4::CubeTextureFilterSupported)},
};
REGISTER_HOOKS(kHooks);

} // namespace
