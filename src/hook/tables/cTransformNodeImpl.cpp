#include "hook/hooks.h"
#include "sg/cTransformNodeImpl.h"

namespace {

using nGZSceneGraph::cTransformNodeImpl;

const HookSpec kHooks[] = {
    {"nGZSceneGraph_cTransformNodeImpl_cTransformNodeImpl", 0xa3a020, 32, 0x0505f971, MemberAddress(&cTransformNodeImpl::Construct)},
    {"FUN_a0a3a7d0", 0xa3a7d0, 32, 0x5989cd8c, MemberAddress(&cTransformNodeImpl::GetWorldTransform)},
    {"FUN_a0a3a860", 0xa3a860, 32, 0x0ebc87ef, MemberAddress(&cTransformNodeImpl::FromParent34)},
    {"FUN_a0a3a8a0", 0xa3a8a0, 32, 0x79d57f64, MemberAddress(&cTransformNodeImpl::FromParent1c)},
    {"FUN_a0a3a8d0", 0xa3a8d0, 32, 0x0dfb3298, MemberAddress(&cTransformNodeImpl::ToParent34)},
    {"FUN_a0a3a960", 0xa3a960, 32, 0xb3dfc926, MemberAddress(&cTransformNodeImpl::ToParent1c)},
    {"FUN_a0a3a9b0", 0xa3a9b0, 32, 0x57717f1a, MemberAddress(&cTransformNodeImpl::GetAttribute)},
    {"FUN_a0a3aa60", 0xa3aa60, 32, 0xbf734b44, MemberAddress(&cTransformNodeImpl::Read)},
    {"FUN_a0a3ab70", 0xa3ab70, 32, 0xf4282353, MemberAddress(&cTransformNodeImpl::SetLimit)},
    {"FUN_a0a3abb0", 0xa3abb0, 32, 0x890a8fa8, MemberAddress(&cTransformNodeImpl::SetTransform)},
    {"FUN_a0a3abe0", 0xa3abe0, 29, 0xf59ce7ff, MemberAddress(&cTransformNodeImpl::IsKindOf)},
    {"FUN_a0a3ac00", 0xa3ac00, 32, 0xc014037d, MemberAddress(&cTransformNodeImpl::GetTypeInfo)},
    {"FUN_a0a3acb0", 0xa3acb0, 32, 0xd2672c6a, MemberAddress(&cTransformNodeImpl::SetAttribute)},
    {"FUN_a0a3ad50", 0xa3ad50, 32, 0x045a834f, MemberAddress(&cTransformNodeImpl::Write)},
    {"FUN_a0a3ae50", 0xa3ae50, 32, 0x5687da9e, MemberAddress(&cTransformNodeImpl::CloneInto)},
};
REGISTER_HOOKS(kHooks);

} // namespace
