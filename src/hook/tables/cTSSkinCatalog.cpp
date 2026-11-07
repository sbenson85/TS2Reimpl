#include "hook/hooks.h"
#include "cas/cTSSkinCatalog.h"

// Standalone shape (docs/standalone-shape.md): the game creates the catalog only through Create (0xb7a0e0, the class
// factory function the COM director registers), which calls the constructor (0xb72310); with these two hooks every
// catalog carries our vtables. The slots, the destructors and the +4 thunk are reached only through the game's vtables,
// which no live object carries any more: their status entries say "in_effect": "callers" and the host traps the
// originals.
namespace {

using nTSCASCore::cTSSkinCatalog;
using nTSCASCore::cTSSkinCatalogAdapters;

const HookSpec kHooks[] = {
    {"nTSCASCore_cTSSkinCatalog_Construct", 0xb72310, 32, 0xe6a5a760, reinterpret_cast<void *>(&cTSSkinCatalogAdapters::Construct)},
    {"nTSCASCore_cTSSkinCatalog_Create", 0xb7a0e0, 32, 0x327916ab, reinterpret_cast<void *>(&cTSSkinCatalog::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
