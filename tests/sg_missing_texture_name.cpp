// Differential test of the missing-texture drawing (src/sg/DrawMissingTextureName.*; lane VV, milestone O).
//
//   MissingTextureName_Draw  nGZSceneGraph::DrawMissingTextureName 0xae8310 (the original, with the image's surface lock
//       buffer 0x18a2a0 and its slots) against ours (the hook's entry, with our cSurfaceLockAsGZBuffer), on the same
//       fakes: the surface (Lock writes a lock record over a pixel area: the rectangle no wider than a row of 4-byte
//       pixels, the rows within the area, a random row byte count; the surface counted in the record or not; one time in
//       six nothing written; Unlock lets the surface go itself one time in three), its pixel format (slot 6 the bits,
//       the ARGB face always there: the original uses it unchecked), the ARGB face (16, 32 or other bits; PackColor
//       noting the colour bytes), the framework in g_Framework (0x10c9378) handing out the font system, the font system
//       (FindFont noting the family and answering a font, counted, four times in five), the font (the line spacing and
//       per-letter width seeded; GetWidth noting where in the name the text starts; DrawText noting where it draws, what,
//       the colours, and setting a pixel through the buffer it is given, through that buffer's own vtable), and the name
//       (a cIGZString fake: its letters, its length 0-13).
//
// The surface is held in the record whenever the name is drawn with a font (the original's colour conversions ask the
// record's surface, unchecked). Compared: the call log with arguments, every note, the pixels (hashed) and the counts of
// the surface, the ARGB face and the font.
#include "code_patch.h"
#include "sgfx.h"

#include "gz/PixelFormat.h"
#include "gz/cIGZFontSys.h"
#include "gz/cSurfaceDX.h"
#include "hook/hooks.h"
#include "sg/DrawMissingTextureName.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <random>

