// Differential tests for cGZDrawContextRenderToBuffer as a whole class (src/gz/cGZDrawContextRenderToBuffer.h; lane NS,
// rendering R5b).
//
// GZDrawRenderToBuffer_Slots: each case builds one engine per side, the original by operator new and the constructor
// 0x8c7dc0 (the game's vtable 0xe4f4e4), ours by placement new (our vtable; CreateEngine's `new`), both in a block of
// the same seeded bytes (the constructor leaves most fields as they were), and calls every slot
// through the object's vtable, so the original side runs the game's slots (and the game's span renderers, which its
// SetTarget makes) and ours our members (and our renderers). The world is drawfx fakes:
// - two targets and a texture (cIGZBuffer) over real pixel memory: each answers the case's width, height, bounds,
//   native colour type, bytes per pixel, surface bits and stride, Lock (the case's bool), GetPixel / SetPixel on its
//   memory, GetTransparentColor and the colour conversions (a fixed packing, so blended pixels round-trip); target 1
//   answers the cIGZBuffer query only by case;
// - a cIGZUnknown that answers no query (SetTexture's failed query).
// A case is 60 random slot calls: SetTarget on any target (formats 8-bit, 1555 both ids, 565, 4444, 8888 both ids, and
// unsupported ones), Detach, GetBounds, SetClip, SetColors, the state words and bytes, gradients (as cGZDrawContext makes
// them, and null), textures (plain, scaled, clipped and tiled, every texture format, with and without a key, a failed
// lock, a failed query, none), the batch and surface locks, ChooseRoute (antialiased triangles, quads and polygons, which
// run the scan converter 0x9cf0c0 on both sides) and DrawSpans (fills, gradients, blends and every texture mode). Calls
// that reach the target are made only while the engine holds one (the original dereferences it unchecked). After every
// call the log gets the result, the out-parameters and every word of the object but its vtable (+0) and its renderer
// (+0xc0: the game's renderer object on the original side, ours on ours; what the renderers draw is compared through
// the pixels); after every drawing call a hash of each target's whole memory (padding and margins included). At the end
// the engine is released to zero (the destructor) and the log gets the fakes' reference counts, the texture memory's
// hash and the side's heap balance (difftest::AllocationCounter). Even seeds run with the CPU level global 0x10df81c
// at 2 (the 16-bit blends' SSE paths), odd ones at 0.
// GZDrawRenderToBuffer_Construct: the constructor on seeded bytes, every byte but the vtable word.
#include "difftest.h"
#include "drawfx.h"

#include "gz/cGZDrawContextRenderToBuffer.h"

#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace {

using drawfx::Ret;
constexpr drawfx::Slot U{0, Ret::Int, false, "", ""};
constexpr drawfx::Slot kQI{2, Ret::Int, true, "QueryInterface", "ui"};
constexpr drawfx::Slot kAddRef{0, Ret::Int, true, "AddRef", ""};
constexpr drawfx::Slot kRelease{0, Ret::Int, true, "Release", ""};

inline constexpr drawfx::Slot kBufferSlots[] = {
    kQI, kAddRef, kRelease, U, U, U,
    {1, Ret::Int, true, "Lock", "u"},
    {1, Ret::Int, true, "Unlock", "u"},
    U,
    {0, Ret::Int, true, "GetWidth", ""},
    {0, Ret::Int, true, "GetHeight", ""},
    {1, Ret::Int, true, "GetBounds", "i"}, // the hidden result pointer
    U, U, U,
    {1, Ret::Int, true, "GetNativeColorType", "i"}, // the hidden result pointer
    U,
    {0, Ret::Int, true, "GetBytesPerPixel", ""},
    U, U, U,
    {2, Ret::Int, true, "GetPixel", "uu"},
    {3, Ret::Int, true, "SetPixel", "uuu"},
    U, U, U,
    {1, Ret::Int, true, "GetTransparentColor", "i"},
    U, U, U,
    {3, Ret::Int, true, "ConvertRGBValueToNative", "bbb"},
    {4, Ret::Int, true, "ConvertNativeValueToRGB", "uiii"},
    {4, Ret::Int, true, "ConvertRGBAValueToNative", "bbbb"},
    U,
    {0, Ret::Int, true, "GetColorSurfaceBits", ""},
    {0, Ret::Int, true, "GetColorSurfaceStride", ""},
    U, U, U, U,
};
inline constexpr drawfx::Iface kBuffer{"cIGZBuffer", 40, kBufferSlots};

