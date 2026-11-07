#pragma once
#include "world/cWorldDB.h"

#include <cstdint>

// The tile maths of nTSWorld's helper module (lane RY, 0x7dcb90-0x7ddea0): conversions between tiles (integer x, y, level),
// positions (float x, y; the third element a float height or, in the "position" records the callers pass around, the
// level's integer bits), 16.16 fixed values and the quadrant bytes of a tile. RVAs are 1.18.0.187; all are __cdecl free
// functions unless the comment says otherwise.
//
// Quadrant bytes (the fourth field of a tile record, WorldTile::segments): bit 0 = west (x - 1/3), bit 1 = south (y + 1/3),
// bit 2 = east, bit 3 = north; 15 (and 255) mean the whole tile. Sides are 1, 2, 4, 8; corners are 3, 6, 9, 12.
//
// The ground height under a position is the world layer's GroundHeightAt (0x7e4680), still an original here.

namespace worldmath {

struct TileQuadrant {
    int32_t x, y, level;
    uint8_t quadrant; // 0x7dd6c0 / 0x7dd250 write this byte only; the three bytes after it are not touched
};

// A kind word followed by a 3-int tile: the record 0x7dde80 fills.
struct KindedTile {
    int32_t kind;
    int32_t tile[3];
    KindedTile *__thiscall Set(const int32_t *tile3); // 0x7dde80 (ret 4): kind = 0, the tile copied; returns this
};

// ---- tile bytes and fixed values ----
// 0x7dcb90: the position of three signed tile bytes: PositionAtGround(out, b[0], b[1], b[2]). Returns out.
float *__cdecl TileBytesToPosition(float *out, const int8_t *bytes);
// 0x7dcbd0: (b[1], b[0]) as floats; out[2] is the integer b[2] - 1 as raw bits, not a converted float.
void __cdecl TileBytesToFloats(float *out, const int8_t *bytes);
// 0x7dcc00: the position of a 16.16 point at a level: PositionAtGround(out, in[1] * 2^-16, in[0] * 2^-16, level). Returns out.
float *__cdecl FixedToPosition(float *out, const int32_t *fixed, int32_t level);
// 0x7dcc90: two 16.16 values as floats (times 2^-16).
void __cdecl Fixed16ToFloats(float *out, const int32_t *fixed);
// 0x7dd0b0: the tile (x, y, level) as signed bytes (y, x, level + 1): the inverse of 0x7dcb60.
int8_t *__cdecl TileToBytes(int8_t *out, const int32_t *tile);
// 0x7dd120: two floats as 16.16 values (times 65536, truncated).
int32_t *__cdecl FloatsToFixed16(int32_t *out, const float *in);
// 0x7dd160: out = (x, y, GroundHeightAt({x, y, level - 1})). Note the parameter order: (out, y, x, level).
void __cdecl PositionAtGround(float *out, float y, float x, int32_t level);
// 0x7dd1b0: GroundHeightAt({x, y, level - 1}). (y, x, level) as above.
float __cdecl HeightAtLevel(float y, float x, int32_t level);

// ---- facings ----
// 0x7dcee0: (float)(int16)(-facing) * pi/4, in an x87 register.
float __cdecl FacingAngle(int32_t facing);
// 0x7dce00: the rotation about z of a facing as a quaternion (0*s, 0*s, s, c): as 0x7dccd0 with FacingAngle.
float *__cdecl FacingQuaternion(float *out, int32_t facing);
// 0x7dcf10: (8 - (int16)facing) modulo 8 with the sign of the dividend.
int32_t __cdecl FacingToNotch(int32_t facing);
// 0x7dd090: (8 - *facing) modulo 8 with the sign of the dividend.
int32_t __cdecl FacingPtrToNotch(const int32_t *facing);
// 0x7dcf30: an angle brought into (-2 pi, 0), turned to eighths of a turn and read as a notch in 0..7 (signed remainder).
int32_t __cdecl AngleToNotch(float angle);
// 0x7dcfe0: an angle brought into [-pi, pi), turned to eighths of a turn (the low 16 bits are the result).
int16_t __cdecl AngleToFacing(float angle);

// ---- tiles and positions ----
// 0x7dd1e0: the neighbour of a tile in a direction byte (bit 0 west, bit 1 south, bit 2 east, bit 3 north; 0xf and 0xff
// mean none): out = tile + the offset. The z/level is kept.
int32_t *__cdecl NeighbourTile(int32_t *out, const int32_t *tile, uint8_t direction);
// 0x7dda90: the corner of a tile in direction 1 (y + 1), 5 (x + 1) or 7 (both); others unchanged.
int32_t *__cdecl CornerTile(int32_t *out, const int32_t *tile, int32_t corner);
// 0x7dde50: a tile as floats: x and y converted, the level copied as raw integer bits.
float *__cdecl TileToFloats(float *out, const int32_t *tile);
// 0x7dda60: CornerTile then TileToFloats. Returns out.
float *__cdecl CornerPosition(float *out, const int32_t *tile, int32_t corner);
// 0x7dd470: x and y rounded to quarters ((int)(v * 4 + 0.5) * 0.25), z kept.
void __cdecl SnapToQuarters(float *out, const float *in);
// 0x7dd870: x and y + 0.5 floored (library floor), z kept; the output is first filled with (-1, -1, 0).
float *__cdecl RoundPositionDown(float *out, const float *in);
// 0x7dd8f0: x and y rounded to nearest-even as integers (magic number), z kept as bits; the output starts as (-1, -1, 0).
int32_t *__cdecl RoundPositionToTile(int32_t *out, const float *in);
// 0x7dddf0: as 0x7dd8f0 without the initial fill.
int32_t *__cdecl RoundPositionToTileB(int32_t *out, const float *in);
// 0x7dd9d0: the tile of a position (x, y floored the compiler's way, z kept as bits).
int32_t *__cdecl PositionToTile(int32_t *out, const float *position);
// 0x7dd6c0: PositionToTile, the quadrant of the point inside it in the fourth field. Returns out.
TileQuadrant *__cdecl PositionToTileQuadrant(TileQuadrant *out, const float *position);
// 0x7ddb80: the quadrant (side) of a point inside its tile: 1, 2, 4 or 8 from the diagonals of the unit square.
uint8_t __cdecl QuadrantOfPoint(float x, float y);
// 0x7ddc10: QuadrantOfPoint(position[0], position[1]).
uint8_t __cdecl QuadrantOfPositionXY(const float *position);
// 0x7dd5b0: the offset of a quadrant's centre from the tile's: bit 0 -1/3 in x, bit 2 +1/3 in x, bit 1 +1/3 in y, bit 3 -1/3.
void __cdecl QuadrantOffset(float *out, uint8_t quadrant);
// 0x7dd500: the centre of a quadrant tile: (x + 0.5 + dx, y + 0.5 + dy, level as a float).
float *__cdecl QuadrantCentre(float *out, const TileQuadrant *tile);
// 0x7dd580 / 0x7dd660 / 0x7dd690: QuadrantCentre, TileCentre-then-height, and tile-of-position-then-centre.
float *__cdecl QuadrantCentreOnGround(float *out, const TileQuadrant *tile);
float *__cdecl TileCentreOnGround(float *out, const int32_t *tile);
float *__cdecl PositionToTileCentre(float *out, const float *position);
// 0x7dddc0: out = (in.x, in.y, GroundHeightAt(in)).
float *__cdecl WithGroundHeight(float *out, const float *in);
// 0x7ddd30: out = (in.x, in.y, <0x7e2cc0 of in>), the third a callee's result (an original here).
int32_t *__cdecl WithTileValue(int32_t *out, const int32_t *in);

// ---- quadrant bytes ----
// 0x7dd800: the corners 3, 6, 9 and 12.
bool __cdecl IsCornerQuadrant(int8_t quadrant);
// 0x7dd840: the sides 1, 2, 4 and 8.
bool __cdecl IsSideQuadrant(int8_t quadrant);
// 0x7dd960: the opposite quadrant (1<->4, 2<->8, 3<->12, 6<->9, 15 stays), 0xff for anything else.
uint8_t __cdecl OppositeQuadrant(int8_t quadrant);
// 0x7ddb40: a compass word (0, 2, 4, 6) as the side byte (2, 1, 8, 4); 0xff otherwise.
uint8_t __cdecl CompassToQuadrant(const uint32_t *compass);

// ---- one more tile ----
// 0x7dd250: the tile and quadrant reached from `tile` by a turn/step code (0 and 1: stepping the sides and rotating, larger:
// the neighbour in a direction). Returns out.
TileQuadrant *__cdecl StepQuadrantTile(TileQuadrant *out, const TileQuadrant *tile, uint32_t step);

} // namespace worldmath
