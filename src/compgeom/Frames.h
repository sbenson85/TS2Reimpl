#pragma once
#include <cstdint>
#include <cstring>

#include "compgeom/CompGeom.h"

// nGZNumerics' frame members (lane PC, scenegraph G0b; RVAs 1.18.0.187): vectors and points through an AffineTransform3
// (src/misc/UtilitiesC.h: flags at +0, the row-major matrix at +4, the offset at +0x28) and back.
//
// The flags word says what the matrix holds: no bit of 0x1e, the identity; bit 2 (4) a uniform scale (the first column's
// length); bit 4 (0x10) or bits 2 and 3 together, a general matrix (inverted by LU decomposition, 0x1a62e0); otherwise
// a rotation (its inverse is its transpose). Bit 0 says the offset is not zero (not read here).
namespace nGZCompGeom {

// The original's members of the frame, on its layout (the game passes AffineTransform3 pointers as `this`).
struct FrameMath : AffineTransform3 {
    // 0x19c770 (__thiscall, ret 8; returns out): out = matrix * in, each row ((m1 y + m0 x) + m2 z) in that order.
    // Each coordinate is stored before the next is read: an `out` that is `in` sees the new x in y's row.
    Vec3 *VectorTo(Vec3 *out, const Vec3 *in) const;
    // 0x19c250 (__thiscall, ret 8): out = inverse(matrix) * in (the identity: a copy; a rotation: the transpose).
    // 0x19c250 eax-ignored: eax is what the last branch left (in.x bits, or the inverse's address); no caller reads it.
    void InverseVectorTo(Vec3 *out, const Vec3 *in) const;
    // 0x19c3b0 (__thiscall, ret 8; returns out): out = inverse(matrix) * (in - offset), computed as
    // inverse * in - inverse * offset per row; `in` is read whole before `out` is written.
    Vec3 *InversePointTo(Vec3 *out, const Vec3 *in) const;
    // 0x19cb30 (__thiscall, ret 4; returns out): the matrix's inverse, nine floats.
    float *InverseMatrix(float *out) const;
    // 0x19cf10 (__thiscall; returns on the x87 stack): the uniform scale, 1 without bit 2, else the first column's length
    // as float(sqrt(double((m0 m0 + m3 m3) + m6 m6))).
    float Scale() const;
    // 0x19d0d0 (__thiscall, ret 4): the frame turned into a rotation: flags = (flags & 1) | 2, the matrix that of the
    // quaternion (x, y, z, w). 0x19d0d0 eax-ignored: eax is the matrix's last word (a spill); no caller reads it.
    void SetRotation(const float *quaternion);
    // 0x19c0e0 (__thiscall, ret 4; returns out; src/compgeom/Matrix4.h): the frame as a 4x4 (sixteen floats: the 3x3
    // matrix, the offset in the last column, a last row of 0 0 0 1).
    float *ToMatrix4(float *out) const;
};
static_assert(sizeof(FrameMath) == sizeof(AffineTransform3));

inline const FrameMath &AsFrame(const AffineTransform3 &frame)
{
    return static_cast<const FrameMath &>(frame);
}

// 0x1a6590 (__cdecl; returns out): the transpose of a 3x3 matrix, element by element in the original's order (out[0],
// out[3], out[6], out[1], ...: an `out` that is `m` gets the original's partly transposed result).
float *__cdecl TransposeMatrix(float *out, const float *m);

// The 3x3 LU decomposition with partial pivoting (0x1a7aa0 and 0x1a7e50; RVAs 1.18.0.187), 0x34 bytes: the packed L and U
// in place of the matrix, the row permutation and the determinant. Every operation keeps the original's operand order
// (a NaN pair gives the first operand's NaN).
struct LuDecomposition3 {
    float mLu[9];
    int32_t mRow[3];
    float mDeterminant; // 0 when singular (the elimination stopped at a zero pivot)
    // 0x1a7aa0 (__thiscall, ret 4; returns this): decompose the row-major matrix `in`.
    LuDecomposition3 *Decompose(const float *in);
    // 0x1a7e50 (__thiscall, ret 8; returns out): out = the solution of (matrix) x = b, by forward then back substitution.
    float *Solve(float *out, const float *b) const;
};
static_assert(sizeof(LuDecomposition3) == 0x34);

// 0x1a62e0 (__cdecl: out, matrix; returns out): the 3x3 inverse by LU decomposition, nine zeros for a singular matrix.
// Row i of the result is the solution of (matrix) x = e_i (the original's layout, not transposed back).
float *__cdecl InverseMatrix3(float *out, const float *m);

// The NaN that SSE arithmetic gives when the first operand is one: that NaN, quieted.
inline float QuietNaN(float nan)
{
    uint32_t bits;
    std::memcpy(&bits, &nan, 4);
    bits |= 0x00400000u;
    std::memcpy(&nan, &bits, 4);
    return nan;
}
// a + b as the original's `addss a, b`: when both are NaNs the result is a's. The compiler may swap the operands of a
// plain +, and x87 arithmetic (a float returned on the x87 stack, then combined) picks the NaN with the larger
// significand instead.
inline float AddInOrder(float a, float b)
{
    return a != a ? QuietNaN(a) : a + b;
}

} // namespace nGZCompGeom
