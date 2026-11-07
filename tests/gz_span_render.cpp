// Differential tests for the software span renderers (src/gz/nGZDrawSoftRender.h; docs/subsystems/rendering.md, R5b).
//
// GZSpanRender_Slots: every format's slots 1 to 4 (Fill, FillGradient, Blend, BlendGradient), the original's functions
// called on an object carrying the game's vtable (cSpanRender8's Blend and BlendGradient call slots 1 and 2 through it),
// ours through our vtable, on the same random pixels (rows with padding past the width, all compared) and random spans
// (some 8 pixels or longer: the 16-bit blends take an SSE path for those when the CPU level global 0x10df81c is above
// 1; every case runs with it at 0 and at 2). Gradients both as SetGradient makes them and as random words.
// GZSpanRender_TexelSpans: TiledSpans 0x8c92d0 and ScaledSpans 0x8c93d0 (the texel span lists, every word compared) and
// the deleting destructor 0x8c92b0 with the destructor 0x8c9200 it runs (the two texel span vectors freed; what they
// free, by AllocationCounter); then the texture slot through our vtable (ours is nGZDrawSoftTexture.cpp now; 0x901ba0 / 0x8d9930 / 0x8e9230 / 0x8c9c60 /
// 0x8f6110 are the originals, compared exhaustively in gz_span_texture.cpp) against the original's
// on the same spans, every pixel compared.
#include "difftest.h"
#include "gz/nGZDrawSoftRender.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

using namespace nGZDrawSoftRender;

namespace {

constexpr uint32_t kCpuLevel = 0x10df81c;

// One renderer class: its game vtable and slot functions, our factory, and its pixel size.
struct Kind {
    const char *name;
    uint32_t vtable;
    uint32_t slots[5]; // Fill, FillGradient, Blend, BlendGradient, DrawTexture
    uint32_t bytesPerPixel;
    uint32_t format; // the destination format it is made for
    cSpanRenderBase *(*make)();
};

const Kind kKinds[] = {
    {"8", 0xe4f4c8, {0x901a90, 0x901ae0, 0x901b70, 0x901b90, 0x901ba0}, 1, 2, [] { return static_cast<cSpanRenderBase *>(new cSpanRender8); }},
    {"1555", 0xe4f474, {0x8d9340, 0x8d9390, 0x8d94d0, 0x8d9730, 0x8d9930}, 2, 6, [] { return static_cast<cSpanRenderBase *>(new cSpanRender1555); }},
    {"565", 0xe4f490, {0x8e8c70, 0x8e8cc0, 0x8e8e00, 0x8e9030, 0x8e9230}, 2, 5, [] { return static_cast<cSpanRenderBase *>(new cSpanRender565); }},
    {"4444", 0xe4f458, {0x8c9680, 0x8c96d0, 0x8c9810, 0x8c9a70, 0x8c9c60}, 2, 7, [] { return static_cast<cSpanRenderBase *>(new cSpanRender4444); }},
    {"8888", 0xe4f4ac, {0x8f5cf0, 0x8f5d40, 0x8f5e70, 0x8f5f30, 0x8f6110}, 4, 8, [] { return static_cast<cSpanRenderBase *>(new cSpanRender8888); }},
};

// The game's object layout: the vtable and two empty vectors (the slots under test don't read them).
struct GameRenderer {
    const void *vtable;
    uint32_t vectors[6];
};

using FillFn = void(__thiscall *)(void *, const uint32_t *, uint32_t, const cGZDrawSurface *, uint32_t);
using GradientFn = void(__thiscall *)(void *, const uint32_t *, uint32_t, const cGZDrawSurface *, const Gradient *);
using BlendFn = void(__thiscall *)(void *, const uint32_t *, uint32_t, const cGZDrawSurface *, uint32_t, uint32_t);
using TexelFn = TexelSpan *(__thiscall *)(void *, const uint32_t *, uint32_t, const cGZDrawSurface *, const cGZDrawSurface *,
                                          const TextureParams *, uint32_t);
using TextureFn = void(__thiscall *)(void *, const void *, uint32_t, const cGZDrawSurface *, const cGZDrawSurface *,
                                     const TextureParams *, uint32_t);
using DeletingFn = void *(__thiscall *)(void *, uint32_t);

// A pixel buffer with rows padded past the width (the padding is compared too: nothing may write it).
struct Pixels {
    uint32_t width = 0, height = 0, stride = 0, bytesPerPixel = 0;
    std::vector<uint8_t> bytes;
    cGZDrawSurface Surface(uint32_t format)
    {
        return cGZDrawSurface{bytes.data(), static_cast<int32_t>(stride), width, height, format, bytesPerPixel};
    }
};

Pixels RandomPixels(std::mt19937 &rng, uint32_t bytesPerPixel)
{
    Pixels p;
    p.width = 1 + rng() % 48;
    p.height = 1 + rng() % 12;
    p.bytesPerPixel = bytesPerPixel;
    p.stride = p.width * bytesPerPixel + (rng() % 4) * 4;
    p.bytes.resize(static_cast<size_t>(p.stride) * p.height);
    for (auto &b : p.bytes) b = static_cast<uint8_t>(rng());
    return p;
}

// `count` spans inside the buffer, some empty, many 8 pixels or longer.
std::vector<uint32_t> RandomSpans(std::mt19937 &rng, const Pixels &p, uint32_t count)
{
    std::vector<uint32_t> spans;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t y = rng() % p.height;
        const uint32_t x0 = rng() % (p.width + 1);
        uint32_t length = rng() % (p.width - x0 + 1);
        if (rng() % 2) length = p.width - x0; // long spans for the SIMD paths
        spans.push_back(y << 16 | x0);
        spans.push_back(y << 16 | (x0 + length));
    }
    return spans;
}

