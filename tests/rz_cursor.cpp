// Differential tests for the cursors (src/gz/cRZCursor.size.cpp, standalone shape): cRZCursor, cRZSystemCursorW95,
// cRZCursorManager and the graphics-system helpers 0x9c5a05 / 0x7ae7f4 / 0x5ba211.
//
// The original side builds its objects with the original constructors (0x9d00ad, 0x9d0181, 0x9d0158, the factory
// 0x9b3460) on the game allocator's memory, ours with ours; both run the same script through their own vtables, called
// through our interface declarations (whose slots are the game's: RZCursor_VTables).
//
// The world, built afresh for each side: a fake framework in 0x10c9378 whose GetSystemService hands out a fake graphics
// system (or nothing), the graphics cell 0x10f51a4 (empty, or the fake already), the system code page cell (so the path
// conversion needs no import), fake buffers (Init / Lock answers per configuration, a surface the test owns), and impfx
// fakes for every Win32 call: the icon file's handles, bitmaps and bits come from a configuration the script changes the
// same way on both sides; the bits from a generator reset per side. ShowCursor keeps a display count per side.
// Our declarations come before <windows.h> (the harness headers include it): its LoadCursor macro would rename the
// method in this file only.
#include "gz/cRZCursor.h"
#include "rz/cRZString.h"

#include "difftest.h"
#include "drawfx_core.h"
#include "free_recorder.h"
#include "impfx.h"
#include "ts_services_fixture.h"

#undef LoadCursor

#include <cstdint>
#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace {

constexpr uint32_t kFrameworkCell = 0x10c9378;
constexpr uint32_t kCodePageCell = 0xff23a4;
constexpr uint32_t kCodePage = 1252;
constexpr uint32_t kCursorTag = 0x7e000000;
constexpr uint32_t kManagerTag = 0x7d000000;
constexpr uint32_t kSurfaceTag = 0x7c000000;
constexpr uint32_t kRealCursor = 0x5000000; // Ident of a real cursor: this with a hash of its contents

// Handles the fakes hand out: above the user address range, so normalisation leaves them alone.
constexpr uint32_t kIcon = 0xc1c00004;
constexpr uint32_t kMaskBitmap = 0xc2b00008;
constexpr uint32_t kColourBitmap = 0xc2b0000c;
constexpr uint32_t kDc = 0xc3d00010;
constexpr uint32_t kFileCursor = 0xc4f00014;
constexpr uint32_t kSystemCursorBase = 0xc5000000;
constexpr uint32_t kHandleArg = 0xc6000018;

using GameNew = void *(__cdecl *)(size_t);
using PathCtor = void *(__thiscall *)(void *, const cIGZString *);
using KindCtor = void *(__thiscall *)(void *, uint32_t);
using PlainCtor = void *(__thiscall *)(void *);
using Factory = void *(__cdecl *)();

// ---- fakes ----
inline constexpr drawfx::Slot kFrameworkSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    {0, drawfx::Ret::Int, false, "", ""},
    {0, drawfx::Ret::Int, false, "", ""},
    {3, drawfx::Ret::Int, true, "GetSystemService", "uui"},
};
inline constexpr drawfx::Iface kFramework{"cIGZFrameWork", 6, kFrameworkSlots};

constexpr drawfx::Slot Unsettled() { return {0, drawfx::Ret::Int, false, "", ""}; }
inline constexpr drawfx::Slot kGraphicsSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    // The out pointer is the cursor's buffer field (often inside a cursor still being built, so not yet a range): its
    // presence is logged here and the field it fills is compared with the cursor's state.
    {1, drawfx::Ret::Int, true, "CreateBuffer", "W0"},
    Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(),
    Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(),
    Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(),
    {1, drawfx::Ret::Int, true, "ShowCursor", "u"},
    {3, drawfx::Ret::Int, true, "SetCursor", "iuu"},
    {0, drawfx::Ret::Int, true, "UseSystemCursor", ""},
};
inline constexpr drawfx::Iface kGraphics{"cIGZGraphicSystem", 33, kGraphicsSlots};

inline constexpr drawfx::Slot kBufferSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    {4, drawfx::Ret::Int, true, "Init", "uuuu"},
    {0, drawfx::Ret::Int, true, "Shutdown", ""},
    Unsettled(),
    {1, drawfx::Ret::Int, true, "Lock", "u"},
    {1, drawfx::Ret::Int, true, "Unlock", "u"},
    Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(),
    Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(),
    Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(), Unsettled(),
    {0, drawfx::Ret::Int, true, "GetColorSurfaceBits", ""},
    {0, drawfx::Ret::Int, true, "GetColorSurfaceStride", ""},
};
inline constexpr drawfx::Iface kBuffer{"cIGZBuffer", 36, kBufferSlots};

