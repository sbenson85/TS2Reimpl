#include "compgeom/Frames.h"

#include <cmath>
#include <cstring>

// nGZNumerics' frame members (src/compgeom/Frames.h). RVAs 1.18.0.187.
//
// NaNs: when both operands of an SSE add or multiply are NaNs, the result is the first operand's (quieted). The compiler
// may swap the operands of + and *, so each row is computed plainly and, only when it comes out NaN, again with every
// operation's operand order spelled out (Ordered): the same bits as the original's for any inputs.
namespace nGZCompGeom {

namespace {

constexpr uint32_t kLinear = 0x1e;      // any bit: the matrix is not the identity
constexpr uint32_t kNotRotation = 0x14; // a uniform scale or a general matrix: the transpose is not the inverse
constexpr uint32_t kUniformScale = 4;
constexpr uint32_t kScaleAndMore = 8;   // with kUniformScale: inverted as a general matrix
constexpr uint32_t kGeneral = 0x10;

// The matrix as dwords (copies keep every bit pattern, signalling NaNs included).
uint32_t *Dwords(float *m)
{
    return reinterpret_cast<uint32_t *>(m);
}
const uint32_t *Dwords(const float *m)
{
    return reinterpret_cast<const uint32_t *>(m);
}

struct Plain {
    static float Mul(float a, float b) { return a * b; }
    static float Add(float a, float b) { return a + b; }
};
// A NaN first operand is the result; otherwise the hardware's (the other operand's NaN, or the default NaN).
struct Ordered {
    static float Mul(float a, float b) { return a != a ? QuietNaN(a) : a * b; }
    static float Add(float a, float b) { return AddInOrder(a, b); }
};

// (a0 b0 + a1 b1) + a2 b2, every product and sum with its operands in the order given.
template <class Op> float Sum3(float a0, float b0, float a1, float b1, float a2, float b2)
{
    return Op::Add(Op::Add(Op::Mul(a0, b0), Op::Mul(a1, b1)), Op::Mul(a2, b2));
}
float Dot(float a0, float b0, float a1, float b1, float a2, float b2)
{
    const float r = Sum3<Plain>(a0, b0, a1, b1, a2, b2);
    return r == r ? r : Sum3<Ordered>(a0, b0, a1, b1, a2, b2);
}
// a - b with the first operand's NaN when both are NaNs. Spelled out too: Dot returns on the x87 stack when not inlined,
// and an x87 subtraction picks the NaN with the larger significand, not the first.
float Difference(float a, float b)
{
    return a != a ? QuietNaN(a) : a - b;
}

} // namespace

float *__cdecl TransposeMatrix(float *out, const float *m)
{
    uint32_t *o = Dwords(out);
    const uint32_t *in = Dwords(m);
    o[0] = in[0];
    o[3] = in[1];
    o[6] = in[2];
    o[1] = in[3];
    o[4] = in[4];
    o[7] = in[5];
    o[2] = in[6];
    o[5] = in[7];
    o[8] = in[8];
    return out;
}

float FrameMath::Scale() const
{
    if (!(mUnknown0 & kUniformScale)) return 1.0f;
    const float *m = mMatrix;
    // Squares: both operands are the same value, so their order can't matter.
    const float squared = Dot(m[0], m[0], m[3], m[3], m[6], m[6]);
    return static_cast<float>(std::sqrt(static_cast<double>(squared)));
}

namespace {
// The scalar operations of the LU code, each with its operand order spelled out: the first operand's NaN wins a NaN pair.
float MulIn(float a, float b) { return a != a ? QuietNaN(a) : a * b; }
float DivIn(float a, float b) { return a != a ? QuietNaN(a) : a / b; }
float SubIn(float a, float b) { return a != a ? QuietNaN(a) : a - b; }
} // namespace

LuDecomposition3 *LuDecomposition3::Decompose(const float *in)
{
    std::memcpy(mLu, in, sizeof(mLu));
    mDeterminant = 1.0f;
    mRow[0] = 0;
    mRow[1] = 1;
    mRow[2] = 2;
    float value = 1.0f;
    for (int k = 1, base = 0; k < 3; ++k, base += 3) {
        int32_t &pivot = mRow[k - 1];
        for (int j = k; j < 3; ++j) {
            const int32_t candidate = mRow[j];
            if (std::fabs(mLu[pivot + base]) < std::fabs(mLu[candidate + base])) {
                mRow[j] = pivot;
                pivot = candidate;
                uint32_t bits;
                std::memcpy(&bits, &mDeterminant, 4);
                bits ^= 0x80000000u;
                std::memcpy(&mDeterminant, &bits, 4);
            }
        }
        value = MulIn(mLu[pivot + base], mDeterminant);
        mDeterminant = value;
        if (value == 0.0f) break;
        for (int j = k; j < 3; ++j) {
            const int32_t row = mRow[j];
            mLu[row + base] = DivIn(mLu[row + base], mLu[pivot + base]);
            for (int at = base + 3; at < 9; at += 3)
                mLu[row + at] = SubIn(mLu[row + at], MulIn(mLu[row + base], mLu[pivot + at]));
        }
    }
    mDeterminant = MulIn(value, mLu[mRow[2] + 6]);
    return this;
}

float *LuDecomposition3::Solve(float *out, const float *b) const
{
    float y[3];
    y[0] = b[mRow[0]];
    for (int i = 1; i < 3; ++i) {
        float sum = 0.0f;
        for (int c = 0; c < i; ++c) sum = AddInOrder(sum, MulIn(y[c], mLu[mRow[i] + 3 * c]));
        y[i] = SubIn(b[mRow[i]], sum);
    }
    out[2] = DivIn(y[2], mLu[mRow[2] + 6]);
    for (int r = 1; r >= 0; --r) {
        float sum = 0.0f;
        for (int c = r + 1; c < 3; ++c) sum = AddInOrder(sum, MulIn(out[c], mLu[mRow[r] + 3 * c]));
        out[r] = DivIn(SubIn(y[r], sum), mLu[mRow[r] + 3 * r]);
    }
    return out;
}

float *__cdecl InverseMatrix3(float *out, const float *m)
{
    LuDecomposition3 lu;
    lu.Decompose(m);
    if (lu.mDeterminant == 0.0f) {
        std::memset(out, 0, 9 * sizeof(float));
        return out;
    }
    float rows[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    for (int i = 0; i < 3; ++i) {
        float b[3] = {rows[3 * i], rows[3 * i + 1], rows[3 * i + 2]};
        lu.Solve(&rows[3 * i], b);
    }
    std::memcpy(out, rows, sizeof(rows));
    return out;
}

float *FrameMath::InverseMatrix(float *out) const
{
    const uint32_t flags = mUnknown0;
    if (!(flags & kLinear)) {
        std::memcpy(out, mMatrix, sizeof(mMatrix));
        return out;
    }
    if (!(flags & kGeneral)) {
        if (!(flags & kUniformScale)) return TransposeMatrix(out, mMatrix);
        if (!(flags & kScaleAndMore)) {
            const float scale = Scale();
            const float r = 1.0f / (scale * scale);
            float t[9];
            TransposeMatrix(t, mMatrix);
            for (unsigned i = 0; i < 9; ++i) t[i] = Ordered::Mul(t[i], r);
            std::memcpy(out, t, sizeof(t));
            return out;
        }
    }
    InverseMatrix3(out, mMatrix);
    return out;
}

Vec3 *FrameMath::VectorTo(Vec3 *out, const Vec3 *in) const
{
    const float *m = mMatrix;
    out->x = Dot(m[1], in->y, m[0], in->x, m[2], in->z);
    out->y = Dot(m[4], in->y, in->x, m[3], m[5], in->z);
    out->z = Dot(m[7], in->y, in->x, m[6], m[8], in->z);
    return out;
}

void FrameMath::InverseVectorTo(Vec3 *out, const Vec3 *in) const
{
    const uint32_t flags = mUnknown0;
    if (!(flags & kLinear)) {
        uint32_t *o = Dwords(&out->x);
        const uint32_t *i = Dwords(&in->x);
        o[0] = i[0];
        o[1] = i[1];
        o[2] = i[2];
        return;
    }
    if (!(flags & kNotRotation)) {
        const float *m = mMatrix;
        out->x = Dot(m[3], in->y, m[0], in->x, m[6], in->z);
        out->y = Dot(m[4], in->y, m[1], in->x, m[7], in->z);
        out->z = Dot(m[5], in->y, m[2], in->x, m[8], in->z);
        return;
    }
    float inv[9];
    InverseMatrix(inv);
    out->x = Dot(inv[1], in->y, inv[0], in->x, inv[2], in->z);
    out->y = Dot(inv[4], in->y, in->x, inv[3], inv[5], in->z);
    out->z = Dot(inv[7], in->y, inv[6], in->x, inv[8], in->z);
}

Vec3 *FrameMath::InversePointTo(Vec3 *out, const Vec3 *in) const
{
    const uint32_t flags = mUnknown0;
    const float *o = mOffset;
    if (!(flags & kLinear)) {
        out->x = Difference(in->x, o[0]);
        out->y = Difference(in->y, o[1]);
        out->z = Difference(in->z, o[2]);
        return out;
    }
    const float x = in->x, y = in->y, z = in->z;
    if (!(flags & kNotRotation)) {
        const float *m = mMatrix;
        out->x = Difference(Dot(y, m[3], x, m[0], z, m[6]), Dot(o[0], m[0], o[1], m[3], o[2], m[6]));
        out->y = Difference(Dot(m[4], y, x, m[1], m[7], z), Dot(o[0], m[1], o[1], m[4], o[2], m[7]));
        out->z = Difference(Dot(m[5], y, x, m[2], m[8], z), Dot(o[0], m[2], o[1], m[5], o[2], m[8]));
        return out;
    }
    float inv[9];
    InverseMatrix(inv);
    out->x = Difference(Dot(x, inv[0], inv[1], y, inv[2], z), Dot(o[0], inv[0], o[1], inv[1], o[2], inv[2]));
    out->y = Difference(Dot(y, inv[4], x, inv[3], z, inv[5]), Dot(o[0], inv[3], o[1], inv[4], o[2], inv[5]));
    out->z = Difference(Dot(y, inv[7], x, inv[6], z, inv[8]), Dot(o[0], inv[6], o[1], inv[7], o[2], inv[8]));
    return out;
}

} // namespace nGZCompGeom
