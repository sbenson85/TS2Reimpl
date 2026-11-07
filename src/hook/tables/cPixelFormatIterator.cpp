#include "hook/hooks.h"
#include "gz/cPixelFormatIterator.h"

namespace {

// The creators are the class's switch (docs/standalone-shape.md; src/gz/cPixelFormatIterator.h): the COM factory
// (registered by address for class 0xbb782354) and the manager's CreateIterator (the constructor 0x15d040's only
// caller) hand out our iterators, so the game's vtable 0xd9d084 is never stored and every slot is in effect through
// its callers (trapped while both hooks are installed; the constructor while CreateIterator's is).
const HookSpec kHooks[] = {
    {"nGZGraphic4_cPixelFormatIterator_CreateEmpty", 0x15ea00, 32, 0xa914e286, reinterpret_cast<void *>(&nGZGraphic4::cPixelFormatIterator::CreateEmpty)},
    {"nGZGraphic4_cPixelFormatsManagerDX_CreateIterator", 0x15e860, 32, 0x04e047d1, reinterpret_cast<void *>(&nGZGraphic4::cPixelFormatIteratorAdapters::CreateRange)},
};
REGISTER_HOOKS(kHooks);

} // namespace
