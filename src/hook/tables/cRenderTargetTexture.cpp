#include "hook/hooks.h"
#include "sg/cRenderTargetTexture.h"

namespace {

using namespace nGZSceneGraph;

// The director's factory (class 0x2b96cc2b) makes our cRenderTargetTexture: its ctor, dtor and vtable are in effect
// through it.
const HookSpec kHooks[] = {
    {"nGZSceneGraph_cRenderTargetTexture_New", 0xab4df0, 32, 0x26d688f6, reinterpret_cast<void *>(&cRenderTargetTexture::New)},
};
REGISTER_HOOKS(kHooks);

} // namespace
