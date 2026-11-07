#pragma once
#include <cstddef>
#include <cstdint>

#include "compgeom/CompGeom.h"

// nGZCompGeom::cFrustumShape (lane PV, scenegraph G0b step 5; RVAs 1.18.0.187): the six planes of a camera's view
// frustum, kept as a shape (id 0x0a6e6298), and the cull tests on them. A reference-counted COM object, 0x68 bytes: the
// vtable, the planes at +4 (six times a, b, c, d: a x + b y + c z + d is positive on the outer side), the count at +0x64.
// It is a cIShape only (slots 0-4, the data accessors 5 and 6, the destructor 7): there are no pair-test slots; the
// scene graph's cull code (cLooseOctreePartitionNode, the display lists, the overlay and particle nodes) calls the
// Frustum members below on the data.
//
// Standalone shape (docs/standalone-shape.md): real C++ classes, the compiler emits the vtables. Two, as the convex
// shapes': the bound (cShapeT<cIFrustumShape>, vtable 0xda1ef4, what Clone's copies get) and the concrete shape deriving
// from it (vtable 0xda1f18; what the director's factory 0x197740 makes). Their slots are the same code. The factory is
// hooked to Create; the generic slots are in effect through callers. The data functions are hooked at their RVAs: the
// original callers pass pointers to the data of shapes the camera code owns.
namespace nGZCompGeom {

// nGZCompGeom::cFrustum<cTypeTraits<float>>: six planes, 0x60 bytes. The planes, in order: near, far, left, right, top,
// bottom (FromView's rows 2, 3 - 2, 3 + 0, 3 - 0, 3 - 1, 3 + 1, each negated).
struct Frustum {
    float plane[6][4];

    // 0x1975a0 (__thiscall, ret 4; no result): the planes of a 4x4 view-projection matrix (sixteen floats, row after
    // row), each combination of rows negated by flipping the sign bits. eax-ignored: eax is the matrix pointer (the last
    // thing loaded); no caller reads it.
    void FromView(const float *matrix);

    // The three cull tests: `mask` holds a bit per plane the box is known to be inside (the planes are skipped); the bits
    // for planes the box lies wholly inside are set. Returns 1 at the first plane the box lies wholly outside of (the mask
    // then stays as it was), otherwise 0, or -1 when all six bits are set after the pass.
    // 0x197520 (__thiscall, ret 0xc): a box of half extents `half` about `centre`.
    int ClassifyBoxAt(uint8_t *mask, const Vec3 *half, const Vec3 *centre) const;
    // 0x1974a0 (__thiscall, ret 0xc): a box of half extents `half` carried by `frame` (the frame's offset is the centre).
    int ClassifyBoxInFrame(uint8_t *mask, const Vec3 *half, const AffineTransform3 *frame) const;
    // 0x197330 (__thiscall, ret 0xc): the axis-aligned box `box` carried by `frame`.
    int ClassifyBox(uint8_t *mask, const BoundBox *box, const AffineTransform3 *frame) const;
};
static_assert(sizeof(Frustum) == 0x60);

// Which side of `plane` (a, b, c, d) a box of half extents `half` about the point `centre` lies on, the reach r = sum
// |a h.x|, |b h.y|, |c h.z| against the centre's distance d = a c.x + b c.y + c c.z + w:
// 0x1a28c0 (__cdecl: plane, half, centre): -1 when d + r is negative, 1 when r - d is negative, else 0 (the sign bits).
int __cdecl PlaneBoxSide(const float *plane, const Vec3 *half, const Vec3 *centre);
// 0x1a2810 (__cdecl: plane, half, frame): the same for a box carried by `frame`: the plane's normal turned into the
// box's frame (ConvexFrame::TransposedVectorTo) for the reach, the frame's offset the centre.
int __cdecl PlaneFrameBoxSide(const float *plane, const Vec3 *half, const AffineTransform3 *frame);

// ---- the shape ----

// nGZCompGeom::cIFrustumShape (id 0x0a6e6298, the class id).
class cIFrustumShape : public cIShapeT<Frustum, cIShape> {
public:
    static constexpr uint32_t kInterfaceId = 0x0a6e6298;
};

// cShapeT<cIFrustumShape> (vtable 0xda1ef4; Clone's copies), 0x68 bytes: the planes at +4, the count at +0x64.
class cFrustumBound : public cIFrustumShape {
public:
    cFrustumBound() : mRefCount(0) {}
    cFrustumBound(const cFrustumBound &) = delete;
    virtual ~cFrustumBound() {} // slot 7 (the deleting destructor: operator delete on bit 0)

    bool QueryInterface(uint32_t id, void **out) override; // 0x197780: 1, the class id, cIShape
    uint32_t AddRef() override { return ++mRefCount; }     // 0x1972b0 (shared with unrelated classes)
    uint32_t Release() override;                           // 0x1977b0: at zero, delete this and return 0
    bool Clone(uint32_t id, void **out) override;          // 0x1972c0: a bound copy (count 0), queried for `id`
    uint32_t GetClassID() override { return kInterfaceId; } // 0x197770
    Frustum *ShapeData() override { return &mData; }                // 0x18ea60 (4 bytes, shared)
    const Frustum *ShapeDataConst() override { return &mData; }     // 0x18ea60

    Frustum mData;      // +4
    uint32_t mRefCount; // +0x64
};

// nGZCompGeom::cFrustumShape (vtable 0xda1f18; the director's factory 0x197740).
class cFrustumShape : public cFrustumBound {
public:
    cFrustumShape() {}
    ~cFrustumShape() override {}
    static cFrustumShape *__cdecl Create(); // 0x197740: a shape with count 0 and the planes unset
};
static_assert(sizeof(cFrustumShape) == 0x68);
static_assert(offsetof(cFrustumBound, mData) == 4);
static_assert(offsetof(cFrustumBound, mRefCount) == 0x64);

} // namespace nGZCompGeom
