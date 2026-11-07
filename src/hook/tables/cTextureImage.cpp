#include "hook/hooks.h"
#include "sg/cTextureImage.h"

namespace {

using namespace nGZSceneGraph;

// The director's two factories make our cTextureImage (its ctor, dtor and vtable are in effect through them);
// 0xab57d0 is hooked at its RVA (its callers, the two Restores, are ours).
const HookSpec kHooks[] = {
    {"nGZSceneGraph_cTextureImage_NewTexture", 0xab4e50, 32, 0xcb360378,
     reinterpret_cast<void *>(&cTextureImage::NewTexture)},
    {"nGZSceneGraph_cTextureImage_NewCube", 0xab4d30, 32, 0x6058eb7b, reinterpret_cast<void *>(&cTextureImage::NewCube)},
    {"nGZSceneGraph_cTextureImage_CreateDeviceTexture", 0xab57d0, 32, 0x4b923270,
     reinterpret_cast<void *>(&cTextureImage::CreateDeviceTexture)},
};
REGISTER_HOOKS(kHooks);

} // namespace
