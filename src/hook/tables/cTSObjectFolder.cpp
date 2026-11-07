#include "hook/hooks.h"
#include "ts/cTSObjectFolder.h"

// Standalone shape (docs/standalone-shape.md): the game creates its object folder through the factory 0x4d3280 (operator new
// 0xa4 + the constructor 0x4cef20), so that hook gives every folder our vtables. The slots have no entries: only the game's
// vtables 0xdccf50 / 0xdccf7c / 0xdcd068 reference them, and only the constructor and destructor reference those vtables, so
// they are in effect through their callers and trapped under Hooks.cTSObjectFolder_Create=1.
namespace {

const HookSpec kHooks[] = {
    {"cTSObjectFolder_Create", 0x4d3280, 32, 0x51b4aaf2, reinterpret_cast<void *>(&cTSObjectFolder::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
