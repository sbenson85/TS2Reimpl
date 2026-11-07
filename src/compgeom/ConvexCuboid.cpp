#include "compgeom/ConvexInternal.h"
#include "compgeom/SeparatingAxes.h"
#include "compgeom/ShapePairs.h"

// The translated and oriented cuboids, cConvexT<cITranslatedCuboidShape> and cConvexT<cIOrientedCuboidShape> (lane PP,
// scenegraph G0b step 4). Data: the half extents at +4, then the position (translated) or the rigid transform (oriented:
// translation, quaternion). The translated one's "In" slots place the cuboid at its position's image under the frame; the
// oriented one's compose the frame of its own rotation and translation with the carrying frame. The other shape's slots
// meet by the helpers (CuboidsOverlap, SetMeetsCuboidTest, the sphere reach), the linear ones by the pair tests.
namespace nGZCompGeom {

using namespace convex;

namespace {

// The oriented cuboid's own frame: the unit frame at its translation turned to its rotation (flags 3).
AffineTransform3 OrientedBox(const RigidTransform3 &rigid)
{
    AffineTransform3 box = UnitFrameAt(rigid.translation);
    static_cast<FrameMath &>(box).SetRotation(rigid.rotation);
    return box;
}
// That frame followed by `frame`: the cuboid carried by `frame`.
AffineTransform3 OrientedIn(const RigidTransform3 &rigid, const AffineTransform3 &frame)
{
    AffineTransform3 box = OrientedBox(rigid);
    static_cast<FramePlacement &>(box).Compose(static_cast<const FramePlacement *>(&frame));
    return box;
}

// The corner of the cuboid along `direction`.
Vec3 CornerAlong(const Cuboid &cuboid, const Vec3 &direction)
{
    Vec3 corner;
    static_cast<const ConvexCuboid &>(cuboid).SupportCorner(&corner, &direction);
    return corner;
}

} // namespace

// ================= the translated cuboid =================

// ---- cones are not met by a cuboid here ----
bool cTranslatedCuboidBound::DoIntersectsOrientedConeIn(const AffineTransform3 *, const OrientedCone *) { return false; }
bool cTranslatedCuboidBound::DoIntersectsTranslatedConeIn(const AffineTransform3 *, const TranslatedCone *) { return false; }
bool cTranslatedCuboidBound::DoIntersectsConeAt(const Cone *, const AffineTransform3 *) { return false; }
bool cTranslatedCuboidBound::DoIntersectsConeInAt(const AffineTransform3 *, const Cone *, const AffineTransform3 *)
{
    return false;
}

// ---- the other shape in world space, this cuboid placed by `frame` ----

bool cTranslatedCuboidBound::DoIntersectsTranslatedSphereIn(const AffineTransform3 *frame, const TranslatedSphere *sphere)
{
    const AffineTransform3 here = Placed(*frame, mData.position);
    const AffineTransform3 there = UnitFrameAt(sphere->centre);
    const float gap = BoxGap(mData.cuboid, here, OffsetOf(there));
    return SphereReaches(sphere->radius, ScaleOf(there), gap);
}

bool cTranslatedCuboidBound::DoIntersectsOrientedCuboidIn(const AffineTransform3 *frame, const OrientedCuboid *cuboid)
{
    const AffineTransform3 here = Placed(*frame, mData.position);
    const AffineTransform3 box = OrientedBox(cuboid->frame);
    return CuboidsOverlap(&mData.cuboid, &here, &cuboid->cuboid, &box);
}

bool cTranslatedCuboidBound::DoIntersectsTranslatedCuboidIn(const AffineTransform3 *frame, const TranslatedCuboid *cuboid)
{
    const AffineTransform3 here = Placed(*frame, mData.position);
    const AffineTransform3 box = UnitFrameAt(cuboid->position);
    return CuboidsOverlap(&mData.cuboid, &here, &cuboid->cuboid, &box);
}

bool cTranslatedCuboidBound::DoIntersectsBoundIn(const AffineTransform3 *frame, cIBound *bound)
{
    (void)frame;
    const AffineTransform3 at = UnitFrameAt(mData.position);
    return bound->IntersectsCuboidAt(&mData.cuboid, &at);
}

// ---- this cuboid as it is, the other placed by `frame` ----

bool cTranslatedCuboidBound::DoIntersectsTriangleSetAt(const TriangleSet *set, const AffineTransform3 *frame)
{
    const AffineTransform3 box = UnitFrameAt(mData.position);
    return SetMeetsCuboidTest(set, frame, &mData.cuboid, &box);
}

bool cTranslatedCuboidBound::DoIntersectsSphereAt(const Sphere *sphere, const AffineTransform3 *frame)
{
    const AffineTransform3 box = UnitFrameAt(mData.position);
    const float gap = BoxGap(mData.cuboid, box, OffsetOf(*frame));
    return SphereReaches(sphere->radius, ScaleOf(*frame), gap);
}

bool cTranslatedCuboidBound::DoIntersectsCuboidAt(const Cuboid *cuboid, const AffineTransform3 *frame)
{
    const AffineTransform3 box = UnitFrameAt(mData.position);
    return CuboidsOverlap(&mData.cuboid, &box, cuboid, frame);
}

bool cTranslatedCuboidBound::DoIntersectsBoundAt(cIBound *bound, const AffineTransform3 *frame)
{
    const AffineTransform3 box = UnitFrameAt(mData.position);
    return bound->IntersectsCuboidInAt(frame, &mData.cuboid, &box);
}

// ---- both placed ----

bool cTranslatedCuboidBound::DoIntersectsTriangleSetInAt(const AffineTransform3 *frame, const TriangleSet *set,
                                                         const AffineTransform3 *otherFrame)
{
    const AffineTransform3 here = Placed(*frame, mData.position);
    return SetMeetsCuboidTest(set, otherFrame, &mData.cuboid, &here);
}

bool cTranslatedCuboidBound::DoIntersectsSphereInAt(const AffineTransform3 *frame, const Sphere *sphere,
                                                    const AffineTransform3 *otherFrame)
{
    const AffineTransform3 here = Placed(*frame, mData.position);
    const float gap = BoxGap(mData.cuboid, here, OffsetOf(*otherFrame));
    return SphereReaches(sphere->radius, ScaleOf(*otherFrame), gap);
}

bool cTranslatedCuboidBound::DoIntersectsCuboidInAt(const AffineTransform3 *frame, const Cuboid *cuboid,
                                                    const AffineTransform3 *otherFrame)
{
    const AffineTransform3 here = Placed(*frame, mData.position);
    return CuboidsOverlap(&mData.cuboid, &here, cuboid, otherFrame);
}

bool cTranslatedCuboidBound::DoIntersectsBoundInAt(const AffineTransform3 *frame, cIBound *bound,
                                                   const AffineTransform3 *otherFrame)
{
    const AffineTransform3 here = Placed(*frame, mData.position);
    return bound->IntersectsCuboidInAt(otherFrame, &mData.cuboid, &here);
}

// ---- the linear shapes ----

bool cTranslatedCuboidBound::DoIntersectsSegmentIn(const AffineTransform3 *frame, const Segment3 *segment)
{
    const AffineTransform3 here = Placed(*frame, mData.position);
    return TestPair<ShapeKind::Cuboid, Placement::InFrame, Segment3>(&mData.cuboid, &here, segment);
}
bool cTranslatedCuboidBound::DoIntersectsRayIn(const AffineTransform3 *frame, const Ray3 *ray)
{
    const AffineTransform3 here = Placed(*frame, mData.position);
    return TestPair<ShapeKind::Cuboid, Placement::InFrame, Ray3>(&mData.cuboid, &here, ray);
}
bool cTranslatedCuboidBound::DoIntersectsLineIn(const AffineTransform3 *frame, const Line3 *line)
{
    const AffineTransform3 here = Placed(*frame, mData.position);
    return TestPair<ShapeKind::Cuboid, Placement::InFrame, Line3>(&mData.cuboid, &here, line);
}
bool cTranslatedCuboidBound::DoIntersectsSegment(const Segment3 *segment)
{
    return TestPair<ShapeKind::Cuboid, Placement::Translated, Segment3>(&mData.cuboid, &mData.position, segment);
}
bool cTranslatedCuboidBound::DoIntersectsRay(const Ray3 *ray)
{
    return TestPair<ShapeKind::Cuboid, Placement::Translated, Ray3>(&mData.cuboid, &mData.position, ray);
}
bool cTranslatedCuboidBound::DoIntersectsLine(const Line3 *line)
{
    return TestPair<ShapeKind::Cuboid, Placement::Translated, Line3>(&mData.cuboid, &mData.position, line);
}
bool cTranslatedCuboidBound::DoFirstHitSegmentIn(float *t, const AffineTransform3 *frame, const Segment3 *segment)
{
    const AffineTransform3 here = Placed(*frame, mData.position);
    return FirstHitPair<ShapeKind::Cuboid, Placement::InFrame, Segment3>(t, &mData.cuboid, &here, segment);
}
bool cTranslatedCuboidBound::DoFirstHitRayIn(float *t, const AffineTransform3 *frame, const Ray3 *ray)
{
    const AffineTransform3 here = Placed(*frame, mData.position);
    return FirstHitPair<ShapeKind::Cuboid, Placement::InFrame, Ray3>(t, &mData.cuboid, &here, ray);
}
bool cTranslatedCuboidBound::DoFirstHitLineIn(float *t, const AffineTransform3 *frame, const Line3 *line)
{
    const AffineTransform3 here = Placed(*frame, mData.position);
    return FirstHitPair<ShapeKind::Cuboid, Placement::InFrame, Line3>(t, &mData.cuboid, &here, line);
}
bool cTranslatedCuboidBound::DoFirstHitSegment(float *t, const Segment3 *segment)
{
    return FirstHitPair<ShapeKind::Cuboid, Placement::Translated, Segment3>(t, &mData.cuboid, &mData.position, segment);
}
bool cTranslatedCuboidBound::DoFirstHitRay(float *t, const Ray3 *ray)
{
    return FirstHitPair<ShapeKind::Cuboid, Placement::Translated, Ray3>(t, &mData.cuboid, &mData.position, ray);
}
bool cTranslatedCuboidBound::DoFirstHitLine(float *t, const Line3 *line)
{
    return FirstHitPair<ShapeKind::Cuboid, Placement::Translated, Line3>(t, &mData.cuboid, &mData.position, line);
}

// ---- boxes ----

BoundBox *cTranslatedCuboidBound::DoGetBoundBoxIn(BoundBox *out, const AffineTransform3 *frame)
{
    const AffineTransform3 here = Placed(*frame, mData.position);
    *out = mData.cuboid.BoxInFrame(here);
    return out;
}
BoundBox *cTranslatedCuboidBound::DoGetBoundBoxRigid(BoundBox *out, const RigidTransform3 *rigid)
{
    Vec3 p = mData.position;
    rigid->TransformPoint(&p);
    RigidTransform3 here;
    here.translation = p;
    for (int i = 0; i < 4; ++i) here.rotation[i] = rigid->rotation[i];
    *out = mData.cuboid.BoxInRigid(here);
    return out;
}
BoundBox *cTranslatedCuboidBound::DoGetBoundBox(BoundBox *out)
{
    *out = mData.cuboid.BoxAt(mData.position);
    return out;
}

// ---- support points ----

Vec3 *cTranslatedCuboidBound::DoSupportPointIn(Vec3 *out, const AffineTransform3 *frame, const Vec3 *direction)
{
    const AffineTransform3 here = Placed(*frame, mData.position);
    Vec3 local;
    static_cast<const ConvexFrame &>(here).TransposedVectorTo(&local, direction);
    Vec3 corner = CornerAlong(mData.cuboid, local);
    const_cast<AffineTransform3 &>(here).TransformPoint(&out->x, &corner.x);
    return out;
}
Vec3 *cTranslatedCuboidBound::DoSupportPointRigid(Vec3 *out, const RigidTransform3 *rigid, const Vec3 *direction)
{
    Vec3 p = mData.position;
    rigid->TransformPoint(&p);
    RigidTransform3 here;
    here.translation = p;
    for (int i = 0; i < 4; ++i) here.rotation[i] = rigid->rotation[i];
    Vec3 local = *direction;
    here.InverseTransformVector(&local);
    Vec3 corner = CornerAlong(mData.cuboid, local);
    here.TransformPoint(&corner);
    *out = corner;
    return out;
}
Vec3 *cTranslatedCuboidBound::DoSupportPoint(Vec3 *out, const Vec3 *direction)
{
    const Vec3 corner = CornerAlong(mData.cuboid, *direction);
    // The operand order is the original's, per axis (it shows when both are NaN): x is corner first, y and z position first.
    *out = {AddS(corner.x, mData.position.x), AddS(mData.position.y, corner.y), AddS(mData.position.z, corner.z)};
    return out;
}

cTranslatedCuboidShape::cTranslatedCuboidShape(const Vec3 *extents, const Vec3 *centre)
{
    // 0x18fc10: each extent times 0.5 (`mulss extent, 0.5`: a NaN extent keeps its payload, quieted); the centre as three words.
    const Vec3 e = *extents;
    mData.cuboid.half = {e.x != e.x ? QuietNaN(e.x) : e.x * 0.5f, e.y != e.y ? QuietNaN(e.y) : e.y * 0.5f,
                         e.z != e.z ? QuietNaN(e.z) : e.z * 0.5f};
    std::memcpy(&mData.position, centre, sizeof(Vec3));
}

cTranslatedCuboidShape *__cdecl cTranslatedCuboidShape::Create()
{
    return new cTranslatedCuboidShape;
}

// ================= the oriented cuboid =================

// ---- cones are not met by a cuboid here ----
bool cOrientedCuboidBound::DoIntersectsOrientedConeIn(const AffineTransform3 *, const OrientedCone *) { return false; }
bool cOrientedCuboidBound::DoIntersectsTranslatedConeIn(const AffineTransform3 *, const TranslatedCone *) { return false; }
bool cOrientedCuboidBound::DoIntersectsConeAt(const Cone *, const AffineTransform3 *) { return false; }
bool cOrientedCuboidBound::DoIntersectsConeInAt(const AffineTransform3 *, const Cone *, const AffineTransform3 *)
{
    return false;
}

// ---- the other shape in world space, this cuboid carried by `frame` ----

bool cOrientedCuboidBound::DoIntersectsTranslatedSphereIn(const AffineTransform3 *frame, const TranslatedSphere *sphere)
{
    const AffineTransform3 here = OrientedIn(mData.frame, *frame);
    const AffineTransform3 there = UnitFrameAt(sphere->centre);
    const float gap = BoxGap(mData.cuboid, here, OffsetOf(there));
    return SphereReaches(sphere->radius, ScaleOf(there), gap);
}

bool cOrientedCuboidBound::DoIntersectsOrientedCuboidIn(const AffineTransform3 *frame, const OrientedCuboid *cuboid)
{
    const AffineTransform3 here = OrientedIn(mData.frame, *frame);
    const AffineTransform3 box = OrientedBox(cuboid->frame);
    return CuboidsOverlap(&mData.cuboid, &here, &cuboid->cuboid, &box);
}

bool cOrientedCuboidBound::DoIntersectsTranslatedCuboidIn(const AffineTransform3 *frame, const TranslatedCuboid *cuboid)
{
    const AffineTransform3 here = OrientedIn(mData.frame, *frame);
    const AffineTransform3 box = UnitFrameAt(cuboid->position);
    return CuboidsOverlap(&mData.cuboid, &here, &cuboid->cuboid, &box);
}

bool cOrientedCuboidBound::DoIntersectsBoundIn(const AffineTransform3 *frame, cIBound *bound)
{
    (void)frame;
    const AffineTransform3 at = OrientedBox(mData.frame);
    return bound->IntersectsCuboidAt(&mData.cuboid, &at);
}

// ---- this cuboid as it is, the other placed by `frame` ----

bool cOrientedCuboidBound::DoIntersectsTriangleSetAt(const TriangleSet *set, const AffineTransform3 *frame)
{
    const AffineTransform3 box = OrientedBox(mData.frame);
    return SetMeetsCuboidTest(set, frame, &mData.cuboid, &box);
}

bool cOrientedCuboidBound::DoIntersectsSphereAt(const Sphere *sphere, const AffineTransform3 *frame)
{
    const AffineTransform3 box = OrientedBox(mData.frame);
    const float gap = BoxGap(mData.cuboid, box, OffsetOf(*frame));
    return SphereReaches(sphere->radius, ScaleOf(*frame), gap);
}

bool cOrientedCuboidBound::DoIntersectsCuboidAt(const Cuboid *cuboid, const AffineTransform3 *frame)
{
    const AffineTransform3 box = OrientedBox(mData.frame);
    return CuboidsOverlap(&mData.cuboid, &box, cuboid, frame);
}

bool cOrientedCuboidBound::DoIntersectsBoundAt(cIBound *bound, const AffineTransform3 *frame)
{
    const AffineTransform3 box = OrientedBox(mData.frame);
    return bound->IntersectsCuboidInAt(frame, &mData.cuboid, &box);
}

// ---- both placed ----

bool cOrientedCuboidBound::DoIntersectsTriangleSetInAt(const AffineTransform3 *frame, const TriangleSet *set,
                                                       const AffineTransform3 *otherFrame)
{
    const AffineTransform3 here = OrientedIn(mData.frame, *frame);
    return SetMeetsCuboidTest(set, otherFrame, &mData.cuboid, &here);
}

bool cOrientedCuboidBound::DoIntersectsSphereInAt(const AffineTransform3 *frame, const Sphere *sphere,
                                                  const AffineTransform3 *otherFrame)
{
    const AffineTransform3 here = OrientedIn(mData.frame, *frame);
    const float gap = BoxGap(mData.cuboid, here, OffsetOf(*otherFrame));
    return SphereReaches(sphere->radius, ScaleOf(*otherFrame), gap);
}

bool cOrientedCuboidBound::DoIntersectsCuboidInAt(const AffineTransform3 *frame, const Cuboid *cuboid,
                                                  const AffineTransform3 *otherFrame)
{
    const AffineTransform3 here = OrientedIn(mData.frame, *frame);
    return CuboidsOverlap(&mData.cuboid, &here, cuboid, otherFrame);
}

bool cOrientedCuboidBound::DoIntersectsBoundInAt(const AffineTransform3 *frame, cIBound *bound,
                                                 const AffineTransform3 *otherFrame)
{
    const AffineTransform3 here = OrientedIn(mData.frame, *frame);
    return bound->IntersectsCuboidInAt(otherFrame, &mData.cuboid, &here);
}

// ---- the linear shapes ----

bool cOrientedCuboidBound::DoIntersectsSegmentIn(const AffineTransform3 *frame, const Segment3 *segment)
{
    const AffineTransform3 here = OrientedIn(mData.frame, *frame);
    return TestPair<ShapeKind::Cuboid, Placement::InFrame, Segment3>(&mData, &here, segment);
}
bool cOrientedCuboidBound::DoIntersectsRayIn(const AffineTransform3 *frame, const Ray3 *ray)
{
    const AffineTransform3 here = OrientedIn(mData.frame, *frame);
    return TestPair<ShapeKind::Cuboid, Placement::InFrame, Ray3>(&mData, &here, ray);
}
bool cOrientedCuboidBound::DoIntersectsLineIn(const AffineTransform3 *frame, const Line3 *line)
{
    const AffineTransform3 here = OrientedIn(mData.frame, *frame);
    return TestPair<ShapeKind::Cuboid, Placement::InFrame, Line3>(&mData, &here, line);
}
bool cOrientedCuboidBound::DoIntersectsSegment(const Segment3 *segment)
{
    return TestPair<ShapeKind::Cuboid, Placement::Oriented, Segment3>(&mData, &mData.frame, segment);
}
bool cOrientedCuboidBound::DoIntersectsRay(const Ray3 *ray)
{
    return TestPair<ShapeKind::Cuboid, Placement::Oriented, Ray3>(&mData, &mData.frame, ray);
}
bool cOrientedCuboidBound::DoIntersectsLine(const Line3 *line)
{
    return TestPair<ShapeKind::Cuboid, Placement::Oriented, Line3>(&mData, &mData.frame, line);
}
bool cOrientedCuboidBound::DoFirstHitSegmentIn(float *t, const AffineTransform3 *frame, const Segment3 *segment)
{
    const AffineTransform3 here = OrientedIn(mData.frame, *frame);
    return FirstHitPair<ShapeKind::Cuboid, Placement::InFrame, Segment3>(t, &mData, &here, segment);
}
bool cOrientedCuboidBound::DoFirstHitRayIn(float *t, const AffineTransform3 *frame, const Ray3 *ray)
{
    const AffineTransform3 here = OrientedIn(mData.frame, *frame);
    return FirstHitPair<ShapeKind::Cuboid, Placement::InFrame, Ray3>(t, &mData, &here, ray);
}
bool cOrientedCuboidBound::DoFirstHitLineIn(float *t, const AffineTransform3 *frame, const Line3 *line)
{
    const AffineTransform3 here = OrientedIn(mData.frame, *frame);
    return FirstHitPair<ShapeKind::Cuboid, Placement::InFrame, Line3>(t, &mData, &here, line);
}
bool cOrientedCuboidBound::DoFirstHitSegment(float *t, const Segment3 *segment)
{
    return FirstHitPair<ShapeKind::Cuboid, Placement::Oriented, Segment3>(t, &mData, &mData.frame, segment);
}
bool cOrientedCuboidBound::DoFirstHitRay(float *t, const Ray3 *ray)
{
    return FirstHitPair<ShapeKind::Cuboid, Placement::Oriented, Ray3>(t, &mData, &mData.frame, ray);
}
bool cOrientedCuboidBound::DoFirstHitLine(float *t, const Line3 *line)
{
    return FirstHitPair<ShapeKind::Cuboid, Placement::Oriented, Line3>(t, &mData, &mData.frame, line);
}

// ---- boxes ----

BoundBox *cOrientedCuboidBound::DoGetBoundBoxIn(BoundBox *out, const AffineTransform3 *frame)
{
    const AffineTransform3 here = OrientedIn(mData.frame, *frame);
    *out = mData.cuboid.BoxInFrame(here);
    return out;
}
BoundBox *cOrientedCuboidBound::DoGetBoundBoxRigid(BoundBox *out, const RigidTransform3 *rigid)
{
    RigidTransform3 here = mData.frame;
    here.Compose(rigid);
    *out = mData.cuboid.BoxInRigid(here);
    return out;
}
BoundBox *cOrientedCuboidBound::DoGetBoundBox(BoundBox *out)
{
    *out = mData.cuboid.BoxInRigid(mData.frame);
    return out;
}

// ---- support points ----

Vec3 *cOrientedCuboidBound::DoSupportPointIn(Vec3 *out, const AffineTransform3 *frame, const Vec3 *direction)
{
    const AffineTransform3 here = OrientedIn(mData.frame, *frame);
    Vec3 local;
    static_cast<const ConvexFrame &>(here).TransposedVectorTo(&local, direction);
    Vec3 corner = CornerAlong(mData.cuboid, local);
    const_cast<AffineTransform3 &>(here).TransformPoint(&out->x, &corner.x);
    return out;
}
Vec3 *cOrientedCuboidBound::DoSupportPointRigid(Vec3 *out, const RigidTransform3 *rigid, const Vec3 *direction)
{
    RigidTransform3 here = mData.frame;
    here.Compose(rigid);
    Vec3 local = *direction;
    here.InverseTransformVector(&local);
    Vec3 corner = CornerAlong(mData.cuboid, local);
    here.TransformPoint(&corner);
    *out = corner;
    return out;
}
Vec3 *cOrientedCuboidBound::DoSupportPoint(Vec3 *out, const Vec3 *direction)
{
    Vec3 local = *direction;
    mData.frame.InverseTransformVector(&local);
    Vec3 corner = CornerAlong(mData.cuboid, local);
    mData.frame.TransformPoint(&corner);
    *out = corner;
    return out;
}

cOrientedCuboidShape *__cdecl cOrientedCuboidShape::Create()
{
    return new cOrientedCuboidShape;
}

} // namespace nGZCompGeom
