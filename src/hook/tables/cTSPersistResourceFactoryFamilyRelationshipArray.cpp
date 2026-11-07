#include "hook/hooks.h"
#include "ts/cTSPersistResourceFactoryFamilyRelationshipArray.h"

// Standalone shape (docs/standalone-shape.md): the game creates the factory only through 0x4f2740 (registered by address in the
// cTSSimCOMDirector constructor), so that hook gives every factory our vtable. The slot entries are adapters for objects
// carrying the game's vtable (none while the Create hook is on). The folded slots (0x1ee250, 0x148fb0, 0x1ee310, 0x431e60,
// 0x147a30) are shared with other factories and never hooked.
// Lane GP: CreateInstanceByType, Read and Write have no entry (only the game's vtable reaches them): trapped under
// Hooks.cTSPersistResourceFactoryFamilyRelationshipArray_Create=1 ("in_effect": "callers"). The deleting destructor keeps
// its adapter (byte match).
namespace {

using A = cTSPersistResourceFactoryFamilyRelationshipArrayAdapters;

// The creator 0x4f2740 (37 bytes) is only referenced by address, so it is not in db/functions.jsonl: CRC computed by hand
// (hookcrc.py checks it).
const HookSpec kHooks[] = {
    {"cTSPersistResourceFactoryFamilyRelationshipArray_Create", 0x4f2740, 32, 0xcf2c3138, reinterpret_cast<void *>(&cTSPersistResourceFactoryFamilyRelationshipArray::Create)},
    {"cTSPersistResourceFactoryFamilyRelationshipArray_vf7", 0x4f2170, 32, 0xe0b7449f, reinterpret_cast<void *>(&A::DeletingDestructor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
