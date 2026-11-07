#include "hook/hooks.h"
#include "ts/cTSNeighborhoodGroupData.h"

namespace {

// Standalone shape (lane FP): Create builds our class (the only creation site, so the class's switch); the other entries are
// adapters serving direct calls and objects that still carry the game's vtables.
#define ADAPTER(name) reinterpret_cast<void *>(&cTSNeighborhoodGroupDataAdapters::name)

const HookSpec kHooks[] = {
    {"cTSNeighborhoodGroupData_Create", 0x473c40, 32, 0x4c48bee3, reinterpret_cast<void *>(&cTSNeighborhoodGroupData::Create)},
    {"cTSNeighborhoodGroupData_DeletingDestructor", 0x4729f0, 32, 0x310a40c0, ADAPTER(DeletingDestructor)},
    {"cTSNeighborhoodGroupData_QueryInterface", 0x4772e0, 32, 0x534412ab, ADAPTER(QueryInterface)},
    {"cTSNeighborhoodGroupData_UnknownFace_QueryInterface", 0x477307, 8, 0x0eada1c2, ADAPTER(UnknownQueryInterface)},
    {"cTSNeighborhoodGroupData_UnknownFace_DeletingDestructor", 0x4728af, 8, 0x1c1a8430, ADAPTER(UnknownDeletingDestructor)},
    {"cTSNeighborhoodGroupData_Add", 0x476080, 32, 0x5ed975f7, ADAPTER(Add)},
    {"cTSNeighborhoodGroupData_Refresh", 0x476340, 32, 0xcc670890, ADAPTER(Refresh)},
    {"cTSNeighborhoodGroupData_SaveAll", 0x47a4b0, 32, 0xe7ddc771, ADAPTER(SaveAll)},
    {"cTSNeighborhoodGroupData_Flush", 0x479590, 32, 0x0c86c810, ADAPTER(Flush)},
    {"cTSNeighborhoodGroupData_Reset", 0x47a620, 8, 0x4386b300, ADAPTER(Reset)},
    {"cTSNeighborhoodGroupData_Rebuild", 0x47aae0, 32, 0x96643093, ADAPTER(Rebuild)},
    {"cTSNeighborhoodGroupData_TrackCurrent", 0x47a8a0, 32, 0xb11c7714, ADAPTER(TrackCurrent)},
    {"cTSNeighborhoodGroupData_Remove",0x47adb0, 32, 0x68ce43f2, ADAPTER(Remove)},
    {"cTSNeighborhoodGroupData_MarkDirtyById", 0x476db0, 32, 0xfb325423, ADAPTER(MarkDirtyById)},
    {"cTSNeighborhoodGroupData_MarkDirtyByKey", 0x476df0, 32, 0xdb03f204, ADAPTER(MarkDirtyByKey)},
    {"cTSNeighborhoodGroupData_GetByKey", 0x4753c0, 32, 0xdb03f204, ADAPTER(GetByKey)},
    {"cTSNeighborhoodGroupData_GetById", 0x475340, 32, 0xbcdd6eca, ADAPTER(GetById)},
    {"cTSNeighborhoodGroupData_MarkAndQuery", 0x474790, 32, 0xdb03f204, ADAPTER(MarkAndQuery)},
    {"cTSNeighborhoodGroupData_QueryByKey", 0x475050, 32, 0xdb03f204, ADAPTER(QueryByKey)},
    {"cTSNeighborhoodGroupData_BeginIteration", 0x473b70, 13, 0x2e53bb11, ADAPTER(BeginIteration)},
    {"cTSNeighborhoodGroupData_Next", 0x476f70, 28, 0xb4a2e830, ADAPTER(Next)},
    {"cTSNeighborhoodGroupData_NextKey", 0x475460, 32, 0xbafa2a72, ADAPTER(NextKey)},
    {"cTSNeighborhoodGroupData_LoadNeighborhoods", 0x476a40, 32, 0x8e874086, ADAPTER(LoadNeighborhoods)},
};
REGISTER_HOOKS(kHooks);

} // namespace
