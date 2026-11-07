#include "hook/hooks.h"
#include "simantics/PrimAA.h"

namespace {

// SimAntics primitive handlers (called from cTSObject::vf332's switch), lane AA.
const HookSpec kHooks[] = {
    {"Prim_6D_MaterialChange", 0x5827e0, 32, 0x85f06edc, MemberAddress(&PrimAAObject::Prim6D)},
    {"Prim_70_Effect", 0x5805c0, 32, 0xbd86dbc0, MemberAddress(&PrimAAObject::Prim70)},
};
REGISTER_HOOKS(kHooks);

} // namespace
