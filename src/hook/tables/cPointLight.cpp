#include "hook/hooks.h"
#include "sg/cPointLight.h"

namespace {

// The director's factory (src/rz/COMDirectorData.cpp registers its game address): every point light the director makes is
// ours. The game's constructor and slot bodies stay live for nTSSG::cHalfspacePointLight, built on them.
const HookSpec kHooks[] = {
    {"nGZSceneGraph_cPointLight_Create", 0xa47c90, 32, 0x2f7032af,
     reinterpret_cast<void *>(&nGZSceneGraph::cPointLight::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
