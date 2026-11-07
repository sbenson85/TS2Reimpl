#include "hook/hooks.h"
#include "ts/cTSDecalEffect.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSDecalEffect_Create", 0xbfd440, 32, 0x0ab9635f, &cTSDecalEffect::Create},
};
REGISTER_HOOKS(kHooks);

} // namespace
