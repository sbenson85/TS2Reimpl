// Differential tests for the memory-texture routines of cImageManipDX (lane OW, src/gz/cImageManipDXMemory.cpp): the level
// routines (copy, 24 to 32 bit conversion, the converter chooser, the three DXT level decoders, the two level reducers),
// the normal map kernel, and the two entry points CopyMemoryTextures (0x184710) and ComputeMemoryNormalMap (0x183500).
//
// The surfaces and textures are the engine's real memory ones (nRZGraphic4::cMemBlockAsSurface 0x18d1a0 / 0x18d2c0 and
// cMemBlockAsTexture 0x18c300 / 0x18c350 / 0x18c5b0 / 0x18c3e0, game code that both sides run): their bits live in a
// per-side arena, filled alike from the case's seed, and the whole arena (canaries around every level), the answers, the
// logs of the fakes and the objects' reference counts are compared after each side. The formats are our pixel format
// objects (src/gz/PixelFormat.h), the formats manager and the image manip are fakes (their cache globals 0x10ca830 and
// 0x10ca990 hold them, so the cached getters, the original's and ours, read them without a service): the manager
// answers by the arguments it is asked for and logs them; the image manip's CopySurfaceRect (slot 3) logs the
// rectangle, the colour key and the sizes, then copies the rectangle between the two surfaces (so a copy to a wrong
// level shows in the arena).
//
// The chooser (0x185a30) is run on fake textures and formats whose slots log every call (the queries and the releases
// of the interface answers, the bit counts in the order asked).
#include "difftest.h"

#include "game/original.h"
#include "game/vtable.h"
#include "gz/DxtBlocks.h"
#include "gz/MipKernels.h"
#include "gz/PixelFormat.h"
#include "gz/cImageManipDX.h"
#include "gz/cImageManipDXMemory.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/cILockableSurface.h"
#include "ifaces/cITexture.h"

#include <windows.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

using nGZGraphic4::cPixelFormat;
using nGZGraphic4::cPixelFormatARGB;
using nGZGraphic4::cPixelFormatAlphaLuminance;
using nGZGraphic4::cPixelFormatDXT;
namespace memchain = nGZGraphic4::memchain;
namespace mip = nGZGraphic4::mip;

namespace {

// ---- logs ----
struct Entry {
    int tag;
    uint32_t a, b, c, d;
    bool operator==(const Entry &o) const { return tag == o.tag && a == o.a && b == o.b && c == o.c && d == o.d; }
};
std::vector<Entry> gLog;
void Log(int tag, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0) { gLog.push_back({tag, a, b, c, d}); }

std::string LogDiff(const std::vector<Entry> &a, const std::vector<Entry> &b)
{
    char buf[256];
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i)
        if (!(a[i] == b[i])) {
            std::snprintf(buf, sizeof(buf), "log entry %zu: original (%d %x %x %x %x), ours (%d %x %x %x %x)", i, a[i].tag, a[i].a,
                          a[i].b, a[i].c, a[i].d, b[i].tag, b[i].a, b[i].b, b[i].c, b[i].d);
            return buf;
        }
    std::snprintf(buf, sizeof(buf), "log lengths %zu vs %zu", a.size(), b.size());
    return a.size() == b.size() ? std::string() : std::string(buf);
}

// ---- the formats ----
enum Fmt { kX8R8G8B8, kA8R8G8B8, kA2R10G10B10, kA8, kL8, kDxt1, kDxt3, kDxt5, kR8G8B8, kOther, kFmtCount };
struct FmtInfo {
    uint32_t id;
    int bytes;      // per pixel (0: compressed)
    int blockBytes; // compressed
};
constexpr FmtInfo kInfo[kFmtCount] = {
    {22, 4, 0}, {21, 4, 0}, {35, 4, 0}, {28, 1, 0}, {50, 1, 0}, {0x31545844, 0, 8}, {0x33545844, 0, 16}, {0x35545844, 0, 16}, {20, 3, 0}, {23, 2, 0},
};

struct FormatObjects {
    alignas(16) uint8_t storage[kFmtCount][0x40];
    void *Get(Fmt f) { return storage[f]; }
    FormatObjects()
    {
        std::memset(storage, 0, sizeof(storage));
        reinterpret_cast<cPixelFormatARGB *>(storage[kX8R8G8B8])->Construct(22, 32, 0, 8, 8, 8);
        reinterpret_cast<cPixelFormatARGB *>(storage[kA8R8G8B8])->Construct(21, 32, 8, 8, 8, 8);
        reinterpret_cast<cPixelFormatARGB *>(storage[kA2R10G10B10])->Construct(35, 32, 2, 10, 10, 10);
        reinterpret_cast<cPixelFormatARGB *>(storage[kA8])->Construct(28, 8, 8, 0, 0, 0);
        reinterpret_cast<cPixelFormatAlphaLuminance *>(storage[kL8])->Construct(50, 8, 0, 8);
        reinterpret_cast<cPixelFormatDXT *>(storage[kDxt1])->Construct(0x31545844, 1);
        reinterpret_cast<cPixelFormatDXT *>(storage[kDxt3])->Construct(0x33545844, 3);
        reinterpret_cast<cPixelFormatDXT *>(storage[kDxt5])->Construct(0x35545844, 5);
        reinterpret_cast<cPixelFormatARGB *>(storage[kR8G8B8])->Construct(20, 24, 0, 8, 8, 8);
        reinterpret_cast<cPixelFormatARGB *>(storage[kOther])->Construct(23, 16, 0, 5, 6, 5);
    }
};
FormatObjects &Formats()
{
    static FormatObjects formats;
    return formats;
}

