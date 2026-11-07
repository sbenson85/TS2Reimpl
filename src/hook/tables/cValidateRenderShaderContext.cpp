// Hooks for cValidateRenderShaderContext (src/sg/cValidateRenderShaderContext.h): the factory, which makes ours, and
// the light type names its Init reports with (also called by three other original functions). The image's
// constructor, destructor and slot bodies are in effect through the factory (only it constructs the class, so only our
// objects, with our vtable, exist).
#include "hook/hooks.h"
#include "sg/cValidateRenderShaderContext.h"

using namespace nGZSceneGraph;

namespace {

const HookSpec kHooks[] = {
    {"nGZSceneGraph_cValidateRenderShaderContext_Create", 0xac3370, 32, 0xc5bbdac1, reinterpret_cast<void *>(&cValidateRenderShaderContext::Create)},
    {"nGZSceneGraph_LightTypeName", 0xaeed90, 32, 0x5f02e079, reinterpret_cast<void *>(&LightTypeName)},
};
REGISTER_HOOKS(kHooks);

} // namespace
