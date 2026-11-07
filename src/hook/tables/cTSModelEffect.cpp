#include "hook/hooks.h"
#include "ts/cTSModelEffect.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSModelEffect_Create", 0xc06c00, 32, 0x8db9e460, reinterpret_cast<void *>(&cTSModelEffect::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
