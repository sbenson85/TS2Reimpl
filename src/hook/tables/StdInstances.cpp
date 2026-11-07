#include "hook/hooks.h"
#include "std/StdInstances.h"

namespace {

const HookSpec kHooks[] = {
    {"FUN_a01c0366", 0x1c0366, 7, 0xa014771b, MemberAddress(&IntSetObject::Destroy)},
    {"FUN_a01773e0", 0x1773e0, 32, 0xbfb2057a, MemberAddress(&ServiceStringObject::ConstructFromText)},
    {"FUN_a057c3c0", 0x57c3c0, 14, 0x9c5fc679, MemberAddress(&StaticObjectStub::Construct)},
    {"FUN_a0af6560", 0xaf6560, 32, 0xd8d68f46, MemberAddress(&NameValueTable::Construct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
