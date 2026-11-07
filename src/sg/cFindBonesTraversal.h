#pragma once
// nGZSceneGraph::cFindBonesTraversal (lane QR, scenegraph D3 part 2): a visitor over a composition tree that collects the
// nodes a skeleton is made of. Class id 0x5ca10ca7 (the COM director's factory 0xa95d30), interface cIFindBonesTraversal
// 0x5ca10ca0. The object is 0x1c bytes:
//   +0 vptr (0xe74a38, 24 slots)   +4 the count (cCompositionTreeTraversalT's)
//   +8 filter (int, -1 when made)  +0xc flag (byte, 0)  +0x10 the collected nodes (a vector of counted pointers: begin,
//   end, capacity limit)
// The class has three layers, as its RTTI says: cICompositionTreeTraversal (the visitor: slot 3 traverses a node, slots
// 4 and 5-15 visit by node kind), cIFindBonesTraversal (slots 16-22: the filter, the flag and the list), and
// cCompositionTreeTraversalT<cIFindBonesTraversal> (the count, slot 3 and the kind forwarders 6-15, the virtual
// destructor at slot 23). Only slots 4, 5 and 18 and the destructors are the class's own bodies; the others are the
// template's (6-15, 3) or folded accessors shared with other classes.
//
// What the visit does: slot 4 visits a node's children (skipping those with the filter's flag bit set, when the filter is
// not negative) through slot 3; slot 5 (the transform nodes) collects the node when it has a tag (or the flag is set),
// then visits its children. The result is the list of tagged transform nodes under the starting node, in tree order: the
// bones (the names are tentative).
//
// Standalone shape (docs/standalone-shape.md): the factory is hooked and answers ours; no other body is reached except
// through the vtable of our objects, so every slot is "in effect through callers".
#include "gz/cIGZUnknown.h"
#include "sg/SGNodeInterfaces.h"

#include <cstdint>
#include <vector>

namespace nGZSceneGraph {

// A counted pointer (AddRef on copy, Release on destruction, null allowed): the game's vector of nodes holds these.
class cNodeRef {
public:
    cNodeRef(cIGZUnknown *p = nullptr) : mPtr(p) { if (mPtr) mPtr->AddRef(); }
    cNodeRef(const cNodeRef &other) : mPtr(other.mPtr) { if (mPtr) mPtr->AddRef(); }
    cNodeRef &operator=(const cNodeRef &) = delete;
    ~cNodeRef() { if (mPtr) mPtr->Release(); }
    cIGZUnknown *Get() const { return mPtr; }

private:
    cIGZUnknown *mPtr;
};
static_assert(sizeof(cNodeRef) == 4, "a counted pointer is one word");

// nGZSceneGraph::cIFindBonesTraversal (interface id 0x5ca10ca0): the visitor's 7 own slots, 16-22.
class cIFindBonesTraversal : public cICompositionTreeTraversal {
public:
    static constexpr uint32_t kIid = 0x5ca10ca0;

