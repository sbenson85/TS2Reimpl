#include "hook/hooks.h"
#include "gz/cBrushes.h"

namespace {

// The brushes are made only by their COM creators (hooked: every object is ours, every slot in effect through them).
const HookSpec kHooks[] = {
    {"nGZGraphic4_cColorBrush_Create", 0x1538b0, 32, 0xf74a4d98, reinterpret_cast<void *>(&nGZGraphic4::cColorBrush::Create)},
    {"nGZGraphic4_cOutlineColorBrush_Create", 0x1538f0, 32, 0x182654bb, reinterpret_cast<void *>(&nGZGraphic4::cOutlineColorBrush::Create)},
    {"nGZGraphic4_cPatternBrush_Create", 0x153930, 32, 0x876c2578, reinterpret_cast<void *>(&nGZGraphic4::cPatternBrush::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