// ---- the fake formats manager (slots 6 GetARGB, 7 GetAlphaLuminance, 10 GetDXT) ----
struct FakeObject {
    void *vtable;
};
void *gManagerTable[16];
FakeObject gManager{gManagerTable};
void *__fastcall FakeGetARGB(void *, void *, uint32_t total, uint32_t a, uint32_t r, uint32_t g, uint32_t b)
{
    Log(1, total, a, (r << 16) | (g << 8) | b);
    if (total == 32 && a == 0 && r == 8) return Formats().Get(kX8R8G8B8);
    if (total == 32 && a == 8 && r == 8) return Formats().Get(kA8R8G8B8);
    if (total == 32 && a == 2) return Formats().Get(kA2R10G10B10);
    return Formats().Get(kA8);
}
void *__fastcall FakeGetAlphaLuminance(void *, void *, uint32_t total, uint32_t a, uint32_t l)
{
    Log(2, total, a, l);
    return Formats().Get(kL8);
}
void *__fastcall FakeGetDxt(void *, void *, uint32_t version)
{
    Log(3, version);
    return Formats().Get(version == 1 ? kDxt1 : version == 3 ? kDxt3 : kDxt5);
}
uint32_t __fastcall FakeOne(void *, void *) { return 1; }

// ---- the fake image manip (slot 3: CopySurfaceRect) ----
void *gImageTable[8];
FakeObject gImageManip{gImageTable};
struct Rec {
    void *surface = nullptr;
    RECT rect{};
    uint8_t *bits = nullptr;
    int32_t pitch = 0;
    uint32_t rowBytes = 0, rows = 0;
};
bool __fastcall FakeCopySurfaceRect(void *, void *, void *source, const RECT *sourceRect, void *target, const RECT *targetRect,
                                    const uint32_t *colorKey)
{
    using ifaces::cILockableSurface;
    auto *s = static_cast<cILockableSurface *>(source);
    auto *t = static_cast<cILockableSurface *>(target);
    Log(4, uint32_t(sourceRect->right), uint32_t(sourceRect->bottom), colorKey ? *colorKey : 0xfeedf00d,
        (uint32_t(s->GetWidth()) << 16) | s->GetHeight());
    Log(5, uint32_t(targetRect->left), uint32_t(targetRect->top), (t->GetWidth() << 16) | t->GetHeight(),
        uint32_t(targetRect->right) << 16 | uint32_t(targetRect->bottom));
    Rec from, to;
    s->Lock(&from, 1);
    t->Lock(&to, 0);
    const uint32_t rowBytes = std::min(from.rowBytes, to.rowBytes), rows = std::min(from.rows, to.rows);
    for (uint32_t y = 0; y < rows; ++y) std::memcpy(to.bits + int(y) * to.pitch, from.bits + int(y) * from.pitch, rowBytes);
    t->Unlock(&to);
    if (to.surface) static_cast<cIGZUnknown *>(to.surface)->Release();
    s->Unlock(&from);
    if (from.surface) static_cast<cIGZUnknown *>(from.surface)->Release();
    return false;
}

void InstallFakes(difftest::Context &ctx)
{
    std::memset(gManagerTable, 0, sizeof(gManagerTable));
    gManagerTable[1] = reinterpret_cast<void *>(&FakeOne);
    gManagerTable[2] = reinterpret_cast<void *>(&FakeOne);
    gManagerTable[6] = reinterpret_cast<void *>(&FakeGetARGB);
    gManagerTable[7] = reinterpret_cast<void *>(&FakeGetAlphaLuminance);
    gManagerTable[10] = reinterpret_cast<void *>(&FakeGetDxt);
    std::memset(gImageTable, 0, sizeof(gImageTable));
    gImageTable[1] = reinterpret_cast<void *>(&FakeOne);
    gImageTable[2] = reinterpret_cast<void *>(&FakeOne);
    gImageTable[3] = reinterpret_cast<void *>(&FakeCopySurfaceRect);
    *ctx.image.At<void *>(0x10ca830) = &gManager;
    *ctx.image.At<void *>(0x10ca990) = &gImageManip;
}
void RemoveFakes(difftest::Context &ctx)
{
    *ctx.image.At<void *>(0x10ca830) = nullptr;
    *ctx.image.At<void *>(0x10ca990) = nullptr;
}

// ---- memory surfaces and textures (game code) ----
struct SurfaceObject {
    alignas(8) uint8_t bytes[0x1c];
};
using SurfaceCtor = void *(__thiscall *)(void *, void *, void *, uint32_t, uint32_t, uint32_t);
using SurfaceDtor = void(__thiscall *)(void *);
struct TextureObject {
    alignas(8) uint8_t bytes[12];
};
using TextureCtor = void *(__thiscall *)(void *);
using TextureTop = void(__thiscall *)(void *, void *, void *, uint32_t, uint32_t, uint32_t);
using TextureLevel = void(__thiscall *)(void *, void *, uint32_t, uint32_t, uint32_t);

constexpr size_t kArena = 1 << 20;
constexpr size_t kCanary = 64;

// One side's memory: the arena (bump allocated, canary bytes around each block) and the memory textures built over it.
struct World {
    std::vector<uint8_t> arena = std::vector<uint8_t>(kArena, 0);
    size_t used = 0;
    std::mt19937 rng;

