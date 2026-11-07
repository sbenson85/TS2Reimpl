// Hooks for cAttributeFFTextureMatrix (src/sg/cAttributeFFTextureMatrix.h): the factory, which makes ours. The image's
// constructor, destructor, slot bodies and the helpers only they call are in effect through it (only the factory
// constructs the class, so only our objects, with our vtable, exist).
#include "hook/hooks.h"
#include "sg/cAttributeFFTextureMatrix.h"

using namespace nGZSceneGraph;

namespace {

const HookSpec kHooks[] = {
    {"nGZSceneGraph_cAttributeFFTextureMatrix_Create", 0xac32a0, 32, 0x811276de, reinterpret_cast<void *>(&cAttributeFFTextureMatrix::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
