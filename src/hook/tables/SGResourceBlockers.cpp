#include "hook/hooks.h"
#include "sg/SGResourceBlockers.h"

namespace {

const HookSpec kHooks[] = {
    {"SGResourceBlockers_InvalidDwordTag", 0x18c080, 7, 0xa66dfac6, MemberAddress(&SGResourceBlockers::cInvalidDwordTag::IsInvalid)},
};
REGISTER_HOOKS(kHooks);

} // namespace