inline constexpr drawfx::Slot kCursorSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    Unsettled(),
    Unsettled(),
    {1, drawfx::Ret::Int, true, "SetCursor", "b"},
};
inline constexpr drawfx::Iface kFakeCursor{"cIGZCursor", 6, kCursorSlots};

drawfx::Fake gFramework;
drawfx::Fake gGraphics;
drawfx::Fake gBuffer;
drawfx::Fake gBuffer2;
constexpr unsigned kFakeCursors = 3;
drawfx::Fake gCursors[kFakeCursors];
alignas(16) uint8_t gSurface[512];

// ---- the configuration of one load (the icon file and the graphics system's answers) ----
struct Config {
    uint32_t framework;     // a framework in the cell
    uint32_t serviceAnswers; // its GetSystemService hands out the graphics system
    uint32_t graphicsCell;  // the graphics cell already holds it
    uint32_t useSystem;     // UseSystemCursor's answer
    uint32_t fileCursor;    // LoadCursorFromFileA's
    uint32_t icon;          // LoadImageA's
    uint32_t iconInfo;      // GetIconInfo's
    uint32_t hotX;
    uint32_t hotY;
    uint32_t colour;        // a colour bitmap
    uint32_t mask;          // a mask bitmap handle
    uint32_t dc;            // GetDC's
    uint32_t object;        // GetObjectA's
    uint32_t width;
    uint32_t height;
    uint32_t create;        // CreateBuffer's
    uint32_t init;          // the buffer's Init
    uint32_t lock;          // and Lock
    uint32_t maskBits;      // GetDIBits of the mask
    uint32_t colourBits;    // and of the colour bitmap
    uint32_t strideExtra;
    uint32_t visible;       // GetCursorInfo's flags
};
Config gCfg;
std::mt19937 gBits;   // the bitmaps' contents, reset per side
int32_t gDisplayCount; // ShowCursor's, per side
unsigned gShowCalls;

// A configuration from the script's generator: mostly the whole path to a loaded image.
Config MakeConfig(std::mt19937 &rng)
{
    Config c{};
    auto often = [&] { return rng() % 8 != 0 ? 1u : 0u; };
    c.framework = rng() % 10 != 0;
    c.serviceAnswers = often();
    c.graphicsCell = rng() % 3 == 0;
    const uint32_t answers[] = {0, 0, 1, 0x100};
    c.useSystem = answers[rng() % 4];
    c.fileCursor = rng() % 3 ? kFileCursor : 0;
    c.icon = often() ? kIcon : 0;
    c.iconInfo = often();
    const uint32_t hots[] = {0, 3, 7, 0x7fff, 0x8000, 0x1fffe, 0xffffffff};
    c.hotX = hots[rng() % 7];
    c.hotY = hots[rng() % 7];
    c.colour = rng() % 3 ? kColourBitmap : 0;
    c.mask = rng() % 12 ? kMaskBitmap : 0;
    c.dc = often() ? kDc : 0;
    c.object = often();
    c.width = rng() % 6;
    c.height = rng() % 9;
    if (!c.colour) c.height &= ~1u; // the mask holds both halves: GetDIBits writes all of them (odd overflows)
    c.create = often();
    c.init = often();
    c.lock = often();
    c.maskBits = often();
    c.colourBits = often();
    c.strideExtra = (rng() % 3) * 4;
    c.visible = rng() % 3;
    return c;
}

uint32_t ImportArg(const uint32_t *args, unsigned i) { return args[i]; }
template <class T> T *At(uint32_t word) { return reinterpret_cast<T *>(uintptr_t(word)); }

uint32_t OnGetSystemService(drawfx::Fake &, const uint32_t *args)
{
    if (!gCfg.serviceAnswers) return 0;
    ++gGraphics.refs;
    *At<void *>(args[2]) = &gGraphics;
    return 1;
}
uint32_t OnUseSystemCursor(drawfx::Fake &, const uint32_t *) { return gCfg.useSystem; }
uint32_t OnCreateBuffer(drawfx::Fake &, const uint32_t *args)
{
    if (!gCfg.create) return 0;
    ++gBuffer.refs;
    *At<void *>(args[0]) = &gBuffer;
    return 1;
}
uint32_t OnInit(drawfx::Fake &, const uint32_t *) { return gCfg.init; }
uint32_t OnLock(drawfx::Fake &, const uint32_t *) { return gCfg.lock; }
uint32_t OnBits(drawfx::Fake &, const uint32_t *) { return drawfx::P(gSurface); }
uint32_t OnStride(drawfx::Fake &, const uint32_t *) { return gCfg.width * 4 + gCfg.strideExtra; }

