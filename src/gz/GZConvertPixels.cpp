#include "gz/GZConvertPixels.h"

#include "gz/cGZBuffer.h"

#include <cstring>

namespace {

template <class T>
T Load(const uint8_t *p)
{
    T v;
    std::memcpy(&v, p, sizeof(T));
    return v;
}
template <class T>
void Store(uint8_t *p, uint32_t v)
{
    const T t = static_cast<T>(v);
    std::memcpy(p, &t, sizeof(T));
}

// ---- the pixel formulas: one per (destination, source) pair, as the original's loops compute them. Names: To<dst>From<src>
// with the format ids of the table (4 xRGB1555, 5 565, 6 ARGB1555, 7 ARGB4444, 8 xRGB8888, 9 ARGB8888). The arithmetic is
// the original's, term by term (some of it only works because the terms never overlap: e.g. 8 -> 4444 adds -0x1000, which
// wraps to the alpha nibble 0xf in the 16-bit store).

// ---- sources 4 and 6 (1555)
uint32_t To4From4(uint32_t s) { return s | 0x8000; }
uint32_t To5From4(uint32_t s)
{
    return ((((s & 0x3e0) >> 4) + (s & 0x3e0) * 2) & 0x7e0) + (s & 0x7c00) * 2 + (s & 0x1f);
}
uint32_t To5From6(uint32_t s) { return (s & 0xffe0) + ((s >> 4) & 0x20) + s; }
uint32_t To6From4(uint32_t s) { return (s & 0x7fff) + 0x8000; }
uint32_t To7From4(uint32_t s) { return ((s >> 1 & 0xf) - 0x1000) + (s >> 3 & 0xf00) + (s >> 2 & 0xf0); }
uint32_t To7From6(uint32_t s)
{
    return ((static_cast<int32_t>(static_cast<int16_t>(s)) >> 15) & 0xf000) + (s >> 3 & 0xf00) + (s >> 2 & 0xf0) +
           (s >> 1 & 0xf);
}
uint32_t To8From4(uint32_t s)
{
    const uint32_t u = (s & 0x1f) + ((s & 0x3e0) + (s & 0x7c00) * 8) * 8;
    return ((u >> 2 & 0x70707) + u * 8) | 0xff000000u;
}
uint32_t To9From4(uint32_t s)
{
    const uint32_t u = ((s & 0x1f) + ((s & 0x3e0) + (s & 0x7c00) * 8) * 8) * 8 - 0x1000000u;
    return (u >> 5 & 0x70707) + u;
}
uint32_t To9From6(uint32_t s)
{
    const uint32_t u = (0u - (s >> 15) & 0xff000000u) + ((s & 0x1f) + ((s & 0x3e0) + (s & 0x7c00) * 8) * 8) * 8;
    return (u >> 5 & 0x70707) + u;
}

// ---- source 5 (565)
uint32_t To4From5(uint32_t s)
{
    const uint32_t v = s >> 1;
    return ((v & 0x3e0) + (v & 0xfc00) + (s & 0x1f)) | 0x8000;
}
uint32_t To6From5(uint32_t s)
{
    const uint32_t v = s >> 1;
    return (v & 0x7c00) + (v & 0x3e0) + 0x8000 + (s & 0x1f);
}
uint32_t To7From5(uint32_t s) { return ((s >> 1 & 0xf) - 0x1000) + (s >> 4 & 0xf00) + (s >> 3 & 0xf0); }
uint32_t To8From5(uint32_t s)
{
    const uint32_t u = (((s & 0x7e0) >> 1) + (s & 0x7e0) * 0x20 & 0xff00) + ((s & 0xf800) * 0x20 + (s & 0x1f)) * 8;
    return ((u >> 5 & 0x70007) + u) | 0xff000000u;
}
uint32_t To9From5(uint32_t s)
{
    const uint32_t u = ((((s & 0x7e0) >> 1) + (s & 0x7e0) * 0x20 & 0xff00) - 0x1000000u) +
                       ((s & 0xf800) * 0x20 + (s & 0x1f)) * 8;
    return (u >> 5 & 0x70007) + u;
}

// ---- source 7 (4444)
uint32_t To4From7(uint32_t s)
{
    const uint32_t u = (s & 0xf) + ((s & 0xf0) + (s & 0xf00) * 2) * 2;
    return (((u >> 3) & 0x421) + static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(u)) * 2)) | 0x8000;
}
uint32_t To5From7(uint32_t s)
{
    const uint32_t u = (((s & 0xf0) >> 1) + (s & 0xf0) * 8 & 0x7e0) + ((s & 0xf) + (s & 0xf00) * 8) * 2;
    return ((u >> 4) & 0x801) + u;
}
uint32_t To6From7(uint32_t s)
{
    const uint32_t u = (s & 0x8000) + ((s & 0xf) + ((s & 0xf0) + (s & 0xf00) * 2) * 2) * 2;
    return ((u >> 4) & 0x421) + u;
}
uint32_t To8From7(uint32_t s)
{
    return ((((s & 0xf00) * 0x10 + (s & 0xf0)) * 0x10 + (s & 0xf)) * 0x11) | 0xff000000u;
}
uint32_t To9From7(uint32_t s)
{
    return ((((s & 0xfffff000u) * 0x10 + (s & 0xf00)) * 0x10 + (s & 0xf0)) * 0x10 + (s & 0xf)) * 0x11;
}

