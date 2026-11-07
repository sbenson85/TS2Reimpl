// Differential test of nGZSceneGraph::ChooseTextureDescription and the range copy constructor it calls, 0x167180
// (lane TH, milestone O row M4b; lane WN for the copy;
// src/sg/TextureDescription.h).
//
//   SG_ChooseTextureDescription  0xab5f50 called directly: a random image format (ARGB formats with or without an
//       alpha-luminance face, DXT1 / DXT5 formats, some DXT ones with an ARGB face too), normal map or not, kinds 4, 6
//       and others, random sizes and levels, the texture manager's filter flags 0-3, which formats the device takes
//       for each kind, and the device's colour format list (0-6 of the formats, its cursor at the start or one in).
//       The description, the result and every call on the fakes are compared, then the fakes' counts and the heap.
//
//   SG_AdjustCubeTextureSize  0x18e1d0 called directly (random caps, edges, levels and formats).
//   SG_AdjustTextureSize  0x18c990 called directly (lane VG): random texture caps (power of two, square only,
//       conditional non-powers of two, mipmaps), maxima and aspect ratios, sizes around 32 and the powers of two,
//       negative and huge sizes, levels, formats (DXT, YUV, others), and the last argument 0, 1 or with only high bits.
//
// Stood in on both sides: the device getter 0x18ae40, AdjustTextureSize 0x18c990 and the cube fit 0x18e1d0 (ours and
// the game's on both sides: they note their arguments and shrink the size). FindClosestColorFormat 0x18a9b0 and the
// range release 0x146670 run as each side has them (the image's, ours). The texture manager is a fake in 0xae8750's
// cache 0x10f8f94, the formats manager one in its cache 0x10ca830.
#include "sgfx.h"

#include "code_patch.h"

#include "gz/GraphicServices.h"
#include "sg/TextureDescription.h"

#include <array>
#include <bit>
#include <climits>
#include <cstdint>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;

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

constexpr At kDeviceAt[] = {{15, {0, Ret::Int, true, "GetDeviceEnum", ""}}, {16, {0, Ret::Int, true, "GetDeviceEnumConst", ""}}};
constexpr auto kDeviceSlots = Table<17>(kDeviceAt);
constexpr drawfx::Iface kDevice{"Device", 17, kDeviceSlots.data()};

// The device enumeration: 9 GetColorFormatsForDisplay (hidden result, usage, iid), 11 IsColorFormatSupportedForDisplay.
constexpr At kEnumAt[] = {{9, {3, Ret::Int, true, "GetColorFormatsForDisplay", "auu"}},
                          {11, {2, Ret::Int, true, "IsColorFormatSupportedForDisplay", "au"}},
                          {24, {0, Ret::Int, true, "GetCurrentCaps", ""}}};
constexpr auto kEnumSlots = Table<25>(kEnumAt);
constexpr drawfx::Iface kEnum{"DeviceEnum", 25, kEnumSlots.data()};

// A pixel format and its faces (each face the format itself): 6 bits per pixel, 11-14 alpha, red, green, blue (ARGB),
// 15 unused bits (ARGB) or the version (DXT).
constexpr At kFormatAt[] = {{5, {0, Ret::Int, true, "GetClassId", ""}}, {6, {0, Ret::Int, true, "BitsPerPixel", ""}},  {11, {0, Ret::Int, true, "AlphaBits", ""}},
                            {12, {0, Ret::Int, true, "RedBits", ""}},      {13, {0, Ret::Int, true, "GreenBits", ""}},
                            {14, {0, Ret::Int, true, "BlueBits", ""}},     {15, {0, Ret::Int, true, "Slot15", ""}}};
constexpr auto kFormatSlots = Table<16>(kFormatAt);
constexpr drawfx::Iface kFormat{"PixelFormat", 16, kFormatSlots.data()};

// The pixel format iterator: 3 Clone, 4 IsEqual, 5 Get, 6 Next, 8 CloneFormatIterator.
constexpr At kIterAt[] = {{3, {0, Ret::Int, true, "Clone", ""}},
                          {4, {1, Ret::Int, true, "IsEqual", "a"}},
                          {5, {0, Ret::Int, true, "Get", ""}},
                          {6, {0, Ret::Int, true, "Next", ""}},
                          {8, {0, Ret::Int, true, "CloneFormatIterator", ""}}};
constexpr auto kIterSlots = Table<9>(kIterAt);
constexpr drawfx::Iface kIter{"FormatIterator", 9, kIterSlots.data()};

