// nGZSceneGraph's geometry data container helpers (lane 261111g). Declarations and behaviour: sg/SGUtilities.h.
#include "sg/SGUtilities.h"

#include "sg/cGeometryDataContainerClass.h"
#include "sg/cGeometryIterators.h"
#include "sg/cIndexedIterator.h" // LinkedElementHeader 0xaee030

#include <vector>

namespace nGZSceneGraph {

namespace {

// A range a container slot filled, released end, begin, cursor (each when not null), as the originals' cleanups do.
template <class T> struct HeldRange {
    cGeometryRange<T> r{};

    HeldRange() = default;
    HeldRange(const HeldRange &) = delete;
    HeldRange &operator=(const HeldRange &) = delete;
    ~HeldRange()
    {
        if (r.mEnd) r.mEnd->Release();
        if (r.mBegin) r.mBegin->Release();
        if (r.mCurrent) r.mCurrent->Release();
    }
};

template <class T> using ValuesSlot = cGeometryRange<T> *(cIGeometryDataContainer::*)(cGeometryRange<T> *, uint32_t);

// The two readers' one body (0xaee2c0 floats, 0xaee570 vectors).
template <class T>
void ReadLinked(cIGeometryDataContainer *container, uint32_t linkage, uint32_t position, std::vector<uint16_t> *aliases,
                std::vector<T> *values, ValuesSlot<T> slot)
{
    cGeometryElementHeader linked;
    const cGeometryElementHeader *found = LinkedElementHeader(&linked, container, linkage, position);
    const uint32_t element = found->mIndex;
    const uint32_t setFormat = found->mSetFormat;
    cGeometryElementHeader header;
    // ORIGINAL-BUG: indexed by vertex and alias numbers without a bounds check.
    std::vector<int32_t> place(container->GetElementHeader(&header, element)->mNumber, -1);
    {
        HeldRange<uint16_t> indices;
        container->GetElementIndices(&indices.r, element);
        int32_t n = 0;
        while (!indices.r.mCurrent->IsEqual(indices.r.mEnd)) {
            place[indices.r.mCurrent->Get()] = n;
            indices.r.mCurrent->Next();
            ++n;
        }
    }
    aliases->clear();
    values->clear();
    HeldRange<T> range;
    (container->*slot)(&range.r, element);
    HeldRange<uint16_t> list;
    container->GetLinkageAliases(&list.r, linkage, setFormat);
    uint32_t k = 0;
    while (!list.r.mCurrent->IsEqual(list.r.mEnd)) {
        const int32_t at = place[list.r.mCurrent->Get()];
        if (at != -1) {
            aliases->push_back(static_cast<uint16_t>(k));
            values->push_back(range.r.mCurrent->At(at));
        }
        list.r.mCurrent->Next();
        ++k;
    }
}

} // namespace

// 0xaeda40
cGeometryElementHeader *FindLinkedElement(cGeometryElementHeader *out, cIGeometryDataContainer *container, uint32_t linkage,
                                          uint32_t identity, uint32_t repetition, uint32_t blockFormat)
{
    cGeometryLinkageHeader linkageHeader;
    const uint32_t count = container->GetLinkageHeader(&linkageHeader, linkage)->mActiveCount;
    for (uint32_t j = 0; j < count; ++j) {
        cGeometryElementHeader header;
        *out = *LinkedElementHeader(&header, container, linkage, j);
        if (out->mIdentity == identity && out->mRepetition == repetition && out->mBlockFormat == blockFormat) return out;
    }
    out->mIndex = 0xffffffffu;
    return out;
}

// 0xaedfa0
uint32_t FindLinkedElementPosition(cIGeometryDataContainer *container, uint32_t linkage, uint32_t identity,
                                   uint32_t repetition, uint32_t blockFormat)
{
    cGeometryLinkageHeader linkageHeader;
    const uint32_t count = container->GetLinkageHeader(&linkageHeader, linkage)->mActiveCount;
    for (uint32_t j = 0; j < count; ++j) {
        cGeometryElementHeader buffer;
        const cGeometryElementHeader header = *LinkedElementHeader(&buffer, container, linkage, j);
        if (header.mIdentity == identity && header.mRepetition == repetition && header.mBlockFormat == blockFormat) return j;
    }
    return 0xffffffffu;
}

// 0xaee2c0
void ReadLinkedFloats(cIGeometryDataContainer *container, uint32_t linkage, uint32_t position,
                      std::vector<uint16_t> *aliases, std::vector<float> *values)
{
    ReadLinked<float>(container, linkage, position, aliases, values, &cIGeometryDataContainer::GetElementFloats);
}

// 0xaee570
void ReadLinkedVectors3(cIGeometryDataContainer *container, uint32_t linkage, uint32_t position,
                        std::vector<uint16_t> *aliases, std::vector<cGeometryVector<3>> *values)
{
    ReadLinked<cGeometryVector<3>>(container, linkage, position, aliases, values,
                                   &cIGeometryDataContainer::GetElementVectors3);
}

} // namespace nGZSceneGraph
