#include "hook/hooks.h"
#include "sg/cLightRefNode.h"

namespace {

const HookSpec kHooks[] = {
    {"FUN_a0a61a80", 0xa61a80, 32, 0x92cdc0b3, reinterpret_cast<void *>(&nGZSceneGraph::cLightRefNode::Create)},
    {"nGZSceneGraph_cLightRefNode_Construct", 0xa60c00, 31, 0x35c32de0, MemberAddress<&nGZSceneGraph::cLightRefNode::Construct>()},
    {"nGZSceneGraph_cLightRefNode_Destruct", 0xa60ea0, 18, 0xa3966643, MemberAddress<&nGZSceneGraph::cLightRefNode::Destruct>()},
    {"nGZSceneGraph_cLightRefNode_vf98", 0xa61150, 32, 0xc270cb76, MemberAddress<&nGZSceneGraph::cLightRefNode::DeletingDestructor>()},
    {"FUN_a0a61d90", 0xa61d90, 32, 0x2e7c2d35, MemberAddress<&nGZSceneGraph::cLightRefNodeImpl::MakeIterator51>()},
};
REGISTER_HOOKS(kHooks);

} // namespace
