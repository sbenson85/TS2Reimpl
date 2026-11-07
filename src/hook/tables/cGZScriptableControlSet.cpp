// Hooks for cGZScriptableControlSet (src/gz/cGZScriptableControlSet.size.cpp), lane MC: the standalone shape. The
// UIScript director's creator 0x85b2c9 (class 0x5a354595) is the only way to the constructor 0x85bfe2 (a tail jump),
// and the vtables 0xe481a0 / 0xe481d8 (and the bases' 0xe48154 / 0xe48188) are stored only by that constructor and the
// destructor 0x85c00f: with this hook every set carries our vtables, and the slots are in effect through callers.
#include "gz/cGZScriptableControlSet.h"
#include "hook/hooks.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZScriptableControlSet_Create", 0x85b2c9, 22, 0x4fe93a4c, reinterpret_cast<void *>(&cGZScriptableControlSet::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
