// Differential tests for cGZBuffer's file-local helpers (src/gz/cGZBuffer.h): the colour mask 0x8c7ca0 (hooked: the pixel
// converter 0x8c6b10 calls it too), the bytes per pixel of a format 0x8c76f0 (BltUnclipped's) and the line clipper 0x8c7a50
// (DrawLine's). All three are cdecl leaves with no side effects beyond their out arguments.
#include "difftest.h"
#include "gz/cGZBuffer.h"

#include <cstdint>
#include <random>

namespace {

// Formats around the known ones (0..9), plus a few far ones.
const uint32_t kFormats[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 15, 16, 0x100, 0x7fffffff, 0x80000000u, 0xffffffffu};

} // namespace

DIFF_TEST(GZBuffer_FormatMask, 0x8c7ca0)
{
    using Fn = uint32_t(__cdecl *)(uint32_t);
    const Fn original = ctx.Original<Fn>(0x8c7ca0);
    for (uint32_t format : kFormats) {
        const uint32_t a = original(format), b = cGZBuffer::FormatMask(format);
        ++ctx.cases;
        if (a != b) ctx.Fail("format %u: original %08x, ours %08x", format, a, b);
    }
}

DIFF_TEST(GZBuffer_BytesPerPixelOf, 0x8c76f0)
{
    using Fn = uint32_t(__cdecl *)(uint32_t);
    const Fn original = ctx.Original<Fn>(0x8c76f0);
    for (uint32_t format : kFormats) {
        const uint32_t a = original(format), b = cGZBuffer::BytesPerPixelOf(format);
        ++ctx.cases;
        if (a != b) ctx.Fail("format %u: original %u, ours %u", format, a, b);
    }
}

DIFF_TEST(GZBuffer_ClipLine, 0x8c7a50)
{
    using Fn = bool(__cdecl *)(int32_t *, int32_t *, int32_t *, int32_t *, int32_t, int32_t, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(0x8c7a50);
    std::mt19937 rng(0x8c7a50);
    const auto draw = [&](int32_t lo, int32_t hi) { return lo + int32_t(rng() % uint32_t(hi - lo + 1)); };
    for (int i = 0; i < 200000; ++i) {
        // A window as DrawLine passes it (0, 0, width - thickness, height - thickness), sometimes empty or inverted.
        const int32_t left = i % 7 == 0 ? draw(-5, 5) : 0, top = i % 11 == 0 ? draw(-5, 5) : 0;
        const int32_t right = draw(-3, 300), bottom = draw(-3, 300);
        // Ends inside, near or far outside the window; some lines horizontal, vertical or a single point. The slope
        // products stay inside 32 bits (the original divides with idiv: a quotient past 32 bits would fault both sides).
        const int32_t span = i % 5 == 0 ? 20000 : 400;
        int32_t p[4] = {draw(-span, span), draw(-span, span), draw(-span, span), draw(-span, span)};
        switch (rng() % 8) {
        case 0: p[3] = p[1]; break;
        case 1: p[2] = p[0]; break;
        case 2: p[2] = p[0], p[3] = p[1]; break;
        default: break;
        }
        int32_t a[4] = {p[0], p[1], p[2], p[3]}, b[4] = {p[0], p[1], p[2], p[3]};
        const bool ra = original(&a[0], &a[1], &a[2], &a[3], left, top, right, bottom);
        const bool rb = cGZBuffer::ClipLine(&b[0], &b[1], &b[2], &b[3], left, top, right, bottom);
        ++ctx.cases;
        if (ra != rb || a[0] != b[0] || a[1] != b[1] || a[2] != b[2] || a[3] != b[3]) {
            ctx.Fail("(%d,%d)-(%d,%d) in %d %d %d %d: original %d (%d,%d)-(%d,%d), ours %d (%d,%d)-(%d,%d)", p[0], p[1], p[2],
                     p[3], left, top, right, bottom, ra, a[0], a[1], a[2], a[3], rb, b[0], b[1], b[2], b[3]);
            return;
        }
    }
}