// ---- sources 8 and 9 (32-bit)
uint32_t To4From8(uint32_t u) { return (((u >> 9) & 0xfc00) + ((u >> 6) & 0x3e0) + ((u >> 3) & 0x1f)) | 0x8000; }
uint32_t To5From8(uint32_t u) { return ((u >> 8) & 0xf800) + ((u >> 5) & 0x7e0) + ((u >> 3) & 0x1f); }
uint32_t To5From9(uint32_t u) { return ((u & 0xf800f8) * 0x21 + (u & 0xfc00) * 8) >> 8; }
uint32_t To6From8(uint32_t u) { return ((u >> 3) & 0x1f) + 0x8000 + ((u >> 9) & 0x7c00) + ((u >> 6) & 0x3e0); }
uint32_t To6From9(uint32_t u) { return ((u >> 9) & 0x7c00) + ((u >> 6) & 0x3e0) + ((u >> 3) & 0x1f) + ((u >> 16) & 0x8000); }
uint32_t To7From8(uint32_t u) { return (((u >> 4) & 0xf) - 0x1000) + ((u >> 12) & 0xf00) + ((u >> 8) & 0xf0); }
uint32_t To7From9(uint32_t u) { return ((u >> 16) & 0xf000) + ((u >> 12) & 0xf00) + ((u >> 8) & 0xf0) + ((u >> 4) & 0xf); }
uint32_t To8From8(uint32_t u) { return u | 0xff000000u; }
uint32_t To9From8(uint32_t u) { return (u & 0xffffff) - 0x1000000u; }

// Same format: a copy.
uint32_t Same(uint32_t s) { return s; }

// ---- the loops: the rows of `width` pixels, `height` of them. A source pixel with the key (the source's colour bits equal
// to args.key) is skipped when the loop is a keyed one. The reads and writes go pixel by pixel, in order, as the original's
// do: an overlapping row (destination above the source in the same row) sees the pixels it has already written.
// Not for width or height of zero or less (the original's do-while would run 2^32 times).
template <class S, class D, uint32_t (*Convert)(uint32_t), uint32_t Mask, bool Keyed>
void __cdecl Loop(GZConvertArgs a)
{
    uint8_t *dstRow = a.dst;
    const uint8_t *srcRow = a.src;
    for (int32_t y = 0; y < a.height; ++y) {
        uint8_t *d = dstRow;
        const uint8_t *s = srcRow;
        for (int32_t x = 0; x < a.width; ++x) {
            const uint32_t pixel = Load<S>(s);
            if (!Keyed || (pixel & Mask) != a.key) Store<D>(d, Convert(pixel));
            s += sizeof(S);
            d += sizeof(D);
        }
        dstRow += a.dstStride;
        srcRow += a.srcStride;
    }
}

using U16 = uint16_t;
using U32 = uint32_t;
// Masks of the source formats (cGZBuffer::FormatMask): 1555 0x7fff, 565 0xffff, 4444 0xfff, 8888 0xffffff.
constexpr uint32_t kM1555 = 0x7fff, kM565 = 0xffff, kM4444 = 0xfff, kM8888 = 0xffffff;

#define LOOP(rva, S, D, F, M, K) {rva, &Loop<S, D, F, M, K>}

} // namespace

