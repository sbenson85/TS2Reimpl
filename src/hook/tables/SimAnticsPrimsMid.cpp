#include "hook/hooks.h"
#include "simantics/PrimMid.h"

namespace {

// SimAntics primitive handlers (called from cTSObject::vf332's switch) and the small helpers of DirectionTo and
// the ranking primitive. Not hooked: the 4..17-byte param accessors (0x57e570 and its neighbours, inlined in the
// handlers), and 0x57cc80 (reads the holder's pointer).
const HookSpec kHooks[] = {
    {"Prim_Idle", 0x5988a0, 32, 0x8f2be00d, MemberAddress(&PrimMidObject::Idle)},
    {"Prim_DistanceTo", 0x588eb0, 32, 0x167a2ed6, MemberAddress(&PrimMidObject::DistanceTo)},
    {"Prim_DirectionTo", 0x588bb0, 32, 0xfa065775, MemberAddress(&PrimMidObject::DirectionTo)},
    {"Prim_14", 0x580360, 32, 0x6aeecbd2, MemberAddress(&PrimMidObject::Prim14)},
    {"Prim_Budget", 0x5801b0, 32, 0x7a4b63a2, MemberAddress(&PrimMidObject::Budget)},
    {"Prim_20", 0x5a79f0, 32, 0xd8a929d5, MemberAddress(&PrimMidObject::Prim20)},
    {"Prim_21", 0x58a110, 32, 0x90cac3d1, MemberAddress(&PrimMidObject::Prim21)},
    {"Prim_23", 0x5a87f0, 32, 0x05d0ef9d, MemberAddress(&PrimMidObject::Prim23)},
    {"Prim_30", 0x59d590, 32, 0x697f3b4c, MemberAddress(&PrimMidObject::Prim30)},
    {"Prim_OnTimer", 0x59efa0, 32, 0x5c8d0c33, MemberAddress(&PrimMidObject::OnTimer)},
    {"Prim_7E", 0x5a1260, 32, 0xc263d9bf, MemberAddress(&PrimMidObject::Prim7E)},
    {"SimFixedPoint_Octant", 0x460400, 32, 0x14ce5ed7, MemberAddress(&SimFixedPoint::Octant)},
    {"SimFixed_ToFloat", 0x57f290, 26, 0x867af641, MemberAddress(&SimFixed::ToFloat)},
    {"Atan2Float", 0x275390, 32, 0x0aad5029, reinterpret_cast<void *>(&Atan2Float)},
    {"RankCompare", 0x5a9330, 32, 0x318a0b18, reinterpret_cast<void *>(&RankCompare)},
};
REGISTER_HOOKS(kHooks);

} // namespace
