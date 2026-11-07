#include "hook/hooks.h"
#include "sg/cSkinExtension.h"

namespace {

// The COM director's factory for class 0xfca0e8b3: every skin extension is ours from here (its other bodies are reached
// only through the vtables our objects replace; db/status/cSkinExtension.jsonl).
const HookSpec kHooks[] = {
    {"nGZSceneGraph_cSkinExtension_Create", 0xa97720, 32, 0x58e88a79,
     reinterpret_cast<void *>(&nGZSceneGraph::cSkinExtension::Create)},
    {"nGZSceneGraph_NextSkinExtension", 0xaea060, 32, 0xbb0b92f2, reinterpret_cast<void *>(&nGZSceneGraph::NextSkinExtension)},
};
REGISTER_HOOKS(kHooks);

} // namespace
