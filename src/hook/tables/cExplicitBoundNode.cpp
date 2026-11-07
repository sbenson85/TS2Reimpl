#include "hook/hooks.h"
#include "sg/cExplicitBoundNode.h"

namespace {

const HookSpec kHooks[] = {
    {"FUN_a0a4bc30", 0xa4bc30, 32, 0x65749e14, reinterpret_cast<void *>(&nGZSceneGraph::cExplicitBoundNode::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
