#pragma once
// More of the original (not yet replaced) functions the GenericSimCall cases call (the ones cases 43, 52, 60, 70 and 81 need).
// They are defined in PrimGenericSimCall.cpp so the status entry of 0x58bc50 can declare them in one place.
#include <cstdint>

namespace gscorig {

// 0x4f8e50 (__cdecl, no arguments): the address of the 16-byte all-zero GUID (a function-local static, built on first use).
const uint8_t *NullGuid();
// 0x212e61 (__cdecl): the cached tile/grid service holder (slot 39 the grid, slot 71 "is this tile usable").
void *TileService();
// 0x7dccd0 (__cdecl): the rotation quaternion (4 floats) of a direction notch; 0x1a6c40 rotates a 3-float vector by it in place.
void NotchQuaternion(float *quaternion, uint32_t notch);
void RotateVector(float *vector, const float *quaternion);
// 0x7eed90 / 0x7eec00 (__cdecl): the two tiles (3 ints each, copied out of the returned pointer) a box of 6 floats touches.
const int32_t *FirstTileOfBox(int32_t *scratch, const float *box);
const int32_t *SecondTileOfBox(int32_t *scratch, const float *box);
// 0x7deff0 (__cdecl): the object standing on a tile (null when none).
void *ObjectOnTile(const int32_t *tile, int32_t mode);
// 0x7e7e00 (__cdecl, result in al): is the position (3 floats) blocked within the radius (flags a, b).
bool PositionBlocked(const float *position, float radius, int32_t flagA, int32_t flagB);
// 0x7e8c60 with a mode word (the placement worker; PlaceObject in PrimGenericSimCallOriginals.h passes 0). Case 43 passes the
// object's reported position as `second` and the address of its direction handle as `third` (that order matters).
bool PlaceObjectMode(void *object, void *second, void *third, int32_t mode);
// 0x5a9de0 / 0x5a9a90 (__cdecl, st0 result): sine and cosine of an angle (the game's wrappers of the CRT's).
float SineOf(float angle);
float CosineOf(float angle);

} // namespace gscorig
