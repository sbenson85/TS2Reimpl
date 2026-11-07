#include "hook/hooks.h"
#include "ts/cTSFamilyRelationship.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSFamilyRelationship_GetGZCLSID", 0x4f2780, 6, 0x4d2c831c, MemberAddress(&cTSFamilyRelationshipSerialFace::GetGZCLSID)},
    {"cTSFamilyRelationship_Construct", 0x4f1fd0, 32, 0xe3e425fd, MemberAddress(&cTSFamilyRelationship::Construct)},
    {"cTSFamilyRelationship_Create", 0x4f2680, 32, 0xa9c165e6, reinterpret_cast<void *>(&cTSFamilyRelationship::Create)},
    {"cTSFamilyRelationship_Release", 0x4f3720, 32, 0x5f175440, MemberAddress(&cTSFamilyRelationship::Release)},
    {"cTSFamilyRelationship_QueryInterface", 0x4f2e90, 32, 0x24f863ab, MemberAddress(&cTSFamilyRelationship::QueryInterface)},
    {"cTSFamilyRelationship_vf3", 0x4f2d50, 32, 0xb90aa6bf, MemberAddress(&cTSFamilyRelationship::GetTie)},
    {"cTSFamilyRelationship_vf9", 0x4f3840, 32, 0xf66a8593, MemberAddress(&cTSFamilyRelationship::RemoveTie)},
    {"cTSFamilyRelationship_vf4", 0x4f2d00, 32, 0xc6ef5e86, MemberAddress(&cTSFamilyRelationship::GetTieType)},
    {"cTSFamilyRelationship_vf8", 0x4f37e0, 32, 0x74aa7b5e, MemberAddress(&cTSFamilyRelationship::RemoveSim)},
    {"cTSFamilyRelationship_vf5", 0x4f2c90, 32, 0xe53b2813, MemberAddress(&cTSFamilyRelationship::GetChildren)},
    {"cTSFamilyRelationship_vf6", 0x4f2e10, 32, 0xe53b2813, MemberAddress(&cTSFamilyRelationship::GetSiblings)},
    {"cTSFamilyRelationship_vf7", 0x4f3cd0, 32, 0xa7a2e527, MemberAddress(&cTSFamilyRelationship::SetTie)},
    {"cTSFamilyRelationship_Write", 0x4f3dc0, 32, 0x901cc507, MemberAddress(&cTSFamilyRelationshipSerialFace::Write)},
    {"cTSFamilyRelationship_Read", 0x4f2f80, 32, 0x1ca32328, MemberAddress(&cTSFamilyRelationshipSerialFace::Read)},
    {"cTSFamilyRelationship_vf11", 0x4f3a90, 32, 0x21417773, MemberAddress(&cTSFamilyRelationship::ReadFrom)},
    {"cTSFamilyRelationship_vf12", 0x4f3b90, 32, 0x21417773, MemberAddress(&cTSFamilyRelationship::WriteTo)},
};
REGISTER_HOOKS(kHooks);

} // namespace
