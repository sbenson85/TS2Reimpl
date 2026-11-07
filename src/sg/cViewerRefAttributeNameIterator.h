#pragma once
// The attribute-name ranges cViewerRefNode's slot 51 hands out (scenegraph N3r): each a pair of iterators over the
// entries for one attribute, every position naming the same {group, name} pair ("viewersGroup" and "fov" or
// "nearFarClip").
//
//   nGZSceneGraph::cViewerRefAttributeNameIterator (game vtable 0xe71d80, 0x14 bytes, pool 0x10f639c, a
//     cRZFastFixedAllocator2<0x14>: 0xa648c0 allocates, 0xa648f0 frees): +4 the count, +8 the position, +0xc the group
//     text, +0x10 the attribute's name. Constructors 0xa64470 (position, group, name) and 0xa64440 (copy, count 0),
//     destructor 0xa64810, deleting destructor 0xa64b90 (slot 10), Clone 0xa65690. The other slots are bodies the light
//     iterator shares (Get 0xa618e0, Next 0xa632b0, Previous 0xa63350, Advance 0xa611c0, QueryInterface 0x178b20,
//     AddRef 0x148fb0, Release 0x1aafb0, IsEqual 0x17b680, At 0x1aac30): see sg/cLightRefAttributeNameIterator.h.
//   the range builder 0xa66440 (the Impl's MakeIterator51): see cViewerRefNodeImpl.
//
// Only the builder and the iterator's own Clone build them, so with the builder ours no game iterator exists and the
// own bodies above are in effect through it; the shared ones stay unclaimed.
#include "rz/RZFastFixedAllocators.h"
#include "game/owned.h"
#include "sg/cNullArrayAttributeEntryIterator.h"

#include <cstddef>
#include <cstdint>

namespace nGZSceneGraph {

TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x14>, 10f639c); // cViewerRefAttributeNameIterator's pool (operator new / delete)
class cViewerRefAttributeNameIterator final : public cIArrayAttributeEntryIterator {
public:
    static constexpr uint32_t kPool = 0x10f639c; // cRZFastFixedAllocator2<0x14> (0xa648c0 / 0xa648f0)

    // 0xa64470: the count 0 (the caller counts), the position, the group text and the attribute's name.
    cViewerRefAttributeNameIterator(uint32_t position, const char *group, const char *name)
        : mPosition(position), mGroup(group), mName(name)
    {
    }
    // 0xa64440: the same place, count 0.
    cViewerRefAttributeNameIterator(const cViewerRefAttributeNameIterator &other)
        : cIArrayAttributeEntryIterator(), mPosition(other.mPosition), mGroup(other.mGroup), mName(other.mName)
    {
    }
    cViewerRefAttributeNameIterator &operator=(const cViewerRefAttributeNameIterator &) = delete;
    virtual ~cViewerRefAttributeNameIterator() = default; // 0xa64810 / 0xa64b90 (slot 10)

    static void *operator new(size_t) noexcept;
    static void operator delete(void *block) noexcept;

    bool QueryInterface(uint32_t riid, void **out) override;           // shared 0x178b20: riid 1 only
    uint32_t AddRef() override;                                        // shared 0x148fb0
    uint32_t Release() override;                                       // shared 0x1aafb0: deletes at 0
    cIArrayAttributeEntryIterator *Clone() override;                   // 0xa65690 (null when the pool fails)
    bool IsEqual(const cIArrayAttributeEntryIterator *other) override; // shared 0x17b680: the same position
    cArrayAttributeEntryName Get() override;                           // shared 0xa618e0
    void Next() override;                                              // shared 0xa632b0
    void Previous() override;                                          // shared 0xa63350
    void Advance(int32_t count) override;                              // shared 0xa611c0
    cArrayAttributeEntryName At(int32_t index) override;               // shared 0x1aac30: Get, the index ignored

    uint32_t mRefCount = 0; // +4
    int32_t mPosition;      // +8
    const char *mGroup;     // +0xc
    const char *mName;      // +0x10
};
static_assert(sizeof(cViewerRefAttributeNameIterator) == 0x14, "cViewerRefAttributeNameIterator size");

// The range the builder hands out: out = {a clone of begin, begin (position 0), end (position `count`)}, each counted,
// the end built first. `end` is null when the pool fails (the original does not check begin).
cArrayAttributeEntryRange *MakeViewerAttributeNameRange(cArrayAttributeEntryRange *out, uint32_t count,
                                                        const char *group, const char *name);

} // namespace nGZSceneGraph
