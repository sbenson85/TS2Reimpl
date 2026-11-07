// cTSAudioListener (lane KH, src/audio/cTSAudioListener.h): the factory (every listener is ours; the rest is in effect
// through it). 0x211065 is missing from db/functions.jsonl (the director only pushes its address), so hookspec.py refuses it;
// the entry was written by hand: 53 bytes (`mov eax, ...; call _EH_prolog` .. `ret` at 0x211099), CRC of the first 32.
#include "audio/cTSAudioListener.h"
#include "hook/hooks.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSAudioListener_Create", 0x211065, 32, 0x4c653e81, reinterpret_cast<void *>(&cTSAudioListener::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
