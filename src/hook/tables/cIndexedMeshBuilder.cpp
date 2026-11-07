#include "hook/hooks.h"
#include "sg/cIndexedMeshBuilder.h"

namespace {

const HookSpec kHooks[] = {
    {"cIndexedMeshBuilder_Create", 0xa7ffa0, 32, 0x8e91d920, reinterpret_cast<void *>(&nGZSceneGraph::cIndexedMeshBuilder::Create)},
    {"nGZSceneGraph_QuaternionFromMatrix", 0x1a7570, 32, 0x7dfeac0d, reinterpret_cast<void *>(&nGZSceneGraph::indexedmesh::QuaternionFromMatrix)},
};
REGISTER_HOOKS(kHooks);

} // namespace
