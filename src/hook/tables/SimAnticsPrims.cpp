#include "hook/hooks.h"
#include "simantics/PrimHandlers.h"

namespace {

// SimAntics primitive handlers (called from cTSObject::vf332's switch). The destination helpers they share
// (0x57c8d0, 0x57cc90, 0x57ed80) are hooked as SimDataRef (SimAnticsPrimExpression.cpp).
const HookSpec kHooks[] = {
    {"Prim_Random", 0x5a1060, 32, 0x11fe3081, MemberAddress(&PrimObject::Random)},
    {"Prim_07", 0x5a86b0, 32, 0x732a0b3d, MemberAddress(&PrimObject::Prim07)},
    {"Prim_0F", 0x5a7bb0, 32, 0xdec39571, MemberAddress(&PrimObject::Prim0F)},
};
REGISTER_HOOKS(kHooks);

} // namespace
