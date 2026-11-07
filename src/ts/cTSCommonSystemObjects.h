#pragma once
// The objects cTSCommonSystem::Init makes (lane LF), in the standalone shape (docs/standalone-shape.md): the two expression
// functions RegisterExpressionFunctions registers with the material parser, and the five named objects Init hands the effects
// manager. The game's classes sit in the anonymous namespace 0x4c801c06; their objects are built only by Init 0xcba7c0 and
// RegisterExpressionFunctions 0xcbb420 (both ours), so every object carries our vtables and the game's bodies are reached by
// nothing (in effect through callers, under the common system's factory hook).
#include "gz/ExpressionFunctionBase.h"
#include "rz/RZUnknownBase.h"
#include "ts/EffectsTransform.h"

#include <cstdint>

namespace common_system {

// ---- the expression functions (0x10 bytes; gzexpr::cFunctionT) ----

// "tsIsDay": 1 unless the effects manager's debug bit 0 is clear (vtables 0xeb7cec / 0xeb7cfc).
class cIsDayFunction : public gzexpr::cFunctionT {
public:
    gzexpr::Value Evaluate(const char **cursor, void *parser) override; // 0xcb9510
    const char *GetHelp() override;                                     // 0xcb9ce0
    const char *GetName() override;                                     // 0xcbadf0
};

// "tsIsLightMaterial": ("name"): whether the text from the name on contains "lighting" (vtables 0xeb7d44 / 0xeb7d54).
class cIsLightMaterialFunction : public gzexpr::cFunctionT {
public:
    gzexpr::Value Evaluate(const char **cursor, void *parser) override; // 0xcb9590
    const char *GetHelp() override;                                     // 0xcb9cf0
    const char *GetName() override;                                     // 0xcbae00
};

// ---- the effects' named objects (0xc bytes: the interface at +0, a cRZUnknown at +4) ----

using fx::Add;
using fx::Div;
using fx::EffectsTransform;
using fx::Mul;
using fx::RowFromX;
using fx::RowFromY;
using fx::Sub;
using fx::Vector3;

// cITSEffectsConstraint (vtable 0xdbd2dc, 4 slots).
class cITSEffectsConstraint : public cIGZUnknown {
public:
    // 3 (ret 0x10): `point` (in the effect's space) moved by the constraint; `offset` is a world-space offset.
    virtual void Apply(void *context, const EffectsTransform *transform, Vector3 *point, const Vector3 *offset) = 0;
};

// cITSEffectsCollider (vtable 0xdbd2f0, 7 slots).
class cITSEffectsCollider : public cIGZUnknown {
public:
    // 3: the size of the state slot 4 copies (a transform).
    virtual uint32_t GetStateSize() = 0;
    // 4 (ret 0x10): `from` copied to `to` (0x38 bytes); the first two arguments are not used by the world colliders.
    virtual void CopyState(void *a, void *b, const EffectsTransform *from, EffectsTransform *to) = 0;
    // 5 (ret 0x18): a particle moving from `from` to `to` (both in the effect's space) against the world: on a hit, `to`
    // is the corrected point and `normal` the surface's (both in the effect's space) and the result true; else both
    // untouched. `radius` times the context's float at +0x4c is the world test's distance.
    virtual bool Collide(const void *context, const EffectsTransform *transform, float radius, const Vector3 *from, Vector3 *to,
                         Vector3 *normal) = 0;
    // 6 (ret 0xc)
    virtual bool Slot6(uint32_t, uint32_t, uint32_t) = 0;
};

// Pins a point to the floor: its x and y moved by the offset in world space, its height the offset's (vtables 0xeb7bb0 /
// 0xeb7bc4).
class cPinToFloorConstraint : public cITSEffectsConstraint, public rz::cRZUnknown {
public:
    bool QueryInterface(uint32_t riid, void **out) override; // 0x1e2e00 (+4 thunk 0x356874)
    uint32_t AddRef() override;                              // 0x1dab20 (0x3545d3)
    uint32_t Release() override;                             // 0x1e3970 (0x356902)
    void Apply(void *context, const EffectsTransform *transform, Vector3 *point, const Vector3 *offset) override; // 0xcb9640
};

// The same, under another name (vtables 0xeb7be0 / 0xeb7bf4: every slot cPinToFloorConstraint's).
class cPinToTopsConstraint : public cPinToFloorConstraint {};

// The world colliders' base: Collide takes both points to the world, asks CollideWorld (slot 7), and brings the result back.
class cTSWorldSpaceColliderBase : public cITSEffectsCollider, public rz::cRZUnknown {
public:
    bool QueryInterface(uint32_t riid, void **out) override; // 0x1e2e00 (+4 thunk 0x356874)
    uint32_t AddRef() override;                              // 0x1dab20 (0x3545d3)
    uint32_t Release() override;                             // 0x1e3970 (0x356902)
    uint32_t GetStateSize() override;                                                         // 0x3a3c20
    void CopyState(void *a, void *b, const EffectsTransform *from, EffectsTransform *to) override; // 0x3a35b0 eax-ignored: scratch of the copy, no value
    bool Collide(const void *context, const EffectsTransform *transform, float radius, const Vector3 *from, Vector3 *to,
                 Vector3 *normal) override;                                                   // 0xcb97a0
    bool Slot6(uint32_t, uint32_t, uint32_t) override;                                        // 0x1474a0
    // 7 (ret 0x10): the world test on world-space points: `to` corrected and `normal` written on a hit.
    virtual bool CollideWorld(const Vector3 *from, Vector3 *to, float distance, Vector3 *normal) = 0;
};

// The floor (height `distance`) and the ceiling (3 - `distance`), while the "floorCollisions" setting is on (vtables 0xeb7c6c /
// 0xeb7c90).
class cFloorsCollider : public cTSWorldSpaceColliderBase {
public:
    bool CollideWorld(const Vector3 *from, Vector3 *to, float distance, Vector3 *normal) override; // 0xcb9ab0
};

// The walls (y, then x, at `distance`), while the "wallCollisions" setting is on (vtables 0xeb7cac / 0xeb7cd0).
class cWallsCollider : public cTSWorldSpaceColliderBase {
public:
    bool CollideWorld(const Vector3 *from, Vector3 *to, float distance, Vector3 *normal) override; // 0xcb9c70
};

// Floors first, then walls (vtables 0xeb7c2c / 0xeb7c50).
class cWallsAndFloorsCollider : public cTSWorldSpaceColliderBase {
public:
    bool CollideWorld(const Vector3 *from, Vector3 *to, float distance, Vector3 *normal) override; // 0xcb9b60
};

static_assert(sizeof(cPinToFloorConstraint) == 0xc && sizeof(cFloorsCollider) == 0xc, "new 0xc in Init");
static_assert(sizeof(cIsDayFunction) == 0x10, "new 0x10 in RegisterExpressionFunctions");

} // namespace common_system
