#pragma once
// nGZSceneGraph::cDynamicGeometryRect (lane QR, scenegraph D3 part 2): a grid of textured quads the display list draws
// into the context's dynamic vertex and index arrays (a screen-space rectangle split nx by ny cells, with optional
// half-texel shift and mirrored texture rows). The game makes it inline at three sites (0x377920 the censor render
// strategy, 0xaa0eb0, 0xaa8810: new 0xb0 and the constructor); there is no factory. The object is 0xb0 bytes:
//   +0 vptr (0xe69af4, 10 slots: cIGeometryRender)   +4 the count   +8 a flag word (1)   +0xc..+0x1b zeros
//   +0x1c bounds A (min, max: -1000 / 1000)   +0x34 the draw info words (4, 1)   +0x3c not written by the constructor
//   +0x40 the format again (raw)   +0x44..+0x4f zeros   +0x50 bounds B   +0x68 a short (1)   +0x6a not written
//   +0x6c the vertex format (counted)   +0x70 its stride   +0x74 the position offset   +0x78 the texture offset
//   +0x7c z (0)   +0x80 x0 -1  +0x84 x1 1  +0x88 y0 -1  +0x8c y1 1  +0x90 u0 0  +0x94 u1 1  +0x98 v0 0  +0x9c v1 1
//   +0xa0 nx 1.0f  +0xa4 ny 1.0f (cell counts, floats)  +0xa8 the UV flag (int, 0)  +0xac the vertex declaration (counted)
//
// Standalone shape (docs/standalone-shape.md): the constructor 0xa43ec0 is hooked (the three sites then build ours) and
// so are the destructor 0xa440f0, the deleting destructor (slot 9) and slots 7 and 8; the other slots are folded bodies
// (shared with other classes) reached through our vtable. The services it asks (the vertex formats and declarations
// managers, the current device) are the cached getters of src/gz/GraphicServices.h.
#include "gz/cVertexFormat.h"
#include "sg/cIGeometryRender.h"

#include <cstddef>
#include <cstdint>

namespace nGZSceneGraph {

struct cBounds {
    float mMin[3];
    float mMax[3];
};

class cDynamicGeometryRect final : public cIGeometryRender {
public:
    cDynamicGeometryRect(); // 0xa43ec0
    ~cDynamicGeometryRect() override; // 0xa440f0 (slot 9: deleting 0xa44150)

    bool QueryInterface(uint32_t iid, void **out) override; // 0: 1 and 0x89d39f12 answer this, counted (folded 0x3af660)
    uint32_t AddRef() override { return ++mRefs; }          // 1 (folded 0x148fb0)
    uint32_t Release() override;                            // 2 (folded 0x153f50)
    bool IsSupported(uint32_t) override { return true; }    // 3 (folded 0x148260)
    uint32_t *GetFlags() override { return &mFlags; }       // 4 (folded 0x142840)
    uint32_t *GetDrawInfo(uint32_t) override { return mDrawInfo; } // 5 (folded 0xa441a0)
    uint32_t Prepare(uint32_t, uint32_t, uint32_t, uint32_t) override { return 0; } // 6 (folded 0x18a5b0)
    void Begin(uint32_t index, cGeometryRenderContext *context, uint32_t first) override; // 7: 0xa44180
    uint32_t Render(uint32_t unused0, cGeometryRenderContext *context, uint8_t flag, uint32_t unused3) override; // 8: 0xa441b0

    uint32_t mRefs;                          // +4 (0)
    uint32_t mFlags;                         // +8 (1)
    uint32_t mZeros[4];                      // +0xc
    cBounds mBoundsA;                        // +0x1c
    uint32_t mDrawInfo[2];                   // +0x34
    uint32_t mUnset3c;                       // +0x3c (not written by the constructor)
    nGZGraphic4::cIVertexFormat *mFormatRaw; // +0x40
    uint32_t mZeros2[3];                     // +0x44
    cBounds mBoundsB;                        // +0x50
    uint16_t mShort;                         // +0x68 (1)
    uint16_t mUnset6a;                       // +0x6a (not written)
    nGZGraphic4::cIVertexFormat *mFormat;    // +0x6c counted
    uint32_t mStride;                        // +0x70
    uint32_t mPositionOffset;                // +0x74
    uint32_t mTextureOffset;                 // +0x78
    float mZ;                                // +0x7c
    float mX0, mX1, mY0, mY1;                // +0x80
    float mU0, mU1, mV0, mV1;                // +0x90
    float mCountX, mCountY;                  // +0xa0
    int32_t mUvFlag;                         // +0xa8
    nGZGraphic4::cIVertexDeclarationDX *mDeclaration; // +0xac counted
};
static_assert(sizeof(cDynamicGeometryRect) == 0xb0, "cDynamicGeometryRect size");
static_assert(offsetof(cDynamicGeometryRect, mFormat) == 0x6c);
static_assert(offsetof(cDynamicGeometryRect, mZ) == 0x7c);
static_assert(offsetof(cDynamicGeometryRect, mCountX) == 0xa0);
static_assert(offsetof(cDynamicGeometryRect, mDeclaration) == 0xac);

// The hooks' entry points (src/hook/tables/cDynamicGeometryRect.cpp): original calling convention, our bodies.
struct cDynamicGeometryRectAdapters {
    using Self = cDynamicGeometryRect;
    static Self *__fastcall Construct(Self *memory, void *);
    static void __fastcall Destruct(Self *self, void *);
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);
    static void __fastcall Begin(Self *self, void *, uint32_t index, cGeometryRenderContext *context, uint32_t first);
    static uint32_t __fastcall Render(Self *self, void *, uint32_t unused0, cGeometryRenderContext *context, uint32_t flag,
                                      uint32_t unused3);
};

} // namespace nGZSceneGraph
