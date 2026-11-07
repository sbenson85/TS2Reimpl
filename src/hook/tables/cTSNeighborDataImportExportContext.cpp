#include "hook/hooks.h"
#include "ts/cTSNeighborDataImportExportContext.h"

namespace {

using C = cTSNeighborDataImportExportContext;

// 0x46fb80 is the factory (no db name: reached only from the cTSSimCOMDirector constructor).
const HookSpec kHooks[] = {
    {"cTSNeighborDataImportExportContext_Create", 0x46fb80, 32, 0x202f3ee5, reinterpret_cast<void *>(&C::Create)},
    {"cTSNeighborDataImportExportContext_DeletingDestructor", 0x46f950, 32, 0x338a5f18, MemberAddress(&C::DeletingDestructor)},
    {"cTSNeighborDataImportExportContext_QueryInterface", 0x470150, 32, 0x655284ed, MemberAddress(&C::QueryInterface)},
    {"cTSNeighborDataImportExportContext_Release", 0x470500, 26, 0x882b5f1a, MemberAddress(&C::Release)},
    {"cTSNeighborDataImportExportContext_QueryHeld", 0x46fd40, 21, 0xaa4111b3, MemberAddress(&C::QueryHeld)},
    {"cTSNeighborDataImportExportContext_SetHeld", 0x4706d0, 32, 0x9371d6e6, MemberAddress(&C::SetHeld)},
    {"cTSNeighborDataImportExportContext_AddObject", 0x46fa40, 32, 0xda56bbff, MemberAddress(&C::AddObject)},
    {"cTSNeighborDataImportExportContext_RemoveObject", 0x470610, 32, 0xc81284ac, MemberAddress(&C::RemoveObject)},
    {"cTSNeighborDataImportExportContext_ClearObjects", 0x46fb50, 32, 0x6cae050c, MemberAddress(&C::ClearObjects)},
    {"cTSNeighborDataImportExportContext_CopyObjects", 0x46fd00, 32, 0x726d6d28, MemberAddress(&C::CopyObjects)},
    {"cTSNeighborDataImportExportContext_GetClassId", 0x46fcf0, 6, 0x6d70dd3e, MemberAddress(&C::GetClassId)},
    {"cTSNeighborDataImportExportContext_AddListedId", 0x46fb00, 32, 0xa2d81d35, MemberAddress(&C::AddListedId)},
    {"cTSNeighborDataImportExportContext_CopyListedIds", 0x46fd60, 16, 0xaf9623d3, MemberAddress(&C::CopyListedIds)},
    {"cTSNeighborDataImportExportContext_AddResolved", 0x470030, 32, 0x9dc32951, MemberAddress(&C::AddResolved)},
    {"cTSNeighborDataImportExportContext_AddPending", 0x46ffe0, 32, 0x6666f875, MemberAddress(&C::AddPending)},
    {"cTSNeighborDataImportExportContext_RemoveId", 0x4706a0, 32, 0xb6ce14b5, MemberAddress(&C::RemoveId)},
    {"cTSNeighborDataImportExportContext_IsResolved", 0x470110, 32, 0xd894c303, MemberAddress(&C::IsResolved)},
    {"cTSNeighborDataImportExportContext_IsKnown", 0x470090, 32, 0x6e7ad1a0, MemberAddress(&C::IsKnown)},
    {"cTSNeighborDataImportExportContext_CopyResolved", 0x46fde0, 32, 0x2b8900b6, MemberAddress(&C::CopyResolved)},
    {"cTSNeighborDataImportExportContext_CopyAll", 0x46fd80, 32, 0xdd0a44a0, MemberAddress(&C::CopyAll)},
    {"cTSNeighborDataImportExportContext_SetMapA", 0x470580, 32, 0x4ef24b21, MemberAddress(&C::SetMapA)},
    {"cTSNeighborDataImportExportContext_GetMapA", 0x46ff50, 32, 0xdb03f204, MemberAddress(&C::GetMapA)},
    {"cTSNeighborDataImportExportContext_GetMapC", 0x46fe30, 32, 0xdb03f204, MemberAddress(&C::GetMapC)},
    {"cTSNeighborDataImportExportContext_AddMapC", 0x470720, 32, 0x532c7090, MemberAddress(&C::AddMapC)},
    {"cTSNeighborDataImportExportContext_SetMapBForValue", 0x470530, 32, 0x11de5ed5, MemberAddress(&C::SetMapBForValue)},
    {"cTSNeighborDataImportExportContext_GetMapB", 0x46fec0, 32, 0xdb03f204, MemberAddress(&C::GetMapB)},
    {"cTSNeighborDataImportExportContext_SerialFace_Write", 0x470820, 32, 0x30ca56e3, MemberAddress(&NeighborContextSerialFace::Write)},
    {"cTSNeighborDataImportExportContext_SerialFace_Read", 0x4701c0, 32, 0xe8f3c4eb, MemberAddress(&NeighborContextSerialFace::Read)},
    {"cTSNeighborDataImportExportContext_SerialFace_QueryInterface", 0x4701ad, 8, 0xfa04199b, MemberAddress(&NeighborContextSerialFace::QueryInterface)},
    {"cTSNeighborDataImportExportContext_SerialFace_Release", 0x47051a, 8, 0x56ceb194, MemberAddress(&NeighborContextSerialFace::Release)},
    {"cTSNeighborDataImportExportContext_SerialFace_GetGZCLSID", 0x46fd70, 8, 0x3ecef74d, MemberAddress(&NeighborContextSerialFace::GetGZCLSID)},
};
REGISTER_HOOKS(kHooks);

} // namespace
