#include "hook/hooks.h"
#include "sg/cShape.h"

namespace {

// The director's factory: every shape the director makes is ours. Nothing else builds one (the game's Clone 0xa5d070 is
// reached only through its vtable), so the game's constructor and the slots our class replaces are no longer reached.
// The name cell 0xaed300 is hooked rather than trapped: some 20 game functions outside cShape call it too.
const HookSpec kHooks[] = {
    {"nGZSceneGraph_cShape_Create", 0xa5d3b0, 32, 0xc788943d, reinterpret_cast<void *>(&nGZSceneGraph::cShape::Create)},
    {"nGZSceneGraph_MakeNameCell", 0xaed300, 32, 0xe0caac87, reinterpret_cast<void *>(&nGZSceneGraph::MakeNameCell)},
    // The behaviour registry: the game's cAbstractBehaviorTypeT<cIShape> template slots (0xa5cba0, 0xa5ce10, 0xa5e230) still
    // call it. Its holder 0xa59480 is folded into ours (0xa480c0 is hooked in cLinearFogLight.cpp's table).
    {"nGZSceneGraph_cShape_EventRegistry", 0xa5f010, 32, 0x8bdec4d9,
     reinterpret_cast<void *>(&nGZSceneGraph::cShapeBehaviorTraits::RegistryFor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