    explicit World(uint32_t seed) : rng(seed)
    {
        for (auto &b : arena) b = uint8_t(rng());
    }
    uint8_t *Alloc(size_t bytes)
    {
        used += kCanary;
        uint8_t *p = arena.data() + used;
        used += (bytes + 7) & ~size_t(7);
        used += kCanary;
        return p;
    }
};

struct Dim {
    uint32_t w, h;
};

size_t PitchOf(Fmt f, uint32_t w, uint32_t pad)
{
    const FmtInfo &info = kInfo[f];
    if (info.blockBytes) return size_t((w + 3) / 4) * info.blockBytes + pad;
    return size_t(w) * info.bytes + pad;
}
size_t SizeOf(Fmt f, uint32_t w, uint32_t h, size_t pitch)
{
    const FmtInfo &info = kInfo[f];
    const size_t rows = info.blockBytes ? (h + 3) / 4 : h;
    // Compressed levels are allocated like 4-byte ones (the level reducers read them as such when they are not decoded).
    return std::max(pitch * (rows + 1), size_t(w) * h * 4 + 16);
}

struct MemTexture {
    TextureObject object;
    bool built = false;
    ~MemTexture() = default;
};

struct Built {
    std::unique_ptr<MemTexture> texture;
    std::vector<uint8_t *> bits;
};

void BuildTexture(difftest::Context &ctx, World &world, MemTexture &texture, Fmt fmt, const std::vector<Dim> &levels, uint32_t pad)
{
    auto construct = ctx.Original<TextureCtor>(0x18c300);
    auto top = ctx.Original<TextureTop>(0x18c5b0);
    auto add = ctx.Original<TextureLevel>(0x18c3e0);
    construct(&texture.object);
    texture.built = true;
    for (size_t i = 0; i < levels.size(); ++i) {
        const size_t pitch = PitchOf(fmt, levels[i].w, pad);
        uint8_t *bits = world.Alloc(SizeOf(fmt, levels[i].w, levels[i].h, pitch));
        if (i == 0)
            top(&texture.object, bits, Formats().Get(fmt), levels[i].w, levels[i].h, uint32_t(pitch));
        else
            add(&texture.object, bits, levels[i].w, levels[i].h, uint32_t(pitch));
    }
}
void DestroyTexture(difftest::Context &ctx, MemTexture &texture)
{
    if (texture.built) ctx.Original<SurfaceDtor>(0x18c350)(&texture.object);
    texture.built = false;
}

std::vector<Dim> Chain(Dim top, size_t count)
{
    std::vector<Dim> levels;
    Dim d = top;
    for (size_t i = 0; i < count; ++i) {
        levels.push_back(d);
        d = {(d.w + 1) >> 1, (d.h + 1) >> 1};
    }
    return levels;
}
size_t ChainLength(Dim top)
{
    size_t n = 1;
    Dim d = top;
    while (d.w > 1 || d.h > 1) {
        d = {(d.w + 1) >> 1, (d.h + 1) >> 1};
        ++n;
    }
    return n;
}

using ifaces::cILockableSurface;
using ifaces::cITexture;

struct Outcome {
    int result = 0;
    std::vector<uint8_t> arena;
    std::vector<Entry> log;
};

// Runs `body(world, ours)` on a fresh world for each side and compares the answer, the arena and the log.
template <typename Body> void Both(difftest::Context &ctx, uint32_t seed, const char *what, Body body)
{
    Outcome out[2];
    for (int side = 0; side < 2; ++side) {
        World world(seed);
        gLog.clear();
        out[side].result = body(world, side == 1);
        out[side].arena = world.arena;
        out[side].log = gLog;
    }
    ++ctx.cases;
    if (out[0].result != out[1].result)
        ctx.Fail("%s seed %u: result original %d, ours %d", what, seed, out[0].result, out[1].result);
    else if (out[0].arena != out[1].arena) {
        size_t at = 0;
        while (out[0].arena[at] == out[1].arena[at]) ++at;
        ctx.Fail("%s seed %u: arena differs at %zu", what, seed, at);
    } else if (!(out[0].log == out[1].log))
        ctx.Fail("%s seed %u: %s", what, seed, LogDiff(out[0].log, out[1].log).c_str());
}

Fmt PickFormat(std::mt19937 &rng) { return Fmt(rng() % kFmtCount); }
uint32_t PowerOfTwo(std::mt19937 &rng, int maxLog) { return 1u << (rng() % (maxLog + 1)); }

void *Level(MemTexture &texture, uint32_t level)
{
    return static_cast<cITexture *>(static_cast<void *>(&texture.object))->GetLockableSurface(level);
}

// ---- the level routines ----
using LevelFn = void(__cdecl *)(void *, void *);

void LevelTest(difftest::Context &ctx, uint32_t rva, LevelFn ours, Fmt sourceFmt, Fmt targetFmt, bool ceilTarget, uint32_t seed)
{
    LevelFn original = ctx.Original<LevelFn>(rva);
    InstallFakes(ctx);
    std::mt19937 rng(seed);
    for (int i = 0; i < 150; ++i) {
        const uint32_t w = 1 + rng() % 24, h = 1 + rng() % 24;
        const uint32_t tw = ceilTarget ? (w + 3) & ~3u : w, th = ceilTarget ? (h + 3) & ~3u : h;
        const uint32_t sourcePad = rng() % 3 * 4, targetPad = rng() % 3 * 4;
        Both(ctx, seed * 1000 + i, "level", [&](World &world, bool useOurs) {
            MemTexture source, target;
            BuildTexture(ctx, world, source, sourceFmt, {{w, h}}, sourcePad);
            BuildTexture(ctx, world, target, targetFmt, {{tw, th}}, targetPad);
            (useOurs ? ours : original)(Level(target, 0), Level(source, 0));
            DestroyTexture(ctx, target);
            DestroyTexture(ctx, source);
            return 0;
        });
    }
    RemoveFakes(ctx);
}

} // namespace

