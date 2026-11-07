#include "hook/hooks.h"
#include "sg/cPartitionNodeImpl.h"

namespace {

using nGZSceneGraph::cPartitionNodeImpl;

const HookSpec kHooks[] = {
    {"nGZSceneGraph_cPartitionNodeImpl_cPartitionNodeImpl", 0xadfe10, 18, 0x0dc72ed8, MemberAddress(&cPartitionNodeImpl::Construct)},
    {"nGZSceneGraph_cPartitionNodeImpl_IsKindOf", 0xadfe40, 29, 0x580427a4, MemberAddress(&cPartitionNodeImpl::IsKindOf)},
    {"nGZSceneGraph_cPartitionNodeImpl_GetTypeInfo", 0xadfe60, 32, 0x967111fd, MemberAddress(&cPartitionNodeImpl::GetTypeInfo)},
    {"nGZSceneGraph_cPartitionNodeImpl_Write", 0xadff10, 32, 0x045a834f, MemberAddress(&cPartitionNodeImpl::Write)},
};
REGISTER_HOOKS(kHooks);

} // namespace
