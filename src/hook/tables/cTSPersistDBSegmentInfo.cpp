#include "hook/hooks.h"
#include "ts/cTSPersistDBSegmentInfo.h"

namespace {

// Not hooked: the folded slots 1, 2, 4, 5, 6, 8, 19 of the primary vtable and slots 1, 2 of the +4 vtable
// (0x1dab20, 0x1e3970, 0x147780, 0x1c6e40, 0x1e0950, 0x1dff70, 0x1e5370, 0x1dab28, 0x1e3978): other classes
// share their bytes.
const HookSpec kHooks[] = {
    {"cTSPersistDBSegmentInfo_Create", 0x1df210, 32, 0x4c5c8837, reinterpret_cast<void *>(&cTSPersistDBSegmentInfo::Create)},
    {"cTSPersistDBSegmentInfo_DeletingDestructor", 0x1da210, 32, 0x9a8bdca1, MemberAddress(&cTSPersistDBSegmentInfo::DeletingDestructor)},
    {"cTSPersistDBSegmentInfo_vf4_3", 0x1da04b, 8, 0x7e21a55c, MemberAddress(&cTSPersistDBSegmentInfoThunks::DeletingDestructor4)},
    {"cTSPersistDBSegmentInfo_QueryInterface", 0x1e2e10, 32, 0xc2d208bd, MemberAddress(&cTSPersistDBSegmentInfo::QueryInterface)},
    {"cTSPersistDBSegmentInfo_QueryInterface_4", 0x1e2e48, 8, 0xe6089138, MemberAddress(&cTSPersistDBSegmentInfoThunks::QueryInterface4)},
    {"cTSPersistDBSegmentInfo_vf7", 0x1e0540, 5, 0x47eaa867, MemberAddress(&cTSPersistDBSegmentInfo::GetField54)},
    {"cTSPersistDBSegmentInfo_vf9", 0x1dcc40, 17, 0x3d3c83c2, MemberAddress(&cTSPersistDBSegmentInfo::TestFlags)},
    {"cTSPersistDBSegmentInfo_vf11", 0x1e0c20, 32, 0x30d2b97f, MemberAddress(&cTSPersistDBSegmentInfo::SetFromChars)},
    {"cTSPersistDBSegmentInfo_vf15", 0x1e4f20, 30, 0x4efb91a5, MemberAddress(&cTSPersistDBSegmentInfo::SetPathFromString)},
    {"cTSPersistDBSegmentInfo_vf17", 0x1e4dc0, 9, 0x35f5fd8f, MemberAddress(&cTSPersistDBSegmentInfo::SetString2FromChar)},
    {"cTSPersistDBSegmentInfo_vf18", 0x1e4db0, 9, 0x5e9eac7b, MemberAddress(&cTSPersistDBSegmentInfo::AssignString2)},
    {"cTSPersistDBSegmentInfo_vf20", 0x1e5250, 12, 0x221e953a, MemberAddress(&cTSPersistDBSegmentInfo::SetField54)},
    {"cTSPersistDBSegmentInfo_vf21", 0x1e4d70, 32, 0x0451039c, MemberAddress(&cTSPersistDBSegmentInfo::ChangeFlags)},
};
REGISTER_HOOKS(kHooks);

} // namespace
