#include "simantics/TreeReplay.h"

#include <algorithm>
#include <cmath>
#include <new>

// The record/replay table of the interpreter (TreeReplay.h): construction, lookup, insertion with its rehash, and the
// two ways to empty it. Written from the disassembly of the game's hash_map instantiation (0x575280 insert, 0x578680
// rehash, 0x5787b0 clear, 0x575860 / 0x5757a0 destructors); the rows and the vectors are std::vector, as in the game.

namespace {

// How the table grows when an insert pushes the load past the maximum (the heuristics of the STL the game was built
// with): at least 8 buckets, and below 512 buckets at least eight times the current count when that is enough.
constexpr uint32_t kMinGrownBuckets = 8;
constexpr uint32_t kGrowthFactorLimit = 0x200;
constexpr uint32_t kGrowthShift = 3;

bool SameKey(const TreeReplayNode *a, const TreeReplayNode *b)
{
    return a->mKey.mTreeIdentity == b->mKey.mTreeIdentity && a->mKey.mCallSite == b->mKey.mCallSite;
}

// Takes `node` out of the list and puts it back right before `where` (the pointer writes are in the order of the
// original, which matters when `where` is the node's own successor).
void MoveBefore(StdHashNode *node, StdHashNode *where)
{
    StdHashNode *oldPrev = node->prev;
    StdHashNode *oldNext = node->next;
    oldPrev->next = oldNext;
    StdHashNode *self = oldNext->prev;
    self->next = where;
    StdHashNode *whereBefore = where->prev;
    whereBefore->next = node;
    where->prev = self;
    oldNext->prev = oldPrev;
    node->prev = whereBefore;
}

TreeReplayNode *AsNode(StdHashNode *node)
{
    return static_cast<TreeReplayNode *>(node);
}

} // namespace

void TreeReplayMap::Construct()
{
    mMaxLoadFactor = 0;
    mList.mHead = nullptr;
    mList.mSize = 0;
    TreeReplayNode *head = static_cast<TreeReplayNode *>(::operator new(TreeReplayNode::kSize));
    head->next = head;
    head->prev = head;
    mList.mHead = head;
    mMask = kInitialBuckets - 1;
    mBucketCount = kInitialBuckets;
    mMaxLoadFactor = 1.0f;
    new (&mBuckets) std::vector<TreeReplayNode *>(2 * kInitialBuckets, head);
}

TreeReplayNode *TreeReplayMap::Find(const TreeReplayKey &key)
{
    StdHashFindResult found;
    AsHashTable()->Find64(&found, &key, key.Hash());
    return AsNode(found.node);
}

// Cuts the ring open after the last node and frees every node with its rows (not the sentinel).
void TreeReplayList::FreeElements()
{
    mHead->prev->next = nullptr;
    for (StdHashNode *node = mHead->next; node != nullptr;) {
        StdHashNode *next = node->next;
        AsNode(node)->mRows.~vector();
        ::operator delete(node, TreeReplayNode::kSize);
        node = next;
    }
}

// 0x575860.
void TreeReplayList::FreeNodes()
{
    FreeElements();
    ::operator delete(mHead, TreeReplayNode::kSize);
}

// 0x5757a0.
void TreeReplayMap::Destroy()
{
    std::vector<TreeReplayNode *>().swap(mBuckets);
    mList.FreeNodes();
}

// 0x5787b0.
void TreeReplayMap::Clear()
{
    if (mList.mSize == 0) return;
    mList.FreeElements();
    mList.mHead->next = mList.mHead;
    mList.mHead->prev = mList.mHead;
    mList.mSize = 0;
    std::fill(mBuckets.begin(), mBuckets.end(), mList.mHead);
}

