// nGZSceneGraph::cTangentSpaceBuilder (lane QG). Layout and the hook decision: src/sg/cTangentSpaceBuilder.h.
#include "sg/cTangentSpaceBuilder.h"

#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "sg/SGLocalString.h"
#include "sg/cGeometryDataContainerClass.h"
#include "sg/cObjectGraphNodeImpl.h" // NewTypeName

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

namespace nGZSceneGraph {

static_assert(sizeof(cTangentSpaceBuilder) == 0x30, "cTangentSpaceBuilder size (the factory allocates 0x30)");
static_assert(offsetof(cTangentSpaceBuilder, mNames) == 0x14, "cTangentSpaceBuilder names");

namespace {

constexpr uint32_t kPositionIdentity = 0x5b830781; // the vertex positions element
constexpr uint32_t kNormalIdentity = 0x3b83078b;   // the vertex normals element
constexpr uint32_t kTexCoordIdentity = 0xbb8307ab; // the texture coordinates element
constexpr uint32_t kTangentIdentity = 0x89d92ba0;   // the tangents element Build makes
constexpr uint32_t kTriangleList = 2;               // the group primitive type Build takes
constexpr uint32_t kAnyLink = 0x7fffffff;

// The thresholds (as floats; the game's constants are these bit patterns).
const float kEpsLength = std::bit_cast<float>(0x1e8637beu);   // about 1.42e-20: "no length" for a vector's squared length
const float kEpsTriangle = std::bit_cast<float>(0x2f03126fu); // about 1.19e-10: a triangle's texture-space area

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

// 0xaed570 / 0xaedcd0: the groups (in index order) of the primitive type `primType` whose name is `name` (case blind),
// as indices. Holds a reference on the container for its life.
struct GroupFilter {
    cIGeometryDataContainer *container;
    std::vector<uint32_t> groups;

