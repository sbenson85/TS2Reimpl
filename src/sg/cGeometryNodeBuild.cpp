// nGZSceneGraph::cGeometryNode::BuildGeometry 0xa3f180 (lane QY, scenegraph D3 part 5): the container turned into the
// node's draw elements, subset names, vertex formats, the device's vertex and index buffers and the bounds. Layout and
// the switch: src/sg/cGeometryNode.h.
//
// The order of the calls follows the original's: the tests compare call streams (the formats manager, the string pool,
// the device objects, the container's slots), so every re-fetch of the manager, every copy through the pool and every
// pair of container reads is kept where the original has it. The helpers are ours: the element header and range fillers
// (0xaee030, 0xaeddd0, 0xaee820, 0xaee9f0, 0xaeebc0, 0xaee0f0: src/sg/cIndexedIterator.cpp), the known-identity test
// 0x18da20 (src/sg/cGeometryNodeFormats.cpp), the box growth 0x19d4a0 and the rigid inverse 0x19cd80 (src/compgeom/). The device-object helper 0xa419e0 is ours (part 6, cGeometryNodeRender.cpp).
#include "sg/cGeometryNode.h"

#include "compgeom/CompGeom.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/GraphicServices.h"
#include "gz/cVertexFormat.h"
#include "rz/StaticGetters.h"
#include "rz/StaticGetters2.h"
#include "sg/cGeometryDataContainerClass.h"
#include "sg/cGeometryNodeFormats.h"
#include "sg/cIndexedIterator.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace nGZSceneGraph {