inline constexpr drawfx::Slot kUnknownSlots[] = {kQI, kAddRef, kRelease};
inline constexpr drawfx::Iface kUnknown{"cIGZUnknown", 3, kUnknownSlots};

constexpr uint32_t kConstructor = 0x8c7dc0, kGameVTable = 0xe4f4e4, kCpuLevel = 0x10df81c;
constexpr uint32_t kRendererOffset = 0xc0;
constexpr uint32_t kObjectTag = 0x0c000000, kMemoryTag = 0x0d000000;

// ---- the case ----

// Three buffers (two targets, the texture), each over its own memory: rows of `stride` bytes, the pixels starting
// kMargin rows and kMargin pixels in (the texture paths' reads may pass an area's edge by a texel or two).
constexpr uint32_t kMaxWidth = 40, kMaxHeight = 24, kMargin = 8;
constexpr uint32_t kMaxStride = (kMaxWidth + 2 * kMargin) * 4 + 12;
constexpr uint32_t kMemoryBytes = kMaxStride * (kMaxHeight + 2 * kMargin);
alignas(16) uint8_t gMemory[3][kMemoryBytes];

struct Buffer {
    uint32_t width, height, format, bits, bytes, stride;
    cGZRect bounds;
    uint32_t lockOk;
    uint32_t transparent, hasTransparent;
    bool bufferFace;
    uint8_t *pixels; // the first pixel
};
struct Case {
    Buffer buffers[3];
    uint32_t cpuLevel;
};
Case gCase;

drawfx::Fake gBuffers[3], gStranger;

uint32_t BytesOf(uint32_t format)
{
    switch (format) {
    case 2: return 1;
    case 4: case 5: case 6: case 7: return 2;
    case 8: case 9: return 4;
    default: return 2;
    }
}

uint8_t *PixelAt(const Buffer &b, uint32_t x, uint32_t y) { return b.pixels + size_t(y) * b.stride + size_t(x) * b.bytes; }

// The buffers' answers by their index (value[0]).
uint32_t GetWidth(drawfx::Fake &self, const uint32_t *) { return gCase.buffers[self.value[0]].width; }
uint32_t GetHeight(drawfx::Fake &self, const uint32_t *) { return gCase.buffers[self.value[0]].height; }
uint32_t GetBytesPerPixel(drawfx::Fake &self, const uint32_t *) { return gCase.buffers[self.value[0]].bytes; }
uint32_t GetBits(drawfx::Fake &self, const uint32_t *) { return drawfx::P(gCase.buffers[self.value[0]].pixels); }
uint32_t GetStride(drawfx::Fake &self, const uint32_t *) { return gCase.buffers[self.value[0]].stride; }
uint32_t Lock(drawfx::Fake &self, const uint32_t *) { return gCase.buffers[self.value[0]].lockOk; }
uint32_t Unlock(drawfx::Fake &, const uint32_t *) { return 1; }
uint32_t GetBounds(drawfx::Fake &self, const uint32_t *args)
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(args[0])), &gCase.buffers[self.value[0]].bounds, sizeof(cGZRect));
    return args[0];
}
uint32_t GetNativeColorType(drawfx::Fake &self, const uint32_t *args)
{
    const Buffer &b = gCase.buffers[self.value[0]];
    const cGZColorType type{b.format, b.bits};
    std::memcpy(reinterpret_cast<void *>(uintptr_t(args[0])), &type, sizeof(type));
    return args[0];
}
uint32_t GetTransparentColor(drawfx::Fake &self, const uint32_t *args)
{
    const Buffer &b = gCase.buffers[self.value[0]];
    *reinterpret_cast<uint32_t *>(uintptr_t(args[0])) = b.transparent;
    return b.hasTransparent;
}
// GetPixel / SetPixel on the memory; outside the buffer (FillPolygon clamps to the clip, not the width) a fixed answer
// and no write, as a real buffer's bounds check does.
uint32_t GetPixel(drawfx::Fake &self, const uint32_t *args)
{
    const Buffer &b = gCase.buffers[self.value[0]];
    if (args[0] >= b.width || args[1] >= b.height) return 0x5a5a5a5a;
    uint32_t v = 0;
    std::memcpy(&v, PixelAt(b, args[0], args[1]), b.bytes);
    return v;
}
uint32_t SetPixel(drawfx::Fake &self, const uint32_t *args)
{
    const Buffer &b = gCase.buffers[self.value[0]];
    if (args[0] < b.width && args[1] < b.height) std::memcpy(PixelAt(b, args[0], args[1]), &args[2], b.bytes);
    return 0;
}
// A fixed packing: r, g, b in bits 16, 8, 0 (alpha in 24), cut to the pixel size, so blended pixels round-trip.
uint32_t Mask(const Buffer &b) { return b.bytes >= 4 ? 0xffffffffu : (1u << (8 * b.bytes)) - 1; }
uint32_t ToNative(drawfx::Fake &self, const uint32_t *args)
{
    const uint32_t v = (args[0] & 0xff) << 16 | (args[1] & 0xff) << 8 | (args[2] & 0xff);
    return v & Mask(gCase.buffers[self.value[0]]);
}
uint32_t ToNativeA(drawfx::Fake &self, const uint32_t *args)
{
    const uint32_t v = (args[3] & 0xff) << 24 | (args[0] & 0xff) << 16 | (args[1] & 0xff) << 8 | (args[2] & 0xff);
    return v & Mask(gCase.buffers[self.value[0]]);
}
uint32_t ToRGB(drawfx::Fake &, const uint32_t *args)
{
    *reinterpret_cast<uint8_t *>(uintptr_t(args[1])) = uint8_t(args[0] >> 16);
    *reinterpret_cast<uint8_t *>(uintptr_t(args[2])) = uint8_t(args[0] >> 8);
    *reinterpret_cast<uint8_t *>(uintptr_t(args[3])) = uint8_t(args[0]);
    return 1;
}

