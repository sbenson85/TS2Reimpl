#include "hook/hooks.h"
#include "world/TSWorld.h"

namespace {

// cWorldDB's QueryInterface, GetGZCLSID and slots 32 / 42 are members of nTSWorld::cWorldDB (src/world/cWorldDB.h, lane JC),
// in effect through its constructor hook.
const HookSpec kHooks[] = {
    {"nTSWorld_cWorldTerrain_QueryInterface", 0x8032e0, 32, 0x8ff0bf76, reinterpret_cast<void *>(&tsworld::WorldTerrainQueryInterface)},
    {"nTSWorld_cWorldRoof_QueryInterface", 0x80f370, 32, 0xb7af2fd3, reinterpret_cast<void *>(&tsworld::WorldRoofQueryInterface)},
    {"nTSWorld_cWorldPool_QueryInterface", 0x811e00, 32, 0x893fde3f, reinterpret_cast<void *>(&tsworld::WorldPoolQueryInterface)},
    {"nTSWorld_cWorldCurvedPool_QueryInterface", 0x812f60, 32, 0xecbf38fa,
     reinterpret_cast<void *>(&tsworld::WorldCurvedPoolQueryInterface)},
    {"nTSWorld_cWallGraph_QueryInterface", 0x806c70, 32, 0x6a7ed51c, reinterpret_cast<void *>(&tsworld::WallGraphQueryInterface)},
    {"nTSWorld_cWallGraph_Release", 0x807240, 21, 0x2ef41bc6, reinterpret_cast<void *>(&tsworld::WallGraphRelease)},
    {"nTSWorld_cWorldOutsideSupport_QueryInterface", 0x81df10, 32, 0x851adb66,
     reinterpret_cast<void *>(&tsworld::WorldOutsideSupportQueryInterface)},
    {"nTSWorld_cPartitionManager_QueryInterface", 0x816c40, 32, 0x40c95b14,
     reinterpret_cast<void *>(&tsworld::PartitionManagerQueryInterface)},
    {"nTSWorld_cProxyManager_QueryInterface", 0x81bbe0, 32, 0xd7565d30, reinterpret_cast<void *>(&tsworld::ProxyManagerQueryInterface)},
    {"nTSWorld_cWorldTerrain_GetGZCLSID", 0x801ff0, 6, 0x5b406db0, reinterpret_cast<void *>(&tsworld::WorldTerrainGetGZCLSID)},
    {"nTSWorld_cWallGraph_GetGZCLSID", 0x806a90, 6, 0x9729ed92, reinterpret_cast<void *>(&tsworld::WallGraphGetGZCLSID)},
    {"nTSWorld_cPartitionManager_vf6", 0x8188e0, 17, 0x4579f7aa, reinterpret_cast<void *>(&tsworld::PartitionManagerSetPair)},
    {"nTSWorld_cPartitionManager_vf5", 0x818900, 31, 0x7ddf056a, reinterpret_cast<void *>(&tsworld::PartitionManagerSetFour)},
    {"nTSWorld_cWorldRoof_GetGZCLSID", 0x80e640, 6, 0xed230e54, reinterpret_cast<void *>(&tsworld::WorldRoofGetGZCLSID)},
    {"nTSWorld_cWorldPool_GetGZCLSID", 0x811280, 6, 0xb2d60a53, reinterpret_cast<void *>(&tsworld::WorldPoolGetGZCLSID)},
    {"nTSWorld_cWorldCurvedPool_GetGZCLSID", 0x812f30, 6, 0x38d7a201, reinterpret_cast<void *>(&tsworld::WorldCurvedPoolGetGZCLSID)},
};
REGISTER_HOOKS(kHooks);

} // namespace
