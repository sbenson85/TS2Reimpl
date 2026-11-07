#include "compgeom/Linear3.h"

#include <new>

#include "compgeom/Frames.h"
#include "compgeom/ShapePairs.h"
#include "compgeom/TrianglePairs.h"
#include "game/original.h"

// nGZCompGeom::cLinear3T<I>, cLine3, cRay3, cSegment3 (src/compgeom/Linear3.h). RVAs 1.18.0.187.
//
// The intersection tests the slots forward to (0x197d70-0x19b330, one per shape kind and linear kind) are still game code,
// called through OriginalFunction with the same arguments the original slots pass.
namespace nGZCompGeom {

// ---- frames ----

Vec3 FramePoint(const AffineTransform3 &frame, const Vec3 &p)
{
    Vec3 r;
    const_cast<AffineTransform3 &>(frame).TransformPoint(&r.x, &p.x);
    return r;
}
// The other three: the frame's members in src/compgeom/Frames.cpp (lane PC).
Vec3 FrameVector(const AffineTransform3 &frame, const Vec3 &v)
{
    Vec3 r;
    AsFrame(frame).VectorTo(&r, &v);
    return r;
}
Vec3 FrameInversePoint(const AffineTransform3 &frame, const Vec3 &p)
{
    Vec3 r;
    AsFrame(frame).InversePointTo(&r, &p);
    return r;
}
Vec3 FrameInverseVector(const AffineTransform3 &frame, const Vec3 &v)
{
    Vec3 r;
    AsFrame(frame).InverseVectorTo(&r, &v);
    return r;
}

// ---- containment ----

// 0x18ec30: p - o parallel to d, by (d.v)^2 nearly equal to |d|^2 |v|^2 (so every point of a degenerate line, d = 0).
bool Line3::Contains(const Vec3 &p) const
{
    const float vx = p.x - origin.x, vy = p.y - origin.y, vz = p.z - origin.z;
    const float dot = (vy * direction.y + direction.x * vx) + vz * direction.z;
    const float dd = (direction.y * direction.y + direction.x * direction.x) + direction.z * direction.z;
    const float vv = (vx * vx + vy * vy) + vz * vz;
    return NearlyEqual(dot * dot, vv * dd);
}

// Slot 32 of the ray (0x18ed30): the origin itself (each coordinate nearly equal), otherwise ahead of it on the line.
bool Ray3::Contains(const Vec3 &p) const
{
    if (NearlyEqual(p.x, origin.x) && NearlyEqual(p.y, origin.y) && NearlyEqual(p.z, origin.z)) return true;
    const Vec3 v{p.x - origin.x, p.y - origin.y, p.z - origin.z};
    return AheadAlong(&v, &direction);
}

bool __cdecl AheadAlong(const Vec3 *pv, const Vec3 *pd)
{
    const Vec3 &v = *pv, &d = *pd;
    const float dot = (d.x * v.x + d.y * v.y) + d.z * v.z;
    if (dot <= 0.0f) return false;
    const float dd = (d.x * d.x + d.y * d.y) + d.z * d.z;
    const float vv = (v.x * v.x + v.y * v.y) + v.z * v.z;
    return NearlyEqual(dot * dot, dd * vv);
}

// 0x18ee90: inside the box the two ends span (each coordinate between them, products not below zero; a NaN passes), then
// on the line from start to end.
bool Segment3::Contains(const Vec3 &p) const
{
    const float px = (p.x - end.x) * (start.x - p.x);
    const float py = (start.y - p.y) * (p.y - end.y);
    const float pz = (p.z - end.z) * (start.z - p.z);
    if (0.0f > px || 0.0f > py || 0.0f > pz) return false;
    Line3 line;
    line.origin = start;
    line.direction = Direction();
    return line.Contains(p);
}

// ---- the intersection tests (game code) ----
// Each takes the other shape (and its frame or position, or a frame beside it), then the linear shape's data; the
// FirstHit tests first the parameter they write. Overloaded by the linear kind; the RVAs are per kind.
namespace {
using PartTest = bool(__cdecl *)(const void *shape, const void *part, const void *linear);
using FrameTest = bool(__cdecl *)(const void *shape, const AffineTransform3 *frame, const void *linear);
using PartHit = bool(__cdecl *)(float *t, const void *shape, const void *part, const void *linear);
using FrameHit = bool(__cdecl *)(float *t, const void *shape, const AffineTransform3 *frame, const void *linear);

// Slot 8: an oriented cone (frame at +8).
bool TestOrientedCone(const void *c, const void *f, const Line3 &s) { return TestPair<ShapeKind::Cone, Placement::Oriented>(c, f, &s); }
bool TestOrientedCone(const void *c, const void *f, const Ray3 &s) { return TestPair<ShapeKind::Cone, Placement::Oriented>(c, f, &s); }
bool TestOrientedCone(const void *c, const void *f, const Segment3 &s) { return TestPair<ShapeKind::Cone, Placement::Oriented>(c, f, &s); }
// Slot 9: a translated cone (position at +8).
bool TestTranslatedCone(const void *c, const void *f, const Line3 &s) { return TestPair<ShapeKind::Cone, Placement::Translated>(c, f, &s); }
bool TestTranslatedCone(const void *c, const void *f, const Ray3 &s) { return TestPair<ShapeKind::Cone, Placement::Translated>(c, f, &s); }
bool TestTranslatedCone(const void *c, const void *f, const Segment3 &s) { return TestPair<ShapeKind::Cone, Placement::Translated>(c, f, &s); }
// Slot 10: a translated sphere (centre at +4).
bool TestTranslatedSphere(const void *c, const void *f, const Line3 &s) { return TestPair<ShapeKind::Sphere, Placement::Translated>(c, f, &s); }
bool TestTranslatedSphere(const void *c, const void *f, const Ray3 &s) { return TestPair<ShapeKind::Sphere, Placement::Translated>(c, f, &s); }
bool TestTranslatedSphere(const void *c, const void *f, const Segment3 &s) { return TestPair<ShapeKind::Sphere, Placement::Translated>(c, f, &s); }
// Slot 11: an oriented cuboid (frame at +0xc).
bool TestOrientedCuboid(const void *c, const void *f, const Line3 &s) { return TestPair<ShapeKind::Cuboid, Placement::Oriented>(c, f, &s); }
bool TestOrientedCuboid(const void *c, const void *f, const Ray3 &s) { return TestPair<ShapeKind::Cuboid, Placement::Oriented>(c, f, &s); }
bool TestOrientedCuboid(const void *c, const void *f, const Segment3 &s) { return TestPair<ShapeKind::Cuboid, Placement::Oriented>(c, f, &s); }
// Slot 12: a translated cuboid (position at +0xc).
bool TestTranslatedCuboid(const void *c, const void *f, const Line3 &s) { return TestPair<ShapeKind::Cuboid, Placement::Translated>(c, f, &s); }
bool TestTranslatedCuboid(const void *c, const void *f, const Ray3 &s) { return TestPair<ShapeKind::Cuboid, Placement::Translated>(c, f, &s); }
bool TestTranslatedCuboid(const void *c, const void *f, const Segment3 &s) { return TestPair<ShapeKind::Cuboid, Placement::Translated>(c, f, &s); }
// Slot 14: a triangle set in a frame.
bool TestTriangleSetIn(const void *c, const AffineTransform3 *f, const Line3 &s) { return nGZCompGeom::TestTriangleSetInFrame<Line3>(static_cast<const TriangleSet *>(c), f, &s); }
bool TestTriangleSetIn(const void *c, const AffineTransform3 *f, const Ray3 &s) { return nGZCompGeom::TestTriangleSetInFrame<Ray3>(static_cast<const TriangleSet *>(c), f, &s); }
bool TestTriangleSetIn(const void *c, const AffineTransform3 *f, const Segment3 &s) { return nGZCompGeom::TestTriangleSetInFrame<Segment3>(static_cast<const TriangleSet *>(c), f, &s); }
// Slot 15: a cone in a frame.
bool TestConeIn(const void *c, const AffineTransform3 *f, const Line3 &s) { return TestPair<ShapeKind::Cone, Placement::InFrame>(c, f, &s); }
bool TestConeIn(const void *c, const AffineTransform3 *f, const Ray3 &s) { return TestPair<ShapeKind::Cone, Placement::InFrame>(c, f, &s); }
bool TestConeIn(const void *c, const AffineTransform3 *f, const Segment3 &s) { return TestPair<ShapeKind::Cone, Placement::InFrame>(c, f, &s); }
// Slot 16: a sphere in a frame.
bool TestSphereIn(const void *c, const AffineTransform3 *f, const Line3 &s) { return TestPair<ShapeKind::Sphere, Placement::InFrame>(c, f, &s); }
bool TestSphereIn(const void *c, const AffineTransform3 *f, const Ray3 &s) { return TestPair<ShapeKind::Sphere, Placement::InFrame>(c, f, &s); }
bool TestSphereIn(const void *c, const AffineTransform3 *f, const Segment3 &s) { return TestPair<ShapeKind::Sphere, Placement::InFrame>(c, f, &s); }
// Slot 17: a cuboid in a frame.
bool TestCuboidIn(const void *c, const AffineTransform3 *f, const Line3 &s) { return TestPair<ShapeKind::Cuboid, Placement::InFrame>(c, f, &s); }
bool TestCuboidIn(const void *c, const AffineTransform3 *f, const Ray3 &s) { return TestPair<ShapeKind::Cuboid, Placement::InFrame>(c, f, &s); }
bool TestCuboidIn(const void *c, const AffineTransform3 *f, const Segment3 &s) { return TestPair<ShapeKind::Cuboid, Placement::InFrame>(c, f, &s); }
// Slot 19: the first hit on an oriented cone.
bool HitOrientedCone(float *t, const void *c, const void *f, const Line3 &s) { return FirstHitPair<ShapeKind::Cone, Placement::Oriented>(t, c, f, &s); }
bool HitOrientedCone(float *t, const void *c, const void *f, const Ray3 &s) { return FirstHitPair<ShapeKind::Cone, Placement::Oriented>(t, c, f, &s); }
bool HitOrientedCone(float *t, const void *c, const void *f, const Segment3 &s) { return FirstHitPair<ShapeKind::Cone, Placement::Oriented>(t, c, f, &s); }
// Slot 20: on a translated cone.
bool HitTranslatedCone(float *t, const void *c, const void *f, const Line3 &s) { return FirstHitPair<ShapeKind::Cone, Placement::Translated>(t, c, f, &s); }
bool HitTranslatedCone(float *t, const void *c, const void *f, const Ray3 &s) { return FirstHitPair<ShapeKind::Cone, Placement::Translated>(t, c, f, &s); }
bool HitTranslatedCone(float *t, const void *c, const void *f, const Segment3 &s) { return FirstHitPair<ShapeKind::Cone, Placement::Translated>(t, c, f, &s); }
// Slot 21: on a translated sphere.
bool HitTranslatedSphere(float *t, const void *c, const void *f, const Line3 &s) { return FirstHitPair<ShapeKind::Sphere, Placement::Translated>(t, c, f, &s); }
bool HitTranslatedSphere(float *t, const void *c, const void *f, const Ray3 &s) { return FirstHitPair<ShapeKind::Sphere, Placement::Translated>(t, c, f, &s); }
bool HitTranslatedSphere(float *t, const void *c, const void *f, const Segment3 &s) { return FirstHitPair<ShapeKind::Sphere, Placement::Translated>(t, c, f, &s); }
// Slot 22: on an oriented cuboid.
bool HitOrientedCuboid(float *t, const void *c, const void *f, const Line3 &s) { return FirstHitPair<ShapeKind::Cuboid, Placement::Oriented>(t, c, f, &s); }
bool HitOrientedCuboid(float *t, const void *c, const void *f, const Ray3 &s) { return FirstHitPair<ShapeKind::Cuboid, Placement::Oriented>(t, c, f, &s); }
bool HitOrientedCuboid(float *t, const void *c, const void *f, const Segment3 &s) { return FirstHitPair<ShapeKind::Cuboid, Placement::Oriented>(t, c, f, &s); }
// Slot 23: on a translated cuboid.
bool HitTranslatedCuboid(float *t, const void *c, const void *f, const Line3 &s) { return FirstHitPair<ShapeKind::Cuboid, Placement::Translated>(t, c, f, &s); }
bool HitTranslatedCuboid(float *t, const void *c, const void *f, const Ray3 &s) { return FirstHitPair<ShapeKind::Cuboid, Placement::Translated>(t, c, f, &s); }
bool HitTranslatedCuboid(float *t, const void *c, const void *f, const Segment3 &s) { return FirstHitPair<ShapeKind::Cuboid, Placement::Translated>(t, c, f, &s); }
// Slot 25: on a triangle set in a frame.
bool HitTriangleSetIn(float *t, const void *c, const AffineTransform3 *f, const Line3 &s) { return nGZCompGeom::HitTriangleSetInFrame<Line3>(t, static_cast<const TriangleSet *>(c), f, &s); }
bool HitTriangleSetIn(float *t, const void *c, const AffineTransform3 *f, const Ray3 &s) { return nGZCompGeom::HitTriangleSetInFrame<Ray3>(t, static_cast<const TriangleSet *>(c), f, &s); }
bool HitTriangleSetIn(float *t, const void *c, const AffineTransform3 *f, const Segment3 &s) { return nGZCompGeom::HitTriangleSetInFrame<Segment3>(t, static_cast<const TriangleSet *>(c), f, &s); }
// Slot 26: on a cone in a frame.
bool HitConeIn(float *t, const void *c, const AffineTransform3 *f, const Line3 &s) { return FirstHitPair<ShapeKind::Cone, Placement::InFrame>(t, c, f, &s); }
bool HitConeIn(float *t, const void *c, const AffineTransform3 *f, const Ray3 &s) { return FirstHitPair<ShapeKind::Cone, Placement::InFrame>(t, c, f, &s); }
bool HitConeIn(float *t, const void *c, const AffineTransform3 *f, const Segment3 &s) { return FirstHitPair<ShapeKind::Cone, Placement::InFrame>(t, c, f, &s); }
// Slot 27: on a sphere in a frame.
bool HitSphereIn(float *t, const void *c, const AffineTransform3 *f, const Line3 &s) { return FirstHitPair<ShapeKind::Sphere, Placement::InFrame>(t, c, f, &s); }
bool HitSphereIn(float *t, const void *c, const AffineTransform3 *f, const Ray3 &s) { return FirstHitPair<ShapeKind::Sphere, Placement::InFrame>(t, c, f, &s); }
bool HitSphereIn(float *t, const void *c, const AffineTransform3 *f, const Segment3 &s) { return FirstHitPair<ShapeKind::Sphere, Placement::InFrame>(t, c, f, &s); }
// Slot 28: on a cuboid in a frame.
bool HitCuboidIn(float *t, const void *c, const AffineTransform3 *f, const Line3 &s) { return FirstHitPair<ShapeKind::Cuboid, Placement::InFrame>(t, c, f, &s); }
bool HitCuboidIn(float *t, const void *c, const AffineTransform3 *f, const Ray3 &s) { return FirstHitPair<ShapeKind::Cuboid, Placement::InFrame>(t, c, f, &s); }
bool HitCuboidIn(float *t, const void *c, const AffineTransform3 *f, const Segment3 &s) { return FirstHitPair<ShapeKind::Cuboid, Placement::InFrame>(t, c, f, &s); }

// The bound's slot for each linear kind (double dispatch).
bool BoundTest(cIBound *b, const Line3 &s) { return b->IntersectsLine(&s); }
bool BoundTest(cIBound *b, const Ray3 &s) { return b->IntersectsRay(&s); }
bool BoundTest(cIBound *b, const Segment3 &s) { return b->IntersectsSegment(&s); }
bool BoundTestIn(cIBound *b, const AffineTransform3 *f, const Line3 &s) { return b->IntersectsLineIn(f, &s); }
bool BoundTestIn(cIBound *b, const AffineTransform3 *f, const Ray3 &s) { return b->IntersectsRayIn(f, &s); }
bool BoundTestIn(cIBound *b, const AffineTransform3 *f, const Segment3 &s) { return b->IntersectsSegmentIn(f, &s); }
bool BoundHit(float *t, cIBound *b, const Line3 &s) { return b->FirstHitLine(t, &s); }
bool BoundHit(float *t, cIBound *b, const Ray3 &s) { return b->FirstHitRay(t, &s); }
bool BoundHit(float *t, cIBound *b, const Segment3 &s) { return b->FirstHitSegment(t, &s); }
bool BoundHitIn(float *t, cIBound *b, const AffineTransform3 *f, const Line3 &s) { return b->FirstHitLineIn(t, f, &s); }
bool BoundHitIn(float *t, cIBound *b, const AffineTransform3 *f, const Ray3 &s) { return b->FirstHitRayIn(t, f, &s); }
bool BoundHitIn(float *t, cIBound *b, const AffineTransform3 *f, const Segment3 &s) { return b->FirstHitSegmentIn(t, f, &s); }
} // namespace

// ---- cLinear3T ----

template <class I> bool cLinear3T<I>::QueryInterface(uint32_t id, void **out)
{
    if (!out) return false;
    switch (id) {
    case 1:
    case cIShape::kInterfaceId:
    case cILinear3::kInterfaceId:
    case I::kInterfaceId:
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

template <class I> uint32_t cLinear3T<I>::AddRef()
{
    return ++mRefCount;
}

template <class I> uint32_t cLinear3T<I>::Release()
{
    const uint32_t count = --mRefCount;
    if (count == 0) {
        delete this;
        return 0;
    }
    return count;
}

// A null allocation is not tested: the original then reads the vtable of address zero.
template <class I> bool cLinear3T<I>::Clone(uint32_t id, void **out)
{
    cLinear3T *copy = new cLinear3T(mShape);
    return copy->QueryInterface(id, out);
}

template <class I> uint32_t cLinear3T<I>::GetClassID()
{
    return kClassId;
}

template <class I> void cLinear3T<I>::Reframe(const AffineTransform3 *from, const AffineTransform3 *to)
{
    mShape.Reframe(*from, *to);
}
template <class I> void cLinear3T<I>::Transform(const AffineTransform3 *frame)
{
    mShape.Transform(*frame);
}
template <class I> void cLinear3T<I>::InverseTransform(const AffineTransform3 *frame)
{
    mShape.InverseTransform(*frame);
}

template <class I> bool cLinear3T<I>::IntersectsOrientedCone(const OrientedCone *cone)
{
    return TestOrientedCone(cone, ShapePart(cone, kConeFrameOffset), mShape);
}
template <class I> bool cLinear3T<I>::IntersectsTranslatedCone(const TranslatedCone *cone)
{
    return TestTranslatedCone(cone, ShapePart(cone, kConeFrameOffset), mShape);
}
template <class I> bool cLinear3T<I>::IntersectsTranslatedSphere(const TranslatedSphere *sphere)
{
    return TestTranslatedSphere(sphere, ShapePart(sphere, kSphereCentreOffset), mShape);
}
template <class I> bool cLinear3T<I>::IntersectsOrientedCuboid(const OrientedCuboid *cuboid)
{
    return TestOrientedCuboid(cuboid, ShapePart(cuboid, kCuboidFrameOffset), mShape);
}
template <class I> bool cLinear3T<I>::IntersectsTranslatedCuboid(const TranslatedCuboid *cuboid)
{
    return TestTranslatedCuboid(cuboid, ShapePart(cuboid, kCuboidFrameOffset), mShape);
}
template <class I> bool cLinear3T<I>::IntersectsBound(cIBound *bound)
{
    return BoundTest(bound, mShape);
}
template <class I> bool cLinear3T<I>::IntersectsTriangleSetIn(const TriangleSet *set, const AffineTransform3 *frame)
{
    return TestTriangleSetIn(set, frame, mShape);
}
template <class I> bool cLinear3T<I>::IntersectsConeIn(const Cone *cone, const AffineTransform3 *frame)
{
    return TestConeIn(cone, frame, mShape);
}
template <class I> bool cLinear3T<I>::IntersectsSphereIn(const Sphere *sphere, const AffineTransform3 *frame)
{
    return TestSphereIn(sphere, frame, mShape);
}
template <class I> bool cLinear3T<I>::IntersectsCuboidIn(const Cuboid *cuboid, const AffineTransform3 *frame)
{
    return TestCuboidIn(cuboid, frame, mShape);
}
template <class I> bool cLinear3T<I>::IntersectsBoundIn(cIBound *bound, const AffineTransform3 *frame)
{
    return BoundTestIn(bound, frame, mShape);
}
template <class I> bool cLinear3T<I>::FirstHitOrientedCone(float *t, const OrientedCone *cone)
{
    return HitOrientedCone(t, cone, ShapePart(cone, kConeFrameOffset), mShape);
}
template <class I> bool cLinear3T<I>::FirstHitTranslatedCone(float *t, const TranslatedCone *cone)
{
    return HitTranslatedCone(t, cone, ShapePart(cone, kConeFrameOffset), mShape);
}
template <class I> bool cLinear3T<I>::FirstHitTranslatedSphere(float *t, const TranslatedSphere *sphere)
{
    return HitTranslatedSphere(t, sphere, ShapePart(sphere, kSphereCentreOffset), mShape);
}
template <class I> bool cLinear3T<I>::FirstHitOrientedCuboid(float *t, const OrientedCuboid *cuboid)
{
    return HitOrientedCuboid(t, cuboid, ShapePart(cuboid, kCuboidFrameOffset), mShape);
}
template <class I> bool cLinear3T<I>::FirstHitTranslatedCuboid(float *t, const TranslatedCuboid *cuboid)
{
    return HitTranslatedCuboid(t, cuboid, ShapePart(cuboid, kCuboidFrameOffset), mShape);
}
template <class I> bool cLinear3T<I>::FirstHitBound(float *t, cIBound *bound)
{
    return BoundHit(t, bound, mShape);
}
template <class I>
bool cLinear3T<I>::FirstHitTriangleSetIn(float *t, const TriangleSet *set, const AffineTransform3 *frame)
{
    return HitTriangleSetIn(t, set, frame, mShape);
}
template <class I> bool cLinear3T<I>::FirstHitConeIn(float *t, const Cone *cone, const AffineTransform3 *frame)
{
    return HitConeIn(t, cone, frame, mShape);
}
template <class I> bool cLinear3T<I>::FirstHitSphereIn(float *t, const Sphere *sphere, const AffineTransform3 *frame)
{
    return HitSphereIn(t, sphere, frame, mShape);
}
template <class I> bool cLinear3T<I>::FirstHitCuboidIn(float *t, const Cuboid *cuboid, const AffineTransform3 *frame)
{
    return HitCuboidIn(t, cuboid, frame, mShape);
}
template <class I> bool cLinear3T<I>::FirstHitBoundIn(float *t, cIBound *bound, const AffineTransform3 *frame)
{
    return BoundHitIn(t, bound, frame, mShape);
}

template <class I> Vec3 cLinear3T<I>::GetDirection()
{
    return mShape.Direction();
}
template <class I> Vec3 cLinear3T<I>::GetOrigin()
{
    return mShape.Origin();
}
template <class I> bool cLinear3T<I>::ContainsPoint(const Vec3 *p)
{
    return mShape.Contains(*p);
}
template <class I> Vec3 cLinear3T<I>::PointAt(float t)
{
    return mShape.PointAt(t);
}
template <class I> typename cLinear3T<I>::Shape *cLinear3T<I>::ShapeData()
{
    return &mShape;
}
template <class I> const typename cLinear3T<I>::Shape *cLinear3T<I>::ShapeDataConst()
{
    return &mShape;
}

template class cLinear3T<cILine3>;
template class cLinear3T<cIRay3>;
template class cLinear3T<cISegment3>;

// ---- the factories (the director's, by class id) ----

cLine3 *__cdecl cLine3::Create()
{
    return new cLine3;
}
cRay3 *__cdecl cRay3::Create()
{
    return new cRay3;
}
cSegment3 *__cdecl cSegment3::Create()
{
    return new cSegment3;
}

// ---- adapters ----

template <class I> bool __fastcall cLinear3TAdapters<I>::QueryInterface(Self *self, void *, uint32_t id, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::QueryInterface(id, out);
}
template <class I> uint32_t __fastcall cLinear3TAdapters<I>::Release(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::Release();
}
template <class I> bool __fastcall cLinear3TAdapters<I>::Clone(Self *self, void *, uint32_t id, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::Clone(id, out);
}
template <class I> uint32_t __fastcall cLinear3TAdapters<I>::GetClassID(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetClassID();
}
template <class I>
void __fastcall cLinear3TAdapters<I>::Reframe(Self *self, void *, const AffineTransform3 *from, const AffineTransform3 *to)
{
    [[msvc::forceinline_calls]] self->Self::Reframe(from, to);
}
template <class I> void __fastcall cLinear3TAdapters<I>::Transform(Self *self, void *, const AffineTransform3 *frame)
{
    [[msvc::forceinline_calls]] self->Self::Transform(frame);
}
template <class I> void __fastcall cLinear3TAdapters<I>::InverseTransform(Self *self, void *, const AffineTransform3 *frame)
{
    [[msvc::forceinline_calls]] self->Self::InverseTransform(frame);
}
template <class I> bool __fastcall cLinear3TAdapters<I>::IntersectsOrientedCone(Self *self, void *, const OrientedCone *cone)
{
    [[msvc::forceinline_calls]] return self->Self::IntersectsOrientedCone(cone);
}
template <class I>
bool __fastcall cLinear3TAdapters<I>::IntersectsTranslatedCone(Self *self, void *, const TranslatedCone *cone)
{
    [[msvc::forceinline_calls]] return self->Self::IntersectsTranslatedCone(cone);
}
template <class I>
bool __fastcall cLinear3TAdapters<I>::IntersectsTranslatedSphere(Self *self, void *, const TranslatedSphere *sphere)
{
    [[msvc::forceinline_calls]] return self->Self::IntersectsTranslatedSphere(sphere);
}
template <class I>
bool __fastcall cLinear3TAdapters<I>::IntersectsOrientedCuboid(Self *self, void *, const OrientedCuboid *cuboid)
{
    [[msvc::forceinline_calls]] return self->Self::IntersectsOrientedCuboid(cuboid);
}
template <class I>
bool __fastcall cLinear3TAdapters<I>::IntersectsTranslatedCuboid(Self *self, void *, const TranslatedCuboid *cuboid)
{
    [[msvc::forceinline_calls]] return self->Self::IntersectsTranslatedCuboid(cuboid);
}
template <class I> bool __fastcall cLinear3TAdapters<I>::IntersectsBound(Self *self, void *, cIBound *bound)
{
    [[msvc::forceinline_calls]] return self->Self::IntersectsBound(bound);
}
template <class I>
bool __fastcall cLinear3TAdapters<I>::IntersectsTriangleSetIn(Self *self, void *, const TriangleSet *set,
                                                             const AffineTransform3 *frame)
{
    [[msvc::forceinline_calls]] return self->Self::IntersectsTriangleSetIn(set, frame);
}
template <class I>
bool __fastcall cLinear3TAdapters<I>::IntersectsConeIn(Self *self, void *, const Cone *cone, const AffineTransform3 *frame)
{
    [[msvc::forceinline_calls]] return self->Self::IntersectsConeIn(cone, frame);
}
template <class I>
bool __fastcall cLinear3TAdapters<I>::IntersectsSphereIn(Self *self, void *, const Sphere *sphere,
                                                        const AffineTransform3 *frame)
{
    [[msvc::forceinline_calls]] return self->Self::IntersectsSphereIn(sphere, frame);
}
template <class I>
bool __fastcall cLinear3TAdapters<I>::IntersectsCuboidIn(Self *self, void *, const Cuboid *cuboid,
                                                        const AffineTransform3 *frame)
{
    [[msvc::forceinline_calls]] return self->Self::IntersectsCuboidIn(cuboid, frame);
}
template <class I>
bool __fastcall cLinear3TAdapters<I>::IntersectsBoundIn(Self *self, void *, cIBound *bound, const AffineTransform3 *frame)
{
    [[msvc::forceinline_calls]] return self->Self::IntersectsBoundIn(bound, frame);
}
template <class I>
bool __fastcall cLinear3TAdapters<I>::FirstHitOrientedCone(Self *self, void *, float *t, const OrientedCone *cone)
{
    [[msvc::forceinline_calls]] return self->Self::FirstHitOrientedCone(t, cone);
}
template <class I>
bool __fastcall cLinear3TAdapters<I>::FirstHitTranslatedCone(Self *self, void *, float *t, const TranslatedCone *cone)
{
    [[msvc::forceinline_calls]] return self->Self::FirstHitTranslatedCone(t, cone);
}
template <class I>
bool __fastcall cLinear3TAdapters<I>::FirstHitTranslatedSphere(Self *self, void *, float *t, const TranslatedSphere *sphere)
{
    [[msvc::forceinline_calls]] return self->Self::FirstHitTranslatedSphere(t, sphere);
}
template <class I>
bool __fastcall cLinear3TAdapters<I>::FirstHitOrientedCuboid(Self *self, void *, float *t, const OrientedCuboid *cuboid)
{
    [[msvc::forceinline_calls]] return self->Self::FirstHitOrientedCuboid(t, cuboid);
}
template <class I>
bool __fastcall cLinear3TAdapters<I>::FirstHitTranslatedCuboid(Self *self, void *, float *t, const TranslatedCuboid *cuboid)
{
    [[msvc::forceinline_calls]] return self->Self::FirstHitTranslatedCuboid(t, cuboid);
}
template <class I> bool __fastcall cLinear3TAdapters<I>::FirstHitBound(Self *self, void *, float *t, cIBound *bound)
{
    [[msvc::forceinline_calls]] return self->Self::FirstHitBound(t, bound);
}
template <class I>
bool __fastcall cLinear3TAdapters<I>::FirstHitTriangleSetIn(Self *self, void *, float *t, const TriangleSet *set,
                                                           const AffineTransform3 *frame)
{
    [[msvc::forceinline_calls]] return self->Self::FirstHitTriangleSetIn(t, set, frame);
}
template <class I>
bool __fastcall cLinear3TAdapters<I>::FirstHitConeIn(Self *self, void *, float *t, const Cone *cone,
                                                    const AffineTransform3 *frame)
{
    [[msvc::forceinline_calls]] return self->Self::FirstHitConeIn(t, cone, frame);
}
template <class I>
bool __fastcall cLinear3TAdapters<I>::FirstHitSphereIn(Self *self, void *, float *t, const Sphere *sphere,
                                                      const AffineTransform3 *frame)
{
    [[msvc::forceinline_calls]] return self->Self::FirstHitSphereIn(t, sphere, frame);
}
template <class I>
bool __fastcall cLinear3TAdapters<I>::FirstHitCuboidIn(Self *self, void *, float *t, const Cuboid *cuboid,
                                                      const AffineTransform3 *frame)
{
    [[msvc::forceinline_calls]] return self->Self::FirstHitCuboidIn(t, cuboid, frame);
}
template <class I>
bool __fastcall cLinear3TAdapters<I>::FirstHitBoundIn(Self *self, void *, float *t, cIBound *bound,
                                                     const AffineTransform3 *frame)
{
    [[msvc::forceinline_calls]] return self->Self::FirstHitBoundIn(t, bound, frame);
}
template <class I> Vec3 *__fastcall cLinear3TAdapters<I>::GetDirection(Self *self, void *, Vec3 *out)
{
    [[msvc::forceinline_calls]] *out = self->Self::GetDirection();
    return out;
}
template <class I> Vec3 *__fastcall cLinear3TAdapters<I>::GetOrigin(Self *self, void *, Vec3 *out)
{
    [[msvc::forceinline_calls]] *out = self->Self::GetOrigin();
    return out;
}
template <class I> bool __fastcall cLinear3TAdapters<I>::ContainsPoint(Self *self, void *, const Vec3 *p)
{
    [[msvc::forceinline_calls]] return self->Self::ContainsPoint(p);
}
template <class I> Vec3 *__fastcall cLinear3TAdapters<I>::PointAt(Self *self, void *, Vec3 *out, float t)
{
    [[msvc::forceinline_calls]] *out = self->Self::PointAt(t);
    return out;
}

template struct cLinear3TAdapters<cILine3>;
template struct cLinear3TAdapters<cIRay3>;
template struct cLinear3TAdapters<cISegment3>;

cRay3 *__fastcall Linear3Adapters::RayConstruct(void *memory, void *, const Vec3 *origin, const Vec3 *direction)
{
    return ::new (memory) cRay3(*origin, *direction);
}
cRay3 *__fastcall Linear3Adapters::RayConstructThrough(void *memory, void *, const Vec3 *from, const Vec3 *to)
{
    return ::new (memory) cRay3(cRay3::Through{}, *from, *to);
}
cRay3 *__fastcall Linear3Adapters::RayConstructFromShape(void *memory, void *, const Ray3 *ray)
{
    return ::new (memory) cRay3(*ray);
}
cSegment3 *__fastcall Linear3Adapters::SegmentConstruct(void *memory, void *, const Vec3 *start, const Vec3 *end)
{
    return ::new (memory) cSegment3(*start, *end);
}
void __fastcall Linear3Adapters::LineDestroy(cLine3 *self, void *)
{
    self->cLine3::~cLine3();
}
void __fastcall Linear3Adapters::RayDestroy(cRay3 *self, void *)
{
    self->cRay3::~cRay3();
}
void __fastcall Linear3Adapters::SegmentDestroy(cSegment3 *self, void *)
{
    self->cSegment3::~cSegment3();
}
void __fastcall Linear3Adapters::LineTemplateDestroy(cLinear3T<cILine3> *self, void *)
{
    self->cLinear3T<cILine3>::~cLinear3T();
}
void __fastcall Linear3Adapters::RayTemplateDestroy(cLinear3T<cIRay3> *self, void *)
{
    self->cLinear3T<cIRay3>::~cLinear3T();
}
void __fastcall Linear3Adapters::SegmentTemplateDestroy(cLinear3T<cISegment3> *self, void *)
{
    self->cLinear3T<cISegment3>::~cLinear3T();
}
cLine3 *__fastcall Linear3Adapters::LineDeletingDestructor(cLine3 *self, void *, uint32_t flags)
{
    self->cLine3::~cLine3();
    if (flags & 1) ::operator delete(self, sizeof(cLine3));
    return self;
}
cRay3 *__fastcall Linear3Adapters::RayDeletingDestructor(cRay3 *self, void *, uint32_t flags)
{
    self->cRay3::~cRay3();
    if (flags & 1) ::operator delete(self, sizeof(cRay3));
    return self;
}
cSegment3 *__fastcall Linear3Adapters::SegmentDeletingDestructor(cSegment3 *self, void *, uint32_t flags)
{
    self->cSegment3::~cSegment3();
    if (flags & 1) ::operator delete(self, sizeof(cSegment3));
    return self;
}
cLinear3T<cILine3> *__fastcall Linear3Adapters::LineTemplateDeletingDestructor(cLinear3T<cILine3> *self, void *,
                                                                             uint32_t flags)
{
    self->cLinear3T<cILine3>::~cLinear3T();
    if (flags & 1) ::operator delete(self, cLinear3T<cILine3>::kSize);
    return self;
}
cLinear3T<cIRay3> *__fastcall Linear3Adapters::RayTemplateDeletingDestructor(cLinear3T<cIRay3> *self, void *,
                                                                           uint32_t flags)
{
    self->cLinear3T<cIRay3>::~cLinear3T();
    if (flags & 1) ::operator delete(self, cLinear3T<cIRay3>::kSize);
    return self;
}
cLinear3T<cISegment3> *__fastcall Linear3Adapters::SegmentTemplateDeletingDestructor(cLinear3T<cISegment3> *self, void *,
                                                                                   uint32_t flags)
{
    self->cLinear3T<cISegment3>::~cLinear3T();
    if (flags & 1) ::operator delete(self, cLinear3T<cISegment3>::kSize);
    return self;
}
cLinear3T<cIRay3> *__fastcall Linear3Adapters::Assign(cLinear3T<cIRay3> *self, void *, const cLinear3T<cIRay3> *other)
{
    return &self->Assign(*other);
}

} // namespace nGZCompGeom