uint32_t OnLoadCursorFromFile(drawfx::Fake &, const uint32_t *) { return gCfg.fileCursor; }
uint32_t OnLoadImage(drawfx::Fake &, const uint32_t *) { return gCfg.icon; }
uint32_t OnGetIconInfo(drawfx::Fake &, const uint32_t *args)
{
    if (!gCfg.iconInfo) return 0;
    uint32_t *const info = At<uint32_t>(args[1]);
    info[0] = 0;
    info[1] = gCfg.hotX;
    info[2] = gCfg.hotY;
    info[3] = gCfg.mask;
    info[4] = gCfg.colour;
    return 1;
}
uint32_t OnGetDC(drawfx::Fake &, const uint32_t *) { return gCfg.dc; }
uint32_t OnGetObject(drawfx::Fake &, const uint32_t *args)
{
    if (!gCfg.object) return 0;
    uint32_t *const bitmap = At<uint32_t>(args[2]);
    bitmap[0] = 0;
    bitmap[1] = gCfg.width;
    bitmap[2] = gCfg.height;
    bitmap[3] = (gCfg.width + 15) / 16 * 2;
    bitmap[4] = 0x10001; // one plane, one bit
    bitmap[5] = 0;
    return 0x18;
}
// The bitmap's lines (the header's width each) into the bits: mask pixels with and without colour bits, colours anything.
uint32_t OnGetDIBits(drawfx::Fake &, const uint32_t *args)
{
    const bool mask = args[1] == kMaskBitmap;
    if (!(mask ? gCfg.maskBits : gCfg.colourBits)) return 0;
    const int32_t width = At<int32_t>(args[5])[1];
    uint32_t *const bits = At<uint32_t>(args[4]);
    const uint32_t count = args[3] * uint32_t(width);
    const uint32_t masks[] = {0, 0x00ffffff, 0xff000000, 0x00000100, 0x00010000};
    for (uint32_t i = 0; bits && i < count; ++i) bits[i] = mask ? masks[gBits() % 5] : uint32_t(gBits());
    return args[3];
}
uint32_t OnLoadSystemCursor(drawfx::Fake &, const uint32_t *args) { return kSystemCursorBase | args[1]; }
uint32_t OnShowCursor(drawfx::Fake &, const uint32_t *args)
{
    if (++gShowCalls > 200) {
        drawfx::gErrors.emplace_back("ShowCursor called more than 200 times");
        return args[0] ? 0 : uint32_t(-1);
    }
    gDisplayCount += args[0] ? 1 : -1;
    return uint32_t(gDisplayCount);
}
uint32_t OnGetCursorInfo(drawfx::Fake &, const uint32_t *args)
{
    uint32_t *const info = At<uint32_t>(args[0]);
    info[1] = gCfg.visible;
    info[2] = kFileCursor;
    info[3] = 11;
    info[4] = 22;
    return 1;
}

void AddImports(impfx::Imports &imp)
{
    IMPFX_ADD(imp, LoadCursorFromFileA, "s").On(OnLoadCursorFromFile);
    IMPFX_ADD(imp, LoadImageA, "usuuuu").On(OnLoadImage);
    IMPFX_ADD(imp, GetIconInfo, "ui").On(OnGetIconInfo);
    IMPFX_ADD(imp, GetDC, "u").On(OnGetDC);
    IMPFX_ADD(imp, GetObjectA, "uui").On(OnGetObject);
    IMPFX_ADD(imp, GetDIBits, "uuuuW0W10u").On(OnGetDIBits);
    IMPFX_ADD(imp, ReleaseDC, "uu").Returns(1);
    IMPFX_ADD(imp, DeleteObject, "u").Returns(1);
    IMPFX_ADD(imp, DestroyCursor, "u").Returns(1);
    IMPFX_ADD(imp, LoadCursorA, "uu").On(OnLoadSystemCursor);
    IMPFX_ADD(imp, SetCursor, "u").Returns(kFileCursor);
    IMPFX_ADD(imp, ShowCursor, "u").On(OnShowCursor);
    IMPFX_ADD(imp, GetCursorInfo, "W1").On(OnGetCursorInfo);
}

