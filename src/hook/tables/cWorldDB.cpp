#include "hook/hooks.h"
#include "world/cWorldDB.h"

namespace {

using nTSWorld::cWorldDBAdapters;

// The constructor is the class's switch: its only caller is cTSAppFW::AddApplicationServicesHere (ours, which builds ours
// directly), so with it on every database carries our vtables. The slots, the destructor, the deleting destructor and the
// other helpers have no entry: only the game's vtables and the replaced members reach their originals, trapped under
// Hooks.cWorldDB_Construct=1 ("in_effect": "callers"). The five helpers below stay hooked: Load (0xd0fb30) and Create
// (0xd0ef80), still game code, call them on our objects.
const HookSpec kHooks[] = {
    {"cWorldDB_Construct", 0xd0b6c0, 32, 0xc081883f, reinterpret_cast<void *>(&cWorldDBAdapters::Construct)},
    {"cWorldDB_PatternId", 0xd0e1d0, 32, 0x8b6aa5f7, reinterpret_cast<void *>(&cWorldDBAdapters::PatternId)},
    {"cWorldDB_SetPatternByNameB", 0xd127b0, 32, 0x08d74d9d, reinterpret_cast<void *>(&cWorldDBAdapters::SetPatternByNameB)},
    {"cWorldDB_SetLevels", 0xd13230, 32, 0x66cc8b6b, reinterpret_cast<void *>(&cWorldDBAdapters::SetLevels)},
    {"cWorldDB_SetByteTile", 0xd13290, 32, 0xaf30677c, reinterpret_cast<void *>(&cWorldDBAdapters::SetByteTile)},
    {"cWorldDB_UsePatternName", 0xd138d0, 32, 0xbffd2a60, reinterpret_cast<void *>(&cWorldDBAdapters::UsePatternName)},
};
REGISTER_HOOKS(kHooks);

} // namespace
