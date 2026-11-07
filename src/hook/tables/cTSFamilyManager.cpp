#include "hook/hooks.h"
#include "ts/cTSFamilyManager.h"

// Slots only the game's vtables reach have no entry (lane GJ): no object carries those vtables while the Create hook
// FUN_a04b5440 is in, so their originals are trapped under Hooks.FUN_a04b5440=1 ("in_effect": "callers"). The slots left
// keep their adapter: QueryInterface and Release (thunks of original code still reference them), UnregisterMessages
// (byte match through the adapter).
namespace {

const HookSpec kHooks[] = {
    {"FUN_a04b5440", 0x4b5440, 32, 0xae4c1b20, reinterpret_cast<void *>(&cTSFamilyManager::Create)},
    {"cTSFamilyManager_QueryInterface", 0x4b5f50, 32, 0x83f02d3d, reinterpret_cast<void *>(&cTSFamilyManagerAdapters::QueryInterface)},
    {"cTSFamilyManager_Release", 0x4b6880, 23, 0x0474f6b8, reinterpret_cast<void *>(&cTSFamilyManagerAdapters::Release)},
    {"cTSFamilyManager_vf4", 0x4b6e40, 25, 0x565a7c23, reinterpret_cast<void *>(&cTSFamilyManagerAdapters::UnregisterMessages)},
};

REGISTER_HOOKS(kHooks);

} // namespace
