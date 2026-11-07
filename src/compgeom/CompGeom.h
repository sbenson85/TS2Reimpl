#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "gz/cIGZUnknown.h"
#include "misc/UtilitiesC.h" // AffineTransform3: nGZCompGeom's frame (flags, 3x3 matrix, offset)

// nGZCompGeom: the game's computational geometry (lines, rays, segments, bounding shapes and their intersection tests).
// RVAs are 1.18.0.187. Shapes are reference-counted COM objects built by the nGZCompGeom COM director's factories (steps
// 26..35 of src/rz/COMDirectorData.cpp) or on the stack by the scenegraph, the world and the UI.
//
// Interface chain of every shape (RTTI): cIGZUnknown -> cIShape (slots 3, 4) -> a shape-kind interface (cILinear3 for lines,
// rays and segments; cIBound -> cIConvex for the convex bounds) -> cIShapeT<Data, Kind> (two slots returning the shape's
// data) -> the shape's own interface (cIRay3, ...; adds nothing) -> the implementation template (cLinear3T<I>, cConvexT<I>,
// cBoundT<I>), which introduces the virtual destructor (the last slot) -> the concrete class (cRay3, ...; constructors only).
// Two shapes test each other by double dispatch: a line's slot taking a bound calls the bound's slot for the line's kind.
namespace nGZCompGeom {

// nGZNumerics::cVector<3, cTypeTraits<float>>.
struct Vec3 {
    float x, y, z;
};
static_assert(sizeof(Vec3) == 12);

// Float bits as an ordered integer (negative floats mirrored below zero, -0 and +0 both 0), as the original computes them.
inline int32_t OrderedBits(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    const uint32_t sign = bits >> 31;
    return int32_t((((0u - sign) >> 1) ^ bits) + sign);
}

// The original's "nearly equal" test (inlined in many places): the ordered difference a - b lies in [-32, 31] ulps (the
// sign trick `(d >> 26) ^ d` clears the high bits of small negative differences).
inline bool NearlyEqual(float a, float b)
{
    const int32_t d = int32_t(uint32_t(OrderedBits(a)) - uint32_t(OrderedBits(b)));
    return (((d >> 26) ^ d) & ~31) == 0;
}

// ---- frames (AffineTransform3) ----
// Points and vectors through a frame and back: the frame's own members in the original (0x19c820 in
// src/misc/UtilitiesC.h; the others in src/compgeom/Frames.h, lane PC), by value (src/compgeom/Linear3.cpp).
Vec3 FramePoint(const AffineTransform3 &frame, const Vec3 &p);         // our AffineTransform3::TransformPoint
Vec3 FrameVector(const AffineTransform3 &frame, const Vec3 &v);        // FrameMath::VectorTo
Vec3 FrameInversePoint(const AffineTransform3 &frame, const Vec3 &p);  // FrameMath::InversePointTo
Vec3 FrameInverseVector(const AffineTransform3 &frame, const Vec3 &v); // FrameMath::InverseVectorTo

// nGZNumerics' axis-aligned box: two corners (src/compgeom/Boxes.cpp).
struct BoundBox {
    Vec3 min; // +0
    Vec3 max; // +0xc

    // 0x19d430 (__thiscall, ret 4): grows to take in `other`, per coordinate min = std::min(min, other.min) and
    // max = std::max(max, other.max) (a NaN in `other` is ignored, one here stays).
    // 0x19d430 eax-ignored: eax holds the stored max.z bits (a spill); the box is the result and no caller reads eax.
    void Grow(const BoundBox &other);
    // 0x19d4a0 (__thiscall, ret 4): grows to take in the point `p`, per coordinate min = std::min(min, p) and
    // max = std::max(max, p) (a NaN in `p` is ignored, one here stays). Each side reads all of `p` before storing, and the
    // max side reads it after the min side is stored (a point inside the box object sees the new min).
    // eax-ignored: eax holds the stored max.z bits (a spill).
    void Extend(const Vec3 &p);

