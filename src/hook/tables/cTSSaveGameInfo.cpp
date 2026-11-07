#include "hook/hooks.h"
#include "ts/cTSSaveGameInfo.h"

namespace {

// cTSSaveGameInfo (lane DV; Describe 0xcd8140, slot 9: lane DY).
const HookSpec kHooks[] = {
    {"cTSSaveGameInfo_Construct", 0xcd7a10, 28, 0x01ad9e73, MemberAddress(&cTSSaveGameInfo::Construct)},
    {"cTSSaveGameInfo_Create", 0xcd7aa0, 32, 0x693981ca, reinterpret_cast<void *>(&cTSSaveGameInfo::Create)},
    {"cTSSaveGameInfo_Destruct", 0xcd7a30, 32, 0xbfc05cf5, MemberAddress(&cTSSaveGameInfo::Destruct)},
    {"cTSSaveGameInfo_DeletingDestructor", 0xcd7a80, 32, 0x6cde1029, MemberAddress(&cTSSaveGameInfo::DeletingDestructor)},
    {"cTSSaveGameInfo_QueryInterface", 0xcd8720, 32, 0x78c99738, MemberAddress(&cTSSaveGameInfo::QueryInterface)},
    {"cTSSaveGameInfo_Release", 0xcd8750, 23, 0xe7df182b, MemberAddress(&cTSSaveGameInfo::Release)},
    {"cTSSaveGameInfo_Fill", 0xcd7fa0, 32, 0x0270fc9f, MemberAddress(&cTSSaveGameInfo::Fill)},
    {"cTSSaveGameInfo_SetUint32", 0xcd8a50, 32, 0x3be982d2, MemberAddress(&cTSSaveGameInfo::SetUint32)},
    {"cTSSaveGameInfo_SetString", 0xcd88c0, 32, 0x118fc2d9, MemberAddress(&cTSSaveGameInfo::SetString)},
    {"cTSSaveGameInfo_GetUint32", 0xcd7b10, 32, 0xa0c8e569, MemberAddress(&cTSSaveGameInfo::GetUint32)},
    {"cTSSaveGameInfo_GetString", 0xcd7c20, 32, 0x08eb93cd, MemberAddress(&cTSSaveGameInfo::GetString)},
    {"cTSSaveGameInfo_GetIndices", 0xcd7d50, 32, 0x67684b1e, MemberAddress(&cTSSaveGameInfo::GetIndices)},
    {"cTSSaveGameInfo_Save", 0xcd8770, 32, 0x3b3f16f2, MemberAddress(&cTSSaveGameInfo::Save)},
    {"cTSSaveGameInfo_Load", 0xcd7ed0, 32, 0x98883f85, MemberAddress(&cTSSaveGameInfo::Load)},
    {"cTSSaveGameInfo_EnsureProperties", 0xcd7e30, 32, 0xff18654b, MemberAddress(&cTSSaveGameInfo::EnsureProperties)},
    {"cTSSaveGameInfo_Describe", 0xcd8140, 32, 0x9e7a2ce0, MemberAddress(&cTSSaveGameInfo::Describe)},
};
REGISTER_HOOKS(kHooks);

} // namespace
