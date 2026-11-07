// Differential tests of the surface lock buffer (src/gz/cSurfaceLockAsGZBuffer.*; lane VV, milestone O).
//
//   SurfaceLockBuffer_Life  a buffer's life on each side: the original's constructor 0x18a2a0 (game vtable 0xda0ca8)
//       against ours (the constructor hook's entry), both on operator new memory filled with 0xc5; random calls through
//       the object's own vtable, every slot: QueryInterface 0x18a8c0, AddRef shared_vf_148fb0, Release 0x18a8f0, the
//       constant slots (shared bodies), IsLocked / IsReady 0x17da80, the size 0x18a990 / 0x18a7b0, the bounds
//       shared_vf_18a670 / 0x18a650, the colour types 0x18a6f0 / 0x18a6c0 (into 0x18a440), the bit and byte counts
//       0x18a630 / 0x18a680, the pixels 0x18a720 / 0x18a910, the conversions 0x18a5d0 / 0x18a530, the bits 0x147d00
//       and the pitch 0x15e550; then the destructor 0x18a330 (ours: the hook's entry) or the last Release into the
//       deleting destructor 0x18a420.
//   SurfaceLockBuffer_Adapters  the destructor hook's entry on a buffer the original constructor built (the game's
//       vtable copied with every slot a trap): the adapter calls our destructor directly, never a slot.
//
// Fakes: the surface (Lock writes a random lock record over a pixel area, or nothing; the surface counted in it or not;
// Unlock lets it go itself one time in three), its pixel format (slot 6 the bits, QueryInterface answering the ARGB face
// or not) and the ARGB face (bits, channel widths, unused bits, PackColor / UnpackColor noting the colour bytes).
// Compared: the call log, every result and out-parameter, the object's words after each step (the vtable word
// excepted), the pixels, the surface's count.
#include "sgfx.h"

#include "gz/PixelFormat.h"
#include "gz/cSurfaceLockAsGZBuffer.h"
#include "hook/hooks.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <random>

