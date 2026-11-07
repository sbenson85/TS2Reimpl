#include "hook/hooks.h"
#include "gz/cGZPersistBinaryWriter.h"

namespace {

using W = cGZPersistBinaryWriter;

const HookSpec kHooks[] = {
    {"cGZPersistBinaryWriter_QueryInterface", 0x9bfce0, 32, 0xb1aa7764, MemberAddress(&W::QueryInterface)},
    {"cGZPersistBinaryWriter_GetCurrent", 0x9bfc80, 32, 0x7cf0a8b9, MemberAddress(&W::GetCurrent)},
    {"cGZPersistBinaryWriter_SetUint8", 0x9c0930, 12, 0x020dc631, MemberAddress(&W::SetUint8)},
    {"cGZPersistBinaryWriter_SetSint8", 0x9c0c80, 12, 0x05600228, MemberAddress(&W::SetSint8)},
    {"cGZPersistBinaryWriter_SetSint16", 0x9c0be0, 12, 0x0bbb8a1a, MemberAddress(&W::SetSint16)},
    {"cGZPersistBinaryWriter_SetUint16", 0x9c0cb0, 12, 0x0cd64e03, MemberAddress(&W::SetUint16)},
    {"cGZPersistBinaryWriter_SetSint32", 0x9c0c30, 12, 0x23b93284, MemberAddress(&W::SetSint32)},
    {"cGZPersistBinaryWriter_SetUint32", 0x9c0d00, 12, 0x24d4f69d, MemberAddress(&W::SetUint32)},
    {"cGZPersistBinaryWriter_SetFloat32", 0x9c0980, 27, 0x43195825, MemberAddress(&W::SetFloat32)},
    {"cGZPersistBinaryWriter_SetFloat64", 0x9c09f0, 29, 0xb583f27a, MemberAddress(&W::SetFloat64)},
    {"cGZPersistBinaryWriter_SetVoid", 0x9c0bd0, 12, 0x696697c5, MemberAddress(&W::SetVoid)},
    {"cGZPersistBinaryWriter_SetUint8Array", 0x9c0940, 32, 0xff3eb022, MemberAddress(&W::SetUint8Array)},
    {"cGZPersistBinaryWriter_SetSint16Array", 0x9c0bf0, 32, 0x6baf4199, MemberAddress(&W::SetSint16Array)},
    {"cGZPersistBinaryWriter_SetUint16Array", 0x9c0cc0, 32, 0x6baf4199, MemberAddress(&W::SetUint16Array)},
    {"cGZPersistBinaryWriter_SetSint32Array", 0x9c0c40, 32, 0x04e520b7, MemberAddress(&W::SetSint32Array)},
    {"cGZPersistBinaryWriter_vf19", 0x9c0d10, 32, 0x04e520b7, MemberAddress(&W::SetUint32Array)},
    {"cGZPersistBinaryWriter_vf20", 0x9c09a0, 32, 0xa38a35b3, MemberAddress(&W::SetFloat32Array)},
    {"cGZPersistBinaryWriter_vf21", 0x9c0a10, 32, 0x6f5a9097, MemberAddress(&W::SetFloat64Array)},
    {"cGZPersistBinaryWriter_vf23", 0x9c0c90, 12, 0x30d5aad2, MemberAddress(&W::WriteRZCharStr)},
    {"cGZPersistBinaryWriter_vf24", 0x9c0ca0, 12, 0x37b86ecb, MemberAddress(&W::WriteGZStr)},
    {"cGZPersistBinaryWriter_vf25", 0x9c0a60, 32, 0x9e43b2be, MemberAddress(&W::WriteSerialized)},
    {"cGZPersistBinaryWriter_vf27", 0x9c0ad0, 32, 0xa50bb2d8, MemberAddress(&W::WriteFlaggedObject)},
    {"cGZPersistBinaryWriter_vf28", 0x9c1230, 32, 0xed16e7a2, MemberAddress(&W::WriteRecord)},
    {"cGZPersistBinaryWriter_vf3", 0x9c0d50, 32, 0x903c091e, MemberAddress(&cGZPersistBinaryWriter::WriteRoot)},
    {"cGZPersistBinaryWriter_vf31", 0x9bfc50, 32, 0x2c130b3a, MemberAddress(&cGZPersistBinaryWriter::DeletingDestructor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