DIFF_TEST(MemChain_CopyLevel, 0x184290)
{
    LevelTest(ctx, 0x184290, &memchain::CopyLevel, kA8R8G8B8, kA8R8G8B8, false, 11);
    LevelTest(ctx, 0x184290, &memchain::CopyLevel, kL8, kL8, false, 12);
    LevelTest(ctx, 0x184290, &memchain::CopyLevel, kA2R10G10B10, kA2R10G10B10, false, 13);
}
DIFF_TEST(MemChain_ConvertRgbToArgb, 0x1844b0)
{
    LevelTest(ctx, 0x1844b0, &memchain::ConvertRgbToArgb, kR8G8B8, kX8R8G8B8, false, 21);
    LevelTest(ctx, 0x1844b0, &memchain::ConvertRgbToArgb, kR8G8B8, kA8R8G8B8, false, 22);
}
DIFF_TEST(MemChain_DecodeDxt1Level, 0x185b90) { LevelTest(ctx, 0x185b90, &memchain::DecodeDxt1Level, kDxt1, kA8R8G8B8, true, 31); }
DIFF_TEST(MemChain_DecodeDxt3Level, 0x185da0) { LevelTest(ctx, 0x185da0, &memchain::DecodeDxt3Level, kDxt3, kA8R8G8B8, true, 32); }
DIFF_TEST(MemChain_DecodeDxt5Level, 0x185fb0) { LevelTest(ctx, 0x185fb0, &memchain::DecodeDxt5Level, kDxt5, kA8R8G8B8, true, 33); }

// ---- the converter chooser: every pair of formats, both flags; the answer is named by which routine it is ----
namespace {
void *gFormatOf[2];
void *__fastcall PickTarget(void *, void *) { return gFormatOf[0]; }
void *__fastcall PickSource(void *, void *) { return gFormatOf[1]; }
} // namespace

DIFF_TEST(MemChain_ChooseConverter, 0x185a30)
{
    using Choose = void *(__cdecl *)(void *, void *, bool);
    Choose original = ctx.Original<Choose>(0x185a30);
    // Two fake textures (slot 19 answers their format).
    static void *tableTarget[32], *tableSource[32];
    static FakeObject target{tableTarget}, source{tableSource};
    tableTarget[19] = reinterpret_cast<void *>(&PickTarget);
    tableSource[19] = reinterpret_cast<void *>(&PickSource);
    auto classify = [&](void *fn) -> int {
        if (!fn) return 0;
        if (fn == ctx.image.At(0x184290) || fn == reinterpret_cast<void *>(&memchain::CopyLevel)) return 1;
        if (fn == ctx.image.At(0x1844b0) || fn == reinterpret_cast<void *>(&memchain::ConvertRgbToArgb)) return 2;
        return -1;
    };
    for (int t = 0; t < kFmtCount; ++t)
        for (int s = 0; s < kFmtCount; ++s)
            for (int allow = 0; allow < 2; ++allow) {
                ++ctx.cases;
                gFormatOf[0] = Formats().Get(Fmt(t));
                gFormatOf[1] = Formats().Get(Fmt(s));
                uint8_t before[sizeof(FormatObjects::storage)];
                std::memcpy(before, Formats().storage, sizeof(before));
                const int a = classify(original(&target, &source, allow != 0));
                uint8_t afterOriginal[sizeof(before)];
                std::memcpy(afterOriginal, Formats().storage, sizeof(before));
                std::memcpy(Formats().storage, before, sizeof(before));
                const int b = classify(reinterpret_cast<void *>(memchain::ChooseConverter(&target, &source, allow != 0)));
                if (a != b || std::memcmp(afterOriginal, Formats().storage, sizeof(before)) != 0)
                    ctx.Fail("target %d source %d allow %d: original %d, ours %d (or the formats' counts differ)", t, s, allow, a, b);
            }
}

// ---- the chooser again, on fake formats that log every call (ids asked, answers released, channel sizes asked in order) ----
namespace {
struct FakeFormat {
    void *vtable;
    uint32_t index, id, bits, alpha, red, green, blue;
    bool argb;
};
void *gFakeFormatTable[16];
bool __fastcall FakeQuery(FakeFormat *self, void *, uint32_t iid, void **out)
{
    Log(20, self->index, iid, self->argb);
    *out = self->argb ? self : nullptr;
    return self->argb;
}
uint32_t __fastcall FakeAddRef(FakeFormat *self, void *)
{
    Log(21, self->index);
    return 1;
}
uint32_t __fastcall FakeRelease(FakeFormat *self, void *)
{
    Log(22, self->index);
    return 1;
}
uint32_t __fastcall FakeFormatId(FakeFormat *self, void *) { Log(23, self->index); return self->id; }
uint32_t __fastcall FakeBits(FakeFormat *self, void *) { Log(24, self->index); return self->bits; }
uint32_t __fastcall FakeAlpha(FakeFormat *self, void *) { Log(25, self->index); return self->alpha; }
uint32_t __fastcall FakeRed(FakeFormat *self, void *) { Log(26, self->index); return self->red; }
uint32_t __fastcall FakeGreen(FakeFormat *self, void *) { Log(27, self->index); return self->green; }
uint32_t __fastcall FakeBlue(FakeFormat *self, void *) { Log(28, self->index); return self->blue; }
FakeFormat gFakeFormats[2];
void *gFakeTextureFormats[2];
void *__fastcall FakeTextureFormat0(void *, void *) { return gFakeTextureFormats[0]; }
void *__fastcall FakeTextureFormat1(void *, void *) { return gFakeTextureFormats[1]; }
} // namespace

