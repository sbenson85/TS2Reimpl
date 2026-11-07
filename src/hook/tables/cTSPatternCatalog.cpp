// Hooks for cTSPatternCatalog (src/cas/cTSPatternCatalog.cpp), lane JW: the standalone shape. The creator (0xca27f0, the
// COM director's) and the constructor (0xc9f9e0, called only by the creator) build our class, so every catalog carries our
// vtables; its slots, QueryInterface, destructors, the +4 / +0x1c thunks, the loader's helpers and cPatternFilter (built
// only by Load) are reached only through those (in effect through callers, trapped by tools/callers.py --write-traps).
#include "hook/hooks.h"
#include "cas/cTSPatternCatalog.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSPatternCatalog_Construct", 0xc9f9e0, 32, 0xac0dcc6a, reinterpret_cast<void *>(&cTSPatternCatalogAdapters::Construct)},
    {"cTSPatternCatalog_Create", 0xca27f0, 32, 0x29101ac2, reinterpret_cast<void *>(&cTSPatternCatalog::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
