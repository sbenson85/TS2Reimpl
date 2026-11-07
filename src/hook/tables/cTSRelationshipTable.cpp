#include "hook/hooks.h"
#include "ts/cTSRelationshipTable.h"

// Standalone shape (docs/standalone-shape.md): every table is built through the constructor 0x55ab10 (cTSNeighbor::Create, ours,
// with `new`; the game's 0x52c4b0 through this hook), so the Construct entry gives every table our vtables. The other entries
// are adapters for the old entry points (cTSRelationshipTableAdapters): reached only through direct calls, the game's +4 thunks
// (0x55b359 / 0x55bb07 jump to 0x55b310 / 0x55baf0; no longer hooked: ours are the compiler's) or a table carrying the game's
// vtables (none while the constructor hook is on). Shared folded functions are not hooked.
// Slots only the game's vtables (and the +4 thunks they hold) reach have no entry (lane GP): their originals are trapped under
// Hooks.cTSRelationshipTable_Construct=1 ("in_effect": "callers"). The slot adapters left are byte matches.
namespace {

using A = cTSRelationshipTableAdapters;

const HookSpec kHooks[] = {
    {"cTSRelationshipTable_Construct", 0x55ab10, 32, 0x75ad3341, reinterpret_cast<void *>(&A::Construct)},
    {"cTSRelationshipTable_Release", 0x55baf0, 23, 0x106a7088, reinterpret_cast<void *>(&A::Release)},
    {"cTSRelationshipTable_GetGZCLSID", 0x55b000, 6, 0x582c72f1, reinterpret_cast<void *>(&A::GetGZCLSID)},
    {"cTSRelationshipTable_WriteForPerson", 0x55bec0, 24, 0x95f9b736, reinterpret_cast<void *>(&A::WriteForPerson)},
    {"cTSRelationshipTable_ReadForPerson", 0x55b2a0, 24, 0xe1ed75d6, reinterpret_cast<void *>(&A::ReadForPerson)},
    {"FUN_a055b090", 0x55b090, 32, 0x32dd0b83, reinterpret_cast<void *>(&A::LoadEntry)},
    {"FUN_a055ac60", 0x55ac60, 32, 0x313dd6a9, reinterpret_cast<void *>(&A::SetEntry)},
};
REGISTER_HOOKS(kHooks);

} // namespace
