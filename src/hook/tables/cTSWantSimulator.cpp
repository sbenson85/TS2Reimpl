#include "hook/hooks.h"
#include "ts/cTSWantSimulator.h"

namespace {

// Functions only the game's vtables (or original code reached only through them) reach have no entry (lane GT, 34: 30 slots,
// the deleting destructor, RestartTick, WantMemoryStore::AddToIds, WantFloatText): their originals are trapped under
// Hooks.cTSWantSimulator_Construct=1 ("in_effect": "callers").
const HookSpec kHooks[] = {
    {"cTSWantSimulator_Construct", 0x4855d0, 32, 0x69dcee90, reinterpret_cast<void *>(&cTSWantSimulatorAdapters::Construct)},
    {"cTSWantSimulator_Destruct", 0x485ba0, 32, 0x4ef1810e, reinterpret_cast<void *>(&cTSWantSimulatorAdapters::Destruct)},
    {"cTSWantSimulator_Release", 0x494800, 26, 0xba9c5405, reinterpret_cast<void *>(&cTSWantSimulatorAdapters::Release)},
    {"cTSWantSimulator_PositiveAttributeA1", 0x48de30, 32, 0x290ca0d3, reinterpret_cast<void *>(&cTSWantSimulatorAdapters::PositiveAttributeA1)},
    {"cTSWantSimulator_TogglePaused", 0x497460, 17, 0xa91b62a3, reinterpret_cast<void *>(&cTSWantSimulatorAdapters::TogglePaused)},
    {"cTSWantSimulator_IsPaused", 0x496920, 7, 0xee471211, reinterpret_cast<void *>(&cTSWantSimulatorAdapters::IsPaused)},
    {"WantMemoryStore_Construct", 0x5509c0, 32, 0x806377ae, MemberAddress(&WantMemoryStore::Construct)},
    {"WantMemoryStore_Destruct", 0x550b70, 32, 0x68aeda8d, MemberAddress(&WantMemoryStore::Destruct)},
    {"WantMemoryStore_Clear", 0x5516d0, 32, 0xfbc0845e, MemberAddress(&WantMemoryStore::Clear)},
    {"WantMemoryStore_SyncTick", 0x551360, 26, 0x1d786efb, MemberAddress(&WantMemoryStore::SyncTick)},
    {"WantMemoryStore_AddMemory", 0x550cb0, 32, 0x314c786e, MemberAddress(&WantMemoryStore::AddMemory)},
    {"WantMemoryStore_Read", 0x551380, 32, 0xd36d36d8, MemberAddress(&WantMemoryStore::Read)},
    {"WantMemoryStore_Write", 0x551700, 32, 0x410abc2d, MemberAddress(&WantMemoryStore::Write)},
    {"cTSWantSimulator_GetMemoryStore", 0x48d650, 32, 0x8f6e5989, reinterpret_cast<void *>(&cTSWantSimulatorAdapters::GetMemoryStore)},
    {"cTSWantSimulator_AddDefaultMemory", 0x486310, 32, 0x4f86aeec, reinterpret_cast<void *>(&cTSWantSimulatorAdapters::AddDefaultMemory)},
    {"cTSWantSimulator_IsWantAllowed", 0x48e2f0, 32, 0x8e85baeb, reinterpret_cast<void *>(&cTSWantSimulatorAdapters::IsWantAllowed)},
    {"cTSWantSimulator_WriteSimWants", 0x4983a0, 32, 0x8566e4b2, reinterpret_cast<void *>(&cTSWantSimulatorAdapters::WriteSimWants)},
    {"cTSWantSimulator_ReadSimWants", 0x492970, 32, 0xbdc46d83, reinterpret_cast<void *>(&cTSWantSimulatorAdapters::ReadSimWants)},
    {"cTSWantSimulator_ReadSimRecord", 0x492ea0, 32, 0xf6e5cd31, reinterpret_cast<void *>(&cTSWantSimulatorAdapters::ReadSimRecord)},
    {"WantSimContext_WantSimContext", 0x4854b0, 32, 0x447a55f4, MemberAddress(&WantSimContext::Construct)},
    {"cTSWantSimulator_DeleteSim", 0x487be0, 32, 0x1c0f75fb, reinterpret_cast<void *>(&cTSWantSimulatorAdapters::DeleteSim)},
    {"cTSWantSimulator_vf55", 0x48d1e0, 32, 0xbb083766, reinterpret_cast<void *>(&cTSWantSimulatorAdapters::FlushUpdates)},
    {"cTSWantSimulator_vf23", 0x48dfc0, 8, 0x99e85cbe, reinterpret_cast<void *>(&cTSWantSimulatorAdapters::GetWantDefinition)},
    {"cTSWantSimulator_vf24", 0x48dfd0, 8, 0x9733d48c, reinterpret_cast<void *>(&cTSWantSimulatorAdapters::DefinitionsSlot7)},
    {"FUN_a048df40", 0x48df40, 32, 0xbbdd6670, &cTSWantSimulatorAdapters::FindWantObject},
    {"FUN_a0551320", 0x551320, 32, 0x28423e72, MemberAddress(&WantMemoryStore::AdvanceToTick)},
    {"FUN_a0550d80", 0x550d80, 32, 0x2207e30b, MemberAddress(&WantMemoryStore::CollectMemories)},
    {"FUN_a05516a0", 0x5516a0, 32, 0xf19e86d3, MemberAddress(&WantMemoryStore::RestartIds)},
    // Lane GF: the helpers of slot 52 (0x489210 itself has no hook: only the vtable refers to it).
    {"WantSimContext_Assign", 0x485e40, 32, 0xdb7ccf37, MemberAddress(&WantSimContext::Assign)},
    {"cTSWantSimulator_IndexOfMatch", 0x48ce50, 32, 0xefaa3360, MemberAddress<&cTSWantSimulator::IndexOfMatch>()},
};
REGISTER_HOOKS(kHooks);

} // namespace