namespace {

using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;
using nGZGraphic4::cSurfaceLock;
using nRZGraphic4::cSurfaceLockAsGZBuffer;

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

constexpr At kArgbAt[] = {{6, {0, Ret::Int, true, "GetBitsPerPixel", ""}},  {11, {0, Ret::Int, true, "GetAlphaBits", ""}},
                          {12, {0, Ret::Int, true, "GetRedBits", ""}},      {13, {0, Ret::Int, true, "GetGreenBits", ""}},
                          {14, {0, Ret::Int, true, "GetBlueBits", ""}},     {15, {0, Ret::Int, true, "GetUnusedBits", ""}},
                          {16, {1, Ret::Int, true, "PackColor", "a"}},      {17, {2, Ret::Int, true, "UnpackColor", "au"}}};
constexpr auto kArgbSlots = Table<18>(kArgbAt);
constexpr drawfx::Iface kArgb{"PixelFormatARGB", 18, kArgbSlots.data(), false, drawfx::kTestTable};

constexpr unsigned kPixels = 4096;

struct World {
    Fake surface, format, argb;
    alignas(16) uint8_t pixels[kPixels];
    uint32_t lcg = 1;
    int32_t formatBits = 32;
    uint32_t Next()
    {
        lcg = lcg * 1664525u + 1013904223u;
        return lcg >> 8;
    }
};
World gw;

uint32_t Pick(std::mt19937 &g, std::initializer_list<uint32_t> v) { return v.begin()[g() % v.size()]; }

// Lock(record, readOnly): one time in six nothing written (0); else a record over the pixels (the surface in it,
// counted, three times in four), the pitch returned.
uint32_t Lock(Fake &self, const uint32_t *a)
{
    auto *const lock = reinterpret_cast<cSurfaceLock *>(uintptr_t(a[0]));
    const uint32_t r = gw.Next();
    if (r % 6 == 0) return 0;
    const bool held = (r >> 3) % 4 != 0;
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

uint32_t UnpackColor(Fake &, const uint32_t *a)
{
    auto *const bgra = reinterpret_cast<uint8_t *>(uintptr_t(a[0]));
    const uint32_t v = a[1] * 2654435761u;
    for (int i = 0; i < 4; ++i) bgra[i] = uint8_t(v >> (8 * i));
    return a[0];
}

void Build(uint32_t seed, drawfx::Cells &)
{
    std::mt19937 g(seed * 31u + 5u);
    gw.lcg = seed * 2654435761u + 7;
    gw.surface.Reset<kSurface>(540);
    gw.format.Reset<kFormat>(541);
    gw.argb.Reset<kArgb>(542);
    gw.surface.On(26, &Lock);
    gw.surface.On(27, &Unlock);
    gw.surface.Returns(22, &gw.format);
    gw.formatBits = int32_t(Pick(g, {8, 16, 24, 32, 32, 16, 0, 12, 0xfffffff9u, 0xfffffff8u, 0xffffffe1u, 40}));
    gw.format.Returns(6, uint32_t(gw.formatBits));
    if (g() % 6 != 0) gw.format.AddFace(nGZGraphic4::kIidARGB, gw.argb);
    gw.argb.Returns(6, Pick(g, {8, 16, 32, 32, 16, 24, 0}));
    gw.argb.Returns(11, Pick(g, {0, 1, 4, 8, 8, 2}));
    gw.argb.Returns(12, Pick(g, {5, 8, 4}));
    gw.argb.Returns(13, Pick(g, {5, 6, 6, 5, 4, 8}));
    gw.argb.Returns(14, Pick(g, {5, 8, 4}));
    gw.argb.Returns(15, Pick(g, {0, 8, 8, 1}));
    gw.argb.On(16, &PackColor);
    gw.argb.On(17, &UnpackColor);
    for (unsigned i = 0; i < kPixels; ++i) gw.pixels[i] = uint8_t(i * 7 + seed);
    drawfx::AddRange(gw.pixels, kPixels, 0x51000000);
}

// The replacement a hook table registers for `rva` (the hook's entry, what the game calls in its place).
template <class Fn> Fn HookEntry(uint32_t rva)
{
    for (const HookSpec *h : RegisteredHooks())
        if (h->rva == rva) return reinterpret_cast<Fn>(h->replacement);
    return nullptr;
}

template <class R, class... A> R CallSlot(void *object, unsigned slot, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*static_cast<void ***>(object))[slot])(object, args...);
}

void NotePixels()
{
    difftest::NotCounted bookkeeping;
    uint32_t hash = 2166136261u;
    for (uint8_t b : gw.pixels) hash = (hash ^ b) * 16777619u;
    drawfx::Note("pixels", hash);
}

// The object's words after the vtable: the count, the lock's surface, rectangle, bits, pitch, row bytes and rows.
void NoteObject(const void *object)
{
    difftest::NotCounted bookkeeping;
    const auto *w = static_cast<const uint32_t *>(object);
    drawfx::Note("object", {w[1], drawfx::Ptr(w[2]), w[3], w[4], w[5], w[6], drawfx::Ptr(w[7]), w[8], w[9], w[10]});
    drawfx::Note("surface refs", uint32_t(gw.surface.refs));
}

bool Locked(const void *object) { return static_cast<const uint32_t *>(object)[7] != 0; }

// The bytes per pixel the buffer works with (slot 17), as the test expects it: signed bits / 8.
int32_t BytesPerPixel() { return gw.formatBits / 8; }

