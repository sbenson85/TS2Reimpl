#pragma once
// The free helpers of the particle code (lane WV, rendering row E5): random points in the source shapes, the normalising divide and
// the collision sweep over an effect's colliders. All scalar SSE single with the first-operand NaN rule of fx::Add / fx::Mul; the
// random generator is the game's (0x10fd138).
#include "game/game.h"
#include "rz/cRZRandom.h"
#include "ts/EffectsTransform.h"

namespace fx {

inline cRZRandom &GameRandom() { return *GameAddress<cRZRandom>(0x10fd138); }
// A random in [low, high), the bounds taken as doubles from the floats.
inline float RandomIn(float low, float high)
{
    return static_cast<float>(GameRandom().RandomDoubleRangeUniform(static_cast<double>(low), static_cast<double>(high)));
}

// 0xbbcd80 (cdecl): a random in each axis of the box {min xyz, max xyz}, x then y then z. Returns `out`.
Vector3 *__cdecl RandomInBox(Vector3 *out, const float *box);
// 0xbc2f80 (cdecl): `in` over its length; a squared length of 1e-6 or less copies it. Returns `out`.
Vector3 *__cdecl Normalize(Vector3 *out, const Vector3 *in);
// 0xbdd2f0 (cdecl): a random point of the box's inscribed ellipsoid (by rejection from three randoms). Returns `out`.
Vector3 *__cdecl RandomInEllipsoid(Vector3 *out, const float *box);
// 0xbdd430 (cdecl): likewise a ring of the box (a torus: its tube as a fraction `ratio` of the radius). Returns `out`.
Vector3 *__cdecl RandomOnRing(Vector3 *out, const float *box, float ratio);

} // namespace fx
