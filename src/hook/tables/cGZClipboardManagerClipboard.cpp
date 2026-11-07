// Hooks for cGZClipboardManagerClipboard (src/gz/cGZClipboardManagerClipboard.size.cpp), lane LT: the standalone shape.
// The constructor 0xc6d26f (its one caller, the manager's Init, is ours and builds the class directly) and the
// clipboard data constructor 0xc6d2a5 (called only by SetText 0xc6e596) build ours; the clipboard's slots, the viewer
// (constructor 0xc6d2cf, 0xc6d3f0, read 0xc6dd9a, write 0xc6e80f, window procedure 0xc6e73f) and the notice helpers are
// reached only through our vtable and members (in effect through callers). The message server cache 0x9c5ab4 and its
// acquisition 0x762db4 are hooked for their many callers.
#include "hook/hooks.h"
#include "gz/cGZClipboardManagerClipboard.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZClipboardManagerClipboard_Construct", 0xc6d26f, 32, 0xcfaa58b2,
     reinterpret_cast<void *>(&cGZClipboardManagerClipboardAdapters::Construct)},
    {"cGZClipboardManagerClipboardData_Construct", 0xc6d2a5, 32, 0x338be89f,
     reinterpret_cast<void *>(&cGZClipboardManagerClipboardAdapters::ConstructData)},
    {"AcquireMessageServer", 0x762db4, 32, 0x57e75c2b,
     reinterpret_cast<void *>(&cGZClipboardManagerClipboardAdapters::AcquireMessageServer)},
    {"GetMessageServer", 0x9c5ab4, 32, 0xa8422dea, reinterpret_cast<void *>(&GetMessageServer)},
};
REGISTER_HOOKS(kHooks);

} // namespace
