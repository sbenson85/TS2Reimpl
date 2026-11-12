// nTSSG::cWallDiffusionManager (lane 261110c, src/sg/cWallDiffusionManager.h): the constructor and slot 0, the settings,
// the run's and segment's constructors and accessors, the Fixed16 helpers and the angle tests; lane 261110d (row LD2h's
// bottom): the reference test, the packed-colour writers, the gradient and test colours, the point along a run, the
// neighbour search, border block and walk, the ambient colour and the four simple passes. The vector, set and
// allocator instances over the records are library code and stay the game's. Not hooked: the folded setters 0x20c6f0,
// 0x229f60, 0x4099f0, 0x411b70 and 0x411b90, the folded getters 0x410160 and 0x410230, the 4-byte getters 0x410220 and
// 0x410240.
#include "hook/hooks.h"
#include "sg/cWallDiffusionManager.h"

#include <new>

namespace {

using nTSSG::cWallDiffusionManager;
using nTSSG::Fixed16;
using nTSSG::WallDiffusionRun;
using nTSSG::WallDiffusionSegment;

// 0x40bd90 (plain ret).
WallDiffusionSegment *__fastcall SegmentConstruct(WallDiffusionSegment *self, void *)
{
    return ::new (self) WallDiffusionSegment;
}
// 0x40bcd0 (ret 4).
WallDiffusionSegment *__fastcall SegmentCopy(WallDiffusionSegment *self, void *, const WallDiffusionSegment *other)
{
    return ::new (self) WallDiffusionSegment(*other);
}
// 0x40bc90 (plain ret).
WallDiffusionRun *__fastcall RunConstruct(WallDiffusionRun *self, void *) { return ::new (self) WallDiffusionRun; }

const HookSpec kHooks[] = {
    {"nTSSG_cWallDiffusionManager_cWallDiffusionManager", 0x40be50, 32, 0x68ddd0cf,
     reinterpret_cast<void *>(&cWallDiffusionManager::Construct)},
    {"nTSSG_cWallDiffusionManager_vf0", 0x40bff0, 32, 0x685fc23c, MemberAddress(&cWallDiffusionManager::DeletingDestructor)},
    {"nTSSG_cWallDiffusionManager_SetInsideAmbientColor", 0x411b30, 22, 0x01973f0a,
     MemberAddress(&cWallDiffusionManager::SetInsideAmbientColor)},
    {"nTSSG_cWallDiffusionManager_SetOutsideWallAmbient", 0x411b60, 14, 0x54580c33,
     MemberAddress(&cWallDiffusionManager::SetOutsideWallAmbient)},
    {"nTSSG_WallDiffusionSegment_WallDiffusionSegment", 0x40bd90, 32, 0x607f5ce0, reinterpret_cast<void *>(&SegmentConstruct)},
    {"nTSSG_WallDiffusionSegment_WallDiffusionSegment_copy", 0x40bcd0, 32, 0x315bddde, reinterpret_cast<void *>(&SegmentCopy)},
    {"nTSSG_WallDiffusionSegment_Above", 0x4114a0, 7, 0xe6734594, MemberAddress(&WallDiffusionSegment::Above)},
    {"nTSSG_WallDiffusionSegment_Below", 0x4114d0, 7, 0xb2d25bd2, MemberAddress(&WallDiffusionSegment::Below)},
    {"nTSSG_WallDiffusionSegment_SetAbove", 0x411bc0, 28, 0xd8d711d6, MemberAddress(&WallDiffusionSegment::SetAbove)},
    {"nTSSG_WallDiffusionSegment_SetBelow", 0x411c00, 28, 0x6e6fa06f, MemberAddress(&WallDiffusionSegment::SetBelow)},
    {"nTSSG_WallDiffusionRun_WallDiffusionRun", 0x40bc90, 32, 0x26791e8d, reinterpret_cast<void *>(&RunConstruct)},
    {"nTSSG_WallDiffusionRun_SetFlag19", 0x4119e0, 10, 0x54869379, MemberAddress(&WallDiffusionRun::SetFlag19Hook)},
    {"nTSSG_WallDiffusionRun_SetFlag1A", 0x411b50, 10, 0x46333c97, MemberAddress(&WallDiffusionRun::SetFlag1AHook)},
    {"nTSSG_WallDiffusionRun_SegmentCount", 0x410680, 27, 0x43342d9e, MemberAddress(&WallDiffusionRun::SegmentCount)},
    {"nTSSG_WallDiffusionRun_SideCount", 0x410660, 29, 0xf6b020dc, MemberAddress(&WallDiffusionRun::SideCount)},
    {"nTSSG_WallDiffusionRun_FrontLocation", 0x410270, 6, 0x16e0e8ce, MemberAddress(&WallDiffusionRun::FrontLocation)},
    {"nTSSG_WallDiffusionRun_FrontLocationWord2", 0x410310, 6, 0x25fe675a,
     MemberAddress(&WallDiffusionRun::FrontLocationWord2)},
    {"nTSSG_WallDiffusionRun_BackLocation", 0x411420, 9, 0x8ba0f996, MemberAddress(&WallDiffusionRun::BackLocation)},
    {"nTSSG_WallDiffusionRun_FrontCellQuery", 0x411460, 17, 0x6c974395, MemberAddress(&WallDiffusionRun::FrontCellQuery)},
    {"nTSSG_WallDiffusionRun_LocationAt", 0x4114e0, 18, 0x01905376, MemberAddress(&WallDiffusionRun::LocationAt)},
    {"nTSSG_WallDiffusionRun_AboveAt", 0x411480, 20, 0xcc3cdf82, MemberAddress(&WallDiffusionRun::AboveAt)},
    {"nTSSG_WallDiffusionRun_BelowAt", 0x4114b0, 20, 0xcc3cdf82, MemberAddress(&WallDiffusionRun::BelowAt)},
    {"nTSSG_WallDiffusionRun_SetAboveAt", 0x411ba0, 24, 0x8327f347, MemberAddress(&WallDiffusionRun::SetAboveAt)},
    {"nTSSG_WallDiffusionRun_SetBelowAt", 0x411be0, 24, 0x8327f347, MemberAddress(&WallDiffusionRun::SetBelowAt)},
    {"nTSSG_WallDiffusionRun_Clear", 0x40d570, 32, 0xad4b81bc, MemberAddress(&WallDiffusionRun::Clear)},
    {"nTSSG_WallDiffusionRun_SetAngle", 0x411960, 32, 0xdd15803c, MemberAddress(&WallDiffusionRun::SetAngle)},
    {"nTSSG_WallDiffusionRun_Length", 0x410280, 32, 0xd4d0703a, MemberAddress(&WallDiffusionRun::Length)},
    {"nTSSG_Fixed16_SetFromFloat", 0x40bc60, 32, 0x16b93c05, MemberAddress(&Fixed16::SetFromFloat)},
    {"nTSSG_Fixed16_SetFalloff", 0x40bc00, 32, 0x7e147ac3, MemberAddress(&Fixed16::SetFalloff)},
    {"nTSSG_WallAnglesNear", 0x4101f0, 32, 0xc295943d, reinterpret_cast<void *>(&nTSSG::WallAnglesNear)},
    {"nTSSG_WallAnglesQuarterApart", 0x411430, 32, 0x1944ac7b, reinterpret_cast<void *>(&nTSSG::WallAnglesQuarterApart)},
    {"nTSSG_WallOctant", 0x410170, 32, 0x00533b00, reinterpret_cast<void *>(&nTSSG::WallOctant)},
    // Row LD2h's bottom (lane 261110d).
    {"nTSSG_WallSegmentRef_IsValid", 0x410250, 18, 0x5316db40, MemberAddress(&nTSSG::WallSegmentRef::IsValid)},
    {"nTSSG_WallDiffusionSegment_SetPacked", 0x40cf30, 32, 0x2c75e60c, MemberAddress(&WallDiffusionSegment::SetPacked)},
    {"nTSSG_WallDiffusionSegment_SetSidePacked", 0x40d180, 32, 0x2c75e60c,
     MemberAddress(&WallDiffusionSegment::SetSidePacked)},
    {"nTSSG_WallDiffusionSegment_SetRow", 0x40d3c0, 32, 0x2c75e60c, MemberAddress(&WallDiffusionSegment::SetRow)},
    {"nTSSG_WallDiffusionSegment_MergeMax", 0x40d260, 32, 0xfe88ef1e, MemberAddress(&WallDiffusionSegment::MergeMax)},
    {"nTSSG_WallDiffusionSegment_FillBlock", 0x40da80, 32, 0x0f2ca867, MemberAddress(&WallDiffusionSegment::FillBlock)},
    {"nTSSG_WallDiffusionRun_SetPacked", 0x40cec0, 32, 0xe8885854, MemberAddress(&WallDiffusionRun::SetPacked)},
    {"nTSSG_WallDiffusionRun_PaintGradient", 0x40d090, 32, 0x59985874, MemberAddress(&WallDiffusionRun::PaintGradient)},
    {"nTSSG_WallDiffusionRun_PaintTestColors", 0x40d4b0, 32, 0xa6854453,
     MemberAddress(&WallDiffusionRun::PaintTestColors)},
    {"nTSSG_WallDiffusionRun_PointAlong", 0x410760, 32, 0xe8313616, MemberAddress(&WallDiffusionRun::PointAlong)},
    {"nTSSG_WallDiffusionRun_Neighbours", 0x40fe70, 32, 0x15badf79, MemberAddress(&WallDiffusionRun::Neighbours)},
    {"nTSSG_WallDiffusionRun_Walk", 0x4119f0, 32, 0x32da796d, MemberAddress(&WallDiffusionRun::Walk)},
    {"nTSSG_cWallDiffusionManager_AmbientFor", 0x40c720, 32, 0x1f346126, MemberAddress(&cWallDiffusionManager::AmbientFor)},
    {"nTSSG_cWallDiffusionManager_AmbientPass", 0x40d010, 32, 0x910ba34f,
     MemberAddress(&cWallDiffusionManager::AmbientPass)},
    {"nTSSG_cWallDiffusionManager_GradientPass", 0x411c60, 32, 0x7b4e601e,
     MemberAddress(&cWallDiffusionManager::GradientPass)},
    {"nTSSG_cWallDiffusionManager_TestColorPass", 0x411c20, 32, 0x7b4e601e,
     MemberAddress(&cWallDiffusionManager::TestColorPass)},
    {"nTSSG_cWallDiffusionManager_BorderPass", 0x40f5a0, 32, 0x32a05aab, MemberAddress(&cWallDiffusionManager::BorderPass)},
    // Row LD2i, the propagation (lane 261110e). SpreadWords is thiscall-shaped but never reads ecx: ours is stdcall.
    {"nTSSG_WallDiffusionSegment_SpreadWords", 0x411380, 32, 0x0b772370,
     reinterpret_cast<void *>(&WallDiffusionSegment::SpreadWords)},
    {"nTSSG_WallDiffusionSegment_SpreadAlong", 0x410c10, 32, 0x0184d82c,
     MemberAddress(&WallDiffusionSegment::SpreadAlong)},
    {"nTSSG_WallDiffusionSegment_SpreadAcross", 0x410d50, 32, 0xca85465b,
     MemberAddress(&WallDiffusionSegment::SpreadAcross)},
    {"nTSSG_WallDiffusionSegment_SpreadWithin", 0x411160, 32, 0x3f9bb2be,
     MemberAddress(&WallDiffusionSegment::SpreadWithin)},
    {"nTSSG_WallDiffusionRun_DiffuseWithin", 0x410fe0, 32, 0x5e1adea7, MemberAddress(&WallDiffusionRun::DiffuseWithin)},
    {"nTSSG_WallDiffusionRun_DiffuseBetween", 0x4108d0, 32, 0x127a58eb, MemberAddress(&WallDiffusionRun::DiffuseBetween)},
    {"nTSSG_WallDiffusionRun_DiffuseStep", 0x411300, 32, 0x4bf7f70c, MemberAddress(&WallDiffusionRun::DiffuseStep)},
    {"nTSSG_WallDiffusionRun_Diffuse", 0x410890, 32, 0x0b21fcab, MemberAddress(&WallDiffusionRun::Diffuse)},
    {"nTSSG_cWallDiffusionManager_DiffuseMarked", 0x40f0f0, 32, 0xbbc64368,
     MemberAddress(&cWallDiffusionManager::DiffuseMarked)},
    // Row LD2j, Step's maths (lane 261110f): the projection, the two painters and the light that calls them.
    {"nTSSG_WallDiffusionRun_Project", 0x40d5a0, 32, 0xa7e52533, MemberAddress(&WallDiffusionRun::Project)},
    {"nTSSG_WallDiffusionRun_PaintBack", 0x411500, 32, 0xb937e071, MemberAddress(&WallDiffusionRun::PaintBack)},
    {"nTSSG_WallDiffusionRun_PaintAhead", 0x411700, 32, 0x26762063, MemberAddress(&WallDiffusionRun::PaintAhead)},
    {"nTSSG_WallDiffusionRun_Light", 0x40c040, 32, 0xa8a0f613, MemberAddress(&WallDiffusionRun::Light)},
    // Row LD2e, the entry points cDiffusionManager calls (lane 261110g).
    {"nTSSG_cWallDiffusionManager_CollectWalls2", 0x40c620, 32, 0x65233cb8,
     MemberAddress(&cWallDiffusionManager::CollectWalls2)},
    {"nTSSG_cWallDiffusionManager_CollectWalls", 0x40c650, 32, 0x43440099,
     MemberAddress(&cWallDiffusionManager::CollectWalls)},
    {"nTSSG_cWallDiffusionManager_Reset", 0x40eff0, 32, 0x3224ffc2, MemberAddress(&cWallDiffusionManager::Reset)},
    {"nTSSG_cWallDiffusionManager_Mark", 0x40c800, 32, 0xf261ab0a, MemberAddress(&cWallDiffusionManager::Mark)},
    {"nTSSG_cWallDiffusionManager_FindSegment", 0x40f960, 32, 0xb7741b7d,
     MemberAddress(&cWallDiffusionManager::FindSegment)},
    {"nTSSG_cWallDiffusionManager_GetColor", 0x410320, 32, 0xe4b43360, MemberAddress(&cWallDiffusionManager::GetColor)},
    // Row LD2k, the rebuild (lane 261110h).
    {"nTSSG_WallDiffusionRun_AddSegment", 0x40c680, 32, 0x40de1d96, MemberAddress(&WallDiffusionRun::AddSegment)},
    {"nTSSG_cWallDiffusionManager_BuildRuns", 0x411ca0, 32, 0xeb97b09e, MemberAddress(&cWallDiffusionManager::BuildRuns)},
    {"nTSSG_cWallDiffusionManager_BuildAll", 0x4120d0, 32, 0xbda67e20, MemberAddress(&cWallDiffusionManager::BuildAll)},
    {"nTSSG_cWallDiffusionManager_LinkLevels", 0x40d850, 32, 0x80c2215f, MemberAddress(&cWallDiffusionManager::LinkLevels)},
    {"nTSSG_cWallDiffusionManager_Rebuild", 0x412240, 32, 0xacaa6437, MemberAddress(&cWallDiffusionManager::Rebuild)},
    // Row LD2f, Step and the blob pass (lane 261110i).
    {"nTSSG_WallDiffusionSegment_BlendRow", 0x4104e0, 32, 0xabf93f2b, MemberAddress(&WallDiffusionSegment::BlendRow)},
    {"nTSSG_WallDiffusionRun_BlendRowAt", 0x4104a0, 32, 0x6fdcebaf, MemberAddress(&WallDiffusionRun::BlendRowAt)},
    {"nTSSG_cWallDiffusionManager_Step", 0x40c2e0, 32, 0xb7cbcb81, MemberAddress(&cWallDiffusionManager::Step)},
    {"nTSSG_cWallDiffusionManager_BlobPass", 0x40edb0, 32, 0xe1354de5, MemberAddress(&cWallDiffusionManager::BlobPass)},
    {"nTSSG_cWallDiffusionManager_CollectBlobs", 0x40fa40, 32, 0x5cec7d35,
     MemberAddress(&cWallDiffusionManager::CollectBlobs)},
    {"nTSSG_cWallDiffusionManager_Blend", 0x40f6a0, 32, 0x2bcd771b, MemberAddress(&cWallDiffusionManager::Blend)},
    {"nTSSG_cWallDiffusionManager_BlendUnlit", 0x40f800, 32, 0x2bcd771b, MemberAddress(&cWallDiffusionManager::BlendUnlit)},
    {"nTSSG_WallDiffusionRun_BlendLightMap", 0x40f390, 32, 0xc9fc08c1, MemberAddress(&WallDiffusionRun::BlendLightMap)},
    {"nTSSG_WallDiffusionRun_BlendFloors", 0x40f1c0, 32, 0xbf68dc0e, MemberAddress(&WallDiffusionRun::BlendFloors)},
    // Row LD2m, the lights (lane 261110k).
    {"nTSSG_cWallDiffusionManager_ApplyLights", 0x40dd30, 32, 0xa130239d,
     MemberAddress(&cWallDiffusionManager::ApplyLights)},
    {"nTSSG_PackLightColor", 0x4106a0, 32, 0x09e4ef86, reinterpret_cast<void *>(&nTSSG::PackLightColor)},
    {"nTSSG_cWallDiffusionManager_ApplyAllLights", 0x40ed70, 26, 0xe379a69c,
     MemberAddress(&cWallDiffusionManager::ApplyAllLights)},
    {"nTSSG_cWallDiffusionManager_ApplyChangedLights", 0x40ed90, 26, 0xfb261f58,
     MemberAddress(&cWallDiffusionManager::ApplyChangedLights)},
};
REGISTER_HOOKS(kHooks);

} // namespace
