#include "hook/hooks.h"
#include "gz/cGZDBSerialRecordAsIGZFile.h"

namespace {

using F = cGZDBSerialRecordAsIGZFile::File;

const HookSpec kHooks[] = {
    {"cGZDBSerialRecordAsIGZFile_QueryInterface", 0x9adad0, 32, 0x872c2d47, MemberAddress(&cGZDBSerialRecordAsIGZFile::QueryInterface)},
    {"cGZDBSerialRecordAsIGZFile_Release", 0x9af990, 32, 0xce3b402f, MemberAddress(&cGZDBSerialRecordAsIGZFile::Release)},
    {"cGZDBSerialRecordAsIGZFile_SetRecord", 0x9afcd0, 32, 0xec897e5b, MemberAddress(&cGZDBSerialRecordAsIGZFile::SetRecord)},
    {"cGZDBSerialRecordAsIGZFile_Close", 0x9a8090, 32, 0x04a1ab98, MemberAddress(&F::Close)},
    {"cGZDBSerialRecordAsIGZFile_Position", 0x9ada80, 19, 0xc2c37252, MemberAddress(&F::Position)},
    {"cGZDBSerialRecordAsIGZFile_Length", 0x9ad730, 18, 0x0ae0c3a1, MemberAddress(&F::Length)},
    {"cGZDBSerialRecordAsIGZFile_SetLength", 0x9b01e0, 22, 0x559be894, MemberAddress(&F::SetLength)},
    {"cGZDBSerialRecordAsIGZFile_SeekToBegin", 0x9afbe0, 32, 0xb63fe668, MemberAddress(&F::SeekToBegin)},
    {"cGZDBSerialRecordAsIGZFile_SeekToEnd", 0x9afc10, 32, 0x418ff86e, MemberAddress(&F::SeekToEnd)},
    {"cGZDBSerialRecordAsIGZFile_SeekToRelativePosition", 0x9afc80, 32, 0x30fd3194, MemberAddress(&F::SeekToRelativePosition)},
    {"cGZDBSerialRecordAsIGZFile_SeekToPosition", 0x9afc50, 32, 0xe70980b0, MemberAddress(&F::SeekToPosition)},
    {"cGZDBSerialRecordAsIGZFile_Seek", 0x9af9c0, 32, 0x2bd2c473, MemberAddress(&F::Seek)},
    {"cGZDBSerialRecordAsIGZFile_Read", 0x9ae4e0, 19, 0xf0aef712, MemberAddress(&F::Read)},
    {"cGZDBSerialRecordAsIGZFile_ReadWithCount", 0x9af930, 32, 0x858335f7, MemberAddress(&F::ReadWithCount)},
    {"cGZDBSerialRecordAsIGZFile_Write", 0x9b1180, 22, 0xed1b8216, MemberAddress(&F::Write)},
    {"cGZDBSerialRecordAsIGZFile_vf6", 0x9a7b30, 32, 0x59f15211, MemberAddress(&cGZDBSerialRecordAsIGZFile::DeletingDestructor)},
    {"cGZDBSerialRecordAsIGZFile_WriteWithCount", 0x9b26b0, 32, 0x1223dcce, MemberAddress(&F::WriteWithCount)},
    {"cGZDBSerialRecordAsIGZFile_GetOpenModes", 0x9ac800, 32, 0x6f95d197, MemberAddress(&F::GetOpenModes)},
    {"cGZDBSerialRecordAsIGZFile_GetPath_1", 0x9ac870, 32, 0xad695db2, MemberAddress(&F::GetPath)},
};
REGISTER_HOOKS(kHooks);

} // namespace