// 0x575280.
TreeReplayMap::InsertResult *TreeReplayMap::Insert(InsertResult *result, const TreeReplayKey *key)
{
    const uint32_t hash = key->Hash();
    StdHashFindResult found;
    AsHashTable()->Find64(&found, key, hash);
    if (found.node != nullptr) {
        result->mNode = AsNode(found.node);
        result->mInserted = 0;
        return result;
    }
    if (mList.mSize == kMaxSize) std::_Xlength_error("unordered_map/set too long");

    TreeReplayNode *node = static_cast<TreeReplayNode *>(::operator new(TreeReplayNode::kSize));
    node->mKey = *key;
    new (&node->mRows) std::vector<TreeReplayRow>();

    // Grow first when the new element would push the load past the maximum.
    const uint32_t newSize = mList.mSize + 1;
    const float sizeAfter = static_cast<float>(static_cast<double>(newSize));
    const float bucketCount = static_cast<float>(static_cast<double>(mBucketCount));
    StdHashNode *where = found.where;
    if (sizeAfter / bucketCount > mMaxLoadFactor) {
        const float wanted = sizeAfter / mMaxLoadFactor;
        uint32_t target = static_cast<uint32_t>(static_cast<int32_t>(static_cast<float>(std::ceil(static_cast<double>(wanted)))));
        target = std::max(kMinGrownBuckets, target);
        uint32_t grown = mBucketCount;
        if (mBucketCount < target) {
            grown = target;
            if (mBucketCount < kGrowthFactorLimit && (mBucketCount << kGrowthShift) >= target) grown = mBucketCount << kGrowthShift;
        }
        Rehash(grown);
        AsHashTable()->Find64(&found, &node->mKey, hash); // the position changed with the buckets
        where = found.where;
    }

    // Link the node in front of `where` (the first node of its bucket, or the list head when the bucket is empty).
    StdHashNode *before = where->prev;
    ++mList.mSize;
    node->prev = before;
    node->next = where;
    before->next = node;
    where->prev = node;

    TreeReplayBucket *bucket = BucketFor(hash);
    if (bucket->mFirst == mList.mHead) {
        bucket->mFirst = node;
        bucket->mLast = node;
    } else if (bucket->mFirst == where) {
        bucket->mFirst = node;
    } else if (bucket->mLast == before) {
        bucket->mLast = node;
    }
    result->mNode = node;
    result->mInserted = 1;
    return result;
}

// 0x578680. The new bucket count is the power of two above (buckets - 1) | 1. Every node is then re-linked into its
// new bucket, equal keys next to each other (the lookup relies on a bucket's nodes being contiguous).
void TreeReplayMap::Rehash(uint32_t buckets)
{
    if (buckets > kMaxBuckets) std::_Xlength_error("invalid hash bucket count");

    uint32_t highBit = 31;
    const uint32_t seed = (buckets - 1) | 1;
    while ((seed >> highBit) == 0) --highBit;
    const uint32_t newCount = 1u << ((highBit + 1) & 31);

    TreeReplayNode *head = mList.mHead;
    // The bucket vector only grows: a smaller table keeps the vector it has (all entries reset to the sentinel).
    if (mBuckets.size() >= newCount * 2) {
        std::fill(mBuckets.begin(), mBuckets.end(), head);
    } else {
        std::vector<TreeReplayNode *> grown(newCount * 2, head);
        mBuckets.swap(grown);
    }
    mMask = newCount - 1;
    mBucketCount = newCount;

    StdHashNode *next = nullptr;
    for (StdHashNode *current = head->next; current != head; current = next) {
        TreeReplayNode *node = AsNode(current);
        next = node->next; // the node may move in the list: remember where the walk goes on
        TreeReplayBucket *bucket = BucketFor(node->mKey.Hash());

        if (bucket->mFirst == head) { // first node of an empty bucket
            bucket->mFirst = node;
            bucket->mLast = node;
            continue;
        }
        TreeReplayNode *last = bucket->mLast;
        if (SameKey(node, last)) {
            // The same key as the bucket's last node: right behind it.
            if (last->next != node) MoveBefore(node, last->next);
            bucket->mLast = node;
            continue;
        }
        // Search the bucket backwards for the key.
        TreeReplayNode *match = last;
        for (;;) {
            if (bucket->mFirst == match) { // not there: becomes the bucket's first node
                MoveBefore(node, match);
                bucket->mFirst = node;
                break;
            }
            match = AsNode(match->prev);
            if (SameKey(node, match)) { // behind its equal
                MoveBefore(node, match->next);
                break;
            }
        }
    }
}
