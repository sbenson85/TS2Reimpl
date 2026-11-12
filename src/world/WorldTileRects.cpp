// The tile rects of nTSWorld's helper module (lane RY): src/world/WorldTileRects.h. Built for speed (/O2).
#include "world/WorldTileRects.h"

#include <cmath>
#include <cstring>

namespace worldrect {

float *__cdecl OffsetRect(float *out, const float *rect, const float *point)
{
    const float px = point[0], py = point[1];
    int32_t *const o = reinterpret_cast<int32_t *>(out);
    const int32_t *const r = reinterpret_cast<const int32_t *>(rect);
    const int32_t *const p = reinterpret_cast<const int32_t *>(point);
    out[0] = px + rect[0];
    out[1] = py + rect[1];
    o[2] = r[2] + p[2];
    out[3] = px + rect[3];
    out[4] = py + rect[4];
    o[5] = r[5] + p[2];
    return out;
}

int32_t *__cdecl CellBetween(int32_t *out, const float *a, const float *b)
{
    const float ax = a[0], ay = a[1], bx = b[0], by = b[1];
    out[2] = reinterpret_cast<const int32_t *>(a)[2];
    if (bx > ax) {
        out[0] = static_cast<int32_t>(ax);
        if (by > ay) {
            out[1] = static_cast<int32_t>(ay);
            out[3] = 0x20;
            out[4] = 1;
        } else if (ay > by) {
            out[1] = static_cast<int32_t>(by);
            out[3] = 0x10;
            out[4] = 2;
        } else {
            out[1] = static_cast<int32_t>(ay - 1.0f);
            out[3] = 4;
            out[4] = 0;
        }
    } else if (ax > bx) {
        out[0] = static_cast<int32_t>(bx);
        if (ay > by) {
            out[1] = static_cast<int32_t>(by);
            out[3] = 0x20;
            out[4] = 3;
        } else if (by > ay) {
            out[1] = static_cast<int32_t>(ay);
            out[3] = 0x10;
            out[4] = 4;
        } else {
            out[1] = static_cast<int32_t>(by);
            out[3] = 1;
            out[4] = 0;
        }
    } else if (by > ay) {
        out[0] = static_cast<int32_t>(ax);
        out[1] = static_cast<int32_t>(ay);
        out[3] = 2;
        out[4] = 0;
    } else {
        out[0] = static_cast<int32_t>(bx - 1.0f);
        out[1] = static_cast<int32_t>(by);
        out[3] = 8;
        out[4] = 0;
    }
    return out;
}

int32_t *__cdecl CellOfRect(int32_t *out, const float *rect) { return CellBetween(out, rect, rect + 3); }

namespace {

// The cell's fields.
enum CellField { kCellX, kCellY, kCellLevel, kCellMask, kCellKind, kCellWords };

// Sides and diagonals in a cell's mask.
constexpr int32_t kSideMinusY = 1, kSideMinusX = 2, kSidePlusY = 4, kSidePlusX = 8, kDiagonalA = 0x10, kDiagonalB = 0x20;

// The distance of a non-negative float from 1.0 in units in the last place, the way the original measures it (the sign-magnitude
// bits turned into an ordered integer), then folded so that only |distance| < 32 gives a value under 32.
bool NearOne(float v)
{
    const float magnitude = std::fabs(v);
    uint32_t bits;
    std::memcpy(&bits, &magnitude, sizeof(bits));
    const uint32_t sign = bits >> 31;
    const int32_t distance = static_cast<int32_t>((sign - 0x3f800000u) + (((0u - sign) >> 1) ^ bits));
    return ((distance >> 26 ^ distance) & ~31) == 0;
}

} // namespace

worldmath::TileQuadrant *__cdecl CellToQuadrants(worldmath::TileQuadrant *out, const int32_t *cell)
{
    out->x = cell[kCellX];
    out->y = cell[kCellY];
    out->level = cell[kCellLevel];
    switch (cell[kCellMask]) {
    case kSideMinusY: out->quadrant = 8; break;
    case kSideMinusX: out->quadrant = 1; break;
    case kSidePlusY: out->quadrant = 2; break;
    case kSidePlusX: out->quadrant = 4; break;
    case kDiagonalA: out->quadrant = cell[kCellKind] == 2 ? 9 : 6; break;
    case kDiagonalB: out->quadrant = cell[kCellKind] == 1 ? 0xc : 3; break;
    default: out->quadrant = 0; break;
    }
    return out;
}

worldmath::TileQuadrant *__cdecl SegmentToQuadrants(worldmath::TileQuadrant *out, const float *rect)
{
    int32_t cell[kCellWords];
    CellOfRect(cell, rect);
    return CellToQuadrants(out, cell);
}

worldmath::TileQuadrant *__cdecl ReversedSegmentToQuadrants(worldmath::TileQuadrant *out, const float *rect)
{
    float reversed[6];
    std::memcpy(reversed, rect + 3, 3 * sizeof(float));
    std::memcpy(reversed + 3, rect, 3 * sizeof(float));
    return SegmentToQuadrants(out, reversed);
}

bool __cdecl IsUnitSegment(const float *rect) { return NearOne(rect[0] - rect[3]) && NearOne(rect[1] - rect[4]); }

float *__cdecl RotateRect(float *out, const float *rect, const int32_t *facing)
{
    float x0 = rect[0], y0 = rect[1], x1 = rect[3], y1 = rect[4];
    const int32_t level0 = reinterpret_cast<const int32_t *>(rect)[2];
    const int32_t level1 = reinterpret_cast<const int32_t *>(rect)[5];
    switch (*facing) {
    case 0: {
        const float nx0 = y0, nx1 = y1;
        y0 = -x0;
        y1 = -x1;
        x0 = nx0;
        x1 = nx1;
        break;
    }
    case 4: {
        const float nx0 = -y0, nx1 = -y1;
        y0 = x0;
        y1 = x1;
        x0 = nx0;
        x1 = nx1;
        break;
    }
    case 6:
        x0 = -x0;
        y0 = -y0;
        x1 = -x1;
        y1 = -y1;
        break;
    default: break;
    }
    out[0] = x0;
    out[1] = y0;
    reinterpret_cast<int32_t *>(out)[2] = level0;
    out[3] = x1;
    out[4] = y1;
    reinterpret_cast<int32_t *>(out)[5] = level1;
    return out;
}

void __cdecl EdgeEndpoints(const int32_t *cell, float *a, float *b)
{
    int32_t first, second; // tile corners, as worldmath::CornerTile numbers them
    switch (cell[kCellMask]) {
    case kSideMinusY: first = 5, second = 3; break;
    case kSideMinusX: first = 3, second = 1; break;
    case kSidePlusY: first = 1, second = 7; break;
    case kSidePlusX: first = 7, second = 5; break;
    case kDiagonalA:
        if (cell[kCellKind] == 4) first = 5, second = 1;
        else first = 1, second = 5;
        break;
    case kDiagonalB:
        first = cell[kCellKind] == 3 ? 7 : 3;
        second = cell[kCellKind] != 3 ? 7 : 3;
        break;
    default: return;
    }
    float corner[3];
    std::memcpy(a, worldmath::CornerPosition(corner, cell, first), sizeof(corner));
    std::memcpy(b, worldmath::CornerPosition(corner, cell, second), sizeof(corner));
}

float *__cdecl CellToSegment(float *out, const int32_t *cell)
{
    float rect[6] = {-1.0f, -1.0f, 0.0f, -1.0f, -1.0f, 0.0f};
    EdgeEndpoints(cell, rect, rect + 3);
    std::memcpy(out, rect, sizeof(rect));
    return out;
}

float *__cdecl CanonicalSegment(float *out, const float *rect)
{
    int32_t cell[kCellWords];
    CellOfRect(cell, rect);
    const int32_t mask = cell[kCellMask];
    cell[kCellKind] = mask == kDiagonalA ? 2 : mask == kDiagonalB ? 3 : 0;
    if (mask == kSidePlusY) {
        cell[kCellY] += 1;
        cell[kCellMask] = kSideMinusY;
    } else if (mask == kSidePlusX) {
        cell[kCellX] += 1;
        cell[kCellMask] = kSideMinusX;
    }
    return CellToSegment(out, cell);
}

} // namespace worldrect
