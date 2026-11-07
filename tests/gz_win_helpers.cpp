// GZWin free helpers (src/ui/GZWinHelpers*.cpp) against the original, through the drawfx / winfx recording fakes
// (tests/drawfx.h, tests/winfx.h; docs/ui-testing.md). The draw helpers draw into a fake cIGZDrawContext: the logs
// (slot, colour, rect contents) of both sides are compared call by call. The window helpers act on a fake window tree:
// the logs and the windows' state afterwards are compared.
#include "difftest.h"
#include "drawfx.h"
#include "winfx.h"

#include "ui/GZWinHelpers.h"

#include <random>

namespace {

using FrameFn = void(__cdecl *)(void *, const int32_t *, uint32_t, int32_t);
using BevelFn = void(__cdecl *)(void *, const int32_t *, uint32_t, uint32_t);
using ShadeFn = uint32_t(__cdecl *)(uint32_t, uint32_t, uint32_t, int32_t);
using WindowFn = void(__cdecl *)(void *);
using ListFn = void(__cdecl *)(void *, const uint32_t *, uint32_t, uint32_t);
using RangeFn = void(__cdecl *)(void *, uint32_t, uint32_t, uint32_t);

// A rect with edges anywhere in -100..700, sometimes inverted or empty, and a thickness from -3 to 40.
void RandomRect(std::mt19937 &rng, int32_t *rect)
{
    for (int i = 0; i < 4; ++i) rect[i] = int32_t(rng() % 800) - 100;
    if (rng() % 4) {
        if (rect[0] > rect[2]) std::swap(rect[0], rect[2]);
        if (rect[1] > rect[3]) std::swap(rect[1], rect[3]);
    }
    if (rng() % 10 == 0) rect[2] = rect[0];
}

drawfx::Fake gContext;

template <class Draw> std::pair<drawfx::Log, std::vector<std::string>> DrawSide(Draw &&draw)
{
    return drawfx::Side([] { gContext.Reset<fx_slots::cIGZDrawContext>(201); }, draw);
}

} // namespace

DIFF_TEST(GZWinHelpers_DrawFrame, 0x92ef70)
{
    const auto orig = ctx.Original<FrameFn>(0x92ef70);
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        std::mt19937 rng(seed);
        int32_t rect[4];
        RandomRect(rng, rect);
        const uint32_t argb = rng();
        const int32_t thickness = int32_t(rng() % 44) - 3;
        const auto a = DrawSide([&] { orig(&gContext, rect, argb, thickness); });
        const auto b = DrawSide([&] { GZWinDrawFrame(&gContext, rect, argb, thickness); });
        if (!drawfx::Same(ctx, a.first, b.first, "DrawFrame", seed, a.second)) break;
    }
}

DIFF_TEST(GZWinHelpers_DrawBevel, 0x92ee00)
{
    const auto orig = ctx.Original<BevelFn>(0x92ee00);
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        std::mt19937 rng(seed);
        int32_t rect[4];
        RandomRect(rng, rect);
        // Colours near the clamp edges as well as anywhere; `raised` with a zero or non-zero low byte and junk above.
        uint32_t argb = rng();
        if (seed % 3 == 0) argb = (argb & 0xff000000) | (rng() % 2 ? 0x00c8 : 0x0030) << (8 * (rng() % 3));
        const uint32_t raised = (rng() & 0xffffff00) | (rng() % 2 ? rng() % 256 : 0);
        const auto a = DrawSide([&] { orig(&gContext, rect, argb, raised); });
        const auto b = DrawSide([&] { GZWinDrawBevel(&gContext, rect, argb, raised); });
        if (!drawfx::Same(ctx, a.first, b.first, "DrawBevel", seed, a.second)) break;
    }
}

DIFF_TEST(GZWinHelpers_ShadeColor, 0x92f310)
{
    const auto orig = ctx.Original<ShadeFn>(0x92f310);
    const int32_t deltas[] = {0, 1, -1, 0x38, -0x38, 255, -255, 256, -256, 0x7fffffff, INT32_MIN};
    for (uint32_t seed = 0; seed < 20000; ++seed) {
        std::mt19937 rng(seed);
        uint32_t c[3];
        for (uint32_t &x : c) x = (rng() & 0xffffff00) | (rng() % 4 == 0 ? (rng() % 2 ? 0 : 0xff) : rng() % 256);
        const int32_t delta = seed % 2 ? deltas[rng() % std::size(deltas)] : int32_t(rng() % 600) - 300;
        ++ctx.cases;
        const uint32_t want = orig(c[0], c[1], c[2], delta), got = GZWinShadeColor(c[0], c[1], c[2], delta);
        if (want != got) {
            ctx.Fail("ShadeColor(%#x, %#x, %#x, %d): orig %#x ours %#x", c[0], c[1], c[2], delta, want, got);
            break;
        }
    }
}

