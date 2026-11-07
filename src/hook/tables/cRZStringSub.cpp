#include "hook/hooks.h"
#include "rz/cRZStringSub.h"

// cRZString's substring helpers (lane LZ, src/rz/cRZStringSub.cpp).
namespace {

const HookSpec kHooks[] = {
    {"cRZString_Mid", 0x1ad18d, 32, 0xb25bf187, MemberAddress(&cRZStringSub::Mid)},
    {"cRZString_Left", 0x1ad13b, 32, 0x3fd0915c, MemberAddress(&cRZStringSub::Left)},
    {"cRZString_Right", 0x1ad3ed, 32, 0x900613a8, MemberAddress(&cRZStringSub::Right)},
};
REGISTER_HOOKS(kHooks);

} // namespace
