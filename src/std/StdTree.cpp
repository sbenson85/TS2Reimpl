#include "std/StdTree.h"

#include <map>
#include <new>

// Each function follows the disassembly step by step (see the RVAs in StdTree.h). Stores happen in
// the original's order where that is visible (out-parameters, the node's fields before rebalancing).

namespace {

// Rotation used by InsertAt: the pivot's right child (leftRotation) or left child moves up.
void Rotate(StdTreeNode *head, StdTreeNode *pivot, bool leftRotation)
{
    StdTreeNode *x = leftRotation ? pivot->right : pivot->left;
    StdTreeNode *inner = leftRotation ? x->left : x->right;
    if (leftRotation) pivot->right = inner;
    else pivot->left = inner;
    if (!inner->isNil) inner->parent = pivot;

    StdTreeNode *grand = pivot->parent;
    x->parent = grand;
    if (pivot == head->parent) head->parent = x;
    else if (pivot == grand->left) grand->left = x;
    else grand->right = x;

    if (leftRotation) x->left = pivot;
    else x->right = pivot;
    pivot->parent = x;
}

} // namespace

StdTreeBound *StdTreeHead::FindLowerBound(StdTreeBound *out, const uint32_t *key)
{
    StdTreeNode *head = this->head;
    StdTreeNode *node = head->parent;
    out->node = node;
    out->wentLeft = 0;
    out->bound = head;
    if (node->isNil) return out;

    uint32_t wanted = *key;
    while (!node->isNil) {
        out->node = node;
        out->wentLeft = 0;
        if (node->key >= wanted) { // unsigned, as the original's jae
            out->bound = node;
            out->wentLeft = 1;
            node = node->left;
        } else {
            node = node->right;
        }
    }
    return out;
}

StdTreeBound *StdTreeHead::FindLowerBoundSigned(StdTreeBound *out, const int32_t *key)
{
    StdTreeNode *head = this->head;
    StdTreeNode *node = head->parent;
    out->node = node;
    out->wentLeft = 0;
    out->bound = head;
    if (node->isNil) return out;

    int32_t wanted = *key;
    while (!node->isNil) {
        out->node = node;
        out->wentLeft = 0;
        if (static_cast<int32_t>(node->key) >= wanted) { // signed, as the original's jge
            out->bound = node;
            out->wentLeft = 1;
            node = node->left;
        } else {
            node = node->right;
        }
    }
    return out;
}

StdTreeBound *StdTreeHead::FindLowerBoundKey4(StdTreeBound *out, const uint32_t *key)
{
    StdTreeNode *head = this->head;
    StdTreeNode *node = head->parent;
    out->node = node;
    out->wentLeft = 0;
    out->bound = head;
    if (node->isNil) return out;

    while (!node->isNil) {
        out->node = node;
        // The node's key words are at +0x10..+0x1f, so the key words start at word 4 of the node.
        const uint32_t *w = reinterpret_cast<const uint32_t *>(node) + 4;
        bool nodeLess;
        if (w[0] != key[0]) nodeLess = w[0] < key[0];
        else if (w[1] != key[1]) nodeLess = w[1] < key[1];
        else if (w[3] != key[3]) nodeLess = w[3] < key[3];
        else nodeLess = w[2] < key[2];
        if (!nodeLess) {
            out->bound = node;
            out->wentLeft = 1;
            node = node->left;
        } else {
            out->wentLeft = 0;
            node = node->right;
        }
    }
    return out;
}

StdTreeNode *StdTreeHead::Find(const uint32_t *key)
{
    // The map object is laid out as std::map (head pointer, then size), so the library's find runs
    // on it directly. Its iterator is the node pointer; end() is the head.
    static_assert(sizeof(std::map<uint32_t, uint32_t>) == 8, "std::map layout");
    auto *map = reinterpret_cast<std::map<uint32_t, uint32_t> *>(this);
    return reinterpret_cast<StdTreeNode *>(map->find(*key)._Ptr);
}

StdTreeNode *StdTreeHead::InsertAt(StdTreeNode *where, uint32_t addLeft, StdTreeNode *node)
{
    StdTreeNode *head = this->head;
    ++size;
    node->parent = where;

    if (where == head) { // empty tree: the node is root, leftmost and rightmost, and black
        head->left = node;
        head->parent = node;
        head->right = node;
        node->color = 1;
        return node;
    }

    if (addLeft != 0) {
        where->left = node;
        if (where == head->left) head->left = node;
    } else {
        where->right = node;
        if (where == head->right) head->right = node;
    }

    // Red-black fixup. Entered only when the parent is red (black parents need nothing); a red
    // node's parent is never the root, so the grandparent is a real node. The head is the nil
    // node and is black, which ends the loop when the root itself was recoloured red.
    StdTreeNode *x = node;
    while (x->parent->color == 0) {
        StdTreeNode *p = x->parent;
        StdTreeNode *g = p->parent;
        bool parentIsLeft = p == g->left;
        StdTreeNode *uncle = parentIsLeft ? g->right : g->left;
        if (uncle->color == 0) {
            p->color = 1;
            uncle->color = 1;
            g->color = 0;
            x = g;
            continue;
        }
        if (parentIsLeft) {
            if (x == p->right) { // inside child: rotate it up first
                Rotate(head,p, true);
                x = p;
            }
            x->parent->color = 1;
            x->parent->parent->color = 0;
            Rotate(head,x->parent->parent, false);
        } else {
            if (x == p->left) {
                Rotate(head,p, false);
                x = p;
            }
            x->parent->color = 1;
            x->parent->parent->color = 0;
            Rotate(head,x->parent->parent, true);
        }
        break;
    }
    head->parent->color = 1;
    return node;
}

void StdTreeHead::EraseSubtree(void *alloc, StdTreeNode *node)
{
    (void)alloc;
    while (!node->isNil) {
        EraseSubtree(alloc, node->right);
        StdTreeNode *dead = node;
        node = node->left;
        ::operator delete(dead, sizeof(StdTreeNode));
    }
}

void StdTreeHead::Tidy(void *alloc)
{
    EraseSubtree(alloc, head->parent);
    ::operator delete(head, sizeof(StdTreeNode));
}
