// cTSAudioResourceManager (lane IY, src/audio/cTSAudioResourceManager.h): the factory (every manager is ours) and
// AddSegmentKeys, which the original BuildResourceMaps 0x22cf55 still calls.
#include "audio/cTSAudioResourceManager.h"
#include "hook/hooks.h"

namespace {
const HookSpec kHooks[] = {
    {"cTSAudioResourceManager_Create", 0x22c83b, 32, 0xe394dd4e, reinterpret_cast<void *>(&cTSAudioResourceManager::Create)},
    {"cTSAudioResourceManager_AddSegmentKeys", 0x22b97b, 32, 0xec6d48f2, MemberAddress<&cTSAudioResourceManager::AddSegmentKeys>()},
};
REGISTER_HOOKS(kHooks);
} // namespace
