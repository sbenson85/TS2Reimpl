// Hooks for cGZRegistry (src/gz/cGZRegistry.size.cpp, cGZRegistryOps.size.cpp), lane KD: the standalone shape. The
// constructor (0xc6cb27; no caller in the image, cTSAppFW::AddApplicationServicesHere is ours and builds through the
// adapter) builds our class, so every registry carries our vtables; QueryInterface, slots 3..7, the destructors and the
// thunks are reached only through the game's vtables, which nothing stores any more (in effect through callers, trapped by
// tools/callers.py --write-traps). The tree is a real std::multimap (cGZRegistryMap): its constructor, destructor, node
// destruction and subtree erase (0xc6caeb, 0xc6ca2d, 0xc6cad2, 0xc6ca4d) are library matches and have no entry.
// The constructor is missing from db/functions.jsonl (CRC computed by hand from the unpacked exe).
#include "hook/hooks.h"
#include "gz/cGZRegistry.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZRegistry_Construct", 0xc6cb27, 32, 0x9ceb3a4a, reinterpret_cast<void *>(&cGZRegistryAdapters::Construct)},
    {"cGZRegistryEntry_Destruct", 0xc6cb8a, 26, 0x6292ca2f, MemberAddress(&cGZRegistryEntry::Destruct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
