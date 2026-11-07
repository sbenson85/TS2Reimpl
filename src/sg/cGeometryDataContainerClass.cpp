// nGZSceneGraph::cGeometryDataContainer as a whole class (lane PA). Layout and the switch: src/sg/cGeometryDataContainerClass.h.
#include "sg/cGeometryDataContainerClass.h"

#include "compgeom/TriangleSet.h"
#include "game/original.h"
#include "gz/cIGZCOM.h"
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "rz/StaticGetters.h" // GetDirectorFrameworkObject: the COM
#include "sg/SGLocalString.h"
#include "sg/cGeometryDataContainer.h" // the hook-shaped slot bodies
#include "sg/cObjectGraphNodeImpl.h"   // NewTypeName

#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace nGZSceneGraph {

static_assert(sizeof(cGeometryElement) == 0x30, "element size");
static_assert(offsetof(cGeometryElement, mData) == 0x18 && offsetof(cGeometryElement, mIndices) == 0x24, "element");
static_assert(sizeof(cGeometryLinkage) == 0x3c, "linkage size");
static_assert(offsetof(cGeometryLinkage, mElementIndices) == 0xc && offsetof(cGeometryLinkage, mAliases) == 0x18,
              "linkage");
static_assert(sizeof(cGeometryGroup) == 0x4c, "group size");
static_assert(offsetof(cGeometryGroup, mName) == 0x1c && offsetof(cGeometryGroup, mVertexIndices) == 0x34 &&
                  offsetof(cGeometryGroup, mBones) == 0x40,
              "group");
static_assert(sizeof(cGeometryGroupInfo) == 0x1c && offsetof(cGeometryGroupInfo, mName) == 0x14, "group info");
static_assert(sizeof(cGeometryTransform) == 0x1c, "transform size");
static_assert(sizeof(cGeometryNamePair) == 0x30, "name pair size");
static_assert(sizeof(cGeometryMeshRef) == 4, "mesh ref size");
static_assert(sizeof(cGeometryDataContainer) == cGeometryDataContainer::kSize, "cGeometryDataContainer size (0x84)");
static_assert(offsetof(cGeometryDataContainer, mElements) == 0x30, "elements");
static_assert(offsetof(cGeometryDataContainer, mLinkages) == 0x3c, "linkages");
static_assert(offsetof(cGeometryDataContainer, mGroups) == 0x48, "groups");
static_assert(offsetof(cGeometryDataContainer, mTransforms) == 0x54, "transforms");
static_assert(offsetof(cGeometryDataContainer, mNamePairs) == 0x60, "name pairs");
static_assert(offsetof(cGeometryDataContainer, mSubsetMeshes) == 0x6c, "subset meshes");
static_assert(offsetof(cGeometryDataContainer, mBoundingMesh) == 0x78, "bounding mesh");

