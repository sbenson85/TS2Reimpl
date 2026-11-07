#pragma once
// nGZSceneGraph::cIGeometryRender (lane QR, scenegraph D3 part 2): the 10-slot interface the display list draws
// dynamic geometry through (cDynamicGeometryRect is the first class on it; cTSParticlesGeometry's vtable shares its
// slot 5 body). The names are ours; what each slot does is from cDynamicGeometryRect's bodies.
//
//   slots 0-2  cIGZUnknown (the count at +4)
//   3          IsSupported(uint32_t): the rect says true to everything
//   4          GetFlags: the address of the object's flag word (+8); the display list reads it as a GeometryInfo
//   5          GetDrawInfo(uint32_t): the address of two words inside the object (+0x34 in the rect: 4, 1); the display
//              list reads it as the subset's GeometrySubset
//   6          Prepare(4 words): the rect answers 0
//   7          Begin(index, context, first): draws a strip of two primitives (the strip type 5) from vertex `first`
//   8          Render(unused, context, flag, unused): fills the context's dynamic arrays and draws; answers the
//              vertex array's span start
//   9          the deleting destructor
#include "compgeom/CompGeom.h" // BoundBox, RigidTransform3
#include "gz/cIGZUnknown.h"

#include <cstddef>
#include <cstdint>

namespace nGZGraphic4 {
class cIDeviceDX;
class cIDeviceState;
class cIDynamicVertexArray;
} // namespace nGZGraphic4

namespace nGZSceneGraph {

// The draw context the geometry slots get (only the fields they read; everything between is the context's own).
struct cGeometryRenderContext {
    uint8_t mBefore[0xc0];
    uint32_t mWidth;  // +0xc0: pixels (the half-texel offset is 0.5 / this)
    uint32_t mHeight; // +0xc4
    uint8_t mBeforeView[0xdc - 0xc8];
    AffineTransform3 mView;                         // +0xdc (cDisplayList's Pass: DrawViewer's frame)
    AffineTransform3 mViewInverse;                  // +0x110
    float mProjection[16];                          // +0x144
    uint8_t mBetween[0x2a8 - 0x184];
    nGZGraphic4::cIDeviceDX *mDevice;               // +0x2a8
    nGZGraphic4::cIDeviceState *mState;             // +0x2ac
    nGZGraphic4::cIDynamicVertexArray *mVertices;   // +0x2b0 (used when the render flag is set)
    nGZGraphic4::cIDynamicVertexArray *mVerticesB;  // +0x2b4 (flag clear)
    nGZGraphic4::cIDynamicVertexArray *mIndices;    // +0x2b8 (flag set)
    nGZGraphic4::cIDynamicVertexArray *mIndicesB;   // +0x2bc (flag clear)
};
static_assert(offsetof(cGeometryRenderContext, mHeight) == 0xc4);
static_assert(offsetof(cGeometryRenderContext, mDevice) == 0x2a8);
static_assert(offsetof(cGeometryRenderContext, mIndicesB) == 0x2bc);

// The vertex format object a subset names (slot 3 the only one the display list asks).
class cIGeometryFormat : public cIGZUnknown {
public:
    virtual uint32_t GetFormatBits() = 0; // 3: the 7 bits the display list puts in an item's state bits 4-10
};

// What slot 4 points at, as the display list's slot 28 (0xa9e700) reads it: the subset count, the bones and their bind
// frames (rigid transforms, 0x1c bytes each).
struct GeometryInfo {
    uint32_t mSubsets;                          // +0x00
    uint32_t mWord04;
    uint32_t mBones;                            // +0x08
    const nGZCompGeom::RigidTransform3 *mBoneFrames; // +0x0c
};

// What slot 5 points at for a subset (the builder 0xaeae00 reads the layer, the format and +0x34; slot 28 all of it).
struct GeometrySubset {
    uint32_t mWords[2];
    int32_t mLayer;                  // +0x08
    cIGeometryFormat *mFormat;       // +0x0c
    uint32_t mBoneCount;             // +0x10 the bones the subset uses (0: none)
    const int32_t *mBoneIndices;     // +0x14 the first is the subset's frame for its depth
    uint32_t mBlendCount;            // +0x18 the second count the material variant is asked for
    nGZCompGeom::BoundBox mBounds;   // +0x1c
    uint8_t mFlag;                   // +0x34 the state's bit 0
    uint8_t mSubKeyFlag;             // +0x35 the sub key's bit 16
};
static_assert(offsetof(GeometrySubset, mBounds) == 0x1c && offsetof(GeometrySubset, mFlag) == 0x34 &&
                  offsetof(GeometrySubset, mSubKeyFlag) == 0x35,
              "the display list's reads");

class cIGeometryRender : public cIGZUnknown {
public:
    virtual bool IsSupported(uint32_t what) = 0;                                                    // 3
    virtual uint32_t *GetFlags() = 0;                                                               // 4
    virtual uint32_t *GetDrawInfo(uint32_t what) = 0;                                               // 5
    virtual uint32_t Prepare(uint32_t a, uint32_t b, uint32_t c, uint32_t d) = 0;                   // 6
    virtual void Begin(uint32_t index, cGeometryRenderContext *context, uint32_t first) = 0;    // 7
    virtual uint32_t Render(uint32_t unused0, cGeometryRenderContext *context, uint8_t flag, uint32_t unused3) = 0; // 8
    virtual ~cIGeometryRender() = default;                                                          // 9
};

} // namespace nGZSceneGraph