    // 0x19d390 (__thiscall, ret 8; returns out): corner `index` (bit 0 picks max.x, bit 1 max.y, bit 2 max.z, else min).
    Vec3 *Corner(Vec3 *out, uint32_t index) const;
    // 0x19d730 (__thiscall, ret 4; returns this): the box of this box's points through `frame`, in place. A frame whose
    // flags have neither bit 3 nor 4 keeps the axes: the centre goes through the frame and the half extents grow by the
    // matrix's absolute values (an x87 sum, Dot87); otherwise the eight corners go through it and are min/max'd.
    BoundBox *Transform(const AffineTransform3 *frame);
    // 0x19d2f0 (__thiscall, ret 4): `other` lies inside this box: false when this.min exceeds other.min or other.max exceeds
    // this.max on any axis (strict, ordered comparisons: a NaN coordinate never makes it false).
    bool Contains(const BoundBox &other) const;
};
static_assert(sizeof(BoundBox) == 0x18);

// A rigid transform (nGZNumerics' cRigidTransform: the translation, then the rotation quaternion x, y, z, w).
struct RigidTransform3 {
    Vec3 translation;  // +0
    float rotation[4]; // +0xc

    // 0x19c900 (__thiscall, ret 4): the point through the transform in place, rotated then translated.
    // 0x19c900 eax-ignored: eax is what the rotation helper 0x1a6c40 left; no caller reads it.
    void TransformPoint(Vec3 *p) const;
    // 0x19c6d0 (__thiscall, ret 4): the point back through it in place (inverse-rotated point minus inverse-rotated
    // translation). eax-ignored: eax is the address of a stack local.
    void InverseTransformPoint(Vec3 *p) const;
    // 0x19ca50 (__thiscall, ret 4): a plane (normal x, y, z, then a fourth word w) carried through the transform in
    // place: the normal turned by the rotation (0x1a6c40), then w -= turned normal . translation (the products summed
    // y + x, then + z).
    // eax-ignored: eax is what the rotation helper left; no caller reads it.
    void TransformPlane(float *plane) const;
    // 0x19c390 (__thiscall, ret 4; returns v): the vector rotated by the inverse rotation, in place.
    Vec3 *InverseTransformVector(Vec3 *v) const;
    // 0x19c230 (__thiscall, ret 4; returns out): the rotation as a row-major 3x3 matrix.
    float *ToMatrix(float *out) const;
    // 0x19c080 (__thiscall, ret 4; returns this): this = this followed by rhs's placement: the rotation becomes the
    // normalised product (rhs rotation * this rotation), the translation the old one carried through rhs.
    RigidTransform3 *Compose(const RigidTransform3 *rhs);
    // 0x19c1c0 (__thiscall, ret 4; returns out; src/compgeom/Matrix4.h): the 4x4 matrix (sixteen floats, the translation
    // in the last column).
    float *ToMatrix4(float *out) const;
    // 0x19c800 (__thiscall, ret 4; returns v; src/compgeom/Matrix4.h): v turned by the rotation, in place.
    float *RotateVector(float *v) const;
    // 0x19cd80 (__thiscall; returns this): inverts in place: the rotation's x, y, z negated (w kept), then the translation
    // turned by that rotation and negated.
    RigidTransform3 *Invert();
};
static_assert(sizeof(RigidTransform3) == 0x1c);

// ---- shape data the linear shapes test against ----
// Owned by the bound shapes (cConvexT / cBoundT, a later lane): only the part each forwarding slot hands on is known
// here, so these stay opaque and the offsets are named.
struct OrientedCone;     // cone dimensions, then the cone's frame at +8
struct TranslatedCone;   // cone dimensions, then its position at +8
struct TranslatedSphere; // radius, then the centre at +4
struct OrientedCuboid;   // half extents, then the cuboid's frame at +0xc
struct TranslatedCuboid; // half extents, then its position at +0xc
struct Cone;             // a cone in its own frame (the frame is passed beside it)
struct Sphere;           // a sphere in its own frame
// A cuboid in its own frame: its half extents (nGZCompGeom::cCuboid<float>).
struct Cuboid {
    Vec3 half; // +0

