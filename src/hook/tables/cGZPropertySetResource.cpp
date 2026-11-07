#include "gz/cGZPropertySetResource.h"
#include "hook/hooks.h"

namespace {

using FactoryAdapters = cGZPropertySetFactoryAdapters;
using ResourceAdapters = cGZPropertySetResourceStringAdapters;
using U32ResourceAdapters = cGZPropertySetResourceUint32Adapters;

// The property-set resources and cGZPropertySetFactory (src/gz/cGZPropertySetResource.cpp / .size.cpp).
const HookSpec kHooks[] = {
    // cGZPropertySetResourceString in the standalone shape (lane EL): the constructor hook gives every resource the game builds
    // our vtables (our creation sites use new); the other two are adapters for direct calls. The class's switch (bisect):
    // _Construct (the game's creation sites; ours always build our class).
    {"cGZPropertySetResourceString_Construct", 0x5519c0, 32, 0x03af3d80, reinterpret_cast<void *>(&ResourceAdapters::Construct)},
    // cGZPropertySetResourceUint32 in the standalone shape (lane EQ): our factory builds it with `new`; the constructor hook
    // covers the game's factory (hook off). The class's switch (bisect): _Construct with the factory's _CreateInstance2.
    {"cGZPropertySetResourceUint32_Construct", 0xc2bb25, 32, 0x52a32fdb, reinterpret_cast<void *>(&U32ResourceAdapters::Construct)},
    {"cGZPropertySetResourceUint32_Destruct", 0xc2bcb3, 27, 0x4941af3e, reinterpret_cast<void *>(&U32ResourceAdapters::Destruct)},
    // cGZPropertySetFactory in the standalone shape (docs/standalone-shape.md): Create (the only creation site) gives every
    // factory our vtables; the constructor and slot entries are adapters for a factory the game built itself. The class's
    // switch (bisect): turn off both _Create and _Construct.
    {"cGZPropertySetFactory_Construct", 0xc2bb7a, 32, 0x684a0214, reinterpret_cast<void *>(&FactoryAdapters::Construct)},
    {"cGZPropertySetFactory_Create", 0xc2be41, 32, 0x59e6dc45, reinterpret_cast<void *>(&cGZPropertySetFactory::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
