#include "hook/hooks.h"
#include "ts/cTSSequenceEffect.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSSequenceEffect_Create", 0xc07490, 32, 0xc073a6de, &cTSSequenceEffect::Create},
};
REGISTER_HOOKS(kHooks);

} // namespace
