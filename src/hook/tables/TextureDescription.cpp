#include "hook/hooks.h"
#include "sg/TextureDescription.h"

namespace {

// ChooseTextureDescription: its callers (cTextureImage::Restore 0xab5610, cProceduralTexture::Restore 0xab5090) are
// ours; hooked at the RVA all the same.
const HookSpec kHooks[] = {
    {"nGZSceneGraph_ChooseTextureDescription", 0xab5f50, 32, 0xf8e9f340,
     reinterpret_cast<void *>(&nGZSceneGraph::ChooseTextureDescription)},
    {"nGZSceneGraph_AdjustCubeTextureSize", 0x18e1d0, 32, 0xa90cd858,
     reinterpret_cast<void *>(&nGZSceneGraph::AdjustCubeTextureSize)},
    // AdjustTextureSize: the game's cRenderTargetTexture::Restore, cPatternBrush and 0x37b3d0 call it too.
    {"nGZSceneGraph_AdjustTextureSize", 0x18c990, 32, 0xacf9c527,
     reinterpret_cast<void *>(&nGZSceneGraph::AdjustTextureSize)},
};
REGISTER_HOOKS(kHooks);

} // namespace
