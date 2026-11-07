#include "hook/hooks.h"
#include "sg/cExplicitBoundNodeImpl.h"

namespace {

using nGZSceneGraph::cExplicitBoundNodeImpl;

const HookSpec kHooks[] = {
    {"nGZSceneGraph_cExplicitBoundNodeImpl_cExplicitBoundNodeImpl", 0xa4b630, 32, 0xb3d208c8, MemberAddress(&cExplicitBoundNodeImpl::Destruct)},
    {"FUN_a0a4bd60", 0xa4bd60, 32, 0xa96afd57, MemberAddress(&cExplicitBoundNodeImpl::Read)},
    {"FUN_a0a4bdd0", 0xa4bdd0, 29, 0x1715e09b, MemberAddress(&cExplicitBoundNodeImpl::IsKindOf)},
    {"FUN_a0a4bdf0", 0xa4bdf0, 32, 0xd1d127d2, MemberAddress(&cExplicitBoundNodeImpl::GetTypeInfo)},
    {"FUN_a0a4bec0", 0xa4bec0, 32, 0x045a834f, MemberAddress(&cExplicitBoundNodeImpl::Write)},
};
REGISTER_HOOKS(kHooks);

} // namespace
