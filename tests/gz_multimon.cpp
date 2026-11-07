// Differential tests for the multi-monitor shim (lane OS, rendering plan 4c): src/gz/MultiMon.h, multimon.h's stubs
// InitMultipleMonitorStubs 0x1547f0, EnumDisplayMonitors 0x154950 and MonitorFromWindow 0x154ae0.
//
// Each case presets the game's statics (the seven USER32 pointers, the "initialised" flag and the NT flag: drawfx::Cells,
// restored afterwards) and fakes every Win32 import the stubs call (impfx: GetVersionExA, GetModuleHandleA,
// GetProcAddress, GetSystemMetrics, GetClipBox, GetDCOrgEx, OffsetRect, IntersectRect, IsIconic, GetWindowPlacement,
// GetWindowRect), the same for both sides. USER32's functions are recording stdcall fakes that GetProcAddress hands out
// (a seeded one missing, or the module, or none). Compared: the call log (imports and USER32 fakes with their arguments,
// the enumeration callback's calls), the results and the statics afterwards.
#include "difftest.h"
#include "drawfx.h"
#include "impfx.h"

#include "game/game.h"
#include "gz/MultiMon.h"

#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kStatics[7] = {0x10c96b8, 0x10c96bc, 0x10c96c0, 0x10c96c4, 0x10c96cc, 0x10c96c8, 0x10c96d0};
constexpr uint32_t kInitDone = 0x10c96d4, kPlatformNT = 0x10c96d8;
constexpr uint32_t kNameRvas[7] = {0xd9c9a8, 0xd9c9bc, 0xd9c9d0, 0xd9c9e0, 0xd9c9f4, 0xd9ca08, 0xd9ca18};

struct Script {
    uint32_t platform = 2;
    bool moduleMissing = false;
    int missing = -1; // the index of the USER32 function GetProcAddress does not find
    uint32_t metrics[2] = {640, 480};
    uint32_t clipResult = 2;
    RECT clip{}, windowRect{}, placementRect{}, ask{};
    POINT origin{};
    bool originOk = true, iconic = false, windowOk = true, placementOk = true;
    uint32_t enumResult = 1, callbackResult = 1;
    uint32_t monitorResult = 0x5e000001;
} gScript;

// USER32's fakes: what GetProcAddress hands out. Each logs its arguments and answers from the script.
BOOL WINAPI FakeEnumDisplayMonitors(HDC dc, const RECT *clip, MONITORENUMPROC proc, LPARAM data)
{
    drawfx::Note("user32.EnumDisplayMonitors", {drawfx::Norm(drawfx::P(dc)), drawfx::P(clip), drawfx::P(proc),
                                                uint32_t(data)});
    if (clip) drawfx::Note("clip", {uint32_t(clip->left), uint32_t(clip->top), uint32_t(clip->right), uint32_t(clip->bottom)});
    return gScript.enumResult;
}
HMONITOR WINAPI FakeMonitorFromWindow(HWND window, DWORD flags)
{
    drawfx::Note("user32.MonitorFromWindow", {drawfx::P(window), flags});
    return reinterpret_cast<HMONITOR>(uintptr_t(gScript.monitorResult));
}
HMONITOR WINAPI FakeMonitorFromRect(const RECT *rect, DWORD flags)
{
    drawfx::Note("user32.MonitorFromRect", {uint32_t(rect->left), uint32_t(rect->top), uint32_t(rect->right),
                                            uint32_t(rect->bottom), flags});
    return reinterpret_cast<HMONITOR>(uintptr_t(gScript.monitorResult ^ 0x10));
}
int WINAPI FakeGetSystemMetrics(int index)
{
    drawfx::Note("user32.GetSystemMetrics", uint32_t(index));
    return int(gScript.metrics[index & 1]);
}
int WINAPI FakeOther() { return 0; }
void *const kUser32Fakes[7] = {reinterpret_cast<void *>(&FakeGetSystemMetrics), reinterpret_cast<void *>(&FakeMonitorFromWindow),
                               reinterpret_cast<void *>(&FakeMonitorFromRect),  reinterpret_cast<void *>(&FakeOther),
                               reinterpret_cast<void *>(&FakeEnumDisplayMonitors), reinterpret_cast<void *>(&FakeOther),
                               reinterpret_cast<void *>(&FakeOther)};

