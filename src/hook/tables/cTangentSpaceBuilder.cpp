// nGZSceneGraph::cTangentSpaceBuilder (lane QG): the factory is the only hook; the other bodies are in effect through
// our vtables.
#include "hook/hooks.h"
#include "sg/cTangentSpaceBuilder.h"

namespace {

const HookSpec kHooks[] = {
    {"nGZSceneGraph_cTangentSpaceBuilder_Create", 0xa86bf0, 32, 0xff898498,
     reinterpret_cast<void *>(&nGZSceneGraph::cTangentSpaceBuilder::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
