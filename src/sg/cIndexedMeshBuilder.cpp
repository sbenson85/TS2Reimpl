// nGZSceneGraph::cIndexedMeshBuilder (lane QH). Layout and the hook decision: src/sg/cIndexedMeshBuilder.h.
#include "sg/cIndexedMeshBuilder.h"

#include "compgeom/Frames.h"
#include "game/game.h"
#include "game/original.h"
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "misc/SmallVirtuals.h" // nGZEvent::cNullEvent::kVTable
#include "gz/cEventFaces.h"    // nGZEvent::NullEventVTable (the owned word)
#include "sg/SGLocalString.h"
#include "sg/cGeometryDataContainerClass.h"
#include "sg/cObjectGraphNodeImpl.h" // NewTypeName

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace nGZSceneGraph {

static_assert(sizeof(cIndexedMeshBuilder) == 0x1e0, "cIndexedMeshBuilder size (the factory allocates 0x1e0)");
static_assert(offsetof(cIndexedMeshBuilder, mPositions) == 0x14, "positions");
static_assert(offsetof(cIndexedMeshBuilder, mNormals) == 0x20, "normals");
static_assert(offsetof(cIndexedMeshBuilder, mTexCoords) == 0x2c, "texture coordinates");
static_assert(offsetof(cIndexedMeshBuilder, mColours) == 0x5c, "colours");
static_assert(offsetof(cIndexedMeshBuilder, mWord74) == 0x74, "word 0x74");
static_assert(offsetof(cIndexedMeshBuilder, mWordsA) == 0x78, "words A");
static_assert(offsetof(cIndexedMeshBuilder, mWordsB) == 0x84, "words B");
static_assert(offsetof(cIndexedMeshBuilder, mGroups) == 0x90, "groups");
static_assert(offsetof(cIndexedMeshBuilder, mRuns) == 0x9c, "runs");
static_assert(offsetof(cIndexedMeshBuilder, mNames) == 0xa8, "names");
static_assert(offsetof(cIndexedMeshBuilder, mBones) == 0xb4, "bones");
static_assert(offsetof(cIndexedMeshBuilder, mInfluences) == 0xc0, "influences");
static_assert(offsetof(cIndexedMeshBuilder, mMorphs) == 0xcc, "morphs");
static_assert(offsetof(cIndexedMeshBuilder, mNameIndex) == 0xd8, "name index");
static_assert(offsetof(cIndexedMeshBuilder, mTransform) == 0xe0, "transform");
static_assert(offsetof(cIndexedMeshBuilder, mNormalMatrix) == 0x114, "normal matrix");
static_assert(offsetof(cIndexedMeshBuilder, mCollecting) == 0x138, "collecting");
static_assert(offsetof(cIndexedMeshBuilder, mCurrentName) == 0x13c, "current name");
static_assert(offsetof(cIndexedMeshBuilder, mGroupBase) == 0x148, "group base");
static_assert(offsetof(cIndexedMeshBuilder, mTexCoordBase) == 0x154, "texture coordinate bases");
static_assert(offsetof(cIndexedMeshBuilder, mColourBase) == 0x164, "colour bases");
static_assert(offsetof(cIndexedMeshBuilder, mPositionIndices) == 0x16c, "position indices");
static_assert(offsetof(cIndexedMeshBuilder, mNormalIndices) == 0x178, "normal indices");
static_assert(offsetof(cIndexedMeshBuilder, mTexCoordIndices) == 0x184, "texture coordinate indices");
static_assert(offsetof(cIndexedMeshBuilder, mColourIndices) == 0x1b4, "colour indices");

using indexedmesh::Bone;
using indexedmesh::Float2;
using indexedmesh::Float3;
using indexedmesh::Group;
using indexedmesh::Influence;
using indexedmesh::Morph;
using indexedmesh::Run;

namespace {

constexpr uint32_t kPositionIdentity = 0x5b830781;  // the vertex positions element
constexpr uint32_t kNormalIdentity = 0x3b83078b;    // the vertex normals element
constexpr uint32_t kTexCoordIdentity = 0xbb8307ab;  // the texture coordinates element
constexpr uint32_t kColourIdentity = 0xdb830795;    // the vertex colours element
constexpr uint32_t kInfluenceIdentity = 0x5c4afc5c; // a bone's weights element
constexpr uint32_t kMorphPositionIdentity = 0x1c4afc56;
constexpr uint32_t kMorphNormalIdentity = 0x7c4dee82;
constexpr uint32_t kWordsAIdentity = 0x114113c3;
constexpr uint32_t kWordsBIdentity = 0x114113cd;
constexpr uint32_t kTriangleList = 2;
constexpr uint32_t kFinishEvent = 0xbbb112f6; // sent to the listeners when Finish is done

// The writer's two array slots without names (18: signed words, 19: unsigned words), called through the vtable.
uint32_t WriterArray(ifaces::cIGZPersistWriter *writer, int slot, const void *data, uint32_t count)
{
    using Fn = uint32_t(__thiscall *)(void *, const void *, uint32_t);
    void **const table = *reinterpret_cast<void ***>(writer);
    return reinterpret_cast<Fn>(table[slot])(writer, data, count);
}
constexpr int kWriteSint32Array = 18;
constexpr int kWriteUint32Array = 19;

template <typename T> T *DataOrNull(std::vector<T> &v) { return v.empty() ? nullptr : v.data(); }

// A vector's elements as 16 bit words (the container's index lists are 16 bit).
std::vector<uint16_t> Low16(const uint32_t *begin, size_t count)
{
    std::vector<uint16_t> out;
    out.resize(count);
    for (size_t i = 0; i < count; ++i) out[i] = static_cast<uint16_t>(begin[i]);
    return out;
}

// One colour channel as a byte: the float (0..1 scaled by the caller) plus 49152.0f has its integer part in the low
// mantissa bits; the result is clamped to 0..255 without branches.
uint32_t ChannelByte(float f)
{
    const float shifted = f + 49152.0f;
    const int32_t x = static_cast<int32_t>(std::bit_cast<uint32_t>(shifted)) - 0x47400000;
    return static_cast<uint32_t>(((~(x >> 31) & x) | ((0xff - x) >> 31)) & 0xff);
}

uint32_t PackColour(const float *rgba)
{
    const uint32_t a = ChannelByte(rgba[0]);
    const uint32_t b = ChannelByte(rgba[1]);
    const uint32_t c = ChannelByte(rgba[2]);
    const uint32_t d = ChannelByte(rgba[3]);
    return (a << 24) | (b << 16) | (c << 8) | d;
}

// 1/length in the game's way: the sum in float, the root in double.
float InverseLength(float squared) { return 1.0f / static_cast<float>(std::sqrt(static_cast<double>(squared))); }

} // namespace

// 0xa7ffa0: new and the constructor (most-derived), answered as the cIGeometryBuilder.
cIGZUnknown *__cdecl cIndexedMeshBuilder::Create()
{
    cIndexedMeshBuilder *const builder = new cIndexedMeshBuilder;
    cIGeometryBuilder *const face = static_cast<cIIndexedMeshBuilder *>(builder);
    return face;
}

// 0xa7b1d0: cGeometryBuilder's part (0xa44b50), empty lists, the identity transform and normal matrix, -1 for the three
// group / name cursors, an empty name index.
cIndexedMeshBuilder::cIndexedMeshBuilder() = default;

// 0xa7b8d0: every list and the name index, then cGeometryBuilder's part.
cIndexedMeshBuilder::~cIndexedMeshBuilder() = default;

// 0xa80c30 (ret 8): as cCompactorBuilder's (0xa83a00), for kIid.
bool cIndexedMeshBuilder::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid != cIIndexedMeshBuilder::kIid) return cGeometryBuilder::QueryInterface(iid, out);
    *out = static_cast<cIIndexedMeshBuilder *>(this);
    static_cast<cIGeometryBuilder *>(this)->AddRef();
    return true;
}

