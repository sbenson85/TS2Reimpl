#include "hook/hooks.h"
#include "misc/Quaternion.h"

namespace {

const HookSpec kHooks[] = {
    {"Quaternion_PreMultiplyNormalised", 0x1a7020, 32, 0x09e07db1, MemberAddress(&math::Quaternion::PreMultiplyNormalised)},
    {"math_Quaternion_SetRotationBetween", 0x1a6db0, 32, 0x48c795c4, MemberAddress(&math::Quaternion::SetRotationBetween)},
};
REGISTER_HOOKS(kHooks);

} // namespace
