#include "hook/hooks.h"
#include "simantics/PrimT.h"

namespace {

// SimAntics primitive handlers (called from cTSObject::vf332's switch), lane T.
const HookSpec kHooks[] = {
    {"Prim_76_ArrayOperation", 0x57f2b0, 32, 0xcee320ff, MemberAddress(&PrimTObject::Prim76)},
    {"Prim_71_SnapInto", 0x5a6e40, 32, 0x4c196cdf, MemberAddress(&PrimTObject::Prim71)},
    {"Prim_24_Dialog", 0x5879a0, 32, 0xdba55268, MemberAddress(&PrimTObject::Prim24)},
    {"Prim_17_PlaySound", 0x59f370, 32, 0xfa3d6568, MemberAddress(&PrimTObject::Prim17)},
};
REGISTER_HOOKS(kHooks);

} // namespace