// 0xa81fc0 (hidden out): {"cIndexedMeshBuilder" (a new counted cRZString), the class id, version 10}.
cPersistTypeInfo cIndexedMeshBuilder::GetTypeInfo()
{
    cPersistTypeInfo info;
    info.mName = NewTypeName("cIndexedMeshBuilder");
    info.mWord4 = kClassId;
    info.mVersion = kVersion;
    return info;
}

// 0xa7e368.
const char *cIndexedMeshBuilder::GetName() { return "IndexedMeshBuilder"; }

// ---- reading the mesh in ----

// 0xa7d960 / 0xa7da50: the point through the transform.
void cIndexedMeshBuilder::AddPositions(uint32_t count, const float *points)
{
    mPositions.reserve(mPositions.size() + count);
    for (uint32_t i = 0; i < count; ++i) {
        Float3 p;
        mTransform.TransformPoint(p.v, points + 3 * i);
        mPositions.push_back(p);
    }
}

void cIndexedMeshBuilder::AddPosition(const float *point)
{
    Float3 p;
    mTransform.TransformPoint(p.v, point);
    mPositions.push_back(p);
}

// 0xa7d780: the vector through the normal matrix and normalised (term order is the game's, for the float result).
void cIndexedMeshBuilder::AddNormals(uint32_t count, const float *vectors)
{
    mNormals.reserve(mNormals.size() + count);
    const float *m = mNormalMatrix;
    for (uint32_t i = 0; i < count; ++i) {
        const float x = vectors[3 * i], y = vectors[3 * i + 1], z = vectors[3 * i + 2];
        const float nx = (y * m[1] + x * m[0]) + z * m[2];
        const float ny = (x * m[3] + y * m[4]) + z * m[5];
        const float nz = (x * m[6] + y * m[7]) + z * m[8];
        const float s = InverseLength((ny * ny + nx * nx) + nz * nz);
        mNormals.push_back(Float3{{s * nx, s * ny, s * nz}});
    }
}

// 0xa7d590: the same for one, the terms in another order.
void cIndexedMeshBuilder::AddNormal(const float *vector)
{
    const float *m = mNormalMatrix;
    const float x = vector[0], y = vector[1], z = vector[2];
    const float nx = (y * m[1] + x * m[0]) + z * m[2];
    const float ny = (y * m[4] + x * m[3]) + z * m[5];
    const float nz = (y * m[7] + x * m[6]) + z * m[8];
    const float s = InverseLength((ny * ny + nx * nx) + nz * nz);
    mNormals.push_back(Float3{{nx * s, ny * s, nz * s}});
}

// 0xa7dc90: count pairs at the end of the set's list.
void cIndexedMeshBuilder::AddTexCoords(uint32_t count, const float *coords, uint32_t set)
{
    std::vector<Float2> &list = mTexCoords[set];
    const Float2 *src = reinterpret_cast<const Float2 *>(coords);
    list.insert(list.end(), src, src + count);
}

// 0xa7dbc8: slot 5 with one.
void cIndexedMeshBuilder::AddTexCoord(const float *coord, uint32_t set) { AddTexCoords(1, coord, set); }

// 0xa7c670 / 0xa7c4b4: four floats a colour, packed to four bytes.
void cIndexedMeshBuilder::AddColours(uint32_t count, const float *rgba, uint32_t set)
{
    std::vector<uint32_t> &list = mColours[set];
    list.reserve(list.size() + count);
    for (uint32_t i = 0; i < count; ++i) list.push_back(PackColour(rgba + 4 * i));
}

void cIndexedMeshBuilder::AddColour(const float *rgba, uint32_t set) { mColours[set].push_back(PackColour(rgba)); }

// 0xa7dcc7 / 0xa7db33.
void cIndexedMeshBuilder::AddWordsA(uint32_t count, const uint32_t *values)
{
    mWordsA.reserve(mWordsA.size() + count);
    mWordsA.insert(mWordsA.end(), values, values + count);
}

void cIndexedMeshBuilder::AddWordsB(uint32_t count, const uint32_t *values)
{
    mWordsB.reserve(mWordsB.size() + count);
    mWordsB.insert(mWordsB.end(), values, values + count);
}

// 0xa81ebf: the 13 words kept; the normal matrix is the inverse's transpose of the matrix.
void cIndexedMeshBuilder::SetTransform(const AffineTransform3 *transform)
{
    mTransform = *transform;
    float inverse[9];
    nGZCompGeom::InverseMatrix3(inverse, mTransform.mMatrix);
    nGZCompGeom::TransposeMatrix(mNormalMatrix, inverse);
}

// ---- bones ----

// 0xa81b10: both lists to the count (new influences empty, new bones the default).
void cIndexedMeshBuilder::SetBoneCount(uint32_t count)
{
    mInfluences.resize(count);
    mBones.resize(count);
}

// 0xa81b6e.
void cIndexedMeshBuilder::SetBoneData(uint32_t bone, const void *data) { std::memcpy(&mBones[bone], data, sizeof(Bone)); }

// 0xa7c395: the vertices are relative to the position list's size at Reset.
void cIndexedMeshBuilder::AddInfluences(uint32_t bone, uint32_t count, const uint32_t *vertices, const float *weights)
{
    Influence &influence = mInfluences[bone];
    influence.vertices.reserve(influence.vertices.size() + count);
    influence.weights.reserve(influence.weights.size() + count);
    for (uint32_t i = 0; i < count; ++i) {
        influence.vertices.push_back(vertices[i] + mPositionBase);
        influence.weights.push_back(weights[i]);
    }
}

// 0xa7c313.
void cIndexedMeshBuilder::AddInfluence(uint32_t bone, uint32_t vertex, float weight)
{
    Influence &influence = mInfluences[bone];
    influence.vertices.push_back(vertex + mPositionBase);
    influence.weights.push_back(weight);
}

// ---- morph targets ----

// 0xa81ae1 -> 0xa83740.
void cIndexedMeshBuilder::SetMorphTargetCount(uint32_t count) { mMorphs.resize(count); }

// 0xa81a73.
void cIndexedMeshBuilder::SetMorphNames(uint32_t morph, const char *first, const char *second)
{
    mMorphs[morph].first.value.mString = first;
    mMorphs[morph].second.value.mString = second;
}

// 0xa7d440: the transformed point's difference from the position at the vertex (relative to the list's size at Reset).
void cIndexedMeshBuilder::AddMorphPositions(uint32_t morph, uint32_t count, const float *points)
{
    Morph &m = mMorphs[morph];
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t vertex = mPositionBase + i;
        Float3 p;
        mTransform.TransformPoint(p.v, points + 3 * i);
        const Float3 &base = mPositions[vertex];
        const Float3 d{{p.v[0] - base.v[0], p.v[1] - base.v[1], p.v[2] - base.v[2]}};
        m.positionVertices.push_back(vertex);
        m.positionDeltas.push_back(d);
    }
}

