#pragma once
// nGZSceneGraph's pre- and postorder tree iterators (lane OP, scenegraph N0 part 3 step 4): what every composition node's
// slots 43 / 44 hand out. The node family's forwarders (shared 0xa37380 / 0xa37360, the viewer node's 0xa6a5e0 / 0xa6a5c0,
// the nTSSG object view's 0x259bf0 / 0x259bc0) call the composition Impl's builders 0xa37fd0 (slot 43, the const
// iterators) and 0xa37e40 (slot 44); thiscall on the Impl (not read), ret 0x10: (out, node, order, filter). Each builds
// an end iterator and a begin iterator over the node from its class's pool, and fills out with {a clone of begin, begin,
// end}, each counted; order 0 is preorder, anything else postorder; a null filter means "every node".
//
// The classes (eight in the game, four instantiations of two levels):
//   cCompositionTree{Pre,Post}orderIteratorT<cIGZForwardIterator<(const) cICompositionTreeNode *>> (0x9c bytes; vtables
//     0xe68c48 / 0xe68c90 pre, 0xe68cd8 / 0xe68d20 post): the walk. Clone (0xa37570 / 0xa375d0 / 0xa374b0 / 0xa37510)
//     makes one of these on the heap (operator new 0x9c, the copy constructors 0xa35f50 / 0xa363e0 / 0xa353d0 /
//     0xa35990); their deleting destructors (0xa37000 / 0xa37030 / 0xa36fa0 / 0xa36fd0) free with the sized delete.
//   nGZSceneGraph::c(Const)CompositionTree{Pre,Post}orderIterator: the same object from a pool (the builders' blocks,
//     cRZFastFixedAllocator2<0x9c> 0x10f5f54 pre / 0x10f5f60 const pre / 0x10f5f6c post / 0x10f5f78 const post; inline
//     allocations 0xa36c60 / 0xa36cc0 / 0xa36c30 / 0xa36c90), whose deleting destructors (0xa37110 / 0xa37190 /
//     0xa370d0 / 0xa37150) give the block back to that pool (0xa36d70 / 0xa36e70 / 0xa36cf0 / 0xa36df0). A pooled
//     iterator's Clone is a heap iterator of the template class; no block crosses between pool and heap.
//
// Layout (both levels): +0 vtable, +4 count, +8 the filter (cIBranchNodesStrategy; the static default 0x1069ffc when
// none is given), +0xc the depth (entries on the stack, the bottom one a null sentinel), +0x10 16 inline stack entries
// {node, cursor} (each word counted), +0x90 a std::vector of the entries past the 16th. An entry's cursor is the child
// of its node to visit next. The current node is the top entry's node; an iterator is at the end when only the sentinel
// is left (Get answers null, and it equals the end iterator).
//
// The filter's answer for a node: 0 visit it, 1 skip it but walk its children, 2 skip it and its subtree. Preorder: a
// node is visited before its children (the begin iterator steps on with Next when the root is not answered 0; it still
// walks a root answered 2's children, as the game's does). Postorder: after its children; the descent stops at a node
// without children or one answered 2, and a node is passed over when its answer is not 0.
//
// The switch: both builders are hooked. Only they store the pooled classes' vtables, and the template vtables are
// stored only by the template constructors (reached from the builders) and by Clone's copy constructors (reached through
// those vtables), so once the builders are ours no game iterator exists and every body above is in effect through them.
// The slots IsEqual 0xa37760, Get 0xa37700 and Next 0xa38a80 (pre) / 0xa38840 (post) are shared by the eight vtables
// and nothing else. Library-shaped helpers of the entry vector (0xa36ef0 the entry's assignment, 0xa39250 push_back,
// 0xa35110 the reallocating insert) are our toolchain's std::vector over the same element: its copy and destruction
// order are the game's.
#include "gz/cIGZUnknown.h"
#include "sg/SGNodeInterfaces.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nGZSceneGraph {

// nGZSceneGraph::cIBranchNodesStrategy: the iterators' filter (2 slots; the default cBranchNodesNoFilter answers 0,
// cBranchNodesIIDFilter 0xaeacb0 1 for a node without an interface, cBranchNodesMaskBitAndIIDFilter 0xaeacd0 2 for a
// node with a flag).
class cIBranchNodesStrategy {
public:
    static constexpr uint32_t kVisit = 0;      // the node is visited
    static constexpr uint32_t kSkipNode = 1;   // passed over; its children are still walked
    static constexpr uint32_t kSkipBranch = 2; // passed over with its subtree
    virtual uint32_t Classify(const cICompositionTreeNode *node) = 0; // 0 (ret 4)
    virtual ~cIBranchNodesStrategy() = default;                        // 1: the deleting destructor
};

// The filter an iterator keeps when it is given none (ours; the game's is its static 0x1069ffc).
cIBranchNodesStrategy *DefaultBranchFilter();

// cIGZIterator / cIGZForwardIterator<(const) cICompositionTreeNode *, cGZElementByValue<...>>: slots 3-6 (the deleting
// destructor is slot 7). The const and non-const interfaces have the same slots.
class cICompositionTreeNodeIterator : public cIGZUnknown {
public:
    virtual cICompositionTreeNodeIterator *Clone() = 0;                    // 3: a heap copy at the same place (count 0)
    virtual bool IsEqual(const cICompositionTreeNodeIterator *other) = 0;  // 4: the same current node
    virtual cICompositionTreeNode *Get() = 0;                              // 5: the current node (null at the end)
    virtual void Next() = 0;                                               // 6
};

// Slots 43 / 44's result (by hidden pointer): a cursor (a clone of begin) and the walk's two ends, each counted.
struct cCompositionTreeNodeRange {
    cICompositionTreeNodeIterator *mCurrent;
    cICompositionTreeNodeIterator *mBegin;
    cICompositionTreeNodeIterator *mEnd;
};
static_assert(sizeof(cCompositionTreeNodeRange) == 0xc, "cCompositionTreeNodeRange size");