// The texture manager: 26 GetFilterParams (hidden result).
constexpr At kTexManAt[] = {{26, {1, Ret::Int, true, "GetFilterParams", "a"}}};
constexpr auto kTexManSlots = Table<27>(kTexManAt);
constexpr drawfx::Iface kTexMan{"TextureManager", 27, kTexManSlots.data()};

// The formats manager: 6 GetARGB(bits, alpha, red, green, blue).
constexpr At kFormatsAt[] = {{6, {5, Ret::Int, true, "GetARGB", "uuuuu"}}};
constexpr auto kFormatsSlots = Table<7>(kFormatsAt);
constexpr drawfx::Iface kFormats{"FormatsManager", 7, kFormatsSlots.data()};

constexpr uint32_t kIidDxt = 0xbac3f24f, kIidArgb = 0x9ac38991, kIidAlphaLum = 0x2cb4c095;
constexpr uint32_t kTexManCell = 0x10f8f94, kFormatsCell = 0x10ca830;
constexpr unsigned kFormatCount = 8, kNormalFormat = 7, kIters = 24, kListMax = 6;

struct World {
    Fake device, enumeration, texman, formatsManager, alFace;
    Fake formats[kFormatCount];
    Fake iters[kIters];
    unsigned nextIter = 0;
    Fake *list[kListMax];
    unsigned listSize = 0;
    unsigned cursorStart = 0;
    uint32_t flags = 0;
    uint32_t lcg = 1;
    uint32_t Next()
    {
        lcg = lcg * 1664525u + 1013904223u;
        return lcg >> 8;
    }
};
World gw;

// Format values: 0 bits, 1 alpha, 2 red, 3 green, 4 blue, 5 slot 15, 6 the kinds the device takes (bit per kind & 7).
template <unsigned I> uint32_t FormatWord(Fake &self, const uint32_t *) { return self.value[I]; }

uint32_t GetEnum(Fake &, const uint32_t *) { return drawfx::P(&gw.enumeration); }
uint32_t Supported(Fake &, const uint32_t *a)
{
    const Fake *const format = reinterpret_cast<const Fake *>(uintptr_t(a[0]));
    return (format->value[6] >> (a[1] & 7)) & 1;
}

Fake *NewIter(uint32_t pos)
{
    Fake *const it = &gw.iters[gw.nextIter++ % kIters];
    it->value[0] = pos;
    return it;
}
uint32_t IterClone(Fake &self, const uint32_t *) { return drawfx::P(NewIter(self.value[0])); }
uint32_t IterEqual(Fake &self, const uint32_t *a)
{
    return self.value[0] == reinterpret_cast<const Fake *>(uintptr_t(a[0]))->value[0];
}
uint32_t IterGet(Fake &self, const uint32_t *) { return self.value[0] < gw.listSize ? drawfx::P(gw.list[self.value[0]]) : 0; }
uint32_t IterNext(Fake &self, const uint32_t *)
{
    ++self.value[0];
    return 0;
}
// GetColorFormatsForDisplay: the device's list, each end counted.
uint32_t ColorFormats(Fake &, const uint32_t *a)
{
    Fake **const range = reinterpret_cast<Fake **>(uintptr_t(a[0]));
    range[0] = NewIter(gw.cursorStart);
    range[1] = NewIter(0);
    range[2] = NewIter(gw.listSize);
    for (unsigned i = 0; i < 3; ++i) ++range[i]->refs;
    return a[0];
}
uint32_t FilterParams(Fake &, const uint32_t *a)
{
    uint32_t *const out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    out[0] = gw.flags;
    out[1] = 3;
    out[2] = std::bit_cast<uint32_t>(-0.2f);
    return a[0];
}
uint32_t GetArgb(Fake &, const uint32_t *) { return drawfx::P(&gw.formats[kNormalFormat]); }

void *__cdecl StandDevice() { return &gw.device; }
// 0x18c990's stand: notes its arguments and halves the size above 256, holding the levels to 9.
void __cdecl StandAdjust(void *device, void *format, int32_t *width, int32_t *height, uint32_t *levels, uint32_t last)
{
    drawfx::Note("adjust", {drawfx::Ptr(device), drawfx::Ptr(format), uint32_t(*width), uint32_t(*height), *levels, last});
    if (*width > 256) *width /= 2;
    if (*height > 256) *height /= 2;
    if (*levels > 9) *levels = 9;
}
// 0x18e1d0's stand: the same for a cube's edge.
void __cdecl StandAdjustCube(void *device, void *format, int32_t *edge, uint32_t *levels)
{
    drawfx::Note("adjust cube", {drawfx::Ptr(device), drawfx::Ptr(format), uint32_t(*edge), *levels});
    if (*edge > 128) *edge = 128;
    if (*levels > 7) *levels = 7;
}