const GZConvertLoop kGZConvertLoops[kGZConvertLoopCount] = {
    LOOP(0x8c38e0, U16, U16, To5From7, kM4444, true),
    LOOP(0x8c3970, U32, U32, To9From8, kM8888, false),
    LOOP(0x8c39c0, U32, U16, To5From8, kM8888, false),
    LOOP(0x8c3a30, U32, U32, To9From8, kM8888, true),
    LOOP(0x8c3a80, U32, U16, To5From8, kM8888, true),
    LOOP(0x8c3b00, U32, U32, Same, kM8888, false),
    LOOP(0x8c3b40, U32, U16, To5From9, kM8888, false),
    LOOP(0x8c3ba0, U32, U32, Same, kM8888, true),
    LOOP(0x8c3bf0, U32, U16, To5From9, kM8888, true),
    LOOP(0x8c3c70, U16, U16, To6From4, kM1555, false),
    LOOP(0x8c3cd0, U16, U16, To6From4, kM1555, true),
    LOOP(0x8c3d30, U16, U16, To6From5, kM565, false),
    LOOP(0x8c3da0, U16, U16, To6From5, kM565, true),
    LOOP(0x8c3e20, U16, U16, Same, kM565, false),
    LOOP(0x8c3e70, U16, U16, Same, kM1555, true),
    LOOP(0x8c3ed0, U16, U16, To6From7, kM4444, false),
    LOOP(0x8c3f50, U16, U16, To6From7, kM4444, true),
    LOOP(0x8c3fe0, U32, U16, To6From8, kM8888, false),
    LOOP(0x8c4050, U32, U16, To6From8, kM8888, true),
    LOOP(0x8c40d0, U32, U16, To6From9, kM8888, false),
    LOOP(0x8c4150, U32, U16, To6From9, kM8888, true),
    LOOP(0x8c41e0, U16, U16, To7From4, kM1555, false),
    LOOP(0x8c4260, U16, U16, To7From4, kM1555, true),
    LOOP(0x8c42e0, U16, U16, To7From5, kM565, false),
    LOOP(0x8c4360, U16, U16, To7From5, kM565, true),
    LOOP(0x8c43e0, U16, U16, To7From6, kM1555, false),
    LOOP(0x8c4460, U16, U16, To7From6, kM1555, true),
    LOOP(0x8c4570, U16, U16, Same, kM4444, true),
    LOOP(0x8c45d0, U32, U16, To7From8, kM8888, false),
    LOOP(0x8c4640, U32, U16, To7From8, kM8888, true),
    LOOP(0x8c46c0, U32, U16, To7From9, kM8888, false),
    LOOP(0x8c4740, U32, U16, To7From9, kM8888, true),
    LOOP(0x8c47d0, U16, U32, To8From4, kM1555, false),
    LOOP(0x8c4850, U16, U16, To4From4, kM1555, false),
    LOOP(0x8c48a0, U16, U32, To8From4, kM1555, true),
    LOOP(0x8c4930, U16, U16, To4From4, kM1555, true),
    LOOP(0x8c4990, U16, U32, To8From5, kM565, false),
    LOOP(0x8c4a20, U16, U16, To4From5, kM565, false),
    LOOP(0x8c4a90, U16, U32, To8From5, kM565, true),
    LOOP(0x8c4b30, U16, U16, To4From5, kM565, true),
    LOOP(0x8c4bb0, U16, U32, To8From7, kM4444, false),
    LOOP(0x8c4c20, U16, U16, To4From7, kM4444, false),
    LOOP(0x8c4ca0, U16, U32, To8From7, kM4444, true),
    LOOP(0x8c4d30, U16, U16, To4From7, kM4444, true),
    LOOP(0x8c4dc0, U32, U32, To8From8, kM8888, false),
    LOOP(0x8c4e10, U32, U16, To4From8, kM8888, false),
    LOOP(0x8c4e80, U32, U32, To8From8, kM8888, true),
    LOOP(0x8c4ed0, U32, U16, To4From8, kM8888, true),
    LOOP(0x8c4f60, U16, U32, To9From4, kM1555, false),
    LOOP(0x8c4fe0, U16, U16, To5From4, kM1555, false),
    LOOP(0x8c5060, U16, U32, To9From4, kM1555, true),
    LOOP(0x8c50f0, U16, U16, To5From4, kM1555, true),
    LOOP(0x8c5170, U16, U32, To9From5, kM565, false),
    LOOP(0x8c5200, U16, U32, To9From5, kM565, true),
    LOOP(0x8c52a0, U16, U16, Same, kM565, true),
    LOOP(0x8c52f0, U16, U32, To9From6, kM1555, false),
    LOOP(0x8c5370, U16, U16, To5From6, kM1555, false),
    LOOP(0x8c53e0, U16, U32, To9From6, kM1555, true),
    LOOP(0x8c5490, U16, U16, To5From6, kM1555, true),
    LOOP(0x8c5500, U16, U32, To9From7, kM4444, false),
    LOOP(0x8c5580, U16, U16, To5From7, kM4444, false),
    LOOP(0x8c5610, U16, U32, To9From7, kM4444, true),
};

