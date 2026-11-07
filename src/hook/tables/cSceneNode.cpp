#include "hook/hooks.h"
#include "sg/cSceneNode.h"

namespace {

const HookSpec kHooks[] = {
    {"nGZSceneGraph_cSceneNode_Create", 0xa4e9b0, 32, 0x6045ecaa,
     reinterpret_cast<void *>(&nGZSceneGraph::cSceneNode::Create)},
    {"nGZSceneGraph_cSceneNode_GetClassId", 0xa4ea10, 6, 0xbd7d1de2,
     reinterpret_cast<void *>(&nGZSceneGraph::cSceneNode::GetClassId)},
};
REGISTER_HOOKS(kHooks);

} // namespace
