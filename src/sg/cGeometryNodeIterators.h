#pragma once
// The subset iterators cGeometryNode hands out (lane QZ, scenegraph D3 part 6): slot 42 (GetSubsetNames) walks the
// subset map's names, slot 44 (GetSubsetParts) the parts of one name. Both are cIGZForwardIterator over the map's tree
// nodes, pool-allocated:
//
//   names (game vtable 0xe69a3c, 0x10 bytes, pool 0x10f6020): +4 the count, +8 the map, +0xc the node it stands on.
//     Constructor 0xa3e070 (map, node), copy 0xa3e040 (count 0), Clone 0xa3f080, Get 0xa42340 (the name's text),
//     Next 0xa42850 (the next name: the first node past every node with this one's key), deleting destructor 0xa3e710.
//   parts (game vtable 0xe69a60, 0xc bytes, pool 0x10f602c): +4 the count, +8 the node. Constructor 0xa3e0c0, copy
//     0xa3e0a0, Clone 0xa3f0b0, Get 0xa42350 (the node's value), deleting destructor 0xa3e750; Next and IsEqual are the
//     shared tree-iterator bodies 0x17b700 / 0x17b680.
//
// The allocators 0xa3e540 / 0xa3e570 and frees 0xa3e5a0 / 0xa3e620 are the pools' (class operator new / delete here).
// Only cGeometryNode's slots 42 / 44 and the iterators' own Clone build them, so with those ours no game iterator
// exists and every slot below is in effect through them. The shared slots (QueryInterface 0x178b20, AddRef 0x148fb0,
// Release 0x1a84c0, IsEqual 0x178550 / 0x17b680, Next 0x17b700) stay unclaimed.
#include "rz/RZFastFixedAllocators.h"
#include "game/owned.h"
#include "gz/cIGZUnknown.h"
#include "sg/cGeometryNode.h"

#include <cstddef>
#include <cstdint>

namespace nGZSceneGraph {

// cIGZIterator / cIGZForwardIterator<uint32_t, ...>: slots 3-6, the deleting destructor slot 7 (Release's delete goes
// through it, so each class's pool operator delete frees the block).
class cIGeometrySubsetIterator : public cIGZUnknown {
public:
    virtual cIGeometrySubsetIterator *Clone() = 0;                   // 3: a new iterator at the same place (count 0)
    virtual bool IsEqual(const cIGeometrySubsetIterator *other) = 0; // 4: the same node
    virtual uint32_t Get() = 0;                                      // 5
    virtual void Next() = 0;                                         // 6
    virtual ~cIGeometrySubsetIterator() = default;                   // 7: the deleting destructor (the pool free)
};

using cGeometrySubsetMap = std::multimap<cGeometryNodeCell, uint32_t, cGeometryNodeCellLess>;

// The common part: the count and the pool-free counted object.
class cGeometrySubsetIteratorBase : public cIGeometrySubsetIterator {
public:
    bool QueryInterface(uint32_t riid, void **out) override; // shared 0x178b20: riid 1 only
    uint32_t AddRef() override;                              // shared 0x148fb0
    uint32_t Release() override;                             // shared 0x1a84c0: deletes at 0

    uint32_t mRefCount = 0; // +4
};

TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x10>, 10f6020); // cGeometrySubsetNameIterator's pool (operator new / delete)
class cGeometrySubsetNameIterator final : public cGeometrySubsetIteratorBase {
public:
    static constexpr uint32_t kPool = 0x10f6020; // RZFastFixedAllocator 0x10 (0xa3e540 / 0xa3e5a0)

    cGeometrySubsetNameIterator(cGeometrySubsetMap *map, cGeometrySubsetMap::iterator node) : mMap(map), mNode(node) {}
    cGeometrySubsetNameIterator(const cGeometrySubsetNameIterator &other) : mMap(other.mMap), mNode(other.mNode) {}
    ~cGeometrySubsetNameIterator() override = default; // 0xa3e710 (slot 7)

    static void *operator new(size_t) noexcept;
    static void operator delete(void *block) noexcept;

    cIGeometrySubsetIterator *Clone() override;                   // 0xa3f080
    bool IsEqual(const cIGeometrySubsetIterator *other) override; // shared 0x178550: +0xc
    uint32_t Get() override;                                      // 0xa42340: the name's text
    void Next() override;                                         // 0xa42850

    cGeometrySubsetMap *mMap;          // +8
    cGeometrySubsetMap::iterator mNode; // +0xc
};
static_assert(sizeof(cGeometrySubsetNameIterator) == 0x10, "cGeometrySubsetNameIterator size");

TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0xc>, 10f602c); // cSubsetGeomPartIndexIterator's pool (operator new / delete)
class cSubsetGeomPartIndexIterator final : public cGeometrySubsetIteratorBase {
public:
    static constexpr uint32_t kPool = 0x10f602c; // RZFastFixedAllocator 0xc (0xa3e570 / 0xa3e620)

    explicit cSubsetGeomPartIndexIterator(cGeometrySubsetMap::iterator node) : mNode(node) {}
    cSubsetGeomPartIndexIterator(const cSubsetGeomPartIndexIterator &other) : mNode(other.mNode) {}
    ~cSubsetGeomPartIndexIterator() override = default; // 0xa3e750 (slot 7)

    static void *operator new(size_t) noexcept;
    static void operator delete(void *block) noexcept;

    cIGeometrySubsetIterator *Clone() override;                   // 0xa3f0b0
    bool IsEqual(const cIGeometrySubsetIterator *other) override; // shared 0x17b680: +8
    uint32_t Get() override;                                      // 0xa42350: the part
    void Next() override;                                         // shared 0x17b700: the tree's successor

    cGeometrySubsetMap::iterator mNode; // +8
};
static_assert(sizeof(cSubsetGeomPartIndexIterator) == 0xc, "cSubsetGeomPartIndexIterator size");

} // namespace nGZSceneGraph
