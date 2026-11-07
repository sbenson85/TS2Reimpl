#include "hook/hooks.h"
#include "ts/cTSTreeProperties.h"

namespace {
const HookSpec kHooks[] = {
    {"cTSTreeProperties_Construct", 0x41bcd0, 32, 0xa19b7a0f, MemberAddress(&cTSTreeProperties::Construct)},
    {"cTSTreeProperties_DeletingDestructor", 0x41be70, 32, 0x41d25d60, MemberAddress(&cTSTreeProperties::DeletingDestructor)},
    {"cTSTreeProperties_QueryInterface", 0x41c740, 32, 0x588524d6, MemberAddress(&cTSTreeProperties::QueryInterface)},
    {"cTSTreeProperties_Release", 0x41ca70, 26, 0x8b58c6ca, MemberAddress(&cTSTreeProperties::Release)},
    {"cTSTreeProperties_GetGZCLSID", 0x41c3d0, 6, 0xae4a1db3, MemberAddress(&cTSTreeProperties::GetGZCLSID)},
    {"cTSTreeProperties_Initialize", 0x41c620, 32, 0x2eaf190f, MemberAddress(&cTSTreeProperties::Initialize)},
    {"cTSTreeProperties_CopyFrom", 0x41bfb0, 32, 0x7d456f39, MemberAddress(&cTSTreeProperties::CopyFrom)},
    {"cTSTreeProperties_Equals", 0x41c0e0, 32, 0x37e6bfe1, MemberAddress(&cTSTreeProperties::Equals)},
    {"cTSTreeProperties_LoadFromService", 0x41c6f0, 32, 0x1725037c, MemberAddress(&cTSTreeProperties::LoadFromService)},
    {"cTSTreeProperties_SaveToService", 0x41cb90, 32, 0xb380eeaf, MemberAddress(&cTSTreeProperties::SaveToService)},
    {"cTSTreeProperties_Write", 0x41d0b0, 32, 0x33b3c6d9, MemberAddress(&cTSTreeProperties::Write)},
    {"cTSTreeProperties_Read", 0x41c7a0, 32, 0x9bc0c550, MemberAddress(&cTSTreeProperties::Read)},
    {"cTSTreeProperties_InitializeParameters", 0x41cc30, 32, 0xd514cc01, MemberAddress(&cTSTreeProperties::InitializeParameters)},
    {"cTSTreeProperties_GetTree", 0x41c3b0, 18, 0x008d7ffa, MemberAddress(&cTSTreeProperties::GetTree)},
    {"cTSTreeProperties_GetScopeName", 0x41c570, 32, 0x3de9b463, MemberAddress(&cTSTreeProperties::GetScopeName)},
    {"cTSTreeProperties_GetNameCount", 0x41c0b0, 32, 0xeabe8820, MemberAddress(&cTSTreeProperties::GetNameCount)},
    {"cTSTreeProperties_GetName", 0x41c3e0, 32, 0x4407822e, MemberAddress(&cTSTreeProperties::GetName)},
    {"cTSTreeProperties_SetName", 0x41cf20, 32, 0x6b21cdf4, MemberAddress(&cTSTreeProperties::SetName)},
    {"cTSTreeProperties_RemoveName", 0x41caa0, 32, 0x7197112d, MemberAddress(&cTSTreeProperties::RemoveName)},
    {"cTSTreeProperties_GetParameterFlag", 0x41c5b0, 32, 0xa5e5ef95, MemberAddress(&cTSTreeProperties::GetParameterFlag)},
    {"cTSTreeProperties_SetParameterFlag", 0x41d070, 32, 0xa9c0f9dd, MemberAddress(&cTSTreeProperties::SetParameterFlag)},
    {"cTSTreeProperties_ClearFlag23", 0x41d0a0, 5, 0xbbb9be8d, MemberAddress(&cTSTreeProperties::ClearFlag23)},
    {"cTSTreeProperties_SetDescription", 0x41cf00, 24, 0xee0f9183, MemberAddress(&cTSTreeProperties::SetDescription)},
    {"cTSTreeProperties_GetNameTable", 0x41c5e0, 31, 0xa93faff0, MemberAddress(&cTSTreeProperties::GetNameTable)},
};
// Folded shared slots, secondary thunks, and library insert 0x41d2b0 are deliberately unhooked.
REGISTER_HOOKS(kHooks);
}