struct Stands {
    shape::StandIn device{0x18ae40, &nGZGraphic4::GetCurrentDeviceCached, &StandDevice};
    shape::StandIn adjust{0x18c990, &AdjustTextureSize, &StandAdjust};
    shape::StandIn adjustCube{0x18e1d0, &AdjustCubeTextureSize, &StandAdjustCube};
};

void Build(uint32_t seed, drawfx::Cells &cells)
{
    gw.lcg = seed * 2654435761u + 11;
    gw.nextIter = 0;
    gw.device.Reset<kDevice>(540);
    gw.device.On(16, &GetEnum);
    gw.enumeration.Reset<kEnum>(541);
    gw.enumeration.On(9, &ColorFormats);
    gw.enumeration.On(11, &Supported);
    gw.texman.Reset<kTexMan>(542);
    gw.texman.On(26, &FilterParams);
    gw.formatsManager.Reset<kFormats>(543);
    gw.formatsManager.On(6, &GetArgb);
    gw.alFace.Reset<sgfx::kUnknown>(544);
    // 12 and 15 sit between the chooser's colour minimums (12 / 16 for DXT, the clamp to 16).
    static constexpr uint32_t kBits[] = {8, 12, 15, 16, 16, 24, 32, 32};
    static constexpr uint32_t kChannel[] = {0, 1, 4, 5, 6, 8};
    for (unsigned i = 0; i < kFormatCount; ++i) {
        Fake &f = gw.formats[i];
        f.Reset<kFormat>(550 + i);
        f.On(6, &FormatWord<0>);
        f.On(11, &FormatWord<1>);
        f.On(12, &FormatWord<2>);
        f.On(13, &FormatWord<3>);
        f.On(14, &FormatWord<4>);
        f.On(15, &FormatWord<5>);
        const uint32_t r = gw.Next();
        f.value[0] = kBits[r % 8];
        f.value[1] = kChannel[(r >> 3) % 6];
        f.value[2] = kChannel[(r >> 6) % 6];
        f.value[3] = kChannel[(r >> 9) % 6];
        f.value[4] = kChannel[(r >> 12) % 6];
        f.value[5] = (r >> 15) % 3 == 0 ? 1 : (r >> 15) % 6;
        f.value[6] = gw.Next() & 0xff;
        const bool dxt = i == 4 || i == 5 || (i == 6 && seed % 3 == 0);
        if (dxt) f.AddFace(kIidDxt, f);
        if (!dxt || (r >> 18) % 4 == 0) f.AddFace(kIidArgb, f);
        if (!dxt && (r >> 20) % 3 == 0) f.AddFace(kIidAlphaLum, gw.alFace);
    }
    for (unsigned i = 0; i < kIters; ++i) {
        gw.iters[i].Reset<kIter>(600 + i);
        gw.iters[i].On(3, &IterClone);
        gw.iters[i].On(4, &IterEqual);
        gw.iters[i].On(5, &IterGet);
        gw.iters[i].On(6, &IterNext);
        gw.iters[i].On(8, &IterClone);
    }
    gw.listSize = gw.Next() % (kListMax + 1);
    for (unsigned i = 0; i < gw.listSize; ++i) gw.list[i] = &gw.formats[gw.Next() % kFormatCount];
    gw.cursorStart = gw.listSize && gw.Next() % 4 == 0 ? 1 : 0;
    gw.flags = gw.Next() % 4;
    cells.Set(kTexManCell, &gw.texman);
    cells.Set(kFormatsCell, &gw.formatsManager);
}

void NoteFakes()
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = "fake refs";
    for (Fake *f : {&gw.device, &gw.enumeration, &gw.texman, &gw.formatsManager, &gw.alFace})
        c.words.push_back(uint32_t(f->refs));
    for (const Fake &f : gw.formats) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.iters) c.words.push_back(uint32_t(f.refs));
    c.words.push_back(gw.nextIter);
    drawfx::gLog.push_back(std::move(c));
}

// The caps the cube fit reads (D3DCAPS9 +0x3c TextureCaps, +0x58 MaxTextureWidth, +0x5c MaxTextureHeight).
uint32_t gCapsWords[76];
uint32_t GetCaps(Fake &, const uint32_t *) { return drawfx::P(gCapsWords); }

constexpr uint32_t kIidYuv = 0x9ac3899e, kIidEnumFace = 0x6c02c311;

