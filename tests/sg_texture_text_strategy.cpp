// Differential test of the debug text billboards' strategy (lane 261111m; src/sg/cTextureTextUpdateStrategy.h).
//
//   SG_TextureTextStrategy  a strategy's life on each side: the original's constructor 0xaf3560 or ours, on the same
//       dirty block (0x1c bytes of operator new memory filled with a seeded byte), with a seeded height and font size;
//       then random steps: QueryInterface 0xaf4100 (iids 0, 1, the label helpers' 0xcb86c1bd, others; a null out),
//       AddRef / Release in runs, Push (0xaf37f0 / ours: colours with and without alpha; texts empty, short, "%s" /
//       "%d", 15 / 16 characters, long), Clear (0xaf40b0 / ours), Update 0xaf44b0 (through the vtable), GetDescription
//       and Finish (the base's slots); after each, the whole state: the count, the font, every line's colour, the text
//       (its size, capacity, cRZString count word and letters) and the vector's size and capacity. The last Release is
//       the deleting destructor 0xaf3780; the font's Release notes the heap's live blocks at that moment (the lines are
//       freed before the font goes) and the heap is compared at the end.
//
// Fakes: the framework in g_Framework (0x10c9378) hands out the font system (GetSystemService, counted); the font system
// (FindFont noting the name, the size and the flags, answering the font, counted); the font (SetQuality noted; the line
// spacing seeded: 1, small, larger than the height, negative (no lines: one line)); DrawText notes x, y, the text and
// its length, the colours and the clip / position words. The surface: Lock writes a lock record over a pixel area (the
// surface counted in it or not; Unlock lets it go itself one time in three), its pixel format (the ARGB face always
// there, as the original uses it unchecked), PackColor notes the colour's bytes. The pixels are hashed after each
// Update (the clear: every row's bytes set to 0, the pitch between rows). The ring: pushes up to twice the size and
// past it (the oldest overwritten, the first drawn line the oldest kept), Clear in between.
#include "sgfx.h"

#include "gz/PixelFormat.h"
#include "gz/cIGZFontSys.h"
#include "gz/cSurfaceDX.h"
#include "free_recorder.h"
#include "sg/cTextureTextUpdateStrategy.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <string>