DIFF_TEST(MemChain_ChooseConverterFakes, 0x185a30)
{
    using Choose = void *(__cdecl *)(void *, void *, bool);
    Choose original = ctx.Original<Choose>(0x185a30);
    gFakeFormatTable[0] = reinterpret_cast<void *>(&FakeQuery);
    gFakeFormatTable[1] = reinterpret_cast<void *>(&FakeAddRef);
    gFakeFormatTable[2] = reinterpret_cast<void *>(&FakeRelease);
    gFakeFormatTable[3] = reinterpret_cast<void *>(&FakeFormatId);
    gFakeFormatTable[6] = reinterpret_cast<void *>(&FakeBits);
    gFakeFormatTable[11] = reinterpret_cast<void *>(&FakeAlpha);
    gFakeFormatTable[12] = reinterpret_cast<void *>(&FakeRed);
    gFakeFormatTable[13] = reinterpret_cast<void *>(&FakeGreen);
    gFakeFormatTable[14] = reinterpret_cast<void *>(&FakeBlue);
    static void *tableTarget[32], *tableSource[32];
    static FakeObject target{tableTarget}, source{tableSource};
    tableTarget[19] = reinterpret_cast<void *>(&FakeTextureFormat0);
    tableSource[19] = reinterpret_cast<void *>(&FakeTextureFormat1);
    auto classify = [&](void *fn) -> int {
        if (!fn) return 0;
        if (fn == ctx.image.At(0x184290) || fn == reinterpret_cast<void *>(&memchain::CopyLevel)) return 1;
        if (fn == ctx.image.At(0x1844b0) || fn == reinterpret_cast<void *>(&memchain::ConvertRgbToArgb)) return 2;
        return -1;
    };
    std::mt19937 rng(81);
    const uint32_t sizes[] = {0, 8, 5, 6};
    int answers[3] = {0, 0, 0};
    for (int i = 0; i < 3000; ++i) {
        for (int k = 0; k < 2; ++k) {
            FakeFormat &f = gFakeFormats[k];
            f.vtable = gFakeFormatTable;
            f.index = k;
            f.id = rng() % 5 == 0 ? 7 : uint32_t(rng() % 4);
            f.argb = rng() % 8 != 0;
            // The channel sizes of a plain 24-bit / 32-bit 8/8/8 format most of the time, so the accepting path runs.
            const bool plain = rng() % 3 != 0;
            f.red = plain ? 8 : sizes[rng() % 4];
            f.green = plain ? 8 : sizes[rng() % 4];
            f.blue = plain ? 8 : sizes[rng() % 4];
            f.alpha = plain ? 0 : sizes[rng() % 4];
            f.bits = plain ? (k == 1 ? 0x18 : 0x20) : (rng() % 2 ? 0x18 : 0x20);
            gFakeTextureFormats[k] = &f;
        }
        const bool allow = rng() % 4 != 0;
        gLog.clear();
        const int a = classify(original(&target, &source, allow));
        const std::vector<Entry> logA = gLog;
        gLog.clear();
        const int b = classify(reinterpret_cast<void *>(memchain::ChooseConverter(&target, &source, allow)));
        ++ctx.cases;
        ++answers[a < 0 ? 0 : a];
        if (a != b || !(logA == gLog)) {
            ctx.Fail("case %d allow %d: original %d, ours %d; %s", i, allow, a, b, LogDiff(logA, gLog).c_str());
            return;
        }
    }
    if (answers[1] == 0 || answers[2] == 0 || answers[0] == 0)
        ctx.Fail("the sweep never produced all three answers (copy %d, convert %d, none %d)", answers[1], answers[2], answers[0]);
}

