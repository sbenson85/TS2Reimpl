// nGZSceneGraph's remaining "layer" quad makers and the pool water's rect mesh (lane 261111e): 0xaf1420 (the text
// billboard's), 0xaf1ad0 (the deck foundation thumbnail's), 0xaf1eb0 (the family thumbnail's and the screen effect's) and
// 0xaf09e0 (lane 261111n). The same shape as MakeQuadMesh (SGUtilitiesMesh.cpp): a geometry node and an indexed mesh builder through the
// COM director, the arithmetic SSE in the original's operand order.
#include "sg/SGUtilities.h"

#include "compgeom/ConvexMath.h"
#include "game/game.h"
#include "gz/cIGZCOM.h"
#include "rz/StaticGetters.h"
#include "rz/cRZString.h"
#include "sg/SGNodeInterfaces.h"
#include "sg/cBoundingVolumeBuilder.h"
#include "sg/cCompactorBuilder.h"
#include "sg/cGeometryBuilder.h"
#include "sg/cGeometryNode.h"
#include "sg/cIndexedMeshBuilder.h"

#include <cmath>

namespace nGZSceneGraph {

namespace {

using nGZCompGeom::convex::AddS;
using nGZCompGeom::convex::DivS;
using nGZCompGeom::convex::MulS;
using nGZCompGeom::convex::Negated;
using nGZCompGeom::convex::SubS;

constexpr uint32_t kGeometryNodeClass = 0x7ba3838c;
constexpr uint32_t kIndexedMeshBuilderClass = 0x9bffc10d;
constexpr uint32_t kOldPartClass = 0x5ce7e026; // the part cGeometryNode::Read gives a version-3 record (iid below)
constexpr uint32_t kOldPartIid = 0x7ce7e022;

template <class T> void Create(uint32_t classId, T **out)
{
    static_cast<cIGZCOM *>(GetDirectorFrameworkObject())->GetClassObject(classId, T::kIid, reinterpret_cast<void **>(out));
}

// half a size as the original takes it: the float widened, times 0.5 as a double, rounded back.
float Half(float v) { return float(double(v) * 0.5); }

} // namespace

cIGeometryNode **MakeBillboardQuadMesh(cIGeometryNode **out, float halfSize, float y, float scale, float uScale,
                                       float vOffset)
{
    *out = nullptr;
    Create(kGeometryNodeClass, out);
    cIIndexedMeshBuilder *mesh = nullptr;
    Create(kIndexedMeshBuilderClass, &mesh);
    // ORIGINAL-BUG: neither the node nor the mesh is checked.
    (*out)->AddPart(mesh); // receiver: the node (slot 32), the mesh by its cIGeometryBuilder face
    mesh->Reset(false);

    const float low = Negated(halfSize);
    const float corners[4][3] = {
        {low, y, low}, {halfSize, y, low}, {halfSize, y, halfSize}, {low, y, halfSize},
    };
    for (const float *corner : corners) mesh->AddPosition(corner);

    const float u0 = MulS(scale, uScale);
    const float v0 = MulS(AddS(vOffset, 1.0f), scale);
    const float u1 = MulS(AddS(uScale, 1.0f), scale);
    const float v1 = MulS(scale, vOffset);
    const float coords[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
    for (const float *coord : coords) mesh->AddTexCoord(coord, 0);

    mesh->SetGroupName("layer");
    mesh->MarkUnknown();
    for (uint32_t k = 0; k < 4; ++k) mesh->AddIndex(5, k);
    mesh->CommitGroup();
    mesh->Finish();
    if (mesh) mesh->Release();
    return out;
}

cIGeometryNode **MakeQuadMeshXZ(cIGeometryNode **out, const float *center, const float *size, const float *uvLow,
                                const float *uvHigh)
{
    *out = nullptr;
    Create(kGeometryNodeClass, out);
    cIIndexedMeshBuilder *mesh = nullptr;
    Create(kIndexedMeshBuilderClass, &mesh);
    // ORIGINAL-BUG: neither the node nor the mesh is checked.
    (*out)->AddPart(mesh); // receiver: the node (slot 32), the mesh by its cIGeometryBuilder face
    mesh->Reset(false);

    const float x = center[0], z = center[2];
    const float y = AddS(center[1], 0.0f);
    const float corners[4][3] = {
        {SubS(x, Half(size[0])), y, SubS(z, Half(size[1]))},
        {AddS(Half(size[0]), x), y, SubS(z, Half(size[1]))},
        {AddS(Half(size[0]), x), y, AddS(Half(size[1]), z)},
        {SubS(x, Half(size[0])), y, AddS(Half(size[1]), z)},
    };
    for (const float *corner : corners) mesh->AddPosition(corner);
    const float coords[4][2] = {
        {uvLow[0], uvHigh[1]}, {uvHigh[0], uvHigh[1]}, {uvHigh[0], uvLow[1]}, {uvLow[0], uvLow[1]},
    };
    for (const float *coord : coords) mesh->AddTexCoord(coord, 0);
    const float down[3] = {0.0f, -1.0f, 0.0f};
    mesh->AddNormal(down);
    mesh->SetGroupName("layer");
    mesh->MarkUnknown();
    for (uint32_t k = 0; k < 4; ++k) mesh->AddIndex(5, k);
    mesh->AddNormalIndex(0);
    mesh->CommitGroup();
    mesh->Finish();
    if (mesh) mesh->Release();
    return out;
}

cIGeometryNode **MakePlainQuadMesh(cIGeometryNode **out, const float *center, const float *size, bool normal)
{
    *out = nullptr;
    Create(kGeometryNodeClass, out);
    cIIndexedMeshBuilder *mesh = nullptr;
    Create(kIndexedMeshBuilderClass, &mesh);
    // ORIGINAL-BUG: neither the node nor the mesh is checked.
    (*out)->AddPart(mesh); // receiver: the node (slot 32), the mesh by its cIGeometryBuilder face
    mesh->Reset(false);
    if (normal) {
        static const float kUp[3] = {0.0f, 0.0f, 1.0f}; // the original passes its global 0x01073e98 (never written)
        mesh->AddNormal(kUp);
    }

    const float x = center[0], y = center[1];
    const float z = AddS(center[2], 0.0f);
    const float corners[4][3] = {
        {SubS(x, Half(size[0])), SubS(y, Half(size[1])), z},
        {AddS(Half(size[0]), x), SubS(y, Half(size[1])), z},
        {AddS(Half(size[0]), x), AddS(Half(size[1]), y), z},
        {SubS(x, Half(size[0])), AddS(Half(size[1]), y), z},
    };
    for (const float *corner : corners) mesh->AddPosition(corner);
    mesh->SetGroupName("layer");
    mesh->MarkUnknown();
    for (uint32_t k = 0; k < 4; ++k) mesh->AddPositionIndex(k);
    if (normal) mesh->AddNormalIndex(0);
    mesh->CommitGroup();
    mesh->Finish();
    if (mesh) mesh->Release();
    return out;
}

namespace {

// The morph target count's lattice side: the square root of `morphs` (a double, rounded to float) rounded up, the
// original's way: (root - 0.5) + 1.5 * 2^23 read as an integer (round to nearest), one more where that plus one is the
// root exactly, one more again where the root is still above it.
int32_t LatticeSide(int32_t morphs)
{
    const float root = float(std::sqrt(double(morphs)));
    const float biased = AddS(SubS(root, 0.5f), 12582912.0f);
    int32_t below = int32_t(nGZCompGeom::convex::BitsOf(biased) - 0x4b400000u);
    const int32_t next = int32_t(uint32_t(below) + 1u);
    if (float(next) == root) below = next;
    return root > float(below) ? int32_t(uint32_t(below) + 1u) : below;
}

} // namespace

cIGeometryNode **MakePoolWaterRectMesh(cIGeometryNode **out, int32_t columns, int32_t rows, int32_t morphs,
                                       uint8_t morphNormals, uint8_t normalised, uint8_t centred, int32_t mode)
{
    *out = nullptr;
    Create(kGeometryNodeClass, out);
    cIIndexedMeshBuilder *mesh = nullptr;
    Create(kIndexedMeshBuilderClass, &mesh);
    cICompactorBuilder *compactor = nullptr;
    Create(cCompactorBuilder::kClassId, &compactor);
    cIBoundingVolumeBuilder *bounds = nullptr;
    Create(cBoundingVolumeBuilder::kClassId, &bounds);
    cGeometryBuilderV *oldPart = nullptr; // held by its vbptr alone (+0)
    static_cast<cIGZCOM *>(GetDirectorFrameworkObject())
        ->GetClassObject(kOldPartClass, kOldPartIid, reinterpret_cast<void **>(&oldPart));
    // ORIGINAL-BUG: neither the node nor the mesh is checked.
    // receiver: the node (slot 32 AddPart, each part by its cIGeometryBuilder face, null when it failed)
    (*out)->AddPart(mesh);
    (*out)->AddPart(compactor);
    (*out)->AddPart(bounds);
    (*out)->AddPart(oldPart);

    const int32_t lastColumn = int32_t(uint32_t(columns) - 1u), lastRow = int32_t(uint32_t(rows) - 1u);
    const int32_t columnDivisor = normalised ? lastColumn : 1;
    const float rowDivisor = float(normalised ? lastRow : 1);
    float offsetX = centred ? 0.5f : 0.0f;
    float offsetY = centred ? float(0.5) : 0.0f; // the original converts the double 0.5 (0xda1870)
    if (!normalised) {
        offsetX = MulS(offsetX, float(lastColumn));
        offsetY = MulS(float(lastRow), offsetY);
    }
    const int32_t side = LatticeSide(morphs);

    mesh->Reset(false);
    mesh->SetMorphTargetCount(uint32_t(morphs));

    int32_t vertex = 0;
    for (int32_t row = 0; row < rows; ++row) {
        const float rowF = float(row);
        const float v = DivS(rowF, rowDivisor);
        if (columns <= 0) continue;
        const float posY = SubS(v, offsetY);
        const float lastColumnF = float(lastColumn);
        const float texV = SubS(1.0f, DivS(rowF, float(lastRow)));
        for (int32_t column = 0; column < columns; ++column, ++vertex) {
            const float columnF = float(column);
            const float u = DivS(columnF, float(columnDivisor));
            const float position[3] = {SubS(u, offsetX), posY, 0.0f};
            const float texCoord[2] = {DivS(columnF, lastColumnF), texV};
            mesh->AddPosition(position);
            static const float kUp[3] = {0.0f, 0.0f, 1.0f}; // the original's global 0x01073e98 (never written)
            mesh->AddNormal(kUp);
            mesh->AddTexCoord(texCoord, 0);
            if (morphs <= 0) continue;
            const float scale = float(std::sqrt(double(MulS(float(side), 30.0f))));
            const float scale2 = AddS(scale, scale);
            for (int32_t morph = 0; morph < morphs; ++morph) {
                const int32_t across = morph / side, along = morph - across * side;
                const float a = float((double(across) + 0.5) / double(side));
                const float b = float((double(along) + 0.5) / double(side));
                const float dz = MulS(SubS(v, a), scale);
                const float dx = MulS(SubS(u, b), scale);
                const float dz2 = MulS(dz, dz);
                const float k = DivS(0.5f, AddS(AddS(MulS(dx, dx), 1.0f), dz2));
                const float kk = MulS(k, k);
                float slopeX = MulS(MulS(scale2, dx), kk);
                slopeX = AddS(slopeX, slopeX);
                float slopeZ = MulS(MulS(scale2, dz), kk);
                slopeZ = AddS(slopeZ, slopeZ);
                {
                    cRZString name;
                    mesh->SetMorphNames(uint32_t(morph), "rectMesh", name.Sprintf("bump_%d", morph)->ToChar());
                }
                const float raised[3] = {AddS(MulS(k, kUp[0]), position[0]), AddS(MulS(kUp[1], k), position[1]),
                                         AddS(MulS(kUp[2], k), position[2])};
                mesh->AddMorphPosition(uint32_t(morph), vertex, raised);
                if (morphNormals) {
                    const float length2 = AddS(AddS(MulS(slopeZ, slopeZ), MulS(slopeX, slopeX)), 1.0f);
                    const float inverse = DivS(1.0f, float(std::sqrt(double(length2))));
                    const float normal[3] = {MulS(inverse, slopeX), MulS(inverse, slopeZ), inverse};
                    mesh->AddMorphNormal(uint32_t(morph), vertex, normal);
                }
            }
        }
    }

    const uint8_t kinds = uint8_t(uint32_t(morphNormals) * 2u + 5u); // the byte doubled: 0x80 gives kinds 5 (0x105)
    const uint32_t width = uint32_t(columns);
    int32_t pattern = mode;
    mesh->SetGroupName("subset0");
    uint32_t base = 0;
    for (int32_t row = 0; row < lastRow; ++row, base += width) {
        uint32_t i = base;
        for (int32_t column = 0; column < lastColumn; ++column, ++i) {
            if (mode == 3) pattern = int32_t((uint32_t(column) ^ uint32_t(row)) & 1u) + 1;
            mesh->MarkUnknown();
            mesh->AddIndex(kinds, i);
            mesh->AddIndex(kinds, i + 1);
            if (pattern == 0) {
                mesh->AddIndex(kinds, width + 1 + i);
                mesh->AddIndex(kinds, width + i);
            } else if (pattern == 1) {
                const uint32_t corner = width + i;
                mesh->AddIndex(kinds, corner);
                if (!morphNormals) mesh->AddNormalIndex(0);
                mesh->CommitGroup();
                mesh->MarkUnknown();
                mesh->AddIndex(kinds, corner);
                mesh->AddIndex(kinds, i + 1);
                mesh->AddIndex(kinds, width + 1 + i);
            } else {
                const uint32_t corner = width + 1 + i;
                mesh->AddIndex(kinds, corner);
                if (!morphNormals) mesh->AddNormalIndex(0);
                mesh->CommitGroup();
                mesh->MarkUnknown();
                mesh->AddIndex(kinds, corner);
                mesh->AddIndex(kinds, width + i);
                mesh->AddIndex(kinds, i);
            }
            if (!morphNormals) mesh->AddNormalIndex(0);
            mesh->CommitGroup();
        }
    }
    mesh->Finish();
    if (oldPart) static_cast<cIGeometryBuilder *>(oldPart)->Release();
    if (bounds) bounds->Release();
    if (compactor) compactor->Release();
    if (mesh) mesh->Release();
    return out;
}

} // namespace nGZSceneGraph
