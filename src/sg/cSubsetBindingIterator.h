#pragma once
// nGZSceneGraph::cSubsetBindingIterator (lane SH, scenegraph H1b part 6): what cShape's slot 54 hands out, a
// bidirectional iterator over the shape's subset bindings (the map at +0x58), pool-allocated.
//
//   game vtable 0xe6f6d8 (9 slots), 0xc bytes, pool cRZFastFixedAllocator2<0xc> at 0x10f62f0: +4 the count, +8 the node.
//   Constructor 0xa5bea0 (node), copy 0xa5be80 (count 0), destructor 0xa5c1e0, Clone 0xa5d0f0, Get 0xa5d1e0 (the
//   binding: the subset name's text and the three words), deleting destructor 0xa5c4e0. The pool's allocate 0xa5c1f0
//   and free 0xa5c220 are the class operator new / delete here.
//
// Only cShape's slot 54 and the iterator's own Clone build one, so with the slot ours no game iterator exists and
// every body above is in effect through it. The shared slots (QueryInterface 0x178b20, AddRef 0x148fb0, Release
// 0x178b60, IsEqual 0x17b680, Next 0x17b700, Prev 0x17b760) stay unclaimed; Next and Prev are the library's tree steps.
#include "rz/RZFastFixedAllocators.h"
#include "game/owned.h"
#include "gz/cIGZUnknown.h"
#include "sg/cShape.h"

#include <cstddef>
#include <cstdint>

namespace nGZSceneGraph {

// What Get writes: the subset name's text (the string pool's, zero for the empty cell) and the binding's words.
struct cSubsetBinding {
    uint32_t mName;     // the key cell's text
    uint32_t mMaterial; // node +0x14
    uint32_t mMask;     // node +0x18
    uint32_t mWord;     // node +0x1c
};
static_assert(sizeof(cSubsetBinding) == 0x10, "cSubsetBinding size");

class cISubsetBindingIterator : public cIGZUnknown {
public:
    virtual cISubsetBindingIterator *Clone() = 0;                   // 3: a new iterator at the same place (count 0)
    virtual bool IsEqual(const cISubsetBindingIterator *other) = 0; // 4: the same node
    virtual cSubsetBinding *Get(cSubsetBinding *out) = 0;           // 5 (ret 4: returns out)
    virtual void Next() = 0;                                        // 6
    virtual void Prev() = 0;                                        // 7
    virtual ~cISubsetBindingIterator() = default;                   // 8: the deleting destructor (the pool free)
};

using ShapeBindingMap = std::map<OwnedCell, ShapeBinding, CellLess>;

TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0xc>, 10f62f0); // cSubsetBindingIterator's pool (operator new / delete)
class cSubsetBindingIterator final : public cISubsetBindingIterator {
public:
    static constexpr uint32_t kPool = 0x10f62f0; // RZFastFixedAllocator 0xc (0xa5c1f0 / 0xa5c220)

    explicit cSubsetBindingIterator(ShapeBindingMap::iterator node) : mNode(node) {}          // 0xa5bea0
    cSubsetBindingIterator(const cSubsetBindingIterator &other) : mNode(other.mNode) {}       // 0xa5be80
    ~cSubsetBindingIterator() override = default;                                              // 0xa5c1e0 (slot 8 0xa5c4e0)

    static void *operator new(size_t) noexcept;  // 0xa5c1f0
    static void operator delete(void *block) noexcept; // 0xa5c220

    bool QueryInterface(uint32_t riid, void **out) override; // shared 0x178b20: riid 1 only
    uint32_t AddRef() override;                              // shared 0x148fb0
    uint32_t Release() override;                             // shared 0x178b60: deletes at 0

    cISubsetBindingIterator *Clone() override;                   // 0xa5d0f0
    bool IsEqual(const cISubsetBindingIterator *other) override; // shared 0x17b680: +8
    cSubsetBinding *Get(cSubsetBinding *out) override;           // 0xa5d1e0
    void Next() override;                                        // shared 0x17b700: the tree's successor
    void Prev() override;                                        // shared 0x17b760: the tree's predecessor

    uint32_t mRefCount = 0;          // +4
    ShapeBindingMap::iterator mNode; // +8
};
static_assert(sizeof(cSubsetBindingIterator) == 0xc, "cSubsetBindingIterator size");

} // namespace nGZSceneGraph