// The enumeration callback the stub calls on the fallback.
BOOL CALLBACK Callback(HMONITOR monitor, HDC dc, LPRECT rect, LPARAM data)
{
    drawfx::Note("callback", {uint32_t(reinterpret_cast<uintptr_t>(monitor)), drawfx::Norm(drawfx::P(dc)),
                              uint32_t(rect->left), uint32_t(rect->top), uint32_t(rect->right), uint32_t(rect->bottom),
                              uint32_t(data)});
    return gScript.callbackResult;
}

// ---- import handlers ----
uint32_t GetVersionExH(drawfx::Fake &, const uint32_t *a)
{
    auto *v = reinterpret_cast<uint8_t *>(uintptr_t(a[0]));
    uint32_t sum = 0;
    for (unsigned i = 4; i < 0x94; ++i) sum = sum * 31 + v[i]; // (the original zeroes 0x90 bytes after the size)
    drawfx::Note("osversion", {*reinterpret_cast<uint32_t *>(v), sum});
    *reinterpret_cast<uint32_t *>(v + 16) = gScript.platform;
    return 1;
}
uint32_t GetModuleHandleH(drawfx::Fake &, const uint32_t *) { return gScript.moduleMissing ? 0 : 0x77700000; }
uint32_t GetProcAddressH(drawfx::Fake &, const uint32_t *a)
{
    const char *name = reinterpret_cast<const char *>(uintptr_t(a[1]));
    for (unsigned i = 0; i < 7; ++i)
        if (std::strcmp(name, GameAddress<const char>(kNameRvas[i])) == 0)
            return int(i) == gScript.missing ? 0 : drawfx::P(kUser32Fakes[i]);
    return 0;
}
uint32_t GetSystemMetricsH(drawfx::Fake &, const uint32_t *a) { return gScript.metrics[a[0] & 1]; }
uint32_t GetClipBoxH(drawfx::Fake &, const uint32_t *a)
{
    *reinterpret_cast<RECT *>(uintptr_t(a[1])) = gScript.clip;
    return gScript.clipResult;
}
uint32_t GetDCOrgExH(drawfx::Fake &, const uint32_t *a)
{
    *reinterpret_cast<POINT *>(uintptr_t(a[1])) = gScript.origin;
    return gScript.originOk;
}
uint32_t OffsetRectH(drawfx::Fake &, const uint32_t *a)
{
    return ::OffsetRect(reinterpret_cast<RECT *>(uintptr_t(a[0])), int(a[1]), int(a[2]));
}
uint32_t IntersectRectH(drawfx::Fake &, const uint32_t *a)
{
    return ::IntersectRect(reinterpret_cast<RECT *>(uintptr_t(a[0])), reinterpret_cast<const RECT *>(uintptr_t(a[1])),
                           reinterpret_cast<const RECT *>(uintptr_t(a[2])));
}
uint32_t IsIconicH(drawfx::Fake &, const uint32_t *) { return gScript.iconic; }
uint32_t GetWindowPlacementH(drawfx::Fake &, const uint32_t *a)
{
    // (the stub never sets the structure's length; only the rectangle it reads is written)
    if (gScript.placementOk) *reinterpret_cast<RECT *>(uintptr_t(a[1]) + 0x1c) = gScript.placementRect;
    return gScript.placementOk;
}
uint32_t GetWindowRectH(drawfx::Fake &, const uint32_t *a)
{
    if (gScript.windowOk) *reinterpret_cast<RECT *>(uintptr_t(a[1])) = gScript.windowRect;
    return gScript.windowOk;
}

uint32_t Pick(std::mt19937 &g, std::initializer_list<uint32_t> v) { return v.begin()[g() % v.size()]; }
LONG Coord(std::mt19937 &g) { return LONG(int32_t(g() % 1400) - 300); }
RECT RandomRect(std::mt19937 &g)
{
    RECT r;
    r.left = Coord(g);
    r.top = Coord(g);
    r.right = r.left + LONG(g() % 5 == 0 ? 0 : int32_t(g() % 1200));
    r.bottom = r.top + LONG(g() % 5 == 0 ? 0 : int32_t(g() % 900));
    return r;
}

