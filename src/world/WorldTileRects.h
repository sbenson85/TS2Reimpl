// Tile rects of nTSWorld's helper module (lane RY): 0x7ef060, 0x7ef260, 0x7ef3b0. A rect is two float triples (x, y, level);
// the level is kept as raw integer bits. Built for speed (/O2).
#pragma once

#include <cstdint>

namespace worldrect {

// 0x7ef060 (__cdecl): out = the rect moved by the point's x and y (the level words add as integers). Returns out.
float *__cdecl OffsetRect(float *out, const float *rect, const float *point);
// 0x7ef260 (__cdecl): the tile cell between two float triples a and b: out is five ints (x, y, level, quadrant mask, kind). Returns out.
int32_t *__cdecl CellBetween(int32_t *out, const float *a, const float *b);
// 0x7ef3b0 (__cdecl): CellBetween(out, rect, rect + 3). Returns out.
int32_t *__cdecl CellOfRect(int32_t *out, const float *rect);

} // namespace worldrect
