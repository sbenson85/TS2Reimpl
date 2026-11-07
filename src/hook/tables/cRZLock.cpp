// cRZLock (lane FD): src/rz/cRZLock.h.
#include "hook/hooks.h"
#include "rz/cRZLock.h"

namespace {

const HookSpec kHooks[] = {
    {"cRZLock_Construct", 0x1d8af0, 27, 0x0a0badfb, MemberAddress(&cRZLock::Construct)},
    {"cRZLock_vf0", 0x142248, 32, 0x60016cd3, MemberAddress(&cRZLock::DeletingDestructor)},
};

} // namespace

REGISTER_HOOKS(kHooks);
