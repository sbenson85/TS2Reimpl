#include "hook/hooks.h"
#include "sg/cMaterialAttributes.h"

namespace {

const HookSpec kHooks[] = {
    {"nGZSceneGraph_cMaterialAttributes_Create", 0xaaa3c0, 32, 0x051a3fc6, &nGZSceneGraph::CreateMaterialAttributes},
    {"nGZSceneGraph_OpenNameCell", 0xae87d0, 32, 0xbec1ccb8, &nGZSceneGraph::OpenNameCell},
};
REGISTER_HOOKS(kHooks);

} // namespace
