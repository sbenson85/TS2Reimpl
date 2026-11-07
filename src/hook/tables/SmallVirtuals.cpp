#include "hook/hooks.h"
#include "misc/SmallVirtuals.h"

namespace {

const HookSpec kHooks[] = {
    {"nGZEvent_cNullEvent_vf6", 0x1559b0, 13, 0xc887428e, MemberAddress(&nGZEvent::cNullEvent::CloneInto)},
    {"nGZEvent_cNullEvent_vf3", 0x157710, 6, 0xdaacabcf, MemberAddress(&nGZEvent::cNullEvent::GetEventType)},
    {"nTSSG_cTSSGObjectsFilter_vf0", 0x3ac280, 28, 0xb6a63bb7, MemberAddress(&nTSSG::cTSSGObjectsFilter::IsIncluded)},
    {"nTSSG_cBasePolygon_vf0", 0x413360, 32, 0xb3e50f2d, MemberAddress(&nTSSG::cBasePolygon::NotifyOwner)},
    {"nGZGraphic4_cError_vf0", 0x15a900, 32, 0x62dbaf5b, MemberAddress(&nGZGraphic4::cError::GetMessage)},
    {"AVcGameTipFilter_A0x1b5bdced_vf0", 0x6549fa, 32, 0xe8c28492, MemberAddress(&cGameTipFilter::IsIncluded)},
    {"cTSUISimCtrl_vf0", 0x77f908, 31, 0x786bb651, MemberAddress(&cTSUISimCtrl::DeletingDestructor)},
    {"cTSUISimCtrl_vf1", 0x7d4f42, 32, 0x66ccd09b, MemberAddress(&cTSUISimCtrl::SetSim)},
    {"nTSSG_cObjectImposterFilter_vf0", 0x3f8950, 32, 0x92fb2b9e, MemberAddress(&nTSSG::cObjectImposterFilter::IsIncluded)},
    {"nRZSceneGraph_cBranchNodesIIDFilter_vf0", 0xaeacb0, 28, 0x4170626f, MemberAddress(&nRZSceneGraph::cBranchNodesIIDFilter::IsIncluded)},
    {"cBranchNodesIIDFilter_Destruct", 0xaea7e0, 7, 0xbcf0d343, MemberAddress(&nRZSceneGraph::cBranchNodesIIDFilter::Destruct)},
    {"nRZSceneGraph_cBranchNodesIIDFilter_vf1", 0xaea800, 32, 0x852de22e, MemberAddress(&nRZSceneGraph::cBranchNodesIIDFilter::DeletingDestructor)},
    {"nRZSceneGraph_cBranchNodesMaskBitAndIIDFilter_vf0", 0xaeacd0, 32, 0xed991286, MemberAddress(&nRZSceneGraph::cBranchNodesMaskBitAndIIDFilter::IsIncluded)},
    {"cBranchNodesMaskBitAndIIDFilter_Destruct", 0xaea7f0, 7, 0xe851cd05, MemberAddress(&nRZSceneGraph::cBranchNodesMaskBitAndIIDFilter::Destruct)},
    {"nRZSceneGraph_cBranchNodesMaskBitAndIIDFilter_vf1", 0xaea820, 32, 0x934452c0, MemberAddress(&nRZSceneGraph::cBranchNodesMaskBitAndIIDFilter::DeletingDestructor)},
    {"AVcTSDirectoryCache_A0xb80aa905_vf0", 0x1f20a0, 32, 0x7c8e500f, MemberAddress(&cTSDirectoryCache::DeletingDestructor)},
    {"cTSWinProcUITool_cSearchFilter_vf0", 0x676aac, 32, 0xbf21a854, MemberAddress(&cTSWinProcUITool::cSearchFilter::IsIncluded)},
    {"AVcTSAnchorFilter_A0xdb977827_vf0", 0x74367d, 32, 0xbb201004, MemberAddress(&cTSAnchorFilter::IsIncluded)},
    {"cTSUserToolTerrainBrush_vf0", 0x79bcea, 31, 0xca001bbb, MemberAddress(&cTSUserToolTerrainBrush::DeletingDestructor)},
    {"cTSUserToolTerrainBrush_vf7", 0x79c51d, 30, 0x1821bf90, MemberAddress(&cTSUserToolTerrainBrush::SetPosition)},
    {"cRZSocketError_vf0", 0x99a83b, 26, 0x854753e9, MemberAddress(&cRZSocketError::SetError)},
    {"cRZSocketError_vf2", 0x99a0ac, 32, 0xcd523c5b, MemberAddress(&cRZSocketError::Describe)},
};
REGISTER_HOOKS(kHooks);

} // namespace