// The fakes and cells for one side, with the configuration as it is.
void BuildWorld(impfx::Imports &imp, drawfx::Cells &cells, uint32_t seed, int32_t displayCount)
{
    imp.BeginSide();
    gBits.seed(seed * 7919 + 1);
    gDisplayCount = displayCount;
    gShowCalls = 0;
    gFramework.Reset<kFramework>(0x910);
    gFramework.On(5, OnGetSystemService);
    gGraphics.Reset<kGraphics>(0x920);
    gGraphics.On(3, OnCreateBuffer);
    gGraphics.On(32, OnUseSystemCursor);
    for (drawfx::Fake *b : {&gBuffer, &gBuffer2}) {
        b->Reset<kBuffer>(b == &gBuffer ? 0x930 : 0x931);
        b->On(3, OnInit);
        b->On(6, OnLock);
        b->On(34, OnBits);
        b->On(35, OnStride);
    }
    for (unsigned i = 0; i < kFakeCursors; ++i) gCursors[i].Reset<kFakeCursor>(0x940 + i);
    std::memset(gSurface, 0xcd, sizeof(gSurface));
    drawfx::AddRange(gSurface, sizeof(gSurface), kSurfaceTag);
    cells.Set(kFrameworkCell, gCfg.framework ? &gFramework : nullptr);
    cells.Set(kGraphicSystemCell, gCfg.graphicsCell ? &gGraphics : nullptr);
    cells.Set(kCodePageCell, kCodePage);
}

void NoteSurface()
{
    const auto *words = reinterpret_cast<const uint32_t *>(gSurface);
    for (uint32_t i = 0; i < sizeof(gSurface) / 4; ++i)
        if (words[i] != 0xcdcdcdcd) drawfx::Note("pixel", {i, words[i]});
}

void NoteRefs()
{
    drawfx::Note("refs", {uint32_t(gFramework.refs), uint32_t(gGraphics.refs), uint32_t(gBuffer.refs),
                          uint32_t(gBuffer2.refs)});
    for (unsigned i = 0; i < kFakeCursors; ++i) drawfx::Note("cursor refs", uint32_t(gCursors[i].refs));
}

// A game cRZString holding `text` (the cursor's path argument), built and destroyed by hand.
struct Path {
    union {
        cRZString s;
    };
    explicit Path(const char *text) { s.Construct(text); }
    ~Path() { s.Destruct(); }
    Path(const Path &) = delete;
    Path &operator=(const Path &) = delete;
    const cIGZString &Get() const { return *s.AsInterface(); }
};

uint32_t Text(const char *text)
{
    uint32_t h = 2166136261u;
    for (; *text; ++text) h = (h ^ uint8_t(*text)) * 16777619u;
    return h;
}

// Every field of a cursor (the flag byte alone: its padding is never written).
void NoteCursor(const char *label, cIGZCursor *cursor)
{
    const auto *raw = reinterpret_cast<const uint8_t *>(cursor);
    auto word = [&](unsigned at) { return *reinterpret_cast<const uint32_t *>(raw + at); };
    const auto *path = reinterpret_cast<const cRZString *>(raw + 0x10);
    drawfx::Note(label, {word(4), raw[8], drawfx::Norm(word(0xc)), path->Strlen(), Text(path->ToChar()),
                         drawfx::Norm(word(0x30)), word(0x34), word(0x38)});
}

const char *const kPaths[] = {"cursor.cur", "a\\b\\hand.ani", "", "C:/x/y/arrow.ico"};

// The image load calls through the graphics system 0x9c5a05 returns without a null check (0x9d069f reads its vtable),
// as the game always has one: the scripts load only while it can be had, from the cell or the framework.
bool GraphicsReachable()
{
    return *GameAddress<uint32_t>(kGraphicSystemCell) != 0 ||
           (*GameAddress<uint32_t>(kFrameworkCell) != 0 && gCfg.serviceAnswers);
}

