#include "hook/hooks.h"
#include "ui/cGZWinText.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZWinText_Construct", 0x949ed1, 32, 0x52db2775, MemberAddress(&cGZWinText::Construct)},
    {"cGZWinText_Destruct", 0x949f68, 32, 0x5eac6a61, MemberAddress(&cGZWinText::Destruct)},
    {"cGZWinText_DeletingDestructor", 0x949f97, 32, 0x7cbf4a0e, MemberAddress(&cGZWinText::DeletingDestructor)},
    {"cGZWinText_QueryInterface", 0x94a382, 32, 0xe1510d54, MemberAddress(&cGZWinText::QueryInterface)},
    {"cGZWinText_DoMessage", 0x949ffd, 32, 0x9cc3707b, MemberAddress(&cGZWinText::DoMessage)},
    {"cGZWinText_Activate", 0x94a315, 32, 0x781d2ebf, MemberAddress(&cGZWinText::Activate)},
    {"cGZWinText_Teardown", 0x94a784, 32, 0xcd28d3d2, MemberAddress(&cGZWinText::Teardown)},
    {"cGZWinText_SetArea", 0x94a428, 32, 0xf611a169, MemberAddress(&cGZWinText::SetArea)},
    {"cGZWinText_SetCaption", 0x94a4a0, 32, 0xb83a56d8, MemberAddress(&cGZWinText::SetCaption)},
    {"cGZWinText_Draw", 0x94a0bb, 32, 0x2574234e, MemberAddress(&cGZWinText::Draw)},
    {"cGZWinText_SetBackgroundColor", 0x94a4cc, 26, 0xf52deeb1, MemberAddress(&cGZWinText::SetBackgroundColor)},
    {"cGZWinText_Layout", 0x94a7a9, 32, 0xc20eaf37, MemberAddress(&cGZWinText::Layout)},
    {"cGZWinText_Measure", 0x94a916, 32, 0xbcea9db8, MemberAddress(&cGZWinText::Measure)},
    {"cGZWinTextFace_SizeToText", 0x94a064, 32, 0xbe07ed81, MemberAddress(&cGZWinTextFace::SizeToText)},
    {"cGZWinTextFace_SetStyle", 0x94a6ed, 32, 0x02d57655, MemberAddress(&cGZWinTextFace::SetStyle)},
    {"cGZWinTextFace_SetColorRGB", 0x94a632, 32, 0x4b18abb4, MemberAddress(&cGZWinTextFace::SetColorRGB)},
    {"cGZWinTextFace_SetColor", 0x94a609, 32, 0x3020db76, MemberAddress(&cGZWinTextFace::SetColor)},
    {"cGZWinTextFace_SetTextColor", 0x94a5fb, 14, 0x72e59503, MemberAddress(&cGZWinTextFace::SetTextColor)},
    {"cGZWinTextFace_GetColorRGB", 0x94a2d1, 32, 0x551b4538, MemberAddress(&cGZWinTextFace::GetColorRGB)},
    {"cGZWinTextFace_GetColor", 0x94a292, 32, 0x9a253e02, MemberAddress(&cGZWinTextFace::GetColor)},
    {"cGZWinTextFace_GetTextColor", 0x94a284, 14, 0xad9e55cb, MemberAddress(&cGZWinTextFace::GetTextColor)},
    {"cGZWinTextFace_ResetColor", 0x949fb9, 32, 0x30e9b0c1, MemberAddress(&cGZWinTextFace::ResetColor)},
    {"cGZWinTextFace_ResetTextColor", 0x949ff5, 8, 0xc32066dd, MemberAddress(&cGZWinTextFace::ResetTextColor)},
    {"cGZWinTextFace_SetFill", 0x94a492, 14, 0xb335615e, MemberAddress(&cGZWinTextFace::SetFill)},
    {"cGZWinTextFace_IsFilled", 0x94a37a, 8, 0xa324e4bf, MemberAddress(&cGZWinTextFace::IsFilled)},
    {"cGZWinTextFace_SetFont", 0x94a4e6, 32, 0xcf7c9f36, MemberAddress(&cGZWinTextFace::SetFont)},
    {"cGZWinTextFace_SetFontID", 0x94a576, 32, 0x282e6c25, MemberAddress(&cGZWinTextFace::SetFontID)},
    {"cGZWinTextFace_GetFontID", 0x94a23c, 22, 0x2b0a8cc0, MemberAddress(&cGZWinTextFace::GetFontID)},
    {"cGZWinTextFace_SetAlignment", 0x94a3bb, 32, 0xd09fb802, MemberAddress(&cGZWinTextFace::SetAlignment)},
    {"cGZWinTextFace_GetAlignment", 0x94a22e, 14, 0xa6c63054, MemberAddress(&cGZWinTextFace::GetAlignment)},
    {"cGZWinTextFace_SetMargins", 0x94a5bd, 30, 0xc5f3a857, MemberAddress(&cGZWinTextFace::SetMargins)},
    {"cGZWinTextFace_GetMargins", 0x94a252, 23, 0x9d0c5cc0, MemberAddress(&cGZWinTextFace::GetMargins)},
    {"cGZWinTextFace_SetExtra", 0x94a6da, 19, 0xd29fcbea, MemberAddress(&cGZWinTextFace::SetExtra)},
    {"cGZWinTextFace_GetExtra", 0x94a2fe, 23, 0x0b15cb7a, MemberAddress(&cGZWinTextFace::GetExtra)},
    {"cGZWinTextFace_SetFrame", 0x94a5db, 32, 0xaa575f14, MemberAddress(&cGZWinTextFace::SetFrame)},
    {"cGZWinTextFace_GetFrame", 0x94a269, 27, 0x9ad9d1a3, MemberAddress(&cGZWinTextFace::GetFrame)},
};
REGISTER_HOOKS(kHooks);

} // namespace
