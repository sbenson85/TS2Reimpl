#include "hook/hooks.h"
#include "sg/cFindBonesTraversal.h"

namespace {

// The COM director's factory for class 0x5ca10ca7: every find-bones traversal is ours from here (its other bodies are
// reached only through the vtable our objects have; db/status/cFindBonesTraversal.jsonl).
const HookSpec kHooks[] = {
    {"nGZSceneGraph_cFindBonesTraversal_Create", 0xa95d30, 32, 0x7869da3b,
     reinterpret_cast<void *>(&nGZSceneGraph::cFindBonesTraversal::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
