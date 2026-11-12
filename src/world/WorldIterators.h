#pragma once
// The small iterators and value classes of nTSWorld's helper module (lane RZ), 0x7f0f00-0x7f1df0: boxes of tiles (with a
// quadrant byte or a bit per tile), diamonds of tiles (a rectangle turned 45 degrees: rows of y between a low and a high
// bound that move by one per column of x), the triangle of a tile quadrant, and the rings of points, corners and edge
// neighbours around a tile. Every class is a plain value the callers keep on their stacks; every member is __thiscall
// and returns `this` (Next, constructors) or the output pointer (getters) as the originals do. Built for speed (/O2).
//
// Not hooked: 0x7f1a30 QuadrantBitIterator::Bit (3 bytes, `mov al, [ecx]; ret`). Six of the functions are missing from
// Ghidra's records (db/missed-functions.jsonl has them): 0x7f0e40, 0x7f0ea0, 0x7f18c0, 0x7f1930, 0x7f1d00, 0x7f1d20.
#include "world/cWorldDB.h"

#include <cstdint>

namespace worlditer {

struct TileCoord {
    int32_t x, y, level;
};

struct TileCoordQuadrant {
    int32_t x, y, level;
    uint8_t quadrant;
};

struct TileCoordBit {
    int32_t x, y, level;
    uint32_t bit;
};

struct PointCoord {
    float x, y;
    int32_t level;
};

// 0x7f1b80 (__cdecl): the four corners of the box (x1, y1)-(x2, y2) turned 45 degrees, each (sum / 2) rounded to nearest
// (even) through the 1.5 * 2^23 trick in SSE floats: a = x1 - y2 + y1 + x2, b = x1 - x2 + y1 + y2, c = x1 - y1 + x2 + y2,
// d = y1 - x1 + x2 + y2.
void __cdecl RotateCornersInt(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t *a, int32_t *b, int32_t *c,
                              int32_t *d);
// 0x7f1c60 (__cdecl): the same in floats, unrounded: ((x1 + y1 + x2) - y2) / 2, ((x1 + y1) - x2 + y2) / 2,
// ((x1 - y1) + x2 + y2) / 2, ((y1 - x1) + x2 + y2) / 2.
void __cdecl RotateCornersFloat(float x1, float y1, float x2, float y2, float *a, float *b, float *c, float *d);
// 0x7f1ae0 (__cdecl): a diamond's start and turning columns from the box: rotated corners a, b, c (RotateCornersInt); when
// the box is taller than wide (x2 - x1 < y2 - y1) start (a, b) and turn at x1 and x2, else start (x1, y1) and turn at a
// and c.
void __cdecl DiamondBounds(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t *startX, int32_t *startY,
                           int32_t *lowTurnX, int32_t *highTurnX);

// A diamond of tiles over a range of levels. Columns of x from startX; in each, y runs from low to high; after a column
// low moves down until x passes lowTurnX and up after, high up until x passes highTurnX and down after; the level ends
// when low passes high.
struct TileDiamondIterator {
    int32_t startX, startY, lowTurnX, highTurnX; // 0x00
    int32_t low, high;                           // 0x10
    int32_t firstLevel, lastLevel;               // 0x18
    int32_t x, y, level;                         // 0x20

    // 0x7f0f00: the diamond of the box from a to b (DiamondBounds), levels a.level to b.level.
    TileDiamondIterator *__thiscall Construct(const TileCoord *a, const TileCoord *b);
    // 0x7f17a0: the next tile; after a column, low +1 when x > lowTurnX else -1, high -1 when x > highTurnX else +1
    // (equal counts as before the turn); a new level (StartLevel) when low passes high.
    TileDiamondIterator *__thiscall Next();
    // 0x7f18d0: past the last level.
    bool __thiscall Done() const;
    // 0x7f1950: the current tile.
    TileCoord *__thiscall Get(TileCoord *out) const;
    // 0x7f1d10: StartLevel, then the level is the first.
    void __thiscall Start();
    // 0x7f1da0: back to the first column: x = startX, low = high = y = startY.
    void __thiscall StartLevel();
};

// The diamond without its first column (0x7f1740 / 0x7f1d50; nothing in the image calls 0x7f1740).
struct InnerDiamondIterator {
    int32_t startX, startY, lowTurnX, highTurnX; // 0x00
    int32_t low, high;                           // 0x10
    int32_t firstLevel, lastLevel;               // 0x18
    int32_t x, y, level;                         // 0x20

