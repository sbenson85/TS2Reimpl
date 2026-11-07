#include "hook/hooks.h"
#include "sg/SGNodeCopies.h"
#include "sg/cLightRefNodeImpl.h"
#include "sg/cViewerRefNodeImpl.h"

namespace {

const HookSpec kHooks[] = {
    {"nGZSceneGraph_cCompositionTreeNode_Construct", 0xa36900, 31, 0x80edad07, reinterpret_cast<void *>(&nGZSceneGraph::node_copies::ConstructCompositionTreeNode)},
    {"nGZSceneGraph_cTransformNode_Construct", 0xa3a000, 31, 0xe0a867f9, reinterpret_cast<void *>(&nGZSceneGraph::node_copies::ConstructTransformNode)},
    {"nGZSceneGraph_cLightRefNodeImpl_Construct", 0xa60c20, 32, 0xe05c0d8e, MemberAddress(&nGZSceneGraph::cLightRefNodeImpl::Construct)},
    {"nGZSceneGraph_cLightRefNodeImpl_Destruct", 0xa60ec0, 32, 0xc87e6ee4, MemberAddress(&nGZSceneGraph::cLightRefNodeImpl::Destruct)},
    {"nGZSceneGraph_cViewerRefNodeImpl_Construct", 0xa644c0, 32, 0x9ead172d, MemberAddress(&nGZSceneGraph::cViewerRefNodeImpl::Construct)},
    {"nGZSceneGraph_cViewerRefNodeImpl_Destruct", 0xa64840, 22, 0x34a01e7a, MemberAddress(&nGZSceneGraph::cViewerRefNodeImpl::Destruct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
