#pragma once
// cGZDrawContextRenderToBuffer: the software draw context engine (1.18.0.187; docs/subsystems/rendering.md, R5b). It
// draws a cGZDrawContext's primitives into a cIGZBuffer target through a span renderer for the target's pixel format
// (src/gz/nGZDrawSoftRender.h): spans filled with the colour, a gradient or a texture, plain or blended; antialiased
// polygons (ChooseRoute's kinds 6 to 8) pixel by pixel through the buffer's GetPixel / SetPixel.
//
// 0xe4 bytes (operator new). One face, cIGZDrawContextEngine (vtable 0xe4f4e4, 24 slots). Made only by
// cGZDrawContextRenderToBufferFactory::CreateEngine (ours, src/gz/cGZDrawSystem.cpp; the constructor 0x8c7dc0 was its
// only caller), so every engine carries our vtable and the slots, the constructor, the destructor 0x8c7e10 and the
// helpers 0x8c8b30 / 0x8c8c00 are in effect through that caller.
#include <cstddef>
#include <cstdint>

#include "gz/cIGZBuffer.h"
#include "gz/cIGZDrawContextEngine.h"
#include "gz/nGZDrawSoftRender.h"

class cGZDrawContextRenderToBuffer : public cIGZDrawContextEngine {
public:
    static constexpr uint32_t kSize = 0xe4;
    static constexpr uint32_t kIid = 0x2ae78a07;
    // The target's Lock / Unlock flags: what the engine holds while it draws (level 2), while a surface is read
    // (level 1), and on a texture while it is drawn.
    static constexpr uint32_t kLockWrite = 0x8040;
    static constexpr uint32_t kLockRead = 0x40;
    static constexpr uint32_t kLockTexture = 0x800;

    cGZDrawContextRenderToBuffer(); // 0x8c7dc0 (count 0)
    // 0x8c7e10 (inline in Release): the renderer deleted, the texture and the target released.
    ~cGZDrawContextRenderToBuffer();

    // cIGZUnknown
    bool QueryInterface(uint32_t riid, void **out) override; // 0x8c7eb0: ids 1 and kIid
    uint32_t AddRef() override;                              // 0x8c7e50
    uint32_t Release() override;                             // 0x8c7e60: destroys and frees 0xe4 bytes at 0

    // cIGZDrawContextEngine
    bool SetTarget(cIGZBuffer *target) override;                                          // 0x8c7ee0
    void Detach() override;                                                               // 0x8c80d0
    cGZRect *GetBounds(cGZRect *bounds) override;                                         // 0x8c8130
    void SetClip(const cGZRect *clip) override;                                           // 0x8c8140
    void SetColors(uint32_t first, uint32_t count, const uint32_t *argb) override;        // 0x8c8150
    void vf8(uint32_t value) override;                                                    // 0x8c84e0 blend: value > 0
    void vf9(uint8_t value) override;                                                     // 0x8c84f0
    void vf10(uint8_t value) override;                                                    // 0x8c8500 textures modulated
    void vf11(uint8_t value) override;                                                    // 0x8c8510 antialias
    void SetLineWidth(float width) override;                                              // 0x8c8520 (nothing)
    void SetGradient(const float *coefficients, float x, float y) override;               // 0x8c8530
    bool SetTexture(cIGZUnknown *texture, float x, float y, float sx, float sy, cGZRect *area) override; // 0x8c81b0
    void ClearTextureClip() override;                                                     // 0x8c84b0
    void SetTextureClip(const cGZRect *clip) override;                                    // 0x8c84c0
    void vf17() override;                                                                 // 0x8c8620 (nothing)
    void vf18() override;                                                                 // 0x8c8630 batch lock off
    void vf19() override;                                                                 // 0x8c8640 batch lock on
    bool LockSurface(cGZDrawSurface *surface, uint32_t readOnly) override;                // 0x8c8650
    void UnlockSurface() override;                                                        // 0x8c8690
    uint32_t ChooseRoute(uint32_t kind, const float *points, uint32_t count) override;    // 0x8c86a0
    void DrawSpans(const int32_t *spans, uint32_t count) override;                       // 0x8c8750

    // 0x8c8b30: the target locked at the higher of the batch and surface levels (0 none, 1 read, 2 write), and the
    // surface read again when that changes.
    void UpdateLock();
    // 0x8c8c00: a polygon of `count` points (x, y floats) filled in colour 0, antialiased (coverage on an 8x8 grid
    // per pixel), through the target's GetPixel / SetPixel.
    void FillPolygon(const float *points, int32_t count);

    uint32_t mCount = 0;                  // +4
    cIGZBuffer *mTarget = nullptr;        // +8
    uint32_t mTargetWidth;                // +0xc  (SetTarget's)
    uint32_t mTargetHeight;               // +0x10
    cGZRect mClip;                        // +0x14
    cIGZBuffer *mTexture = nullptr;       // +0x24
    int32_t mTextureX;                    // +0x28 the texture's origin (texels when scaled)
    int32_t mTextureY;                    // +0x2c
    int32_t mScaleX;                      // +0x30 16.16
    int32_t mScaleY;                      // +0x34
    int32_t mStepX;                       // +0x38 16.16 texels per pixel
    int32_t mStepY;                       // +0x3c
    bool mScaled;                         // +0x40
    bool mBlend = false;                  // +0x41
    uint8_t mFlag42;                      // +0x42 (vf9)
    uint8_t mModulate;                    // +0x43 (vf10)
    uint8_t mAntialias = 0;               // +0x44 (vf11)
    uint8_t mPad45[3];
    nGZDrawSoftRender::Gradient mGradient; // +0x48
    bool mGradientOn = false;             // +0x78
    uint8_t mPad79[3];
    int32_t mLockLevel = 0;               // +0x7c what the target is locked at
    int32_t mBatchLevel = 0;              // +0x80
    int32_t mSurfaceLevel = 0;            // +0x84
    cGZDrawSurface mSurface;              // +0x88 the target's pixels while locked
    uint32_t mBytesPerPixel;              // +0xa0 (SetTarget's)
    cGZRect mTextureClip;                 // +0xa4
    bool mTextureClipped;                 // +0xb4
    uint8_t mPadB5[3];
    int32_t mTextureAreaWidth;            // +0xb8
    int32_t mTextureAreaHeight;           // +0xbc
    nGZDrawSoftRender::cSpanRenderBase *mRenderer = nullptr; // +0xc0
    uint32_t mColors[4];                  // +0xc4 ARGB
    uint32_t mNativeColors[4];            // +0xd4 in the target's format
};
static_assert(sizeof(cGZDrawContextRenderToBuffer) == cGZDrawContextRenderToBuffer::kSize, "layout: 0xe4 bytes");
static_assert(offsetof(cGZDrawContextRenderToBuffer, mGradient) == 0x48 &&
                  offsetof(cGZDrawContextRenderToBuffer, mSurface) == 0x88 &&
                  offsetof(cGZDrawContextRenderToBuffer, mRenderer) == 0xc0 &&
                  offsetof(cGZDrawContextRenderToBuffer, mNativeColors) == 0xd4,
              "layout");
