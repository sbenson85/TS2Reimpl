#include "hook/hooks.h"
#include "ts/cTSNeighborhoodInfo.h"

// Standalone shape (docs/standalone-shape.md): the game creates records only through Create 0x4a6720 (constructor inline) and
// the constructor 0x4a6350 (called after `operator new` at 0x767bc7), so those two hooks give every record our vtables: they are
// the class's switch (turn off both to take it out). The other entries are adapters for the old entry points
// (cTSNeighborhoodInfoAdapters): reached only through direct calls (GetGZCLSID from the cTSSimCOMDirector constructor, the
// game's thunks into 0x4a6d00 / 0x4a66c0) or a record carrying the game's vtables (none while the switch is on).
namespace {

using A = cTSNeighborhoodInfoAdapters;

const HookSpec kHooks[] = {
    {"cTSNeighborhoodInfo_Construct", 0x4a6350, 32, 0x25033a63, reinterpret_cast<void *>(&A::Construct)},
    {"cTSNeighborhoodInfo_Create", 0x4a6720, 32, 0x5cec6e80, reinterpret_cast<void *>(&cTSNeighborhoodInfo::Create)},
    {"cTSNeighborhoodInfo_Destruct", 0x4a6500, 32, 0xaebab240, reinterpret_cast<void *>(&A::Destruct)},
    {"cTSNeighborhoodInfo_DeletingDestructor", 0x4a66c0, 32, 0x2f978950, reinterpret_cast<void *>(&A::DeletingDestructor)},
    {"cTSNeighborhoodInfo_QueryInterface", 0x4a6d00, 32, 0x3261d338, reinterpret_cast<void *>(&A::QueryInterface)},
    {"cTSNeighborhoodInfo_SerialFace_QueryInterface", 0x4a6d7c, 8, 0xb1ac601a, reinterpret_cast<void *>(&A::SerialQueryInterface)},
    {"cTSNeighborhoodInfo_PersistFace_QueryInterface", 0x4a6d84, 8, 0x03da888e, reinterpret_cast<void *>(&A::PersistQueryInterface)},
    {"cTSNeighborhoodInfo_DescribeFace_QueryInterface", 0x4a6d94, 8, 0xc8529d07, reinterpret_cast<void *>(&A::DescribeQueryInterface)},
    {"cTSNeighborhoodInfo_UnknownFace_QueryInterface", 0x4a6d8c, 8, 0x79f27708, reinterpret_cast<void *>(&A::UnknownQueryInterface)},
    {"cTSNeighborhoodInfo_UnknownFace_DeletingDestructor", 0x4a66ac, 8, 0x426aa619, reinterpret_cast<void *>(&A::UnknownDeletingDestructor)},
    {"cTSNeighborhoodInfo_SetNameText", 0x4a71c0, 8, 0x0ea81900, reinterpret_cast<void *>(&A::SetName)},
    {"cTSNeighborhoodInfo_SetTextD", 0x4a71e0, 8, 0xa66eb964, reinterpret_cast<void *>(&A::SetTextD)},
    {"cTSNeighborhoodInfo_SetNumber", 0x4a7180, 13, 0x3b1b80b7, reinterpret_cast<void *>(&A::SetNumber)},
    {"cTSNeighborhoodInfo_IsNumber", 0x4a6ae0, 16, 0x04bd52a5, reinterpret_cast<void *>(&A::IsNumber)},
    {"cTSNeighborhoodInfo_SetFlags", 0x4a7140, 32, 0xa2e8bf9c, reinterpret_cast<void *>(&A::SetFlags)},
    {"cTSNeighborhoodInfo_HasFlags", 0x4a6700, 20, 0xc7f5bbdb, reinterpret_cast<void *>(&A::HasFlags)},
    {"cTSNeighborhoodInfo_MatchesName", 0x4a6af0, 32, 0x75bfa45d, reinterpret_cast<void *>(&A::MatchesName)},
    {"cTSNeighborhoodInfo_GetSlotValue", 0x4a6950, 14, 0xbff2dfb3, reinterpret_cast<void *>(&A::GetSlotValue)},
    {"cTSNeighborhoodInfo_SetSlotValue", 0x4a71f0, 28, 0x46b11cdd, reinterpret_cast<void *>(&A::SetSlotValue)},
    {"cTSNeighborhoodInfo_IsKind567", 0x4a6b70, 26, 0xea58b13f, reinterpret_cast<void *>(&A::IsKind567)},
    {"cTSNeighborhoodInfo_IsCategory13WithFlag4", 0x4a6b30, 27, 0x372509ed, reinterpret_cast<void *>(&A::IsCategory13WithFlag4)},
    {"cTSNeighborhoodInfo_IsCategory16WithFlag4", 0x4a6b50, 27, 0xd4109552, reinterpret_cast<void *>(&A::IsCategory16WithFlag4)},
    {"cTSNeighborhoodInfo_LoadTexts", 0x4a6b90, 32, 0xc9107375, reinterpret_cast<void *>(&A::LoadTexts)},
    {"cTSNeighborhoodInfo_Unload", 0x4a7220, 32, 0x6f90de01, reinterpret_cast<void *>(&A::Unload)},
    {"cTSNeighborhoodInfo_StoreTexts", 0x4a6fb0, 32, 0x0c44457f, reinterpret_cast<void *>(&A::StoreTexts)},
    {"cTSNeighborhoodInfo_GetRecord", 0x4a6960, 32, 0xd2ec1d45, reinterpret_cast<void *>(&A::GetRecord)},
    {"cTSNeighborhoodInfo_GetGZCLSID", 0x4a6900, 6, 0xd1551f9f, reinterpret_cast<void *>(&A::GetGZCLSID)},
    {"cTSNeighborhoodInfo_Write", 0x4a7310, 32, 0xe8d31158, reinterpret_cast<void *>(&A::Write)},
    {"cTSNeighborhoodInfo_Read", 0x4a6da0, 32, 0xf18be4a7, reinterpret_cast<void *>(&A::Read)},
    {"cTSNeighborhoodInfo_GetKey", 0x4a6910, 17, 0x5aaa53aa, reinterpret_cast<void *>(&A::GetKey)},
    {"cTSNeighborhoodInfo_SetKey", 0x4a71a0, 19, 0x02299fca, reinterpret_cast<void *>(&A::SetKey)},
    {"cTSNeighborhoodInfo_SetInstance", 0x4a7190, 15, 0x7c4776b7, reinterpret_cast<void *>(&A::SetInstance)},
    {"cTSNeighborhoodInfo_Describe", 0x4a6930, 30, 0x665359b7, reinterpret_cast<void *>(&A::Describe)},
};
REGISTER_HOOKS(kHooks);

} // namespace
