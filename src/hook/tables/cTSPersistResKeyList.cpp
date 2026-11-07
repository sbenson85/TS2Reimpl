#include "hook/hooks.h"
#include "ts/cTSPersistResKeyList.h"

// Standalone shape (docs/standalone-shape.md): the game creates every list through the factory 0x1f4dc0 (operator new + the
// constructor inline), so that hook gives every list our vtables. The slot entries are adapters for the old entry points
// (cTSPersistResKeyListAdapters): reached only through direct calls (GetGZCLSID, from the cTSPersistCOMDirector getter; the
// game's QueryInterface thunks 0x1f4eb7 / 0x1f4ebf jump to 0x1f4e60) or a list carrying the game's vtables (none while the
// factory hook is on).
// Slots only the game's vtables reach have no entry (lane GP): Write, Read, Commit and the deleting destructor are trapped
// under Hooks.cTSPersistResKeyList_Create=1 ("in_effect": "callers"); QueryInterface keeps its adapter (byte match).
namespace {

using A = cTSPersistResKeyListAdapters;

// The factory 0x1f4dc0 (114 bytes) is a real function start Ghidra missed (only a pointer to it is stored, by the
// cTSPersistCOMDirector getter); db/missed-functions.jsonl has it now, so hookspec writes its row. The third field is the
// number of bytes the CRC covers (32 here), not the function's size: a hand-written 64 made the host skip the hook.
const HookSpec kHooks[] = {
    {"cTSPersistResKeyList_Create", 0x1f4dc0, 32, 0xe2048f50, reinterpret_cast<void *>(&cTSPersistResKeyList::Create)},
    {"cTSPersistResKeyList_GetGZCLSID", 0x1f4e40, 6, 0xe17b60d3, reinterpret_cast<void *>(&A::GetGZCLSID)},
    {"cTSPersistResKeyList_QueryInterface", 0x1f4e60, 32, 0x0ed38b9d, reinterpret_cast<void *>(&A::QueryInterface)},
};
REGISTER_HOOKS(kHooks);

} // namespace
