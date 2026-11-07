#include "hook/hooks.h"
#include "ts/cTSPersistCacheStrategyGenerational.h"

namespace {

using CS = cTSPersistCacheStrategyGenerational;

const HookSpec kHooks[] = {
    {"cTSPersistCacheStrategyGenerational_Release", 0x1fc090, 8, 0x6e3b1323, MemberAddress(&CS::Release)},
    {"cTSPersistCacheStrategyGenerational_vf4", 0x1fc140, 22, 0x180725f9, MemberAddress(&CS::vf4)},
    {"cTSPersistCacheStrategyGenerational_vf11", 0x1fb790, 30, 0xd9643b1c, MemberAddress(&CS::vf11)},
    {"cTSPersistCacheStrategyGenerational_vf14", 0x1fc040, 18, 0x30c5e77a, MemberAddress(&CS::vf14)},
    {"cTSPersistCacheStrategyGenerational_vf17", 0x1fb830, 32, 0x2e5f0ecf, MemberAddress(&CS::vf17)},
    {"cTSPersistCacheStrategyGenerational_vf21", 0x1fc0a0, 32, 0xfe5589cd, MemberAddress(&CS::vf21)},
    {"cTSPersistCacheStrategyGenerational_vf22", 0x1fbd90, 32, 0x4ba9b109, MemberAddress(&CS::vf22)},
    {"cTSPersistCacheStrategyGenerational_vf7", 0x1fbf00, 32, 0x1cd3ed87, MemberAddress(&cTSPersistCacheStrategyGenerational::Get)},
    {"cTSPersistCacheStrategyGenerational_vf8", 0x1fb580, 32, 0x4378af0e, MemberAddress(&cTSPersistCacheStrategyGenerational::Add)},
    {"cTSPersistCacheStrategyGenerational_vf9", 0x1fc160, 32, 0x0337204e, MemberAddress(&cTSPersistCacheStrategyGenerational::Remove)},
    {"cTSPersistCacheStrategyGenerational_vf10", 0x1fbff0, 32, 0x38405913, MemberAddress(&cTSPersistCacheStrategyGenerational::Contains)},
    {"cTSPersistCacheStrategyGenerational_vf12", 0x1fbde0, 32, 0xb4121cc6, MemberAddress(&cTSPersistCacheStrategyGenerational::GetKeys)},
    {"cTSPersistCacheStrategyGenerational_vf13", 0x1fbe70, 32, 0x01a971c2, MemberAddress(&cTSPersistCacheStrategyGenerational::GetKeysOfType)},
    {"cTSPersistCacheStrategyGenerational_vf18", 0x1fb7b0, 32, 0x4846f136, MemberAddress(&cTSPersistCacheStrategyGenerational::Clear)},
    {"cTSPersistCacheStrategyGenerational_vf23", 0x1fb870, 32, 0xeab99960, MemberAddress(&cTSPersistCacheStrategyGenerational::Evict)},
    {"FUN_a01fb4f0", 0x1fb4f0, 32, 0x13875718, MemberAddress(&cTSPersistCacheStrategyGenerational::DeletingDestructor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
