#include "compgeom/Frustum.h"

#include "compgeom/ConvexMath.h"
#include "compgeom/ConvexShapes.h"

// The frustum shape and its cull tests (lane PV, scenegraph G0b step 5). Every float operation keeps the original's SSE
// operand order (the first operand's NaN wins; MulS / AddS / SubS in ConvexMath.h); abs is a sign-bit clear (andps with
// the mask at 0xda1880) and negation a sign-bit flip (xorps with the mask at 0xd9c6f0).
namespace nGZCompGeom {

using namespace convex;

namespace {

float Absolute(float f)
{
    return FloatOf(BitsOf(f) & 0x7fffffffu);
}

void PutNegated(float *to, float x, float y, float z, float w)
{
    to[0] = Negated(x);
    to[1] = Negated(y);
    to[2] = Negated(z);
    to[3] = Negated(w);
}

// The sign trick both plane tests end with: the sign bit of the low end of the reach interval minus the sign bit of the
// high end (r - d, d + r): 1 when the box lies wholly on the positive side, -1 on the negative side, else 0.
int SideOf(float reach, float distance)
{
    const float low = SubS(reach, distance);
    const float high = AddS(distance, reach);
    return int(BitsOf(low) >> 31) - int(BitsOf(high) >> 31);
}

// The six-plane pass the three cull tests share. `side(plane)` is the plane test.
template <class Side> int ClassifyPlanes(const Frustum &frustum, uint8_t *mask, Side side)
{
    uint32_t inside = 0;
    for (unsigned i = 0; i < 6; ++i) {
        if (*mask & (1u << i)) continue;
        const int s = side(frustum.plane[i]);
        if (s > 0) return s;
        inside |= (uint32_t(s) >> 31) << i;
    }
    *mask = uint8_t(*mask | inside);
    return *mask != 0x3f ? 0 : -1;
}

} // namespace

void Frustum::FromView(const float *m)
{
    // Row r of the matrix is m[4r..4r+3]; the planes are the negated combinations of the rows.
    PutNegated(plane[2], AddS(m[0], m[12]), AddS(m[1], m[13]), AddS(m[2], m[14]), AddS(m[3], m[15])); // left: row 3 + row 0
    PutNegated(plane[3], SubS(m[12], m[0]), SubS(m[13], m[1]), SubS(m[14], m[2]), SubS(m[15], m[3])); // right: row 3 - row 0
    PutNegated(plane[4], SubS(m[12], m[4]), SubS(m[13], m[5]), SubS(m[14], m[6]), SubS(m[15], m[7])); // top: row 3 - row 1
    // bottom: row 1 + row 3, the row 1 element the first operand for x and the row 3 element for y, z and w
    PutNegated(plane[5], AddS(m[4], m[12]), AddS(m[13], m[5]), AddS(m[6], m[14]), AddS(m[15], m[7]));
    PutNegated(plane[0], m[8], m[9], m[10], m[11]); // near: row 2, the words copied
    PutNegated(plane[1], SubS(m[12], m[8]), SubS(m[13], m[9]), SubS(m[14], m[10]), SubS(m[15], m[11])); // far: row 3 - row 2
}

int __cdecl PlaneBoxSide(const float *p, const Vec3 *half, const Vec3 *centre)
{
    const float tx = Absolute(MulS(p[0], half->x));
    const float ty = Absolute(MulS(p[1], half->y));
    const float tz = Absolute(MulS(p[2], half->z));
    const float reach = AddS(AddS(ty, tx), tz);
    const float cy = MulS(p[1], centre->y);
    const float cx = MulS(p[0], centre->x);
    const float cz = MulS(p[2], centre->z);
    const float distance = AddS(AddS(AddS(cy, cx), cz), p[3]);
    return SideOf(reach, distance);
}

int __cdecl PlaneFrameBoxSide(const float *p, const Vec3 *half, const AffineTransform3 *frame)
{
    Vec3 local;
    static_cast<const ConvexFrame &>(*frame).TransposedVectorTo(&local, reinterpret_cast<const Vec3 *>(p));
    const float tx = Absolute(MulS(local.x, half->x));
    const float ty = Absolute(MulS(local.y, half->y));
    const float tz = Absolute(MulS(local.z, half->z));
    const float reach = AddS(AddS(ty, tx), tz);
    const Vec3 offset = OffsetOf(*frame);
    const float oy = MulS(offset.y, p[1]);
    const float ox = MulS(p[0], offset.x);
    const float oz = MulS(offset.z, p[2]);
    const float distance = AddS(AddS(AddS(oy, ox), oz), p[3]);
    return SideOf(reach, distance);
}

int Frustum::ClassifyBoxAt(uint8_t *mask, const Vec3 *half, const Vec3 *centre) const
{
    return ClassifyPlanes(*this, mask, [&](const float *p) { return PlaneBoxSide(p, half, centre); });
}

int Frustum::ClassifyBoxInFrame(uint8_t *mask, const Vec3 *half, const AffineTransform3 *frame) const
{
    return ClassifyPlanes(*this, mask, [&](const float *p) { return PlaneFrameBoxSide(p, half, frame); });
}

int Frustum::ClassifyBox(uint8_t *mask, const BoundBox *box, const AffineTransform3 *frame) const
{
    const float dx = SubS(box->max.x, box->min.x);
    const float dy = SubS(box->max.y, box->min.y);
    const float dz = SubS(box->max.z, box->min.z);
    const Vec3 half = {MulS(dx, 0.5f), MulS(dy, 0.5f), MulS(dz, 0.5f)};
    const Vec3 centre = {AddS(half.x, box->min.x), AddS(half.y, box->min.y), AddS(half.z, box->min.z)};
    const AffineTransform3 placed = FrameAt(*frame, FramePoint(*frame, centre));
    return ClassifyPlanes(*this, mask, [&](const float *p) { return PlaneFrameBoxSide(p, &half, &placed); });
}

// ---- the shape ----

bool cFrustumBound::QueryInterface(uint32_t id, void **out)
{
    if (!out) return false;
    if (id == 1 || id == kInterfaceId || id == cIShape::kInterfaceId) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

uint32_t cFrustumBound::Release()
{
    const uint32_t count = --mRefCount;
    if (count == 0) {
        delete this;
        return 0;
    }
    return count;
}

bool cFrustumBound::Clone(uint32_t id, void **out)
{
    // ORIGINAL-BUG: a copy the query refuses (an id the shape doesn't answer) is never released: it leaks.
    cFrustumBound *const copy = new cFrustumBound;
    copy->mData = mData;
    return copy->QueryInterface(id, out);
}

cFrustumShape *__cdecl cFrustumShape::Create()
{
    return new cFrustumShape;
}

} // namespace nGZCompGeom
