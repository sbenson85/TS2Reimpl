#pragma once
#include <cstddef>
#include <cstdint>

#include "compgeom/CompGeom.h"

// nGZCompGeom's convex bound shapes, cConvexT<I> (lane PM, scenegraph G0b step 4; RVAs 1.18.0.187): the translated sphere,
// the translated and oriented cuboids and the translated and oriented cones. Each is a reference-counted COM object whose
// data sits at +4 (the shape's own struct, see below) and whose count follows the data.
//
// Standalone shape (docs/standalone-shape.md): real C++ classes; the compiler emits the vtables. Per shape two classes, as
// the triangle set's: the bound (cConvexT<I>, the vtable Clone's copies get) and the concrete shape deriving from it (what
// the director's factory stores; the only difference is the deleting destructor and the vtable address). The factory is
// hooked to Create. Every other slot is hooked at its RVA, too: the point and spot lights embed game-vtable shapes
// (src/sg/SGLightShapes.h) whose slots must run our bodies. The hooks are the non-virtual Do<Slot> members that the
// virtual overrides call.
//
// The shapes meet by double dispatch: slot 5-10 take the other bound in the world ("In": this bound placed by a frame),
// 11-15 and 16-20 the other placed by frames. The linear shapes (21-23, 25-27, 29-31, 33-35) go to the pair tests
// (ShapePairs.h), 24, 28, 32 and 36 hand this bound to the linear's own slot, 37-39 give the bound's box, and 40-42 the
// support point along a direction (cIConvex).
namespace nGZCompGeom {

// nGZCompGeom::cIConvex (id 0xbd166dd5): the support points; slots 40-42, after cIBound's 5-39.
class cIConvex : public cIBound {
public:
    static constexpr uint32_t kInterfaceId = 0xbd166dd5;
    virtual Vec3 *SupportPointIn(Vec3 *out, const AffineTransform3 *frame, const Vec3 *direction) = 0;    // 40
    virtual Vec3 *SupportPointRigid(Vec3 *out, const RigidTransform3 *rigid, const Vec3 *direction) = 0; // 41
    virtual Vec3 *SupportPoint(Vec3 *out, const Vec3 *direction) = 0;                                     // 42
};

// ---- the shapes' data ----

// A sphere in its own frame (the Sphere the "At" slots take): the radius. The members are the original's helpers on it.
struct Sphere {
    float radius;

    // 0x192250 (__thiscall, ret 8; returns out): the box of the sphere carried by `frame` (the frame's offset is the
    // centre): without rotation, offset -/+ the scaled radius; otherwise the cube of the radius, transformed.
    BoundBox *BoxInFrame(BoundBox *out, const AffineTransform3 *frame) const;
    // 0x192380 (__thiscall, ret 8; returns out): the box of the sphere about `centre`.
    BoundBox *BoxAt(BoundBox *out, const Vec3 *centre) const;
    // 0x193380 (__thiscall, ret 8; returns out): the support point of the sphere about the origin along v: the radius
    // times v's direction (along +x when v is shorter than 1.19e-10... the original's test is on the length).
    Vec3 *SupportAlong(Vec3 *out, const Vec3 *v) const;
};
static_assert(sizeof(Sphere) == 4);

struct TranslatedSphere : Sphere {
    Vec3 centre; // +4 (the radius is Sphere's, at +0)
};
static_assert(sizeof(TranslatedSphere) == 0x10);

// The cuboids' data: the half extents, then where the cuboid is.
struct TranslatedCuboid {
    Cuboid cuboid;  // +0
    Vec3 position;  // +0xc
};
static_assert(sizeof(TranslatedCuboid) == 0x18);
struct OrientedCuboid {
    Cuboid cuboid;          // +0
    RigidTransform3 frame;  // +0xc: translation, then the quaternion
};
static_assert(sizeof(OrientedCuboid) == 0x28);

// A cone in its own frame (the "At" slots take it): the radius, then the half height; its axis is z, the apex at +h and the
// base at -h. The members are the original's helpers on it (lane PR, scenegraph G0b step 4).
struct Cone {
    float radius; // +0
    float height; // +4 (the half height)