// One script on a cursor: the loads reconfigure the icon file the same way on both sides.
void RunCursorScript(cIGZCursor *cursor, std::mt19937 &rng, int steps)
{
    // A new cursor holds no reference: the script holds one, so a QueryInterface's Release does not delete it.
    drawfx::Note("hold", cursor->AddRef());
    for (int step = 0; step < steps; ++step) {
        switch (rng() % 12) {
        case 0:
        case 1: {
            gCfg = MakeConfig(rng);
            gCfg.serviceAnswers = 1;
            const uint32_t pick = rng() % 4;
            if (!GraphicsReachable()) {
                drawfx::Note("load skipped", pick);
                break;
            }
            Path path(kPaths[pick]);
            drawfx::Note("load", cursor->LoadCursor(path.Get()) ? 1u : 0u);
            NoteSurface();
            break;
        }
        case 2: cursor->SetCursor(rng() % 4 != 0); break;
        case 3: cursor->ShowCursor(rng() % 2 != 0); break;
        case 4: {
            int32_t x = 0x55, y = 0x66;
            cursor->GetCursorHotSpot(&x, &y);
            drawfx::Note("hot spot", {uint32_t(x), uint32_t(y)});
            break;
        }
        case 5: {
            const uint32_t values[] = {0, 5, 0x7fff, 0x8000, 0x12345, 0xfffffffe};
            cursor->SetCursorHotSpot(int32_t(values[rng() % 6]), int32_t(values[rng() % 6]));
            break;
        }
        case 6: {
            const uint32_t handles[] = {0, kHandleArg, kFileCursor};
            cursor->SetCursorHandle(At<void>(handles[rng() % 3]));
            break;
        }
        case 7: {
            drawfx::Fake *const buffers[] = {nullptr, &gBuffer, &gBuffer2};
            cursor->SetCursorBuffer(reinterpret_cast<cIGZBuffer *>(buffers[rng() % 3]));
            break;
        }
        case 8: drawfx::Note("uses system", cursor->UsesSystemCursor() ? 1u : 0u); break;
        case 9: drawfx::Note("buffer", drawfx::Norm(drawfx::P(cursor->GetCursorBuffer()))); break;
        case 10: {
            const uint32_t iids[] = {1, cIGZCursor::kIid, cIGZCursorManager::kIid, 0};
            void *out = At<void>(0xc7000000);
            const bool yes = cursor->QueryInterface(iids[rng() % 4], &out);
            drawfx::Note("qi", {yes ? 1u : 0u, drawfx::Norm(drawfx::P(out))});
            if (yes) cursor->Release();
            break;
        }
        default: drawfx::Note("slots 3 4", {cursor->Slot3() ? 1u : 0u, cursor->Slot4() ? 1u : 0u}); break;
        }
        NoteCursor("cursor", cursor);
    }
}

// The last references (the script's hold and one more): the frees (the destructor's) by size.
void ReleaseAll(cIGZCursor *cursor)
{
    drawfx::Note("add ref", cursor->AddRef());
    drawfx::Note("release", cursor->Release());
    difftest::FreeRecorder rec;
    drawfx::Note("last", cursor->Release());
    rec.Stop();
    size_t sizes[8];
    const size_t n = rec.SortedSizes(sizes, 8);
    drawfx::Note("frees", uint32_t(n));
    for (size_t i = 0; i < n; ++i) drawfx::Note("freed", uint32_t(sizes[i]));
}

void *GameMemory(difftest::Context &ctx, size_t size) { return ctx.Original<GameNew>(0x824632)(size); }

} // namespace

// The cursor with a path (0x9d00ad, then LoadCursor 0x9d04f2) over every branch of the load, then random slot calls;
// the last Release reaches the destructor 0x9d0251 (the handle and the buffer let go).
DIFF_TEST(RZCursor_Load, 0x9d04f2)
{
    svcfx::AllocatorScope allocator;
    impfx::Imports imp(ctx);
    AddImports(imp);
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        auto side = [&](bool ours) {
            drawfx::Cells cells;
            std::mt19937 rng(seed);
            gCfg = MakeConfig(rng);
            // The constructor loads at once (GraphicsReachable): the plain and system constructors cover the rest.
            if (!gCfg.framework || !gCfg.serviceAnswers) gCfg.graphicsCell = 1;
            return drawfx::Side([&] { BuildWorld(imp, cells, seed, 0); }, [&] {
                Path path(kPaths[seed % 4]);
                cIGZCursor *cursor =
                    ours ? static_cast<cIGZCursor *>(new cRZCursor(path.Get()))
                         : static_cast<cIGZCursor *>(ctx.Original<PathCtor>(0x9d00ad)(GameMemory(ctx, 0x3c), &path.Get()));
                drawfx::AddRange(cursor, cRZCursor::kSize, kCursorTag);
                NoteCursor("built", cursor);
                NoteSurface();
                RunCursorScript(cursor, rng, int(rng() % 9));
                ReleaseAll(cursor);
                NoteSurface();
                NoteRefs();
                drawfx::Note("graphics cell", drawfx::Norm(*GameAddress<uint32_t>(kGraphicSystemCell)));
            });
        };
        auto a = side(false);
        auto b = side(true);
        if (!drawfx::Same(ctx, a.first, b.first, "cursor", seed, a.second.empty() ? b.second : a.second)) return;
    }
}

