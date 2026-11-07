#pragma once
#include <cstdint>
#include <cstring>

#include "compgeom/Frames.h"
#include "misc/X87.h"

// Scalar helpers of the cConvexT shapes (lane PM, scenegraph G0b step 4): the original's SSE and x87 operations with
// their operand order, so that a NaN pair gives the NaN the original gives (docs/lane/test.md "NaN operand order").
namespace nGZCompGeom::convex {

inline uint32_t BitsOf(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
inline float FloatOf(uint32_t b)
{
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

// mulss a, b / subss a, b: a NaN first operand wins (quieted); addss is AddInOrder (Frames.h).
inline float MulS(float a, float b) { return a != a ? QuietNaN(a) : a * b; }
inline float AddS(float a, float b) { return AddInOrder(a, b); }
inline float SubS(float a, float b) { return a != a ? QuietNaN(a) : a - b; }
inline float DivS(float a, float b) { return a != a ? QuietNaN(a) : a / b; } // divss a, b
inline float Negated(float f) { return FloatOf(BitsOf(f) ^ 0x80000000u); }

// x87: of two NaNs the larger significand wins, equal significands the positive one (found by lane PH's tests).
inline double X87Pick(double a, double b)
{
    uint64_t ba, bb;
    std::memcpy(&ba, &a, 8);
    std::memcpy(&bb, &b, 8);
    const uint64_t sa = ba & 0x000fffffffffffffull, sb = bb & 0x000fffffffffffffull;
    if (sa != sb) return sb > sa ? b : a;
    return (ba >> 63) ? b : a;
}
// fmul / fadd between two values the x87 holds (a float loaded, or an earlier result), rounded to the precision control.
inline double Mul87(double a, double b)
{
    return x87::Rounded((a != a && b != b) ? X87Pick(a, b) : a * b);
}
inline double Add87(double a, double b)
{
    return x87::Rounded((a != a && b != b) ? X87Pick(a, b) : a + b);
}
// fld a; fmul dword [b] (or the other way round): a NaN in the register beats a signalling memory operand.
inline double Mul87(float a, float b)
{
    if (a != a && b != b && !(BitsOf(b) & 0x00400000u)) return x87::Rounded(static_cast<double>(a));
    return Mul87(static_cast<double>(a), static_cast<double>(b));
}
inline double Add87(float a, float b)
{
    if (a != a && b != b && !(BitsOf(b) & 0x00400000u)) return x87::Rounded(static_cast<double>(a));
    return Add87(static_cast<double>(a), static_cast<double>(b));
}
// fstp dword: the x87 value stored as a float.
inline float Stored(double v) { return static_cast<float>(v); }

// A frame with no rotation or scale and the offset `p` (flags 1), as the shapes build them on the stack.
inline AffineTransform3 UnitFrameAt(const Vec3 &p)
{
    AffineTransform3 f;
    f.mUnknown0 = 1;
    for (float &m : f.mMatrix) m = 0.0f;
    f.mMatrix[0] = f.mMatrix[4] = f.mMatrix[8] = 1.0f;
    f.mOffset[0] = p.x;
    f.mOffset[1] = p.y;
    f.mOffset[2] = p.z;
    return f;
}
// The frame placed at `p`: a copy with bit 0 of the flags set and the offset replaced.
inline AffineTransform3 FrameAt(const AffineTransform3 &frame, const Vec3 &p)
{
    AffineTransform3 f = frame;
    f.mUnknown0 |= 1;
    f.mOffset[0] = p.x;
    f.mOffset[1] = p.y;
    f.mOffset[2] = p.z;
    return f;
}
inline Vec3 OffsetOf(const AffineTransform3 &f) { return {f.mOffset[0], f.mOffset[1], f.mOffset[2]}; }
inline float ScaleOf(const AffineTransform3 &f) { return AsFrame(f).Scale(); }

// The squared length of v as the shapes sum it: (x x + y y) + z z.
inline float LengthSquared(const Vec3 &v) { return AddS(AddS(MulS(v.x, v.x), MulS(v.y, v.y)), MulS(v.z, v.z)); }

} // namespace nGZCompGeom::convex
