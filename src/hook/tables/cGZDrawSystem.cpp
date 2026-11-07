#include "hook/hooks.h"
#include "gz/cGZDrawSystem.h"

namespace {

// The COM director's creator 0x8c0920 (the constructor inline) and the constructor 0x8bfdb0 are the class's switch
// (docs/standalone-shape.md): every draw system carries our vtables, so its slots, Init's buffer engine factory (made
// inline: our cGZDrawContextRenderToBufferFactory), the destructors and the std helpers only they call are in effect
// through their callers (status "in_effect": "callers").
const HookSpec kHooks[] = {
    {"cGZDrawSystem_Create", 0x8c0920, 32, 0x6e67b831, reinterpret_cast<void *>(&cGZDrawSystem::Create)},
    {"cGZDrawSystem_Construct", 0x8bfdb0, 32, 0x141b70b5, reinterpret_cast<void *>(&cGZDrawSystemAdapters::Construct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
