#include "hook/hooks.h"
#include "gz/cHitChannel.h"

namespace {

const HookSpec kHooks[] = {
    {"cHitChannel_QueryInterface", 0xb32945, 32, 0x3ebfc2cf, MemberAddress(&cHitChannel::QueryInterface)},
    {"cHitChannel_Release", 0xb32a01, 26, 0x6b1d91ea, MemberAddress(&cHitChannel::Release)},
};
REGISTER_HOOKS(kHooks);

} // namespace
