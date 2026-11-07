// cGZWinIMEServerW32 (lane KR, src/gz/cGZWinIMEServerW32.h): the factory the Win32 COM director registers. Every IME
// server is ours; the constructors, destructors, slots and helpers (and the key message it posts) are in effect through it.
#include "gz/cGZWinIMEServerW32.h"
#include "hook/hooks.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZWinIMEServerW32_Create", 0x970d15, 32, 0xa204df83, reinterpret_cast<void *>(&cGZWinIMEServerW32::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
