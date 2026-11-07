#include "hook/hooks.h"
#include "sg/SGTreeCode.h"

namespace {

const HookSpec kHooks[] = {
    {"nGZSceneGraph_NodeConnected", 0xa396d0, 32, 0x2b8a503c, &nGZSceneGraph::NodeConnected},
    {"nGZSceneGraph_NodeDisconnecting", 0xa39710, 32, 0xa23b2e1a, &nGZSceneGraph::NodeDisconnecting},
    {"nGZSceneGraph_NodeTransformChanged", 0xa39690, 32, 0x569368d8, &nGZSceneGraph::NodeTransformChanged},
    {"nGZSceneGraph_NodeBoundFlagChanged", 0xa39ae0, 32, 0x67f9e311, &nGZSceneGraph::NodeBoundFlagChanged},
    {"nGZSceneGraph_FindLimitRoot", 0xae7aa0, 32, 0x66ad12af, &nGZSceneGraph::FindLimitRoot},
    {"nGZSceneGraph_GatherLimitTransforms", 0xae7910, 32, 0xfd21060c, &nGZSceneGraph::GatherLimitTransforms},
};
REGISTER_HOOKS(kHooks);

} // namespace
