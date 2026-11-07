#include "app/AppCOMDirectors.h"
#include "hook/hooks.h"

namespace {

// The application's COM director registration (src/app/AppCOMDirectors.*).
const HookSpec kHooks[] = {
    {"FUN_a0140f40", 0x140f40, 32, 0x8a5c5541, reinterpret_cast<void *>(&appcom::AddAudioDirector)},
};
REGISTER_HOOKS(kHooks);

} // namespace