void BuildWorld()
{
    for (unsigned i = 0; i < 3; ++i) {
        drawfx::Fake &f = gBuffers[i];
        f.Reset<kBuffer>(500 + i);
        f.value[0] = i;
        f.On(6, &Lock);
        f.On(7, &Unlock);
        f.On(9, &GetWidth);
        f.On(10, &GetHeight);
        f.On(11, &GetBounds);
        f.On(15, &GetNativeColorType);
        f.On(17, &GetBytesPerPixel);
        f.On(21, &GetPixel);
        f.On(22, &SetPixel);
        f.On(26, &GetTransparentColor);
        f.On(30, &ToNative);
        f.On(31, &ToRGB);
        f.On(32, &ToNativeA);
        f.On(34, &GetBits);
        f.On(35, &GetStride);
        if (gCase.buffers[i].bufferFace) f.AddFace(cIGZBuffer::kIid, f);
    }
    gStranger.Reset<kUnknown>(510);
}

void NewCase(uint32_t seed)
{
    std::mt19937 rng(seed * 7919u + 5);
    const uint32_t formats[] = {2, 4, 5, 6, 7, 8, 9};
    for (unsigned i = 0; i < 3; ++i) {
        Buffer &b = gCase.buffers[i];
        b.format = rng() % 8 ? formats[rng() % 7] : (rng() % 2 ? 3 : rng() % 12);
        b.bits = rng() % 33;
        b.bytes = BytesOf(b.format);
        b.width = 1 + rng() % kMaxWidth;
        b.height = 1 + rng() % kMaxHeight;
        b.stride = (b.width + 2 * kMargin) * b.bytes + (rng() % 4) * 4;
        b.pixels = gMemory[i] + kMargin * b.stride + kMargin * b.bytes;
        const int32_t left = rng() % 4 ? 0 : int32_t(rng() % 3), top = rng() % 4 ? 0 : int32_t(rng() % 3);
        b.bounds = {left, top, left + int32_t(b.width), top + int32_t(b.height)};
        b.lockOk = rng() % 10 ? 1 : 0;
        b.transparent = rng() % 2 ? rng() : rng() & 0xff;
        b.hasTransparent = rng() % 2;
        b.bufferFace = i != 1 || rng() % 5 != 0;
        for (uint8_t &byte : gMemory[i]) byte = uint8_t(rng());
        // A quarter of the pixels are the transparent colour, some with bits above the format's key mask changed (so
        // the texture paths' key test, masked per format by DrawSpans, sees matches and near misses).
        for (uint32_t y = 0; y < b.height; ++y)
            for (uint32_t x = 0; x < b.width; ++x) {
                if (rng() % 4) continue;
                const uint32_t value = b.transparent ^ (rng() % 2 ? 0u : (rng() % 2 ? 0xf000u : 0xff000000u) & rng());
                std::memcpy(PixelAt(b, x, y), &value, b.bytes);
            }
    }
    gCase.cpuLevel = seed % 2 ? 0 : 2;
}

