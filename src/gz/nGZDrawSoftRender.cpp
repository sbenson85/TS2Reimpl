// The software span renderers (src/gz/nGZDrawSoftRender.h; docs/subsystems/rendering.md, R5b).
#include "gz/nGZDrawSoftRender.h"

#include <cstring>

#include "game/original.h"
#include "gz/nGZDrawSoftTexture.h"

namespace nGZDrawSoftRender {

namespace {

// A span's first pixel: column, row and the address of the pixel in `dest`.
inline uint32_t SpanX(uint32_t start) { return start & 0xffff; }
inline uint32_t SpanY(uint32_t start) { return start >> 16; }

template <typename Pixel>
inline Pixel *SpanPixels(const cGZDrawSurface *dest, uint32_t start)
{
    return reinterpret_cast<Pixel *>(dest->bits + static_cast<uint32_t>(dest->stride) * SpanY(start) +
                                     SpanX(start) * sizeof(Pixel));
}

// A gradient channel's value at the span's first pixel (16.16, wrapping).
inline uint32_t GradientAt(const Gradient *g, int channel, uint32_t x, uint32_t y)
{
    return static_cast<uint32_t>(g->dx[channel]) * x + static_cast<uint32_t>(g->dy[channel]) * y +
           static_cast<uint32_t>(g->base[channel]);
}

// The 16-bit formats' gradient blends (slot 4): the gradient's colour channels shifted into the format's bit positions
// of a 16.16 word, its alpha scaled to 0..32 (1555 and 565) or 0..16 (4444), and the destination blended per field.
struct Blend16Format {
    uint32_t blueShift;  // B >> blueShift (the step shifted arithmetically)
    uint32_t greenMul;   // G * greenMul
    uint32_t redMul;     // R * redMul
    uint32_t alphaMul;   // A * alphaMul, then >> alphaShift
    uint32_t alphaShift;
    uint32_t redMask;    // of R's word
    uint32_t blueMask;   // of B's word
    uint32_t greenMask;  // of G's word >> 16
    uint32_t redBlue;    // the destination's red and blue fields
    uint32_t green;      // the destination's green field
    uint32_t round;      // added to the red / blue sum
    uint32_t greenRound; // added to the green field before the scale
    uint32_t bits;       // the scale (the alpha range's bits)
    uint32_t keepRedBlue; // or'ed into the red / blue result (the bits the green result supplies)
    uint32_t keepGreen;   // or'ed into the green result
};

constexpr Blend16Format k1555 = {3, 4, 0x80, 0x84, 26, 0x7c000000, 0x1f0000, 0x3e0, 0x7c1f, 0x3e0, 0x4010, 0x10, 5,
                                 0x83e0, 0xfc1f};
constexpr Blend16Format k565 = {3, 8, 0x100, 0x84, 26, 0xf8000000, 0x1f0000, 0x7e0, 0xf81f, 0x7e0, 0x8010, 0x10, 5,
                                0x7e0, 0xf81f};
constexpr Blend16Format k4444 = {4, 1, 0x10, 0x88, 27, 0xf000000, 0xf0000, 0xf0, 0xf0f, 0xf0, 0x808, 8, 4, 0xf0f0,
                                 0xff0f};

void BlendGradient16(const Blend16Format &f, const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest,
                     const Gradient *g)
{
    const uint32_t stepB = static_cast<uint32_t>(g->dx[0] >> f.blueShift);
    const uint32_t stepG = static_cast<uint32_t>(g->dx[1]) * f.greenMul;
    const uint32_t stepR = static_cast<uint32_t>(g->dx[2]) * f.redMul;
    const uint32_t stepA = static_cast<uint32_t>(g->dx[3]) * f.alphaMul;
    do {
        const uint32_t start = spans[0];
        const uint32_t x = SpanX(start), y = SpanY(start);
        uint16_t *p = SpanPixels<uint16_t>(dest, start);
        uint32_t b = GradientAt(g, 0, x, y) >> f.blueShift;
        uint32_t gr = GradientAt(g, 1, x, y) * f.greenMul;
        uint32_t r = GradientAt(g, 2, x, y) * f.redMul;
        uint32_t a = GradientAt(g, 3, x, y) * f.alphaMul;
        uint32_t length = spans[1] - start;
        if (length != 0) {
            do {
                const uint32_t alpha = a >> f.alphaShift;
                const uint32_t colorRedBlue = ((r & f.redMask) + (b & f.blueMask)) >> 16;
                const uint32_t colorGreen = (gr >> 16) & f.greenMask;
                const uint32_t pixelRedBlue = *p & f.redBlue;
                const uint32_t pixelGreen = *p & f.green;
                const uint16_t redBlue = static_cast<uint16_t>(
                    (pixelRedBlue * (1u << f.bits) + (colorRedBlue - pixelRedBlue) * alpha + f.round) >> f.bits);
                const uint16_t green = static_cast<uint16_t>(
                    ((colorGreen - pixelGreen) * alpha + (pixelGreen + f.greenRound) * (1u << f.bits)) >> f.bits);
                *p = static_cast<uint16_t>((redBlue | f.keepRedBlue) & (green | f.keepGreen));
                ++p;
                b += stepB;
                gr += stepG;
                r += stepR;
                a += stepA;
            } while (--length != 0);
        }
        spans += 2;
    } while (--count != 0);
}

// The 16-bit and 32-bit fills (the image's 0x1b2ccf and 0x1b2d0a do the same with rep stosd).
template <typename Pixel>
void FillSpans(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest, Pixel value)
{
    do {
        Pixel *p = SpanPixels<Pixel>(dest, spans[0]);
        for (uint32_t n = spans[1] - spans[0]; n != 0; --n) *p++ = value;
        spans += 2;
    } while (--count != 0);
}

// A 16-bit blend of `value` by alpha (slot 3); `Mix` makes one pixel.
template <typename Mix>
void BlendSpans16(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest, Mix mix)
{
    do {
        uint16_t *p = SpanPixels<uint16_t>(dest, spans[0]);
        for (uint32_t n = spans[1] - spans[0]; n != 0; --n, ++p) *p = mix(*p);
        spans += 2;
    } while (--count != 0);
}

} // namespace

// ---- cSpanRenderBase ----

cSpanRenderBase::~cSpanRenderBase() = default;

TexelSpan *cSpanRenderBase::TiledSpans(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest,
                                       const cGZDrawSurface *texture, const TextureParams *params,
                                       uint32_t destBytesPerPixel)
{
    mTiled.resize(count);
    TexelSpan *out = mTiled.data();
    do {
        const uint32_t start = spans[0], end = spans[1];
        const uint32_t x = SpanX(start), y = SpanY(start);
        out->dest = dest->bits + static_cast<uint32_t>(dest->stride) * y + x * destBytesPerPixel;
        out->row = texture->bits +
                   ((static_cast<uint32_t>(params->offsetY) + y) % texture->height) * static_cast<uint32_t>(texture->stride);
        out->b = end - start;
        out->a = (static_cast<uint32_t>(params->offsetX) + x) % texture->width;
        ++out;
        spans += 2;
    } while (--count != 0);
    return mTiled.data();
}

TexelSpan *cSpanRenderBase::ScaledSpans(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest,
                                        const cGZDrawSurface *texture, const TextureParams *params,
                                        uint32_t destBytesPerPixel)
{
    mScaled.resize(count);
    TexelSpan *out = mScaled.data();
    do {
        const uint32_t start = spans[0], end = spans[1];
        const uint32_t x = SpanX(start), y = SpanY(start);
        out->dest = dest->bits + static_cast<uint32_t>(dest->stride) * y + x * destBytesPerPixel;
        out->row = texture->bits +
                   ((static_cast<uint32_t>(params->stepY) * y + static_cast<uint32_t>(params->offsetY)) >> 16) *
                       static_cast<uint32_t>(texture->stride);
        out->a = end - start;
        out->b = static_cast<uint32_t>(params->stepX) * x + static_cast<uint32_t>(params->offsetX);
        ++out;
        spans += 2;
    } while (--count != 0);
    return mScaled.data();
}

// ---- cSpanRender8 ----

void cSpanRender8::Fill(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest, uint32_t value)
{
    do {
        std::memset(SpanPixels<uint8_t>(dest, spans[0]), static_cast<int>(value), spans[1] - spans[0]);
        spans += 2;
    } while (--count != 0);
}

void cSpanRender8::FillGradient(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest,
                                const Gradient *gradient)
{
    // Palette formats take the gradient's alpha channel as the index.
    const uint32_t step = static_cast<uint32_t>(gradient->dx[3]);
    do {
        const uint32_t start = spans[0];
        uint32_t v = GradientAt(gradient, 3, SpanX(start), SpanY(start));
        uint8_t *p = SpanPixels<uint8_t>(dest, start);
        uint32_t length = spans[1] - start;
        if (length != 0) {
            do {
                *p++ = static_cast<uint8_t>(v >> 16);
                v += step;
            } while (--length != 0);
        }
        spans += 2;
    } while (--count != 0);
}

void cSpanRender8::Blend(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest, uint32_t value, uint32_t)
{
    Fill(spans, count, dest, value); // no blending in a palette format
}

void cSpanRender8::BlendGradient(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest,
                                 const Gradient *gradient)
{
    FillGradient(spans, count, dest, gradient);
}

void cSpanRender8::DrawTexture(const void *spans, uint32_t count, const cGZDrawSurface *dest,
                               const cGZDrawSurface *texture, const TextureParams *params, uint32_t mode)
{
    DrawTextureSpans(DestPixel::Palette8, spans, count, dest, texture, params, mode);
}

// ---- cSpanRender1555 ----

void cSpanRender1555::Fill(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest, uint32_t value)
{
    FillSpans<uint16_t>(spans, count, dest, static_cast<uint16_t>(value));
}

void cSpanRender1555::FillGradient(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest,
                                   const Gradient *gradient)
{
    // ORIGINAL-BUG: green and red start unshifted (their 16.16 values at bit 16) while their steps are shifted into
    // the 1555 positions (<< 5, << 10), so the gradient's start colour lands in the wrong fields; red starts with bit
    // 31 flipped.
    const uint32_t stepB = static_cast<uint32_t>(gradient->dx[0]);
    const uint32_t stepG = static_cast<uint32_t>(gradient->dx[1]) * 0x20;
    const uint32_t stepR = static_cast<uint32_t>(gradient->dx[2]) * 0x400;
    do {
        const uint32_t start = spans[0];
        const uint32_t x = SpanX(start), y = SpanY(start);
        uint16_t *p = SpanPixels<uint16_t>(dest, start);
        uint32_t g = GradientAt(gradient, 1, x, y);
        uint32_t b = GradientAt(gradient, 0, x, y);
        uint32_t r = GradientAt(gradient, 2, x, y) + 0x80000000u;
        uint32_t length = spans[1] - start;
        if (length != 0) {
            do {
                *p++ = static_cast<uint16_t>(((r & 0x7c000000) + (g & 0x3e00000) + (b & 0x1f0000)) >> 16);
                g += stepG;
                b += stepB;
                r += stepR;
            } while (--length != 0);
        }
        spans += 2;
    } while (--count != 0);
}

void cSpanRender1555::Blend(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest, uint32_t value,
                            uint32_t argb)
{
    const uint32_t colorRedBlue = value & 0x7c1f;
    const uint32_t colorGreen = value & 0x3e0;
    const uint32_t alpha = (argb >> 24) * 0x818180 >> 26; // 0..32
    BlendSpans16(spans, count, dest, [=](uint32_t pixel) {
        const uint32_t green = pixel & 0x3e0;
        const uint32_t redBlue = pixel & 0x7c1f;
        return static_cast<uint16_t>(
            (static_cast<uint16_t>(((colorGreen - green) * alpha + (green + 0x10) * 0x20) >> 5) | 0xfc1f) &
            (static_cast<uint16_t>(((colorRedBlue - redBlue) * alpha + 0x4010 + redBlue * 0x20) >> 5) | 0x83e0));
    });
}

void cSpanRender1555::BlendGradient(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest,
                                    const Gradient *gradient)
{
    BlendGradient16(k1555, spans, count, dest, gradient);
}

void cSpanRender1555::DrawTexture(const void *spans, uint32_t count, const cGZDrawSurface *dest,
                                  const cGZDrawSurface *texture, const TextureParams *params, uint32_t mode)
{
    DrawTextureSpans(DestPixel::Argb1555, spans, count, dest, texture, params, mode);
}

// ---- cSpanRender565 ----

void cSpanRender565::Fill(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest, uint32_t value)
{
    FillSpans<uint16_t>(spans, count, dest, static_cast<uint16_t>(value));
}

void cSpanRender565::FillGradient(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest,
                                  const Gradient *gradient)
{
    const uint32_t stepB = static_cast<uint32_t>(gradient->dx[0] >> 3);
    const uint32_t stepG = static_cast<uint32_t>(gradient->dx[1]) * 8;
    const uint32_t stepR = static_cast<uint32_t>(gradient->dx[2]) * 0x100;
    do {
        const uint32_t start = spans[0];
        const uint32_t x = SpanX(start), y = SpanY(start);
        uint16_t *p = SpanPixels<uint16_t>(dest, start);
        uint32_t r = GradientAt(gradient, 2, x, y) * 0x100;
        uint32_t g = GradientAt(gradient, 1, x, y) * 8;
        uint32_t b = GradientAt(gradient, 0, x, y) >> 3;
        uint32_t length = spans[1] - start;
        if (length != 0) {
            do {
                *p++ = static_cast<uint16_t>(((r & 0xf8000000) + (g & 0x7e00000) + (b & 0x1f0000)) >> 16);
                r += stepR;
                g += stepG;
                b += stepB;
            } while (--length != 0);
        }
        spans += 2;
    } while (--count != 0);
}

void cSpanRender565::Blend(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest, uint32_t value,
                           uint32_t argb)
{
    const uint32_t colorRedBlue = value & 0xf81f;
    const uint32_t colorGreen = value & 0x7e0;
    const uint32_t alpha = (argb >> 24) * 0x818180 >> 26; // 0..32
    BlendSpans16(spans, count, dest, [=](uint32_t pixel) {
        const uint32_t green = pixel & 0x7e0;
        const uint32_t redBlue = pixel & 0xf81f;
        return static_cast<uint16_t>(
            ((((colorGreen - green) * alpha + (green + 0x10) * 0x20) & 0xfc00) +
             (((colorRedBlue - redBlue) * alpha + 0x8010 + redBlue * 0x20) & 0x1f03e0)) >> 5);
    });
}

void cSpanRender565::BlendGradient(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest,
                                   const Gradient *gradient)
{
    BlendGradient16(k565, spans, count, dest, gradient);
}

void cSpanRender565::DrawTexture(const void *spans, uint32_t count, const cGZDrawSurface *dest,
                                 const cGZDrawSurface *texture, const TextureParams *params, uint32_t mode)
{
    DrawTextureSpans(DestPixel::Rgb565, spans, count, dest, texture, params, mode);
}

// ---- cSpanRender4444 ----

void cSpanRender4444::Fill(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest, uint32_t value)
{
    FillSpans<uint16_t>(spans, count, dest, static_cast<uint16_t>(value));
}

void cSpanRender4444::FillGradient(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest,
                                   const Gradient *gradient)
{
    // Alpha comes out as 0xf, plus whatever red's steps carry into it.
    const uint32_t stepB = static_cast<uint32_t>(gradient->dx[0] >> 4);
    const uint32_t stepG = static_cast<uint32_t>(gradient->dx[1]);
    const uint32_t stepR = static_cast<uint32_t>(gradient->dx[2]) * 0x10;
    do {
        const uint32_t start = spans[0];
        const uint32_t x = SpanX(start), y = SpanY(start);
        uint16_t *p = SpanPixels<uint16_t>(dest, start);
        uint32_t r = GradientAt(gradient, 2, x, y) * 0x10 | 0xf0000000u;
        uint32_t g = GradientAt(gradient, 1, x, y);
        uint32_t b = GradientAt(gradient, 0, x, y) >> 4;
        uint32_t length = spans[1] - start;
        if (length != 0) {
            do {
                *p++ = static_cast<uint16_t>(((g & 0xf00000) + (b & 0xf0000) + (r & 0xff000000)) >> 16);
                r += stepR;
                g += stepG;
                b += stepB;
            } while (--length != 0);
        }
        spans += 2;
    } while (--count != 0);
}

void cSpanRender4444::Blend(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest, uint32_t value,
                            uint32_t argb)
{
    const uint32_t colorRedBlue = value & 0xf0f;
    const uint32_t colorGreen = value & 0xf0;
    const uint32_t alpha = ((argb >> 27) + 1) >> 1; // 0..16
    BlendSpans16(spans, count, dest, [=](uint32_t pixel) {
        const uint32_t green = pixel & 0xf0;
        const uint32_t redBlue = pixel & 0xf0f;
        return static_cast<uint16_t>(
            (static_cast<uint16_t>(((colorGreen - green) * alpha + (green + 8) * 0x10) >> 4) | 0xff0f) &
            (static_cast<uint16_t>(((colorRedBlue - redBlue) * alpha + 0x808 + redBlue * 0x10) >> 4) | 0xf0f0));
    });
}

void cSpanRender4444::BlendGradient(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest,
                                    const Gradient *gradient)
{
    BlendGradient16(k4444, spans, count, dest, gradient);
}

void cSpanRender4444::DrawTexture(const void *spans, uint32_t count, const cGZDrawSurface *dest,
                                  const cGZDrawSurface *texture, const TextureParams *params, uint32_t mode)
{
    DrawTextureSpans(DestPixel::Argb4444, spans, count, dest, texture, params, mode);
}

// ---- cSpanRender8888 ----

void cSpanRender8888::Fill(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest, uint32_t value)
{
    FillSpans<uint32_t>(spans, count, dest, value);
}

void cSpanRender8888::FillGradient(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest,
                                   const Gradient *gradient)
{
    const uint32_t stepB = static_cast<uint32_t>(gradient->dx[0]);
    const uint32_t stepG = static_cast<uint32_t>(gradient->dx[1]);
    const uint32_t stepR = static_cast<uint32_t>(gradient->dx[2]);
    do {
        const uint32_t start = spans[0];
        const uint32_t x = SpanX(start), y = SpanY(start);
        uint32_t *p = SpanPixels<uint32_t>(dest, start);
        uint32_t r = GradientAt(gradient, 2, x, y) | 0xff000000u;
        uint32_t g = GradientAt(gradient, 1, x, y);
        uint32_t b = GradientAt(gradient, 0, x, y);
        uint32_t length = spans[1] - start;
        if (length != 0) {
            do {
                *p++ = ((b >> 16) & 0xff) + ((g >> 8) & 0xff00) + (r & 0xffff0000);
                r += stepR;
                g += stepG;
                b += stepB;
            } while (--length != 0);
        }
        spans += 2;
    } while (--count != 0);
}

void cSpanRender8888::Blend(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest, uint32_t value,
                            uint32_t argb)
{
    const uint32_t alpha = (argb >> 24) + (argb >> 31); // 0..256
    const uint32_t colorRedBlue = value & 0xff00ff;
    const uint32_t colorGreen = value & 0xff00;
    do {
        const uint32_t start = spans[0];
        uint32_t length = spans[1] - start;
        if (length != 0) {
            uint32_t *p = SpanPixels<uint32_t>(dest, start);
            do {
                const uint32_t redBlue = *p & 0xff00ff;
                const uint32_t green = *p & 0xff00;
                *p = ((((colorRedBlue - redBlue) * alpha + 0x800080) >> 8) + redBlue | 0xff00ff00) &
                     (((colorGreen - green) >> 8) * alpha + 0x80 + green | 0xffff00ff);
                ++p;
            } while (--length != 0);
        }
        spans += 2;
    } while (--count != 0);
}

void cSpanRender8888::BlendGradient(const uint32_t *spans, uint32_t count, const cGZDrawSurface *dest,
                                    const Gradient *gradient)
{
    const uint32_t stepB = static_cast<uint32_t>(gradient->dx[0]);
    const uint32_t stepG = static_cast<uint32_t>(gradient->dx[1]);
    const uint32_t stepR = static_cast<uint32_t>(gradient->dx[2]);
    const uint32_t stepA = static_cast<uint32_t>(gradient->dx[3]);
    do {
        const uint32_t start = spans[0];
        const uint32_t x = SpanX(start), y = SpanY(start);
        uint32_t *p = SpanPixels<uint32_t>(dest, start);
        uint32_t b = GradientAt(gradient, 0, x, y);
        uint32_t g = GradientAt(gradient, 1, x, y);
        uint32_t r = GradientAt(gradient, 2, x, y) - 0x1000000u;
        uint32_t a = GradientAt(gradient, 3, x, y);
        uint32_t length = spans[1] - start;
        if (length != 0) {
            do {
                const uint32_t alpha = a >> 16; // not masked: the 16.16 alpha's whole integer part
                const uint32_t colorRedBlue = ((r & 0xffff0000) + (b >> 16)) & 0xff00ff;
                const uint32_t redBlue = *p & 0xff00ff;
                const uint32_t green = *p & 0xff00;
                *p = ((((colorRedBlue - redBlue) * alpha + 0x800080) >> 8) + redBlue | 0xff00ff00) &
                     (((((g >> 8) & 0xff00) - green) >> 8) * alpha + 0x80 + green | 0xffff00ff);
                ++p;
                b += stepB;
                g += stepG;
                r += stepR;
                a += stepA;
            } while (--length != 0);
        }
        spans += 2;
    } while (--count != 0);
}

void cSpanRender8888::DrawTexture(const void *spans, uint32_t count, const cGZDrawSurface *dest,
                                  const cGZDrawSurface *texture, const TextureParams *params, uint32_t mode)
{
    DrawTextureSpans(DestPixel::Argb8888, spans, count, dest, texture, params, mode);
}

} // namespace nGZDrawSoftRender
