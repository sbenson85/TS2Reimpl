#include "compgeom/ConvexInternal.h"
#include "compgeom/ShapePairs.h"

// The translated sphere, cConvexT<cITranslatedSphereShape> (lane PM, scenegraph G0b step 4). Data: the radius at +4, the
// centre at +8. "In" slots carry the sphere by a frame: its centre goes through the frame (Q) and the sphere is tested as
// the frame placed at Q (B); the other shape in the unit frame at its own position (U).
namespace nGZCompGeom {

using namespace convex;

// ---- Cones and triangle sets are not met by a sphere here ----
bool cTranslatedSphereBound::DoIntersectsOrientedConeIn(const AffineTransform3 *, const OrientedCone *) { return false; }
bool cTranslatedSphereBound::DoIntersectsTranslatedConeIn(const AffineTransform3 *, const TranslatedCone *) { return false; }
bool cTranslatedSphereBound::DoIntersectsTriangleSetAt(const TriangleSet *, const AffineTransform3 *) { return false; }
bool cTranslatedSphereBound::DoIntersectsConeAt(const Cone *, const AffineTransform3 *) { return false; }
bool cTranslatedSphereBound::DoIntersectsTriangleSetInAt(const AffineTransform3 *, const TriangleSet *,
                                                         const AffineTransform3 *)
{
    return false;
}
bool cTranslatedSphereBound::DoIntersectsConeInAt(const AffineTransform3 *, const Cone *, const AffineTransform3 *)
{
    return false;
}

// ---- the other shape in world space, this sphere placed by `frame` ----

bool cTranslatedSphereBound::DoIntersectsTranslatedSphereIn(const AffineTransform3 *frame, const TranslatedSphere *other)
{
    const AffineTransform3 here = Placed(*frame, mData.centre);
    const AffineTransform3 there = UnitFrameAt(other->centre);
    const float reach = ReachOf(here, mData.radius, there, other->radius);
    const Vec3 d = Difference(OffsetOf(here), other->centre);
    return MulS(reach, reach) >= LengthSquared(d);
}

bool cTranslatedSphereBound::DoIntersectsOrientedCuboidIn(const AffineTransform3 *frame, const OrientedCuboid *cuboid)
{
    const AffineTransform3 here = Placed(*frame, mData.centre);
    AffineTransform3 box = UnitFrameAt(cuboid->frame.translation);
    static_cast<FrameMath &>(box).SetRotation(cuboid->frame.rotation);
    const float gap = BoxGap(cuboid->cuboid, box, OffsetOf(here));
    const float s = ScaleOf(here);
    return MulS(MulS(MulS(mData.radius, mData.radius), s), s) >= gap;
}

bool cTranslatedSphereBound::DoIntersectsTranslatedCuboidIn(const AffineTransform3 *frame, const TranslatedCuboid *cuboid)
{
    const AffineTransform3 here = Placed(*frame, mData.centre);
    const AffineTransform3 box = UnitFrameAt(cuboid->position);
    const float gap = BoxGap(cuboid->cuboid, box, OffsetOf(here));
    const float s = ScaleOf(here);
    return MulS(MulS(MulS(mData.radius, mData.radius), s), s) >= gap;
}

bool cTranslatedSphereBound::DoIntersectsBoundIn(const AffineTransform3 *frame, cIBound *bound)
{
    (void)frame;
    const AffineTransform3 at = UnitFrameAt(mData.centre);
    return bound->IntersectsSphereAt(&mData, &at);
}

// ---- this sphere as it is, the other placed by `frame` ----

bool cTranslatedSphereBound::DoIntersectsSphereAt(const Sphere *sphere, const AffineTransform3 *frame)
{
    const AffineTransform3 here = UnitFrameAt(mData.centre);
    const float reach = ReachOf(here, mData.radius, *frame, sphere->radius);
    const Vec3 d = Difference(mData.centre, OffsetOf(*frame));
    return MulS(reach, reach) >= LengthSquared(d);
}

bool cTranslatedSphereBound::DoIntersectsCuboidAt(const Cuboid *cuboid, const AffineTransform3 *frame)
{
    const float gap = BoxGap(*cuboid, *frame, mData.centre);
    const AffineTransform3 here = UnitFrameAt(mData.centre);
    const float s = ScaleOf(here);
    return MulS(MulS(MulS(mData.radius, mData.radius), s), s) >= gap;
}

bool cTranslatedSphereBound::DoIntersectsBoundAt(cIBound *bound, const AffineTransform3 *frame)
{
    const AffineTransform3 at = UnitFrameAt(mData.centre);
    return bound->IntersectsSphereInAt(frame, &mData, &at);
}

// ---- both placed ----

bool cTranslatedSphereBound::DoIntersectsSphereInAt(const AffineTransform3 *frame, const Sphere *sphere,
                                                    const AffineTransform3 *otherFrame)
{
    const AffineTransform3 here = Placed(*frame, mData.centre);
    const float reach = ReachOf(here, mData.radius, *otherFrame, sphere->radius);
    const Vec3 d = Difference(OffsetOf(here), OffsetOf(*otherFrame));
    return MulS(reach, reach) >= LengthSquared(d);
}

bool cTranslatedSphereBound::DoIntersectsCuboidInAt(const AffineTransform3 *frame, const Cuboid *cuboid,
                                                    const AffineTransform3 *otherFrame)
{
    const AffineTransform3 here = Placed(*frame, mData.centre);
    const float gap = BoxGap(*cuboid, *otherFrame, OffsetOf(here));
    const float s = ScaleOf(here);
    return MulS(MulS(MulS(mData.radius, mData.radius), s), s) >= gap;
}

bool cTranslatedSphereBound::DoIntersectsBoundInAt(const AffineTransform3 *frame, cIBound *bound,
                                                   const AffineTransform3 *otherFrame)
{
    const AffineTransform3 here = Placed(*frame, mData.centre);
    return bound->IntersectsSphereInAt(otherFrame, &mData, &here);
}

// ---- the linear shapes ----

bool cTranslatedSphereBound::DoIntersectsSegmentIn(const AffineTransform3 *frame, const Segment3 *segment)
{
    const AffineTransform3 here = Placed(*frame, mData.centre);
    return TestPair<ShapeKind::Sphere, Placement::InFrame, Segment3>(&mData, &here, segment);
}
bool cTranslatedSphereBound::DoIntersectsRayIn(const AffineTransform3 *frame, const Ray3 *ray)
{
    const AffineTransform3 here = Placed(*frame, mData.centre);
    return TestPair<ShapeKind::Sphere, Placement::InFrame, Ray3>(&mData, &here, ray);
}
bool cTranslatedSphereBound::DoIntersectsLineIn(const AffineTransform3 *frame, const Line3 *line)
{
    const AffineTransform3 here = Placed(*frame, mData.centre);
    return TestPair<ShapeKind::Sphere, Placement::InFrame, Line3>(&mData, &here, line);
}
bool cTranslatedSphereBound::DoIntersectsSegment(const Segment3 *segment)
{
    return TestPair<ShapeKind::Sphere, Placement::Translated, Segment3>(&mData, &mData.centre, segment);
}
bool cTranslatedSphereBound::DoIntersectsRay(const Ray3 *ray)
{
    return TestPair<ShapeKind::Sphere, Placement::Translated, Ray3>(&mData, &mData.centre, ray);
}
bool cTranslatedSphereBound::DoIntersectsLine(const Line3 *line)
{
    return TestPair<ShapeKind::Sphere, Placement::Translated, Line3>(&mData, &mData.centre, line);
}
bool cTranslatedSphereBound::DoFirstHitSegmentIn(float *t, const AffineTransform3 *frame, const Segment3 *segment)
{
    const AffineTransform3 here = Placed(*frame, mData.centre);
    return FirstHitPair<ShapeKind::Sphere, Placement::InFrame, Segment3>(t, &mData, &here, segment);
}
bool cTranslatedSphereBound::DoFirstHitRayIn(float *t, const AffineTransform3 *frame, const Ray3 *ray)
{
    const AffineTransform3 here = Placed(*frame, mData.centre);
    return FirstHitPair<ShapeKind::Sphere, Placement::InFrame, Ray3>(t, &mData, &here, ray);
}
bool cTranslatedSphereBound::DoFirstHitLineIn(float *t, const AffineTransform3 *frame, const Line3 *line)
{
    const AffineTransform3 here = Placed(*frame, mData.centre);
    return FirstHitPair<ShapeKind::Sphere, Placement::InFrame, Line3>(t, &mData, &here, line);
}
bool cTranslatedSphereBound::DoFirstHitSegment(float *t, const Segment3 *segment)
{
    return FirstHitPair<ShapeKind::Sphere, Placement::Translated, Segment3>(t, &mData, &mData.centre, segment);
}
bool cTranslatedSphereBound::DoFirstHitRay(float *t, const Ray3 *ray)
{
    return FirstHitPair<ShapeKind::Sphere, Placement::Translated, Ray3>(t, &mData, &mData.centre, ray);
}
bool cTranslatedSphereBound::DoFirstHitLine(float *t, const Line3 *line)
{
    return FirstHitPair<ShapeKind::Sphere, Placement::Translated, Line3>(t, &mData, &mData.centre, line);
}

// ---- boxes ----

BoundBox *cTranslatedSphereBound::DoGetBoundBoxIn(BoundBox *out, const AffineTransform3 *frame)
{
    const AffineTransform3 here = Placed(*frame, mData.centre);
    return mData.BoxInFrame(out, &here);
}
BoundBox *cTranslatedSphereBound::DoGetBoundBoxRigid(BoundBox *out, const RigidTransform3 *rigid)
{
    Vec3 p = mData.centre;
    rigid->TransformPoint(&p);
    return mData.BoxAt(out, &p);
}
BoundBox *cTranslatedSphereBound::DoGetBoundBox(BoundBox *out)
{
    return mData.BoxAt(out, &mData.centre);
}

// ---- support points ----

Vec3 *cTranslatedSphereBound::DoSupportPointIn(Vec3 *out, const AffineTransform3 *frame, const Vec3 *direction)
{
    const AffineTransform3 here = Placed(*frame, mData.centre);
    Vec3 local, support;
    static_cast<const ConvexFrame &>(here).TransposedVectorTo(&local, direction);
    mData.SupportAlong(&support, &local);
    const_cast<AffineTransform3 &>(here).TransformPoint(&out->x, &support.x);
    return out;
}
Vec3 *cTranslatedSphereBound::DoSupportPointRigid(Vec3 *out, const RigidTransform3 *rigid, const Vec3 *direction)
{
    Vec3 p = mData.centre;
    rigid->TransformPoint(&p);
    RigidTransform3 here;
    here.translation = p;
    for (int i = 0; i < 4; ++i) here.rotation[i] = rigid->rotation[i];
    Vec3 local = *direction;
    here.InverseTransformVector(&local);
    Vec3 support;
    mData.SupportAlong(&support, &local);
    here.TransformPoint(&support);
    *out = support;
    return out;
}
Vec3 *cTranslatedSphereBound::DoSupportPoint(Vec3 *out, const Vec3 *direction)
{
    Vec3 support;
    mData.SupportAlong(&support, direction);
    *out = {AddS(support.x, mData.centre.x), AddS(support.y, mData.centre.y), AddS(support.z, mData.centre.z)};
    return out;
}

cTranslatedSphereShape *__cdecl cTranslatedSphereShape::Create()
{
    return new cTranslatedSphereShape;
}

} // namespace nGZCompGeom
