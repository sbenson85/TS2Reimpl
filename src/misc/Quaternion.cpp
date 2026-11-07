#include "misc/Quaternion.h"

#include <cmath>
#include "compgeom/Frames.h"
#include "compgeom/Rigid.h" // Dot87

namespace math {

namespace {
// The scalar operations with their operand order spelled out: when both operands are NaNs, SSE returns the first one's
// (quieted), and the compiler may swap the operands of a plain + or * (lane PH: the rigid transform's composition shows it).
float Mul(float a, float b) { return a != a ? nGZCompGeom::QuietNaN(a) : a * b; }
float Add(float a, float b) { return nGZCompGeom::AddInOrder(a, b); }
float Sub(float a, float b) { return a != a ? nGZCompGeom::QuietNaN(a) : a - b; }
float Div(float a, float b) { return a != a ? nGZCompGeom::QuietNaN(a) : a / b; }
float RootF(float a) { return static_cast<float>(std::sqrt(static_cast<double>(a))); }

constexpr float kHalfVectorEpsilon = 1.1920929e-07f; // 2^-23
} // namespace

Quaternion *Quaternion::SetRotationBetween(const float *from, const float *to)
{
    const float inverse =
        1.0f / RootF(Add(Add(Mul(from[0], from[0]), Mul(from[1], from[1])), Mul(from[2], from[2])));
    const float n[3] = {Mul(from[0], inverse), Mul(from[1], inverse), Mul(from[2], inverse)};
    float h[3] = {Add(from[0], to[0]), Add(from[1], to[1]), Add(from[2], to[2])};
    const float lengthSquared = Add(Add(Mul(h[0], h[0]), Mul(h[1], h[1])), Mul(h[2], h[2]));
    if (lengthSquared > kHalfVectorEpsilon) {
        const float length = RootF(lengthSquared);
        h[0] = Div(h[0], length);
        h[1] = Div(h[1], length);
        h[2] = Div(h[2], length);
        x = Sub(Mul(n[1], h[2]), Mul(h[1], n[2]));
        w = static_cast<float>(nGZCompGeom::Dot87(n, h));
        y = Sub(Mul(h[0], n[2]), Mul(n[0], h[2]));
        z = Sub(Mul(n[0], h[1]), Mul(n[1], h[0]));
        return this;
    }
    const float negated = -n[0];
    w = 0.0f;
    const float squareX = Mul(n[0], n[0]);
    const float xy = Add(Mul(n[1], n[1]), squareX);
    const float xz = Add(Mul(n[2], n[2]), squareX);
    if (xy > xz) {
        x = n[1];
        y = negated;
        z = 0.0f;
    } else {
        x = n[2];
        y = 0.0f;
        z = negated;
    }
    return this;
}

Quaternion *Quaternion::PreMultiplyNormalised(const Quaternion *rhs)
{
    const float ax = x, ay = y, az = z, aw = w;
    const float bx = rhs->x, by = rhs->y, bz = rhs->z, bw = rhs->w;
    x = Sub(Add(Add(Mul(bx, aw), Mul(bw, ax)), Mul(by, az)), Mul(bz, ay));
    y = Sub(Add(Add(Mul(by, aw), Mul(bw, ay)), Mul(bz, ax)), Mul(bx, az));
    z = Sub(Add(Add(Mul(bz, aw), Mul(bw, az)), Mul(bx, ay)), Mul(by, ax));
    w = Sub(Sub(Sub(Mul(bw, aw), Mul(bx, ax)), Mul(by, ay)), Mul(bz, az));
    const float sum = Add(Add(Add(Mul(x, x), Mul(y, y)), Mul(z, z)), Mul(w, w));
    const float inverse = 1.0f / static_cast<float>(std::sqrt(static_cast<double>(sum)));
    // The four products in one mulps (the scale is the first operand), as the original's.
    x = Mul(inverse, x);
    y = Mul(inverse, y);
    z = Mul(inverse, z);
    w = Mul(inverse, w);
    return this;
}

Quaternion Quaternion::FromAxisHalfAngle(const float axis[3], double halfAngle)
{
    const float inverseLength =
        1.0f / static_cast<float>(std::sqrt(static_cast<double>(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2])));
    const float s = static_cast<float>(std::sin(halfAngle));
    Quaternion q;
    q.z = axis[2] * inverseLength * s;
    q.x = axis[0] * inverseLength * s;
    q.y = axis[1] * inverseLength * s;
    q.w = static_cast<float>(std::cos(halfAngle));
    return q;
}

} // namespace math
