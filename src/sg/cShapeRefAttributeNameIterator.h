#pragma once
// The attribute-name ranges cShapeRefNode's slot 51 hands out (lane TD, scenegraph H2e): a counted list of
// cArrayAttributeEntryName (the game's cRZRefCountedT<std::vector<cArrayAttributeEntryName>>) and random-access
// iterators over it, pool-allocated:
//
//   the list (game vtable 0xe6f128, 0x14 bytes: the vector at +4, the count at +0x10; deleting destructor 0xa54740);
//   the iterator nGZSceneGraph::cShapeRefAttributeNameIterator (game vtable 0xe6f13c, 0x10 bytes, pool 0x10f625c): +4 the
//     count, +8 the list (counted), +0xc the element it stands on. Constructors 0xa541a0 (list, position) and 0xa54160
//     (copy, count 0), destructor 0xa54480 (the list released), deleting destructor 0xa54840 (slot 10), Clone 0xa54fb0,
//     Get 0xa550b0, Next 0xa57f50, Previous 0xa58010, Advance 0xa54920 (n elements), At 0xa550f0 (the element n on).
//     The pool's allocate 0xa545e0 and free 0xa54610 are the class operator new / delete here.
//
// Only the Impl's builder 0xa55830 (hooked) and the iterator's own Clone build them, so with the builder ours no game
// list or iterator exists and the bodies above are in effect through it. The shared slots (QueryInterface 0x178b20,
// AddRef 0x148fb0, Release 0x1aafb0, IsEqual 0x178550: the same element) stay unclaimed.
#include "rz/RZFastFixedAllocators.h"
#include "game/owned.h"
#include "gz/cIGZUnknown.h"
#include "sg/cNullArrayAttributeEntryIterator.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nGZSceneGraph {

// The counted name list. Not reached by game code: only our builder and iterators hold one.
class cShapeRefAttributeNameList final : public cIGZUnknown {
public:
    cShapeRefAttributeNameList() = default;
    cShapeRefAttributeNameList(const cShapeRefAttributeNameList &) = delete;
    cShapeRefAttributeNameList &operator=(const cShapeRefAttributeNameList &) = delete;
    virtual ~cShapeRefAttributeNameList() = default; // 0xa54740 (slot 3)

    bool QueryInterface(uint32_t riid, void **out) override; // riid 1 only
    uint32_t AddRef() override;
    uint32_t Release() override; // deletes at 0

    std::vector<cArrayAttributeEntryName> mNames; // +4
    uint32_t mRefCount = 0;                       // +0x10
};

TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x10>, 10f625c); // cShapeRefAttributeNameIterator's pool (operator new / delete)
class cShapeRefAttributeNameIterator final : public cIArrayAttributeEntryIterator {
public:
    static constexpr uint32_t kPool = 0x10f625c; // cRZFastFixedAllocator2<0x10> (0xa545e0 / 0xa54610)

    // 0xa541a0: the list counted, at `position`.
    cShapeRefAttributeNameIterator(cShapeRefAttributeNameList *list, const cArrayAttributeEntryName *position);
    // 0xa54160: the same list (counted again) and place, count 0.
    cShapeRefAttributeNameIterator(const cShapeRefAttributeNameIterator &other);
    cShapeRefAttributeNameIterator &operator=(const cShapeRefAttributeNameIterator &) = delete;
    // 0xa54480 / 0xa54840 (slot 10): the list released.
    virtual ~cShapeRefAttributeNameIterator();

    static void *operator new(size_t) noexcept;
    static void operator delete(void *block) noexcept;

    bool QueryInterface(uint32_t riid, void **out) override;               // shared 0x178b20: riid 1 only
    uint32_t AddRef() override;                                            // shared 0x148fb0
    uint32_t Release() override;                                           // shared 0x1aafb0: deletes at 0
    cIArrayAttributeEntryIterator *Clone() override;                       // 0xa54fb0 (null when the pool fails)
    bool IsEqual(const cIArrayAttributeEntryIterator *other) override;     // shared 0x178550: the same element
    cArrayAttributeEntryName Get() override;                               // 0xa550b0
    void Next() override;                                                  // 0xa57f50
    void Previous() override;                                              // 0xa58010
    void Advance(int32_t count) override;                                  // 0xa54920
    cArrayAttributeEntryName At(int32_t index) override;                   // 0xa550f0

    uint32_t mRefCount = 0;                       // +4
    cShapeRefAttributeNameList *mList;            // +8
    const cArrayAttributeEntryName *mPosition;    // +0xc
};
static_assert(sizeof(cShapeRefAttributeNameIterator) == 0x10, "cShapeRefAttributeNameIterator size");

// The range over `list` the builder hands out: {a clone of begin, begin, end}, each counted (the list is counted by both
// ends); the list itself is not released here.
cArrayAttributeEntryRange *MakeAttributeNameRange(cArrayAttributeEntryRange *out, cShapeRefAttributeNameList *list);

} // namespace nGZSceneGraph
