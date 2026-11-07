#include "hook/hooks.h"
#include "simantics/PrimExpression.h"

namespace {

// Not hooked: 0x57e4c0 and 0x57e950 (4 bytes, a 5-byte jump would overwrite the next function) and 0x141120 (a
// 5-byte identity function called from everywhere; the compiler inlines ours).
const HookSpec kHooks[] = {
    {"SimAnticsPrimExpression_Execute", 0x5890c0, 32, 0x3aad0342, MemberAddress(&ExpressionPrimitive::Execute)},
    {"SimDataRef_Get", 0x512030, 32, 0xaa2504b8, MemberAddress(&SimDataRef::Get)},
    {"SimDataRef_Reset", 0x57c8d0, 17, 0xf5d10296, MemberAddress(&SimDataRef::Reset)},
    {"SimDataRef_HasData", 0x57cc90, 8, 0x30dba4b7, MemberAddress(&SimDataRef::HasData)},
    {"SimDataRef_Advance", 0x57cdc0, 32, 0x5a60c802, MemberAddress(&SimDataRef::Advance)},
    {"SimDataRef_HasNext", 0x57d6b0, 24, 0x2421eb6a, MemberAddress(&SimDataRef::HasNext)},
    {"SimDataRef_Set", 0x57ed80, 32, 0x87002e0d, MemberAddress(&SimDataRef::Set)},
};
REGISTER_HOOKS(kHooks);

} // namespace
