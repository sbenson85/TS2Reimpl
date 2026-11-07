#include "hook/hooks.h"
#include "ts/cTSLightEffect.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSLightEffect_Create", 0xc0a140, 32, 0x67f9396d, reinterpret_cast<void *>(&cTSLightEffect::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
