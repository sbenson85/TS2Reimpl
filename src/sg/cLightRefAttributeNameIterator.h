#pragma once
// The attribute-name ranges cLightRefNode's slot 51 hands out (lane TN, scenegraph N2r): each a pair of iterators over
// one name, the light attribute's {group, name} pair.
//
//   nGZSceneGraph::cLightRefAttributeNameIterator (game vtable 0xe704e4, 0x14 bytes, pool 0x10f6340, a
//     cRZFastFixedAllocator2<0x14>: 0xa60f20 allocates, 0xa60f50 frees): +4 the count, +8 the position (0 or 1), +0xc the
//     group text, +0x10 the attribute's name. Constructors 0xa60bd0 (position, group, name) and 0xa60ba0 (copy, count 0),
//     destructor 0xa60e90, deleting destructor 0xa61110 (slot 10), Clone 0xa61810, Get 0xa618e0 ({group, name}),
//     Next 0xa632b0, Previous 0xa63350, Advance 0xa611c0 (the position moved by n); the shared slots are QueryInterface
//     0x178b20, AddRef 0x148fb0, Release 0x1aafb0, IsEqual 0x17b680 (the positions) and At 0x1aac30 (Get, the index
//     ignored).
//   the range builder 0xa61d90 (the Impl's MakeIterator51): see cLightRefNodeImpl.
//
// Only the builder and the iterator's own Clone build them, so with the builder ours no game iterator exists and the
// bodies above are in effect through it. The shared slots (0x178b20, 0x148fb0, 0x1aafb0, 0x17b680 and 0x1aac30) stay
// unclaimed: ours implement them.
#include "rz/RZFastFixedAllocators.h"
#include "game/owned.h"
#include "sg/cNullArrayAttributeEntryIterator.h"

#include <cstddef>
#include <cstdint>

namespace nGZSceneGraph {

TS2_OWNED_STATIC(rz::cRZFastFixedAllocator2<0x14>, 10f6340); // cLightRefAttributeNameIterator's pool (operator new / delete)
class cLightRefAttributeNameIterator final : public cIArrayAttributeEntryIterator {
public:
    static constexpr uint32_t kPool = 0x10f6340; // cRZFastFixedAllocator2<0x14> (0xa60f20 / 0xa60f50)

    // 0xa60bd0: the count 0 (the caller counts), the position, the group text and the attribute's name.
    cLightRefAttributeNameIterator(uint32_t position, const char *group, const char *name)
        : mPosition(position), mGroup(group), mName(name)
    {
    }
    // 0xa60ba0: the same place, count 0.
    cLightRefAttributeNameIterator(const cLightRefAttributeNameIterator &other)
        : cIArrayAttributeEntryIterator(), mPosition(other.mPosition), mGroup(other.mGroup), mName(other.mName)
    {
    }
    cLightRefAttributeNameIterator &operator=(const cLightRefAttributeNameIterator &) = delete;
    virtual ~cLightRefAttributeNameIterator() = default; // 0xa60e90 / 0xa61110 (slot 10)

    static void *operator new(size_t) noexcept;
    static void operator delete(void *block) noexcept;

    bool QueryInterface(uint32_t riid, void **out) override;           // shared 0x178b20: riid 1 only
    uint32_t AddRef() override;                                        // shared 0x148fb0
    uint32_t Release() override;                                       // shared 0x1aafb0: deletes at 0
    cIArrayAttributeEntryIterator *Clone() override;                   // 0xa61810 (null when the pool fails)
    bool IsEqual(const cIArrayAttributeEntryIterator *other) override; // shared 0x17b680: the same position
    cArrayAttributeEntryName Get() override;                           // 0xa618e0
    void Next() override;                                              // 0xa632b0
    void Previous() override;                                          // 0xa63350
    void Advance(int32_t count) override;                              // 0xa611c0
    cArrayAttributeEntryName At(int32_t index) override;               // shared 0x1aac30: Get, the index ignored

    uint32_t mRefCount = 0; // +4
    int32_t mPosition;      // +8
    const char *mGroup;     // +0xc
    const char *mName;      // +0x10
};
static_assert(sizeof(cLightRefAttributeNameIterator) == 0x14, "cLightRefAttributeNameIterator size");

// The range the builder hands out over one name: out = {a clone of begin, begin, end}, each counted. `end` is null when
// the pool fails (the original does not check begin: a failing pool would crash it there too).
cArrayAttributeEntryRange *MakeLightAttributeNameRange(cArrayAttributeEntryRange *out, bool hasLight, const char *group,
                                                       const char *name);

} // namespace nGZSceneGraph
