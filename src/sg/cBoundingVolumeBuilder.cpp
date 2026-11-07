// nGZSceneGraph::cBoundingVolumeBuilder (lane QE). Layout and the hook decision: src/sg/cBoundingVolumeBuilder.h.
#include "sg/cBoundingVolumeBuilder.h"

#include "compgeom/CompGeom.h"
#include "compgeom/TriangleSet.h"
#include "gz/cIGZCOM.h"
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "rz/StaticGetters.h" // GetDirectorFrameworkObject: the COM
#include "sg/SGLocalString.h"
#include "sg/cGeometryDataContainerClass.h"
#include "sg/cObjectGraphNodeImpl.h" // NewTypeName

#include <algorithm>
#include <cstddef>

namespace nGZSceneGraph {

static_assert(sizeof(cBoundingVolumeBuilder) == 0x34, "cBoundingVolumeBuilder size (the factory allocates 0x34)");
static_assert(offsetof(cBoundingVolumeBuilder, mNames) == 0x14, "cBoundingVolumeBuilder names");
static_assert(offsetof(cBoundingVolumeBuilder, mNamedOnly) == 0x1c, "cBoundingVolumeBuilder named-only flag");

namespace {

using nGZCompGeom::cITriangleSetShape;

constexpr uint32_t kWeightIdentity = 0x5c4afc5c;   // the bone weights element
constexpr uint32_t kPositionIdentity = 0x5b830781; // the vertex positions element
constexpr uint32_t kTriangleList = 2;               // the group primitive type Fill takes

// A range the container hands out, its three iterators released as the original's (end, begin, then the cursor).
template <typename T> struct Range {
    cGeometryRange<T> r;
    Range() = default;
    Range(const Range &) = delete;
    Range &operator=(const Range &) = delete;
    ~Range()
    {
        if (r.mEnd) r.mEnd->Release();
        if (r.mBegin) r.mBegin->Release();
        if (r.mCurrent) r.mCurrent->Release();
    }
    T At(uint32_t i) { return r.mCurrent->At(static_cast<int32_t>(i)); }
};

// The cursor put back at begin (a clone of it, counted) unless it is there already.
template <typename T> void Rewind(cGeometryRange<T> &r)
{
    if (r.mCurrent->IsEqual(r.mBegin)) return;
    cIGeometryIterator<T> *const begin = r.mBegin->Clone();
    if (begin) begin->AddRef();
    if (r.mCurrent) r.mCurrent->Release();
    r.mCurrent = begin;
}

// The geometry library's triangle set behind a shape (src/compgeom/TriangleSet.h).
nGZCompGeom::TriangleSet *SetOf(cITriangleSetShape *mesh)
{
    return reinterpret_cast<nGZCompGeom::TriangleSet *>(mesh->GetTriangleSet());
}

nGZCompGeom::Vec3 ToVec3(const cGeometryVector<3> &v)
{
    nGZCompGeom::Vec3 p;
    p.x = v.v[0];
    p.y = v.v[1];
    p.z = v.v[2];
    return p;
}

// A new triangle set from the COM director (class and interface 0xfcfe5320) into `out`.
void NewMesh(cITriangleSetShape **out)
{
    static_cast<cIGZCOM *>(GetDirectorFrameworkObject())
        ->GetClassObject(cITriangleSetShape::kClassId, cITriangleSetShape::kClassId, reinterpret_cast<void **>(out));
}

// A new set sized for `vertices` vertices (all at the origin) and `triangles` triangles.
void SizeMesh(cITriangleSetShape *mesh, uint32_t vertices, uint32_t triangles)
{
    SetOf(mesh)->Resize(vertices, triangles);
    nGZCompGeom::Vec3 origin;
    origin.x = origin.y = origin.z = 0.0f;
    for (uint32_t i = 0; i < vertices; ++i) SetOf(mesh)->SetVertex(static_cast<uint16_t>(i), &origin);
}

// 0xaeda40: the header of the linkage's first element (in its list's order) with the identity, repetition and block
// format; mIndex -1 when there is none.
cGeometryElementHeader FindLinkedElement(cIGeometryDataContainer *container, uint32_t link, uint32_t identity,
                                         uint32_t repetition, uint32_t blockFormat)
{
    cGeometryLinkageHeader linkage;
    container->GetLinkageHeader(&linkage, link);
    for (uint32_t j = 0; j < linkage.mActiveCount; ++j) {
        Range<uint16_t> elements;
        container->GetLinkageElements(&elements.r, link);
        cGeometryElementHeader header;
        container->GetElementHeader(&header, elements.At(j));
        if (header.mIdentity == identity && header.mRepetition == repetition && header.mBlockFormat == blockFormat)
            return header;
    }
    cGeometryElementHeader none{};
    none.mIndex = static_cast<uint32_t>(-1);
    return none;
}

} // namespace

// 0xa85090: new and the constructor (most-derived), answered as the cIGeometryBuilder (reached through the
// cIBoundingVolumeBuilder's own virtual base, the same subobject).
cIGZUnknown *__cdecl cBoundingVolumeBuilder::Create()
{
    cBoundingVolumeBuilder *const builder = new cBoundingVolumeBuilder;
    cIGeometryBuilder *const face = static_cast<cIBoundingVolumeBuilder *>(builder);
    return face;
}

// 0xa84b20: cGeometryBuilder's part (0xa44b50), an empty name set, the flag clear.
cBoundingVolumeBuilder::cBoundingVolumeBuilder() = default;

// 0xa84c10: the name set, then cGeometryBuilder's part.
cBoundingVolumeBuilder::~cBoundingVolumeBuilder() = default;

// 0xa85f30 (ret 8): as cCompactorBuilder's (0xa83a00), for kIid.
bool cBoundingVolumeBuilder::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid != cIBoundingVolumeBuilder::kIid) return cGeometryBuilder::QueryInterface(iid, out);
    *out = static_cast<cIBoundingVolumeBuilder *>(this);
    static_cast<cIGeometryBuilder *>(this)->AddRef();
    return true;
}