    GroupFilter(cIGeometryDataContainer *c, const char *name) : container(c)
    {
        container->AddRef();
        for (uint32_t i = 0; i < container->CountGroups(); ++i) {
            cGeometryGroupInfo info;
            container->GetGroupInfo(&info, i);
            const bool nameMatches = name == nullptr || _stricmp(info.mName, name) == 0;
            if (nameMatches && info.mPrimType == kTriangleList) groups.push_back(i);
        }
    }
    GroupFilter(const GroupFilter &) = delete;
    GroupFilter &operator=(const GroupFilter &) = delete;
    ~GroupFilter() { container->Release(); }
};

// One vertex's accumulated frame: the nine floats of a 3x3 matrix, rows x, y, z, columns tangent, bitangent, normal.
struct Frame {
    float f[9];
};

// 0xa88560: the frame of a triangle (corners p0..p2 with texture coordinates t0..t2): tangent and bitangent from the
// positions' and the texture coordinates' edges, the normal their cross product (so its length is twice the area).
// A triangle with no texture-space area has no tangent or bitangent.
void TriangleFrame(Frame &out, const float *p0, const float *p1, const float *p2, const float *t0, const float *t1,
                   const float *t2)
{
    const float e1x = p1[0] - p0[0];
    const float e1y = p1[1] - p0[1];
    const float e1z = p1[2] - p0[2];
    const float e2x = p2[0] - p0[0];
    const float e2y = p2[1] - p0[1];
    const float e2z = p2[2] - p0[2];
    const float du1 = t1[0] - t0[0];
    const float dv1 = t1[1] - t0[1];
    const float dv2 = t2[1] - t0[1];
    const float du2 = t2[0] - t0[0];
    const float negDu2 = -du2;
    const float det = dv2 * du1 - du2 * dv1;
    float tx = 0.0f, ty = 0.0f, tz = 0.0f, bx = 0.0f, by = 0.0f, bz = 0.0f;
    if (!(std::fabs(det) <= kEpsTriangle)) {
        tz = (dv2 * e1z - dv1 * e2z) / det;
        bx = (e2x * du1 + e1x * negDu2) / det;
        by = (e1y * negDu2 + e2y * du1) / det;
        tx = (e1x * dv2 - e2x * dv1) / det;
        ty = (dv2 * e1y - dv1 * e2y) / det;
        bz = (negDu2 * e1z + e2z * du1) / det;
    }
    out.f[0] = tx;
    out.f[1] = bx;
    out.f[3] = ty;
    out.f[4] = by;
    out.f[6] = tz;
    out.f[7] = bz;
    out.f[2] = e1y * e2z - e2y * e1z;
    out.f[5] = e2x * e1z - e1x * e2z;
    out.f[8] = e1x * e2y - e2x * e1y;
}

// The (float) length of a vector given its squared length: sqrt in double, as the game's code does.
float RootOf(float squared) { return static_cast<float>(std::sqrt(static_cast<double>(squared))); }

// The tangent of a vertex's accumulated frame (stage two of Build): its normal (a default of +z when it is too short)
// kept; the tangent and bitangent made orthogonal to it (Gram-Schmidt) and to each other when both have a length; a
// frame still without two usable vectors replaced by a tangent perpendicular to the normal (in the plane of its two
// longest axes) and the bitangent their cross product.
void TangentOf(const Frame &frame, float out[3])
{
    float nx = frame.f[2], ny = frame.f[5], nz = frame.f[8];
    if (kEpsLength >= std::fabs((nx * nx + ny * ny) + nz * nz)) {
        nx = 0.0f;
        ny = 0.0f;
        nz = 1.0f;
    }
    const float tx = frame.f[0], ty = frame.f[3], tz = frame.f[6];
    const float bx = frame.f[1], by = frame.f[4], bz = frame.f[7];
    float Tx = tx, Ty = ty, Tz = tz;
    float Bx = bx, By = by, Bz = bz;

    if (!(kEpsLength >= std::fabs((tx * tx + ty * ty) + tz * tz)) &&
        !(kEpsLength >= std::fabs((bx * bx + by * by) + bz * bz))) {
        // ORIGINAL-BUG: the projections are scaled by the normal's length where dividing by its square would make
        // them exact; the game's normals are unit, where the two agree.
        const float length = RootOf((nx * nx + ny * ny) + nz * nz);
        const float s = ((nx * tx + ny * ty) + tz * nz) * length;
        const float px = tx - nx * s;
        const float py = ty - ny * s;
        const float pz = tz - nz * s;
        const float scaleT = 1.0f / RootOf((px * px + py * py) + pz * pz);
        Tx = px * scaleT;
        Ty = py * scaleT;
        Tz = pz * scaleT;
        const float s2 = ((by * ny + bx * nx) + bz * nz) * length;
        const float qx = bx - nx * s2;
        const float qy = by - ny * s2;
        const float qz = bz - nz * s2;
        const float lengthT = RootOf((Tx * Tx + Ty * Ty) + Tz * Tz);
        const float d = (qx * Tx + Ty * qy) + qz * Tz;
        const float s3 = lengthT * d;
        const float rx = qx - Tx * s3;
        const float ry = qy - Ty * s3;
        const float rz = qz - Tz * s3;
        const float scaleB = 1.0f / RootOf((ry * ry + rx * rx) + rz * rz);
        Bx = rx * scaleB;
        By = ry * scaleB;
        Bz = rz * scaleB;
    }

    if (kEpsLength >= std::fabs((Ty * Ty + Tx * Tx) + Tz * Tz) || kEpsLength >= std::fabs((Bx * Bx + By * By) + Bz * Bz)) {
        const float m = std::fabs(ny) > std::fabs(nx) ? ny : nx;
        float wx, wy, wz;
        if (std::fabs(nz) > std::fabs(m)) {
            wx = nz;
            wy = 0.0f;
            wz = -nx;
        } else {
            wx = ny;
            wy = -nx;
            wz = 0.0f;
        }
        const float scale = 1.0f / RootOf((wx * wx + wy * wy) + wz * wz);
        Tx = scale * wx;
        Ty = wy * scale;
        Tz = wz * scale;
    }
    out[0] = Tx;
    out[1] = Ty;
    out[2] = Tz;
}

} // namespace

// 0xa86bf0: new and the constructor (most-derived), answered as the cIGeometryBuilder (reached through the
// cITangentSpaceBuilder's own virtual base, the same subobject).
cIGZUnknown *__cdecl cTangentSpaceBuilder::Create()
{
    cTangentSpaceBuilder *const builder = new cTangentSpaceBuilder;
    cIGeometryBuilder *const face = static_cast<cITangentSpaceBuilder *>(builder);
    return face;
}

// 0xa86920: cGeometryBuilder's part (0xa44b50) and an empty name set.
cTangentSpaceBuilder::cTangentSpaceBuilder() = default;

// 0xa86a80: the name set, then cGeometryBuilder's part.
cTangentSpaceBuilder::~cTangentSpaceBuilder() = default;

// 0xa88230 (ret 8): as cCompactorBuilder's (0xa83a00), for kIid.
bool cTangentSpaceBuilder::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid != cITangentSpaceBuilder::kIid) return cGeometryBuilder::QueryInterface(iid, out);
    *out = static_cast<cITangentSpaceBuilder *>(this);
    static_cast<cIGeometryBuilder *>(this)->AddRef();
    return true;
}

