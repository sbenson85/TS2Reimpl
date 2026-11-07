#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "compgeom/CompGeom.h"
#include "rz/GameAllocator.h" // GZServiceAllocator

// nGZCompGeom's triangle sets (lane PC, scenegraph G0b; RVAs 1.18.0.187): the set's data (vertices, corner indices and an
// AABB tree over the triangles) and its bound shape, cBoundT<cITriangleSetShape> / cTriangleSetShape.
//
// Layout of the shape (0x30 bytes): +0 the vtable, +4 the TriangleSet (0x28 bytes), +0x2c the reference count.
// Vtables (43 slots; slot 42 the deleting destructor): cBoundT<cITriangleSetShape> 0xda1d5c (what Clone gives its copies),
// cTriangleSetShape 0xda1e0c (what the director's factory 0x196560 stores).
//
// Standalone shape (docs/standalone-shape.md): real C++ classes; the compiler emits the vtables. The factory is hooked to
// Create; the slot bodies are reached only through the two vtables (in effect through callers). The set's own functions
// (Resize, SetVertex, SetTriangle, Build, Refit) are called directly by game code that fills sets (the GMDC reader, the
// world and lot code), so they are hooked at their RVAs.
namespace nGZCompGeom {

// The set's vectors take their memory from the allocator service, as the game's (0x194dc0, 0x194d20, 0x194c80
// allocate; 0x197100 frees).
template <class T> using ServiceVector = std::vector<T, GZServiceAllocator<T>>;

// A vertex of the set. Growing the vertex list leaves new vertices as the memory held them (the original's resize
// constructs nothing), hence the empty constructor.
struct TriangleSetVertex : Vec3 {
    TriangleSetVertex() {}
};
static_assert(sizeof(TriangleSetVertex) == 12);

// A node of the set's tree (32 bytes): the box as half extents and centre, then the two children. A child code is
// index * 2 for another node, triangle * 2 + 1 for a triangle. The constructor (0x195530) clears the children only.
struct AABBTreeNode {
    Cuboid box;      // +0: half extents
    Vec3 centre;     // +0xc
    uint32_t left;   // +0x18
    uint32_t right;  // +0x1c
    AABBTreeNode() : left(0), right(0) {}

    static constexpr uint32_t kTriangleBit = 1;
};
static_assert(sizeof(AABBTreeNode) == 0x20);

struct TriangleSet;

// nGZCompGeom::cAABBNoLeafTree<cTypeTraits<float>, cTriangleSetBase<...>> (base cBVNoLeafTree; vtables 0xda1d54 / 0xda1d4c,
// one slot, the deleting destructor 0x195a40): the triangles' boxes as a binary tree whose leaves are triangle codes,
// root first, every node before its children.
class cAABBNoLeafTree {
public:
    cAABBNoLeafTree() = default;
    cAABBNoLeafTree(const cAABBNoLeafTree &other) : mNodes(other.mNodes) {}
    cAABBNoLeafTree &operator=(const cAABBNoLeafTree &) = delete;
    virtual ~cAABBNoLeafTree() {}

    // 0x195c80 (__thiscall, ret 4): rebuilt from the set's triangles (a top-down median split along the longest axis).
    void Build(const TriangleSet &set);
    // 0x196e20 (__thiscall, ret 8; the second argument unused): every node's box recomputed from its children, last node
    // first (the triangles may have moved; the shape of the tree stays).
    void Refit(const TriangleSet &set, uint32_t unused);

    // +0 the vtable
    ServiceVector<AABBTreeNode> mNodes; // +4
};
static_assert(sizeof(cAABBNoLeafTree) == 0x10);

// nGZCompGeom::cTriangleSet<cTypeTraits<float>> (the shape's +4; what its data slots answer).
struct TriangleSet {
    ServiceVector<TriangleSetVertex> mVertices; // +0
    ServiceVector<uint16_t> mIndices;           // +0xc: three corners per triangle
    cAABBNoLeafTree mTree;                      // +0x18

    uint32_t TriangleCount() const { return static_cast<uint32_t>(mIndices.size()) / 3; }
    // The box of a triangle (0x1a26d0 on the triangle's three corners).
    BoundBox TriangleBox(uint32_t triangle) const;

