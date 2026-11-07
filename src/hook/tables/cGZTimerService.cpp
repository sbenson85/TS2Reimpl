// Hooks for cGZTimerService (src/gz/cGZTimerService.size.cpp), lane KD: the standalone shape. The constructor (0xc6d0da; no
// caller in the image, cTSAppFW::AddApplicationServicesHere is ours and builds through the adapter) builds our class, so every
// timer service carries our vtables; QueryInterface, Init, Shutdown, OnTick, the destructors and the thunks are reached only
// through the game's vtables, which nothing stores any more (in effect through callers, trapped by tools/callers.py
// --write-traps). The constructor is missing from db/functions.jsonl: its CRC is zlib's crc32 of its first 32 bytes.
#include "hook/hooks.h"
#include "gz/cGZTimerService.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZTimerService_Construct", 0xc6d0da, 32, 0x1fb9cb0b, reinterpret_cast<void *>(&cGZTimerServiceAdapters::Construct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