namespace {

using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;
using nGZGraphic4::cSurfaceLock;

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

constexpr At kArgbAt[] = {{6, {0, Ret::Int, true, "GetBitsPerPixel", ""}}, {16, {1, Ret::Int, true, "PackColor", "a"}}};
constexpr auto kArgbSlots = Table<18>(kArgbAt);
constexpr drawfx::Iface kArgb{"PixelFormatARGB", 18, kArgbSlots.data(), false, drawfx::kTestTable};

constexpr At kFrameworkAt[] = {{5, {3, Ret::Int, true, "GetSystemService", "uua"}}};
constexpr auto kFrameworkSlots = Table<11>(kFrameworkAt);
constexpr drawfx::Iface kFramework{"FrameWork", 11, kFrameworkSlots.data(), false, drawfx::kTestTable};

constexpr At kFontSysAt[] = {{11, {5, Ret::Int, true, "FindFont", "Suuub"}}};
constexpr auto kFontSysSlots = Table<24>(kFontSysAt);
constexpr drawfx::Iface kFontSys{"FontSys", 24, kFontSysSlots.data(), false, drawfx::kTestTable};

constexpr At kFontAt[] = {{35, {0, Ret::Int, true, "GetLineSpacing", ""}},
                          {49, {2, Ret::Int, true, "GetWidth", "au"}},
                          {52, {9, Ret::Int, true, "DrawText", "auuauuuaa"}}};
constexpr auto kFontSlots = Table<54>(kFontAt);
constexpr drawfx::Iface kFont{"Font", 54, kFontSlots.data(), false, drawfx::kTestTable};

constexpr At kNameAt[] = {{5, {0, Ret::Int, true, "ToChar", ""}}, {7, {0, Ret::Int, true, "Strlen", ""}}};
constexpr auto kNameSlots = Table<8>(kNameAt);
constexpr drawfx::Iface kName{"Name", 8, kNameSlots.data(), false, drawfx::kTestTable};

constexpr uint32_t kFrameworkCell = 0x10c9378;
constexpr unsigned kPixels = 4096;

struct World {
    Fake surface, format, argb, framework, fonts, font, name;
    alignas(16) uint8_t pixels[kPixels];
    char text[16];
    uint32_t lcg = 1;
    uint32_t letterWidth = 1;
    bool mustHold = false; // the surface always held and written in the record
    bool fontFound = true;
    uint32_t Next()
    {
        lcg = lcg * 1664525u + 1013904223u;
        return lcg >> 8;
    }
};
World gw;

uint32_t Pick(std::mt19937 &g, std::initializer_list<uint32_t> v) { return v.begin()[g() % v.size()]; }

uint32_t Lock(Fake &self, const uint32_t *a)
{
    auto *const lock = reinterpret_cast<cSurfaceLock *>(uintptr_t(a[0]));
    const uint32_t r = gw.Next();
    if (!gw.mustHold && r % 6 == 0) return 0;
    const bool held = gw.mustHold || (r >> 3) % 4 != 0;
    if (held) ++self.refs;
    lock->surface = held ? &self : nullptr;
    const uint32_t pitch = 16 * (1 + (r >> 5) % 8);
    const uint32_t rows = 1 + (r >> 9) % (kPixels / pitch);
    const int32_t left = int32_t((r >> 13) % 7) - 3, top = int32_t((r >> 16) % 7) - 3;
    lock->rect = {left, top, left + LONG(pitch / 4 - (r >> 19) % 3), top + LONG(rows)};
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
    const auto *const bgra = reinterpret_cast<const uint8_t *>(uintptr_t(a[0]));
    drawfx::Note("pack", {bgra[0], bgra[1], bgra[2], bgra[3]});
    return (uint32_t(bgra[0]) * 0x9e3779b1u) ^ (uint32_t(bgra[1]) << 9) ^ (uint32_t(bgra[2]) << 17) ^ (uint32_t(bgra[3]) << 25);
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
    if (!gw.fontFound) return 0;
    ++gw.font.refs;
    return drawfx::P(&gw.font);
}

uint32_t GetWidth(Fake &, const uint32_t *a)
{
    return a[1] * gw.letterWidth;
}

// DrawText(buffer, x, y, text, length, colour, colour2, clip, pos): a pixel set through the buffer's own vtable, where
// the text starts, inside the locked area when the record holds the surface (the buffer's pixel slots ask it).
uint32_t DrawText(Fake &, const uint32_t *a)
{
    void *const buffer = reinterpret_cast<void *>(uintptr_t(a[0]));
    void **const vt = *static_cast<void ***>(buffer);
    const uint32_t *const w = static_cast<const uint32_t *>(buffer);
    using Size = uint32_t(__thiscall *)(void *);
    using Set = void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
    const uint32_t width = reinterpret_cast<Size>(vt[9])(buffer), height = reinterpret_cast<Size>(vt[10])(buffer);
    drawfx::Note("buffer size", {width, height, w[9], w[10]});
    if (w[2] && width && w[10]) reinterpret_cast<Set>(vt[22])(buffer, a[1] % width, a[2] % w[10], a[5]);
    return 1;
}

uint32_t ToChar(Fake &, const uint32_t *) { return drawfx::P(gw.text); }

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
    gw.name.Reset<kName>(546);
    gw.surface.On(26, &Lock);
    gw.surface.On(27, &Unlock);
    gw.surface.Returns(22, &gw.format);
    const uint32_t bits = Pick(g, {16, 32, 32, 16, 24, 8});
    gw.format.Returns(6, bits);
    gw.format.AddFace(nGZGraphic4::kIidARGB, gw.argb);
    gw.argb.Returns(6, bits);
    gw.argb.On(16, &PackColor);
    gw.framework.On(5, &GetSystemService);
    gw.fonts.On(11, &FindFont);
    gw.font.Returns(35, 1 + g() % 20);
    gw.font.On(49, &GetWidth);
    gw.font.On(52, &DrawText);
    gw.letterWidth = 1 + g() % 9;
    gw.fontFound = g() % 5 != 0;
    const uint32_t length = g() % 6 == 0 ? 0 : 1 + g() % 13;
    std::memset(gw.text, 0, sizeof(gw.text));
    for (uint32_t i = 0; i < length; ++i) gw.text[i] = char('A' + g() % 26);
    gw.name.On(5, &ToChar);
    gw.name.Returns(7, length);
    gw.mustHold = length != 0 && gw.fontFound;
    for (unsigned i = 0; i < kPixels; ++i) gw.pixels[i] = uint8_t(i * 7 + seed);
    drawfx::AddRange(gw.pixels, kPixels, 0x51000000);
    drawfx::AddRange(gw.text, sizeof(gw.text), 0x52000000);
    cells.Set(kFrameworkCell, &gw.framework);
}

template <class Fn> Fn HookEntry(uint32_t rva)
{
    for (const HookSpec *h : RegisteredHooks())
        if (h->rva == rva) return reinterpret_cast<Fn>(h->replacement);
    return nullptr;
}

void NoteWorld()
{
    difftest::NotCounted bookkeeping;
    uint32_t hash = 2166136261u;
    for (uint8_t b : gw.pixels) hash = (hash ^ b) * 16777619u;
    drawfx::Note("pixels", hash);
    drawfx::Note("refs", {uint32_t(gw.surface.refs), uint32_t(gw.argb.refs), uint32_t(gw.fonts.refs), uint32_t(gw.font.refs)});
}

using Draw = void(__cdecl *)(void *, void *);

} // namespace

// Verifies: 0xae8310 (through its hook's entry), and 0x18a5d0 (cSurfaceLockAsGZBuffer::ConvertRGBValueToNative, which
// the original calls directly; ours calls our member).
DIFF_TEST(MissingTextureName_Draw, 0xae8310)
{
    sgfx::CheckSides(
        ctx, "missing texture name", 3000, [&](uint32_t seed, drawfx::Cells &cells) { Build(seed, cells); },
        [&](bool original, std::mt19937 &) {
            if (original) ctx.Original<Draw>(0xae8310)(&gw.surface, &gw.name);
            else HookEntry<Draw>(0xae8310)(&gw.surface, &gw.name);
            NoteWorld();
            ++ctx.cases;
        });
}