// ---- calls through the object's vtable ----

template <class R, class... A> R Call(void *object, unsigned slot, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*static_cast<void ***>(object))[slot])(object, args...);
}

uint32_t Word(const void *object, uint32_t offset)
{
    uint32_t v;
    std::memcpy(&v, static_cast<const uint8_t *>(object) + offset, 4);
    return v;
}

// Every word of the object but its vtable and its renderer pointer. By kind (lane RK): the target (+8) and the texture
// (+0x24) are pointers (the test's fakes, or null); the rest is data, as it is (the block starts as seeded bytes). The
// locked surface's bits (+0x88) are data too: a pointer into gMemory (one address on both sides) or the seeded bytes.
void NoteObject(void *object)
{
    drawfx::Call c;
    c.note = "object";
    for (uint32_t off = 4; off < cGZDrawContextRenderToBuffer::kSize; off += 4)
        if (off != kRendererOffset)
            c.words.push_back(off == 8 || off == 0x24 ? drawfx::Ptr(Word(object, off)) : Word(object, off));
    drawfx::gLog.push_back(std::move(c));
}

uint32_t Hash(const uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}
void NoteTargets()
{
    drawfx::Note("pixels", {Hash(gMemory[0], kMemoryBytes), Hash(gMemory[1], kMemoryBytes)});
}

float Unit(std::mt19937 &rng) { return float(rng() % 1001) / 1000.0f; }

// `count` spans inside [x0, x1) x [y0, y1) (some empty, many as long as they can be).
std::vector<int32_t> Spans(std::mt19937 &rng, int32_t x0, int32_t x1, int32_t y0, int32_t y1, uint32_t count)
{
    std::vector<int32_t> spans;
    for (uint32_t i = 0; i < count; ++i) {
        const int32_t y = y0 + int32_t(rng() % uint32_t(y1 - y0));
        const int32_t a = x0 + int32_t(rng() % uint32_t(x1 - x0 + 1));
        int32_t length = int32_t(rng() % uint32_t(x1 - a + 1));
        if (rng() % 2) length = x1 - a;
        spans.push_back(y << 16 | a);
        spans.push_back(y << 16 | (a + length));
    }
    return spans;
}

// The block both sides construct in: seeded bytes (the fields the constructor leaves start alike on both sides).
void *Block(uint32_t seed)
{
    uint8_t *const block = static_cast<uint8_t *>(::operator new(cGZDrawContextRenderToBuffer::kSize));
    std::mt19937 rng(seed * 31u + 7);
    for (uint32_t i = 0; i < cGZDrawContextRenderToBuffer::kSize; ++i) block[i] = uint8_t(rng());
    return block;
}
void *OriginalCreate(uint32_t seed)
{
    using Ctor = void *(__thiscall *)(void *);
    return reinterpret_cast<Ctor>(GameAddress<void>(kConstructor))(Block(seed));
}
// As CreateEngine's new, in a seeded block (Release frees it with the sized delete, as the original's).
void *OurCreate(uint32_t seed)
{
    return static_cast<cIGZDrawContextEngine *>(::new (Block(seed)) cGZDrawContextRenderToBuffer);
}