    // 0x7f0ea0: the diamond of the box from a to b (DiamondBounds), levels a.level to b.level, then Start.
    InnerDiamondIterator *__thiscall Construct(const TileCoord *a, const TileCoord *b);
    // 0x7f1d00: the level is the first, then StartLevel.
    void __thiscall Start();
    // 0x7f1740: the next tile; after a column low/high move as TileDiamondIterator's with equal as no move; a new level
    // (StartLevel) when low passes high, else y = low.
    InnerDiamondIterator *__thiscall Next();
    // 0x7f1d50: x = startX + 1; when that is lowTurnX too it is the last column (with highTurnX as well the level ends:
    // level = lastLevel + 1); low and y are startY there, startY - 1 otherwise; high is startY, or startY - 1 on highTurnX.
    void __thiscall StartLevel();
};

// The diamond in tile quadrants: each tile's quadrants in a mask (all four inside, the triangles on the edges), visited
// one bit at a time.
struct QuadDiamondIterator {
    int32_t startX, startY, lowTurnX, highTurnX; // 0x00
    int32_t low, high;                           // 0x10
    int32_t firstLevel, lastLevel;               // 0x18
    uint8_t mask, pad21[3];                      // 0x20
    int32_t x, y, level;                         // 0x24
    int8_t bit, pad31[3];                        // 0x30

    // 0x7f0e40: the diamond of the box from a to b (DiamondBounds), levels a.level to b.level, then Start.
    QuadDiamondIterator *__thiscall Construct(const TileCoord *a, const TileCoord *b);
    // 0x7f18c0: past the last level.
    bool __thiscall Done() const;
    // 0x7f1930: the current tile and quadrant bit.
    TileCoordQuadrant *__thiscall Get(TileCoordQuadrant *out) const;
    // 0x7f1690: the next quadrant bit of the tile in the mask, else the next tile: y + 1 below high is a whole tile (mask
    // 15, bit 1), y == high the column's top (mask 9 / bit 1 from highTurnX on, else 12 / 4); past high the next column,
    // low/high moved as InnerDiamondIterator's, then a new level (level + 1, StartColumn) or StartRow.
    QuadDiamondIterator *__thiscall Next();
    // 0x7f1cf0: StartColumn, then the level is the first.
    void __thiscall Start();
    // 0x7f1d30: x = startX, low = high = startY, then StartRow.
    void __thiscall StartColumn();
    // 0x7f1dc0: y = low - 1, the column's bottom: mask 6 / bit 2 before lowTurnX, else 3 / 1.
    void __thiscall StartRow();
};

// A box of tiles: y fastest, then x, then level. The fourth word is a quadrant byte (1, 2, 4, 8 per tile), a bit (1 to 32
// per tile) or, for the plain box, not touched.
struct TileBoxIterator {
    TileCoord min;  // 0x00
    TileCoord max;  // 0x0c
    TileCoord cur;  // 0x18
    uint32_t extra; // 0x24: the quadrant byte (low byte only) or the bit

