#include "hook/hooks.h"
#include "gz/cGZPersistDBRecord.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZPersistDBRecord_QueryInterface", 0x99cf40, 32, 0x3bcc99a6, MemberAddress(&cGZPersistDBRecord::QueryInterface)},
    {"cGZPersistDBRecord_Release", 0x99d050, 28, 0x4259a917, MemberAddress(&cGZPersistDBRecord::Release)},
    {"cGZPersistDBRecord_SetKey", 0x1e4dd0, 14, 0xfe43a3e7, MemberAddress(&cGZPersistDBRecord::SetKey)},
    {"cGZPersistDBRecord_GetAccessFlags", 0x1df630, 8, 0x42b34632, MemberAddress(&cGZPersistDBRecord::GetAccessFlags)},
    {"cGZPersistDBRecord_Close", 0x99ba60, 32, 0x10ef6ffc, MemberAddress(&cGZPersistDBRecord::Close)},
    {"cGZPersistDBRecord_Abort", 0x99b9e0, 32, 0x0943e0e7, MemberAddress(&cGZPersistDBRecord::Abort)},
    {"cGZPersistDBRecord_QueryChild", 0x1e03f0, 18, 0x35a1dbc8, MemberAddress(&cGZPersistDBRecord::QueryChild)},
    {"cGZPersistDBRecord_SetChild", 0x1e5060, 32, 0xf2a6c912, MemberAddress(&cGZPersistDBRecord::SetChild)},
    {"cGZPersistDBRecord_TestHighFlags", 0x1e0960, 17, 0x1246e566, MemberAddress(&cGZPersistDBRecord::TestHighFlags)},
    {"cGZPersistDBRecord_SetHighFlags", 0x1e5380, 32, 0xec1a85a7, MemberAddress(&cGZPersistDBRecord::SetHighFlags)},
    {"cGZPersistDBRecord_DeletingDestructor", 0x99b960, 32, 0xf2ce60c8, MemberAddress(&cGZPersistDBRecord::DeletingDestructor)},
    {"cGZPersistDBRecord_SetAccessFlags", 0x1e4b20, 22, 0xc76e6bea, MemberAddress(&cGZPersistDBRecord::SetAccessFlags)},
};
REGISTER_HOOKS(kHooks);

} // namespace