void RunSide(bool ours, uint32_t seed)
{
    drawfx::Cells cells;
    cells.Set(kCpuLevel, gCase.cpuLevel);
    drawfx::gLog.reserve(1 << 15);
    for (unsigned i = 0; i < 3; ++i) drawfx::AddRange(gMemory[i], kMemoryBytes, kMemoryTag + (i << 20));
    std::mt19937 rng(seed);
    difftest::AllocationCounter heap;

    void *const object = ours ? OurCreate(seed) : OriginalCreate(seed);
    {
        difftest::NotCounted bookkeeping;
        drawfx::AddRange(object, cGZDrawContextRenderToBuffer::kSize, kObjectTag);
    }
    NoteObject(object);
    drawfx::Note("addref", Call<uint32_t>(object, 1));

    int target = -1;       // the buffer the engine holds (SetTarget's answer), or none
    cGZRect area{};        // the last texture's area (SetTexture's out-parameter)
    for (int step = 0; step < 60; ++step) {
        // SetTarget more often while there is none.
        uint32_t pick = rng() % 24;
        if (target < 0 && rng() % 2) pick = 0;
        drawfx::Note("pick", pick);
        const Buffer *const t = target >= 0 ? &gCase.buffers[target] : nullptr;
        bool drew = false;
        switch (pick) {
        case 0: {
            const unsigned which = rng() % 4 ? rng() % 2 : 2; // the texture buffer as a target too, sometimes
            const uint32_t ok = Call<uint32_t>(object, 3, static_cast<void *>(&gBuffers[which])) & 0xff;
            drawfx::Note("settarget", ok);
            target = ok ? int(which) : -1;
            break;
        }
        case 1:
            Call<void>(object, 4);
            target = -1;
            break;
        case 2: {
            if (!t) break;
            cGZRect r{-7, -7, -7, -7};
            const uint32_t back = Call<uint32_t>(object, 5, &r);
            drawfx::Note("bounds", {uint32_t(back == drawfx::P(&r)), uint32_t(r.left), uint32_t(r.top), uint32_t(r.right),
                                    uint32_t(r.bottom)});
            break;
        }
        case 3: {
            // A clip around the target (FillPolygon clamps to it), sometimes larger or empty.
            const int32_t w = t ? int32_t(t->width) : 20, h = t ? int32_t(t->height) : 20;
            cGZRect r{int32_t(rng() % 6) - 2, int32_t(rng() % 6) - 2, w - int32_t(rng() % 6) + 2, h - int32_t(rng() % 6) + 2};
            if (rng() % 8 == 0) r.right = r.left;
            Call<void>(object, 6, &r);
            break;
        }
        case 4: {
            if (!t) break;
            uint32_t colours[4];
            for (uint32_t &c : colours) c = rng() % 3 ? rng() : (rng() % 2 ? 0xff000000u | rng() : rng() & 0xffffff);
            const uint32_t first = rng() % 4, count = 1 + rng() % (4 - first);
            Call<void>(object, 7, first, count, static_cast<const uint32_t *>(colours));
            break;
        }
        case 5: {
            const uint32_t values[] = {0, 1, 2, 0x80000000u, 0xffffffffu};
            Call<void>(object, 8, rng() % 3 ? values[rng() % 5] : rng());
            break;
        }
        case 6: case 7: case 8: {
            // The byte slots 9, 10, 11 (high bytes garbage: only the low byte is the argument).
            const uint32_t value = (rng() & 0xffffff00u) | (rng() % 3 ? rng() % 2 : rng() & 0xff);
            Call<void>(object, pick + 3, value);
            break;
        }
        case 9: Call<void>(object, 12, Unit(rng) * 4.0f); break;
        case 10: {
            if (rng() % 5 == 0) {
                Call<void>(object, 13, static_cast<const float *>(nullptr), 0.0f, 0.0f);
                break;
            }
            // As cGZDrawContext makes them: channels 0..1 at the origin, small steps per pixel; sometimes anything.
            float c[12];
            for (int i = 0; i < 4; ++i) c[i] = Unit(rng);
            for (int i = 4; i < 12; ++i) c[i] = (Unit(rng) - 0.5f) * (rng() % 4 ? 0.05f : 1.0f);
            if (rng() % 10 == 0)
                for (float &v : c) v = (Unit(rng) - 0.5f) * 4.0f;
            const float x = float(int(rng() % 60) - 10) + (rng() % 2 ? 0.5f : 0.0f), y = float(int(rng() % 40) - 10);
            Call<void>(object, 13, static_cast<const float *>(c), x, y);
            break;
        }
        case 11: case 12: {
            // A texture: the texture buffer (the area noted), a stranger without the buffer face, or none.
            const uint32_t kind = rng() % 6;
            void *const texture = kind < 4 ? static_cast<void *>(&gBuffers[2]) : kind == 4 ? static_cast<void *>(&gStranger)
                                                                                           : nullptr;
            const float scales[] = {1.0f, 1.0f, 2.0f, 0.5f, 1.5f, 0.75f, 3.0f, 0.3f};
            const float sx = scales[rng() % 8], sy = rng() % 3 ? sx : scales[rng() % 8];
            const float x = float(int(rng() % 40) - 10) + (rng() % 3 ? 0.0f : Unit(rng));
            const float y = float(int(rng() % 24) - 8) + (rng() % 3 ? 0.0f : Unit(rng));
            cGZRect r{-9, -9, -9, -9};
            const uint32_t ok = Call<uint32_t>(object, 14, texture, x, y, sx, sy, &r) & 0xff;
            drawfx::Note("texture", {ok, uint32_t(r.left), uint32_t(r.top), uint32_t(r.right), uint32_t(r.bottom)});
            if (texture == &gBuffers[2]) area = r;
            break;
        }
        case 13: Call<void>(object, 15); break;
        case 14: {
            // A clip inside the texture (tiled over the spans).
            const Buffer &x = gCase.buffers[2];
            const int32_t l = int32_t(rng() % x.width), tp = int32_t(rng() % x.height);
            cGZRect r{l, tp, l + 1 + int32_t(rng() % (x.width - l)), tp + 1 + int32_t(rng() % (x.height - tp))};
            Call<void>(object, 16, &r);
            break;
        }
        case 15: Call<void>(object, 17); break;
        case 16: case 17:
            if (t) Call<void>(object, pick + 2); // vf18 / vf19: the batch lock
            break;
        case 18: {
            if (!t) break;
            cGZDrawSurface s;
            std::memset(&s, 0xcd, sizeof(s));
            const uint32_t ok = Call<uint32_t>(object, 20, &s, rng() % 3 ? rng() % 2 : rng()) & 0xff;
            drawfx::Note("surface", {ok, drawfx::Norm(drawfx::P(s.bits)), uint32_t(s.stride), s.width, s.height, s.format,
                                     s.bytesPerPixel});
            break;
        }
        case 19:
            if (t) Call<void>(object, 21);
            break;
        case 20: {
            // ChooseRoute: triangles, quads, a polygon, or another kind; points around the target.
            if (!t) break;
            const uint32_t kinds[] = {6, 7, 8, 8, 1, 5};
            const uint32_t kind = kinds[rng() % 6];
            uint32_t count = kind == 6 ? 3 * (1 + rng() % 3) : kind == 7 ? 4 * (1 + rng() % 2) : 3 + rng() % 6;
            if (rng() % 10 == 0) count = rng() % 3;
            std::vector<float> points(2 * size_t(count) + 2);
            for (uint32_t i = 0; i < count; ++i) {
                points[2 * i] = float(int(rng() % (t->width + 6)) - 3) + Unit(rng);
                points[2 * i + 1] = float(int(rng() % (t->height + 6)) - 3) + Unit(rng);
                if (rng() % 8 == 0) points[2 * i] = float(int(rng() % (t->width + 1)));
            }
            drawfx::Note("route", Call<uint32_t>(object, 22, kind, static_cast<const float *>(points.data()), count));
            drew = true;
            break;
        }
        default: {
            // DrawSpans: inside the target, and where a texture is drawn without a clip, inside its area.
            if (!t) break;
            int32_t x0 = 0, x1 = int32_t(t->width), y0 = 0, y1 = int32_t(t->height);
            const bool texture = Word(object, 0x24) != 0, clipped = (Word(object, 0xb4) & 0xff) != 0;
            if (texture && !clipped) {
                x0 = (std::max)(x0, area.left);
                x1 = (std::min)(x1, area.right);
                y0 = (std::max)(y0, area.top);
                y1 = (std::min)(y1, area.bottom);
            }
            if (x0 >= x1 || y0 >= y1) {
                drawfx::Note("nospans", 0);
                break;
            }
            const std::vector<int32_t> spans = Spans(rng, x0, x1, y0, y1, 1 + rng() % 12);
            Call<void>(object, 23, static_cast<const int32_t *>(spans.data()), uint32_t(spans.size() / 2));
            drew = true;
            break;
        }
        }
        NoteObject(object);
        if (drew) NoteTargets();
    }

    const uint32_t count = Word(object, 4);
    drawfx::Note("count", count);
    if (rng() % 2) {
        uint32_t out = 0xdeadbeef;
        const uint32_t ids[] = {1, cGZDrawContextRenderToBuffer::kIid, 0x12345678};
        const uint32_t answer = Call<uint32_t>(object, 0, ids[rng() % 3], static_cast<void *>(&out)) & 0xff;
        drawfx::Note("query", {answer, drawfx::Norm(out)});
        if (answer) drawfx::Note("release", Call<uint32_t>(object, 2));
    }
    for (uint32_t i = 0; i < count; ++i) drawfx::Note("release", Call<uint32_t>(object, 2));
    heap.Stop();
    drawfx::Note("refs", {uint32_t(gBuffers[0].refs), uint32_t(gBuffers[1].refs), uint32_t(gBuffers[2].refs),
                          uint32_t(gStranger.refs)});
    NoteTargets();
    drawfx::Note("texture memory", Hash(gMemory[2], kMemoryBytes));
    drawfx::Note("heap", {uint32_t(heap.Blocks()), uint32_t(heap.Bytes())});
}

} // namespace

