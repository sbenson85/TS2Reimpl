#pragma once
// nGZDrawSoftRender: the software span renderers cGZDrawContextRenderToBuffer draws with (1.18.0.187;
// docs/subsystems/rendering.md, R5b). One per destination pixel format, made by the engine's SetTarget
// (src/gz/cGZDrawContextRenderToBuffer.h) and deleted by its Detach and destructor:
//   cSpanRender8 (format 2, vtable 0xe4f4c8), cSpanRender1555 (4 and 6, 0xe4f474), cSpanRender565 (5, 0xe4f490),
//   cSpanRender4444 (7, 0xe4f458), cSpanRender8888 (8 and 9, 0xe4f4ac); the base cSpanRenderBase (0xe4f43c).
// 0x1c bytes: the vtable and two vectors of texel spans (+4 tiled, +0x10 scaled) the texture paths build per call.
// Slot 0 is the base's deleting destructor 0x8c92b0 (destructor 0x8c9200) in every vtable.
//
// Spans are pairs of words, (row << 16 | x) of the first pixel and of the end (exclusive); the slots' loops run at least
// once (a count of 0 is never passed). Our classes are made only by our engine, so every slot is in effect through it.
#include <cstdint>
#include <vector>

#include "gz/cIGZDrawContextEngine.h"

namespace nGZDrawSoftRender {

// The engine's gradient (+0x48): 16.16 fixed-point B, G, R, A (byte order of an ARGB word) at the origin and their
// steps per pixel in x and y.
struct Gradient {
    int32_t base[4];
    int32_t dx[4];
    int32_t dy[4];
};
static_assert(sizeof(Gradient) == 0x30, "Gradient: 0x30 bytes");

// What the texture span functions take (0x20 bytes, by value): the texel origin and steps (16.16 when scaled), the
// texture's format index (the engine's DrawSpans: 0 4444, 1 1555 (6), 2 1555 (4), 3 565, 4 888 (9), 5 8888, 6 8-bit),
// the drawing colour (ARGB), the transparent colour and whether there is one.
struct TextureParams {
    int32_t offsetX;
    int32_t offsetY;
    int32_t stepX;
    int32_t stepY;
    uint32_t formatIndex;
    uint32_t color;
    uint32_t key;
    uint8_t hasKey;
    uint8_t pad[3];
};
static_assert(sizeof(TextureParams) == 0x20, "TextureParams: 0x20 bytes");

// One span for the texture paths (16 bytes): the first destination pixel, the texture row, and two words: tiled
// (first texel x, length), scaled (length, 16.16 u of the first pixel).
struct TexelSpan {
    uint8_t *dest;
    const uint8_t *row;
    uint32_t a;
    uint32_t b;
};
static_assert(sizeof(TexelSpan) == 16, "TexelSpan: 16 bytes");

class cSpanRenderBase {
public:
    cSpanRenderBase() = default;
    virtual ~cSpanRenderBase(); // slot 0: 0x8c92b0 (deleting), destructor 0x8c9200

    // slot 1: every pixel of the spans set to `value` (native).
    virtual void Fill(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest, uint32_t value) = 0;
    // slot 2: the gradient's colour at each pixel.
    virtual void FillGradient(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest,
                              const Gradient *gradient) = 0;
    // slot 3: `value` (native) blended over the pixels by `argb`'s alpha.
    virtual void Blend(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest, uint32_t value,
                       uint32_t argb) = 0;
    // slot 4: the gradient blended over the pixels by its alpha.
    virtual void BlendGradient(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest,
                               const Gradient *gradient) = 0;
    // slot 5: a texture drawn into the spans in `mode` (0..4 plain, +5 scaled, +10 tiled; the engine's DrawSpans).
    // `spans` is the engine's span list, or what TiledSpans / ScaledSpans made of it.
    virtual void DrawTexture(const void *spans, uint32_t count, const cGZDrawSurface *dest, const cGZDrawSurface *texture,
                             const TextureParams *params, uint32_t mode) = 0;