namespace {

constexpr GZConvertLoopFn FindLoop(uint32_t rva)
{
    for (const GZConvertLoop &loop : kGZConvertLoops)
        if (loop.rva == rva) return loop.fn;
    return nullptr;
}

// The original's table at 0xe4f0d8: [destination - 4][source - 4][keyed]. The loops it holds, by address.
struct Pair {
    uint32_t plain;
    uint32_t keyed;
};
constexpr Pair kTable[6][6] = {
    {{0x8c4850, 0x8c4930}, {0x8c4a20, 0x8c4b30}, {0x8c4850, 0x8c4930}, {0x8c4c20, 0x8c4d30}, {0x8c4e10, 0x8c4ed0}, {0x8c4e10, 0x8c4ed0}},
    {{0x8c4fe0, 0x8c50f0}, {0x8c3e20, 0x8c52a0}, {0x8c5370, 0x8c5490}, {0x8c5580, 0x8c38e0}, {0x8c39c0, 0x8c3a80}, {0x8c3b40, 0x8c3bf0}},
    {{0x8c3c70, 0x8c3cd0}, {0x8c3d30, 0x8c3da0}, {0x8c3e20, 0x8c3e70}, {0x8c3ed0, 0x8c3f50}, {0x8c3fe0, 0x8c4050}, {0x8c40d0, 0x8c4150}},
    {{0x8c41e0, 0x8c4260}, {0x8c42e0, 0x8c4360}, {0x8c43e0, 0x8c4460}, {0x8c3e20, 0x8c4570}, {0x8c45d0, 0x8c4640}, {0x8c46c0, 0x8c4740}},
    {{0x8c47d0, 0x8c48a0}, {0x8c4990, 0x8c4a90}, {0x8c47d0, 0x8c48a0}, {0x8c4bb0, 0x8c4ca0}, {0x8c4dc0, 0x8c4e80}, {0x8c4dc0, 0x8c4e80}},
    {{0x8c4f60, 0x8c5060}, {0x8c5170, 0x8c5200}, {0x8c52f0, 0x8c53e0}, {0x8c5500, 0x8c5610}, {0x8c3970, 0x8c3a30}, {0x8c3b00, 0x8c3ba0}},
};

uint32_t BytesPerPixel(uint32_t format)
{
    if (format >= 4 && format <= 7) return 2;
    if (format == 8 || format == 9) return 4;
    return 0;
}

} // namespace

bool __cdecl GZConvertPixels(uint8_t *dst, int32_t dstStride, uint32_t dstFormat, const uint8_t *src, int32_t srcStride,
                             uint32_t srcFormat, int32_t width, int32_t height, const uint32_t *key)
{
    const uint32_t srcBytes = BytesPerPixel(srcFormat), dstBytes = BytesPerPixel(dstFormat);
    if (srcBytes == 0 || dstBytes == 0) return false;

    // The first and last byte each range touches (unsigned, wrapping as 32-bit addresses do).
    const uint32_t srcAddress = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(src));
    const uint32_t dstAddress = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(dst));
    const uint32_t srcReach = static_cast<uint32_t>(height - 1) * static_cast<uint32_t>(srcStride);
    const uint32_t dstReach = static_cast<uint32_t>(height - 1) * static_cast<uint32_t>(dstStride);
    uint32_t srcLow, srcHigh, dstLow, dstHigh;
    if (srcStride < 0) {
        srcLow = srcAddress + srcReach;
        srcHigh = srcBytes * static_cast<uint32_t>(width) - 1 + srcAddress;
    } else {
        srcLow = srcAddress;
        srcHigh = srcBytes * static_cast<uint32_t>(width) + srcReach - 1 + srcAddress;
    }
    if (dstStride < 0) {
        dstLow = dstAddress + dstReach;
        dstHigh = dstBytes * static_cast<uint32_t>(width) - 1 + dstAddress;
    } else {
        dstLow = dstAddress;
        dstHigh = dstReach + dstBytes * static_cast<uint32_t>(width) + dstAddress - 1;
    }
    // Overlapping, with the destination starting below the source: start from the last row and walk up, so the rows still
    // to be read are not overwritten first.
    if (srcLow <= dstHigh && dstLow <= srcHigh && dstAddress < srcAddress) {
        srcStride = -srcStride;
        src += srcReach;
        dstStride = -dstStride;
        dst += dstReach;
    }

    GZConvertArgs args = {};
    args.width = args.width2 = args.width3 = width;
    args.height = args.height2 = args.height3 = height;
    args.dst = dst;
    args.dstStride = dstStride;
    args.dstFormat = dstFormat;
    args.dstBytes = dstBytes;
    args.src = src;
    args.srcStride = srcStride;
    args.srcFormat = srcFormat;
    args.srcBytes = srcBytes;
    if (key) {
        args.hasKey = 1;
        args.key = *key & cGZBuffer::FormatMask(srcFormat);
    }
    const Pair &pair = kTable[dstFormat - 4][srcFormat - 4];
    const GZConvertLoopFn loop = FindLoop(key ? pair.keyed : pair.plain);
    loop(args);
    return true;
}
