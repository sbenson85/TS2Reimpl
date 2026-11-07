#include "hook/hooks.h"
#include "ts/cTSRelationshipEntry.h"

// Standalone shape (docs/standalone-shape.md): the game builds entries only inline in the table's SetEntry 0x55ac60 and Read
// 0x55b420, both ours (src/ts/cTSRelationshipTable.cpp), so every entry carries our vtables while the table's hooks are on. The
// slot entries are adapters for the old entry points (cTSRelationshipEntryAdapters): reached only through the game's adjustor
// thunks (0x55b2f2 / 0x55b2fa jump to 0x55b2c0, 0x55bade / 0x55bae6 to 0x55ba70) or an entry carrying the game's vtables (none
// while the table's hooks are on). The thunks are no longer hooked: ours are the compiler's. Slots 4, 5 and 7 (ReadFromRecord,
// WriteToRecord, SetValueCount) have no entry (lane GT): only the game's vtable reaches them, trapped under
// Hooks.FUN_a055ac60=1&Hooks.cTSRelationshipTable_Construct=1. The others are byte matches.
namespace {

using A = cTSRelationshipEntryAdapters;

const HookSpec kHooks[] = {
    {"cTSRelationshipEntry_QueryInterface", 0x55b2c0, 32, 0xb3c81f72, reinterpret_cast<void *>(&A::QueryInterface)},
    {"cTSRelationshipEntry_Release", 0x55ba70, 32, 0x4e8ec24d, reinterpret_cast<void *>(&A::Release)},
    {"cTSRelationshipEntry_vf8", 0x55aed0, 30, 0x72ff3b90, reinterpret_cast<void *>(&A::GetValue)},
    {"cTSRelationshipEntry_vf9", 0x55bee0, 32, 0x2470c31d, reinterpret_cast<void *>(&A::SetValue)},
    {"cTSRelationshipEntry_Read", 0x55b370, 32, 0xde2b125e, reinterpret_cast<void *>(&A::Read)},
    {"cTSRelationshipEntry_GetGZCLSID", 0x55aff0, 6, 0x83cd9917, reinterpret_cast<void *>(&A::GetGZCLSID)},
};
REGISTER_HOOKS(kHooks);

} // namespace
