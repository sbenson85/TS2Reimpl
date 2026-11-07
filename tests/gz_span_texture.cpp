// Differential tests for the span renderers' texture slot (vf5: 0x901ba0 cSpanRender8, 0x8d9930 cSpanRender1555,
// 0x8e9230 cSpanRender565, 0x8c9c60 cSpanRender4444, 0x8f6110 cSpanRender8888; src/gz/nGZDrawSoftTexture.cpp;
// docs/subsystems/rendering.md, R5b step 1).
//
// GZSpanTexture_Cells: every renderer x texture format (7) x key (off, on) x mode (15: plain, scaled, tiled, each copy /
// blend / modulate / modulate+blend / premultiplied), the original's slot on an object carrying the game's vtable against
// ours through our vtable, on the same random destination, texture, spans and colour; every destination byte compared
// (rows have padding past the width), at CPU level 0 and 2. Scaled and tiled modes get their texel spans from ours
// (cSpanRenderBase::ScaledSpans / TiledSpans, verified in tests/gz_span_render.cpp).
#include "difftest.h"
#include "gz/nGZDrawSoftRender.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

using namespace nGZDrawSoftRender;

namespace {

constexpr uint32_t kCpuLevel = 0x10df81c;

struct Kind {
    const char *name;
    uint32_t vtable;
    uint32_t slot; // vf5
    uint32_t bytesPerPixel;
    uint32_t format;
    cSpanRenderBase *(*make)();
};

const Kind kKinds[] = {
    {"8", 0xe4f4c8, 0x901ba0, 1, 2, [] { return static_cast<cSpanRenderBase *>(new cSpanRender8); }},
    {"1555", 0xe4f474, 0x8d9930, 2, 6, [] { return static_cast<cSpanRenderBase *>(new cSpanRender1555); }},
    {"565", 0xe4f490, 0x8e9230, 2, 5, [] { return static_cast<cSpanRenderBase *>(new cSpanRender565); }},
    {"4444", 0xe4f458, 0x8c9c60, 2, 7, [] { return static_cast<cSpanRenderBase *>(new cSpanRender4444); }},
    {"8888", 0xe4f4ac, 0x8f6110, 4, 8, [] { return static_cast<cSpanRenderBase *>(new cSpanRender8888); }},
};

struct GameRenderer {
    const void *vtable;
    uint32_t vectors[6];
};

using TextureFn = void(__thiscall *)(void *, const void *, uint32_t, const cGZDrawSurface *, const cGZDrawSurface *,
                                     const TextureParams *, uint32_t);

constexpr uint32_t kTextureFormats[] = {7, 6, 4, 5, 9, 8, 2};
constexpr uint32_t kTextureBytes[] = {2, 2, 2, 2, 4, 4, 1};
constexpr uint32_t kTextureWidth = 128, kTextureHeight = 16;
const char *const kFormatNames[] = {"4444", "1555a", "1555", "565", "8888a", "8888", "8bit"};

struct Pixels {
    uint32_t width = 0, height = 0, stride = 0, bytesPerPixel = 0;
    std::vector<uint8_t> bytes;
    cGZDrawSurface Surface(uint32_t format)
    {
        return cGZDrawSurface{bytes.data(), static_cast<int32_t>(stride), width, height, format, bytesPerPixel};
    }
};

Pixels RandomDest(std::mt19937 &rng, uint32_t bytesPerPixel)
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

std::vector<uint32_t> RandomSpans(std::mt19937 &rng, const Pixels &p, uint32_t count)
{
    std::vector<uint32_t> spans;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t y = rng() % p.height;
        const uint32_t x0 = rng() % (p.width + 1);
        uint32_t length = rng() % (p.width - x0 + 1);
        if (rng() % 2) length = p.width - x0;
        if (length == 0) length = p.width - x0 ? 1 : 0;
        spans.push_back(y << 16 | x0);
        spans.push_back(y << 16 | (x0 + length));
    }
    return spans;
}

// A texture of random texels: 16-bit formats from the whole range, 32-bit with alpha often at its extremes.
std::vector<uint8_t> RandomTexture(std::mt19937 &rng, uint32_t formatIndex)
{
    const uint32_t bpp = kTextureBytes[formatIndex];
    std::vector<uint8_t> bytes(static_cast<size_t>(kTextureWidth) * kTextureHeight * bpp + 64);
    for (auto &b : bytes) b = static_cast<uint8_t>(rng());
    if (bpp == 4) {
        for (size_t i = 3; i < bytes.size(); i += 4) {
            switch (rng() % 4) {
            case 0: bytes[i] = 0; break;
            case 1: bytes[i] = 0xff; break;
            default: break;
            }
        }
    }
    return bytes;
}

} // namespace

