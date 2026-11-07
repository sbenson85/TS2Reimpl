#include "hook/hooks.h"
#include "rz/MiscHelpersB.h"

namespace {

const HookSpec kHooks[] = {
    {"ServiceHolder_QueryFromService", 0x1c02bf, 32, 0xd681787b, MemberAddress(&ServiceHolder::QueryFromService)},
    {"KeyMapWalker_CallSlot6ForEachKey", 0x1c081e, 32, 0xdcf8b5e8, MemberAddress(&KeyMapWalker::CallSlot6ForEachKey)},
    {"GetCachedServiceObjectB", 0x9c5baa, 32, 0x140ec6a7, reinterpret_cast<void *>(&GetCachedServiceObjectB)},
};
REGISTER_HOOKS(kHooks);

} // namespace