// The plain cursor's factory (0x9b3460 against ours) and the plain constructor 0x9d0109 through it.
DIFF_TEST(RZCursor_Factory, 0x9b3460)
{
    svcfx::AllocatorScope allocator;
    impfx::Imports imp(ctx);
    AddImports(imp);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        auto side = [&](bool ours) {
            drawfx::Cells cells;
            std::mt19937 rng(seed);
            gCfg = MakeConfig(rng);
            return drawfx::Side([&] { BuildWorld(imp, cells, seed, 0); }, [&] {
                cIGZCursor *cursor = ours ? static_cast<cIGZCursor *>(cRZCursorAdapters::Create())
                                          : static_cast<cIGZCursor *>(ctx.Original<Factory>(0x9b3460)());
                drawfx::AddRange(cursor, cRZCursor::kSize, kCursorTag);
                NoteCursor("built", cursor);
                RunCursorScript(cursor, rng, int(rng() % 6));
                ReleaseAll(cursor);
                NoteRefs();
            });
        };
        auto a = side(false);
        auto b = side(true);
        if (!drawfx::Same(ctx, a.first, b.first, "factory", seed, a.second.empty() ? b.second : a.second)) return;
    }
}

// The system cursors (0x9d0181) of every kind: their handle, the folded LoadCursor / SetCursorHandle, the destructor
// that does not destroy the shared handle.
DIFF_TEST(RZCursor_System, 0x9d0181)
{
    svcfx::AllocatorScope allocator;
    impfx::Imports imp(ctx);
    AddImports(imp);
    for (uint32_t seed = 0; seed < 0x14 * 6; ++seed) {
        const uint32_t kind = seed % 0x14;
        auto side = [&](bool ours) {
            drawfx::Cells cells;
            std::mt19937 rng(seed);
            gCfg = MakeConfig(rng);
            return drawfx::Side([&] { BuildWorld(imp, cells, seed, 0); }, [&] {
                cIGZCursor *cursor =
                    ours ? static_cast<cIGZCursor *>(new cRZSystemCursorW95(kind))
                         : static_cast<cIGZCursor *>(ctx.Original<KindCtor>(0x9d0181)(GameMemory(ctx, 0x3c), kind));
                drawfx::AddRange(cursor, cRZCursor::kSize, kCursorTag);
                NoteCursor("built", cursor);
                RunCursorScript(cursor, rng, int(rng() % 8));
                ReleaseAll(cursor);
                NoteRefs();
            });
        };
        auto a = side(false);
        auto b = side(true);
        if (!drawfx::Same(ctx, a.first, b.first, "system cursor", seed, a.second.empty() ? b.second : a.second)) return;
    }
}

namespace {

// Pointers the manager hands back: a fake's id, or a live real cursor's contents (its addresses are reused once a
// cursor is freed, and the two sides allocate differently): the flag, the path, the buffer, the hot spot, the handle.
uint32_t Ident(const void *p)
{
    if (!p) return 0;
    for (unsigned i = 0; i < kFakeCursors; ++i)
        if (p == &gCursors[i]) return gCursors[i].id;
    const auto *raw = static_cast<const uint8_t *>(p);
    auto word = [&](unsigned at) { return *reinterpret_cast<const uint32_t *>(raw + at); };
    const auto *path = reinterpret_cast<const cRZString *>(raw + 0x10);
    uint32_t h = 2166136261u;
    for (const uint32_t w : {uint32_t(raw[8]), Text(path->ToChar()), drawfx::Norm(word(0x30)), word(0x34), word(0x38)})
        h = (h ^ w) * 16777619u;
    return kRealCursor | (h & 0xfffff);
}

void NoteManager(cIGZCursorManager *manager)
{
    const auto *raw = reinterpret_cast<const uint8_t *>(manager);
    const auto *current = *reinterpret_cast<cIGZCursor *const *>(raw + 0x28);
    drawfx::Note("manager", {*reinterpret_cast<const uint32_t *>(raw + 8), raw[4], Ident(current)});
}

} // namespace

