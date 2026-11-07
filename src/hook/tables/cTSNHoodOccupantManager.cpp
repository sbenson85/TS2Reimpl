#include "hook/hooks.h"
#include "ts/cTSNHoodOccupantManager.h"

namespace {

#define OCC_ADAPTER(name) reinterpret_cast<void *>(&cTSNHoodOccupantManagerAdapters::name)

// Construct and Create are the switch (docs/standalone-shape.md): with them on, every manager the game makes is ours, with our
// vtables; the other entries are the adapters for old entry points.
const HookSpec kHooks[] = {
    {"cTSNHoodOccupantManager_GetGZCLSID", 0x4a9250, 6, 0x375cf81f, OCC_ADAPTER(GetGZCLSID)},
    {"cTSNHoodOccupantManager_vf8_24", 0x4a9430, 19, 0x8984a204, OCC_ADAPTER(GetOccupantLayerMask)},
    {"cTSNHoodOccupantManager_vf8_12", 0x4a8ef0, 32, 0xf0aab872, OCC_ADAPTER(CreateFloraOccupant)},
    {"cTSNHoodOccupantManager_vf8_18", 0x4a8f50, 32, 0x2717937d, OCC_ADAPTER(CreatePropOccupant)},
    {"cTSNHoodOccupantManager_vf8_14", 0x4a8fb0, 32, 0xc3d517b6, OCC_ADAPTER(CreateRoadOccupant)},
    {"cTSNHoodOccupantManager_vf8_16", 0x4a9010, 32, 0xb27f94c2, OCC_ADAPTER(CreateModelRoadOccupant)},
    {"cTSNHoodOccupantManager_Construct", 0x4a7ff0, 32, 0x2c7639a1, OCC_ADAPTER(Construct)},
    {"cTSNHoodOccupantManager_Create", 0x4a91f0, 32, 0x62cff8d2, reinterpret_cast<void *>(&cTSNHoodOccupantManager::Create)},
    {"cTSNHoodOccupantManager_vf3", 0x4a84a0, 32, 0x4563143f, OCC_ADAPTER(DeletingDestructor)},
    {"cTSNHoodOccupantManager_QueryInterface", 0x4aa090, 32, 0xaf8141ab, OCC_ADAPTER(QueryInterface)},
    {"cTSNHoodOccupantManager_InitGrid", 0x4a9b10, 32, 0xe1efe25b, OCC_ADAPTER(InitGrid)},
    {"cTSNHoodOccupantManager_BoundsToCellRect", 0x4a9370, 32, 0xfe5af7ca, OCC_ADAPTER(BoundsToCellRect)},
    {"cTSNHoodOccupantManager_InsertIntoGrid", 0x4a9c10, 32, 0x9d650dda, OCC_ADAPTER(InsertIntoGrid)},
    {"cTSNHoodOccupantManager_RemoveFromGrid", 0x4aa550, 32, 0xab0f7bb7, OCC_ADAPTER(RemoveFromGrid)},
    {"cTSNHoodOccupantManager_AddOccupantInternal", 0x4a9e10, 32, 0xc6157e00, OCC_ADAPTER(AddOccupantInternal)},
    {"cTSNHoodOccupantManager_vf8_9", 0x4aa070, 30, 0x5866d130, OCC_ADAPTER(UpdateOccupantPosition)},
    {"cTSNHoodOccupantManager_vf8_3", 0x4a9ad0, 32, 0x4c08d16e, OCC_ADAPTER(Initialize)},
    {"cTSNHoodOccupantManager_vf8_4", 0x4aad00, 32, 0x079a870a, OCC_ADAPTER(Shutdown)},
    {"cTSNHoodOccupantManager_vf8_5", 0x4aac50, 32, 0x9fc833aa, OCC_ADAPTER(Save)},
    {"cTSNHoodOccupantManager_Read", 0x4aa110, 32, 0xfd876c7d, OCC_ADAPTER(Read)},
    {"cTSNHoodOccupantManager_Write", 0x4ab6f0, 32, 0x24b3bc90, OCC_ADAPTER(Write)},
    {"cTSNHoodOccupantManager_vf8_11", 0x4a92c0, 32, 0x4f75589d, OCC_ADAPTER(GetFloraOccupants)},
    {"cTSNHoodOccupantManager_vf8_13", 0x4a96f0, 32, 0x4f75589d, OCC_ADAPTER(GetRoadOccupants)},
    {"cTSNHoodOccupantManager_vf8_15", 0x4a97a0, 32, 0x4f75589d, OCC_ADAPTER(GetModelRoadOccupants)},
    {"cTSNHoodOccupantManager_vf8_17", 0x4a9630, 32, 0x4f75589d, OCC_ADAPTER(GetPropOccupants)},
    {"cTSNHoodOccupantManager_DoMessage", 0x4a9070, 32, 0xd7dea6ba, OCC_ADAPTER(DoMessage)},
    {"cTSNHoodOccupantManager_vf8_6", 0x4a9d20, 32, 0x3fe58cdc, OCC_ADAPTER(AddOccupant)},
    {"cTSNHoodOccupantManager_FaceQueryInterface", 0x4aa0e7, 8, 0x1250c125, OCC_ADAPTER(ManagerQueryInterface)},
    {"cTSNHoodOccupantManager_SerialFaceQueryInterface", 0x4aa0ff, 8, 0xec4704e2, OCC_ADAPTER(SerialQueryInterface)},
    {"cTSNHoodOccupantManager_PersistFaceQueryInterface", 0x4aa0ef, 8, 0xc84a919d, OCC_ADAPTER(PersistQueryInterface)},
    {"cTSNHoodOccupantManager_MessageFaceQueryInterface", 0x4aa0f7, 8, 0x966ffb64, OCC_ADAPTER(MessageQueryInterface)},
    {"cTSNHoodOccupantManager_vf8_7", 0x4aa830, 32, 0xd2384f79, OCC_ADAPTER(RemoveOccupant)},
    {"cTSNHoodOccupantManager_vf8_8", 0x4aa690, 32, 0x1cbe84c6, OCC_ADAPTER(FindLinkedOccupants)},
    {"cTSNHoodOccupantManager_vf8_10", 0x4a9450, 32, 0x7a45ed9f, OCC_ADAPTER(FindOccupantsInRect)},
    {"cTSNHoodOccupantManager_vf8_19", 0x4aadb0, 32, 0x55bdd1db, OCC_ADAPTER(SnapAllRoads)},
    {"cTSNHoodOccupantManager_vf8_20", 0x4ab400, 32, 0x7b1c594a, OCC_ADAPTER(SnapRoadsInRect)},
    {"cTSNHoodOccupantManager_vf8_21", 0x4a85f0, 32, 0x3fa25ae1, OCC_ADAPTER(ResetGridOccupants)},
    {"cTSNHoodOccupantManager_vf8_22", 0x4a8b70, 32, 0x9d8aec3c, OCC_ADAPTER(BuildRoadGroups)},
    {"cTSNHoodOccupantManager_vf8_23", 0x4a9260, 32, 0x18418a52, OCC_ADAPTER(GetRoadGroup)},
};
REGISTER_HOOKS(kHooks);

} // namespace
