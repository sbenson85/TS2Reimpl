#pragma once
#include <cstdint>

#include "compgeom/CompGeom.h"

// nGZNumerics' 4x4 matrices (sixteen floats, row-major, the translation in the last column: elements 3, 7 and 11) and the
// frame / rigid-transform members that build them (scenegraph campaign, lane TR; RVAs 1.18.0.187). The members are
// declared beside their types: FrameMath::ToMatrix4 (Frames.h), RigidTransform3::ToMatrix4 and RotateVector
// (CompGeom.h), FramePlacement::ComposeRigid (SeparatingAxes.h); they are defined in Matrix4.cpp.
//
// NaNs: every product, sum, difference and quotient keeps the original's operand order (a NaN pair gives the first
// operand's NaN, quieted), as in Frames.cpp; the product's order is a table read off 0x1a5eb0.
namespace nGZCompGeom {

// 0x1a5eb0 (__cdecl: out, a, b; returns out): out = the product, element 4i+j the sum over k of a[4k+j] b[4i+k] (the
// first operand of the product `b * a` in the usual row-major notation), each sum in the original's term and operand
// order. Elements are stored column by column (0, 4, 8, 12, 1, ...); an `out` that is `a` or `b` sees the stores.
float *__cdecl Matrix4Multiply(float *out, const float *a, const float *b);

// The 4x4 LU decomposition with partial pivoting (0x1a7c50 and 0x1a8030), 0x54 bytes: the packed L and U in place of the
// matrix, the pivot row for each column, and the determinant. The 3x3 analogue is LuDecomposition3 (Frames.h).
struct LuDecomposition4 {
    float mLu[16];
    int32_t mRow[4];
    float mDeterminant; // 0 when singular (the elimination stopped at a zero pivot)
    // 0x1a7c50 (__thiscall, ret 4; returns this): decompose the row-major matrix `in`.
    LuDecomposition4 *Decompose(const float *in);
    // 0x1a8030 (__thiscall, ret 8; returns out): out = the solution of (matrix) x = b, by forward then back substitution.
    float *Solve(float *out, const float *b) const;
};
static_assert(sizeof(LuDecomposition4) == 0x54);

// 0x1a6410 (__cdecl: out, m; returns out): the 4x4 inverse by LU decomposition, sixteen zeros when the determinant is
// zero. Row i of the result is the solution of (matrix) x = e_i (the original's layout, not transposed back).
float *__cdecl Matrix4Inverse(float *out, const float *m);

// 0x1a5870 (__cdecl: out, v; returns out): the identity with the vector in the last column (elements 3, 7, 11).
float *__cdecl Matrix4Translation(float *out, const float *translation);

// 0x1a7200 (__cdecl: rotation, scale, translation, out; no result; eax is not read by any caller): out = the 3x3
// `rotation` with column j scaled by scale[j] (element 4i+j = scale[j] * rotation[3i+j], i and j below 3), the
// translation in the last column and a last row of 0 0 0 1. Stored column by column (scale[0]'s, then scale[1]'s...).
void __cdecl Matrix4FromRotationScaleTranslation(const float *rotation, const float *scale, const float *translation,
                                                 float *out);

// 0x1a5660 (__cdecl: out, x, y, z; returns out): the row-major 3x3 rotation matrix of the angles (radians) about the three
// axes: with c, s the cosines and sines (float(cos(double(angle)))), row 0 is (cz cy, sy sx cz - sz cx, sy cx cz + sz sx),
// row 1 (sz cy, sy sx sz + cz cx, sy cx sz - cz sx), row 2 (-sy, cy sx, cy cx). Operand order of every product and sum is
// the original's.
float *__cdecl EulerAnglesToMatrix3(float *out, float x, float y, float z);

} // namespace nGZCompGeom