// ---- the normal map entry point on fake textures and surfaces that log every call (the order of the lookups and locks) ----
namespace {
struct FakeSurface {
    void *vtable;
    uint32_t index, width, height;
    uint8_t *bits;
    int32_t pitch;
    uint32_t rowBytes;
};
struct FakeTexture {
    void *vtable;
    uint32_t index; // 0 source, 1 target
    Fmt fmt;
    std::vector<FakeSurface> levels;
};
void *gFakeSurfaceTable[32];
void *gFakeTextureTable[32];
uint32_t __fastcall SurfRef(FakeSurface *self, void *)
{
    Log(40, self->index);
    return 1;
}
uint32_t __fastcall SurfWidth(FakeSurface *self, void *) { Log(41, self->index); return self->width; }
uint32_t __fastcall SurfHeight(FakeSurface *self, void *) { Log(42, self->index); return self->height; }
bool __fastcall SurfLock(FakeSurface *self, void *, Rec *record, uint32_t readOnly)
{
    Log(43, self->index, readOnly);
    record->surface = self;
    record->rect = {0, 0, LONG(self->width), LONG(self->height)};
    record->bits = self->bits;
    record->pitch = self->pitch;
    record->rowBytes = self->rowBytes;
    record->rows = self->height;
    return true;
}
void __fastcall SurfUnlock(FakeSurface *self, void *, Rec *record)
{
    Log(44, self->index);
    record->surface = nullptr; // the unlock lets the surface go
}
uint32_t __fastcall TexLevels(FakeTexture *self, void *) { Log(50, self->index); return uint32_t(self->levels.size()); }
uint32_t __fastcall TexWidth(FakeTexture *self, void *, uint32_t level) { Log(51, self->index, level); return self->levels[level].width; }
uint32_t __fastcall TexHeight(FakeTexture *self, void *, uint32_t level) { Log(52, self->index, level); return self->levels[level].height; }
void *__fastcall TexFormat(FakeTexture *self, void *) { Log(53, self->index); return Formats().Get(self->fmt); }
void *__fastcall TexSurface(FakeTexture *self, void *, uint32_t level) { Log(54, self->index, level); return &self->levels[level]; }
} // namespace

DIFF_TEST(MemChain_ComputeMemoryNormalMapFakes, 0x183500)
{
    using Fn = bool(__cdecl *)(void *, void *, float);
    Fn original = ctx.Original<Fn>(0x183500);
    InstallFakes(ctx);
    gFakeSurfaceTable[1] = gFakeSurfaceTable[2] = reinterpret_cast<void *>(&SurfRef);
    gFakeSurfaceTable[20] = reinterpret_cast<void *>(&SurfWidth);
    gFakeSurfaceTable[21] = reinterpret_cast<void *>(&SurfHeight);
    gFakeSurfaceTable[26] = reinterpret_cast<void *>(&SurfLock);
    gFakeSurfaceTable[27] = reinterpret_cast<void *>(&SurfUnlock);
    gFakeTextureTable[16] = reinterpret_cast<void *>(&TexLevels);
    gFakeTextureTable[17] = reinterpret_cast<void *>(&TexWidth);
    gFakeTextureTable[18] = reinterpret_cast<void *>(&TexHeight);
    gFakeTextureTable[19] = reinterpret_cast<void *>(&TexFormat);
    gFakeTextureTable[27] = reinterpret_cast<void *>(&TexSurface);
    gFakeTextureTable[28] = reinterpret_cast<void *>(&TexSurface);
    std::mt19937 rng(91);
    for (int i = 0; i < 200; ++i) {
        const Dim top{PowerOfTwo(rng, 4), PowerOfTwo(rng, 4)};
        const size_t count = 1 + rng() % ChainLength(top);
        const std::vector<Dim> chain = Chain(top, count);
        std::vector<uint8_t> memory[2][2]; // [side][texture] raw level storage
        std::vector<uint8_t> dumps[2];
        std::vector<Entry> logs[2];
        bool results[2];
        for (int side = 0; side < 2; ++side) {
            FakeTexture textures[2];
            std::vector<uint8_t> storage;
            storage.resize(count * 2 * 17 * 17 * 4 + 64);
            std::mt19937 fill(1000 + i);
            for (auto &b : storage) b = uint8_t(fill());
            size_t at = 0;
            for (int t = 0; t < 2; ++t) {
                textures[t].vtable = gFakeTextureTable;
                textures[t].index = t;
                textures[t].fmt = t == 0 ? kL8 : kA8R8G8B8;
                for (size_t l = 0; l < count; ++l) {
                    FakeSurface s{};
                    s.vtable = gFakeSurfaceTable;
                    s.index = uint32_t(t * 10 + l);
                    s.width = chain[l].w;
                    s.height = chain[l].h;
                    s.bits = storage.data() + at;
                    s.pitch = int(chain[l].w * (t == 0 ? 1 : 4));
                    s.rowBytes = uint32_t(s.pitch);
                    at += size_t(s.pitch) * s.height + 8;
                    textures[t].levels.push_back(s);
                }
            }
            gLog.clear();
            results[side] = (side == 1 ? &memchain::ComputeMemoryNormalMap : original)(&textures[0], &textures[1], 3.0f);
            logs[side] = gLog;
            dumps[side] = storage;
        }
        ++ctx.cases;
        if (results[0] != results[1] || dumps[0] != dumps[1] || !(logs[0] == logs[1])) {
            ctx.Fail("case %d: results %d/%d, memory %s, %s", i, results[0], results[1], dumps[0] == dumps[1] ? "same" : "differs",
                     LogDiff(logs[0], logs[1]).c_str());
            break;
        }
    }
    RemoveFakes(ctx);
}

