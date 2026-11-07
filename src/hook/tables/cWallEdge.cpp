#include "hook/hooks.h"
#include "world/cWallEdge.h"

namespace {

using nTSWorld::cWallEdgeAdapters;

// The folded single-word accessors (0x20daf0, 0x14a260, 0x1eeb80, 0x149c60, 0x145770, 0x149d00, 0x46390, 0x1458b0,
// 0x149cf0; slots 0-2, 4, 12-13, 28-29, 34-35) are shared with unrelated classes and under 12 bytes: ours only in our vtable.
const HookSpec kHooks[] = {
    {"nTSWorld::cWallEdge::cWallEdge", 0x820be0, 32, 0x2c0d53ca, reinterpret_cast<void *>(&cWallEdgeAdapters::Construct)},
    {"nTSWorld::cWallEdge::~cWallEdge", 0x820c10, 14, 0xd68e1026, reinterpret_cast<void *>(&cWallEdgeAdapters::DestructBody)},
    {"nTSWorld::cWallEdge::vf37", 0x820c20, 32, 0x9be755b0, reinterpret_cast<void *>(&cWallEdgeAdapters::DeletingDestructor)},
    {"nTSWorld::cWallEdge::vf3", 0x821070, 12, 0x90cf720d, reinterpret_cast<void *>(&cWallEdgeAdapters::SetDestVertex)},
    {"nTSWorld::cWallEdge::vf5", 0x821080, 10, 0xf6f51c3b, reinterpret_cast<void *>(&cWallEdgeAdapters::SetFlag)},
    {"nTSWorld::cWallEdge::vf6", 0x821050, 20, 0xfe5a5533, reinterpret_cast<void *>(&cWallEdgeAdapters::Rot)},
    {"nTSWorld::cWallEdge::vf8", 0x820cf0, 20, 0x7bba87e6, reinterpret_cast<void *>(&cWallEdgeAdapters::InvRot)},
    {"nTSWorld::cWallEdge::vf10", 0x821090, 20, 0xfc8028ed, reinterpret_cast<void *>(&cWallEdgeAdapters::Sym)},
    {"nTSWorld::cWallEdge::vf14", 0x820d90, 19, 0x492c9121, reinterpret_cast<void *>(&cWallEdgeAdapters::RotOnextRot)},
    {"nTSWorld::cWallEdge::vf15", 0x820d70, 19, 0x40773ea1, reinterpret_cast<void *>(&cWallEdgeAdapters::RotOnextRotC)},
    {"nTSWorld::cWallEdge::vf16", 0x820c60, 19, 0xa352005c, reinterpret_cast<void *>(&cWallEdgeAdapters::SymOnextSym)},
    {"nTSWorld::cWallEdge::vf17", 0x820c40, 19, 0xaa09afdc, reinterpret_cast<void *>(&cWallEdgeAdapters::SymOnextSymC)},
    {"nTSWorld::cWallEdge::vf18", 0x820ca0, 19, 0x620044e9, reinterpret_cast<void *>(&cWallEdgeAdapters::InvRotOnextInvRot)},
    {"nTSWorld::cWallEdge::vf19", 0x820c80, 19, 0x6b5beb69, reinterpret_cast<void *>(&cWallEdgeAdapters::InvRotOnextInvRotC)},
    {"nTSWorld::cWallEdge::vf20", 0x820d30, 19, 0x4a02fc77, reinterpret_cast<void *>(&cWallEdgeAdapters::InvRotOnextRot)},
    {"nTSWorld::cWallEdge::vf21", 0x820d10, 19, 0x435953f7, reinterpret_cast<void *>(&cWallEdgeAdapters::InvRotOnextRotC)},
    {"nTSWorld::cWallEdge::vf22", 0x820d60, 12, 0x80b576a1, reinterpret_cast<void *>(&cWallEdgeAdapters::OnextSym)},
    {"nTSWorld::cWallEdge::vf23", 0x820d50, 12, 0x0392bc42, reinterpret_cast<void *>(&cWallEdgeAdapters::OnextSymC)},
    {"nTSWorld::cWallEdge::vf24", 0x821010, 19, 0x612e29bf, reinterpret_cast<void *>(&cWallEdgeAdapters::RotOnextInvRot)},
    {"nTSWorld::cWallEdge::vf25", 0x820ff0, 19, 0x6875863f, reinterpret_cast<void *>(&cWallEdgeAdapters::RotOnextInvRotC)},
    {"nTSWorld::cWallEdge::vf26", 0x821040, 12, 0x3c87c469, reinterpret_cast<void *>(&cWallEdgeAdapters::SymOnext)},
    {"nTSWorld::cWallEdge::vf27", 0x821030, 12, 0xbfa00e8a, reinterpret_cast<void *>(&cWallEdgeAdapters::SymOnextC)},
    {"nTSWorld::cWallEdge::vf30", 0x820db0, 32, 0x73141ea1, reinterpret_cast<void *>(&cWallEdgeAdapters::Angle)},
    {"nTSWorld::cWallEdge::vf31", 0x820cd0, 14, 0x8828d27c, reinterpret_cast<void *>(&cWallEdgeAdapters::ReverseAngle)},
    {"nTSWorld::cWallEdge::vf32", 0x820ee0, 32, 0x73141ea1, reinterpret_cast<void *>(&cWallEdgeAdapters::AngleDegrees)},
    {"nTSWorld::cWallEdge::vf33", 0x820ce0, 15, 0x906a8b09, reinterpret_cast<void *>(&cWallEdgeAdapters::ReverseAngleDegrees)},
    {"nTSWorld::cWallEdge::vf36", 0x820cc0, 15, 0xdabc34b1, reinterpret_cast<void *>(&cWallEdgeAdapters::DestVertex)},
};
REGISTER_HOOKS(kHooks);

} // namespace
