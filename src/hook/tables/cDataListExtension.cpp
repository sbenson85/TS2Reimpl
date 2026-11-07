#include "hook/hooks.h"
#include "sg/cDataListExtension.h"

namespace {

// The COM director's factory for class 0x6a836d56: every data-list extension is ours from here (its other bodies are
// reached only through the vtables our objects replace; db/status/cDataListExtension.jsonl).
const HookSpec kHooks[] = {
    {"nGZSceneGraph_cDataListExtension_Create", 0xa990b0, 32, 0x3e0d61f9,
     reinterpret_cast<void *>(&nGZSceneGraph::cDataListExtension::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
