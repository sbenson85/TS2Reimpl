// nGZSceneGraph::cBoundingVolumeBuilder (lane QE): the factory is the only hook; the other bodies are in effect through
// our vtables.
#include "hook/hooks.h"
#include "sg/cBoundingVolumeBuilder.h"

namespace {

const HookSpec kHooks[] = {
    {"nGZSceneGraph_cBoundingVolumeBuilder_Create", 0xa85090, 32, 0x2320c936,
     reinterpret_cast<void *>(&nGZSceneGraph::cBoundingVolumeBuilder::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
