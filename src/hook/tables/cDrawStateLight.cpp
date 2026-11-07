#include "hook/hooks.h"
#include "sg/cDrawStateLight.h"

namespace {

// The director's factory: every draw-state light the director makes is ours (the game's Clone 0xa47180 is reached only
// through its vtable), so the game's draw-state bodies are no longer reached.
const HookSpec kHooks[] = {
    {"nGZSceneGraph_cDrawStateLight_Create", 0xa47b70, 32, 0x0f94d6f8, reinterpret_cast<void *>(&nGZSceneGraph::cDrawStateLight::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