namespace {

using nGZCompGeom::cTriangleSetData;
using nGZCompGeom::cTriangleSetVertex;

// The geometry library's triangle set (src/compgeom/TriangleSet.h, lane PC) behind the container's slice of it.
nGZCompGeom::TriangleSet *SetOf(cTriangleSetData *data)
{
    return reinterpret_cast<nGZCompGeom::TriangleSet *>(data);
}

const char kVertLabel[] = "vert";
const char kVertIndicesLabel[] = "vertIndices";

// A counted triangle set, read: the vertex count is already read (non-zero). The old set is released before the COM
// makes the new one into `slot`.
void ReadMeshInto(cITriangleSetShape **slot, uint32_t vertexCount, uint32_t version, ifaces::cIGZPersistReader *reader)
{
    cIGZCOM *const com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    if (cITriangleSetShape *const old = *slot) {
        *slot = nullptr;
        old->Release();
    }
    com->GetClassObject(cITriangleSetShape::kClassId, cITriangleSetShape::kClassId, reinterpret_cast<void **>(slot));
    cTriangleSetData *const data = (*slot)->GetTriangleSet();
    const uint32_t triangles = reader->GetUint32() / 3;
    SetOf(data)->Resize(vertexCount, triangles);
    ReadGeometryVertices(kVertLabel, data, static_cast<int32_t>(vertexCount), reader);
    if (version < 4)
        ReadGeometryTriangles32(kVertIndicesLabel, data, static_cast<int32_t>(triangles), reader);
    else
        ReadGeometryTriangles16(kVertIndicesLabel, data, static_cast<int32_t>(triangles), reader);
    SetOf(data)->Build();
}

// A triangle set, written: 0 for none, else the vertex and corner counts, the vertices, the corners one by one.
void WriteMesh(cITriangleSetShape *mesh, ifaces::cIGZPersistWriter *writer)
{
    if (mesh == nullptr) {
        writer->SetUint32(0);
        return;
    }
    cTriangleSetData *data = mesh->GetTriangleSet();
    const uint32_t vertices = static_cast<uint32_t>(data->mVertexEnd - data->mVertexBegin);
    writer->SetUint32(vertices);
    data = mesh->GetTriangleSet();
    const uint32_t corners = static_cast<uint32_t>(data->mIndexEnd - data->mIndexBegin);
    writer->SetUint32(corners);
    // A set with corners and no vertices still writes both counts (a reader takes the corner count for the next
    // field): the game's format.
    for (uint32_t i = 0; i < vertices; ++i) {
        // ORIGINAL-BUG: the vertex index is kept to 16 bits, so a set past 65535 vertices writes the first ones again.
        cTriangleSetVertex vertex = mesh->GetTriangleSet()->mVertexBegin[i & 0xffff];
        writer->SetFloat32Array(&vertex.x, 3);
    }
    for (uint32_t i = 0; i < corners; ++i) writer->SetUint16(mesh->GetTriangleSet()->mIndexBegin[i]);
}

// The range slots' MemPtr pair over [begin, end) of `stride` bytes; an empty range gives two null-pointer iterators.
template <typename T>
cGeometryRange<T> *MemRange(cGeometryRange<T> *out, const uint8_t *begin, const uint8_t *end, uint32_t stride)
{
    if (begin == end) begin = end = nullptr;
    cIGeometryIterator<T> *const last = new cGeometryMemPtrIterator<T>(end, stride);
    cIGeometryIterator<T> *const first = new cGeometryMemPtrIterator<T>(begin, stride);
    return FillGeometryRange(out, last, first);
}

// The uint16 lists' range: the end is begin + size (from a null begin when empty).
cGeometryRange<uint16_t> *WordRange(cGeometryRange<uint16_t> *out, const std::vector<uint16_t> &list)
{
    const uint8_t *const begin = list.empty() ? nullptr : reinterpret_cast<const uint8_t *>(list.data());
    cIGeometryIterator<uint16_t> *const last = new cGeometryMemPtrIterator<uint16_t>(begin + list.size() * 2, 2);
    cIGeometryIterator<uint16_t> *const first = new cGeometryMemPtrIterator<uint16_t>(begin, 2);
    return FillGeometryRange(out, last, first);
}

template <typename T> cGeometryRange<T> *ElementRange(cGeometryRange<T> *out, const cGeometryElement &element, uint32_t stride)
{
    const uint8_t *const begin = element.mData.data();
    return MemRange<T>(out, begin, begin + element.mData.size(), stride);
}

} // namespace

// ---- reader helpers ----

void __cdecl ReadGeometryIndexList(const char *, uint16_t *dst, uint32_t count, int32_t version,
                                   ifaces::cIGZPersistReader *reader)
{
    if (version < 3) {
        std::vector<int32_t> wide(count);
        reader->GetSint32Array(wide.data(), count);
        for (uint32_t i = 0; i < count; ++i) dst[i] = static_cast<uint16_t>(wide[i]);
    } else {
        reader->GetUint16Array(dst, count);
    }
}

void __cdecl ReadGeometryVertices(const char *, nGZCompGeom::cTriangleSetData *data, int32_t count,
                                  ifaces::cIGZPersistReader *reader)
{
    const uint32_t floats = static_cast<uint32_t>(count) * 3;
    std::vector<float> buffer(floats);
    reader->GetFloat32Array(buffer.data(), floats);
    for (int32_t i = 0; i < count; ++i) {
        const nGZCompGeom::Vec3 vertex{buffer[i * 3], buffer[i * 3 + 1], buffer[i * 3 + 2]};
        SetOf(data)->SetVertex(static_cast<uint16_t>(i), &vertex); // the index kept to 16 bits, as the original's
    }
}

