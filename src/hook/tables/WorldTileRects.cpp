#include "hook/hooks.h"
#include "world/WorldTileRects.h"

namespace {

const HookSpec kHooks[] = {
    {"FUN_a07ef060", 0x7ef060, 32, 0x5837c53e, reinterpret_cast<void *>(&worldrect::OffsetRect)},
    {"FUN_a07ef260", 0x7ef260, 32, 0x8bedee3f, reinterpret_cast<void *>(&worldrect::CellBetween)},
    {"FUN_a07ef3b0", 0x7ef3b0, 26, 0x3d8bf3ef, reinterpret_cast<void *>(&worldrect::CellOfRect)},
};
REGISTER_HOOKS(kHooks);

} // namespace
