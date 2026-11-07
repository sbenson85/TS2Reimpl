#include "hook/hooks.h"
#include "sg/cIKChain.h"

namespace {

using Proxy = nGZSceneDBAnim::cIKChainConstProxy;
using Sub = nGZSceneDBAnim::cIKChainSubobject;

const HookSpec kHooks[] = {
    {"nGZSceneDBAnim_cIKChainConstProxy_QueryInterface", 0xa331a0, 32, 0x849ad724, MemberAddress(&Proxy::QueryInterface)},
    {"nGZSceneDBAnim_cIKChainConstProxy_vf23", 0xa33110, 11, 0x3d93a3b2, MemberAddress(&Proxy::ElementAddress)},
    {"nGZSceneDBAnim_cIKChainConstProxy_vf25", 0xa33030, 23, 0xd12fc53e, MemberAddress(&Proxy::EntryAddress)},
    {"nGZSceneDBAnim_cIKChainConstProxy_vf46", 0xa33140, 32, 0xe6c6750c, MemberAddress(&Proxy::IndexOf)},
    {"nGZSceneDBAnim_cIKChainConstProxy_vf48", 0xa332e0, 32, 0x1efd0efc, MemberAddress(&Proxy::FindTagged)},
    {"nGZSceneDBAnim_cIKChainConstProxy_vf49", 0xa33350, 32, 0xa2e38126, MemberAddress(&Proxy::AnyElementAccepts)},
    {"nGZSceneDBAnim_cIKChainConstProxy_vf50", 0xa33090, 32, 0x4e9bfd7a, MemberAddress(&Proxy::ContainsValue)},
    {"nGZSceneDBAnim_cIKChain_vf364_1", 0x9e7cb0, 22, 0x246e4193, MemberAddress(&Sub::IncrementWord)},
    // cIKChain::vf364_25 (0x9e7cce) is not hooked: its bytes are folded with shared_vf_2b642e (db/families.jsonl).
};
REGISTER_HOOKS(kHooks);

} // namespace
