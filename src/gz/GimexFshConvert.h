#pragma once
// The FSH back end's image conversion (lane MV, 0x885d30, 0x8885a1..0x888f97, 0x88cc20..0x88d5e8; src/gz/GimexFshConvert.size.cpp):
// the YCbCr / RGB converters (float and integer), the 4:2:0 planar and packed 4:2:2 writers, the plane reorderer and the
// size helpers. All __cdecl; names are tentative. Source pixels are B, G, R bytes (a pixel is 4 bytes in the packers).
#include "gz/Gimex.h"

namespace gimex {
namespace fsh {

// 0x885d30: (int)(x + 0.5f) clamped to 0..255 (a value the conversion cannot represent gives 0).
int32_t __cdecl ClampRound(float x);
// 0x8885a1: out1 = red, out2 = green, out3 = blue (floats, 0..255 scale) of a studio-range Y, U, V.
void __cdecl YuvToRgbFloat(float *out1, float *out2, float *out3, float y, float u, float v);
// 0x888f97: out1 = Y, out2 = Cb, out3 = Cr (full-range, Y offset 16) of a red, green, blue.
void __cdecl RgbToYCbCrFloat(float *out1, float *out2, float *out3, float red, float green, float blue);
// 0x888939: in = Y, Cb, Cr; out = red, green, blue, each clamped to 0..255.
void __cdecl YCbCrToRgb(const int32_t *in, int32_t *out);
// 0x88d5e8: in = red, green, blue; out = Y, Cb + 128, Cr + 128 (JPEG weights), truncated.
void __cdecl RgbToYCbCrInt(const int32_t *in, int32_t *out);
// 0x888a19: two pixels from a U Y V Y pair at src (bytes: Y0, U, Y1, V) written as two B, G, R, 0xff pixels at dst.
void __cdecl YuyvPairToBgra(uint8_t *dst, const uint8_t *src);
// 0x88d362: v rounded to the nearest multiple of d (ties away from zero), 0 for 0.
int32_t __cdecl RoundToMultiple(int32_t v, int32_t d);
// 0x888b13 / 0x888c40: one Cb / Cr byte for a 2x2 block of pixels p0..p3 (B, G, R bytes), the block's average weighted by
// each pixel's luma and rounded to a multiple of d, clamped to 0..255. Returns dst + 1.
uint8_t *__cdecl AverageCb(uint8_t *dst, const uint8_t *p0, const uint8_t *p1, const uint8_t *p2, const uint8_t *p3, int32_t d);
uint8_t *__cdecl AverageCr(uint8_t *dst, const uint8_t *p0, const uint8_t *p1, const uint8_t *p2, const uint8_t *p3, int32_t d);
// 0x88d212: the luma bytes of a 2x2 block, each rounded to a multiple of d and clamped, stored at dst[0], dst[1],
// dst[stride], dst[stride + 1]. Returns dst + 2.
uint8_t *__cdecl StoreLuma(uint8_t *dst, const uint8_t *p0, const uint8_t *p1, const uint8_t *p2, const uint8_t *p3, int32_t d,
                           int32_t stride);
// 0x88d39e: dst[0..3] = Y of p1, the Cb average of p1 and p2, Y of p2, the Cr average (p0 and p3 are converted and
// dropped by the original).
void __cdecl PackYuyv(uint8_t *dst, const uint8_t *p0, const uint8_t *p1, const uint8_t *p2, const uint8_t *p3);
// 0x88ce4e: the whole image (info width / height) as packed Y U Y V pairs, rows `stride` bytes apart in src.
// Returns the byte count, ((width + 1) & ~1) * 2 * height.
int32_t __cdecl RgbToYuyv(const Info *info, int32_t stride, const uint8_t *src, uint8_t *dst);
// 0x88cc20: the whole image as 4:2:0 planes (Y, then Cb, then Cr; each a quarter of the size), optionally with alpha
// planes (flags 0x2000) written by ConvertAlphaPlanes. Returns the plane bytes (height x width x 3 / 2).
// (lumaStep, cbStep, crStep: the rounding multiples; mode 5 with the alpha flag uses alphaWidth / alphaHeight for the
// count and the luma row step; pixelFormat and mode go to ReorderPlanes; `unused` goes nowhere.)
int32_t __cdecl RgbToYuv420(const Info *info, int32_t stride, const uint8_t *src, uint8_t *dst, int32_t lumaStep,
                            int32_t cbStep, int32_t crStep, uint32_t flags, int32_t mode, int32_t pixelFormat, int32_t unused,
                            int32_t alphaWidth, int32_t alphaHeight);
// 0x88cf36: the index of the highest set bit (0 for 0 and 1).
uint32_t __cdecl HighestBit(uint32_t v);
// 0x88cf96: a new buffer of height x stride + 0x20 bytes (zeroed) holding the image reordered: mode 6 swizzles (4, 8, 16
// bits; 32 copies; 24 frees the buffer and answers 0), modes 2, 5 and 8 copy pixel by pixel (tiled for mode 5,
// interleaved for 2 and 8; format 0x16 / 0x1e 4-byte tiles, 0x40 / 0x79 / 0x7a nibbles). 0 when out of memory.
uint8_t *__cdecl ReorderImage(const uint8_t *src, int32_t width, int32_t height, int32_t bits, int32_t format, int32_t mode,
                              int32_t stride);
// 0x888e00: reorders the three 8-bit planes (luma, then two quarter planes) at `planes` in place through ReorderImage.
void __cdecl ReorderPlanes(uint8_t *planes, int32_t width, int32_t height, int32_t format, int32_t mode);
// 0x88d5c9: b + f, rounded up to the alignment mask `align` (the low bits to clear) unless a is the last (a >= count - 1).
int32_t __cdecl AlignedOffset(int32_t a, int32_t b, int32_t unused, int32_t align, int32_t count, int32_t f);
// 0x88d52e: the bytes of `levels` mip levels of width x height at `bits` per pixel (each level but the last aligned),
// plus count times the size computed from the remaining dimensions; *levelSize gets that size.
int32_t __cdecl MipChainSize(int32_t levels, int32_t count, uint32_t unused, uint32_t align, int32_t lastLevel,
                             int32_t *levelSize, int32_t width, int32_t height, int32_t bits);
// 0x888d6d: the bytes of a mip chain (float arithmetic), *total = 6 times that + 0x20; answers a new buffer of that size.
void *__cdecl AllocateMipChain(int32_t width, int32_t height, int32_t bytes, int32_t *total, int32_t levels, uint32_t align);
// 0x888efc: for a 3 (block compressed) header, or when `force` is set, answers (*bits, *kind): 1afb or word 0x61 gives
// (8, 3), 1cfb or 0x62 gives (8, 5), else (4, 1). Calls the original 0x88888f.
void __cdecl DetectPixelFormat(const uint8_t *header, const void *data, int32_t force, int32_t *bits, int32_t *kind,
                               int32_t platform);

} // namespace fsh
} // namespace gimex
