#include "hook/hooks.h"
#include "rz/cRZIniFile.h"

// cRZString::TrimBoth 0x1ad83c (lane LN): TrimLeading then TrimTrailing, both already ours (the IniFile group's
// cRZIniStringHelpers, src/rz/cRZIniFileRead.size.cpp).
namespace {

const HookSpec kHooks[] = {
    {"cRZString_TrimBoth", 0x1ad83c, 16, 0xf0a1e37f, MemberAddress(&cRZIniStringHelpers::TrimBoth)},
};
REGISTER_HOOKS(kHooks);

} // namespace
