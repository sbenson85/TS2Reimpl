#pragma once
// A rotation quaternion {x, y, z, w} as the game's math code keeps it (16 bytes, w last; lane HQ). The class name is not
// known (no RTTI); 0x1a7020 is called from about forty places (scenegraph, camera, thumbnails, the person head).
#include <cstdint>

namespace math {

struct Quaternion {
    float x, y, z, w;

    // 0x1a7020 (thiscall, ret 4, returns this): *this = normalise(rhs * *this) (Hamilton product, rhs on the left),
    // in single precision in the original's operation order; the length is the double square root of the float sum of
    // squares, its float reciprocal multiplied into each component.
    Quaternion *PreMultiplyNormalised(const Quaternion *rhs);

    // 0x1a6db0 (thiscall, ret 8, returns this): the rotation taking the direction `from` to the direction `to`: `from`
    // normalised against the unit half vector, {cross, dot}. `to` is taken as a unit vector (its length tilts the half
    // vector) and `from` must not be zero. When the two nearly cancel (the half vector's square at most 2^-23) it is the
    // half turn about an axis perpendicular to `from`, in the plane of its larger pair of components.
    Quaternion *SetRotationBetween(const float *from, const float *to);

    // The quaternion of a rotation by twice `halfAngle` about `axis` (normalised here): {axis * sin, cos} in float,
    // from the double sine and cosine of the half angle (XC slot 193's two rotations).
    static Quaternion FromAxisHalfAngle(const float axis[3], double halfAngle);
};
static_assert(sizeof(Quaternion) == 16);

} // namespace math
