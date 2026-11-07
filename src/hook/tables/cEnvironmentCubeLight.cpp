#include "hook/hooks.h"
#include "sg/cEnvironmentCubeLight.h"

namespace {

// The director's factory: every environment cube light the director makes is ours (the game's Clone 0xa47200 is reached
// only through its vtable), so the game's environment cube bodies are no longer reached.
const HookSpec kHooks[] = {
    {"nGZSceneGraph_cEnvironmentCubeLight_Create", 0xa47bd0, 32, 0x8e6776ee, reinterpret_cast<void *>(&nGZSceneGraph::cEnvironmentCubeLight::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
