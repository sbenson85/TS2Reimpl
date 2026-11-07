// The composition Impl's CloneInto and its clone context (lane OR). See the header.
#include "sg/cNodeCloneContext.h"

#include "sg/cCompositionTreeNodeImpl.h"

namespace nGZSceneGraph {

namespace {
constexpr uint32_t kCloneContextIid = 0xecce4c52; // nGZSceneGraph::cINodeCloneContext
constexpr uint32_t kQueuedFlag = 13;
constexpr uint32_t kStaleFlag = 12;
constexpr uint32_t kDeepClone = 1;
constexpr uint32_t kShallowClone = 2; // the node alone: no content, no children
} // namespace

static_assert(sizeof(cNodeCloneContext) == cNodeCloneContext::kSize, "cNodeCloneContext size");

cNodeCloneContext::cNodeCloneContext(cICompositionTreeNode *root, cICompositionTreeNode *rootClone) : mRoot(root)
{
    auto [entry, inserted] = mClones.try_emplace(root, rootClone);
    if (inserted && rootClone) rootClone->AddRef();
}

cNodeCloneContext::~cNodeCloneContext()
{
    for (auto &[source, clone] : mClones)
        if (clone) clone->Release();
}

bool cNodeCloneContext::QueryInterface(uint32_t iid, void **out)
{
    if (iid == kCloneContextIid) {
        *out = static_cast<cINodeCloneContext *>(this);
        return true;
    }
    return rz::cRZUnknown::QueryInterface(iid, out);
}

void cNodeCloneContext::FindClone(cIObjectGraphNode *asked, uint32_t iid, void *out)
{
    cICompositionTreeNode *const node = static_cast<cICompositionTreeNode *>(asked);
    auto [entry, inserted] = mClones.try_emplace(node, nullptr);
    if (inserted) {
        // A node met for the first time: cloned (shallow) when it lies under the root source, else remembered as null.
        for (cICompositionTreeNode *n = node; n; n = n->GetParentConst()) {
            if (n != mRoot) continue;
            cICompositionTreeNode *made = nullptr;
            node->Clone(kShallowClone, cICompositionTreeNode::kIid, reinterpret_cast<void **>(&made), nullptr);
            if (made) {
                made->AddRef();
                entry->second = made;
                made->Release();
            }
            break;
        }
    }
    if (cICompositionTreeNode *const clone = entry->second)
        clone->QueryInterface(iid, static_cast<void **>(out));
    else
        *static_cast<void **>(out) = nullptr;
}

void __fastcall CloneCompositionInto(cCompositionTreeNodeImpl *impl, void *, cICompositionTreeNode *source,
                                     cICompositionTreeNode *clone, cCompositionTreeNodeImpl *cloneImpl, uint32_t mode,
                                     cINodeCloneContext *context)
{
    impl->CopyTo(source, clone, mode, context);
    cloneImpl->mFlags = impl->mFlags;
    cloneImpl->SetFlag(kQueuedFlag, false);
    cloneImpl->SetFlag(kStaleFlag, true);
    if (mode != kDeepClone) return;

    // One context for the whole subtree: the caller's, or ours (released at the end, which frees it).
    cINodeCloneContext *const shared = context ? context : new cNodeCloneContext(source, clone);
    shared->AddRef();
    for (cICompositionTreeNode *child = impl->mFirstChild; child; child = child->GetNextSiblingConst()) {
        cICompositionTreeNode *copy = nullptr;
        child->Clone(kDeepClone, cICompositionTreeNode::kIid, reinterpret_cast<void **>(&copy), shared);
        clone->AddChild(copy);
        if (copy) copy->Release();
    }
    shared->Release();
}

} // namespace nGZSceneGraph
