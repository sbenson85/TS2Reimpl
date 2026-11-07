#include "hook/hooks.h"
#include "sg/cDynamicGeometryRect.h"

namespace {

using A = nGZSceneGraph::cDynamicGeometryRectAdapters;

// The constructor (inline at three sites), the destructor, and slots 7, 8 and 9 (the deleting destructor): every rect the
// game makes is ours, and the other slots are folded bodies reached through our vtable (db/status/cDynamicGeometryRect.jsonl).
const HookSpec kHooks[] = {
    {"nGZSceneGraph_cDynamicGeometryRect_Construct", 0xa43ec0, 32, 0xf78d0113, reinterpret_cast<void *>(&A::Construct)},
    {"nGZSceneGraph_cDynamicGeometryRect_Destruct", 0xa440f0, 32, 0x4ae4ca90, reinterpret_cast<void *>(&A::Destruct)},
    {"nGZSceneGraph_cDynamicGeometryRect_DeletingDestructor", 0xa44150, 31, 0x4562c64a, reinterpret_cast<void *>(&A::DeletingDestructor)},
    {"nGZSceneGraph_cDynamicGeometryRect_Begin", 0xa44180, 31, 0xd4434ed1, reinterpret_cast<void *>(&A::Begin)},
    {"nGZSceneGraph_cDynamicGeometryRect_Render", 0xa441b0, 32, 0xc07fc7c9, reinterpret_cast<void *>(&A::Render)},
};
REGISTER_HOOKS(kHooks);

} // namespace
