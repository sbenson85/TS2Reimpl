// Differential tests for the lot imposter's raster helpers (lane 261111q, src/sg/cLotImposterRaster.h): PaintColumn
// 0x3637d0, PaintRow 0x363a10, PaintRect 0x363950, PaintFrame 0x363680 and FadeBorder 0x363490.
//
// The image is a logging fake (cIImageData slots 20 SetPixel, 26 LevelWidth, 27 LevelHeight, 39 GetPixel; any other
// slot aborts). One ordered log per side holds every call with its arguments, the colour's four words as the callee
// sees them and where the colour lives (the caller's array, or the stack for FadeBorder's scaled pixel). The sizes
// change between calls when the script asks (so a size read once instead of each time shows), and a SetPixel can
// change the caller's point or rectangle (so a coordinate read once instead of each time shows). The wrap flag comes
// with junk above its low byte.
//
// ImposterRaster_Run: PaintColumn and PaintRow, wrap and not, over points inside and outside the level, lengths of 0,
// short, as long as the level and longer, both signs.
// ImposterRaster_Rect: PaintRect over every corner order and shape (wide, tall, square, single pixel), outside the
// level too. ImposterRaster_Frame: PaintFrame with thickness 0, positive, negative inside and negative too big.
// ImposterRaster_Fade: FadeBorder with rings <= 0, a null image, and GetPixel answering in its buffer or elsewhere.
#include "difftest.h"

#include "sg/cImageData.h"
#include "sg/cLotImposterRaster.h"

#include <Windows.h>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

