// cTSAudioEvent (lane IY, src/audio/cTSAudioEvent.h): the class operator new and both constructors (every event is ours;
// the slots and the destructor are in effect through them) and the class operator delete.
#include "audio/cTSAudioEvent.h"
#include "hook/hooks.h"

namespace {
const HookSpec kHooks[] = {
    {"cTSAudioEvent_Construct", 0x20ff24, 32, 0xa5121802, reinterpret_cast<void *>(&cTSAudioEventAdapters::Construct)},
    {"cTSAudioEvent_ConstructDefault", 0x20ff8d, 32, 0xfafb4b39, reinterpret_cast<void *>(&cTSAudioEventAdapters::ConstructDefault)},
    {"cTSAudioEvent_OperatorNew", 0x210087, 32, 0xd84bc9bc,
     reinterpret_cast<void *>(static_cast<void *(*)(size_t)>(&cTSAudioEvent::operator new))},
    // Also called by the unwind funclets of the 71 new-expressions (a constructor that throws), outside known functions.
    {"cTSAudioEvent_OperatorDelete", 0x2100f1, 32, 0xea1ccb63,
     reinterpret_cast<void *>(static_cast<void (*)(void *, size_t)>(&cTSAudioEvent::operator delete))},
};
REGISTER_HOOKS(kHooks);
} // namespace