// 0xa7cfe0: the normalised normal's difference from the normal at the vertex.
void cIndexedMeshBuilder::AddMorphNormals(uint32_t morph, uint32_t count, const float *vectors)
{
    Morph &m = mMorphs[morph];
    const float *n = mNormalMatrix;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t vertex = mNormalBase + i;
        const float x = vectors[3 * i], y = vectors[3 * i + 1], z = vectors[3 * i + 2];
        const float nx = (y * n[1] + x * n[0]) + z * n[2];
        const float ny = (x * n[3] + y * n[4]) + z * n[5];
        const float nz = (x * n[6] + y * n[7]) + z * n[8];
        const float s = InverseLength((ny * ny + nx * nx) + nz * nz);
        const Float3 &base = mNormals[vertex];
        const Float3 d{{s * nx - base.v[0], s * ny - base.v[1], s * nz - base.v[2]}};
        m.normalVertices.push_back(vertex);
        m.normalDeltas.push_back(d);
    }
}

// 0xa7d390: the count of positions' vertices and vectors replaced; the vectors through the transform's matrix only.
void cIndexedMeshBuilder::SetMorphPositions(uint32_t morph, uint32_t count, const float *vectors, const int32_t *vertices)
{
    Morph &m = mMorphs[morph];
    m.positionVertices.resize(count);
    m.positionDeltas.resize(count);
    const nGZCompGeom::FrameMath &frame = nGZCompGeom::AsFrame(mTransform);
    for (uint32_t i = 0; i < count; ++i) {
        nGZCompGeom::Vec3 out;
        frame.VectorTo(&out, reinterpret_cast<const nGZCompGeom::Vec3 *>(vectors + 3 * i));
        m.positionVertices[i] = static_cast<uint32_t>(vertices[i]) + mPositionBase;
        m.positionDeltas[i] = Float3{{out.x, out.y, out.z}};
    }
}

// 0xa7cdd0: the vectors through the normal matrix, scaled by one factor from the matrix's first column.
void cIndexedMeshBuilder::SetMorphNormals(uint32_t morph, uint32_t count, const float *vectors, const int32_t *vertices)
{
    Morph &m = mMorphs[morph];
    m.normalVertices.resize(count);
    m.normalDeltas.resize(count);
    const float *n = mNormalMatrix;
    const float s = InverseLength((n[3] * n[3] + n[0] * n[0]) + n[6] * n[6]);
    float scaled[9]; // the matrix times the factor first (the original hoists it out of the loop)
    for (int k = 0; k < 9; ++k) scaled[k] = n[k] * s;
    for (uint32_t i = 0; i < count; ++i) {
        const float x = vectors[3 * i], y = vectors[3 * i + 1], z = vectors[3 * i + 2];
        m.normalVertices[i] = static_cast<uint32_t>(vertices[i]) + mNormalBase;
        m.normalDeltas[i] = Float3{{(x * scaled[0] + y * scaled[1]) + z * scaled[2],
                                    (x * scaled[3] + y * scaled[4]) + z * scaled[5],
                                    (y * scaled[7] + x * scaled[6]) + z * scaled[8]}};
    }
}

// 0xa7d1e0.
void cIndexedMeshBuilder::AddMorphPosition(uint32_t morph, int32_t vertex, const float *point)
{
    Morph &m = mMorphs[morph];
    const uint32_t at = static_cast<uint32_t>(vertex) + mPositionBase;
    Float3 p;
    mTransform.TransformPoint(p.v, point);
    const Float3 &base = mPositions[at];
    const Float3 d{{p.v[0] - base.v[0], p.v[1] - base.v[1], p.v[2] - base.v[2]}};
    m.positionVertices.push_back(at);
    m.positionDeltas.push_back(d);
}

// 0xa7caf0.
void cIndexedMeshBuilder::AddMorphNormal(uint32_t morph, int32_t vertex, const float *vector)
{
    Morph &m = mMorphs[morph];
    const uint32_t at = static_cast<uint32_t>(vertex) + mNormalBase;
    const float *n = mNormalMatrix;
    const float x = vector[0], y = vector[1], z = vector[2];
    const float nx = (y * n[1] + x * n[0]) + z * n[2];
    const float ny = (y * n[4] + x * n[3]) + z * n[5];
    const float nz = (y * n[7] + x * n[6]) + z * n[8];
    const float s = InverseLength((ny * ny + nx * nx) + nz * nz);
    const Float3 &base = mNormals[at];
    const Float3 d{{nx * s - base.v[0], ny * s - base.v[1], nz * s - base.v[2]}};
    m.normalVertices.push_back(at);
    m.normalDeltas.push_back(d);
}

// 0xa7d2f0: the transformed point itself.
void cIndexedMeshBuilder::AddMorphPositionAbsolute(uint32_t morph, int32_t vertex, const float *point)
{
    Morph &m = mMorphs[morph];
    Float3 p;
    mTransform.TransformPoint(p.v, point);
    m.positionVertices.push_back(static_cast<uint32_t>(vertex) + mPositionBase);
    m.positionDeltas.push_back(p);
}

// 0xa7ccb0: the vector through the normal matrix, not normalised.
void cIndexedMeshBuilder::AddMorphNormalRaw(uint32_t morph, int32_t vertex, const float *vector)
{
    Morph &m = mMorphs[morph];
    const float *n = mNormalMatrix;
    const float x = vector[0], y = vector[1], z = vector[2];
    const Float3 v{{(y * n[1] + x * n[0]) + z * n[2], (y * n[4] + x * n[3]) + z * n[5], (y * n[7] + x * n[6]) + z * n[8]}};
    m.normalVertices.push_back(static_cast<uint32_t>(vertex) + mNormalBase);
    m.normalDeltas.push_back(v);
}

// ---- groups ----

// 0xa81bc0: the name's index (new names appended).
void cIndexedMeshBuilder::SetGroupName(const char *name)
{
    const std::string key(name);
    auto found = mNameIndex.find(key);
    if (found != mNameIndex.end()) {
        mCurrentName = static_cast<int32_t>(found->second);
        return;
    }
    mCurrentName = static_cast<int32_t>(mNames.size());
    mNames.push_back(key);
    mNameIndex.insert(std::make_pair(key, static_cast<uint32_t>(mCurrentName)));
}

// 0xa7e34d.
void cIndexedMeshBuilder::MarkUnknown() { mUnknown = true; }

// 0xa7fa10: the vertices collected (index lists, one entry per vertex) become a vertex run of the group that has the
// same attributes (the first from the group base; a new one when none) and, as a fan, triangles.
void cIndexedMeshBuilder::CommitGroup()
{
    uint32_t mask = 0;
    if (!mNormalIndices.empty()) mask |= 1;
    for (uint32_t k = 0; k < 4; ++k)
        if (!mTexCoordIndices[k].empty()) mask |= 2u << k;
    for (uint32_t k = 0; k < 2; ++k)
        if (!mColourIndices[k].empty()) mask |= 0x20u << k;

    uint32_t index = mGroupBase;
    while (index < mGroups.size() && mGroups[index].flags != mask) ++index;
    if (index == mGroups.size()) {
        mGroups.resize(index + 1);
        mGroups[index].flags = mask;
        mGroups[index].count = 0;
    }
    Group &group = mGroups[index];

    for (size_t j = 0; j < mPositionIndices.size(); ++j) {
        group.positions.push_back(mPositionIndices[j] + mPositionBase);
        if (mNormalIndices.size() == 1)
            group.normals.push_back(mNormalIndices[0] + mNormalBase);
        else if (group.flags & 1)
            group.normals.push_back(mNormalIndices[j] + mNormalBase);
        for (uint32_t k = 0; k < 4; ++k) {
            if (mTexCoordIndices[k].size() == 1)
                group.texCoords[k].push_back(mTexCoordIndices[k][0] + mTexCoordBase[k]);
            else if (group.flags & (2u << k))
                group.texCoords[k].push_back(mTexCoordIndices[k][j] + mTexCoordBase[k]);
        }
        for (uint32_t k = 0; k < 2; ++k) {
            if (mColourIndices[k].size() == 1)
                group.colours[k].push_back(mColourIndices[k][0] + mColourBase[k]);
            else if (group.flags & (0x20u << k))
                group.colours[k].push_back(mColourIndices[k][j] + mColourBase[k]);
        }
    }
    const uint32_t oldCount = group.count;
    group.count = static_cast<uint32_t>(group.positions.size());

    if (static_cast<int32_t>(index) != mCurrentGroup || mCurrentName != mLastName) {
        if (mCurrentGroup >= 0 && !mRuns.empty())
            mRuns.back().end = static_cast<int32_t>(mGroups[mCurrentGroup].triangles.size());
        mRuns.push_back(Run{mCurrentName, static_cast<int32_t>(index), static_cast<int32_t>(group.triangles.size()), -1});
    }
    mCurrentGroup = static_cast<int32_t>(index);
    mLastName = mCurrentName;

    const size_t n = mPositionIndices.size();
    for (size_t t = 2; t < n; ++t) {
        group.triangles.push_back(oldCount);
        group.triangles.push_back(oldCount + static_cast<uint32_t>(t) - 1);
        group.triangles.push_back(oldCount + static_cast<uint32_t>(t));
    }

    mPositionIndices.clear();
    mNormalIndices.clear();
    for (auto &list : mTexCoordIndices) list.clear();
    for (auto &list : mColourIndices) list.clear();
    mUnknown = false;
}

