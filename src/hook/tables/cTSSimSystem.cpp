#include "hook/hooks.h"
#include "ts/cTSSimSystem.h"

namespace {

// The constructor is the class's switch (every system then carries our vtables; the only creator is cTSSimCOMDirector::
// OnStart 0x416370, ours); the slots, the destructor and the helpers 0x41b1d0 / 0x41ad10 have no entry: only the game's
// vtables and those slots reach their originals, trapped under Hooks.cTSSimSystem_Construct=1 ("in_effect": "callers").
const HookSpec kHooks[] = {
    {"cTSSimSystem_Construct", 0x416810, 32, 0x6f258076, reinterpret_cast<void *>(&cTSSimSystemAdapters::Construct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
