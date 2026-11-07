#include "hook/hooks.h"
#include "ts/cTSNeighborhoodDataImportExportContext.h"

namespace {

using C = cTSNeighborhoodDataImportExportContext;
using H = NeighborhoodContextHoodFace;
using F = NeighborhoodContextFamilyFace;
using N = NeighborhoodContextNeighborFace;
using S = NeighborhoodContextSerialFace;

// 0x472040 (the constructor) and 0x473be0 (the factory, reached from the cTSSimCOMDirector constructor) have no db names. The
// 8-byte slots of the +0xc / +0x10 / +0x14 faces are `sub ecx, N; jmp` thunks to the bodies below and are not hooked, nor are
// the shared slots (3 / 4 / 8 / 9 / 10 of the cIGZPersistResource part that other classes share).
const HookSpec kHooks[] = {
    {"cTSNeighborhoodDataImportExportContext_Construct", 0x472040, 32, 0xbfb48423, MemberAddress(&C::Construct)},
    {"cTSNeighborhoodDataImportExportContext_Create", 0x473be0, 32, 0x6d41c5b8, reinterpret_cast<void *>(&C::Create)},
    {"cTSNeighborhoodDataImportExportContext_DeletingDestructor", 0x4728f0, 32, 0xd47f14af, MemberAddress(&C::DeletingDestructor)},
    {"cTSNeighborhoodDataImportExportContext_QueryInterface", 0x477240, 32, 0x7ef91ce9, MemberAddress(&C::QueryInterface)},
    {"cTSNeighborhoodDataImportExportContext_GetKey", 0x4749f0, 17, 0xc410540a, MemberAddress(&H::GetKey)},
    {"cTSNeighborhoodDataImportExportContext_SetKey", 0x479a40, 19, 0x4622eab3, MemberAddress(&H::SetKey)},
    {"cTSNeighborhoodDataImportExportContext_SetType", 0x479cb0, 15, 0xb5cd95d9, MemberAddress(&H::SetType)},
    {"cTSNeighborhoodDataImportExportContext_SetInstance", 0x479a00, 15, 0xa2d575b0, MemberAddress(&H::SetInstance)},
    {"cTSNeighborhoodDataImportExportContext_QueryHeld", 0x4741c0, 21, 0xf9860916, MemberAddress(&H::QueryHeld)},
    {"cTSNeighborhoodDataImportExportContext_SetHeld", 0x479940, 32, 0x536d55db, MemberAddress(&H::SetHeld)},
    {"cTSNeighborhoodDataImportExportContext_SetField10C", 0x479990, 13, 0xd82f4f5a, MemberAddress(&H::SetField10C)},
    {"cTSNeighborhoodDataImportExportContext_AddObject", 0x472d30, 32, 0xda56bbff, MemberAddress(&H::AddObject)},
    {"cTSNeighborhoodDataImportExportContext_RemoveObject", 0x4789c0, 32, 0xa2ef8d5b, MemberAddress(&H::RemoveObject)},
    {"cTSNeighborhoodDataImportExportContext_ClearObjects", 0x4737a0, 32, 0xdb34e557, MemberAddress(&H::ClearObjects)},
    {"cTSNeighborhoodDataImportExportContext_CopyObjects", 0x474160, 32, 0x62f0bb57, MemberAddress(&H::CopyObjects)},
    {"cTSNeighborhoodDataImportExportContext_GetClassId", 0x473f20, 6, 0x97019b2e, MemberAddress(&H::GetClassId)},
    {"cTSNeighborhoodDataImportExportContext_Hood_AddId", 0x475ed0, 32, 0x9ad51b9a, MemberAddress(&H::AddId)},
    {"cTSNeighborhoodDataImportExportContext_Hood_CopyIds", 0x474db0, 32, 0x20f61e1c, MemberAddress(&H::CopyIds)},
    {"cTSNeighborhoodDataImportExportContext_Hood_SetMappedId", 0x478920, 32, 0xdb03f204, MemberAddress(&H::SetMappedId)},
    {"cTSNeighborhoodDataImportExportContext_Hood_GetMappedId", 0x4759c0, 32, 0xdb03f204, MemberAddress(&H::GetMappedId)},
    {"cTSNeighborhoodDataImportExportContext_SetPairKey", 0x4789b0, 10, 0x3ab17359, MemberAddress(&H::SetPairKey)},
    {"cTSNeighborhoodDataImportExportContext_GetPairValue", 0x475a50, 28, 0x9095bf4f, MemberAddress(&H::GetPairValue)},
    {"cTSNeighborhoodDataImportExportContext_AddAux", 0x478720, 32, 0x18ec6092, MemberAddress(&H::AddAux)},
    {"cTSNeighborhoodDataImportExportContext_GetAux", 0x475780, 32, 0xdb03f204, MemberAddress(&H::GetAux)},
    {"cTSNeighborhoodDataImportExportContext_Family_AddId", 0x475e40, 32, 0x4646845e, MemberAddress(&F::AddId)},
    {"cTSNeighborhoodDataImportExportContext_Family_CopyIds", 0x474210, 32, 0x9b91c25d, MemberAddress(&F::CopyIds)},
    {"cTSNeighborhoodDataImportExportContext_Family_SetMappedId", 0x4787b0, 32, 0xdb03f204, MemberAddress(&F::SetMappedId)},
    {"cTSNeighborhoodDataImportExportContext_Family_GetMappedId", 0x475810, 32, 0xdb03f204, MemberAddress(&F::GetMappedId)},
    {"cTSNeighborhoodDataImportExportContext_Family_QueryOwner", 0x475210, 10, 0x982f72a7, MemberAddress(&F::QueryOwner)},
    {"cTSNeighborhoodDataImportExportContext_Neighbor_AddListedId", 0x472e00, 9, 0x7ad4906c, MemberAddress(&N::AddListedId)},
    {"cTSNeighborhoodDataImportExportContext_Neighbor_AddResolved", 0x475fb0, 32, 0xa2026036, MemberAddress(&N::AddResolved)},
    {"cTSNeighborhoodDataImportExportContext_Neighbor_AddPending", 0x475f60, 32, 0xa6fcdc20, MemberAddress(&N::AddPending)},
    {"cTSNeighborhoodDataImportExportContext_Neighbor_RemoveId", 0x478d50, 32, 0x5902c78a, MemberAddress(&N::RemoveId)},
    {"cTSNeighborhoodDataImportExportContext_Neighbor_IsResolved", 0x4762f0, 32, 0x4a11818c, MemberAddress(&N::IsResolved)},
    {"cTSNeighborhoodDataImportExportContext_Neighbor_IsKnown", 0x476270, 32, 0x59546b02, MemberAddress(&N::IsKnown)},
    {"cTSNeighborhoodDataImportExportContext_Neighbor_CopyResolved", 0x474370, 32, 0x46f647f6, MemberAddress(&N::CopyResolved)},
    {"cTSNeighborhoodDataImportExportContext_Neighbor_CopyAll", 0x474310, 32, 0xf374b25a, MemberAddress(&N::CopyAll)},
    {"cTSNeighborhoodDataImportExportContext_Neighbor_SetMapA", 0x478890, 32, 0xc7354580, MemberAddress(&N::SetMapA)},
    {"cTSNeighborhoodDataImportExportContext_Neighbor_GetMapA", 0x475930, 32, 0xdb03f204, MemberAddress(&N::GetMapA)},
    {"cTSNeighborhoodDataImportExportContext_Neighbor_GetMapC", 0x475220, 32, 0xdb03f204, MemberAddress(&N::GetMapC)},
    {"cTSNeighborhoodDataImportExportContext_Neighbor_AddMapC", 0x479a80, 32, 0xa5a68666, MemberAddress(&N::AddMapC)},
    {"cTSNeighborhoodDataImportExportContext_Neighbor_SetMapBForValue", 0x478840, 32, 0x091fa89c, MemberAddress(&N::SetMapBForValue)},
    {"cTSNeighborhoodDataImportExportContext_Neighbor_GetMapB", 0x4758a0, 32, 0xdb03f204, MemberAddress(&N::GetMapB)},
    {"cTSNeighborhoodDataImportExportContext_SerialFace_Write", 0x47b150, 32, 0x332edcb0, MemberAddress(&S::Write)},
    {"cTSNeighborhoodDataImportExportContext_SerialFace_Read", 0x477980, 32, 0xe8f3c4eb, MemberAddress(&S::Read)},
};
REGISTER_HOOKS(kHooks);

} // namespace