DIFF_TEST(GZDrawRenderToBuffer_Slots, 0x8c8750)
{
    // Every slot (QueryInterface 0x8c7eb0, AddRef 0x8c7e50, Release 0x8c7e60, SetTarget 0x8c7ee0, Detach 0x8c80d0,
    // GetBounds 0x8c8130, SetClip 0x8c8140, SetColors 0x8c8150, 0x8c84e0 0x8c84f0 0x8c8500 0x8c8510, SetLineWidth
    // 0x8c8520, SetGradient 0x8c8530, SetTexture 0x8c81b0, ClearTextureClip 0x8c84b0, SetTextureClip 0x8c84c0, 0x8c8620
    // 0x8c8630 0x8c8640, LockSurface 0x8c8650, UnlockSurface 0x8c8690, ChooseRoute 0x8c86a0, DrawSpans 0x8c8750), the
    // helpers UpdateLock 0x8c8b30 and FillPolygon 0x8c8c00, the constructor 0x8c7dc0 and the destructor 0x8c7e10 (the
    // last Release); through them the span renderers SetTarget makes (their slots 1 to 4, TiledSpans 0x8c92d0,
    // ScaledSpans 0x8c93d0, GrowTexelSpans 0x8c94d0 and the deleting destructor 0x8c92b0 / destructor 0x8c9200).
    char seedText[16] = {};
    const uint32_t only = GetEnvironmentVariableA("TS2_TEST_SEED", seedText, sizeof(seedText)) ? uint32_t(atoi(seedText)) : 0;
    for (uint32_t seed = only ? only : 1; seed <= (only ? only : 3000); ++seed) {
        NewCase(seed);
        ctx.Mark("orig");
        auto a = drawfx::Side(&BuildWorld, [&] { RunSide(false, seed); });
        NewCase(seed);
        ctx.Mark("ours");
        auto b = drawfx::Side(&BuildWorld, [&] { RunSide(true, seed); });
        ++ctx.cases;
        if (!drawfx::Same(ctx, a.first, b.first, "render to buffer", seed, a.second)) break;
    }
}