void __cdecl ReadGeometryTriangles16(const char *, nGZCompGeom::cTriangleSetData *data, int32_t count,
                                     ifaces::cIGZPersistReader *reader)
{
    const uint32_t corners = static_cast<uint32_t>(count) * 3;
    std::vector<uint16_t> buffer(corners);
    reader->GetUint16Array(buffer.data(), corners);
    for (int32_t i = 0; i < count; ++i)
        SetOf(data)->SetTriangle(i, buffer[i * 3], buffer[i * 3 + 1], buffer[i * 3 + 2]);
}

void __cdecl ReadGeometryTriangles32(const char *, nGZCompGeom::cTriangleSetData *data, int32_t count,
                                     ifaces::cIGZPersistReader *reader)
{
    const uint32_t corners = static_cast<uint32_t>(count) * 3;
    std::vector<uint32_t> buffer(corners);
    reader->GetUint32Array(buffer.data(), corners);
    for (int32_t i = 0; i < count; ++i)
        SetOf(data)->SetTriangle(i, static_cast<uint16_t>(buffer[i * 3]), static_cast<uint16_t>(buffer[i * 3 + 1]),
                                 static_cast<uint16_t>(buffer[i * 3 + 2]));
}

// ---- lifetime ----

cGeometryDataContainer::~cGeometryDataContainer()
{
    if (cITriangleSetShape *const mesh = mBoundingMesh) mesh->Release();
    // The members then go in reverse order: the subset meshes, the name pairs, ... the elements (the game's order).
}

// (The original's operator new throws on failure, so its null branch is dead.)
cISGResource *__cdecl cGeometryDataContainer::Create() { return static_cast<cISGResource *>(new cGeometryDataContainer); }

uint32_t __cdecl cGeometryDataContainer::GetClassId() { return kClassId; }

cIGeometryDataContainerSub *cGeometryDataContainer::Sub()
{
    return reinterpret_cast<cIGeometryDataContainerSub *>(static_cast<cIGeometryDataContainer *>(this));
}

// ---- cIGZUnknown ----

bool cGeometryDataContainer::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == cIGeometryDataContainer::kIid) {
        *out = static_cast<cIGeometryDataContainer *>(this);
        AddRef();
        return true;
    }
    return cSGResource::QueryInterface(iid, out);
}

uint32_t cGeometryDataContainer::AddRef() { return cSGResource::AddRef(); }
uint32_t cGeometryDataContainer::Release() { return cSGResource::Release(); }

// ---- persistence ----

DecodeObserver gDecodeObserver = nullptr;

