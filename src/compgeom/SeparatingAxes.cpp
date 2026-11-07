#include "compgeom/SeparatingAxes.h"

#include <cfloat>
#include <cstring>

#include "compgeom/Frames.h"
#include "compgeom/TrianglePairs.h"

// The separating-axis tests (src/compgeom/SeparatingAxes.h). RVAs 1.18.0.187.
//
// The scalar helpers spell out the original's operand order (SSE returns the first operand's NaN, quieted) where a NaN
// can reach a stored result (the frames); the tests that only branch use plain arithmetic in the original's association.
namespace nGZCompGeom {

namespace {

float MulS(float a, float b) { return a != a ? QuietNaN(a) : a * b; } // mulss a, b
float AddS(float a, float b) { return AddInOrder(a, b); }              // addss a, b
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
float Absolute(float f) { return FloatOf(Bits(f) & 0x7fffffffu); }
float Negated(float f) { return FloatOf(Bits(f) ^ 0x80000000u); }

const float kEpsilon = FLT_EPSILON; // 1.1920929e-07: added to every |matrix element| of the reach tables

struct Triangle {
    Vec3 a, b, c;
};
Triangle TriangleAt(const TriangleSet *set, uint32_t index)
{
    const Vec3 *vertices = set->mVertices.data();
    const uint16_t *corners = set->mIndices.data() + index * 3;
    return {vertices[corners[0]], vertices[corners[1]], vertices[corners[2]]};
}
inline bool IsTriangle(uint32_t child) { return (child & AABBTreeNode::kTriangleBit) != 0; }

// One edge-cross axis of 0x1a3410: the triangle's projections X and Y at two of its corners against the box's reach `r`.
// True when the axis separates (comiss X, Y; jbe: a NaN takes the second arm).
bool AxisSeparates(float x, float y, float r)
{
    if (x > y) return y > r || Negated(r) > x;
    return x > r || Negated(r) > y;
}

} // namespace

// ---- 0x1a5d20, 0x1a6650: the 3x3 product ----

float *__cdecl MatrixProduct(float *out, const float *a, const float *b)
{
    // a is [ecx], b is [edx] in the original.
    out[0] = AddS(AddS(MulS(b[1], a[3]), MulS(a[0], b[0])), MulS(b[2], a[6]));
    out[3] = AddS(AddS(MulS(b[3], a[0]), MulS(a[3], b[4])), MulS(b[5], a[6]));
    out[6] = AddS(AddS(MulS(a[0], b[6]), MulS(a[3], b[7])), MulS(b[8], a[6]));
    out[1] = AddS(AddS(MulS(a[1], b[0]), MulS(a[4], b[1])), MulS(b[2], a[7]));
    out[4] = AddS(AddS(MulS(a[1], b[3]), MulS(b[4], a[4])), MulS(b[5], a[7]));
    out[7] = AddS(AddS(MulS(b[6], a[1]), MulS(b[7], a[4])), MulS(b[8], a[7]));
    out[2] = AddS(AddS(MulS(a[5], b[1]), MulS(a[2], b[0])), MulS(a[8], b[2]));
    out[5] = AddS(AddS(MulS(a[2], b[3]), MulS(a[5], b[4])), MulS(b[5], a[8]));
    out[8] = AddS(AddS(MulS(a[5], b[7]), MulS(a[2], b[6])), MulS(b[8], a[8]));
    return out;
}

Matrix3x3 *Matrix3x3::MultiplyBy(const Matrix3x3 *b)
{
    float product[9];
    MatrixProduct(product, m, b->m);
    std::memcpy(m, product, sizeof(m));
    return this;
}

// ---- 0x19cca0, 0x19be20: frames ----

FramePlacement *FramePlacement::Invert()
{
    float inverse[9];
    AsFrame(*this).InverseMatrix(inverse);
    std::memcpy(mMatrix, inverse, sizeof(inverse));
    const float n0 = Negated(mOffset[0]), n1 = Negated(mOffset[1]), n2 = Negated(mOffset[2]);
    const float *m = mMatrix;
    const float x = AddS(AddS(MulS(n1, m[1]), MulS(n0, m[0])), MulS(n2, m[2]));
    const float y = AddS(AddS(MulS(n1, m[4]), MulS(n0, m[3])), MulS(n2, m[5]));
    const float z = AddS(AddS(MulS(n1, m[7]), MulS(n0, m[6])), MulS(n2, m[8]));
    mOffset[0] = x;
    mOffset[1] = y;
    mOffset[2] = z;
    return this;
}

FramePlacement *FramePlacement::Compose(const FramePlacement *other)
{
    mUnknown0 |= other->mUnknown0;
    reinterpret_cast<Matrix3x3 *>(mMatrix)->MultiplyBy(reinterpret_cast<const Matrix3x3 *>(other->mMatrix));
    float moved[3];
    const_cast<FramePlacement *>(other)->TransformPoint(moved, mOffset);
    std::memcpy(mOffset, moved, sizeof(moved));
    return this;
}

// ---- 0x19ffd0: the context ----

void SeparationContext::Build(const void *firstShape, const AffineTransform3 *firstFrame, const void *secondShape,
                              const AffineTransform3 *secondFrame)
{
    first = firstShape;
    second = secondShape;
    FramePlacement secondInverse;
    std::memcpy(static_cast<AffineTransform3 *>(&secondInverse), secondFrame, sizeof(AffineTransform3));
    secondInverse.Invert();
    FramePlacement composed;
    std::memcpy(static_cast<AffineTransform3 *>(&composed), firstFrame, sizeof(AffineTransform3));
    composed.Compose(&secondInverse);
    std::memcpy(static_cast<AffineTransform3 *>(&frame), static_cast<const AffineTransform3 *>(&composed),
                sizeof(AffineTransform3));
    std::memcpy(static_cast<AffineTransform3 *>(&inverse), static_cast<const AffineTransform3 *>(&composed),
                sizeof(AffineTransform3));
    inverse.Invert();
    std::memcpy(reach, frame.mMatrix, sizeof(reach));
    std::memcpy(inverseReach, inverse.mMatrix, sizeof(inverseReach));
    for (float &f : reach) f = AddS(Absolute(f), kEpsilon);
    for (float &f : inverseReach) f = AddS(Absolute(f), kEpsilon);
}

// ---- 0x19f800 ----

int __cdecl TreeBoxLineSpan(float *t0, float *t1, const AABBTreeNode *node, const Line3 *line)
{
    return BoxLineSpan(t0, t1, node, &node->centre, line);
}

// ---- 0x1a3410: a triangle against a box ----

bool __cdecl TriangleMeetsBox(const float *t, const float *box)
{
    const float bx = box[0], by = box[1], bz = box[2];

    // Edge 0 (corner 1 less corner 0).
    const float ex = t[3] - t[0], ey = t[4] - t[1], ez = t[5] - t[2];
    {
        const float a = t[1] * ez - t[2] * ey, c = t[7] * ez - t[8] * ey;
        if (AxisSeparates(c, a, by * Absolute(ez) + bz * Absolute(ey))) return false;
    }
    {
        const float a = ex * t[2] - t[0] * ez, c = ex * t[8] - t[6] * ez;
        if (AxisSeparates(c, a, bx * Absolute(ez) + bz * Absolute(ex))) return false;
    }
    {
        const float p1 = ey * t[3] - t[4] * ex, p2 = ey * t[6] - t[7] * ex;
        if (AxisSeparates(p2, p1, bx * Absolute(ey) + by * Absolute(ex))) return false;
    }
    // Edge 1 (corner 2 less corner 1).
    const float fx = t[6] - t[3], fy = t[7] - t[4], fz = t[8] - t[5];
    {
        const float a = t[1] * fz - t[2] * fy, c = t[7] * fz - t[8] * fy;
        if (AxisSeparates(c, a, by * Absolute(fz) + bz * Absolute(fy))) return false;
    }
    {
        const float a = t[2] * fx - fz * t[0], c = t[8] * fx - fz * t[6];
        if (AxisSeparates(c, a, bx * Absolute(fz) + bz * Absolute(fx))) return false;
    }
    {
        const float a = fy * t[0] - t[1] * fx, p = fy * t[3] - t[4] * fx;
        if (AxisSeparates(p, a, by * Absolute(fx) + bx * Absolute(fy))) return false;
    }
    // Edge 2 (corner 0 less corner 2).
    const float gx = t[0] - t[6], gy = t[1] - t[7], gz = t[2] - t[8];
    {
        const float a = t[1] * gz - gy * t[2], c = t[4] * gz - gy * t[5];
        if (AxisSeparates(c, a, by * Absolute(gz) + bz * Absolute(gy))) return false;
    }
    {
        const float a = t[2] * gx - t[0] * gz, c = t[5] * gx - t[3] * gz;
        if (AxisSeparates(c, a, bx * Absolute(gz) + bz * Absolute(gx))) return false;
    }
    {
        const float p3 = gy * t[3] - t[4] * gx, p6 = gy * t[6] - t[7] * gx;
        if (AxisSeparates(p6, p3, by * Absolute(gx) + bx * Absolute(gy))) return false;
    }

    // The box's own axes: the triangle's extent along each against the half extent.
    for (unsigned k = 0; k < 3; ++k) {
        const float a = t[k], b = t[3 + k], c = t[6 + k], e = box[k];
        float low = a > b ? b : a;
        low = low > c ? c : low;
        if (low > e) return false;
        float high = b > a ? b : a;
        high = c > high ? c : high;
        if (Negated(e) > high) return false;
    }

    // The triangle's plane: the normal (from corner 2), the box's corner furthest along it.
    const float da = t[4] - t[7], db = t[1] - t[7], dc = t[5] - t[8], dd = t[3] - t[6], de = t[0] - t[6], df = t[2] - t[8];
    const float nx = db * dc - df * da;
    const float ny = df * dd - de * dc;
    const float nz = da * de - db * dd;
    const float offset = Negated((t[7] * ny + t[6] * nx) + t[8] * nz);
    float vz = bz, vy = by, vx = bx;
    if (nz > 0.0f) vz = Negated(vz);
    if (ny > 0.0f) vy = Negated(vy);
    if (nx > 0.0f) vx = Negated(vx);
    const float nearSum = ((nx * vx) + (ny * vy)) + (nz * vz) + offset;
    if (!(0.0f >= nearSum)) return false;
    const float farSum = ((Negated(vx) * nx - ny * vy) - nz * vz) + offset;
    return farSum >= 0.0f;
}

// ---- 0x19f930, 0x19f8e0: the set against a cuboid ----

namespace {

// The triangle with index `index` of the context's set, taken through the context's frame into the cuboid's.
bool LeafMeetsCuboid(const SeparationContext *c, uint32_t index)
{
    const Triangle triangle = TriangleAt(static_cast<const TriangleSet *>(c->first), index);
    float placed[9];
    FramePlacement *frame = const_cast<FramePlacement *>(&c->frame);
    frame->TransformPoint(&placed[0], &triangle.a.x);
    frame->TransformPoint(&placed[3], &triangle.b.x);
    frame->TransformPoint(&placed[6], &triangle.c.x);
    return TriangleMeetsBox(placed, static_cast<const float *>(c->second));
}

} // namespace

bool __cdecl TreeMeetsCuboid(const AABBTreeNode *base, const AABBTreeNode *node, const SeparationContext *c)
{
    const float *he = &node->box.half.x;
    const float hx = he[0], hy = he[1], hz = he[2];
    const float *cuboid = static_cast<const float *>(c->second);
    const float cx = cuboid[0], cy = cuboid[1], cz = cuboid[2];
    const float *r = c->reach;
    float centre[3];
    const_cast<FramePlacement *>(&c->frame)->TransformPoint(centre, &node->centre.x);

    // The node's box in the cuboid's frame against the cuboid: the three axes of the cuboid.
    if (Absolute(centre[0]) > ((hy * r[1] + hx * r[0]) + hz * r[2]) + cx) return false;
    if (Absolute(centre[1]) > ((hy * r[4] + hx * r[3]) + hz * r[5]) + cy) return false;
    if (Absolute(centre[2]) > ((hy * r[7] + hx * r[6]) + hz * r[8]) + cz) return false;
    // And the three axes of the node's box, from the cuboid's side (the cuboid's centre in the node's frame).
    const float *ir = c->inverseReach;
    const float ox = c->inverse.mOffset[0] - node->centre.x;
    const float oy = c->inverse.mOffset[1] - node->centre.y;
    const float oz = c->inverse.mOffset[2] - node->centre.z;
    if (Absolute(ox) > ((cy * ir[1] + cx * ir[0]) + cz * ir[2]) + hx) return false;
    if (Absolute(oy) > ((cy * ir[4] + cx * ir[3]) + cz * ir[5]) + hy) return false;
    if (Absolute(oz) > ((cy * ir[7] + cx * ir[6]) + cz * ir[8]) + hz) return false;

    const bool leftIsTriangle = IsTriangle(node->left), rightIsTriangle = IsTriangle(node->right);
    if (!leftIsTriangle) {
        if (!rightIsTriangle) {
            if (TreeMeetsCuboid(base, base + (node->left >> 1), c)) return true;
            return TreeMeetsCuboid(base, base + (node->right >> 1), c);
        }
        if (LeafMeetsCuboid(c, node->right >> 1)) return true;
        return TreeMeetsCuboid(base, base + (node->left >> 1), c);
    }
    if (rightIsTriangle) {
        if (LeafMeetsCuboid(c, node->left >> 1)) return true;
        return LeafMeetsCuboid(c, node->right >> 1);
    }
    if (LeafMeetsCuboid(c, node->left >> 1)) return true;
    return TreeMeetsCuboid(base, base + (node->right >> 1), c);
}

bool __cdecl SetMeetsCuboidTest(const TriangleSet *set, const AffineTransform3 *setFrame, const void *cuboid,
                                const AffineTransform3 *cuboidFrame)
{
    SeparationContext context;
    context.Build(set, setFrame, cuboid, cuboidFrame);
    const AABBTreeNode *root = set->mTree.mNodes.data();
    return TreeMeetsCuboid(root, root, &context);
}

// ---- 0x1a4ea0, 0x1a42d0, 0x1a4620: a triangle against a triangle ----
//
// All three return a bool, so a NaN only matters through which test it passes or fails: the comparisons are the
// ordered C++ ones and the arithmetic keeps the original's association.

namespace {

// The distance of a vertex from a plane counts as zero within this (1.1920929e-10).
const float kPlaneEpsilon = FloatOf(0x2f03126fu);

// The plane through the triangle t (normal taken at corner 2, offset the negated dot of the normal with corner 2).
struct Plane {
    float nx, ny, nz, offset;
};
Plane PlaneOf(const float *t)
{
    Plane p;
    p.nx = (t[1] - t[7]) * (t[5] - t[8]) - (t[4] - t[7]) * (t[2] - t[8]);
    p.ny = (t[3] - t[6]) * (t[2] - t[8]) - (t[0] - t[6]) * (t[5] - t[8]);
    p.nz = (t[0] - t[6]) * (t[4] - t[7]) - (t[3] - t[6]) * (t[1] - t[7]);
    p.offset = Negated((p.ny * t[7] + p.nx * t[6]) + p.nz * t[8]);
    return p;
}
// A vertex's signed distance from the plane, snapped to zero when that small (a NaN stays).
float Distance(const Plane &p, const float *v)
{
    float d = ((p.ny * v[1] + p.nx * v[0]) + p.nz * v[2]) + p.offset;
    if (kPlaneEpsilon >= Absolute(d)) d = 0.0f;
    return d;
}

// One triangle's crossing of the line the two planes share: `s` its corners' distances from the other plane, `p` their
// positions along the line's dominant axis. The corner alone on its side (else the first not on the plane) is the pivot.
struct Crossing {
    float x0, x1; // the other corners' offsets from the pivot along the axis, times the pivot's distance
    float r0, r1; // the pivot's distance less each other corner's
    float pivot;  // the pivot's position
};
// False when all three distances are zero (the triangles are coplanar).
bool CrossingOf(const float s[3], const float p[3], Crossing *out)
{
    unsigned k;
    if (s[1] * s[0] > 0.0f) k = 2;
    else if (s[2] * s[0] > 0.0f) k = 1;
    else if (s[2] * s[1] > 0.0f) k = 0;
    else if (!(s[0] == 0.0f)) k = 0;
    else if (!(s[1] == 0.0f)) k = 1;
    else if (s[2] == 0.0f) return false;
    else k = 2;
    const unsigned a = k == 0 ? 1 : 0, b = k == 2 ? 1 : 2;
    out->x0 = (p[a] - p[k]) * s[k];
    out->x1 = (p[b] - p[k]) * s[k];
    out->r0 = s[k] - s[a];
    out->r1 = s[k] - s[b];
    out->pivot = p[k];
    return true;
}

} // namespace

bool __cdecl PointInTriangleProjected(const float *p, const float *t, int d, int s)
{
    const float *v0 = t, *v1 = t + 3, *v2 = t + 6;
    const float c1 = (v1[s] - v0[s]) * (p[d] - v0[d]) - (v1[d] - v0[d]) * (p[s] - v0[s]);
    const float c2 = (v2[s] - v1[s]) * (p[d] - v1[d]) - (v2[d] - v1[d]) * (p[s] - v1[s]);
    const float c3 = (v0[s] - v2[s]) * (p[d] - v2[d]) - (v0[d] - v2[d]) * (p[s] - v2[s]);
    return c2 * c1 > 0.0f && c3 * c1 > 0.0f;
}

bool __cdecl CoplanarTrianglesMeet(const float *a, const float *b)
{
    // The axis a's normal is largest on is dropped; the other two span the plane.
    const float n[3] = {Absolute((a[4] - a[1]) * (a[8] - a[2]) - (a[7] - a[1]) * (a[5] - a[2])),
                        Absolute((a[6] - a[0]) * (a[5] - a[2]) - (a[3] - a[0]) * (a[8] - a[2])),
                        Absolute((a[3] - a[0]) * (a[7] - a[1]) - (a[4] - a[1]) * (a[6] - a[0]))};
    unsigned u = n[1] > n[0] ? 1 : 0;
    if (n[2] > n[u]) u = 2;
    const int d = int((u + 1) % 3), s = int((u + 2) % 3);

    // Each edge of b against each edge of a, in the plane.
    for (unsigned i = 0; i < 3; ++i) {
        const float *b0 = b + i * 3, *b1 = b + ((i + 1) % 3) * 3;
        const float dd = b1[d] - b0[d], ds = b1[s] - b0[s];
        for (unsigned j = 0; j < 3; ++j) {
            const float *a0 = a + j * 3, *a1 = a + ((j + 1) % 3) * 3;
            const float fromD = b0[d] - a0[d], edgeD = a0[d] - a1[d];
            const float edgeS = a0[s] - a1[s], fromS = b0[s] - a0[s];
            const float f11 = edgeD * ds - edgeS * dd;
            const float f12 = fromD * edgeS - fromS * edgeD;
            if ((f11 > 0.0f && f12 >= 0.0f && f11 >= f12) || (f11 < 0.0f && f12 <= 0.0f && f12 >= f11)) {
                const float f13 = fromS * dd - fromD * ds;
                if (f11 > 0.0f ? (f13 >= 0.0f && f11 >= f13) : (f13 <= 0.0f && f13 >= f11)) return true;
            }
        }
    }
    // No edges cross: one may lie wholly inside the other.
    return PointInTriangleProjected(b, a, d, s) || PointInTriangleProjected(a, b, d, s);
}

bool __cdecl TriangleMeetsTriangle(const float *a, const float *b)
{
    const Plane pa = PlaneOf(a);
    const float d[3] = {Distance(pa, b), Distance(pa, b + 3), Distance(pa, b + 6)};
    if (d[1] * d[0] > 0.0f && d[2] * d[0] > 0.0f) return false; // b wholly on one side of a's plane

    const Plane pb = PlaneOf(b);
    const float e[3] = {Distance(pb, a), Distance(pb, a + 3), Distance(pb, a + 6)};
    if (e[1] * e[0] > 0.0f && e[2] * e[0] > 0.0f) return false;

    // The shared line runs along the cross product of the normals; its largest component is the axis to project on.
    const float c[3] = {Absolute(pa.ny * pb.nz - pa.nz * pb.ny), Absolute(pa.nz * pb.nx - pa.nx * pb.nz),
                        Absolute(pa.nx * pb.ny - pa.ny * pb.nx)};
    unsigned u = c[1] > c[0] ? 1 : 0;
    if (c[2] > c[u]) u = 2;

    const float aAxis[3] = {a[u], a[3 + u], a[6 + u]};
    const float bAxis[3] = {b[u], b[3 + u], b[6 + u]};
    Crossing onA, onB;
    if (!CrossingOf(e, aAxis, &onA)) return CoplanarTrianglesMeet(a, b);
    if (!CrossingOf(d, bAxis, &onB)) return CoplanarTrianglesMeet(a, b);

    // Each triangle's interval on the line, as fractions with a common denominator, tested for overlap.
    const float q1 = onA.r1 * onA.r0, q2 = onB.r1 * onB.r0;
    const float q = q2 * q1;
    const float f1 = (onA.r1 * onA.x0) * q2 + q * onA.pivot;
    const float f2 = (onA.r0 * onA.x1) * q2 + q * onA.pivot;
    const float g0 = (onB.r1 * onB.x0) * q1 + q * onB.pivot;
    const float g1 = (onB.r0 * onB.x1) * q1 + q * onB.pivot;
    float hiF, loF, hiG, loG;
    if (f1 > f2) { hiF = f1; loF = f2; } else { hiF = f2; loF = f1; }
    if (g0 > g1) { hiG = g0; loG = g1; } else { hiG = g1; loG = g0; }
    return hiF >= loG && hiG >= loF;
}

// ---- 0x1a0210, 0x1a01c0: a set against a set ----

namespace {

// Triangle `i` of the first set through the context's frame, against triangle `j` of the second as it is.
bool LeavesMeet(const SeparationContext *c, uint32_t i, uint32_t j)
{
    const Triangle first = TriangleAt(static_cast<const TriangleSet *>(c->first), i);
    const Triangle second = TriangleAt(static_cast<const TriangleSet *>(c->second), j);
    float placed[9];
    FramePlacement *frame = const_cast<FramePlacement *>(&c->frame);
    frame->TransformPoint(&placed[0], &first.a.x);
    frame->TransformPoint(&placed[3], &first.b.x);
    frame->TransformPoint(&placed[6], &first.c.x);
    float raw[9];
    std::memcpy(&raw[0], &second.a, 12);
    std::memcpy(&raw[3], &second.b, 12);
    std::memcpy(&raw[6], &second.c, 12);
    return TriangleMeetsTriangle(placed, raw);
}

} // namespace

bool __cdecl NodesMeet(const AABBTreeNode *base1, const AABBTreeNode *base2, const AABBTreeNode *n1,
                       const AABBTreeNode *n2, const SeparationContext *c)
{
    const float h1x = n1->box.half.x, h1y = n1->box.half.y, h1z = n1->box.half.z;
    const float h2x = n2->box.half.x, h2y = n2->box.half.y, h2z = n2->box.half.z;
    // The first box's centre in the second's frame, and the first box's reach along the second's axes.
    const float *r = c->reach;
    float t[3];
    const_cast<FramePlacement *>(&c->frame)->TransformPoint(t, &n1->centre.x);
    t[0] -= n2->centre.x;
    t[1] -= n2->centre.y;
    t[2] -= n2->centre.z;
    if (Absolute(t[0]) > ((h1y * r[1] + h1x * r[0]) + h1z * r[2]) + h2x) return false;
    if (Absolute(t[1]) > ((h1y * r[4] + h1x * r[3]) + h1z * r[5]) + h2y) return false;
    if (Absolute(t[2]) > ((h1y * r[7] + h1x * r[6]) + h1z * r[8]) + h2z) return false;
    // The same from the other side.
    const float *ir = c->inverseReach;
    float u[3];
    const_cast<FramePlacement *>(&c->inverse)->TransformPoint(u, &n2->centre.x);
    u[0] -= n1->centre.x;
    u[1] -= n1->centre.y;
    u[2] -= n1->centre.z;
    if (Absolute(u[0]) > ((h2y * ir[1] + h2x * ir[0]) + h2z * ir[2]) + h1x) return false;
    if (Absolute(u[1]) > ((h2y * ir[4] + h2x * ir[3]) + h2z * ir[5]) + h1y) return false;
    if (Absolute(u[2]) > ((h2y * ir[7] + h2x * ir[6]) + h2z * ir[8]) + h1z) return false;

    // A child is a node (index into the tree) or a triangle (bit 0 set); go down the nodes and test the leaf pairs.
    auto node1 = [&](unsigned side) { return base1 + ((side ? n1->right : n1->left) >> 1); };
    auto node2 = [&](unsigned side) { return base2 + ((side ? n2->right : n2->left) >> 1); };
    auto walk = [&](const AABBTreeNode *x, const AABBTreeNode *y) { return NodesMeet(base1, base2, x, y, c); };
    auto leaves = [&](unsigned side1, unsigned side2) {
        return LeavesMeet(c, (side1 ? n1->right : n1->left) >> 1, (side2 ? n2->right : n2->left) >> 1);
    };

    const unsigned kind = (IsTriangle(n1->left) ? 8u : 0u) | (IsTriangle(n1->right) ? 4u : 0u) |
                          (IsTriangle(n2->left) ? 2u : 0u) | (IsTriangle(n2->right) ? 1u : 0u);
    switch (kind) {
    case 0: // nodes on both sides
        return walk(node1(0), node2(0)) || walk(node1(0), node2(1)) || walk(node1(1), node2(0)) ||
               walk(node1(1), node2(1));
    case 1: case 2: case 3: // the first has only nodes: go down it
        return walk(node1(1), n2) || walk(node1(0), n2);
    case 4: case 8: case 12: // the second has only nodes: go down it
        return walk(n1, node2(1)) || walk(n1, node2(0));
    case 5: return leaves(1, 1) || walk(n1, node2(0)) || walk(node1(0), n2);
    case 6: return leaves(1, 0) || walk(n1, node2(1)) || walk(node1(0), n2);
    case 9: return leaves(0, 1) || walk(n1, node2(0)) || walk(node1(1), n2);
    case 10: return leaves(0, 0) || walk(n1, node2(1)) || walk(node1(1), n2);
    case 7: return leaves(1, 0) || leaves(1, 1) || walk(node1(0), n2);
    case 11: return leaves(0, 0) || leaves(0, 1) || walk(node1(1), n2);
    case 13: return leaves(0, 1) || leaves(1, 1) || walk(n1, node2(0));
    case 14: return leaves(0, 0) || leaves(1, 0) || walk(n1, node2(1));
    default: return leaves(0, 0) || leaves(0, 1) || leaves(1, 0) || leaves(1, 1);
    }
}

bool __cdecl SetMeetsSetTest(const TriangleSet *first, const AffineTransform3 *firstFrame, const TriangleSet *second,
                             const AffineTransform3 *secondFrame)
{
    SeparationContext context;
    context.Build(first, firstFrame, second, secondFrame);
    const AABBTreeNode *root1 = first->mTree.mNodes.data(), *root2 = second->mTree.mNodes.data();
    return NodesMeet(root1, root2, root1, root2, &context);
}

} // namespace nGZCompGeom
