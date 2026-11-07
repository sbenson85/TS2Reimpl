#include "hook/hooks.h"
#include "sg/cCompositionTreeNode.h"

namespace {

const HookSpec kHooks[] = {
    {"FUN_a0a37910", 0xa37910, 32, 0x5c5e0730, reinterpret_cast<void *>(&nGZSceneGraph::cCompositionTreeNode::Create)},
    {"nGZSceneGraph_cCompositionTreeNode_EventRegistry", 0x263f30, 32, 0x89d04cb1, reinterpret_cast<void *>(&nGZSceneGraph::cCompositionTreeNodeBehaviorTraits::RegistryFor)},
    {"nGZSceneGraph_cCompositionTreeNode_RegisterEvents", 0x257030, 32, 0x2842c11e, nGZSceneGraph::cCompositionTreeNodeBehaviorTraits::RegisterEventsAdapter},
};
REGISTER_HOOKS(kHooks);

} // namespace
