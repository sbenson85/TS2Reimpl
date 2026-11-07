#include "hook/hooks.h"
#include "ts/cTSSimDNA.h"

// Standalone shape (docs/standalone-shape.md): the game creates every DNA object through Create 0x4f9020 (operator new + the
// constructor inline; its address stored by the cTSSimCOMDirector constructor), so that hook gives every object our vtable. The
// slot entries are adapters for the old entry points (cTSSimDNAAdapters): reached only by direct calls or by an object carrying
// the game's vtable (none while the Create hook is on). Only QueryInterface and Release keep one (their byte matches tie through
// it); the other slots' originals are trapped under Hooks.SimDNA_Create=1 (lane GJ, "in_effect": "callers"). To take the class
// out (bisect), turn off SimDNA_Create: the traps disarm and the game's own slot code serves the game-built objects.
namespace {
using nTSGenetics::cTSSimDNA;
using A = nTSGenetics::cTSSimDNAAdapters;
const HookSpec kHooks[] = {
    {"SimDNA_Create", 0x4f9020, 32, 0x9ad4a2eb, reinterpret_cast<void *>(&cTSSimDNA::Create)},
    {"SimDNA_QueryInterface", 0x4fb6a0, 32, 0xb2aa39b6, reinterpret_cast<void *>(&A::QueryInterface)},
    {"SimDNA_Release", 0x4fb6f0, 23, 0x03822fe0, reinterpret_cast<void *>(&A::Release)},
    {"SimDNA_WeightedInsertReallocate", 0x4f50e0, 32, 0x17ff495f, MemberAddress(&nTSGenetics::DNAWeightedVector::InsertReallocate)},
};
// Folded slots 1, 7 and 8 belong to their defining groups and are never hooked here (ours now fill our vtable).
REGISTER_HOOKS(kHooks);
}
