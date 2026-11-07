#pragma once
#include <cstddef>
#include <cstdint>

#include "compgeom/CompGeom.h"

// nGZCompGeom::cLinear3T<I> and its three concrete classes cLine3, cRay3, cSegment3 (RVAs 1.18.0.187).
//
// Layout (0x20 bytes): +0 the vtable, +4 the shape data (Line3 / Ray3 / Segment3, 0x18 bytes), +0x1c the reference count.
// Vtables (37 slots each; slot 36 the deleting destructor):
//   cLinear3T<cILine3> 0xda11d4, cLine3 0xda126c; cLinear3T<cIRay3> 0xda1304, cRay3 0xda139c;
//   cLinear3T<cISegment3> 0xda1434, cSegment3 0xda14cc.
// The template's own vtable is what Clone (slot 3) gives its copies and what the template's destructor leaves; the
// concrete class's is what the director's factories and the constructors store. Both hold the same slot functions except
// slot 36.
//
// Standalone shape (docs/standalone-shape.md): real C++ classes; the compiler emits the six vtables. Creation paths:
//   - the director's factories 0x18ead0 (line) / 0x18eb00 (ray) / 0x18eb30 (segment): hooked to Create;
//   - the constructors 0x18e6b0 / 0x18e720 / 0x18e780 (ray) and 0x18e7c0 (segment), called on stack objects all over the
//     game: hooked to placement-new adapters;
//   - Clone (slot 3, ours through our vtable; its RVA hooks are adapters);
//   - inline: tMonitorEntry::Step 0x2299b7 (ours, src/audio) stores the game's cRay3 vtable itself, and the world-lot
//     functions 0x358c60 / 0x35d260 store the game's cSegment3 vtable inline. Those objects carry the game's vtables, so the
//     slot adapters (the RVA hooks) stay for the ray and the segment.
// The game also calls several slot bodies directly on its stack objects (GetDirection 0x18ea70, GetOrigin 0x18f5f0,
// PointAt 0x18f430, the destructors 0x18e860 / 0x18e870, the assignment 0x18e880): their hooks are adapters as well.
namespace nGZCompGeom {

template <class I> class cLinear3T : public I {
public:
    using Shape = typename I::Shape;
    static constexpr uint32_t kClassId = I::kInterfaceId; // also the factory's class id
    static constexpr uint32_t kSize = 0x20;

    cLinear3T() : mRefCount(0) {} // the factories leave the data as allocated
    explicit cLinear3T(const Shape &shape) : mShape(shape), mRefCount(0) {}
    cLinear3T(const cLinear3T &) = delete;
    virtual ~cLinear3T() {} // slot 36 (the scalar deleting destructor: this, then operator delete(this, 0x20) on bit 0)

    // 0x18e880 (folded for every kind): copies the shape data only; returns this.
    cLinear3T &Assign(const cLinear3T &other)
    {
        mShape = other.mShape;
        return *this;
    }

    // cIGZUnknown
    bool QueryInterface(uint32_t id, void **out) override; // slot 0: 1, cIShape, cILinear3 and the kind's id
    uint32_t AddRef() override;                            // slot 1 (folded 0x18e960)
    uint32_t Release() override;                           // slot 2 (0x18f5d0): at zero, delete this and return 0
    // cIShape
    bool Clone(uint32_t id, void **out) override; // slot 3: a template object (not the concrete class) with a copy
    uint32_t GetClassID() override;               // slot 4
    // cILinear3
    void Reframe(const AffineTransform3 *from, const AffineTransform3 *to) override;
    void Transform(const AffineTransform3 *frame) override;
    void InverseTransform(const AffineTransform3 *frame) override;
    bool IntersectsOrientedCone(const OrientedCone *cone) override;
    bool IntersectsTranslatedCone(const TranslatedCone *cone) override;
    bool IntersectsTranslatedSphere(const TranslatedSphere *sphere) override;
    bool IntersectsOrientedCuboid(const OrientedCuboid *cuboid) override;
    bool IntersectsTranslatedCuboid(const TranslatedCuboid *cuboid) override;
    bool IntersectsBound(cIBound *bound) override;
    bool IntersectsTriangleSetIn(const TriangleSet *set, const AffineTransform3 *frame) override;
    bool IntersectsConeIn(const Cone *cone, const AffineTransform3 *frame) override;
    bool IntersectsSphereIn(const Sphere *sphere, const AffineTransform3 *frame) override;
    bool IntersectsCuboidIn(const Cuboid *cuboid, const AffineTransform3 *frame) override;
    bool IntersectsBoundIn(cIBound *bound, const AffineTransform3 *frame) override;
    bool FirstHitOrientedCone(float *t, const OrientedCone *cone) override;
    bool FirstHitTranslatedCone(float *t, const TranslatedCone *cone) override;
    bool FirstHitTranslatedSphere(float *t, const TranslatedSphere *sphere) override;
    bool FirstHitOrientedCuboid(float *t, const OrientedCuboid *cuboid) override;
    bool FirstHitTranslatedCuboid(float *t, const TranslatedCuboid *cuboid) override;
    bool FirstHitBound(float *t, cIBound *bound) override;
    bool FirstHitTriangleSetIn(float *t, const TriangleSet *set, const AffineTransform3 *frame) override;
    bool FirstHitConeIn(float *t, const Cone *cone, const AffineTransform3 *frame) override;
    bool FirstHitSphereIn(float *t, const Sphere *sphere, const AffineTransform3 *frame) override;
    bool FirstHitCuboidIn(float *t, const Cuboid *cuboid, const AffineTransform3 *frame) override;
    bool FirstHitBoundIn(float *t, cIBound *bound, const AffineTransform3 *frame) override;
    Vec3 GetDirection() override;
    Vec3 GetOrigin() override;
    bool ContainsPoint(const Vec3 *p) override;
    Vec3 PointAt(float t) override;
    // cIShapeT
    Shape *ShapeData() override;            // slot 34 (folded 0x18ea60)
    const Shape *ShapeDataConst() override; // slot 35 (folded 0x18ea60)

    // +0x00: the vtable pointer
    Shape mShape;       // +0x04
    uint32_t mRefCount; // +0x1c
};

// nGZCompGeom::cLine3 (factory 0x18ead0, class id 0x3ce57cc0).
class cLine3 : public cLinear3T<cILine3> {
public:
    // vtables: 0xda126c (template 0xda11d4)
    cLine3() {}
    ~cLine3() override {}
    static cLine3 *__cdecl Create(); // 0x18ead0
};

// nGZCompGeom::cRay3 (factory 0x18eb00, class id 0x7ce57eff).
class cRay3 : public cLinear3T<cIRay3> {
public:
    // vtables: 0xda139c (template 0xda1304)
    cRay3() {}
    cRay3(const Vec3 &origin, const Vec3 &direction) // 0x18e720
    {
        mShape.origin = origin;
        mShape.direction = direction;
    }
    struct Through {}; // tag: the ray from `from` through `to`
    cRay3(Through, const Vec3 &from, const Vec3 &to) // 0x18e6b0: direction = to - from
    {
        mShape.origin = from;
        mShape.direction = {to.x - from.x, to.y - from.y, to.z - from.z};
    }
    explicit cRay3(const Ray3 &ray) : cLinear3T(ray) {} // 0x18e780
    ~cRay3() override {}
    static cRay3 *__cdecl Create(); // 0x18eb00
};

// nGZCompGeom::cSegment3 (factory 0x18eb30, class id 0x3ce57f31).
class cSegment3 : public cLinear3T<cISegment3> {
public:
    // vtables: 0xda14cc (template 0xda1434)
    cSegment3() {}
    cSegment3(const Vec3 &start, const Vec3 &end) // 0x18e7c0
    {
        mShape.start = start;
        mShape.end = end;
    }
    ~cSegment3() override {}
    static cSegment3 *__cdecl Create(); // 0x18eb30
};

static_assert(sizeof(cLine3) == 0x20 && sizeof(cRay3) == 0x20 && sizeof(cSegment3) == 0x20);
static_assert(offsetof(cRay3, mShape) == 4 && offsetof(cRay3, mRefCount) == 0x1c);

extern template class cLinear3T<cILine3>;
extern template class cLinear3T<cIRay3>;
extern template class cLinear3T<cISegment3>;

// The RVA hooks' entry points (src/hook/tables/CompGeomLinear3.cpp): the old slot bodies, constructors and destructors,
// for direct calls and for objects carrying the game's vtables (the inline builders above). Original calling convention
// (this in ecx), our members called without virtual dispatch, bodies inlined. One set per template instantiation; a body
// the original folded within the family (line and ray) is hooked once, through one instantiation (the data layouts are
// the same).
template <class I> struct cLinear3TAdapters {
    using Self = cLinear3T<I>;
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t id, void **out);
    static uint32_t __fastcall Release(Self *self, void *);
    static bool __fastcall Clone(Self *self, void *, uint32_t id, void **out);
    static uint32_t __fastcall GetClassID(Self *self, void *);
    static void __fastcall Reframe(Self *self, void *, const AffineTransform3 *from, const AffineTransform3 *to);
    static void __fastcall Transform(Self *self, void *, const AffineTransform3 *frame);
    static void __fastcall InverseTransform(Self *self, void *, const AffineTransform3 *frame);
    static bool __fastcall IntersectsOrientedCone(Self *self, void *, const OrientedCone *cone);
    static bool __fastcall IntersectsTranslatedCone(Self *self, void *, const TranslatedCone *cone);
    static bool __fastcall IntersectsTranslatedSphere(Self *self, void *, const TranslatedSphere *sphere);
    static bool __fastcall IntersectsOrientedCuboid(Self *self, void *, const OrientedCuboid *cuboid);
    static bool __fastcall IntersectsTranslatedCuboid(Self *self, void *, const TranslatedCuboid *cuboid);
    static bool __fastcall IntersectsBound(Self *self, void *, cIBound *bound);
    static bool __fastcall IntersectsTriangleSetIn(Self *self, void *, const TriangleSet *set, const AffineTransform3 *frame);
    static bool __fastcall IntersectsConeIn(Self *self, void *, const Cone *cone, const AffineTransform3 *frame);
    static bool __fastcall IntersectsSphereIn(Self *self, void *, const Sphere *sphere, const AffineTransform3 *frame);
    static bool __fastcall IntersectsCuboidIn(Self *self, void *, const Cuboid *cuboid, const AffineTransform3 *frame);
    static bool __fastcall IntersectsBoundIn(Self *self, void *, cIBound *bound, const AffineTransform3 *frame);
    static bool __fastcall FirstHitOrientedCone(Self *self, void *, float *t, const OrientedCone *cone);
    static bool __fastcall FirstHitTranslatedCone(Self *self, void *, float *t, const TranslatedCone *cone);
    static bool __fastcall FirstHitTranslatedSphere(Self *self, void *, float *t, const TranslatedSphere *sphere);
    static bool __fastcall FirstHitOrientedCuboid(Self *self, void *, float *t, const OrientedCuboid *cuboid);
    static bool __fastcall FirstHitTranslatedCuboid(Self *self, void *, float *t, const TranslatedCuboid *cuboid);
    static bool __fastcall FirstHitBound(Self *self, void *, float *t, cIBound *bound);
    static bool __fastcall FirstHitTriangleSetIn(Self *self, void *, float *t, const TriangleSet *set,
                                                 const AffineTransform3 *frame);
    static bool __fastcall FirstHitConeIn(Self *self, void *, float *t, const Cone *cone, const AffineTransform3 *frame);
    static bool __fastcall FirstHitSphereIn(Self *self, void *, float *t, const Sphere *sphere, const AffineTransform3 *frame);
    static bool __fastcall FirstHitCuboidIn(Self *self, void *, float *t, const Cuboid *cuboid, const AffineTransform3 *frame);
    static bool __fastcall FirstHitBoundIn(Self *self, void *, float *t, cIBound *bound, const AffineTransform3 *frame);
    static Vec3 *__fastcall GetDirection(Self *self, void *, Vec3 *out);
    static Vec3 *__fastcall GetOrigin(Self *self, void *, Vec3 *out);
    static bool __fastcall ContainsPoint(Self *self, void *, const Vec3 *p);
    static Vec3 *__fastcall PointAt(Self *self, void *, Vec3 *out, float t);
};

