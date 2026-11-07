#pragma once
// The polygon scan converter 0x9cf0c0 (docs/subsystems/rendering.md, R5b step 2) with its two helpers (0x9cd620 builds
// the working vertex list, 0x9ce570 walks the two edges). Points are float (x, y) pairs, already in subsamples; the output
// is one entry per subsample row: the x range covered, end exclusive (x0 from the left edge rounded down, x1 from the
// right edge rounded up).
#include <cstdint>
#include <vector>

struct PolygonSpan {
    int32_t y;
    int16_t x0;
    int16_t x1;
};
static_assert(sizeof(PolygonSpan) == 8, "PolygonSpan: 8 bytes");

// Clears `out`, then fills it. Fewer than three points give an empty list. The polygon is walked from its topmost point
// (the first one when several tie); the left edge is the walk forward, the right edge the walk backward.
void GZScanConvertPolygon(std::vector<PolygonSpan> *out, const float *points, int32_t count);

// The hook's entry (0x9cf0c0: ecx = the output vector, then the points and the count, callee pops 8).
struct GZScanConvertAdapters {
    static void __fastcall Convert(std::vector<PolygonSpan> *out, void *, const float *points, int32_t count);
};
