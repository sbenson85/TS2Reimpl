// The memory mip chain's pixel kernels (lane OW): src/gz/MipKernels.h.
#include "gz/MipKernels.h"

namespace nGZGraphic4::mip {

namespace {

// Per-field average rounding up.
constexpr uint32_t Average(uint32_t a, uint32_t b, uint32_t mask) { return (a | b) - (((a ^ b) >> 1) & mask); }

// count pixels, each the average of a pair.
template <uint32_t Mask> void HalveRow(uint32_t *dst, const uint32_t *src, int count)
{
    do {
        *dst = Average(src[0], src[1], Mask);
        --count;
        src += 2;
        ++dst;
    } while (count != 0);
}

// The 1-3-3-1 kernel as pairs of averages: the first and last pixels fold the edge inwards.
template <uint32_t Mask> void FilterRow(uint32_t *dst, const uint32_t *src, int count)
{
    if (count == 1) {
        dst[0] = Average(src[0], src[1], Mask);
        return;
    }
    {
        const uint32_t outer = Average(src[1], src[2], Mask);
        const uint32_t inner = Average(src[0], src[1], Mask);
        dst[0] = Average(Average(inner, outer, Mask), inner, Mask);
    }
    const uint32_t *p = src + 1;
    for (int i = 1; i < count - 1; ++i, p += 2) {
        const uint32_t outer = Average(p[0], p[3], Mask);
        const uint32_t inner = Average(p[1], p[2], Mask);
        dst[i] = Average(Average(inner, outer, Mask), inner, Mask);
    }
    {
        const uint32_t outer = Average(p[0], p[1], Mask);
        const uint32_t inner = Average(p[1], p[2], Mask);
        dst[count - 1] = Average(Average(inner, outer, Mask), inner, Mask);
    }
}

template <uint32_t Mask> void CombineTwo(uint32_t *dst, const uint32_t *const *rows, int count)
{
    const uint32_t *a = rows[0];
    const uint32_t *b = rows[1];
    do {
        *dst = Average(*a, *b, Mask);
        ++a;
        ++b;
        --count;
        ++dst;
    } while (count != 0);
}

template <uint32_t Mask> void CombineFour(uint32_t *dst, const uint32_t *const *rows, int count)
{
    const uint32_t *r0 = rows[0], *r1 = rows[1], *r2 = rows[2], *r3 = rows[3];
    do {
        const uint32_t outer = Average(*r0, *r3, Mask);
        const uint32_t inner = Average(*r1, *r2, Mask);
        *dst = Average(Average(inner, outer, Mask), inner, Mask);
        ++r0;
        ++r1;
        ++r2;
        ++r3;
        --count;
        ++dst;
    } while (count != 0);
}

} // namespace

void __cdecl HalveRow8888(uint32_t *dst, const uint32_t *src, int count) { HalveRow<kMaskChannels8888>(dst, src, count); }
void __cdecl FilterRow8888(uint32_t *dst, const uint32_t *src, int count) { FilterRow<kMaskChannels8888>(dst, src, count); }
void __cdecl HalveRow2101010(uint32_t *dst, const uint32_t *src, int count) { HalveRow<kMask2101010>(dst, src, count); }
void __cdecl FilterRow2101010(uint32_t *dst, const uint32_t *src, int count) { FilterRow<kMask2101010>(dst, src, count); }

// Bytes: the pair rounds up; the filtered pixel is (a + 3b + 3c + d + 4) / 8, the edges folding inwards.
void __cdecl HalveRow8(uint8_t *dst, const uint8_t *src, int count)
{
    do {
        *dst = uint8_t((src[0] + 1u + src[1]) >> 1);
        --count;
        src += 2;
        ++dst;
    } while (count != 0);
}

void __cdecl FilterRow8(uint8_t *dst, const uint8_t *src, int count)
{
    if (count == 1) {
        dst[0] = uint8_t((src[0] + src[1] + 1u) >> 1);
        return;
    }
    dst[0] = uint8_t((src[2] + 4u + src[0] * 3u + src[1] * 4u) >> 3);
    const uint8_t *p = src + 1;
    for (int i = 1; i < count - 1; ++i, p += 2) dst[i] = uint8_t((p[0] + 4u + (p[1] + p[2] * 1u) * 3u + p[3]) >> 3);
    dst[count - 1] = uint8_t((p[2] * 3u + p[1] * 4u + 4u + p[0]) >> 3);
}

void __cdecl CombineTwoRows8888(uint32_t *dst, const uint32_t *const *rows, int count) { CombineTwo<kMaskChannels8888>(dst, rows, count); }
void __cdecl CombineFourRows8888(uint32_t *dst, const uint32_t *const *rows, int count) { CombineFour<kMaskChannels8888>(dst, rows, count); }
void __cdecl CombineTwoRows2101010(uint32_t *dst, const uint32_t *const *rows, int count) { CombineTwo<kMask2101010>(dst, rows, count); }
void __cdecl CombineFourRows2101010(uint32_t *dst, const uint32_t *const *rows, int count) { CombineFour<kMask2101010>(dst, rows, count); }

void __cdecl CombineTwoRows8(uint8_t *dst, const uint8_t *const *rows, int count)
{
    const uint8_t *a = rows[0];
    const uint8_t *b = rows[1];
    do {
        *dst = uint8_t((*a + 1u + *b) >> 1);
        ++a;
        ++b;
        --count;
        ++dst;
    } while (count != 0);
}

void __cdecl CombineFourRows8(uint8_t *dst, const uint8_t *const *rows, int count)
{
    const uint8_t *r0 = rows[0], *r1 = rows[1], *r2 = rows[2], *r3 = rows[3];
    do {
        *dst = uint8_t((*r3 + 4u + (*r2 + *r1 * 1u) * 3u + *r0) >> 3);
        ++r0;
        ++r1;
        ++r2;
        ++r3;
        --count;
        ++dst;
    } while (count != 0);
}

} // namespace nGZGraphic4::mip
