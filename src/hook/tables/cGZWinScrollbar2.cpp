#include "hook/hooks.h"
#include "ui/cGZWinScrollbar2.h"

namespace {

const HookSpec kHooks[] = {
    {"cGZWinScrollbar2_Construct", 0x9596cd, 32, 0x98a03a53, MemberAddress(&cGZWinScrollbar2::Construct)},
    {"cGZWinScrollbar2_Destruct", 0x959799, 32, 0xee2315bc, MemberAddress(&cGZWinScrollbar2::Destruct)},
    {"cGZWinScrollbar2_DeletingDestructor", 0x9597ec, 32, 0x511269ce, MemberAddress(&cGZWinScrollbar2::DeletingDestructor)},
    {"cGZWinScrollbar2_QueryInterface", 0x95ae54, 32, 0xabbee803, MemberAddress(&cGZWinScrollbar2::QueryInterface)},
    {"cGZWinScrollbar2_DoMessage", 0x959b69, 32, 0x7bd276a7, MemberAddress(&cGZWinScrollbar2::DoMessage)},
    {"cGZWinScrollbar2_Activate", 0x95ad1d, 32, 0x4e584954, MemberAddress(&cGZWinScrollbar2::Activate)},
    {"cGZWinScrollbar2_Teardown", 0x95b0c3, 32, 0x08eaf5e5, MemberAddress(&cGZWinScrollbar2::Teardown)},
    {"cGZWinScrollbar2_SetArea", 0x95ae8d, 32, 0xc71ea211, MemberAddress(&cGZWinScrollbar2::SetArea)},
    {"cGZWinScrollbar2_Draw", 0x959f6c, 32, 0xdd39c89a, MemberAddress(&cGZWinScrollbar2::Draw)},
    {"cGZWinScrollbar2_OnMouseDown", 0x959c88, 32, 0xf5ab0847, MemberAddress(&cGZWinScrollbar2::OnMouseDown)},
    {"cGZWinScrollbar2_OnMouseUp", 0x959ee7, 32, 0x3c8e823f, MemberAddress(&cGZWinScrollbar2::OnMouseUp)},
    {"cGZWinScrollbar2_OnMouseMove", 0x959de2, 32, 0x05a8fd6b, MemberAddress(&cGZWinScrollbar2::OnMouseMove)},
    {"cGZWinScrollbar2_OnMouseExit", 0x959d96, 32, 0x9f5c4ede, MemberAddress(&cGZWinScrollbar2::OnMouseExit)},
    {"cGZWinScrollbar2_MeasureParts", 0x95980e, 32, 0xdaf48af9, MemberAddress(&cGZWinScrollbar2::MeasureParts)},
    {"cGZWinScrollbar2_PlaceThumb", 0x959a77, 32, 0x94c9d0e2, MemberAddress(&cGZWinScrollbar2::PlaceThumb)},
    {"cGZWinScrollbar2_HitTest", 0x95ad49, 32, 0x68963e28, MemberAddress(&cGZWinScrollbar2::HitTest)},
    {"cGZWinScrollbar2_RepeatTick", 0x95b10e, 32, 0x54e550d9, reinterpret_cast<void *>(&cGZWinScrollbar2::RepeatTick)},
    {"cGZWinScrollbar2Face_SetOrientation", 0x95afb6, 32, 0xf22e20b5, MemberAddress(&cGZWinScrollbar2Face::SetOrientation)},
    {"cGZWinScrollbar2Face_GetOrientation", 0x95ace4, 15, 0x3b38bbff, MemberAddress(&cGZWinScrollbar2Face::GetOrientation)},
    {"cGZWinScrollbar2Face_SetValue", 0x95b015, 32, 0x3c8fdcb2, MemberAddress(&cGZWinScrollbar2Face::SetValue)},
    {"cGZWinScrollbar2Face_GetValue", 0x95ad01, 14, 0x8c5549b7, MemberAddress(&cGZWinScrollbar2Face::GetValue)},
    {"cGZWinScrollbar2Face_SetMin", 0x95af83, 32, 0x03bc549c, MemberAddress(&cGZWinScrollbar2Face::SetMin)},
    {"cGZWinScrollbar2Face_SetMax", 0x95af50, 32, 0x1a4c7a14, MemberAddress(&cGZWinScrollbar2Face::SetMax)},
    {"cGZWinScrollbar2Face_GetMax", 0x95ac9f, 14, 0x5fb05202, MemberAddress(&cGZWinScrollbar2Face::GetMax)},
    {"cGZWinScrollbar2Face_SetPageSize", 0x95b089, 32, 0xeefb47a3, MemberAddress(&cGZWinScrollbar2Face::SetPageSize)},
    {"cGZWinScrollbar2Face_SetPageOverlap", 0x95afdb, 19, 0x915ff86d, MemberAddress(&cGZWinScrollbar2Face::SetPageOverlap)},
    {"cGZWinScrollbar2Face_SetLineStep", 0x95af3d, 19, 0xe69d3816, MemberAddress(&cGZWinScrollbar2Face::SetLineStep)},
    {"cGZWinScrollbar2Face_SetArrowImage", 0x95aeef, 32, 0x34cdc201, MemberAddress(&cGZWinScrollbar2Face::SetArrowImage)},
    {"cGZWinScrollbar2Face_SetTrackImage", 0x95af16, 32, 0xaed7295f, MemberAddress(&cGZWinScrollbar2Face::SetTrackImage)},
    {"cGZWinScrollbar2Face_SetThumbImage", 0x95afee, 32, 0x2164c848, MemberAddress(&cGZWinScrollbar2Face::SetThumbImage)},
    {"cGZWinScrollbar2Face_GetMinSize", 0x95acad, 32, 0xc7d94f86, MemberAddress(&cGZWinScrollbar2Face::GetMinSize)},
};
REGISTER_HOOKS(kHooks);

} // namespace