void cGeometryDataContainer::Read(ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    const uint32_t version = info.mVersion;
    cSGResource::Read(reader);

    mElements.resize(reader->GetUint32());
    for (uint32_t i = 0; i < mElements.size(); ++i) {
        cGeometryElement &element = mElements[i];
        element.mIndex = i;
        element.mNumber = reader->GetUint32();
        element.mIdentity = reader->GetUint32();
        element.mRepetition = reader->GetUint32();
        element.mBlockFormat = reader->GetUint32();
        element.mSetFormat = reader->GetUint32();
        element.mData.resize(reader->GetUint32());
        reader->GetVoid(element.mData.data(), static_cast<uint32_t>(element.mData.size()));
        element.mIndices.resize(reader->GetUint32());
        ReadGeometryIndexList("elemIndices", element.mIndices.data(), static_cast<uint32_t>(element.mIndices.size()),
                              static_cast<int32_t>(version), reader);
    }

    static const char *const kAliasLabels[3] = {"positionClassIndices", "surfaceClassIndices", "materialClassIndices"};
    mLinkages.resize(reader->GetUint32());
    for (uint32_t i = 0; i < mLinkages.size(); ++i) {
        cGeometryLinkage &linkage = mLinkages[i];
        linkage.mIndex = i;
        linkage.mElementIndices.resize(reader->GetUint32());
        ReadGeometryIndexList("eltArrayIndices", linkage.mElementIndices.data(),
                              static_cast<uint32_t>(linkage.mElementIndices.size()), static_cast<int32_t>(version), reader);
        linkage.mReferencedSize = reader->GetUint32();
        linkage.mActiveCount = reader->GetUint32();
        for (int which = 0; which < 3; ++which) {
            std::vector<uint16_t> &aliases = linkage.mAliases[which];
            aliases.resize(reader->GetUint32());
            ReadGeometryIndexList(kAliasLabels[which], aliases.data(), static_cast<uint32_t>(aliases.size()),
                                  static_cast<int32_t>(version), reader);
        }
    }

    mGroups.resize(reader->GetUint32());
    for (uint32_t i = 0; i < mGroups.size(); ++i) {
        cGeometryGroup &group = mGroups[i];
        group.mIndex = i;
        group.mPrimType = reader->GetUint32();
        group.mLinkIndex = reader->GetUint32();
        group.mWord14 = 0;
        nTSSG::SGLocalString name;
        reader->GetGZStr(&name.value);
        group.mName.assign(name.value.mString.data(), name.value.mString.size());
        group.mIndexCount = reader->GetUint32();
        group.mVertexIndices.resize(group.mIndexCount);
        ReadGeometryIndexList("vertexIndices", group.mVertexIndices.data(),
                              static_cast<uint32_t>(group.mVertexIndices.size()), static_cast<int32_t>(version), reader);
        group.mWord10 = reader->GetSint32();
        group.mBoneCount = 0;
        if (version > 1) {
            group.mBoneCount = reader->GetUint32();
            if (group.mBoneCount != 0) {
                // (A group read with no bones keeps the bone list it had: only the count is reset.)
                group.mBones.resize(group.mBoneCount);
                ReadGeometryIndexList("remappedBoneIndices", group.mBones.data(),
                                      static_cast<uint32_t>(group.mBones.size()), static_cast<int32_t>(version), reader);
            }
        }
    }

    {
        // One default for the new transforms, whose rotation part is also the buffer the rotations are read into (so a
        // reader that writes nothing leaves the previous transform's rotation). The original's translation buffer is
        // uninitialised; ours starts at 0.
        cGeometryTransform buffer = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
        mTransforms.resize(reader->GetUint32(), buffer);
        float translation[3] = {0.0f, 0.0f, 0.0f};
        for (uint32_t i = 0; i < mTransforms.size(); ++i) {
            reader->GetFloat32Array(buffer.mRotation, 4);
            reader->GetFloat32Array(translation, 3);
            std::memcpy(mTransforms[i].mRotation, buffer.mRotation, sizeof(buffer.mRotation));
            std::memcpy(mTransforms[i].mTranslation, translation, sizeof(translation));
        }
    }

    mNamePairs.resize(reader->GetUint32());
    for (uint32_t i = 0; i < mNamePairs.size(); ++i) {
        // One string for both reads: a failed second read gives the second name the first's text.
        nTSSG::SGLocalString text;
        reader->GetGZStr(&text.value);
        mNamePairs[i].first.assign(text.value.mString.data(), text.value.mString.size());
        reader->GetGZStr(&text.value);
        mNamePairs[i].second.assign(text.value.mString.data(), text.value.mString.size());
    }

    if (const uint32_t vertices = reader->GetUint32(); vertices != 0) ReadMeshInto(&mBoundingMesh, vertices, version, reader);

    mSubsetMeshes.resize(reader->GetUint32());
    for (uint32_t i = 0; i < mSubsetMeshes.size(); ++i) {
        // A subset with no vertices keeps the set it had.
        if (const uint32_t vertices = reader->GetUint32(); vertices != 0)
            ReadMeshInto(&mSubsetMeshes[i].mMesh, vertices, version, reader);
    }

    if (gDecodeObserver != nullptr) gDecodeObserver(*this, mKey);

    if (info.mName) info.mName->Release();
}

cPersistTypeInfo cGeometryDataContainer::GetTypeInfo()
{
    cPersistTypeInfo info;
    info.mName = NewTypeName("cGeometryDataContainer");
    info.mWord4 = kClassId;
    info.mVersion = kVersion;
    return info;
}

