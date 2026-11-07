#include "hook/hooks.h"
#include "sg/cAnimResourceConst.h"

namespace {

using Sub = nGZSceneDBAnim::cAnimResourceConstSubobject;
using Secondary = nGZSceneDBAnim::cAnimResourceConstSecondary;

const HookSpec kHooks[] = {
    {"nGZSceneDBAnim_cAnimResourceConst_vf44_58", 0x57e910, 6, 0x8afc837c, MemberAddress(&Sub::FlagBit0)},
    {"nGZSceneDBAnim_cAnimResourceConst_vf44_16", 0xa111b0, 7, 0x4372376f, MemberAddress(&Sub::RecordFieldC)},
    {"nGZSceneDBAnim_cAnimResourceConst_vf44_20", 0xa10da0, 22, 0x4a913f9c, MemberAddress(&Sub::EntryAt)},
    {"nGZSceneDBAnim_cAnimResourceConst_vf44_57", 0xa15e90, 8, 0x7f856add, MemberAddress(&Sub::HasFlagBits0To1)},
    {"nGZSceneDBAnim_cAnimResourceConst_vf44_53", 0xa15eb0, 9, 0xb5519189, MemberAddress(&Sub::RecordFlagBit0)},
    {"nGZSceneDBAnim_cAnimResourceConst_vf44_54", 0xa16380, 11, 0x7c429211, MemberAddress(&Sub::RecordFlagBit1)},
    {"nGZSceneDBAnim_cAnimResourceConst_vf44_38", 0xa16490, 15, 0x5efa3799, MemberAddress(&Sub::RecordField8Bits12To23)},
    {"nGZSceneDBAnim_cAnimResourceConst_vf44_51", 0xa16530, 8, 0x24be2564, MemberAddress(&Sub::RecordWord2)},
    {"nGZSceneDBAnim_cAnimResourceConst_vf44_48", 0xa16590, 13, 0x9740e8c2, MemberAddress(&Sub::RecordField8Bits8To11)},
    {"nGZSceneDBAnim_cAnimResourceConst_vf44_33", 0xa17770, 7, 0xe5e89c59, MemberAddress(&Sub::RecordDword14)},
    {"nGZSceneDBAnim_cAnimResourceConst_vf44_34", 0xa17780, 7, 0x495dd355, MemberAddress(&Sub::RecordDword18)},
    {"nGZSceneDBAnim_cAnimResourceConst_vf44_31", 0xa175c0, 32, 0x3728dc4b, MemberAddress(&Sub::SetFlagBits)},
    {"nGZSceneDBAnim_cAnimResourceConst_vf44_37", 0xa125f0, 7, 0x4c35d178, MemberAddress(&Sub::RecordFloat1C)},
    {"nGZSceneDBAnim_cAnimResourceConst_vf44_39", 0xa163a0, 7, 0x3fc6a887, MemberAddress(&Sub::RecordFloat20)},
    {"nGZSceneDBAnim_cAnimResourceConst_vf44_40", 0xa16390, 7, 0x5baa6d83, MemberAddress(&Sub::RecordFloat24)},
    {"nGZSceneDBAnim_cAnimResourceConst_vf44_41", 0xa16470, 7, 0xf71f228f, MemberAddress(&Sub::RecordFloat28)},
    {"nGZSceneDBAnim_cAnimResourceConst_vf44_42", 0xa16480, 7, 0x9373e78b, MemberAddress(&Sub::RecordFloat2C)},
    {"nGZSceneDBAnim_cAnimResourceConst_vf44_44", 0xa17870, 7, 0x7504bad6, MemberAddress(&Sub::RecordFloat30)},
    {"nGZSceneDBAnim_cAnimResourceConst_vf44_45", 0xa17790, 7, 0x11687fd2, MemberAddress(&Sub::RecordFloat34)},
    {"nGZSceneDBAnim_cAnimResourceConst_vf44_46", 0xa16580, 7, 0xbddd30de, MemberAddress(&Sub::RecordFloat38)},
    {"nGZSceneDBAnim_cAnimResourceConst_vf44_47", 0xa17100, 7, 0xd9b1f5da, MemberAddress(&Sub::RecordFloat3C)},
    // Thunks of the second subobject (vtable 0xe6657c): 11 bytes each, `sub ecx,[ecx-4]; sub ecx,0x28; jmp body`.
    {"nGZSceneDBAnim_cAnimResourceConst_vf88_6", 0xa170e0, 11, 0x0f1fd122, MemberAddress(&Secondary::VirtualBaseIfObject)},
    {"nGZSceneDBAnim_cAnimResourceConst_vf88_7", 0xa10e60, 11, 0x5490477a, MemberAddress(&Secondary::GetOwner)},
    {"nGZSceneDBAnim_cAnimResourceConst_vf88_9", 0xa170d5, 11, 0x98f38e2e, MemberAddress(&Secondary::GetVirtualBase)},
    {"nGZSceneDBAnim_cAnimResourceConst_vf88_11", 0xa15e76, 11, 0xe6bdc5b1, MemberAddress(&Secondary::CompareIdentity)},
    {"nGZSceneDBAnim_cAnimResourceConst_vf88_15", 0xa175ff, 11, 0x51030dfe, MemberAddress(&Secondary::SetResource)},
    {"nGZSceneDBAnim_cAnimResourceConst_vf88_16", 0xa1759c, 11, 0x0fabdf43, MemberAddress(&Secondary::SetFlag)},
    {"nGZSceneDBAnim_cAnimResourceConst_vf88_22", 0xa170eb, 11, 0x076596eb, MemberAddress(&Secondary::CopyKeyTo)},
    {"nGZSceneDBAnim_cAnimResourceConst_vf88_23", 0xa15e98, 11, 0x51c643a0, MemberAddress(&Secondary::GetFlag)},
    {"nGZSceneDBAnim_cAnimResourceConst_vf88_24", 0xa170af, 11, 0x7695de50, MemberAddress(&Secondary::ForwardKey)},
};
REGISTER_HOOKS(kHooks);

} // namespace
