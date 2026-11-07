#include "hook/hooks.h"
#include "sg/cNullArrayAttributeEntryIterator.h"

namespace {

const HookSpec kHooks[] = {
    {"nGZSceneGraph_cCompositionTreeNodeImpl_MakeEmptyAttributeEntryRange", 0xa37d80, 32, 0x5485eb1f, &nGZSceneGraph::MakeEmptyAttributeEntryRange},
};
REGISTER_HOOKS(kHooks);

} // namespace
