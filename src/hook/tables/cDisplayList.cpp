#include "hook/hooks.h"
#include "sg/cFrameCache.h"
#include "sg/cDisplayList.h"

namespace {

using nGZSceneGraph::cDisplayListAdapters;

const HookSpec kHooks[] = {
    // The class: the factory makes ours (its slots, the item insertion and the sorts are in effect through it).
    {"cDisplayList_Create", 0xaa0e00, 32, 0xd268b1c4, reinterpret_cast<void *>(&cDisplayListAdapters::Create)},
    // The views' frame caches (game-built objects): slot 0, and slot 3 (the destructor and Flip are ours through it).
    {"nGZSceneGraph_cFrameCache_QueryInterface", 0xaa21a0, 32, 0x9052e758, MemberAddress(&nGZSceneGraph::cFrameCache::QueryInterface)},
    {"nGZSceneGraph_cFrameCache_vf3", 0xa9e5b0, 32, 0x56c2491b, MemberAddress(&nGZSceneGraph::cFrameCache::DeletingDestructor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
