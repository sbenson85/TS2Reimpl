#include "hook/hooks.h"
#include "ts/cTSFloatConstants.h"

namespace {

using Adapters = cTSFloatConstantsAdapters;

const HookSpec kHooks[] = {
    {"cTSFloatConstants_Construct", 0x4df0c0, 32, 0x374f5f1a, reinterpret_cast<void *>(&Adapters::Construct)},
    {"cTSFloatConstants_Destruct", 0x4df260, 14, 0xcf4aecd7, reinterpret_cast<void *>(&Adapters::Destruct)},
    {"cTSFloatConstants_Create", 0x4df2d0, 32, 0xf8eef734, reinterpret_cast<void *>(&cTSFloatConstants::Create)},
    {"cTSFloatConstants_vf7", 0x4df320, 32, 0x5c8c3a3e, reinterpret_cast<void *>(&Adapters::GetFloat)},
    {"cTSFloatConstants_vf12", 0x4df760, 32, 0xe3eae63e, reinterpret_cast<void *>(&Adapters::Remove)},
    {"cTSFloatConstants_vf14", 0x4df4a0, 32, 0xe5682114, reinterpret_cast<void *>(&Adapters::Load)},
    {"cTSFloatConstants_vf15", 0x4df880, 32, 0xa62c443a, reinterpret_cast<void *>(&Adapters::Save)},
};
REGISTER_HOOKS(kHooks);

} // namespace