void Step(void *b, std::mt19937 &rng)
{
    const unsigned op = rng() % 11;
    drawfx::Note("op", op);
    switch (op) {
    case 0: {
        const uint32_t iid = Pick(rng, {1, cIGZBuffer::kIid, 0, nGZGraphic4::kIidARGB, 0x20732181});
        void *out = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
        const bool useOut = rng() % 4 != 0;
        const bool got = CallSlot<bool>(b, 0, iid, useOut ? &out : nullptr);
        drawfx::Note("query", {iid, uint32_t(got), uint32_t(out == b), uint32_t(uintptr_t(out) == 0x5a5a5a5a)});
        if (got) drawfx::Note("query release", CallSlot<uint32_t>(b, 2));
        break;
    }
    case 1: {
        const unsigned n = 1 + rng() % 3;
        for (unsigned i = 0; i < n; ++i) drawfx::Note("addref", CallSlot<uint32_t>(b, 1));
        for (unsigned i = 0; i < n; ++i) drawfx::Note("release", CallSlot<uint32_t>(b, 2));
        break;
    }
    case 2: { // the slots answering a constant (the low byte for the bool ones)
        const uint32_t x = rng(), y = rng();
        cGZRect rect{1, 2, 3, 4};
        uint32_t word = 0x77;
        uint8_t c[4] = {1, 2, 3, 4};
        drawfx::Note("init", uint8_t(CallSlot<uint32_t>(b, 3, x, y, x, y)));
        drawfx::Note("shutdown", uint8_t(CallSlot<uint32_t>(b, 4)));
        drawfx::Note("uninit", uint8_t(CallSlot<uint32_t>(b, 5)));
        drawfx::Note("lock", uint8_t(CallSlot<uint32_t>(b, 6, x)));
        drawfx::Note("unlock", uint8_t(CallSlot<uint32_t>(b, 7, y)));
        drawfx::Note("memory", CallSlot<uint32_t>(b, 13));
        drawfx::Note("fill", uint8_t(CallSlot<uint32_t>(b, 18, x, &rect)));
        drawfx::Note("fill coords", uint8_t(CallSlot<uint32_t>(b, 19, x, 1, 2, 3, 4, &rect)));
        drawfx::Note("fill rect", uint8_t(CallSlot<uint32_t>(b, 20, x, &rect, &rect)));
        CallSlot<void>(b, 23, x);
        drawfx::Note("transparency", uint8_t(CallSlot<uint32_t>(b, 24)));
        drawfx::Note("transparent", uint8_t(CallSlot<uint32_t>(b, 25, x, y)));
        drawfx::Note("transparent colour", {uint32_t(uint8_t(CallSlot<uint32_t>(b, 26, &word))), word});
        CallSlot<void>(b, 27);
        drawfx::Note("line", uint8_t(CallSlot<uint32_t>(b, 28, 1, 2, 3, 4, x, 1)));
        drawfx::Note("blt", uint8_t(CallSlot<uint32_t>(b, 29, b, &rect, &rect, &rect)));
        drawfx::Note("rgba", CallSlot<uint32_t>(b, 32, uint32_t(c[0]), uint32_t(c[1]), uint32_t(c[2]), uint32_t(c[3])));
        drawfx::Note("rgba out", uint8_t(CallSlot<uint32_t>(b, 33, x, &c[0], &c[1], &c[2], &c[3])));
        drawfx::Note("rgba bytes", {c[0], c[1], c[2], c[3]});
        drawfx::Note("change", CallSlot<uint32_t>(b, 36));
        drawfx::Note("cache", CallSlot<uint32_t>(b, 37));
        drawfx::Note("set cache", uint8_t(CallSlot<uint32_t>(b, 38, b)));
        break;
    }
    case 3: { // the lock's view
        drawfx::Note("locked", uint8_t(CallSlot<uint32_t>(b, 8)));
        drawfx::Note("ready", uint8_t(CallSlot<uint32_t>(b, 39)));
        drawfx::Note("size", {CallSlot<uint32_t>(b, 9), CallSlot<uint32_t>(b, 10)});
        cGZRect r{-1, -1, -1, -1};
        const cGZRect *got = CallSlot<cGZRect *>(b, 11, &r);
        drawfx::Note("bounds", {uint32_t(got == &r), uint32_t(r.left), uint32_t(r.top), uint32_t(r.right), uint32_t(r.bottom)});
        const cGZRect *p = CallSlot<const cGZRect *>(b, 12);
        drawfx::Note("bounds ptr", {uint32_t(p != nullptr), uint32_t(p->left), uint32_t(p->top), uint32_t(p->right), uint32_t(p->bottom)});
        drawfx::Note("bits", drawfx::Ptr(CallSlot<void *>(b, 34)));
        drawfx::Note("stride", CallSlot<uint32_t>(b, 35));
        break;
    }
    case 4: { // the colour types (the surface must be held: the slots ask it for its format)
        if (!Locked(b) || !static_cast<const uint32_t *>(b)[2]) break;
        cGZColorType t1{0x11, 0x22}, t2{0x33, 0x44};
        const bool r1 = CallSlot<cGZColorType *>(b, 14, &t1) == &t1;
        const bool r2 = CallSlot<cGZColorType *>(b, 15, &t2) == &t2;
        drawfx::Note("colour types", {uint32_t(r1), t1.format, t1.bits, uint32_t(r2), t2.format, t2.bits});
        break;
    }
    case 5:
        if (!static_cast<const uint32_t *>(b)[2]) break;
        drawfx::Note("bits per pixel", {CallSlot<uint32_t>(b, 16), CallSlot<uint32_t>(b, 17)});
        break;
    case 6:
    case 7: { // pixels: inside the locked area (or with no byte count the slots handle: no memory touched)
        if (!static_cast<const uint32_t *>(b)[2] || !Locked(b)) break;
        const int32_t bytes = BytesPerPixel();
        const uint32_t pitch = static_cast<const uint32_t *>(b)[8];
        const uint32_t rows = static_cast<const uint32_t *>(b)[10];
        uint32_t x = 0, y = 0;
        if (bytes >= 1 && bytes <= 4) {
            x = rng() % (pitch / uint32_t(bytes));
            y = rng() % rows;
        } else {
            x = rng() % 64;
            y = rng() % 64;
        }
        if (op == 6) {
            drawfx::Note("get pixel", {x, y, CallSlot<uint32_t>(b, 21, x, y)});
        } else {
            const uint32_t v = rng();
            CallSlot<void>(b, 22, x, y, v);
            drawfx::Note("set pixel", {x, y, v});
            NotePixels();
        }
        break;
    }
    case 8:
    case 9: { // the conversions (the face must be there: the original uses it unchecked)
        if (!static_cast<const uint32_t *>(b)[2] || gw.format.faceCount == 0) break;
        if (op == 8) {
            const uint32_t r = rng() & 0xff, g = rng() & 0xff, bl = rng() & 0xff;
            // the stack words' upper bytes are garbage: the slot takes bytes
            drawfx::Note("to native", CallSlot<uint32_t>(b, 30, r | 0xab00u, g | 0xcd00u, bl | 0xef00u));
        } else {
            uint8_t r = 0x11, g = 0x22, bl = 0x33;
            const uint32_t v = rng();
            const bool ok = CallSlot<bool>(b, 31, v, &r, &g, &bl);
            drawfx::Note("to rgb", {uint32_t(uint8_t(ok)), r, g, bl});
        }
        break;
    }
    default:
        for (unsigned i = 0; i < 16; ++i) gw.pixels[rng() % kPixels] = uint8_t(rng());
        NotePixels();
        break;
    }
    NoteObject(b);
}

