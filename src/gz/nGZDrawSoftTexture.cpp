// The software span renderers' texture slot (src/gz/nGZDrawSoftTexture.h; docs/subsystems/rendering.md, R5b step 1).
#include "gz/nGZDrawSoftTexture.h"

#include <algorithm>

namespace nGZDrawSoftRender {

namespace {

using Argb = uint32_t; // a << 24 | r << 16 | g << 8 | b

// ---- Texture pixel formats: the texel as ARGB8888 (fields widened by repeating their top bits) ----

inline uint32_t Widen4(uint32_t n) { return n * 0x11; }
inline uint32_t Widen5(uint32_t n) { return n << 3 | n >> 2; }
inline uint32_t Widen6(uint32_t n) { return n << 2 | n >> 4; }
inline Argb Pack(uint32_t a, uint32_t r, uint32_t g, uint32_t b) { return a << 24 | r << 16 | g << 8 | b; }

struct Src4444 { // formatIndex 0
    using Texel = uint16_t;
    static constexpr uint32_t kMask = 0xfff;
    static constexpr bool kHasKey = true;
    static Argb Decode(uint32_t t)
    {
        return Pack(Widen4(t >> 12), Widen4(t >> 8 & 15), Widen4(t >> 4 & 15), Widen4(t & 15));
    }
};

struct Src1555Alpha { // formatIndex 1 (format 6)
    using Texel = uint16_t;
    static constexpr uint32_t kMask = 0x7fff;
    static constexpr bool kHasKey = true;
    static Argb Decode(uint32_t t)
    {
        return Pack(t & 0x8000 ? 0xff : 0, Widen5(t >> 10 & 31), Widen5(t >> 5 & 31), Widen5(t & 31));
    }
};

struct Src1555 { // formatIndex 2 (format 4)
    using Texel = uint16_t;
    static constexpr uint32_t kMask = 0x7fff;
    static constexpr bool kHasKey = true;
    static Argb Decode(uint32_t t) { return Pack(0xff, Widen5(t >> 10 & 31), Widen5(t >> 5 & 31), Widen5(t & 31)); }
};

struct Src565 { // formatIndex 3
    using Texel = uint16_t;
    static constexpr uint32_t kMask = 0xffff;
    static constexpr bool kHasKey = true;
    static Argb Decode(uint32_t t) { return Pack(0xff, Widen5(t >> 11), Widen6(t >> 5 & 63), Widen5(t & 31)); }
};

struct Src8888Alpha { // formatIndex 4 (format 9)
    using Texel = uint32_t;
    static constexpr uint32_t kMask = 0xffffff;
    static constexpr bool kHasKey = true;
    static Argb Decode(uint32_t t) { return t; }
};

struct Src8888 { // formatIndex 5 (format 8)
    using Texel = uint32_t;
    static constexpr uint32_t kMask = 0xffffff;
    static constexpr bool kHasKey = true;
    static Argb Decode(uint32_t t) { return t | 0xff000000u; }
};

struct Src8 { // formatIndex 6: an alpha map
    using Texel = uint8_t;
    static constexpr uint32_t kMask = 0;
    static constexpr bool kHasKey = false;
    static Argb Decode(uint32_t t) { return t << 24 | 0xffffff; }
};

// The colour's channels times the texel's, (c * m + 0x80) >> 8, saturated.
inline Argb Modulate(Argb t, Argb c)
{
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const uint32_t v = ((t >> shift & 0xff) * (c >> shift & 0xff) + 0x80) >> 8;
        out |= std::min<uint32_t>(v, 0xff) << shift;
    }
    return out;
}

// ---- Destination pixel formats ----
// Each: Pixel, Encode (truncating), and the three blends of the modes 1, 3 and 4.

struct Dest4444 {
    using Pixel = uint16_t;
    static Pixel Encode(Argb c)
    {
        return static_cast<Pixel>((c >> 16 & 0xf000) + (c >> 12 & 0xf00) + (c >> 8 & 0xf0) + (c >> 4 & 0xf));
    }
    // Mode 1: the colour's alpha, 0..16 (the texel's own is ignored: the blend aims at alpha 15).
    static uint32_t ColorAlpha(Argb color) { return (color >> 28) + (color >> 31); }
    // Modes 3 and 4: the modulated alpha, 0..16.
    static uint32_t ByteAlpha(Argb m) { return (m >> 28) + (m >> 31); }
    static Pixel Lerp(Pixel d, Argb t, uint32_t a)
    {
        const uint32_t texel = Encode(t | 0xff000000u);
        uint32_t out = 0;
        for (int shift = 0; shift < 16; shift += 4) {
            const uint32_t td = texel >> shift & 15, dd = d >> shift & 15;
            out |= ((dd * 16 + (td - dd) * a + 8) >> 4 & 15) << shift;
        }
        return static_cast<Pixel>(out);
    }
    static Pixel Premultiplied(Pixel d, Argb m)
    {
        const uint32_t a = (m >> 24 >> 4) + (m >> 24 >> 7);
        const uint32_t factor = 16 - a;
        const uint32_t texel = Encode(m);
        uint32_t out = 0;
        for (int shift = 0; shift < 16; shift += 4) {
            const uint32_t v = (texel >> shift & 15) + ((d >> shift & 15) * factor >> 4);
            out |= std::min<uint32_t>(v, 15) << shift;
        }
        return static_cast<Pixel>(out);
    }
};

// The 16-bit formats' mode 1 and 3 blend is a rounded lerp per field (the original runs it in packed fields, which
// never carry into the next field): (d * (scale - a) + t * a + scale / 2) >> bits, `a` in 0..scale.
template <uint32_t Shift, uint32_t Bits, uint32_t Scale>
inline uint32_t LerpField(uint32_t d, uint32_t t, uint32_t a)
{
    const uint32_t dd = d >> Shift & ((1u << Bits) - 1), tt = t >> Shift & ((1u << Bits) - 1);
    return ((dd * Scale + (tt - dd) * a + Scale / 2) / Scale & ((1u << Bits) - 1)) << Shift;
}

struct Dest1555 {
    using Pixel = uint16_t;
    static Pixel Encode(Argb c)
    {
        return static_cast<Pixel>((c >> 16 & 0x8000) + (c >> 9 & 0x7c00) + (c >> 6 & 0x3e0) + (c >> 3 & 0x1f));
    }
    static uint32_t ColorAlpha(Argb color) { return (color >> 27) + (color >> 31); }
    static uint32_t ByteAlpha(Argb m) { return (m >> 27) + (m >> 31); }
    static Pixel Lerp(Pixel d, Argb t, uint32_t a)
    {
        const uint32_t texel = Encode(t | 0xff000000u);
        return static_cast<Pixel>(LerpField<15, 1, 32>(d, texel, a) | LerpField<10, 5, 32>(d, texel, a) |
                                  LerpField<5, 5, 32>(d, texel, a) | LerpField<0, 5, 32>(d, texel, a));
    }
    // The original shifts the destination's share by one, not by the five of the 0..32 factor: a bug that makes most
    // results saturate (reproduced).
    static Pixel Premultiplied(Pixel d, Argb m)
    {
        const uint32_t factor = 32 - (m >> 31) - (m >> 27);
        const uint32_t texel = Encode(m);
        auto field = [&](uint32_t shift, uint32_t max) {
            return std::min<uint32_t>((texel >> shift & max) + ((d >> shift & max) * factor >> 1), max) << shift;
        };
        return static_cast<Pixel>(field(15, 1) | field(10, 31) | field(5, 31) | field(0, 31));
    }
};

struct Dest565 {
    using Pixel = uint16_t;
    static Pixel Encode(Argb c) { return static_cast<Pixel>((c >> 8 & 0xf800) + (c >> 5 & 0x7e0) + (c >> 3 & 0x1f)); }
    static uint32_t ColorAlpha(Argb color) { return (color >> 27) + (color >> 31); }
    static uint32_t ByteAlpha(Argb m) { return (m >> 27) + (m >> 31); }
    static Pixel Lerp(Pixel d, Argb t, uint32_t a)
    {
        const uint32_t texel = Encode(t);
        return static_cast<Pixel>(LerpField<11, 5, 32>(d, texel, a) | LerpField<5, 6, 32>(d, texel, a) |
                                  LerpField<0, 5, 32>(d, texel, a));
    }
    // No shift at all of the destination's share (the same bug as the 1555's, worse).
    static Pixel Premultiplied(Pixel d, Argb m)
    {
        const uint32_t factor = 32 - (m >> 31) - (m >> 27);
        const uint32_t texel = Encode(m);
        auto field = [&](uint32_t shift, uint32_t max) {
            return std::min<uint32_t>((texel >> shift & max) + (d >> shift & max) * factor, max) << shift;
        };
        return static_cast<Pixel>(field(11, 31) | field(5, 63) | field(0, 31));
    }
};

struct Dest8888 {
    using Pixel = uint32_t;
    static Pixel Encode(Argb c) { return c; }
    static uint32_t ColorAlpha(Argb color) { return (color >> 25) + (color >> 31); } // 0..128
    static uint32_t ByteAlpha(Argb m) { return (m >> 25) + (m >> 31); }
    static Pixel Lerp(Pixel d, Argb t, uint32_t a)
    {
        uint32_t out = 0;
        for (int shift = 0; shift < 32; shift += 8) {
            const int32_t dd = static_cast<int32_t>(d >> shift & 0xff);
            const int32_t tt = shift == 24 ? 0xff : static_cast<int32_t>(t >> shift & 0xff);
            const int32_t v = dd + (((tt - dd) * static_cast<int32_t>(a) + 0x40) >> 7);
            out |= static_cast<uint32_t>(std::clamp(v, 0, 255)) << shift;
        }
        return out;
    }
    static Pixel Premultiplied(Pixel d, Argb m)
    {
        const uint32_t a = (m >> 31) + (m >> 25 & 0x7f);
        uint32_t out = 0;
        for (int shift = 0; shift < 32; shift += 8) {
            const uint32_t dd = d >> shift & 0xff;
            const uint32_t v = (m >> shift & 0xff) + (dd - ((a * dd + 0x40) >> 7));
            out |= std::min<uint32_t>(v, 255) << shift;
        }
        return out;
    }
};

// The 8-bit format keeps an alpha value. Its "blend" (modes 1 and 3) ignores the texel's colour and darkens the pixel
// by a factor taken from the LOW byte of the colour (blue, plus the alpha byte's top bit), and mode 4 multiplies by the
// whole packed colour: the original's, reproduced.
struct Dest8 {
    using Pixel = uint8_t;
    static Pixel Encode(Argb c) { return static_cast<Pixel>(c >> 24); }
    static uint32_t ColorAlpha(Argb color) { return ((color >> 31) + color) & 0xff; }
    static uint32_t ByteAlpha(Argb m) { return ((m >> 31) + m) & 0xff; }
    static Pixel Lerp(Pixel d, Argb, uint32_t a)
    {
        return static_cast<Pixel>(d - ((d + 1u) & 0xff) * a);
    }
    static Pixel Premultiplied(Pixel d, Argb m)
    {
        const int32_t factor = static_cast<int32_t>(1u - (m >> 31) - m);
        const int32_t v = (static_cast<int32_t>(static_cast<uint32_t>(factor) * d) >> 8) + static_cast<int32_t>(m >> 24);
        return v < 0xff ? static_cast<Pixel>(v) : 0xff;
    }
};

// ---- The operations and the three addressings ----

template <class D, class S, int Op>
inline void Apply(typename D::Pixel *p, uint32_t texel, Argb color)
{
    const Argb t = S::Decode(texel);
    if constexpr (Op == 0) {
        *p = D::Encode(t);
    } else if constexpr (Op == 1) {
        *p = D::Lerp(*p, t, D::ColorAlpha(color));
    } else if constexpr (Op == 2) {
        *p = D::Encode(Modulate(t, color));
    } else if constexpr (Op == 3) {
        const Argb m = Modulate(t, color);
        *p = D::Lerp(*p, m, D::ByteAlpha(m));
    } else {
        *p = D::Premultiplied(*p, Modulate(t, color));
    }
}

template <class S, bool Key>
inline bool Skip(uint32_t texel, const TextureParams *params)
{
    if constexpr (Key && S::kHasKey) return (texel & S::kMask) == params->key;
    else return false;
}

template <class D, class S, bool Key, int Op>
void Plain(const void *spanList, uint32_t count, const cGZDrawSurface *dest, const cGZDrawSurface *texture,
           const TextureParams *params)
{
    using Pixel = typename D::Pixel;
    using Texel = typename S::Texel;
    const uint32_t *spans = static_cast<const uint32_t *>(spanList);
    do {
        const uint32_t start = spans[0];
        const uint32_t x = start & 0xffff, y = start >> 16;
        Pixel *p = reinterpret_cast<Pixel *>(dest->bits + static_cast<uint32_t>(dest->stride) * y + x * sizeof(Pixel));
        const Texel *t = reinterpret_cast<const Texel *>(texture->bits + static_cast<uint32_t>(texture->stride) * y +
                                                         x * sizeof(Texel));
        for (uint32_t n = spans[1] - start; n != 0; --n, ++p, ++t) {
            if (Skip<S, Key>(*t, params)) continue;
            Apply<D, S, Op>(p, *t, params->color);
        }
        spans += 2;
    } while (--count != 0);
}

template <class D, class S, bool Key, int Op>
void Scaled(const void *spanList, uint32_t count, const cGZDrawSurface *, const cGZDrawSurface *,
            const TextureParams *params)
{
    using Pixel = typename D::Pixel;
    using Texel = typename S::Texel;
    const TexelSpan *span = static_cast<const TexelSpan *>(spanList);
    do {
        Pixel *p = reinterpret_cast<Pixel *>(span->dest);
        uint32_t u = span->b;
        for (uint32_t n = span->a; n != 0; --n, ++p) {
            const Texel texel = reinterpret_cast<const Texel *>(span->row)[u >> 16];
            u += static_cast<uint32_t>(params->stepX);
            if (Skip<S, Key>(texel, params)) continue;
            Apply<D, S, Op>(p, texel, params->color);
        }
        ++span;
    } while (--count != 0);
}

template <class D, class S, bool Key, int Op>
void Tiled(const void *spanList, uint32_t count, const cGZDrawSurface *, const cGZDrawSurface *texture,
           const TextureParams *params)
{
    using Pixel = typename D::Pixel;
    using Texel = typename S::Texel;
    const TexelSpan *span = static_cast<const TexelSpan *>(spanList);
    do {
        Pixel *p = reinterpret_cast<Pixel *>(span->dest);
        const Texel *row = reinterpret_cast<const Texel *>(span->row);
        uint32_t x = span->a;
        const Texel *t = row + x;
        for (uint32_t n = span->b; n != 0; --n, ++p) {
            const Texel texel = *t++;
            if (++x >= texture->width) x = 0, t = row;
            if (Skip<S, Key>(texel, params)) continue;
            Apply<D, S, Op>(p, texel, params->color);
        }
        ++span;
    } while (--count != 0);
}

template <class D, class S, bool Key, int Op>
void Run(const void *spans, uint32_t count, const cGZDrawSurface *dest, const cGZDrawSurface *texture,
         const TextureParams *params, uint32_t addressing)
{
    switch (addressing) {
    case 0: Plain<D, S, Key, Op>(spans, count, dest, texture, params); break;
    case 1: Scaled<D, S, Key, Op>(spans, count, dest, texture, params); break;
    default: Tiled<D, S, Key, Op>(spans, count, dest, texture, params); break;
    }
}

template <class D, class S, bool Key>
void PickOp(const void *spans, uint32_t count, const cGZDrawSurface *dest, const cGZDrawSurface *texture,
            const TextureParams *params, uint32_t mode)
{
    const uint32_t addressing = mode / 5;
    switch (mode % 5) {
    case 0: Run<D, S, Key, 0>(spans, count, dest, texture, params, addressing); break;
    case 1: Run<D, S, Key, 1>(spans, count, dest, texture, params, addressing); break;
    case 2: Run<D, S, Key, 2>(spans, count, dest, texture, params, addressing); break;
    case 3: Run<D, S, Key, 3>(spans, count, dest, texture, params, addressing); break;
    default: Run<D, S, Key, 4>(spans, count, dest, texture, params, addressing); break;
    }
}

template <class D, class S>
void PickKey(const void *spans, uint32_t count, const cGZDrawSurface *dest, const cGZDrawSurface *texture,
             const TextureParams *params, uint32_t mode)
{
    if (params->hasKey) PickOp<D, S, true>(spans, count, dest, texture, params, mode);
    else PickOp<D, S, false>(spans, count, dest, texture, params, mode);
}

template <class D>
void PickSource(const void *spans, uint32_t count, const cGZDrawSurface *dest, const cGZDrawSurface *texture,
                const TextureParams *params, uint32_t mode)
{
    switch (params->formatIndex) {
    case 0: PickKey<D, Src4444>(spans, count, dest, texture, params, mode); break;
    case 1: PickKey<D, Src1555Alpha>(spans, count, dest, texture, params, mode); break;
    case 2: PickKey<D, Src1555>(spans, count, dest, texture, params, mode); break;
    case 3: PickKey<D, Src565>(spans, count, dest, texture, params, mode); break;
    case 4: PickKey<D, Src8888Alpha>(spans, count, dest, texture, params, mode); break;
    case 5: PickKey<D, Src8888>(spans, count, dest, texture, params, mode); break;
    default: PickKey<D, Src8>(spans, count, dest, texture, params, mode); break;
    }
}

} // namespace

void DrawTextureSpans(DestPixel destFormat, const void *spans, uint32_t count, const cGZDrawSurface *dest,
                      const cGZDrawSurface *texture, const TextureParams *params, uint32_t mode)
{
    switch (destFormat) {
    case DestPixel::Palette8: PickSource<Dest8>(spans, count, dest, texture, params, mode); break;
    case DestPixel::Argb1555: PickSource<Dest1555>(spans, count, dest, texture, params, mode); break;
    case DestPixel::Rgb565: PickSource<Dest565>(spans, count, dest, texture, params, mode); break;
    case DestPixel::Argb4444: PickSource<Dest4444>(spans, count, dest, texture, params, mode); break;
    case DestPixel::Argb8888: PickSource<Dest8888>(spans, count, dest, texture, params, mode); break;
    default: break;
    }
}

} // namespace nGZDrawSoftRender
