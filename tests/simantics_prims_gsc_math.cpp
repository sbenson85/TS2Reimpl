// Differential test for the pure helper of GenericSimCall case 34: the centre of a rectangle (0x57d6d0). The helper is
// src/simantics/PrimGenericSimCallMath.h (inline in the case, not hooked).
#include "difftest.h"
#include "simantics/PrimGenericSimCallMath.h"

#include <random>

DIFF_TEST(simantics_gsc_rect_center, 0x57d6d0)
{
    using Fn = gscmath::Pair *(__thiscall *)(const int32_t *, gscmath::Pair *);
    Fn orig = ctx.Original<Fn>(0x57d6d0);
    std::mt19937 rng(0x57d6d0);
    const int32_t edges[] = {0, 1, -1, 2, -2, 3, -3, 0x7fffffff, int32_t(0x80000000), 0x40000000, -0x40000000, 0x7ffffffe};
    for (int i = 0; i < 40000; ++i) {
        int32_t rect[4];
        for (int32_t &v : rect) {
            switch (rng() % 3) {
            case 0: v = edges[rng() % (sizeof(edges) / sizeof(edges[0]))]; break;
            case 1: v = int32_t(rng() % 2001) - 1000; break;
            default: v = int32_t(rng()); break;
            }
        }
        gscmath::Pair a{int32_t(0xdeadbeef), int32_t(0xcafef00d)};
        gscmath::Pair *ret = orig(rect, &a);
        const gscmath::Pair b = gscmath::RectCenter(rect);
        ++ctx.cases;
        if (ret != &a) ctx.Fail("case %d: the original did not return its output pointer", i);
        if (a.x != b.x || a.y != b.y) {
            ctx.Fail("case %d: rect %d %d %d %d: original (%d,%d), ours (%d,%d)", i, rect[0], rect[1], rect[2], rect[3], a.x, a.y, b.x, b.y);
        }
    }
}