// 0xa88280 (ret 4): its own record (any version), cGeometryBuilder's Read (0x9dc310), the name count (GetUint32, taken
// as signed) and each name (GetGZStr into a cRZString local) added to the set. Its own record's name released last.
void cTangentSpaceBuilder::Read(ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cGeometryBuilder::Read(reader);
    const int32_t count = static_cast<int32_t>(reader->GetUint32());
    for (int32_t i = 0; i < count; ++i) {
        nTSSG::SGLocalString name;
        reader->GetGZStr(&name.value);
        mNames.insert(std::string(name.value.ToChar()));
    }
    if (info.mName) info.mName->Release();
}

// 0xa888c0 (hidden out): {"cTangentSpaceBuilder" (a new counted cRZString), the class id, version 2}.
cPersistTypeInfo cTangentSpaceBuilder::GetTypeInfo()
{
    cPersistTypeInfo info;
    info.mName = NewTypeName("cTangentSpaceBuilder");
    info.mWord4 = kClassId;
    info.mVersion = kVersion;
    return info;
}

// 0xa88990 (ret 4): its record (0xa888c0 directly), its name released, cGeometryBuilder's Write (0xa45790), then the
// name count (SetUint32) and each name in the set's order (WriteRZCharStr).
void cTangentSpaceBuilder::Write(ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info = cTangentSpaceBuilder::GetTypeInfo();
    writer->WriteTypeInfo(&info);
    if (info.mName) info.mName->Release();
    cGeometryBuilder::Write(writer);
    writer->SetUint32(static_cast<uint32_t>(mNames.size()));
    for (const std::string &name : mNames) writer->WriteRZCharStr(const_cast<char *>(name.c_str()));
}

// 0xa86bd3.
const char *cTangentSpaceBuilder::GetName() { return "TangentSpaceBuilder"; }

// 0xa86b20.
void cTangentSpaceBuilder::AddName(const char *name) { mNames.insert(std::string(name)); }

