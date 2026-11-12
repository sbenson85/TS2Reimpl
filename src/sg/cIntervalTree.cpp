// nTSSG::cIntervalTree (lane 261108o, scenegraph row IT1): see the header.
#include "sg/cIntervalTree.h"

namespace nTSSG {

namespace {
// A node's maximum from its own high end and its children's maxima (the rotations' recomputation).
void RecomputeMax(cIntervalTree::Node *node)
{
    int32_t max = node->mHigh;
    if (node->mLeft && node->mLeft->mMax > max) max = node->mLeft->mMax;
    if (node->mRight && node->mRight->mMax > max) max = node->mRight->mMax;
    node->mMax = max;
}
} // namespace

cIntervalTree::Node::Node(int32_t low, int32_t high, int32_t data) : mData(data), mLow(low), mHigh(high)
{
    if (low > high) {
        mLow = high;
        mHigh = low;
    }
}

cIntervalTree::cIntervalTree(bool ownsNodes) : mOwnsNodes(ownsNodes)
{
    mRoot = new Node *;
    *mRoot = nullptr;
}

cIntervalTree::~cIntervalTree()
{
    Clear();
    ::operator delete(mRoot, sizeof(Node *));
    mRoot = nullptr;
}

void cIntervalTree::Clear()
{
    if (mOwnsNodes) DeleteSubtree(mRoot);
    *mRoot = nullptr;
}

void cIntervalTree::DeleteSubtree(Node **link)
{
    if (!*link) return;
    DeleteSubtree(&(*link)->mLeft);
    DeleteSubtree(&(*link)->mRight);
    delete *link;
    *link = nullptr;
}

void cIntervalTree::InsertUnbalanced(Node *node)
{
    node->mMax = node->mHigh;
    Node *parent = nullptr;
    for (Node *at = *mRoot; at;) {
        parent = at;
        if (at->mMax < node->mMax) at->mMax = node->mMax;
        at = node->mLow < at->mLow ? at->mLeft : at->mRight;
    }
    node->mParent = parent;
    if (!parent)
        *mRoot = node;
    else if (node->mLow < parent->mLow)
        parent->mLeft = node;
    else
        parent->mRight = node;
}

void cIntervalTree::RotateLeft(Node *node)
{
    Node *const up = node->mRight;
    node->mRight = up->mLeft;
    if (up->mLeft) up->mLeft->mParent = node;
    up->mParent = node->mParent;
    if (!node->mParent)
        *mRoot = up;
    else if (node == node->mParent->mLeft)
        node->mParent->mLeft = up;
    else
        node->mParent->mRight = up;
    up->mLeft = node;
    node->mParent = up;
    RecomputeMax(node);
    RecomputeMax(up);
}

void cIntervalTree::RotateRight(Node *node)
{
    Node *const up = node->mLeft;
    node->mLeft = up->mRight;
    if (up->mRight) up->mRight->mParent = node;
    up->mParent = node->mParent;
    if (!node->mParent)
        *mRoot = up;
    else if (node == node->mParent->mLeft)
        node->mParent->mLeft = up;
    else
        node->mParent->mRight = up;
    up->mRight = node;
    node->mParent = up;
    RecomputeMax(node);
    RecomputeMax(up);
}

void cIntervalTree::Insert(Node *node)
{
    InsertUnbalanced(node);
    node->mColour = kRed;
    // The parent of a red node is never the root (the root is black), so the grandparent exists.
    while (node != *mRoot && node->mParent->mColour == kRed) {
        Node *parent = node->mParent;
        Node *const grandparent = parent->mParent;
        if (parent == grandparent->mLeft) {
            Node *const uncle = grandparent->mRight;
            if (uncle && uncle->mColour == kRed) {
                parent->mColour = kBlack;
                uncle->mColour = kBlack;
                grandparent->mColour = kRed;
                node = grandparent;
                continue;
            }
            if (node == parent->mRight) {
                node = parent;
                RotateLeft(node);
                parent = node->mParent;
            }
            parent->mColour = kBlack;
            node->mParent->mParent->mColour = kRed;
            RotateRight(node->mParent->mParent);
        } else {
            Node *const uncle = grandparent->mLeft;
            if (uncle && uncle->mColour == kRed) {
                parent->mColour = kBlack;
                uncle->mColour = kBlack;
                grandparent->mColour = kRed;
                node = grandparent;
                continue;
            }
            if (node == parent->mLeft) {
                node = parent;
                RotateRight(node);
                parent = node->mParent;
            }
            parent->mColour = kBlack;
            node->mParent->mParent->mColour = kRed;
            RotateLeft(node->mParent->mParent);
        }
    }
    (*mRoot)->mColour = kBlack;
}

void cIntervalTree::Query(Node *node, int32_t lo, int32_t hi, NodeSet *out)
{
    if (!node) return;
    if (lo <= node->mHigh && node->mLow <= hi) out->insert(node);
    if (node->mLeft && lo <= node->mLeft->mMax) Query(node->mLeft, lo, hi, out);
    if (node->mRight && node->mLow <= hi && lo <= node->mRight->mMax) Query(node->mRight, lo, hi, out);
}

} // namespace nTSSG
