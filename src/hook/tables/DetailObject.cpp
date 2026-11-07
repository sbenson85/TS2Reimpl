#include "hook/hooks.h"
#include "sg/DetailObject.h"

namespace {

const HookSpec kHooks[] = {
    {"nGZSceneGraph_GetDetailObject", 0xaead80, 32, 0x67740fff, reinterpret_cast<void *>(&nGZSceneGraph::GetDetailObject)},
};
REGISTER_HOOKS(kHooks);

} // namespace
