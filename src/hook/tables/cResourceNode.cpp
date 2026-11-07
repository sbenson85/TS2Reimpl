#include "hook/hooks.h"
#include "sg/cResourceNode.h"

namespace {

const HookSpec kHooks[] = {
    {"nGZSceneGraph_cResourceNode_Create", 0xa503a0, 32, 0x1b8ed346,
     reinterpret_cast<void *>(&nGZSceneGraph::cResourceNode::Create)},
    {"nGZSceneGraph_cResourceNode_GetClassId", 0xa50400, 6, 0x980c5f3d,
     reinterpret_cast<void *>(&nGZSceneGraph::cResourceNode::GetClassId)},
};
REGISTER_HOOKS(kHooks);

} // namespace
