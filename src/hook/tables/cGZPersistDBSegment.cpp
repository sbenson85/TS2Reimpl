#include "hook/hooks.h"
#include "gz/cGZPersistDBSegment.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZPersistDBSegment_Construct", 0x99b790, 32, 0xcd29b122, MemberAddress(&cGZPersistDBSegment::Construct)},
    {"cGZPersistDBSegment_Destruct", 0x99b8e0, 32, 0x841875ad, MemberAddress(&cGZPersistDBSegment::Destruct)},
    {"cGZPersistDBSegment_DeletingDestructor", 0x99b990, 32, 0x841875ad, MemberAddress(&cGZPersistDBSegment::DeletingDestructor)},
    {"cGZPersistDBSegment_QueryInterface", 0x99cf70, 32, 0x6622fcc0, MemberAddress(&cGZPersistDBSegment::QueryInterface)},
    {"cGZPersistDBSegment_Release", 0x99d070, 32, 0x52830c36, MemberAddress(&cGZPersistDBSegment::Release)},
    {"cGZPersistDBSegment_Shutdown", 0x99de70, 32, 0xbf7e046d, MemberAddress(&cGZPersistDBSegment::Shutdown)},
    {"cGZPersistDBSegment_Lock", 0x99cef0, 12, 0x39371115, MemberAddress(&cGZPersistDBSegment::Lock)},
    {"cGZPersistDBSegment_Unlock", 0x99def0, 12, 0x73e1aead, MemberAddress(&cGZPersistDBSegment::Unlock)},
    {"cGZPersistDBSegment_SetSegmentCache", 0x1d4540, 32, 0xe02f4625, MemberAddress(&cGZPersistDBSegment::SetSegmentCache)},
    {"cGZPersistDBSegment_GetRecordCount", 0x99ce10, 32, 0xc989a5b6, MemberAddress(&cGZPersistDBSegment::GetRecordCount)},
    {"cGZPersistDBSegment_GetRecordSize", 0x99ce40, 32, 0x06349c2a, MemberAddress(&cGZPersistDBSegment::GetRecordSize)},
    {"cGZPersistDBSegment_GetSegmentChangeID", 0x99ceb0, 32, 0x371ccad0, MemberAddress(&cGZPersistDBSegment::GetSegmentChangeID)},
    {"cGZPersistDBSegment_GetRecordChangeID", 0x99cdd0, 32, 0x770dcce9, MemberAddress(&cGZPersistDBSegment::GetRecordChangeID)},
    {"cGZPersistDBSegment_SetNamespace", 0x99dd70, 32, 0xb874ace1, MemberAddress(&cGZPersistDBSegment::SetNamespace)},
    {"cGZPersistDBSegment_GetNamespace", 0x99cd90, 32, 0x78ebb688, MemberAddress(&cGZPersistDBSegment::GetNamespace)},
    {"cGZPersistDBSegment_vf34", 0x99bba0, 15, 0x218b5f9d, MemberAddress(&cGZPersistDBSegment::vf34)},
    {"cGZPersistDBSegment_vf47", 0x99bb70, 32, 0x0c9c653d, MemberAddress(&cGZPersistDBSegment::vf47)},
    {"cGZPersistDBSegment_vf49", 0x99bb50, 31, 0x39a728c9, MemberAddress(&cGZPersistDBSegment::vf49)},
    {"cGZPersistDBSegment_GetResourceKeyList", 0x99ce70, 32, 0xef181ec8, MemberAddress(&cGZPersistDBSegment::GetResourceKeyList)},
    {"cGZPersistDBSegment_TestForRecord", 0x99deb0, 32, 0x82936fd0, MemberAddress(&cGZPersistDBSegment::TestForRecord)},
    {"cGZPersistDBSegment_AbortRecord", 0x99ba10, 32, 0x2c4a18c9, MemberAddress(&cGZPersistDBSegment::AbortRecord)},
    {"cGZPersistDBSegment_CloseRecord", 0x99ba90, 32, 0xfe285efb, MemberAddress(&cGZPersistDBSegment::CloseRecord)},
    {"cGZPersistDBSegment_CreateNewRecord", 0x99bad0, 32, 0x585df480, MemberAddress(&cGZPersistDBSegment::CreateNewRecord)},
    {"cGZPersistDBSegment_DeleteRecord", 0x99bb10, 32, 0xa8001633, MemberAddress(&cGZPersistDBSegment::DeleteRecord)},
    {"cGZPersistDBSegment_OpenRecord", 0x99cf00, 32, 0x59d9a401, MemberAddress(&cGZPersistDBSegment::OpenRecord)},
    {"cGZPersistDBSegment_ReadRecord", 0x99d010, 32, 0xca878582, MemberAddress(&cGZPersistDBSegment::ReadRecord)},
    {"cGZPersistDBSegment_WriteRecord", 0x99df00, 32, 0x59d9a401, MemberAddress(&cGZPersistDBSegment::WriteRecord)},
};
REGISTER_HOOKS(kHooks);

} // namespace
