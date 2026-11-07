#include "hook/hooks.h"
#include "sg/cRenderGroupManager.h"

namespace {

const HookSpec kHooks[] = {
    {"cRenderGroupManager_Create", 0xaa37f0, 32, 0xd8188ffc, reinterpret_cast<void *>(&nGZSceneGraph::cRenderGroupManagerAdapters::Create)},
    {"RenderGroupRegistry", 0xaeb370, 32, 0xf7c74028, reinterpret_cast<void *>(&nGZSceneGraph::RenderGroupRegistry)},
};
REGISTER_HOOKS(kHooks);

} // namespace