    // 0x195af0 (__thiscall, ret 8): `vertexCount` vertices (new ones left as the memory held them) and room for
    // `triangleCount` triangles (new corners zero).
    // 0x195af0 eax-ignored: eax is what the index vector's resize left there; no caller reads it.
    void Resize(uint32_t vertexCount, uint32_t triangleCount);
    // 0x196d00 (__thiscall, ret 8): vertex `index` (16 bits) set to *vertex.
    // 0x196d00 eax-ignored: eax holds the copied z bits; no caller reads it.
    void SetVertex(uint16_t index, const Vec3 *vertex);
    // 0x196cc0 (__thiscall, ret 0x10): the corners of triangle `index`.
    // 0x196cc0 eax-ignored: eax holds the third corner in ax (a spill); no caller reads it.
    void SetTriangle(int32_t index, uint16_t a, uint16_t b, uint16_t c);
    // 0x196300 (__thiscall): the tree rebuilt (mTree.Build).
    // 0x196300 eax-ignored: eax is what the tree's Build left there; no caller reads it.
    void Build();
    // 0x1970f0 (__thiscall, ret 4; the argument unused): the tree's boxes refitted (mTree.Refit).
    // 0x1970f0 eax-ignored: eax is what the tree's Refit left there; no caller reads it.
    void Refit(uint32_t unused);
};
static_assert(sizeof(TriangleSet) == 0x28);
static_assert(offsetof(TriangleSet, mIndices) == 0xc && offsetof(TriangleSet, mTree) == 0x18);

// 0x1a26d0 (__thiscall on the three corners, ret 4; returns the box): per coordinate max = std::max(std::max(a, b), c),
// min = std::min(std::min(a, b), c).
BoundBox TriangleBounds(const Vec3 &a, const Vec3 &b, const Vec3 &c);

// nGZCompGeom::cITriangleSetShape (id 0xfcfe5320, also the director's class id). The scenegraph's own slice of it is
// src/sg/cGeometryDataContainerClass.h's cITriangleSetShape (the same vtable), hence this name.
class cITriangleSetBound : public cIShapeT<TriangleSet, cIBound> {
public:
    static constexpr uint32_t kInterfaceId = 0xfcfe5320;
};

// nGZCompGeom::cBoundT<cITriangleSetShape> (vtable 0xda1d5c).
class cTriangleSetBound : public cITriangleSetBound {
public:
    static constexpr uint32_t kClassId = kInterfaceId;

    cTriangleSetBound() : mRefCount(0) {}
    explicit cTriangleSetBound(const TriangleSet &set);
    cTriangleSetBound(const cTriangleSetBound &) = delete;
    virtual ~cTriangleSetBound() {} // slot 42 (0x195ad0: the destructor 0x1955f0, then operator delete on bit 0)

