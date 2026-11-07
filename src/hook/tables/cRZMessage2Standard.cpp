#include "hook/hooks.h"
#include "rz/cRZMessage2Standard.h"

namespace {

const HookSpec kHooks[] = {
    {"cRZMessage2Standard_QueryInterface", 0x1c0660, 32, 0x00a7a7a2, MemberAddress(&cRZMessage2Standard::QueryInterface)},
    {"cRZMessage2Standard_Release", 0x1c0804, 26, 0x76d5541f, MemberAddress(&cRZMessage2Standard::Release)},
    {"cRZMessage2Standard_SetId", 0x1c0915, 32, 0x408619fb, MemberAddress(&cRZMessage2Standard::SetId)},
    {"cRZMessage2Standard_IdEquals", 0x1c03c8, 22, 0x46aedf4f, MemberAddress(&cRZMessage2Standard::IdEquals)},
    {"cRZMessage2Standard_IdLess", 0x1c03de, 22, 0xc9ee2aef, MemberAddress(&cRZMessage2Standard::IdLess)},
    {"cRZMessage2Standard_HasValue0", 0x1c05a0, 8, 0x17fae022, MemberAddress(&cRZMessage2Standard::HasValue0)},
    {"cRZMessage2Standard_SetValue0", 0x1c0874, 14, 0xe116976d, MemberAddress(&cRZMessage2Standard::SetValue0)},
    {"cRZMessage2Standard_HasValue1", 0x1c05a8, 9, 0x44dd043d, MemberAddress(&cRZMessage2Standard::HasValue1)},
    {"cRZMessage2Standard_SetValue1", 0x1c0882, 14, 0x0da43500, MemberAddress(&cRZMessage2Standard::SetValue1)},
    {"cRZMessage2Standard_HasValue2", 0x1c05b1, 9, 0xfc616358, MemberAddress(&cRZMessage2Standard::HasValue2)},
    {"cRZMessage2Standard_SetValue2", 0x1c0890, 14, 0x80d2e0cc, MemberAddress(&cRZMessage2Standard::SetValue2)},
    {"cRZMessage2Standard_HasValue3", 0x1c05ba, 9, 0x61b65be1, MemberAddress(&cRZMessage2Standard::HasValue3)},
    {"cRZMessage2Standard_SetValue3", 0x1c089e, 14, 0x5b81a532, MemberAddress(&cRZMessage2Standard::SetValue3)},
    {"cRZMessage2Standard_HasPtr20", 0x1c05e0, 9, 0xcbbf936a, MemberAddress(&cRZMessage2Standard::HasPtr20)},
    {"cRZMessage2Standard_HasPtr24", 0x1c05cc, 7, 0xd9ee9219, MemberAddress(&cRZMessage2Standard::HasPtr24)},
    {"cRZMessage2Standard_Construct", 0x1c033d, 32, 0xab21270b, MemberAddress(&cRZMessage2Standard::Construct)},
    {"cRZMessage2Standard_Destruct", 0x1c0388, 32, 0xbc668bec, MemberAddress(&cRZMessage2Standard::Destruct)},
    {"cRZMessage2Standard_Create", 0x1c04ea, 32, 0x30fdc03c, MemberAddress(&cRZMessage2Standard::Create)},
    {"cRZMessage2Standard_GetString", 0x1c05f4, 26, 0xdbc89fcf, MemberAddress(&cRZMessage2Standard::GetString)},
    {"cRZMessage2Standard_SetString", 0x1c08f2, 32, 0xac1c627c, MemberAddress(&cRZMessage2Standard::SetString)},
    {"cRZMessage2Standard_SetObject", 0x1c08cf, 32, 0x632c5d4d, MemberAddress(&cRZMessage2Standard::SetObject)},
    {"cRZMessage2Standard_HasExtra", 0x1c05c3, 9, 0xd90a3c84, MemberAddress(&cRZMessage2Standard::HasExtra)},
    {"cRZMessage2Standard_GetExtra", 0x1c0582, 29, 0x6c4f7102, MemberAddress(&cRZMessage2Standard::GetExtra)},
    {"cRZMessage2Standard_SetExtra", 0x1c08ac, 32, 0xc825c883, MemberAddress(&cRZMessage2Standard::SetExtra)},
    {"cRZMessage2Standard_DeletingDestructor", 0x1c042a, 32, 0x02b5f86e, MemberAddress(&cRZMessage2Standard::DeletingDestructor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
