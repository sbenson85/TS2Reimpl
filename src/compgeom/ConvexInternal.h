#pragma once
#include "compgeom/ConvexMath.h"
#include "compgeom/ConvexShapes.h"

// Pieces the convex shapes' bodies share (lane PM for the sphere, lane PP for the cuboids; scenegraph G0b step 4). Internal
// to src/compgeom: inline, so each body keeps the original's operation order.
namespace nGZCompGeom::convex {

// The placement of a shape carried by `frame`: the frame, offset to the position's image.
inline AffineTransform3 Placed(const AffineTransform3 &frame, const Vec3 &position)
{
    return FrameAt(frame, FramePoint(frame, position));
}

// The two scaled radii added, as the original sums them on the x87: scale(a) * a.radius + scale(b) * b.radius, the first
// product from the first frame, the second from the second (stored as a float between).
inline float ReachOf(const AffineTransform3 &first, float firstRadius, const AffineTransform3 &second, float secondRadius)
{
    const float other = Stored(Mul87(ScaleOf(second), secondRadius));
    return Stored(Add87(Mul87(ScaleOf(first), firstRadius), static_cast<double>(other)));
}

inline Vec3 Difference(const Vec3 &a, const Vec3 &b) { return {SubS(a.x, b.x), SubS(a.y, b.y), SubS(a.z, b.z)}; }

// The squared distance to the box at the origin of a point (p + 0 in each coordinate, as the original adds it), scaled.
inline float BoxGap(const Cuboid &cuboid, const AffineTransform3 &frame, const Vec3 &pointIn)
{
    Vec3 p = FrameInversePoint(frame, pointIn);
    p = {AddS(p.x, 0.0f), AddS(p.y, 0.0f), AddS(p.z, 0.0f)};
    const double d = PointBoxDistanceSquared(&cuboid.half.x, &p.x);
    const double s = ScaleOf(frame);
    return Stored(Mul87(Mul87(d, s), s));
}

// A sphere of radius r reaches a box gap `gap` (frame scale s): r r s s >= gap, products in the SSE order.
inline bool SphereReaches(float radius, float s, float gap) { return MulS(MulS(MulS(radius, radius), s), s) >= gap; }

} // namespace nGZCompGeom::convex
