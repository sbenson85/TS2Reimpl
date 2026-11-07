#pragma once
#include <cstdint>

// Lookups in hash tables of the game's MSVC 14.40 STL (stdext::hash_map style: a std::list of
// nodes plus a bucket vector). Each function is a separate instantiation for one key type in the
// original, so each is a separate member here. Layout shared by all of them:
//   +0x04  list head (sentinel node, not an element)
//   +0x0c  bucket array: 8-byte {first, last} pairs, one per bucket
//   +0x18  bucket mask: the bucket of a hash is (hash & mask)
// List nodes are std::list nodes: next at +0, prev at +4, key at +8. A bucket's nodes are contiguous
// in the list from first to last; an empty bucket has last == head.
//
// The walk: start at last and step towards first (through prev), comparing keys. Results are
// written through the first argument, a pair of node pointers (see StdHashFindResult). All these
// functions are __thiscall with three stack arguments (ret 0xc): out, key pointer, hash.

struct StdHashNode {
    StdHashNode *next; // +0
    StdHashNode *prev; // +4
};

struct StdHashBucket {
    StdHashNode *first;
    StdHashNode *last;
};

// Result pair written through `out`. Found: where = the node after the match, node = the match.
// Not found: where = the bucket's first node (the list head for an empty bucket), node = null.
// The layout and the values are certain; the names describe what the callers can use them for.
struct StdHashFindResult {
    StdHashNode *where;
    StdHashNode *node;
};

struct StdHashTable {
    uint32_t mUnknown00;
    StdHashNode *mHead;       // +0x04
    uint32_t mUnknown08;
    StdHashBucket *mBuckets;  // +0x0c
    uint32_t mUnknown10[2];
    uint32_t mMask;           // +0x18

    // 0x15ac20: key is 4 bytes.
    StdHashFindResult *Find32(StdHashFindResult *out, const void *key, uint32_t hash);
    // 0x1f10b0: key is 8 bytes.
    StdHashFindResult *Find64(StdHashFindResult *out, const void *key, uint32_t hash);
    // 0x2c43b0 and 0x4eba70 (same bytes compared): key is 12 bytes.
    StdHashFindResult *Find96(StdHashFindResult *out, const void *key, uint32_t hash);
    // 0x1f5570: key is 16 bytes.
    StdHashFindResult *Find128(StdHashFindResult *out, const void *key, uint32_t hash);
    // 0x1d5c20 and 0x4ebb00 (same code): key is a std::string at key+4, node string at node+0xc.
    StdHashFindResult *FindStringAt12(StdHashFindResult *out, const void *key, uint32_t hash);
    // 0xcd8b30: key is a `const char *` (key points at it), compared with node's char pointer at +8
    // as strcmp equality.
    StdHashFindResult *FindCString(StdHashFindResult *out, const void *key, uint32_t hash);
};

static_assert(sizeof(StdHashTable) == 0x1c, "StdHashTable layout");
static_assert(sizeof(StdHashBucket) == 8, "bucket layout");
