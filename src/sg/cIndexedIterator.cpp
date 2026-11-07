// The linked element ranges (lane VS). The class and the game's bodies: src/sg/cIndexedIterator.h.
#include "sg/cIndexedIterator.h"

namespace nGZSceneGraph {

namespace {

// A range a container slot filled, released end, begin, cursor (each when not null).
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

template <class T> using ElementSlot = cGeometryRange<T> *(cIGeometryDataContainer::*)(cGeometryRange<T> *, uint32_t);

// The fillers' one body: the element's values (its slot for T) indexed by the linkage's aliases for the element's set
// format. The end iterator is made first, then begin; both read from the values' begin. The two source ranges are
// released after the out range is filled, the aliases' first.
template <class T>
cGeometryRange<T> *FillLinked(cGeometryRange<T> *out, cIGeometryDataContainer *container, uint32_t linkage, uint32_t position,
                              ElementSlot<T> slot)
{
    cGeometryElementHeader header;
    LinkedElementHeader(&header, container, linkage, position);
    HeldRange<T> values;
    (container->*slot)(&values.r, header.mIndex);
    HeldRange<uint16_t> aliases;
    container->GetLinkageAliases(&aliases.r, linkage, header.mSetFormat);
    cIGeometryIterator<T> *end = new (std::nothrow) cIndexedIterator<T>(values.r.mBegin, aliases.r.mEnd);
    cIGeometryIterator<T> *begin = new (std::nothrow) cIndexedIterator<T>(values.r.mBegin, aliases.r.mBegin);
    // ORIGINAL-BUG: a failed allocation of begin is not checked (FillGeometryRange clones it).
    return FillGeometryRange(out, end, begin);
}

} // namespace

cGeometryElementHeader *__cdecl LinkedElementHeader(cGeometryElementHeader *out, cIGeometryDataContainer *container,
                                                    uint32_t linkage, uint32_t position)
{
    HeldRange<uint16_t> elements;
    container->GetLinkageElements(&elements.r, linkage);
    const uint16_t index = elements.r.mCurrent->At(static_cast<int32_t>(position));
    cGeometryElementHeader header;
    *out = *container->GetElementHeader(&header, index);
    return out;
}

cGeometryRange<float> *__cdecl LinkedFloats(cGeometryRange<float> *out, cIGeometryDataContainer *container, uint32_t linkage,
                                            uint32_t position)
{
    return FillLinked<float>(out, container, linkage, position, &cIGeometryDataContainer::GetElementFloats);
}

cGeometryRange<cGeometryVector<2>> *__cdecl LinkedVectors2(cGeometryRange<cGeometryVector<2>> *out,
                                                           cIGeometryDataContainer *container, uint32_t linkage,
                                                           uint32_t position)
{
    return FillLinked<cGeometryVector<2>>(out, container, linkage, position, &cIGeometryDataContainer::GetElementVectors2);
}

cGeometryRange<cGeometryVector<3>> *__cdecl LinkedVectors3(cGeometryRange<cGeometryVector<3>> *out,
                                                           cIGeometryDataContainer *container, uint32_t linkage,
                                                           uint32_t position)
{
    return FillLinked<cGeometryVector<3>>(out, container, linkage, position, &cIGeometryDataContainer::GetElementVectors3);
}

cGeometryRange<cGeometryVector<4>> *__cdecl LinkedVectors4(cGeometryRange<cGeometryVector<4>> *out,
                                                           cIGeometryDataContainer *container, uint32_t linkage,
                                                           uint32_t position)
{
    return FillLinked<cGeometryVector<4>>(out, container, linkage, position, &cIGeometryDataContainer::GetElementVectors4);
}

cGeometryRange<uint32_t> *__cdecl LinkedWords(cGeometryRange<uint32_t> *out, cIGeometryDataContainer *container,
                                              uint32_t linkage, uint32_t position)
{
    return FillLinked<uint32_t>(out, container, linkage, position, &cIGeometryDataContainer::GetElementWords);
}

} // namespace nGZSceneGraph
