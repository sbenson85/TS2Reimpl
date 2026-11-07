#include "hook/hooks.h"
#include "ts/EffectsTransform.h"

namespace {

const HookSpec kHooks[] = {
    {"EffectsTransform_ToLocal", 0x3a34b0, 32, 0xaa3a0f9a, MemberAddress(&fx::EffectsTransform::ToLocal)},
    {"EffectsTransform_Reset", 0xbbc9b0, 32, 0x1d269bfb, MemberAddress(&fx::EffectsTransform::Reset)},
    {"EffectsTransform_Apply", 0xbc1010, 32, 0x02385c94, MemberAddress(&fx::EffectsTransform::Apply)},
};
REGISTER_HOOKS(kHooks);

} // namespace