void SeedScript(uint32_t seed)
{
    std::mt19937 g(seed);
    Script &s = gScript;
    s = Script{};
    s.platform = Pick(g, {1, 2, 2, 3});
    s.moduleMissing = g() % 8 == 0;
    s.missing = g() % 3 == 0 ? int(g() % 7) : -1;
    s.metrics[0] = Pick(g, {640, 1024, 1920, 300});
    s.metrics[1] = Pick(g, {480, 768, 1080, 200});
    s.clipResult = Pick(g, {0, 1, 2, 3, 2, 2});
    s.clip = RandomRect(g);
    s.windowRect = RandomRect(g);
    s.placementRect = RandomRect(g);
    // Rectangles touching the screen's edges (the four comparisons of the fallback are strict).
    for (RECT *r : {&s.windowRect, &s.placementRect}) {
        switch (g() % 8) {
        case 0: r->right = 0; break;
        case 1: r->bottom = 0; break;
        case 2: r->left = LONG(s.metrics[0]); break;
        case 3: r->top = LONG(s.metrics[1]); break;
        default: break;
        }
    }
    s.ask = RandomRect(g);
    s.origin = POINT{Coord(g) / 4, Coord(g) / 4};
    s.originOk = g() % 6 != 0;
    s.iconic = g() % 2;
    s.windowOk = g() % 6 != 0;
    s.placementOk = g() % 6 != 0;
    s.enumResult = g() % 2;
    s.callbackResult = Pick(g, {0, 1, 7});
    s.monitorResult = Pick(g, {0, 0x5e000001, 0x5e000002});
}

// The statics before a call: not initialised, or initialised with USER32's functions (the fakes), or with none.
void SeedStatics(std::mt19937 &g, drawfx::Cells &cells)
{
    const unsigned mode = g() % 4; // 0, 1: not initialised; 2: initialised with all; 3: initialised with none
    for (unsigned i = 0; i < 7; ++i)
        cells.Set(kStatics[i], mode == 2 ? kUser32Fakes[i] : mode == 0 ? kUser32Fakes[(i + 1) % 7] : nullptr);
    cells.Set(kInitDone, mode >= 2 ? 1u : 0u);
    cells.Set(kPlatformNT, uint32_t(g() % 2));
}

void NoteStatics()
{
    std::vector<uint32_t> w;
    for (uint32_t rva : kStatics) w.push_back(*GameAddress<uint32_t>(rva));
    w.push_back(*GameAddress<uint32_t>(kInitDone));
    w.push_back(*GameAddress<uint32_t>(kPlatformNT));
    drawfx::Call c;
    c.note = "statics";
    c.words = std::move(w);
    drawfx::gLog.push_back(std::move(c));
}

struct World {
    drawfx::Cells cells;
    impfx::Imports imp;
    explicit World(difftest::Context &ctx) : imp(ctx)
    {
        IMPFX_ADD(imp, GetVersionExA).On(&GetVersionExH);
        IMPFX_ADD(imp, GetModuleHandleA, "s").On(&GetModuleHandleH);
        IMPFX_ADD(imp, GetProcAddress, "is").On(&GetProcAddressH);
        IMPFX_ADD(imp, GetSystemMetrics, "u").On(&GetSystemMetricsH);
        IMPFX_ADD(imp, GetClipBox).On(&GetClipBoxH);
        IMPFX_ADD(imp, GetDCOrgEx).On(&GetDCOrgExH);
        IMPFX_ADD(imp, OffsetRect, "rii").On(&OffsetRectH);
        IMPFX_ADD(imp, IntersectRect, "irr").On(&IntersectRectH);
        IMPFX_ADD(imp, IsIconic).On(&IsIconicH);
        IMPFX_ADD(imp, GetWindowPlacement).On(&GetWindowPlacementH);
        IMPFX_ADD(imp, GetWindowRect).On(&GetWindowRectH);
    }
};

struct Seen {
    unsigned fallback = 0, real = 0, init = 0;
};

template <class Run> void Case(difftest::Context &ctx, World &w, const char *what, uint32_t seed, Seen &seen, Run run)
{
    auto side = [&](bool original) {
        drawfx::Cells cells;
        std::mt19937 g(seed * 5 + 1);
        SeedScript(seed);
        auto result = drawfx::Side(
            [&] {
                w.imp.BeginSide();
                SeedStatics(g, cells);
            },
            [&] {
                run(original);
                NoteStatics();
            });
        return result;
    };
    auto a = side(true);
    auto b = side(false);
    for (const drawfx::Call &c : a.first) {
        if (!c.iface && std::strcmp(c.note, "callback") == 0) ++seen.fallback;
        if (!c.iface && std::strncmp(c.note, "user32.", 7) == 0) ++seen.real;
        if (!c.iface && std::strcmp(c.note, "osversion") == 0) ++seen.init;
    }
    std::vector<std::string> errors = a.second;
    errors.insert(errors.end(), b.second.begin(), b.second.end());
    drawfx::Same(ctx, a.first, b.first, what, seed, errors);
}