namespace raster_test {

using Log = std::vector<uint32_t>;
Log gLog;
void L(uint32_t v) { gLog.push_back(v); }

struct FakeImage {
    void **vt;
    uint32_t id;
};
void *gVT[48];
FakeImage gImage;

struct Script {
    int32_t width, height;
    uint32_t growEvery;   // every n-th size query grows the answer by one (0: never)
    uint32_t pokeAt;      // the SetPixel call that changes the caller's numbers (0: none)
    int32_t *poke;        // what it changes
    int32_t pokeValue;
    bool elsewhere;       // GetPixel answers in its own buffer instead of the caller's
};
Script gS;
uint32_t gSizeCalls, gSetCalls, gGetCalls;
const float *gColour;
int32_t *gNumbers; // the caller's point or rectangle (logged at each call)
int gNumberCount;
float gOther[4];

bool IsStack(const void *p)
{
    const auto *tib = reinterpret_cast<const NT_TIB *>(NtCurrentTeb());
    return p >= tib->StackLimit && p < tib->StackBase;
}
uint32_t Where(const void *p)
{
    if (!p) return 0;
    if (p == gColour) return 0xc01;
    if (p == gOther) return 0x07e;
    if (IsStack(p)) return 0x57ac;
    return 0xbad;
}

void Numbers()
{
    for (int i = 0; i < gNumberCount; ++i) L(uint32_t(gNumbers[i]));
}

void __fastcall SetPixel(void *self, void *, int32_t x, int32_t y, const float *colour, int32_t level)
{
    L(0x20), L(static_cast<FakeImage *>(self)->id), L(uint32_t(x)), L(uint32_t(y)), L(Where(colour)), L(uint32_t(level));
    for (int k = 0; k < 4; ++k) L(std::bit_cast<uint32_t>(colour[k]));
    Numbers();
    if (++gSetCalls == gS.pokeAt && gS.poke) *gS.poke = gS.pokeValue;
}
int32_t Size(int32_t base)
{
    const uint32_t n = gSizeCalls++;
    return base + (gS.growEvery ? int32_t(n / gS.growEvery) : 0);
}
int32_t __fastcall Width(void *self, void *, uint32_t level)
{
    L(0x26), L(static_cast<FakeImage *>(self)->id), L(level);
    Numbers();
    return Size(gS.width);
}
int32_t __fastcall Height(void *self, void *, uint32_t level)
{
    L(0x27), L(static_cast<FakeImage *>(self)->id), L(level);
    Numbers();
    return Size(gS.height);
}
float *__fastcall GetPixel(void *self, void *, float *out, int32_t x, int32_t y, int32_t level)
{
    L(0x39), L(static_cast<FakeImage *>(self)->id), L(Where(out)), L(uint32_t(x)), L(uint32_t(y)), L(uint32_t(level));
    const uint32_t n = gGetCalls++;
    float *const into = gS.elsewhere && n % 3 != 1 ? gOther : out;
    for (int k = 0; k < 4; ++k) into[k] = float(int32_t((n * 37 + k * 11) % 200) - 40) / 7.0f;
    if (into != out)
        for (int k = 0; k < 4; ++k) out[k] = -1000.0f - float(k);
    return into;
}
void __fastcall BadSlot(void *, void *)
{
    std::fprintf(stderr, "raster test: an unexpected image slot\n");
    std::abort();
}

void Setup(uint32_t id)
{
    for (void *&s : gVT) s = reinterpret_cast<void *>(&BadSlot);
    gVT[20] = reinterpret_cast<void *>(&SetPixel);
    gVT[26] = reinterpret_cast<void *>(&Width);
    gVT[27] = reinterpret_cast<void *>(&Height);
    gVT[39] = reinterpret_cast<void *>(&GetPixel);
    gImage = {gVT, id};
    gSizeCalls = gSetCalls = gGetCalls = 0;
    gLog.clear();
}

// Junk above the wrap flag's low byte.
uint32_t Flag(std::mt19937 &r, bool on) { return (r() & 0xffffff00u) | (on ? 1u : 0u); }

int32_t Coord(std::mt19937 &r, int32_t size)
{
    switch (r() % 6) {
    case 0: return -int32_t(r() % (2 * size + 4)) - 1;
    case 1: return size + int32_t(r() % (2 * size + 4));
    case 2: return r() % 2 ? 0 : size - 1;
    default: return int32_t(r() % uint32_t(size));
    }
}

int32_t Length(std::mt19937 &r, int32_t size)
{
    int32_t n;
    switch (r() % 6) {
    case 0: n = 0; break;
    case 1: n = size + int32_t(r() % 3) - 1; break;
    case 2: n = size + int32_t(r() % (2 * size + 2)); break;
    default: n = 1 + int32_t(r() % uint32_t(size)); break;
    }
    return r() % 2 ? -n : n;
}

bool Compare(difftest::Context &ctx, const char *what, uint32_t seed, const Log (&logs)[2])
{
    ++ctx.cases;
    if (logs[0] == logs[1]) return true;
    size_t k = 0;
    while (k < logs[0].size() && k < logs[1].size() && logs[0][k] == logs[1][k]) ++k;
    ctx.Fail("%s seed %u: the logs differ at word %zu of %zu / %zu (original %08x, ours %08x)", what, seed, k,
             logs[0].size(), logs[1].size(), k < logs[0].size() ? logs[0][k] : 0u, k < logs[1].size() ? logs[1][k] : 0u);
    return false;
}

using RunFn = void(__cdecl *)(void *, const int32_t *, int32_t, const float *, uint32_t);
using RectFn = void(__cdecl *)(void *, const int32_t *, const float *, uint32_t);
using FrameFn = void(__cdecl *)(void *, int32_t, int32_t, int32_t, int32_t, int32_t, const float *, uint32_t);
using FadeFn = void(__cdecl *)(int32_t, void *, int32_t, int32_t);

} // namespace raster_test

using namespace raster_test;