// 0xa85f80 (ret 4): its own record, cGeometryBuilder's Read (0x9dc310); from version 2 the flag (GetBool), the name
// count (GetUint32, taken as signed) and each name (GetGZStr into a cRZString local) added to the set. Its own
// record's name released last.
void cBoundingVolumeBuilder::Read(ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cGeometryBuilder::Read(reader);
    if (info.mVersion > 1) {
        mNamedOnly = reader->GetBool();
        const int32_t count = static_cast<int32_t>(reader->GetUint32());
        for (int32_t i = 0; i < count; ++i) {
            nTSSG::SGLocalString name;
            reader->GetGZStr(&name.value);
            mNames.insert(std::string(name.value.ToChar()));
        }
    }
    if (info.mName) info.mName->Release();
}

// 0xa861f0 (hidden out): {"cBoundingVolumeBuilder" (a new counted cRZString), the class id, version 2}.
cPersistTypeInfo cBoundingVolumeBuilder::GetTypeInfo()
{
    cPersistTypeInfo info;
    info.mName = NewTypeName("cBoundingVolumeBuilder");
    info.mWord4 = kClassId;
    info.mVersion = kVersion;
    return info;
}

// 0xa862d0 (ret 4): its record (0xa861f0 directly), its name released, cGeometryBuilder's Write (0xa45790), then the
// flag (SetUint8), the name count (SetUint32) and each name in the set's order (WriteRZCharStr).
void cBoundingVolumeBuilder::Write(ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info = cBoundingVolumeBuilder::GetTypeInfo();
    writer->WriteTypeInfo(&info);
    if (info.mName) info.mName->Release();
    cGeometryBuilder::Write(writer);
    writer->SetUint8(mNamedOnly);
    writer->SetUint32(static_cast<uint32_t>(mNames.size()));
    for (const std::string &name : mNames) writer->WriteRZCharStr(const_cast<char *>(name.c_str()));
}

// 0xa84d89.
const char *cBoundingVolumeBuilder::GetName() { return "BoundingVolumeBuilder"; }

// 0xa84cc0.
void cBoundingVolumeBuilder::AddName(const char *name) { mNames.insert(std::string(name)); }

// 0x9e0f30.
void cBoundingVolumeBuilder::SetNamedOnly(bool namedOnly) { mNamedOnly = namedOnly; }

// 0xa85c60.
bool cBoundingVolumeBuilder::HasName(const char *name) { return mNames.find(std::string(name)) != mNames.end(); }

