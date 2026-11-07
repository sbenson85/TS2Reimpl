#include "hook/hooks.h"
#include "ts/cTSWaterEffect.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSWaterEffect_Create", 0xc09210, 32, 0x5d8b2384, &cTSWaterEffect::Create},
};
REGISTER_HOOKS(kHooks);

} // namespace
