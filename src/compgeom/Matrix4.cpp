#include "compgeom/Matrix4.h"

#include <cmath>
#include <cstring>

#include "compgeom/Frames.h"
#include "compgeom/Rigid.h"
#include "compgeom/SeparatingAxes.h"

// The 4x4 matrices of src/compgeom/Matrix4.h and the members that build them. RVAs 1.18.0.187.
//
// Every operation is spelled out with its operand order as the original has it (read off the disassembly): SSE
// arithmetic returns the first operand's NaN (quieted) when both are NaNs, and the original's operand order is not
// always the written one.
namespace nGZCompGeom {

namespace {

float Mul(float a, float b) { return a != a ? QuietNaN(a) : a * b; }
float Add(float a, float b) { return AddInOrder(a, b); }
float Sub(float a, float b) { return a != a ? QuietNaN(a) : a - b; }
float Div(float a, float b) { return a != a ? QuietNaN(a) : a / b; }

// The matrix as dwords (copies keep every bit pattern, signalling NaNs included).
uint32_t *Dwords(float *m) { return reinterpret_cast<uint32_t *>(m); }
const uint32_t *Dwords(const float *m) { return reinterpret_cast<const uint32_t *>(m); }

constexpr uint32_t kOne = 0x3f800000;

// The identity, written as the original does: zeros, then the diagonal.
void ZeroWithDiagonal(uint32_t *out)
{
    for (int i = 0; i < 16; ++i) out[i] = 0;
    out[0] = out[5] = out[10] = out[15] = kOne;
}

// One element of the product: the four terms (a[4k+j] and b[4i+k], k in the order given) summed left to right. A bit in
// `aFirst` says that term's multiply has a's element as its first operand.
struct ProductRow {
    uint8_t k[4];
    uint8_t aFirst;
};
// Indexed by the output element 4i+j; read off the disassembly of 0x1a5eb0 by tools (the only irregularities are the
// swapped first two terms of elements 1, 3, 11 and 14, and the operand orders).
constexpr ProductRow kProduct[16] = {
    {{0, 1, 2, 3}, 0x5}, {{1, 0, 2, 3}, 0xb}, {{0, 1, 2, 3}, 0xf}, {{1, 0, 2, 3}, 0x0},
    {{0, 1, 2, 3}, 0x1}, {{0, 1, 2, 3}, 0x6}, {{0, 1, 2, 3}, 0xa}, {{0, 1, 2, 3}, 0x2},
    {{0, 1, 2, 3}, 0x0}, {{0, 1, 2, 3}, 0x7}, {{0, 1, 2, 3}, 0xc}, {{1, 0, 2, 3}, 0x4},
    {{0, 1, 2, 3}, 0x9}, {{0, 1, 2, 3}, 0x4}, {{1, 0, 2, 3}, 0x8}, {{0, 1, 2, 3}, 0x0},
};

float ProductElement(const float *a, const float *b, int element)
{
    const int i = element >> 2, j = element & 3;
    const ProductRow &row = kProduct[element];
    float sum = 0.0f;
    for (int n = 0; n < 4; ++n) {
        const float x = a[4 * row.k[n] + j], y = b[4 * i + row.k[n]];
        const float term = (row.aFirst >> n) & 1 ? Mul(x, y) : Mul(y, x);
        sum = n == 0 ? term : Add(sum, term);
    }
    return sum;
}

} // namespace

float *__cdecl Matrix4Multiply(float *out, const float *a, const float *b)
{
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i) out[4 * i + j] = ProductElement(a, b, 4 * i + j);
    return out;
}

LuDecomposition4 *LuDecomposition4::Decompose(const float *in)
{
    std::memcpy(mLu, in, sizeof(mLu));
    mDeterminant = 1.0f;
    for (int i = 0; i < 4; ++i) mRow[i] = i;
    float value = 1.0f;
    for (int k = 1, base = 0; k < 4; ++k, base += 4) {
        int32_t &pivot = mRow[k - 1];
        for (int j = k; j < 4; ++j) {
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
        value = Mul(mLu[pivot + base], mDeterminant);
        mDeterminant = value;
        if (value == 0.0f) break;
        for (int j = k; j < 4; ++j) {
            const int32_t row = mRow[j];
            mLu[row + base] = Div(mLu[row + base], mLu[pivot + base]);
            for (int at = base + 4; at < 16; at += 4)
                mLu[row + at] = Sub(mLu[row + at], Mul(mLu[pivot + at], mLu[row + base]));
        }
    }
    mDeterminant = Mul(value, mLu[mRow[3] + 12]);
    return this;
}

float *LuDecomposition4::Solve(float *out, const float *b) const
{
    float y[4];
    y[0] = b[mRow[0]];
    for (int i = 1; i < 4; ++i) {
        float sum = 0.0f;
        for (int c = 0; c < i; ++c) sum = Add(sum, Mul(y[c], mLu[mRow[i] + 4 * c]));
        y[i] = Sub(b[mRow[i]], sum);
    }
    out[3] = Div(y[3], mLu[mRow[3] + 12]);
    for (int r = 2; r >= 0; --r) {
        float sum = 0.0f;
        for (int c = r + 1; c < 4; ++c) sum = Add(sum, Mul(out[c], mLu[mRow[r] + 4 * c]));
        out[r] = Div(Sub(y[r], sum), mLu[mRow[r] + 4 * r]);
    }
    return out;
}

float *__cdecl Matrix4Inverse(float *out, const float *m)
{
    LuDecomposition4 lu;
    lu.Decompose(m);
    if (lu.mDeterminant == 0.0f) {
        std::memset(out, 0, 16 * sizeof(float));
        return out;
    }
    float rows[16];
    ZeroWithDiagonal(Dwords(rows));
    for (int i = 0; i < 4; ++i) {
        const float b[4] = {rows[4 * i], rows[4 * i + 1], rows[4 * i + 2], rows[4 * i + 3]};
        lu.Solve(&rows[4 * i], b);
    }
    std::memcpy(out, rows, sizeof(rows));
    return out;
}

float *__cdecl Matrix4Translation(float *out, const float *translation)
{
    uint32_t *o = Dwords(out);
    const uint32_t *t = Dwords(translation);
    ZeroWithDiagonal(o);
    o[3] = t[0];
    o[7] = t[1];
    o[11] = t[2];
    return out;
}

void __cdecl Matrix4FromRotationScaleTranslation(const float *rotation, const float *scale, const float *translation,
                                                 float *out)
{
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i) out[4 * i + j] = Mul(scale[j], rotation[3 * i + j]);
    uint32_t *o = Dwords(out);
    const uint32_t *t = Dwords(translation);
    o[3] = t[0];
    o[7] = t[1];
    o[11] = t[2];
    o[12] = o[13] = o[14] = 0;
    o[15] = kOne;
}

