// cRZLanguageManager (lane LS, src/rz/cRZLanguageManager.h): the creator cTSAppFWCOMDirector registers (class
// 0xa3024279), not a known function start (its CRC computed by hand, checked by tools/hookcrc.py). Every manager is
// ours; the constructor, destructors, slots, thunks and helpers are in effect through it (trapped by
// tools/callers.py --write-traps).
#include "hook/hooks.h"
#include "rz/cRZLanguageManager.h"

namespace {

const HookSpec kHooks[] = {
    {"cRZLanguageManager_Create", 0xc6f338, 32, 0x353f1b71, reinterpret_cast<void *>(&cRZLanguageManager::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
