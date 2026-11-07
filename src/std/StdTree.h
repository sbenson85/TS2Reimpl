#pragma once
#include <cstdint>

// std::_Tree helpers of MSVC 14.40 as compiled into the game, for a std::map<uint32_t, V> whose
// nodes are 0x2c bytes (V is 0x18 bytes, starting at +0x14). Layouts come from the disassembly:
// the node is MSVC's _Tree_node (left, parent, right, colour, isnil, value), and the map object is
// its head pointer followed by the element count. The head node is the nil node: its parent is the
// root, its left and right are the leftmost and rightmost nodes, and its colour and isnil bytes are
// black and true. Colours: 0 is red, 1 is black. Keys compare as unsigned, like std::less<uint32_t>.

struct StdTreeNode {
    StdTreeNode *left;   // +0
    StdTreeNode *parent; // +4
    StdTreeNode *right;  // +8
    uint8_t color;       // +0xc: 0 red, 1 black
    uint8_t isNil;       // +0xd
    uint8_t reserved[2]; // +0xe
    uint32_t key;        // +0x10
    uint8_t mapped[0x18]; // +0x14
};
static_assert(sizeof(StdTreeNode) == 0x2c, "std::map<uint32_t, 0x18-byte value> node size");

// Result of the lower-bound search (returned through a caller's buffer): the last node visited
// (+0), whether the last step went left (+4), and the last node whose key was not less than the
// search key (+8, the head when there is none).
struct StdTreeBound {
    StdTreeNode *node;
    uint32_t wentLeft;
    StdTreeNode *bound;
};
static_assert(sizeof(StdTreeBound) == 12, "lower-bound result");

// The map object: head node at +0, element count at +4.
struct StdTreeHead {
    StdTreeNode *head;
    uint32_t size;

    // 0x1463b0 (__thiscall, ret 8): lower-bound search from the root for a map whose nodes are
    // 0x2c bytes. Fills *out and returns out. (0x144c95 is the library's _Find_lower_bound for
    // std::map<uint32_t, uint32_t>; it is not reimplemented.)
    StdTreeBound *FindLowerBound(StdTreeBound *out, const uint32_t *key);

    // 0x144c35 (__thiscall, ret 4): std::map<uint32_t, uint32_t>::find, through the library's own
    // find (the original calls the library's _Find_lower_bound and _Lower_bound_duplicate). Returns
    // the node whose key equals *key, otherwise the head (end()).
    StdTreeNode *Find(const uint32_t *key);

    // 0x1d5d80 (__thiscall, ret 8): the same walk as FindLowerBound, but the key compares as signed
    // (the original uses jge there). Same node layout: the key is the int32 at +0x10.
    StdTreeBound *FindLowerBoundSigned(StdTreeBound *out, const int32_t *key);

    // 0x1d5d10 (__thiscall, ret 8): lower-bound walk for a map whose key is four dwords at +0x10 (the
    // node is still 0x2c bytes). Keys compare word 0, then word 1, then word 3, then word 2, each
    // unsigned, as the original's compare chain does. Same result layout as FindLowerBound.
    StdTreeBound *FindLowerBoundKey4(StdTreeBound *out, const uint32_t *key);

    // 0x148bb0 (__thiscall, ret 0xc): inserts an already-initialised node as the left (addLeft != 0)
    // or right child of where (where is the head for an empty tree), updates leftmost and rightmost,
    // rebalances, and returns the node. Increments the count. Does not write node->left/right.
    StdTreeNode *InsertAt(StdTreeNode *where, uint32_t addLeft, StdTreeNode *node);

    // 0x146370 (__thiscall, ret 8): deallocates the subtree at node in order (right subtree, then
    // the node, then left). The first argument is passed through unused by the original.
    void EraseSubtree(void *alloc, StdTreeNode *node);

    // 0x146320 (__thiscall, ret 4): erases every node, then the head. The first argument is passed
    // through unused by the original.
    void Tidy(void *alloc);
};
