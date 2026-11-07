#include "hook/hooks.h"
#include "gz/cGZDBSDFRecord.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZDBSDFRecord_QueryInterface", 0x9a2610, 32, 0x88010a45, MemberAddress(&cGZDBSDFRecord::QueryInterface)},
    {"cGZDBSDFRecord_AddRef", 0x99f2e0, 8, 0xfec051dc, MemberAddress(&cGZDBSDFRecord::AddRef)},
    {"cGZDBSDFRecord_Release", 0x9a2780, 8, 0x918b25b7, MemberAddress(&cGZDBSDFRecord::Release)},
    {"cGZDBSDFRecord_GetPosition", 0x9a1b90, 19, 0x9e195382, MemberAddress(&cGZDBSDFRecordSerialView::GetPosition)},
    {"cGZDBSDFRecord_SeekAbsolute", 0x9a2820, 32, 0x717410ba, MemberAddress(&cGZDBSDFRecordSerialView::SeekAbsolute)},
    {"cGZDBSDFRecord_SeekRelative", 0x9a2860, 32, 0xd4646a39, MemberAddress(&cGZDBSDFRecordSerialView::SeekRelative)},
    {"cGZDBSDFRecord_SetSize", 0x9a2a40, 22, 0x220af6d1, MemberAddress(&cGZDBSDFRecordSerialView::SetSize)},
    {"cGZDBSDFRecord_GetSize", 0x9a1c50, 19, 0xca12e06b, MemberAddress(&cGZDBSDFRecord::GetSize)},
    {"cGZDBSDFRecord_GetFieldVoid", 0x9a1460, 32, 0xd12018e3, MemberAddress(&cGZDBSDFRecord::GetFieldVoid)},
    {"cGZDBSDFRecord_SetFieldVoid", 0x9a28d0, 32, 0x5c04c48e, MemberAddress(&cGZDBSDFRecord::SetFieldVoid)},
    {"cGZDBSDFRecord_ReadCounted", 0x9a2740, 32, 0x9452bd3e, MemberAddress(&cGZDBSDFRecord::ReadCounted)},
    {"cGZDBSDFRecord_WriteCounted", 0x9a2bd0, 32, 0x7b000bdf, MemberAddress(&cGZDBSDFRecord::WriteCounted)},
    {"cGZDBSDFRecord_GetPositionTo", 0x9a2ba0, 32, 0xaef1fc59, MemberAddress(&cGZDBSDFRecord::GetPositionTo)},
    {"cGZDBSDFRecord_SeekTo", 0x9a27e0, 32, 0x87686748, MemberAddress(&cGZDBSDFRecord::SeekTo)},
    {"cGZDBSDFRecordSerial_AsIGZPersistDBRecord", 0x99f330, 12, 0xbbbbe51c,
     MemberAddress(&cGZDBSDFRecordSerialView::AsIGZPersistDBRecord)},
};
REGISTER_HOOKS(kHooks);

} // namespace
