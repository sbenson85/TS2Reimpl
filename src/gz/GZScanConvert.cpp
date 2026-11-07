#include "gz/GZScanConvert.h"

#include <cmath>
#include <emmintrin.h>

namespace {

// The original's float-to-int (_ftoi2): truncation, 0x80000000 when it does not fit.
int32_t ToInt(double value) { return _mm_cvttsd_si32(_mm_set_sd(value)); }

struct Vertex {
    float x, y; // the point less half a subsample
    int32_t top; // y rounded up: the first subsample row the point lies on or above
};

// One edge from `from` to `to` at row `row`: x there as 16.16 (plus 0xffff, so a later shift rounds up) and its step per row.
void Edge(const Vertex &from, const Vertex &to, int32_t row, int32_t *x, int32_t *step)
{
    const float slopeFloat = (to.x - from.x) / (to.y - from.y);
    const double slope = slopeFloat;
    const double start = ((static_cast<double>(row) - static_cast<double>(from.y)) * slope + static_cast<double>(from.x)) *
                             65536.0 + 0.5;
    *x = static_cast<int32_t>(static_cast<uint32_t>(ToInt(std::floor(start))) + 0xffffu);
    *step = ToInt(std::floor(slope * 65536.0 + 0.5));
}

} // namespace

void GZScanConvertPolygon(std::vector<PolygonSpan> *out, const float *points, int32_t count)
{
    out->clear();
    if (count < 3) return;

    // The vertices from the topmost on, round the polygon.
    std::vector<Vertex> vertices(static_cast<size_t>(count));
    int32_t first = 0;
    for (int32_t i = 1; i < count; ++i) {
        if (points[first * 2 + 1] > points[i * 2 + 1]) first = i;
    }
    int32_t at = first;
    for (int32_t k = 0; k < count; ++k) {
        Vertex &vertex = vertices[static_cast<size_t>(k)];
        vertex.x = points[at * 2] - 0.5f;
        vertex.y = points[at * 2 + 1] - 0.5f;
        vertex.top = ToInt(static_cast<float>(std::ceil(static_cast<double>(vertex.y))));
        at = at + 1 < count ? at + 1 : 0;
    }

    int32_t row = vertices[0].top;
    int32_t leftFrom = 0, leftTo = 1, rightFrom = 0, rightTo = count - 1;
    int32_t leftEnd = row, rightEnd = row; // the row each edge ends on (the row it is replaced on)
    int32_t leftX = 0, rightX = 0, leftStep = 0, rightStep = 0;
    for (;;) {
        if (leftEnd == row) {
            // Skip the vertices not below this row; the edge runs from the last of them to the next.
            for (;;) {
                if (leftTo >= count) return;
                leftEnd = vertices[static_cast<size_t>(leftTo)].top;
                if (leftEnd > row) break;
                leftFrom = leftTo;
                ++leftTo;
            }
            Edge(vertices[static_cast<size_t>(leftFrom)], vertices[static_cast<size_t>(leftTo)], row, &leftX, &leftStep);
            leftFrom = leftTo;
            ++leftTo;
        }
        if (rightEnd == row) {
            if (rightTo < 0) return;
            int32_t top = vertices[static_cast<size_t>(rightTo)].top;
            while (top <= row) {
                if (top < row) return;
                rightFrom = rightTo;
                if (--rightTo < 0) return; // the original reads before the list here
                top = vertices[static_cast<size_t>(rightTo)].top;
            }
            Edge(vertices[static_cast<size_t>(rightFrom)], vertices[static_cast<size_t>(rightTo)], row, &rightX,
                 &rightStep);
            rightEnd = top;
            rightFrom = rightTo;
            --rightTo;
        }
        out->push_back({row, static_cast<int16_t>(leftX >> 16), static_cast<int16_t>(rightX >> 16)});
        leftX = static_cast<int32_t>(static_cast<uint32_t>(leftX) + static_cast<uint32_t>(leftStep));
        rightX = static_cast<int32_t>(static_cast<uint32_t>(rightX) + static_cast<uint32_t>(rightStep));
        ++row;
    }
}

void __fastcall GZScanConvertAdapters::Convert(std::vector<PolygonSpan> *out, void *, const float *points, int32_t count)
{
    GZScanConvertPolygon(out, points, count);
}
