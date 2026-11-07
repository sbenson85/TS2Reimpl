#include "compgeom/ConvexInternal.h"
#include "compgeom/SeparatingAxes.h"
#include "compgeom/ShapePairs.h"

// The translated cone, cConvexT<cITranslatedConeShape> (lane PR, scenegraph G0b step 4). Data: the radius at +4, the half
// height at +8, the position at +0xc. A cone meets only the other bound (slot 10, 15, 20, which hand the cone to the other
// shape's "cone" slots) and the linear shapes; every other pairing returns false (the original builds frames and discards
// them). "In" slots carry the cone by a frame: its position goes through the frame (Q) and the cone is tested as the frame
// placed at Q (the unit frame at the position when no frame is given).
namespace nGZCompGeom {

using namespace convex;

// ---- shapes a cone is never reported to meet ----
bool cTranslatedConeBound::DoIntersectsOrientedConeIn(const AffineTransform3 *, const OrientedCone *) { return false; }
bool cTranslatedConeBound::DoIntersectsTranslatedConeIn(const AffineTransform3 *, const TranslatedCone *) { return false; }
bool cTranslatedConeBound::DoIntersectsTranslatedSphereIn(const AffineTransform3 *, const TranslatedSphere *) { return false; }
bool cTranslatedConeBound::DoIntersectsOrientedCuboidIn(const AffineTransform3 *, const OrientedCuboid *) { return false; }
bool cTranslatedConeBound::DoIntersectsTranslatedCuboidIn(const AffineTransform3 *, const TranslatedCuboid *) { return false; }
bool cTranslatedConeBound::DoIntersectsTriangleSetAt(const TriangleSet *, const AffineTransform3 *) { return false; }
bool cTranslatedConeBound::DoIntersectsConeAt(const Cone *, const AffineTransform3 *) { return false; }
bool cTranslatedConeBound::DoIntersectsSphereAt(const Sphere *, const AffineTransform3 *) { return false; }
bool cTranslatedConeBound::DoIntersectsCuboidAt(const Cuboid *, const AffineTransform3 *) { return false; }
bool cTranslatedConeBound::DoIntersectsTriangleSetInAt(const AffineTransform3 *, const TriangleSet *,
                                                       const AffineTransform3 *)
{
    return false;
}
bool cTranslatedConeBound::DoIntersectsConeInAt(const AffineTransform3 *, const Cone *, const AffineTransform3 *)
{
    return false;
}
bool cTranslatedConeBound::DoIntersectsSphereInAt(const AffineTransform3 *, const Sphere *, const AffineTransform3 *)
{
    return false;
}
bool cTranslatedConeBound::DoIntersectsCuboidInAt(const AffineTransform3 *, const Cuboid *, const AffineTransform3 *)
{
    return false;
}

// ---- the other bound ----

bool cTranslatedConeBound::DoIntersectsBoundIn(const AffineTransform3 *frame, cIBound *bound)
{
    (void)frame;
    const AffineTransform3 at = UnitFrameAt(mData.position);
    return bound->IntersectsConeAt(&mData, &at);
}

bool cTranslatedConeBound::DoIntersectsBoundAt(cIBound *bound, const AffineTransform3 *frame)
{
    const AffineTransform3 at = UnitFrameAt(mData.position);
    return bound->IntersectsConeInAt(frame, &mData, &at);
}

bool cTranslatedConeBound::DoIntersectsBoundInAt(const AffineTransform3 *frame, cIBound *bound,
                                                 const AffineTransform3 *otherFrame)
{
    const AffineTransform3 here = Placed(*frame, mData.position);
    return bound->IntersectsConeInAt(otherFrame, &mData, &here);
}

// ---- the linear shapes ----

bool cTranslatedConeBound::DoIntersectsSegmentIn(const AffineTransform3 *frame, const Segment3 *segment)
{
    const AffineTransform3 here = Placed(*frame, mData.position);
    return TestPair<ShapeKind::Cone, Placement::InFrame, Segment3>(&mData, &here, segment);
}
bool cTranslatedConeBound::DoIntersectsRayIn(const AffineTransform3 *frame, const Ray3 *ray)
{
    const AffineTransform3 here = Placed(*frame, mData.position);
    return TestPair<ShapeKind::Cone, Placement::InFrame, Ray3>(&mData, &here, ray);
}
bool cTranslatedConeBound::DoIntersectsLineIn(const AffineTransform3 *frame, const Line3 *line)
{
    const AffineTransform3 here = Placed(*frame, mData.position);
    return TestPair<ShapeKind::Cone, Placement::InFrame, Line3>(&mData, &here, line);
}
bool cTranslatedConeBound::DoIntersectsSegment(const Segment3 *segment)
{
    return TestPair<ShapeKind::Cone, Placement::Translated, Segment3>(&mData, &mData.position, segment);
}
bool cTranslatedConeBound::DoIntersectsRay(const Ray3 *ray)
{
    return TestPair<ShapeKind::Cone, Placement::Translated, Ray3>(&mData, &mData.position, ray);
}
bool cTranslatedConeBound::DoIntersectsLine(const Line3 *line)
{
    return TestPair<ShapeKind::Cone, Placement::Translated, Line3>(&mData, &mData.position, line);
}
bool cTranslatedConeBound::DoFirstHitSegmentIn(float *t, const AffineTransform3 *frame, const Segment3 *segment)
{
    const AffineTransform3 here = Placed(*frame, mData.position);
    return FirstHitPair<ShapeKind::Cone, Placement::InFrame, Segment3>(t, &mData, &here, segment);
}
bool cTranslatedConeBound::DoFirstHitRayIn(float *t, const AffineTransform3 *frame, const Ray3 *ray)
{
    const AffineTransform3 here = Placed(*frame, mData.position);
    return FirstHitPair<ShapeKind::Cone, Placement::InFrame, Ray3>(t, &mData, &here, ray);
}
bool cTranslatedConeBound::DoFirstHitLineIn(float *t, const AffineTransform3 *frame, const Line3 *line)
{
    const AffineTransform3 here = Placed(*frame, mData.position);
    return FirstHitPair<ShapeKind::Cone, Placement::InFrame, Line3>(t, &mData, &here, line);
}
bool cTranslatedConeBound::DoFirstHitSegment(float *t, const Segment3 *segment)
{
    return FirstHitPair<ShapeKind::Cone, Placement::Translated, Segment3>(t, &mData, &mData.position, segment);
}
bool cTranslatedConeBound::DoFirstHitRay(float *t, const Ray3 *ray)
{
    return FirstHitPair<ShapeKind::Cone, Placement::Translated, Ray3>(t, &mData, &mData.position, ray);
}
bool cTranslatedConeBound::DoFirstHitLine(float *t, const Line3 *line)
{
    return FirstHitPair<ShapeKind::Cone, Placement::Translated, Line3>(t, &mData, &mData.position, line);
}

// ---- boxes ----

BoundBox *cTranslatedConeBound::DoGetBoundBoxIn(BoundBox *out, const AffineTransform3 *frame)
{
    const AffineTransform3 here = Placed(*frame, mData.position);
    return mData.BoxInFrame(out, &here);
}
BoundBox *cTranslatedConeBound::DoGetBoundBoxRigid(BoundBox *out, const RigidTransform3 *rigid)
{
    Vec3 p = mData.position;
    rigid->TransformPoint(&p);
    RigidTransform3 here;
    here.translation = p;
    for (int i = 0; i < 4; ++i) here.rotation[i] = rigid->rotation[i];
    return mData.BoxInRigid(out, &here);
}
BoundBox *cTranslatedConeBound::DoGetBoundBox(BoundBox *out)
{
    return mData.BoxAt(out, &mData.position);
}

// ---- support points ----

Vec3 *cTranslatedConeBound::DoSupportPointIn(Vec3 *out, const AffineTransform3 *frame, const Vec3 *direction)
{
    const AffineTransform3 here = Placed(*frame, mData.position);
    Vec3 local, support;
    static_cast<const ConvexFrame &>(here).TransposedVectorTo(&local, direction);
    mData.SupportAlong(&support, &local);
    const_cast<AffineTransform3 &>(here).TransformPoint(&out->x, &support.x);
    return out;
}
Vec3 *cTranslatedConeBound::DoSupportPointRigid(Vec3 *out, const RigidTransform3 *rigid, const Vec3 *direction)
{
    Vec3 p = mData.position;
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
Vec3 *cTranslatedConeBound::DoSupportPoint(Vec3 *out, const Vec3 *direction)
{
    Vec3 support;
    mData.SupportAlong(&support, direction);
    const Vec3 &pos = mData.position;
    // addss: x is support + position, y and z are position + support (the original's operand order per axis).
    *out = {AddS(support.x, pos.x), AddS(pos.y, support.y), AddS(pos.z, support.z)};
    return out;
}

cTranslatedConeShape *__cdecl cTranslatedConeShape::Create()
{
    return new cTranslatedConeShape;
}

// ================= the oriented cone (lane PS) =================
// Data: the radius at +4, the half height at +8, then the rigid transform (translation at +0xc, quaternion at +0x18). As the
// translated cone it meets only the other bound and the linear shapes; the other pairings return false (the original builds
// frames and discards them). Its "In" slots compose the frame of its own rotation and translation with the carrying frame.

namespace {

// The oriented cone's own frame: the unit frame at its translation turned to its rotation (flags 3).
AffineTransform3 OrientedCone_Frame(const RigidTransform3 &rigid)
{
    AffineTransform3 cone = UnitFrameAt(rigid.translation);
    static_cast<FrameMath &>(cone).SetRotation(rigid.rotation);
    return cone;
}
// That frame followed by `frame`: the cone carried by `frame`.
AffineTransform3 OrientedCone_In(const RigidTransform3 &rigid, const AffineTransform3 &frame)
{
    AffineTransform3 cone = OrientedCone_Frame(rigid);
    static_cast<FramePlacement &>(cone).Compose(static_cast<const FramePlacement *>(&frame));
    return cone;
}

} // namespace

// ---- shapes an oriented cone is never reported to meet ----
bool cOrientedConeBound::DoIntersectsOrientedConeIn(const AffineTransform3 *, const OrientedCone *) { return false; }
bool cOrientedConeBound::DoIntersectsTranslatedConeIn(const AffineTransform3 *, const TranslatedCone *) { return false; }
bool cOrientedConeBound::DoIntersectsTranslatedSphereIn(const AffineTransform3 *, const TranslatedSphere *) { return false; }
bool cOrientedConeBound::DoIntersectsOrientedCuboidIn(const AffineTransform3 *, const OrientedCuboid *) { return false; }
bool cOrientedConeBound::DoIntersectsTranslatedCuboidIn(const AffineTransform3 *, const TranslatedCuboid *) { return false; }
bool cOrientedConeBound::DoIntersectsTriangleSetAt(const TriangleSet *, const AffineTransform3 *) { return false; }
bool cOrientedConeBound::DoIntersectsConeAt(const Cone *, const AffineTransform3 *) { return false; }
bool cOrientedConeBound::DoIntersectsSphereAt(const Sphere *, const AffineTransform3 *) { return false; }
bool cOrientedConeBound::DoIntersectsCuboidAt(const Cuboid *, const AffineTransform3 *) { return false; }
bool cOrientedConeBound::DoIntersectsTriangleSetInAt(const AffineTransform3 *, const TriangleSet *, const AffineTransform3 *)
{
    return false;
}
bool cOrientedConeBound::DoIntersectsConeInAt(const AffineTransform3 *, const Cone *, const AffineTransform3 *)
{
    return false;
}
bool cOrientedConeBound::DoIntersectsSphereInAt(const AffineTransform3 *, const Sphere *, const AffineTransform3 *)
{
    return false;
}
bool cOrientedConeBound::DoIntersectsCuboidInAt(const AffineTransform3 *, const Cuboid *, const AffineTransform3 *)
{
    return false;
}

// ---- the other bound ----

bool cOrientedConeBound::DoIntersectsBoundIn(const AffineTransform3 *frame, cIBound *bound)
{
    (void)frame;
    const AffineTransform3 at = OrientedCone_Frame(mData.frame);
    return bound->IntersectsConeAt(&mData, &at);
}

bool cOrientedConeBound::DoIntersectsBoundAt(cIBound *bound, const AffineTransform3 *frame)
{
    const AffineTransform3 at = OrientedCone_Frame(mData.frame);
    return bound->IntersectsConeInAt(frame, &mData, &at);
}

bool cOrientedConeBound::DoIntersectsBoundInAt(const AffineTransform3 *frame, cIBound *bound,
                                               const AffineTransform3 *otherFrame)
{
    const AffineTransform3 here = OrientedCone_In(mData.frame, *frame);
    return bound->IntersectsConeInAt(otherFrame, &mData, &here);
}

// ---- the linear shapes ----

bool cOrientedConeBound::DoIntersectsSegmentIn(const AffineTransform3 *frame, const Segment3 *segment)
{
    const AffineTransform3 here = OrientedCone_In(mData.frame, *frame);
    return TestPair<ShapeKind::Cone, Placement::InFrame, Segment3>(&mData, &here, segment);
}
bool cOrientedConeBound::DoIntersectsRayIn(const AffineTransform3 *frame, const Ray3 *ray)
{
    const AffineTransform3 here = OrientedCone_In(mData.frame, *frame);
    return TestPair<ShapeKind::Cone, Placement::InFrame, Ray3>(&mData, &here, ray);
}
bool cOrientedConeBound::DoIntersectsLineIn(const AffineTransform3 *frame, const Line3 *line)
{
    const AffineTransform3 here = OrientedCone_In(mData.frame, *frame);
    return TestPair<ShapeKind::Cone, Placement::InFrame, Line3>(&mData, &here, line);
}
bool cOrientedConeBound::DoIntersectsSegment(const Segment3 *segment)
{
    return TestPair<ShapeKind::Cone, Placement::Oriented, Segment3>(&mData, &mData.frame, segment);
}
bool cOrientedConeBound::DoIntersectsRay(const Ray3 *ray)
{
    return TestPair<ShapeKind::Cone, Placement::Oriented, Ray3>(&mData, &mData.frame, ray);
}
bool cOrientedConeBound::DoIntersectsLine(const Line3 *line)
{
    return TestPair<ShapeKind::Cone, Placement::Oriented, Line3>(&mData, &mData.frame, line);
}
bool cOrientedConeBound::DoFirstHitSegmentIn(float *t, const AffineTransform3 *frame, const Segment3 *segment)
{
    const AffineTransform3 here = OrientedCone_In(mData.frame, *frame);
    return FirstHitPair<ShapeKind::Cone, Placement::InFrame, Segment3>(t, &mData, &here, segment);
}
bool cOrientedConeBound::DoFirstHitRayIn(float *t, const AffineTransform3 *frame, const Ray3 *ray)
{
    const AffineTransform3 here = OrientedCone_In(mData.frame, *frame);
    return FirstHitPair<ShapeKind::Cone, Placement::InFrame, Ray3>(t, &mData, &here, ray);
}
bool cOrientedConeBound::DoFirstHitLineIn(float *t, const AffineTransform3 *frame, const Line3 *line)
{
    const AffineTransform3 here = OrientedCone_In(mData.frame, *frame);
    return FirstHitPair<ShapeKind::Cone, Placement::InFrame, Line3>(t, &mData, &here, line);
}
bool cOrientedConeBound::DoFirstHitSegment(float *t, const Segment3 *segment)
{
    return FirstHitPair<ShapeKind::Cone, Placement::Oriented, Segment3>(t, &mData, &mData.frame, segment);
}
bool cOrientedConeBound::DoFirstHitRay(float *t, const Ray3 *ray)
{
    return FirstHitPair<ShapeKind::Cone, Placement::Oriented, Ray3>(t, &mData, &mData.frame, ray);
}
bool cOrientedConeBound::DoFirstHitLine(float *t, const Line3 *line)
{
    return FirstHitPair<ShapeKind::Cone, Placement::Oriented, Line3>(t, &mData, &mData.frame, line);
}

// ---- boxes ----

BoundBox *cOrientedConeBound::DoGetBoundBoxIn(BoundBox *out, const AffineTransform3 *frame)
{
    const AffineTransform3 here = OrientedCone_In(mData.frame, *frame);
    return mData.BoxInFrame(out, &here);
}
BoundBox *cOrientedConeBound::DoGetBoundBoxRigid(BoundBox *out, const RigidTransform3 *rigid)
{
    RigidTransform3 here = mData.frame;
    here.Compose(rigid);
    return mData.BoxInRigid(out, &here);
}
BoundBox *cOrientedConeBound::DoGetBoundBox(BoundBox *out)
{
    return mData.BoxInRigid(out, &mData.frame);
}

// ---- support points ----

Vec3 *cOrientedConeBound::DoSupportPointIn(Vec3 *out, const AffineTransform3 *frame, const Vec3 *direction)
{
    const AffineTransform3 here = OrientedCone_In(mData.frame, *frame);
    Vec3 local, support;
    static_cast<const ConvexFrame &>(here).TransposedVectorTo(&local, direction);
    mData.SupportAlong(&support, &local);
    const_cast<AffineTransform3 &>(here).TransformPoint(&out->x, &support.x);
    return out;
}
Vec3 *cOrientedConeBound::DoSupportPointRigid(Vec3 *out, const RigidTransform3 *rigid, const Vec3 *direction)
{
    RigidTransform3 here = mData.frame;
    here.Compose(rigid);
    Vec3 local = *direction;
    here.InverseTransformVector(&local);
    Vec3 support;
    mData.SupportAlong(&support, &local);
    here.TransformPoint(&support);
    *out = support;
    return out;
}
Vec3 *cOrientedConeBound::DoSupportPoint(Vec3 *out, const Vec3 *direction)
{
    Vec3 local = *direction;
    mData.frame.InverseTransformVector(&local);
    Vec3 support;
    mData.SupportAlong(&support, &local);
    mData.frame.TransformPoint(&support);
    *out = support;
    return out;
}

cOrientedConeShape *__cdecl cOrientedConeShape::Create()
{
    return new cOrientedConeShape;
}

} // namespace nGZCompGeom
