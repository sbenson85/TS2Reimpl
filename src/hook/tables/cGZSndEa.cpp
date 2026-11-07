#include "hook/hooks.h"
#include "gz/cGZSndEa.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZSndEa_QueryInterface", 0xb146c1, 32, 0x319bc487, MemberAddress(&cGZSndEa::QueryInterface)},
    {"cGZSndEa_AddRef", 0xb14128, 17, 0xa6a0a525, MemberAddress(&cGZSndEa::AddRef)},
    {"cGZSndEa_Release", 0xb14787, 32, 0xc5becfc7, MemberAddress(&cGZSndEa::Release)},
};
REGISTER_HOOKS(kHooks);

} // namespace
