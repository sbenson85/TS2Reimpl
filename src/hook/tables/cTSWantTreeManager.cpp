#include "hook/hooks.h"
#include "ts/cTSWantTreeManager.h"

namespace {

// The constructor is the class's switch (every manager then carries our vtables); the slots have no entry: only the game's
// vtables reach their originals, trapped under Hooks.cTSWantTreeManager_Construct=1 ("in_effect": "callers"). The loading
// helpers are __cdecl free functions in the original, called only by Init.
const HookSpec kHooks[] = {
    {"cTSWantTreeManager_Construct", 0x551c40, 32, 0x790b3313, reinterpret_cast<void *>(&cTSWantTreeManagerAdapters::Construct)},
    {"cTSWantTreeManager_AddPackage", 0x496ef0, 32, 0x20fb0bd9, reinterpret_cast<void *>(&cTSWantTreeManager::AddPackage)},
    {"cTSWantTreeManager_AddDirectory", 0x496c00, 32, 0xb84b7c4a, reinterpret_cast<void *>(&cTSWantTreeManager::AddDirectory)},
    {"cTSWantTreeManager_WatchDirectories", 0x497230, 32, 0xdd21b97c, reinterpret_cast<void *>(&cTSWantTreeManager::WatchDirectories)},
    {"cTSWantTreeManager_LoadDefinition", 0x555280, 32, 0x98ac4261, reinterpret_cast<void *>(&cTSWantTreeManager::LoadDefinition)},
};
REGISTER_HOOKS(kHooks);

} // namespace
