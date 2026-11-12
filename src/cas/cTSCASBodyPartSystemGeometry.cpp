// nTSCASCore::cTSCASBodyPartSystem (lane KA): slot 31, which writes a part entry's vertices back into the part's
// geometry mesh. The face's `this` is the subobject at +0x1c; see cTSCASBodyPartSystem.h.
#include "cas/cTSCASBodyPartSystem.h"

#include "game/original.h"
#include "game/vtable.h"
#include "sg/SGUtilities.h"

#include <cstring>
#include <vector>

namespace nTSCASCore {

namespace {

cTSCASBodyPartSystem *SystemOf(BodyPartSystemIface *iface)
{
    return reinterpret_cast<cTSCASBodyPartSystem *>(reinterpret_cast<uint8_t *>(iface) - 0x1c);
}

// The vertex elements looked up in a vertex block (0xaeda40's type argument), and its other two arguments.
constexpr uint32_t kElementPosition = 0x5b830781;
constexpr uint32_t kElementNormal = 0x3b83078b;
constexpr uint32_t kElementIndex = 0;
constexpr uint32_t kElementSize = 2;

void AddRef(void *o) { VirtualCall<uint32_t(__thiscall *)(void *), 1>(o); }
void Release(void *o) { VirtualCall<uint32_t(__thiscall *)(void *), 2>(o); }

// One counted reference, released at the end of its scope.
struct HeldRef {
    void *mObject = nullptr;
    HeldRef() = default;
    HeldRef(const HeldRef &) = delete;
    ~HeldRef()
    {
        if (mObject != nullptr) Release(mObject);
    }
};

// What the mesh's iterator / map slots fill in: three counted references (the iterator itself, a second one, the end
// it is compared with), released last to first.
struct RefTriple {
    void *mRef[3] = {nullptr, nullptr, nullptr};
    RefTriple() = default;
    RefTriple(const RefTriple &) = delete;
    ~RefTriple()
    {
        for (int k = 2; k >= 0; --k)
            if (mRef[k] != nullptr) Release(mRef[k]);
    }
    void Replace(int k, void *value)
    {
        void *const old = mRef[k];
        if (value == old) return;
        if (value != nullptr) AddRef(value);
        mRef[k] = value;
        if (old != nullptr) Release(old);
    }
    // The game's assignment (0xcdffa0): the iterator taken through the source's slot 3, the other two copied.
    void Assign(const RefTriple &source)
    {
        Replace(0, VirtualCall<void *(__thiscall *)(void *), 3>(source.mRef[0]));
        Replace(1, source.mRef[1]);
        Replace(2, source.mRef[2]);
    }
    bool AtEnd() const { return VirtualCall<bool(__thiscall *)(void *, void *), 4>(mRef[0], mRef[2]); }
    void Next() { VirtualCall<void(__thiscall *)(void *), 6>(mRef[0]); }
    uint16_t Index() const { return VirtualCall<uint16_t(__thiscall *)(void *), 5>(mRef[0]); }
    // The point at the iterator: 12 bytes read (written into room for 16, as the original's buffer is followed by a
    // dead local).
    PartPoint Vector() const
    {
        float room[4];
        VirtualCall<void(__thiscall *)(void *, float *), 5>(mRef[0], room);
        PartPoint p;
        std::memcpy(p.mXyz, room, sizeof(p.mXyz));
        return p;
    }
    uint16_t Map(uint16_t index) const { return VirtualCall<uint16_t(__thiscall *)(void *, uint32_t), 9>(mRef[0], index); }
};

// The element 0xaeda40 finds: 24 bytes, the element's index first (-1 when the block has none) and its stream sixth.
struct VertexElement {
    int32_t mIndex;
    uint32_t mField4, mType, mField0C, mSize;
    uint32_t mStream;
};
static_assert(sizeof(VertexElement) == 0x18, "VertexElement layout");

// What the mesh's slot 45 fills in for a group. The original reserves 0x1c bytes with locals written only later after
// them; the callee's size is not known here, so this has room to spare (an out smaller than the callee writes
// overwrote a saved `this` in slot 26).
struct GroupInfo {
    uint32_t mField0, mField4;
    uint32_t mHasVertices; // +0x08
    uint32_t mBlock;       // +0x0c  the vertex block the elements are looked up in
    uint32_t mRest[12];
};

using IndexList = std::vector<uint16_t, CasAlloc<uint16_t>>;
using PointList = std::vector<PartPoint, CasAlloc<PartPoint>>;

VertexElement FindElement(void *mesh, uint32_t block, uint32_t type)
{
    VertexElement found;
    const auto *const r = reinterpret_cast<const VertexElement *>(nGZSceneGraph::FindLinkedElement(
        reinterpret_cast<nGZSceneGraph::cGeometryElementHeader *>(&found),
        static_cast<nGZSceneGraph::cIGeometryDataContainer *>(mesh), block, type, kElementIndex, kElementSize));
    return *r;
}

uint32_t GroupCount(void *mesh) { return VirtualCall<uint32_t(__thiscall *)(void *), 44>(mesh); }

// The element's indices (mesh slot 38) or points (slot 35): an iterator filled in, then, for the second element, the
// same iterator assigned the new one.
using IterateFn = RefTriple *(__thiscall *)(void *, RefTriple *, int32_t);

void ReadIndices(RefTriple &it, IndexList &out)
{
    for (; !it.AtEnd(); it.Next()) out.push_back(it.Index());
}
void ReadPoints(RefTriple &it, PointList &out)
{
    for (; !it.AtEnd(); it.Next()) out.push_back(it.Vector());
}

// Mesh slot 12: the element's points (count, data, bytes) and its indices (none when empty).
void Upload(void *mesh, int32_t element, const PointList &points, const IndexList &indices)
{
    const uint32_t count = static_cast<uint32_t>(points.size());
    const uint32_t indexCount = static_cast<uint32_t>(indices.size());
    VirtualCall<void(__thiscall *)(void *, int32_t, uint32_t, const PartPoint *, uint32_t, const uint16_t *, uint32_t), 12>(
        mesh, element, count, points.data(), count * sizeof(PartPoint), indexCount != 0 ? indices.data() : nullptr, indexCount);
}

} // namespace

void BodyPartSystemIface::RefreshGeometry(uint32_t kind, uint32_t key, uint32_t id, uint32_t partId)
{
    void *const part = VirtualCall<void *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t), 52>(this, kind, key, id);
    if (part == nullptr) return;
    const PartMeshEntry *const entry = OriginalFunction<const PartMeshEntry *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t), 0xcf04b0>()(
        SystemOf(this), kind, key, id, partId);
    if (entry->mGroupsFirst == entry->mGroupsLast) return;
    void *const geometry = VirtualCall<void *(__thiscall *)(void *, uint32_t), 48>(part, partId);
    if (geometry == nullptr) return;
    HeldRef mesh;
    VirtualCall<void(__thiscall *)(void *, void **), 39>(geometry, &mesh.mObject);
    if (mesh.mObject == nullptr) return;
    if (static_cast<uint32_t>(entry->mGroupsLast - entry->mGroupsFirst) != GroupCount(mesh.mObject)) return;
    for (uint32_t i = 0; i < GroupCount(mesh.mObject); ++i) {
        GroupInfo info;
        VirtualCall<void(__thiscall *)(void *, GroupInfo *, uint32_t), 45>(mesh.mObject, &info, i);
        if (info.mHasVertices == 0) continue;
        const VertexElement position = FindElement(mesh.mObject, info.mBlock, kElementPosition);
        const VertexElement normal = FindElement(mesh.mObject, info.mBlock, kElementNormal);
        if (position.mIndex == -1 || normal.mIndex == -1) continue;

        IndexList positionIndices, normalIndices;
        RefTriple indexIt;
        VirtualCall<IterateFn, 38>(mesh.mObject, &indexIt, position.mIndex);
        ReadIndices(indexIt, positionIndices);
        {
            RefTriple next;
            indexIt.Assign(*VirtualCall<IterateFn, 38>(mesh.mObject, &next, normal.mIndex));
        }
        ReadIndices(indexIt, normalIndices);

        PointList positions, normals;
        RefTriple pointIt;
        VirtualCall<IterateFn, 35>(mesh.mObject, &pointIt, position.mIndex);
        ReadPoints(pointIt, positions);
        {
            RefTriple next;
            pointIt.Assign(*VirtualCall<IterateFn, 35>(mesh.mObject, &next, normal.mIndex));
        }
        ReadPoints(pointIt, normals);

        // The group's vertices, mapped through the two elements' index maps (slot 42) to the slots they fill. The
        // mapped slots are not checked against the lists, as in the original.
        RefTriple vertexIt;
        VirtualCall<RefTriple *(__thiscall *)(void *, RefTriple *, uint32_t), 46>(mesh.mObject, &vertexIt, i);
        const PartMeshGroup &group = entry->mGroupsFirst[i];
        using MapFn = RefTriple *(__thiscall *)(void *, RefTriple *, uint32_t, uint32_t);
        RefTriple positionMap, normalMap;
        VirtualCall<MapFn, 42>(mesh.mObject, &positionMap, info.mBlock, position.mStream);
        VirtualCall<MapFn, 42>(mesh.mObject, &normalMap, info.mBlock, normal.mStream);
        for (uint32_t j = 0; !vertexIt.AtEnd(); vertexIt.Next(), ++j) {
            const uint16_t vertex = vertexIt.Index();
            const uint16_t at = positionMap.Map(vertex);
            const uint16_t normalAt = normalMap.Map(vertex);
            if (j < static_cast<uint32_t>(group.mLast - group.mFirst)) {
                const PartMeshVertex &v = group.mFirst[j];
                std::memcpy(positions[at].mXyz, v.mPosition, sizeof(v.mPosition)); // bit copies, as the original's
                std::memcpy(normals[normalAt].mXyz, v.mNormal, sizeof(v.mNormal));
            }
        }
        Upload(mesh.mObject, position.mIndex, positions, positionIndices);
        Upload(mesh.mObject, normal.mIndex, normals, normalIndices);
    }
    VirtualCall<void(__thiscall *)(void *, void *), 38>(geometry, mesh.mObject);
}

} // namespace nTSCASCore
