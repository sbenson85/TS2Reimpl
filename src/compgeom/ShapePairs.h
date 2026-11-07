#pragma once
#include <cstdint>

#include "compgeom/CompGeom.h"

// The line, ray and segment tests against the convex shapes (lane PI, scenegraph G0b step 3; RVAs 1.18.0.187), the
// functions behind the linear shapes' slots 8-12, 15-17 and 19-23, 26-28 (src/compgeom/Linear3.cpp).
//
// Every test takes the other shape, where it is (a placement) and the linear shape's data, and works in the other shape's
// own frame: the line goes through the inverse of the placement, a core solves the line (or the segment's end points)
// against the shape there, and the wrapper reads the parameters the core left:
//   - cores (stdcall, ret 0x10: t0, t1, shape, line): write t0 <= t1, the span of the line inside the shape, and return
//     whether there is one (a core can return false after writing);
//   - Line: the span exists; Ray: and its end t1 is not before the origin (t1 >= 0); Segment: and it overlaps [0, 1];
//   - first hit: the Line gives its t0 straight out (the core writes the caller's float), the Ray and Segment the first
//     of the span that lies on the shape (t0, or t1 when t0 is before the origin).
// Placements: Oriented (a RigidTransform3), Translated (a position), InFrame (an AffineTransform3 beside the shape).
// Every function is cdecl and returns a bool in al.
namespace nGZCompGeom {

enum class ShapeKind { Cone, Sphere, Cuboid };
enum class Placement { Oriented, Translated, InFrame };

// 0x197d70 ...: (shape, placement, linear) -> does the linear shape meet the shape?
template <ShapeKind S, Placement P, class L> bool __cdecl TestPair(const void *shape, const void *placement, const L *linear);
// 0x1984f0 ...: (t, shape, placement, linear) -> the first meeting, in t.
template <ShapeKind S, Placement P, class L>
bool __cdecl FirstHitPair(float *t, const void *shape, const void *placement, const L *linear);

// 0x1978a0 / 0x197b00 (__stdcall, ret 0x10): the span of a line (origin, direction) / a segment (start, end) inside a
// cone in its own frame (radius at +0, half height at +4). t0 and t1 are written before a false return.
bool __stdcall ConeLineSpan(float *t0, float *t1, const void *cone, const Line3 *line);
bool __stdcall ConeSegmentSpan(float *t0, float *t1, const void *cone, const Segment3 *segment);

// 0x198cf0 / 0x198e50 (__stdcall, ret 0x10): the span inside a sphere of radius *sphere centred on the origin; nothing is
// written when it returns false.
bool __stdcall SphereLineSpan(float *t0, float *t1, const void *sphere, const Line3 *line);
bool __stdcall SphereSegmentSpan(float *t0, float *t1, const void *sphere, const Segment3 *segment);
// 0x1999e0 / 0x199b90 (__stdcall, ret 0x10): the span inside the box of half extents cuboid[0..2] centred on the origin
// (slab by slab, x then y then z); false when empty, or when every axis is parallel to the line (nothing was written
// then); t0 and t1 are written before a false return.
bool __stdcall CuboidLineSpan(float *t0, float *t1, const void *cuboid, const Line3 *line);
bool __stdcall CuboidSegmentSpan(float *t0, float *t1, const void *cuboid, const Segment3 *segment);
// 0x1a2950 / 0x1a2a80 / 0x1a2be0 (__cdecl, the value in st0 as a float widened): the squared distance from a point to a
// line, a ray, a segment.
double __cdecl PointLineDistanceSquared(const Vec3 *point, const Line3 *line);
double __cdecl PointRayDistanceSquared(const Vec3 *point, const Ray3 *ray);
double __cdecl PointSegmentDistanceSquared(const Vec3 *point, const Segment3 *segment);

} // namespace nGZCompGeom
