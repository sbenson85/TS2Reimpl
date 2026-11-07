#pragma once
#include <cstdint>

#include "compgeom/CompGeom.h"
#include "compgeom/TriangleSet.h"

// The separating-axis tests of triangle sets (lane PL, scenegraph G0b step 3, part 5; RVAs 1.18.0.187): a triangle set
// against a cuboid (0x19f8e0) and against another triangle set (0x1a01c0), with the frame helpers and the triangle tests
// they are built from.
//
// Both tests take the first set into the other shape's frame once (a context of 0xb8 bytes: the frame A * inverse(B), its
// inverse, and the absolute values of both matrices plus FLT_EPSILON, which bound a box's reach along the other box's axes),
// then walk the first set's AABB tree: a node's box is tested against the other shape with the 15 axes of two boxes (the
// three of each and the nine cross products, in the cuboid's case as three centre-offset tests each way), and a triangle
// leaf goes through the triangle tests (Akenine-Moller's triangle against a box).
// Every comparison is the ordered C++ one the original's comiss/jcc pair computes (a NaN fails `ja` and passes `jbe`/`jb`).
namespace nGZCompGeom {

// A 3x3 matrix (row major) with the original's product.
struct Matrix3x3 {
    float m[9];
    // 0x1a6650 (__thiscall, ret 4; returns this): this = this * b (0x1a5d20 into a temporary, then copied).
    Matrix3x3 *MultiplyBy(const Matrix3x3 *b);
};
static_assert(sizeof(Matrix3x3) == 0x24);

// 0x1a5d20 (__cdecl; returns out): out = a * b, each element ((.. + ..) + ..) in the original's order. `out` is distinct
// from both (0x1a6650 passes a temporary).
float *__cdecl MatrixProduct(float *out, const float *a, const float *b);

// The frame members the contexts use (AffineTransform3 layout: flags +0, matrix +4, offset +0x28).
struct FramePlacement : AffineTransform3 {
    // 0x19cca0 (__thiscall; returns this): the frame turned into its inverse in place: matrix = InverseMatrix (0x19cb30),
    // offset = that matrix * (-offset). The flags stay.
    FramePlacement *Invert();
    // 0x19be20 (__thiscall, ret 4; returns this): this followed by `other` as a product: flags |= other's, matrix =
    // this matrix * other matrix, offset = other applied to the offset (a point through `other`).
    FramePlacement *Compose(const FramePlacement *other);
    // 0x19be70 (__thiscall, ret 4; returns this; src/compgeom/Matrix4.h): this followed by a rigid transform: flags |= 3,
    // matrix = the rigid's rotation matrix * this matrix, offset = the offset carried through the rigid transform.
    FramePlacement *ComposeRigid(const RigidTransform3 *rigid);
};
static_assert(sizeof(FramePlacement) == sizeof(AffineTransform3));

// What the two tests share (0xb8 bytes).
struct SeparationContext {
    const void *first;  // +0: the triangle set
    const void *second; // +4: the cuboid's half extents, or the second set
    FramePlacement frame;   // +8: the first set's frame composed with the inverse of the second shape's
    FramePlacement inverse; // +0x3c: the inverse of `frame`
    float reach[9];        // +0x70: frame's matrix, absolute values plus FLT_EPSILON
    float inverseReach[9]; // +0x94: inverse's matrix, the same

    // 0x19ffd0 (__thiscall, ret 0x10): builds the context for (first, firstFrame) and (second, secondFrame).
    void Build(const void *firstShape, const AffineTransform3 *firstFrame, const void *secondShape,
               const AffineTransform3 *secondFrame);
};
static_assert(sizeof(SeparationContext) == 0xb8);

// 0x19f800 (__cdecl): the span of a line in a tree node's box (BoxLineSpan on the node's half extents and centre).
int __cdecl TreeBoxLineSpan(float *t0, float *t1, const AABBTreeNode *node, const Line3 *line);

// 0x1a3410 (__cdecl): the triangle (nine floats, in the box's frame) meets the box centred at the origin with the given
// half extents. The edge-cross axes first (nine), then the box's three axes, then the triangle's plane.
bool __cdecl TriangleMeetsBox(const float *triangle, const float *halfExtents);

// 0x19f930 (__cdecl): does anything below `node` (nodes indexed from `base`) meet the cuboid of the context?
bool __cdecl TreeMeetsCuboid(const AABBTreeNode *base, const AABBTreeNode *node, const SeparationContext *context);

// 0x19f8e0 (__cdecl): the set (placed by setFrame) meets the cuboid (half extents, placed by cuboidFrame).
bool __cdecl SetMeetsCuboidTest(const TriangleSet *set, const AffineTransform3 *setFrame, const void *cuboid,
                                const AffineTransform3 *cuboidFrame);

// 0x1a4ea0 (__cdecl): is the point p (its coordinates d and s) strictly inside triangle t projected on those two axes?
bool __cdecl PointInTriangleProjected(const float *p, const float *t, int d, int s);

// 0x1a42d0 (__cdecl): two triangles (nine floats each) in one plane: do they meet (an edge crossing, or one inside the other)?
bool __cdecl CoplanarTrianglesMeet(const float *a, const float *b);

// 0x1a4620 (__cdecl): do the triangles meet (Moller's interval test; coplanar ones go to CoplanarTrianglesMeet)?
bool __cdecl TriangleMeetsTriangle(const float *a, const float *b);

// 0x1a0210 (__cdecl; the result is in al): do the subtrees at n1 (of the first set, nodes indexed from base1) and n2
// meet? Box tests both ways, then the children by kind, leaf pairs through TriangleMeetsTriangle.
bool __cdecl NodesMeet(const AABBTreeNode *base1, const AABBTreeNode *base2, const AABBTreeNode *n1,
                       const AABBTreeNode *n2, const SeparationContext *context);

// 0x1a01c0 (__cdecl): the first set (placed by firstFrame) meets the second (placed by secondFrame).
bool __cdecl SetMeetsSetTest(const TriangleSet *first, const AffineTransform3 *firstFrame, const TriangleSet *second,
                             const AffineTransform3 *secondFrame);

} // namespace nGZCompGeom
