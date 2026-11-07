#include "hook/hooks.h"
#include "misc/StartupObjects.h"

namespace {

const HookSpec kHooks[] = {
    {"ZeroedVector20_Construct", 0xc0a664, 21, 0xa572969a, MemberAddress(&startupobj::ZeroedVector20::Construct)},
    {"FixedPool_Construct", 0x945bd6, 32, 0x4b5a1392, MemberAddress(&startupobj::FixedPool::Construct)},
    {"ListAndLock_Construct", 0x1ce3f3, 32, 0x3f8e031e, MemberAddress(&startupobj::ListAndLock::Construct)},
    {"NamedWordHashSet_Construct", 0x97df6d, 32, 0xc11a300e, MemberAddress(&startupobj::NamedWordHashSet::Construct)},
    {"RecordPoolReset_Construct", 0x26bbb0, 32, 0x3c089c97, MemberAddress(&startupobj::RecordPoolReset::Construct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
