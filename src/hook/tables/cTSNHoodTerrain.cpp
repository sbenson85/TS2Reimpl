#include "hook/hooks.h"
#include "ts/cTSNHoodTerrain.h"

// cTSNHoodTerrain in the standalone shape (docs/standalone-shape.md, lane FO): the Create hook makes every terrain the game
// creates ours, with our vtables; the slot entries are adapters for old entry points and terrains built with the game's
// vtables. Bisect switch: turn off cTSNHoodTerrain_Create (the only creation site); the slot adapters then serve the
// game-built terrains as the hook shape did.
namespace {

using A = cTSNHoodTerrainAdapters;
#define ADAPTER(f) reinterpret_cast<void *>(&A::f)

const HookSpec kHooks[] = {
    {"cTSNHoodTerrain_GetGZCLSID", 0x4af2d0, 6, 0x29ff511b, ADAPTER(GetGZCLSID)},
    {"cTSNHoodTerrain_GetKey", 0x4af5d0, 17, 0x4096b324, ADAPTER(GetKey)},
    {"cTSNHoodTerrain_SetKey", 0x4b21d0, 19, 0xc589f5be, ADAPTER(SetKey)},
    {"cTSNHoodTerrain_SetType", 0x4b2630, 15, 0x8d6b65ca, ADAPTER(SetType)},
    {"cTSNHoodTerrain_vf8_10", 0x4afc10, 27, 0x797d36df, ADAPTER(GetHeightAt)},
    {"cTSNHoodTerrain_vf8_17", 0x4b2d50, 32, 0x4811b956, ADAPTER(GetWaterDepthAt)},
    {"cTSNHoodTerrain_vf8_24", 0x4b00e0, 32, 0xf725480a, ADAPTER(GetVertexFlag)},
    {"cTSNHoodTerrain_vf8_37", 0x4b0110, 32, 0x353d7223, ADAPTER(IsVertexUnderWater)},
    {"cTSNHoodTerrain_vf8_38", 0x4b0030, 32, 0xe919b5b4, ADAPTER(IsCellUnderWater)},
    {"cTSNHoodTerrain_vf8_39", 0x4affc0, 32, 0xe919b5b4, ADAPTER(IsCellAboveWater)},
    {"cTSNHoodTerrain_vf8_43", 0x4b00a0, 32, 0xf6e53937, ADAPTER(IsInWorld)},
    {"cTSNHoodTerrain_vf8_13", 0x4ae780, 32, 0xd463fce7, ADAPTER(WorldToVertex)},
    {"cTSNHoodTerrain_vf8_14", 0x4af700, 32, 0xe4c1f79a, ADAPTER(GetVertexNormal)},
    {"cTSNHoodTerrain_vf8_22", 0x4afbf0, 30, 0x100ed94c, ADAPTER(GetTerrainTypeName)},
    {"cTSNHoodTerrain_vf8_34", 0x4b0290, 6, 0x76b434f4, ADAPTER(GetMaxTypeId)},
    {"cTSNHoodTerrain_SetSize", 0x4b2200, 32, 0xef7cdf76, MemberAddress<&cTSNHoodTerrain::SetSize>()},
    {"cTSNHoodTerrain_ResetHeights", 0x4afd90, 32, 0x9f3edff1, MemberAddress<&cTSNHoodTerrain::ResetHeights>()},
    {"cTSNHoodTerrain_ResetVertexFlags", 0x4aff00, 32, 0x8c8440bf, MemberAddress<&cTSNHoodTerrain::ResetVertexFlags>()},
    {"cTSNHoodTerrain_RegisterCheatAndMessages", 0x4afcd0, 32, 0xaf0d0e98, MemberAddress<&cTSNHoodTerrain::RegisterCheatAndMessages>()},
    {"cTSNHoodTerrain_Read", 0x4b0980, 32, 0x6cc7b9c9, ADAPTER(Read)},
    {"cTSNHoodTerrain_Write", 0x4b2fb0, 32, 0xc7b1875e, ADAPTER(Write)},
    {"cTSNHoodTerrain_vf8_20", 0x4b1e40, 32, 0x9fc833aa, ADAPTER(Save)},
    {"cTSNHoodTerrain_Create", 0x4af120, 32, 0x14d32a8d, reinterpret_cast<void *>(&cTSNHoodTerrain::Create)},
    {"cTSNHoodTerrain_vf3", 0x4ad290, 32, 0xd72be6a5, ADAPTER(DeletingDestructor)},
    {"cTSNHoodTerrain_QueryInterface", 0x4b05b0, 32, 0x209ae385, ADAPTER(QueryInterface)},
    {"cTSNHoodTerrain_vf8_6", 0x4afc30, 32, 0x188e1d6f, ADAPTER(Initialize)},
    {"cTSNHoodTerrain_vf8_7", 0x4b2640, 32, 0x982296c1, ADAPTER(Shutdown)},
    {"cTSNHoodTerrain_vf8_8", 0x4b2540, 32, 0x1a1072a4, ADAPTER(SetHeightAt)},
    {"cTSNHoodTerrain_PostTerrainMessage", 0x4b04f0, 32, 0x3070307b, reinterpret_cast<void *>(&cTSNHoodTerrain::PostTerrainMessage)},
    {"cTSNHoodTerrain_SendTerrainMessage", 0x4b1ef0, 32, 0x7239d651, reinterpret_cast<void *>(&cTSNHoodTerrain::SendTerrainMessage)},
    {"cTSNHoodTerrain_vf8_12", 0x4af650, 32, 0x2743e985, ADAPTER(GetHeightRange)},
    {"cTSNHoodTerrain_vf8_18", 0x4b29c0, 32, 0xcb07567e, ADAPTER(UpdateNormals)},
    {"cTSNHoodTerrain_vf8_21", 0x4b24b0, 32, 0xd82f6808, ADAPTER(SetTerrainType)},
    {"cTSNHoodTerrain_vf8_23", 0x4b25a0, 32, 0xc02383a5, ADAPTER(SetVertexFlag)},
    {"cTSNHoodTerrain_vf8_25", 0x4af870, 32, 0xe6125fc6, ADAPTER(GetVertexTerrainTypes)},
    {"cTSNHoodTerrain_vf8_26", 0x4af8f0, 32, 0x50086eed, ADAPTER(GetRectTerrainTypes)},
    {"cTSNHoodTerrain_vf8_27", 0x4af7b0, 32, 0x7a07c71b, ADAPTER(GetVertexTypeWeight)},
    {"cTSNHoodTerrain_vf8_29", 0x4af760, 32, 0x0bd6b162, ADAPTER(GetTypeWeightMap)},
    {"cTSNHoodTerrain_vf8_30", 0x4af580, 32, 0x74fb044b, ADAPTER(GetTerrainTypeIds)},
    {"cTSNHoodTerrain_vf8_31", 0x4af810, 32, 0x768c69fd, ADAPTER(GetTypeName)},
    {"cTSNHoodTerrain_vf8_32", 0x4af5f0, 32, 0x768c69fd, ADAPTER(GetTypeAltName)},
    {"cTSNHoodTerrain_vf8_33", 0x4b04a0, 32, 0x0bd6b162, ADAPTER(GetTypeFlag)},
    {"cTSNHoodTerrain_vf8_9", 0x4b1fb0, 32, 0x47a0d815, ADAPTER(ResampleGrid)},
    {"cTSNHoodTerrain_vf8_11", 0x4af9e0, 32, 0x95480046, ADAPTER(GetInterpolatedHeight)},
    {"cTSNHoodTerrain_vf8_19", 0x4b2a00, 32, 0xd8ae909f, ADAPTER(RebuildDerivedMaps)},
    {"cTSNHoodTerrain_vf8_36", 0x4b02a0, 32, 0xf8c9f675, ADAPTER(ImportHeights)},
    {"cTSNHoodTerrain_vf8_41", 0x4b0620, 32, 0x66ada8fc, ADAPTER(RaiseRect)},
    {"cTSNHoodTerrain_vf8_42", 0x4b0150, 32, 0x12b96a1c, ADAPTER(SetRectHeight)},
    {"cTSNHoodTerrain_vf6", 0x4b2c00, 32, 0x86f27eea, ADAPTER(InvalidateRect)},
    {"cTSNHoodTerrain_DoMessage", 0x4aecc0, 32, 0xe27b6f1a, ADAPTER(DoMessage)},
    {"cTSNHoodTerrain_vf8_28", 0x4af2e0, 32, 0x62c297d1, ADAPTER(GetRectTypeWeights)},
    {"cTSNHoodTerrain_vf8_35", 0x4b2700, 32, 0x2c5d9c4d, ADAPTER(SlopeRect)},
    {"cTSNHoodTerrain_FaceQueryInterface", 0x4b0602, 8, 0x8f87f99c, ADAPTER(TerrainQueryInterface)},
    {"cTSNHoodTerrain_PersistFaceQueryInterface", 0x4b0612, 8, 0xe43d432b, ADAPTER(PersistQueryInterface)},
    {"cTSNHoodTerrain_MessageFaceQueryInterface", 0x4b060a, 8, 0x559da924, ADAPTER(MessageQueryInterface)},
};
REGISTER_HOOKS(kHooks);

} // namespace