    // 0x7f12a0: the box min..max, cur = min.
    TileBoxIterator *__thiscall Construct(const TileCoord *lo, const TileCoord *hi);
    // 0x7f0f60: as 0x7f12a0 with quadrant byte 1.
    TileBoxIterator *__thiscall ConstructQuadrants(const TileCoord *lo, const TileCoord *hi);
    // 0x7f1620: as 0x7f12a0 with bit 1 (a whole word).
    TileBoxIterator *__thiscall ConstructBits(const TileCoord *lo, const TileCoord *hi);
    // 0x7f12f0: the lot's tiles on one level (db, or the cached world DB when null: GetMinX, GetMinY, GetMaxX, GetMaxY).
    TileBoxIterator *__thiscall ConstructLevel(int32_t level, nTSWorld::cIWorldDB *db);
    // 0x7f0fc0: as 0x7f12f0 with quadrant byte 1.
    TileBoxIterator *__thiscall ConstructLevelQuadrants(int32_t level, nTSWorld::cIWorldDB *db);
    // 0x7f1530: as 0x7f12f0 over the corners (max + 1 on x and y).
    TileBoxIterator *__thiscall ConstructLevelCorners(int32_t level, nTSWorld::cIWorldDB *db);
    // 0x7f13e0: the lot's tiles on every level (GetMinX, GetMinY, GetMinLevel, GetMaxX, GetMaxY, GetMaxLevel).
    TileBoxIterator *__thiscall ConstructAll(nTSWorld::cIWorldDB *db);
    // 0x7f10b0: as 0x7f13e0 with quadrant byte 1.
    TileBoxIterator *__thiscall ConstructAllQuadrants(nTSWorld::cIWorldDB *db);
    // 0x7f1840: the next tile (y; past max.y it is min.y again and x moves on; past max.x x is min.x and level moves on).
    TileBoxIterator *__thiscall Next();
    // 0x7f1800: the next quadrant (the byte doubled; past 8 it is 1 again and the tile moves on).
    TileBoxIterator *__thiscall NextQuadrant();
    // 0x7f1880: the next bit (doubled; past 32 it is 1 again and the tile moves on).
    TileBoxIterator *__thiscall NextBit();
    // 0x7f18e0: past the last level.
    bool __thiscall Done() const;
    // 0x7f1d20: back to the first tile (cur = min).
    void __thiscall Reset();
    // 0x7f1a10: the current tile.
    TileCoord *__thiscall Get(TileCoord *out) const;
    // 0x7f1970: the current tile and quadrant byte.
    TileCoordQuadrant *__thiscall GetQuadrant(TileCoordQuadrant *out) const;
    // 0x7f1ac0: the current tile and bit.
    TileCoordBit *__thiscall GetBit(TileCoordBit *out) const;
};

// The triangle of a tile quadrant: the quadrant's outer edge (two tile corners) and the tile's centre, visited as edges.
struct TileTriangle {
    int32_t index;        // 0x00
    PointCoord point[3];  // 0x04

    // 0x7f11b0: from a tile and its quadrant bit (1: the x edge from (x, y + 1) to (x, y); 2: (x + 1, y + 1) to
    // (x, y + 1); 4: (x + 1, y) to (x + 1, y + 1); else (x, y) to (x + 1, y)), the centre (x + 0.5, y + 0.5); index 0.
    TileTriangle *__thiscall Construct(const TileCoordQuadrant *tile);
    // 0x7f1670: the points' destructors do nothing.
    void __thiscall Destroy();
    // 0x7f1990: the current edge, point[index] to point[(index + 1) % 3].
    PointCoord *__thiscall Edge(PointCoord *out) const;
    // 0x7f18f0: past the third edge.
    bool __thiscall Done() const;
};

// The eight points at half steps around a point (x, y - 1 first, clockwise in the tile grid).
struct RingPointIterator {
    int32_t index; // 0x00
    PointCoord centre;

    // 0x7f14e0: around the tile's corner (x, y) as floats; index 0.
    RingPointIterator *__thiscall Construct(const TileCoord *tile);
    // 0x7f1a80: the current point.
    PointCoord *__thiscall Get(PointCoord *out) const;
    // 0x7f1920: past the eighth.
    bool __thiscall Done() const;
};

// The four corners of a tile, (x, y), (x + 1, y), (x + 1, y + 1), (x, y + 1).
struct TileCornerIterator {
    int32_t index; // 0x00
    TileCoord tile;

    // 0x7f19e0: the current corner.
    TileCoord *__thiscall Get(TileCoord *out) const;
    // 0x7f1900: past the fourth (shared with TileEdgeIterator).
    bool __thiscall Done() const;
};

// The four tiles across a tile corner's edges, with the quadrants facing it: (x - 1, y - 1) 6, (x, y - 1) 3, (x, y) 9,
// (x - 1, y) 12.
struct TileEdgeIterator {
    int32_t index; // 0x00
    TileCoord tile;

    // 0x7f1a40: the current tile and quadrants.
    TileCoordQuadrant *__thiscall Get(TileCoordQuadrant *out) const;
};

// A quadrant bit 1, 2, 4, 8.
struct QuadrantBitIterator {
    uint32_t bit;

    // 0x7f1870: doubled.
    QuadrantBitIterator *__thiscall Next();
    // 0x7f1910: past 8.
    bool __thiscall Done() const;
};

} // namespace worlditer