int32_t Round(float v) { return static_cast<int32_t>(v < 0 ? v - 0.5f : v + 0.5f); }

// A gradient as SetGradient makes them (channels 0..255 in 16.16, small steps), or random words.
Gradient RandomGradient(std::mt19937 &rng)
{
    Gradient g;
    std::uniform_real_distribution<float> unit(0.0f, 1.0f), step(-0.05f, 0.05f);
    const int style = rng() % 4;
    if (style == 3) {
        // Alpha starting just below a step of the 16-bit blends' alpha (A * 0x84 >> 26 for 1555 / 565, A * 0x88 >> 27
        // for 4444): flat, so the steps alone decide whether a pixel crosses it.
        g = RandomGradient(rng);
        const uint32_t mul = rng() % 2 ? 0x84 : 0x88, shift = mul == 0x84 ? 26 : 27;
        const uint32_t low = static_cast<uint32_t>(std::countr_zero(mul)), odd = mul >> low;
        uint32_t inverse = odd; // odd * inverse == 1 (mod 2^32), by Newton's iteration
        for (int k = 0; k < 5; ++k) inverse *= 2 - odd * inverse;
        const uint32_t level = rng() % 33;
        g.base[3] = static_cast<int32_t>(((level << (shift - low)) - 1) * inverse); // * mul == level << shift, less 2^low
        g.dx[3] = rng() % 2 ? 0 : static_cast<int32_t>(rng() % 3);
        g.dy[3] = 0;
        return g;
    }
    for (int i = 0; i < 4; ++i) {
        if (style == 0) {
            g.base[i] = static_cast<int32_t>(rng());
            g.dx[i] = static_cast<int32_t>(rng());
            g.dy[i] = static_cast<int32_t>(rng());
        } else {
            g.dx[i] = Round(step(rng) * 16711680.0f);
            g.dy[i] = Round(step(rng) * 16711680.0f);
            g.base[i] = Round(unit(rng) * 16711680.0f) + 0x8000;
            if (style == 2) { // flat in places, fully opaque or clear alpha
                if (rng() % 2) g.dx[i] = 0;
                if (i == 3) g.base[i] = rng() % 2 ? 0xff8000 : 0x8000;
            }
        }
    }
    return g;
}

bool SamePixels(difftest::Context &ctx, const char *what, const Kind &kind, int level, uint32_t seed, const Pixels &a,
                const Pixels &b)
{
    ++ctx.cases;
    for (size_t i = 0; i < a.bytes.size(); ++i) {
        if (a.bytes[i] != b.bytes[i]) {
            ctx.Fail("%s %s (cpu level %d, seed %u): byte %u (row %u) original %02x, ours %02x", kind.name, what, level,
                     seed, static_cast<unsigned>(i), static_cast<unsigned>(i / a.stride), a.bytes[i], b.bytes[i]);
            return false;
        }
    }
    return true;
}

} // namespace