// The index lists: values appended, each plus the offset.
void cIndexedMeshBuilder::AddIndicesTo(std::vector<uint32_t> &list, uint32_t count, const uint32_t *values, int32_t offset)
{
    const size_t first = list.size();
    list.insert(list.end(), values, values + count);
    if (offset != 0)
        for (size_t i = first; i < list.size(); ++i) list[i] += static_cast<uint32_t>(offset);
}

void cIndexedMeshBuilder::AddPositionIndices(uint32_t count, const uint32_t *values, int32_t offset)
{
    AddIndicesTo(mPositionIndices, count, values, offset);
}
void cIndexedMeshBuilder::AddNormalIndices(uint32_t count, const uint32_t *values, int32_t offset)
{
    AddIndicesTo(mNormalIndices, count, values, offset);
}
void cIndexedMeshBuilder::AddTexCoordIndices(uint32_t count, const uint32_t *values, uint32_t set, int32_t offset)
{
    AddIndicesTo(mTexCoordIndices[set], count, values, offset);
}
void cIndexedMeshBuilder::AddColourIndices(uint32_t count, const uint32_t *values, uint32_t set, int32_t offset)
{
    AddIndicesTo(mColourIndices[set], count, values, offset);
}

// 0xa7c957: the kinds named by the flag bits, through the slots above.
void cIndexedMeshBuilder::AddIndices(uint8_t kinds, uint32_t count, const uint32_t *values, int32_t offset)
{
    if (kinds & 0x01) AddPositionIndices(count, values, offset);
    if (kinds & 0x02) AddNormalIndices(count, values, offset);
    if (kinds & 0x04) AddTexCoordIndices(count, values, 0, offset);
    if (kinds & 0x08) AddTexCoordIndices(count, values, 1, offset);
    if (kinds & 0x10) AddTexCoordIndices(count, values, 2, offset);
    if (kinds & 0x20) AddTexCoordIndices(count, values, 3, offset);
    if (kinds & 0x40) AddColourIndices(count, values, 0, offset);
    if (kinds & 0x80) AddColourIndices(count, values, 1, offset);
}

void cIndexedMeshBuilder::AddPositionIndex(uint32_t value) { mPositionIndices.push_back(value); }
void cIndexedMeshBuilder::AddNormalIndex(uint32_t value) { mNormalIndices.push_back(value); }
void cIndexedMeshBuilder::AddTexCoordIndex(uint32_t value, uint32_t set) { mTexCoordIndices[set].push_back(value); }
void cIndexedMeshBuilder::AddColourIndex(uint32_t value, uint32_t set) { mColourIndices[set].push_back(value); }

// 0xa7c7ec.
void cIndexedMeshBuilder::AddIndex(uint8_t kinds, uint32_t value)
{
    if (kinds & 0x01) mPositionIndices.push_back(value);
    if (kinds & 0x02) mNormalIndices.push_back(value);
    for (uint32_t k = 0; k < 4; ++k)
        if (kinds & (0x04u << k)) mTexCoordIndices[k].push_back(value);
    for (uint32_t k = 0; k < 2; ++k)
        if (kinds & (0x40u << k)) mColourIndices[k].push_back(value);
}

// ---- state ----

bool cIndexedMeshBuilder::IsCollecting() { return mCollecting; }
bool cIndexedMeshBuilder::GetUnknown() { return mUnknown; }
uint32_t cIndexedMeshBuilder::CountBones() { return static_cast<uint32_t>(mBones.size()); }
uint32_t cIndexedMeshBuilder::CountMorphTargets() { return static_cast<uint32_t>(mMorphs.size()); }
uint32_t cIndexedMeshBuilder::CountPositions() { return static_cast<uint32_t>(mPositions.size()) - mPositionBase; }
uint32_t cIndexedMeshBuilder::CountNormals() { return static_cast<uint32_t>(mNormals.size()) - mNormalBase; }
uint32_t cIndexedMeshBuilder::CountTexCoords(uint32_t set)
{
    return static_cast<uint32_t>(mTexCoords[set].size()) - mTexCoordBase[set];
}
uint32_t cIndexedMeshBuilder::CountColours(uint32_t set)
{
    return static_cast<uint32_t>(mColours[set].size()) - mColourBase[set];
}

// 0xa7dd80: start collecting. Without keepGroups everything is dropped; with it the groups stay (their flags kept for the
// attributes and the extra lists; the index lists of the texture coordinate and colour sets named by the flags become
// what the group's source list holds). Either way the bases are the lists' sizes now, so the next mesh's indices start
// after what is there.
void cIndexedMeshBuilder::Reset(bool keepGroups)
{
    mCollecting = true;
    if (!keepGroups) {
        mPositions.clear();
        mNormals.clear();
        mWordsA.clear();
        mWordsB.clear();
        for (auto &list : mTexCoords) list.clear();
        for (auto &list : mColours) list.clear();
        mBones.clear();
        mInfluences.clear();
        mMorphs.clear();
        mGroups.clear();
        mRuns.clear();
        mNames.clear();
        mNameIndex.clear();
    } else {
        for (Group &group : mGroups) {
            group.flags &= 0xc7f;
            for (uint32_t k = 0; k < 4; ++k)
                if (group.flags & (2u << k)) group.texCoords[k] = group.source;
            for (uint32_t k = 0; k < 2; ++k)
                if (group.flags & (0x20u << k)) group.colours[k] = group.source;
            if (!group.source.empty()) group.source.clear();
        }
    }
    mGroupBase = static_cast<uint32_t>(mGroups.size());
    mPositionBase = static_cast<uint32_t>(mPositions.size());
    mNormalBase = static_cast<uint32_t>(mNormals.size());
    for (uint32_t k = 0; k < 4; ++k) mTexCoordBase[k] = static_cast<uint32_t>(mTexCoords[k].size());
    for (uint32_t k = 0; k < 2; ++k) mColourBase[k] = static_cast<uint32_t>(mColours[k].size());
    mWord74 = 0;
    mLastName = -1;
    mCurrentGroup = -1;
}

