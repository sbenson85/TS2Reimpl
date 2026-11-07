#include "hook/hooks.h"
#include "ts/cTSObjectTypeAttributeManager.h"

namespace {

// Standalone shape (lane FP): Create builds our class (the only creation site, so the class's switch: FUN_a04d8b60); the other
// entries are adapters serving direct calls and objects that still carry the game's vtables.
// Slots only the game's vtables reach have no entry (lane GP): their originals are trapped under Hooks.FUN_a04d8b60=1
// ("in_effect": "callers"). Left: DeletingDestructor and Clear (byte matches), QueryInterface, Release and GetGZCLSID
// (referenced from original code).
#define ADAPTER(name) reinterpret_cast<void *>(&cTSObjectTypeAttributeManagerAdapters::name)

const HookSpec kHooks[] = {
    {"FUN_a04d8b60", 0x4d8b60, 32, 0x56cb2764, reinterpret_cast<void *>(&cTSObjectTypeAttributeManager::Create)},
    {"cTSObjectTypeAttributeManager_vf12", 0x4d8a30, 32, 0x9e885200, ADAPTER(DeletingDestructor)},
    {"cTSObjectTypeAttributeManager_QueryInterface", 0x4d8c30, 32, 0xc97929c1, ADAPTER(QueryInterface)},
    {"cTSObjectTypeAttributeManager_GetGZCLSID", 0x4d8c20, 6, 0x63452de8, ADAPTER(GetGZCLSID)},
    {"cTSObjectTypeAttributeManager_vf11", 0x4d9030, 32, 0xccc888e2, ADAPTER(Clear)},
};
REGISTER_HOOKS(kHooks);

} // namespace
