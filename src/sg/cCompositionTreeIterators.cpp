// nGZSceneGraph's pre- and postorder tree iterators (lane OP). Layout, the classes and the switch: the header.
#include "sg/cCompositionTreeIterators.h"

#include "game/game.h"
#include "game/owned.h"
#include "rz/RZFastFixedAllocators.h"
#include "rz/RZFixedAllocator.h"

// The pooled iterators' pools (cCompositionTreeIterator::kPool): preorder, const preorder, postorder, const postorder.
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x9c>, 10f5f54);
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x9c>, 10f5f60);
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x9c>, 10f5f6c);
TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x9c>, 10f5f78);

namespace nGZSceneGraph {

namespace {

template <cTreeOrder Order, bool kConst> rz::cRZFastFixedAllocator2<0x9c> &TreeIteratorPool()
{
    if constexpr (Order == cTreeOrder::kPreorder) return kConst ? ts2_rva_10f5f60 : ts2_rva_10f5f54;
    else return kConst ? ts2_rva_10f5f78 : ts2_rva_10f5f6c;
}

// The default filter (the game's anonymous-namespace cBranchNodesNoFilter, its static at 0x1069ffc): every node visited.
class cBranchNodesNoFilter final : public cIBranchNodesStrategy {
public:
    constexpr cBranchNodesNoFilter() = default;
    uint32_t Classify(const cICompositionTreeNode *) override { return kVisit; }
};
constinit cBranchNodesNoFilter gNoFilter;

} // namespace

cIBranchNodesStrategy *DefaultBranchFilter() { return &gNoFilter; }

template <cTreeOrder Order, bool kConst> cCompositionTreeIteratorT<Order, kConst>::cCompositionTreeIteratorT()
    : mFilter(&gNoFilter)
{
    Push(cTreeIteratorEntry{});
}

template <cTreeOrder Order, bool kConst>
cCompositionTreeIteratorT<Order, kConst>::cCompositionTreeIteratorT(cICompositionTreeNode *root,
                                                                    cIBranchNodesStrategy *filter)
    : mFilter(filter)
{
    if constexpr (Order == cTreeOrder::kPreorder) {
        Push(cTreeIteratorEntry{});
        PushNode(root);
        if (!mFilter) mFilter = &gNoFilter;
        // A root answered 1 or 2 is passed over; Next walks its children either way.
        if (mFilter->Classify(root) != cIBranchNodesStrategy::kVisit) NextPreorder();
    } else {
        if (!mFilter) mFilter = &gNoFilter;
        Push(cTreeIteratorEntry{});
        PushNode(root);
        // Down the first children to the first leaf (or a node answered 2).
        while (Top().mNode.Get()->HasChildren()) {
            if (mFilter->Classify(Top().mNode.Get()) == cIBranchNodesStrategy::kSkipBranch) break;
            PushNode(Top().mNode.Get()->GetFirstChild());
        }
        if (mDepth > 1 && mFilter->Classify(Top().mNode.Get()) != cIBranchNodesStrategy::kVisit) NextPostorder();
    }
}

template <cTreeOrder Order, bool kConst>
cCompositionTreeIteratorT<Order, kConst>::cCompositionTreeIteratorT(const cCompositionTreeIteratorT &other)
    : mFilter(other.mFilter), mDepth(other.mDepth), mDeeper(other.mDeeper)
{
    const int32_t inlineCount = mDepth > kInline ? kInline : mDepth;
    for (int32_t i = 0; i < inlineCount; ++i) mStack[i] = other.mStack[i];
}

template <cTreeOrder Order, bool kConst>
bool cCompositionTreeIteratorT<Order, kConst>::QueryInterface(uint32_t riid, void **out)
{
    if (!out || riid != 1) return false;
    *out = this;
    AddRef();
    return true;
}

template <cTreeOrder Order, bool kConst> uint32_t cCompositionTreeIteratorT<Order, kConst>::AddRef() { return ++mRefCount; }

template <cTreeOrder Order, bool kConst> uint32_t cCompositionTreeIteratorT<Order, kConst>::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

template <cTreeOrder Order, bool kConst>
cICompositionTreeNodeIterator *cCompositionTreeIteratorT<Order, kConst>::Clone()
{
    return new cCompositionTreeIteratorT(*this);
}

template <cTreeOrder Order, bool kConst>
bool cCompositionTreeIteratorT<Order, kConst>::IsEqual(const cICompositionTreeNodeIterator *other)
{
    // Every iterator of the family has this layout; the other's current node read as this class reads its own.
    auto *const that = static_cast<cCompositionTreeIteratorT *>(const_cast<cICompositionTreeNodeIterator *>(other));
    return Top().mNode.Get() == that->Top().mNode.Get();
}

template <cTreeOrder Order, bool kConst> cICompositionTreeNode *cCompositionTreeIteratorT<Order, kConst>::Get()
{
    return Top().mNode.Get();
}

template <cTreeOrder Order, bool kConst> void cCompositionTreeIteratorT<Order, kConst>::Next()
{
    if constexpr (Order == cTreeOrder::kPreorder)
        NextPreorder();
    else
        NextPostorder();
}

template <cTreeOrder Order, bool kConst>
void cCompositionTreeIteratorT<Order, kConst>::Push(const cTreeIteratorEntry &entry)
{
    if (mDepth < kInline)
        mStack[mDepth] = entry;
    else
        mDeeper.push_back(entry);
    ++mDepth;
}

template <cTreeOrder Order, bool kConst> void cCompositionTreeIteratorT<Order, kConst>::PushNode(cICompositionTreeNode *node)
{
    // The node is counted before its first child is asked for.
    const cTreeIteratorEntry entry{cTreeNodeRef(node), cTreeNodeRef(node ? node->GetFirstChild() : nullptr)};
    Push(entry);
}

template <cTreeOrder Order, bool kConst> void cCompositionTreeIteratorT<Order, kConst>::Pop()
{
    --mDepth;
    if (mDepth < kInline) {
        mStack[mDepth].mNode.Assign(nullptr);
        mStack[mDepth].mCursor.Assign(nullptr);
    } else {
        mDeeper.pop_back();
    }
}

template <cTreeOrder Order, bool kConst> void cCompositionTreeIteratorT<Order, kConst>::NextPreorder()
{
    cICompositionTreeNode *last = nullptr; // the node pushed last
    do {
        while (cICompositionTreeNode *const child = Top().mCursor.Get()) {
            // The cursor moves on before the child is looked at (its count drops with it; the entry below counts it).
            Top().mCursor.Assign(child->GetNextSibling());
            if (mFilter->Classify(child) != cIBranchNodesStrategy::kSkipBranch) {
                PushNode(child);
                last = child;
            }
            // Stop on a node to visit; a node answered 1 is passed over and its children walked (a branch answered 2
            // asks again about the last node pushed).
            if (mDepth < 2 || (last && mFilter->Classify(last) != cIBranchNodesStrategy::kSkipNode)) return;
        }
        Pop();
    } while (mDepth != 1);
}

template <cTreeOrder Order, bool kConst> void cCompositionTreeIteratorT<Order, kConst>::NextPostorder()
{
    do {
        Pop();
        if (mDepth == 1) return;
        // The parent's cursor moves past the child just finished, then down that sibling's first children.
        if (cICompositionTreeNode *const cursor = Top().mCursor.Get()) Top().mCursor.Assign(cursor->GetNextSibling());
        if (Top().mCursor.Get()) {
            cICompositionTreeNode *node;
            do {
                node = Top().mCursor.Get();
                PushNode(node);
            } while (node->HasChildren() && mFilter->Classify(node) != cIBranchNodesStrategy::kSkipBranch);
        }
        if (mDepth < 2) return;
    } while (mFilter->Classify(Top().mNode.Get()) != cIBranchNodesStrategy::kVisit);
}

template <cTreeOrder Order, bool kConst> void *cCompositionTreeIterator<Order, kConst>::operator new(size_t) noexcept
{
    return reinterpret_cast<RZFixedAllocatorBase *>(&TreeIteratorPool<Order, kConst>())->Alloc();
}

template <cTreeOrder Order, bool kConst> void cCompositionTreeIterator<Order, kConst>::operator delete(void *block) noexcept
{
    reinterpret_cast<RZFastFixed0x9c *>(&TreeIteratorPool<Order, kConst>())->Free(block);
}

template class cCompositionTreeIteratorT<cTreeOrder::kPreorder, false>;
template class cCompositionTreeIteratorT<cTreeOrder::kPreorder, true>;
template class cCompositionTreeIteratorT<cTreeOrder::kPostorder, false>;
template class cCompositionTreeIteratorT<cTreeOrder::kPostorder, true>;
template class cCompositionTreeIterator<cTreeOrder::kPreorder, false>;
template class cCompositionTreeIterator<cTreeOrder::kPreorder, true>;
template class cCompositionTreeIterator<cTreeOrder::kPostorder, false>;
template class cCompositionTreeIterator<cTreeOrder::kPostorder, true>;

static_assert(sizeof(cCompositionTreeIteratorT<cTreeOrder::kPreorder, true>) == 0x9c, "tree iterator size");
static_assert(sizeof(cConstCompositionTreePreorderIterator) == 0x9c, "pooled tree iterator size");
static_assert(sizeof(std::vector<cTreeIteratorEntry>) == 0xc, "the entry vector: three words at +0x90");

namespace {

// The builders' shared body: the end, then the begin over the node (both from the class's pool), out = {a clone of
// begin, begin, end}, each counted.
template <class Iterator>
cCompositionTreeNodeRange *BuildRange(cCompositionTreeNodeRange *out, cICompositionTreeNode *node,
                                      cIBranchNodesStrategy *filter)
{
    Iterator *const end = new Iterator;
    Iterator *const begin = new Iterator(node, filter);
    out->mCurrent = begin->Clone();
    if (out->mCurrent) out->mCurrent->AddRef();
    out->mBegin = begin;
    begin->AddRef();
    out->mEnd = end;
    if (end) end->AddRef();
    return out;
}

} // namespace

cCompositionTreeNodeRange *__fastcall MakeConstTreeIterators(void *, void *, cCompositionTreeNodeRange *out,
                                                             cICompositionTreeNode *node, uint32_t order,
                                                             cIBranchNodesStrategy *filter)
{
    if (order == 0) return BuildRange<cConstCompositionTreePreorderIterator>(out, node, filter);
    return BuildRange<cConstCompositionTreePostorderIterator>(out, node, filter);
}

cCompositionTreeNodeRange *__fastcall MakeTreeIterators(void *, void *, cCompositionTreeNodeRange *out,
                                                        cICompositionTreeNode *node, uint32_t order,
                                                        cIBranchNodesStrategy *filter)
{
    if (order == 0) return BuildRange<cCompositionTreePreorderIterator>(out, node, filter);
    return BuildRange<cCompositionTreePostorderIterator>(out, node, filter);
}

} // namespace nGZSceneGraph
