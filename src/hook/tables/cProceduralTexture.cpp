#include "hook/hooks.h"
#include "sg/cProceduralTexture.h"

namespace {

using namespace nGZSceneGraph;

// The director's factory (class 0x6b81dfe9) makes our cProceduralTexture: its ctor, dtor and vtable are in effect
// through it.
const HookSpec kHooks[] = {
    {"nGZSceneGraph_cProceduralTexture_New", 0xab4d90, 32, 0x5b7e1c78, reinterpret_cast<void *>(&cProceduralTexture::New)},
};
REGISTER_HOOKS(kHooks);

} // namespace
