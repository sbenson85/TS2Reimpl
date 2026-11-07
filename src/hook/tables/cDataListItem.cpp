#include "hook/hooks.h"
#include "sg/cDataListItem.h"

namespace {

const HookSpec kHooks[] = {
    {"nGZSceneGraph_cDataListItem_Create", 0xac6d50, 32, 0xaa5caf17, reinterpret_cast<void *>(&nGZSceneGraph::cDataListItem::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
