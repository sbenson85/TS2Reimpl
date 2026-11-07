#include "hook/hooks.h"
#include "simantics/PrimX.h"

namespace {

// SimAntics primitive handlers (called from cTSObject::vf332's switch), lane X.
const HookSpec kHooks[] = {
    {"Prim_73_Debug", 0x586560, 32, 0xe2ab48a6, MemberAddress(&PrimXObject::Prim73)},
    {"Prim_2E_Snap", 0x5a5210, 32, 0x5aa1e59b, MemberAddress(&PrimXObject::Prim2E)},
};
REGISTER_HOOKS(kHooks);

} // namespace
