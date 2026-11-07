#include "hook/hooks.h"
#include "simantics/PrimS.h"

namespace {

// SimAntics primitive handlers (called from cTSObject::vf332's switch), lane S, and the two small helpers they use.
const HookSpec kHooks[] = {
    {"Prim_7C_WantSatisfy", 0x5a8b30, 32, 0x1ca812a1, MemberAddress(&PrimSObject::Prim7C)},
    {"Prim_31_NotifyStackObject", 0x59eb90, 32, 0xc46e019a, MemberAddress(&PrimSObject::Prim31)},
    {"Prim_77_Message", 0x59de90, 32, 0x538a7d29, MemberAddress(&PrimSObject::Prim77)},
    {"Prim_7B_Cinematic", 0x585590, 32, 0xd72777db, MemberAddress(&PrimSObject::Prim7B)},
    {"Prim_2A_CreateObject", 0x585a60, 32, 0x2f93721b, MemberAddress(&PrimSObject::Prim2A)},
    {"Prim_22_UIEffect", 0x5a7c60, 32, 0xd1c39d66, MemberAddress(&PrimSObject::Prim22)},
    {"Prim_32_MakeActionString", 0x59d740, 32, 0xa3920b9b, MemberAddress(&PrimSObject::Prim32)},
    {"ResolveEntityId", 0x5a9f00, 32, 0xefde13e0, reinterpret_cast<void *>(&ResolveEntityId)},
    {"SString_AssignText", 0x26be10, 32, 0x59c3951c, MemberAddress(&SString::AssignText)},
};
REGISTER_HOOKS(kHooks);

} // namespace
