#include "hook/hooks.h"
#include "sg/cTransformNode.h"

namespace {

const HookSpec kHooks[] = {
    {"FUN_a0a3a6c0", 0xa3a6c0, 32, 0x5bd83b75, reinterpret_cast<void *>(&nGZSceneGraph::cTransformNode::Create)},
    {"nGZSceneGraph_cTransformNode_EventRegistry", 0x2640f0, 32, 0xf0819f95, reinterpret_cast<void *>(&nGZSceneGraph::cTransformNodeBehaviorTraits::RegistryFor)},
    {"nGZSceneGraph_cTransformNode_RegisterEvents", 0x257110, 32, 0xf8dc132c, nGZSceneGraph::cTransformNodeBehaviorTraits::RegisterEventsAdapter},
};
REGISTER_HOOKS(kHooks);

} // namespace
