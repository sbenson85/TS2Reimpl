#include "hook/hooks.h"
#include "gz/cGZPersistDBRecord.h"
#include "gz/cGZPersistDBSerialRecord.h"

namespace {

// Overloaded members need an explicit type to take their address.
using OneMember = bool (cGZPersistDBSerialRecord::*)(void *);
using CountedMember = bool (cGZPersistDBSerialRecord::*)(void *, uint32_t);
using SetCountedMember = bool (cGZPersistDBSerialRecord::*)(const void *, uint32_t);
using SetU8Member = bool (cGZPersistDBSerialRecord::*)(uint8_t);
using SetU16Member = bool (cGZPersistDBSerialRecord::*)(uint16_t);
using SetS16Member = bool (cGZPersistDBSerialRecord::*)(int16_t);
using SetU32Member = bool (cGZPersistDBSerialRecord::*)(uint32_t);
using SetU64Member = bool (cGZPersistDBSerialRecord::*)(uint64_t);
using SetF32Member = bool (cGZPersistDBSerialRecord::*)(float);
using SetF64Member = bool (cGZPersistDBSerialRecord::*)(double);

const HookSpec kHooks[] = {
    {"cGZPersistDBSerialRecord_QueryInterface", 0x99cfa0, 32, 0xb0bb8fce, MemberAddress(&cGZPersistDBSerialRecord::QueryInterface)},
    {"cGZPersistDBSerialRecord_AddRef", 0x97b790, 9, 0x580d09c1, MemberAddress(&cGZPersistDBSerialRecord::AddRef)},
    {"cGZPersistDBSerialRecord_Release", 0x99d0a0, 31, 0x219be540, MemberAddress(&cGZPersistDBSerialRecord::Release)},
    {"cGZPersistDBSerialRecord_GetFieldUint8_1", 0x99beb0, 7, 0xf3e04623,
     MemberAddress(static_cast<CountedMember>(&cGZPersistDBSerialRecord::GetFieldUint8))},
    {"cGZPersistDBSerialRecord_GetFieldUint8_2", 0x99bea0, 16, 0x7a8d518e,
     MemberAddress(static_cast<OneMember>(&cGZPersistDBSerialRecord::GetFieldUint8))},
    {"cGZPersistDBSerialRecord_GetFieldUint16_1", 0x99bcc0, 32, 0x3bee5dc3,
     MemberAddress(static_cast<CountedMember>(&cGZPersistDBSerialRecord::GetFieldUint16))},
    {"cGZPersistDBSerialRecord_GetFieldUint16_2", 0x99bc80, 32, 0x00a29a77,
     MemberAddress(static_cast<OneMember>(&cGZPersistDBSerialRecord::GetFieldUint16))},
    {"cGZPersistDBSerialRecord_GetFieldUint32_1", 0x99bd60, 32, 0x6339c029,
     MemberAddress(static_cast<CountedMember>(&cGZPersistDBSerialRecord::GetFieldUint32))},
    {"cGZPersistDBSerialRecord_GetFieldUint32_2", 0x99bd20, 32, 0xeeee68cf,
     MemberAddress(static_cast<OneMember>(&cGZPersistDBSerialRecord::GetFieldUint32))},
    {"cGZPersistDBSerialRecord_GetFieldUint64_1", 0x99be20, 32, 0x4602dfc2,
     MemberAddress(static_cast<CountedMember>(&cGZPersistDBSerialRecord::GetFieldUint64))},
    {"cGZPersistDBSerialRecord_GetFieldUint64_2", 0x99bdc0, 32, 0x38b2b215,
     MemberAddress(static_cast<OneMember>(&cGZPersistDBSerialRecord::GetFieldUint64))},
    {"cGZPersistDBSerialRecord_GetFieldFloat32_1", 0x99bbc0, 18, 0x6e3d76cb,
     MemberAddress(static_cast<CountedMember>(&cGZPersistDBSerialRecord::GetFieldFloat32))},
    {"cGZPersistDBSerialRecord_GetFieldFloat32_2", 0x99bbb0, 16, 0x67a2e29d,
     MemberAddress(static_cast<OneMember>(&cGZPersistDBSerialRecord::GetFieldFloat32))},
    {"cGZPersistDBSerialRecord_GetFieldFloat64_1", 0x99bbf0, 18, 0x81ff1df5,
     MemberAddress(static_cast<CountedMember>(&cGZPersistDBSerialRecord::GetFieldFloat64))},
    {"cGZPersistDBSerialRecord_GetFieldFloat64_2", 0x99bbe0, 16, 0xfd185b66,
     MemberAddress(static_cast<OneMember>(&cGZPersistDBSerialRecord::GetFieldFloat64))},
    {"cGZPersistDBSerialRecord_GetFieldResKey", 0x99bc10, 32, 0x8c100707, MemberAddress(&cGZPersistDBSerialRecord::GetFieldResKey)},
    {"cGZPersistDBSerialRecord_SetFieldResKey", 0x99d140, 32, 0xbc863527, MemberAddress(&cGZPersistDBSerialRecord::SetFieldResKey)},
    {"cGZPersistDBSerialRecord_SetFieldUint8_2", 0x99d540, 20, 0x09649a29,
     MemberAddress(static_cast<SetU8Member>(&cGZPersistDBSerialRecord::SetFieldUint8))},
    {"cGZPersistDBSerialRecord_SetFieldUint8_1", 0x99d560, 10, 0x39573c78,
     MemberAddress(static_cast<SetCountedMember>(&cGZPersistDBSerialRecord::SetFieldUint8))},
    {"cGZPersistDBSerialRecord_SetFieldUint16_2", 0x99d5c0, 32, 0x1e4449fc,
     MemberAddress(static_cast<SetU16Member>(&cGZPersistDBSerialRecord::SetFieldUint16))},
    {"cGZPersistDBSerialRecord_SetFieldUint16_1", 0x99d1d0, 32, 0x00570d6a,
     MemberAddress(static_cast<SetCountedMember>(&cGZPersistDBSerialRecord::SetFieldUint16))},
    {"cGZPersistDBSerialRecord_SetFieldSint16_2", 0x99d190, 32, 0x1e4449fc,
     MemberAddress(static_cast<SetS16Member>(&cGZPersistDBSerialRecord::SetFieldSint16))},
    {"cGZPersistDBSerialRecord_SetFieldUint32_2", 0x99d2b0, 32, 0xb74a5c43,
     MemberAddress(static_cast<SetU32Member>(&cGZPersistDBSerialRecord::SetFieldUint32))},
    {"cGZPersistDBSerialRecord_SetFieldUint32_1", 0x99d2f0, 32, 0xfe237234,
     MemberAddress(static_cast<SetCountedMember>(&cGZPersistDBSerialRecord::SetFieldUint32))},
    {"cGZPersistDBSerialRecord_SetFieldUint64_2", 0x99d600, 32, 0x69be8334,
     MemberAddress(static_cast<SetU64Member>(&cGZPersistDBSerialRecord::SetFieldUint64))},
    {"cGZPersistDBSerialRecord_SetFieldUint64_1", 0x99d3d0, 32, 0x7b3d5801,
     MemberAddress(static_cast<SetCountedMember>(&cGZPersistDBSerialRecord::SetFieldUint64))},
    {"cGZPersistDBSerialRecord_SetFieldFloat32_2", 0x99d0c0, 20, 0x144b293a,
     MemberAddress(static_cast<SetF32Member>(&cGZPersistDBSerialRecord::SetFieldFloat32))},
    {"cGZPersistDBSerialRecord_SetFieldFloat32_1", 0x99d0e0, 21, 0xb9822567,
     MemberAddress(static_cast<SetCountedMember>(&cGZPersistDBSerialRecord::SetFieldFloat32))},
    {"cGZPersistDBSerialRecord_SetFieldFloat64_2", 0x99d100, 20, 0x2244dfcd,
     MemberAddress(static_cast<SetF64Member>(&cGZPersistDBSerialRecord::SetFieldFloat64))},
    {"cGZPersistDBSerialRecord_SetFieldFloat64_1", 0x99d120, 21, 0x6414fce2,
     MemberAddress(static_cast<SetCountedMember>(&cGZPersistDBSerialRecord::SetFieldFloat64))},
    {"cGZPersistDBSerialRecord_SetFieldSint64", 0x99d4d0, 32, 0x7fbf3b53, MemberAddress(&cGZPersistDBSerialRecord::SetFieldSint64)},
    {"cGZPersistDBSerialRecord_SetFieldString", 0x99d570, 32, 0x4642bbc0, MemberAddress(&cGZPersistDBSerialRecord::SetFieldString)},
    {"cGZPersistDBSerialRecord_GetFieldBlob", 0x99cd60, 24, 0xcf9d9fb8, MemberAddress(&cGZPersistDBSerialRecord::GetFieldBlob)},
    {"cGZPersistDBSerialRecord_SetFieldBlob", 0x99dd40, 27, 0x1686a73e, MemberAddress(&cGZPersistDBSerialRecord::SetFieldBlob)},
    {"cGZPersistDBSerialRecord_SetFieldStringThenOStream", 0x99b930, 22, 0x20d0127e,
     MemberAddress(&cGZPersistDBSerialRecord::SetFieldStringThenOStream)},
    {"cGZPersistDBSerialRecord_GetOStream", 0x99b950, 6, 0x200aff48, MemberAddress(&cGZPersistDBSerialRecord::GetOStream)},
    {"cGZPersistDBSerialRecord_GetFieldStringThenIStream", 0x99b910, 19, 0x6ef0972d,
     MemberAddress(&cGZPersistDBSerialRecord::GetFieldStringThenIStream)},
    {"cGZPersistDBSerialRecordThunks_GetVoid", 0x99cee0, 11, 0xd019d049, MemberAddress(&cGZPersistDBSerialRecordThunks::GetVoid)},
    {"cGZPersistDBSerialRecordThunks_SetVoid", 0x99de60, 14, 0xa575e35f, MemberAddress(&cGZPersistDBSerialRecordThunks::SetVoid)},
    {"cGZPersistDBSerialRecordThunks_Skip", 0x99dea0, 14, 0xef03d0c4, MemberAddress(&cGZPersistDBSerialRecordThunks::Skip)},
    {"cGZPersistDBSerialRecordThunks_GetGZStr", 0x99cd80, 11, 0xd903292d, MemberAddress(&cGZPersistDBSerialRecordThunks::GetGZStr)},
    {"cGZPersistDBSerialRecordThunks_SetGZStr", 0x99dd60, 14, 0xd2b72324, MemberAddress(&cGZPersistDBSerialRecordThunks::SetGZStr)},
    {"cGZPersistDBSerialRecordThunks_QueryInterface4", 0x1e2de3, 8, 0xae2670ee,
     MemberAddress(&cGZPersistDBSerialRecordThunks::QueryInterface4)},
    {"cGZPersistDBSerialRecordThunks_AddRef4", 0x1dab02, 8, 0x8403d8d3, MemberAddress(&cGZPersistDBSerialRecordThunks::AddRef4)},
    {"cGZPersistDBSerialRecordThunks_Release4", 0x1e3950, 8, 0xfacc27b6, MemberAddress(&cGZPersistDBSerialRecordThunks::Release4)},
    {"cGZPersistDBSerialRecordThunks_AddRef32", 0x1dab0a, 8, 0x5beca1bb, MemberAddress(&cGZPersistDBSerialRecordThunks::AddRef32)},
    {"cGZPersistDBSerialRecordThunks_Release32", 0x1e3958, 8, 0xa3454879, MemberAddress(&cGZPersistDBSerialRecordThunks::Release32)},
    {"cGZPersistDBSerialRecordThunks_QueryInterface32", 0x1e2deb, 8, 0xa7b648be,
     MemberAddress(&cGZPersistDBSerialRecordThunks::QueryInterface32)},
    {"cGZPersistDBSerialRecordThunks_AddRef48", 0x1dab12, 8, 0x9d8e8bcf, MemberAddress(&cGZPersistDBSerialRecordThunks::AddRef48)},
    {"cGZPersistDBSerialRecordThunks_Release48", 0x1e3960, 8, 0x0e18c291, MemberAddress(&cGZPersistDBSerialRecordThunks::Release48)},
    {"cGZPersistDBSerialRecordThunks_QueryInterface48", 0x1e2df3, 8, 0x61d462ca,
     MemberAddress(&cGZPersistDBSerialRecordThunks::QueryInterface48)},
    {"cGZPersistDBSerialRecordThunks_GetError48", 0x1dff06, 8, 0xf45472bb, MemberAddress(&cGZPersistDBSerialRecordThunks::GetError48)},
    {"cGZPersistDBSerialRecordThunks_SetRZCharStr", 0x99dda0, 32, 0x37a40e34,
     MemberAddress(static_cast<bool (cGZPersistDBSerialRecordThunks::*)(const char *)>(&cGZPersistDBSerialRecordThunks::SetRZCharStr))},
    {"cGZPersistDBSerialRecord_GetFieldString", 0x99bec0, 32, 0x1e2c8164, MemberAddress(&cGZPersistDBSerialRecord::GetFieldString)},
    {"cGZPersistDBSerialRecord_GetFieldVariant", 0x99c280, 32, 0x0aaaa0fd, MemberAddress(&cGZPersistDBSerialRecord::GetFieldVariant)},
    {"cGZPersistDBSerialRecord_SetFieldVariant", 0x99d660, 32, 0x4bb9063b, MemberAddress(&cGZPersistDBSerialRecord::SetFieldVariant)},
    {"FUN_a099b7c0", 0x99b7c0, 32, 0xeb9e44db, MemberAddress(&cGZPersistDBSerialRecord::Construct)},
    {"FUN_a01da060", 0x1da060, 32, 0x55ae9e07, MemberAddress(&cGZPersistDBSerialRecord::DeletingDestructor)},
    {"FUN_a099b8c0", 0x99b8c0, 19, 0x49098cd4, MemberAddress(&cGZPersistDBRecord::Destruct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