namespace {

winfx::World gWorld;

// A world of 1..5 windows; with `faces`, most windows answer QueryInterface(0x8810) with a face (fake 400 + index)
// whose slots 6 / 7 (enable / disable) and 2 (Release) the list helpers call.
constexpr uint32_t kEnableIid = 0x8810;
inline constexpr drawfx::Slot kEnableFaceSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", ""}, {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},        {0, drawfx::Ret::Int, false, "", ""},
    {0, drawfx::Ret::Int, false, "", ""},              {0, drawfx::Ret::Int, false, "", ""},
    {0, drawfx::Ret::Int, true, "Enable", ""},         {0, drawfx::Ret::Int, true, "Disable", ""},
};
inline constexpr drawfx::Iface kEnableFace{"iid 0x8810 face", 8, kEnableFaceSlots};
drawfx::Fake gFaces[winfx::World::kMax];

void BuildWorld(uint32_t seed, unsigned windows, bool faces)
{
    gWorld.Build(seed, windows, 0x1000, 8);
    if (!faces) return;
    std::mt19937 rng(seed ^ 0x5eed);
    for (unsigned i = 0; i < gWorld.count; ++i) {
        gFaces[i].Reset<kEnableFace>(400 + i);
        if (rng() % 4) gWorld[i].f.AddFace(kEnableIid, gFaces[i]);
    }
}

std::vector<uint32_t> FaceRefs()
{
    std::vector<uint32_t> out;
    for (unsigned i = 0; i < gWorld.count; ++i) out.push_back(uint32_t(gFaces[i].refs));
    return out;
}

// Runs one side over a fresh world: the log, the errors, and the windows' state (plus the faces' references) after.
struct WinResult {
    drawfx::Log log;
    std::vector<std::string> errors;
    std::vector<uint32_t> state;
};
template <class Run> WinResult WinSide(uint32_t seed, unsigned windows, bool faces, Run &&run)
{
    WinResult r;
    std::vector<uint32_t> state;
    auto side = drawfx::Side([&] { BuildWorld(seed, windows, faces); },
                             [&] {
                                 run();
                                 state = gWorld.State();
                                 if (faces) {
                                     const auto refs = FaceRefs();
                                     state.insert(state.end(), refs.begin(), refs.end());
                                 }
                             });
    r.log = std::move(side.first);
    r.errors = std::move(side.second);
    r.state = std::move(state);
    return r;
}

bool SameWin(difftest::Context &ctx, const WinResult &a, const WinResult &b, const char *what, uint32_t seed)
{
    if (!drawfx::Same(ctx, a.log, b.log, what, seed, a.errors)) return false;
    if (a.state != b.state) {
        ctx.Fail("%s seed %u: window state differs after the call", what, seed);
        return false;
    }
    return true;
}

template <class Ours> void QuietTest(difftest::Context &ctx, uint32_t rva, Ours ours, const char *what)
{
    const auto orig = ctx.Original<WindowFn>(rva);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        const bool null = seed % 10 == 0;
        const auto a = WinSide(seed, 1 + seed % 3, false, [&] { orig(null ? nullptr : &gWorld[0].f); });
        const auto b = WinSide(seed, 1 + seed % 3, false, [&] { ours(null ? nullptr : &gWorld[0].f); });
        if (!SameWin(ctx, a, b, what, seed)) break;
    }
}

} // namespace

DIFF_TEST(GZWinHelpers_HideQuiet, 0x9351b9) { QuietTest(ctx, 0x9351b9, &GZWinHideQuiet, "HideQuiet"); }
DIFF_TEST(GZWinHelpers_ShowQuiet, 0x935329) { QuietTest(ctx, 0x935329, &GZWinShowQuiet, "ShowQuiet"); }

DIFF_TEST(GZWinHelpers_EnableOnlyFromList, 0x93509c)
{
    const auto orig = ctx.Original<ListFn>(0x93509c);
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        std::mt19937 rng(seed);
        uint32_t ids[8];
        const uint32_t count = rng() % 9;
        for (uint32_t &id : ids) id = 0x1000 + rng() % 10; // 0x1008, 0x1009: no window has them
        const uint32_t selected = rng() % 4 ? ids[rng() % 8] : 0x1000 + rng() % 10;
        const unsigned windows = 1 + rng() % 5;
        const auto a = WinSide(seed, windows, true, [&] { orig(&gWorld[0].f, ids, count, selected); });
        const auto b = WinSide(seed, windows, true, [&] { GZWinEnableOnlyFromList(&gWorld[0].f, ids, count, selected); });
        if (!SameWin(ctx, a, b, "EnableOnlyFromList", seed)) break;
    }
}

DIFF_TEST(GZWinHelpers_EnableOnlyInRange, 0x9350d2)
{
    const auto orig = ctx.Original<RangeFn>(0x9350d2);
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        std::mt19937 rng(seed);
        uint32_t first = 0x1000 - 2 + rng() % 10, count = rng() % 9;
        if (seed % 25 == 0) first = 0xfffffffe - rng() % 3, count = 1 + rng() % 5; // the end wraps: nothing is enabled
        const uint32_t selected = first + rng() % 10 - 1;
        const unsigned windows = 1 + rng() % 5;
        const auto a = WinSide(seed, windows, true, [&] { orig(&gWorld[0].f, first, count, selected); });
        const auto b = WinSide(seed, windows, true, [&] { GZWinEnableOnlyInRange(&gWorld[0].f, first, count, selected); });
        if (!SameWin(ctx, a, b, "EnableOnlyInRange", seed)) break;
    }
}
