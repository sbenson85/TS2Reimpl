#include "hook/hooks.h"
#include "rz/cRZExceptionNotification.h"

namespace {

// cRZExceptionNotification in the standalone shape (lane HV): the constructor is an adapter (docs/standalone-shape.md).
// The one creation site, the framework constructor 0x141d76, is ours and builds it with `new`, so in game no object
// carries the game's vtable 0xda44d4. Its slots (QueryInterface 0x1c8d3f, Release 0x1c8d77, Notify 0x1c8cb7, the
// deleting destructor 0x1c8c98) and the destructor body 0x1c8c91 are in effect through their callers: no hook, the
// originals trapped while the constructor hook is in. AddRef 0x148fb0 is folded with other classes: not hooked.
using A = cRZExceptionNotificationAdapters;
const HookSpec kHooks[] = {
    {"cRZExceptionNotification_Construct", 0x1c8c84, 13, 0xc5677ed3, reinterpret_cast<void *>(&A::Construct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
