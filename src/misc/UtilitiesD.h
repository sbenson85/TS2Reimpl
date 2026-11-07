#pragma once
#include <cstdint>

// Reimplementations for the UtilitiesD group (RVAs are 1.18.0.187). Each class carries only the
// layout this group touches; the gaps are bytes the original code never reads for these functions.

// Finds the child of `window` with id `id` through window slot 45 (kind 0x8810). If there is one, its
// slot 6 runs when the low byte of `enabled` is nonzero, slot 7 when it is zero. The child is then
// released through its slot 2 (the original releases whatever the lookup stored, found or not).
// Returns 1 when a child was found, else 0 (al).
uint8_t __cdecl FindChildAndSetEnabled(void *window, uint32_t id, uint32_t enabled); // 0x9351fd

// Origin-relative position: the three origin words at +0x24, +0x28, +0x2c.
class cUtilitiesDOrigin {
public:
    // Writes src[0] - origin.x, src[1] - origin.y, src[2] - origin.z to the three out words, in that
    // order and with the reads and writes interleaved as the original does, so aliasing matches.
    void SubtractOrigin(const int32_t *src, int32_t *outX, int32_t *outY, int32_t *outZ); // 0xd13780

    uint8_t mUnused[0x24];
    int32_t mOriginX;
    int32_t mOriginY;
    int32_t mOriginZ;
};

// Node of an MSVC std::_Tree for std::map<int32_t, uint32_t> (_Tree_node layout): left at +0, parent
// at +4, right at +8, isnil byte at +0xd, key at +0x10 and value at +0x14.
struct IntTreeNode {
    IntTreeNode *left;
    IntTreeNode *parent;
    IntTreeNode *right;
    uint8_t color;
    uint8_t isNil;
    uint8_t pad[2];
    int32_t key;
    uint32_t value;
};

// Result of the lower-bound walk (0x5f921a): the last node visited, the flag for whether that step went
// to the candidate branch, and the lower-bound candidate (the head when there is none).
struct IntTreeFindResult {
    IntTreeNode *last;
    uint32_t flag;
    IntTreeNode *bound;
};

// Tree header (_Tree): the head node at +0 (its parent is the root), the size at +4.
class IntTree {
public:
    // Lower bound by key (0x5f921a, thiscall, returns `out`): standard descent where a node with key >=
    // the argument becomes the candidate and the walk goes left, otherwise right, until the head.
    IntTreeFindResult *LowerBound(IntTreeFindResult *out, const int32_t *key) const;

    // 0x5f91ea: the lower-bound node when its key equals `key`, else the head.
    IntTreeNode *FindNode(const int32_t *key) const;

    IntTreeNode *mHead;
    uint32_t mSize;
};

// Owner of a map at +0x100 (0x93d6eb).
class cUtilitiesDTreeOwner {
public:
    // Value stored under `key` (thiscall, one stack argument), or 0 when the key is absent.
    uint32_t ValueForKey(int32_t key) const; // 0x93d6eb

    uint8_t mUnused[0x100];
    IntTree mTree;
};

// 0x5b7da4 (stdcall, two arguments): returns 0 for the head (isnil set), else whether *key >= node->key.
uint8_t __stdcall KeyNotLessThanNode(const IntTreeNode *node, const int32_t *key);
