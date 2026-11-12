#include "hook/hooks.h"
#include "world/WorldHeights.h"

namespace {

const HookSpec kHooks[] = {
    {"WorldUtils_GroundHeightAt", 0x7e4680, 32, 0x6c27ee0f, reinterpret_cast<void *>(&worldheights::GroundHeightAt)},
    {"FUN_a07ddc50", 0x7ddc50, 32, 0x22fcea15, reinterpret_cast<void *>(&worldheights::HasFloorOrGroundAt)},
    {"FUN_a07ddd60", 0x7ddd60, 32, 0xab93f5d1, reinterpret_cast<void *>(&worldheights::WithLevelAboveGround)},
    {"FUN_a07ddd90", 0x7ddd90, 32, 0xbee0f9ca, reinterpret_cast<void *>(&worldheights::WithLevelOfGround)},
    {"FUN_a07e4250", 0x7e4250, 32, 0xec493c83, reinterpret_cast<void *>(&worldheights::RoomAreaInTiles)},
    {"FUN_a07e43c0", 0x7e43c0, 32, 0x929ea771, reinterpret_cast<void *>(&worldheights::RoomMostlyFlagged)},
    {"FUN_a07e4c00", 0x7e4c00, 32, 0xb27f7647, reinterpret_cast<void *>(&worldheights::SurfaceHeightAt)},
    {"FUN_a07e4ce0", 0x7e4ce0, 32, 0xfefb919a, reinterpret_cast<void *>(&worldheights::HighestSurfaceAt)},
    {"FUN_a07e4f40", 0x7e4f40, 32, 0x8823d89c, reinterpret_cast<void *>(&worldheights::ObjectQueryAt)},
    {"FUN_a07e4f90", 0x7e4f90, 32, 0x06fb6164, reinterpret_cast<void *>(&worldheights::IsRaisedObject)},
    {"FUN_a07e5170", 0x7e5170, 32, 0x1eec98ab, reinterpret_cast<void *>(&worldheights::TileClaimsObject)},
    {"FUN_a07e5330", 0x7e5330, 32, 0x7f041599, reinterpret_cast<void *>(&worldheights::AnyStructureAt)},
    {"FUN_a07e5370", 0x7e5370, 32, 0x23b8efb6, reinterpret_cast<void *>(&worldheights::ObjectHeightAt)},
    {"FUN_a07e55c0", 0x7e55c0, 32, 0x5c482cd8, reinterpret_cast<void *>(&worldheights::ObjectPositionAt)},
    {"FUN_a07e5600", 0x7e5600, 32, 0x681b0a4d, reinterpret_cast<void *>(&worldheights::TileHasStructure)},
    {"FUN_a07e56e0", 0x7e56e0, 32, 0xaea12b3b, reinterpret_cast<void *>(&worldheights::TileHasFlaggedObject)},
    {"FUN_a07e5810", 0x7e5810, 32, 0xb81d6037, reinterpret_cast<void *>(&worldheights::PartitionMaskRotated)},
    {"WorldUtils_RoofHeightAt", 0x7e59c0, 32, 0x31eb2c0a, reinterpret_cast<void *>(&worldheights::SegmentEndHeight)},
    {"FUN_a07e5b40", 0x7e5b40, 15, 0x6437ec82, reinterpret_cast<void *>(&worldheights::Lookup9)},
};
REGISTER_HOOKS(kHooks);

} // namespace
