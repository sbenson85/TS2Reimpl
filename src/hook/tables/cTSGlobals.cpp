// Hooks for cTSGlobals (src/ts/cTSGlobals.size.cpp), lane KE: the standalone shape. The constructor (0xc2c4cf; its only
// caller, cTSAppFW::AddApplicationServicesHere, is ours and builds through the adapter) builds our class, so every
// cTSGlobals carries our vtables; QueryInterface and its thunks, Init, Shutdown, the setters, the destructor, the deleting
// destructor and its thunk are reached only through the game's vtables, which nothing stores any more (in effect through
// callers, trapped by tools/callers.py --write-traps), except slot 37's setter 0x760d95, which cTSAlbumLoader::vf3 also
// calls directly (its adapter stays hooked). Slots 31 and 56 forward to the AppServices2 bodies (their hooks
// are that group's).
#include "hook/hooks.h"
#include "ts/cTSGlobals.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSGlobals_Construct", 0xc2c4cf, 32, 0x7bc3fd34, reinterpret_cast<void *>(&cTSGlobalsAdapters::Construct)},
    {"cTSGlobals_vf37", 0x760d95, 8, 0xeaeababd, reinterpret_cast<void *>(&cTSGlobalsAdapters::SetNeighborhood)},
};
REGISTER_HOOKS(kHooks);

} // namespace
