#include "hook/hooks.h"
#include "ts/cTSPersistDBRecord.h"

namespace {

using ITS = cTSPersistDBRecordITS;
using IGZ = cTSPersistDBRecordIGZ;

const HookSpec kHooks[] = {
    {"cTSPersistDBRecord_vf30", 0x1f0e50, 12, 0x9fe5573a, MemberAddress(&cTSPersistDBRecord::vf30)},
    {"cTSPersistDBRecord_vf56_5", 0x1f08a0, 12, 0x0b1580b5, MemberAddress(&ITS::vf56_5)},
    {"cTSPersistDBRecord_vf56_6", 0x1f0890, 12, 0xdf6ab09b, MemberAddress(&ITS::vf56_6)},
    {"cTSPersistDBRecord_vf56_15", 0x1f0c20, 17, 0xbd49527d, MemberAddress(&ITS::vf56_15)},
    {"cTSPersistDBRecord_vf56_16", 0x1f0ed0, 19, 0xb3aef341, MemberAddress(&ITS::vf56_16)},
    {"cTSPersistDBRecord_vf56_17", 0x1f0ab0, 17, 0xe97880d4, MemberAddress(&ITS::vf56_17)},
    {"cTSPersistDBRecord_vf60_13", 0x1f0df0, 18, 0x7da7552b, MemberAddress(&IGZ::vf60_13)},
    {"cTSPersistDBRecord_vf60_14", 0x1f0e10, 32, 0xe86ac27a, MemberAddress(&IGZ::vf60_14)},
    {"cTSPersistDBRecord_vf60_15", 0x1f0a70, 32, 0x09615591, MemberAddress(&IGZ::vf60_15)},
    {"cTSPersistDBRecord_vf60_17", 0x1f0bf0, 17, 0x8bfeb181, MemberAddress(&IGZ::vf60_17)},
    {"cTSPersistDBRecord_vf60_18", 0x1f0fa0, 32, 0xc9244fe1, MemberAddress(&IGZ::vf60_18)},
    // Second batch. The shared_vf_* members (primary slots 29 and 31, cITSPersistDBRecord slots 25 and
    // 26, cIGZPersistDBRecord slot 16) are folded with unrelated classes and cannot be hooked.
    {"cTSPersistDBRecord_vf16_18", 0x1f0a33, 8, 0xad6a6c96, MemberAddress(&cTSPersistDBRecordIS::vf16_18)},
    {"cTSPersistDBRecord_vf32_13", 0x1f0a3b, 8, 0x9c8fbb75, MemberAddress(&cTSPersistDBRecordSS::vf32_13)},
    {"cTSPersistDBRecord_vf32_7", 0x1f0860, 12, 0xf4db7668, MemberAddress(&cTSPersistDBRecordSS::vf32_7)},
    {"cTSPersistDBRecord_vf32_10", 0x1f0a10, 27, 0xc4bb383f, MemberAddress(&cTSPersistDBRecordSS::vf32_10)},
    {"cTSPersistDBRecord_vf32_11", 0x1f09e0, 27, 0x5afe1c19, MemberAddress(&cTSPersistDBRecordSS::vf32_11)},
    {"cTSPersistDBRecord_ITS_AddRef", 0x1f0805, 8, 0x2737563e, MemberAddress(&cTSPersistDBRecordITS2::AddRef)},
    {"cTSPersistDBRecord_IGZ_AddRef", 0x1f080d, 8, 0x8939c466, MemberAddress(&cTSPersistDBRecordIGZ2::AddRef)},
    {"cTSPersistDBRecord_vf56_3", 0x1f0884, 8, 0xed553c2e, MemberAddress(&cTSPersistDBRecordITS2::vf56_3)},
    {"cTSPersistDBRecord_vf56_4", 0x1f086c, 8, 0x78f8435e, MemberAddress(&cTSPersistDBRecordITS2::vf56_4)},
    {"cTSPersistDBRecord_vf56_9", 0x1f0a2b, 8, 0x306fdc9a, MemberAddress(&cTSPersistDBRecordITS2::vf56_9)},
    {"cTSPersistDBRecord_vf56_10", 0x1f09fb, 8, 0x306fdc9a, MemberAddress(&cTSPersistDBRecordITS2::vf56_10)},
    {"cTSPersistDBRecord_vf56_11", 0x1f0a60, 8, 0xcf85aa8e, MemberAddress(&cTSPersistDBRecordITS2::vf56_11)},
    {"cTSPersistDBRecord_vf56_20", 0x1f0cd0, 32, 0x086cefc1, MemberAddress(&cTSPersistDBRecordITS2::vf56_20)},
    {"cTSPersistDBRecord_vf56_22", 0x1f0ef0, 32, 0x8a31207c, MemberAddress(&cTSPersistDBRecordITS2::vf56_22)},
    {"cTSPersistDBRecord_vf56_23", 0x1f0ad0, 32, 0x61a24204, MemberAddress(&cTSPersistDBRecordITS2::vf56_23)},
    {"cTSPersistDBRecord_GetKey", 0x1f0a68, 8, 0xc424acb5, MemberAddress(&cTSPersistDBRecordIGZ2::GetKey)},
    {"cTSPersistDBRecord_SetKey", 0x1f0e7e, 8, 0x2987de62, MemberAddress(&cTSPersistDBRecordIGZ2::SetKey)},
    {"cTSPersistDBRecord_Close", 0x1f07e0, 11, 0x9d196629, MemberAddress(&cTSPersistDBRecordIGZ2::Close)},
    {"cTSPersistDBRecord_vf60_10", 0x1f0be0, 9, 0xec208890, MemberAddress(&cTSPersistDBRecordIGZ2::vf60_10)},
    {"cTSPersistDBRecord_vf60_12", 0x1f0aa0, 9, 0x6ed5cc23, MemberAddress(&cTSPersistDBRecordIGZ2::vf60_12)},
    // Third batch: constructor, factory, destructor and the thunks into cRZSerializableStream. The
    // plain `jmp` thunks (GetVoid 0x1f0c10, SetByteOrder 0x1f0c40, Read 0x1f0cc0, Release 0x1f0dc0,
    // SetVoid 0x1f0fd0, Reset 0x1f1010, Write 0x1f1030) and the shared_vf_* ones (0x1f07f0, 0x1f07f5,
    // 0x1f07fd, 0x1f0880) cannot be hooked.
    {"cTSPersistDBRecord_Construct", 0x1f0690, 32, 0x0ad676ba, MemberAddress(&cTSPersistDBRecordObject::Construct)},
    {"cTSPersistDBRecord_Create", 0x1f0900, 32, 0x52a691fb, reinterpret_cast<void *>(&cTSPersistDBRecordObject::Create)},
    {"cTSPersistDBRecord_vf21", 0x1f0740, 32, 0x026a5022, MemberAddress(&cTSPersistDBRecordObject::DeletingDestructor)},
    {"cTSPersistDBRecord_vf16_21", 0x1f0733, 8, 0x3f62e993, MemberAddress(&cTSPersistDBRecordIS2::vf16_21)},
    {"cTSPersistDBRecord_vf56_21", 0x1f0820, 32, 0x53a484b7, MemberAddress(&cTSPersistDBRecordITS3::vf56_21)},
    {"cTSPersistDBRecord_vf56_19", 0x1f08b0, 32, 0x4fd49bfd, MemberAddress(&cTSPersistDBRecordITS3::vf56_19)},
    {"cTSPersistDBRecord_vf60_8", 0x1f0a50, 8, 0x7dbe05d8, MemberAddress(&cTSPersistDBRecordIGZ3::vf60_8)},
    {"cTSPersistDBRecord_vf16_22", 0x1f0c45, 8, 0x9cd186fa, MemberAddress(&cTSPersistDBRecordIS2::vf16_22)},
    {"cTSPersistDBRecord_vf32_8", 0x1f0c4d, 8, 0x0d06fe27, MemberAddress(&cTSPersistDBRecordSS2::vf32_8)},
    {"cTSPersistDBRecord_vf56_7", 0x1f0c55, 8, 0x2737563e, MemberAddress(&cTSPersistDBRecordITS3::vf56_7)},
    {"cTSPersistDBRecord_QueryInterface", 0x1f0c60, 32, 0x2081a369, MemberAddress(&cTSPersistDBRecordObject::QueryInterface)},
    {"cTSPersistDBRecord_QueryInterface_IS", 0x1f0c9d, 8, 0xc271f628, MemberAddress(&cTSPersistDBRecordIS2::QueryInterface)},
    {"cTSPersistDBRecord_QueryInterface_SS", 0x1f0ca5, 8, 0x03bfd96a, MemberAddress(&cTSPersistDBRecordSS2::QueryInterface)},
    {"cTSPersistDBRecord_QueryInterface_ITS", 0x1f0cad, 8, 0x799726ec, MemberAddress(&cTSPersistDBRecordITS3::QueryInterface)},
    {"cTSPersistDBRecord_QueryInterface_IGZ", 0x1f0cb5, 8, 0x27b24c15, MemberAddress(&cTSPersistDBRecordIGZ3::QueryInterface)},
    {"cTSPersistDBRecord_Release_IS", 0x1f0dc5, 8, 0x9cd186fa, MemberAddress(&cTSPersistDBRecordIS2::Release)},
    {"cTSPersistDBRecord_Release_SS", 0x1f0dcd, 8, 0x0d06fe27, MemberAddress(&cTSPersistDBRecordSS2::Release)},
    {"cTSPersistDBRecord_Release_ITS", 0x1f0dd5, 8, 0x2737563e, MemberAddress(&cTSPersistDBRecordITS3::Release)},
    {"cTSPersistDBRecord_Release_IGZ", 0x1f0ddd, 8, 0x8939c466, MemberAddress(&cTSPersistDBRecordIGZ3::Release)},
    {"cTSPersistDBRecord_vf60_9", 0x1f0e40, 8, 0x583b20ff, MemberAddress(&cTSPersistDBRecordIGZ3::vf60_9)},
    {"cTSPersistDBRecord_vf56_18", 0x1f0fd5, 8, 0x772e01a1, MemberAddress(&cTSPersistDBRecordITS3::vf56_18)},
    {"cTSPersistDBRecord_vf56_14", 0x1f0fe0, 32, 0x63a7616f, MemberAddress(&cTSPersistDBRecordITS3::vf56_14)},
    {"cTSPersistDBRecord_vf16_23", 0x1f1015, 8, 0x9cd186fa, MemberAddress(&cTSPersistDBRecordIS2::vf16_23)},
    {"cTSPersistDBRecord_vf32_9", 0x1f101d, 8, 0x0d06fe27, MemberAddress(&cTSPersistDBRecordSS2::vf32_9)},
    {"cTSPersistDBRecord_vf56_8", 0x1f1025, 8, 0x2737563e, MemberAddress(&cTSPersistDBRecordITS3::vf56_8)},
};
REGISTER_HOOKS(kHooks);

} // namespace
