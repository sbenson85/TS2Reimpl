#include "hook/hooks.h"
#include "sg/cNodeCloneContext.h"

namespace {

const HookSpec kHooks[] = {
    {"nGZSceneGraph_cCompositionTreeNodeImpl_CloneInto", 0xa384f0, 32, 0xdf275749, &nGZSceneGraph::CloneCompositionInto},
};
REGISTER_HOOKS(kHooks);

} // namespace
