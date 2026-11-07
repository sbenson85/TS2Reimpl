#include "hook/hooks.h"
#include "world/WorldMessageData.h"

namespace {

const HookSpec kHooks[] = {
    {"nTSWorld_cSupportChanged_QueryInterface", 0x544380, 32, 0x2f46cb86, MemberAddress(&nTSWorld::cSupportChanged::QueryInterface)},
    {"nTSWorld_cRoofAddSpec_QueryInterface", 0x80f2b0, 32, 0xc5df6da0, MemberAddress(&nTSWorld::cRoofAddSpec::QueryInterface)},
    {"nTSWorld_cRoofRemoveSpec_QueryInterface", 0x80f2f0, 32, 0x46b3f077, MemberAddress(&nTSWorld::cRoofRemoveSpec::QueryInterface)},
    {"nTSWorld_cRoofVisibilityChange_QueryInterface", 0x80f330, 32, 0x2d1bb82e, MemberAddress(&nTSWorld::cRoofVisibilityChange::QueryInterface)},
    {"nTSWorld_cPoolSpec_QueryInterface", 0x811dc0, 32, 0x304e0073, MemberAddress(&nTSWorld::cPoolSpec::QueryInterface)},
    {"nTSWorld_cFencePostAddSpec_QueryInterface", 0x816c00, 32, 0xc3d77656, MemberAddress(&nTSWorld::cFencePostAddSpec::QueryInterface)},
    {"nTSWorld_cWallAddSpec_QueryInterface", 0x816c80, 32, 0x3f946bd7, MemberAddress(&nTSWorld::cWallAddSpec::QueryInterface)},
    {"nTSWorld_cFloorElevationChange_QueryInterface", 0xd11260, 32, 0x955977dc, MemberAddress(&nTSWorld::cFloorElevationChange::QueryInterface)},
    {"nTSWorld_cFloorPatternChange_QueryInterface", 0xd112a0, 32, 0x9a9407d8, MemberAddress(&nTSWorld::cFloorPatternChange::QueryInterface)},
    {"nTSWorld_cFloorVisibilityChange_QueryInterface", 0xd112e0, 32, 0xbf10b689, MemberAddress(&nTSWorld::cFloorVisibilityChange::QueryInterface)},
    {"nTSWorld_cTerrainElevationChange_QueryInterface", 0xd11320, 32, 0xdaeb20d2, MemberAddress(&nTSWorld::cTerrainElevationChange::QueryInterface)},
    {"nTSWorld_cTerrainRectElevationChange_QueryInterface", 0x6b955c, 32, 0x0a657618, MemberAddress(&nTSWorld::cTerrainRectElevationChange::QueryInterface)},
    {"nTSWorld_cAwningUpdateData_QueryInterface", 0x6bce47, 32, 0x64f38d7d, MemberAddress(&nTSWorld::cAwningUpdateData::QueryInterface)},
    {"nTSWorld_cWallPatternChange_QueryInterface", 0x816cc0, 32, 0x155402eb, MemberAddress(&nTSWorld::cWallPatternChange::QueryInterface)},
    {"nTSWorld_cWallRemoveSpec_QueryInterface", 0x816d00, 32, 0x866f0f30, MemberAddress(&nTSWorld::cWallRemoveSpec::QueryInterface)},
    {"nTSWorld_cWaterElevationChange_QueryInterface", 0x8032a0, 32, 0x4dd9a237, MemberAddress(&nTSWorld::cWaterElevationChange::QueryInterface)},
};
REGISTER_HOOKS(kHooks);

} // namespace
