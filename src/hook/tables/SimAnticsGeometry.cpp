#include "hook/hooks.h"
#include "simantics/SimGeometry.h"

namespace {

// The geometry helpers of the SimAntics primitives (lane AJ): quaternion rotation, object vector, sine / cosine of a float,
// tile and position conversions, notch angle and rotation.
const HookSpec kHooks[] = {
    {"SimGeometry_RotateByQuaternion", 0x1a6c40, 32, 0xad7ee0fc, reinterpret_cast<void *>(&simgeom::RotateByQuaternion)},
    {"SimGeometry_ReadObjectVector", 0x7dd0d0, 32, 0xcd83859b, reinterpret_cast<void *>(&simgeom::ReadObjectVector)},
    {"SimGeometry_SineOfFloat", 0x5a9de0, 29, 0x09078026, reinterpret_cast<void *>(&simgeom::SineOfFloat)},
    {"SimGeometry_CosineOfFloat", 0x5a9a90, 29, 0xa3047eeb, reinterpret_cast<void *>(&simgeom::CosineOfFloat)},
    {"SimGeometry_TileFromBytes", 0x7dcb60, 32, 0xb2c0c967, reinterpret_cast<void *>(&simgeom::TileFromBytes)},
    {"SimGeometry_PositionToFloats", 0x7dcc50, 32, 0x97d9ab09, reinterpret_cast<void *>(&simgeom::PositionToFloats)},
    {"SimGeometry_TileCentre", 0x7dd620, 32, 0xf5ba56bb, reinterpret_cast<void *>(&simgeom::TileCentre)},
    {"SimGeometry_FacingToNotchOffset", 0x7dcde0, 25, 0x2d5039bf, reinterpret_cast<void *>(&simgeom::FacingToNotchOffset)},
    {"SimGeometry_NotchAngle", 0x7dcdb0, 32, 0xd258690e, reinterpret_cast<void *>(&simgeom::NotchAngle)},
    {"SimGeometry_NotchQuaternion", 0x7dccd0, 32, 0xa768ac0e, reinterpret_cast<void *>(&simgeom::NotchQuaternion)},
};
REGISTER_HOOKS(kHooks);

} // namespace
