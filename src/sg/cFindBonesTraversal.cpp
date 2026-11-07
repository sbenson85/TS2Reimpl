// nGZSceneGraph::cFindBonesTraversal (lane QR). Layout and the hook decision: src/sg/cFindBonesTraversal.h.
#include "sg/cFindBonesTraversal.h"

namespace nGZSceneGraph {

// 0xa95d30: new and the constructor, answered as the cIGZUnknown (the original answers the raw block, null if the
// allocation fails).
cIGZUnknown *__cdecl cFindBonesTraversal::Create()
{
    cFindBonesTraversal *const traversal = new cFindBonesTraversal;
    return traversal;
}

// 0xa95d90 (ret 8): a null out answers false; kIid answers this, counted; anything else is the template's (0xa95d60).
bool cFindBonesTraversal::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid != cIFindBonesTraversal::kIid) return cCompositionTreeTraversalT<cIFindBonesTraversal>::QueryInterface(iid, out);
    *out = static_cast<cIGZUnknown *>(this);
    AddRef();
    return true;
}

// 0xa95dd0 (ret 4): the node's children range (slot 42, the cursor, a start and the end); for each child up to the end,
// unless the filter is not negative and the child has that flag bit set (slot 48), slot 3 traverses it; the cursor
// moves on through the child's GetNextSibling (slot 38). The answer is the cursor where it stopped (the end).
uint32_t cFindBonesTraversal::Visit(cICompositionTreeNode *node)
{
    const cChildRange range = node->GetChildren();
    cICompositionTreeNode *child = range.mCurrent;
    if (child == range.mEnd) return reinterpret_cast<uintptr_t>(child);
    do {
        if (GetFilter() < 0 || !child->TestFlag(uint32_t(GetFilter()))) Traverse(child);
        child = child->GetNextSibling();
    } while (child != range.mEnd);
    return reinterpret_cast<uintptr_t>(child);
}

// 0xa95ec0 (ret 4): a node with a tag (or with the flag on) is added to the list, counted (the original makes a counted
// temporary first, the vector copies it, the temporary is released); then slot 4 visits the node's children.
uint32_t cFindBonesTraversal::VisitTransform(cICompositionTreeNode *node)
{
    if (node->GetTag() != nullptr || mCollectUntagged) {
        const cNodeRef held(node);
        mNodes.push_back(held);
    }
    return Visit(node);
}

// 0xa95cc0: releases each collected node in order, then the list is empty (the storage is kept).
void cFindBonesTraversal::Clear() { mNodes.clear(); }

} // namespace nGZSceneGraph
