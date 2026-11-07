#include "hook/hooks.h"
#include "sg/SGWorldTransformRefresh.h"

namespace {

const HookSpec kHooks[] = {
    {"nGZSceneGraph_NodeRefreshWorldTransforms", 0xa39b70, 32, 0x415631b1, &nGZSceneGraph::RefreshWorldTransforms},
};
REGISTER_HOOKS(kHooks);

} // namespace