void cGeometryDataContainer::Write(ifaces::cIGZPersistWriter *writer)
{
    Slot55();
    {
        cPersistTypeInfo info = cGeometryDataContainer::GetTypeInfo();
        writer->WriteTypeInfo(&info);
        if (info.mName) info.mName->Release();
    }
    cSGResource::WriteResource(writer);

    writer->SetUint32(static_cast<uint32_t>(mElements.size()));
    for (cGeometryElement &element : mElements) {
        writer->SetUint32(element.mNumber);
        writer->SetUint32(element.mIdentity);
        writer->SetUint32(element.mRepetition);
        writer->SetUint32(element.mBlockFormat);
        writer->SetUint32(element.mSetFormat);
        writer->SetUint32(static_cast<uint32_t>(element.mData.size()));
        writer->SetVoid(element.mData.data(), static_cast<uint32_t>(element.mData.size()));
        writer->SetUint32(static_cast<uint32_t>(element.mIndices.size()));
        writer->SetUint16Array(element.mIndices.data(), static_cast<uint32_t>(element.mIndices.size()));
    }

    writer->SetUint32(static_cast<uint32_t>(mLinkages.size()));
    for (cGeometryLinkage &linkage : mLinkages) {
        writer->SetUint32(static_cast<uint32_t>(linkage.mElementIndices.size()));
        writer->SetUint16Array(linkage.mElementIndices.data(), static_cast<uint32_t>(linkage.mElementIndices.size()));
        writer->SetUint32(linkage.mReferencedSize);
        writer->SetUint32(linkage.mActiveCount);
        for (std::vector<uint16_t> &aliases : linkage.mAliases) {
            writer->SetUint32(static_cast<uint32_t>(aliases.size()));
            writer->SetUint16Array(aliases.data(), static_cast<uint32_t>(aliases.size()));
        }
    }

    writer->SetUint32(static_cast<uint32_t>(mGroups.size()));
    for (cGeometryGroup &group : mGroups) {
        writer->SetUint32(group.mPrimType);
        writer->SetUint32(group.mLinkIndex);
        writer->WriteRZCharStr(const_cast<char *>(group.mName.c_str()));
        writer->SetUint32(static_cast<uint32_t>(group.mVertexIndices.size()));
        writer->SetUint16Array(group.mVertexIndices.data(), static_cast<uint32_t>(group.mVertexIndices.size()));
        writer->SetSint32(group.mWord10);
        writer->SetUint32(group.mBoneCount);
        // The bone list goes without its own count: a reader takes the bone count for it.
        if (group.mBoneCount != 0)
            writer->SetUint16Array(group.mBones.data(), static_cast<uint32_t>(group.mBones.size()));
    }

    writer->SetUint32(static_cast<uint32_t>(mTransforms.size()));
    for (cGeometryTransform &transform : mTransforms) {
        float rotation[4];
        std::memcpy(rotation, transform.mRotation, sizeof(rotation));
        writer->SetFloat32Array(rotation, 4);
        writer->SetFloat32Array(transform.mTranslation, 3);
    }

    writer->SetUint32(static_cast<uint32_t>(mNamePairs.size()));
    for (cGeometryNamePair &pair : mNamePairs) {
        writer->WriteRZCharStr(const_cast<char *>(pair.first.c_str()));
        writer->WriteRZCharStr(const_cast<char *>(pair.second.c_str()));
    }

    WriteMesh(mBoundingMesh, writer);
    writer->SetUint32(static_cast<uint32_t>(mSubsetMeshes.size()));
    for (uint32_t i = 0; i < mSubsetMeshes.size(); ++i) WriteMesh(mSubsetMeshes[i].mMesh, writer);
}

uint32_t cGeometryDataContainer::GetOwnerInfo() { return cSGResource::GetOwnerInfo(); }

const void *cGeometryDataContainer::GetRecord18() { return "_gmdc"; }
const void *cGeometryDataContainer::GetRecord19() { return "5gd"; }

// ---- cIGeometryDataContainer ----

cISGResource *cGeometryDataContainer::GetResourceConst() { return static_cast<cISGResource *>(this); }
cISGResource *cGeometryDataContainer::GetResource() { return static_cast<cISGResource *>(this); }

uint32_t cGeometryDataContainer::AddElement(uint32_t identity, uint32_t repetition, uint32_t blockFormat,
                                            uint32_t setFormat)
{
    cGeometryElement element{};
    element.mIndex = static_cast<uint32_t>(mElements.size());
    element.mNumber = 0;
    element.mIdentity = identity;
    element.mRepetition = repetition;
    element.mBlockFormat = blockFormat;
    element.mSetFormat = setFormat;
    mElements.push_back(std::move(element));
    return static_cast<uint32_t>(mElements.size()) - 1;
}