DIFF_TEST(GZDrawRenderToBuffer_Construct, 0x8c7dc0)
{
    // The constructor on seeded bytes: every byte it writes and every byte it leaves, but the vtable word (the game's on
    // the original side, ours on ours).
    const auto original = ctx.Original<void *(__thiscall *)(void *)>(kConstructor);
    alignas(8) uint8_t a[cGZDrawContextRenderToBuffer::kSize], b[cGZDrawContextRenderToBuffer::kSize];
    for (uint32_t seed = 0; seed < 200; ++seed) {
        std::mt19937 rng(seed);
        for (auto &x : a) x = uint8_t(rng());
        std::memcpy(b, a, sizeof(a));
        void *const ra = original(a);
        void *const rb = ::new (static_cast<void *>(b)) cGZDrawContextRenderToBuffer;
        ++ctx.cases;
        if (ra != a || rb != static_cast<void *>(b)) ctx.Fail("seed %u: the constructor returned another address", seed);
        if (Word(a, 0) != drawfx::P(GameAddress<void>(kGameVTable))) ctx.Fail("seed %u: the original's vtable", seed);
        if (Word(b, 0) == drawfx::P(GameAddress<void>(kGameVTable))) ctx.Fail("seed %u: ours has the game's vtable", seed);
        std::memset(a, 0, 4);
        std::memset(b, 0, 4);
        for (uint32_t off = 0; off < sizeof(a); ++off)
            if (a[off] != b[off]) {
                ctx.Fail("seed %u: byte +%#x orig %#x ours %#x", seed, off, a[off], b[off]);
                return;
            }
    }
}
