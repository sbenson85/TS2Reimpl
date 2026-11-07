// cTSAudioTSObjectUpdateDemon (lanes LD, LE; src/audio/cTSAudioTSObjectUpdateDemon.h): the factory only. Every demon is
// ours, and the rest, its monitors and playing sounds included, is in effect through it: no original body of the three
// classes is left, so nothing original calls a member (lane LE dropped the hooks of tPlayingSound::SetMonitor 0x229f6e
// and Seconds 0x22a07f, which LD's left-original update bodies had needed).
#include "audio/cTSAudioTSObjectUpdateDemon.h"
#include "hook/hooks.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSAudioTSObjectUpdateDemon_Create", 0x229c42, 32, 0x0f2228e5,
     reinterpret_cast<void *>(&cTSAudioTSObjectUpdateDemon::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