    virtual void SetFilter(int32_t flagBit) = 0;       // 16: a node flag bit; nodes with it set are skipped (negative: none)
    virtual void SetCollectUntagged(bool on) = 0;      // 17: collect nodes without a tag too
    virtual void Clear() = 0;                          // 18: releases the collected nodes (the storage is kept)
    virtual int32_t GetFilter() = 0;                   // 19
    virtual bool GetCollectUntagged() = 0;             // 20
    virtual cICompositionTreeNode *At(uint32_t i) = 0; // 21: the i-th collected node, uncounted
    virtual uint32_t Count() = 0;                      // 22
};

// nGZSceneGraph::cCompositionTreeTraversalT<I>: the count and the forwarders every traversal class shares. The folded
// bodies are shared across the template's instances (shared_vf_*); the virtual destructor lands after I's slots.
template <class I> class cCompositionTreeTraversalT : public I {
public:
    cCompositionTreeTraversalT() = default;
    cCompositionTreeTraversalT(const cCompositionTreeTraversalT &) = delete;
    cCompositionTreeTraversalT &operator=(const cCompositionTreeTraversalT &) = delete;
    virtual ~cCompositionTreeTraversalT() = default; // 23: the deleting destructor

    // 0xa95d60 (ret 8; shared with cFindAttributeNodesTraversal): iid 1 or 0xc51851eb answers this, counted.
    bool QueryInterface(uint32_t iid, void **out) override
    {
        if (!out) return false;
        if (iid != 1 && iid != 0xc51851eb) return false;
        *out = static_cast<cIGZUnknown *>(this);
        AddRef();
        return true;
    }
    uint32_t AddRef() override { return ++mCount; } // 1: 0x148fb0
    uint32_t Release() override                     // 2: 0x356910 (the count, or 0 once deleted)
    {
        if (--mCount != 0) return mCount;
        delete this; // the deleting destructor, slot 23
        return 0;
    }
    uint32_t Traverse(cICompositionTreeNode *node) override { return node->Accept(this); } // 3: 0xa95eb0
    // 6-15: each forwards to another slot (the shared bodies 0x2b5b00, 0x1a9f00, 0x2b5b10, 0x1ae2f0, 0x2b5d40, 0x2b5d50)
    uint32_t TraversalSlot6(cICompositionTreeNode *node) override { return this->VisitTransform(node); }
    uint32_t TraversalSlot7(cICompositionTreeNode *node) override { return this->TraversalSlot6(node); }
    uint32_t TraversalSlot8(cICompositionTreeNode *node) override { return this->TraversalSlot7(node); }
    uint32_t TraversalSlot9(cICompositionTreeNode *node) override { return this->TraversalSlot7(node); }
    uint32_t TraversalSlot10(cICompositionTreeNode *node) override { return this->TraversalSlot7(node); }
    uint32_t TraversalSlot11(cICompositionTreeNode *node) override { return this->TraversalSlot10(node); }
    uint32_t VisitResource(cICompositionTreeNode *node) override { return this->Visit(node); }
    uint32_t VisitPartition(cICompositionTreeNode *node) override { return this->VisitTransform(node); }
    uint32_t VisitScene(cICompositionTreeNode *node) override { return this->VisitPartition(node); }
    uint32_t VisitExplicitBound(cICompositionTreeNode *node) override { return this->TraversalSlot6(node); }

protected:
    uint32_t mCount = 0; // +4
};

class cFindBonesTraversal final : public cCompositionTreeTraversalT<cIFindBonesTraversal> {
public:
    static constexpr uint32_t kClassId = 0x5ca10ca7;

    // 0xa95d30 (__cdecl): the COM director's factory: a new traversal (count 0), as its cIGZUnknown.
    static cIGZUnknown *__cdecl Create();

    cFindBonesTraversal() = default;           // 0xa95c10
    ~cFindBonesTraversal() override = default; // 0xa95c60 (deleting 0xa95ca0)

    bool QueryInterface(uint32_t iid, void **out) override;        // 0: 0xa95d90
    uint32_t Visit(cICompositionTreeNode *node) override;          // 4: 0xa95dd0
    uint32_t VisitTransform(cICompositionTreeNode *node) override; // 5: 0xa95ec0

    void SetFilter(int32_t flagBit) override { mFilter = flagBit; }      // 16: 0x14a260
    void SetCollectUntagged(bool on) override { mCollectUntagged = on; } // 17: 0x1f0e90
    void Clear() override;                                               // 18: 0xa95cc0
    int32_t GetFilter() override { return mFilter; }                     // 19: 0x46390
    bool GetCollectUntagged() override { return mCollectUntagged; }      // 20: 0x1b7090
    cICompositionTreeNode *At(uint32_t i) override                       // 21: 0x808d30
    {
        return static_cast<cICompositionTreeNode *>(mNodes[i].Get());
    }
    uint32_t Count() override { return uint32_t(mNodes.size()); } // 22: 0x806c60

private:
    int32_t mFilter = -1;          // +8
    bool mCollectUntagged = false; // +0xc
    std::vector<cNodeRef> mNodes;  // +0x10
};
static_assert(sizeof(cFindBonesTraversal) == 0x1c, "cFindBonesTraversal size (the factory allocates 0x1c)");

} // namespace nGZSceneGraph
