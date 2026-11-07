#include <cmath>
#include <cstring>
#include "compgeom/ConvexInternal.h"
#include "compgeom/SeparatingAxes.h"

// The box helpers under the cuboid shapes (lane PP, scenegraph G0b step 4): the frame's per-axis scales, a cuboid's corner
// along a direction, and the overlap of two cuboids placed by frames (the 15-axis separating test). Every operation keeps
// the original's operand order, so that a NaN pair gives the NaN the original gives.
namespace nGZCompGeom {

using namespace convex;

Vec3 *ConvexFrame::AxisScales(Vec3 *out) const
{
    if (!(mUnknown0 & 4)) {
        *out = {1.0f, 1.0f, 1.0f};
        return out;
    }
    const float *m = mMatrix;
    const auto norm = [](float a, float b, float c) {
        return static_cast<float>(std::sqrt(static_cast<double>(AddS(AddS(MulS(a, a), MulS(b, b)), MulS(c, c)))));
    };
    const float x = norm(m[0], m[3], m[6]);
    if (!(mUnknown0 & 8)) {
        *out = {x, x, x};
        return out;
    }
    out->x = x;
    out->y = norm(m[1], m[4], m[7]);
    out->z = norm(m[2], m[5], m[8]);
    return out;
}

Vec3 *ConvexCuboid::SupportCorner(Vec3 *out, const Vec3 *direction) const
{
    // comiss 0, d: only an ordered 0 > d flips the sign (a NaN direction does not).
    const auto pick = [](float h, float d) { return 0.0f > d ? Negated(h) : h; };
    out->z = pick(half.z, direction->z);
    out->y = pick(half.y, direction->y);
    out->x = pick(half.x, direction->x);
    return out;
}

bool __cdecl BoxesMeetOnAxes15(const float *a, const float *b, const float *R, const float *T)
{
    constexpr float kEpsilon = 1.1920929e-07f; // FLT_EPSILON
    float A[9];
    for (int i = 0; i < 9; ++i) A[i] = AddS(FloatOf(BitsOf(R[i]) & 0x7fffffffu), kEpsilon);
    const auto M = [](float x, float y) { return MulS(x, y); };
    const auto S = [](float x, float y) { return AddS(x, y); };
    const auto Abs = [](float x) { return FloatOf(BitsOf(x) & 0x7fffffffu); };
    const float a0 = a[0], a1 = a[1], a2 = a[2], b0 = b[0], b1 = b[1], b2 = b[2];
    const float T0 = T[0], T1 = T[1], T2 = T[2];
    const float R0 = R[0], R1 = R[1], R2 = R[2], R3 = R[3], R4 = R[4], R5 = R[5], R6 = R[6], R7 = R[7], R8 = R[8];
    const float A0 = A[0], A1 = A[1], A2 = A[2], A3 = A[3], A4 = A[4], A5 = A[5], A6 = A[6], A7 = A[7], A8 = A[8];

    // The three axes of the first box.
    if (Abs(T0) > S(S(S(M(a1, A1), M(A0, a0)), M(A2, a2)), b0)) return false;
    if (Abs(T1) > S(S(S(M(a1, A4), M(a0, A3)), M(a2, A5)), b1)) return false;
    if (Abs(T2) > S(S(S(M(a1, A7), M(a0, A6)), M(a2, A8)), b2)) return false;

    // The three axes of the second box (the centre offset through the second box's axes).
    const float P0 = S(S(M(T1, R3), M(T0, R0)), M(R6, T2));
    const float P1 = S(S(M(T1, R4), M(R1, T0)), M(T2, R7));
    const float P2 = S(S(M(T1, R5), M(R2, T0)), M(T2, R8));
    if (Abs(P0) > S(S(S(M(A0, b0), M(b1, A3)), M(A6, b2)), a0)) return false;
    if (Abs(P1) > S(S(S(M(A1, b0), M(A4, b1)), M(A7, b2)), a1)) return false;
    if (Abs(P2) > S(S(S(M(b0, A2), M(b1, A5)), M(b2, A8)), a2)) return false;

    // The nine cross products.
    if (Abs(SubS(M(P2, R1), M(R2, P1))) > S(S(S(M(A1, a2), M(a1, A2)), M(A6, b1)), M(b2, A3))) return false;
    if (Abs(SubS(M(R4, P2), M(R5, P1))) > S(S(S(M(A4, a2), M(a1, A5)), M(A6, b0)), M(A0, b2))) return false;
    if (Abs(SubS(M(R7, P2), M(R8, P1))) > S(S(S(M(a2, A7), M(a1, A8)), M(b0, A3)), M(A0, b1))) return false;
    if (Abs(SubS(M(R2, P0), M(P2, R0))) > S(S(S(M(A0, a2), M(a0, A2)), M(A7, b1)), M(A4, b2))) return false;
    if (Abs(SubS(M(R5, P0), M(P2, R3))) > S(S(S(M(a0, A5), M(a2, A3)), M(A7, b0)), M(A1, b2))) return false;
    if (Abs(SubS(M(R8, P0), M(R6, P2))) > S(S(S(M(a0, A8), M(a2, A6)), M(A4, b0)), M(b1, A1))) return false;
    if (Abs(SubS(M(P1, R0), M(R1, P0))) > S(S(S(M(a0, A1), M(a1, A0)), M(b1, A8)), M(b2, A5))) return false;
    if (Abs(SubS(M(P1, R3), M(R4, P0))) > S(S(S(M(a1, A3), M(a0, A4)), M(b0, A8)), M(b2, A2))) return false;
    return !(Abs(SubS(M(R6, P1), M(R7, P0))) > S(S(S(M(a1, A6), M(a0, A7)), M(b0, A5)), M(b1, A2)));
}

bool __cdecl CuboidsOverlap(const Cuboid *halfA, const AffineTransform3 *frameA, const Cuboid *halfB,
                            const AffineTransform3 *frameB)
{
    float a[3] = {halfA->half.x, halfA->half.y, halfA->half.z};
    float b[3] = {halfB->half.x, halfB->half.y, halfB->half.z};
    float MA[9];
    std::memcpy(MA, frameA->mMatrix, sizeof(MA));
    float TB[9];
    TransposeMatrix(TB, frameB->mMatrix);

    if (frameA->mUnknown0 & 4) {
        Vec3 s;
        static_cast<const ConvexFrame *>(frameA)->AxisScales(&s);
        a[0] = MulS(a[0], s.x);
        a[1] = MulS(s.y, a[1]);
        a[2] = MulS(s.z, a[2]);
        const float inverse[3] = {1.0f / s.x, 1.0f / s.y, 1.0f / s.z};
        for (int i = 0; i < 9; ++i) MA[i] = MulS(MA[i], inverse[i % 3]); // column i % 3
    }
    if (frameB->mUnknown0 & 4) {
        Vec3 s;
        static_cast<const ConvexFrame *>(frameB)->AxisScales(&s);
        b[0] = MulS(b[0], s.x);
        b[1] = MulS(s.y, b[1]);
        b[2] = MulS(s.z, b[2]);
        const float inverse[3] = {1.0f / s.x, 1.0f / s.y, 1.0f / s.z};
        for (int i = 0; i < 9; ++i) TB[i] = MulS(TB[i], inverse[i / 3]); // row i / 3 of the transpose
    }
    const Vec3 d = {SubS(frameA->mOffset[0], frameB->mOffset[0]), SubS(frameA->mOffset[1], frameB->mOffset[1]),
                    SubS(frameA->mOffset[2], frameB->mOffset[2])};
    float T[3];
    for (int i = 0; i < 3; ++i) T[i] = AddS(AddS(MulS(TB[3 * i], d.x), MulS(TB[3 * i + 1], d.y)), MulS(TB[3 * i + 2], d.z));
    float R[9];
    MatrixProduct(R, MA, TB);
    return BoxesMeetOnAxes15(a, b, R, T);
}

} // namespace nGZCompGeom
