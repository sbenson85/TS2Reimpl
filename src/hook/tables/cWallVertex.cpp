#include "hook/hooks.h"
#include "world/cWallVertex.h"

namespace {

using nTSWorld::cWallVertexAdapters;

// The one-word slots 0, 4, 5, 6 and 7 (0x1489e0, 0x147a30, 0x142840, 0x148ba0, 0x149d00) are shared with unrelated classes
// and under 12 bytes: ours only in our vtable.
const HookSpec kHooks[] = {
    {"nTSWorld::cWallVertex::cWallVertex", 0x81fd90, 32, 0xe7890caa, reinterpret_cast<void *>(&cWallVertexAdapters::ConstructAt)},
    {"nTSWorld::cWallVertex::cWallVertex", 0x81fdd0, 32, 0x3a56e676, reinterpret_cast<void *>(&cWallVertexAdapters::Construct)},
    {"nTSWorld::cWallVertex::~cWallVertex", 0x81fe10, 7, 0x878b30d4, reinterpret_cast<void *>(&cWallVertexAdapters::DestructBody)},
    {"nTSWorld::cWallVertex::vf9", 0x81fe20, 32, 0x82ad4bdd, reinterpret_cast<void *>(&cWallVertexAdapters::DeletingDestructor)},
    {"nTSWorld::cWallVertex::vf2", 0x81fe40, 32, 0x42549912, reinterpret_cast<void *>(&cWallVertexAdapters::AddEdge)},
    {"nTSWorld::cWallVertex::vf8", 0x81fe90, 32, 0xe8c9d6ba, reinterpret_cast<void *>(&cWallVertexAdapters::EdgeForAngle)},
    {"nTSWorld::cWallVertex::vf3", 0x81fef0, 32, 0x9050d77b, reinterpret_cast<void *>(&cWallVertexAdapters::RemoveEdge)},
    {"nTSWorld::cWallVertex::vf1", 0x81ff30, 22, 0x6882ac60, reinterpret_cast<void *>(&cWallVertexAdapters::SetPosition)},
};
REGISTER_HOOKS(kHooks);

} // namespace
