#include "hook/hooks.h"
#include "sg/cLinearFogLight.h"

namespace {

// The director's factory: every fog light the director makes is ours. Nothing else builds one (the game's Clone 0xa47280 is
// reached only through its vtable), so the game's fog-light bodies are no longer reached.
const HookSpec kHooks[] = {
    // The simple lights' shared registry (also the parent of the standard lights' and the shape's): its holder 0x3a3e60 is
    // folded into ours; 0xa480c0 is its id-0 form (0xa49210 calls it).
    {"nGZSceneGraph_cSimpleLight_EventRegistry", 0x3a5d00, 32, 0x1d2afa36,
     reinterpret_cast<void *>(&nGZSceneGraph::cSimpleLightBehaviorTraits::RegistryFor)},
    {"nGZSceneGraph_cSimpleLight_EventRegistryNoId", 0xa480c0, 32, 0x8ff279cf,
     reinterpret_cast<void *>(&nGZSceneGraph::SimpleLightEventRegistry)},
    {"nGZSceneGraph_cLinearFogLight_Create", 0xa47c30, 32, 0xa860d732, reinterpret_cast<void *>(&nGZSceneGraph::cLinearFogLight::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
