// cTSAudioSoundManager (lane IY, src/audio/cTSAudioSoundManager.h): the factory, so every sound manager is ours, and the
// volume ramp's step, which cTSAudioSoundInstance (0x231744) still calls, and the startup workers.
#include "audio/cTSAudioSoundManager.h"
#include "hook/hooks.h"

namespace {
const HookSpec kHooks[] = {
    {"cTSAudioSoundManager_Create", 0x21a182, 32, 0x45c12dc2, reinterpret_cast<void *>(&cTSAudioSoundManager::Create)},
    {"AudioRamp_Step", 0x21d114, 32, 0xaf92a0c4, MemberAddress<&AudioRamp::Step>()},
    // The startup workers (only our Init / Update / HandleEvent / Request37 call the first four; the request worker
    // 0x220eec, still the original's, calls the two config lookups).
    {"cTSAudioSoundManager_LoadSoundDefaults", 0x21f164, 32, 0x7970def3, MemberAddress<&cTSAudioSoundManager::LoadSoundDefaults>()},
    {"cTSAudioSoundManager_ApplySettings", 0x220320, 32, 0xfdef7e70, MemberAddress<&cTSAudioSoundManager::ApplySettings>()},
    {"cTSAudioSoundManager_UpdateSpeakerConfig", 0x2221e8, 32, 0x14f8854c, MemberAddress<&cTSAudioSoundManager::UpdateSpeakerConfig>()},
    {"cTSAudioSoundManager_UpdateSounds", 0x222147, 32, 0xdeba3600, MemberAddress<&cTSAudioSoundManager::UpdateSounds>()},
    {"cTSAudioSoundManager_HasConfigProperty", 0x21e9ab, 32, 0xe0012c0c, MemberAddress<&cTSAudioSoundManager::HasConfigProperty>()},
    {"cTSAudioSoundManager_GetConfigProperty", 0x21dc23, 32, 0xe49c82ae, MemberAddress<&cTSAudioSoundManager::GetConfigProperty>()},
};
REGISTER_HOOKS(kHooks);
} // namespace