// 0xa86c80. Stage one: for each name in the set (in its order), each triangle-list group of that name whose linkage has
// positions (repetition 0, block format 2), normals (the same) and texture coordinates (repetition 0, block format 1),
// with at least one normal: the linkage gets (once, across all names) a frame per vertex, zeroed, and a new tangent
// element; each triangle of the group adds its frame, weighted by the half length of its normal (the area), to its three
// corners, then the corners' normals are overwritten with the normal element's. Stage two: for each linkage so
// treated, in the order met, every vertex's tangent (TangentOf) goes into the linkage's new element.
void cTangentSpaceBuilder::Build(cIGeometryDataContainer *container)
{
    std::vector<std::vector<Frame>> frames;
    std::vector<int32_t> slotOfLink;
    uint32_t element = container->CountElements();

    for (const std::string &name : mNames) {
        GroupFilter filter(container, name.c_str());
        for (uint32_t k = 0; k < filter.groups.size(); ++k) {
            cGeometryGroupInfo group;
            container->GetGroupInfo(&group, filter.groups[k]);
            const uint32_t link = group.mLinkIndex;
            const cGeometryElementHeader position = FindLinkedElement(container, link, kPositionIdentity, 0, 2);
            const cGeometryElementHeader normal = FindLinkedElement(container, link, kNormalIdentity, 0, 2);
            const cGeometryElementHeader texture = FindLinkedElement(container, link, kTexCoordIdentity, 0, 1);
            if (position.mIndex == static_cast<uint32_t>(-1) || normal.mIndex == static_cast<uint32_t>(-1) ||
                texture.mIndex == static_cast<uint32_t>(-1))
                continue;
            cGeometryElementHeader normals;
            container->GetElementHeader(&normals, normal.mIndex);
            if (normals.mNumber == 0) continue;

            if (slotOfLink.size() < link + 1) slotOfLink.resize(link + 1, -1);
            if (slotOfLink[link] == -1) {
                slotOfLink[link] = static_cast<int32_t>(frames.size());
                cGeometryLinkageHeader linkage;
                container->GetLinkageHeader(&linkage, link);
                frames.emplace_back();
                frames.back().assign(linkage.mReferencedSize, Frame{});
                container->AddElement(kTangentIdentity, 0, 2, 3);
                container->AddLinkageElement(link, static_cast<uint16_t>(container->CountElements() - 1));
            }
            std::vector<Frame> &mine = frames[static_cast<size_t>(slotOfLink[link])];

            Range<cGeometryVector<3>> positions;
            Range<cGeometryVector<3>> normalVectors;
            Range<cGeometryVector<2>> coordinates;
            Range<uint16_t> positionAliases;
            Range<uint16_t> normalAliases;
            Range<uint16_t> coordinateAliases;
            Range<uint16_t> corners;
            container->GetElementVectors3(&positions.r, position.mIndex);
            container->GetElementVectors3(&normalVectors.r, normal.mIndex);
            container->GetElementVectors2(&coordinates.r, texture.mIndex);
            container->GetLinkageAliases(&positionAliases.r, link, position.mSetFormat);
            container->GetLinkageAliases(&normalAliases.r, link, normal.mSetFormat);
            container->GetLinkageAliases(&coordinateAliases.r, link, texture.mSetFormat);
            container->GetGroupVertexIndices(&corners.r, group.mIndex);

            while (!corners.r.mCurrent->IsEqual(corners.r.mEnd)) {
                const uint32_t i0 = corners.At(0) & 0xffff;
                const uint32_t i1 = corners.At(1) & 0xffff;
                const uint32_t i2 = corners.At(2) & 0xffff;
                const cGeometryVector<2> c2 = coordinates.At(coordinateAliases.At(i2));
                const cGeometryVector<2> c1 = coordinates.At(coordinateAliases.At(i1));
                const cGeometryVector<2> c0 = coordinates.At(coordinateAliases.At(i0));
                const cGeometryVector<3> p2 = positions.At(positionAliases.At(i2));
                const cGeometryVector<3> p1 = positions.At(positionAliases.At(i1));
                const cGeometryVector<3> p0 = positions.At(positionAliases.At(i0));
                Frame frame;
                TriangleFrame(frame, p0.v, p1.v, p2.v, c0.v, c1.v, c2.v);
                const float area = RootOf((frame.f[5] * frame.f[5] + frame.f[2] * frame.f[2]) + frame.f[8] * frame.f[8]) * 0.5f;
                for (float &value : frame.f) value *= area;
                for (const uint32_t i : {i0, i1, i2})
                    for (int j = 0; j < 9; ++j) mine[i].f[j] = frame.f[j] + mine[i].f[j];
                for (const uint32_t i : {i0, i1, i2}) {
                    const cGeometryVector<3> n = normalVectors.At(normalAliases.At(i));
                    mine[i].f[2] = n.v[0];
                    mine[i].f[5] = n.v[1];
                    mine[i].f[8] = n.v[2];
                }
                corners.r.mCurrent->Advance(3);
            }
        }
    }

    for (const std::vector<Frame> &list : frames) {
        std::vector<float> tangents(list.size() * 3);
        for (size_t v = 0; v < list.size(); ++v) TangentOf(list[v], &tangents[v * 3]);
        container->SetElementData(element, static_cast<uint32_t>(list.size()), tangents.data(),
                                  static_cast<uint32_t>(list.size() * 12), nullptr, 0);
        ++element;
    }
}

} // namespace nGZSceneGraph