float *FrameMath::ToMatrix4(float *out) const
{
    uint32_t *o = Dwords(out);
    const uint32_t *m = Dwords(mMatrix);
    const uint32_t *offset = Dwords(mOffset);
    ZeroWithDiagonal(o);
    o[0] = m[0];
    o[1] = m[1];
    o[2] = m[2];
    o[4] = m[3];
    o[5] = m[4];
    o[6] = m[5];
    o[8] = m[6];
    o[9] = m[7];
    o[10] = m[8];
    o[3] = offset[0];
    o[7] = offset[1];
    o[11] = offset[2];
    return out;
}

float *RigidTransform3::ToMatrix4(float *out) const
{
    Matrix4Translation(out, &translation.x);
    float rotationMatrix[9];
    QuaternionToMatrix(rotationMatrix, rotation);
    const uint32_t *r = Dwords(rotationMatrix);
    uint32_t *o = Dwords(out);
    o[0] = r[0];
    o[1] = r[1];
    o[2] = r[2];
    o[4] = r[3];
    o[5] = r[4];
    o[6] = r[5];
    o[8] = r[6];
    o[9] = r[7];
    o[10] = r[8];
    return out;
}

float *__cdecl EulerAnglesToMatrix3(float *out, float x, float y, float z)
{
    const float cx = static_cast<float>(std::cos(static_cast<double>(x)));
    const float sx = static_cast<float>(std::sin(static_cast<double>(x)));
    const float cy = static_cast<float>(std::cos(static_cast<double>(y)));
    const float sy = static_cast<float>(std::sin(static_cast<double>(y)));
    const float cz = static_cast<float>(std::cos(static_cast<double>(z)));
    const float sz = static_cast<float>(std::sin(static_cast<double>(z)));
    const float syx = Mul(sy, sx);
    const float syCx = Mul(sy, cx);
    // The zero words first (the nine stores below overwrite them all).
    out[4] = out[5] = out[7] = out[8] = 0.0f;
    out[0] = Mul(cz, cy);
    out[3] = Mul(sz, cy);
    uint32_t negSy;
    std::memcpy(&negSy, &sy, 4);
    negSy ^= 0x80000000u;
    std::memcpy(&out[6], &negSy, 4);
    out[1] = Sub(Mul(syx, cz), Mul(sz, cx));
    out[4] = Add(Mul(syx, sz), Mul(cz, cx));
    out[7] = Mul(cy, sx);
    out[8] = Mul(cy, cx);
    out[2] = Add(Mul(syCx, cz), Mul(sz, sx));
    out[5] = Sub(Mul(syCx, sz), Mul(cz, sx));
    return out;
}

float *RigidTransform3::RotateVector(float *v) const
{
    RotateByQuaternionOrdered(v, rotation);
    return v;
}

FramePlacement *FramePlacement::ComposeRigid(const RigidTransform3 *rigid)
{
    mUnknown0 |= 3;
    float t[9];
    rigid->ToMatrix(t);
    const float *m = mMatrix;
    float n[9];
    n[0] = Add(Add(Mul(t[0], m[0]), Mul(t[1], m[3])), Mul(t[2], m[6]));
    n[3] = Add(Add(Mul(t[4], m[3]), Mul(t[3], m[0])), Mul(t[5], m[6]));
    n[6] = Add(Add(Mul(t[7], m[3]), Mul(t[6], m[0])), Mul(t[8], m[6]));
    n[1] = Add(Add(Mul(t[1], m[4]), Mul(t[0], m[1])), Mul(t[2], m[7]));
    n[4] = Add(Add(Mul(t[4], m[4]), Mul(m[1], t[3])), Mul(t[5], m[7]));
    n[7] = Add(Add(Mul(m[4], t[7]), Mul(m[1], t[6])), Mul(t[8], m[7]));
    n[2] = Add(Add(Mul(t[1], m[5]), Mul(m[2], t[0])), Mul(t[2], m[8]));
    n[5] = Add(Add(Mul(t[4], m[5]), Mul(t[3], m[2])), Mul(t[5], m[8]));
    n[8] = Add(Add(Mul(t[7], m[5]), Mul(t[6], m[2])), Mul(t[8], m[8]));
    std::memcpy(mMatrix, n, sizeof(n));
    Vec3 moved;
    std::memcpy(&moved, mOffset, sizeof(moved));
    rigid->TransformPoint(&moved);
    std::memcpy(mOffset, &moved, sizeof(moved));
    return this;
}

} // namespace nGZCompGeom
