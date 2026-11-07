// Hooks for cGZClipboardManager (src/gz/cGZClipboardManager.size.cpp), lane KD: the standalone shape. The constructor
// (0xc6d222; no caller in the image, cTSAppFW::AddApplicationServicesHere is ours and builds the class directly) builds
// our class, so every manager carries our vtables; QueryInterface, AddRef, Release, Init, Shutdown, slots 5..20, the
// destructors and the thunks are reached only through the game's vtables, which nothing stores any more (in effect through
// callers, trapped by tools/callers.py --write-traps). The clipboard object's hooks are in cGZClipboardManagerClipboard.cpp.
#include "hook/hooks.h"
#include "gz/cGZClipboardManager.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZClipboardManager_Construct", 0xc6d222, 32, 0xa804208a, reinterpret_cast<void *>(&cGZClipboardManagerAdapters::Construct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
