#include "hook/hooks.h"
#include "sg/cLooseOctreePartitionNode.h"

namespace {

using nGZSceneGraph::cLooseOctreePartitionNode;

const HookSpec kHooks[] = {
    {"nGZSceneGraph_cLooseOctreePartitionNode_Create", 0xa51ab0, 32, 0x6045ecaa, reinterpret_cast<void *>(&cLooseOctreePartitionNode::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