using Ctor = void *(__thiscall *)(void *, void *);
using Dtor = void(__thiscall *)(void *);

void *Make(difftest::Context &ctx, bool original)
{
    void *const memory = ::operator new(0x2c);
    std::memset(memory, 0xc5, 0x2c);
    void *const made = original ? ctx.Original<Ctor>(0x18a2a0)(memory, &gw.surface)
                                : HookEntry<Ctor>(0x18a2a0)(memory, &gw.surface);
    drawfx::Note("made", uint32_t(made == memory));
    return memory;
}

void Life(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    void *const b = Make(ctx, original);
    NoteObject(b);
    drawfx::Note("hold", CallSlot<uint32_t>(b, 1)); // the steps' Releases must not reach zero
    const unsigned steps = 2 + rng() % 24;
    for (unsigned s = 0; s < steps; ++s) Step(b, rng);
    if (rng() % 4 == 0) {
        sgfx::MarkStep(ctx, "last release");
        drawfx::Note("last release", CallSlot<uint32_t>(b, 2)); // the deleting destructor frees it
    } else {
        sgfx::MarkStep(ctx, "destructor");
        if (original) ctx.Original<Dtor>(0x18a330)(b);
        else HookEntry<Dtor>(0x18a330)(b);
        NoteObject(b);
        ::operator delete(b);
    }
    drawfx::Note("surface refs", uint32_t(gw.surface.refs));
}

} // namespace