namespace {

using nGZGraphic4::cIVertexFormat;
using nGZGraphic4::cIVertexFormatsManager;
using nGZGraphic4::cVertexElement;
using Container = cIGeometryDataContainer;

constexpr uint32_t kPosition = 0x5b830781;
constexpr uint32_t kNormal = 0x3b83078b;
constexpr uint32_t kTangent = 0x89d92ba0;
constexpr uint32_t kBinormal = 0x69d92b93;
constexpr uint32_t kBoneIndices = 0xfbd70111;
constexpr uint32_t kBoneWeights = 0x3bd70105;
constexpr uint32_t kDeclarationNone = 6; // the "no such element" type FindElementAny answers

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// 0xaed300: a name's pool cell, empty (0xffffffff) for a null or empty text.
void OpenNameCell(ServiceCell &cell, const char *name)
{
    if (name && *name)
        cell.Open(Word(name), 0);
    else
        cell.mValue = 0xffffffff;
}

// 0xaeb6f0: a cell copied through the pool (slot 6 of the cached object), unconditionally.
void PoolCopy(uint32_t value) { RawSlot<6>(GetCachedObjectB(), value); }

cGeometryNodeCell CopyCell(const cGeometryNodeCell &other)
{
    PoolCopy(other.mCell.mValue);
    cGeometryNodeCell copy;
    copy.mCell.mValue = other.mCell.mValue;
    return copy;
}

// A container range's three iterators, released end, begin, cursor (each when not null).
template <class T> struct RangeHolder {
    cGeometryRange<T> r{};

    RangeHolder() = default;
    RangeHolder(const RangeHolder &) = delete;
    RangeHolder &operator=(const RangeHolder &) = delete;
    ~RangeHolder()
    {
        if (r.mEnd) r.mEnd->Release();
        if (r.mBegin) r.mBegin->Release();
        if (r.mCurrent) r.mCurrent->Release();
    }
};

// ---- shared helpers (ours) -------------------------------------------------------------------------------------------

// 0xaee030 (src/sg/cIndexedIterator.cpp).
cGeometryElementHeader *ElementHeader(cGeometryElementHeader *out, Container *c, uint32_t link, uint32_t e)
{
    return LinkedElementHeader(out, c, link, e);
}

// 0x18da20 (src/sg/cGeometryNodeFormats.cpp).
bool KnownIdentity(uint32_t identity) { return IsKnownVertexElement(identity); }

// 0x19d4a0 (ours: nGZCompGeom::BoundBox::Extend).
void GrowBox(void *box, const nGZCompGeom::Vec3 *point) { static_cast<nGZCompGeom::BoundBox *>(box)->Extend(*point); }

void RigidInvert(void *transform) { static_cast<nGZCompGeom::RigidTransform3 *>(transform)->Invert(); } // 0x19cd80 (ours; eax-ignored: our own helper, the caller discards the result)

// ---- helpers ---------------------------------------------------------------------------------------------------------

cIVertexFormatsManager *Formats() { return static_cast<cIVertexFormatsManager *>(nGZGraphic4::GetVertexFormatsManagerCached()); }

cIVertexFormat *FormatOf(const cGeometryNodeRefRecord &record) { return static_cast<cIVertexFormat *>(record.mRef.mPtr); }

cIVertexFormat *FormatAt(uint32_t word) { return reinterpret_cast<cIVertexFormat *>(static_cast<uintptr_t>(word)); }

// The stream sizes of the stream count's first and second stream (0 when there is none).
uint32_t StreamSizeOrZero(cIVertexFormat *format, uint32_t stream)
{
    return static_cast<int32_t>(format->GetStreamCount()) > static_cast<int32_t>(stream) ? format->GetStreamSize(stream) : 0;
}

// Stores each value of a range at `dest`, `size` bytes apart; the range's iterators are released after.
template <class T> void StoreRange(RangeHolder<T> &range, uint8_t *dest, uint32_t size)
{
    while (!range.r.mCurrent->IsEqual(range.r.mEnd)) {
        const T value = range.r.mCurrent->Get();
        std::memcpy(dest, &value, sizeof(T));
        range.r.mCurrent->Next();
        dest += size;
    }
}

// The subset map's insert: the name copied through the pool (twice: the pair, then its key), the pair closed again.
void InsertSubset(std::multimap<cGeometryNodeCell, uint32_t, cGeometryNodeCellLess> &subsets, const cGeometryNodeCell &name,
                  uint32_t index)
{
    cGeometryNodeCell pair = CopyCell(name);
    cGeometryNodeCell key = CopyCell(pair);
    subsets.emplace(std::move(key), index);
}

float AsFloat(uint32_t bits)
{
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// An inverted box (a maximum below the minimum on an axis) is zeroed: min at `words`, max three words on.
void ZeroInvertedBox(uint32_t *minWords, uint32_t *maxWords)
{
    bool inverted = false;
    for (int axis = 0; axis < 3; ++axis) {
        const float mn = AsFloat(minWords[axis]);
        const float mx = AsFloat(maxWords[axis]);
        if (mx <= mn && mn != mx) inverted = true;
    }
    if (inverted)
        for (int axis = 0; axis < 3; ++axis) minWords[axis] = maxWords[axis] = 0;
}

} // namespace

// 0xa3db00: growth of the element list copies an element.
cGeometryNodeElement::cGeometryNodeElement(const cGeometryNodeElement &other)
    : mPrepared(other.mPrepared), mPad36(other.mPad36), mName(CopyCell(other.mName)), mWord3c(other.mWord3c),
      mIndices(other.mIndices), mWord4c(other.mWord4c), mWord50(other.mWord50)
{
    for (int i = 0; i < 13; ++i) mWords[i] = other.mWords[i];
    for (int i = 0; i < 6; ++i) mRefs[i] = other.mRefs[i];
    for (int i = 0; i < 4; ++i) mTail[i] = other.mTail[i];
}

void cGeometryNode::BuildGeometry(cIGeometryDataContainer *c)
{
    Reset();
    ReleaseElementResources();
    if (!c) return;

    const bool skipTangents = ReadBooleanSetting("skipTangentsInVertexData");
    const uint32_t linkageCount = c->CountLinkages();
    mRecords.resize(linkageCount);

    // ---- the records' vertex formats ----
    uint32_t total0 = 0, total1 = 0;
    uint32_t type = 0; // ORIGINAL-BUG: carried across groups (uninitialised before the first known element)
    if (c->CountGroups() != 0) {
        uint32_t g = 0;
        do {
            cGeometryGroupInfo info;
            c->GetGroupInfo(&info, g);
            if (info.mPrimType == 0 || info.mPrimType == 2) {
                cGeometryLinkageHeader header;
                c->GetLinkageHeader(&header, info.mLinkIndex);
                cGeometryNodeRefRecord &record = mRecords[info.mLinkIndex];
                if (info.mIndexCount != 0 && header.mReferencedSize != 0 && record.mRef.mPtr == nullptr) {
                    Formats()->BeginFormat();
                    for (int32_t e = 0; e < static_cast<int32_t>(header.mActiveCount); ++e) {
                        cGeometryElementHeader hdr;
                        ElementHeader(&hdr, c, info.mLinkIndex, static_cast<uint32_t>(e));
                        if (KnownIdentity(hdr.mIdentity)) {
                            switch (hdr.mBlockFormat) {
                            case 0:
                            case 1:
                            case 2:
                            case 3: type = hdr.mBlockFormat; break;
                            case 4: type = (hdr.mIdentity == 0xdb830795 ? 1u : 0u) + 4; break;
                            default: break;
                            }
                            if (!(skipTangents && (hdr.mIdentity == kTangent || hdr.mIdentity == kBinormal))) {
                                uint32_t stream = 0;
                                switch (hdr.mIdentity) {
                                case 0xcb7206a1:
                                case 0x3bd70105:
                                case 0x5cf2cfe1:
                                case 0xcb6f3a6a:
                                case 0xdcf2cfdc:
                                case 0xeb720693:
                                case 0xfbd70111: stream = 1; break;
                                default: break;
                                }
                                Formats()->AddElement(stream, type, hdr.mIdentity, static_cast<uint8_t>(hdr.mRepetition));
                            }
                        }
                    }
                    record.mRef.Reset();
                    Formats()->EndFormat(reinterpret_cast<cIVertexFormat **>(&record.mRef.mPtr));

                    cIVertexFormat *const format = FormatOf(record);
                    cVertexElement found;
                    format->FindElementAny(&found, kPosition, 0);
                    if (found.type == 2) {
                        record.mWords[0] = header.mReferencedSize;
                        const uint32_t size0 = StreamSizeOrZero(format, 0);
                        const uint32_t size1 = StreamSizeOrZero(format, 1);
                        if (size0 != 0 && static_cast<int32_t>(total0) % static_cast<int32_t>(size0) != 0)
                            total0 = (static_cast<int32_t>(total0) / static_cast<int32_t>(size0) + 1) * size0;
                        if (size1 != 0 && static_cast<int32_t>(total1) % static_cast<int32_t>(size1) != 0)
                            total1 = (static_cast<int32_t>(total1) / static_cast<int32_t>(size1) + 1) * size1;
                        // ORIGINAL-BUG: no zero check on the first stream's size (a format without a first stream).
                        const uint32_t base0 = total0 / size0;
                        const uint32_t base1 = size1 ? total1 / size1 : 0;
                        if (static_cast<int32_t>(base0) < static_cast<int32_t>(base1)) {
                            record.mWords[1] = base1;
                            total0 = size0 * base1;
                        } else {
                            record.mWords[1] = base0;
                            if (size1) total1 = size1 * base0;
                        }
                        total0 += size0 * record.mWords[0];
                        total1 += size1 * record.mWords[0];
                    } else {
                        record.mRef.Reset();
                    }
                }
            }
            ++g;
        } while (g < c->CountGroups());
    }

    // ---- the elements ----
    const uint32_t transformCount = c->CountTransforms();
    const uint32_t groupCount = c->CountGroups();
    mElements.reserve(groupCount);
    uint32_t runIndex = 0;
    if (c->CountGroups() != 0) {
        uint32_t g = 0;
        do {
            cGeometryGroupInfo info;
            c->GetGroupInfo(&info, g);
            cGeometryNodeRefRecord &record = mRecords[info.mLinkIndex];
            if (record.mRef.mPtr != nullptr && info.mIndexCount != 0 && (info.mPrimType == 0 || info.mPrimType == 2)) {
                cGeometryNodeCell name;
                OpenNameCell(name.mCell, info.mName);
                InsertSubset(mSubsets, name, static_cast<uint32_t>(mElements.size()));
                {
                    const cGeometryNodeElement blank;
                    mElements.push_back(blank);
                }
                cGeometryNodeElement &el = mElements.back();
                if (info.mPrimType == 0) {
                    el.mWord4c = 1;
                    el.mWords[1] = static_cast<uint32_t>(static_cast<int32_t>(info.mIndexCount) / 2);
                } else {
                    el.mWord4c = 3;
                    el.mWords[1] = static_cast<uint32_t>(static_cast<int32_t>(info.mIndexCount) / 3);
                }
                el.mWords[0] = 0;
                el.mWords[2] = static_cast<uint32_t>(info.mWord10);
                el.mWords[3] = Word(record.mRef.mPtr);
                el.mWords[4] = 0;
                el.mWords[5] = 0;
                el.mWords[6] = 0;
                el.mPrepared = 0;
                {
                    cGeometryNodeCell second;
                    OpenNameCell(second.mCell, info.mName);
                    el.mName.mCell.Assign(&second.mCell.mValue);
                }
                el.mWord3c = g;
                el.mWord50 = runIndex;
                el.mTail[0] = record.mWords[1];
                cIVertexFormat *const format = FormatOf(record);
                el.mTail[2] = StreamSizeOrZero(format, 0);
                el.mTail[3] = StreamSizeOrZero(format, 1);

                if (info.mBoneCount != 0) {
                    el.mWords[4] = info.mBoneCount;
                    el.mIndices.reserve(info.mBoneCount);
                    RangeHolder<uint16_t> bones;
                    c->GetGroupBones(&bones.r, g);
                    while (!bones.r.mCurrent->IsEqual(bones.r.mEnd)) {
                        const uint32_t bone = bones.r.mCurrent->Get() & 0xffff;
                        if (bone < transformCount)
                            el.mIndices.push_back(bone);
                        else
                            el.mWords[4] -= 1;
                        bones.r.mCurrent->Next();
                    }
                }

                cVertexElement tangents;
                format->FindElementAny(&tangents, 0xdcf2cfdc, 0);
                if (tangents.type != kDeclarationNone) el.mWords[6] = c->CountNamePairs();
                runIndex += info.mIndexCount;
            }
            ++g;
        } while (g < c->CountGroups());
    }

    // ---- the visualization elements ----
    bool visualize = (mFlags >> 1) & 1;
    if (visualize && runIndex != 0) {
        uint32_t vertexCount = 0, extraCount = 0;
        for (const cGeometryNodeRefRecord &record : mRecords) {
            cVertexElement a, b, d;
            // ORIGINAL-BUG: no null check on the record's format (a linkage without one).
            FormatOf(record)->FindElementAny(&a, kNormal, 0);
            FormatOf(record)->FindElementAny(&b, kTangent, 0);
            FormatOf(record)->FindElementAny(&d, kBinormal, 0);
            if (a.type != kDeclarationNone || b.type != kDeclarationNone || d.type != kDeclarationNone) {
                vertexCount += record.mWords[0];
                extraCount += record.mWords[0] * 2;
            }
        }
        if (extraCount != 0 && vertexCount != 0) {
            cGeometryNodeRef<cIGZUnknown> vizFormat;
            Formats()->BeginFormat();
            Formats()->AddElement(0, 3, kPosition, 0);
            Formats()->AddElement(0, 2, kNormal, 0);
            Formats()->AddElement(0, 2, kTangent, 0);
            Formats()->AddElement(0, 2, kBinormal, 0);
            cIVertexFormatsManager *const manager = Formats();
            vizFormat.Reset();
            manager->EndFormat(reinterpret_cast<cIVertexFormat **>(&vizFormat.mPtr));
            cIVertexFormat *const vf = static_cast<cIVertexFormat *>(vizFormat.mPtr);

            const uint32_t stride = vf->GetStreamSize(0);
            uint32_t aligned = total0;
            if (stride != 0 && static_cast<int32_t>(total0) % static_cast<int32_t>(stride) != 0)
                aligned = (static_cast<int32_t>(total0) / static_cast<int32_t>(stride) + 1) * stride;
            const uint32_t vizBase = aligned / stride;
            {
                cGeometryNodeRefRecord record;
                record.mRef.mPtr = vizFormat.mPtr; // counted by the copy the push makes
                record.mWords[0] = extraCount;
                record.mWords[1] = vizBase;
                record.mWords[2] = 0;
                mRecords.push_back(record);
                record.mRef.mPtr = nullptr;
            }
            total0 = extraCount * stride + aligned;

            {
                cGeometryNodeElement tmpl;
                tmpl.mWords[0] = extraCount;
                tmpl.mWords[1] = vertexCount;
                tmpl.mWords[2] = 0;
                tmpl.mWords[3] = Word(vizFormat.mPtr);
                tmpl.mWord3c = 0xffffffff;
                tmpl.mWord4c = 1;
                tmpl.mWord50 = runIndex;
                tmpl.mTail[0] = vizBase;
                tmpl.mTail[1] = 0; // ORIGINAL-BUG: never written (stack garbage in the original)
                tmpl.mTail[2] = stride;
                tmpl.mTail[3] = 0;
                static const char *const kNames[3] = {"@@VisualizeNormals@@", "@@VisualizeTangents@@",
                                                      "@@VisualizeBinormals@@"};
                for (const char *text : kNames) {
                    {
                        cGeometryNodeCell temp;
                        OpenNameCell(temp.mCell, text);
                        tmpl.mName.mCell.Assign(&temp.mCell.mValue);
                    }
                    InsertSubset(mSubsets, tmpl.mName, static_cast<uint32_t>(mElements.size()));
                    mElements.push_back(tmpl);
                }
                runIndex += vertexCount * 2;
            }
        } else {
            visualize = false;
        }
    }

    // ---- the device buffers ----
    nGZGraphic4::cIDeviceDX *const device = nGZGraphic4::GetCurrentDeviceCached();
    if (device && runIndex != 0 && !ResetDeviceState(false, false)) throw static_cast<const char *>("Failed initialization of sw vertex declaration!");
    uint8_t *p0 = nullptr;
    uint8_t *p1 = nullptr;
    if (device && runIndex != 0) {
        void *director = GetDirectorFrameworkObject();
        mRefBC.Reset();
        RawSlot<3>(director, 0x3bffec52u, 0xfbffec42u, reinterpret_cast<void **>(&mRefBC.mPtr));
        if (!RawSlot<20, bool>(mRefBC.mPtr, static_cast<void *>(device), total0, 3u))
            throw static_cast<const char *>("Failed initialization of vertex component array!");
        mRefC0.Reset();
        if (total1 != 0) {
            director = GetDirectorFrameworkObject();
            RawSlot<3>(director, 0x3bffec52u, 0xfbffec42u, reinterpret_cast<void **>(&mRefC0.mPtr));
            if (!RawSlot<20, bool>(mRefC0.mPtr, static_cast<void *>(device), total1, 2u))
                throw static_cast<const char *>("Failed initialization of vertex component array!");
        }
        p0 = RawSlot<15, uint8_t *>(mRefBC.mPtr, 2u, 0u, 0u);
        if (mRefC0.mPtr) p1 = RawSlot<15, uint8_t *>(mRefC0.mPtr, 2u, 0u, 0u);

        const uint32_t again = c->CountLinkages();
        for (uint32_t i = 0; i < again; ++i) {
            cGeometryNodeRefRecord &record = mRecords[i];
            if (record.mRef.mPtr == nullptr) continue;
            cGeometryLinkageHeader header;
            c->GetLinkageHeader(&header, i);
            cIVertexFormat *const format = FormatOf(record);
            for (int32_t j = 0; j < static_cast<int32_t>(header.mActiveCount); ++j) {
                cGeometryElementHeader hdr;
                ElementHeader(&hdr, c, i, static_cast<uint32_t>(j));
                cVertexElement slot;
                format->FindElementAny(&slot, hdr.mIdentity, static_cast<uint8_t>(hdr.mRepetition));
                if (slot.type == kDeclarationNone) continue;
                const uint32_t size = static_cast<int32_t>(slot.stream) < static_cast<int32_t>(format->GetStreamCount())
                                          ? format->GetStreamSize(slot.stream)
                                          : 0;
                const uint32_t offset = format->GetElementOffset(slot.stream, slot.position);
                uint8_t *const base = slot.stream == 0 ? p0 : p1;
                uint8_t *const dest = base + record.mWords[1] * size + offset;
                switch (hdr.mBlockFormat) {
                case 0: {
                    RangeHolder<float> r;
                    LinkedFloats(&r.r, c, i, static_cast<uint32_t>(j));
                    StoreRange(r, dest, size);
                    break;
                }
                case 1: {
                    RangeHolder<cGeometryVector<2>> r;
                    LinkedVectors2(&r.r, c, i, static_cast<uint32_t>(j));
                    StoreRange(r, dest, size);
                    break;
                }
                case 2: {
                    RangeHolder<cGeometryVector<3>> r;
                    LinkedVectors3(&r.r, c, i, static_cast<uint32_t>(j));
                    StoreRange(r, dest, size);
                    break;
                }
                case 3: {
                    RangeHolder<cGeometryVector<4>> r;
                    LinkedVectors4(&r.r, c, i, static_cast<uint32_t>(j));
                    StoreRange(r, dest, size);
                    break;
                }
                case 4: {
                    RangeHolder<uint32_t> r;
                    LinkedWords(&r.r, c, i, static_cast<uint32_t>(j));
                    StoreRange(r, dest, size);
                    break;
                }
                default: break;
                }
            }
        }
    }

    // ---- the index buffer ----
    uint8_t *indexPtr = nullptr;
    if (device && runIndex != 0) {
        void *const director = GetDirectorFrameworkObject();
        mRefB8.Reset();
        RawSlot<3>(director, 0x9bfff0eau, 0xdbfff0deu, reinterpret_cast<void **>(&mRefB8.mPtr));
        if (!RawSlot<20, bool>(mRefB8.mPtr, static_cast<void *>(device), runIndex * 2, 3u))
            throw static_cast<const char *>("Failed initialization of vertex index array!\n");
        indexPtr = RawSlot<15, uint8_t *>(mRefB8.mPtr, 2u, 0u, 0u);
    }

    // ---- the visualization vertices ----
    if (visualize && device && runIndex != 0) {
        const cGeometryNodeRefRecord &vizRecord = mRecords.back();
        cIVertexFormat *const vf = FormatOf(vizRecord);
        const uint32_t vstride = vf->GetStreamSize(0);
        uint8_t *vptr = p0 + vizRecord.mWords[1] * vstride;
        const uint32_t offPos = vf->GetOffsetAny(kPosition, 0);
        const uint32_t offNormal = vf->GetOffsetAny(kNormal, 0);
        const uint32_t offTangent = vf->GetOffsetAny(kTangent, 0);
        const uint32_t offBinormal = vf->GetOffsetAny(kBinormal, 0);
        uint8_t *ip = indexPtr + mElements.back().mWord50 * 2;
        uint16_t k = 0;
        for (size_t r = 0; r + 1 < mRecords.size(); ++r) {
            const cGeometryNodeRefRecord &record = mRecords[r];
            if (record.mRef.mPtr == nullptr) continue;
            cIVertexFormat *const format = FormatOf(record);
            cVertexElement ep, en, et, eb;
            format->FindElementAny(&ep, kPosition, 0);
            format->FindElementAny(&en, kNormal, 0);
            format->FindElementAny(&et, kTangent, 0);
            format->FindElementAny(&eb, kBinormal, 0);
            const bool hasN = en.type != kDeclarationNone;
            const bool hasT = et.type != kDeclarationNone;
            const bool hasB = eb.type != kDeclarationNone;
            if (!(hasN || hasT || hasB)) continue;
            const uint32_t srcStride = format->GetStreamSize(0);
            const uint32_t srcBase = record.mWords[1] * srcStride;
            const uint32_t sPos = format->GetElementOffset(ep.stream, ep.position);
            const uint32_t sNormal = hasN ? format->GetElementOffset(en.stream, en.position) : 0;
            const uint32_t sTangent = hasT ? format->GetElementOffset(et.stream, et.position) : 0;
            const uint32_t sBinormal = hasB ? format->GetElementOffset(eb.stream, eb.position) : 0;
            if (record.mWords[0] == 0) continue;
            const uint8_t *src = p0 + srcBase;
            for (uint32_t v = 0; v < record.mWords[0]; ++v) {
                float pos[3], normal[3] = {}, tangent[3] = {}, binormal[3] = {};
                std::memcpy(pos, src + sPos, 12);
                if (hasN) std::memcpy(normal, src + sNormal, 12);
                if (hasT) std::memcpy(tangent, src + sTangent, 12);
                if (hasB) std::memcpy(binormal, src + sBinormal, 12);
                const float zero[3] = {};
                const float zeroW = 0.0f, oneW = 1.0f;
                std::memcpy(vptr + offPos, pos, 12);
                std::memcpy(vptr + offPos + 12, &zeroW, 4);
                std::memcpy(vptr + offNormal, zero, 12);
                std::memcpy(vptr + offTangent, zero, 12);
                std::memcpy(vptr + offBinormal, zero, 12);
                vptr += vstride;
                std::memcpy(vptr + offPos, pos, 12);
                std::memcpy(vptr + offPos + 12, &oneW, 4);
                std::memcpy(vptr + offNormal, normal, 12);
                std::memcpy(vptr + offTangent, tangent, 12);
                std::memcpy(vptr + offBinormal, binormal, 12);
                vptr += vstride;
                const uint16_t pair[2] = {k, static_cast<uint16_t>(k + 1)};
                std::memcpy(ip, pair, 4);
                k = static_cast<uint16_t>(k + 2);
                ip += 4;
                src += srcStride;
            }
        }
    }

    // ---- the bounds ----
    if (!mRecords24.empty()) mRecords24.clear();
    mRenderInfo[5] = mRenderInfo[6] = mRenderInfo[7] = 0x7f7fffff;
    mRenderInfo[8] = mRenderInfo[9] = mRenderInfo[10] = 0xff7fffff;
    const size_t elementCount = mElements.size();
    std::vector<uint32_t> baseVertex(elementCount);
    for (size_t ei = 0; ei < elementCount; ++ei) {
        cGeometryNodeElement &el = mElements[ei];
        if (static_cast<int32_t>(el.mWord3c) < 0) {
            for (int w = 0; w < 6; ++w) el.mWords[7 + w] = mRenderInfo[5 + w];
            continue;
        }
        el.mWords[5] = el.mIndices.empty() ? 0 : Word(el.mIndices.data());
        RangeHolder<uint16_t> range;
        c->GetGroupVertexIndices(&range.r, el.mWord3c);
        int32_t lowest = 0x7fffffff, highest = 0;
        while (!range.r.mCurrent->IsEqual(range.r.mEnd)) {
            const int32_t a = range.r.mCurrent->Get() & 0xffff;
            if (a < lowest) lowest = a;
            const int32_t b = range.r.mCurrent->Get() & 0xffff;
            if (b > highest) highest = b;
            range.r.mCurrent->Next();
        }
        el.mWords[0] = static_cast<uint32_t>(highest - lowest + 1);
        el.mTail[0] += static_cast<uint32_t>(lowest);
        baseVertex[ei] = static_cast<uint32_t>(lowest);
    }

    {
        cGeometryNodeRecord24 empty = {{0x7f7fffff, 0x7f7fffff, 0x7f7fffff, 0xff7fffff, 0xff7fffff, 0xff7fffff}};
        mRecords24.resize(std::max<uint32_t>(transformCount, 1), empty);
    }

    for (size_t ei = 0; ei < elementCount; ++ei) {
        cGeometryNodeElement &el = mElements[ei];
        RangeHolder<uint16_t> range;
        c->GetGroupVertexIndices(&range.r, el.mWord3c);
        el.mWords[7] = el.mWords[8] = el.mWords[9] = 0x7f7fffff;
        el.mWords[10] = el.mWords[11] = el.mWords[12] = 0xff7fffff;
        if (device && runIndex != 0) {
            cIVertexFormat *const format = FormatAt(el.mWords[3]);
            cVertexElement pos, bone, other;
            format->FindElementAny(&pos, kPosition, 0);
            format->FindElementAny(&bone, kBoneIndices, 0);
            format->FindElementAny(&other, kBoneWeights, 0);
            const uint32_t posOffset = format->GetElementOffset(pos.stream, pos.position);
            const uint32_t boneOffset =
                bone.type == kDeclarationNone ? 0xffffffffu : format->GetElementOffset(bone.stream, bone.position);
            const uint32_t base = baseVertex[ei];
            if (!range.r.mCurrent->IsEqual(range.r.mBegin)) {
                cIGeometryIterator<uint16_t> *const fresh = range.r.mBegin->Clone();
                if (fresh != range.r.mCurrent) {
                    if (fresh) fresh->AddRef();
                    cIGeometryIterator<uint16_t> *const old = range.r.mCurrent;
                    range.r.mCurrent = fresh;
                    if (old) old->Release();
                }
            }
            while (!range.r.mCurrent->IsEqual(range.r.mEnd)) {
                const uint32_t value = range.r.mCurrent->Get();
                const uint32_t relative = (value & 0xffff) - base;
                if (indexPtr) {
                    const uint16_t narrow = static_cast<uint16_t>(relative);
                    std::memcpy(indexPtr, &narrow, 2);
                }
                const uint32_t vertex = (relative & 0xffff) + el.mTail[0];
                const uint32_t posStride = pos.stream == 0 ? el.mTail[2] : el.mTail[3];
                const uint8_t *const posBase = pos.stream == 0 ? p0 : p1;
                nGZCompGeom::Vec3 point;
                std::memcpy(&point, posBase + posStride * vertex + posOffset, 12);
                if (boneOffset == 0xffffffffu) {
                    GrowBox(mRecords24.data(), &point);
                } else {
                    const uint32_t boneStride = bone.stream == 0 ? el.mTail[2] : el.mTail[3];
                    const uint8_t *const boneBase = bone.stream == 0 ? p0 : p1;
                    uint32_t packed;
                    std::memcpy(&packed, boneBase + boneStride * vertex + boneOffset, 4);
                    const uint32_t b = packed & 0xff;
                    const uint32_t mapped = b < el.mIndices.size() ? el.mIndices[b] : 0;
                    if (mapped < mRecords24.size()) {
                        cGeometryTransform transform;
                        c->GetTransform(&transform, mapped);
                        nGZCompGeom::Vec3 moved = point;
                        reinterpret_cast<const nGZCompGeom::RigidTransform3 *>(&transform)->TransformPoint(&moved);
                        GrowBox(&mRecords24[mapped], &moved);
                    }
                }
                GrowBox(&mRenderInfo[5], &point);
                GrowBox(&el.mWords[7], &point);
                range.r.mCurrent->Next();
                if (indexPtr) indexPtr += 2;
            }
        }
        ZeroInvertedBox(&el.mWords[7], &el.mWords[10]);
    }
    ZeroInvertedBox(&mRenderInfo[5], &mRenderInfo[8]);

    if (device && runIndex != 0) {
        RawSlot<16, void>(mRefB8.mPtr);
        RawSlot<16, void>(mRefBC.mPtr);
        if (mRefC0.mPtr) RawSlot<16, void>(mRefC0.mPtr);
    }

    // ---- the transforms ----
    {
        const cGeometryNodeRecord28 identity = {{0, 0, 0, 0, 0, 0, 0x3f800000}};
        mRecordsF4.resize(transformCount, identity);
        mRecords100.resize(transformCount, identity);
        for (uint32_t i = 0; i < transformCount; ++i) {
            cGeometryTransform transform;
            c->GetTransform(&transform, i);
            std::memcpy(&mRecordsF4[i], &transform, 28);
            std::memcpy(&mRecords100[i], &mRecordsF4[i], 28);
            RigidInvert(&mRecords100[i]);
        }
    }

    // ---- the name pairs ----
    const uint32_t nameCount = c->CountNamePairs();
    mNamesB.resize(nameCount);
    mNamesA.resize(nameCount);
    for (uint32_t i = 0; i < nameCount; ++i) {
        const char *const second = c->GetNameSecond(i);
        const char *const first = c->GetNameFirst(i);
        {
            cGeometryNodeCell temp;
            OpenNameCell(temp.mCell, second);
            mNamesB[i].mCell.Assign(&temp.mCell.mValue);
        }
        {
            cGeometryNodeCell temp;
            OpenNameCell(temp.mCell, first);
            mNamesA[i].mCell.Assign(&temp.mCell.mValue);
        }
    }

    // ---- the bounds meshes ----
    constexpr uint32_t kIidBounds = 0x5ce57549;
    if (cITriangleSetShape *mesh = c->GetBoundingMesh()) {
        mBounds.resize(1);
        mesh = c->GetBoundingMesh();
        mBounds[0].Reset();
        mesh->CloneAs(kIidBounds, reinterpret_cast<void **>(&mBounds[0].mPtr));
    } else {
        bool any = false;
        for (uint32_t i = 0; i < transformCount && !any; ++i) any = c->GetSubsetMesh(i) != nullptr;
        if (any) {
            mBounds.resize(transformCount);
            for (uint32_t i = 0; i < transformCount; ++i) {
                if (c->GetSubsetMesh(i) == nullptr) continue;
                cITriangleSetShape *const mesh = c->GetSubsetMesh(i);
                mBounds[i].Reset();
                mesh->CloneAs(kIidBounds, reinterpret_cast<void **>(&mBounds[i].mPtr));
            }
        }
    }

    // ---- what the node publishes ----
    mBoundsInfo = static_cast<uint32_t>(mRecords24.size());
    mBoundsCount = static_cast<uint32_t>(mBounds.size());
    if (!mRecords24.empty()) mWord94 = Word(mRecords24.data());
    if (!mBounds.empty()) mBoundsData = Word(mBounds.data());
    mRenderInfo[0] = static_cast<uint32_t>(mElements.size());
    mRenderInfo[1] = static_cast<uint32_t>(mNamesA.size());
    mRenderInfo[2] = static_cast<uint32_t>(mRecordsF4.size());
    mRenderInfo[3] = mRecordsF4.empty() ? 0 : Word(mRecordsF4.data());
    mRenderInfo[4] = mRecords100.empty() ? 0 : Word(mRecords100.data());
}

void __fastcall cGeometryNodeAdapters::BuildGeometry(Self *self, void *, cIGeometryDataContainer *container)
{
    self->cGeometryNode::BuildGeometry(container);
}

} // namespace nGZSceneGraph
