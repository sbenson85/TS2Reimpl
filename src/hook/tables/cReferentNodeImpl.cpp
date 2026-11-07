#include "hook/hooks.h"
#include "sg/cReferentNodeImpl.h"

namespace {

const HookSpec kHooks[] = {
    {"nGZSceneGraph_cReferentNodeImpl_Construct", 0xa446b0, 32, 0xf693c503, MemberAddress(&nGZSceneGraph::cReferentNodeImpl::Construct)},
    {"nGZSceneGraph_cReferentNodeImpl_Destruct", 0xa44710, 32, 0xdba809da, MemberAddress(&nGZSceneGraph::cReferentNodeImpl::Destruct)},
    {"nGZSceneGraph_cReferentNodeImpl_AddReferrer", 0xa44770, 32, 0xd8259c08, MemberAddress(&nGZSceneGraph::cReferentNodeImpl::AddReferrer)},
    {"nGZSceneGraph_cReferentNodeImpl_RemoveReferrer", 0xa44850, 32, 0xc205edb2, MemberAddress(&nGZSceneGraph::cReferentNodeImpl::RemoveReferrer)},
    {"nGZSceneGraph_cReferentNodeImpl_Ask", 0xa447a0, 32, 0x754df977, MemberAddress(&nGZSceneGraph::cReferentNodeImpl::Ask)},
    {"nGZSceneGraph_cReferentNodeImpl_Notify", 0xa44880, 32, 0x3838ba26, MemberAddress(&nGZSceneGraph::cReferentNodeImpl::Notify)},
    {"nGZSceneGraph_cReferentNodeImpl_Read", 0xa447e0, 32, 0xa96afd57, MemberAddress(&nGZSceneGraph::cReferentNodeImpl::Read)},
    {"nGZSceneGraph_cReferentNodeImpl_Write", 0xa449a0, 32, 0x045a834f, MemberAddress(&nGZSceneGraph::cReferentNodeImpl::Write)},
    {"nGZSceneGraph_cReferentNodeImpl_IsKindOf", 0xa448d0, 29, 0x126f2cd9, MemberAddress(&nGZSceneGraph::cReferentNodeImpl::IsKindOf)},
    {"nGZSceneGraph_cReferentNodeImpl_GetTypeInfo", 0xa448f0, 32, 0xcc84c175, MemberAddress(&nGZSceneGraph::cReferentNodeImpl::GetTypeInfo)},
};
REGISTER_HOOKS(kHooks);

} // namespace