// 0xa85d50: nothing without groups and linkages. The bone of each vertex (none without weights), then two passes of
// Fill (the first makes the meshes at their sizes, the second puts the triangles in), then the container takes the
// bounding mesh and one subset mesh per transform (null where no triangle went), and the builder's references go.
void cBoundingVolumeBuilder::Build(cIGeometryDataContainer *container)
{
    if (container->CountGroups() == 0 || container->CountLinkages() == 0) return;
    cITriangleSetShape *mesh = nullptr;
    Meshes subsets(container->CountTransforms(), nullptr);
    std::vector<uint32_t> bones;
    SelectBones(container, bones);
    Fill(container, &mesh, subsets, bones);
    Fill(container, &mesh, subsets, bones);
    container->SetBoundingMesh(mesh);
    for (uint32_t i = 0; i < subsets.size(); ++i) container->SetSubsetMesh(i, subsets[i]);
    for (cITriangleSetShape *subset : subsets)
        if (subset) subset->Release();
    if (mesh) mesh->Release();
}

// 0xa84dc0: the weight elements (identity 0x5c4afc5c, block and set format 0, any repetition) in index order. The
// first sizes the tables to its vertex count (weights -1, bones 0xffffffff); each element's indices and weights are
// walked together, and a weight greater than the vertex's best so far makes the element's repetition its bone (a NaN
// weight never does).
void cBoundingVolumeBuilder::SelectBones(cIGeometryDataContainer *container, std::vector<uint32_t> &bones)
{
    std::vector<float> best;
    for (uint32_t i = 0; i < container->CountElements(); ++i) {
        cGeometryElementHeader header;
        container->GetElementHeader(&header, i);
        if (header.mIdentity != kWeightIdentity || header.mBlockFormat != 0 || header.mSetFormat != 0) continue;
        if (best.empty()) {
            best.resize(header.mNumber, -1.0f);
            bones.resize(header.mNumber, 0xffffffffu);
        }
        Range<float> weights;
        container->GetElementFloats(&weights.r, header.mIndex);
        Range<uint16_t> vertices;
        container->GetElementIndices(&vertices.r, header.mIndex);
        Rewind(vertices.r);
        while (!vertices.r.mCurrent->IsEqual(vertices.r.mEnd)) {
            const uint16_t vertex = vertices.r.mCurrent->Get();
            const float weight = weights.r.mCurrent->Get();
            if (weight > best[vertex]) {
                bones[vertex] = header.mRepetition;
                best[vertex] = weight;
            }
            vertices.r.mCurrent->Next();
            weights.r.mCurrent->Next();
        }
    }
}

