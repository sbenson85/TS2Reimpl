// nGZSceneGraph's light bound meshes (lane 261111c): the cone and the sphere t_SetSceneFlags (0xaeeea0, RebuildLightBounds
// here too) hangs under the spot and point lights, and the shadow's quad (0xaf16f0). All build a geometry node of the COM director's builders; the arithmetic is SSE in the
// original's operand order (compgeom/ConvexMath.h), the angles' sines and cosines the toolchain's (double, as the
// original's _libm_sse2 calls).
#include "sg/SGUtilities.h"

#include "compgeom/ConvexMath.h"
#include "game/game.h"
#include "gz/cIGZCOM.h"
#include "misc/SmallVirtuals.h"
#include "rz/StaticGetters.h"
#include "sg/SGLightInterfaces.h"
#include "sg/SGNodeInterfaces.h"
#include "sg/cBoundingVolumeBuilder.h"
#include "sg/cCompactorBuilder.h"
#include "sg/cGeometryNode.h"
#include "sg/cIndexedMeshBuilder.h"
#include "sg/cLightRefNode.h"
#include "sg/cSceneGraphSystem.h"
#include "sg/cShape.h"
#include "sg/cShapeRefNode.h"
#include "sg/cTangentSpaceBuilder.h"

#include <cmath>
#include <cstring>

