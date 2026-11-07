#include "gz/cGZPersistResource.h"
#include "hook/hooks.h"

namespace {

using PersistResourceAdapters = cGZPersistResourceAdapters;

// cGZPersistResource in the standalone shape (src/gz/cGZPersistResource.cpp, lane EL): the base of every persist resource.
// The game's derived classes (cRZStringResource, cGZBufferResource, every cSGResource, ...) call the constructor and destructor
// directly and jump to Release from their own Release slots: these adapters run our base for them. Our property-set resource
// derives from the class itself. Not hooked: the folded slots (QI 0x437e30, AddRef 0x99ba50, the key accessors, the count).
// Bisect: turn off all four (the derived game classes then run the game's base again).
const HookSpec kHooks[] = {
    {"cGZPersistResource_Construct", 0x9b5fd0, 32, 0x18a886f7, reinterpret_cast<void *>(&PersistResourceAdapters::Construct)},
    {"cGZPersistResource_Destruct", 0x9b6030, 7, 0xf9edf356, reinterpret_cast<void *>(&PersistResourceAdapters::Destruct)},
    {"cGZPersistResource_DeletingDestructor", 0x9b6050, 32, 0x26928fd8, reinterpret_cast<void *>(&PersistResourceAdapters::DeletingDestructor)},
    {"cGZPersistResource_Release", 0x9b6260, 32, 0x2336bb5b, reinterpret_cast<void *>(&PersistResourceAdapters::Release)},
};
REGISTER_HOOKS(kHooks);

} // namespace