// ---- the level reducers ----
namespace {

using Reduce2 = void(__cdecl *)(void *, int, int, void *, int, int, uint8_t *, int, memchain::RowKernel, memchain::RowKernel);
using Reduce4 = void(__cdecl *)(void *, int, int, void *, int, uint32_t, uint8_t *, int, memchain::RowKernel, memchain::RowKernel);

template <typename Fn> void ReduceTest(difftest::Context &ctx, uint32_t rva, Fn ours, bool fourTap, uint32_t seed)
{
    Fn original = ctx.Original<Fn>(rva);
    InstallFakes(ctx);
    std::mt19937 rng(seed);
    for (int i = 0; i < 400; ++i) {
        const bool bytePixels = (i & 1) != 0;
        const Fmt fmt = bytePixels ? kL8 : kA8R8G8B8;
        const int pixel = bytePixels ? 1 : 4;
        const uint32_t w = PowerOfTwo(rng, 5), h = PowerOfTwo(rng, 5);
        const uint32_t ow = (w + 1) >> 1, oh = (h + 1) >> 1;
        const uint32_t sourcePad = rng() % 3 * 4, targetPad = rng() % 3 * 4;
        auto kernel = [](auto fn) { return reinterpret_cast<memchain::RowKernel>(fn); };
        memchain::RowKernel reducer, combiner;
        if (bytePixels) {
            reducer = fourTap ? kernel(&mip::FilterRow8) : kernel(&mip::HalveRow8);
            combiner = fourTap ? kernel(&mip::CombineFourRows8) : kernel(&mip::CombineTwoRows8);
        } else {
            reducer = fourTap ? kernel(&mip::FilterRow8888) : kernel(&mip::HalveRow8888);
            combiner = fourTap ? kernel(&mip::CombineFourRows8888) : kernel(&mip::CombineTwoRows8888);
        }
        Both(ctx, seed * 1000 + i, fourTap ? "reduce4" : "reduce2", [&](World &world, bool useOurs) {
            MemTexture source, target;
            BuildTexture(ctx, world, source, fmt, {{w, h}}, sourcePad);
            BuildTexture(ctx, world, target, fmt, {{ow, oh}}, targetPad);
            uint8_t *scratch = world.Alloc(size_t(ow) * pixel * 4 + 64);
            Fn run = useOurs ? ours : original;
            if constexpr (std::is_same_v<Fn, Reduce2>)
                run(Level(target, 0), int(ow), int(oh), Level(source, 0), int(w), int(h), scratch, pixel, reducer, combiner);
            else
                run(Level(target, 0), int(ow), int(oh), Level(source, 0), int(w), h, scratch, pixel, reducer, combiner);
            DestroyTexture(ctx, target);
            DestroyTexture(ctx, source);
            return 0;
        });
    }
    RemoveFakes(ctx);
}

} // namespace

DIFF_TEST(MemChain_ReduceLevelTwoTap, 0x1854e0) { ReduceTest<Reduce2>(ctx, 0x1854e0, &memchain::ReduceLevelTwoTap, false, 41); }
DIFF_TEST(MemChain_ReduceLevelFourTap, 0x185770) { ReduceTest<Reduce4>(ctx, 0x185770, &memchain::ReduceLevelFourTap, true, 42); }

// ---- the normal map kernel ----
DIFF_TEST(MemChain_ComputeNormalMapLevel, 0x184ec0)
{
    using Fn = void(__cdecl *)(uint8_t *, int, const uint8_t *, int, int, int, float);
    Fn original = ctx.Original<Fn>(0x184ec0);
    const float amplitudes[] = {1.0f, 2.0f, 0.5f, 100.0f, 0.001f, 0.0f, -3.0f, 255.0f, 1e6f, 1e-20f, 37.25f, -0.0f};
    std::mt19937 rng(51);
    for (int i = 0; i < 600; ++i) {
        const int w = 1 + rng() % 12, h = 1 + rng() % 12;
        const int sp = w + int(rng() % 3), tp = w * 4 + int(rng() % 3) * 4;
        std::vector<uint8_t> source(size_t(sp) * h + 8);
        for (auto &b : source) {
            const uint32_t r = rng();
            b = i % 5 == 0 ? uint8_t((r & 1) ? 255 : 0) : uint8_t(r);
        }
        const float amplitude = amplitudes[i % (sizeof(amplitudes) / sizeof(amplitudes[0]))];
        std::vector<uint8_t> a(size_t(tp) * h + 2 * kCanary, 0xa5), b = a;
        original(a.data() + kCanary, tp, source.data(), sp, w, h, amplitude);
        memchain::ComputeNormalMapLevel(b.data() + kCanary, tp, source.data(), sp, w, h, amplitude);
        ++ctx.cases;
        if (a != b) {
            size_t at = 0;
            while (a[at] == b[at]) ++at;
            ctx.Fail("case %d (%dx%d amplitude %g): byte %zu differs (original %02x, ours %02x)", i, w, h, double(amplitude), at,
                     a[at], b[at]);
            return;
        }
    }
}

