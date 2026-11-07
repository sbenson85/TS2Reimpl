#pragma once
// The iterators nGZSceneGraph::cGeometryDataContainer hands out (lane PA, scenegraph D1b): cIGZRandomAccessIterator<T,
// cGZElementByValue<T>> over an element's data (float, cVector<2..4>, unsigned long), over the uint16 index lists, an
// empty one, and a counter (cUnitIterator, anonymous namespace in the game) for a linkage without an alias list.
//
// The game's classes (all 8-16 bytes: vtable, count, then their state):
//   cRZMemPtrIterator<T>          vtable, count, pointer, stride; Next / Previous / Advance move by the stride, Get reads
//                                 a T where the pointer is, At(n) the T n strides on. Vtables 0xe73460 (float), 0xe73490
//                                 (cVector<2>), 0xe734c0 (cVector<3>), 0xe734f0 (cVector<4>), 0xe73520 (unsigned long),
//                                 0xe73550 (unsigned short); 11 slots, the deleting destructor last.
//   cRZNullRandomAccessIterator   vtable, count; equal to every iterator, reads 0 (a zero static, 0x10f6438), never
//                                 moves; At is Get (through the vtable). Vtable 0xe73580, 11 slots.
//   cUnitIterator                 vtable, count, a uint16 value: Get is the value, Next / Previous / Advance / At add to it
//                                 (16 bits). Vtable 0xe73384, 10 slots: no destructor slot, Release frees it (12 bytes).
// Only the container stores the vtables of cVector<4>'s (0xe734f0), the null one (0xe73580) and cUnitIterator (0xe73384);
// the other MemPtr vtables are also stored by game code outside this class (0xa8b6e0, 0xce4590, 0xd005e0, 0xd02610), so
// their bodies stay the game's, and ours are our own classes with the same slot shapes.
//
// Shared slot bodies (folded across unrelated classes, the game's): QueryInterface 0x178b20 (iid 1 only: *out = this,
// AddRef), AddRef 0x148fb0, Release 0x1aafb0 (the deleting destructor at 0), IsEqual 0x17b680 (the pointers), Next
// 0xa70df0, Previous 0xa70ef0, Advance 0xa6ffd0; the null one's IsEqual 0x148260 (true), Next / Previous 0x140fe0,
// Advance 0x300a0 (nothing).
#include "gz/cIGZUnknown.h"

#include <cstdint>

namespace nGZSceneGraph {

// nGZNumerics::cVector<N, cTypeTraits<float>> as the iterators return it (a class: member functions return it by hidden
// pointer, as the game's Get / At do: `ret 4` / `ret 8`).
template <int N> struct cGeometryVector {
    float v[N];
    cGeometryVector() {}
};
static_assert(sizeof(cGeometryVector<2>) == 8 && sizeof(cGeometryVector<4>) == 16, "cGeometryVector size");

// cIGZRandomAccessIterator<T, cGZElementByValue<T>> (with its cIGZIterator / forward / bidirectional bases): slots 3-9.
// The deleting destructor (slot 10) is the concrete classes' (cUnitIterator has none).
template <typename T> class cIGeometryIterator : public cIGZUnknown {
public:
    virtual cIGeometryIterator *Clone() = 0;                    // 3: a new iterator at the same place, count 0
    virtual bool IsEqual(const cIGeometryIterator *other) = 0;  // 4
    virtual T Get() = 0;                                        // 5
    virtual void Next() = 0;                                    // 6
    virtual void Previous() = 0;                                // 7
    virtual void Advance(int32_t count) = 0;                    // 8
    virtual T At(int32_t index) = 0;                            // 9
};

// What the container's range slots fill (their hidden out): a cursor (a clone of begin) and the two ends, each counted.
template <typename T> struct cGeometryRange {
    cIGeometryIterator<T> *mCurrent;
    cIGeometryIterator<T> *mBegin;
    cIGeometryIterator<T> *mEnd;
};
static_assert(sizeof(cGeometryRange<float>) == 0xc, "cGeometryRange size");

// cRZMemPtrIterator<T, cGZElementByValue<T>>.
template <typename T> class cGeometryMemPtrIterator final : public cIGeometryIterator<T> {
public:
    cGeometryMemPtrIterator(const uint8_t *pointer, uint32_t stride) : mPointer(pointer), mStride(stride) {}
    virtual ~cGeometryMemPtrIterator() = default; // 10: the deleting destructor (0x10 bytes)

    bool QueryInterface(uint32_t iid, void **out) override
    {
        if (out == nullptr || iid != 1) return false;
        *out = this;
        AddRef();
        return true;
    }
    uint32_t AddRef() override { return ++mRefCount; }
    uint32_t Release() override
    {
        if (--mRefCount != 0) return mRefCount;
        delete this;
        return 0;
    }
    cIGeometryIterator<T> *Clone() override { return new cGeometryMemPtrIterator(mPointer, mStride); }
    bool IsEqual(const cIGeometryIterator<T> *other) override
    {
        return mPointer == static_cast<const cGeometryMemPtrIterator *>(other)->mPointer;
    }
    T Get() override { return *reinterpret_cast<const T *>(mPointer); }
    void Next() override { mPointer += mStride; }
    void Previous() override { mPointer -= mStride; }
    void Advance(int32_t count) override { mPointer += static_cast<int32_t>(mStride) * count; }
    T At(int32_t index) override { return *reinterpret_cast<const T *>(mPointer + static_cast<int32_t>(mStride) * index); }

    uint32_t mRefCount = 0;   // +4
    const uint8_t *mPointer;  // +8
    uint32_t mStride;         // +0xc
};

// cRZNullRandomAccessIterator<unsigned short, cGZElementByValue<unsigned short>>: both ends of an empty range.
class cGeometryNullIterator final : public cIGeometryIterator<uint16_t> {
public:
    virtual ~cGeometryNullIterator() = default; // 10: 0xa6f7a0 (8 bytes)