DIFF_TEST(GZSpanTexture_Cells, 0x8c9c60)
{
    int32_t &level = *reinterpret_cast<int32_t *>(ctx.image.At(kCpuLevel));
    const int32_t savedLevel = level;
    const uint32_t trials = 256;
    for (const Kind &kind : kKinds) {
        GameRenderer game{ctx.image.At(kind.vtable), {}};
        std::unique_ptr<cSpanRenderBase> ours(kind.make());
        std::unique_ptr<cSpanRenderBase> helper(kind.make());
        for (uint32_t formatIndex = 0; formatIndex < 7; ++formatIndex) {
            for (uint32_t key = 0; key < 2; ++key) {
                for (uint32_t mode = 0; mode < 15; ++mode) {
                    bool failed = false;
                    for (uint32_t trial = 0; trial < trials && !failed; ++trial) {
                        level = trial & 1 ? 2 : 0;
                        std::mt19937 rng(trial * 7919u + mode * 104729u + formatIndex * 31u + key + kind.vtable);
                        const Pixels start = RandomDest(rng, kind.bytesPerPixel);
                        std::vector<uint8_t> textureBytes = RandomTexture(rng, formatIndex);
                        Pixels texture;
                        texture.width = kTextureWidth;
                        texture.height = kTextureHeight;
                        texture.bytesPerPixel = kTextureBytes[formatIndex];
                        texture.stride = kTextureWidth * texture.bytesPerPixel;
                        texture.bytes = textureBytes;
                        cGZDrawSurface t = texture.Surface(kTextureFormats[formatIndex]);

                        TextureParams params{};
                        params.formatIndex = formatIndex;
                        params.color = rng();
                        params.color = (params.color & 0xffffff) | (trial & 0xff) << 24; // every alpha once per cell
                        if (rng() % 5 == 0) params.color |= 0xffffff;
                        params.hasKey = static_cast<uint8_t>(key);
                        {
                            // The key: a texel of the texture (so it matches somewhere), masked as DrawSpans masks it.
                            const uint32_t pick = rng() % (kTextureWidth * kTextureHeight);
                            uint32_t texel = 0;
                            std::memcpy(&texel, texture.bytes.data() + pick * texture.bytesPerPixel, texture.bytesPerPixel);
                            static const uint32_t kMasks[] = {0xfff, 0x7fff, 0x7fff, 0xffff, 0xffffff, 0xffffff, 0};
                            params.key = rng() % 4 ? texel & kMasks[formatIndex] : rng() & kMasks[formatIndex];
                        }
                        const std::vector<uint32_t> spans = RandomSpans(rng, start, 1 + rng() % 6);
                        const uint32_t count = static_cast<uint32_t>(spans.size() / 2);
                        const uint32_t addressing = mode / 5;
                        const void *list = spans.data();
                        Pixels a = start, b = start;
                        const cGZDrawSurface da = a.Surface(kind.format), db = b.Surface(kind.format);
                        if (addressing == 1) {
                            params.stepX = static_cast<int32_t>(rng() % 0x10000);
                            params.stepY = static_cast<int32_t>(rng() % 0x10000);
                            params.offsetX = static_cast<int32_t>(rng() % 0x10000);
                            params.offsetY = static_cast<int32_t>(rng() % 0x10000);
                            list = helper->ScaledSpans(spans.data(), count, &da, &t, &params, kind.bytesPerPixel);
                        } else if (addressing == 2) {
                            t.width = 1 + rng() % kTextureWidth;
                            t.height = 1 + rng() % kTextureHeight;
                            params.offsetX = static_cast<int32_t>(rng() % 1000);
                            params.offsetY = static_cast<int32_t>(rng() % 1000);
                            list = helper->TiledSpans(spans.data(), count, &da, &t, &params, kind.bytesPerPixel);
                        }
                        // The helper's spans point into `a`'s memory; ours draws into `b`: rebase them.
                        std::vector<TexelSpan> rebased;
                        if (addressing != 0) {
                            const TexelSpan *src = static_cast<const TexelSpan *>(list);
                            rebased.assign(src, src + count);
                            for (auto &s : rebased) s.dest = db.bits + (s.dest - da.bits);
                        }
                        const void *listB = addressing != 0 ? static_cast<const void *>(rebased.data()) : list;
                        ctx.Original<TextureFn>(kind.slot)(&game, list, count, &da, &t, &params, mode);
                        ours->DrawTexture(listB, count, &db, &t, &params, mode);
                        ++ctx.cases;
                        for (size_t i = 0; i < a.bytes.size(); ++i) {
                            if (a.bytes[i] != b.bytes[i]) {
                                uint32_t texel = 0;
                                const uint32_t row = static_cast<uint32_t>(i / a.stride);
                                const uint32_t col = static_cast<uint32_t>(i % a.stride) / kind.bytesPerPixel;
                                if (addressing == 0)
                                    std::memcpy(&texel,
                                                texture.bytes.data() + row * texture.stride + col * texture.bytesPerPixel,
                                                texture.bytesPerPixel);
                                ctx.Fail("%s dest, %s texture, key %u, mode %u (cpu %d, trial %u): byte %u (row %u col %u) "
                                         "original %02x, ours %02x; before %02x; texel %08x color %08x key %08x",
                                         kind.name, kFormatNames[formatIndex], key, mode, level, trial,
                                         static_cast<unsigned>(i), row, col, a.bytes[i], b.bytes[i], start.bytes[i], texel,
                                         params.color, params.key);
                                failed = true;
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
    level = savedLevel;
}

// GZSpanTexture_Exhaustive: the 16-bit texture formats over every texel value (a 256 x 256 texture holding 0..65535),
// the five plain modes, key off and on (the key a texel value), a handful of colours (every alpha for the
// colour-driven modes is in GZSpanTexture_Cells), each renderer; every destination byte compared.
DIFF_TEST(GZSpanTexture_Exhaustive, 0x8c9c60)
{
    for (const Kind &kind : kKinds) {
        GameRenderer game{ctx.image.At(kind.vtable), {}};
        std::unique_ptr<cSpanRenderBase> ours(kind.make());
        for (uint32_t formatIndex = 0; formatIndex < 4; ++formatIndex) {
            std::vector<uint8_t> textureBytes(65536 * 2);
            for (uint32_t i = 0; i < 65536; ++i) {
                textureBytes[i * 2] = static_cast<uint8_t>(i);
                textureBytes[i * 2 + 1] = static_cast<uint8_t>(i >> 8);
            }
            const cGZDrawSurface t{textureBytes.data(), 512, 256, 256, kTextureFormats[formatIndex], 2};
            std::vector<uint32_t> spans;
            for (uint32_t y = 0; y < 256; ++y) {
                spans.push_back(y << 16);
                spans.push_back(y << 16 | 256);
            }
            for (uint32_t pass = 0; pass < 6; ++pass) {
                std::mt19937 rng(pass * 977u + formatIndex + kind.vtable);
                std::vector<uint8_t> start(static_cast<size_t>(256) * kind.bytesPerPixel * 256);
                for (auto &byte : start) byte = static_cast<uint8_t>(rng());
                static const uint32_t kColors[] = {0xffffffff, 0x80ffffff, 0xff000000, 0x00ffffff};
                TextureParams params{};
                params.formatIndex = formatIndex;
                params.color = pass < 4 ? kColors[pass] : static_cast<uint32_t>(rng());
                for (uint32_t key = 0; key < 2; ++key) {
                    params.hasKey = static_cast<uint8_t>(key);
                    params.key = (rng() & 0xffff) & (formatIndex == 0 ? 0xfff : formatIndex == 3 ? 0xffff : 0x7fff);
                    for (uint32_t mode = 0; mode < 5; ++mode) {
                        std::vector<uint8_t> a = start, b = start;
                        const cGZDrawSurface da{a.data(), static_cast<int32_t>(256 * kind.bytesPerPixel), 256, 256,
                                                kind.format, kind.bytesPerPixel};
                        const cGZDrawSurface db{b.data(), da.stride, 256, 256, kind.format, kind.bytesPerPixel};
                        ctx.Original<TextureFn>(kind.slot)(&game, spans.data(), 256, &da, &t, &params, mode);
                        ours->DrawTexture(spans.data(), 256, &db, &t, &params, mode);
                        ++ctx.cases;
                        for (size_t i = 0; i < a.size(); ++i) {
                            if (a[i] != b[i]) {
                                const size_t pixel = i / kind.bytesPerPixel;
                                ctx.Fail("%s dest, %s texture, key %u, mode %u, color %08x pass %u: texel %04x original "
                                         "%02x, ours %02x, before %02x",
                                         kind.name, kFormatNames[formatIndex], key, mode, params.color, pass,
                                         static_cast<unsigned>(pixel), a[i], b[i], start[i]);
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
}
