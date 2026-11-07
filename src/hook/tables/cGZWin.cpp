#include "hook/hooks.h"
#include "gz/cGZWin.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZWin_SetFlag8C", 0x931862, 8, 0x858e0677, MemberAddress(&cGZWin::SetFlag8C)},
    {"cGZWin_GetOpaqueF4", 0x931727, 12, 0xe89fb69b, MemberAddress(&cGZWin::GetOpaqueF4)},
    {"cGZWin_GetWidth", 0x9317b7, 13, 0x7fb48d09, MemberAddress(&cGZWin::GetWidth)},
    {"cGZWin_GetHeight", 0x93174e, 13, 0x3aed584b, MemberAddress(&cGZWin::GetHeight)},
    {"cGZWin_SetF0", 0x933038, 13, 0x83428235, MemberAddress(&cGZWin::SetF0)},
    {"cGZWin_GetF0", 0x931792, 15, 0x385719f8, MemberAddress(&cGZWin::GetF0)},
    {"cGZWin_TestFlagsE8", 0x931733, 16, 0xbcefc2e4, MemberAddress(&cGZWin::TestFlagsE8)},
    {"cGZWin_GetF4", 0x931716, 17, 0x0b855ee8, MemberAddress(&cGZWin::GetF4)},
    {"cGZWin_CopyRectA", 0x9311ac, 18, 0x6c4991a2, MemberAddress(&cGZWin::CopyRectA)},
    {"cGZWin_CopyRectB", 0x931197, 21, 0x1475000b, MemberAddress(&cGZWin::CopyRectB)},
    {"cGZWin_AddOffset", 0x933327, 21, 0xcc3de961, MemberAddress(&cGZWin::AddOffset)},
    {"cGZWin_PackWords", 0x931c61, 17, 0xb8e15e58, reinterpret_cast<void *>(&cGZWin::PackWords)},
    {"cGZWin_SetFlag4000", 0x933f35, 20, 0x8b7cdce3, MemberAddress(&cGZWin::SetFlag4000)},
    {"cGZWin_TestFlag4000", 0x933b29, 14, 0x2d80d28f, MemberAddress(&cGZWin::TestFlag4000)},
    {"cGZWin_Teardown", 0x9330c1, 32, 0x751cf7fb, MemberAddress(&cGZWin::Teardown)},
    {"cGZWin_SetChildFlagNotify", 0x9352d8, 32, 0x653793f9, reinterpret_cast<void *>(&SetChildFlagNotify)},
    {"cGZWin_Activate", 0x9317d9, 32, 0x2b708f82, MemberAddress(&cGZWin::Activate)},
    {"cGZWin_SetFlag1", 0x9330b0, 17, 0x98d76cea, MemberAddress(&cGZWin::SetFlag1)},
    {"cGZWin_ClearFlag1", 0x9317cc, 13, 0xcc6c4815, MemberAddress(&cGZWin::ClearFlag1)},
    {"cGZWin_SetF90Notify", 0x932be5, 28, 0xf40f06bb, MemberAddress(&cGZWin::SetF90Notify)},
    {"cGZWin_SetF4", 0x5c8be0, 13, 0x770da626, MemberAddress(&cGZWin::SetF4)},
    {"cGZWin_SetF4Ok", 0x932c79, 15, 0xff3924bc, MemberAddress(&cGZWin::SetF4Ok)},
    {"cGZWin_GetF4Bytes", 0x9316ef, 32, 0x6188864a, MemberAddress(&cGZWin::GetF4Bytes)},
    {"cGZWin_SetF4Bytes", 0x932c18, 32, 0x3ece7f3a, MemberAddress(&cGZWin::SetF4Bytes)},
};
REGISTER_HOOKS(kHooks);

} // namespace
