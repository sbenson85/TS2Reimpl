#include "compgeom/Rigid.h"

#include <cmath>
#include <cstring>

#include "compgeom/Frames.h"
#include "misc/Quaternion.h"
#include "misc/X87.h"

// The rigid transform, quaternion helpers and cuboid boxes of src/compgeom/Rigid.h. RVAs 1.18.0.187.
//
// Every scalar operation below is one of the helpers with its operand order spelled out: SSE arithmetic returns the
// first operand's NaN (quieted) when both are NaNs, and the compiler is free to swap the operands of a plain + or *.
namespace nGZCompGeom {

namespace {

float Mul(float a, float b) { return a != a ? QuietNaN(a) : a * b; }
float Add(float a, float b) { return AddInOrder(a, b); }
float Sub(float a, float b) { return a != a ? QuietNaN(a) : a - b; }
float Twice(float p) { return AddInOrder(p, p); } // `addss p, p`
float Negated(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    bits ^= 0x80000000u;
    std::memcpy(&f, &bits, 4);
    return f;
}

// x87 arithmetic on doubles loaded from floats: with two NaNs the one with the larger significand is the result (SSE
// would pick the first). A single NaN, infinities and zeros behave alike in both.
double X87Pick(double a, double b)
{
    uint64_t ba, bb;
    std::memcpy(&ba, &a, 8);
    std::memcpy(&bb, &b, 8);
    const uint64_t sa = ba & 0x000fffffffffffffull, sb = bb & 0x000fffffffffffffull;
    if (sa != sb) return sb > sa ? b : a;
    return (ba >> 63) ? b : a; // equal significands: the positive NaN (found by the differential test)
}
double Mul87(double a, double b) { return (a != a && b != b) ? X87Pick(a, b) : a * b; }
uint32_t Bits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
// fld a; fmul dword [b]: the load quiets a but not b, and a NaN in the register beats a signalling memory operand whatever
// the significands (found by the differential test); otherwise the larger significand wins.
double Mul87(float a, float b)
{
    if (a != a && b != b && !(Bits(b) & 0x00400000u)) return static_cast<double>(a);
    return Mul87(static_cast<double>(a), static_cast<double>(b));
}
double Add87(double a, double b) { return (a != a && b != b) ? X87Pick(a, b) : a + b; }

// The half extents' reach along one axis of a frame: the x87 dot product with the matrix row's absolute values, as a float.
float Reach(const Vec3 &half, const float *row)
{
    const float absolute[3] = {std::fabs(row[0]), std::fabs(row[1]), std::fabs(row[2])};
    return static_cast<float>(Dot87(&half.x, absolute));
}

// The box of half extents `half` carried by the matrix `m` and the centre `centre` (the shared tail of 0x18ff90 / 0x190170).
BoundBox BoxByMatrix(const Vec3 &half, const float *m, const float *centre)
{
    const float r0 = Reach(half, m);
    const float r1 = Reach(half, m + 3);
    const float r2 = Reach(half, m + 6);
    BoundBox box;
    box.max = {Add(centre[0], r0), Add(centre[1], r1), Add(centre[2], r2)};
    box.min = {Add(Negated(r0), centre[0]), Add(Negated(r1), centre[1]), Add(Negated(r2), centre[2])};
    return box;
}

} // namespace

double __cdecl Dot87(const float *a, const float *b)
{
    const double t1 = Mul87(a[1], b[1]);
    const double t0 = Mul87(a[0], b[0]);
    const double t2 = Mul87(a[2], b[2]);
    if (!x87::SinglePrecision()) return Add87(Add87(t1, t0), t2); // 53-bit: the products are exact (src/misc/X87.h)
    using x87::Round24;
    return Round24(Add87(Round24(Add87(Round24(t1), Round24(t0))), Round24(t2)));
}

float *__cdecl QuaternionToMatrix(float *out, const float *q)
{
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float zx = Twice(Mul(z, x));
    const float xx = Twice(Mul(x, x));
    const float yx = Twice(Mul(y, x));
    const float wx = Twice(Mul(w, x));
    const float yy = Twice(Mul(y, y));
    const float zy = Twice(Mul(z, y));
    const float wy = Twice(Mul(w, y));
    const float wz = Twice(Mul(w, z));
    const float zz = Twice(Mul(z, z));
    const float one = 1.0f;
    const float rest = Sub(one, xx);
    out[0] = Sub(Sub(one, yy), zz);
    out[1] = Sub(yx, wz);
    out[2] = Add(wy, zx);
    out[3] = Add(wz, yx);
    out[4] = Sub(rest, zz);
    out[5] = Sub(zy, wx);
    out[6] = Sub(zx, wy);
    out[7] = Add(zy, wx);
    out[8] = Sub(rest, yy);
    return out;
}

float *__cdecl RotateByInverseQuaternion(float *v, const float *q)
{
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float vx = v[0], vy = v[1], vz = v[2];
    const float xx = Twice(Mul(x, x));
    const float wy = Twice(Mul(w, y));
    const float yx = Twice(Mul(y, x));
    const float zx = Twice(Mul(z, x));
    const float wx = Twice(Mul(w, x));
    const float yy = Twice(Mul(y, y));
    const float wz = Twice(Mul(w, z));
    const float zz = Twice(Mul(z, z));
    const float zy = Twice(Mul(z, y));
    const float one = 1.0f;
    const float rest = Sub(one, xx);
    const float nx = Add(Add(Mul(Sub(Sub(one, yy), zz), vx), Mul(Add(wz, yx), vy)), Mul(Sub(zx, wy), vz));
    const float ny = Add(Add(Mul(Sub(rest, zz), vy), Mul(Sub(yx, wz), vx)), Mul(Add(zy, wx), vz));
    const float nz = Add(Add(Mul(Sub(zy, wx), vy), Mul(Add(wy, zx), vx)), Mul(Sub(rest, yy), vz));
    v[0] = nx;
    v[1] = ny;
    v[2] = nz;
    return v;
}

// 0x1a6c40: v turned by the quaternion, in place. simgeom::RotateByQuaternion is the same arithmetic without the
// operand order, which only shows when two NaNs meet; the rigid transform's callers can pass anything.
void RotateByQuaternionOrdered(float *v, const float *q)
{
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float v0 = v[0], v1 = v[1], v2 = v[2];
    const float xx2 = Twice(Mul(x, x));
    const float wy2 = Twice(Mul(w, y));
    const float xy2 = Twice(Mul(y, x));
    const float xz2 = Twice(Mul(z, x));
    const float wx2 = Twice(Mul(w, x));
    const float yy2 = Twice(Mul(y, y));
    const float wz2 = Twice(Mul(w, z));
    const float zz2 = Twice(Mul(z, z));
    const float yz2 = Twice(Mul(z, y));
    const float one = 1.0f;
    const float m00 = Sub(Sub(one, yy2), zz2), m01 = Sub(xy2, wz2), m02 = Add(wy2, xz2);
    const float m10 = Add(wz2, xy2), m11 = Sub(Sub(one, xx2), zz2), m12 = Sub(yz2, wx2);
    const float m20 = Sub(xz2, wy2), m21 = Add(yz2, wx2), m22 = Sub(Sub(one, xx2), yy2);
    v[0] = Add(Add(Mul(m00, v0), Mul(m01, v1)), Mul(m02, v2));
    v[1] = Add(Add(Mul(m11, v1), Mul(m10, v0)), Mul(m12, v2));
    v[2] = Add(Add(Mul(m20, v0), Mul(m21, v1)), Mul(m22, v2));
}

void RigidTransform3::TransformPoint(Vec3 *p) const
{
    Vec3 turned = *p;
    RotateByQuaternionOrdered(&turned.x, rotation);
    p->x = Add(turned.x, translation.x);
    p->y = Add(turned.y, translation.y);
    p->z = Add(turned.z, translation.z);
}

void RigidTransform3::TransformPlane(float *plane) const
{
    RotateByQuaternionOrdered(plane, rotation);
    const float dot = Add(Add(Mul(plane[1], translation.y), Mul(plane[0], translation.x)), Mul(plane[2], translation.z));
    plane[3] = Sub(plane[3], dot);
}

void RigidTransform3::InverseTransformPoint(Vec3 *p) const
{
    Vec3 turned = *p;
    RotateByInverseQuaternion(&turned.x, rotation);
    Vec3 shift = translation;
    RotateByInverseQuaternion(&shift.x, rotation);
    p->x = Sub(turned.x, shift.x);
    p->y = Sub(turned.y, shift.y);
    p->z = Sub(turned.z, shift.z);
}

Vec3 *RigidTransform3::InverseTransformVector(Vec3 *v) const
{
    RotateByInverseQuaternion(&v->x, rotation);
    return v;
}

float *RigidTransform3::ToMatrix(float *out) const
{
    QuaternionToMatrix(out, rotation);
    return out;
}

RigidTransform3 *RigidTransform3::Compose(const RigidTransform3 *rhs)
{
    reinterpret_cast<math::Quaternion *>(rotation)->PreMultiplyNormalised(reinterpret_cast<const math::Quaternion *>(rhs->rotation));
    Vec3 moved = translation;
    rhs->TransformPoint(&moved);
    translation = moved;
    return this;
}

RigidTransform3 *RigidTransform3::Invert()
{
    // 0x19cd80: the conjugate quaternion (sign flips keep every payload), then the translation turned by it and negated.
    rotation[0] = Negated(rotation[0]);
    rotation[1] = Negated(rotation[1]);
    rotation[2] = Negated(rotation[2]);
    RotateByQuaternionOrdered(&translation.x, rotation);
    translation.x = Negated(translation.x);
    translation.y = Negated(translation.y);
    translation.z = Negated(translation.z);
    return this;
}

void FrameMath::SetRotation(const float *quaternion)
{
    mUnknown0 = (mUnknown0 & 1) | 2;
    float m[9];
    QuaternionToMatrix(m, quaternion);
    std::memcpy(mMatrix, m, sizeof(m));
}

Vec3 *BoundBox::Corner(Vec3 *out, uint32_t index) const
{
    const float z = (index & 4) ? max.z : min.z;
    const float y = (index & 2) ? max.y : min.y;
    out->x = (index & 1) ? max.x : min.x;
    out->y = y;
    out->z = z;
    return out;
}

BoundBox *BoundBox::Transform(const AffineTransform3 *frameIn)
{
    AffineTransform3 &frame = const_cast<AffineTransform3 &>(*frameIn);
    if (!(frame.mUnknown0 & 0x18)) {
        const float hx = Sub(max.x, min.x) * 0.5f;
        const float hy = Sub(max.y, min.y) * 0.5f;
        const float hz = Sub(max.z, min.z) * 0.5f;
        const Vec3 half = {hx, hy, hz};
        const Vec3 centre = {Add(min.x, hx), Add(hy, min.y), Add(hz, min.z)};
        Vec3 moved;
        frame.TransformPoint(&moved.x, &centre.x);
        const float *m = frame.mMatrix;
        const float r0 = Reach(half, m);
        const float r1 = Reach(half, m + 3);
        const float r2 = Reach(half, m + 6);
        const BoundBox result = {{Sub(moved.x, r0), Sub(moved.y, r1), Sub(moved.z, r2)},
                                 {Add(moved.x, r0), Add(moved.y, r1), Add(moved.z, r2)}};
        *this = result;
        return this;
    }
    Vec3 low, high;
    frame.TransformPoint(&low.x, &min.x);
    high = low;
    for (uint32_t i = 1; i < 8; ++i) {
        Vec3 corner, p;
        Corner(&corner, i);
        frame.TransformPoint(&p.x, &corner.x);
        // minss / maxss: the second operand (the running value) when the compare fails or a NaN is involved.
        low.x = p.x < low.x ? p.x : low.x;
        low.y = p.y < low.y ? p.y : low.y;
        low.z = p.z < low.z ? p.z : low.z;
        high.x = p.x > high.x ? p.x : high.x;
        high.y = p.y > high.y ? p.y : high.y;
        high.z = p.z > high.z ? p.z : high.z;
    }
    min = low;
    max = high;
    return this;
}

BoundBox Cuboid::BoxInFrame(const AffineTransform3 &frame) const
{
    if (!(frame.mUnknown0 & 0x18)) return BoxByMatrix(half, frame.mMatrix, frame.mOffset);
    BoundBox box;
    box.min = {Negated(half.x), Negated(half.y), Negated(half.z)};
    box.max = half;
    return *box.Transform(&frame);
}

BoundBox Cuboid::BoxInRigid(const RigidTransform3 &rigid) const
{
    float m[9];
    rigid.ToMatrix(m);
    return BoxByMatrix(half, m, &rigid.translation.x);
}

bool __cdecl RigidNearlyEqual(const RigidTransform3 *a, const RigidTransform3 *b)
{
    constexpr float kTolerance = 1e-4f;
    const float dx = a->translation.x - b->translation.x;
    const float dy = a->translation.y - b->translation.y;
    const float dz = a->translation.z - b->translation.z;
    const float distance = (dx * dx + dy * dy) + dz * dz;
    if (!(kTolerance > distance)) return false;
    float dot = b->rotation[1] * a->rotation[1] + b->rotation[0] * a->rotation[0];
    dot = dot + b->rotation[2] * a->rotation[2];
    dot = dot + b->rotation[3] * a->rotation[3];
    return kTolerance > std::fabs(std::fabs(dot) - 1.0f);
}

} // namespace nGZCompGeom
