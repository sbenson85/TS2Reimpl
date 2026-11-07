#include "hook/hooks.h"
#include "ts/cTSNeighborGeneticDataTable.h"

namespace {
const HookSpec kHooks[] = {
    {"cTSNeighborGeneticDataTable_Create", 0x4f0f30, 32, 0x62cdc1e0, reinterpret_cast<void *>(&cTSNeighborGeneticDataTable::Create)},
    {"cTSNeighborGeneticDataTable_Construct", 0x4f0ac0, 32, 0x8376b845, MemberAddress(&cTSNeighborGeneticDataTable::Construct)},
    {"cTSNeighborGeneticDataTable_QueryInterface", 0x4f10c0, 32, 0x800610d8, MemberAddress(&cTSNeighborGeneticDataTable::QueryInterface)},
    {"cTSNeighborGeneticDataTable_Release", 0x4f1660, 32, 0xbb964d85, MemberAddress(&cTSNeighborGeneticDataTable::Release)},
    {"cTSNeighborGeneticDataTable_Write", 0x4f1950, 32, 0xa224ceea, MemberAddress(&cTSNeighborGeneticDataTable::Write)},
    {"cTSNeighborGeneticDataTable_Read", 0x4f1130, 32, 0x532c0315, MemberAddress(&cTSNeighborGeneticDataTable::Read)},
    {"cTSNeighborGeneticDataTable_GetGZCLSID", 0x4f0fc0, 6, 0xd67efa07, MemberAddress(&cTSNeighborGeneticDataTable::GetGZCLSID)},
    {"cTSNeighborGeneticDataTable_Clear", 0x4f1920, 32, 0x6e471f5f, MemberAddress(&cTSNeighborGeneticDataTable::Clear)},
    {"cTSNeighborGeneticDataTable_Save", 0x4f1860, 32, 0xfee2d36e, MemberAddress(&cTSNeighborGeneticDataTable::Save)},
    {"cTSNeighborGeneticDataTable_Set", 0x4f0b80, 32, 0x1ff72503, MemberAddress(&cTSNeighborGeneticDataTable::Set)},
    {"cTSNeighborGeneticDataTable_Remove", 0x4f16c0, 32, 0x3ce5ab97, MemberAddress(&cTSNeighborGeneticDataTable::Remove)},
    {"cTSNeighborGeneticDataTable_SetModified", 0x4f10b0, 5, 0xa0e43195, MemberAddress(&cTSNeighborGeneticDataTable::SetModified)},
    {"cTSNeighborGeneticDataTable_Get", 0x4f1000, 32, 0xa19aae8f, MemberAddress(&cTSNeighborGeneticDataTable::Get)},
    {"cTSNeighborGeneticDataTable_FirstId", 0x4f0fe0, 17, 0x977a5978, MemberAddress(&cTSNeighborGeneticDataTable::FirstId)},
    {"cTSNeighborGeneticDataTable_NextId", 0x4f1050, 32, 0x31fa38fd, MemberAddress(&cTSNeighborGeneticDataTable::NextId)},
    {"cTSNeighborGeneticDataTable_CreateData", 0x4f0ea0, 32, 0x8f51dcf7, MemberAddress(&cTSNeighborGeneticDataTable::CreateData)},
    {"cTSNeighborGeneticDataTable_LoadOrCreate", 0x4f0d00, 32, 0x214079d1, MemberAddress(&cTSNeighborGeneticDataTable::LoadOrCreate)},
    {"cTSNeighborGeneticDataTable_ReadFromHolder", 0x4f1760, 32, 0x21417773, MemberAddress(&cTSNeighborGeneticDataTable::ReadFromHolder)},
    {"cTSNeighborGeneticDataTable_WriteToHolder", 0x4f17e0, 32, 0x21417773, MemberAddress(&cTSNeighborGeneticDataTable::WriteToHolder)},
    // The container face's slots run with `this` at the +4 subobject.
    {"cTSNeighborGeneticDataTable_ExportTo", 0x4f1b60, 32, 0xc0021927, MemberAddress(&NeighborContainerFace::ExportTo)},
    {"cTSNeighborGeneticDataTable_ImportFrom", 0x4f13b0, 32, 0x3be62529, MemberAddress(&NeighborContainerFace::ImportFrom)},
};
// Not hooked: the folded one-line slots (IsModified, Count, GetRefCount, GetPersistResource, the persist face's key
// accessors), the secondary-face thunks (QueryInterface/Release with this adjusted), and 0x4f09e0, the std::map insert
// that only Set called (it is now the toolchain's map code).
REGISTER_HOOKS(kHooks);
}
