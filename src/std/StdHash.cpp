#include "std/StdHash.h"

#include <cstring>
#include <string>

namespace {

// The key of a node: at +8 in every table here.
const uint8_t *NodeKey(const StdHashNode *node)
{
    return reinterpret_cast<const uint8_t *>(node) + 8;
}

// Shared walk of the original: starts at the bucket's last node and steps towards its first
// (through prev), comparing keys. `matches(node)` is the key comparison of the instantiation.
template <typename Matches>
StdHashFindResult *FindInBucket(const StdHashTable *table, StdHashFindResult *out, uint32_t hash, Matches matches)
{
    const StdHashBucket &bucket = table->mBuckets[hash & table->mMask];
    if (bucket.last == table->mHead) { // empty bucket
        out->where = table->mHead;
        out->node = nullptr;
        return out;
    }
    StdHashNode *node = bucket.last;
    if (!matches(node)) {
        for (;;) {
            if (node == bucket.first) { // every node of the bucket was compared
                out->where = bucket.first;
                out->node = nullptr;
                return out;
            }
            node = node->prev;
            if (matches(node)) break;
        }
    }
    out->where = node->next;
    out->node = node;
    return out;
}

bool SameBytes(const StdHashNode *node, const void *key, size_t size)
{
    return std::memcmp(NodeKey(node), key, size) == 0;
}

// A std::string stored in the node at `offset` (MSVC layout: buffer, size, capacity).
bool SameString(const StdHashNode *node, size_t offset, const std::string &key)
{
    return *reinterpret_cast<const std::string *>(reinterpret_cast<const uint8_t *>(node) + offset) == key;
}

} // namespace

StdHashFindResult *StdHashTable::Find32(StdHashFindResult *out, const void *key, uint32_t hash)
{
    return FindInBucket(this, out, hash, [key](const StdHashNode *n) { return SameBytes(n, key, 4); });
}

StdHashFindResult *StdHashTable::Find64(StdHashFindResult *out, const void *key, uint32_t hash)
{
    return FindInBucket(this, out, hash, [key](const StdHashNode *n) { return SameBytes(n, key, 8); });
}

StdHashFindResult *StdHashTable::Find96(StdHashFindResult *out, const void *key, uint32_t hash)
{
    return FindInBucket(this, out, hash, [key](const StdHashNode *n) { return SameBytes(n, key, 12); });
}

StdHashFindResult *StdHashTable::Find128(StdHashFindResult *out, const void *key, uint32_t hash)
{
    return FindInBucket(this, out, hash, [key](const StdHashNode *n) { return SameBytes(n, key, 16); });
}

StdHashFindResult *StdHashTable::FindStringAt12(StdHashFindResult *out, const void *key, uint32_t hash)
{
    const auto &search = *reinterpret_cast<const std::string *>(static_cast<const uint8_t *>(key) + 4);
    return FindInBucket(this, out, hash, [&search](const StdHashNode *n) { return SameString(n, 0xc, search); });
}

StdHashFindResult *StdHashTable::FindCString(StdHashFindResult *out, const void *key, uint32_t hash)
{
    const char *search = *static_cast<const char *const *>(key);
    return FindInBucket(this, out, hash, [search](const StdHashNode *n) {
        return std::strcmp(search, *reinterpret_cast<const char *const *>(NodeKey(n))) == 0;
    });
}
