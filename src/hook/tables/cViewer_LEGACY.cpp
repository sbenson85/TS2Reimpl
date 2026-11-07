#include "hook/hooks.h"
#include "sg/cViewer_LEGACY.h"

namespace {

const HookSpec kHooks[] = {
    {"FUN_a0a66010", 0xa66010, 32, 0x3dfda68d, reinterpret_cast<void *>(&nGZSceneGraph::cViewer_LEGACY::Create)},
    {"FUN_a0a64520", 0xa64520, 32, 0x5d554ca0, reinterpret_cast<void *>(&nGZSceneGraph::cViewer_LEGACY::Construct)},
    {"FUN_a0a64860", 0xa64860, 32, 0xc3c53af4, reinterpret_cast<void *>(&nGZSceneGraph::cViewer_LEGACY::Destruct)},
    {"nGZSceneGraph_cViewer_LEGACY_vf12", 0xa64c00, 32, 0x72a22186, reinterpret_cast<void *>(&nGZSceneGraph::cViewer_LEGACY::DeletingDestructor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
