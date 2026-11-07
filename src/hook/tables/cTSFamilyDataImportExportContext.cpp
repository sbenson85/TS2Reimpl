#include "hook/hooks.h"
#include "ts/cTSFamilyDataImportExportContext.h"

namespace {

// 0x470bf0 is the factory (no db name: reached only from the cTSSimCOMDirector constructor).
const HookSpec kHooks[] = {
    {"cTSFamilyDataImportExportContext_Create", 0x470bf0, 32, 0xe67b5f98, reinterpret_cast<void *>(&cTSFamilyDataImportExportContext::Create)},
    {"cTSFamilyDataImportExportContext_DeletingDestructor", 0x470a60, 32, 0xfe3632da, MemberAddress(&cTSFamilyDataImportExportContext::DeletingDestructor)},
    {"cTSFamilyDataImportExportContext_QueryInterface", 0x470f40, 32, 0x27626ed1, MemberAddress(&cTSFamilyDataImportExportContext::QueryInterface)},
    {"cTSFamilyDataImportExportContext_Release", 0x471080, 23, 0x4eca005a, MemberAddress(&cTSFamilyDataImportExportContext::Release)},
    {"cTSFamilyDataImportExportContext_QuerySource", 0x470d80, 18, 0x5adac064, MemberAddress(&cTSFamilyDataImportExportContext::QuerySource)},
    {"cTSFamilyDataImportExportContext_SetSource", 0x4711c0, 32, 0x26669bef, MemberAddress(&cTSFamilyDataImportExportContext::SetSource)},
    {"cTSFamilyDataImportExportContext_AddObject", 0x470b00, 32, 0xda56bbff, MemberAddress(&cTSFamilyDataImportExportContext::AddObject)},
    {"cTSFamilyDataImportExportContext_RemoveObject", 0x471130, 32, 0xe6bc53f9, MemberAddress(&cTSFamilyDataImportExportContext::RemoveObject)},
    {"cTSFamilyDataImportExportContext_ClearObjects", 0x470bc0, 32, 0x9264fedb, MemberAddress(&cTSFamilyDataImportExportContext::ClearObjects)},
    {"cTSFamilyDataImportExportContext_CopyObjects", 0x470d40, 32, 0x6fe5488c, MemberAddress(&cTSFamilyDataImportExportContext::CopyObjects)},
    {"cTSFamilyDataImportExportContext_GetClassId", 0x470d30, 6, 0x3d52228b, MemberAddress(&cTSFamilyDataImportExportContext::GetClassId)},
    {"cTSFamilyDataImportExportContext_AddId", 0x470eb0, 32, 0xdf7916a1, MemberAddress(&cTSFamilyDataImportExportContext::AddId)},
    {"cTSFamilyDataImportExportContext_CopyIds", 0x470da0, 32, 0x0a6288c2, MemberAddress(&cTSFamilyDataImportExportContext::CopyIds)},
    {"cTSFamilyDataImportExportContext_SetMappedId", 0x4710a0, 32, 0xdb03f204, MemberAddress(&cTSFamilyDataImportExportContext::SetMappedId)},
    {"cTSFamilyDataImportExportContext_GetMappedId", 0x470e20, 32, 0xdb03f204, MemberAddress(&cTSFamilyDataImportExportContext::GetMappedId)},
    {"cTSFamilyDataImportExportContext_SetDestinationFrom", 0x471220, 32, 0x5e640978, MemberAddress(&cTSFamilyDataImportExportContext::SetDestinationFrom)},
    {"cTSFamilyDataImportExportContext_QueryDestination", 0x470e00, 18, 0xfdf813ac, MemberAddress(&cTSFamilyDataImportExportContext::QueryDestination)},
    {"cTSFamilyDataImportExportContext_SerialFace_Write", 0x471260, 32, 0x0596098e, MemberAddress(&FamilyContextSerialFace::Write)},
    {"cTSFamilyDataImportExportContext_SerialFace_Read", 0x470fb0, 32, 0xe0b9ef68, MemberAddress(&FamilyContextSerialFace::Read)},
    {"cTSFamilyDataImportExportContext_SerialFace_QueryInterface", 0x470f9d, 8, 0xfa04199b, MemberAddress(&FamilyContextSerialFace::QueryInterface)},
    {"cTSFamilyDataImportExportContext_SerialFace_Release", 0x471097, 8, 0xfe865963, MemberAddress(&FamilyContextSerialFace::Release)},
    {"cTSFamilyDataImportExportContext_SerialFace_GetGZCLSID", 0x470ddd, 8, 0xdc50a002, MemberAddress(&FamilyContextSerialFace::GetGZCLSID)},
};
REGISTER_HOOKS(kHooks);

} // namespace
