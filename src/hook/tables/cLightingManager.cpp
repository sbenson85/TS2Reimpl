#include "hook/hooks.h"
#include "sg/cLightingManager.h"

namespace {

const HookSpec kHooks[] = {
    {"nTSSG_cLightingManager_QueryInterface", 0x2dfda0, 32, 0xfbf2750e, MemberAddress(&nTSSG::cLightingManager::QueryInterface)},
    {"nTSSG_cLightingManager_Release", 0x2dfee0, 24, 0x30a85008, MemberAddress(&nTSSG::cLightingManager::Release)},
    {"nTSSG_cLightingManager_vf18", 0x2e1010, 14, 0x7ff8905f, MemberAddress(&nTSSG::cLightingManager::SetFlag44)},
    {"nTSSG_cLightingManager_vf20", 0x2d8370, 32, 0x3b08d09c, MemberAddress(&nTSSG::cLightingManager::Refresh)},
    {"nTSSG_cLightingManager_vf21", 0x2cbd40, 32, 0x76885564, MemberAddress(&nTSSG::cLightingManager::AddSample)},
    {"nTSSG_cLightingManager_vf23", 0x2ded30, 30, 0xd5414f0d, MemberAddress(&nTSSG::cLightingManager::Name)},
    {"nTSSG_cLightingManager_vf25", 0x2db060, 10, 0x7671f290, MemberAddress(&nTSSG::cLightingManager::Value404)},
    {"nTSSG_cLightingManager_vf27", 0x2e3690, 32, 0x02c90c1d, MemberAddress(&nTSSG::cLightingManager::Vector18C)},
    {"nTSSG_cLightingManager_vf28", 0x2d8310, 32, 0x02c90c1d, MemberAddress(&nTSSG::cLightingManager::Vector19C)},
    {"nTSSG_cLightingManager_vf29", 0x2d4e20, 32, 0x02c90c1d, MemberAddress(&nTSSG::cLightingManager::Vector174)},
    {"nTSSG_cLightingManager_vf30", 0x2df7e0, 32, 0x18dd6acf, MemberAddress(&nTSSG::cLightingManager::NegatedDirection)},
    {"nTSSG_cLightingManager_vf31", 0x2df860, 28, 0xf2c7f2f8, MemberAddress(&nTSSG::cLightingManager::Vector2C4)},
    {"nTSSG_cLightingManager_vf34", 0x2ded50, 32, 0x31ebbe9c, MemberAddress(&nTSSG::cLightingManager::Luminance)},
    {"nTSSG_cLightingManager_vf38", 0x2cbc40, 32, 0xa1585fc6, MemberAddress(&nTSSG::cLightingManager::DeletingDestructor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