using InitFn = bool(__cdecl *)();
using EnumFn = BOOL(__stdcall *)(HDC, const RECT *, MONITORENUMPROC, LPARAM);
using FromWindowFn = HMONITOR(__stdcall *)(HWND, DWORD);

} // namespace

DIFF_TEST(gz_MultiMon_Init, 0x1547f0)
{
    World w(ctx);
    Seen seen;
    for (uint32_t seed = 1; seed <= 600; ++seed) {
        Case(ctx, w, "Init", seed, seen, [&](bool original) {
            const bool r = original ? ctx.Original<InitFn>(0x1547f0)() : nMultiMon::InitMultipleMonitorStubs();
            drawfx::Note("result", r);
            // a second call: the early return
            const bool again = original ? ctx.Original<InitFn>(0x1547f0)() : nMultiMon::InitMultipleMonitorStubs();
            drawfx::Note("again", again);
        });
    }
    if (!seen.init) ctx.Fail("Init: the resolving path never ran");
}

DIFF_TEST(gz_MultiMon_Enum, 0x154950)
{
    World w(ctx);
    Seen seen;
    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        Case(ctx, w, "Enum", seed, seen, [&](bool original) {
            std::mt19937 g(seed * 7 + 2);
            const HDC dc = g() % 3 == 0 ? nullptr : reinterpret_cast<HDC>(uintptr_t(0x4a0000a0));
            const RECT ask = gScript.ask;
            const RECT *clip = g() % 2 ? &ask : nullptr;
            const MONITORENUMPROC proc = g() % 10 == 0 ? nullptr : &Callback;
            const LPARAM data = LPARAM(0x1234 + g() % 5);
            const BOOL r = original ? ctx.Original<EnumFn>(0x154950)(dc, clip, proc, data)
                                    : nMultiMon::EnumDisplayMonitorsStub(dc, clip, proc, data);
            drawfx::Note("result", uint32_t(r));
        });
    }
    if (!seen.fallback || !seen.real) ctx.Fail("Enum: a path never ran (fallback %u, real %u)", seen.fallback, seen.real);
}

DIFF_TEST(gz_MultiMon_FromWindow, 0x154ae0)
{
    World w(ctx);
    Seen seen;
    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        Case(ctx, w, "FromWindow", seed, seen, [&](bool original) {
            std::mt19937 g(seed * 11 + 3);
            const HWND window = reinterpret_cast<HWND>(uintptr_t(0x4a0000c0));
            const DWORD flags = Pick(g, {0, 0, 1, 2, 3});
            const HMONITOR r = original ? ctx.Original<FromWindowFn>(0x154ae0)(window, flags)
                                        : nMultiMon::MonitorFromWindowStub(window, flags);
            drawfx::Note("result", uint32_t(reinterpret_cast<uintptr_t>(r)));
        });
    }
    if (!seen.real) ctx.Fail("FromWindow: the USER32 path never ran");
}

// GetSystemMetrics 0x154a80 (lane RB): the indices the fallback answers itself (0x4c-0x51), the ones it maps to the
// screen's and others, on the real and fallback paths; the import's and USER32's calls are logged with their index.
DIFF_TEST(gz_MultiMon_SystemMetrics, 0x154a80)
{
    using MetricsFn = int(__stdcall *)(int);
    World w(ctx);
    Seen seen;
    unsigned fallback = 0;
    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        Case(ctx, w, "SystemMetrics", seed, seen, [&](bool original) {
            std::mt19937 g(seed * 13 + 4);
            const int index = g() % 3 ? int(0x4a + g() % 10) : int(Pick(g, {0, 1, 2, 0x1f, 0x50, 0x1000, 0xffffffffu}));
            const int r = original ? ctx.Original<MetricsFn>(0x154a80)(index) : nMultiMon::GetSystemMetricsStub(index);
            drawfx::Note("result", uint32_t(r));
            if (original && *GameAddress<uint32_t>(0x10c96c8) == 0) ++fallback;
        });
    }
    if (!seen.real || !fallback) ctx.Fail("SystemMetrics: a path never ran (real %u, fallback %u)", seen.real, fallback);
}
