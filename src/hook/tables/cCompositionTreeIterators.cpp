#include "hook/hooks.h"
#include "sg/cCompositionTreeIterators.h"

namespace {

const HookSpec kHooks[] = {
    {"nGZSceneGraph_cCompositionTreeNodeImpl_MakeConstTreeIterators", 0xa37fd0, 32, 0x8f34e504, &nGZSceneGraph::MakeConstTreeIterators},
    {"nGZSceneGraph_cCompositionTreeNodeImpl_MakeTreeIterators", 0xa37e40, 32, 0xf876c6ca, &nGZSceneGraph::MakeTreeIterators},
};
REGISTER_HOOKS(kHooks);

} // namespace
