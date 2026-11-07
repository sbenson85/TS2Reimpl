#include "hook/hooks.h"
#include "rz/RZSystemServiceBase.h"

// Standalone shape (docs/standalone-shape.md; lane EI). rz::cRZSystemService and rz::cRZUnknownMT are base classes: the
// game constructs the base part of every system service through 0x1b9584 / 0x1b95bf (and the cRZUnknownMT part through
// 0x1b9078, called only by those two), so the constructor hooks build that part with our constructor; the derived
// constructor then stores its own vtables. The other entries are adapters for the old entry points: the derived classes'
// destructors call 0x1b95fe / 0x1b90ab directly, and 0x1b9517 / 0x1b9544 / 0x1b912e / 0x1b961a / 0x1b9650 are slot bodies
// that the game's vtables of many classes hold. Bisect switch: the whole table (no other class depends on one entry alone).
// Folded slots (the accessors, 0x1b9640, 0x1b945c, 0x147a30, 0x144910, 0x148260) and the 8-byte thunks are never hooked
// here; ours fill our vtables.
namespace {
using MT = rz::cRZUnknownMTAdapters;
using SS = rz::cRZSystemServiceAdapters;
const HookSpec kHooks[] = {
    {"cRZUnknownMT_Construct", 0x1b9078, 16, 0x96237f27, reinterpret_cast<void *>(&MT::Construct)},
    {"cRZUnknownMT_Destruct", 0x1b90ab, 7, 0x668ba76d, reinterpret_cast<void *>(&MT::Destruct)},
    {"cRZUnknownMT_DeletingDestructor", 0x1b912e, 31, 0xed1f66c6, reinterpret_cast<void *>(&MT::DeletingDestructor)},
    {"cRZUnknownMT_Release", 0x1b9517, 32, 0x05561906, reinterpret_cast<void *>(&MT::Release)},
    {"cRZUnknownMT_RemoveRef", 0x1b9544, 26, 0x4e1fb391, reinterpret_cast<void *>(&MT::RemoveRef)},
    {"cRZSystemService_Construct", 0x1b9584, 32, 0xfabe7fa9, reinterpret_cast<void *>(&SS::Construct)},
    {"cRZSystemService_ConstructWithTickPriority", 0x1b95bf, 32, 0xa4c96e2e, reinterpret_cast<void *>(&SS::ConstructWithTickPriority)},
    {"cRZSystemService_Destruct", 0x1b95fe, 20, 0x9a99587b, reinterpret_cast<void *>(&SS::Destruct)},
    {"cRZSystemService_DeletingDestructor", 0x1b961a, 31, 0x3d3c8216, reinterpret_cast<void *>(&SS::DeletingDestructor)},
    {"cRZSystemService_QueryInterface", 0x1b9650, 32, 0xed43a632, reinterpret_cast<void *>(&SS::QueryInterface)},
};
REGISTER_HOOKS(kHooks);
} // namespace
