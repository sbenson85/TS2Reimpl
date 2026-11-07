// Hooks for cApplyShaderProgramConstants (src/sg/cApplyShaderProgramConstants.h): the factory, which makes ours. The
// image's constructor, destructor and slot bodies are in effect through it (only the factory constructs the class, so
// only our objects, with our vtable, exist).
#include "hook/hooks.h"
#include "sg/cApplyShaderProgramConstants.h"

using namespace nGZSceneGraph;

namespace {

const HookSpec kHooks[] = {
    {"nGZSceneGraph_cApplyShaderProgramConstants_Create", 0xac3200, 32, 0x6b774448,
     reinterpret_cast<void *>(&cApplyShaderProgramConstants::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
