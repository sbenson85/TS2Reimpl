// Hooks for the application's property manager, cGZPropertyManagerT<string traits, cGZPropertySetString>
// (src/gz/cGZPropertyManager.size.cpp), lane PU: the standalone shape. The constructor (0xc26045; its one caller in the
// image, cTSAppFW::AddApplicationServicesHere, is ours and builds the class directly) builds our class, so every manager
// carries our vtables; QueryInterface, SetProperty, the notification slots, Shutdown, the destructors, the thunks and the
// internal helpers (0xc28d0f, 0x1bf595, 0x1bf539) are reached only through the game's vtables, which nothing stores any more
// (in effect through callers, trapped by tools/callers.py --write-traps).
#include "hook/hooks.h"
#include "gz/cGZPropertyManager.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZPropertyManagerString_Construct", 0xc26045, 32, 0x719d2ed9, reinterpret_cast<void *>(&cGZPropertyManagerStringAdapters::Construct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
