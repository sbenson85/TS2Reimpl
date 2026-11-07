#include "hook/hooks.h"
#include "sg/cObjectGraphNodeImpl.h"

namespace {

const HookSpec kHooks[] = {
    // The behaviour registry of the object-graph nodes: the template slots of cAbstractBehaviorTypeT<cIObjectGraphNode> and the
    // other registries' holders call it. Its holder 0x255910 is folded into ours (0x25f030, its id-0 form, is reached only by 0x3a5d00).
    {"nGZSceneGraph_cObjectGraphNode_EventRegistry", 0x264010, 32, 0x2170c03c,
     reinterpret_cast<void *>(&nGZSceneGraph::cObjectGraphBehaviorTraits::RegistryFor)},
    {"nGZSceneGraph_cObjectGraphNodeImpl_cObjectGraphNodeImpl", 0xa342a0, 32, 0xe958fc1f, MemberAddress(&nGZSceneGraph::cObjectGraphNodeImpl::Destruct)},
    {"FUN_a0a34530", 0xa34530, 32, 0xd73b286a, MemberAddress(&nGZSceneGraph::cObjectGraphNodeImpl::AddExtension)},
    {"FUN_a0a34570", 0xa34570, 32, 0x9a3868c2, MemberAddress(&nGZSceneGraph::cObjectGraphNodeImpl::HasExtension)},
    {"FUN_a0a345a0", 0xa345a0, 32, 0xbb5690c8, MemberAddress(&nGZSceneGraph::cObjectGraphNodeImpl::FindExtension)},
    {"FUN_a0a345f0", 0xa345f0, 32, 0xea7c5022, MemberAddress(&nGZSceneGraph::cObjectGraphNodeImpl::Read)},
    {"FUN_a0a34870", 0xa34870, 32, 0x3ec420a2, MemberAddress(&nGZSceneGraph::cObjectGraphNodeImpl::RemoveExtension)},
    {"FUN_a0a34900", 0xa34900, 32, 0x59bf77e2, MemberAddress(&nGZSceneGraph::cObjectGraphNodeImpl::SetTagHandle)},
    {"FUN_a0a349d0", 0xa349d0, 32, 0x054256c2, MemberAddress(&nGZSceneGraph::cObjectGraphNodeImpl::SetTag)},
    {"FUN_a0a34ac0", 0xa34ac0, 32, 0x2c5c2af2, MemberAddress(&nGZSceneGraph::cObjectGraphNodeImpl::IsKindOf)},
    {"FUN_a0a34af0", 0xa34af0, 32, 0xcdb57807, MemberAddress(&nGZSceneGraph::cObjectGraphNodeImpl::GetTagHandle)},
    {"FUN_a0a34bb0", 0xa34bb0, 32, 0xc8d502c7, MemberAddress(&nGZSceneGraph::cObjectGraphNodeImpl::GetTag)},
    {"FUN_a0a34cb0", 0xa34cb0, 32, 0xd1d127d2, MemberAddress(&nGZSceneGraph::cObjectGraphNodeImpl::GetTypeInfo)},
    {"FUN_a0a34d70", 0xa34d70, 32, 0x54a18d5c, MemberAddress(&nGZSceneGraph::cObjectGraphNodeImpl::Write)},
    {"FUN_a0a34f20", 0xa34f20, 32, 0x11844692, MemberAddress(&nGZSceneGraph::cObjectGraphNodeImpl::CopyTo)},
    {"FUN_a0a34310", 0xa34310, 32, 0xdea36b70, reinterpret_cast<void *>(&nGZSceneGraph::NotifySubordinateAdding)},
    {"FUN_a0a34370", 0xa34370, 32, 0x46b15bff, reinterpret_cast<void *>(&nGZSceneGraph::NotifySubordinateRemoving)},
    {"FUN_a0a34430", 0xa34430, 32, 0x46b15bff, reinterpret_cast<void *>(&nGZSceneGraph::NotifySubordinateAdded)},
    {"FUN_a0a344d0", 0xa344d0, 32, 0xdea36b70, reinterpret_cast<void *>(&nGZSceneGraph::NotifySubordinateRemoved)},
    {"nGZSceneGraph_cSubordinateListChanged_vf1", 0xa342f0, 19, 0x3c249a3d, MemberAddress(&nGZSceneGraph::cSubordinateListChanged::Assign)},
    {"nGZSceneGraph_cSubordinateListChanged_vf3", 0xa34fc0, 6, 0x7bfacba9, MemberAddress(&nGZSceneGraph::cSubordinateListChanged::GetEventType)},
    {"nGZSceneGraph_cSubordinateListChanged_vf6", 0xa34410, 25, 0xb25c45b2, MemberAddress(&nGZSceneGraph::cSubordinateListChanged::CloneInto)},
    {"nGZSceneGraph_cObjectGraph_RegisterEvents", 0x2570a0, 32, 0x3a6cca6b, nGZSceneGraph::cObjectGraphBehaviorTraits::RegisterEventsAdapter},
};
REGISTER_HOOKS(kHooks);

} // namespace
