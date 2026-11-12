#pragma once
// nTSSG::cIntervalTree (lane 261108o, scenegraph row IT1; split out of row TS1e): the interval tree the dynamic render
// strategy's dirty-rectangle drawing uses to find which display-list items a frame's dirty rectangles touch. A
// cRZUnknown (vtable 0xdc20e0, 6 slots: the cRZUnknown bodies with its own deleting destructor 0x401a70), 0x10 bytes:
// the count at +4, a heap cell holding the root at +8 (the constructor allocates it, the destructor frees it) and whether
// the tree owns (and so deletes) its nodes at +0xc.
//
// The tree is a red-black tree keyed on each interval's low end, every node keeping the largest high end in its subtree
// (the classic augmented interval tree): Insert adds a node and rebalances, Query collects every node whose interval meets
// [lo, hi]. Its one user is the strategy's member at +0x98 (DynamicStrategyTrees, src/sg/cDisplayListDynamicRenderStrategy.h),
// which keeps one tree of the rectangles' x spans and one of their y spans, with the nodes in its own vector (so its trees
// are made not owning them: 0x36e9c0 passes 0).
//
// Standalone shape (docs/standalone-shape.md): our class, our vtable. Every function is reached only from our code (the
// trees' constructor, Build 0x370b70 and Overlaps 0x373750), so all are in effect through callers; none is hooked.
#include "rz/RZUnknownBase.h"

#include <cstddef>
#include <cstdint>
#include <set>

namespace nTSSG {

class cIntervalTree : public rz::cRZUnknown {
public:
    enum Colour : uint32_t { kBlack = 0, kRed = 1 };

    // A node (0x20 bytes): an interval [mLow, mHigh] and the value it stands for.
    struct Node {
        // 0x4019d0: every word zero (the vector resize's default element).
        Node() = default;
        // 0x401980 (ret 0xc): no links, black, the interval with its ends in order (swapped when high < low), the value;
        // the subtree maximum 0 until the node is inserted.
        Node(int32_t low, int32_t high, int32_t data);

        Node *mParent = nullptr; // +0
        Node *mLeft = nullptr;   // +4
        Node *mRight = nullptr;  // +8
        int32_t mData = 0;       // +0xc the value (the strategy's rectangle number, from 1)
        uint32_t mColour = 0;    // +0x10 kRed / kBlack
        int32_t mLow = 0;        // +0x14
        int32_t mHigh = 0;       // +0x18
        int32_t mMax = 0;        // +0x1c the largest mHigh in the subtree
    };

    // The set Query fills: nodes ordered (and kept unique) by their value.
    struct ByData {
        bool operator()(const Node *a, const Node *b) const { return a->mData < b->mData; }
    };
    using NodeSet = std::set<Node *, ByData>;

    // 0x401910 (ret 4): the root cell allocated (new 4) and emptied; ownsNodes kept at +0xc.
    explicit cIntervalTree(bool ownsNodes);
    // 0x401a10: Clear, the root cell freed (the deleting destructor 0x401a70 frees the 0x10 bytes).
    ~cIntervalTree() override;

    // 0x401a90: the nodes deleted when the tree owns them (DeleteSubtree from the root), then the tree empty.
    void Clear();
    // 0x401ab0 (ret 4): the subtree at *link deleted, children first, and *link cleared.
    void DeleteSubtree(Node **link);
    // 0x401af0 (ret 4): node inserted (InsertUnbalanced), coloured red, then the red-black fix-up; the root ends black.
    void Insert(Node *node);
    // 0x401d40 (ret 0x10; recursive, its tree unused): every node in the subtree at node whose interval meets [lo, hi]
    // added to out (the bounds inclusive). The left child is visited when its maximum reaches lo, the right one when
    // node's low end is at most hi and the right maximum reaches lo.
    void Query(Node *node, int32_t lo, int32_t hi, NodeSet *out);

    // The root (the game's 0x401d30, a 6-byte body folded with cRZMemPtrIterator's slot 5: inlined).
    Node *Root() const { return *mRoot; }

    Node **mRoot = nullptr;  // +8 a heap cell holding the root
    bool mOwnsNodes = false; // +0xc

private:
    // 0x401e90 (ret 4): the plain binary-tree insert keyed on mLow (left when lower, else right), node's maximum set to
    // its high end and each node passed on the way raised to it.
    void InsertUnbalanced(Node *node);
    // 0x401bf0 (ret 4): node's right child rotated up into its place, then node's and the child's maxima recomputed.
    void RotateLeft(Node *node);
    // 0x401c90 (ret 4): node's left child rotated up into its place, then node's and the child's maxima recomputed.
    void RotateRight(Node *node);
};
static_assert(sizeof(cIntervalTree::Node) == 0x20, "cIntervalTree::Node: 0x20 bytes");
static_assert(offsetof(cIntervalTree::Node, mData) == 0xc && offsetof(cIntervalTree::Node, mColour) == 0x10 &&
              offsetof(cIntervalTree::Node, mMax) == 0x1c);
static_assert(offsetof(cIntervalTree, mRoot) == 8 && offsetof(cIntervalTree, mOwnsNodes) == 0xc);
static_assert(sizeof(cIntervalTree) == 0x10, "cIntervalTree: 0x10 bytes");

} // namespace nTSSG