    // 0x193630 (__thiscall, ret 8; returns out): the box of the cone carried by `frame`: (-r, -r, -h) to (r, r, h) through
    // BoundBox::Transform.
    BoundBox *BoxInFrame(BoundBox *out, const AffineTransform3 *frame) const;
    // 0x1936b0 (__thiscall, ret 8; returns out): the same through a rigid transform (ConvexBox::TransformRigid).
    BoundBox *BoxInRigid(BoundBox *out, const RigidTransform3 *rigid) const;
    // 0x193730 (__thiscall, ret 8; returns out): the box of the cone about `centre`: max (c.x + r, c.y + r, h + c.z),
    // min (-r + c.x, c.y + -r, c.z - h).
    BoundBox *BoxAt(BoundBox *out, const Vec3 *centre) const;
    // 0x194450 (__thiscall, ret 8; returns out): the support point of the cone about the origin along v. Shared with the
    // oriented cone.
    Vec3 *SupportAlong(Vec3 *out, const Vec3 *v) const;
};
static_assert(sizeof(Cone) == 8);

struct TranslatedCone : Cone {
    Vec3 position; // +8
};
static_assert(sizeof(TranslatedCone) == 0x14);

// The oriented cone's data: the cone, then where it is (translation, quaternion).
struct OrientedCone : Cone {
    RigidTransform3 frame; // +8
};
static_assert(sizeof(OrientedCone) == 0x24);

// nGZNumerics' box carrying the original's rigid transform (0x19da20, shared with the oriented cone's slots).
struct ConvexBox : BoundBox {
    // 0x19da20 (__thiscall, ret 4; returns this): the box of this box's points through `rigid`, in place: the centre
    // (half + min.x, half + min.y, min.z + half) goes through the rigid's matrix and translation; the half extents grow
    // by the x87 dot product with each matrix row's absolute values (as in BoundBox::Transform's axis-keeping case).
    BoundBox *TransformRigid(const RigidTransform3 *rigid);
};
static_assert(sizeof(ConvexBox) == sizeof(BoundBox));

// 0x19f820 (__cdecl: half, p; the float on the x87 stack): the squared distance from the point p to the box of half
// extents `half` centred on the origin (the squared overshoots summed, skipping the axes the point is inside on).
double __cdecl PointBoxDistanceSquared(const float *half, const float *p);

// nGZNumerics' frame carrying the original's transposed product (0x19caa0, whose only other caller is the convex shapes).
struct ConvexFrame : AffineTransform3 {
    // 0x19caa0 (__thiscall, ret 8; returns out): out = (the matrix transposed) * in, each coordinate stored before the
    // next is read: out.x = (m3 y + m0 x) + m6 z, out.y = (m4 y + m1 x) + m7 z, out.z = (m5 y + m2 x) + m8 z.
    Vec3 *TransposedVectorTo(Vec3 *out, const Vec3 *in) const;
    // 0x19cf80 (__thiscall, ret 4; returns out): the scale of each axis. Without flag 4 (1, 1, 1); with it the first
    // column's length, as float(sqrt(double((m0 m0 + m3 m3) + m6 m6))), for all three without flag 8, otherwise the
    // columns' lengths (m0 m3 m6; m1 m4 m7; m2 m5 m8).
    Vec3 *AxisScales(Vec3 *out) const;
};

// The cuboid's helper that picks a corner (the original's cCuboid member).
struct ConvexCuboid : Cuboid {
    // 0x191ea0 (__thiscall, ret 8; returns out): the corner of the box (half extents, centred on the origin) that lies
    // furthest along `direction`: each half extent, its sign flipped where the direction's component is negative.
    Vec3 *SupportCorner(Vec3 *out, const Vec3 *direction) const;
};
static_assert(sizeof(ConvexCuboid) == sizeof(Cuboid));

// 0x19e070 (__cdecl: halfA, halfB, R, T; the result in al): do two boxes (half extents a and b; the second in the first's
// frame by the rotation R (nine floats) and the centre offset T) overlap? The 15 separating axes: no axis with |lhs| >
// rhs (R's absolute values plus FLT_EPSILON bound the reach); the last test's result is the answer.
bool __cdecl BoxesMeetOnAxes15(const float *a, const float *b, const float *R, const float *T);
// 0x19dcb0 (__cdecl: halfA, frameA, halfB, frameB; the result in al): do two cuboids placed by frames overlap? The scales of
// each frame (flag 4) are folded into the half extents and out of the matrices, then the second box is taken into the first's
// frame (R = A's matrix times the transpose of B's, T = that transpose times the offsets' difference).
bool __cdecl CuboidsOverlap(const Cuboid *halfA, const AffineTransform3 *frameA, const Cuboid *halfB,
                            const AffineTransform3 *frameB);

// ---- the slots every shape implements itself (X(return, name, (parameters), (arguments))) ----
#define CONVEX_SLOTS(X)                                                                                                \
    X(bool, IntersectsOrientedConeIn, (const AffineTransform3 *frame, const OrientedCone *cone), (frame, cone))       \
    X(bool, IntersectsTranslatedConeIn, (const AffineTransform3 *frame, const TranslatedCone *cone), (frame, cone))   \
    X(bool, IntersectsTranslatedSphereIn, (const AffineTransform3 *frame, const TranslatedSphere *sphere),            \
      (frame, sphere))                                                                                                 \
    X(bool, IntersectsOrientedCuboidIn, (const AffineTransform3 *frame, const OrientedCuboid *cuboid),                \
      (frame, cuboid))                                                                                                 \
    X(bool, IntersectsTranslatedCuboidIn, (const AffineTransform3 *frame, const TranslatedCuboid *cuboid),            \
      (frame, cuboid))                                                                                                 \
    X(bool, IntersectsBoundIn, (const AffineTransform3 *frame, cIBound *bound), (frame, bound))                       \
    X(bool, IntersectsTriangleSetAt, (const TriangleSet *set, const AffineTransform3 *frame), (set, frame))           \
    X(bool, IntersectsConeAt, (const Cone *cone, const AffineTransform3 *frame), (cone, frame))                       \
    X(bool, IntersectsSphereAt, (const Sphere *sphere, const AffineTransform3 *frame), (sphere, frame))               \
    X(bool, IntersectsCuboidAt, (const Cuboid *cuboid, const AffineTransform3 *frame), (cuboid, frame))               \
    X(bool, IntersectsBoundAt, (cIBound *bound, const AffineTransform3 *frame), (bound, frame))                       \
    X(bool, IntersectsTriangleSetInAt,                                                                                 \
      (const AffineTransform3 *frame, const TriangleSet *set, const AffineTransform3 *otherFrame),                    \
      (frame, set, otherFrame))                                                                                        \
    X(bool, IntersectsConeInAt, (const AffineTransform3 *frame, const Cone *cone, const AffineTransform3 *otherFrame), \
      (frame, cone, otherFrame))                                                                                       \
    X(bool, IntersectsSphereInAt,                                                                                      \
      (const AffineTransform3 *frame, const Sphere *sphere, const AffineTransform3 *otherFrame),                      \
      (frame, sphere, otherFrame))                                                                                     \
    X(bool, IntersectsCuboidInAt,                                                                                      \
      (const AffineTransform3 *frame, const Cuboid *cuboid, const AffineTransform3 *otherFrame),                      \
      (frame, cuboid, otherFrame))                                                                                     \
    X(bool, IntersectsBoundInAt, (const AffineTransform3 *frame, cIBound *bound, const AffineTransform3 *otherFrame),  \
      (frame, bound, otherFrame))                                                                                      \
    X(bool, IntersectsSegmentIn, (const AffineTransform3 *frame, const Segment3 *segment), (frame, segment))          \
    X(bool, IntersectsRayIn, (const AffineTransform3 *frame, const Ray3 *ray), (frame, ray))                          \
    X(bool, IntersectsLineIn, (const AffineTransform3 *frame, const Line3 *line), (frame, line))                      \
    X(bool, IntersectsSegment, (const Segment3 *segment), (segment))                                                  \
    X(bool, IntersectsRay, (const Ray3 *ray), (ray))                                                                  \
    X(bool, IntersectsLine, (const Line3 *line), (line))                                                              \
    X(bool, FirstHitSegmentIn, (float *t, const AffineTransform3 *frame, const Segment3 *segment), (t, frame, segment)) \
    X(bool, FirstHitRayIn, (float *t, const AffineTransform3 *frame, const Ray3 *ray), (t, frame, ray))               \
    X(bool, FirstHitLineIn, (float *t, const AffineTransform3 *frame, const Line3 *line), (t, frame, line))           \
    X(bool, FirstHitSegment, (float *t, const Segment3 *segment), (t, segment))                                       \
    X(bool, FirstHitRay, (float *t, const Ray3 *ray), (t, ray))                                                       \
    X(bool, FirstHitLine, (float *t, const Line3 *line), (t, line))                                                   \
    X(BoundBox *, GetBoundBoxIn, (BoundBox * out, const AffineTransform3 *frame), (out, frame))                       \
    X(BoundBox *, GetBoundBoxRigid, (BoundBox * out, const RigidTransform3 *rigid), (out, rigid))                     \
    X(BoundBox *, GetBoundBox, (BoundBox * out), (out))                                                               \
    X(Vec3 *, SupportPointIn, (Vec3 * out, const AffineTransform3 *frame, const Vec3 *direction),                     \
      (out, frame, direction))                                                                                         \
    X(Vec3 *, SupportPointRigid, (Vec3 * out, const RigidTransform3 *rigid, const Vec3 *direction),                   \
      (out, rigid, direction))                                                                                         \
    X(Vec3 *, SupportPoint, (Vec3 * out, const Vec3 *direction), (out, direction))

// The part every cConvexT shares: the COM and shape slots, the linear dispatch slots, the data accessors. `Self` is the
// bound class (what Clone copies), `Interface` the shape's cIShapeT<Data, cIConvex>.
template <class Self, class Interface, uint32_t ClassId> class cConvexT : public Interface {
public:
    using Data = typename Interface::Shape;
    static constexpr uint32_t kClassId = ClassId;

    cConvexT() : mRefCount(0) {}
    cConvexT(const cConvexT &) = delete;
    virtual ~cConvexT() {} // slot 45 (the deleting destructor: the vtable, then operator delete on bit 0)

    // cIGZUnknown
    bool QueryInterface(uint32_t id, void **out) override // 0: 1, cIShape, cIBound, cIConvex or the class id
    {
        if (!out) return false;
        if (id == 1 || id == cIShape::kInterfaceId || id == cIBound::kInterfaceId || id == cIConvex::kInterfaceId ||
            id == ClassId) {
            *out = this;
            AddRef();
            return true;
        }
        return false;
    }
    uint32_t AddRef() override { return ++mRefCount; } // 1 (shared with the other classes with the count at this offset)
    uint32_t Release() override                         // 2: at zero, delete this and return 0
    {
        const uint32_t count = --mRefCount;
        if (count == 0) {
            delete this;
            return 0;
        }
        return count;
    }
    // cIShape
    bool Clone(uint32_t id, void **out) override // 3: a bound copy (count 0) of the data, queried for `id`
    {
        // ORIGINAL-BUG: a copy the query refuses (an id the shape doesn't answer) is never released: it leaks.
        Self *const copy = new Self;
        copy->mData = mData;
        return copy->QueryInterface(id, out);
    }
    uint32_t GetClassID() override { return ClassId; } // 4

    // The four slots that hand this bound to the linear shape's own slot.
    bool IntersectsLinearIn(const AffineTransform3 *frame, cILinear3 *linear) override // 24 (shared 0x190c40)
    {
        return linear->IntersectsBoundIn(this, frame);
    }
    bool IntersectsLinear(cILinear3 *linear) override { return linear->IntersectsBound(this); } // 28 (shared 0x190ed0)
    bool FirstHitLinearIn(float *t, const AffineTransform3 *frame, cILinear3 *linear) override // 32 (shared 0x191f50)
    {
        return linear->FirstHitBoundIn(t, this, frame);
    }
    bool FirstHitLinear(float *t, cILinear3 *linear) override { return linear->FirstHitBound(t, this); } // 36 (0x191fd0)

    // cIShapeT
    Data *ShapeData() override { return &mData; }                    // 43 (shared 0x18ea60)
    const Data *ShapeDataConst() override { return &mData; }          // 44 (shared 0x18ea60)

    // +0 the vtable
    Data mData;         // +4
    uint32_t mRefCount; // after the data
};

#define CONVEX_DECLARE_SLOT(R, N, P, A) \
    R N P override { return Do##N A; }  \
    R Do##N P;

// ---- the translated sphere ----

// nGZCompGeom::cITranslatedSphereShape (id 0x7ce57a3c).
class cITranslatedSphereBound : public cIShapeT<TranslatedSphere, cIConvex> {
public:
    static constexpr uint32_t kInterfaceId = 0x7ce57a3c;
};

// cConvexT<cITranslatedSphereShape> (vtable 0xda18b4; Clone's copies), 0x18 bytes: the data at +4, the count at +0x14.
class cTranslatedSphereBound : public cConvexT<cTranslatedSphereBound, cITranslatedSphereBound, 0x7ce57a3c> {
public:
    CONVEX_SLOTS(CONVEX_DECLARE_SLOT)
};

// nGZCompGeom::cTranslatedSphereShape (vtable 0xda1970; the director's factory 0x192460).
class cTranslatedSphereShape : public cTranslatedSphereBound {
public:
    cTranslatedSphereShape() {}
    ~cTranslatedSphereShape() override {}
    static cTranslatedSphereShape *__cdecl Create(); // 0x192460
};
static_assert(sizeof(cTranslatedSphereShape) == 0x18);
static_assert(offsetof(cTranslatedSphereBound, mRefCount) == 0x14);

// ---- the translated cuboid (lane PP) ----

// nGZCompGeom::cITranslatedCuboidShape (id 0x3ce553fa).
class cITranslatedCuboidBound : public cIShapeT<TranslatedCuboid, cIConvex> {
public:
    static constexpr uint32_t kInterfaceId = 0x3ce553fa;
};

// cConvexT<cITranslatedCuboidShape> (vtable 0xda1584; Clone's copies), 0x20 bytes: the data at +4, the count at +0x1c.
class cTranslatedCuboidBound : public cConvexT<cTranslatedCuboidBound, cITranslatedCuboidBound, 0x3ce553fa> {
public:
    CONVEX_SLOTS(CONVEX_DECLARE_SLOT)
};

// nGZCompGeom::cTranslatedCuboidShape (vtable 0xda1640; the director's factory 0x190450).
class cTranslatedCuboidShape : public cTranslatedCuboidBound {
public:
    cTranslatedCuboidShape() {}
    // 0x18fc10 (__thiscall, ret 8): a cuboid of the given full extents (stored halved) at `centre`, count 0. The octree's
    // cell queries build one on the stack per cell.
    cTranslatedCuboidShape(const Vec3 *extents, const Vec3 *centre);
    ~cTranslatedCuboidShape() override {}
    static cTranslatedCuboidShape *__cdecl Create(); // 0x190450
};
static_assert(sizeof(cTranslatedCuboidShape) == 0x20);
static_assert(offsetof(cTranslatedCuboidBound, mRefCount) == 0x1c);

// ---- the oriented cuboid (lane PP) ----

// nGZCompGeom::cIOrientedCuboidShape (id 0x7ce6d2e1).
class cIOrientedCuboidBound : public cIShapeT<OrientedCuboid, cIConvex> {
public:
    static constexpr uint32_t kInterfaceId = 0x7ce6d2e1;
};

// cConvexT<cIOrientedCuboidShape> (vtable 0xda16fc; Clone's copies), 0x30 bytes: the data at +4, the count at +0x2c.
class cOrientedCuboidBound : public cConvexT<cOrientedCuboidBound, cIOrientedCuboidBound, 0x7ce6d2e1> {
public:
    CONVEX_SLOTS(CONVEX_DECLARE_SLOT)
};

// nGZCompGeom::cOrientedCuboidShape (vtable 0xda17b8; the director's factory 0x190420).
class cOrientedCuboidShape : public cOrientedCuboidBound {
public:
    cOrientedCuboidShape() {}
    ~cOrientedCuboidShape() override {}
    static cOrientedCuboidShape *__cdecl Create(); // 0x190420
};
static_assert(sizeof(cOrientedCuboidShape) == 0x30);
static_assert(offsetof(cOrientedCuboidBound, mRefCount) == 0x2c);

// ---- the translated cone (lane PR) ----

// nGZCompGeom::cITranslatedConeShape (id 0xbd18b3f9).
class cITranslatedConeBound : public cIShapeT<TranslatedCone, cIConvex> {
public:
    static constexpr uint32_t kInterfaceId = 0xbd18b3f9;
};

// cConvexT<cITranslatedConeShape> (vtable 0xda1a44; Clone's copies), 0x1c bytes: the data at +4, the count at +0x18.
class cTranslatedConeBound : public cConvexT<cTranslatedConeBound, cITranslatedConeBound, 0xbd18b3f9> {
public:
    CONVEX_SLOTS(CONVEX_DECLARE_SLOT)
};

// nGZCompGeom::cTranslatedConeShape (vtable 0xda1b00; the director's factory 0x193a80).
class cTranslatedConeShape : public cTranslatedConeBound {
public:
    cTranslatedConeShape() {}
    ~cTranslatedConeShape() override {}
    static cTranslatedConeShape *__cdecl Create(); // 0x193a80
};
static_assert(sizeof(cTranslatedConeShape) == 0x1c);
static_assert(offsetof(cTranslatedConeBound, mRefCount) == 0x18);

// ---- the oriented cone (lane PS) ----

// nGZCompGeom::cIOrientedConeShape (id 0x7d18b3eb).
class cIOrientedConeBound : public cIShapeT<OrientedCone, cIConvex> {
public:
    static constexpr uint32_t kInterfaceId = 0x7d18b3eb;
};

// cConvexT<cIOrientedConeShape> (vtable 0xda1bbc; Clone's copies), 0x2c bytes: the data at +4, the count at +0x28.
class cOrientedConeBound : public cConvexT<cOrientedConeBound, cIOrientedConeBound, 0x7d18b3eb> {
public:
    CONVEX_SLOTS(CONVEX_DECLARE_SLOT)
};

// nGZCompGeom::cOrientedConeShape (vtable 0xda1c78; the director's factory 0x193a50).
class cOrientedConeShape : public cOrientedConeBound {
public:
    cOrientedConeShape() {}
    ~cOrientedConeShape() override {}
    static cOrientedConeShape *__cdecl Create(); // 0x193a50
};
static_assert(sizeof(cOrientedConeShape) == 0x2c);
static_assert(offsetof(cOrientedConeBound, mRefCount) == 0x28);

} // namespace nGZCompGeom