struct Linear3Adapters {
    // Constructors and destructors.
    static cRay3 *__fastcall RayConstruct(void *memory, void *, const Vec3 *origin, const Vec3 *direction);
    static cRay3 *__fastcall RayConstructThrough(void *memory, void *, const Vec3 *from, const Vec3 *to);
    static cRay3 *__fastcall RayConstructFromShape(void *memory, void *, const Ray3 *ray);
    static cSegment3 *__fastcall SegmentConstruct(void *memory, void *, const Vec3 *start, const Vec3 *end);
    static void __fastcall LineDestroy(cLine3 *self, void *);
    static void __fastcall RayDestroy(cRay3 *self, void *);
    static void __fastcall SegmentDestroy(cSegment3 *self, void *);
    static void __fastcall LineTemplateDestroy(cLinear3T<cILine3> *self, void *);
    static void __fastcall RayTemplateDestroy(cLinear3T<cIRay3> *self, void *);
    static void __fastcall SegmentTemplateDestroy(cLinear3T<cISegment3> *self, void *);
    static cLine3 *__fastcall LineDeletingDestructor(cLine3 *self, void *, uint32_t flags);
    static cRay3 *__fastcall RayDeletingDestructor(cRay3 *self, void *, uint32_t flags);
    static cSegment3 *__fastcall SegmentDeletingDestructor(cSegment3 *self, void *, uint32_t flags);
    static cLinear3T<cILine3> *__fastcall LineTemplateDeletingDestructor(cLinear3T<cILine3> *self, void *, uint32_t flags);
    static cLinear3T<cIRay3> *__fastcall RayTemplateDeletingDestructor(cLinear3T<cIRay3> *self, void *, uint32_t flags);
    static cLinear3T<cISegment3> *__fastcall SegmentTemplateDeletingDestructor(cLinear3T<cISegment3> *self, void *,
                                                                                uint32_t flags);
    static cLinear3T<cIRay3> *__fastcall Assign(cLinear3T<cIRay3> *self, void *, const cLinear3T<cIRay3> *other);
};

} // namespace nGZCompGeom
