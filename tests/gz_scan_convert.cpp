// Differential test for the polygon scan converter 0x9cf0c0 (src/gz/GZScanConvert.cpp; with 0x9cd620 and 0x9ce570;
// docs/subsystems/rendering.md, R5b step 2): random polygons (fractional and integer corners, ties on the top row, flat
// edges, repeated points, wide and narrow ranges, fewer than three points) through the original and through ours, the
// output list compared entry by entry. Both sides reuse one list across cases, so the clearing is checked too.
#include "difftest.h"
#include "gz/GZScanConvert.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace {

struct RawVector {
    PolygonSpan *first = nullptr;
    PolygonSpan *last = nullptr;
    PolygonSpan *end = nullptr;
};

using ConvertFn = void(__thiscall *)(RawVector *, const float *, int32_t);

} // namespace

DIFF_TEST(GZScanConvert_Polygons, 0x9cf0c0)
{
    const auto original = ctx.Original<ConvertFn>(0x9cf0c0);
    RawVector theirs;
    std::vector<PolygonSpan> ours;
    for (uint32_t seed = 0; seed < 40000; ++seed) {
        std::mt19937 rng(seed * 2654435761u + 17);
        const uint32_t shape = seed % 8;
        int32_t count = 3 + static_cast<int32_t>(rng() % 8);
        if (shape == 7) count = static_cast<int32_t>(rng() % 4);
        std::vector<float> points(static_cast<size_t>(count) * 2);
        const float range = shape == 4 ? 3000.0f : shape == 5 ? 6.0f : 90.0f;
        const float base = shape == 3 ? -50.0f : 0.0f;
        for (int32_t i = 0; i < count; ++i) {
            float x = base + static_cast<float>(rng() % 100000) / 100000.0f * range;
            float y = base + static_cast<float>(rng() % 100000) / 100000.0f * range;
            if (shape == 1 || shape == 5) { // integer and half corners (ties, flat edges, exact rounding)
                x = static_cast<float>(static_cast<int32_t>(x * 2.0f)) / 2.0f;
                y = static_cast<float>(static_cast<int32_t>(y * 2.0f)) / 2.0f;
            }
            if (shape == 2 && i >= 2) { // repeated points and shared rows
                x = points[(rng() % i) * 2];
                if (rng() & 1) y = points[(rng() % i) * 2 + 1];
            }
            if (shape == 6) { // a convex-ish ring
                const float angle = 6.2831853f * static_cast<float>(i) / static_cast<float>(count);
                x = 40.0f + std::cos(angle) * (10.0f + static_cast<float>(rng() % 30));
                y = 40.0f + std::sin(angle) * (10.0f + static_cast<float>(rng() % 30));
            }
            points[static_cast<size_t>(i) * 2] = x;
            points[static_cast<size_t>(i) * 2 + 1] = y;
        }
        // Stale rows in both lists.
        if (theirs.last != theirs.first) theirs.last = theirs.first + (rng() % (theirs.last - theirs.first + 1));
        original(&theirs, points.data(), count);
        GZScanConvertPolygon(&ours, points.data(), count);
        const size_t theirCount = static_cast<size_t>(theirs.last - theirs.first);
        bool same = theirCount == ours.size();
        for (size_t i = 0; same && i < theirCount; ++i) {
            same = std::memcmp(&theirs.first[i], &ours[i], sizeof(PolygonSpan)) == 0;
        }
        if (!same) {
            ctx.Fail("seed %u shape %u count %d: original %zu rows, ours %zu", seed, shape, count, theirCount,
                     ours.size());
            for (size_t i = 0; i < theirCount && i < ours.size(); ++i) {
                if (std::memcmp(&theirs.first[i], &ours[i], sizeof(PolygonSpan)) != 0) {
                    ctx.Fail("  row %zu: original y %d [%d, %d), ours y %d [%d, %d)", i, theirs.first[i].y,
                             theirs.first[i].x0, theirs.first[i].x1, ours[i].y, ours[i].x0, ours[i].x1);
                    break;
                }
            }
            return;
        }
    }
}
