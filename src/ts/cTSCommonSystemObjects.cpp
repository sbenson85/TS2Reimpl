// The objects cTSCommonSystem::Init makes (lane LF). Layout and slots: src/ts/cTSCommonSystemObjects.h.
#include "ts/cTSCommonSystemObjects.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/ExpressionEvaluator.h"
#include "ts/cTSCommonSystem.h"   // cITSCommonSystem
#include "ts/cTSEffectsManager.h" // cITSEffectsManager

#include <cstring>

namespace common_system {

namespace {

// The value of a test: 1 or 0 as a number.
gzexpr::Value Truth(bool on) { return gzexpr::Value{{static_cast<float>(on ? 1 : 0), 0.0f, 0.0f, 0.0f}, 1}; }

// 0xb38f60 (__cdecl, not ours yet): the common system service as cITSCommonSystem (cached in a game static).
cITSCommonSystem *CommonSystem() { return OriginalFunction<cITSCommonSystem *(__cdecl *)(), 0xb38f60>()(); }

using nGZCommandParser::QuotedName;

// 0xb096d0 (ours: gz/ExpressionEvaluator.h): white space skipped, then `ch` expected and passed (a parser error thrown
// otherwise).
void ExpectChar(const char **cursor, char ch) { nGZCommandParser::ExpectChar(cursor, ch); }

// 0xb0a580 (ours; `this` not used, so the parser stands in for the evaluator): a quoted name read into `out` (an error
// thrown without the closing quote).
void ReadQuotedName(void *parser, QuotedName *out, const char **cursor)
{
    reinterpret_cast<nGZCommandParser::ExpressionEvaluator *>(parser)->ReadQuotedName(out, cursor);
}

// The two collision settings' values: the value bytes (+4) of the game's AnyBoolean statics 0x1087514 / 0x108751c, which
// cTSCommonSystem keeps.
bool FloorCollisions() { return *GameAddress<uint8_t>(0x1087518) != 0; }
bool WallCollisions() { return *GameAddress<uint8_t>(0x1087520) != 0; }

// The axes the colliders answer as normals (the game's .data 0x1086ef8, 0x1086f04, 0x1086f10, which only they read).
constexpr Vector3 kXAxis{1.0f, 0.0f, 0.0f};
constexpr Vector3 kYAxis{0.0f, 1.0f, 0.0f};
constexpr Vector3 kZAxis{0.0f, 0.0f, 1.0f};
constexpr float kCeiling = 3.0f;

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

constexpr uint8_t kRotated = 2;

bool Floors(Vector3 *to, float distance, Vector3 *normal)
{
    if (distance > to->z) {
        to->z = distance;
        *normal = kZAxis;
        return true;
    }
    const float ceiling = kCeiling - distance;
    if (to->z > ceiling) {
        to->z = ceiling;
        *normal = Vector3{-kZAxis.x, -kZAxis.y, -kZAxis.z};
        return true;
    }
    return false;
}

bool Walls(Vector3 *to, float distance, Vector3 *normal)
{
    if (distance > to->y) {
        to->y = distance;
        *normal = kYAxis;
        return true;
    }
    if (distance > to->x) {
        to->x = distance;
        *normal = kXAxis;
        return true;
    }
    return false;
}

} // namespace

// ---- the expression functions ----

// 0xcb9510
gzexpr::Value cIsDayFunction::Evaluate(const char **, void *)
{
    bool day = true;
    if (CommonSystem() && CommonSystem()->GetEffectsManager())
        day = As<cITSEffectsManager>(CommonSystem()->GetEffectsManager())->GetDebugBit(0);
    return Truth(day);
}

// 0xcb9ce0
const char *cIsDayFunction::GetHelp() { return "Returns whether its day or not"; }

// 0xcbadf0
const char *cIsDayFunction::GetName() { return "tsIsDay"; }

// 0xcb9590
gzexpr::Value cIsLightMaterialFunction::Evaluate(const char **cursor, void *parser)
{
    ExpectChar(cursor, '(');
    QuotedName name;
    ReadQuotedName(parser, &name, cursor);
    ExpectChar(cursor, ')');
    // ORIGINAL-BUG: the name is not terminated: the search runs on past the closing quote to the end of the expression.
    return Truth(std::strstr(name.text, "lighting") != nullptr);
}

// 0xcb9cf0
const char *cIsLightMaterialFunction::GetHelp() { return "Returns whether the material passed is lighting material or not"; }

// 0xcbae00
const char *cIsLightMaterialFunction::GetName() { return "tsIsLightMaterial"; }

// ---- the constraints ----

bool cPinToFloorConstraint::QueryInterface(uint32_t riid, void **out) { return rz::cRZUnknown::QueryInterface(riid, out); }
uint32_t cPinToFloorConstraint::AddRef() { return rz::cRZUnknown::AddRef(); }
uint32_t cPinToFloorConstraint::Release() { return rz::cRZUnknown::Release(); }

// 0xcb9640: the point's x and y rotated and placed (its height not: the offset's is the height), moved by the offset, and
// brought back (the translation subtracted, the scale divided, the rotation undone).
void cPinToFloorConstraint::Apply(void *, const EffectsTransform *transform, Vector3 *point, const Vector3 *offset)
{
    const EffectsTransform &t = *transform;
    Vector3 p = *point;
    const bool rotated = (t.flags & kRotated) != 0;
    if (rotated) {
        const Vector3 turned = Rotate(t, p);
        p.x = turned.x;
        p.y = turned.y;
    }
    const float scale = t.scale;
    Vector3 local{Sub(Add(Add(Mul(p.x, scale), t.translation.x), offset->x), t.translation.x),
                  Sub(Add(Add(Mul(p.y, scale), t.translation.y), offset->y), t.translation.y),
                  Sub(Add(offset->z, 0.0f), t.translation.z)};
    if (scale != 1.0f) {
        local.z = Div(local.z, scale);
        local.y = Div(local.y, scale);
        local.x = Div(local.x, scale);
    }
    if (rotated) local = RotateBack(t, local, true); // x summed from its x term here
    *point = local;
}

// ---- the colliders ----

bool cTSWorldSpaceColliderBase::QueryInterface(uint32_t riid, void **out) { return rz::cRZUnknown::QueryInterface(riid, out); }
uint32_t cTSWorldSpaceColliderBase::AddRef() { return rz::cRZUnknown::AddRef(); }
uint32_t cTSWorldSpaceColliderBase::Release() { return rz::cRZUnknown::Release(); }

// 0x3a3c20
uint32_t cTSWorldSpaceColliderBase::GetStateSize() { return sizeof(EffectsTransform); }

// 0x3a35b0
void cTSWorldSpaceColliderBase::CopyState(void *, void *, const EffectsTransform *from, EffectsTransform *to) { *to = *from; }

// 0xcb97a0
bool cTSWorldSpaceColliderBase::Collide(const void *context, const EffectsTransform *transform, float radius, const Vector3 *from,
                                        Vector3 *to, Vector3 *normal)
{
    const EffectsTransform &t = *transform;
    Vector3 start = *from;
    Vector3 end = *to;
    const bool rotated = (t.flags & kRotated) != 0;
    if (rotated) start = Rotate(t, start);
    start = Place(t, start);
    if (rotated) { // Rotate's, but x summed from its x term
        const Vector3 turned = Rotate(t, end);
        end = {RowFromX(end.x, end.y, end.z, t.m[0][0], t.m[0][1], t.m[0][2]), turned.y, turned.z};
    }
    end = Place(t, end);
    const float distance = Mul(radius, *reinterpret_cast<const float *>(static_cast<const uint8_t *>(context) + 0x4c));
    Vector3 hit;
    if (!CollideWorld(&start, &end, distance, &hit)) return false;
    transform->ToLocal(&end);
    if (t.flags & kRotated) hit = RotateBack(t, hit, false);
    *to = end;
    *normal = hit;
    return true;
}

// 0x1474a0
bool cTSWorldSpaceColliderBase::Slot6(uint32_t, uint32_t, uint32_t) { return false; }

// 0xcb9ab0
bool cFloorsCollider::CollideWorld(const Vector3 *, Vector3 *to, float distance, Vector3 *normal)
{
    return FloorCollisions() && Floors(to, distance, normal);
}

// 0xcb9c70
bool cWallsCollider::CollideWorld(const Vector3 *, Vector3 *to, float distance, Vector3 *normal)
{
    return WallCollisions() && Walls(to, distance, normal);
}

// 0xcb9b60
bool cWallsAndFloorsCollider::CollideWorld(const Vector3 *, Vector3 *to, float distance, Vector3 *normal)
{
    if (FloorCollisions() && Floors(to, distance, normal)) return true;
    return WallCollisions() && Walls(to, distance, normal);
}

} // namespace common_system