// The manager (0x9d0158 against ours) under a random script of every slot, with fake and real cursors; the final
// Release reaches the destructor 0x9d0287 (which releases no cursor).
DIFF_TEST(RZCursorManager_Script, 0x9d03fa)
{
    svcfx::AllocatorScope allocator;
    impfx::Imports imp(ctx);
    AddImports(imp);
    for (uint32_t seed = 0; seed < 800; ++seed) {
        auto side = [&](bool ours) {
            drawfx::Cells cells;
            std::mt19937 rng(seed);
            gCfg = MakeConfig(rng);
            const int32_t display = int32_t(rng() % 7) - 3;
            return drawfx::Side([&] { BuildWorld(imp, cells, seed, display); }, [&] {
                cIGZCursorManager *manager =
                    ours ? static_cast<cIGZCursorManager *>(new cRZCursorManager)
                         : static_cast<cIGZCursorManager *>(ctx.Original<PlainCtor>(0x9d0158)(GameMemory(ctx, 0x2c)));
                drawfx::AddRange(manager, cRZCursorManager::kGameSize, kManagerTag);
                drawfx::Note("add ref", manager->AddRef());
                NoteManager(manager);
                const uint32_t ids[] = {1, 3, 0xf, 0x10, 0x11, 0x20, 0x21};
                auto id = [&] { return ids[rng() % 7]; };
                const int steps = int(rng() % 24);
                for (int step = 0; step < steps; ++step) {
                    switch (rng() % 17) {
                    case 0:
                    case 1: drawfx::Note("init", manager->Init() ? 1u : 0u); break;
                    case 2: drawfx::Note("shutdown", manager->Shutdown() ? 1u : 0u); break;
                    case 3: {
                        gCfg = MakeConfig(rng);
                        gCfg.serviceAnswers = 1;
                        const uint32_t pick = rng() % 4;
                        const uint32_t at = id();
                        if (!GraphicsReachable()) { // the new cursor loads at once
                            drawfx::Note("add from file skipped", {pick, at});
                            break;
                        }
                        Path path(kPaths[pick]);
                        drawfx::Note("add from file", manager->AddCursorFromFile(path.Get(), at) ? 1u : 0u);
                        break;
                    }
                    case 4:
                    case 5: {
                        const unsigned pick = rng() % (kFakeCursors + 2);
                        cIGZCursor *c = pick < kFakeCursors ? reinterpret_cast<cIGZCursor *>(&gCursors[pick])
                                        : pick == kFakeCursors ? nullptr
                                                               : manager->GetCursor(id());
                        drawfx::Note("add", {Ident(c), manager->AddCursor(c, id()) ? 1u : 0u});
                        break;
                    }
                    case 6: drawfx::Note("remove", manager->RemoveCursor(id()) ? 1u : 0u); break;
                    case 7: drawfx::Note("current", Ident(manager->GetCurrentCursor())); break;
                    case 8: drawfx::Note("get", Ident(manager->GetCursor(id()))); break;
                    case 9: {
                        const unsigned pick = rng() % (kFakeCursors + 1);
                        cIGZCursor *c = pick < kFakeCursors ? reinterpret_cast<cIGZCursor *>(&gCursors[pick])
                                                            : manager->GetCursor(id());
                        if (c) drawfx::Note("set current", manager->SetCurrentCursor(c) ? 1u : 0u);
                        break;
                    }
                    case 10: drawfx::Note("set by id", manager->SetCurrentCursorById(id()) ? 1u : 0u); break;
                    case 11:
                        if (manager->GetCurrentCursor()) drawfx::Note("push", manager->PushAutoCursor(id()) ? 1u : 0u);
                        break;
                    case 12: drawfx::Note("pop", manager->PopAutoCursor() ? 1u : 0u); break;
                    case 13: manager->ShowCursor(); break;
                    case 14: manager->HideCursor(); break;
                    case 15: drawfx::Note("visible", manager->IsCursorVisible() ? 1u : 0u); break;
                    default: {
                        const uint32_t iids[] = {1, cIGZCursorManager::kIid, cIGZCursor::kIid};
                        void *out = At<void>(0xc7000000);
                        const bool yes = manager->QueryInterface(iids[rng() % 3], &out);
                        drawfx::Note("qi", {yes ? 1u : 0u, drawfx::Norm(drawfx::P(out))});
                        if (yes) manager->Release();
                        break;
                    }
                    }
                    NoteManager(manager);
                    drawfx::Note("display", uint32_t(gDisplayCount));
                }
                if (rng() % 2) drawfx::Note("final shutdown", manager->Shutdown() ? 1u : 0u);
                difftest::FreeRecorder rec;
                drawfx::Note("last", manager->Release());
                rec.Stop();
                size_t sizes[16];
                const size_t n = rec.SortedSizes(sizes, 16);
                drawfx::Note("frees", uint32_t(n));
                for (size_t i = 0; i < n; ++i) drawfx::Note("freed", uint32_t(sizes[i]));
                NoteRefs();
            });
        };
        auto a = side(false);
        auto b = side(true);
        if (!drawfx::Same(ctx, a.first, b.first, "manager", seed, a.second.empty() ? b.second : a.second)) return;
    }
}