void cGeometryDataContainer::RemoveElements(uint32_t identity, uint32_t blockFormat, uint32_t setFormat)
{
    const auto matches = [&](const cGeometryElement &element) {
        return element.mIdentity == identity && element.mBlockFormat == blockFormat && element.mSetFormat == setFormat;
    };

    // The new position of every element that stays.
    std::unordered_map<uint32_t, uint32_t> kept;
    const uint32_t count = CountElements();
    uint32_t next = 0;
    for (uint32_t i = 0; i < count; ++i)
        if (!matches(mElements[i])) kept[i] = next++;
    if (kept.size() == count) return;

    mElements.erase(std::remove_if(mElements.begin(), mElements.end(), matches), mElements.end());
    uint32_t position = 0;
    for (cGeometryElement &element : mElements) element.mIndex = position++;

    const uint32_t linkages = CountLinkages();
    for (uint32_t i = 0; i < linkages; ++i) {
        std::vector<uint16_t> &list = mLinkages[i].mElementIndices;
        list.erase(std::remove_if(list.begin(), list.end(), [&](uint16_t index) { return kept.find(index) == kept.end(); }),
                   list.end());
        for (uint16_t &index : list) index = static_cast<uint16_t>(kept[index]);
        mLinkages[i].mActiveCount = static_cast<uint32_t>(list.size());
    }
}

void cGeometryDataContainer::SetElementSetFormat(uint32_t index, uint32_t value) { Sub()->SetRecordADword(index, value); }

void cGeometryDataContainer::SetElementData(uint32_t index, uint32_t number, const void *data, uint32_t size,
                                            const uint16_t *indices, uint32_t indexCount)
{
    cGeometryElement &element = mElements[index];
    element.mNumber = number;
    element.mData.clear();
    element.mIndices.clear();
    if (size != 0) {
        element.mData.resize(size);
        std::memcpy(element.mData.data(), data, size);
    }
    if (indices != nullptr) {
        element.mIndices.resize(indexCount);
        std::memcpy(element.mIndices.data(), indices, indexCount * 2);
    }
}

void cGeometryDataContainer::AddLinkage()
{
    cGeometryLinkage linkage{};
    linkage.mIndex = static_cast<uint32_t>(mLinkages.size());
    mLinkages.push_back(std::move(linkage));
}

uint32_t cGeometryDataContainer::AddLinkageElement(uint32_t index, uint16_t element)
{
    cGeometryLinkage &linkage = mLinkages[index];
    linkage.mElementIndices.push_back(element);
    linkage.mActiveCount = static_cast<uint32_t>(linkage.mElementIndices.size());
    return linkage.mActiveCount;
}

void cGeometryDataContainer::SetLinkageElement(uint32_t index, uint32_t position, uint16_t element)
{
    Sub()->SetRecordBWord(index, position, element);
}

void cGeometryDataContainer::SetLinkageReferencedSize(uint32_t index, uint32_t value)
{
    Sub()->SetRecordBAndTrim(index, value);
}

void cGeometryDataContainer::SetLinkageAliases(uint32_t index, uint32_t which, const uint16_t *data, uint32_t count)
{
    if (which > 2) return;
    std::vector<uint16_t> &aliases = mLinkages[index].mAliases[which];
    aliases.resize(count);
    if (count != 0) std::memmove(aliases.data(), data, count * 2);
}

void cGeometryDataContainer::ClearLinkageAliases(uint32_t index, uint32_t which) { Sub()->TrimWordVector(index, which); }

void cGeometryDataContainer::ReserveGroups(uint32_t count) { mGroups.reserve(count); }

void cGeometryDataContainer::AddGroup(uint32_t primType, const char *name, uint32_t linkIndex, int32_t word10)
{
    cGeometryGroup group{};
    group.mIndex = static_cast<uint32_t>(mGroups.size());
    group.mPrimType = primType;
    group.mIndexCount = 0;
    group.mLinkIndex = linkIndex;
    group.mWord10 = word10;
    group.mWord14 = 0;
    group.mBoneCount = 0;
    group.mName.assign(name);
    mGroups.push_back(group);
}

void cGeometryDataContainer::SetGroupVertexIndices(uint32_t index, const uint16_t *data, uint32_t count)
{
    cGeometryGroup &group = mGroups[index];
    group.mIndexCount = count;
    group.mVertexIndices.resize(count);
    std::memcpy(group.mVertexIndices.data(), data, count * 2);
}

void cGeometryDataContainer::SetGroupVertexIndicesAndWord(uint32_t index, const uint16_t *data, uint32_t count,
                                                          int32_t word10)
{
    Sub()->SetRecordCDword(index, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data)), count,
                           static_cast<uint32_t>(word10));
}