// 0xa7f940: close the last run, the five helpers, done, and the listeners told.
void cIndexedMeshBuilder::Finish()
{
    if (mCurrentGroup >= 0) mRuns.back().end = static_cast<int32_t>(mGroups[mCurrentGroup].triangles.size());
    WeldVertices();
    MergeDuplicateVertices();
    ClassifyGroups();
    MergeGroups();
    RebuildRuns();
    mCollecting = false;
    const void *event = nGZEvent::NullEventVTable();
    NotifyListeners(kFinishEvent, 0, &event);
}

// ---- Finish's helpers ----

namespace {

// The six words a vertex of the welded lists is made of: the texture coordinate index of each of the four sets, then the
// colour index of each of the two. kNone: the group has no such list.
using Tuple = std::array<uint32_t, 6>;
constexpr uint32_t kNone = 0xffffffffu;

} // namespace

// 0xa7e380: the vertices of every group that has texture coordinates or colours (flags & 0x7e) are tuples of their
// indices into those lists. Equal tuples become one vertex: the group's lists of indices are emptied, its `source` says
// which vertex each of its vertices is, and the builder's own lists keep one entry per distinct tuple (in the order of
// the tuples, smallest first).
void cIndexedMeshBuilder::WeldVertices()
{
    std::vector<Tuple> tuples;
    std::vector<uint32_t> starts;
    Tuple none;
    none.fill(kNone);
    for (Group &group : mGroups) {
        starts.push_back(static_cast<uint32_t>(tuples.size()));
        if (!(group.flags & 0x7e)) continue;
        const size_t first = tuples.size();
        tuples.resize(first + group.count, none);
        for (uint32_t k = 0; k < 4; ++k) {
            std::vector<uint32_t> &list = group.texCoords[k];
            if (list.empty()) continue;
            for (uint32_t i = 0; i < group.count; ++i) tuples[first + i][k] = list[i];
            list.clear();
        }
        for (uint32_t k = 0; k < 2; ++k) {
            std::vector<uint32_t> &list = group.colours[k];
            if (list.empty()) continue;
            for (uint32_t i = 0; i < group.count; ++i) tuples[first + i][4 + k] = list[i];
            list.clear();
        }
    }
    if (tuples.empty()) return;

    // Smallest tuple first; the vertex of a tuple is the number of
    // distinct tuples before it, and the representative of a vertex its first tuple in that order.
    std::vector<uint32_t> order(tuples.size());
    for (uint32_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return tuples[a] < tuples[b]; });
    std::vector<uint32_t> vertexOf(tuples.size());
    std::vector<uint32_t> representatives;
    for (size_t n = 0; n < order.size(); ++n) {
        if (n == 0 || tuples[order[n]] != tuples[order[n - 1]]) representatives.push_back(order[n]);
        vertexOf[order[n]] = static_cast<uint32_t>(representatives.size() - 1);
    }

    for (size_t g = 0; g < mGroups.size(); ++g) {
        Group &group = mGroups[g];
        if (!(group.flags & 0x7e)) continue;
        group.source.resize(group.count);
        for (uint32_t i = 0; i < group.count; ++i) group.source[i] = vertexOf[starts[g] + i];
    }
    for (uint32_t k = 0; k < 4; ++k) {
        if (mTexCoords[k].empty()) continue;
        std::vector<Float2> welded;
        welded.reserve(representatives.size());
        for (const uint32_t r : representatives) {
            const uint32_t index = tuples[r][k];
            welded.push_back(index == kNone ? Float2{{0.0f, 0.0f}} : mTexCoords[k][index]);
        }
        mTexCoords[k] = std::move(welded);
    }
    for (uint32_t k = 0; k < 2; ++k) {
        if (mColours[k].empty()) continue;
        std::vector<uint32_t> welded;
        welded.reserve(representatives.size());
        for (const uint32_t r : representatives) {
            const uint32_t index = tuples[r][4 + k];
            welded.push_back(index == kNone ? 0u : mColours[k][index]);
        }
        mColours[k] = std::move(welded);
    }
    mWord74 = static_cast<uint32_t>(representatives.size());
}

// 0xa7f4a0: inside each group, vertices with equal position, normal and source indices become one (numbered in the order
// of those keys, position first, equal keys by first appearance). The triangles are renumbered and the group's lists keep
// one entry per distinct vertex.
// ORIGINAL-BUG: a group with no vertices (a committed empty polygon) makes the game read the first element of an empty
// array and crash; the same read is here.
void cIndexedMeshBuilder::MergeDuplicateVertices()
{
    for (Group &group : mGroups) {
        const uint32_t n = group.count;
        std::vector<uint32_t> order(n);
        for (uint32_t i = 0; i < n; ++i) order[i] = i;
        const auto compare = [](const std::vector<uint32_t> &list, uint32_t a, uint32_t b) {
            if (list.empty()) return 0;
            const int32_t x = static_cast<int32_t>(list[a]);
            const int32_t y = static_cast<int32_t>(list[b]);
            return x < y ? -1 : (x > y ? 1 : 0);
        };
        std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
            int c = compare(group.positions, a, b);
            if (c == 0) c = compare(group.normals, a, b);
            if (c == 0) c = compare(group.source, a, b);
            return c < 0;
        });

        std::vector<int32_t> mapping(n, -1);
        std::vector<uint32_t> representatives;
        representatives.push_back(order[0]);
        mapping[order[0]] = 0;
        for (uint32_t k = 1; k < n; ++k) {
            const uint32_t current = order[k];
            const uint32_t last = representatives.back();
            const bool same = group.positions[current] == group.positions[last] &&
                              (group.normals.empty() || group.normals[current] == group.normals[last]) &&
                              (group.source.empty() || group.source[current] == group.source[last]);
            if (!same) representatives.push_back(current);
            mapping[current] = static_cast<int32_t>(representatives.size() - 1);
        }
        for (uint32_t &index : group.triangles) index = static_cast<uint32_t>(mapping[index]);
        for (std::vector<uint32_t> *list : {&group.positions, &group.normals, &group.source}) {
            if (list->empty()) continue;
            std::vector<uint32_t> kept;
            kept.reserve(representatives.size());
            for (const uint32_t r : representatives) kept.push_back((*list)[r]);
            *list = std::move(kept);
        }
        group.count = static_cast<uint32_t>(representatives.size());
    }
}

// 0xa820a0: which groups use bone influences, morph position / normal deltas (moved by more than a tiny amount), and the
// two extra word lists (for the groups added since Reset), as flag bits 7, 8, 9, 10, 11. Every index is trusted: on a
// builder that was not finished (or whose lists disagree) the game reads out of range here, and so does this.
void cIndexedMeshBuilder::ClassifyGroups()
{
    constexpr float kWeightLimit = std::bit_cast<float>(0x38d1b717u); // 1e-4f
    constexpr float kMoveLimit = std::bit_cast<float>(0x322bcc76u);   // 1e-8f, on the squared length
    std::vector<int32_t> influenced(mPositions.size(), 0);
    std::vector<int32_t> positionMoved(mPositions.size(), 0);
    std::vector<int32_t> normalMoved(mNormals.size(), 0);
    for (const Influence &influence : mInfluences)
        for (size_t j = 0; j < influence.vertices.size(); ++j)
            if (influence.weights[j] > kWeightLimit) ++influenced[influence.vertices[j]];
    for (const Morph &morph : mMorphs) {
        for (size_t j = 0; j < morph.positionVertices.size(); ++j) {
            const Float3 &d = morph.positionDeltas[j];
            const float length = d.v[0] * d.v[0] + d.v[1] * d.v[1] + d.v[2] * d.v[2];
            if (length > kMoveLimit) ++positionMoved[morph.positionVertices[j]];
        }
        for (size_t j = 0; j < morph.normalVertices.size(); ++j) {
            const Float3 &d = morph.normalDeltas[j];
            const float length = d.v[0] * d.v[0] + d.v[1] * d.v[1] + d.v[2] * d.v[2];
            if (length > kMoveLimit) ++normalMoved[morph.normalVertices[j]];
        }
    }
    for (size_t g = 0; g < mGroups.size(); ++g) {
        Group &group = mGroups[g];
        const bool added = g >= mGroupBase;
        if (!group.positions.empty()) {
            uint32_t i = 0;
            do {
                const uint32_t p = group.positions[i];
                if (influenced[p] > 0) group.flags |= 0x80;
                if (positionMoved[p] > 0) group.flags |= 0x100;
                if (added) {
                    if (!mWordsA.empty() && mWordsA[p] != 0) group.flags |= 0x400;
                    if (!mWordsB.empty() && mWordsB[p] != 0) group.flags |= 0x800;
                }
                const bool more = (group.flags & 0x180) != 0x180 || (added && (group.flags & 0xc00) != 0xc00);
                if (!more) break;
            } while (++i < group.count);
        }
        if (!group.normals.empty() && group.count != 0) {
            for (uint32_t i = 0; i < group.count; ++i) {
                if (normalMoved[group.normals[i]] > 0) {
                    group.flags |= 0x200;
                    break;
                }
            }
        }
    }
}

