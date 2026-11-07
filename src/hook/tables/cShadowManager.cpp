#include "hook/hooks.h"
#include "sg/cShadowManager.h"

namespace {

const HookSpec kHooks[] = {
    {"nTSSG_cShadowManager_QueryInterface", 0x2e89a0, 32, 0x4e8709b5, MemberAddress(&nTSSG::cShadowManager::QueryInterface)},
    {"nTSSG_cShadowManager_Release", 0x2e8a10, 21, 0x287544db, MemberAddress(&nTSSG::cShadowManager::Release)},
    {"nTSSG_cShadowManager_vf6", 0x2e8d80, 32, 0x93025ebb, MemberAddress(&nTSSG::cShadowManager::SetOption)},
    {"nTSSG_cShadowManager_vf11", 0x2e7b90, 12, 0x3f168b5d, MemberAddress(&nTSSG::cShadowManager::GetOptionBits)},
    {"nTSSG_cShadowManager_vf13", 0x2e9620, 32, 0x04e496ed, MemberAddress(&nTSSG::cShadowManager::ShadowTexture)},
    {"nTSSG_cShadowManager_vf14", 0x2e9480, 32, 0x4f89193b, MemberAddress(&nTSSG::cShadowManager::GetShadowMatrix)},
    {"nTSSG_cShadowManager_vf15", 0x2e95a0, 32, 0x0f968863, MemberAddress(&nTSSG::cShadowManager::GetShadowScale)},
    {"nTSSG_cShadowManager_vf16", 0x2e9500, 32, 0x014155f8, MemberAddress(&nTSSG::cShadowManager::GetShadowBox)},
    {"nTSSG_cShadowManager_vf17", 0x2e6e10, 32, 0x15d8ead0, MemberAddress(&nTSSG::cShadowManager::DeletingDestructor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
