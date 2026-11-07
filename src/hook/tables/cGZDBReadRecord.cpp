#include "hook/hooks.h"
#include "gz/cGZDBReadRecord.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZDBReadRecordPackedFile_Construct", 0x9a6de0, 32, 0x7a1e0300, MemberAddress(&cGZDBReadRecordPackedFile::Construct)},
    {"cGZDBReadRecordPackedFile_DeletingDestructor", 0x9a78e0, 32, 0x55ae9e07, MemberAddress(&cGZDBReadRecordPackedFile::DeletingDestructor)},
    {"cGZDBReadRecordPackedFile_GetFieldVoid", 0x9abc70, 32, 0x377e3bb6, MemberAddress(&cGZDBReadRecordPackedFile::GetFieldVoid)},
    {"cGZDBReadRecordPackedFile_SetFieldVoid", 0x9afdf0, 29, 0x208de305, MemberAddress(&cGZDBReadRecordPackedFile::SetFieldVoid)},
    {"cGZDBRecordPackedFile_Seek", 0x9afa20, 32, 0xe58c105f, MemberAddress(&cGZDBRecordPackedFile::Seek)},
    {"cGZDBReadRecordPackedFile_SeekRelative", 0x9afb10, 32, 0x62f5304d, MemberAddress(&cGZDBReadRecordPackedFile::SeekRelative)},
    {"cGZDBReadRecordPackedFile_vf62", 0x9a9ff0, 32, 0xbc5a86d4, MemberAddress(&cGZDBReadRecordPackedFile::Close)},
    {"cGZDBReadRecordPackedFile_vf64", 0x9af120, 32, 0xb19a839b, MemberAddress(&cGZDBReadRecordPackedFile::ReadSource)},
    {"cGZDBReadRecordPackedFile_vf65", 0x9ae500, 32, 0x30f88c0c, MemberAddress(&cGZDBReadRecordPackedFile::ReadBuffered)},
    {"cGZDBReadRecordPackedFile_vf66", 0x9a8050, 32, 0x9ed125db, MemberAddress(&cGZDBReadRecordPackedFile::CachedRemaining)},
    {"cGZDBReadRecordRAM_Construct", 0x9a6e60, 32, 0x6a759fea, MemberAddress(&cGZDBReadRecordRAM::Construct)},
    {"cGZDBReadRecordRAM_DeletingDestructor", 0x9a7950, 32, 0x3c2f8000, MemberAddress(&cGZDBReadRecordRAM::DeletingDestructor)},
    {"cGZDBReadRecordRAM_GetFieldVoid", 0x9abd20, 32, 0x142de4a2, MemberAddress(&cGZDBReadRecordRAM::GetFieldVoid)},
    {"cGZDBReadRecordRAM_SetFieldVoid", 0x9afe20, 32, 0x261b8620, MemberAddress(&cGZDBReadRecordRAM::SetFieldVoid)},
    {"cGZDBReadRecordRAM_SeekRelative", 0x9afb50, 32, 0x3482850a, MemberAddress(&cGZDBReadRecordRAM::SeekRelative)},
    {"cGZDBReadRecordRAM_vf62", 0x9aa020, 32, 0x8d5e3617, MemberAddress(&cGZDBReadRecordRAM::Close)},
};
REGISTER_HOOKS(kHooks);

} // namespace
