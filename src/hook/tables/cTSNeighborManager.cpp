#include "hook/hooks.h"
#include "ts/cTSNeighborManager.h"

// Create (FUN_a04bc570) is the class's switch: with it on, every manager the game makes carries our vtables. The other entries
// are adapters for direct calls and for managers still carrying the game's vtables (docs/standalone-shape.md), except the
// statics, hooked directly.
// Slots only the game's vtables reach have no entry (lane GP): no object carries those vtables while FUN_a04bc570 is in, so
// their originals are trapped under Hooks.FUN_a04bc570=1 ("in_effect": "callers"). The slots left keep their adapter for a
// byte match (Release, Write, RegisterMessages, UnregisterMessages, SetDirty, SetKey, CountRelatedNeighborsOf) or because
// original code calls them (GetGZCLSID); the non-slot members are called from original code.
#define NBRMGR_ADAPTER(name) reinterpret_cast<void *>(&cTSNeighborManagerAdapters::name)

namespace {

const HookSpec kHooks[] = {
    {"FUN_a04bc570", 0x4bc570, 32, 0x7d89eb6d, reinterpret_cast<void *>(&cTSNeighborManager::Create)},
    {"cTSNeighborManager_Release", 0x4be130, 26, 0xa12f5091, NBRMGR_ADAPTER(Release)},
    {"cTSNeighborManager_GetGZCLSID", 0x4bc6f0, 6, 0x1799f1e4, NBRMGR_ADAPTER(GetGZCLSID)},
    {"cTSNeighborManager_Write", 0x4bf6c0, 32, 0x85632356, NBRMGR_ADAPTER(Write)},
    {"cTSNeighborManager_vf6", 0x4bd270, 25, 0x59167fec, NBRMGR_ADAPTER(RegisterMessages)},
    {"cTSNeighborManager_vf7", 0x4be650, 25, 0x75066edc, NBRMGR_ADAPTER(UnregisterMessages)},
    {"cTSNeighborManager_vf12", 0x4bd470, 5, 0x891851d3, NBRMGR_ADAPTER(SetDirty)},
    {"cTSNeighborManager_SetKey", 0x4be640, 16, 0xd0c59ad7, NBRMGR_ADAPTER(SetKey)},
    {"FUN_a04bfba0", 0x4bfba0, 32, 0xa9606b03, NBRMGR_ADAPTER(SaveNeighbors)},
    {"FUN_a04be670", 0x4be670, 32, 0x0eed3141, reinterpret_cast<void *>(&cTSNeighborManager::RelationsAllowBasic)},
    {"FUN_a04be7f0", 0x4be7f0, 32, 0x77d335ab, reinterpret_cast<void *>(&cTSNeighborManager::RelationsAllowExtended)},
    {"FUN_a04bfd00", 0x4bfd00, 32, 0xe8c39e40, MemberAddress<&cTSNeighborManager::CountRelatedNeighbors>()},
    {"cTSNeighborManager_vf28", 0x4bf4d0, 20, 0x96e67ee8, NBRMGR_ADAPTER(CountRelatedNeighborsOf)},
    {"FUN_a04bdf00", 0x4bdf00, 32, 0x5f8ac23a, NBRMGR_ADAPTER(LoadNeighbors)},
    {"FUN_a04be3f0", 0x4be3f0, 32, 0xe79af68e, NBRMGR_ADAPTER(RemoveNeighborEffects)},
    {"FUN_a04bf4f0", 0x4bf4f0, 32, 0x0de372b6, NBRMGR_ADAPTER(UpdateKeyMap)},
    {"FUN_a04be170", 0x4be170, 32, 0xc0d2b8ff, reinterpret_cast<void *>(&cTSNeighborManager::RemapLotEntry)},
};
REGISTER_HOOKS(kHooks);

} // namespace
