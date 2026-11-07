#include "hook/hooks.h"
#include "sg/cSceneManager.h"

namespace {

const HookSpec kHooks[] = {
    {"nTSSG_cSceneManager_QueryInterface", 0x2bfa90, 32, 0x0237fe9d, MemberAddress(&nTSSG::cSceneManager::QueryInterface)},
    {"nTSSG_cSceneManager_Release", 0x2bfac0, 21, 0x049ee5c5, MemberAddress(&nTSSG::cSceneManager::Release)},
    {"nTSSG_cSceneManager_vf16", 0x2bff90, 32, 0x8a489b5a, MemberAddress(&nTSSG::cSceneManager::SetRef520)},
    {"nTSSG_cSceneManager_vf20", 0x2be740, 8, 0x76481423, MemberAddress(&nTSSG::cSceneManager::LastFrame)},
    {"nTSSG_cSceneManager_vf21", 0x2be7f0, 32, 0xe2905288, MemberAddress(&nTSSG::cSceneManager::FrameValue)},
    {"nTSSG_cSceneManager_vf22", 0x2be820, 32, 0x9597621e, MemberAddress(&nTSSG::cSceneManager::FrameFlag)},
    {"nTSSG_cSceneManager_vf23", 0x2be6f0, 32, 0xcc207748, MemberAddress(&nTSSG::cSceneManager::Tick)},
    {"nTSSG_cSceneManager_vf24", 0x2be6e0, 9, 0x2c1bb90b, MemberAddress(&nTSSG::cSceneManager::Name1)},
    {"nTSSG_cSceneManager_vf25", 0x2be6d0, 15, 0x4e284e81, MemberAddress(&nTSSG::cSceneManager::Name10)},
    {"nTSSG_cSceneManager_vf26", 0x2c00c0, 32, 0x1931de0d, MemberAddress(&nTSSG::cSceneManager::ResetSamples)},
    {"nTSSG_cSceneManager_vf28", 0x2bde20, 32, 0x9e7373aa, MemberAddress(&nTSSG::cSceneManager::DeletingDestructor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