    // 0x1902e0 (__thiscall, ret 8; the hidden result pointer first): the cuboid's box when centred on `centre`
    // (max = half + centre, min = centre - half).
    BoundBox BoxAt(const Vec3 &centre) const;
    // 0x18ff90 (__thiscall, ret 8; the hidden result pointer first): the box of this cuboid carried by `frame` (the
    // frame's offset is the centre). The result pointer is returned.
    BoundBox BoxInFrame(const AffineTransform3 &frame) const;
    // 0x190170 (__thiscall, ret 8; the hidden result pointer first): the same for a rigid transform.
    BoundBox BoxInRigid(const RigidTransform3 &rigid) const;
};
struct TriangleSet;      // a triangle set (its tree at +0x1c)

constexpr uint32_t kConeFrameOffset = 8;
constexpr uint32_t kSphereCentreOffset = 4;
constexpr uint32_t kCuboidFrameOffset = 0xc;

// A part of a shape at a byte offset (the forwarders pass the shape and its frame or position as two pointers).
inline const void *ShapePart(const void *shape, uint32_t offset)
{
    return static_cast<const uint8_t *>(shape) + offset;
}

// ---- interfaces ----

// nGZCompGeom::cIShape (id 0x1ce54fd0, which every shape's QueryInterface accepts).
class cIShape : public cIGZUnknown {
public:
    static constexpr uint32_t kInterfaceId = 0x1ce54fd0;
    virtual bool Clone(uint32_t id, void **out) = 0; // slot 3: a new copy (count 0), queried for `id`
    virtual uint32_t GetClassID() = 0;               // slot 4: the class id the director creates it by
};

struct Line3;
struct Ray3;
struct Segment3;

class cILinear3;

// nGZCompGeom::cIBound (id 0x5ce57549): the bounding shapes' interface (cConvexT, cBoundT; slots 5-39). Every test returns
// AL. Shapes meet by double dispatch, one slot per other shape kind, in four placements (named by lane PC from
// cBoundT<cITriangleSetShape>'s bodies and the shared forwarders):
//   - "In" (slots 5-10, 21-24, 29-32): this bound placed by `frame`, the other shape as given (in world space);
//   - "At" (slots 11-15): this bound as it is, the other shape placed by `frame`;
//   - "InAt" (slots 16-20): this bound placed by `frame`, the other placed by `otherFrame`;
//   - none (slots 25-28, 33-36): both as given.
// MSVC places each overload group in reverse declaration order, so every slot has its own name. Slots 19, 20, 37 and 38
// keep their numbered forms for the callers that use them (inline wrappers at the end).
class cIBound : public cIShape {
public:
    static constexpr uint32_t kInterfaceId = 0x5ce57549;
    virtual bool IntersectsOrientedConeIn(const AffineTransform3 *frame, const OrientedCone *cone) = 0;             // 5
    virtual bool IntersectsTranslatedConeIn(const AffineTransform3 *frame, const TranslatedCone *cone) = 0;         // 6
    virtual bool IntersectsTranslatedSphereIn(const AffineTransform3 *frame, const TranslatedSphere *sphere) = 0;   // 7
    virtual bool IntersectsOrientedCuboidIn(const AffineTransform3 *frame, const OrientedCuboid *cuboid) = 0;       // 8
    virtual bool IntersectsTranslatedCuboidIn(const AffineTransform3 *frame, const TranslatedCuboid *cuboid) = 0;   // 9
    virtual bool IntersectsBoundIn(const AffineTransform3 *frame, cIBound *bound) = 0;                              // 10
    virtual bool IntersectsTriangleSetAt(const TriangleSet *set, const AffineTransform3 *frame) = 0;                 // 11
    virtual bool IntersectsConeAt(const Cone *cone, const AffineTransform3 *frame) = 0;                             // 12
    virtual bool IntersectsSphereAt(const Sphere *sphere, const AffineTransform3 *frame) = 0;                       // 13
    virtual bool IntersectsCuboidAt(const Cuboid *cuboid, const AffineTransform3 *frame) = 0;                       // 14
    virtual bool IntersectsBoundAt(cIBound *bound, const AffineTransform3 *frame) = 0;                              // 15
    virtual bool IntersectsTriangleSetInAt(const AffineTransform3 *frame, const TriangleSet *set,
                                           const AffineTransform3 *otherFrame) = 0;                                 // 16
    virtual bool IntersectsConeInAt(const AffineTransform3 *frame, const Cone *cone,
                                    const AffineTransform3 *otherFrame) = 0;                                        // 17
    virtual bool IntersectsSphereInAt(const AffineTransform3 *frame, const Sphere *sphere,
                                      const AffineTransform3 *otherFrame) = 0;                                      // 18
    virtual bool IntersectsCuboidInAt(const AffineTransform3 *frame, const Cuboid *cuboid,
                                      const AffineTransform3 *otherFrame) = 0;                                      // 19
    virtual bool IntersectsBoundInAt(const AffineTransform3 *frame, cIBound *bound,
                                     const AffineTransform3 *otherFrame) = 0;                                       // 20
    virtual bool IntersectsSegmentIn(const AffineTransform3 *frame, const Segment3 *segment) = 0; // slot 21
    virtual bool IntersectsRayIn(const AffineTransform3 *frame, const Ray3 *ray) = 0;             // slot 22
    virtual bool IntersectsLineIn(const AffineTransform3 *frame, const Line3 *line) = 0;          // slot 23
    // 24 (shared 0x190c40): linear->IntersectsBoundIn(this, frame).
    virtual bool IntersectsLinearIn(const AffineTransform3 *frame, cILinear3 *linear) = 0;
    virtual bool IntersectsSegment(const Segment3 *segment) = 0; // slot 25
    virtual bool IntersectsRay(const Ray3 *ray) = 0;             // slot 26
    virtual bool IntersectsLine(const Line3 *line) = 0;          // slot 27
    // 28 (shared 0x190ed0, a tail jump into the linear's slot 13, which pops the argument): linear->IntersectsBound(this).
    virtual bool IntersectsLinear(cILinear3 *linear) = 0;
    virtual bool FirstHitSegmentIn(float *t, const AffineTransform3 *frame, const Segment3 *segment) = 0; // slot 29
    virtual bool FirstHitRayIn(float *t, const AffineTransform3 *frame, const Ray3 *ray) = 0;             // slot 30
    virtual bool FirstHitLineIn(float *t, const AffineTransform3 *frame, const Line3 *line) = 0;          // slot 31
    // 32 (shared 0x191f50): linear->FirstHitBoundIn(t, this, frame).
    virtual bool FirstHitLinearIn(float *t, const AffineTransform3 *frame, cILinear3 *linear) = 0;
    virtual bool FirstHitSegment(float *t, const Segment3 *segment) = 0; // slot 33
    virtual bool FirstHitRay(float *t, const Ray3 *ray) = 0;             // slot 34
    virtual bool FirstHitLine(float *t, const Line3 *line) = 0;          // slot 35
    // 36 (shared 0x191fd0, a tail jump into the linear's slot 24): linear->FirstHitBound(t, this).
    virtual bool FirstHitLinear(float *t, cILinear3 *linear) = 0;
    // 37-39 return a box by value in the original: written through the hidden result pointer, which comes back in eax.
    // Spelled out here, so that a caller reads the box from its own `out` whatever the callee leaves in eax.
    virtual BoundBox *GetBoundBoxIn(BoundBox *out, const AffineTransform3 *frame) = 0;   // 37: this bound placed by `frame`
    virtual BoundBox *GetBoundBoxRigid(BoundBox *out, const RigidTransform3 *rigid) = 0; // 38: placed by a rigid transform
    virtual BoundBox *GetBoundBox(BoundBox *out) = 0;                                    // 39: the box as it is

