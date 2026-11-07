// Hooks for the network manager service, cRZNetManager (src/rz/cRZNetManager.cpp), lane PZ: the standalone shape. The
// constructor (0xc65932; its one caller in the image, cTSAppFW::AddApplicationServicesHere, is ours and builds the class
// directly) builds our class, so every manager carries our vtables; the slots, the destructors, the thunks, the connection
// state clear (0xc65a40) and the RAS callback (0xc663c5) are reached only through the game's vtables, each other and the
// constructor, which nothing reaches any more (in effect through callers, trapped by tools/callers.py --write-traps).
#include "hook/hooks.h"
#include "rz/cRZNetManager.h"

namespace {

const HookSpec kHooks[] = {
    {"cRZNetManager_Construct", 0xc65932, 32, 0x8f146b99, reinterpret_cast<void *>(&cRZNetManagerAdapters::Construct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
