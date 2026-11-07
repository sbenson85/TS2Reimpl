#include "hook/hooks.h"
#include "ts/cTSPersistSystem.h"

namespace {

// Slots only the game's vtables reach have no entry (lane GT, 79: 77 slots and both deleting destructors): their originals
// are trapped under Hooks.cTSPersistSystem_Construct=1&Hooks.cTSPersistSystem_Destruct=1 (the base's deleting destructor
// under its own constructor and destructor), "in_effect": "callers". The slot adapters left are byte matches.
const HookSpec kHooks[] = {
    {"cTSPersistSystem_GetServicePriority", 0x1e0930, 6, 0x2e141d8e, reinterpret_cast<void *>(&cTSPersistSystemAdapters::GetServicePriority)},
    {"cTSPersistSystem_GetResourceManager", 0x1dae50, 7, 0x58f64b5f, reinterpret_cast<void *>(&cTSPersistSystemAdapters::GetResourceManager)},
    {"cTSPersistSystem_ClearDeferWrites", 0x1dea20, 8, 0x86fa4f4e, reinterpret_cast<void *>(&cTSPersistSystemAdapters::ClearDeferWrites)},
    {"cTSPersistSystem_LockSystem", 0x1dae80, 15, 0xac522fbf, reinterpret_cast<void *>(&cTSPersistSystemAdapters::LockSystem)},
    {"cTSPersistSystem_UnlockSystem", 0x1dea10, 15, 0x6a302ffc, reinterpret_cast<void *>(&cTSPersistSystemAdapters::UnlockSystem)},
    {"cTSPersistSystem_StopBackgroundOpen", 0x1e5e40, 32, 0x046e00b6, reinterpret_cast<void *>(&cTSPersistSystemAdapters::StopBackgroundOpen)},
    {"cTSPersistSystem_vf56", 0x1e2d50, 15, 0xf06dd78d, reinterpret_cast<void *>(&cTSPersistSystemAdapters::vf56)},
    {"FUN_a01de870", 0x1de870, 32, 0xf6503f71, MemberAddress<&cTSPersistSystem::NotifyListeners>()},
    {"cTSPersistSystem_vf3", 0x1e0890, 32, 0x787df1a8, reinterpret_cast<void *>(&cTSPersistSystemAdapters::vf3)},
    {"cTSPersistSystem_vf49", 0x1dfd00, 32, 0xfc187191, reinterpret_cast<void *>(&cTSPersistSystemAdapters::vf49)},
    {"cTSPersistSystem_vf52", 0x1dd9d0, 32, 0xf703b8e4, reinterpret_cast<void *>(&cTSPersistSystemAdapters::vf52)},
    {"cTSPersistSystem_ClearSpeculativeLoader", 0x1defd0, 16, 0x47c7941b, reinterpret_cast<void *>(&cTSPersistSystemAdapters::ClearSpeculativeLoader)},
    {"FUN_a01e5580", 0x1e5580, 32, 0x331a6358, MemberAddress<&cTSPersistSystem::ReleaseSegments>()},
    {"cTSPersistSystem_SetKeyedObject", 0x1e5200, 32, 0x161fd087, reinterpret_cast<void *>(&cTSPersistSystemAdapters::SetKeyedObject)},
    {"cTSPersistSystem_vf168_56", 0x1e3b10, 11, 0xa0ee26f2, reinterpret_cast<void *>(&cTSPersistSystemAdapters::vf168_56)},
    {"cTSPersistSystem_SetPackedFileGroup", 0x1e5260, 32, 0x97f54831, reinterpret_cast<void *>(&PersistSetPackedFileGroup)},
    {"cTSPersistSystem_CompareRecordContents", 0x1dacb0, 32, 0x8d509392, reinterpret_cast<void *>(&CompareRecordContents)},
    {"FUN_a01e7230", 0x1e7230, 32, 0xa30dcb97, MemberAddress<&cTSPersistSystem::SetUpSegment>()},
    {"cTSPersistSystemPrimary_OpenContentFolder", 0x1e3c70, 32, 0xb78c5c24, MemberAddress<&cTSPersistSystem::OpenContentFolder>()},
    {"cTSPersistSystem_MakePathKey", 0x1e6e80, 32, 0x1be487aa, MemberAddress<&cTSPersistSystem::MakePathKey>()},
    {"cTSPersistSystem_NormalisePath", 0x1e8b80, 32, 0x3fc75422, MemberAddress<&cTSPersistSystem::NormalisePath>()},
    {"cTSPersistSystem_FillInSegments", 0x1e95c0, 32, 0x6ea6adcf, MemberAddress<&cTSPersistSystem::FillInSegments>()},
    {"cTSPersistSystem_SaveChangeRecords", 0x1dedf0, 32, 0x17c76598, MemberAddress<&cTSPersistSystem::SaveChangeRecords>()},
    {"cTSPersistSystem_PathStamp", 0x1e8ef0, 32, 0x8925daee, MemberAddress<&cTSPersistSystem::PathStamp>()},
    {"cTSPersistSystem_ResetPathEntry", 0x1e94e0, 32, 0x61ccc4b5, MemberAddress<&cTSPersistSystem::ResetPathEntry>()},
    {"cTSPersistSystem_IndexSegmentGroups", 0x1e9b50, 32, 0xb1743e0f, MemberAddress<&cTSPersistSystem::IndexSegmentGroups>()},
    {"cTSPersistSystem_SegmentGroups", 0x1e97b0, 32, 0x852ba90f, MemberAddress<&cTSPersistSystem::SegmentGroups>()},
    {"cTSPersistSystem_ParkOpenSegments", 0x1ea760, 32, 0x48ed0e0c, MemberAddress<&cTSPersistSystem::ParkOpenSegments>()},
    {"cTSPersistSystem_RelistParkedSegments", 0x1e9eb0, 32, 0x5b37c9d8, MemberAddress<&cTSPersistSystem::RelistParkedSegments>()},
    {"cTSPersistSystem_LoadResourceNames", 0x1e8fd0, 32, 0x431bb5f6, MemberAddress<&cTSPersistSystem::LoadResourceNames>()},
    {"cTSPersistSystem_SaveResourceNames", 0x1ea4f0, 32, 0xe11d425a, MemberAddress<&cTSPersistSystem::SaveResourceNames>()},
    // TS2-Extender also hooks 0x1ea190 (save_groupmap_cache); native DLL mods are not a compatibility target.
    {"cTSPersistSystem_SaveGroupMap", 0x1ea190, 32, 0x7fb0ee49, MemberAddress<&cTSPersistSystem::SaveGroupMap>()},
    {"cTSPersistSystem_BackgroundOpenThread", 0x1defe0, 32, 0xf3e86bbc, reinterpret_cast<void *>(&cTSPersistSystem::BackgroundOpenThread)},
    // Lane FX (stage A of the standalone shape): the constructor and destructors build and take down our class, with our
    // vtables. 0x1d8b90 is the bisect switch: without it the game builds the object as before and nothing below is reached.
    {"cTSPersistSystem_Construct", 0x1d8b90, 32, 0x9a728e70, reinterpret_cast<void *>(&cTSPersistSystemAdapters::Construct)},
    {"cTSPersistSystem_Destruct", 0x1d9cb0, 32, 0x20568b9e, reinterpret_cast<void *>(&cTSPersistSystemAdapters::Destruct)},
    // The base, which the game builds only inside 0x1d8b90 (the twin in src/gz/cGZPersistResourceManagerShape.*).
    {"cGZPersistResourceManager_Construct", 0x9b3f30, 32, 0xc8b52c31, reinterpret_cast<void *>(&gz::cGZPersistResourceManagerAdapters::Construct)},
    {"cGZPersistResourceManager_Destruct", 0x9b4120, 32, 0xdbd6ef92, reinterpret_cast<void *>(&gz::cGZPersistResourceManagerAdapters::Destruct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
