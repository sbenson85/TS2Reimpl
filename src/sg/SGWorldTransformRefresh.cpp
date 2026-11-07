// nGZSceneGraph's world-transform refresh (lane OP). What it does: the header.
#include "sg/SGWorldTransformRefresh.h"

#include <cstddef>
#include <vector>

namespace nGZSceneGraph {

namespace {

constexpr uint32_t kStaleFlag = 12;   // the cached world transform is stale
constexpr uint32_t kContentFlag = 4;  // the node takes its transform after the walk (with the limit list)
constexpr uint32_t kNoLimit = 0x7fffffff;

// A list record: a node (roots, content nodes) or a limit (the limit list), then a world transform. Slot 60 sees the
// transform of a content node's record and the limit list's array, so the layout is the game's.
struct cTransformRecord {
    uint32_t mWord;
    cRigidTransform mTransform;
};
static_assert(sizeof(cTransformRecord) == 0x20, "cTransformRecord size");
static_assert(offsetof(cTransformRecord, mTransform) == 4, "slot 60 gets record + 4");

struct cWorkLists {
    std::vector<cTransformRecord> mLimits;   // handed to slot 60 (the game's starts with room for 50)
    std::vector<cTransformRecord> mContent;  // content nodes waiting for slot 60
    std::vector<cTransformRecord> mRoots;    // subtrees still to walk
    cWorkLists() { mLimits.reserve(50); }
};

cWorkLists &Lists()
{
    static cWorkLists lists;
    return lists;
}

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
cICompositionTreeNode *NodeOf(uint32_t word) { return reinterpret_cast<cICompositionTreeNode *>(uintptr_t(word)); }

// 0xa39a10: below `node`, whose world transform is *world.
void RefreshBelow(cICompositionTreeNode *node, const cRigidTransform *world, cWorkLists &lists)
{
    if (node->TestFlag(kContentFlag))
        lists.mContent.push_back({Word(node), *world});
    else
        node->OnContentChanged(Word(world), 0, 0);

    const cChildRange children = node->GetChildrenConst();
    cRigidTransform childWorld; // one for all the children, as the game's (slot 57 writes it whole)
    for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd; child = child->GetNextSiblingConst()) {
        child->ToParent1c(&childWorld, world);
        const uint32_t limit = child->GetLimit55();
        if (limit == 0) {
            lists.mRoots.push_back({Word(child), childWorld});
            continue;
        }
        if (limit != kNoLimit) lists.mLimits.push_back({limit, childWorld});
        RefreshBelow(child, &childWorld, lists);
    }
}

} // namespace

void RefreshWorldTransforms(cICompositionTreeNode *node)
{
    for (cICompositionTreeNode *parent = node->GetParentConst(); parent && parent->TestFlag(kStaleFlag);
         parent = parent->GetParentConst())
        node = parent;

    cWorkLists &lists = Lists();
    cRigidTransform parentWorld = {{0, 0, 0, 0, 0, 0, 1.0f}};
    if (cICompositionTreeNode *const parent = node->GetParentConst()) parentWorld = parent->GetWorldTransform();
    cRigidTransform world;
    node->ToParent1c(&world, &parentWorld);
    lists.mRoots.push_back({Word(node), world});

    // Every list is read through its size each time: slot 60 and the walk may add to them (or, re-entering, empty them).
    for (size_t i = 0; i < lists.mRoots.size(); ++i) {
        cTransformRecord root = lists.mRoots[i];
        cICompositionTreeNode *const rootNode = NodeOf(root.mWord);
        if (rootNode->GetLimit55() == 0) lists.mLimits.push_back({0, root.mTransform});
        RefreshBelow(rootNode, &root.mTransform, lists);
        for (size_t j = 0; j < lists.mContent.size(); ++j) {
            cTransformRecord &content = lists.mContent[j];
            NodeOf(content.mWord)
                ->OnContentChanged(Word(&content.mTransform), Word(lists.mLimits.data()),
                                   static_cast<uint32_t>(lists.mLimits.size()));
        }
        lists.mLimits.clear();
        lists.mContent.clear();
    }
    lists.mRoots.clear();
}

} // namespace nGZSceneGraph
