#include "hook/hooks.h"
#include "world/WorldTileRects.h"

namespace {

const HookSpec kHooks[] = {
    {"FUN_a07ef060", 0x7ef060, 32, 0x5837c53e, reinterpret_cast<void *>(&worldrect::OffsetRect)},
    {"FUN_a07ef260", 0x7ef260, 32, 0x8bedee3f, reinterpret_cast<void *>(&worldrect::CellBetween)},
    {"FUN_a07ef3b0", 0x7ef3b0, 26, 0x3d8bf3ef, reinterpret_cast<void *>(&worldrect::CellOfRect)},
    {"WorldUtils_TilesOfBoxA", 0x7eec00, 32, 0x0bf1cbb6, worldrect::ReversedSegmentToQuadrants},
    {"FUN_a07eec60", 0x7eec60, 32, 0xcc55a370, worldrect::CellToQuadrants},
    {"WorldUtils_TilesOfBoxB", 0x7eed90, 32, 0x9e60ffdb, worldrect::SegmentToQuadrants},
    {"FUN_a07eedc0", 0x7eedc0, 32, 0x73842257, worldrect::IsUnitSegment},
    {"FUN_a07eee50", 0x7eee50, 32, 0x5de69855, worldrect::RotateRect},
    {"FUN_a07eef00", 0x7eef00, 32, 0xcaaf6cdd, worldrect::CanonicalSegment},
    {"FUN_a07ef0c0", 0x7ef0c0, 32, 0xb4da5397, worldrect::EdgeEndpoints},
    {"FUN_a07ef1d0", 0x7ef1d0, 32, 0x1b670b83, worldrect::CellToSegment},
};
REGISTER_HOOKS(kHooks);

} // namespace
