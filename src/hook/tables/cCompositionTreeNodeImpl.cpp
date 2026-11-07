#include "hook/hooks.h"
#include "sg/cCompositionTreeNodeImpl.h"

namespace {

const HookSpec kHooks[] = {
    {"nGZSceneGraph_cCompositionTreeNodeImpl_cCompositionTreeNodeImpl", 0xa36920, 32, 0x0e795ea3, MemberAddress(&nGZSceneGraph::cCompositionTreeNodeImpl::Construct)},
    {"nGZSceneGraph_cCompositionTreeNodeImpl_cCompositionTreeNodeImpl", 0xa36bd0, 32, 0x80a9a5c4, MemberAddress(&nGZSceneGraph::cCompositionTreeNodeImpl::Destruct)},
    {"FUN_a0a37d00", 0xa37d00, 32, 0x24f50ab6, MemberAddress(&nGZSceneGraph::cCompositionTreeNodeImpl::AddChild)},
    {"FUN_a0a38240", 0xa38240, 32, 0x1c557142, MemberAddress(&nGZSceneGraph::cCompositionTreeNodeImpl::RemoveChild)},
    {"FUN_a0a38320", 0xa38320, 29, 0xff38859a, MemberAddress(&nGZSceneGraph::cCompositionTreeNodeImpl::SetFlag)},
    {"FUN_a0a38340", 0xa38340, 29, 0x784e47f2, MemberAddress(&nGZSceneGraph::cCompositionTreeNodeImpl::IsKindOf)},
    {"FUN_a0a38160", 0xa38160, 26, 0x365f6f63, MemberAddress(&nGZSceneGraph::cCompositionTreeNodeImpl::OnEvent)},
    {"FUN_a0a37be0", 0xa37be0, 32, 0x21da525e, MemberAddress(&nGZSceneGraph::cCompositionTreeNodeImpl::OnGraphConnChanged)},
    {"FUN_a0a38180", 0xa38180, 32, 0xfcde5fc9, MemberAddress(&nGZSceneGraph::cCompositionTreeNodeImpl::Read)},
    {"FUN_a0a38360", 0xa38360, 32, 0xd1d127d2, MemberAddress(&nGZSceneGraph::cCompositionTreeNodeImpl::GetTypeInfo)},
    {"FUN_a0a38430", 0xa38430, 32, 0x5f5c59af, MemberAddress(&nGZSceneGraph::cCompositionTreeNodeImpl::Write)},
};
REGISTER_HOOKS(kHooks);

} // namespace
