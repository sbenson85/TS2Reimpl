#pragma once
#include <cstdint>

// The small geometry helpers of the SimAntics primitives (cells and tiles, facing "notches", rotations). Lane AJ. RVAs are
// 1.18.0.187; all are __cdecl free functions.
//
// A facing is a notch: eight directions, numbered so that notch 2 is angle 0. A position is either integer (16.16 fixed
// point for x and y, a level for z) or float (x, y in tile units, z a level).

namespace simgeom {

// 0x1a6c40: rotates the vector v (3 floats) by the quaternion q (x, y, z, w) in place (the 3x3 matrix of the quaternion, each
// element built as the original does from products doubled by adding them to themselves).
void __cdecl RotateByQuaternion(float *v, const float *q);

// 0x7dd0d0: reads a vector from an object's slots 4, 5 and 6 (floats): out = (slot4, -slot5, slot6). Returns `out`; a null
// object leaves `out` alone.
float *__cdecl ReadObjectVector(float *out, void *object);

// 0x5a9de0 / 0x5a9a90: sine and cosine of a float angle, computed in double precision and rounded to float.
float __cdecl SineOfFloat(float angle);
float __cdecl CosineOfFloat(float angle);

// 0x7dcb60: three signed tile bytes (bytes[0], bytes[1], bytes[2]) as the integer position (bytes[1], bytes[0], bytes[2] - 1).
// Returns `out`.
int32_t *__cdecl TileFromBytes(int32_t *out, const uint8_t *bytes);

// 0x7dcc50: the 16.16 position (in[0], in[1]) as floats (times 2^-16) with the level minus one as an integer in the third
// element: out = (in[0] * 2^-16, in[1] * 2^-16, level - 1 as int bits). Returns `out`.
float *__cdecl PositionToFloats(float *out, const int32_t *in, int32_t level);

// 0x7dd620: the tile (in[0], in[1], in[2]) as a float position at the tile centre: (in[0] + 0.5, in[1] + 0.5, in[2] as int
// bits). Returns `out`.
float *__cdecl TileCentre(float *out, const int32_t *in);

// 0x7dcde0: (10 - (facing as 16 bits)) modulo 8 with the sign of the dividend (C's %).
int32_t __cdecl FacingToNotchOffset(int32_t facing);

// 0x7dcdb0: the angle of a notch: ((2 - notch) as 16 bits) * (pi / 4) in float.
float __cdecl NotchAngle(int32_t notch);

// 0x7dccd0: the rotation about the z axis of a notch as a quaternion (0*s, 0*s, s, c) with the half angle of NotchAngle
// reduced into [0, 2 pi) first (fmod of angle + 2 pi by 2 pi, in double precision). Returns `out`.
float *__cdecl NotchQuaternion(float *out, int32_t notch);

} // namespace simgeom