void cGeometryDataContainer::SetGroupVertexIndicesAndBones(uint32_t index, const uint16_t *data, uint32_t count,
                                                           const uint16_t *bones, uint32_t boneCount)
{
    SetGroupVertexIndices(index, data, count);
    cGeometryGroup &group = mGroups[index];
    group.mBoneCount = boneCount;
    group.mBones.resize(boneCount);
    std::memcpy(group.mBones.data(), bones, boneCount * 2);
}

void cGeometryDataContainer::AddTransform(const cGeometryTransform *transform) { mTransforms.push_back(*transform); }

void cGeometryDataContainer::AddNamePair(const char *first, const char *second)
{
    mNamePairs.emplace_back();
    mNamePairs.back().first.assign(first);
    mNamePairs.back().second.assign(second);
}

void cGeometryDataContainer::SetBoundingMesh(cITriangleSetShape *mesh)
{
    cITriangleSetShape *const old = mBoundingMesh;
    if (mesh == old) return;
    if (mesh) mesh->AddRef();
    mBoundingMesh = mesh;
    if (old) old->Release();
}

void cGeometryDataContainer::SetSubsetMesh(uint32_t index, cITriangleSetShape *mesh)
{
    if (index >= mSubsetMeshes.size()) mSubsetMeshes.resize(index + 1);
    cITriangleSetShape *const old = mSubsetMeshes[index].mMesh;
    if (mesh == old) return;
    if (mesh) mesh->AddRef();
    mSubsetMeshes[index].mMesh = mesh;
    if (old) old->Release();
}

void cGeometryDataContainer::ClearLinkages() { mLinkages.clear(); }
void cGeometryDataContainer::ClearElements() { mElements.clear(); }
void cGeometryDataContainer::ClearGroups() { mGroups.clear(); }

uint32_t cGeometryDataContainer::CountElements() { return static_cast<uint32_t>(Sub()->CountRecordsA()); }

cGeometryElementHeader *cGeometryDataContainer::GetElementHeader(cGeometryElementHeader *out, uint32_t index)
{
    return static_cast<cGeometryElementHeader *>(Sub()->CopyRecordA24(out, index));
}

cGeometryRange<float> *cGeometryDataContainer::GetElementFloats(cGeometryRange<float> *out, uint32_t index)
{
    return ElementRange<float>(out, mElements[index], 4);
}

cGeometryRange<cGeometryVector<2>> *cGeometryDataContainer::GetElementVectors2(cGeometryRange<cGeometryVector<2>> *out,
                                                                               uint32_t index)
{
    return ElementRange<cGeometryVector<2>>(out, mElements[index], 8);
}

cGeometryRange<cGeometryVector<3>> *cGeometryDataContainer::GetElementVectors3(cGeometryRange<cGeometryVector<3>> *out,
                                                                               uint32_t index)
{
    return ElementRange<cGeometryVector<3>>(out, mElements[index], 0xc);
}

cGeometryRange<cGeometryVector<4>> *cGeometryDataContainer::GetElementVectors4(cGeometryRange<cGeometryVector<4>> *out,
                                                                               uint32_t index)
{
    return ElementRange<cGeometryVector<4>>(out, mElements[index], 0x10);
}

cGeometryRange<uint32_t> *cGeometryDataContainer::GetElementWords(cGeometryRange<uint32_t> *out, uint32_t index)
{
    return ElementRange<uint32_t>(out, mElements[index], 4);
}

cGeometryRange<uint16_t> *cGeometryDataContainer::GetElementIndices(cGeometryRange<uint16_t> *out, uint32_t index)
{
    if (index < mElements.size()) return WordRange(out, mElements[index].mIndices);
    cIGeometryIterator<uint16_t> *const last = new cGeometryNullIterator;
    cIGeometryIterator<uint16_t> *const first = new cGeometryNullIterator;
    return FillGeometryRange(out, last, first);
}

uint32_t cGeometryDataContainer::CountLinkages() { return static_cast<uint32_t>(Sub()->CountRecordsB()); }

cGeometryLinkageHeader *cGeometryDataContainer::GetLinkageHeader(cGeometryLinkageHeader *out, uint32_t index)
{
    return static_cast<cGeometryLinkageHeader *>(Sub()->CopyHeaderB(out, index));
}

cGeometryRange<uint16_t> *cGeometryDataContainer::GetLinkageElements(cGeometryRange<uint16_t> *out, uint32_t index)
{
    return WordRange(out, mLinkages[index].mElementIndices);
}

