#include "hook/hooks.h"
#include "ts/cTSVisualEffect.h"

// Standalone shape (docs/standalone-shape.md): the pool factory 0xbc9ab0 is the constructor's only caller, so with its hook every
// effect the pool makes carries our three vtables; the slots, destructor and deleting destructor are in effect through them.
namespace {

const HookSpec kHooks[] = {
    {"cTSVisualEffect_cTSVisualEffect", 0xbe86a0, 32, 0x95229706, reinterpret_cast<void *>(&cTSVisualEffectAdapters::Construct)},
    {"cTSVisualEffect_ReplayEvents", 0xbe8c80, 32, 0x4b1f5add, MemberAddress<&cTSVisualEffect::ReplayEvents>()},
};
REGISTER_HOOKS(kHooks);

} // namespace