    // The numbered forms some callers still use (src/sg): the same calls.
    uint32_t Slot19(uint32_t frame, uint32_t cuboid, uint32_t otherFrame)
    {
        return IntersectsCuboidInAt(reinterpret_cast<const AffineTransform3 *>(uintptr_t(frame)),
                                    reinterpret_cast<const Cuboid *>(uintptr_t(cuboid)),
                                    reinterpret_cast<const AffineTransform3 *>(uintptr_t(otherFrame)));
    }
    uint32_t Slot20(uint32_t frame, uint32_t bound, uint32_t otherFrame)
    {
        return IntersectsBoundInAt(reinterpret_cast<const AffineTransform3 *>(uintptr_t(frame)),
                                   reinterpret_cast<cIBound *>(uintptr_t(bound)),
                                   reinterpret_cast<const AffineTransform3 *>(uintptr_t(otherFrame)));
    }
    uint32_t Slot37(uint32_t out, uint32_t frame)
    {
        GetBoundBoxIn(reinterpret_cast<BoundBox *>(uintptr_t(out)), reinterpret_cast<const AffineTransform3 *>(uintptr_t(frame)));
        return out;
    }
    uint32_t Slot38(uint32_t out, uint32_t rigid)
    {
        GetBoundBoxRigid(reinterpret_cast<BoundBox *>(uintptr_t(out)), reinterpret_cast<const RigidTransform3 *>(uintptr_t(rigid)));
        return out;
    }
};

// nGZCompGeom::cILinear3 (id 0xfce591cb): lines, rays and segments (slots 5-33). Every test returns AL. "In" variants take
// the other shape in its own frame; "FirstHit" variants also write the parameter of the first intersection to *t; a
// cIBound argument dispatches to the bound's slot for this kind.
class cILinear3 : public cIShape {
public:
    static constexpr uint32_t kInterfaceId = 0xfce591cb;
    virtual void Reframe(const AffineTransform3 *from, const AffineTransform3 *to) = 0; // slot 5: out of `from`, into `to`
    virtual void Transform(const AffineTransform3 *frame) = 0;                         // slot 6
    virtual void InverseTransform(const AffineTransform3 *frame) = 0;                  // slot 7
    virtual bool IntersectsOrientedCone(const OrientedCone *cone) = 0;                 // slot 8
    virtual bool IntersectsTranslatedCone(const TranslatedCone *cone) = 0;             // slot 9
    virtual bool IntersectsTranslatedSphere(const TranslatedSphere *sphere) = 0;       // slot 10
    virtual bool IntersectsOrientedCuboid(const OrientedCuboid *cuboid) = 0;           // slot 11
    virtual bool IntersectsTranslatedCuboid(const TranslatedCuboid *cuboid) = 0;       // slot 12
    virtual bool IntersectsBound(cIBound *bound) = 0;                                  // slot 13
    virtual bool IntersectsTriangleSetIn(const TriangleSet *set, const AffineTransform3 *frame) = 0; // slot 14
    virtual bool IntersectsConeIn(const Cone *cone, const AffineTransform3 *frame) = 0;              // slot 15
    virtual bool IntersectsSphereIn(const Sphere *sphere, const AffineTransform3 *frame) = 0;        // slot 16
    virtual bool IntersectsCuboidIn(const Cuboid *cuboid, const AffineTransform3 *frame) = 0;        // slot 17
    virtual bool IntersectsBoundIn(cIBound *bound, const AffineTransform3 *frame) = 0;               // slot 18
    virtual bool FirstHitOrientedCone(float *t, const OrientedCone *cone) = 0;           // slot 19
    virtual bool FirstHitTranslatedCone(float *t, const TranslatedCone *cone) = 0;       // slot 20
    virtual bool FirstHitTranslatedSphere(float *t, const TranslatedSphere *sphere) = 0; // slot 21
    virtual bool FirstHitOrientedCuboid(float *t, const OrientedCuboid *cuboid) = 0;     // slot 22
    virtual bool FirstHitTranslatedCuboid(float *t, const TranslatedCuboid *cuboid) = 0; // slot 23
    virtual bool FirstHitBound(float *t, cIBound *bound) = 0;                            // slot 24
    virtual bool FirstHitTriangleSetIn(float *t, const TriangleSet *set, const AffineTransform3 *frame) = 0; // slot 25
    virtual bool FirstHitConeIn(float *t, const Cone *cone, const AffineTransform3 *frame) = 0;              // slot 26
    virtual bool FirstHitSphereIn(float *t, const Sphere *sphere, const AffineTransform3 *frame) = 0;        // slot 27
    virtual bool FirstHitCuboidIn(float *t, const Cuboid *cuboid, const AffineTransform3 *frame) = 0;        // slot 28
    virtual bool FirstHitBoundIn(float *t, cIBound *bound, const AffineTransform3 *frame) = 0;               // slot 29
    virtual Vec3 GetDirection() = 0;                // slot 30 (a segment's is end - start)
    virtual Vec3 GetOrigin() = 0;                   // slot 31 (a segment's start)
    virtual bool ContainsPoint(const Vec3 *p) = 0;  // slot 32
    virtual Vec3 PointAt(float t) = 0;              // slot 33: origin + t * direction
};

// nGZCompGeom::cIShapeT<Data, Kind>: the shape's data (slots 34 and 35 return the same pointer: a const and a non-const
// accessor; both are the folded `lea eax, [ecx + 4]` shared_vf_18ea60).
template <class Data, class Kind> class cIShapeT : public Kind {
public:
    using Shape = Data;
    virtual Data *ShapeData() = 0;            // slot 34
    virtual const Data *ShapeDataConst() = 0; // slot 35
};

// ---- the linear shapes' data ----

// The data of a line or a ray: nGZCompGeom::cLine<3, cTypeTraits<float>> / cRay<3, ...>.
struct PointDirection3 {
    Vec3 origin;    // +0
    Vec3 direction; // +0xc (not normalised)

