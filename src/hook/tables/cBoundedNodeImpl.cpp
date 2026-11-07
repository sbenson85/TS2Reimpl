#include "hook/hooks.h"
#include "sg/cBoundedNodeImpl.h"

namespace {

using nGZSceneGraph::cBoundedNodeImpl;

const HookSpec kHooks[] = {
    {"FUN_a0a3b290", 0xa3b290, 32, 0xc09a6d32, MemberAddress(&cBoundedNodeImpl::Construct)},
    {"FUN_a0a3b300", 0xa3b300, 32, 0xb1c81d8f, MemberAddress(&cBoundedNodeImpl::GetBoundBox)},
    {"FUN_a0a3b3b0", 0xa3b3b0, 32, 0x4ff2250a, MemberAddress(&cBoundedNodeImpl::GetBoundBoxIn)},
    {"FUN_a0a3b4a0", 0xa3b4a0, 32, 0x08aa077a, MemberAddress(&cBoundedNodeImpl::Read)},
    {"FUN_a0a3b530", 0xa3b530, 32, 0x344b12f8, MemberAddress(&cBoundedNodeImpl::SetBoundFlag)},
    {"FUN_a0a3b5a0", 0xa3b5a0, 29, 0x095cd7d7, MemberAddress(&cBoundedNodeImpl::IsKindOf)},
    {"FUN_a0a3b5c0", 0xa3b5c0, 32, 0xd0b8a566, MemberAddress(&cBoundedNodeImpl::GetTypeInfo)},
    {"FUN_a0a3b670", 0xa3b670, 32, 0x045a834f, MemberAddress(&cBoundedNodeImpl::Write)},
};
REGISTER_HOOKS(kHooks);

} // namespace
