#include "hook/hooks.h"
#include "world/c3DArray.h"

namespace {

const HookSpec kHooks[] = {
    {"AV_c3DArray_VcFloorPatternStorageTile_cWorldDB_nTSWorld_nTSWorld_Write", 0xd14160, 32, 0x7166b441, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<nTSWorld::FloorPatternTile>::Write)},
    {"AV_c3DArray_VcFloorPatternStorageTile_cWorldDB_nTSWorld_nTSWorld_Read", 0xd11a70, 32, 0xc053ffbd, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<nTSWorld::FloorPatternTile>::Read)},
    {"AV_c3DArray_VcFloorPatternStorageTile_cWorldDB_nTSWorld_nTSWorld_vf6", 0xd0bf50, 32, 0x2c3736a2, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<nTSWorld::FloorPatternTile>::DeletingDestructor)},
    {"FUN_a0d12260", 0xd12260, 32, 0x029e7601, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<nTSWorld::FloorPatternTile>::Resize)},
    {"AV_c3DArray_VcFloorPatternRotationTile_cWorldDB_nTSWorld_nTSWorld_Write", 0xd14040, 32, 0x7214056f, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<nTSWorld::FloorRotationTile>::Write)},
    {"AV_c3DArray_VcFloorPatternRotationTile_cWorldDB_nTSWorld_nTSWorld_Read", 0xd11930, 32, 0xba176690, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<nTSWorld::FloorRotationTile>::Read)},
    {"AV_c3DArray_VcFloorPatternRotationTile_cWorldDB_nTSWorld_nTSWorld_vf6", 0xd0bee0, 32, 0xe3c41982, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<nTSWorld::FloorRotationTile>::DeletingDestructor)},
    {"AV_c3DArray_M_nTSWorld_Write", 0xd13b30, 32, 0x7214056f, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<float>::Write)},
    {"AV_c3DArray_M_nTSWorld_Read", 0xd113e0, 32, 0xba176690, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<float>::Read)},
    {"AV_c3DArray_M_nTSWorld_vf6", 0xd0bd60, 32, 0x4d4e9cd0, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<float>::DeletingDestructor)},
    {"AV_c3DArray_VcFloorVisibilityTile_cWorldDB_nTSWorld_nTSWorld_Write", 0xd142a0, 32, 0x7166b441, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<nTSWorld::FloorVisibilityTile>::Write)},
    {"AV_c3DArray_VcFloorVisibilityTile_cWorldDB_nTSWorld_nTSWorld_Read", 0xd11ba0, 32, 0x06044c64, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<nTSWorld::FloorVisibilityTile>::Read)},
    {"AV_c3DArray_VcFloorVisibilityTile_cWorldDB_nTSWorld_nTSWorld_vf6", 0xd0bfc0, 32, 0xf1a7ca7a, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<nTSWorld::FloorVisibilityTile>::DeletingDestructor)},
    {"FUN_a0d122e0", 0xd122e0, 32, 0x029e7601, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<nTSWorld::FloorVisibilityTile>::Resize)},
    {"AV_c3DArray_E_nTSWorld_Write", 0x81e6e0, 32, 0x7214056f, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<uint8_t>::Write)},
    {"AV_c3DArray_E_nTSWorld_Read", 0x81df60, 32, 0x168320e4, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<uint8_t>::Read)},
    {"AV_c3DArray_E_nTSWorld_vf6", 0x81d3d0, 32, 0x6b9c3534, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<uint8_t>::DeletingDestructor)},
    {"FUN_a081e070", 0x81e070, 32, 0x194960e3, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<uint8_t>::Resize)},
    {"AV_c3DArray_V_vector_JV_allocator_J_std_std_nTSWorld_Write", 0xd13c60, 32, 0x6421612f, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<nTSWorld::ObjectList>::Write)},
    {"AV_c3DArray_V_vector_JV_allocator_J_std_std_nTSWorld_Read", 0xd11520, 32, 0x7c40d549, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<nTSWorld::ObjectList>::Read)},
    {"AV_c3DArray_V_vector_JV_allocator_J_std_std_nTSWorld_vf6", 0xd0bdd0, 32, 0x1c9121d3, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<nTSWorld::ObjectList>::DeletingDestructor)},
    {"AV_c3DArray_VcDeckFoundationMarkerTile_cWorldDB_nTSWorld_nTSWorld_Write", 0xd13f00, 32, 0x7166b441, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<nTSWorld::DeckFoundationTile>::Write)},
    {"AV_c3DArray_VcDeckFoundationMarkerTile_cWorldDB_nTSWorld_nTSWorld_Read", 0xd117d0, 32, 0xba176690, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<nTSWorld::DeckFoundationTile>::Read)},
    {"AV_c3DArray_VcDeckFoundationMarkerTile_cWorldDB_nTSWorld_nTSWorld_vf6", 0xd0be70, 32, 0xb530927b, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<nTSWorld::DeckFoundationTile>::DeletingDestructor)},
    {"AV_c3DArray_VcAwningTile_nTSWorld_nTSWorld_Write", 0xd13dc0, 32, 0xf891c56f, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<nTSWorld::AwningTile>::Write)},
    {"AV_c3DArray_VcAwningTile_nTSWorld_nTSWorld_Read", 0xd116a0, 32, 0x2770d57e, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<nTSWorld::AwningTile>::Read)},
    {"AV_c3DArray_VcAwningTile_nTSWorld_nTSWorld_vf6", 0xd0be00, 32, 0x037ecb2d, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<nTSWorld::AwningTile>::DeletingDestructor)},
    {"FUN_a0d121e0", 0xd121e0, 32, 0x029e7601, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<nTSWorld::AwningTile>::Resize)},
    {"AV_c3DArray_VcWallTile_cWallGraph_nTSWorld_nTSWorld_Write", 0x8099b0, 32, 0x111999c6, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<nTSWorld::WallTile>::Write)},
    {"AV_c3DArray_VcWallTile_cWallGraph_nTSWorld_nTSWorld_Read", 0x806cd0, 32, 0xba176690, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<nTSWorld::WallTile>::Read)},
    {"AV_c3DArray_VcWallTile_cWallGraph_nTSWorld_nTSWorld_vf6", 0x805910, 32, 0x30e8ee2b, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<nTSWorld::WallTile>::DeletingDestructor)},
    {"FUN_a08072b0", 0x8072b0, 32, 0xb12cc840, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<nTSWorld::WallTile>::Resize)},
    {"AV_c3DArray_VcRoomIDTile_cRoomManager_nTSWorld_nTSWorld_Write", 0x80c490, 32, 0x7166b441, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<nTSWorld::RoomIDTile>::Write)},
    {"AV_c3DArray_VcRoomIDTile_cRoomManager_nTSWorld_nTSWorld_Read", 0x80bc90, 32, 0xba176690, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<nTSWorld::RoomIDTile>::Read)},
    {"AV_c3DArray_VcRoomIDTile_cRoomManager_nTSWorld_nTSWorld_vf6", 0x80a700, 32, 0x1109b13c, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<nTSWorld::RoomIDTile>::DeletingDestructor)},
    {"FUN_a081d2f0", 0x81d2f0, 32, 0x5aa57e54, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<uint8_t>::Destruct)},
    // shared_vf_806aa0: slot 5 of the ten c3DArray copies only (every reference in the image from their vtables).
    {"shared_vf_806aa0", 0x806aa0, 6, 0x9ddde2ab, reinterpret_cast<void *>(&nTSWorld::c3DArrayAdapters<uint8_t>::GetGZCLSID)},
};
REGISTER_HOOKS(kHooks);

} // namespace