DIFF_TEST(GZSpanRender_Slots, 0x8d94d0)
{
    int32_t &level = *reinterpret_cast<int32_t *>(ctx.image.At(kCpuLevel));
    const int32_t savedLevel = level;
    for (const Kind &kind : kKinds) {
        GameRenderer game{ctx.image.At(kind.vtable), {}};
        std::unique_ptr<cSpanRenderBase> ours(kind.make());
        for (uint32_t seed = 0; seed < 1500; ++seed) {
            for (int cpu : {0, 2}) {
                level = cpu;
                std::mt19937 rng(seed * 7919u + kind.vtable);
                const Pixels start = RandomPixels(rng, kind.bytesPerPixel);
                const std::vector<uint32_t> spans = RandomSpans(rng, start, 1 + rng() % 6);
                const uint32_t count = static_cast<uint32_t>(spans.size() / 2);
                const uint32_t value = rng() % 3 == 0 ? rng() : rng() & (kind.bytesPerPixel == 4 ? 0xffffffffu : 0xffffu);
                uint32_t argb = rng();
                if (rng() % 4 == 0) argb = (rng() % 2 ? 0xff000000u : 0u) | (argb & 0xffffff);
                const Gradient gradient = RandomGradient(rng);
                const int slot = static_cast<int>(rng() % 4);
                Pixels a = start, b = start;
                const cGZDrawSurface da = a.Surface(kind.format), db = b.Surface(kind.format);
                switch (slot) {
                case 0:
                    ctx.Original<FillFn>(kind.slots[0])(&game, spans.data(), count, &da, value);
                    ours->Fill(spans.data(), count, &db, value);
                    break;
                case 1:
                    ctx.Original<GradientFn>(kind.slots[1])(&game, spans.data(), count, &da, &gradient);
                    ours->FillGradient(spans.data(), count, &db, &gradient);
                    break;
                case 2:
                    ctx.Original<BlendFn>(kind.slots[2])(&game, spans.data(), count, &da, value, argb);
                    ours->Blend(spans.data(), count, &db, value, argb);
                    break;
                default:
                    ctx.Original<GradientFn>(kind.slots[3])(&game, spans.data(), count, &da, &gradient);
                    ours->BlendGradient(spans.data(), count, &db, &gradient);
                    break;
                }
                static const char *const kNames[] = {"Fill", "FillGradient", "Blend", "BlendGradient"};
                if (!SamePixels(ctx, kNames[slot], kind, cpu, seed, a, b)) break;
            }
        }
    }
    level = savedLevel;
}

