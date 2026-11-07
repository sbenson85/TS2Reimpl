#include "hook/hooks.h"
#include "rz/RZFrameworkHelpers.h"

namespace {

// Overloads and varargs need explicit types; the 4-, 5- and 6-byte helpers are not hooked.
const HookSpec kHooks[] = {
    {"FUN_a0142080", 0x142080, 32, 0xc8a3da4a, MemberAddress(&RZUnknownSlot::Assign)},
    {"FUN_a01d099e", 0x1d099e, 32, 0x2a68e958, reinterpret_cast<void *>(&RZFormattedErrorCtor)},
    {"FUN_a01be23b", 0x1be23b, 32, 0xb558b36b, MemberAddress(&RZListNodeVisitor::VisitNodesThenHead)},
    {"FUN_a01b9052", 0x1b9052, 13, 0xdcc0a811, MemberAddress(&RZUnknownLayout::Construct)},
    {"FUN_a0b6d930", 0xb6d930, 7, 0x8a2e0cf5, MemberAddress(&RZWinProc4Vtable::SetVTable)},
    {"FUN_a0b6d920", 0xb6d920, 16, 0x645c43eb, MemberAddress(&RZWinProc4Vtable::Construct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
