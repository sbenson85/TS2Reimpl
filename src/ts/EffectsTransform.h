#pragma once
// The effects' placement and the float arithmetic every effect's code needs (lane WO, rendering row E1; moved here from
// cTSCommonSystemObjects, whose objects use them too).
#include <cstdint>
#include <cstring>

namespace fx {

struct Vector3 {
    float x, y, z;
};

// An effect's placement (0x38 bytes): flag bit 1 = rotated, the rotation's rows, a uniform scale, the translation. A point
// goes to the world as rotation * p * scale + translation.
struct EffectsTransform {
    static constexpr uint8_t kRotated = 2;
    static constexpr uint8_t kTranslated = 4;

    uint8_t flags;      // +0 (bit 1: the rotation is used)
    uint8_t pad[3];     // +1 unused; +2 a version word (Version()), one more per change
    float m[3][3];      // +4 the rotation, row by row
    float scale;        // +0x28
    Vector3 translation; // +0x2c

    // 0x3a34b0 (ret 4): a world point brought back into this transform's space, in place: the translation taken off, the
    // scale divided out (unless it is 1), and the transposed rotation when the flag says so.
    void ToLocal(Vector3 *point) const;
    // 0xbc0f80 (ret 4): a direction turned by the rotation, in place, when the flag says so (each row from y).
    void Rotate(Vector3 *vector) const;
    // 0xbbc9b0: the identity; the flags and the version 0, the word at +1 left alone.
    void Reset();
    // 0xbc1010 (ret 4): a point to the outer space, in place: Rotate, then the scale, then the translation.
    void Apply(Vector3 *point) const;

    uint16_t &Version() { return *reinterpret_cast<uint16_t *>(pad + 1); }
};
static_assert(sizeof(EffectsTransform) == 0x38, "EffectsTransform layout");

// The arithmetic with `a` the first operand. When both operands are NaN, SSE keeps the first one's payload (quieted), and
// these sums meet NaNs of two kinds: a passed-in one and the default one an invalid operation makes (0 * inf, inf - inf:
// sign set). A plain `a + b` leaves the operand order to the compiler, which swaps commutative operands (also of the
// intrinsics), so the first operand's NaN is kept explicitly: with `a` not a NaN, both orders give the same result.
inline float FirstNaN(float a, float result)
{
    if (a == a) return result;
    uint32_t bits;
    std::memcpy(&bits, &a, 4);
    bits |= 0x400000; // quieted
    std::memcpy(&a, &bits, 4);
    return a;
}
inline float Add(float a, float b) { return FirstNaN(a, a + b); }
inline float Sub(float a, float b) { return a - b; }
inline float Mul(float a, float b) { return FirstNaN(a, a * b); }
inline float Div(float a, float b) { return FirstNaN(a, a / b); } // seen keeping the divisor's NaN where several share it

// p.y * r[1] + p.x * r[0] + p.z * r[2], summed in that order (most of the original's rows start from y).
inline float RowFromY(float x, float y, float z, float rx, float ry, float rz)
{
    return Add(Add(Mul(y, ry), Mul(x, rx)), Mul(z, rz));
}
// p.x * r[0] + p.y * r[1] + p.z * r[2] (the rows that start from x).
inline float RowFromX(float x, float y, float z, float rx, float ry, float rz)
{
    return Add(Add(Mul(x, rx), Mul(y, ry)), Mul(z, rz));
}

} // namespace fx
