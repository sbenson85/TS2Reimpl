#include "hook/hooks.h"
#include "sg/cStringPool.h"

namespace {

const HookSpec kHooks[] = {
    {"nGZSceneGraph_cStringPool_Instance", 0xadf8b0, 32, 0x50d8b1ea, reinterpret_cast<void *>(&nGZSceneGraph::cStringPool::Instance)},
    {"nGZSceneGraph_JenkinsHash", 0x98bff0, 32, 0x45ea8ad6, reinterpret_cast<void *>(&nGZSceneGraph::JenkinsHash)},
};
REGISTER_HOOKS(kHooks);

} // namespace
