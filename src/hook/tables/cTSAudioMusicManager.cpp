// cTSAudioMusicManager (lane KI, src/audio/cTSAudioMusicManager.h): the factory (every music manager is ours; the rest is
// in effect through it).
#include "audio/cTSAudioMusicManager.h"
#include "hook/hooks.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSAudioMusicManager_Create", 0x214c96, 32, 0x1dc27616, reinterpret_cast<void *>(&cTSAudioMusicManager::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
