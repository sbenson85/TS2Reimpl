#pragma once
// nGZSceneGraph's empty attribute-entry range (lane OM, scenegraph N0 part 3 step 4): what every composition node's slot
// 51 hands out. The node family's slot-51 forwarders (shared 0xa37260 and the game's other node classes) call the
// composition Impl's builder 0xa37d80 (thiscall on the Impl, ret 8: the out range and a word, neither the Impl nor the
// word read), which builds two null iterators and returns {a clone of the second, the second, the first}, each counted.
//
// The iterator: cRZNullRandomAccessIterator<cArrayAttributeEntryName, cGZElementByValue<cArrayAttributeEntryName>>
// (game vtable 0xe68db0, 8 bytes: vtable, count). Both ends of an empty range: it equals every iterator, reads as the
// zero entry (the game's static at 0x10f5f90 is zero-initialised and never written) and does not move.
//
// The switch: 0xa37d80 is hooked. The game vtable 0xe68db0 is stored only by the builder, the iterator's Clone 0xa37630
// and its deleting destructor 0xa37080, so once the builder is ours no iterator carries it and the iterator's own bodies
// (Clone, Get 0xa37720, the deleting destructor) are in effect through it. Its other slots are folded across classes
// (shared_vf_178b20 QI, 148fb0 AddRef, 1aafb0 Release, 148260 IsEqual, 140fe0 Next / Previous, 300a0 Advance, 1aac30
// At): ours implement them, the game's stay unclaimed.
#include "gz/cIGZUnknown.h"

#include <cstdint>

namespace nGZSceneGraph {

// An entry of an array attribute's name list (8 bytes; the words are not interpreted here).
struct cArrayAttributeEntryName {
    uint32_t mWord0;
    uint32_t mWord1;
};
static_assert(sizeof(cArrayAttributeEntryName) == 8, "cArrayAttributeEntryName size");

// cIGZIterator / cIGZForwardIterator / cIGZBidirectionalIterator / cIGZRandomAccessIterator<cArrayAttributeEntryName,
// cGZElementByValue<...>>: slots 3-9 (the deleting destructor is slot 10).
class cIArrayAttributeEntryIterator : public cIGZUnknown {
public:
    virtual cIArrayAttributeEntryIterator *Clone() = 0;                    // 3: a new iterator at the same place (count 0)
    virtual bool IsEqual(const cIArrayAttributeEntryIterator *other) = 0;  // 4
    virtual cArrayAttributeEntryName Get() = 0;                            // 5 (hidden out, ret 4)
    virtual void Next() = 0;                                               // 6
    virtual void Previous() = 0;                                           // 7
    virtual void Advance(int32_t count) = 0;                               // 8
    virtual cArrayAttributeEntryName At(int32_t index) = 0;               // 9 (hidden out and the index, ret 8)
};

// Slot 51's result (by hidden pointer): a cursor (a clone of begin) and the range's two ends, each counted.
struct cArrayAttributeEntryRange {
    cIArrayAttributeEntryIterator *mCurrent;
    cIArrayAttributeEntryIterator *mBegin;
    cIArrayAttributeEntryIterator *mEnd;
};
static_assert(sizeof(cArrayAttributeEntryRange) == 0xc, "cArrayAttributeEntryRange size");

class cNullArrayAttributeEntryIterator final : public cIArrayAttributeEntryIterator {
public:
    cNullArrayAttributeEntryIterator() = default;
    virtual ~cNullArrayAttributeEntryIterator() = default; // the deleting destructor 0xa37080 is slot 10

    bool QueryInterface(uint32_t riid, void **out) override;                // shared 0x178b20: riid 1 only
    uint32_t AddRef() override;                                             // shared 0x148fb0
    uint32_t Release() override;                                            // shared 0x1aafb0: deletes at 0
    cIArrayAttributeEntryIterator *Clone() override;                        // 0xa37630
    bool IsEqual(const cIArrayAttributeEntryIterator *other) override;      // shared 0x148260: true
    cArrayAttributeEntryName Get() override;                                // 0xa37720: the zero entry
    void Next() override;                                                   // shared 0x140fe0: nothing
    void Previous() override;                                               // shared 0x140fe0: nothing
    void Advance(int32_t count) override;                                   // shared 0x300a0: nothing
    cArrayAttributeEntryName At(int32_t index) override;                    // shared 0x1aac30: slot 5, the index ignored

    uint32_t mRefCount = 0; // +4
};
static_assert(sizeof(cNullArrayAttributeEntryIterator) == 8, "cNullArrayAttributeEntryIterator size");

// 0xa37d80 (thiscall on the composition Impl; neither it nor `a` is read): out = {a clone of the second null iterator,
// the second, the first}, each counted; returns out. The hook's entry point.
cArrayAttributeEntryRange *__fastcall MakeEmptyAttributeEntryRange(void *impl, void *edx, cArrayAttributeEntryRange *out,
                                                                   uint32_t a);

} // namespace nGZSceneGraph
