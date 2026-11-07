#pragma once
// The pixel kernels of the engine's memory mip chain (lane OW, rendering): the row reducers and the row combiners that
// cImageManipDX's memory-texture copy (0x184710) hands to its level reducers (0x1854e0 / 0x185770, MemoryImage.h).
//
// Reducers (dst, src, count): `count` output pixels from 2 * count input pixels of one row (the width halves).
// Combiners (dst, rows, count): `count` output pixels from 2 or 4 rows (the height halves).
// Each comes as a plain halving (the 2-tap pair) and a filtered one (the 4-tap 1-3-3-1 kernel) in three pixel layouts:
// 8-bit channels packed in 32 bits (A8R8G8B8 / X8R8G8B8 and the decoded DXT), A2R10G10B10 packed 10-10-10-2 (the per-field
// carry mask), and one byte per pixel (A8 / L8). All are cdecl, as the original's (the reducer and combiner are passed
// as function pointers). The averages round up per field: (a | b) - (((a ^ b) >> 1) & mask).
//
// Counts are at least 1 (the loops of the plain kernels run count times, as the original's do-while).
#include <cstdint>

namespace nGZGraphic4::mip {

// One 32-bit pixel layout: the mask clears the bit each field's shifted-in neighbour would carry across.
constexpr uint32_t kMaskChannels8888 = 0x7f7f7f7f;
constexpr uint32_t kMask2101010 = 0x5ff7fdff;

using Reducer8888 = void(__cdecl *)(uint32_t *dst, const uint32_t *src, int count);
using Combiner8888 = void(__cdecl *)(uint32_t *dst, const uint32_t *const *rows, int count);
using Reducer8 = void(__cdecl *)(uint8_t *dst, const uint8_t *src, int count);
using Combiner8 = void(__cdecl *)(uint8_t *dst, const uint8_t *const *rows, int count);

// Row reducers.
void __cdecl HalveRow8888(uint32_t *dst, const uint32_t *src, int count);   // 0x185280
void __cdecl FilterRow8888(uint32_t *dst, const uint32_t *src, int count);  // 0x1852c0
void __cdecl HalveRow2101010(uint32_t *dst, const uint32_t *src, int count);  // 0x185100
void __cdecl FilterRow2101010(uint32_t *dst, const uint32_t *src, int count); // 0x185140
void __cdecl HalveRow8(uint8_t *dst, const uint8_t *src, int count);        // 0x185400
void __cdecl FilterRow8(uint8_t *dst, const uint8_t *src, int count);       // 0x185430

// Row combiners: the first two rows (plain) or all four (filtered: 1-3-3-1 as the original's pair of averages).
void __cdecl CombineTwoRows8888(uint32_t *dst, const uint32_t *const *rows, int count);    // 0x1862a0
void __cdecl CombineFourRows8888(uint32_t *dst, const uint32_t *const *rows, int count);   // 0x1862e0
void __cdecl CombineTwoRows2101010(uint32_t *dst, const uint32_t *const *rows, int count);  // 0x1861c0
void __cdecl CombineFourRows2101010(uint32_t *dst, const uint32_t *const *rows, int count); // 0x186200
void __cdecl CombineTwoRows8(uint8_t *dst, const uint8_t *const *rows, int count);         // 0x186380
void __cdecl CombineFourRows8(uint8_t *dst, const uint8_t *const *rows, int count);        // 0x1863c0

} // namespace nGZGraphic4::mip