DIFF_TEST(ImposterRaster_Run, 0x3637d0)
{
    gLog.reserve(1u << 20);
    const RunFn ours[2] = {reinterpret_cast<RunFn>(&nTSSG::PaintColumn), reinterpret_cast<RunFn>(&nTSSG::PaintRow)};
    const uint32_t rvas[2] = {0x3637d0, 0x363a10};
    int painted[2] = {}, wrapped[2] = {};
    for (uint32_t seed = 0; seed < 4000; ++seed) {
        const int which = int(seed % 2);
        Log logs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed * 2654435761u + 9u);
            Setup(0xa11 + seed % 7);
            gS = {};
            gS.width = 1 + int32_t(r() % 24);
            gS.height = r() % 3 ? gS.width : 1 + int32_t(r() % 24);
            gS.growEvery = r() % 5 == 0 ? 1 : 0;
            const int32_t size = which == 0 ? gS.height : gS.width;
            int32_t point[2] = {Coord(r, which == 0 ? gS.width : size), Coord(r, size)};
            if (which == 1) std::swap(point[0], point[1]), point[0] = Coord(r, size), point[1] = Coord(r, gS.height);
            const int32_t length = Length(r, size);
            const bool wrap = r() % 2;
            const uint32_t flag = Flag(r, wrap);
            float colour[4];
            for (float &c : colour) c = float(int32_t(r() % 2000) - 1000) / 16.0f;
            gS.pokeAt = r() % 3 == 0 ? 1 + r() % 4 : 0;
            gS.poke = &point[r() % 2];
            gS.pokeValue = int32_t(r() % 9) - 4;
            gColour = colour;
            gNumbers = point;
            gNumberCount = 2;
            auto *const fn = side == 0 ? ctx.Original<RunFn>(rvas[which]) : ours[which];
            fn(&gImage, point, length, colour, flag);
            L(0xe0d), L(gSetCalls);
            logs[side] = gLog;
            if (side == 0 && gSetCalls) ++painted[which], wrapped[which] += wrap;
        }
        if (!Compare(ctx, which ? "row" : "column", seed, logs) && ctx.failures.size() >= 6) return;
    }
    for (int w = 0; w < 2; ++w)
        if (painted[w] < 600 || wrapped[w] < 300)
            ctx.Fail("coverage (%s): painted %d, wrapped %d", w ? "row" : "column", painted[w], wrapped[w]);
}

DIFF_TEST(ImposterRaster_Rect, 0x363950)
{
    gLog.reserve(1u << 20);
    int rows = 0, columns = 0;
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        Log logs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed * 40503u + 17u);
            Setup(0xb22);
            gS = {};
            gS.width = gS.height = 1 + int32_t(r() % 16);
            if (r() % 4 == 0) gS.height = 1 + int32_t(r() % 16);
            gS.growEvery = r() % 6 == 0 ? 1 + r() % 3 : 0;
            int32_t rect[4];
            for (int i = 0; i < 4; ++i) rect[i] = r() % 4 ? int32_t(r() % 20) - 2 : Coord(r, gS.width);
            if (r() % 5 == 0) rect[2] = rect[0];
            if (r() % 5 == 0) rect[3] = rect[1];
            const bool wrap = r() % 2;
            const uint32_t flag = Flag(r, wrap);
            float colour[4] = {float(seed % 5), 0.25f, -1.5f, float(r() % 100) / 3.0f};
            gS.pokeAt = r() % 3 == 0 ? 1 + r() % 6 : 0;
            gS.poke = &rect[r() % 4];
            gS.pokeValue = int32_t(r() % 12) - 3;
            gColour = colour;
            gNumbers = rect;
            gNumberCount = 4;
            if (side == 0)
                ctx.Original<RectFn>(0x363950)(&gImage, rect, colour, flag);
            else
                reinterpret_cast<RectFn>(&nTSSG::PaintRect)(&gImage, rect, colour, flag);
            L(0xe0d), L(gSetCalls), Numbers();
            logs[side] = gLog;
            if (side == 0) {
                for (uint32_t v : gLog) {
                    if (v == 0x26) { ++rows; break; }
                    if (v == 0x27) { ++columns; break; }
                }
            }
        }
        if (!Compare(ctx, "rect", seed, logs) && ctx.failures.size() >= 6) return;
    }
    if (rows < 800 || columns < 800) ctx.Fail("coverage: rows %d, columns %d", rows, columns);
}

