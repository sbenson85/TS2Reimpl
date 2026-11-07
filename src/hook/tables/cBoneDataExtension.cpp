#include "hook/hooks.h"
#include "sg/cBoneDataExtension.h"

namespace {

// The COM director's factory for class 0xe9075bc5: every bone-data extension is ours from here (its other bodies are
// reached only through the vtables our objects replace; db/status/cBoneDataExtension.jsonl).
const HookSpec kHooks[] = {
    {"nGZSceneGraph_cBoneDataExtension_Create", 0xa98470, 32, 0xc60dcbb7,
     reinterpret_cast<void *>(&nGZSceneGraph::cBoneDataExtension::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