namespace {

using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;
using nGZGraphic4::cILockableSurface;
using nGZGraphic4::cSurfaceLock;
using nGZSceneGraph::cTextureTextUpdateStrategy;
using nRZSceneGraph::cITextureUpdateStrategy;

struct At {
    unsigned slot;
    Slot s;
};
template <size_t N, size_t K> constexpr std::array<Slot, N> Table(const At (&at)[K])
{
    std::array<Slot, N> t{};
    for (Slot &s : t) s = {0, Ret::Int, false, "", ""};
    t[0] = {2, Ret::Int, true, "QueryInterface", "ua"};
    t[1] = {0, Ret::Int, true, "AddRef", ""};
    t[2] = {0, Ret::Int, true, "Release", ""};
    for (const At &a : at) t[a.slot] = a.s;
    return t;
}

constexpr At kSurfaceAt[] = {{22, {0, Ret::Int, true, "GetPixelFormat", ""}},
                             {26, {2, Ret::Int, true, "Lock", "ab"}},
                             {27, {1, Ret::Int, true, "Unlock", "a"}}};
constexpr auto kSurfaceSlots = Table<29>(kSurfaceAt);
constexpr drawfx::Iface kSurface{"LockableSurface", 29, kSurfaceSlots.data(), false, drawfx::kTestTable};

constexpr At kFormatAt[] = {{6, {0, Ret::Int, true, "GetBitsPerPixel", ""}}};
constexpr auto kFormatSlots = Table<11>(kFormatAt);
constexpr drawfx::Iface kFormat{"PixelFormat", 11, kFormatSlots.data(), false, drawfx::kTestTable};

constexpr At kArgbAt[] = {{16, {1, Ret::Int, true, "PackColor", "F1"}}};
constexpr auto kArgbSlots = Table<18>(kArgbAt);
constexpr drawfx::Iface kArgb{"PixelFormatARGB", 18, kArgbSlots.data(), false, drawfx::kTestTable};

constexpr At kFrameworkAt[] = {{5, {3, Ret::Int, true, "GetSystemService", "uua"}}};
constexpr auto kFrameworkSlots = Table<11>(kFrameworkAt);
constexpr drawfx::Iface kFramework{"FrameWork", 11, kFrameworkSlots.data(), false, drawfx::kTestTable};

constexpr At kFontSysAt[] = {{11, {5, Ret::Int, true, "FindFont", "Suuub"}}};
constexpr auto kFontSysSlots = Table<24>(kFontSysAt);
constexpr drawfx::Iface kFontSys{"FontSys", 24, kFontSysSlots.data(), false, drawfx::kTestTable};

constexpr At kFontAt[] = {{16, {1, Ret::Int, true, "SetQuality", "u"}},
                          {35, {0, Ret::Int, true, "GetLineSpacing", ""}},
                          {52, {9, Ret::Int, true, "DrawText", "auusuuuaa"}}};
constexpr auto kFontSlots = Table<54>(kFontAt);
constexpr drawfx::Iface kFont{"Font", 54, kFontSlots.data(), false, drawfx::kTestTable};

constexpr uint32_t kFrameworkCell = 0x10c9378;
constexpr unsigned kPixels = 4096;

struct World {
    Fake surface, format, argb, framework, fonts, font;
    alignas(16) uint8_t pixels[kPixels];
    uint32_t lcg = 1;
    difftest::AllocationCounter *counter = nullptr;
    uint32_t Next()
    {
        lcg = lcg * 1664525u + 1013904223u;
        return lcg >> 8;
    }
};
World gw;

uint32_t Lock(Fake &self, const uint32_t *a)
{
    auto *const lock = reinterpret_cast<cSurfaceLock *>(uintptr_t(a[0]));
    const uint32_t r = gw.Next();
    const bool held = (r >> 3) % 4 != 0;
    if (held) ++self.refs;
    lock->surface = held ? &self : nullptr;
    const uint32_t pitch = 16 * (1 + (r >> 5) % 8);
    const uint32_t rows = (r >> 9) % (kPixels / pitch + 1);
    lock->rect = {0, 0, LONG(pitch / 4), LONG(rows)};
    lock->pitch = int32_t(pitch);
    lock->rowBytes = (r >> 21) % (pitch + 1);
    lock->rows = rows;
    lock->bits = gw.pixels;
    return pitch;
}

uint32_t Unlock(Fake &self, const uint32_t *a)
{
    auto *const lock = reinterpret_cast<cSurfaceLock *>(uintptr_t(a[0]));
    if (gw.Next() % 3 == 0 && lock->surface == &self) {
        --self.refs;
        lock->surface = nullptr;
    }
    return 0;
}

uint32_t PackColor(Fake &, const uint32_t *a)
{
    const auto *const c = reinterpret_cast<const uint8_t *>(uintptr_t(a[0]));
    return (uint32_t(c[0]) * 0x9e3779b1u) ^ (uint32_t(c[1]) << 9) ^ (uint32_t(c[2]) << 17) ^ (uint32_t(c[3]) << 25);
}

uint32_t GetSystemService(Fake &, const uint32_t *a)
{
    if (a[0] != cIGZFontSys::kServiceId || a[1] != cIGZFontSys::kIid) return 0;
    ++gw.fonts.refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &gw.fonts;
    return 1;
}

uint32_t FindFont(Fake &, const uint32_t *)
{
    ++gw.font.refs;
    return drawfx::P(&gw.font);
}

// The font's Release: the heap's live blocks when it is let go (the destructor frees the lines first).
uint32_t FontRelease(Fake &self, const uint32_t *)
{
    if (gw.counter) {
        difftest::NotCounted bookkeeping;
        drawfx::Note("font release, live blocks", uint32_t(gw.counter->Blocks()));
    }
    return uint32_t(--self.refs);
}

uint32_t Pick(std::mt19937 &g, std::initializer_list<uint32_t> v) { return v.begin()[g() % v.size()]; }

void Build(uint32_t seed, drawfx::Cells &cells)
{
    std::mt19937 g(seed * 31u + 5u);
    gw.lcg = seed * 2654435761u + 7;
    gw.surface.Reset<kSurface>(540);
    gw.format.Reset<kFormat>(541);
    gw.argb.Reset<kArgb>(542);
    gw.framework.Reset<kFramework>(543);
    gw.fonts.Reset<kFontSys>(544);
    gw.font.Reset<kFont>(545);
    gw.surface.On(26, &Lock);
    gw.surface.On(27, &Unlock);
    gw.surface.Returns(22, &gw.format);
    gw.format.Returns(6, 32);
    gw.format.AddFace(nGZGraphic4::kIidARGB, gw.argb);
    gw.argb.On(16, &PackColor);
    gw.framework.On(5, &GetSystemService);
    gw.fonts.On(11, &FindFont);
    gw.font.On(2, &FontRelease);
    // The line spacing: 1, small, larger than any height, negative (an unsigned division: no lines, so one).
    gw.font.Returns(35, Pick(g, {1, 2, 7, 8, 13, 16, 33, 1000, 0xfffffff0u, 0x80000000u}));
    for (unsigned i = 0; i < kPixels; ++i) gw.pixels[i] = uint8_t(i * 7 + seed);
    drawfx::AddRange(gw.pixels, kPixels, 0x51000000);
    cells.Set(kFrameworkCell, &gw.framework);
}

void NotePixels()
{
    difftest::NotCounted bookkeeping;
    uint32_t hash = 2166136261u;
    for (uint8_t b : gw.pixels) hash = (hash ^ b) * 16777619u;
    drawfx::Note("pixels", hash);
}

// The whole state through the layout (the same on both sides): refcount, font, count, the vector and every line.
void NoteState(const void *object)
{
    difftest::NotCounted bookkeeping;
    const auto *const w = static_cast<const uint32_t *>(object);
    drawfx::Note("words", {w[1], drawfx::Ptr(w[2]), w[3]});
    const uint32_t first = w[4], last = w[5], end = w[6];
    const uint32_t size = (last - first) / 0x24, capacity = (end - first) / 0x24;
    drawfx::Note("lines", {size, capacity, uint32_t((last - first) % 0x24), uint32_t((end - first) % 0x24)});
    for (uint32_t i = 0; i < size; ++i) {
        const auto *const line = reinterpret_cast<const cTextureTextUpdateStrategy::Line *>(uintptr_t(first) + i * 0x24);
        const std::string &text = line->mText.mString;
        drawfx::Note("line", {line->mColour, uint32_t(text.size()), uint32_t(text.capacity()), line->mText.mRefCount});
        sgfx::NoteText("text", text.c_str());
    }
}

constexpr const char *kTexts[] = {"",
                                  "a",
                                  "%s",
                                  "%d %s %n",
                                  "fifteen chars!!",
                                  "sixteen chars!!!",
                                  "a line of text longer than the short string buffer, with Mixed Case and 0123456789",
                                  "x\xe9y"};

cILockableSurface *Surface() { return reinterpret_cast<cILockableSurface *>(&gw.surface); }

using Ctor = void *(__thiscall *)(void *, uint32_t, uint32_t);
using PushFn = void(__thiscall *)(void *, uint32_t, const char *);
using ClearFn = void(__thiscall *)(void *);

void Step(difftest::Context &ctx, bool original, void *object, std::mt19937 &rng)
{
    auto *const t = static_cast<cITextureUpdateStrategy *>(object);
    const unsigned op = rng() % 10;
    drawfx::Note("op", op);
    switch (op) {
    case 0: {
        static constexpr uint32_t kIids[] = {0, 1, cTextureTextUpdateStrategy::kIid, 0x2a956da8, 0xdeadbeef};
        const uint32_t iid = kIids[rng() % std::size(kIids)];
        void *out = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
        const bool useOut = rng() % 4 != 0;
        const bool got = t->QueryInterface(iid, useOut ? &out : nullptr);
        drawfx::Note("query", {iid, uint32_t(uint8_t(got)), uint32_t(uintptr_t(out) == 0x5a5a5a5a),
                               uint32_t(out == object)});
        if (got) t->Release();
        break;
    }
    case 1: {
        const unsigned n = 1 + rng() % 4;
        for (unsigned i = 0; i < n; ++i) drawfx::Note("addref", t->AddRef());
        for (unsigned i = 0; i < n; ++i) drawfx::Note("release", t->Release());
        break;
    }
    case 2:
    case 3:
    case 4: { // a run of pushes: none, a few, past the size
        static constexpr uint32_t kColours[] = {0, 0xffffffff, 0x00123456, 0x80ff0000, 0x7f00ff00, 0xff0000ff};
        const unsigned n = rng() % 3 == 0 ? rng() % 40 : rng() % 4;
        for (unsigned i = 0; i < n; ++i) {
            const uint32_t colour = rng() % 3 ? kColours[rng() % std::size(kColours)] : uint32_t(rng());
            const char *const text = kTexts[rng() % std::size(kTexts)];
            if (original) ctx.Original<PushFn>(0xaf37f0)(object, colour, text);
            else static_cast<cTextureTextUpdateStrategy *>(object)->Push(colour, text);
        }
        drawfx::Note("pushed", n);
        break;
    }
    case 5:
        if (original) ctx.Original<ClearFn>(0xaf40b0)(object);
        else static_cast<cTextureTextUpdateStrategy *>(object)->Clear();
        break;
    case 6:
    case 7:
        t->Update(Surface(), rng() % 6, rng() % 2 != 0);
        NotePixels();
        break;
    case 8: {
        uint32_t width = 0x11, height = 0x22, levels = 0x33;
        float quality = 0.75f;
        const bool got = t->GetDescription(&width, &height, &levels, &quality);
        drawfx::Note("description", {uint32_t(uint8_t(got)), width, height, levels, std::bit_cast<uint32_t>(quality)});
        break;
    }
    default:
        t->Finish();
        break;
    }
    NoteState(object);
}

void Life(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    difftest::AllocationCounter counter;
    gw.counter = &counter;
    static constexpr uint32_t kHeights[] = {0, 1, 7, 15, 16, 17, 64, 100, 255};
    const uint32_t height = rng() % 5 ? kHeights[rng() % std::size(kHeights)] : rng() % 300;
    const uint32_t fontSize = Pick(rng, {0, 8, 10, 12, 0xffffffffu});
    const uint8_t dirt = uint8_t(rng());
    drawfx::Note("made with", {height, fontSize});
    void *const block = ::operator new(cTextureTextUpdateStrategy::kSize);
    std::memset(block, dirt, cTextureTextUpdateStrategy::kSize);
    void *made;
    if (original) made = ctx.Original<Ctor>(0xaf3560)(block, height, fontSize);
    else made = static_cast<cITextureUpdateStrategy *>(::new (block) cTextureTextUpdateStrategy(height, fontSize));
    drawfx::Note("made", uint32_t(made == block));
    NoteState(block);
    auto *const t = static_cast<cITextureUpdateStrategy *>(block);
    t->AddRef();
    const unsigned steps = 3 + rng() % 24;
    for (unsigned s = 0; s < steps; ++s) {
        char mark[32];
        std::snprintf(mark, sizeof(mark), "step %u", s);
        sgfx::MarkStep(ctx, mark);
        Step(ctx, original, block, rng);
    }
    sgfx::MarkStep(ctx, "last release");
    drawfx::Note("last release", t->Release());
    drawfx::Note("refs", {uint32_t(gw.surface.refs), uint32_t(gw.argb.refs), uint32_t(gw.fonts.refs),
                          uint32_t(gw.font.refs)});
    counter.Stop();
    gw.counter = nullptr;
    drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
}

} // namespace

// Verifies: 0xaf3560 (the constructor), 0xaf4100 (QueryInterface), 0xaf44b0 (Update), 0xaf3780 (the deleting
// destructor), 0xaf37f0 (Push), 0xaf40b0 (Clear), and the game's std::vector<Line> helpers the original side runs
// through them: 0xaf3220 (destroy a range), 0xaf3290 (reallocate), 0xaf34f0 (default-construct n).
DIFF_TEST(SG_TextureTextStrategy, 0xaf3560)
{
    sgfx::CheckSides(
        ctx, "text strategy", 1500, [&](uint32_t seed, drawfx::Cells &cells) { Build(seed, cells); },
        [&](bool original, std::mt19937 &rng) {
            Life(ctx, original, rng);
            ++ctx.cases;
        });
}
