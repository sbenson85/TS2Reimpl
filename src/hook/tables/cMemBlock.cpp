#include "hook/hooks.h"
#include "gz/cMemBlock.h"

namespace {

using nRZGraphic4::cMemBlockAsSurface;
using nRZGraphic4::cMemBlockAsTexture;

// The memory faces' constructors and destructors and the members that make their Direct3D sides
// (docs/standalone-shape.md): every face the game builds is ours, the Direct3D sides are made only by Init and
// SetTopLevel, so the game's four vtables are never reached and every slot is in effect through its callers. The
// level getter is hooked too: the game's image code calls it directly as well as through slots 25-28.
const HookSpec kHooks[] = {
    {"nRZGraphic4_cMemBlockAsTexture_Construct", 0x18c300, 23, 0x03bfb1c6, reinterpret_cast<void *>(&cMemBlockAsTexture::Construct)},
    {"nRZGraphic4_cMemBlockAsTexture_Destruct", 0x18c350, 32, 0xe3cefdfd, reinterpret_cast<void *>(&cMemBlockAsTexture::Destruct)},
    {"nRZGraphic4_cMemBlockAsTexture_SetTopLevel", 0x18c5b0, 32, 0x222c6dd7, MemberAddress(&cMemBlockAsTexture::SetTopLevel)},
    {"nRZGraphic4_cMemBlockAsTexture_GetSurface", 0x18c700, 16, 0x577675ad, MemberAddress(&cMemBlockAsTexture::LevelSurface)},
    {"nRZGraphic4_cMemBlockAsTexture_AddLevel", 0x18c3e0, 32, 0x501887f5, MemberAddress(&cMemBlockAsTexture::AddLevel)},
    {"nRZGraphic4_cMemBlockAsSurface_Construct", 0x18d1a0, 32, 0x395a71ba, reinterpret_cast<void *>(&cMemBlockAsSurface::Construct)},
    {"nRZGraphic4_cMemBlockAsSurface_ConstructEmpty", 0x18d1e0, 23, 0x95d44173, reinterpret_cast<void *>(&cMemBlockAsSurface::ConstructEmpty)},
    {"nRZGraphic4_cMemBlockAsSurface_Destruct", 0x18d2c0, 32, 0xa35a893f, reinterpret_cast<void *>(&cMemBlockAsSurface::Destruct)},
    {"nRZGraphic4_cMemBlockAsSurface_Init", 0x18d500, 32, 0x0a864ecc, MemberAddress(&cMemBlockAsSurface::Init)},
};
REGISTER_HOOKS(kHooks);

} // namespace
