#include "world/WorldPlaneCollider.h"

#include "compgeom/Rigid.h"
#include "gz/cIGZCOM.h"
#include "misc/X87.h"
#include "rz/StaticGetters.h"
#include "sg/cGeometryBuilder.h"
#include "sg/cGeometryNode.h"
#include "sg/cIndexedMeshBuilder.h"

#include <cmath>

namespace world_plane {

namespace {

using fx::Add;
using fx::Div;
using fx::Mul;
using fx::RowFromY;
using fx::Sub;

// The rotation of `t` applied to `v` (each row summed from y), as the plane's code writes it out for every point.
Vector3 Rotated(const EffectsTransform &t, const Vector3 &v)
{
    return {RowFromY(v.x, v.y, v.z, t.m[0][0], t.m[0][1], t.m[0][2]), RowFromY(v.x, v.y, v.z, t.m[1][0], t.m[1][1], t.m[1][2]),
            RowFromY(v.x, v.y, v.z, t.m[2][0], t.m[2][1], t.m[2][2])};
}

// A corner taken out by `t` in place: rotated (when the flag says so), scaled (the scale the first operand), translated. The
// first corner adds each coordinate to its translation; the others add the translation to y and z.
void PlaceCorner(const EffectsTransform &t, Vector3 *c, bool first)
{
    if (t.flags & EffectsTransform::kRotated) *c = Rotated(t, *c);
    c->x = Mul(t.scale, c->x);
    c->y = Mul(t.scale, c->y);
    c->z = Mul(t.scale, c->z);
    c->x = Add(c->x, t.translation.x);
    if (first) {
        c->y = Add(c->y, t.translation.y);
        c->z = Add(c->z, t.translation.z);
    } else {
        c->y = Add(t.translation.y, c->y);
        c->z = Add(t.translation.z, c->z);
    }
}

constexpr uint32_t kGeometryNodeClass = 0x7ba3838c;
constexpr uint32_t kIndexedMeshBuilderClass = 0x9bffc10d;

} // namespace

uint32_t cTSPlaneCollider::GetStateSize() { return sizeof(PlaneState); }

void cTSPlaneCollider::PrepareState(const PlaneParams *params, const EffectsTransform *transform,
                                    const EffectsTransform *placement, PlaneState *state)
{
    const Vector3 a = params->edgeA;
    const Vector3 b = params->edgeB;
    Vector3 *c = state->corner;
    c[0] = params->origin;
    c[1] = {Add(c[0].x, a.x), Add(a.y, c[0].y), Add(a.z, c[0].z)};
    c[2] = {Add(b.x, c[1].x), Add(b.y, c[1].y), Add(b.z, c[1].z)};
    c[3] = {Add(c[0].x, b.x), Add(b.y, c[0].y), Add(b.z, c[0].z)};

    // a x b, made a unit vector when its squared length (summed y, x, z; compared and rooted in double) is over 1e-6.
    Vector3 n{Sub(Mul(a.y, b.z), Mul(a.z, b.y)), Sub(Mul(b.x, a.z), Mul(a.x, b.z)), Sub(Mul(a.x, b.y), Mul(b.x, a.y))};
    const double lengthSq = Add(Add(Mul(n.y, n.y), Mul(n.x, n.x)), Mul(n.z, n.z));
    if (lengthSq > 1e-6) {
        const float length = static_cast<float>(std::sqrt(lengthSq));
        n = {Div(n.x, length), Div(n.y, length), Div(n.z, length)};
    }
    state->normal = n;
    state->placement = *placement;

    const EffectsTransform &t = *transform;
    for (int i = 0; i < 4; ++i) PlaceCorner(t, &c[i], i == 0);
    if (t.flags & EffectsTransform::kRotated) state->normal = Rotated(t, state->normal);
    state->d = -static_cast<float>(nGZCompGeom::Dot87(&state->normal.x, &c[0].x));
}

bool cTSPlaneCollider::Collide(const void *context, const PlaneState *state, float radius, const Vector3 *from, Vector3 *to,
                               Vector3 *normal)
{
    const Vector3 move{Sub(to->x, from->x), Sub(to->y, from->y), Sub(to->z, from->z)};
    const float into = static_cast<float>(nGZCompGeom::Dot87(&move.x, &state->normal.x));
    if (into >= -1e-5f) return false; // (a NaN goes on)
    const float r = Mul(radius, *reinterpret_cast<const float *>(static_cast<const uint8_t *>(context) + 0x4c));
    // The height of `from` over the plane: the x87 dot product plus d, rounded at the x87's precision, then to a float.
    const float height = static_cast<float>(x87::Rounded(nGZCompGeom::Dot87(&state->normal.x, &from->x) +
                                                         static_cast<double>(state->d)));
    const float below = -height;
    const float t = Div(Add(below, r), into);
    if (t > 1.0f) return false;
    if (0.0f > t && 0.0f > Div(below, into)) return false;

    // Where the move meets the raised plane, brought back onto the plane along its normal.
    const Vector3 &n = state->normal;
    const Vector3 p{Sub(Add(Mul(move.x, t), from->x), Mul(n.x, r)), Sub(Add(Mul(t, move.y), from->y), Mul(n.y, r)),
                    Sub(Add(Mul(t, move.z), from->z), Mul(n.z, r))};

    // The corners relative to it, projected along the normal's largest coordinate (the first of equals from z down; a NaN
    // falls to the later choice).
    const float ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
    int drop;
    if (ax > ay)
        drop = ax > az ? 0 : 2;
    else
        drop = ay > az ? 1 : 2;
    float q[4][2];
    for (int i = 0; i < 4; ++i) {
        const Vector3 &c = state->corner[i];
        switch (drop) {
        case 0: q[i][0] = Sub(c.y, p.y); q[i][1] = Sub(c.z, p.z); break;
        case 1: q[i][0] = Sub(c.x, p.x); q[i][1] = Sub(c.z, p.z); break;
        default: q[i][0] = Sub(c.x, p.x); q[i][1] = Sub(c.y, p.y); break;
        }
    }
    // The edges crossed by the ray from the point along +u: those whose ends lie either side of v = 0 and meet it at u >= 0
    // (both ends at u >= 0, or the crossing computed when the edge's dv is over 1e-6).
    unsigned crossed = 0;
    for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) & 3;
        const float ui = q[i][0], vi = q[i][1], uj = q[j][0], vj = q[j][1];
        if (!(0.0f > vj * vi)) continue;
        if (ui >= 0.0f && uj >= 0.0f) {
            ++crossed;
            continue;
        }
        const float dv = vj - vi;
        if (!(std::fabs(dv) > 1e-6f)) continue;
        if (ui - (uj - ui) * vi / dv >= 0.0f) ++crossed;
    }
    if (!(crossed & 1)) return false;
    *normal = state->normal;
    const Vector3 hit{Add(Mul(move.x, t), from->x), Add(Mul(move.y, t), from->y), Add(Mul(move.z, t), from->z)};
    *to = hit;
    return true;
}

