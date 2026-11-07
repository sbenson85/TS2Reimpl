#include "hook/hooks.h"
#include "ts/cTSPersistContext.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSPersistContext_vf9", 0x1fac50, 32, 0x04ae934e, MemberAddress(&cTSPersistContext::vf9)},
    {"cTSPersistContext_QueryInterface", 0x1fabb0, 32, 0xea944a4d, MemberAddress(&cTSPersistContext::QueryInterface)},
    {"cTSPersistContext_vf3", 0x1faaa0, 32, 0xcf676142, MemberAddress(&cTSPersistContext::GetResource)},
    {"cTSPersistContext_vf4", 0x1fa720, 32, 0x0d9ab751, MemberAddress(&cTSPersistContext::AddResource)},
    {"cTSPersistContext_vf5", 0x1fabf0, 32, 0xbcfa6eeb, MemberAddress(&cTSPersistContext::RemoveResource)},
    {"cTSPersistContext_vf6", 0x1fab80, 32, 0x8393fdf1, MemberAddress(&cTSPersistContext::DeleteResource)},
    {"cTSPersistContext_vf7", 0x1faa00, 32, 0xb6361f5e, MemberAddress(&cTSPersistContext::FindIndexedResource)},
    {"cTSPersistContext_vf8", 0x1faa60, 32, 0x00f1850c, MemberAddress(&cTSPersistContext::HasResource)},
    {"cTSPersistContext_vf10", 0x1faf50, 32, 0xb6361f5e, MemberAddress(&cTSPersistContext::SetDirty)},
    {"cTSPersistContext_vf11", 0x1fa990, 32, 0xb6361f5e, MemberAddress(&cTSPersistContext::GetDirty)},
    {"cTSPersistContext_vf12", 0x1faf00, 32, 0xb6361f5e, MemberAddress(&cTSPersistContext::SetMask)},
    {"cTSPersistContext_vf13", 0x1fa940, 32, 0xb6361f5e, MemberAddress(&cTSPersistContext::GetMask)},
    {"cTSPersistContext_vf14", 0x1fad60, 32, 0xd134b173, MemberAddress(&cTSPersistContext::Save)},
    {"cTSPersistContext_vf15", 0x1fa7f0, 32, 0x947ff3f5, MemberAddress(&cTSPersistContext::Clear)},
    {"cTSPersistContext_vf16", 0x1fafb0, 32, 0x61c320ba, MemberAddress(&cTSPersistContext::ReleaseResources)},
    {"FUN_a01fa6e0", 0x1fa6e0, 32, 0xb3d6f9c3, MemberAddress(&cTSPersistContext::DeletingDestructor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
