// cTSAudioAmbienceManager (lane KG, src/audio/cTSAudioAmbienceManager.h): the factory (every manager is ours; the rest
// is in effect through it).
#include "audio/cTSAudioAmbienceManager.h"
#include "hook/hooks.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSAudioAmbienceManager_Create", 0x22784f, 32, 0xead990d0, reinterpret_cast<void *>(&cTSAudioAmbienceManager::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
