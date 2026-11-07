#pragma once
// Small pure helpers of the GenericSimCall cases (opcode 0x01).
#include <cstdint>
#include <cstring>
#include <emmintrin.h>
#include <xmmintrin.h>

namespace gscmath {

struct Pair {
    int32_t x, y;
};

// 0x57d6d0 (__thiscall on a rectangle of four ints (x0, y0, x1, y1), one stack argument, ret 4): the centre, each coordinate
// the sum of the two ends halved toward zero (cdq; sub; sar). Not hooked: GenericSimCall cases 34 uses it inline.
inline Pair RectCenter(const int32_t *rect)
{
    const int32_t sx = static_cast<int32_t>(static_cast<uint32_t>(rect[2]) + static_cast<uint32_t>(rect[0]));
    const int32_t sy = static_cast<int32_t>(static_cast<uint32_t>(rect[3]) + static_cast<uint32_t>(rect[1]));
    return {(sx - (sx >> 31)) >> 1, (sy - (sy >> 31)) >> 1};
}

// The float-to-int trick of the original (cases 62 and 66): add 1.5 * 2^23 (single precision) and take the low mantissa
// bits, which rounds to nearest even for |value| below 2^22.
inline int32_t RoundViaMagic(float value)
{
    const float sum = _mm_cvtss_f32(_mm_add_ss(_mm_set_ss(12582912.0f), _mm_set_ss(value)));
    int32_t bits;
    std::memcpy(&bits, &sum, sizeof(bits));
    return bits - 0x4b400000;
}

} // namespace gscmath