cGeometryRange<uint16_t> *cGeometryDataContainer::GetLinkageAliases(cGeometryRange<uint16_t> *out, uint32_t index,
                                                                    uint32_t which)
{
    cGeometryLinkage &linkage = mLinkages[index];
    if (which <= 2 && !linkage.mAliases[which].empty()) return WordRange(out, linkage.mAliases[which]);
    // No list: the positions 0 .. the referenced size, counted.
    cIGeometryIterator<uint16_t> *const last = new cGeometryUnitIterator(static_cast<uint16_t>(linkage.mReferencedSize));
    cIGeometryIterator<uint16_t> *const first = new cGeometryUnitIterator(0);
    return FillGeometryRange(out, last, first);
}

uint8_t cGeometryDataContainer::LinkageHas(uint32_t index, uint32_t which) { return Sub()->RecordHasWords(index, which); }

uint32_t cGeometryDataContainer::CountGroups() { return static_cast<uint32_t>(Sub()->CountRecordsC()); }

cGeometryGroupInfo *cGeometryDataContainer::GetGroupInfo(cGeometryGroupInfo *out, uint32_t index)
{
    return static_cast<cGeometryGroupInfo *>(Sub()->CopyRecordC(out, index));
}

cGeometryRange<uint16_t> *cGeometryDataContainer::GetGroupVertexIndices(cGeometryRange<uint16_t> *out, uint32_t index)
{
    return WordRange(out, mGroups[index].mVertexIndices);
}

cGeometryRange<uint16_t> *cGeometryDataContainer::GetGroupBones(cGeometryRange<uint16_t> *out, uint32_t index)
{
    return WordRange(out, mGroups[index].mBones);
}

uint32_t cGeometryDataContainer::CountTransforms() { return static_cast<uint32_t>(Sub()->CountElements28()); }

cGeometryTransform *cGeometryDataContainer::GetTransform(cGeometryTransform *out, uint32_t index)
{
    return static_cast<cGeometryTransform *>(Sub()->CopyElement28(out, index));
}

uint32_t cGeometryDataContainer::CountNamePairs() { return static_cast<uint32_t>(Sub()->CountRecordsD()); }
const char *cGeometryDataContainer::GetNameFirst(uint32_t index) { return Sub()->RecordStringAt00(index); }
const char *cGeometryDataContainer::GetNameSecond(uint32_t index) { return Sub()->RecordStringAt18(index); }

cITriangleSetShape *cGeometryDataContainer::GetBoundingMesh() { return mBoundingMesh; }

cITriangleSetShape *cGeometryDataContainer::GetSubsetMesh(uint32_t index)
{
    return index < mSubsetMeshes.size() ? mSubsetMeshes[index].mMesh : nullptr;
}

void cGeometryDataContainer::Slot55() {}

bool cGeometryDataContainer::Clone(uint32_t iid, void **out)
{
    cGeometryDataContainer *const copy = new cGeometryDataContainer;
    copy->mElements = mElements;
    copy->mLinkages = mLinkages;
    copy->mGroups = mGroups;
    copy->mTransforms = mTransforms;
    copy->mNamePairs = mNamePairs;
    copy->mSubsetMeshes = mSubsetMeshes;
    if (mBoundingMesh) mBoundingMesh->CloneAs(cITriangleSetShape::kClassId, reinterpret_cast<void **>(&copy->mBoundingMesh));
    // ORIGINAL-BUG: the copy is never counted itself, so when the query fails it is never freed.
    return copy->QueryInterface(iid, out);
}

void cGeometryDataContainer::CopyLinkageTo(uint32_t index, cIGeometryDataContainer *other, uint32_t otherIndex)
{
    const cGeometryLinkage &source = mLinkages[index];
    cGeometryLinkage &target = static_cast<cGeometryDataContainer *>(other)->mLinkages[otherIndex];
    target.mIndex = source.mIndex;
    target.mActiveCount = source.mActiveCount;
    target.mReferencedSize = source.mReferencedSize;
    target.mElementIndices = source.mElementIndices;
    for (int which = 0; which < 3; ++which) target.mAliases[which] = source.mAliases[which];
}

void cGeometryDataContainer::CopyGroupTo(uint32_t index, cIGeometryDataContainer *other, uint32_t linkIndex)
{
    cGeometryGroup group = mGroups[index];
    group.mLinkIndex = linkIndex;
    static_cast<cGeometryDataContainer *>(other)->mGroups.push_back(group);
}

} // namespace nGZSceneGraph
