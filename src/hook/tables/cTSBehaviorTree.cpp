#include "hook/hooks.h"
#include "simantics/cTSBehaviorTree.h"

namespace {

// Not hooked: the slots shared with other classes (AddRef/Release 0x165e30/0x437f80, the key accessors and the
// 4-byte getters), vf35 (not reimplemented). 0x469551 is the sub-vtable thunk to the destructor (8 bytes, jumps to
// 0x469740 after adjusting this). 0x176f00 and 0x14c060 are the vector helpers the tree shares with other classes.
const HookSpec kHooks[] = {
    {"cTSBehaviorTree_GetGZCLSID", 0x46a120, 6, 0x6d0b577d, MemberAddress(&cTSBehaviorTree::GetClassId)},
    {"cTSBehaviorTree_QueryInterface", 0x46d0e0, 32, 0x9c262cdd, MemberAddress(&cTSBehaviorTree::QueryInterface)},
    {"cTSBehaviorTree_vf12", 0x46bd30, 32, 0x64394ac3, MemberAddress(&cTSBehaviorTree::GetArgCount)},
    {"cTSBehaviorTree_vf14", 0x46a7d0, 32, 0xd4f8af8b, MemberAddress(&cTSBehaviorTree::GetLocalCount)},
    {"cTSBehaviorTree_vf15", 0x46e150, 10, 0x73acecd9, MemberAddress(&cTSBehaviorTree::SetLocalCount)},
    {"cTSBehaviorTree_vf17", 0x46e0d0, 10, 0xcb108bbc, MemberAddress(&cTSBehaviorTree::SetFlags)},
    {"cTSBehaviorTree_vf20", 0x46a190, 5, 0x4d7c0fe8, MemberAddress(&cTSBehaviorTree::GetNodeCount)},
    {"cTSBehaviorTree_vf22", 0x46a170, 30, 0xda2d4a99, MemberAddress(&cTSBehaviorTree::GetNode)},
    {"cTSBehaviorTree_vf24", 0x469910, 32, 0xac684826, MemberAddress(&cTSBehaviorTree::AddNode)},
    {"cTSBehaviorTree_vf26", 0x469c90, 28, 0xe395f40c, MemberAddress(&cTSBehaviorTree::ClearNodes)},
    {"cTSBehaviorTree_vf27", 0x46c3f0, 32, 0x02e9bfe9, MemberAddress(&cTSBehaviorTree::IsJumpTarget)},
    {"cTSBehaviorTree_vf28", 0x46c470, 32, 0x70a65cca, MemberAddress(&cTSBehaviorTree::CheckNode)},
    {"cTSBehaviorTree_vf31", 0x469cb0, 32, 0x0a6488bb, MemberAddress(&cTSBehaviorTree::ExportTo)},
    {"cTSBehaviorTree_vf33", 0x46e170, 9, 0xd9c9dccc, MemberAddress(&cTSBehaviorTree::SetName)},
    {"cTSBehaviorTree_vf34", 0x469a30, 32, 0x7f1a56e6, MemberAddress(&cTSBehaviorTree::AnalyseFlags)},
    {"cTSBehaviorTree_Write", 0x46ef80, 32, 0x0d29d9c6, MemberAddress(&cTSBehaviorTree::Write)},
    {"cTSBehaviorTree_Read", 0x46d530, 32, 0xd95d6e45, MemberAddress(&cTSBehaviorTree::Read)},
    {"cTSBehaviorTree_Create", 0x46a020, 32, 0x7d52adc5, reinterpret_cast<void *>(&cTSBehaviorTree::Create)},
    {"cTSBehaviorTree_DeletingDestructor", 0x469740, 32, 0x1c41cd61, MemberAddress(&cTSBehaviorTree::DeletingDestructor)},
    {"RefPtrVector_Clear", 0x176f00, 32, 0xe502d645, MemberAddress(&RefPtrVector::Clear)},
    {"RefPtrVector_EmplaceReallocate", 0x14aac0, 32, 0x4e6a0731, MemberAddress(&RefPtrVector::EmplaceReallocate)},
    {"RefPtrVector_Destroy", 0x14c060, 32, 0x54a9fcb5, MemberAddress(&RefPtrVector::Destroy)},
};
REGISTER_HOOKS(kHooks);

} // namespace