// 0xa7ed00: groups with equal flags become one group (taken in the order of their flags, equal flags in their order), the
// later ones' vertices and triangles appended to the first's; the runs are renumbered and moved along.
void cIndexedMeshBuilder::MergeGroups()
{
    const size_t n = mGroups.size();
    if (n <= 1) return;
    std::vector<uint32_t> order(n);
    for (uint32_t i = 0; i < n; ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return mGroups[a].flags < mGroups[b].flags; });

    std::vector<Group> merged;
    std::vector<int32_t> newIndex(n, 0);
    std::vector<int32_t> triangleShift(n, 0);
    merged.push_back(mGroups[order[0]]);
    for (size_t k = 1; k < n; ++k) {
        const uint32_t current = order[k];
        const Group &group = mGroups[current];
        Group &last = merged.back();
        if (last.flags == group.flags) {
            triangleShift[current] = static_cast<int32_t>(last.triangles.size());
            const uint32_t oldCount = last.count;
            last.count += group.count;
            last.positions.insert(last.positions.end(), group.positions.begin(), group.positions.end());
            last.normals.insert(last.normals.end(), group.normals.begin(), group.normals.end());
            last.source.insert(last.source.end(), group.source.begin(), group.source.end());
            for (const uint32_t index : group.triangles) last.triangles.push_back(index + oldCount);
        } else {
            merged.push_back(group);
            triangleShift[current] = 0;
        }
        newIndex[current] = static_cast<int32_t>(merged.size() - 1);
    }
    for (Run &run : mRuns) {
        const int32_t old = run.group;
        run.start += triangleShift[old];
        run.end += triangleShift[old];
        run.group = newIndex[old];
    }
    mGroups = merged; // a copy-assign, as the game's: lists of the old groups that end up empty keep their buffers
}

// 0xa7f0b0: each group's triangles are put in the order of the names (all of a name's runs in that group together, in
// the order the runs were made), and one run is made for each name that has triangles in the group.
void cIndexedMeshBuilder::RebuildRuns()
{
    std::vector<Run> runs;
    for (size_t g = 0; g < mGroups.size(); ++g) {
        Group &group = mGroups[g];
        std::vector<uint32_t> triangles;
        for (size_t name = 0; name < mNames.size(); ++name) {
            const size_t before = triangles.size();
            for (const Run &run : mRuns) {
                if (run.group != static_cast<int32_t>(g) || run.name != static_cast<int32_t>(name)) continue;
                triangles.insert(triangles.end(), group.triangles.begin() + run.start, group.triangles.begin() + run.end);
            }
            if (triangles.size() > before)
                runs.push_back(Run{static_cast<int32_t>(name), static_cast<int32_t>(g), static_cast<int32_t>(before),
                                   static_cast<int32_t>(triangles.size())});
        }
        group.triangles = triangles; // a copy: an emptied list keeps its buffer, as the game's does
    }
    mRuns = runs; // a copy, as above
}

namespace indexedmesh {

// 0x1a7570: Shepperd's method in single precision, the root taken in double (the CRT's for a negative operand). A
// positive trace gives w first; otherwise the largest diagonal element picks the axis. The result is divided by its
// length (the sum of squares in the order y, x, z, w).
float *__cdecl QuaternionFromMatrix(float *out, const float *m)
{
    float q[4];
    const float trace = (m[0] + m[4]) + m[8];
    if (trace > 0.0f) {
        const float s = static_cast<float>(std::sqrt(static_cast<double>(trace + 1.0f)));
        const float f = 0.5f / s;
        q[3] = s * 0.5f;
        q[0] = (m[7] - m[5]) * f;
        q[1] = (m[2] - m[6]) * f;
        q[2] = (m[3] - m[1]) * f;
    } else {
        int i = m[4] > m[0] ? 1 : 0;
        if (m[8] > m[4 * i]) i = 2;
        const int j = (i + 1) % 3;
        const int k = (j + 1) % 3;
        const float s = static_cast<float>(std::sqrt(static_cast<double>(m[4 * i] - (m[4 * k] + m[4 * j])) + 1.0));
        const float f = 0.5f / s;
        q[i] = s * 0.5f;
        q[3] = (m[j + 3 * k] - m[k + 3 * j]) * f;
        q[j] = (m[j + 3 * i] + m[i + 3 * j]) * f;
        q[k] = (m[3 * i + k] + m[i + 3 * k]) * f;
    }
    const float sum = ((q[1] * q[1] + q[0] * q[0]) + q[2] * q[2]) + q[3] * q[3];
    const float inverse = 1.0f / static_cast<float>(std::sqrt(static_cast<double>(sum)));
    for (int n = 0; n < 4; ++n) out[n] = inverse * q[n];
    return out;
}

} // namespace indexedmesh

// ---- Build ----

