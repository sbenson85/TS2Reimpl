#include "hook/hooks.h"
#include "ts/cTSCatalog.h"

// Standalone shape (docs/standalone-shape.md): the game creates its one catalogue through the factory 0xc105a5 (operator new
// 0x1bc + the constructor 0xc0e538; registered by nTSMetaData::cCOMDirector), so that hook gives the catalogue our vtable.
// The slots have no entries: only the game's vtable 0xe91b4c references them, and only the constructor and destructor reference
// that vtable, so they are in effect through their callers and trapped under Hooks.cTSCatalog_Create=1.
// ApplyMaterialValue is also called by the pattern-key handler 0xc130db (not ours), so it keeps a hook.
namespace {

const HookSpec kHooks[] = {
    {"cTSCatalog_Create", 0xc105a5, 32, 0x9c2bca6b, reinterpret_cast<void *>(&cTSCatalog::Create)},
    {"cTSCatalog_ApplyMaterialValue", 0xc146e7, 32, 0xf4c25a79, reinterpret_cast<void *>(&cTSCatalog::ApplyMaterialValue)},
};
REGISTER_HOOKS(kHooks);

} // namespace
