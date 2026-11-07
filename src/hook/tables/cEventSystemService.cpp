// Hooks for nGZEvent::cEventSystemService (src/gz/cEventSystemService.size.cpp), lane KD: the standalone shape. The creator
// 0x1a8553 (nGZEvent::cEventCOMDirector's factory for class 0x3b1e8967; missing from db/functions.jsonl, 22 bytes, CRC
// computed from the unpacked exe) and the constructor 0x1a84d5 (its only caller is that creator's tail jump) build our
// class, so every service carries our vtables; QueryInterface, the slot-3 jump 0x1a854e and its target 0x1aad98, the
// destructors and the thunks are reached only through the game's vtables, which nothing stores any more (in effect through
// callers, trapped by tools/callers.py --write-traps).
#include "hook/hooks.h"
#include "gz/cEventSystemService.h"

namespace {

const HookSpec kHooks[] = {
    {"nGZEvent_cEventSystemService_Create", 0x1a8553, 22, 0xb6494ed6, reinterpret_cast<void *>(&nGZEvent::cEventSystemService::Create)},
    {"nGZEvent_cEventSystemService_Construct", 0x1a84d5, 32, 0xba855976, reinterpret_cast<void *>(&nGZEvent::cEventSystemServiceAdapters::Construct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
