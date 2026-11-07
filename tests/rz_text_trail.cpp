// Differential tests for the trail byte counts (src/rz/RZTextTrail.*; lane VU, for the canvas's WM_CHAR handler): UTF-8's
// (0x1b2817), a code page's (0x1b0dc9) and by encoding (0x1b06a6). Every byte 0-0x1ff (the high bits must not count)
// and seeded words, against code pages and encodings including -2 / -1 (the system code page preset to several pages,
// UTF-8 among them, so the original never calls GetACP). Compared: the whole eax.
#include "difftest.h"
#include "game/game.h"
#include "rz/RZTextTrail.h"

#include <cstdint>
#include <random>

namespace {

struct CodePage {
    int32_t *global = GameAddress<int32_t>(0xff23a4);
    int32_t saved = *global;
    explicit CodePage(int32_t page) { *global = page; }
    ~CodePage() { *global = saved; }
};

using Fn1 = uint32_t(__cdecl *)(uint32_t);
using Fn2 = uint32_t(__cdecl *)(uint32_t, int32_t);

const int32_t kPages[] = {932, 936, 949, 950, 1252, 0, 8, 16, -1, -2, 99, 100, 101, 874, 65001, 0x7fffffff,
                          static_cast<int32_t>(0x80000000u), 12345};

uint32_t Byte(std::mt19937 &rng, uint32_t i) { return i < 0x200 ? i : static_cast<uint32_t>(rng()); }

void Check2(difftest::Context &ctx, Fn2 orig, Fn2 ours, const char *what, uint32_t seed)
{
    std::mt19937 rng(seed);
    for (int32_t page : kPages)
        for (uint32_t i = 0; i < 0x300; ++i) {
            const uint32_t byte = Byte(rng, i);
            ++ctx.cases;
            const uint32_t a = orig(byte, page), b = ours(byte, page);
            if (a != b) {
                ctx.Fail("%s(%x, %d): %u / %u", what, byte, page, a, b);
                return;
            }
        }
    for (int i = 0; i < 3000; ++i) {
        const uint32_t byte = static_cast<uint32_t>(rng());
        const int32_t page = rng() % 3 ? kPages[rng() % 18] : static_cast<int32_t>(rng());
        ++ctx.cases;
        const uint32_t a = orig(byte, page), b = ours(byte, page);
        if (a != b) {
            ctx.Fail("%s(%x, %d): %u / %u", what, byte, page, a, b);
            return;
        }
    }
}

} // namespace

DIFF_TEST(RZTextTrail_Utf8, 0x1b2817)
{
    const auto original = ctx.Original<Fn1>(0x1b2817);
    std::mt19937 rng(0x1b2817);
    for (uint32_t i = 0; i < 0x2000; ++i) {
        const uint32_t byte = Byte(rng, i);
        ++ctx.cases;
        const uint32_t a = original(byte), b = RZText::Utf8TrailCount(byte);
        if (a != b) {
            ctx.Fail("utf-8 trail of %x: %u / %u", byte, a, b);
            return;
        }
    }
}

DIFF_TEST(RZTextTrail_CodePage, 0x1b0dc9)
{
    CodePage page(1252);
    Check2(ctx, ctx.Original<Fn2>(0x1b0dc9), &RZText::CodePageTrailCount, "code page trail", 0x1b0dc9);
}

DIFF_TEST(RZTextTrail_ByEncoding, 0x1b06a6)
{
    static const int32_t kSystem[] = {1252, 932, 949, 8, 0, 950};
    for (int32_t system : kSystem) {
        CodePage page(system);
        Check2(ctx, ctx.Original<Fn2>(0x1b06a6), &RZText::TrailCount, "trail", 0x1b06a6 + system);
    }
}
