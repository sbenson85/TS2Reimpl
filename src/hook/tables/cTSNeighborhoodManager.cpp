#include "hook/hooks.h"
#include "ts/cTSNeighborhoodManager.h"

// Slots only the game's vtables reach have no entry (lane GJ): no object carries those vtables while the Create hook
// FUN_a04a3f20 is in, so their originals are trapped under Hooks.FUN_a04a3f20=1 ("in_effect": "callers").
namespace {

#define ADAPTER(name) reinterpret_cast<void *>(&cTSNeighborhoodManagerAdapters::name)

const HookSpec kHooks[] = {
    {"FUN_a04a3f20", 0x4a3f20, 32, 0xf9f56878, reinterpret_cast<void *>(&cTSNeighborhoodManager::Create)},
    {"cTSNeighborhoodManager_QueryInterface", 0x4a5480, 32, 0xf759515b, ADAPTER(QueryInterface)},
    {"cTSNeighborhoodManager_Release", 0x4a5540, 23, 0x919d9472, ADAPTER(Release)},
    {"cTSNeighborhoodManager_GetGZCLSID", 0x4a4050, 6, 0x0e62a7fe, ADAPTER(GetGZCLSID)},
    {"cTSNeighborhoodManager_vf7", 0x4a3f10, 13, 0xffb62fec, ADAPTER(First)},
    {"FUN_a04a6000", 0x4a6000, 32, 0x7f069034, ADAPTER(HasPackage)},
    {"FUN_a04a27a0", 0x4a27a0, 32, 0x6e880b66, ADAPTER(AddTutorial)},
    {"FUN_a04a2950", 0x4a2950, 32, 0x09f77387, ADAPTER(DeleteFiles)},
};
REGISTER_HOOKS(kHooks);

} // namespace
