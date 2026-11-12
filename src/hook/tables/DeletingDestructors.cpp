#include "hook/hooks.h"
#include "misc/DeletingDestructors.h"

namespace {

const HookSpec kHooks[] = {
    {"AVcBranchNodesNoFilter_A0x56c0fdbf_vf1", 0xa35070, 32, 0xe7e39cf3, MemberAddress(&cBranchNodesNoFilter::DeletingDestructor)},
    {"nGZEvent_cIEventHeader_vf0", 0x154e80, 32, 0xf517faa2, MemberAddress(&nGZEvent::cIEventHeader::DeletingDestructor)},
    {"nTSCASCore_cNameMap_vf0", 0xce0920, 32, 0x62a0e727, MemberAddress(&nTSCASCore::cNameMap::DeletingDestructor)},
    {"nTSCASCore_cTSCASLockedObjectManager_vf0", 0xcfe720, 32, 0x79024d3a, MemberAddress(&nTSCASCore::cTSCASLockedObjectManager::DeletingDestructor)},
    {"nPathPlanner_cAStarSearch_vf0", 0x20ac60, 32, 0xad2a8817, MemberAddress(&nPathPlanner::cAStarSearch::DeletingDestructor)},
    {"cSLTokenFieldBase_vf0", 0x9b9f20, 32, 0x6616366a, MemberAddress(&cSLTokenFieldBase::DeletingDestructor)},
    {"cTSDelauneyMesh_vf0", 0xb54680, 32, 0x57f7d43d, MemberAddress(&cTSDelauneyMesh::DeletingDestructor)},
    {"cTSSliceImposterCreator_vf0", 0xb51410, 32, 0x6cad191e, MemberAddress(&cTSSliceImposterCreator::DeletingDestructor)},
    {"HTTPMessage_vf0", 0x98da4f, 32, 0xdfd0464c, MemberAddress(&HTTPMessage::DeletingDestructor)},
    {"TickerMessage_vf0", 0x960123, 31, 0xaff0a6e7, MemberAddress(&TickerMessage::DeletingDestructor)},
    {"cRZProcessMemoryInfo_Destruct", 0x1bab4e, 7, 0x13bd6ac0, MemberAddress(&cRZProcessMemoryInfo::Destruct)},
    {"cRZProcessMemoryInfo_vf0", 0x1babf3, 31, 0xb520f935, MemberAddress(&cRZProcessMemoryInfo::DeletingDestructor)},
    {"cTSRampGenerator_Destruct", 0x7d86ef, 7, 0x9460c3cb, MemberAddress(&cTSRampGenerator::Destruct)},
    {"cTSRampGenerator_vf0", 0x7d86f6, 31, 0xb36496a4, MemberAddress(&cTSRampGenerator::DeletingDestructor)},
    {"cTSUIDaysOfWeekCtrl_vf0", 0x77f3c5, 31, 0x2d02d65e, MemberAddress(&cTSUIDaysOfWeekCtrl::DeletingDestructor)},
    {"cTSUILearnedBehaviorsListCtrl_vf0", 0x78007b, 31, 0x77f41ea5, MemberAddress(&cTSUILearnedBehaviorsListCtrl::DeletingDestructor)},
    {"cTSUISimViewCtrl_vf0", 0x7c3f82, 31, 0x3e2ab749, MemberAddress(&cTSUISimViewCtrl::DeletingDestructor)},
    {"cTSUISimpleThumbnail_vf0", 0x7c5cfa, 31, 0x0b03afc9, MemberAddress(&cTSUISimpleThumbnail::DeletingDestructor)},
    {"cTSUISkillsCtrl_vf0", 0x7c3555, 31, 0xc2a2b8fa, MemberAddress(&cTSUISkillsCtrl::DeletingDestructor)},
    {"cTSUITraitsCtrl_vf0", 0x7c6acb, 31, 0x437c6a0a, MemberAddress(&cTSUITraitsCtrl::DeletingDestructor)},
    {"cTSUIMultiButton_vf0", 0x7d45fa, 31, 0xec86f806, MemberAddress(&cTSUIMultiButton::DeletingDestructor)},
    {"cITSUIModeObject_vf0", 0x61aa47, 32, 0x1ee19ab8, MemberAddress(&cITSUIModeObject::DeletingDestructor)},
    {"cTSUIModeAlbum_vf0", 0x61aaa8, 31, 0x708a9656, MemberAddress(&cTSUIModeAlbum::DeletingDestructor)},
    {"cTSUIModeBuild_vf0", 0x61aac7, 31, 0x4841ccbf, MemberAddress(&cTSUIModeBuild::DeletingDestructor)},
    {"cTSUIModeBuy_vf0", 0x61aae6, 31, 0x16566a4e, MemberAddress(&cTSUIModeBuy::DeletingDestructor)},
    {"cTSUIModeCAS_vf0", 0x61ab05, 31, 0x7a9ead1a, MemberAddress(&cTSUIModeCAS::DeletingDestructor)},
    {"cTSUIModeCinematic_vf0", 0x61ab24, 31, 0xa0b7d3a6, MemberAddress(&cTSUIModeCinematic::DeletingDestructor)},
    {"cTSUIModeLive_vf0", 0x61ab43, 31, 0x9da4a072, MemberAddress(&cTSUIModeLive::DeletingDestructor)},
    {"cTSUIModeNeighborhood_vf0", 0x61ab62, 31, 0x2f932f08, MemberAddress(&cTSUIModeNeighborhood::DeletingDestructor)},
    {"cTSUIModeNone_vf0", 0x61ab81, 31, 0x37167ef4, MemberAddress(&cTSUIModeNone::DeletingDestructor)},
    {"cTSUIModeOptions_vf0", 0x61aba0, 31, 0x468f52a1, MemberAddress(&cTSUIModeOptions::DeletingDestructor)},
    {"cTSUIModePainting_vf0", 0x61abbf, 31, 0xf333bf91, MemberAddress(&cTSUIModePainting::DeletingDestructor)},
    {"cTSUIModeReward_vf0", 0x61abde, 31, 0x6f8d0331, MemberAddress(&cTSUIModeReward::DeletingDestructor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
