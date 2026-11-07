#include "hook/hooks.h"
#include "audio/cEMObjectQualityEffect.h"

namespace {

const HookSpec kHooks[] = {
    {"cEMObjectQualityEffect_vf3", 0x240418, 32, 0xea3f92b7, MemberAddress(&cEMObjectQualityEffect::Init)},
    {"cEMObjectQualityEffect_vf5", 0x2410fe, 32, 0xac59b502, MemberAddress(&cEMObjectQualityEffect::Apply)},
};
REGISTER_HOOKS(kHooks);

} // namespace
