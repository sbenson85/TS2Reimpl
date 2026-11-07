// cGZWinMgrW32 (lane KQ, src/gz/cGZWinMgrW32.h): the factory the Win32 COM director registers (class 0x84174448). Every
// window manager is ours; the constructors, destructors, slots, the message targets and the keyboard are in effect
// through it. The keyboard's AsyncKeyDown is hooked on its own (other callers).
#include "gz/cGZWinMgrW32.h"
#include "hook/hooks.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZWinMgrW32_Create", 0x970d39, 32, 0xdd7d1dcf, reinterpret_cast<void *>(&cGZWinMgrW32::Create)},
    // Win32's key state (cdecl), also called by 12 other functions in the image.
    {"cRZKeyboard_AsyncKeyDown", 0x9d0006, 32, 0x40d0f528, reinterpret_cast<void *>(&cRZKeyboard::AsyncKeyDown)},
};
REGISTER_HOOKS(kHooks);

} // namespace
