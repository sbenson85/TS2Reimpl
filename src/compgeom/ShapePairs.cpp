#include "compgeom/ShapePairs.h"

#include <cmath>
#include <type_traits>
#include <cstring>

#include "compgeom/Frames.h"
#include "compgeom/Rigid.h"
#include "misc/X87.h"

// The line, ray and segment tests against the convex shapes (src/compgeom/ShapePairs.h). RVAs 1.18.0.187.
//
// Every scalar operation spells out its operand order: SSE returns the first operand's NaN (quieted) when both are NaNs
// and the compiler may swap the operands of a plain + or *; comparisons are written as the C++ ordered comparisons the
// original's comiss/jcc pairs compute (a NaN fails `a > b` and `a >= b`; `jb`, `jbe` and the like take the NaN branch).
namespace nGZCompGeom {

namespace {

float MulS(float a, float b) { return a != a ? QuietNaN(a) : a * b; } // mulss a, b
float AddS(float a, float b) { return AddInOrder(a, b); }              // addss a, b
float Negated(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    bits ^= 0x80000000u;
    std::memcpy(&f, &bits, 4);
    return f;
}
float Absolute(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    bits &= 0x7fffffffu;
    std::memcpy(&f, &bits, 4);
    return f;
}
float FloatOf(uint32_t bits)
{
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
uint32_t Bits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
// maxss a, b / minss a, b: b when either is a NaN or they are equal.
float MaxS(float a, float b) { return a > b ? a : b; }
float MinS(float a, float b) { return a < b ? a : b; }

const float kParallel = FloatOf(0x2f03126f); // 1.19209290e-10: a direction component this small is parallel to the slab

// x87 arithmetic on two NaNs picks the larger significand where SSE picks the first (found by lane PH's differential test:
// equal significands take the positive one, a signalling memory operand loses to a NaN in the register).
double X87Pick(double a, double b)
{
    uint64_t ba, bb;
    std::memcpy(&ba, &a, 8);
    std::memcpy(&bb, &b, 8);
    const uint64_t sa = ba & 0x000fffffffffffffull, sb = bb & 0x000fffffffffffffull;
    if (sa != sb) return sb > sa ? b : a;
    return (ba >> 63) ? b : a;
}
// fdivr dword [mem] with `reg` in st0: mem / reg.
double DivMemByReg87(float mem, double reg)
{
    // The memory operand went through an fld/fstp pair first (the radius getter), which quiets a signalling NaN.
    if (mem != mem && reg != reg) return X87Pick(reg, static_cast<double>(FloatOf(Bits(mem) | 0x00400000u)));
    return x87::Rounded(static_cast<double>(mem) / reg);
}

// What a core is given: a line's origin and direction, or a segment's start and end.
struct Pair {
    Vec3 a, b;
};
template <class L> Pair PairOf(const L &l)
{
    if constexpr (std::is_same_v<L, Segment3>)
        return {l.start, l.end};
    else
        return {l.origin, l.direction};
}
template <class L> constexpr bool kSegment = std::is_same_v<L, Segment3>;

// ---- placements: the line in the shape's own frame ----

template <class L> Pair LocalOriented(const RigidTransform3 &r, const L &l)
{
    Pair p = PairOf(l);
    r.InverseTransformPoint(&p.a);
    if constexpr (kSegment<L>)
        r.InverseTransformPoint(&p.b);
    else
        r.InverseTransformVector(&p.b);
    return p;
}
template <class L> Pair LocalTranslated(const Vec3 &position, const L &l)
{
    Pair p = PairOf(l);
    p.a = {p.a.x - position.x, p.a.y - position.y, p.a.z - position.z};
    if constexpr (kSegment<L>) p.b = {p.b.x - position.x, p.b.y - position.y, p.b.z - position.z};
    return p;
}
template <class L> Pair LocalInFrame(const AffineTransform3 &f, const L &l)
{
    Pair p = PairOf(l);
    p.a = FrameInversePoint(f, p.a);
    p.b = kSegment<L> ? FrameInversePoint(f, p.b) : FrameInverseVector(f, p.b);
    return p;
}
template <Placement P, class L> Pair Local(const void *placement, const L &l)
{
    if constexpr (P == Placement::Oriented)
        return LocalOriented(*static_cast<const RigidTransform3 *>(placement), l);
    else if constexpr (P == Placement::Translated)
        return LocalTranslated(*static_cast<const Vec3 *>(placement), l);
    else
        return LocalInFrame(*static_cast<const AffineTransform3 *>(placement), l);
}

// ---- the cone ----

// The shared body of 0x1978a0 and 0x197b00 on an origin and a direction. The cone is its radius r (+0) and half height
// h (+4), both read through x87 getters (0x194400 loads r, 0x193ad0 returns h + h): H = float((h + h) * 0.5) and the
// slope k = float(r / (h + h)). The quadratic in the cone's side, with a, b, c the coefficients of A t^2 + 2 B t + C:
// the roots are the span; then the span is cut to the slab -H <= z <= H.
bool ConeSolve(float *t0, float *t1, const float *cone, const Vec3 &o, const Vec3 &d)
{
    const double twoH = x87::Rounded(static_cast<double>(cone[1]) + static_cast<double>(cone[1]));
    const float H = static_cast<float>(twoH * 0.5);
    const float k = static_cast<float>(DivMemByReg87(cone[0], twoH));

    const float ozH = o.z - H;
    const float dzk = MulS(d.z, k);
    const float ozk = MulS(ozH, k);
    const float dx2 = MulS(d.x, d.x);
    const float dy2 = MulS(d.y, d.y);
    const float A = AddS(dy2, dx2) - MulS(dzk, dzk);
    const float cross = MulS(dzk, ozk);
    const float ozk2 = MulS(ozk, ozk);
    const float dyoy = MulS(d.y, o.y);
    const float oy2 = MulS(o.y, o.y);
    const float dxox = MulS(d.x, o.x);
    const float ox2 = MulS(o.x, o.x);
    const float B = AddS(dyoy, dxox) - cross;
    const float C = AddS(oy2, ox2) - ozk2;
    const float disc = MulS(B, B) - MulS(C, A);
    if (0.0f > disc) return false;

    const float negB = Negated(B);
    if (disc > 0.0f) {
        const float s = static_cast<float>(x87::Rounded(std::sqrt(static_cast<double>(disc))));
        *t0 = (negB - s) / A;
        *t1 = (s - B) / A;
        if (0.0f > A) {
            const float first = *t0;
            *t0 = *t1;
            *t1 = first;
        }
    } else {
        const float root = negB / A;
        *t0 = root;
        *t1 = root;
    }

    // The slab: -H <= z <= H. A direction with no z (parallel) must start inside it.
    const float latest = *t1;
    float lo = Negated(H) - o.z;
    float hi = H - o.z;
    const float dz = d.z;
    const float absDz = Absolute(dz);
    if (!(kParallel < absDz || absDz != absDz)) {
        if (lo > 0.0f) return false;
        if (0.0f > hi) return false;
    }
    lo = lo / dz;
    hi = hi / dz;
    if (lo > hi) {
        const float swap = lo;
        lo = hi;
        hi = swap;
    }
    if (dz > 0.0f) {
        if (lo > *t0) {
            *t0 = latest;
            *t1 = hi;
        }
    } else if (latest > hi) {
        *t1 = *t0;
        *t0 = lo;
    }
    *t0 = MaxS(lo, *t0);
    *t1 = MinS(hi, *t1);
    return *t1 > *t0;
}


// ---- the sphere ----

// 0x198cf0 / 0x198e50 on an origin and a direction: the quadratic a t^2 + 2 b t + c with a = |d|^2, b = d.o (an x87 dot
// product, narrowed), c = |o|^2 - r^2. The line sums a as ((dy2 + dx2) + dz2), the segment as ((dx2 + dy2) + dz2).
template <bool Segment> bool SphereSolve(float *t0, float *t1, const float *sphere, const Vec3 &o, const Vec3 &d)
{
    const float dx2 = MulS(d.x, d.x), dy2 = MulS(d.y, d.y), dz2 = MulS(d.z, d.z);
    const float a = Segment ? AddS(AddS(dx2, dy2), dz2) : AddS(AddS(dy2, dx2), dz2);
    const float b = static_cast<float>(Dot87(&d.x, &o.x));
    const float r2 = MulS(sphere[0], sphere[0]);
    const float c = AddS(AddS(MulS(o.y, o.y), MulS(o.x, o.x)), MulS(o.z, o.z)) - r2;
    const float disc = MulS(b, b) - MulS(c, a);
    if (0.0f > disc) return false;
    if (disc > 0.0f) {
        const float s = static_cast<float>(x87::Rounded(std::sqrt(static_cast<double>(disc))));
        *t0 = (Negated(b) - s) / a;
        *t1 = (s - b) / a;
    } else {
        const float root = Negated(b) / a;
        *t0 = root;
        *t1 = root;
    }
    return true;
}

// ---- the cuboid ----

// 0x1999e0 / 0x199b90 on an origin and a direction: slab by slab (x, y, z) against the half extents h. A direction
// component that is not parallel to its slab (kParallel < |d|, or a NaN) gives the entry and exit parameters; the first such
// slab sets the span, the next ones narrow it, and an empty span fails. A parallel one fails when the origin is outside.
bool CuboidSolve(float *t0, float *t1, const float *h, const Vec3 &o, const Vec3 &d)
{
    const float os[3] = {o.x, o.y, o.z}, ds[3] = {d.x, d.y, d.z};
    bool have = false;
    for (int i = 0; i < 3; ++i) {
        float lo = Negated(h[i]) - os[i];
        float hi = h[i] - os[i];
        const float da = ds[i];
        const float magnitude = Absolute(da);
        if (kParallel < magnitude || magnitude != magnitude) {
            lo = lo / da;
            hi = hi / da;
            if (i == 0) {
                *t0 = lo;
                *t1 = hi;
                have = true;
                if (*t0 > *t1) {
                    *t0 = hi;
                    *t1 = lo;
                }
            } else {
                if (lo > hi) {
                    const float swap = lo;
                    lo = hi;
                    hi = swap;
                }
                if (have) {
                    *t0 = MaxS(lo, *t0);
                    hi = MinS(hi, *t1);
                    *t1 = hi;
                } else {
                    have = true;
                    *t0 = lo;
                    *t1 = hi;
                }
                if (*t0 > hi) return false;
            }
        } else {
            if (lo > 0.0f) return false;
            if (0.0f > hi) return false;
        }
    }
    return have;
}

Vec3 Difference(const Vec3 &a, const Vec3 &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

// ---- the cores by shape, on a Pair ----

template <ShapeKind S, class L> bool Span(float *t0, float *t1, const void *shape, const Pair &p)
{
    const float *f = static_cast<const float *>(shape);
    const Vec3 direction = kSegment<L> ? Difference(p.b, p.a) : p.b;
    if constexpr (S == ShapeKind::Cone)
        return ConeSolve(t0, t1, f, p.a, direction);
    else if constexpr (S == ShapeKind::Sphere)
        return SphereSolve<kSegment<L>>(t0, t1, f, p.a, direction);
    else
        return CuboidSolve(t0, t1, f, p.a, direction);
}

} // namespace

bool __stdcall ConeLineSpan(float *t0, float *t1, const void *cone, const Line3 *line)
{
    return ConeSolve(t0, t1, static_cast<const float *>(cone), line->origin, line->direction);
}
bool __stdcall ConeSegmentSpan(float *t0, float *t1, const void *cone, const Segment3 *segment)
{
    return Span<ShapeKind::Cone, Segment3>(t0, t1, cone, PairOf(*segment));
}
bool __stdcall SphereLineSpan(float *t0, float *t1, const void *sphere, const Line3 *line)
{
    return Span<ShapeKind::Sphere, Line3>(t0, t1, sphere, PairOf(*line));
}
bool __stdcall SphereSegmentSpan(float *t0, float *t1, const void *sphere, const Segment3 *segment)
{
    return Span<ShapeKind::Sphere, Segment3>(t0, t1, sphere, PairOf(*segment));
}
bool __stdcall CuboidLineSpan(float *t0, float *t1, const void *cuboid, const Line3 *line)
{
    return Span<ShapeKind::Cuboid, Line3>(t0, t1, cuboid, PairOf(*line));
}
bool __stdcall CuboidSegmentSpan(float *t0, float *t1, const void *cuboid, const Segment3 *segment)
{
    return Span<ShapeKind::Cuboid, Segment3>(t0, t1, cuboid, PairOf(*segment));
}

// ---- the point to line / ray / segment distances ----

// v = point - origin, k the foot's parameter along d; the residual is |v - k d|^2. The sums keep each function's own order.
double __cdecl PointLineDistanceSquared(const Vec3 *point, const Line3 *line)
{
    const Vec3 v = Difference(*point, line->origin);
    const Vec3 &d = line->direction;
    const double dot = Dot87(&v.x, &d.x);
    const float dd = AddS(AddS(MulS(d.y, d.y), MulS(d.x, d.x)), MulS(d.z, d.z));
    const double ratio = (dot != dot && dd != dd) ? X87Pick(dot, static_cast<double>(dd)) : x87::Rounded(dot / static_cast<double>(dd));
    const float k = static_cast<float>(ratio);
    const float ex = v.x - MulS(k, d.x), ey = v.y - MulS(k, d.y), ez = v.z - MulS(k, d.z);
    return AddS(AddS(MulS(ey, ey), MulS(ex, ex)), MulS(ez, ez));
}
double __cdecl PointRayDistanceSquared(const Vec3 *point, const Ray3 *ray)
{
    const Vec3 v = Difference(*point, ray->origin);
    const Vec3 &d = ray->direction;
    const float b = static_cast<float>(Dot87(&v.x, &d.x));
    if (0.0f >= b) return AddS(AddS(MulS(v.x, v.x), MulS(v.y, v.y)), MulS(v.z, v.z));
    const float dd = AddS(AddS(MulS(d.y, d.y), MulS(d.x, d.x)), MulS(d.z, d.z));
    const float k = b / dd;
    const float ex = v.x - MulS(k, d.x), ey = v.y - MulS(k, d.y), ez = v.z - MulS(k, d.z);
    return AddS(AddS(MulS(ex, ex), MulS(ey, ey)), MulS(ez, ez));
}
double __cdecl PointSegmentDistanceSquared(const Vec3 *point, const Segment3 *segment)
{
    const Vec3 v = Difference(*point, segment->start);
    const Vec3 d = Difference(segment->end, segment->start);
    const float b = static_cast<float>(Dot87(&v.x, &d.x));
    if (0.0f >= b) return AddS(AddS(MulS(v.y, v.y), MulS(v.x, v.x)), MulS(v.z, v.z));
    const float dd = AddS(AddS(MulS(d.y, d.y), MulS(d.x, d.x)), MulS(d.z, d.z));
    if (b >= dd) {
        const float ex = v.x - d.x, ey = v.y - d.y, ez = v.z - d.z;
        return AddS(AddS(MulS(ex, ex), MulS(ey, ey)), MulS(ez, ez));
    }
    const float k = b / dd;
    const float ex = v.x - MulS(k, d.x), ey = v.y - MulS(k, d.y), ez = v.z - MulS(k, d.z);
    return AddS(AddS(MulS(ex, ex), MulS(ey, ey)), MulS(ez, ez));
}

// ---- the tests and first hits ----

template <ShapeKind S, Placement P, class L> bool __cdecl TestPair(const void *shape, const void *placement, const L *linear)
{
    const Pair p = Local<P>(placement, *linear);
    if constexpr (S == ShapeKind::Sphere) {
        // A sphere is tested by the distance from its centre to the linear shape: r^2 >= distance^2.
        const Vec3 centre{0.0f, 0.0f, 0.0f};
        const float r = *static_cast<const float *>(shape);
        float distance;
        if constexpr (std::is_same_v<L, Line3>) {
            Line3 line;
            line.origin = p.a;
            line.direction = p.b;
            distance = static_cast<float>(PointLineDistanceSquared(&centre, &line));
        } else if constexpr (std::is_same_v<L, Ray3>) {
            Ray3 ray;
            ray.origin = p.a;
            ray.direction = p.b;
            distance = static_cast<float>(PointRayDistanceSquared(&centre, &ray));
        } else {
            Segment3 segment;
            segment.start = p.a;
            segment.end = p.b;
            distance = static_cast<float>(PointSegmentDistanceSquared(&centre, &segment));
        }
        return MulS(r, r) >= distance;
    } else {
    float t0 = 0.0f, t1 = 0.0f;
    const bool span = Span<S, L>(&t0, &t1, shape, p);
    if constexpr (std::is_same_v<L, Line3>)
        return span;
    else if constexpr (std::is_same_v<L, Ray3>)
        return span && t1 >= 0.0f;
    else
        return span && 1.0f >= t0 && t1 >= 0.0f;
    }
}

template <ShapeKind S, Placement P, class L>
bool __cdecl FirstHitPair(float *t, const void *shape, const void *placement, const L *linear)
{
    const Pair p = Local<P>(placement, *linear);
    if constexpr (std::is_same_v<L, Line3>) {
        float unused = 0.0f;
        return Span<S, L>(t, &unused, shape, p);
    } else {
        float t0 = 0.0f, t1 = 0.0f;
        if (!Span<S, L>(&t0, &t1, shape, p)) return false;
        if constexpr (std::is_same_v<L, Ray3>) {
            if (!(t1 >= 0.0f)) return false;
        } else {
            if (!(1.0f >= t0) || !(t1 >= 0.0f)) return false;
            if (!(t0 >= 0.0f) && !(1.0f >= t1)) return false;
        }
        *t = t0 >= 0.0f ? t0 : t1;
        return true;
    }
}

#define PAIRS(S, P)                                                                                                      \
    template bool __cdecl TestPair<S, P, Line3>(const void *, const void *, const Line3 *);                              \
    template bool __cdecl TestPair<S, P, Ray3>(const void *, const void *, const Ray3 *);                                \
    template bool __cdecl TestPair<S, P, Segment3>(const void *, const void *, const Segment3 *);                        \
    template bool __cdecl FirstHitPair<S, P, Line3>(float *, const void *, const void *, const Line3 *);                 \
    template bool __cdecl FirstHitPair<S, P, Ray3>(float *, const void *, const void *, const Ray3 *);                   \
    template bool __cdecl FirstHitPair<S, P, Segment3>(float *, const void *, const void *, const Segment3 *);
PAIRS(ShapeKind::Cone, Placement::Oriented)
PAIRS(ShapeKind::Cone, Placement::Translated)
PAIRS(ShapeKind::Cone, Placement::InFrame)
PAIRS(ShapeKind::Sphere, Placement::Translated)
PAIRS(ShapeKind::Sphere, Placement::InFrame)
PAIRS(ShapeKind::Cuboid, Placement::Oriented)
PAIRS(ShapeKind::Cuboid, Placement::Translated)
PAIRS(ShapeKind::Cuboid, Placement::InFrame)
#undef PAIRS

} // namespace nGZCompGeom