// 0xa80200: the collected mesh into the container: elements for each list, a linkage and a group for each collected
// group, a group for each run, the bones as transforms.
void cIndexedMeshBuilder::Build(cIGeometryDataContainer *container)
{
    bool hasA = false, hasB = false;
    for (const Group &group : mGroups) {
        if (group.flags & 0x400) hasA = true;
        if (group.flags & 0x800) hasB = true;
        if (hasA && hasB) break;
    }

    const uint32_t positionCount = static_cast<uint32_t>(mPositions.size());
    const uint32_t normalCount = static_cast<uint32_t>(mNormals.size());

    container->AddElement(kPositionIdentity, 0, 2, 0);
    container->SetElementData(0, positionCount, mPositions.empty() ? nullptr : mPositions.data(), positionCount * 12, nullptr, 0);
    container->AddElement(kNormalIdentity, 0, 2, 1);
    container->SetElementData(1, normalCount, mNormals.empty() ? nullptr : mNormals.data(), normalCount * 12, nullptr, 0);
    for (uint32_t i = 0; i < 4; ++i) {
        std::vector<Float2> &list = mTexCoords[i];
        const uint32_t n = static_cast<uint32_t>(list.size());
        container->AddElement(kTexCoordIdentity, i, 1, 2);
        container->SetElementData(2 + i, n, list.empty() ? nullptr : list.data(), n * 8, nullptr, 0);
    }
    for (uint32_t i = 0; i < 2; ++i) {
        std::vector<uint32_t> &list = mColours[i];
        const uint32_t n = static_cast<uint32_t>(list.size());
        container->AddElement(kColourIdentity, i, 4, 2);
        container->SetElementData(6 + i, n, list.empty() ? nullptr : list.data(), n * 4, nullptr, 0);
    }

    uint32_t element = 8;
    for (uint32_t b = 0; b < mInfluences.size(); ++b) {
        Influence &influence = mInfluences[b];
        container->AddElement(kInfluenceIdentity, b, 0, 0);
        const std::vector<uint16_t> vertices = Low16(influence.vertices.data(), influence.vertices.size());
        container->SetElementData(element, positionCount, influence.weights.empty() ? nullptr : influence.weights.data(),
                                  static_cast<uint32_t>(influence.weights.size() * 4) & ~3u,
                                  vertices.empty() ? nullptr : vertices.data(), static_cast<uint32_t>(vertices.size()));
        ++element;
    }
    for (uint32_t m = 0; m < mMorphs.size(); ++m) {
        Morph &morph = mMorphs[m];
        {
            container->AddElement(kMorphPositionIdentity, m, 2, 0);
            const std::vector<uint16_t> vertices = Low16(morph.positionVertices.data(), morph.positionVertices.size());
            container->SetElementData(element, positionCount,
                                      morph.positionDeltas.empty() ? nullptr : morph.positionDeltas.data(),
                                      static_cast<uint32_t>(morph.positionDeltas.size()) * 12,
                                      vertices.empty() ? nullptr : vertices.data(), static_cast<uint32_t>(vertices.size()));
        }
        {
            container->AddElement(kMorphNormalIdentity, m, 2, 1);
            const std::vector<uint16_t> vertices = Low16(morph.normalVertices.data(), morph.normalVertices.size());
            container->SetElementData(element + 1, normalCount,
                                      morph.normalDeltas.empty() ? nullptr : morph.normalDeltas.data(),
                                      static_cast<uint32_t>(morph.normalDeltas.size()) * 12,
                                      vertices.empty() ? nullptr : vertices.data(), static_cast<uint32_t>(vertices.size()));
        }
        container->AddNamePair(morph.first.value.mString.c_str(), morph.second.value.mString.c_str());
        element += 2;
    }
    if (hasA) {
        const uint32_t n = static_cast<uint32_t>(mWordsA.size());
        container->AddElement(kWordsAIdentity, 0, 4, 0);
        container->SetElementData(element, n, mWordsA.data(), n * 4, nullptr, 0);
        ++element;
    }
    if (hasB) {
        const uint32_t n = static_cast<uint32_t>(mWordsB.size());
        container->AddElement(kWordsBIdentity, 0, 4, 0);
        container->SetElementData(element, n, mWordsB.data(), n * 4, nullptr, 0);
    }

    const uint32_t influenceCount = static_cast<uint32_t>(mInfluences.size());
    const uint32_t morphCount = static_cast<uint32_t>(mMorphs.size());
    for (uint32_t g = 0; g < mGroups.size(); ++g) {
        Group &group = mGroups[g];
        container->AddLinkage();
        container->SetLinkageReferencedSize(g, group.count);
        container->AddLinkageElement(g, 0);
        if (group.flags & 1) container->AddLinkageElement(g, 1);
        for (uint32_t k = 1; k <= 6; ++k)
            if (group.flags & (1u << k)) container->AddLinkageElement(g, static_cast<uint16_t>(1 + k));
        const uint32_t base = 8 + influenceCount;
        if (group.flags & 0x80)
            for (uint32_t j = 0; j < influenceCount; ++j) container->AddLinkageElement(g, static_cast<uint16_t>(8 + j));
        if (group.flags & 0x100)
            for (uint32_t j = 0; j < morphCount; ++j) container->AddLinkageElement(g, static_cast<uint16_t>(base + 2 * j));
        if (group.flags & 0x200)
            for (uint32_t j = 0; j < morphCount; ++j)
                container->AddLinkageElement(g, static_cast<uint16_t>(base + 1 + 2 * j));
        uint32_t next = base + 2 * morphCount;
        if (group.flags & 0x400) {
            container->AddLinkageElement(g, static_cast<uint16_t>(next));
            ++next;
        } else if (hasA) {
            ++next;
        }
        if (group.flags & 0x800) container->AddLinkageElement(g, static_cast<uint16_t>(next));

        const std::vector<uint32_t> *const lists[3] = {&group.positions, &group.normals, &group.source};
        for (uint32_t which = 0; which < 3; ++which) {
            if (lists[which]->empty()) continue;
            const std::vector<uint16_t> aliases = Low16(lists[which]->data(), lists[which]->size());
            container->SetLinkageAliases(g, which, aliases.data(), static_cast<uint32_t>(aliases.size()));
        }
    }

    const uint32_t runCount = static_cast<uint32_t>(mRuns.size());
    container->ReserveGroups(container->CountGroups() + runCount);
    for (uint32_t k = 0; k < runCount; ++k) {
        const Run &run = mRuns[k];
        const std::vector<uint32_t> &triangles = mGroups[run.group].triangles;
        const std::vector<uint16_t> indices =
            Low16(triangles.data() + run.start, static_cast<size_t>(run.end - run.start));
        container->AddGroup(kTriangleList, mNames[run.name].c_str(), static_cast<uint32_t>(run.group), -1);
        container->SetGroupVertexIndices(k, indices.data(), static_cast<uint32_t>(indices.size()));
    }

    for (const Bone &bone : mBones) {
        nGZSceneGraph::cGeometryTransform transform;
        std::memcpy(&transform, &bone, sizeof(transform));
        container->AddTransform(&transform);
    }
}

// ---- persistence ----

void cIndexedMeshBuilder::ReadBones(ifaces::cIGZPersistReader *reader, uint32_t version)
{
    const uint32_t count = reader->GetUint32();
    mInfluences.resize(count);
    mBones.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        Influence &influence = mInfluences[i];
        const uint32_t n = reader->GetUint32();
        influence.vertices.resize(n);
        influence.weights.resize(n);
        if (n) {
            reader->GetUint32Array(influence.vertices.data(), n);
            reader->GetFloat32Array(influence.weights.data(), n);
        }
    }
    for (uint32_t i = 0; i < count; ++i) {
        Bone &bone = mBones[i];
        if (version < 9) {
            reader->GetUint32();
            float matrix[9];
            reader->GetFloat32Array(matrix, 9);
            float rotation[4];
            indexedmesh::QuaternionFromMatrix(rotation, matrix);
            bone.position[0] = reader->GetFloat32();
            bone.position[1] = reader->GetFloat32();
            bone.position[2] = reader->GetFloat32();
            std::memcpy(bone.rotation, rotation, sizeof(rotation));
        } else {
            reader->GetFloat32Array(bone.rotation, 4);
            reader->GetFloat32Array(bone.position, 3);
        }
    }
}

void cIndexedMeshBuilder::ReadMorphs(ifaces::cIGZPersistReader *reader)
{
    const uint32_t count = reader->GetUint32();
    mMorphs.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        Morph &m = mMorphs[i];
        reader->GetGZStr(&m.first.value);
        reader->GetGZStr(&m.second.value);
        uint32_t n = reader->GetUint32();
        m.positionVertices.resize(n);
        m.positionDeltas.resize(n);
        if (n) {
            reader->GetUint32Array(m.positionVertices.data(), n);
            reader->GetFloat32Array(m.positionDeltas.data()->v, n * 3);
        }
        n = reader->GetUint32();
        m.normalVertices.resize(n);
        m.normalDeltas.resize(n);
        if (n) {
            reader->GetUint32Array(m.normalVertices.data(), n);
            reader->GetFloat32Array(m.normalDeltas.data()->v, n * 3);
        }
    }
}