DIFF_TEST(GZSpanRender_TexelSpans, 0x8c92d0)
{
    // The texel span helpers 0x8c92d0 / 0x8c93d0 and the base's deleting destructor 0x8c92b0.
    constexpr uint32_t kTiled = 0x8c92d0, kScaled = 0x8c93d0, kDeleting = 0x8c92b0;
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        std::mt19937 rng(seed + 0x8c93d0);
        const Kind &kind = kKinds[seed % 5];
        Pixels dest = RandomPixels(rng, kind.bytesPerPixel);
        Pixels texture = RandomPixels(rng, 1 + rng() % 4);
        const cGZDrawSurface d = dest.Surface(kind.format), t = texture.Surface(2);
        TextureParams params{};
        params.offsetX = static_cast<int32_t>(rng() % 3 ? rng() % 0x10000 : rng());
        params.offsetY = static_cast<int32_t>(rng() % 3 ? rng() % 0x10000 : rng());
        params.stepX = static_cast<int32_t>(rng() % 0x30000);
        params.stepY = static_cast<int32_t>(rng() % 0x30000);
        const uint32_t calls = 1 + rng() % 3; // the vectors reused, grown and shrunk
        std::vector<std::vector<uint32_t>> lists;
        for (uint32_t i = 0; i < calls; ++i) lists.push_back(RandomSpans(rng, dest, 1 + rng() % 40));
        const uint32_t bpp = d.bytesPerPixel;

        std::vector<TexelSpan> fromA, fromB;
        difftest::AllocationCounter ca;
        void *game = ::operator new(sizeof(GameRenderer));
        *static_cast<GameRenderer *>(game) = GameRenderer{ctx.image.At(kind.vtable), {}};
        for (const auto &spans : lists) {
            const uint32_t count = static_cast<uint32_t>(spans.size() / 2);
            const TexelSpan *tiled = ctx.Original<TexelFn>(kTiled)(game, spans.data(), count, &d, &t, &params, bpp);
            fromA.insert(fromA.end(), tiled, tiled + count);
            const TexelSpan *scaled = ctx.Original<TexelFn>(kScaled)(game, spans.data(), count, &d, &t, &params, bpp);
            fromA.insert(fromA.end(), scaled, scaled + count);
        }
        ctx.Original<DeletingFn>(kDeleting)(game, 1);
        ca.Stop();

        difftest::AllocationCounter cb;
        cSpanRenderBase *ours = kind.make();
        for (const auto &spans : lists) {
            const uint32_t count = static_cast<uint32_t>(spans.size() / 2);
            const TexelSpan *tiled = ours->TiledSpans(spans.data(), count, &d, &t, &params, bpp);
            fromB.insert(fromB.end(), tiled, tiled + count);
            const TexelSpan *scaled = ours->ScaledSpans(spans.data(), count, &d, &t, &params, bpp);
            fromB.insert(fromB.end(), scaled, scaled + count);
        }
        delete ours;
        cb.Stop();

        ++ctx.cases;
        if (fromA.size() != fromB.size() || std::memcmp(fromA.data(), fromB.data(), fromA.size() * sizeof(TexelSpan)) != 0) {
            for (size_t i = 0; i < fromA.size() && i < fromB.size(); ++i) {
                if (std::memcmp(&fromA[i], &fromB[i], sizeof(TexelSpan)) != 0) {
                    ctx.Fail("seed %u: texel span %u: original %p %p %08x %08x, ours %p %p %08x %08x", seed,
                             static_cast<unsigned>(i), fromA[i].dest, fromA[i].row, fromA[i].a, fromA[i].b, fromB[i].dest,
                             fromB[i].row, fromB[i].a, fromB[i].b);
                    return;
                }
            }
            ctx.Fail("seed %u: %u texel spans, ours %u", seed, static_cast<unsigned>(fromA.size()),
                     static_cast<unsigned>(fromB.size()));
            return;
        }
        if (ca.Blocks() != cb.Blocks() || ca.Bytes() != cb.Bytes()) {
            ctx.Fail("seed %u: heap after the renderer's life: original %lld blocks %lld bytes, ours %lld / %lld", seed,
                     ca.Blocks(), ca.Bytes(), cb.Blocks(), cb.Bytes());
            return;
        }
    }

    // The texture slot through our vtable against the original, on spans from the helpers above.
    for (uint32_t seed = 0; seed < 1000; ++seed) {
        std::mt19937 rng(seed + 0x901ba0);
        const Kind &kind = kKinds[seed % 5];
        const Pixels start = RandomPixels(rng, kind.bytesPerPixel);
        static const uint32_t kTextureFormats[] = {7, 6, 4, 5, 9, 8, 2};
        const uint32_t formatIndex = rng() % 7;
        const uint32_t textureBytes[] = {2, 2, 2, 2, 4, 4, 1};
        Pixels texture = RandomPixels(rng, textureBytes[formatIndex]);
        texture.width = 48, texture.height = 12; // as large as any span reaches
        texture.stride = 48 * texture.bytesPerPixel;
        texture.bytes.assign(static_cast<size_t>(texture.stride) * texture.height, 0);
        for (auto &byte : texture.bytes) byte = static_cast<uint8_t>(rng());
        const cGZDrawSurface t = texture.Surface(kTextureFormats[formatIndex]);
        const std::vector<uint32_t> spans = RandomSpans(rng, start, 1 + rng() % 6);
        const uint32_t count = static_cast<uint32_t>(spans.size() / 2);
        TextureParams params{};
        params.formatIndex = formatIndex;
        params.color = rng();
        params.key = rng() & 0xffff;
        params.hasKey = static_cast<uint8_t>(rng() % 2);
        const uint32_t mode = rng() % 5;
        GameRenderer game{ctx.image.At(kind.vtable), {}};
        std::unique_ptr<cSpanRenderBase> ours(kind.make());
        Pixels a = start, b = start;
        const cGZDrawSurface da = a.Surface(kind.format), db = b.Surface(kind.format);
        ctx.Original<TextureFn>(kind.slots[4])(&game, spans.data(), count, &da, &t, &params, mode);
        ours->DrawTexture(spans.data(), count, &db, &t, &params, mode);
        if (!SamePixels(ctx, "DrawTexture", kind, 0, seed, a, b)) return;
    }
}
