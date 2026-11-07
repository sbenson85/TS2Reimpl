#include "hook/hooks.h"
#include "ts/cTSWantDefinition.h"

namespace {

// The constructor builds ours (placement new), so every definition carries our vtables. The slots have no entry: only the
// game vtables 0xdd8548 / 0xdd85a4 refer to the class's own bodies, stored only by the constructor and the destructor
// 0x5b12d0 ("in_effect": "callers"; the shared bodies other classes also use are not ours to hook).
const HookSpec kHooks[] = {
    {"cTSWantDefinition_Construct", 0x5b10e0, 32, 0x85412cd9, reinterpret_cast<void *>(&cTSWantDefinitionAdapters::Construct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
