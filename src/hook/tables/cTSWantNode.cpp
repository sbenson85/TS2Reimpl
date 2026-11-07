#include "hook/hooks.h"
#include "ts/cTSWantNode.h"

namespace {

// The constructor builds ours (placement new), so every node carries our vtable. The slots have no entry: only the game
// vtable 0xdd86d8 refers to the class's own bodies, stored only by the constructor and Release 0x5b36a0 ("in_effect":
// "callers"; the shared getter bodies other classes also use are not ours to hook). The integer test is a free function
// with callers outside the class.
const HookSpec kHooks[] = {
    {"cTSWantNode_Construct", 0x5b2750, 32, 0x8549ea61, reinterpret_cast<void *>(&cTSWantNodeAdapters::Construct)},
    {"WantCompareIntegers", 0x5507b0, 32, 0xd2400908, reinterpret_cast<void *>(&WantCompareIntegers)},
};
REGISTER_HOOKS(kHooks);

} // namespace
