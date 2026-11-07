// Hooks for cApplyFFLightStates (src/sg/cApplyFFLightStates.h): the factory, which makes ours. The image's constructor,
// destructor and slot bodies are in effect through it (only the factory constructs the class, so only our objects, with
// our vtable, exist).
#include "hook/hooks.h"
#include "sg/cApplyFFLightStates.h"

using namespace nGZSceneGraph;

namespace {

const HookSpec kHooks[] = {
    {"nGZSceneGraph_cApplyFFLightStates_Create", 0xac3140, 32, 0x52ae6113, reinterpret_cast<void *>(&cApplyFFLightStates::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