bool cTSPlaneCollider::Draw(uint32_t, const PlaneState *state, void **out)
{
    using namespace nGZSceneGraph;
    cIGeometryNode *node = nullptr;
    static_cast<cIGZCOM *>(GetDirectorFrameworkObject())
        ->GetClassObject(kGeometryNodeClass, cIGeometryNode::kIid, reinterpret_cast<void **>(&node));
    cIIndexedMeshBuilder *mesh = nullptr;
    static_cast<cIGZCOM *>(GetDirectorFrameworkObject())
        ->GetClassObject(kIndexedMeshBuilderClass, cIIndexedMeshBuilder::kIid, reinterpret_cast<void **>(&mesh));
    // Neither is checked: a class that fails to make its object faults here, in the original too.
    node->AddPart(mesh);
    mesh->Reset(false);
    const EffectsTransform &t = state->placement;
    for (int i = 0; i < 4; ++i) {
        Vector3 v = state->corner[i];
        if (t.flags & EffectsTransform::kRotated) v = Rotated(t, v);
        v = {Add(Mul(v.x, t.scale), t.translation.x), Add(Mul(v.y, t.scale), t.translation.y),
             Add(Mul(v.z, t.scale), t.translation.z)};
        mesh->AddPosition(&v.x);
    }
    mesh->SetGroupName("layer");
    mesh->MarkUnknown();
    for (uint32_t i = 0; i < 4; ++i) mesh->AddPositionIndex(i);
    mesh->CommitGroup();
    mesh->Finish();
    *out = node;
    node->AddRef();
    mesh->Release();
    node->Release();
    return true;
}

} // namespace world_plane
