#include "hook/hooks.h"
#include "ts/cTSSoundEffect.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSSoundEffect_Create", 0xc05670, 32, 0xb26b8d81, &cTSSoundEffect::Create},
};
REGISTER_HOOKS(kHooks);

} // namespace