// Verifies: 0x18a2a0 and 0x18a330 (through their hooks' entries; the constructor's parts the stub constructor 0x18a290
// and LockSurface 0x18a7d0 with it), and through our vtable the slot bodies 0x18a420, 0x18a990, 0x18a7b0, 0x18a650,
// 0x18a6f0, 0x18a6c0 (with 0x18a440), 0x18a630, 0x18a680, 0x18a720, 0x18a910, 0x18a530, 0x18a5d0, the stub's QueryInterface
// 0x18a8c0 and Release 0x18a8f0 (slots 0 and 2, inherited; lane WI), the stub's base destructor 0x18a320 (inside 0x18a330
// and 0x18a420 on the original side) and the folded
// bodies 0x18a670 (shared_vf_18a670, slot 11) and 0x18a5b0 (shared_vf_18a5b0, slot 32) as this vtable has them.
DIFF_TEST(SurfaceLockBuffer_Life, 0x18a2a0)
{
    sgfx::CheckSides(
        ctx, "surface lock buffer", 3000, [&](uint32_t seed, drawfx::Cells &cells) { Build(seed, cells); },
        [&](bool original, std::mt19937 &rng) {
            Life(ctx, original, rng);
            ++ctx.cases;
        });
}

// 0x18a400, the stub's own deleting destructor (slot 40 of 0xda0c00). An abstract class's: ours optimises the stub's vtable
// away in the game build, so a complete class of the test's reads it off its own object, in the second base's constructor:
// after the stub's constructor stored it and before the class stored its own.
namespace {

void *const *gStubTable;

struct StubTableReader {
    explicit StubTableReader(void *stub) { gStubTable = *static_cast<void *const *volatile *>(stub); }
};

using Rect = const cGZRect *;
class StubProbe final : public nRZGraphic4::cGZBufferStub, private StubTableReader {
public:
    StubProbe() : StubTableReader(static_cast<nRZGraphic4::cGZBufferStub *>(this)) {}
    bool Init(uint32_t, uint32_t, uint32_t, uint32_t) override { return false; }
    bool Shutdown() override { return false; }
    bool Uninitialize() override { return false; }
    bool Lock(uint32_t) override { return false; }
    bool Unlock(uint32_t) override { return false; }
    bool IsLocked() override { return false; }
    uint32_t GetWidth() override { return 0; }
    uint32_t GetHeight() override { return 0; }
    cGZRect GetBounds() override { return {}; }
    Rect GetBoundsPtr() override { return nullptr; }
    uint32_t GetMemoryUsage() override { return 0; }
    cGZColorType GetColorType() override { return {}; }
    cGZColorType GetNativeColorType() override { return {}; }
    uint32_t GetBitsPerPixel() override { return 0; }
    uint32_t GetBytesPerPixel() override { return 0; }
    bool Fill(uint32_t, Rect) override { return false; }
    bool FillCoords(uint32_t, int32_t, int32_t, int32_t, int32_t, Rect) override { return false; }
    bool FillRect(uint32_t, Rect, Rect) override { return false; }
    uint32_t GetPixel(uint32_t, uint32_t) override { return 0; }
    void SetPixel(uint32_t, uint32_t, uint32_t) override {}
    void SetTransparency(uint32_t) override {}
    bool GetTransparency() override { return false; }
    bool IsPixelTransparent(uint32_t, uint32_t) override { return false; }
    bool GetTransparentColor(uint32_t *) override { return false; }
    void RemoveTransparency() override {}
    bool DrawLine(int32_t, int32_t, int32_t, int32_t, uint32_t, int32_t) override { return false; }
    bool Blt(cIGZBuffer *, Rect, Rect, Rect) override { return false; }
    uint32_t ConvertRGBValueToNative(uint8_t, uint8_t, uint8_t) override { return 0; }
    bool ConvertNativeValueToRGB(uint32_t, uint8_t *, uint8_t *, uint8_t *) override { return false; }
    uint32_t ConvertRGBAValueToNative(uint8_t, uint8_t, uint8_t, uint8_t) override { return 0; }
    bool ConvertNativeValueToRGBA(uint32_t, uint8_t *, uint8_t *, uint8_t *, uint8_t *) override { return false; }
    void *GetColorSurfaceBits() override { return nullptr; }
    uint32_t GetColorSurfaceStride() override { return 0; }
    uint32_t GetChangeCounter() override { return 0; }
    cIGZUnknown *GetHardwareCache() override { return nullptr; }
    bool SetHardwareCache(cIGZUnknown *) override { return false; }
    bool IsReady() override { return false; }
};

} // namespace