    void Reframe(const AffineTransform3 &from, const AffineTransform3 &to)
    {
        origin = FramePoint(to, FrameInversePoint(from, origin));
        direction = FrameVector(to, FrameInverseVector(from, direction));
    }
    void Transform(const AffineTransform3 &frame)
    {
        origin = FramePoint(frame, origin);
        direction = FrameVector(frame, direction);
    }
    void InverseTransform(const AffineTransform3 &frame)
    {
        origin = FrameInversePoint(frame, origin);
        direction = FrameInverseVector(frame, direction);
    }
    Vec3 Direction() const { return direction; }
    Vec3 Origin() const { return origin; }
    Vec3 PointAt(float t) const
    {
        return {t * direction.x + origin.x, t * direction.y + origin.y, t * direction.z + origin.z};
    }
};
struct Line3 : PointDirection3 {
    bool Contains(const Vec3 &p) const; // 0x18ec30: (d.(p-o))^2 nearly equals |d|^2 |p-o|^2
};
struct Ray3 : PointDirection3 {
    bool Contains(const Vec3 &p) const; // inline in slot 32 (0x18ed30): the origin, or ahead of it on the line
};
// 0x1977d0 (cdecl; called by the ray's slot 32): v (a point less the ray's origin) points ahead along d (d.v > 0, or NaN)
// and is parallel to it.
bool __cdecl AheadAlong(const Vec3 *v, const Vec3 *d);
static_assert(sizeof(Line3) == 0x18 && sizeof(Ray3) == 0x18);

// nGZCompGeom::cSegment<3, cTypeTraits<float>>: from `start` to `end`.
struct Segment3 {
    Vec3 start; // +0
    Vec3 end;   // +0xc

