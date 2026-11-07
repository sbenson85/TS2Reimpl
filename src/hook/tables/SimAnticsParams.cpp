#include "hook/hooks.h"
#include "simantics/SimAnticsParams.h"
#include "simantics/cTSBehaviorNode.h"

namespace {

// Not hooked: 0xc0ab08 (ReadThenConvert) is folded with unrelated classes (shared_vf_c0ab08); the
// slot 3 constant returners (0x6c6b0, 0x15e510, 0x155a00, 0x18c4d0, 0x15e570) and the do-nothing slot 2
// (0x140fe0) are shared with other classes and verified in their groups.
// 0xc0aacf is the XPrimParam pool's Free, byte-for-byte the cRZFastFixedAllocator2<0x18> Free.
const HookSpec kHooks[] = {
    {"XPrimParam_Construct", 0xc0aa6a, 31, 0xfb94453f, MemberAddress(&XPrimParam::Construct)},
    {"XPrimParam_CopyConstruct", 0xc0a9f5, 32, 0x4c07bbc4, MemberAddress(&XPrimParam::CopyConstruct)},
    {"XPrimParam_AllocateBlock", 0xc0aa89, 10, 0xf2db1625, reinterpret_cast<void *>(&XPrimParam::AllocateBlock)},
    {"XPrimParam_FreeBlock", 0xc0aa93, 32, 0x000ca362, reinterpret_cast<void *>(&XPrimParam::FreeBlock)},
    {"XPrimParam_PoolFree", 0xc0aacf, 32, 0x35499946, MemberAddress(&RZFastFixed0x18::Free)},
    {"cTSBehaviorNode_BuildOperand", 0x46a860, 32, 0x00868e21, reinterpret_cast<void *>(&XPrimParam::BuildForOpcode)},
    {"XPrimParam_Write", 0xc0b0b7, 32, 0x7a03c0c8, MemberAddress(&XPrimParam::Write)},
    {"XPrimParam_Read", 0xc0ac22, 32, 0x023d7162, MemberAddress(&XPrimParam::Read)},
    {"RunPrivateTreeParam_Read", 0xc0abf3, 32, 0x9a82825b, MemberAddress(&XPrimParam::ReadRunPrivateTree)},
    {"BudgetParam_Read", 0xc0ab29, 32, 0xeea58483, MemberAddress(&XPrimParam::ReadBudget)},
    {"MakeActionStringParam_Read", 0xc0ab83, 32, 0x7ecf7091, MemberAddress(&XPrimParam::ReadMakeActionString)},
    {"DialogParam_Read", 0xc0ab4c, 32, 0x3c67059e, MemberAddress(&XPrimParam::ReadDialog)},
    {"SetToNextParam_Convert", 0xc0afd2, 20, 0xd050c4b6, MemberAddress(&XPrimParam::ConvertSetToNext)},
    {"TurnTowardsParam_Convert", 0xc0b072, 32, 0xd4891478, MemberAddress(&XPrimParam::ConvertTurnTowards)},
    {"NewRelationshipParam_Convert", 0xc0ae6e, 32, 0x4a8d095c, MemberAddress(&XPrimParam::ConvertNewRelationship)},
    {"FindGoodLocationParam_Convert", 0xc0ad18, 26, 0x6a214add, MemberAddress(&XPrimParam::ConvertFindGoodLocation)},
    {"GotoRoutingSlotParam_Convert", 0xc0ad32, 32, 0x993a0b6e, MemberAddress(&XPrimParam::ConvertGotoRoutingSlot)},
    {"SnapParam_Convert", 0xc0b02b, 32, 0x932d10d6, MemberAddress(&XPrimParam::ConvertSnap)},
    {"AnimateOverlayParam_Convert", 0xc0accb, 32, 0x5c63f935, MemberAddress(&XPrimParam::ConvertAnimateOverlay)},
    {"MaterialChangeParam_Convert", 0xc0ae3f, 32, 0x1a7dc138, MemberAddress(&XPrimParam::ConvertMaterialChange)},
    {"LookAtParam_Convert", 0xc0ad87, 32, 0xf7d54bbf, MemberAddress(&XPrimParam::ConvertLookAt)},
    {"ManageInventoryParam_Convert", 0xc0addb, 32, 0xe2e08845, MemberAddress(&XPrimParam::ConvertManageInventory)},
    {"SnapIntoParam_Convert", 0xc0afe6, 32, 0xdfa9cd60, MemberAddress(&XPrimParam::ConvertSnapInto)},
    // cTSBehaviorNode slots. Not hooked (shared_vf_* or under 5 bytes): 0x46c110, 0x432e30, 0x4328e0, 0x46bff0,
    // 0x4351b0, 0x4350e0.
    {"cTSBehaviorNode_QueryInterface", 0x46d0b0, 32, 0xc42ca4d5, MemberAddress(&cTSBehaviorNode::QueryInterface)},
    {"cTSBehaviorNode_Release", 0x46dbc0, 28, 0x7ca2eda2, MemberAddress(&cTSBehaviorNode::Release)},
    {"cTSBehaviorNode_GetOpcode", 0x46c100, 13, 0x8689cb06, MemberAddress(&cTSBehaviorNode::GetOpcode)},
    {"cTSBehaviorNode_SetOpcode", 0x46e300, 32, 0xc1438e67, MemberAddress(&cTSBehaviorNode::SetOpcode)},
    {"cTSBehaviorNode_GetOperand", 0x46bc80, 32, 0x7deb9709, MemberAddress(&cTSBehaviorNode::GetOperand)},
    {"cTSBehaviorNode_SetOperand", 0x46e180, 32, 0x13443b80, MemberAddress(&cTSBehaviorNode::SetOperand)},
    {"cTSBehaviorNode_CopyParamTo", 0x46c330, 19, 0x90960e22, MemberAddress(&cTSBehaviorNode::CopyParamTo)},
    {"cTSBehaviorNode_CopyParamFrom", 0x46e3c0, 32, 0x556cbfa5, MemberAddress(&cTSBehaviorNode::CopyParamFrom)},
    {"cTSBehaviorNode_GetFlag", 0x46a270, 10, 0xbdfad6d0, MemberAddress(&cTSBehaviorNode::GetFlag)},
    {"cTSBehaviorNode_SetFlag", 0x46de60, 32, 0xf70ab4e7, MemberAddress(&cTSBehaviorNode::SetFlag)},
    {"cTSBehaviorNode_Write", 0x46ef30, 32, 0xa912eb67, MemberAddress(&cTSBehaviorNode::Write)},
    {"cTSBehaviorNode_Read", 0x46d2b0, 32, 0xf92d4a06, MemberAddress(&cTSBehaviorNode::Read)},
    {"cTSBehaviorNode_GetClassId", 0x46a110, 6, 0x51be9eab, MemberAddress(&cTSBehaviorNode::GetClassId)},
    {"cTSBehaviorNode_DeletingDestructor", 0x469680, 32, 0x2407bce7, MemberAddress(&cTSBehaviorNode::DeletingDestructor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
