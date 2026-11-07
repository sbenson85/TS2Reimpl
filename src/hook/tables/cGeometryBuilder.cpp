#include "hook/hooks.h"
#include "sg/cGeometryBuilder.h"

namespace {

using GeometryBuilderAdapters = nGZSceneGraph::cGeometryBuilderAdapters;

// nGZSceneGraph::cGeometryBuilder's base bodies (src/sg/cGeometryBuilder.h, lane QM), reached by all nine derived classes
// (four ours, five the game's) through their vtables and vtordisp thunks. QueryInterface and Release are referenced only
// from builder-family vtables and callers (docs/verification.md, the family check). Left original: Read 0x9dc310 and
// AddRef 0x2582b0 (folded across unrelated classes). Through callers: the template's Release 0xa454c0 and notify body
// 0xa45540, the event-manager getter 0xa45010, the base GetTypeInfo 0xa456d0 (only Write and the vtable reach it).
const HookSpec kHooks[] = {
    {"nGZSceneGraph_cGeometryBuilder_QueryInterface", 0xa45310, 32, 0x4105becc, reinterpret_cast<void *>(&GeometryBuilderAdapters::QueryInterface)},
    {"nGZSceneGraph_cGeometryBuilder_Release", 0xa45530, 8, 0x44fa9611, reinterpret_cast<void *>(&GeometryBuilderAdapters::Release)},
    {"nGZSceneGraph_cGeometryBuilder_vf12_5", 0xa45790, 32, 0x79c6bdcc, reinterpret_cast<void *>(&GeometryBuilderAdapters::Write)},
    {"FUN_a0a45240", 0xa45240, 32, 0x1dc4e866, reinterpret_cast<void *>(&GeometryBuilderAdapters::OnEvent)},
    {"FUN_a0a450f0", 0xa450f0, 32, 0xf006cc8c, reinterpret_cast<void *>(&GeometryBuilderAdapters::AskListeners)},
    {"FUN_a0a44fd0", 0xa44fd0, 32, 0xa408ba2e, reinterpret_cast<void *>(&GeometryBuilderAdapters::Connect)},
    {"FUN_a0a450a0", 0xa450a0, 32, 0xae63958f, reinterpret_cast<void *>(&GeometryBuilderAdapters::Disconnect)},
    {"FUN_a0a452c0", 0xa452c0, 32, 0x8ff279cf, reinterpret_cast<void *>(&GeometryBuilderAdapters::GetEventCount)},
    {"FUN_a0a44c60", 0xa44c60, 32, 0x9f4eb9f1, reinterpret_cast<void *>(&GeometryBuilderAdapters::GetEventIds)},
    {"FUN_a0a44ee0", 0xa44ee0, 32, 0x44150bbb, reinterpret_cast<void *>(&GeometryBuilderAdapters::GetEventAttribute)},
    {"FUN_a0a45660", 0xa45660, 32, 0x86e6677f, reinterpret_cast<void *>(&GeometryBuilderAdapters::NotifyListeners)},
    {"FUN_a0a456c0", 0xa456c0, 10, 0x1edb0dbf, reinterpret_cast<void *>(&GeometryBuilderAdapters::SetListener)},
    {"nGZSceneGraph_cGeometryBuilder_EventRegistry", 0xa453a0, 32, 0xe14363d8, reinterpret_cast<void *>(&nGZSceneGraph::cGeometryBuilderBehaviorTraits::RegistryFor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