// 0xa85120: each triangle-list group with indices (only the named ones when mNamedOnly) whose linkage has a position
// element (repetition 0, block format 2):
//   - without bones, every triangle goes to the bounding mesh, its corners the linkage's alias list of the positions'
//     set format applied to the group's indices, each corner at its position;
//   - with bones, each triangle goes to the subset mesh of each distinct bone among its corners' (the corners through
//     alias list 0; a vertex without a bone counts as bone 0), each corner at its position in that bone's frame.
// Corners and counts are counted whether or not the mesh exists yet. Afterwards a missing mesh with triangles is made
// at its size (all vertices at the origin), an existing one rebuilt.
void cBoundingVolumeBuilder::Fill(cIGeometryDataContainer *container, cITriangleSetShape **mesh, Meshes &subsets,
                                  const std::vector<uint32_t> &bones)
{
    std::vector<uint32_t> subsetHighest(subsets.size(), 0);
    std::vector<uint32_t> subsetTriangles(subsets.size(), 0);
    uint32_t triangles = 0;
    uint32_t highest = 0;

    for (uint32_t g = 0; g < container->CountGroups(); ++g) {
        cGeometryGroupInfo info;
        container->GetGroupInfo(&info, g);
        if (mNamedOnly && !HasName(info.mName)) continue;
        if (info.mPrimType != kTriangleList || info.mIndexCount == 0) continue;
        const cGeometryElementHeader position = FindLinkedElement(container, info.mLinkIndex, kPositionIdentity, 0, 2);
        if (position.mIndex == static_cast<uint32_t>(-1)) continue;

        Range<uint16_t> aliases;
        container->GetLinkageAliases(&aliases.r, info.mLinkIndex, position.mSetFormat);
        Range<cGeometryVector<3>> points;
        container->GetElementVectors3(&points.r, position.mIndex);
        Range<uint16_t> indices;
        container->GetGroupVertexIndices(&indices.r, g);

        if (bones.empty()) {
            for (uint32_t t = 0; t < info.mIndexCount; t += 3) {
                const uint16_t a = aliases.At(indices.At(t));
                const uint16_t b = aliases.At(indices.At(t + 1));
                const uint16_t c = aliases.At(indices.At(t + 2));
                if (*mesh) {
                    nGZCompGeom::TriangleSet *const set = SetOf(*mesh);
                    nGZCompGeom::Vec3 pa = ToVec3(points.At(a));
                    set->SetVertex(a, &pa);
                    nGZCompGeom::Vec3 pb = ToVec3(points.At(b));
                    set->SetVertex(b, &pb);
                    nGZCompGeom::Vec3 pc = ToVec3(points.At(c));
                    set->SetVertex(c, &pc);
                    set->SetTriangle(static_cast<int32_t>(triangles), a, b, c);
                }
                highest = std::max<uint32_t>({highest, a, b, c});
                ++triangles;
            }
            continue;
        }

        Range<uint16_t> positions;
        container->GetLinkageAliases(&positions.r, info.mLinkIndex, 0);
        for (uint32_t t = 0; t < info.mIndexCount; t += 3) {
            int32_t used[3] = {-1, -1, -1};
            for (uint32_t k = 0; k < 3; ++k) {
                const int32_t bone = std::max<int32_t>(0, static_cast<int32_t>(bones[positions.At(indices.At(t + k))]));
                if (used[0] != bone && used[1] != bone && used[2] != bone) used[k] = bone;
            }
            const uint16_t a = positions.At(indices.At(t));
            const uint16_t b = positions.At(indices.At(t + 1));
            const uint16_t c = positions.At(indices.At(t + 2));
            for (uint32_t k = 0; k < 3; ++k) {
                if (used[k] == -1) continue;
                const uint32_t bone = static_cast<uint32_t>(used[k]);
                if (cITriangleSetShape *const subset = subsets[bone]) {
                    nGZCompGeom::TriangleSet *const set = SetOf(subset);
                    cGeometryTransform frame;
                    nGZCompGeom::Vec3 pa = ToVec3(points.At(a));
                    reinterpret_cast<const nGZCompGeom::RigidTransform3 *>(container->GetTransform(&frame, bone))
                        ->TransformPoint(&pa);
                    nGZCompGeom::Vec3 pb = ToVec3(points.At(b));
                    reinterpret_cast<const nGZCompGeom::RigidTransform3 *>(container->GetTransform(&frame, bone))
                        ->TransformPoint(&pb);
                    nGZCompGeom::Vec3 pc = ToVec3(points.At(c));
                    reinterpret_cast<const nGZCompGeom::RigidTransform3 *>(container->GetTransform(&frame, bone))
                        ->TransformPoint(&pc);
                    set->SetVertex(a, &pa);
                    set->SetVertex(b, &pb);
                    set->SetVertex(c, &pc);
                    set->SetTriangle(static_cast<int32_t>(subsetTriangles[bone]), a, b, c);
                }
                subsetHighest[bone] = std::max<uint32_t>({subsetHighest[bone], a, b, c});
                ++subsetTriangles[bone];
            }
        }
    }

    if (triangles != 0) {
        if (*mesh == nullptr) {
            cITriangleSetShape *made = nullptr;
            NewMesh(&made);
            SizeMesh(made, highest + 1, triangles);
            *mesh = made;
            made->AddRef();
            made->Release();
        } else {
            SetOf(*mesh)->Build();
        }
    }
    for (uint32_t s = 0; s < subsetTriangles.size(); ++s) {
        if (subsetTriangles[s] == 0) continue;
        if (subsets[s] == nullptr) {
            NewMesh(&subsets[s]);
            SizeMesh(subsets[s], subsetHighest[s] + 1, subsetTriangles[s]);
        } else {
            SetOf(subsets[s])->Build();
        }
    }
}

} // namespace nGZSceneGraph