void BuildCube(uint32_t seed, drawfx::Cells &)
{
    gw.lcg = seed * 2654435761u + 29;
    gw.device.Reset<kDevice>(560);
    gw.device.On(15, &GetEnum);
    gw.enumeration.Reset<kEnum>(561);
    gw.enumeration.On(24, &GetCaps);
    gw.enumeration.AddFace(kIidEnumFace, gw.enumeration);
    static constexpr uint32_t kIds[] = {kIidDxt, kIidYuv, kIidArgb, 0x12345678};
    for (unsigned i = 0; i < kFormatCount; ++i) {
        Fake &f = gw.formats[i];
        f.Reset<kFormat>(570 + i);
        f.On(5, &FormatWord<7>);
        f.value[7] = kIds[i % 4];
    }
    for (uint32_t &w : gCapsWords) w = gw.Next();
    // The two texture flags the fit reads: each present about half the time, on top of random other bits.
    gCapsWords[0x3c / 4] = (gCapsWords[0x3c / 4] & ~0x30000u) | (gw.Next() % 2 ? 0x20000u : 0) | (gw.Next() % 2 ? 0x10000u : 0);
    // Maxima: a power of two, an odd value, zero, or a huge unsigned.
    static constexpr uint32_t kMax[] = {16, 64, 256, 1024, 4096, 100, 7, 0, 1, 0xfffffff0u, 0x80000001u};
    gCapsWords[0x58 / 4] = kMax[gw.Next() % 11];
    gCapsWords[0x5c / 4] = gw.Next() % 3 ? kMax[gw.Next() % 11] : gCapsWords[0x58 / 4];
}

// The 2D fit's caps: the cube's fakes, the texture flags it reads (+0x3c: 0x2 power of two, 0x20 square only, 0x100
// conditional non-powers of two, 0x4000 mipmaps; each about half the time), maxima (+0x58, +0x5c) and the aspect
// ratio (+0x68).
void BuildFit(uint32_t seed, drawfx::Cells &cells)
{
    BuildCube(seed * 7 + 3, cells);
    uint32_t caps = gCapsWords[0x3c / 4] & ~0x4122u;
    for (const uint32_t bit : {0x2u, 0x20u, 0x100u, 0x4000u})
        if (gw.Next() % 2) caps |= bit;
    if (gw.Next() % 3 == 0) caps |= 0x102; // the pair the non-power-of-two case needs
    if (gw.Next() % 4 == 0) caps &= ~0x20u;
    gCapsWords[0x3c / 4] = caps;
    static constexpr uint32_t kMax[] = {16, 64, 256, 1024, 4096, 100, 7, 0, 1, 2048, 0xfffffff0u, 0x80000001u};
    gCapsWords[0x58 / 4] = gw.Next() % 3 ? kMax[gw.Next() % 9 + 2] : kMax[gw.Next() % 12];
    gCapsWords[0x5c / 4] = gw.Next() % 3 ? kMax[gw.Next() % 9 + 2] : kMax[gw.Next() % 12];
    static constexpr uint32_t kRatio[] = {0, 1, 2, 3, 4, 8, 16, 2048, 0x80000000u};
    gCapsWords[0x68 / 4] = gw.Next() % 5 ? kRatio[gw.Next() % 9] : gw.Next();
}

} // namespace

// Verifies: 0x18c990 (AdjustTextureSize).
DIFF_TEST(SG_AdjustTextureSize, 0x18c990)
{
    // As the game's callers see it: the last argument a byte in a stack word.
    using Fn = void(__cdecl *)(void *, void *, int32_t *, int32_t *, uint32_t *, uint32_t);
    const Fn original = ctx.Original<Fn>(0x18c990);
    const Fn ours = reinterpret_cast<Fn>(&AdjustTextureSize);
    unsigned branches[4] = {}; // the power-of-two roundings: up, down; any size; the levels counted
    sgfx::CheckSides(
        ctx, "adjust texture size", 4000, [&](uint32_t seed, drawfx::Cells &cells) { BuildFit(seed, cells); },
        [&](bool isOriginal, std::mt19937 &rng) {
            static constexpr int32_t kSizes[] = {0,  1,   2,   3,   5,    31,   32,   33,         34,        63,
                                                 64, 65,  100, 255, 256,  257,  1000, 4000,       -1,        -77,
                                                 7,  513, 48,  96,  1023, 4097, 0x40000001, 0x7fffffff, INT32_MIN};
            auto size = [&] { return rng() % 3 ? int32_t(rng() % 1100) - 20 : kSizes[rng() % std::size(kSizes)]; };
            int32_t width = size(), height = size();
            uint32_t levels = rng() % 4 ? rng() % 14 : (rng() % 2 ? 0xfffffff0u : 0);
            static constexpr uint32_t kLast[] = {0, 1, 0x100, 0xffffff01u};
            const uint32_t last = kLast[rng() % 4];
            Fake *const format = &gw.formats[rng() % kFormatCount];
            if (!isOriginal) {
                const uint32_t capsWord = gCapsWords[0x3c / 4];
                const bool allow = (last & 0xff) != 0;
                const bool anySize = allow && (capsWord & 0x102) == 0x102 && format->value[7] != kIidDxt;
                if (anySize)
                    ++branches[2];
                else if (capsWord & 2)
                    ++branches[allow && width > 32 && height > 32 ? 1 : 0];
                if (!anySize && (capsWord & 0x4000) && int32_t(levels) > 0) ++branches[3];
            }
            (isOriginal ? original : ours)(&gw.device, format, &width, &height, &levels, last);
            drawfx::Note("fit", {uint32_t(width), uint32_t(height), levels});
            NoteFakes();
            ++ctx.cases;
        });
    for (unsigned b = 0; b < 4; ++b)
        if (branches[b] < 100) ctx.Fail("adjust texture size: branch %u reached %u times", b, branches[b]);
}

