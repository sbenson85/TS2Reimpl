#include "hook/hooks.h"
#include "ui/UIWinProcs.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZWinGrid_vf13", 0x936a5c, 16, 0x92aae376, MemberAddress(&cGZWinGrid::TestFlagsFC)},
    {"cGZWinMsgBox_vf252_6", 0x9529e5, 11, 0xf8f5e5eb, MemberAddress(&cGZWinMsgBoxAt252::ForwardToSlot5)},
    {"cTSWinProcGridThumbnail_vf7", 0x75de32, 32, 0x328d7505, MemberAddress(&cTSWinProcGridThumbnail::Vf7)},
    {"cTSWinProcGrid_vf39", 0x748af4, 32, 0x90abbf89, MemberAddress(&cTSWinProcGrid::Vf39)},
    {"nTSUICAS_cTSUICASComponent_vf360_8", 0x7594a3, 32, 0xd163a68c, MemberAddress(&nTSUICAS::cTSUICASComponent::Vf360_8)},
};
REGISTER_HOOKS(kHooks);

} // namespace
