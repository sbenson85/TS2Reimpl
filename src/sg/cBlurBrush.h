#pragma once
// nTSSG::cBlurBrush (lane 261108d, scenegraph row LT1; docs/subsystems/scenegraph.md): a 2D brush (cIDraw2DMaterial,
// gz/cDraw2D.h: slot 3 the state block, slot 4 the shape) that draws a texture blurred: the shape is drawn (2r + 1)^2
// times, once per texel offset (i, j) in -r..r, each pass with the texture coordinates shifted by (i, j) texels and a
// grey-and-alpha vertex colour that shares 255 out among the passes (each pass takes the integer share of what is left,
// so the passes add up to 255). Draw clears the target first (every buffer: flags 0xffff, the colour in the game global
// 0x10da49c, depth 1, stencil 0). The state block draws the texture with depth testing off, the texture stage modulating
// it by the vertex colour and alpha blending on.
//
// Texture coordinates: u = point.x * (scale.x / span.x) + ((offset.x - scale.x * origin.x) + i) / width, likewise v with
// y and j, where span is the shape's third point less its first (the brush is drawn on rectangles: point 2 is the far
// corner) and width / height the device texture's level 0. With no texture the scale is 1 and the offset 0.
//
// Made by nTSSG::cBlurredRenderTarget::vf6 (0x3ca5e0) and 0x2e7d10: the game's operator new (0x2c bytes) and the
// constructor 0x3c9e50, which is hooked, so every object carries our vtable and every slot is ours (in effect through
// callers); the destructor 0x3c9f40 is hooked too.
//
// Layout (0x2c): +0 vtable, +4 reference count, +8 the texture (nGZSceneGraph::cTextureHandle), +0xc the state block
// (counted), +0x10 / +0x14 the origin (slot 6), +0x18 / +0x1c the offset (slot 7), +0x20 / +0x24 the scale (slot 8, 1 and
// 1 at first), +0x28 the radius r (slot 10, 3 at first).
#include "gz/cDraw2D.h"
#include "sg/cTextureManager.h"

#include <cstddef>
#include <cstdint>

namespace nGZGraphic4 {
class cIDeviceResource;
}

namespace nTSSG {

class cBlurBrush final : public nGZGraphic4::cIDraw2DMaterial {
public:
    static constexpr uint32_t kSize = 0x2c;
    static constexpr uint32_t kIidBrush = 0xdc004962; // nGZGraphic4::cIBrush

    cBlurBrush(); // 0x3c9e50

    bool QueryInterface(uint32_t riid, void **out) override; // 0x3cab10: ids 1 and 0xdc004962
    uint32_t AddRef() override;                              // shared 0x148fb0
    uint32_t Release() override;                             // shared 0x2c1170: at zero the deleting destructor (slot 5)
    // 0x3ca920: the kept block while it is created; else a new one (0 when the texture cannot be restored).
    uint32_t GetStateBlock(nGZGraphic4::cIDraw2D *draw2d, nGZGraphic4::cIDeviceDX *device) override;
    // 0x3ca2a0: the clear, then the (2r + 1)^2 passes (nothing without a ready device or a state block).
    void Draw(nGZGraphic4::cIDraw2D *draw2d, uint32_t kind, uint32_t count, const float *points, const float *values) override;
    virtual ~cBlurBrush();                         // 5: 0x3c9ff0 (deleting), 0x3c9f40: the state block, then the texture
    virtual void SetOrigin(float x, float y);      // 6: 0x3cad10
    virtual void SetOffset(float x, float y);      // 7: 0x3cacf0
    virtual void SetScale(float x, float y);       // 8: shared_vf_3cad30
    virtual void SetTexture(uint32_t textureId);   // 9: 0x3cad50: the texture manager's id (counted); the block let go
    virtual void SetRadius(int32_t radius);        // 10: shared_vf_1f9ac0

private:
    friend struct cBlurBrushAdapters;
    friend struct cBlurBrushLayout;

    uint32_t mRefCount = 0;                               // +4
    nGZSceneGraph::cTextureHandle mTexture;               // +8
    nGZGraphic4::cIDeviceResource *mStateBlock = nullptr; // +0xc
    float mOriginX = 0.0f, mOriginY = 0.0f;               // +0x10
    float mOffsetX = 0.0f, mOffsetY = 0.0f;               // +0x18
    float mScaleX = 1.0f, mScaleY = 1.0f;                 // +0x20
    int32_t mRadius = 3;                                  // +0x28
};

struct cBlurBrushLayout {
    static_assert(sizeof(cBlurBrush) == cBlurBrush::kSize, "cBlurBrush: 0x2c bytes");
    static_assert(offsetof(cBlurBrush, mTexture) == 8 && offsetof(cBlurBrush, mStateBlock) == 0xc, "layout");
    static_assert(offsetof(cBlurBrush, mOriginX) == 0x10 && offsetof(cBlurBrush, mOffsetX) == 0x18, "layout");
    static_assert(offsetof(cBlurBrush, mScaleX) == 0x20 && offsetof(cBlurBrush, mRadius) == 0x28, "layout");
};

// The hooks: the constructor (thiscall, no arguments) and the destructor (not deleting).
struct cBlurBrushAdapters {
    static cBlurBrush *__fastcall Construct(cBlurBrush *memory, void *);
    static void __fastcall Destruct(cBlurBrush *self, void *);
};

} // namespace nTSSG
