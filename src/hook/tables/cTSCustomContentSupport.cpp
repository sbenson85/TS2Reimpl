#include "hook/hooks.h"
#include "ts/cTSCustomContentSupport.h"

namespace {

// The factory is the class's switch: registered by address with nTSSG::cDllCOMDirector (class 0xec59ab08), the only place
// the game makes one; the constructor (its only caller) is hooked too. With them on every service carries our vtables; the
// slots, the destructor and the deleting destructor have no entry: only the game's vtables reach their originals, trapped
// under Hooks.cTSCustomContentSupport_Construct=1 ("in_effect": "callers").
const HookSpec kHooks[] = {
    {"cTSCustomContentSupport_Create", 0x3989c0, 32, 0xdfab0c25, reinterpret_cast<void *>(&cTSCustomContentSupport::Create)},
    {"cTSCustomContentSupport_Construct", 0x38e0a0, 32, 0xe7ead2d9,
     reinterpret_cast<void *>(&cTSCustomContentSupportAdapters::Construct)},
    // Not a slot: the original slot 14 (still game code) calls it on our objects.
    {"cTSCustomContentSupport_StateCode", 0x39cb80, 32, 0xdf4a57b7,
     reinterpret_cast<void *>(&cTSCustomContentSupportAdapters::StateCode)},
};
REGISTER_HOOKS(kHooks);

} // namespace
