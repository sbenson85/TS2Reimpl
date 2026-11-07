// Hooks for cRZCallbackSystemService (src/rz/cRZCallbackSystemService.size.cpp), lane KD: the standalone shape. The
// constructor (0xc68264, called only by cTSAppFW::AddApplicationServicesHere, ours) builds our class, so every service
// carries our vtables; QueryInterface, the destructors and the +4 / +8 thunks are reached only through the game's vtables,
// which nothing stores any more (in effect through callers, trapped by tools/callers.py --write-traps).
#include "hook/hooks.h"
#include "rz/cRZCallbackSystemService.h"

namespace {

const HookSpec kHooks[] = {
    {"cRZCallbackSystemService_Construct", 0xc68264, 32, 0xae5dff05, reinterpret_cast<void *>(&cRZCallbackSystemServiceAdapters::Construct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
