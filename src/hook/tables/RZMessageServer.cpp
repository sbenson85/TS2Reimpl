#include "hook/hooks.h"
#include "rz/RZMessageServer.h"

namespace {

const HookSpec kHooks[] = {
    {"GetCachedMessageServer", 0x9c5a89, 32, 0x50259b41, reinterpret_cast<void *>(&GetCachedMessageServer)},
};
REGISTER_HOOKS(kHooks);

} // namespace
