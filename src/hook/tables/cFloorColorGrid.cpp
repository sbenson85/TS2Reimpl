#include "hook/hooks.h"
#include "sg/cFloorColorGrid.h"

// Floor colour grids live by value in cDiffusionManager's grid vector; LD2a's members are hooked at their RVAs.
namespace {

const HookSpec kHooks[] = {
    {"nTSSG_cFloorColorGrid_cFloorColorGrid", 0x4056d0, 32, 0xcc7adc6a, &nTSSG::cFloorColorGrid::Construct},
    {"nTSSG_cFloorColorGrid_CopyConstruct", 0x3c42c0, 32, 0x6b62109a, &nTSSG::cFloorColorGrid::CopyConstruct},
    {"nTSSG_cFloorColorGrid_Destroy", 0x4058e0, 32, 0xad223a97, &nTSSG::cFloorColorGrid::Destroy},
    {"nTSSG_cFloorColorGrid_vf0", 0x3c4870, 32, 0x7aa96af2, MemberAddress(&nTSSG::cFloorColorGrid::DeletingDestructor)},
    {"nTSSG_cFloorColorGrid_SetSize", 0x4098d0, 32, 0xcd17da1f, MemberAddress(&nTSSG::cFloorColorGrid::SetSize)},
    {"nTSSG_cFloorColorGrid_Cell", 0x409880, 16, 0x83ccf392, MemberAddress(&nTSSG::cFloorColorGrid::Cell)},
    {"nTSSG_cFloorColorGrid_Link", 0x40abd0, 16, 0xa26990e7, MemberAddress(&nTSSG::cFloorColorGrid::Link)},
    {"nTSSG_cFloorColorGrid_CellIndex", 0x40abe0, 32, 0x02bf5276, MemberAddress(&nTSSG::cFloorColorGrid::CellIndex)},
    {"nTSSG_cFloorColorGrid_SetInsideAmbientColor", 0x4098b0, 22, 0x871e0827, MemberAddress(&nTSSG::cFloorColorGrid::SetInsideAmbientColor)},
    {"nTSSG_cFloorColorGrid_SetOutsideAmbientColor", 0x409a00, 22, 0x7620e97d, MemberAddress(&nTSSG::cFloorColorGrid::SetOutsideAmbientColor)},
    {"nTSSG_cFloorColorGrid_CollectTiles", 0x405910, 32, 0x08c1cca9, MemberAddress(&nTSSG::cFloorColorGrid::CollectTiles)},
    {"nTSSG_cFloorColorGrid_CollectTilesB", 0x405a00, 32, 0xd3b50c09, MemberAddress(&nTSSG::cFloorColorGrid::CollectTilesB)},
    {"nTSSG_cFloorColorGrid_CollectTilesA", 0x405ae0, 32, 0xd3b50c09, MemberAddress(&nTSSG::cFloorColorGrid::CollectTilesA)},
    {"nTSSG_cFloorColorGrid_Reset", 0x406f40, 32, 0x30288700, MemberAddress(&nTSSG::cFloorColorGrid::Reset)},
    {"nTSSG_cFloorColorGrid_Mark", 0x405bc0, 32, 0x934ba76c, MemberAddress(&nTSSG::cFloorColorGrid::Mark)},
    // LD2b (lane 261109l): the light cells.
    {"nTSSG_cFloorColorGrid_NeighbourSample", 0x4087e0, 32, 0x00ae8a8a, MemberAddress(&nTSSG::cFloorColorGrid::NeighbourSample)},
    {"nTSSG_FloorLightHalf_ShadeQuad", 0x4080c0, 32, 0x2eb2486e, MemberAddress(&nTSSG::FloorLightHalf::ShadeQuad)},
    {"nTSSG_FloorLightHalf_ShadeTriangle", 0x4072a0, 32, 0x01096465, MemberAddress(&nTSSG::FloorLightHalf::ShadeTriangle)},
    {"nTSSG_FloorLightCell_Shade", 0x4085e0, 32, 0xea1af7e0, MemberAddress(&nTSSG::FloorLightCell::Shade)},
    {"nTSSG_cFloorColorGrid_PrepareLight", 0x4077a0, 32, 0x9b18d2d7, MemberAddress(&nTSSG::cFloorColorGrid::PrepareLight)},
    {"nTSSG_cFloorColorGrid_ShadeLights", 0x4060d0, 32, 0x8baf4643, MemberAddress(&nTSSG::cFloorColorGrid::ShadeLights)},
    {"nTSSG_cFloorColorGrid_ShadeChangedLights", 0x406300, 32, 0x8baf4643, MemberAddress(&nTSSG::cFloorColorGrid::ShadeChangedLights)},
    {"nTSSG_cFloorColorGrid_TakeBlobs", 0x4065b0, 32, 0x9b00c305, MemberAddress(&nTSSG::cFloorColorGrid::TakeBlobs)},
    {"nTSSG_FloorBlob_Key", 0x3c3a50, 23, 0xd248ec38, MemberAddress(&nTSSG::FloorBlob::Key)},
    // LD2c (lane 261109q): the step, GetColor and the rebuild's helpers.
    {"nTSSG_cFloorColorGrid_Spread", 0x409080, 32, 0xedaf60fe, &nTSSG::cFloorColorGrid::Spread},
    {"nTSSG_cFloorColorGrid_ResetColors", 0x408860, 32, 0xe2758747, MemberAddress(&nTSSG::cFloorColorGrid::ResetColors)},
    {"nTSSG_cFloorColorGrid_LightReaches", 0x406ff0, 32, 0x60c109ad, MemberAddress(&nTSSG::cFloorColorGrid::LightReaches)},
    {"nTSSG_cFloorColorGrid_ApplyLight", 0x4089a0, 32, 0xd5617ece, MemberAddress(&nTSSG::cFloorColorGrid::ApplyLight)},
    {"nTSSG_cFloorColorGrid_Propagate", 0x409170, 32, 0xdb53b51a, MemberAddress(&nTSSG::cFloorColorGrid::Propagate)},
    {"nTSSG_cFloorColorGrid_Step", 0x405d70, 32, 0xaf525cc6, MemberAddress(&nTSSG::cFloorColorGrid::Step)},
    {"nTSSG_cFloorColorGrid_GetColor", 0x408e30, 32, 0x08199262, MemberAddress(&nTSSG::cFloorColorGrid::GetColor)},
    {"nTSSG_cFloorColorGrid_LevelAt", 0x408790, 32, 0x45bf1a85, &nTSSG::cFloorColorGrid::LevelAt},
    {"nTSSG_cFloorColorGrid_MarkFloorTiles", 0x40a9e0, 32, 0xc69f83ec, MemberAddress(&nTSSG::cFloorColorGrid::MarkFloorTiles)},
    {"nTSSG_cFloorColorGrid_LinkSamples", 0x40a610, 32, 0x5e3ef0b2, &nTSSG::cFloorColorGrid::LinkSamples},
    {"nTSSG_cFloorColorGrid_PruneCorner", 0x40a790, 32, 0xac61cc9b, MemberAddress(&nTSSG::cFloorColorGrid::PruneCorner)},
    {"nTSSG_cFloorColorGrid_Rebuild", 0x40ac20, 32, 0x9187c4ab, MemberAddress(&nTSSG::cFloorColorGrid::Rebuild)},
    {"nTSSG_cFloorColorGrid_BuildCells", 0x409a20, 32, 0xd40c9482, MemberAddress(&nTSSG::cFloorColorGrid::BuildCells)},
};
REGISTER_HOOKS(kHooks);

} // namespace
