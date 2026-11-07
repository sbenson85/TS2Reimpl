// Hooks for nTSCASCore::cTSBinCatalog (src/cas/cTSBinCatalog.cpp), lane JG: the standalone shape. The creator (0xb8b270,
// the COM director's) and the constructor (0xb86c50, called only by the creator) build our class, so every catalog
// carries our vtables; its slots, QueryInterface, destructors and the +4 / +0x1c thunks are reached only through those
// vtables (in effect through callers, trapped by tools/callers.py --write-traps).
#include "hook/hooks.h"
#include "cas/cTSBinCatalog.h"

namespace {

using nTSCASCore::cTSBinCatalog;
using nTSCASCore::cTSBinCatalogAdapters;

const HookSpec kHooks[] = {
    {"nTSCASCore_cTSBinCatalog_Construct", 0xb86c50, 32, 0x39fe776e, reinterpret_cast<void *>(&cTSBinCatalogAdapters::Construct)},
    {"nTSCASCore_cTSBinCatalog_Create", 0xb8b270, 32, 0x58b1d506, reinterpret_cast<void *>(&cTSBinCatalog::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