    void Reframe(const AffineTransform3 &from, const AffineTransform3 &to)
    {
        start = FramePoint(to, FrameInversePoint(from, start));
        end = FramePoint(to, FrameInversePoint(from, end));
    }
    void Transform(const AffineTransform3 &frame)
    {
        start = FramePoint(frame, start);
        end = FramePoint(frame, end);
    }
    void InverseTransform(const AffineTransform3 &frame)
    {
        start = FrameInversePoint(frame, start);
        end = FrameInversePoint(frame, end);
    }
    Vec3 Direction() const { return {end.x - start.x, end.y - start.y, end.z - start.z}; }
    Vec3 Origin() const { return start; }
    Vec3 PointAt(float t) const
    {
        const Vec3 d = Direction();
        return {d.x * t + start.x, d.y * t + start.y, d.z * t + start.z};
    }
    bool Contains(const Vec3 &p) const; // 0x18ee90 (inside slot 32's 0x18ee80): within the box, then on the line
};
static_assert(sizeof(Segment3) == 0x18);

// The shape interfaces (ids: the class ids the director creates them by).
class cILine3 : public cIShapeT<Line3, cILinear3> {
public:
    static constexpr uint32_t kInterfaceId = 0x3ce57cc0;
};
class cIRay3 : public cIShapeT<Ray3, cILinear3> {
public:
    static constexpr uint32_t kInterfaceId = 0x7ce57eff;
};
class cISegment3 : public cIShapeT<Segment3, cILinear3> {
public:
    static constexpr uint32_t kInterfaceId = 0x3ce57f31;
};

} // namespace nGZCompGeom
