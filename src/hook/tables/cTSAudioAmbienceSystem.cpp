// cTSAudioAmbienceSystem (lane KF, src/audio/cTSAudioAmbienceSystem.h): the factory (every system is ours), and
// ResetCursors / LoadCurve, which the original layer builder 0x224ce3 still calls.
#include "audio/cTSAudioAmbienceSystem.h"
#include "hook/hooks.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSAudioAmbienceSystem_Create", 0x22460b, 32, 0xcbecd467, reinterpret_cast<void *>(&cTSAudioAmbienceSystem::Create)},
    {"cTSAudioAmbienceSystem_ResetCursors", 0x225425, 32, 0xd26db8fc, MemberAddress<&cTSAudioAmbienceSystem::ResetCursors>()},
    {"cTSAudioAmbienceSystem_LoadCurve", 0x22544f, 8, 0xd6923ae4, MemberAddress<&cTSAudioAmbienceSystem::LoadCurve>()},
};
REGISTER_HOOKS(kHooks);

} // namespace