DIFF_TEST(ImposterRaster_Frame, 0x363680)
{
    gLog.reserve(1u << 20);
    int kinds[4] = {}; // zero, positive, negative inside, negative filled
    for (uint32_t seed = 0; seed < 2000; ++seed) {
        Log logs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed * 69069u + 1u);
            Setup(0xc33);
            gS = {};
            gS.width = gS.height = 4 + int32_t(r() % 20);
            if (r() % 5 == 0) gS.height = 4 + int32_t(r() % 20);
            int32_t c[4];
            for (int32_t &v : c) v = int32_t(r() % 26) - 3;
            if (r() % 2) {
                if (c[0] > c[2]) std::swap(c[0], c[2]);
                if (c[1] > c[3]) std::swap(c[1], c[3]);
            }
            int32_t thickness;
            switch (r() % 4) {
            case 0: thickness = 0; break;
            case 1: thickness = 1 + int32_t(r() % 4); break;
            default: thickness = -1 - int32_t(r() % 6); break;
            }
            const bool wrap = r() % 2;
            const uint32_t flag = Flag(r, wrap);
            float colour[4] = {1.0f, float(seed % 3), 0.5f, -2.0f};
            gColour = colour;
            gNumbers = c;
            gNumberCount = 0;
            if (side == 0) {
                ctx.Original<FrameFn>(0x363680)(&gImage, c[0], c[1], c[2], c[3], thickness, colour, flag);
                const int32_t a = thickness < 0 ? -thickness : thickness;
                kinds[thickness == 0 ? 0 : thickness > 0 ? 1 : (c[2] - c[0] <= 2 * a || c[3] - c[1] <= 2 * a) ? 3 : 2]++;
            } else {
                reinterpret_cast<FrameFn>(&nTSSG::PaintFrame)(&gImage, c[0], c[1], c[2], c[3], thickness, colour, flag);
            }
            L(0xe0d), L(gSetCalls);
            logs[side] = gLog;
        }
        if (!Compare(ctx, "frame", seed, logs) && ctx.failures.size() >= 6) return;
    }
    if (kinds[0] < 200 || kinds[1] < 200 || kinds[2] < 100 || kinds[3] < 100)
        ctx.Fail("coverage: zero %d, positive %d, inside %d, filled %d", kinds[0], kinds[1], kinds[2], kinds[3]);
}

DIFF_TEST(ImposterRaster_Fade, 0x363490)
{
    gLog.reserve(1u << 20);
    int faded = 0, elsewhere = 0;
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        Log logs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed * 1103515245u + 12345u);
            Setup(0xd44);
            gS = {};
            gS.width = gS.height = 1 + int32_t(r() % 20);
            gS.growEvery = r() % 4 == 0 ? 1 + r() % 5 : 0;
            gS.elsewhere = r() % 2;
            const int32_t rings = r() % 6 == 0 ? -int32_t(r() % 3) : int32_t(r() % 7);
            const int32_t width = int32_t(r() % 22) - 1;
            const int32_t height = int32_t(r() % 22) - 1;
            void *const image = r() % 12 == 0 ? nullptr : &gImage;
            gColour = nullptr;
            gNumberCount = 0;
            if (side == 0)
                ctx.Original<FadeFn>(0x363490)(rings, image, width, height);
            else
                reinterpret_cast<FadeFn>(&nTSSG::FadeBorder)(rings, image, width, height);
            L(0xe0d), L(gSetCalls), L(gGetCalls);
            logs[side] = gLog;
            if (side == 0 && gSetCalls) ++faded, elsewhere += gS.elsewhere;
        }
        if (!Compare(ctx, "fade", seed, logs) && ctx.failures.size() >= 6) return;
    }
    if (faded < 600 || elsewhere < 250) ctx.Fail("coverage: faded %d, answered elsewhere %d", faded, elsewhere);
}
