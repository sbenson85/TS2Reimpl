#pragma once
// cIndexedIterator<cIGZRandomAccessIterator<T, cGZElementByValue<T>>> (lane VS): an iterator over a geometry element's
// values read through a linkage's alias list (value n is values[index[n]]), and the range fillers that make them for a
// linkage's element (0xaeddd0 float, 0xaee820 cVector<2>, 0xaee9f0 cVector<3>, 0xaeebc0 cVector<4>, 0xaee0f0 unsigned
// long) with the linked element's header 0xaee030 they start from.
//
// The game's class (0x10 bytes): vtable, count, the values' iterator (+8), the index iterator (+0xc), both counted.
// Ten slots, no destructor slot (Release frees it). Vtables 0xe7d0e4 (float), 0xe7d110 (cVector<2>), 0xe7d13c
// (cVector<3>), 0xe7d168 (cVector<4>), 0xe7d194 (unsigned long); each is stored only by its own Clone and its filler.
// Per instantiation: Clone, Get and At. Folded across the five (shared_vf_*, one template family): Release 0xaedb40,
// IsEqual 0xaedad0, Next 0xaedaf0, Previous 0xaedb00, Advance 0xaed5e0; QueryInterface 0x178b20 and AddRef 0x148fb0
// are folded with unrelated classes.
#include "sg/cGeometryDataContainerClass.h"
#include "sg/cGeometryIterators.h"

#include <cstdint>
#include <new>

namespace nGZSceneGraph {

template <typename T> class cIndexedIterator final : public cIGeometryIterator<T> {
public:
    // Counts both (each when not null), the values first.
    cIndexedIterator(cIGeometryIterator<T> *values, cIGeometryIterator<uint16_t> *index) : mValues(values)
    {
        if (mValues) mValues->AddRef();
        mIndex = index;
        if (mIndex) mIndex->AddRef();
    }

    bool QueryInterface(uint32_t iid, void **out) override
    {
        if (out == nullptr || iid != 1) return false;
        *out = this;
        AddRef();
        return true;
    }
    uint32_t AddRef() override { return ++mRefCount; }
    // At zero: the index released, then the values, and the iterator freed.
    uint32_t Release() override
    {
        if (--mRefCount != 0) return mRefCount;
        if (mIndex) mIndex->Release();
        if (mValues) mValues->Release();
        delete this;
        return 0;
    }
    // A new iterator (null when the allocation fails) over the same values and a clone of the index, count 0.
    cIGeometryIterator<T> *Clone() override { return new (std::nothrow) cIndexedIterator(mValues, mIndex->Clone()); }
    bool IsEqual(const cIGeometryIterator<T> *other) override
    {
        return mIndex->IsEqual(static_cast<const cIndexedIterator *>(other)->mIndex);
    }
    T Get() override { return mValues->At(mIndex->Get()); }
    void Next() override { mIndex->Next(); }
    void Previous() override { mIndex->Previous(); }
    void Advance(int32_t count) override { mIndex->Advance(count); }
    T At(int32_t index) override { return mValues->At(mIndex->At(index)); }

    uint32_t mRefCount = 0;                 // +4
    cIGeometryIterator<T> *mValues;         // +8
    cIGeometryIterator<uint16_t> *mIndex;   // +0xc

private:
    ~cIndexedIterator() = default;
};
static_assert(sizeof(cIndexedIterator<float>) == 0x10, "cIndexedIterator size");

// 0xaee030 (__cdecl, returns out): the header of the element at `position` in linkage `linkage`'s element list.
cGeometryElementHeader *__cdecl LinkedElementHeader(cGeometryElementHeader *out, cIGeometryDataContainer *container,
                                                    uint32_t linkage, uint32_t position);

// The range fillers (__cdecl, return out): the values of the element at `position` in linkage `linkage`, read through
// the linkage's alias list for that element's set format.
cGeometryRange<float> *__cdecl LinkedFloats(cGeometryRange<float> *out, cIGeometryDataContainer *container, uint32_t linkage,
                                            uint32_t position); // 0xaeddd0
cGeometryRange<cGeometryVector<2>> *__cdecl LinkedVectors2(cGeometryRange<cGeometryVector<2>> *out,
                                                           cIGeometryDataContainer *container, uint32_t linkage,
                                                           uint32_t position); // 0xaee820
cGeometryRange<cGeometryVector<3>> *__cdecl LinkedVectors3(cGeometryRange<cGeometryVector<3>> *out,
                                                           cIGeometryDataContainer *container, uint32_t linkage,
                                                           uint32_t position); // 0xaee9f0
cGeometryRange<cGeometryVector<4>> *__cdecl LinkedVectors4(cGeometryRange<cGeometryVector<4>> *out,
                                                           cIGeometryDataContainer *container, uint32_t linkage,
                                                           uint32_t position); // 0xaeebc0
cGeometryRange<uint32_t> *__cdecl LinkedWords(cGeometryRange<uint32_t> *out, cIGeometryDataContainer *container,
                                              uint32_t linkage, uint32_t position); // 0xaee0f0

} // namespace nGZSceneGraph
