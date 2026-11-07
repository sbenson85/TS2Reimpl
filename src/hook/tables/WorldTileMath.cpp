#include "hook/hooks.h"
#include "world/WorldTileMath.h"

namespace {

const HookSpec kHooks[] = {
    {"FUN_a07dcb90", 0x7dcb90, 32, 0x050fbb81, reinterpret_cast<void *>(&worldmath::TileBytesToPosition)},
    {"WorldUtils_TileBytesToFloats", 0x7dcbd0, 32, 0x5c04285f, reinterpret_cast<void *>(&worldmath::TileBytesToFloats)},
    {"CellToWorldPosition", 0x7dcc00, 32, 0xe65819c7, reinterpret_cast<void *>(&worldmath::FixedToPosition)},
    {"WorldUtils_Fixed16ToFloats", 0x7dcc90, 32, 0x97d9ab09, reinterpret_cast<void *>(&worldmath::Fixed16ToFloats)},
    {"FUN_a07dce00", 0x7dce00, 32, 0xa768ac0e, reinterpret_cast<void *>(&worldmath::FacingQuaternion)},
    {"FUN_a07dcee0", 0x7dcee0, 32, 0x943ab3ec, reinterpret_cast<void *>(&worldmath::FacingAngle)},
    {"FUN_a07dcf10", 0x7dcf10, 25, 0x59e43f4e, reinterpret_cast<void *>(&worldmath::FacingToNotch)},
    {"FUN_a07dcf30", 0x7dcf30, 32, 0xfdd28c01, reinterpret_cast<void *>(&worldmath::AngleToNotch)},
    {"FUN_a07dcfe0", 0x7dcfe0, 32, 0x4d41b602, reinterpret_cast<void *>(&worldmath::AngleToFacing)},
    {"FUN_a07dd090", 0x7dd090, 24, 0xdbc093f5, reinterpret_cast<void *>(&worldmath::FacingPtrToNotch)},
    {"FUN_a07dd0b0", 0x7dd0b0, 28, 0x618ff956, reinterpret_cast<void *>(&worldmath::TileToBytes)},
    {"FUN_a07dd120", 0x7dd120, 32, 0xaae23734, reinterpret_cast<void *>(&worldmath::FloatsToFixed16)},
    {"FUN_a07dd160", 0x7dd160, 32, 0xcf1edbc8, reinterpret_cast<void *>(&worldmath::PositionAtGround)},
    {"FUN_a07dd1b0", 0x7dd1b0, 32, 0x7206c9b6, reinterpret_cast<void *>(&worldmath::HeightAtLevel)},
    {"FUN_a07dd1e0", 0x7dd1e0, 32, 0x8fdd3cae, reinterpret_cast<void *>(&worldmath::NeighbourTile)},
    {"FUN_a07dd250", 0x7dd250, 32, 0xb6f5a4a2, reinterpret_cast<void *>(&worldmath::StepQuadrantTile)},
    {"FUN_a07dd470", 0x7dd470, 32, 0xac91a20b, reinterpret_cast<void *>(&worldmath::SnapToQuarters)},
    {"FUN_a07dd500", 0x7dd500, 32, 0xd6cc357b, reinterpret_cast<void *>(&worldmath::QuadrantCentre)},
    {"FUN_a07dd580", 0x7dd580, 32, 0xa0baa3dd, reinterpret_cast<void *>(&worldmath::QuadrantCentreOnGround)},
    {"WorldUtils_QuadrantOffset", 0x7dd5b0, 32, 0xf767feda, reinterpret_cast<void *>(&worldmath::QuadrantOffset)},
    {"FUN_a07dd660", 0x7dd660, 32, 0x7417db6c, reinterpret_cast<void *>(&worldmath::TileCentreOnGround)},
    {"FUN_a07dd690", 0x7dd690, 32, 0x88953c83, reinterpret_cast<void *>(&worldmath::PositionToTileCentre)},
    {"WorldUtils_PositionToTileQuadrant", 0x7dd6c0, 32, 0x05c8cbc1, reinterpret_cast<void *>(&worldmath::PositionToTileQuadrant)},
    {"FUN_a07dd800", 0x7dd800, 32, 0xc7ee210a, reinterpret_cast<void *>(&worldmath::IsCornerQuadrant)},
    {"FUN_a07dd840", 0x7dd840, 31, 0xef168180, reinterpret_cast<void *>(&worldmath::IsSideQuadrant)},
    {"FUN_a07dd870", 0x7dd870, 32, 0x8baa1ddd, reinterpret_cast<void *>(&worldmath::RoundPositionDown)},
    {"FUN_a07dd8f0", 0x7dd8f0, 32, 0x109ee095, reinterpret_cast<void *>(&worldmath::RoundPositionToTile)},
    {"FUN_a07dd960", 0x7dd960, 32, 0xd87f77be, reinterpret_cast<void *>(&worldmath::OppositeQuadrant)},
    {"WorldUtils_PositionToTile", 0x7dd9d0, 32, 0x0e6a7c8d, reinterpret_cast<void *>(&worldmath::PositionToTile)},
    {"FUN_a07dda60", 0x7dda60, 32, 0x63a83021, reinterpret_cast<void *>(&worldmath::CornerPosition)},
    {"FUN_a07dda90", 0x7dda90, 32, 0xdc81962f, reinterpret_cast<void *>(&worldmath::CornerTile)},
    {"FUN_a07ddb40", 0x7ddb40, 32, 0xe3a5e1ed, reinterpret_cast<void *>(&worldmath::CompassToQuadrant)},
    {"FUN_a07ddb80", 0x7ddb80, 32, 0x09e99452, reinterpret_cast<void *>(&worldmath::QuadrantOfPoint)},
    {"FUN_a07ddc10", 0x7ddc10, 32, 0x0aa60cb2, reinterpret_cast<void *>(&worldmath::QuadrantOfPositionXY)},
    {"FUN_a07ddd30", 0x7ddd30, 32, 0x92f53f94, reinterpret_cast<void *>(&worldmath::WithTileValue)},
    {"WorldUtils_WithGroundHeight", 0x7dddc0, 32, 0x6251d011, reinterpret_cast<void *>(&worldmath::WithGroundHeight)},
    {"FUN_a07dddf0", 0x7dddf0, 32, 0x167b6cf8, reinterpret_cast<void *>(&worldmath::RoundPositionToTileB)},
    {"FUN_a07dde50", 0x7dde50, 32, 0x5a6f79cb, reinterpret_cast<void *>(&worldmath::TileToFloats)},
    {"FUN_a07dde80", 0x7dde80, 32, 0x85944d4a, MemberAddress(&worldmath::KindedTile::Set)},
};
REGISTER_HOOKS(kHooks);

} // namespace
