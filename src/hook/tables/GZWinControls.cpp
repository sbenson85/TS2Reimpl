#include "hook/hooks.h"
#include "ui/GZWinControls.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZWinListBox_QueryInterface", 0x9555e8, 32, 0xde26d718, MemberAddress(&cGZWinListBox::QueryInterface)},
    {"cGZWinListBox_vf248_36", 0x95428e, 19, 0x1474732e, MemberAddress(&cGZWinListBoxAt248::Vf36)},
    {"cGZWinListBox_vf248_37", 0x9541ca, 23, 0xfcb5d66b, MemberAddress(&cGZWinListBoxAt248::Vf37)},
    {"cGZWinListBox_vf248_44", 0x954276, 24, 0x591b3dc6, MemberAddress(&cGZWinListBoxAt248::Vf44)},
    {"cGZWinListBox_vf248_52", 0x955ed0, 28, 0xb0bf913b, MemberAddress(&cGZWinListBoxAt248::Vf52)},
    {"cGZWinListBox_vf248_54", 0x955c39, 28, 0x2a91bad2, MemberAddress(&cGZWinListBoxAt248::Vf54)},
    {"cGZWinTextEdit_QueryInterface", 0x95079a, 32, 0xa515b954, MemberAddress(&cGZWinTextEdit::QueryInterface)},
    {"cGZWinTextEdit_vf248_14", 0x94f344, 32, 0xa50dcc4d, MemberAddress(&cGZWinTextEditAt248::Vf14)},
    {"cGZWinTextEdit_vf248_19", 0x94f12d, 32, 0x54384515, MemberAddress(&cGZWinTextEditAt248::Vf19)},
    {"cGZWinTextEdit_vf248_40", 0x94f84f, 32, 0xaa8dd8b7, MemberAddress(&cGZWinTextEditAt248::Vf40)},
    {"cGZWinTextEdit_vf248_44", 0x9522e6, 32, 0xed328440, MemberAddress(&cGZWinTextEditAt248::Vf44)},
    {"cGZWinTextEdit_vf248_79", 0x94f14e, 17, 0x0e5042ab, MemberAddress(&cGZWinTextEditAt248::Vf79)},
    {"cGZWinTextEdit_vf248_82", 0x95151c, 27, 0x6d6f45d2, MemberAddress(&cGZWinTextEditAt248::Vf82)},
    {"cGZWinTextEdit_vf248_84", 0x94d3d7, 26, 0xdaf21b01, MemberAddress(&cGZWinTextEditAt248::Vf84)},
    {"cGZWinTextEdit_vf118", 0x95149c, 32, 0x702efccf, MemberAddress(&cGZWinTextEdit::Vf118)},
};
REGISTER_HOOKS(kHooks);

} // namespace
