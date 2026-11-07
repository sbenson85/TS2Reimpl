#include "hook/hooks.h"
#include "sg/cIndexedIterator.h"

namespace {

const HookSpec kHooks[] = {
    {"nGZSceneGraph_LinkedElementHeader", 0xaee030, 32, 0x95ea7429, reinterpret_cast<void *>(&nGZSceneGraph::LinkedElementHeader)},
    {"nGZSceneGraph_LinkedFloats", 0xaeddd0, 32, 0xacfa48be, reinterpret_cast<void *>(&nGZSceneGraph::LinkedFloats)},
    {"nGZSceneGraph_LinkedVectors2", 0xaee820, 32, 0xacfa48be, reinterpret_cast<void *>(&nGZSceneGraph::LinkedVectors2)},
    {"nGZSceneGraph_LinkedVectors3", 0xaee9f0, 32, 0xacfa48be, reinterpret_cast<void *>(&nGZSceneGraph::LinkedVectors3)},
    {"nGZSceneGraph_LinkedVectors4", 0xaeebc0, 32, 0xacfa48be, reinterpret_cast<void *>(&nGZSceneGraph::LinkedVectors4)},
    {"nGZSceneGraph_LinkedWords", 0xaee0f0, 32, 0xacfa48be, reinterpret_cast<void *>(&nGZSceneGraph::LinkedWords)},
};
REGISTER_HOOKS(kHooks);

} // namespace
