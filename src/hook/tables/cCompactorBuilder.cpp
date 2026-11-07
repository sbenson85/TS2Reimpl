// nGZSceneGraph::cCompactorBuilder (lane QE): the factory is the only hook; the other bodies are in effect through our
// vtables.
#include "hook/hooks.h"
#include "sg/cCompactorBuilder.h"

namespace {

const HookSpec kHooks[] = {
    {"nGZSceneGraph_cCompactorBuilder_Create", 0xa83930, 32, 0xe98f068e,
     reinterpret_cast<void *>(&nGZSceneGraph::cCompactorBuilder::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
