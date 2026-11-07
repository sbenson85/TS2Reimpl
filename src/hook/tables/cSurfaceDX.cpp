#include "hook/hooks.h"
#include "gz/cSurfaceDX.h"

namespace {

// The creators are the classes' switch (docs/standalone-shape.md): every surface the game makes is ours, so the game's
// vtables are never reached and every slot is in effect through its callers (status "in_effect": "callers"). The mip
// surfaces are made only by the textures (src/hook/tables/cTextureDX.cpp).
const HookSpec kHooks[] = {
    {"nGZGraphic4_NewImageSurfaceDX", 0x161d40, 32, 0x8c8ed93e, reinterpret_cast<void *>(&nGZGraphic4::cImageSurfaceDX::New)},
    {"nGZGraphic4_NewDepthStencilSurfaceDX", 0x161ce0, 32, 0xc03f18d4, reinterpret_cast<void *>(&nGZGraphic4::cDepthStencilSurfaceDX::New)},
    {"nGZGraphic4_NewLockableRenderTargetDX", 0x161da0, 32, 0x78221558, reinterpret_cast<void *>(&nGZGraphic4::cLockableRenderTargetDX::New)},
    {"nGZGraphic4_NewRenderTargetDX", 0x161e00, 32, 0x46d4fd38, reinterpret_cast<void *>(&nGZGraphic4::cRenderTargetDX::New)},
};
REGISTER_HOOKS(kHooks);

} // namespace
