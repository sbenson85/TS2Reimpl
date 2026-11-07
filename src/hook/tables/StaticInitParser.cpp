#include "hook/hooks.h"
#include "app/StaticInitParser.h"

namespace {

const HookSpec kHooks[] = {
    {"StaticFlag_Construct", 0x90a915, 6, 0xce4393bb, MemberAddress(&staticinit::StaticFlag::Construct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
