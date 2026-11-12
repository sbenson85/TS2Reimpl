// nGZSceneGraph::GeometrySelection (lane 261111g). See sg/GeometrySelection.h.
#include "sg/GeometrySelection.h"

#include <cstring>
#include <new>

namespace nGZSceneGraph {

namespace {

// The held container replaced: the new one counted first, then the old one released (nothing when they are the same).
void Hold(cIGeometryDataContainer *&cell, cIGeometryDataContainer *container)
{
    cIGeometryDataContainer *old = cell;
    if (container == old) return;
    if (container) container->AddRef();
    cell = container;
    if (old) old->Release();
}

} // namespace

// 0xaed500
GeometrySelection::GeometrySelection(cIGeometryDataContainer *container, uint32_t identity, uint32_t blockFormat,
                                     uint32_t setFormat, uint32_t repetition)
{
    SelectElements(container, identity, blockFormat, setFormat, repetition);
}

// 0xaed570
GeometrySelection::GeometrySelection(cIGeometryDataContainer *container, uint32_t linkIndex, const char *name,
                                     uint32_t primType)
{
    SelectGroups(container, linkIndex, name, primType);
}

// 0xaedbb0
void GeometrySelection::SelectElements(cIGeometryDataContainer *container, uint32_t identity, uint32_t blockFormat,
                                       uint32_t setFormat, uint32_t repetition)
{
    Hold(mContainer, container);
    mIndices.clear();
    uint32_t i = 0;
    if (container->CountElements() == 0) return;
    do {
        cGeometryElementHeader header;
        container->GetElementHeader(&header, i);
        const bool identityMatches = identity == kAnyIdentity || header.mIdentity == identity;
        const bool blockMatches = blockFormat == kAny || header.mBlockFormat == blockFormat;
        const bool setMatches = setFormat == kAny || header.mSetFormat == setFormat;
        const bool repetitionMatches = repetition == kAny || header.mRepetition == repetition;
        if (identityMatches && blockMatches && setMatches && repetitionMatches) mIndices.push_back(i);
        ++i;
    } while (i < container->CountElements());
}

// 0xaedcd0
void GeometrySelection::SelectGroups(cIGeometryDataContainer *container, uint32_t linkIndex, const char *name,
                                     uint32_t primType)
{
    Hold(mContainer, container);
    mIndices.clear();
    uint32_t i = 0;
    if (container->CountGroups() == 0) return;
    do {
        cGeometryGroupInfo info;
        container->GetGroupInfo(&info, i);
        const bool linkMatches = linkIndex == kAny || info.mLinkIndex == linkIndex;
        const bool nameMatches = name == nullptr || _stricmp(info.mName, name) == 0;
        const bool typeMatches = primType == kAny || info.mPrimType == primType;
        if (linkMatches && nameMatches && typeMatches) mIndices.push_back(i);
        ++i;
    } while (i < container->CountGroups());
}

// 0xaeda10
cGeometryElementHeader *GeometrySelection::ElementHeader(cGeometryElementHeader *out, uint32_t i)
{
    mContainer->GetElementHeader(out, mIndices.data()[i]);
    return out;
}

// 0xaedb10
cGeometryGroupInfo *GeometrySelection::GroupInfo(cGeometryGroupInfo *out, uint32_t i)
{
    mContainer->GetGroupInfo(out, mIndices.data()[i]);
    return out;
}

GeometrySelection *GeometrySelection::ConstructElements(cIGeometryDataContainer *container, uint32_t identity,
                                                        uint32_t blockFormat, uint32_t setFormat, uint32_t repetition)
{
    return ::new (this) GeometrySelection(container, identity, blockFormat, setFormat, repetition);
}

GeometrySelection *GeometrySelection::ConstructGroups(cIGeometryDataContainer *container, uint32_t linkIndex,
                                                      const char *name, uint32_t primType)
{
    return ::new (this) GeometrySelection(container, linkIndex, name, primType);
}

} // namespace nGZSceneGraph
