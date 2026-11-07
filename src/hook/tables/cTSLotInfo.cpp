#include "hook/hooks.h"
#include "ts/cTSLotInfo.h"

// cTSLotInfo in the standalone shape (docs/standalone-shape.md): the constructors and the creator 0x4c9180 build our class (our
// vtables), so the game reaches our slots directly; the slot entries are adapters for direct calls and for lot infos still
// carrying the game's vtables. Bisect switch: turn off cTSLotInfo_Construct, cTSLotInfo_CopyConstruct and cTSLotInfo_Create
// together.
namespace {

using A = cTSLotInfoAdapters;
#define ADAPTER(f) reinterpret_cast<void *>(&A::f)

const HookSpec kHooks[] = {
    {"cTSLotInfo_Construct", 0x4c8a40, 32, 0x9fb3e15b, ADAPTER(Construct)},
    {"cTSLotInfo_CopyConstruct", 0x4c8800, 32, 0xcd76295c, ADAPTER(CopyConstruct)},
    {"cTSLotInfo_Create", 0x4c9180, 32, 0x3b92da21, reinterpret_cast<void *>(&cTSLotInfo::Create)},
    {"cTSLotInfo_Destruct", 0x4c8c30, 32, 0x1386a810, ADAPTER(Destruct)},
    {"cTSLotInfo_DeletingDestructor", 0x4c8ee0, 32, 0x04909d44, ADAPTER(DeletingDestructor)},
    {"cTSLotInfo_QueryInterface", 0x4ca090, 32, 0xb76e5948, ADAPTER(QueryInterface)},
    {"cTSLotInfo_Clone", 0x4c90b0, 32, 0x6daa97e6, ADAPTER(Clone)},
    {"cTSLotInfo_SetName", 0x4cb410, 32, 0x9d709755, ADAPTER(SetName)},
    {"cTSLotInfo_SetNameText", 0x4cb440, 32, 0x48f33a87, ADAPTER(SetNameText)},
    {"cTSLotInfo_SetOrigin", 0x4cb390, 27, 0x35de1dbe, ADAPTER(SetOrigin)},
    {"cTSLotInfo_GetSize", 0x4c93b0, 32, 0x19166ba4, ADAPTER(GetSize)},
    {"cTSLotInfo_GetBounds", 0x4c9230, 32, 0x3f761f4e, ADAPTER(GetBounds)},
    {"cTSLotInfo_GetInnerBounds", 0x4c9280, 32, 0x7fadf233, ADAPTER(GetInnerBounds)},
    {"cTSLotInfo_GetEdgeStrip", 0x4c9300, 32, 0x7fadf233, ADAPTER(GetEdgeStrip)},
    {"cTSLotInfo_SetElevation", 0x4cb5b0, 27, 0xadd3592f, ADAPTER(SetElevation)},
    {"cTSLotInfo_GetElevation", 0x4c9f90, 32, 0xfcb9da44, ADAPTER(GetElevation)},
    {"cTSLotInfo_LoadNamesFromResource", 0x4c9db0, 32, 0x1fc23c60, ADAPTER(LoadNamesFromResource)},
    {"cTSLotInfo_SetField44", 0x4cb480, 23, 0xa254d1bd, ADAPTER(SetField44)},
    {"cTSLotInfo_GetBaseName", 0x4c9f50, 8, 0x87144df4, ADAPTER(GetBaseName)},
    {"cTSLotInfo_GetEffectiveBaseName", 0x4c9f60, 32, 0xdb26505e, ADAPTER(GetEffectiveBaseName)},
    {"cTSLotInfo_SetRotation", 0x4cb230, 23, 0x1f691d13, ADAPTER(SetRotation)},
    {"cTSLotInfo_GetRotation", 0x4c9110, 5, 0xff274a72, ADAPTER(GetRotation)},
    {"cTSLotInfo_GetRelativeRotation", 0x4cafc0, 23, 0xf4be7ffd, ADAPTER(GetRelativeRotation)},
    {"cTSLotInfo_HasLotResources", 0x4c9cc0, 32, 0x9067f28e, ADAPTER(HasLotResources)},
    {"cTSLotInfo_SetDescription", 0x4cb4f0, 32, 0x4cf6031a, ADAPTER(SetDescription)},
    {"cTSLotInfo_QueryLotResource", 0x4c9550, 32, 0x98413528, ADAPTER(QueryLotResource)},
    {"cTSLotInfo_GetLotType", 0x4c9c50, 8, 0xc3106087, ADAPTER(GetLotType)},
    {"cTSLotInfo_SetField4C", 0x4cb5d0, 31, 0x06f65133, ADAPTER(SetField4C)},
    {"cTSLotInfo_SetLotId", 0x4cb1d0, 32, 0xb677b188, ADAPTER(SetLotId)},
    {"cTSLotInfo_AddConnection", 0x4c8f10, 32, 0xb1b3f470, ADAPTER(AddConnection)},
    {"cTSLotInfo_RemoveConnectionBySecond", 0x4ca6b0, 32, 0x3f7ec349, ADAPTER(RemoveConnectionBySecond)},
    {"cTSLotInfo_RemoveConnectionByFirst", 0x4ca750, 32, 0x3f7ec349, ADAPTER(RemoveConnectionByFirst)},
    {"cTSLotInfo_GetConnectionCount", 0x4c96b0, 32, 0xc079050f, ADAPTER(GetConnectionCount)},
    {"cTSLotInfo_GetConnectionField0", 0x4c9910, 32, 0xc079050f, ADAPTER(GetConnectionField0)},
    {"cTSLotInfo_GetConnectionField1", 0x4c98b0, 32, 0xc079050f, ADAPTER(GetConnectionField1)},
    {"cTSLotInfo_GetConnectionField2", 0x4c9850, 32, 0xc079050f, ADAPTER(GetConnectionField2)},
    {"cTSLotInfo_GetConnectionField3", 0x4c9780, 32, 0xc079050f, ADAPTER(GetConnectionField3)},
    {"cTSLotInfo_GetCappedBaseByte", 0x4c9610, 32, 0xe301e254, ADAPTER(GetCappedBaseByte)},
    {"cTSLotInfo_SetBaseByte88", 0x4cb540, 15, 0x02c32150, ADAPTER(SetBaseByte88)},
    {"cTSLotInfo_GetBaseByte88", 0x4c9650, 15, 0x8da1b607, ADAPTER(GetBaseByte88)},
    {"cTSLotInfo_SetBaseWord90", 0x4cb570, 15, 0xc77709bf, ADAPTER(SetBaseWord90)},
    {"cTSLotInfo_GetBaseWord90", 0x4c9660, 15, 0x48159ee8, ADAPTER(GetBaseWord90)},
    {"cTSLotInfo_SetBaseWord8C", 0x4cb5a0, 15, 0x52da76cf, ADAPTER(SetBaseWord8C)},
    {"cTSLotInfo_SetConnectionField2", 0x4cb130, 32, 0xb3f8cb50, ADAPTER(SetConnectionField2)},
    {"cTSLotInfo_SetConnectionField3", 0x4cb700, 32, 0xb3f8cb50, ADAPTER(SetConnectionField3)},
    {"cTSLotInfo_GetMappedValue", 0x4c9710, 32, 0x44901971, ADAPTER(GetMappedValue)},
    {"cTSLotInfo_SetMappedValue", 0x4cb630, 32, 0x43d3bdab, ADAPTER(SetMappedValue)},
    {"cTSLotInfo_GetDeferredLotInfo", 0x4c9200, 32, 0x207f7242, ADAPTER(GetDeferredLotInfo)},
    {"cTSLotInfo_Write", 0x4cb8f0, 32, 0x6627bf43, ADAPTER(Write)},
    {"cTSLotInfo_Read", 0x4ca2a0, 32, 0x70a845af, ADAPTER(Read)},
    {"cTSLotInfo_GetGZCLSID", 0x4c91f0, 6, 0x466b87c4, ADAPTER(GetGZCLSID)},
    {"cTSLotInfo_Describe", 0x4c9690, 30, 0x6bad0307, ADAPTER(Describe)},
    {"cTSLotInfo_vf8_35", 0x4c9970, 32, 0xe762f973, ADAPTER(GetOrCreateExtraInfo)},
    {"cTSLotInfo_vf8_29", 0x4cafe0, 32, 0x26db2bae, ADAPTER(StoreNamesInResource)},
    {"cTSLotInfo_vf8_41", 0x4ca7f0, 32, 0x9a7cb013, ADAPTER(CreateNewLotInfo)},
};
REGISTER_HOOKS(kHooks);

#undef ADAPTER

} // namespace