// ---- the two entry points ----
namespace {

struct Scenario {
    Fmt sourceFmt, targetFmt;
    std::vector<Dim> sourceLevels, targetLevels;
    uint32_t sourcePad, targetPad;
    bool filtered;
};

std::string Describe(const Scenario &s)
{
    std::string text = "src fmt " + std::to_string(s.sourceFmt) + " [";
    for (const Dim &d : s.sourceLevels) text += std::to_string(d.w) + "x" + std::to_string(d.h) + " ";
    text += "] dst fmt " + std::to_string(s.targetFmt) + " [";
    for (const Dim &d : s.targetLevels) text += std::to_string(d.w) + "x" + std::to_string(d.h) + " ";
    text += s.filtered ? "] filtered" : "] two-tap";
    return text;
}

Scenario MakeCopyScenario(std::mt19937 &rng)
{
    Scenario s{};
    const uint32_t kind = rng() % 10;
    s.sourceFmt = PickFormat(rng);
    s.targetFmt = (rng() % 3 == 0) ? PickFormat(rng) : s.sourceFmt;
    if (s.sourceFmt == kR8G8B8 && rng() % 2) s.targetFmt = rng() % 2 ? kX8R8G8B8 : kA8R8G8B8;
    s.sourcePad = rng() % 3 * 4;
    s.targetPad = rng() % 3 * 4;
    s.filtered = rng() % 2;
    Dim top{PowerOfTwo(rng, 5), PowerOfTwo(rng, 5)};
    if (kind == 0) top = {1 + uint32_t(rng() % 20), 1 + uint32_t(rng() % 20)}; // maybe not a power of two
    const std::vector<Dim> all = Chain(top, ChainLength(top));
    const size_t sourceCount = 1 + rng() % all.size();
    s.sourceLevels.assign(all.begin(), all.begin() + sourceCount);
    Dim targetTop;
    if (kind <= 5) {
        // The target's top is one of the source's levels.
        targetTop = s.sourceLevels[rng() % sourceCount];
    } else {
        // Smaller than the source's last level by 0 to 3 halvings (or an unrelated size).
        targetTop = s.sourceLevels.back();
        const uint32_t halvings = rng() % 4;
        for (uint32_t i = 0; i < halvings; ++i) targetTop = {(targetTop.w + 1) >> 1, (targetTop.h + 1) >> 1};
        if (kind == 9) targetTop = {PowerOfTwo(rng, 5), PowerOfTwo(rng, 5)};
    }
    s.targetLevels = Chain(targetTop, 1 + rng() % ChainLength(targetTop));
    return s;
}

} // namespace

DIFF_TEST(MemChain_CopyMemoryTextures, 0x184710)
{
    using Fn = bool(__cdecl *)(void *, void *, bool);
    Fn original = ctx.Original<Fn>(0x184710);
    InstallFakes(ctx);
    std::mt19937 rng(61);
    for (int i = 0; i < 4000 && ctx.failures.empty(); ++i) {
        const Scenario s = MakeCopyScenario(rng);
        Both(ctx, 700000 + i, Describe(s).c_str(), [&](World &world, bool useOurs) {
            MemTexture source, target;
            BuildTexture(ctx, world, source, s.sourceFmt, s.sourceLevels, s.sourcePad);
            BuildTexture(ctx, world, target, s.targetFmt, s.targetLevels, s.targetPad);
            Fn run = useOurs ? &memchain::CopyMemoryTextures : original;
            const bool ok = run(&source.object, &target.object, s.filtered);
            DestroyTexture(ctx, target);
            DestroyTexture(ctx, source);
            return ok ? 1 : 0;
        });
    }
    // A texture copied onto itself.
    for (int i = 0; i < 300 && ctx.failures.empty(); ++i) {
        Scenario s = MakeCopyScenario(rng);
        s.targetFmt = s.sourceFmt;
        Both(ctx, 800000 + i, "copy onto itself", [&](World &world, bool useOurs) {
            MemTexture texture;
            BuildTexture(ctx, world, texture, s.sourceFmt, s.sourceLevels, s.sourcePad);
            Fn run = useOurs ? &memchain::CopyMemoryTextures : original;
            const bool ok = run(&texture.object, &texture.object, s.filtered);
            DestroyTexture(ctx, texture);
            return ok ? 1 : 0;
        });
    }
    RemoveFakes(ctx);
}

DIFF_TEST(MemChain_ComputeMemoryNormalMap, 0x183500)
{
    using Fn = bool(__cdecl *)(void *, void *, float);
    Fn original = ctx.Original<Fn>(0x183500);
    InstallFakes(ctx);
    const float amplitudes[] = {1.0f, 2.0f, 50.0f, 0.0f, -4.0f, 0.25f, 1e6f};
    std::mt19937 rng(71);
    for (int i = 0; i < 1500 && ctx.failures.empty(); ++i) {
        Scenario s{};
        s.sourceFmt = rng() % 6 ? kL8 : PickFormat(rng);
        s.targetFmt = rng() % 6 ? (rng() % 2 ? kX8R8G8B8 : kA8R8G8B8) : PickFormat(rng);
        const Dim top{PowerOfTwo(rng, 5), PowerOfTwo(rng, 5)};
        const size_t count = 1 + rng() % ChainLength(top);
        s.sourceLevels = Chain(top, count);
        s.targetLevels = s.sourceLevels;
        const uint32_t mismatch = rng() % 8;
        if (mismatch == 0) s.targetLevels = Chain({top.w * 2, top.h}, count);
        if (mismatch == 1) s.targetLevels = Chain(top, std::max<size_t>(1, count - 1));
        if (mismatch == 2) s.targetLevels = Chain({top.w, top.h * 2}, count);
        s.sourcePad = rng() % 3 * 4;
        s.targetPad = rng() % 3 * 4;
        const float amplitude = amplitudes[i % (sizeof(amplitudes) / sizeof(amplitudes[0]))];
        Both(ctx, 900000 + i, "normal map", [&](World &world, bool useOurs) {
            MemTexture source, target;
            BuildTexture(ctx, world, source, s.sourceFmt, s.sourceLevels, s.sourcePad);
            BuildTexture(ctx, world, target, s.targetFmt, s.targetLevels, s.targetPad);
            Fn run = useOurs ? &memchain::ComputeMemoryNormalMap : original;
            const bool ok = run(&source.object, &target.object, amplitude);
            DestroyTexture(ctx, target);
            DestroyTexture(ctx, source);
            return ok ? 1 : 0;
        });
    }
    RemoveFakes(ctx);
}
