#include "hook/hooks.h"
#include "gz/cGZPersistDBSegmentMultiPackedFiles.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZPersistDBSegmentMultiPackedFiles_QueryInterface", 0x9b9950, 32, 0x41df9a54, MemberAddress(&cGZPersistDBSegmentMultiPackedFiles::QueryInterface)},
    {"cGZPersistDBSegmentMultiPackedFiles_Shutdown", 0x9b9a60, 26, 0x51262643, MemberAddress(&cGZPersistDBSegmentMultiPackedFiles::Shutdown)},
    {"cGZPersistDBSegmentMultiPackedFiles_GetPath", 0x9b9630, 20, 0x76cb78b2, MemberAddress(&cGZPersistDBSegmentMultiPackedFiles::GetPath)},
    {"cGZPersistDBSegmentMultiPackedFiles_SetPath", 0x9b9a20, 29, 0x1897c64e, MemberAddress(&cGZPersistDBSegmentMultiPackedFiles::SetPath)},
    {"cGZPersistDBSegmentMultiPackedFiles_SetSegmentID", 0x9b9a50, 14, 0x22573a57, MemberAddress(&cGZPersistDBSegmentMultiPackedFiles::SetSegmentID)},
    {"cGZPersistDBSegmentMultiPackedFiles_vf44", 0x9b9400, 6, 0x7788ac4f, MemberAddress(&cGZPersistDBSegmentMultiPackedFiles::Field7C)},
    {"cGZPersistDBSegmentMultiPackedFiles_vf45", 0x9b9460, 32, 0xa0f16a23, MemberAddress(&cGZPersistDBSegmentMultiPackedFiles::SumItems)},
    {"cGZPersistDBSegmentMultiPackedFiles_vf24_7", 0x9b9660, 10, 0x913a511f, MemberAddress(&MultiPackedFilesSegmentView::ItemCount)},
    {"cGZPersistDBSegmentMultiPackedFiles_vf24_8", 0x9b9650, 13, 0xab1e709e, MemberAddress(&MultiPackedFilesSegmentView::ItemAt)},
    {"cGZPersistDBSegmentMultiPackedFiles_vf35", 0x9b9570, 32, 0x5280835c, MemberAddress(&cGZPersistDBSegmentMultiPackedFiles::TestForRecordImpl)},
    {"cGZPersistDBSegmentMultiPackedFiles_vf36", 0x9b9410, 32, 0xeb722a43, MemberAddress(&cGZPersistDBSegmentMultiPackedFiles::GetRecordSizeImpl)},
    {"cGZPersistDBSegmentMultiPackedFiles_vf37", 0x9b94b0, 32, 0x76454f46, MemberAddress(&cGZPersistDBSegmentMultiPackedFiles::OpenRecordImpl)},
    {"cGZPersistDBSegmentMultiPackedFiles_vf39", 0x9b93a0, 32, 0xae5ecb7a, MemberAddress(&cGZPersistDBSegmentMultiPackedFiles::CloseRecordImpl)},
    {"cGZPersistDBSegmentMultiPackedFiles_vf40", 0x9b9340, 32, 0xae5ecb7a, MemberAddress(&cGZPersistDBSegmentMultiPackedFiles::AbortRecordImpl)},
    {"cGZPersistDBSegmentMultiPackedFiles_vf42", 0x9b9510, 32, 0x76a9fcf1, MemberAddress(&cGZPersistDBSegmentMultiPackedFiles::ReadRecordImpl)},
    {"cGZPersistDBSegmentMultiPackedFiles_Close", 0x9b91b0, 32, 0x6c572825, MemberAddress(&cGZPersistDBSegmentMultiPackedFiles::Close)},
    {"cGZPersistDBSegmentMultiPackedFiles_Construct", 0x9b8df0, 32, 0x96f58cec, MemberAddress(&cGZPersistDBSegmentMultiPackedFiles::Construct)},
    {"cGZPersistDBSegmentMultiPackedFiles_vf33", 0x9b8ef0, 32, 0x81eff32f, MemberAddress(&cGZPersistDBSegmentMultiPackedFiles::DeletingDestructor)},
    {"MultiPackedFilesTable_Clear", 0x1fc2f0, 32, 0xd3bef977, MemberAddress(&MultiPackedFilesTable::Clear)},
    {"cGZPersistDBSegmentMultiPackedFiles_vf24_6", 0x9b95c0, 32, 0x05b96e30, MemberAddress(&MultiPackedFilesSegmentView::GetItem)},
    {"cGZPersistDBSegmentMultiPackedFiles_vf24_10", 0x9b99a0, 32, 0x2aaff17d, MemberAddress(&MultiPackedFilesSegmentView::RemoveItem)},
    {"cGZPersistDBSegmentMultiPackedFiles_vf24_5", 0x9b92d0, 32, 0xf11aa261, MemberAddress(&MultiPackedFilesSegmentView::SumPackedFiles)},
    {"cGZPersistDBSegmentMultiPackedFiles_vf24_3", 0x9b9a40, 9, 0xe42fb2e6, MemberAddress(&MultiPackedFilesSegmentView::SetPattern)},
    {"cGZPersistDBSegmentMultiPackedFiles_vf24_0", 0x9b997f, 8, 0xef141c52, MemberAddress(&MultiPackedFilesSegmentView::QueryInterface)},
    {"cGZPersistDBSegmentMultiPackedFiles_vf24_1", 0x9b9012, 8, 0x9a56b356, MemberAddress(&MultiPackedFilesSegmentView::AddRef)},
    {"cGZPersistDBSegmentMultiPackedFiles_vf24_2", 0x9b9995, 8, 0x70820497, MemberAddress(&MultiPackedFilesSegmentView::Release)},
};
REGISTER_HOOKS(kHooks);

} // namespace
