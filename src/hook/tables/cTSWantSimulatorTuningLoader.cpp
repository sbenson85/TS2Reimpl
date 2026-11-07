#include "hook/hooks.h"
#include "ts/cTSWantSimulatorTuningLoader.h"

namespace {

// The loader's slots (Release 0x494830, StartElement 0x48ece0, EndElement 0x48e390) have no entry: only the game's vtable
// 0xdcae44 refers to them, and only LoadTuning's original stores that vtable; with LoadTuning hooked every loader is ours
// ("in_effect": "callers").
const HookSpec kHooks[] = {
    {"cTSWantSimulatorTuningLoader_LoadTuning", 0x494030, 32, 0xf2b252f7, reinterpret_cast<void *>(&cTSWantSimulatorTuningLoaderAdapters::LoadTuning)},
    {"cTSWantSimulatorTuningLoader_CollectSkillIds", 0x48de50, 32, 0x87ff076e, reinterpret_cast<void *>(&cTSWantSimulatorTuningLoader::CollectSkillIds)},
    {"cTSWantSimulatorTuningLoader_CollectCareerObjects", 0x48d470, 32, 0x0b4b6a1a, reinterpret_cast<void *>(&cTSWantSimulatorTuningLoader::CollectCareerObjects)},
};
REGISTER_HOOKS(kHooks);

} // namespace