namespace nGZSceneGraph {

namespace {

using nGZCompGeom::convex::AddS;
using nGZCompGeom::convex::DivS;
using nGZCompGeom::convex::MulS;
using nGZCompGeom::convex::Negated;
using nGZCompGeom::convex::SubS;

constexpr uint32_t kGeometryNodeClass = 0x7ba3838c;
constexpr uint32_t kIndexedMeshBuilderClass = 0x9bffc10d;
constexpr float kTwoPi = 6.2831855f; // 0xda20f0

// An unsigned count as the original converts it: through double (the 2^32 correction for the high bit), then rounded.
float CountAsFloat(uint32_t count) { return float(double(count)); }

// The angle's sine and cosine as the original takes them: the float widened, the double result rounded.
float Cos(float angle) { return float(std::cos(double(angle))); }
float Sin(float angle) { return float(std::sin(double(angle))); }

template <class T> void Create(uint32_t classId, T **out)
{
    static_cast<cIGZCOM *>(GetDirectorFrameworkObject())->GetClassObject(classId, T::kIid, reinterpret_cast<void **>(out));
}

} // namespace

cIGeometryNode **MakeConeMesh(cIGeometryNode **out, float radius, float length, bool cap, uint32_t segments)
{
    const float depth = Negated(length);
    *out = nullptr;
    Create(kGeometryNodeClass, out);
    cIIndexedMeshBuilder *mesh = nullptr;
    Create(kIndexedMeshBuilderClass, &mesh);
    cIBoundingVolumeBuilder *bounds = nullptr;
    Create(cBoundingVolumeBuilder::kClassId, &bounds);
    cICompactorBuilder *compactor = nullptr;
    Create(cCompactorBuilder::kClassId, &compactor);
    // ORIGINAL-BUG: neither the node nor the mesh is checked; a class that fails to make its object faults here.
    // receiver: the node (slot 32 AddPart, each builder by its cIGeometryBuilder face, null when it failed)
    (*out)->AddPart(mesh);
    (*out)->AddPart(bounds);
    (*out)->AddPart(compactor);
    mesh->Reset(false);
    mesh->SetGroupName("subset0");

    // The spokes: point i of spoke k at i / (segments - 1) along (cos, sin) * radius and -length. One segment divides
    // 0 by 0 (NaN points), as the original does.
    uint32_t last = segments - 1;
    if (segments != 0) {
        const float spokes = CountAsFloat(last);
        const float slope = DivS(Negated(radius), depth);
        const float step = DivS(kTwoPi, CountAsFloat(segments));
        float angle = 0.0f;
        for (uint32_t ring = segments; ring != 0; --ring) {
            const float c = Cos(angle), s = Sin(angle);
            for (uint32_t i = 0; i < segments; ++i) {
                const float t = DivS(float(int32_t(i)), spokes);
                const float r = MulS(t, radius);
                const float point[3] = {MulS(c, r), MulS(s, r), MulS(t, depth)};
                mesh->AddPosition(point);
                const float normal[3] = {c, s, slope};
                mesh->AddNormal(normal);
            }
            angle = AddS(angle, step);
        }
    }

    if (cap) {
        const float down[3] = {0.0f, 0.0f, -1.0f};
        mesh->AddNormal(down);
        mesh->MarkUnknown();
        mesh->AddNormalIndex(mesh->CountNormals() - 1);
        uint32_t index = segments * segments - 1;
        for (uint32_t k = segments; k != 0; --k, index -= segments) mesh->AddPositionIndex(index);
        mesh->CommitGroup();
    }

    // A quad between points k and k + 1 of each spoke and the next (the last spoke's next is the first).
    uint32_t base = 0;
    for (uint32_t spoke = 0; spoke < segments; ++spoke, base += segments) {
        const uint32_t next = spoke != last ? base + segments : 0;
        for (uint32_t k = 0; k < last; ++k) {
            mesh->MarkUnknown();
            mesh->AddIndex(3, base + k);
            mesh->AddIndex(3, base + k + 1);
            mesh->AddIndex(3, next + k + 1);
            mesh->AddIndex(3, next + k);
            mesh->CommitGroup();
        }
    }
    mesh->Finish();
    if (compactor) compactor->Release();
    if (bounds) bounds->Release();
    if (mesh) mesh->Release();
    return out;
}

cIGeometryNode **MakeSphereMesh(cIGeometryNode **out, float radius, uint32_t segments, const char *name, bool tangents)
{
    *out = nullptr;
    Create(kGeometryNodeClass, out);
    cIIndexedMeshBuilder *mesh = nullptr;
    Create(kIndexedMeshBuilderClass, &mesh);
    // ORIGINAL-BUG: neither the node, the mesh nor the tangent space builder is checked before it is called.
    (*out)->AddPart(mesh); // receiver: the node (slot 32 AddPart)
    if (tangents) {
        cITangentSpaceBuilder *tangent = nullptr;
        Create(cTangentSpaceBuilder::kClassId, &tangent);
        (*out)->AddPart(tangent);
        tangent->AddName(name); // receiver: the tangent space builder's own face (slot 0)
        if (tangent) tangent->Release();
    }
    mesh->Reset(false);
    mesh->SetGroupName(name);

    // Ring j at polar angle j * 2pi / segments, point i at azimuth i * 2pi / segments; both ends of each ring share a
    // place (the texture seam).
    const uint32_t half = segments >> 1;
    const float count = CountAsFloat(segments);
    for (uint32_t j = 0; j <= half; ++j) {
        const float v = DivS(float(int32_t(2 * j)), count);
        const float polar = MulS(DivS(float(int32_t(j)), count), kTwoPi);
        const float sinPolar = Sin(polar), cosPolar = Cos(polar);
        const float z = MulS(cosPolar, radius);
        for (uint32_t i = 0; i <= segments; ++i) {
            const float u = DivS(float(int32_t(i)), count);
            const float azimuth = MulS(u, kTwoPi);
            const float nx = MulS(Cos(azimuth), sinPolar);
            const float ny = MulS(Sin(azimuth), sinPolar);
            const float point[3] = {MulS(nx, radius), MulS(ny, radius), z};
            mesh->AddPosition(point);
            const float normal[3] = {nx, ny, cosPolar};
            mesh->AddNormal(normal);
            const float coord[2] = {u, v};
            mesh->AddTexCoord(coord, 0);
        }
    }

    // Faces (kinds 7) from each ring's point k to the next ring's: all but the last row take k, the next ring's k and
    // k + 1, then (past the first row) k + 1; the last row takes k, the next ring's k, then (past the first row) k + 1.
    const int32_t lastRow = int32_t(half) - 1;
    const uint32_t stride = segments + 1;
    uint32_t base = 0;
    for (uint32_t row = 0; row < half; ++row, base += stride) {
        if (segments == 0) continue;
        const uint32_t next = base + stride;
        const bool full = int32_t(row) < lastRow;
        for (uint32_t k = 0; k < segments; ++k) {
            mesh->MarkUnknown();
            mesh->AddIndex(7, base + k);
            mesh->AddIndex(7, next + k);
            if (full) mesh->AddIndex(7, next + k + 1);
            if (int32_t(row) > 0) mesh->AddIndex(7, base + k + 1);
            mesh->CommitGroup();
        }
    }
    mesh->Finish();
    if (mesh) mesh->Release();
    return out;
}

namespace {

constexpr uint32_t kShapeRefNodeClass = 0x65245517;
constexpr uint32_t kRangeAttribute = 0x0a5622e9;
constexpr uint32_t kConeAngleAttribute = 0x29de5328;
constexpr uint32_t kConeAngle2Attribute = 0x49de5331;
constexpr uint32_t kScalarEventVTable = 0xdb07f4; // the game's nGZEvent::cScalarEvent<float>
constexpr uint32_t kPointLightType = 2, kSpotLightType = 3;
constexpr uint32_t kPointLightFlag = 4, kSpotLightFlag = 8;
constexpr uint32_t kBoundSegments = 32;
constexpr char kBoundTag[] = "@@BoundingLights@@";
constexpr char kSubset[] = "subset0";

// The three counted iterators slot 44 fills (the cursor, the begin, the end), released end first.
struct NodeRange {
    cISceneNodeIterator *mCursor = nullptr;
    cISceneNodeIterator *mBegin = nullptr;
    cISceneNodeIterator *mEnd = nullptr;
    NodeRange() = default;
    NodeRange(const NodeRange &) = delete;
    NodeRange &operator=(const NodeRange &) = delete;
    ~NodeRange()
    {
        if (mEnd) mEnd->Release();
        if (mBegin) mBegin->Release();
        if (mCursor) mCursor->Release();
    }
    bool AtEnd() { return mCursor->IsEqual(mEnd); } // receiver: cISceneNodeIterator (slot 4)
};

// The game's cScalarEvent<float> an attribute is read into (the node's slot 10).
struct ScalarEvent {
    const void *mVTable;
    float mValue;
};

uint32_t WordOf(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

nRZSceneGraph::cBranchNodesIIDFilter MakeFilter(uint32_t iid)
{
    return {&ts2_rva_e7cf54, iid}; // the inline constructor 0xaea7a0's two words
}

// The node's attribute `id` read into a scalar event. ORIGINAL-BUG: the value is not set first, so a node that does not
// write it leaves what the stack held.
float ReadScalar(cILightRefNode *node, uint32_t id)
{
    ScalarEvent event;
    event.mVTable = GameAddress<const void>(kScalarEventVTable);
    node->GetAttribute(WordOf(&event), id, 0); // receiver: the light-ref node (slot 10)
    return event.mValue;
}

// The bound mesh given to the shape (lod 0) under `material`, the shape to the shape-ref node, the mesh let go of.
void AddBound(cIShape *shape, cIShapeRefNode *holder, cIGeometryNode *mesh, const char *material)
{
    shape->AddPart(reinterpret_cast<cIGZUnknown *>(mesh), 0); // receiver: cIShape (slot 41)
    shape->BindMaterial(material, kSubset);                   // receiver: cIShape (slot 34)
    holder->AddShape(shape);                                  // receiver: cIShapeRefNode (slot 94)
    if (mesh) reinterpret_cast<cIGZUnknown *>(mesh)->Release();
}

} // namespace

void RebuildLightBounds(cICompositionTreeNode *scene, uint32_t flags)
{
    nRZSceneGraph::cBranchNodesIIDFilter shapes = MakeFilter(cIShapeRefNode::kIid);
    {
        NodeRange range;
        scene->MakeIterator44(&range, 0, WordOf(&shapes)); // receiver: the scene (slot 44)
        while (!range.AtEnd()) {
            auto *const node = reinterpret_cast<cICompositionTreeNode *>(range.mCursor->Get()); // receiver: cISceneNodeIterator
            const char *const tag = GetNodeTag(node);
            if (tag && std::strcmp(tag, kBoundTag) == 0) node->GetParent()->RemoveChild(node); // receiver: the node, its parent
            range.mCursor->Next(); // receiver: cISceneNodeIterator
        }

        if (flags & (kPointLightFlag | kSpotLightFlag)) {
            nRZSceneGraph::cBranchNodesIIDFilter lights = MakeFilter(cILightRefNode::kIid);
            {
                NodeRange lightRange;
                scene->MakeIterator44(&lightRange, 0, WordOf(&lights)); // receiver: the scene (slot 44)
                while (!lightRange.AtEnd()) {
                    auto *const node = reinterpret_cast<cILightRefNode *>(lightRange.mCursor->Get()); // receiver: cISceneNodeIterator
                    cIShapeRefNode *holder = nullptr;
                    static_cast<cIGZCOM *>(GetDirectorFrameworkObject())
                        ->GetClassObject(kShapeRefNodeClass, cIShapeRefNode::kIid, reinterpret_cast<void **>(&holder));
                    // ORIGINAL-BUG: neither the shape-ref node nor the shape is checked before it is used.
                    SetNodeTag(holder, kBoundTag);
                    cIShape *shape = nullptr;
                    static_cast<cIGZCOM *>(GetDirectorFrameworkObject())
                        ->GetClassObject(cShape::kClassId, cIShape::kIid, reinterpret_cast<void **>(&shape));
                    bool keep = true;
                    if (cILight *const light = node->GetLight()) { // receiver: the light-ref node (slot 95)
                        const float range = ReadScalar(node, kRangeAttribute);
                        if (range <= 0.0f) {
                            keep = false; // no bound, and the holder is not added
                        } else if (light->GetLightType() == kPointLightType && (flags & kPointLightFlag)) { // receiver: cILight (slot 36)
                            cIGeometryNode *mesh;
                            MakeSphereMesh(&mesh, range, kBoundSegments, kSubset, false);
                            AddBound(shape, holder, mesh, "VisBound_PointLight");
                        } else if (light->GetLightType() == kSpotLightType && (flags & kSpotLightFlag)) { // asked again
                            const float angle = ReadScalar(node, kConeAngleAttribute);
                            const float angle2 = ReadScalar(node, kConeAngle2Attribute);
                            const float half = MulS(AddS(angle2, angle), 0.5f);
                            const float radius = MulS(float(std::tan(double(half))), range);
                            cIGeometryNode *mesh;
                            MakeConeMesh(&mesh, radius, range, false, kBoundSegments);
                            AddBound(shape, holder, mesh, "VisBound_SpotLight");
                        }
                    }
                    if (keep && int32_t(holder->ShapeCount()) > 0) node->AddChild(holder); // receiver: cIShapeRefNode (98), the node (45)
                    if (shape) shape->Release();
                    if (holder) holder->Release();
                    lightRange.mCursor->Next(); // receiver: cISceneNodeIterator
                }
            }
            lights.Destruct();
        }
    }
    shapes.Destruct();
}

cIGeometryNode **MakeQuadMesh(cIGeometryNode **out, const float *center, const float *size, const float *uvLow,
                              const float *uvHigh)
{
    *out = nullptr;
    Create(kGeometryNodeClass, out);
    cIIndexedMeshBuilder *mesh = nullptr;
    Create(kIndexedMeshBuilderClass, &mesh);
    // ORIGINAL-BUG: neither the node nor the mesh is checked.
    (*out)->AddPart(mesh); // receiver: the node (slot 32), the mesh by its cIGeometryBuilder face
    mesh->Reset(false);

    // The corners: the half sizes through double (the original's cvtps2pd / mulsd 0.5 / cvtpd2ps), the operands in its
    // order (x - w/2, w/2 + x), z + 0 (a -0 becomes +0).
    const auto half = [](float v) { return float(double(v) * 0.5); };
    const float x = center[0], y = center[1];
    const float z = AddS(center[2], 0.0f);
    const float corners[4][3] = {
        {SubS(x, half(size[0])), SubS(y, half(size[1])), z},
        {AddS(half(size[0]), x), SubS(y, half(size[1])), z},
        {AddS(half(size[0]), x), AddS(half(size[1]), y), z},
        {SubS(x, half(size[0])), AddS(half(size[1]), y), z},
    };
    for (const float *corner : corners) mesh->AddPosition(corner);
    const float coords[4][2] = {
        {uvLow[0], uvHigh[1]}, {uvHigh[0], uvHigh[1]}, {uvHigh[0], uvLow[1]}, {uvLow[0], uvLow[1]},
    };
    for (const float *coord : coords) mesh->AddTexCoord(coord, 0);
    const float up[3] = {0.0f, 0.0f, 1.0f};
    mesh->AddNormal(up);
    mesh->SetGroupName("layer");
    mesh->MarkUnknown();
    for (uint32_t k = 0; k < 4; ++k) mesh->AddIndex(5, k);
    mesh->AddNormalIndex(0);
    mesh->CommitGroup();
    mesh->Finish();
    if (mesh) mesh->Release();
    return out;
}

// 0xaef5b0 (lane 261111d)
cIGeometryNode **MakeBoxMesh(cIGeometryNode **out, const float *low, const float *high)
{
    *out = nullptr;
    Create(kGeometryNodeClass, out);
    cIIndexedMeshBuilder *mesh = nullptr;
    Create(kIndexedMeshBuilderClass, &mesh);
    // ORIGINAL-BUG: neither the node nor the mesh is checked.
    (*out)->AddPart(mesh); // receiver: the node (slot 32), the mesh by its cIGeometryBuilder face
    mesh->Reset(false);

    // Corner k takes x from high when bit 0 is set, y on bit 1, z on bit 2 (bit copies).
    for (uint32_t k = 0; k < 8; ++k) {
        const float corner[3] = {(k & 1 ? high : low)[0], (k & 2 ? high : low)[1], (k & 4 ? high : low)[2]};
        mesh->AddPosition(corner);
    }
    // The axes (the original passes its {1,0,0} / {0,1,0} / {0,0,1} globals 0x01073e80 / 8c / 98, never written) and
    // their sign-flipped copies.
    static const float kNormals[6][3] = {
        {1.0f, 0.0f, 0.0f}, {-1.0f, -0.0f, -0.0f}, {0.0f, 1.0f, 0.0f},
        {-0.0f, -1.0f, -0.0f}, {0.0f, 0.0f, 1.0f}, {-0.0f, -0.0f, -1.0f},
    };
    for (const float *normal : kNormals) mesh->AddNormal(normal);
    mesh->SetGroupName("subset0");
    // The faces' corners (0x00e7d228): +X, -X, +Y, -Y, +Z, -Z, each with the normal of the same number.
    static const uint32_t kFaces[6][4] = {
        {1, 3, 7, 5}, {2, 0, 4, 6}, {3, 2, 6, 7}, {0, 1, 5, 4}, {4, 5, 7, 6}, {2, 3, 1, 0},
    };
    for (uint32_t face = 0; face < 6; ++face) {
        mesh->MarkUnknown();
        mesh->AddNormalIndex(face);
        mesh->AddPositionIndices(4, kFaces[face], 0);
        mesh->CommitGroup();
    }
    mesh->Finish();
    if (mesh) mesh->Release();
    return out;
}

} // namespace nGZSceneGraph
