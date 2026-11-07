#include "hook/hooks.h"
#include "gz/cTextureDX.h"

namespace {

// The creators are the classes' switch (docs/standalone-shape.md): every texture the game makes is ours, and with it
// every mip surface (only the textures make them), so the game's vtables are never reached and every slot is in
// effect through its callers.
const HookSpec kHooks[] = {
    {"nGZGraphic4_NewTextureDX", 0x163600, 32, 0x2499dd32, reinterpret_cast<void *>(&nGZGraphic4::cTextureDX::New)},
    {"nGZGraphic4_NewCubeTextureDX", 0x1648a0, 32, 0x19a4d31e, reinterpret_cast<void *>(&nGZGraphic4::cCubeTextureDX::New)},
};
REGISTER_HOOKS(kHooks);

} // namespace
