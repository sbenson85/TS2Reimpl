// Hooks for the SSL manager service, cRZNetSSLManager (src/rz/cRZNetSSLManager.cpp), lane PW: the standalone shape. The
// constructor (0xc644aa; its one caller in the image, cTSAppFW::AddApplicationServicesHere, is ours and builds the class
// directly) builds our class, so every manager carries our vtables; the slots, the destructors, the thunks and the lock
// helpers (0xc64de3, 0xc64d7a and their callbacks) are reached only through the game's vtables and each other, which
// nothing reaches any more (in effect through callers, trapped by tools/callers.py --write-traps).
#include "hook/hooks.h"
#include "rz/cRZNetSSLManager.h"

namespace {

const HookSpec kHooks[] = {
    {"cRZNetSSLManager_Construct", 0xc644aa, 32, 0xac69a0c8, reinterpret_cast<void *>(&cRZNetSSLManagerAdapters::Construct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
