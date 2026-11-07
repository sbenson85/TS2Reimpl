#include <cmath>

#include "compgeom/ConvexMath.h"
#include "compgeom/ConvexShapes.h"
#include "compgeom/Rigid.h"

// The small functions under the convex shapes (lane PM, scenegraph G0b step 4): the sphere's boxes and support point, the
// transposed product of a frame and the point-to-box distance.
namespace nGZCompGeom {

using namespace convex;

BoundBox *Sphere::BoxInFrame(BoundBox *out, const AffineTransform3 *frame) const
{
    if (!(frame->mUnknown0 & 0x18)) {
        const float s = Stored(Mul87(ScaleOf(*frame), radius));
        const Vec3 centre = OffsetOf(*frame);
        out->max = {AddS(centre.x, s), AddS(centre.y, s), AddS(centre.z, s)};
        const float n = Negated(s);
        out->min = {AddS(centre.x, n), AddS(centre.y, n), AddS(centre.z, n)};
        return out;
    }
    BoundBox box;
    const float n = Negated(radius);
    box.min = {n, n, n};
    box.max = {radius, radius, radius};
    box.Transform(frame);
    *out = box;
    return out;
}

BoundBox *Sphere::BoxAt(BoundBox *out, const Vec3 *centre) const
{
    const Vec3 c = *centre;
    out->max = {AddS(c.x, radius), AddS(c.y, radius), AddS(c.z, radius)};
    const float n = Negated(radius);
    out->min = {AddS(c.x, n), AddS(c.y, n), AddS(c.z, n)};
    return out;
}

Vec3 *Sphere::SupportAlong(Vec3 *out, const Vec3 *vector) const
{
    const Vec3 v = *vector;
    const float length = Stored(std::sqrt(static_cast<double>(LengthSquared(v))));
    constexpr float kTiny = 1.1920929e-10f; // 0x2f03126f
    if (kTiny >= std::fabs(length)) { // comiss kTiny, |length|; jb (taken when kTiny < |length| or unordered) goes on to divide
        *out = {radius, 0.0f, 0.0f};
        return out;
    }
    const float k = radius / length;
    *out = {MulS(v.x, k), MulS(v.y, k), MulS(v.z, k)};
    return out;
}

// ---- the cone (lane PR) ----

BoundBox *Cone::BoxInFrame(BoundBox *out, const AffineTransform3 *frame) const
{
    BoundBox box;
    box.min = {Negated(radius), Negated(radius), Negated(height)};
    box.max = {radius, radius, height};
    box.Transform(frame);
    *out = box;
    return out;
}

BoundBox *Cone::BoxInRigid(BoundBox *out, const RigidTransform3 *rigid) const
{
    ConvexBox box;
    box.min = {Negated(radius), Negated(radius), Negated(height)};
    box.max = {radius, radius, height};
    box.TransformRigid(rigid);
    *out = box;
    return out;
}

BoundBox *Cone::BoxAt(BoundBox *out, const Vec3 *centre) const
{
    const Vec3 c = *centre;
    const float n = Negated(radius);
    out->max = {AddS(c.x, radius), AddS(c.y, radius), AddS(height, c.z)};
    out->min = {AddS(n, c.x), AddS(c.y, n), SubS(c.z, height)};
    return out;
}

Vec3 *Cone::SupportAlong(Vec3 *out, const Vec3 *vector) const
{
    const Vec3 v = *vector;
    const float r2 = MulS(radius, radius);
    const float s = AddS(MulS(v.x, v.x), MulS(v.y, v.y));
    const float zz = MulS(v.z, v.z);
    if (v.z > 0.0f) { // comiss v.z, 0; jbe skips on <= or unordered
        const float twice = Stored(Add87(static_cast<double>(height), static_cast<double>(height)));
        const float q = DivS(MulS(AddS(zz, s), r2), AddS(MulS(twice, twice), r2));
        if (zz > q) { // the direction is steep enough to land on the apex
            *out = {0.0f, 0.0f, height};
            return out;
        }
    }
    const float kTiny = FloatOf(0x1e8637beu); // 1.4210856e-20
    if (kTiny >= std::fabs(s)) { // comiss kTiny, |s|; jae
        *out = {0.0f, 0.0f, Negated(height)};
        return out;
    }
    const float k = static_cast<float>(std::sqrt(static_cast<double>(DivS(r2, s))));
    *out = {MulS(v.x, k), MulS(v.y, k), Negated(height)};
    return out;
}

BoundBox *ConvexBox::TransformRigid(const RigidTransform3 *rigid)
{
    float m[9];
    rigid->ToMatrix(m);
    const float hx = MulS(SubS(max.x, min.x), 0.5f);
    const float hy = MulS(SubS(max.y, min.y), 0.5f);
    const float hz = MulS(SubS(max.z, min.z), 0.5f);
    const Vec3 half = {hx, hy, hz};
    const float cx = AddS(hx, min.x), cy = AddS(hy, min.y), cz = AddS(min.z, hz);
    const Vec3 t = rigid->translation;
    const float x = AddS(AddS(AddS(MulS(m[0], cx), MulS(cy, m[1])), MulS(cz, m[2])), t.x);
    const float y = AddS(AddS(AddS(MulS(m[3], cx), MulS(cy, m[4])), MulS(cz, m[5])), t.y);
    const float z = AddS(AddS(AddS(MulS(m[6], cx), MulS(cy, m[7])), MulS(cz, m[8])), t.z);
    float reach[3];
    for (int row = 0; row < 3; ++row) {
        const float absolute[3] = {std::fabs(m[row * 3]), std::fabs(m[row * 3 + 1]), std::fabs(m[row * 3 + 2])};
        reach[row] = Stored(Dot87(&half.x, absolute));
    }
    min = {SubS(x, reach[0]), SubS(y, reach[1]), SubS(z, reach[2])};
    max = {AddS(x, reach[0]), AddS(y, reach[1]), AddS(z, reach[2])};
    return this;
}

Vec3 *ConvexFrame::TransposedVectorTo(Vec3 *out, const Vec3 *in) const
{
    const float *m = mMatrix;
    out->x = AddS(AddS(MulS(m[3], in->y), MulS(m[0], in->x)), MulS(m[6], in->z));
    out->y = AddS(AddS(MulS(m[4], in->y), MulS(m[1], in->x)), MulS(m[7], in->z));
    out->z = AddS(AddS(MulS(m[5], in->y), MulS(m[2], in->x)), MulS(m[8], in->z));
    return out;
}

double __cdecl PointBoxDistanceSquared(const float *half, const float *p)
{
    float acc = 0.0f;
    for (int i = 0; i < 3; ++i) {
        float d;
        if (Negated(half[i]) > p[i])
            d = AddS(p[i], half[i]);
        else if (p[i] > half[i])
            d = SubS(p[i], half[i]);
        else
            continue;
        const float square = MulS(d, d);
        acc = i < 2 ? AddS(acc, square) : AddS(square, acc);
    }
    return acc;
}

} // namespace nGZCompGeom
