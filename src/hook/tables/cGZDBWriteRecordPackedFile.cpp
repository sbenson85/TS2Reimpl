#include "hook/hooks.h"
#include "gz/cGZDBWriteRecordPackedFile.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZDBWriteRecordPackedFile_Construct", 0x9a70f0, 32, 0x27d15ca0, MemberAddress(&cGZDBWriteRecordPackedFile::Construct)},
    {"cGZDBWriteRecordPackedFile_DeletingDestructor", 0x9a7b80, 32, 0x6f3ea86b, MemberAddress(&cGZDBWriteRecordPackedFile::DeletingDestructor)},
    {"cGZDBWriteRecordPackedFile_GetFieldVoid", 0x9abd70, 32, 0xf038fa65, MemberAddress(&cGZDBWriteRecordPackedFile::GetFieldVoid)},
    {"cGZDBWriteRecordPackedFile_SetFieldVoid", 0x9afe70, 32, 0xfddb5d91, MemberAddress(&cGZDBWriteRecordPackedFile::SetFieldVoid)},
    {"cGZDBWriteRecordPackedFile_GetSize", 0x9acb80, 32, 0x827ec226, MemberAddress(&cGZDBWriteRecordPackedFile::GetSize)},
    {"cGZDBWriteRecordPackedFile_SetSize", 0x9b0360, 32, 0xd00e3c8d, MemberAddress(&cGZDBWriteRecordPackedFile::SetSize)},
    {"cGZDBWriteRecordPackedFile_GetPosition", 0x9ac910, 32, 0x2355a108, MemberAddress(&cGZDBWriteRecordPackedFile::GetPosition)},
    {"cGZDBWriteRecordPackedFile_SeekAbsolute", 0x9afac0, 32, 0xe1e5f6e0, MemberAddress(&cGZDBWriteRecordPackedFile::SeekAbsolute)},
    {"cGZDBWriteRecordPackedFile_SeekRelative", 0x9afb90, 32, 0x8cf661ae, MemberAddress(&cGZDBWriteRecordPackedFile::SeekRelative)},
    {"cGZDBWriteRecordPackedFile_Close", 0x9aa070, 32, 0x14c8b8f3, MemberAddress(&cGZDBWriteRecordPackedFile::Close)},
    {"cGZDBWriteRecordPackedFile_FlushToSegment", 0x9b2550, 32, 0x7385ce0e, MemberAddress(&cGZDBWriteRecordPackedFile::FlushToSegment)},
};
REGISTER_HOOKS(kHooks);

} // namespace
