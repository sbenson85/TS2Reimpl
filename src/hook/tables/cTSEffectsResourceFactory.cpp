#include "hook/hooks.h"
#include "ts/cTSEffectsResourceFactory.h"

// Standalone shape (docs/standalone-shape.md): factories are made by Create 0xbd2ff0 (registered by the effects COM
// director) and by cTSEffectsManager::Init (inline new plus the constructor 0xbd2c50): with both hooks every factory carries
// our vtable. The slot entries are adapters (a factory the game built, with those hooks off). Slots 0-6 are the base's
// (hooked as cGZPersistResourceFactory's); slot 8 is the shared body 0x1f62f0, never hooked.
namespace {

using A = cTSEffectsResourceFactoryAdapters;

const HookSpec kHooks[] = {
    {"cTSEffectsResourceFactory_Create", 0xbd2ff0, 32, 0xb6d1f865, reinterpret_cast<void *>(&cTSEffectsResourceFactory::Create)},
    {"cTSEffectsResourceFactory_Construct", 0xbd2c50, 24, 0x724c1d57, reinterpret_cast<void *>(&A::Construct)},
    {"cTSEffectsResourceFactory_Destruct", 0xbd2e30, 11, 0xae7978b0, reinterpret_cast<void *>(&A::Destruct)},
    {"cTSEffectsResourceFactory_DeletingDestructor", 0xbd2e80, 32, 0x4bc10821, reinterpret_cast<void *>(&A::DeletingDestructor)},
    {"cTSEffectsResourceFactory_CreateResource", 0xbd2ed0, 32, 0xc0b74404, reinterpret_cast<void *>(&A::CreateResource)},
    {"cTSEffectsResourceFactory_ReadRecord", 0xbd4030, 32, 0xf34fb4e7, reinterpret_cast<void *>(&A::ReadRecord)},
    {"cTSEffectsResourceFactory_WriteRecord", 0xbd40e0, 32, 0xf34fb4e7, reinterpret_cast<void *>(&A::WriteRecord)},
};
REGISTER_HOOKS(kHooks);

} // namespace