// The helpers: the colour format's bits (0x5ba211), the graphics system's acquisition (0x7ae7f4) and its cache
// (0x9c5a05) with and without a framework, an answering service and a cached value.
DIFF_TEST(RZCursor_Helpers, 0x9c5a05)
{
    using Bits = uint32_t(__stdcall *)(uint32_t);
    for (uint32_t format = 0; format < 0x14; ++format) {
        for (const uint32_t f : {format, format | 0x80000000u, format << 16}) {
            ++ctx.cases;
            const uint32_t a = ctx.Original<Bits>(0x5ba211)(f);
            const uint32_t b = ColorFormatBits(f);
            if (a != b) ctx.Fail("format 0x%x: original %u, ours %u", f, a, b);
        }
    }
    impfx::Imports imp(ctx);
    using Acquire = ScopedRef *(__thiscall *)(ScopedRef *);
    using Get = void *(__cdecl *)();
    for (uint32_t mask = 0; mask < 16; ++mask) {
        auto side = [&](bool ours) {
            drawfx::Cells cells;
            gCfg = Config{};
            gCfg.framework = mask & 1;
            gCfg.serviceAnswers = (mask >> 1) & 1;
            gCfg.graphicsCell = (mask >> 2) & 1;
            return drawfx::Side([&] { BuildWorld(imp, cells, mask, 0); }, [&] {
                if (mask & 8) {
                    ScopedRef ref{At<void>(0xc7000000)};
                    ScopedRef *r = ours ? cRZCursorAdapters::AcquireGraphicSystem(&ref, nullptr)
                                        : ctx.Original<Acquire>(0x7ae7f4)(&ref);
                    drawfx::Note("acquire", {r == &ref ? 1u : 0u, drawfx::Norm(drawfx::P(ref.mObject))});
                } else {
                    void *g = ours ? static_cast<void *>(GetGraphicSystem()) : ctx.Original<Get>(0x9c5a05)();
                    drawfx::Note("get", drawfx::Norm(drawfx::P(g)));
                    drawfx::Note("again", drawfx::Norm(drawfx::P(ours ? static_cast<void *>(GetGraphicSystem())
                                                                      : ctx.Original<Get>(0x9c5a05)())));
                }
                drawfx::Note("cell", drawfx::Norm(*GameAddress<uint32_t>(kGraphicSystemCell)));
                NoteRefs();
            });
        };
        auto a = side(false);
        auto b = side(true);
        if (!drawfx::Same(ctx, a.first, b.first, "graphics", mask, a.second.empty() ? b.second : a.second)) return;
    }
}

// The game's vtables hold the slots our declarations name (the scripts call the original objects through them).
DIFF_TEST(RZCursor_VTables, 0xe61c64)
{
    struct VSlot {
        uint32_t vtable, slot, rva;
    };
    static constexpr VSlot kSlots[] = {
        {0xe61c64, 0, 0x9d09a9},  {0xe61c64, 1, 0x148fb0},  {0xe61c64, 2, 0x7acd61},  {0xe61c64, 3, 0x144910},
        {0xe61c64, 4, 0x144910},  {0xe61c64, 5, 0x9d0a62},  {0xe61c64, 6, 0x9d0b70},  {0xe61c64, 7, 0x9d03bf},
        {0xe61c64, 8, 0x9d0b5b},  {0xe61c64, 9, 0x9d04f2},  {0xe61c64, 10, 0x9d0b39}, {0xe61c64, 11, 0x9d0b0d},
        {0xe61c64, 12, 0x1480a0}, {0xe61c64, 13, 0x1457d0}, {0xe61c64, 14, 0x9d02a3},
        {0xe61cf0, 0, 0x9d09a9},  {0xe61cf0, 5, 0x9d0a62},  {0xe61cf0, 9, 0x18a7a0},  {0xe61cf0, 10, 0x220010},
        {0xe61cf0, 11, 0x9d0b0d}, {0xe61cf0, 14, 0x9d02e1},
        {0xe61ca4, 0, 0x9d09e1},  {0xe61ca4, 1, 0x145260},  {0xe61ca4, 2, 0x9d0a19},  {0xe61ca4, 3, 0x9d03fa},
        {0xe61ca4, 4, 0x9d0ba7},  {0xe61ca4, 5, 0x9d030a},  {0xe61ca4, 6, 0x9d033a},  {0xe61ca4, 7, 0x9d0a30},
        {0xe61ca4, 8, 0x15f390},  {0xe61ca4, 9, 0x9d03a0},  {0xe61ca4, 10, 0x9d0af3}, {0xe61ca4, 11, 0x9d0ac4},
        {0xe61ca4, 12, 0x9d0967}, {0xe61ca4, 13, 0x9d0918}, {0xe61ca4, 14, 0x9d0b86}, {0xe61ca4, 15, 0x9d03d6},
        {0xe61ca4, 16, 0x9d04d2}, {0xe61ca4, 17, 0x9d02c2},
    };
    for (const VSlot &v : kSlots) {
        ++ctx.cases;
        void *const *table = GameAddress<void *const>(v.vtable);
        if (table[v.slot] != GameAddress<void>(v.rva)) ctx.Fail("vtable 0x%x slot %u is not 0x%x", v.vtable, v.slot, v.rva);
    }
}
