
#include "hook/hooks.h"
#include "sg/cMaterialDefinition.h"

namespace {

// The factory (the director's for class 0x49596978): with it ours no game object carries the class's vtables, so every
// slot, the constructor and the destructors are ours through it. 0xae88d0 is the case-sensitive name cell.
const HookSpec kHooks[] = {
    {"nGZSceneGraph_cMaterialDefinition_Create", 0xaadf20, 32, 0x0efa0000,
     reinterpret_cast<void *>(&nGZSceneGraph::cMaterialDefinition::Create)},
    {"nGZSceneGraph_OpenNameCellCaseSensitive", 0xae88d0, 32, 0x93f6eee8,
     reinterpret_cast<void *>(&nGZSceneGraph::OpenNameCellCaseSensitive)},
    // GetTypeInfo: the game's Write (0xaaefc0, not ours yet) calls it directly.
    {"nGZSceneGraph_cMaterialDefinition_GetTypeInfo", 0xaaef20, 32, 0xd1d127d2,
     reinterpret_cast<void *>(&nGZSceneGraph::cMaterialDefinition::GetTypeInfoAt)},
};
REGISTER_HOOKS(kHooks);

} // namespace