    // cIGZUnknown
    bool QueryInterface(uint32_t id, void **out) override; // 0 (0x196c60): 1, cIShape, cIBound, cITriangleSetShape
    uint32_t AddRef() override;                            // 1 (shared 0x18fd70)
    uint32_t Release() override;                           // 2 (0x196ca0): at zero, delete this and return 0
    // cIShape
    bool Clone(uint32_t id, void **out) override; // 3 (0x1963b0): a cBoundT copy (count 0) queried for `id`
    uint32_t GetClassID() override;               // 4 (0x1965d0)
    // cIBound: cones and spheres never meet a triangle set (false); cuboids, other sets and bounds do.
    bool IntersectsOrientedConeIn(const AffineTransform3 *frame, const OrientedCone *cone) override;           // 0x1966d0
    bool IntersectsTranslatedConeIn(const AffineTransform3 *frame, const TranslatedCone *cone) override;       // 0x196720
    bool IntersectsTranslatedSphereIn(const AffineTransform3 *frame, const TranslatedSphere *sphere) override; // 0x196890
    bool IntersectsOrientedCuboidIn(const AffineTransform3 *frame, const OrientedCuboid *cuboid) override;     // 0x1967b0
    bool IntersectsTranslatedCuboidIn(const AffineTransform3 *frame, const TranslatedCuboid *cuboid) override; // 0x196800
    bool IntersectsBoundIn(const AffineTransform3 *frame, cIBound *bound) override;                            // 0x1969e0
    bool IntersectsTriangleSetAt(const TriangleSet *set, const AffineTransform3 *frame) override;               // 0x196b60
    bool IntersectsConeAt(const Cone *cone, const AffineTransform3 *frame) override;                           // 0x196a00
    bool IntersectsSphereAt(const Sphere *sphere, const AffineTransform3 *frame) override;                     // 0x196a00
    bool IntersectsCuboidAt(const Cuboid *cuboid, const AffineTransform3 *frame) override;                     // 0x196a80
    bool IntersectsBoundAt(cIBound *bound, const AffineTransform3 *frame) override;                            // 0x196be0
    bool IntersectsTriangleSetInAt(const AffineTransform3 *frame, const TriangleSet *set,
                                   const AffineTransform3 *otherFrame) override; // 0x1969a0
    bool IntersectsConeInAt(const AffineTransform3 *frame, const Cone *cone,
                            const AffineTransform3 *otherFrame) override; // 0x1966b0
    bool IntersectsSphereInAt(const AffineTransform3 *frame, const Sphere *sphere,
                              const AffineTransform3 *otherFrame) override; // 0x1966b0
    bool IntersectsCuboidInAt(const AffineTransform3 *frame, const Cuboid *cuboid,
                              const AffineTransform3 *otherFrame) override; // 0x196920
    bool IntersectsBoundInAt(const AffineTransform3 *frame, cIBound *bound,
                             const AffineTransform3 *otherFrame) override; // 0x1969c0
    bool IntersectsSegmentIn(const AffineTransform3 *frame, const Segment3 *segment) override; // 0x196980
    bool IntersectsRayIn(const AffineTransform3 *frame, const Ray3 *ray) override;             // 0x196960
    bool IntersectsLineIn(const AffineTransform3 *frame, const Line3 *line) override;          // 0x196940
    bool IntersectsLinearIn(const AffineTransform3 *frame, cILinear3 *linear) override;        // shared 0x190c40
    bool IntersectsSegment(const Segment3 *segment) override;                                  // 0x196b40
    bool IntersectsRay(const Ray3 *ray) override;                                              // 0x196b20
    bool IntersectsLine(const Line3 *line) override;                                           // 0x196b00
    bool IntersectsLinear(cILinear3 *linear) override;                                         // shared 0x190ed0
    bool FirstHitSegmentIn(float *t, const AffineTransform3 *frame, const Segment3 *segment) override; // 0x196da0
    bool FirstHitRayIn(float *t, const AffineTransform3 *frame, const Ray3 *ray) override;             // 0x196d80
    bool FirstHitLineIn(float *t, const AffineTransform3 *frame, const Line3 *line) override;          // 0x196d60
    bool FirstHitLinearIn(float *t, const AffineTransform3 *frame, cILinear3 *linear) override;        // shared 0x191f50
    bool FirstHitSegment(float *t, const Segment3 *segment) override;                                  // 0x196e00
    bool FirstHitRay(float *t, const Ray3 *ray) override;                                              // 0x196de0
    bool FirstHitLine(float *t, const Line3 *line) override;                                           // 0x196dc0
    bool FirstHitLinear(float *t, cILinear3 *linear) override;                                         // shared 0x191fd0
    BoundBox *GetBoundBoxIn(BoundBox *out, const AffineTransform3 *frame) override;   // 0x195b70: the root's box placed by `frame`
    BoundBox *GetBoundBoxRigid(BoundBox *out, const RigidTransform3 *rigid) override; // 0x195be0: placed by `rigid`
    BoundBox *GetBoundBox(BoundBox *out) override;                                    // 0x195c60: the root's box
    // cIShapeT
    TriangleSet *ShapeData() override;            // 40 (shared 0x18ea60)
    const TriangleSet *ShapeDataConst() override; // 41 (shared 0x18ea60)

    // +0 the vtable
    TriangleSet mShape;  // +4
    uint32_t mRefCount; // +0x2c
};

// nGZCompGeom::cTriangleSetShape (vtable 0xda1e0c; the director's factory 0x196560, class id 0xfcfe5320).
class cTriangleSetShape : public cTriangleSetBound {
public:
    cTriangleSetShape() {}
    ~cTriangleSetShape() override {}
    static cTriangleSetShape *__cdecl Create(); // 0x196560: an empty set, count 0
};

static_assert(sizeof(cTriangleSetShape) == 0x30);
static_assert(offsetof(cTriangleSetBound, mShape) == 4 && offsetof(cTriangleSetBound, mRefCount) == 0x2c);

} // namespace nGZCompGeom
