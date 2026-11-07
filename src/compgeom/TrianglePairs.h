#pragma once
#include <cstdint>

#include "compgeom/CompGeom.h"
#include "compgeom/TriangleSet.h"

// The line, ray and segment tests and first hits against a triangle set (lane PK, scenegraph G0b step 3; RVAs 1.18.0.187):
// the functions behind the linear shapes' slots 14 and 25 (src/compgeom/Linear3.cpp) and the triangle-set slots that
// forward to them (src/compgeom/TriangleSet.cpp).
//
// A triangle is nine floats (three corners). The core (Moller-Trumbore) takes the edges from the first corner, the line's
// origin less that corner and the direction, and solves for the parameter t of the hit. The walkers go down the set's AABB
// tree (a node's box tested as a translated cuboid against the linear shape; the children in the order the original takes
// them) and stop at the first triangle that meets the linear shape (tests) or keep the nearest (hits).
// Every function is cdecl.
namespace nGZCompGeom {

// 0x1a3240: Moller-Trumbore on edges e1, e2, s = origin - corner, direction dir. Writes *t only when it returns true.
bool __cdecl TriangleCore(float *t, const float *e1, const float *e2, const float *s, const float *dir);
// 0x1a3130: the triangle (nine floats) against a line: the hit's t (written only on a true).
bool __cdecl TriangleLineHit(float *t, const float *triangle, const Line3 *line);
// 0x1a2db0 / 0x1a2ec0 / 0x1a2fe0: the triangle meets the line / the ray (t >= 0) / the segment (0 <= t <= 1).
bool __cdecl TriangleLineTest(const float *triangle, const Line3 *line);
bool __cdecl TriangleRayTest(const float *triangle, const Ray3 *ray);
bool __cdecl TriangleSegmentTest(const float *triangle, const Segment3 *segment);

// 0x19ace0: the span of a line inside a cuboid centred on `centre` (CuboidLineSpan on the line moved by -centre); returns
// 2 when there is a span, 0 when not (the caller tests for non-zero).
int __cdecl BoxLineSpan(float *t0, float *t1, const void *cuboid, const Vec3 *centre, const Line3 *line);

// 0x19b670 / 0x19b8e0 / 0x19bb50: does anything below `node` meet the linear shape?
template <class L> bool __cdecl TestTreeWalk(const AABBTreeNode *node, const TriangleSet *set, const L *linear);
// 0x19b330: the nearest hit below `node` with parameter in [lo, *t]; *t is lowered to it. Returns whether the walk found
// one (a hit found among this node's own triangles is not counted when both child nodes were walked: kept from the
// original).
bool __cdecl HitTreeWalk(float *t, float lo, const AABBTreeNode *node, const TriangleSet *set, const Line3 *line);

// 0x19ada0 / 0x19adc0 / 0x19ade0: the walk from the root.
template <class L> bool __cdecl TestTriangleSet(const TriangleSet *set, const L *linear);
// 0x19ae00 / 0x19aea0 / 0x19af40: the set in a frame (the linear shape taken into the set's frame first).
template <class L> bool __cdecl TestTriangleSetInFrame(const TriangleSet *set, const AffineTransform3 *frame, const L *linear);
// 0x19afe0 / 0x19b030 / 0x19b0b0: the first hit from the root (t starts at FLT_MAX, or 1 for a segment).
template <class L> bool __cdecl HitTriangleSet(float *t, const TriangleSet *set, const L *linear);
// 0x19b150 / 0x19b1f0 / 0x19b290: in a frame.
template <class L>
bool __cdecl HitTriangleSetInFrame(float *t, const TriangleSet *set, const AffineTransform3 *frame, const L *linear);

} // namespace nGZCompGeom
