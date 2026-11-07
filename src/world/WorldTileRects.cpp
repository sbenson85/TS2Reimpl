// The tile rects of nTSWorld's helper module (lane RY): src/world/WorldTileRects.h. Built for speed (/O2).
#include "world/WorldTileRects.h"

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

} // namespace worldrect