    // 0x8c92d0: the spans as tiled texel spans (texture coordinates wrapped by the texture's size), in the +4 vector.
    TexelSpan *TiledSpans(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest,
                          const cGZDrawSurface *texture, const TextureParams *params, uint32_t destBytesPerPixel);
    // 0x8c93d0: the spans as scaled texel spans (16.16 texture coordinates), in the +0x10 vector.
    TexelSpan *ScaledSpans(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest,
                           const cGZDrawSurface *texture, const TextureParams *params, uint32_t destBytesPerPixel);

protected:
    std::vector<TexelSpan> mTiled;  // +4
    std::vector<TexelSpan> mScaled; // +0x10
};

class cSpanRender8 : public cSpanRenderBase {
public:
    void Fill(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest, uint32_t value) override; // 0x901a90
    void FillGradient(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest,
                      const Gradient *gradient) override;                                                // 0x901ae0
    void Blend(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest, uint32_t value,
               uint32_t argb) override;                                                                  // 0x901b70
    void BlendGradient(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest,
                       const Gradient *gradient) override;                                               // 0x901b90
    void DrawTexture(const void *spans, uint32_t count, const cGZDrawSurface *dest, const cGZDrawSurface *texture,
                     const TextureParams *params, uint32_t mode) override;                               // 0x901ba0
};

class cSpanRender1555 : public cSpanRenderBase {
public:
    void Fill(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest, uint32_t value) override; // 0x8d9340
    void FillGradient(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest,
                      const Gradient *gradient) override;                                                // 0x8d9390
    void Blend(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest, uint32_t value,
               uint32_t argb) override;                                                                  // 0x8d94d0
    void BlendGradient(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest,
                       const Gradient *gradient) override;                                               // 0x8d9730
    void DrawTexture(const void *spans, uint32_t count, const cGZDrawSurface *dest, const cGZDrawSurface *texture,
                     const TextureParams *params, uint32_t mode) override;                               // 0x8d9930
};

class cSpanRender565 : public cSpanRenderBase {
public:
    void Fill(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest, uint32_t value) override; // 0x8e8c70
    void FillGradient(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest,
                      const Gradient *gradient) override;                                                // 0x8e8cc0
    void Blend(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest, uint32_t value,
               uint32_t argb) override;                                                                  // 0x8e8e00
    void BlendGradient(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest,
                       const Gradient *gradient) override;                                               // 0x8e9030
    void DrawTexture(const void *spans, uint32_t count, const cGZDrawSurface *dest, const cGZDrawSurface *texture,
                     const TextureParams *params, uint32_t mode) override;                               // 0x8e9230
};

class cSpanRender4444 : public cSpanRenderBase {
public:
    void Fill(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest, uint32_t value) override; // 0x8c9680
    void FillGradient(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest,
                      const Gradient *gradient) override;                                                // 0x8c96d0
    void Blend(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest, uint32_t value,
               uint32_t argb) override;                                                                  // 0x8c9810
    void BlendGradient(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest,
                       const Gradient *gradient) override;                                               // 0x8c9a70
    void DrawTexture(const void *spans, uint32_t count, const cGZDrawSurface *dest, const cGZDrawSurface *texture,
                     const TextureParams *params, uint32_t mode) override;                               // 0x8c9c60
};

class cSpanRender8888 : public cSpanRenderBase {
public:
    void Fill(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest, uint32_t value) override; // 0x8f5cf0
    void FillGradient(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest,
                      const Gradient *gradient) override;                                                // 0x8f5d40
    void Blend(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest, uint32_t value,
               uint32_t argb) override;                                                                  // 0x8f5e70
    void BlendGradient(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest,
                       const Gradient *gradient) override;                                               // 0x8f5f30
    void DrawTexture(const void *spans, uint32_t count, const cGZDrawSurface *dest, const cGZDrawSurface *texture,
                     const TextureParams *params, uint32_t mode) override;                               // 0x8f6110
};

} // namespace nGZDrawSoftRender
