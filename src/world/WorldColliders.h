#pragma once
// The effects' world-space objects (lane YA, ran as RZ): the three pin constraints and the three world colliders of the
// anonymous namespace 0x65722b7a, which the scene-graph system's manager set-up (cTSSGSystem::CreateManagers 0x24d260, ours)
// hands the effects manager through the registrar 0x3a3c30. The registrar is hooked and makes these classes, so every object
// of the family carries our vtables; the slot bodies are hooked at their RVAs as well (each is referenced only by this
// family's vtables, the folded GetStateSize / CopyState / slot 6 bodies excepted: those stay the bases' and are not hooked).
//
// The interfaces are the effects manager's (cITSEffectsConstraint 0xdbd2dc, cITSEffectsCollider 0xdbd2f0), declared with the
// common system's objects (src/ts/cTSCommonSystemObjects.h, lane LF), whose 0x4c801c06 classes are this family's
// world-less siblings.
#include "rz/RZUnknownBase.h"
#include "ts/EffectsTransform.h"
#include "ts/cTSCommonSystemObjects.h"

#include <cstdint>

class cITSEffectsManager;

namespace world_colliders {

using common_system::cITSEffectsCollider;
using common_system::cITSEffectsConstraint;
using fx::EffectsTransform;
using fx::Vector3;

// 0x3a3c30 (__cdecl, the effects manager): makes the six objects (each with a zero count, null if new failed) and names them:
// the constraints "pinToFloor", "pinToTops", "pinToPools" through slot 29, the colliders "wallsAndFloors", "walls",
// "floors" through slot 27.
void __cdecl RegisterWorldObjects(cITSEffectsManager *manager);

// ---- the constraints (0xc bytes: the interface at +0, a cRZUnknown at +4) ----

class cWorldConstraintBase : public cITSEffectsConstraint, public rz::cRZUnknown {
public:
    bool QueryInterface(uint32_t riid, void **out) override; // 0x1e2e00 (+4 thunk 0x356874)
    uint32_t AddRef() override;                              // 0x1dab20 (0x3545d3)
    uint32_t Release() override;                             // 0x1e3970 (0x356902)
};

// Pins a point to the ground: the point taken to the world, its height the ground's under it (on the ground's level), the
// offset added, and the result brought back into the effect's space (vtables 0xdbd310 / 0xdbd324).
class cPinToFloorConstraint : public cWorldConstraintBase {
public:
    void Apply(void *context, const EffectsTransform *transform, Vector3 *point, const Vector3 *offset) override;
    void ApplyBody(void *context, const EffectsTransform *transform, Vector3 *point, const Vector3 *offset); // 0x3a2b00
};

// Pins a point to the top the world's outside support gives (its slot 17 fills a point; the constraint's own point is not
// read), plus the offset (vtables 0xdbd340 / 0xdbd354).
class cPinToTopsConstraint : public cWorldConstraintBase {
public:
    void Apply(void *context, const EffectsTransform *transform, Vector3 *point, const Vector3 *offset) override;
    void ApplyBody(void *context, const EffectsTransform *transform, Vector3 *point, const Vector3 *offset); // 0x3a2f80
};

// Pins a point to the pools: the world point adjusted in place by the outside support's slot 18, plus the offset (vtables
// 0xdbd370 / 0xdbd384).
class cPinToPoolsConstraint : public cWorldConstraintBase {
public:
    void Apply(void *context, const EffectsTransform *transform, Vector3 *point, const Vector3 *offset) override;
    void ApplyBody(void *context, const EffectsTransform *transform, Vector3 *point, const Vector3 *offset); // 0x3a2d20
};

// ---- the colliders (0x10 bytes: the interface at +0, a cRZUnknown at +4, the wall margin at +0xc) ----

// The world colliders' base (+4 vtable 0xdbd3a0 while constructing): Collide takes both points to the world, asks
// CollideWorld (slot 7) and brings the result back. The unknown's slots, GetStateSize (0x3a3c20), CopyState (0x3a35b0) and
// slot 6 (0x1474a0) are the common system's base's (the same folded bodies; not hooked), CollideWorld (7) stays pure.
class cTSWorldSpaceColliderBase : public common_system::cTSWorldSpaceColliderBase {
public:
    bool Collide(const void *context, const EffectsTransform *transform, float radius, const Vector3 *from, Vector3 *to,
                 Vector3 *normal) override;
    // 0x3a30f0 (ret 0x18): the body of Collide.
    bool CollideBody(const void *context, const EffectsTransform *transform, float radius, const Vector3 *from, Vector3 *to,
                     Vector3 *normal);

    // 0x3a39e0 (ret 0x10): the walls of the placement service's wall object (its slot 40 with 3): a hit is moved off the
    // wall along its normal by `distance` plus the margin.
    bool CollideWalls(const Vector3 *from, float distance, Vector3 *to, Vector3 *normal);

    float mMargin = 0.001f; // +0xc
};

// 0x3a35e0 (__stdcall in effect: `this` is not read; ret 0x10): the floor under and the ceiling over the move from `from` to
// `to`, `distance` off them.
bool __stdcall CollideFloors(const Vector3 *from, float distance, Vector3 *to, Vector3 *normal);

// The floors and ceilings (vtables 0xdbd43c / 0xdbd460).
class cFloorsCollider : public cTSWorldSpaceColliderBase {
public:
    bool CollideWorld(const Vector3 *from, Vector3 *to, float distance, Vector3 *normal) override;
    bool CollideWorldBody(const Vector3 *from, Vector3 *to, float distance, Vector3 *normal); // 0x3a3410
};

// The walls (vtables 0xdbd3fc / 0xdbd420).
class cWallsCollider : public cTSWorldSpaceColliderBase {
public:
    bool CollideWorld(const Vector3 *from, Vector3 *to, float distance, Vector3 *normal) override;
    bool CollideWorldBody(const Vector3 *from, Vector3 *to, float distance, Vector3 *normal); // 0x3a3490
};

// Floors, then walls (vtables 0xdbd3bc / 0xdbd3e0).
class cWallsAndFloorsCollider : public cTSWorldSpaceColliderBase {
public:
    bool CollideWorld(const Vector3 *from, Vector3 *to, float distance, Vector3 *normal) override;
    bool CollideWorldBody(const Vector3 *from, Vector3 *to, float distance, Vector3 *normal); // 0x3a3430
};

static_assert(sizeof(cPinToFloorConstraint) == 0xc && sizeof(cFloorsCollider) == 0x10, "new 0xc / 0x10 in the registrar");

} // namespace world_colliders
