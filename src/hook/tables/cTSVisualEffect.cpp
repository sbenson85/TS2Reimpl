#include "hook/hooks.h"
#include "ts/cTSVisualEffect.h"

// Standalone shape (docs/standalone-shape.md): the pool factory 0xbc9ab0 is the constructor's only caller, so with its hook every
// effect the pool makes carries our three vtables; the slots, destructor and deleting destructor are in effect through them.
namespace {

const HookSpec kHooks[] = {
    {"cTSVisualEffect_cTSVisualEffect", 0xbe86a0, 32, 0x95229706, reinterpret_cast<void *>(&cTSVisualEffectAdapters::Construct)},
    {"cTSVisualEffect_ReplayEvents", 0xbe8c80, 32, 0x4b1f5add, MemberAddress<&cTSVisualEffect::ReplayEvents>()},
    {"FUN_a0be9670", 0xbe9670, 32, 0xb7c84041, MemberAddress<&cTSVisualEffect::Describe>()},
    {"FUN_a0be8de0", 0xbe8de0, 32, 0x3fd46552, MemberAddress<&cTSVisualEffect::IsDone>()},
    {"FUN_a0bc5380", 0xbc5380, 32, 0xabe8a97a, reinterpret_cast<void *>(&cTSVisualEffectAdapters::DestructChild)},
};
REGISTER_HOOKS(kHooks);

} // namespace
