#pragma once
// The leaves of the FSH back end (lane MQ, 0x88374d..0x885849; src/gz/GimexFshLeaves.size.cpp): the PS2-style page
// swizzles for 4, 8 and 16-bit pixels, the word helpers (endian by platform), the colour converters between a 16 / 32-bit
// colour word and B, G, R, A bytes, the tiled-image addressing and the helpers that pick a record type from the channel
// bits. All __cdecl, RVAs 1.18.0.187. Names are tentative.
#include "gz/Gimex.h"

namespace gimex {
namespace fsh {

// ---- swizzling (the PS2's texture memory layout: pages of 128 x 128 / 128 x 64 pixels) ----
// 0x88374d: the bit offset of pixel (x, y) in a swizzled 16-bit image `width` pixels wide (callers shift it right 3).
int32_t __cdecl Swizzle16Offset(uint32_t x, uint32_t y, int32_t width);
// 0x8837a5 / 0x883bf0: the in-block permutation of 4-bit pixels (256 bytes in, 256 out), swizzled -> linear / linear ->
// swizzled. 0x883851 / 0x883c9c: the same for 8-bit pixels. The tables are the image's (the permutations of 32 and 256).
int32_t __cdecl UnswizzleNibbles(const uint8_t *in, uint8_t *out);
int32_t __cdecl SwizzleNibbles(const uint8_t *in, uint8_t *out);
int32_t __cdecl UnswizzleBytes(const uint8_t *in, uint8_t *out);
int32_t __cdecl SwizzleBytes(const uint8_t *in, uint8_t *out);
// 0x883876 / 0x883a33 (4 and 8-bit) and 0x883cf7 / 0x883eb9: one page, unswizzled / swizzled. The callers pass 128 x 128
// (4-bit) and 128 x 64 (8-bit); the order tables hold 32 entries.
int32_t __cdecl UnswizzlePage4(int32_t width, int32_t height, const void *source, void *dest);
int32_t __cdecl UnswizzlePage8(int32_t width, int32_t height, const void *source, void *dest);
int32_t __cdecl SwizzlePage4(int32_t width, int32_t height, const void *source, void *dest);
int32_t __cdecl SwizzlePage8(int32_t width, int32_t height, const void *source, void *dest);
// 0x884b24 / 0x884fa8: a whole 16-bit image, linear -> swizzled / swizzled -> linear (1). 0x884b8b / 0x884d93: a 4 / 8-bit
// image, linear -> swizzled, page by page. 0x88500f / 0x88521a: swizzled -> linear. All return 0 but the 16-bit pair.
int32_t __cdecl Swizzle16(const uint8_t *source, uint8_t *dest, int32_t width, int32_t height);
int32_t __cdecl Unswizzle16(const uint8_t *source, uint8_t *dest, int32_t width, int32_t height);
int32_t __cdecl Swizzle4(const uint8_t *source, uint8_t *dest, int32_t width, int32_t height);
int32_t __cdecl Swizzle8(const uint8_t *source, uint8_t *dest, int32_t width, int32_t height);
int32_t __cdecl Unswizzle4(const uint8_t *source, uint8_t *dest, int32_t width, int32_t height);
int32_t __cdecl Unswizzle8(const uint8_t *source, uint8_t *dest, int32_t width, int32_t height);

// ---- words by platform ----
// 0x884193: platforms 5, 7, 0xf and 0x11 are big-endian. Only al is set.
bool __cdecl IsBigEndianPlatform(int32_t platform);
// 0x88407b: a 1..4 byte number, big or little-endian by platform (0 for another width).
uint32_t __cdecl ReadWord(const void *bytes, int32_t width, int32_t platform);
// 0x8840a3 / 0x8840f5 / 0x884167: store the low `width` (1..4) bytes of a value, least / most significant first / by
// platform. Another width stores nothing.
void __cdecl PutLittleEndian(void *dest, uint32_t value, int32_t width);
void __cdecl PutBigEndian(void *dest, uint32_t value, int32_t width);
void __cdecl PutWord(void *dest, uint32_t value, int32_t width, int32_t platform);

// ---- colour words to B, G, R, A (byte 0 = blue ... byte 3 = alpha, scaled to 0..255 with rounding) ----
uint32_t __cdecl Colour2101010(uint32_t word);        // 0x8841b0
uint32_t __cdecl Colour4444(uint32_t word);           // 0x88421e
uint32_t __cdecl Colour565(uint32_t word);            // 0x884285
uint32_t __cdecl Colour1555(uint32_t word);           // 0x8842df
uint32_t __cdecl Colour655(uint32_t word);            // 0x88433b (red 6 bits)
uint32_t __cdecl Colour6666(uint32_t word);           // 0x884393
uint32_t __cdecl Colour3555(uint32_t word);           // 0x8846b7 (the top bit picks 1555 or 3-4-4-4)
uint32_t __cdecl Colour88(uint32_t word);             // 0x884767 (grey in the low byte, alpha in the next)
uint32_t __cdecl Colour1555Alt(uint32_t word);        // 0x8849c7 (alpha 0, 0x80 or 0xff)
uint32_t __cdecl Colour1555Swizzled(uint32_t word);   // 0x88551d

// ---- B, G, R, A bytes to colour words ----
uint32_t __cdecl Pack2101010(const uint8_t *bgra);    // 0x8843fa
uint32_t __cdecl Pack4444(const uint8_t *bgra);       // 0x88445c (a visible alpha stays at least 1)
uint16_t __cdecl Pack1555(const uint8_t *bgra);       // 0x8844da
uint16_t __cdecl Pack565(const uint8_t *bgra);        // 0x884519
uint16_t __cdecl Pack655(const uint8_t *bgra);        // 0x884546
uint32_t __cdecl Pack6666(const uint8_t *bgra);       // 0x884573 (a visible alpha stays at least 1)
uint8_t __cdecl PackGrey44(const uint8_t *bgra);      // 0x884781 (alpha in the high nibble, luma in the low)
uint16_t __cdecl Pack3555(const uint8_t *bgra);       // 0x8847eb
int32_t __cdecl Luma(const uint8_t *bgra);            // 0x88486e (rounded 0.587 G + 0.299 R + 0.114 B)
uint16_t __cdecl PackGrey88(const uint8_t *bgra);     // 0x8848ce (alpha << 8 | luma)
uint16_t __cdecl Pack556(const uint8_t *bgra);        // 0x884989
uint16_t __cdecl Pack1555Swapped(const uint8_t *bgra); // 0x885579 (red in the low bits, alpha 0 or 1)
uint32_t __cdecl Pack1555Alt(const uint8_t *bgra);    // 0x884a34
// 0x884938: 1 when `count` entries (4 bytes each) are greys and strictly increasing, else 0 (also 1 for no entries).
int32_t __cdecl IsGreyRamp(const uint8_t *entries, int32_t count);

// ---- sizes and tiled addressing ----
// 0x884a7e / 0x884ade: the size of an image padded to its pixel format's block (bits 4 / 8 / 15 / 16 / 32).
void __cdecl PaddedSize(uint32_t *width, uint32_t *height, int32_t sourceWidth, int32_t sourceHeight, int32_t bits);
void __cdecl PageSize(uint32_t *width, uint32_t *height, uint32_t sourceWidth, uint32_t sourceHeight, int32_t bits);
// 0x8845f1 / 0x884623 / 0x884655 / 0x884687: the byte offset of pixel (x, y) in an image tiled in blocks of 4 x 4 16-bit
// pixels, 4 x 4 32-bit, 8 x 8 4-bit and 8 x 4 8-bit pixels; 0x885450 picks one by the bit count.
int32_t __cdecl TileOffset16(int32_t x, int32_t y, int32_t width);
int32_t __cdecl TileOffset32(int32_t x, int32_t y, int32_t width);
int32_t __cdecl TileOffset4(int32_t x, int32_t y, int32_t width);
int32_t __cdecl TileOffset8(int32_t x, int32_t y, int32_t width);
int32_t __cdecl TileOffset(int32_t x, int32_t y, int32_t width, int32_t bits);
// 0x8854b6: the index of pixel (x, y) with the bits of x and y interleaved (power-of-two sides).
int32_t __cdecl InterleavedIndex(int32_t x, int32_t y, int32_t width, int32_t height);
// 0x885429: a palette index with entries 8..15 of each 32 moved up 8, and 16..23 all moved to 24 (the PS2 palette order;
// ORIGINAL-BUG: the second half was meant to swap back).
uint32_t __cdecl PaletteOrder(uint32_t index);

// ---- record types from the channel bits (alpha, red, green, blue); the values are palette types of FSH ----
int32_t __cdecl PaletteTypeForBitsA(int32_t alpha, int32_t red, int32_t green, int32_t blue);  // 0x8855c0
int32_t __cdecl PaletteTypeForBitsB(int32_t alpha, int32_t red, int32_t green, int32_t blue);  // 0x88563c
int32_t __cdecl PaletteTypeForBitsC(int32_t alpha, int32_t red, int32_t green, int32_t blue);  // 0x8856d9
int32_t __cdecl PaletteTypeForBitsD(int32_t alpha, int32_t red, int32_t green, int32_t blue);  // 0x885801
int32_t __cdecl PaletteTypeForBitsE(int32_t alpha, int32_t red, int32_t green, int32_t blue);  // 0x885825
int32_t __cdecl PaletteTypeForBitsF(int32_t alpha, int32_t red, int32_t green, int32_t blue);  // 0x88568b

// 0x88570d: whether the image (palette entries first..count of an 8-bit one, else every pixel) is grey. Only al is set.
bool __cdecl IsGreyImage(const uint8_t *pixels, const Info *info);
// 0x88578d: whether a shape's name is a mipmap's ("mm" and a digit at the start, or "_mm" and a digit within the first
// `length` - 3 characters; length 0: the whole name; 1..4: no).
bool __cdecl IsMipmapName(const char *name, int32_t length);

} // namespace fsh
} // namespace gimex
