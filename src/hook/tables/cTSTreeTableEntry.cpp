#include "hook/hooks.h"
#include "simantics/cTSTreeTableEntry.h"

// Standalone shape (docs/standalone-shape.md, lane GN). The switch is the Construct hook (0x430f60, placement new): the game
// builds entries only there (the table's slot 15 0x431410 and Read 0x433b10) and in the copy constructor 0x430d20 (only Write
// calls it, ours). With it on every entry carries our vtable; the slot entries below are adapters for entries built with the
// game's vtable (none while Construct is on). Bisect: turn off Construct and CopyConstruct together.
// Not hooked: the folded slots (shared_vf_*), and DumpText 0x432060 (slot 92, still the game's: our slot forwards to it).
namespace {

using A = cTSTreeTableEntryAdapters;
using E = cTSTreeTableEntry;

const HookSpec kHooks[] = {
    {"cTSTreeTableEntry_Construct", 0x430f60, 32, 0x4d0c7f94, reinterpret_cast<void *>(&A::Construct)},
    {"cTSTreeTableEntry_CopyConstruct", 0x430d20, 32, 0xd165005d, reinterpret_cast<void *>(&A::CopyConstruct)},
    {"cTSTreeTableEntry_Destruct", 0x4310e0, 32, 0x2816e6ff, reinterpret_cast<void *>(&A::Destruct)},
    {"cTSTreeTableEntry_DeletingDestructor", 0x4312e0, 32, 0xfc9290e5, reinterpret_cast<void *>(&A::DeletingDestructor)},
    {"cTSTreeTableEntry_QueryInterface", 0x433a50, 32, 0x026698ee, reinterpret_cast<void *>(&A::QueryInterface)},
    {"cTSTreeTableEntry_Release", 0x434400, 26, 0x5862b966, reinterpret_cast<void *>(&A::Release)},
    {"cTSTreeTableEntry_Write", 0x435a80, 32, 0x8d3d8710, reinterpret_cast<void *>(&A::Write)},
    {"cTSTreeTableEntry_Read", 0x433e60, 32, 0x3ac711d2, reinterpret_cast<void *>(&A::Read)},
    {"cTSTreeTableEntry_ResetToDefaults", 0x4336c0, 32, 0xb2a779e6, reinterpret_cast<void *>(&A::ResetToDefaults)},
    {"cTSTreeTableEntry_CopyFrom", 0x431b30, 32, 0x921d5857, reinterpret_cast<void *>(&A::CopyFrom)},
    {"cTSTreeTableEntry_HasFlags", 0x433700, 17, 0xb08b0aa6, reinterpret_cast<void *>(&A::HasFlags)},
    {"cTSTreeTableEntry_SetMaskedFlags", 0x435200, 27, 0x8f0210e7, reinterpret_cast<void *>(&A::SetMaskedFlags)},
    {"cTSTreeTableEntry_SetFlags", 0x435220, 32, 0xc42fb669, reinterpret_cast<void *>(&A::SetFlags)},
    {"cTSTreeTableEntry_GetCanJoin", 0x432e20, 8, 0x0e194ccc, reinterpret_cast<void *>(&A::GetCanJoin)},
    {"cTSTreeTableEntry_SetCanJoin", 0x435190, 25, 0x2bac627f, reinterpret_cast<void *>(&A::SetCanJoin)},
    {"cTSTreeTableEntry_GetRunImmediately", 0x433090, 9, 0x037628ee, reinterpret_cast<void *>(&A::GetRunImmediately)},
    {"cTSTreeTableEntry_SetRunImmediately", 0x435270, 25, 0xcc5fca90, reinterpret_cast<void *>(&A::SetRunImmediately)},
    {"cTSTreeTableEntry_GetAgeFlags", 0x432e00, 9, 0x56cac370, reinterpret_cast<void *>(&A::GetAgeFlags)},
    {"cTSTreeTableEntry_GetAllowSubQueue", 0x432b90, 9, 0x49a09756, reinterpret_cast<void *>(&A::GetAllowSubQueue)},
    {"cTSTreeTableEntry_SetAllowSubQueue", 0x435130, 29, 0x9151efdc, reinterpret_cast<void *>(&A::SetAllowSubQueue)},
    {"cTSTreeTableEntry_GetAllowOnSelf", 0x432b80, 9, 0x760420ad, reinterpret_cast<void *>(&A::GetAllowOnSelf)},
    {"cTSTreeTableEntry_SetAllowOnSelf", 0x435110, 29, 0x14d6a109, reinterpret_cast<void *>(&A::SetAllowOnSelf)},
    {"cTSTreeTableEntry_GetOverrideDisable", 0x433130, 9, 0xceb847c8, reinterpret_cast<void *>(&A::GetOverrideDisable)},
    {"cTSTreeTableEntry_SetOverrideDisable", 0x435390, 29, 0x928c78a0, reinterpret_cast<void *>(&A::SetOverrideDisable)},
    {"cTSTreeTableEntry_GetPreferredByFurious", 0x433460, 9, 0x64b18f43, reinterpret_cast<void *>(&A::GetPreferredByFurious)},
    {"cTSTreeTableEntry_SetPreferredByFurious", 0x4354a0, 29, 0x31b0a1d4, reinterpret_cast<void *>(&A::SetPreferredByFurious)},
    {"cTSTreeTableEntry_GetTypeSubQueue", 0x433650, 9, 0xf11cf033, reinterpret_cast<void *>(&A::GetTypeSubQueue)},
    {"cTSTreeTableEntry_SetTypeSubQueue", 0x435600, 29, 0x42f3e34b, reinterpret_cast<void *>(&A::SetTypeSubQueue)},
    {"cTSTreeTableEntry_GetResetMotiveDecay", 0x433470, 9, 0xebd31814, reinterpret_cast<void *>(&A::GetResetMotiveDecay)},
    {"cTSTreeTableEntry_SetResetMotiveDecay", 0x4354c0, 29, 0xba434efd, reinterpret_cast<void *>(&A::SetResetMotiveDecay)},
    {"cTSTreeTableEntry_GetParentalControl", 0x433640, 9, 0x41dad09f, reinterpret_cast<void *>(&A::GetParentalControl)},
    {"cTSTreeTableEntry_SetParentalControl", 0x4355e0, 29, 0xc6acc2fa, reinterpret_cast<void *>(&A::SetParentalControl)},
    {"cTSTreeTableEntry_GetFlag5", 0x432e10, 9, 0x9ea11057, reinterpret_cast<void *>(&A::GetFlag5)},
    {"cTSTreeTableEntry_SetFlag5", 0x435170, 25, 0xa3bc6c4d, reinterpret_cast<void *>(&A::SetFlag5)},
    {"cTSTreeTableEntry_GetDebugOnly", 0x432e50, 9, 0x34a8d8dc, reinterpret_cast<void *>(&A::GetDebugOnly)},
    {"cTSTreeTableEntry_SetDebugOnly", 0x4351e0, 29, 0x22302edc, reinterpret_cast<void *>(&A::SetDebugOnly)},
    {"cTSTreeTableEntry_GetAutoFirstSelect", 0x432df0, 9, 0x6ccbc88a, reinterpret_cast<void *>(&A::GetAutoFirstSelect)},
    {"cTSTreeTableEntry_SetAutoFirstSelect", 0x435150, 29, 0xff41670a, reinterpret_cast<void *>(&A::SetAutoFirstSelect)},
    {"cTSTreeTableEntry_IsAutonomyDisabled", 0x4330b0, 8, 0x21c2e354, reinterpret_cast<void *>(&A::IsAutonomyDisabled)},
    {"cTSTreeTableEntry_GetFlag22", 0x433670, 9, 0xdc0de826, reinterpret_cast<void *>(&A::GetFlag22)},
    {"cTSTreeTableEntry_SetFlag22", 0x435630, 29, 0x4548cdb3, reinterpret_cast<void *>(&A::SetFlag22)},
    {"cTSTreeTableEntry_GetMotiveAdCount", 0x4333d0, 32, 0x43864608, reinterpret_cast<void *>(&A::GetMotiveAdCount)},
    {"cTSTreeTableEntry_GetMotiveAdDelta", 0x4333b0, 32, 0x7e67fbed, reinterpret_cast<void *>(&A::GetMotiveAdDelta)},
    {"cTSTreeTableEntry_GetMotiveAdMin", 0x433390, 32, 0x60aecb5e, reinterpret_cast<void *>(&A::GetMotiveAdMin)},
    {"cTSTreeTableEntry_GetMotiveAdType", 0x433440, 32, 0x645b1b63, reinterpret_cast<void *>(&A::GetMotiveAdType)},
    {"cTSTreeTableEntry_HasAdvertisement", 0x431300, 32, 0x9e245e58, reinterpret_cast<void *>(&A::HasAdvertisement)},
    {"cTSTreeTableEntry_GetMotiveAd", 0x433140, 32, 0x0e787941, reinterpret_cast<void *>(&A::GetMotiveAd)},
    {"cTSTreeTableEntry_FindMotiveAd", 0x4331e0, 32, 0x6a6eca7d, reinterpret_cast<void *>(&A::FindMotiveAd)},
    {"cTSTreeTableEntry_SetMotiveAd", 0x4353b0, 32, 0x0efab329, reinterpret_cast<void *>(&A::SetMotiveAd)},
    {"cTSTreeTableEntry_GetMotiveAds", 0x433260, 32, 0x6ca30b47, reinterpret_cast<void *>(&A::GetMotiveAds)},
    {"cTSTreeTableEntry_GetTraitAds", 0x433480, 32, 0x022b371c, reinterpret_cast<void *>(&A::GetTraitAds)},
    {"cTSTreeTableEntry_GetTraitAdValue", 0x4335b0, 32, 0x8951e476, reinterpret_cast<void *>(&A::GetTraitAdValue)},
    {"cTSTreeTableEntry_SetTraitAdValue", 0x4354e0, 32, 0xe56f9459, reinterpret_cast<void *>(&A::SetTraitAdValue)},
    {"cTSTreeTableEntry_ScaleAds", 0x433720, 32, 0x974d97e4, reinterpret_cast<void *>(&A::ScaleAds)},
    {"cTSTreeTableEntry_GetAdValue", 0x4328f0, 32, 0xf5f52a95, reinterpret_cast<void *>(&A::GetAdValue)},
    {"cTSTreeTableEntry_GetMotiveScore", 0x432f00, 32, 0x93bbba70, reinterpret_cast<void *>(&A::GetMotiveScore)},
    {"cTSTreeTableEntry_GetTraitAdFor", 0x432ba0, 32, 0xbe39c49c, reinterpret_cast<void *>(&A::GetTraitAdFor)},
    {"cTSTreeTableEntry_CopyStageToAll", 0x431940, 32, 0x1481d86e, reinterpret_cast<void *>(&A::CopyStageToAll)},
    {"cTSTreeTableEntry_ClearAds", 0x4318c0, 22, 0x80031330, reinterpret_cast<void *>(&A::ClearAds)},
    {"cTSTreeTableEntry_ClearStage", 0x431800, 32, 0xe54b9eba, reinterpret_cast<void *>(&A::ClearStage)},
    {"cTSTreeTableEntry_ClearMotiveAds", 0x4318e0, 32, 0x1f8bd940, reinterpret_cast<void *>(&A::ClearMotiveAds)},
    {"cTSTreeTableEntry_ClearTraitAds", 0x431910, 32, 0x8942cfd9, reinterpret_cast<void *>(&A::ClearTraitAds)},
    {"cTSTreeTableEntry_RecordUse", 0x4352b0, 32, 0x4af455f0, reinterpret_cast<void *>(&A::RecordUse)},
    {"cTSTreeTableEntry_WasRecentlyUsed", 0x434c10, 32, 0x36570b7d, reinterpret_cast<void *>(&A::WasRecentlyUsed)},
    {"cTSTreeTableEntry_ClearUses", 0x434510, 12, 0xa55f22cb, reinterpret_cast<void *>(&A::ClearUses)},
    {"cTSTreeTableEntry_GetAttenuationValue", 0x432cd0, 32, 0x5f0836ba, reinterpret_cast<void *>(&A::GetAttenuationValue)},
    {"cTSTreeTableEntry_SetCustomAttenuation", 0x4351c0, 21, 0x6ba64b19, reinterpret_cast<void *>(&A::SetCustomAttenuation)},
    {"cTSTreeTableEntry_GetName", 0x4330e0, 22, 0x73d4ab49, reinterpret_cast<void *>(&A::GetName)},
    {"cTSTreeTableEntry_SetName", 0x435380, 9, 0x2f92ba44, reinterpret_cast<void *>(&A::SetName)},
    {"cTSTreeTableEntry_Trim", 0x4339d0, 26, 0x98a26c76, reinterpret_cast<void *>(&A::Trim)},
    {"cTSTreeTableEntry_GetAffectsOwnership", 0x432b70, 9, 0x536f7f71, reinterpret_cast<void *>(&A::GetAffectsOwnership)},
    {"cTSTreeTableEntry_SetAffectsOwnership", 0x4350f0, 29, 0xed09b907, reinterpret_cast<void *>(&A::SetAffectsOwnership)},
    // Non-virtual members and the cdecl helpers (one base chain: plain member pointers).
    {"cTSTreeTableEntry_WriteBody", 0x4357c0, 32, 0x4558fbda, MemberAddress(&E::WriteBody)},
    {"cTSTreeTableEntry_ScaleMotiveMin", 0x433850, 32, 0xcb4d3d55, MemberAddress(&E::ScaleMotiveMin)},
    {"cTSTreeTableEntry_ScaleTraitAds", 0x4338a0, 32, 0x99bd8947, MemberAddress(&E::ScaleTraitAds)},
    {"cTSTreeTableEntry_IsAllZero", 0x42ef30, 32, 0xeefe1598, reinterpret_cast<void *>(&E::IsAllZero)},
    {"cTSTreeTableEntry_TrimMotiveAds", 0x42f210, 32, 0x30e455bf, reinterpret_cast<void *>(&E::TrimMotiveAds)},
    {"cTSTreeTableEntry_TrimTraitAds", 0x42ef80, 32, 0x33462d78, reinterpret_cast<void *>(&E::TrimTraitAds)},
};
REGISTER_HOOKS(kHooks);

} // namespace
