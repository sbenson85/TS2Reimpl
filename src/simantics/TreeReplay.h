#pragma once
#include "std/StdHash.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// The record/replay table of the SimAntics interpreter (cTSTreeSim keeps one at +0x4c, TreeSim.h). Trees whose flag byte
// has bit 3 are "replayable": when one returns, the interpreter records which branch it took (and what it left in the
// object's argument words) under a key made from the tree and its arguments; when the same call comes up again while the
// sim's record switch is on, the recorded answer is delivered without running the tree.
//
// The table is the old MSVC hash_map the game was built with: a std::list of nodes (the list head is a sentinel node;
// next at +0, prev at +4) and a bucket vector holding a {first, last} node pair per bucket, the nodes of a bucket
// being contiguous in the list. The lookup (StdHashTable::Find64, std/StdHash.h) is shared with the other 8-byte-key
// tables, so the layout of the first 0x20 bytes is the one that class describes.

// The key of a call: 8 bytes, compared bytewise by the lookup.
struct TreeReplayKey {
    uint32_t mTreeIdentity; // +0 what the owner's slot 12 says about the called tree id (equal values: the same tree)
    uint32_t mCallSite;     // +4 (opcode << 16) | stack object id, each sign-extended from 16 bits before the OR
                            //    (so an object id with bit 15 set turns the whole upper half on: ORIGINAL-BUG, kept)

    // The hash the table uses: the sum of the two words.
    uint32_t Hash() const { return mTreeIdentity + mCallSite; }
};

// One recorded call of a tree with the arguments it was made with (0x1c bytes).
struct TreeReplayRow {
    std::vector<uint16_t> mArgs;  // +0x00 the argument words of the call
    uint32_t mResult;             // +0x0c the branch the tree took: 1 true, 0 false
    std::vector<uint16_t> mTemps; // +0x10 the object's eight argument words after the call, when the tree's cache flags
                                  //       ask for them (empty otherwise)
};

static_assert(sizeof(TreeReplayRow) == 0x1c, "TreeReplayRow layout");
static_assert(offsetof(TreeReplayRow, mResult) == 0xc, "TreeReplayRow layout");
static_assert(offsetof(TreeReplayRow, mTemps) == 0x10, "TreeReplayRow layout");

// A list node of the table: the key and the rows recorded for it, oldest first (0x20 bytes).
struct TreeReplayNode : StdHashNode {
    TreeReplayKey mKey;                // +0x08
    std::vector<TreeReplayRow> mRows;  // +0x10
    uint32_t mUnused1c;                // +0x1c never written

    static constexpr uint32_t kSize = 0x20;
};

static_assert(sizeof(TreeReplayNode) == 0x20, "TreeReplayNode layout");
static_assert(offsetof(TreeReplayNode, mKey) == 8, "TreeReplayNode layout");
static_assert(offsetof(TreeReplayNode, mRows) == 0x10, "TreeReplayNode layout");

// The {first, last} node pair of one bucket (an empty bucket has first == the list head).
struct TreeReplayBucket {
    TreeReplayNode *mFirst;
    TreeReplayNode *mLast;
};

// The std::list inside the table (at +4 of it): the sentinel node and the number of nodes.
struct TreeReplayList {
    TreeReplayNode *mHead; // +0x00 the sentinel: next is the first node, prev the last; both itself when empty
    uint32_t mSize;        // +0x04 number of nodes

    // The list destructor (0x575860, __thiscall on the list): destroys every node (and its rows), then the sentinel.
    void FreeNodes();
    // Destroys every node but keeps the sentinel (still pointing into the freed nodes: the caller relinks it).
    void FreeElements();
};

static_assert(sizeof(TreeReplayList) == 8, "TreeReplayList layout");

class TreeReplayMap {
public:
    static constexpr uint32_t kInitialBuckets = 8;
    static constexpr uint32_t kMaxSize = 0x7ffffff;
    static constexpr uint32_t kMaxBuckets = 1u << 28;

    // The result of Insert (0x575280 writes it through its first argument): the node for the key and whether it is new.
    struct InsertResult {
        TreeReplayNode *mNode;
        uint8_t mInserted;
    };

    // Builds the empty table: the sentinel node pointing at itself, eight buckets (sixteen vector entries) all at the
    // sentinel (part of the cTSTreeSim constructor, 0x5755e0).
    void Construct();
    // The destructor (0x5757a0): frees the bucket storage, then the nodes and the sentinel (FreeNodes).
    void Destroy();
    // Empties the table, keeping the sentinel and the bucket vector (0x5787b0; cTSTreeSim slot 32).
    void Clear();
    // The node recorded for a key, or null.
    TreeReplayNode *Find(const TreeReplayKey &key);
    // Finds the node for a key or adds an empty one (0x575280, __thiscall ret 8). Returns `result` (its game caller
    // 0x5760c8 reads eax).
    InsertResult *Insert(InsertResult *result, const TreeReplayKey *key);
    // Grows the bucket vector to the power of two above `buckets` and re-links the nodes so equal keys sit together
    // (0x578680, __thiscall ret 4).
    void Rehash(uint32_t buckets);

    float mMaxLoadFactor;                     // +0x00 1.0: more elements per bucket than this triggers a rehash
    TreeReplayList mList;                     // +0x04 the nodes
    std::vector<TreeReplayNode *> mBuckets;  // +0x0c two entries per bucket: first, last
    uint32_t mMask;                           // +0x18 bucket count - 1
    uint32_t mBucketCount;                    // +0x1c a power of two

    TreeReplayBucket *BucketFor(uint32_t hash)
    {
        return reinterpret_cast<TreeReplayBucket *>(mBuckets.data()) + (hash & mMask);
    }

private:
    StdHashTable *AsHashTable() { return reinterpret_cast<StdHashTable *>(this); }
};

static_assert(sizeof(TreeReplayMap) == 0x20, "TreeReplayMap layout");
static_assert(offsetof(TreeReplayMap, mList) == 4, "TreeReplayMap layout");
static_assert(offsetof(TreeReplayMap, mBuckets) == 0xc, "TreeReplayMap layout");
static_assert(offsetof(TreeReplayMap, mMask) == 0x18, "TreeReplayMap layout");
