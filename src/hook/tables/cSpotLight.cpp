#include "hook/hooks.h"
#include "sg/cSpotLight.h"

namespace {

// The director's factory: every spot light the director makes is ours. Nothing else builds one (the game's Clone 0xa47380
// is reached only through its vtable), so the game's spot-light bodies are no longer reached.
const HookSpec kHooks[] = {
    {"nGZSceneGraph_cSpotLight_Create", 0xa47cf0, 32, 0xa1bb39b6,
     reinterpret_cast<void *>(&nGZSceneGraph::cSpotLight::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