void cIndexedMeshBuilder::ReadGroups(ifaces::cIGZPersistReader *reader)
{
    uint32_t n = reader->GetUint32();
    mWordsA.resize(n);
    if (n) reader->GetSint32Array(reinterpret_cast<int32_t *>(mWordsA.data()), n);
    n = reader->GetUint32();
    mWordsB.resize(n);
    if (n) reader->GetSint32Array(reinterpret_cast<int32_t *>(mWordsB.data()), n);

    const uint32_t groups = reader->GetUint32();
    mGroups.resize(groups);
    for (uint32_t i = 0; i < groups; ++i) {
        Group &group = mGroups[i];
        group.flags = reader->GetUint32() & 0xfff;
        n = reader->GetUint32();
        group.count = n;
        group.positions.resize(n);
        if (n) reader->GetUint32Array(group.positions.data(), n);
        std::vector<uint32_t> *const rest[3] = {&group.normals, &group.source, &group.triangles};
        for (std::vector<uint32_t> *list : rest) {
            n = reader->GetUint32();
            list->resize(n);
            if (n) reader->GetUint32Array(list->data(), n);
        }
    }

    n = reader->GetUint32();
    mRuns.resize(n);
    if (n) reader->GetVoid(mRuns.data(), n * sizeof(Run));

    n = reader->GetUint32();
    mNames.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        nTSSG::SGLocalString name;
        reader->GetGZStr(&name.value);
        mNames[i] = name.value.ToChar();
    }
}

// 0xa80c90 (ret 4): its record (any version, which steers the bone format), cGeometryBuilder's Read, the lists in the
// order Write puts them out; before version 10 the helper 0xa820a0 (still the game's) runs on the result.
void cIndexedMeshBuilder::Read(ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cGeometryBuilder::Read(reader);

    uint32_t n = reader->GetUint32();
    mPositions.resize(n);
    if (n) reader->GetFloat32Array(mPositions.data()->v, n * 3);
    n = reader->GetUint32();
    mNormals.resize(n);
    if (n) reader->GetFloat32Array(mNormals.data()->v, n * 3);
    for (auto &list : mTexCoords) {
        n = reader->GetUint32();
        list.resize(n);
        if (n) reader->GetFloat32Array(list.data()->v, n * 2);
    }
    for (auto &list : mColours) {
        n = reader->GetUint32();
        list.resize(n);
        if (n) reader->GetUint32Array(list.data(), n);
    }
    mWord74 = reader->GetUint32();
    ReadBones(reader, info.mVersion);
    ReadMorphs(reader);
    ReadGroups(reader);
    if (info.mVersion < 10) ClassifyGroups();
    if (info.mName) info.mName->Release();
}

void cIndexedMeshBuilder::WriteBones(ifaces::cIGZPersistWriter *writer)
{
    writer->SetUint32(static_cast<uint32_t>(mBones.size()));
    if (mBones.empty()) return;
    if (!mInfluences.empty()) {
        for (Influence &influence : mInfluences) {
            const uint32_t n = static_cast<uint32_t>(influence.vertices.size());
            writer->SetUint32(n);
            WriterArray(writer, kWriteUint32Array, influence.vertices.data(), n);
            writer->SetFloat32Array(influence.weights.data(), n);
        }
    }
    for (Bone &bone : mBones) {
        float rotation[4];
        std::memcpy(rotation, bone.rotation, sizeof(rotation));
        writer->SetFloat32Array(rotation, 4);
        writer->SetFloat32Array(bone.position, 3);
    }
}

void cIndexedMeshBuilder::WriteMorphs(ifaces::cIGZPersistWriter *writer)
{
    writer->SetUint32(static_cast<uint32_t>(mMorphs.size()));
    for (Morph &m : mMorphs) {
        writer->WriteGZStr(&m.first.value);
        writer->WriteGZStr(&m.second.value);
        uint32_t n = static_cast<uint32_t>(m.positionDeltas.size());
        writer->SetUint32(n);
        WriterArray(writer, kWriteUint32Array, m.positionVertices.data(), n);
        writer->SetFloat32Array(reinterpret_cast<float *>(m.positionDeltas.data()), n * 3);
        n = static_cast<uint32_t>(m.normalDeltas.size());
        writer->SetUint32(n);
        WriterArray(writer, kWriteUint32Array, m.normalVertices.data(), n);
        writer->SetFloat32Array(reinterpret_cast<float *>(m.normalDeltas.data()), n * 3);
    }
}

void cIndexedMeshBuilder::WriteGroups(ifaces::cIGZPersistWriter *writer)
{
    writer->SetUint32(static_cast<uint32_t>(mWordsA.size()));
    if (!mWordsA.empty()) WriterArray(writer, kWriteSint32Array, mWordsA.data(), static_cast<uint32_t>(mWordsA.size()));
    writer->SetUint32(static_cast<uint32_t>(mWordsB.size()));
    if (!mWordsB.empty()) WriterArray(writer, kWriteSint32Array, mWordsB.data(), static_cast<uint32_t>(mWordsB.size()));

    writer->SetUint32(static_cast<uint32_t>(mGroups.size()));
    for (Group &group : mGroups) {
        writer->SetUint32(group.flags);
        writer->SetUint32(group.count);
        if (!group.positions.empty())
            WriterArray(writer, kWriteUint32Array, group.positions.data(), static_cast<uint32_t>(group.positions.size()));
        std::vector<uint32_t> *const rest[3] = {&group.normals, &group.source, &group.triangles};
        for (std::vector<uint32_t> *list : rest) {
            writer->SetUint32(static_cast<uint32_t>(list->size()));
            if (!list->empty()) WriterArray(writer, kWriteUint32Array, list->data(), static_cast<uint32_t>(list->size()));
        }
    }

    writer->SetUint32(static_cast<uint32_t>(mRuns.size()));
    if (!mRuns.empty()) writer->SetVoid(mRuns.data(), static_cast<uint32_t>(mRuns.size() * sizeof(Run)));

    writer->SetUint32(static_cast<uint32_t>(mNames.size()));
    for (std::string &name : mNames) writer->WriteRZCharStr(const_cast<char *>(name.c_str()));
}

// 0xa826c0 (ret 4): its record, its name released, cGeometryBuilder's Write, then the lists.
void cIndexedMeshBuilder::Write(ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info = cIndexedMeshBuilder::GetTypeInfo();
    writer->WriteTypeInfo(&info);
    if (info.mName) info.mName->Release();
    cGeometryBuilder::Write(writer);

    writer->SetUint32(static_cast<uint32_t>(mPositions.size()));
    if (!mPositions.empty())
        writer->SetFloat32Array(mPositions.data()->v, static_cast<uint32_t>(mPositions.size()) * 3);
    writer->SetUint32(static_cast<uint32_t>(mNormals.size()));
    if (!mNormals.empty()) writer->SetFloat32Array(mNormals.data()->v, static_cast<uint32_t>(mNormals.size()) * 3);
    for (auto &list : mTexCoords) {
        writer->SetUint32(static_cast<uint32_t>(list.size()));
        if (!list.empty()) writer->SetFloat32Array(list.data()->v, static_cast<uint32_t>(list.size()) * 2);
    }
    for (auto &list : mColours) {
        writer->SetUint32(static_cast<uint32_t>(list.size()));
        if (!list.empty()) WriterArray(writer, kWriteUint32Array, list.data(), static_cast<uint32_t>(list.size()));
    }
    writer->SetUint32(mWord74);
    WriteBones(writer);
    WriteMorphs(writer);
    WriteGroups(writer);
}

} // namespace nGZSceneGraph
