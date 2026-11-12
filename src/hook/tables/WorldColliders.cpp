#include "hook/hooks.h"
#include "world/WorldColliders.h"
#include "world/WorldPlaneCollider.h"

namespace {

using namespace world_colliders;

// 0x3a30f0 is shared_vf_3a30f0, but every reference to it is slot 5 of this family's three collider vtables (0xdbd3bc,
// 0xdbd3fc, 0xdbd43c), which only the registrar 0x3a3c30 (hooked here) installs: hooked under the one-family rule, CRC by
// tools/hookcrc.py.
const HookSpec kHooks[] = {
    {"cTSSGSystem_RegisterWorldColliders", 0x3a3c30, 32, 0xef42be14, reinterpret_cast<void *>(&RegisterWorldObjects)},
    {"AUcPinToFloorConstraint_A0x65722b7a_vf3", 0x3a2b00, 32, 0xe348363e, MemberAddress<&cPinToFloorConstraint::ApplyBody>()},
    {"AUcPinToPoolsConstraint_A0x65722b7a_vf3", 0x3a2d20, 32, 0xe348363e, MemberAddress<&cPinToPoolsConstraint::ApplyBody>()},
    {"AUcPinToTopsConstraint_A0x65722b7a_vf3", 0x3a2f80, 32, 0xe590d512, MemberAddress<&cPinToTopsConstraint::ApplyBody>()},
    {"cTSWorldSpaceColliderBase_A0x65722b7a_Collide", 0x3a30f0, 32, 0x3b2daf55, MemberAddress<&cTSWorldSpaceColliderBase::CollideBody>()},
    {"AVcFloorsCollider_A0x65722b7a_vf7", 0x3a3410, 32, 0x9c5258fb, MemberAddress<&cFloorsCollider::CollideWorldBody>()},
    {"AVcWallsAndFloorsCollider_A0x65722b7a_vf7", 0x3a3430, 32, 0xf6873220, MemberAddress<&cWallsAndFloorsCollider::CollideWorldBody>()},
    {"AVcWallsCollider_A0x65722b7a_vf7", 0x3a3490, 32, 0x8fd5f01f, MemberAddress<&cWallsCollider::CollideWorldBody>()},
    // cTSPlaneCollider (vtable 0xe8ce50, built by cTSEffectsManager::Init): each slot body has one reference, its slot.
    {"cTSPlaneCollider_vf3", 0xbdf690, 6, 0x8c9408bf, MemberAddress<&world_plane::cTSPlaneCollider::GetStateSize>()},
    {"cTSPlaneCollider_vf4", 0xbded70, 32, 0x991259dd, MemberAddress<&world_plane::cTSPlaneCollider::PrepareState>()},
    {"cTSPlaneCollider_vf5", 0xbdea30, 32, 0x26bf05dc, MemberAddress<&world_plane::cTSPlaneCollider::Collide>()},
    {"cTSPlaneCollider_vf6", 0xbdf420, 32, 0x0a6615bb, MemberAddress<&world_plane::cTSPlaneCollider::Draw>()},
    {"A0x65722b7a_CollideFloors", 0x3a35e0, 32, 0x76236d6a, reinterpret_cast<void *>(&CollideFloors)},
    {"cTSWorldSpaceColliderBase_A0x65722b7a_CollideWalls", 0x3a39e0, 32, 0xbf0109b0, MemberAddress<&cTSWorldSpaceColliderBase::CollideWalls>()},
};
REGISTER_HOOKS(kHooks);

} // namespace
