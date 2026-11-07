#include "hook/hooks.h"
#include "rz/PathAttributes.h"

namespace {

// Lane MA (F4d): the file attribute test, __cdecl.
const HookSpec kHooks[] = {
    {"PathAttributesMatch", 0x1c1931, 32, 0xfd237e7e, reinterpret_cast<void *>(&PathAttributesMatch)},
};
REGISTER_HOOKS(kHooks);

} // namespace
