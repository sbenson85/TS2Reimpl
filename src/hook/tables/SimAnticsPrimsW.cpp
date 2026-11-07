#include "hook/hooks.h"
#include "simantics/PrimW.h"

namespace {

// SimAntics primitive handlers (called from cTSObject::vf332's switch), lane W.
const HookSpec kHooks[] = {
    {"Prim_33_ManageInventory", 0x598ac0, 32, 0x2eeff8d7, MemberAddress(&PrimWObject::Prim33)},
};
REGISTER_HOOKS(kHooks);

} // namespace
