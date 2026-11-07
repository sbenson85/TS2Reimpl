#include "misc/UtilitiesD.h"

#include "game/vtable.h"

uint8_t __cdecl FindChildAndSetEnabled(void *window, uint32_t id, uint32_t enabled)
{
    // The original passes id, the kind constant 0x8810 and the address of its local, in that order.
    // It tests the byte-sized return (al) of the lookup and calls the child only then, but it releases
    // whatever the local holds afterwards (the local starts at null, so an unfound child is not touched).
    using FindChild = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
    using ChildCall = void(__thiscall *)(void *);
    using Release = uint32_t(__thiscall *)(void *);

    void *child = nullptr;
    uint8_t found = VirtualCall<FindChild, 45>(window, id, 0x8810, &child);
    if (found) {
        // Only the low byte of `enabled` is compared, so slot 6 runs for any nonzero low byte.
        if (static_cast<uint8_t>(enabled) != 0) VirtualCall<ChildCall, 6>(child);
        else VirtualCall<ChildCall, 7>(child);
    }
    if (child) VirtualCall<Release, 2>(child);
    return found ? 1 : 0;
}

void cUtilitiesDOrigin::SubtractOrigin(const int32_t *src, int32_t *outX, int32_t *outY, int32_t *outZ)
{
    // Each read comes right before its write, as in the original, so an out pointer that aliases a
    // later source word sees the same values.
    *outX = static_cast<int32_t>(static_cast<uint32_t>(src[0]) - static_cast<uint32_t>(mOriginX));
    *outY = static_cast<int32_t>(static_cast<uint32_t>(src[1]) - static_cast<uint32_t>(mOriginY));
    *outZ = static_cast<int32_t>(static_cast<uint32_t>(src[2]) - static_cast<uint32_t>(mOriginZ));
}

uint8_t __stdcall KeyNotLessThanNode(const IntTreeNode *node, const int32_t *key)
{
    if (node->isNil) return 0;
    return *key >= node->key ? 1 : 0;
}

IntTreeFindResult *IntTree::LowerBound(IntTreeFindResult *out, const int32_t *key) const
{
    IntTreeNode *head = mHead;
    IntTreeNode *node = head->parent; // the root
    out->flag = 0;
    out->last = node;
    out->bound = head;
    if (node->isNil) return out;

    const int32_t wanted = *key;
    do {
        uint32_t wentCandidate = 0;
        out->last = node;
        if (node->key >= wanted) {
            out->bound = node;
            wentCandidate = 1;
            node = node->left;
        } else {
            node = node->right;
        }
        out->flag = wentCandidate;
    } while (!node->isNil);
    return out;
}

IntTreeNode *IntTree::FindNode(const int32_t *key) const
{
    IntTreeFindResult result;
    LowerBound(&result, key);
    if (KeyNotLessThanNode(result.bound, key)) return result.bound;
    return mHead;
}

uint32_t cUtilitiesDTreeOwner::ValueForKey(int32_t key) const
{
    IntTreeNode *node = mTree.FindNode(&key);
    if (node == mTree.mHead) return 0;
    return node->value;
}
