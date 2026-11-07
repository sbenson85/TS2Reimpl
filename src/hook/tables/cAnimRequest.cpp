#include "hook/hooks.h"
#include "sg/cAnimRequest.h"

namespace {

// Thunks of the cAnimRequest secondary subobject (vtable 0xdb3dc4): each starts with sub ecx,[ecx-4] and jumps to a
// body that only reads or writes its adjusted pointer. Slots not listed here are shared or not done yet (see
// src/sg/cAnimRequest.h and db/status/cAnimRequest.jsonl).
using nTSSG::cAnimRequestSubobject;

const HookSpec kHooks[] = {
    {"nTSSG_cAnimRequest_vf248_2", 0x2b6c31, 21, 0x34648054, MemberAddress(&cAnimRequestSubobject::SetDwordEc)},
    {"nTSSG_cAnimRequest_vf248_5", 0x2b6e7d, 21, 0x67f8d310, MemberAddress(&cAnimRequestSubobject::SetDwordA8)},
    {"nTSSG_cAnimRequest_vf248_8", 0x2b6daa, 23, 0x00301f11, MemberAddress(&cAnimRequestSubobject::SetWord98)},
    {"nTSSG_cAnimRequest_vf248_10", 0x2b6bef, 23, 0x94656001, MemberAddress(&cAnimRequestSubobject::SetWord96)},
    {"nTSSG_cAnimRequest_vf248_12", 0x2b6c4d, 21, 0x7ee3e251, MemberAddress(&cAnimRequestSubobject::SetByte94)},
    {"nTSSG_cAnimRequest_vf248_16", 0x2b6e3e, 21, 0x959943f8, MemberAddress(&cAnimRequestSubobject::SetByte84)},
    {"nTSSG_cAnimRequest_vf248_17", 0x2b6e5d, 21, 0x7ee3e251, MemberAddress(&cAnimRequestSubobject::SetByte83)},
    {"nTSSG_cAnimRequest_vf248_18", 0x2b6a6d, 18, 0xeb88bb67, MemberAddress(&cAnimRequestSubobject::SetDword80)},
    {"nTSSG_cAnimRequest_vf248_22", 0x2b6d78, 18, 0xb7ca18d8, MemberAddress(&cAnimRequestSubobject::SetByte58)},
    {"nTSSG_cAnimRequest_vf248_24", 0x2b6c6d, 18, 0xeb88bb67, MemberAddress(&cAnimRequestSubobject::SetDword4C)},
    {"nTSSG_cAnimRequest_vf248_26", 0x2b6d0a, 25, 0x96241109, MemberAddress(&cAnimRequestSubobject::SetDwords24And20)},
    {"nTSSG_cAnimRequest_vf248_28", 0x2b6447, 15, 0xd612fbfb, MemberAddress(&cAnimRequestSubobject::GetDwordEc)},
    {"nTSSG_cAnimRequest_vf248_31", 0x2b6ed4, 15, 0x5c6dc25c, MemberAddress(&cAnimRequestSubobject::GetDwordA8)},
    {"nTSSG_cAnimRequest_vf248_47", 0x2b6283, 12, 0x3ac5e373, MemberAddress(&cAnimRequestSubobject::GetDword80)},
    {"nTSSG_cAnimRequest_vf248_57", 0x2b6d4f, 32, 0xe721829d, MemberAddress(&cAnimRequestSubobject::SetDwords18_14_10)},
};
REGISTER_HOOKS(kHooks);

} // namespace
