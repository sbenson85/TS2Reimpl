// Tile rects of nTSWorld's helper module (lane RY): 0x7ef060, 0x7ef260, 0x7ef3b0. A rect is two float triples (x, y, level);
// the level is kept as raw integer bits. Built for speed (/O2).
#pragma once

#include "world/WorldTileMath.h"

#include <cstdint>

namespace worldrect {

// 0x7ef060 (__cdecl): out = the rect moved by the point's x and y (the level words add as integers). Returns out.
float *__cdecl OffsetRect(float *out, const float *rect, const float *point);
// 0x7ef260 (__cdecl): the tile cell between two float triples a and b: out is five ints (x, y, level, quadrant mask, kind). Returns out.
int32_t *__cdecl CellBetween(int32_t *out, const float *a, const float *b);
// 0x7ef3b0 (__cdecl): CellBetween(out, rect, rect + 3). Returns out.
int32_t *__cdecl CellOfRect(int32_t *out, const float *rect);

// Lane RZ. A cell (the five ints CellBetween writes: x, y, level, mask, kind) names one edge of a tile: mask 1, 2, 4, 8 a side,
// 0x10 / 0x20 a diagonal (kind says which way it was drawn). The quadrant record (worldmath::TileQuadrant: x, y, level and a
// byte) names the same edge by the quadrants it touches.

// 0x7eec60 (__cdecl): the quadrant record of a cell: sides 1, 2, 4, 8 give 8, 1, 2, 4; diagonal 0x10 gives 9 for kind 2, else
// 6; diagonal 0x20 gives 0xc for kind 1, else 3; anything else 0. Only the record's byte at +12 is written after the tile.
// Returns out.
worldmath::TileQuadrant *__cdecl CellToQuadrants(worldmath::TileQuadrant *out, const int32_t *cell);
// 0x7eed90 (__cdecl): CellToQuadrants(out, CellOfRect(rect)). Returns out.
worldmath::TileQuadrant *__cdecl SegmentToQuadrants(worldmath::TileQuadrant *out, const float *rect);
// 0x7eec00 (__cdecl): SegmentToQuadrants of the segment drawn the other way (its two points swapped). Returns out.
worldmath::TileQuadrant *__cdecl ReversedSegmentToQuadrants(worldmath::TileQuadrant *out, const float *rect);
// 0x7eedc0 (__cdecl): both |x0 - x1| and |y0 - y1| are within 32 units in the last place of 1.0 (a one-tile segment on each
// axis; a zero length fails).
bool __cdecl IsUnitSegment(const float *rect);
// 0x7eee50 (__cdecl): the rect turned by a facing (*facing): 0 gives (y, -x), 4 gives (-y, x), 6 gives (-x, -y), anything else
// the rect unchanged; the level words are copied. Returns out.
float *__cdecl RotateRect(float *out, const float *rect, const int32_t *facing);
// 0x7ef0c0 (__cdecl): the two end points of a cell's edge, as tile corners (worldmath::CornerPosition) chosen by the mask (and
// the kind for diagonals). A mask that names no edge leaves both untouched.
void __cdecl EdgeEndpoints(const int32_t *cell, float *a, float *b);
// 0x7ef1d0 (__cdecl): the edge of a cell as a rect: EdgeEndpoints into (-1, -1, 0) twice. Returns out.
float *__cdecl CellToSegment(float *out, const int32_t *cell);
// 0x7eef00 (__cdecl): the segment of a rect in its canonical form: a +y side (mask 4) becomes the -y side (mask 1) of the tile
// above, a +x side (mask 8) the -x side (mask 2) of the tile beside it, and the kind is 2 for a 0x10 diagonal, 3 for 0x20, else
// 0. Returns out.
float *__cdecl CanonicalSegment(float *out, const float *rect);

} // namespace worldrect