// Verifies: 0x18e1d0 (AdjustCubeTextureSize).
DIFF_TEST(SG_AdjustCubeTextureSize, 0x18e1d0)
{
    using Fn = void(__cdecl *)(void *, void *, int32_t *, uint32_t *);
    const Fn original = ctx.Original<Fn>(0x18e1d0);
    sgfx::CheckSides(
        ctx, "adjust cube texture size", 3000, [&](uint32_t seed, drawfx::Cells &cells) { BuildCube(seed, cells); },
        [&](bool isOriginal, std::mt19937 &rng) {
            static constexpr int32_t kEdges[] = {0, 1, 2, 3, 5, 31, 32, 33, 100, 255, 256, 257, 1000, 4000, -1, -77};
            int32_t edge = rng() % 3 ? int32_t(rng() % 1100) - 20 : kEdges[rng() % 16];
            uint32_t levels = rng() % 4 ? rng() % 14 : (rng() % 2 ? 0xfffffff0u : 0);
            Fake *const format = &gw.formats[rng() % kFormatCount];
            if (isOriginal)
                original(&gw.device, format, &edge, &levels);
            else
                AdjustCubeTextureSize(reinterpret_cast<nGZGraphic4::cIDeviceDX *>(&gw.device),
                                      reinterpret_cast<nGZGraphic4::cIPixelFormat *>(format), &edge, &levels);
            drawfx::Note("fit", {uint32_t(edge), levels});
            NoteFakes();
            ++ctx.cases;
        });
}

// Verifies: 0xab5f50 (ChooseTextureDescription).
DIFF_TEST(SG_ChooseTextureDescription, 0xab5f50)
{
    Stands stands;
    using Fn = bool(__cdecl *)(TextureDescription *, int32_t, int32_t, uint32_t, nGZGraphic4::cIPixelFormat *, bool,
                               uint32_t);
    const Fn original = ctx.Original<Fn>(0xab5f50);
    sgfx::CheckSides(
        ctx, "choose texture description", 3000, [&](uint32_t seed, drawfx::Cells &cells) { Build(seed, cells); },
        [&](bool isOriginal, std::mt19937 &rng) {
            static constexpr uint32_t kKinds[] = {4, 6, 4, 6, 0, 5};
            const uint32_t kind = kKinds[rng() % 6];
            const int32_t width = int32_t(1 + rng() % 1024), height = int32_t(1 + rng() % 1024);
            const uint32_t levels = rng() % 12;
            auto *const format = reinterpret_cast<nGZGraphic4::cIPixelFormat *>(&gw.formats[rng() % kFormatCount]);
            const bool normalMap = rng() % 4 == 0;
            difftest::AllocationCounter counter;
            TextureDescription desc{-1, -1, 0xffffffffu, reinterpret_cast<nGZGraphic4::cIPixelFormat *>(&gw.device)};
            const bool chosen = isOriginal ? original(&desc, width, height, levels, format, normalMap, kind)
                                           : ChooseTextureDescription(&desc, width, height, levels, format, normalMap, kind);
            counter.Stop();
            drawfx::Note("chosen", uint32_t(chosen));
            drawfx::Note("desc", {uint32_t(desc.width), uint32_t(desc.height), desc.levels, drawfx::Ptr(desc.format)});
            NoteFakes();
            drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
            ++ctx.cases;
        });
}
