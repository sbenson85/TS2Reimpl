// Hooks for cGZUIScriptService (src/gz/cGZUIScriptService.size.cpp), lane KS: the standalone shape. The creator 0x85b2df
// (the framework COM director's creator for class 0xfa3562fb) is the only way to the constructor 0x85b39a (a tail jump),
// and the vtables are stored only by that constructor and the destructor 0x85b434: with this hook every service carries
// our vtables, and every slot, the constructor and the destructors are in effect through callers (trapped by
// tools/callers.py --write-traps).
#include "gz/cGZUIScriptService.h"
#include "hook/hooks.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZUIScriptService_Create", 0x85b2df, 22, 0xa2fbacd2, reinterpret_cast<void *>(&cGZUIScriptService::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