// A counted node pointer (the game's cRZAutoRefCount<cICompositionTreeNode>): assignment counts the new node, stores it,
// then releases the old one, and does nothing for the same pointer. No move: a vector of entries copies them.
class cTreeNodeRef {
public:
    cTreeNodeRef() = default;
    explicit cTreeNodeRef(cICompositionTreeNode *node) : mNode(node)
    {
        if (mNode) mNode->AddRef();
    }
    cTreeNodeRef(const cTreeNodeRef &other) : cTreeNodeRef(other.mNode) {}
    cTreeNodeRef &operator=(const cTreeNodeRef &other)
    {
        Assign(other.mNode);
        return *this;
    }
    ~cTreeNodeRef()
    {
        if (mNode) mNode->Release();
    }
    void Assign(cICompositionTreeNode *node)
    {
        cICompositionTreeNode *const old = mNode;
        if (node == old) return;
        if (node) node->AddRef();
        mNode = node;
        if (old) old->Release();
    }
    cICompositionTreeNode *Get() const { return mNode; }

private:
    cICompositionTreeNode *mNode = nullptr;
};
static_assert(sizeof(cTreeNodeRef) == 4, "cTreeNodeRef size");

// A stack entry: a node and the child of it to visit next.
struct cTreeIteratorEntry {
    cTreeNodeRef mNode;
    cTreeNodeRef mCursor;
};
static_assert(sizeof(cTreeIteratorEntry) == 8, "cTreeIteratorEntry size");

enum class cTreeOrder { kPreorder, kPostorder };

// The template level (the game's cCompositionTree{Pre,Post}orderIteratorT): heap objects (Clone's).
template <cTreeOrder Order, bool kConst> class cCompositionTreeIteratorT : public cICompositionTreeNodeIterator {
public:
    static constexpr int32_t kInline = 16;

    cCompositionTreeIteratorT();                                                       // the end (0xa362f0 ...)
    cCompositionTreeIteratorT(cICompositionTreeNode *root, cIBranchNodesStrategy *filter); // the begin (0xa36100 ...)
    cCompositionTreeIteratorT(const cCompositionTreeIteratorT &other);                 // Clone's (0xa35f50 ...)
    cCompositionTreeIteratorT &operator=(const cCompositionTreeIteratorT &) = delete;
    virtual ~cCompositionTreeIteratorT() = default; // slot 7 (0xa36ab0 ...: the vector, then the inline entries)

    bool QueryInterface(uint32_t riid, void **out) override; // shared 0x178b20: riid 1 only
    uint32_t AddRef() override;                              // shared 0x148fb0
    uint32_t Release() override;                             // shared 0x1a84c0: deletes at 0
    cICompositionTreeNodeIterator *Clone() override;
    bool IsEqual(const cICompositionTreeNodeIterator *other) override; // shared 0xa37760
    cICompositionTreeNode *Get() override;                             // shared 0xa37700
    void Next() override; // shared 0xa38a80 (preorder) / 0xa38840 (postorder)

protected:
    cTreeIteratorEntry &Top() { return mDepth <= kInline ? mStack[mDepth - 1] : mDeeper.back(); }
    void Push(const cTreeIteratorEntry &entry);
    void PushNode(cICompositionTreeNode *node); // {node, its first child}
    void Pop();
    void NextPreorder();
    void NextPostorder();

    uint32_t mRefCount = 0;                // +0x04
    cIBranchNodesStrategy *mFilter;        // +0x08
    int32_t mDepth = 0;                    // +0x0c
    cTreeIteratorEntry mStack[kInline];    // +0x10
    std::vector<cTreeIteratorEntry> mDeeper; // +0x90: entries 16 and up
};

// The pooled level (the builders' objects): the same walk, the block from and back to the class's pool.
template <cTreeOrder Order, bool kConst>
class cCompositionTreeIterator final : public cCompositionTreeIteratorT<Order, kConst> {
public:
    static constexpr uint32_t kPool = Order == cTreeOrder::kPreorder ? (kConst ? 0x10f5f60 : 0x10f5f54)
                                                                     : (kConst ? 0x10f5f78 : 0x10f5f6c);
    using cCompositionTreeIteratorT<Order, kConst>::cCompositionTreeIteratorT;
    static void *operator new(size_t) noexcept;
    static void operator delete(void *block) noexcept;
};

using cCompositionTreePreorderIterator = cCompositionTreeIterator<cTreeOrder::kPreorder, false>;
using cConstCompositionTreePreorderIterator = cCompositionTreeIterator<cTreeOrder::kPreorder, true>;
using cCompositionTreePostorderIterator = cCompositionTreeIterator<cTreeOrder::kPostorder, false>;
using cConstCompositionTreePostorderIterator = cCompositionTreeIterator<cTreeOrder::kPostorder, true>;

// 0xa37fd0 (slot 43, the const iterators) and 0xa37e40 (slot 44): thiscall on the composition Impl (not read), ret 0x10;
// returns out. The hooks' entry points.
cCompositionTreeNodeRange *__fastcall MakeConstTreeIterators(void *impl, void *edx, cCompositionTreeNodeRange *out,
                                                             cICompositionTreeNode *node, uint32_t order,
                                                             cIBranchNodesStrategy *filter);
cCompositionTreeNodeRange *__fastcall MakeTreeIterators(void *impl, void *edx, cCompositionTreeNodeRange *out,
                                                        cICompositionTreeNode *node, uint32_t order,
                                                        cIBranchNodesStrategy *filter);

} // namespace nGZSceneGraph
