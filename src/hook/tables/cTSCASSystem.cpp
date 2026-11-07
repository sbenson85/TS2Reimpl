// Hooks for nTSCASCore::cTSCASSystem (src/cas/cTSCASSystem.cpp), lanes IU, JI: the standalone shape. The creator (0x502860,
// registered by address with the sim COM director) and the constructor (0x4ff850, called only by the creator) build our
// class, so every CAS system carries our vtables; its slots, QueryInterface, Init and the destructors are reached only
// through those vtables (in effect through callers, trapped by tools/callers.py --write-traps).
#include "hook/hooks.h"
#include "cas/cTSCASSystem.h"

namespace {

using nTSCASCore::cTSCASSystem;
using nTSCASCore::cTSCASSystemAdapters;

const HookSpec kHooks[] = {
    {"nTSCASCore_cTSCASSystem_Construct", 0x4ff850, 32, 0x065b27f9, reinterpret_cast<void *>(&cTSCASSystemAdapters::Construct)},
    {"nTSCASCore_cTSCASSystem_Create", 0x502860, 32, 0x4e14c041, reinterpret_cast<void *>(&cTSCASSystem::Create)},
    // LoadResource's body, also called directly by game code (0x502c00, 0x506c80).
    {"nTSCASCore_cTSCASSystem_LoadResource", 0x502dc0, 32, 0x870eb0e9, reinterpret_cast<void *>(&cTSCASSystemAdapters::LoadResource)},
    {"nTSCASCore_cTSCASSystem_AllowCustomContent", 0x5008e0, 32, 0x15462b71, &cTSCASSystem::AllowCustomContent},
    {"nTSCASCore_cTSCASSystem_GatherByType", 0x509b90, 32, 0x4f3bece3, &cTSCASSystemAdapters::GatherByType},
    {"nTSCASCore_cTSCASSystem_GatherFromBin", 0x50a1d0, 32, 0x76063fd2, &cTSCASSystemAdapters::GatherFromBin},
    {"nTSCASCore_cTSCASSystem_FillTypeCache", 0x50bc00, 32, 0x55080a22, &cTSCASSystemAdapters::FillTypeCache},
    {"nTSCASCore_cTSCASSystem_LoadByKeyType", 0x502c00, 32, 0xc487f82f, &cTSCASSystemAdapters::LoadByKeyType},
    // Lane JM: the zero-key test (19 callers) and the AGS filters' fragment test (also cTSSpecialOutfitFragFilter's, game code).
    {"nTSCASCore_IsZeroKey", 0x504cb0, 32, 0xc8665824, &nTSCASCore::CASFilterAdapters::IsZeroKey},
    {"nTSCASCore_cTSAGSFragFilter_AcceptFragment", 0x504fa0, 32, 0x2af00ecd, &nTSCASCore::CASFilterAdapters::AcceptFragment},
    // The mesh overlay filter's Accept: the collar filter's Accept 0x6fa298 (game code) calls it directly.
    {"nTSCASCore_cTSMeshOverlayFragmentFilter_Accept", 0xb64960, 32, 0xe2912213, &nTSCASCore::CASFilterAdapters::MeshAccept},
    {"FUN_a0501f80", 0x501f80, 32, 0xc0ec86fa, &nTSCASCore::FindNamedPart},
};
REGISTER_HOOKS(kHooks);

} // namespace
