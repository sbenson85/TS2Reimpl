// The effects' world-space objects (lane YA, ran as RZ). Layout and slots: src/world/WorldColliders.h. Built for speed (/O2).
#include "world/WorldColliders.h"

#include "compgeom/Rigid.h"
#include "game/original.h"
#include "game/vtable.h"
#include "misc/X87.h"
#include "ts/cTSEffectsManager.h"
#include "world/WorldHeights.h"
#include "world/WorldService.h"

#include <new>

namespace world_colliders {

namespace {

using fx::Add;
using fx::Div;
using fx::Mul;
using fx::RowFromX;
using fx::RowFromY;
using fx::Sub;

constexpr uint8_t kRotated = 2;

// The floor's normal (the game's .data 0x100b650, which nothing writes) and the ceiling's, its sign bits flipped.
constexpr Vector3 kUp{0.0f, 0.0f, 1.0f};
constexpr Vector3 kDown{-0.0f, -0.0f, -1.0f};

// The rotation applied (rows: rotation * p), every row from y.
Vector3 Rotate(const EffectsTransform &t, const Vector3 &p)
{
    return {RowFromY(p.x, p.y, p.z, t.m[0][0], t.m[0][1], t.m[0][2]), RowFromY(p.x, p.y, p.z, t.m[1][0], t.m[1][1], t.m[1][2]),
            RowFromY(p.x, p.y, p.z, t.m[2][0], t.m[2][1], t.m[2][2])};
}

// Its inverse (columns: transpose * p); x from y or from x as `xFromX` says.
Vector3 RotateBack(const EffectsTransform &t, const Vector3 &p, bool xFromX)
{
    const float x = xFromX ? RowFromX(p.x, p.y, p.z, t.m[0][0], t.m[1][0], t.m[2][0])
                           : RowFromY(p.x, p.y, p.z, t.m[0][0], t.m[1][0], t.m[2][0]);
    return {x, RowFromY(p.x, p.y, p.z, t.m[0][1], t.m[1][1], t.m[2][1]), RowFromY(p.x, p.y, p.z, t.m[0][2], t.m[1][2], t.m[2][2])};
}

// Scaled and moved (after the rotation).
Vector3 Place(const EffectsTransform &t, const Vector3 &p)
{
    return {Add(Mul(p.x, t.scale), t.translation.x), Add(Mul(p.y, t.scale), t.translation.y),
            Add(Mul(p.z, t.scale), t.translation.z)};
}

// The effect's point in the world.
Vector3 ToWorld(const EffectsTransform &t, Vector3 p)
{
    if (t.flags & kRotated) p = Rotate(t, p);
    return Place(t, p);
}

// A world point plus the offset, back in the effect's space: the translation off, the scale divided out unless 1, the
// rotation undone (every row from y). The pins' common tail.
void PinBack(const EffectsTransform &t, float x, float y, float z, const Vector3 *offset, Vector3 *point)
{
    Vector3 v{Sub(Add(x, offset->x), t.translation.x), Sub(Add(y, offset->y), t.translation.y),
              Sub(Add(z, offset->z), t.translation.z)};
    const float scale = t.scale;
    if (scale != 1.0f) v = {Div(v.x, scale), Div(v.y, scale), Div(v.z, scale)};
    if (t.flags & kRotated) v = RotateBack(t, v, false);
    *point = v;
}

// A point's tile on a level: {x, y, level} as WithLevelOfGround / WithLevelAboveGround leave it.
struct Located {
    int32_t words[3];
    const float *Floats() const { return reinterpret_cast<const float *>(words); }
    float Height() const { return static_cast<float>(worldheights::GroundHeightAt(reinterpret_cast<const nTSWorld::WorldPoint *>(words))); }
    bool HasFloor() const { return worldheights::HasFloorOrGroundAt(Floats(), true); }
};
Located OnGround(const Vector3 &p)
{
    Located l;
    worldheights::WithLevelOfGround(l.words, &p.x);
    return l;
}
Located AboveGround(const Vector3 &p)
{
    Located l;
    worldheights::WithLevelAboveGround(l.words, &p.x);
    return l;
}

// The world's outside support (the world DB's slot 100).
void *OutsideSupport() { return worldsvc::CachedWorldDB()->GetOutsideSupport(); }

// The x and y of `from` to `to` at fraction t, as the original sums them: (to - from) t + from.
void Lerp(const Vector3 *from, Vector3 *to, float t)
{
    const float x = Add(Mul(Sub(to->x, from->x), t), from->x);
    const float y = Add(Mul(Sub(to->y, from->y), t), from->y);
    to->x = x;
    to->y = y;
}

// The floor at `to`, from the ground's level: true (to.z on it, the normal up) when to.z is no lower than `distance` above
// it and there is a floor.
bool LandOnFloor(float distance, Vector3 *to, Vector3 *normal)
{
    const Located m = OnGround(*to);
    const float g = m.Height();
    if (!(g >= Sub(to->z, distance)) || !m.HasFloor()) return false;
    to->z = Add(g, distance);
    *normal = kUp;
    return true;
}

// The ceiling over `to`, from the level above the ground: true (to.z under it, the normal down) when to.z is no higher than
// `distance` below it and there is a floor there.
bool StopAtCeiling(float distance, Vector3 *to, Vector3 *normal)
{
    const Located m = AboveGround(*to);
    const float c = m.Height();
    if (!(Add(distance, to->z) >= c) || !m.HasFloor()) return false;
    to->z = Sub(c, distance);
    *normal = kDown;
    return true;
}

// The wall object of the placement service (0x20de97, not ours yet: the service cached in game global 0x10cc2b0, else
// 0x20e0b2's slot 39), its slot 40 with 3; null without a service.
void *WallObject()
{
    void *service = OriginalFunction<void *(__cdecl *)(), 0x20de97>()();
    return service ? RawSlot<40, void *>(service, 3) : nullptr;
}

// A new object as its +0 face, null when new failed.
template <class Object, class Face> cIGZUnknown *New() { return static_cast<Face *>(new (std::nothrow) Object); }

} // namespace

// 0x3a3c30
void __cdecl RegisterWorldObjects(cITSEffectsManager *manager)
{
    manager->SetNamedB("pinToFloor", New<cPinToFloorConstraint, cITSEffectsConstraint>());
    manager->SetNamedB("pinToTops", New<cPinToTopsConstraint, cITSEffectsConstraint>());
    manager->SetNamedB("pinToPools", New<cPinToPoolsConstraint, cITSEffectsConstraint>());
    manager->SetNamedA("wallsAndFloors", New<cWallsAndFloorsCollider, cITSEffectsCollider>());
    manager->SetNamedA("walls", New<cWallsCollider, cITSEffectsCollider>());
    manager->SetNamedA("floors", New<cFloorsCollider, cITSEffectsCollider>());
}

// ---- the constraints ----

bool cWorldConstraintBase::QueryInterface(uint32_t riid, void **out) { return rz::cRZUnknown::QueryInterface(riid, out); }
uint32_t cWorldConstraintBase::AddRef() { return rz::cRZUnknown::AddRef(); }
uint32_t cWorldConstraintBase::Release() { return rz::cRZUnknown::Release(); }

void cPinToFloorConstraint::Apply(void *context, const EffectsTransform *transform, Vector3 *point, const Vector3 *offset)
{
    ApplyBody(context, transform, point, offset);
}

// 0x3a2b00: the world point's x and y, the ground's height under it.
void cPinToFloorConstraint::ApplyBody(void *, const EffectsTransform *transform, Vector3 *point, const Vector3 *offset)
{
    const EffectsTransform &t = *transform;
    const Vector3 p = ToWorld(t, *point);
    const float ground = OnGround(p).Height();
    PinBack(t, p.x, p.y, ground, offset, point);
}

void cPinToTopsConstraint::Apply(void *context, const EffectsTransform *transform, Vector3 *point, const Vector3 *offset)
{
    ApplyBody(context, transform, point, offset);
}

// 0x3a2f80: the outside support's point (its slot 17 fills it).
void cPinToTopsConstraint::ApplyBody(void *, const EffectsTransform *transform, Vector3 *point, const Vector3 *offset)
{
    Vector3 top;
    RawSlot<17, void>(OutsideSupport(), &top);
    PinBack(*transform, top.x, top.y, top.z, offset, point);
}

void cPinToPoolsConstraint::Apply(void *context, const EffectsTransform *transform, Vector3 *point, const Vector3 *offset)
{
    ApplyBody(context, transform, point, offset);
}

// 0x3a2d20: the world point, adjusted in place by the outside support's slot 18.
void cPinToPoolsConstraint::ApplyBody(void *, const EffectsTransform *transform, Vector3 *point, const Vector3 *offset)
{
    const EffectsTransform &t = *transform;
    Vector3 p = ToWorld(t, *point);
    RawSlot<18, void>(OutsideSupport(), &p);
    PinBack(t, p.x, p.y, p.z, offset, point);
}

// ---- the colliders ----

bool cTSWorldSpaceColliderBase::Collide(const void *context, const EffectsTransform *transform, float radius, const Vector3 *from,
                                        Vector3 *to, Vector3 *normal)
{
    return CollideBody(context, transform, radius, from, to, normal);
}

// 0x3a30f0: both points to the world, the distance half the context's float at +0x4c times the radius; on a hit the corrected
// point back through the transform and the normal turned back (its x summed from its x term).
bool cTSWorldSpaceColliderBase::CollideBody(const void *context, const EffectsTransform *transform, float radius,
                                            const Vector3 *from, Vector3 *to, Vector3 *normal)
{
    const EffectsTransform &t = *transform;
    Vector3 start = ToWorld(t, *from);
    Vector3 end = ToWorld(t, *to);
    const float size = *reinterpret_cast<const float *>(static_cast<const uint8_t *>(context) + 0x4c);
    const float distance = Mul(Mul(size, 0.5f), radius);
    Vector3 hit;
    if (!CollideWorld(&start, &end, distance, &hit)) return false;
    transform->ToLocal(&end);
    if (t.flags & kRotated) hit = RotateBack(t, hit, true);
    *to = end;
    *normal = hit;
    return true;
}

// 0x3a39e0: the wall object's slot 21 (a sphere of `distance` at a point) and slot 19 (the segment between two points), each
// filling the hit point and the normal. Tried in turn: the sphere at `from` (taken only when the move does not leave the wall:
// the move's dot product with the normal at most 0), the segment, the sphere at `to`. The result lies off the hit along the
// normal by `distance` plus the margin, at the height of `from` (or of `to` for the last).
bool cTSWorldSpaceColliderBase::CollideWalls(const Vector3 *from, float distance, Vector3 *to, Vector3 *normal)
{
    void *walls = WallObject();
    if (!walls) return false;
    const Located a = OnGround(*from);
    const Located b = OnGround(*to);
    Vector3 hit{-1.0f, -1.0f, 0.0f};
    Vector3 scratch{-1.0f, -1.0f, 0.0f};
    const auto moveOff = [&](float baseZ) {
        const float s = Add(distance, mMargin);
        const Vector3 n = *normal;
        const float z = Add(Mul(n.z, s), baseZ);
        to->x = Add(Mul(n.x, s), hit.x);
        to->y = Add(Mul(n.y, s), hit.y);
        to->z = z;
        return true;
    };
    if (distance > 0.0f && RawSlot<21, bool>(walls, a.words, distance, &scratch, &hit, normal)) {
        const Vector3 move{Sub(to->x, from->x), Sub(to->y, from->y), Sub(to->z, from->z)};
        const float dot = static_cast<float>(nGZCompGeom::Dot87(&move.x, &normal->x));
        if (dot <= 0.0f) return moveOff(from->z);
    }
    if (RawSlot<19, bool>(walls, a.words, b.words, &scratch, &hit, normal)) return moveOff(from->z);
    if (distance > 0.0f && RawSlot<21, bool>(walls, b.words, distance, &scratch, &hit, normal)) return moveOff(to->z);
    return false;
}

// 0x3a35e0. Falling (from above to): the floor `distance` over the ground under `from` (on its level); a move that crosses it
// stops on it (x and y at the crossing, measured from the higher of `from` and the ground), one that starts at or below it
// is put on it straight under `from`. Rising: the same against the ceiling `distance` under the ground of the level above.
// Level (or NaN): the floor under `to`. Whatever misses tries the other surface at `to`: the ceiling after the floor, the
// floor after the ceiling.
bool __stdcall CollideFloors(const Vector3 *from, float distance, Vector3 *to, Vector3 *normal)
{
    const float fromZ = from->z;
    const float toZ = to->z;
    if (fromZ > toZ) {
        const Located l = OnGround(*from);
        const float g = l.Height();
        const float top = g > fromZ ? g : fromZ;
        const float floorZ = Add(g, distance);
        if (top > floorZ) {
            if (floorZ >= toZ && l.HasFloor()) {
                if (floorZ > toZ) {
                    Lerp(from, to, Div(Sub(floorZ, top), Sub(toZ, top)));
                    to->z = floorZ;
                }
                *normal = kUp;
                return true;
            }
        } else if (l.HasFloor()) {
            to->x = from->x;
            to->y = from->y;
            to->z = floorZ;
            *normal = kUp;
            return true;
        }
        return StopAtCeiling(distance, to, normal);
    }
    if (toZ > fromZ) {
        const Located l = AboveGround(*from);
        const float c = l.Height();
        const float bottom = c < fromZ ? c : fromZ;
        const float ceilingZ = Sub(c, distance);
        if (ceilingZ > bottom) {
            if (toZ >= ceilingZ && l.HasFloor()) {
                if (toZ > ceilingZ) {
                    Lerp(from, to, Div(Sub(ceilingZ, bottom), Sub(toZ, bottom)));
                    to->z = ceilingZ;
                }
                *normal = kDown;
                return true;
            }
        } else if (l.HasFloor()) {
            to->x = from->x;
            to->y = from->y;
            to->z = ceilingZ;
            *normal = kDown;
            return true;
        }
        return LandOnFloor(distance, to, normal);
    }
    const Located l = OnGround(*to);
    // The ground's x87 result plus the distance, rounded once to the x87's precision and once to a float.
    const float floorZ = static_cast<float>(x87::Rounded(worldheights::GroundHeightAt(reinterpret_cast<const nTSWorld::WorldPoint *>(l.words)) +
                                                         static_cast<double>(distance)));
    if (floorZ >= toZ && l.HasFloor()) {
        to->z = floorZ;
        *normal = kUp;
        return true;
    }
    return StopAtCeiling(distance, to, normal);
}

bool cFloorsCollider::CollideWorld(const Vector3 *from, Vector3 *to, float distance, Vector3 *normal)
{
    return CollideWorldBody(from, to, distance, normal);
}

// 0x3a3410
bool cFloorsCollider::CollideWorldBody(const Vector3 *from, Vector3 *to, float distance, Vector3 *normal)
{
    return CollideFloors(from, distance, to, normal);
}

bool cWallsCollider::CollideWorld(const Vector3 *from, Vector3 *to, float distance, Vector3 *normal)
{
    return CollideWorldBody(from, to, distance, normal);
}

// 0x3a3490
bool cWallsCollider::CollideWorldBody(const Vector3 *from, Vector3 *to, float distance, Vector3 *normal)
{
    return CollideWalls(from, distance, to, normal);
}

bool cWallsAndFloorsCollider::CollideWorld(const Vector3 *from, Vector3 *to, float distance, Vector3 *normal)
{
    return CollideWorldBody(from, to, distance, normal);
}

// 0x3a3430
bool cWallsAndFloorsCollider::CollideWorldBody(const Vector3 *from, Vector3 *to, float distance, Vector3 *normal)
{
    // ORIGINAL-BUG: true only when both hit (the walls tried only after a floor hit, 0x3a3452), where the common system's
    // sibling 0xcb9b60 tries the walls when the floors miss.
    return CollideFloors(from, distance, to, normal) && CollideWalls(from, distance, to, normal);
}

} // namespace world_colliders