DIFF_TEST(SurfaceLockBuffer_StubDeletingDestructor, 0x18a400)
{
    using Deleting = void *(__thiscall *)(void *, uint32_t);
    { StubProbe probe; }
    void *const imageTable = ctx.image.At<void>(0xda0c00);
    const Deleting original = ctx.Original<Deleting>(0x18a400);
    const Deleting ours = reinterpret_cast<Deleting>(gStubTable[40]);
    struct Result {
        bool keptResult, keptTable, freedResult, freedOnce;
        uint32_t count;
        bool operator==(const Result &) const = default;
    };
    const auto run = [](Deleting deleting, const void *table, uint32_t count) {
        uint32_t *const block = static_cast<uint32_t *>(::operator new(8));
        block[0] = reinterpret_cast<uintptr_t>(table);
        block[1] = count;
        Result r{};
        r.keptResult = deleting(block, 0) == block;
        r.keptTable = block[0] == reinterpret_cast<uintptr_t>(table);
        r.count = block[1];
        shape::DeleteRecorder deletes;
        deletes.Begin();
        r.freedResult = deleting(block, 1) == block;
        deletes.End();
        r.freedOnce = deletes.Count() == 1 && deletes.Is(0, block, 8);
        return r;
    };
    for (const uint32_t count : {0u, 1u, 2u, 0x7fffffffu, 0xffffffffu}) {
        ++ctx.cases;
        const Result a = run(original, imageTable, count), b = run(ours, gStubTable, count);
        if (!(a == b) || !a.freedOnce || a.count != count)
            ctx.Fail("count %u: original %d %d %d %d %u, ours %d %d %d %d %u", count, a.keptResult, a.keptTable, a.freedResult,
                     a.freedOnce, a.count, b.keptResult, b.keptTable, b.freedResult, b.freedOnce, b.count);
    }
}

// The destructor's hook entry on a buffer the original constructor built, its vtable a copy with every slot a trap.
DIFF_TEST(SurfaceLockBuffer_Adapters, 0x18a330)
{
    static void *traps[41];
    static bool trapped;
    for (void *&t : traps)
        t = reinterpret_cast<void *>(+[]() { trapped = true; });
    sgfx::CheckSides(
        ctx, "surface lock buffer adapters", 400, [&](uint32_t seed, drawfx::Cells &cells) { Build(seed, cells); },
        [&](bool original, std::mt19937 &) {
            void *const memory = ::operator new(0x2c);
            std::memset(memory, 0xc5, 0x2c);
            ctx.Original<Ctor>(0x18a2a0)(memory, &gw.surface);
            NoteObject(memory);
            trapped = false;
            if (!original) *static_cast<void ***>(memory) = traps;
            if (original) ctx.Original<Dtor>(0x18a330)(memory);
            else HookEntry<Dtor>(0x18a330)(memory);
            drawfx::Note("trapped", uint32_t(trapped));
            NoteObject(memory);
            ::operator delete(memory);
            ++ctx.cases;
        });
}