    bool QueryInterface(uint32_t iid, void **out) override;
    uint32_t AddRef() override;
    uint32_t Release() override;
    cIGeometryIterator<uint16_t> *Clone() override;                    // 0xa702b0
    bool IsEqual(const cIGeometryIterator<uint16_t> *other) override; // true
    uint16_t Get() override;                                          // 0xa70660: the zero static
    void Next() override;
    void Previous() override;
    void Advance(int32_t count) override;
    uint16_t At(int32_t index) override;                              // 0xa70730: Get through the vtable

    uint32_t mRefCount = 0; // +4
};

// cUnitIterator: counts 16-bit values; no destructor slot (Release frees it).
class cGeometryUnitIterator final : public cIGeometryIterator<uint16_t> {
public:
    explicit cGeometryUnitIterator(uint16_t value) : mValue(value) {}

    bool QueryInterface(uint32_t iid, void **out) override;
    uint32_t AddRef() override;
    uint32_t Release() override;                                      // 0xa72470: frees 12 bytes at 0
    cIGeometryIterator<uint16_t> *Clone() override;                    // 0xa70480
    bool IsEqual(const cIGeometryIterator<uint16_t> *other) override; // 0xa70900: the values
    uint16_t Get() override;                                          // shared 0x432e30
    void Next() override;                                             // 0xa70e00
    void Previous() override;                                         // 0xa70f00
    void Advance(int32_t count) override;                             // 0xa6ffe0: 16 bits
    uint16_t At(int32_t index) override;                              // 0xa70740: value + index, 16 bits

    uint32_t mRefCount = 0; // +4
    uint16_t mValue;        // +8
};

static_assert(sizeof(cGeometryMemPtrIterator<float>) == 0x10, "cRZMemPtrIterator size");
static_assert(sizeof(cGeometryNullIterator) == 8, "cRZNullRandomAccessIterator size");
static_assert(sizeof(cGeometryUnitIterator) == 0xc, "cUnitIterator size");

// The range the slots build: `end` made first, then `begin`; out = {begin's clone, begin, end}, each counted once.
template <typename T> cGeometryRange<T> *FillGeometryRange(cGeometryRange<T> *out, cIGeometryIterator<T> *end,
                                                           cIGeometryIterator<T> *begin)
{
    cIGeometryIterator<T> *current = begin->Clone();
    out->mCurrent = current;
    if (current) current->AddRef();
    out->mBegin = begin;
    begin->AddRef();
    out->mEnd = end;
    if (end) end->AddRef();
    return out;
}

} // namespace nGZSceneGraph
