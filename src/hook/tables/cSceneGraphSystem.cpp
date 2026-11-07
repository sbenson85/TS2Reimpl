#include "hook/hooks.h"
#include "sg/cSceneGraphSystem.h"

namespace {

const HookSpec kHooks[] = {
    {"nGZSceneGraph_cSceneGraphSystem_Create", 0xa4cb40, 32, 0x2cc5aa3c,
     reinterpret_cast<void *>(&nGZSceneGraph::cSceneGraphSystem::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
