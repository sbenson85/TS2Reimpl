// Hooks for cTSMaterialCatalog (src/cas/cTSMaterialCatalog.cpp), lane JX: the standalone shape. The creator (0xd06930, the
// COM director's) and the constructor (0xd04bd0, called only by the creator) build our class, so every catalog carries our
// vtables; its slots, QueryInterface, DoMessage, destructors, the +4 / +0x1c / +0x20 thunks, the loader's and getters'
// helpers and cTSMaterialFilter (built only by Load / LoadSegment) are reached only through those (in effect through
// callers, trapped by tools/callers.py --write-traps).
#include "hook/hooks.h"
#include "cas/cTSMaterialCatalog.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSMaterialCatalog_Construct", 0xd04bd0, 32, 0xfb0e99cb, reinterpret_cast<void *>(&cTSMaterialCatalogAdapters::Construct)},
    {"cTSMaterialCatalog_Create", 0xd06930, 32, 0xf2d17d87, reinterpret_cast<void *>(&cTSMaterialCatalog::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
