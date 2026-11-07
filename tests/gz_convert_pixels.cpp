// Differential tests for the pixel converter 0x8c6b10 and its 62 per-format loops 0x8c38e0 .. 0x8c5610
// (src/gz/GZConvertPixels.cpp; docs/subsystems/rendering.md, R5b step 3).
//
// GZConvert_Loops: every loop (kGZConvertLoops, by original address) through the original's code and ours on the same
// argument block: random rectangles (widths 1..13, heights 1..5, strides padded, tight, overlapping or negative), a
// source and destination in separate buffers or in one with the two ranges overlapping, keys taken from a source pixel or
// random, garbage in the block's unused words; every byte of the buffers compared. Which formats a loop serves, and its
// pixel sizes, are read from the original's table at 0xe4f0d8, not from ours.
// GZConvert_Exhaustive: the 16-bit sources over every value, the 32-bit sources over every 24-bit colour (with a varying
// alpha byte) and over every alpha byte; keyed with the pixel's own colour, a neighbour's and random keys.
// GZConvert_Pixels: the converter itself over every pair of formats 0..11 (the original answers false outside 4..9), with
// and without a key, rectangles as above, both in separate buffers and overlapping in one (the overlap rule: bottom-up with
// both strides negated); results and bytes, and the same tables agree entry by entry.
// Each at CPU level 0 and 2 (the loops do not read it; the harness runs the original under both anyway).
//
// The 62 loops, each compared against the original at its own address by GZConvert_Loops and GZConvert_Exhaustive (the
// table is read from the image and every address in kGZConvertLoops is called): 0x8c38e0 0x8c3970 0x8c39c0 0x8c3a30
// 0x8c3a80 0x8c3b00 0x8c3b40 0x8c3ba0 0x8c3bf0 0x8c3c70 0x8c3cd0 0x8c3d30 0x8c3da0 0x8c3e20 0x8c3e70 0x8c3ed0 0x8c3f50
// 0x8c3fe0 0x8c4050 0x8c40d0 0x8c4150 0x8c41e0 0x8c4260 0x8c42e0 0x8c4360 0x8c43e0 0x8c4460 0x8c4570 0x8c45d0 0x8c4640
// 0x8c46c0 0x8c4740 0x8c47d0 0x8c4850 0x8c48a0 0x8c4930 0x8c4990 0x8c4a20 0x8c4a90 0x8c4b30 0x8c4bb0 0x8c4c20 0x8c4ca0
// 0x8c4d30 0x8c4dc0 0x8c4e10 0x8c4e80 0x8c4ed0 0x8c4f60 0x8c4fe0 0x8c5060 0x8c50f0 0x8c5170 0x8c5200 0x8c52a0 0x8c52f0
// 0x8c5370 0x8c53e0 0x8c5490 0x8c5500 0x8c5580 0x8c5610.
#include "difftest.h"
#include "gz/GZConvertPixels.h"
#include "gz/cGZBuffer.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <random>
#include <vector>

namespace {

constexpr uint32_t kCpuLevel = 0x10df81c;
constexpr uint32_t kTable = 0xe4f0d8;
constexpr int32_t kRegion = 2048; // bytes a rectangle may use

uint32_t BytesOf(uint32_t format)
{
    if (format >= 4 && format <= 7) return 2;
    if (format == 8 || format == 9) return 4;
    return 0;
}

struct LoopInfo {
    uint32_t srcFormat = 0, dstFormat = 0;
};

// The original's table: which (destination, source) pair each loop serves (the first, when several share it).
std::map<uint32_t, LoopInfo> ReadTable(difftest::Context &ctx)
{
    std::map<uint32_t, LoopInfo> loops;
    const uint8_t *base = ctx.image.At<uint8_t>(0);
    const uint32_t *table = ctx.image.At<uint32_t>(kTable);
    for (uint32_t to = 0; to < 6; ++to)
        for (uint32_t from = 0; from < 6; ++from)
            for (uint32_t keyed = 0; keyed < 2; ++keyed) {
                const uint32_t rva = uint32_t(reinterpret_cast<const uint8_t *>(uintptr_t(table[to * 12 + from * 2 + keyed])) - base);
                if (!loops.count(rva)) loops[rva] = {from + 4, to + 4};
            }
    return loops;
}

struct Region {
    int32_t offset;
    int32_t stride;
};

// A random rectangle inside [0, kRegion): where its first row starts and its stride, every row inside.
bool Place(std::mt19937 &rng, int32_t w, int32_t h, int32_t bytes, Region *out, bool useHint, int32_t hintOffset)
{
    for (int attempt = 0; attempt < 50; ++attempt) {
        int32_t stride;
        switch (rng() % 6) {
        case 0: stride = w * bytes; break;                         // tight
        case 1: stride = w * bytes + int32_t(rng() % 17); break;   // padded
        case 2: stride = -(w * bytes + int32_t(rng() % 17)); break; // upside down
        case 3: stride = int32_t(rng() % 25) - 12; break;          // rows overlapping each other
        case 4: stride = 0; break;                                 // one row over and over
        default: stride = (int32_t(rng() % 9) - 4) * bytes; break; // whole pixels
        }
        const int32_t reach = (h - 1) * stride;
        const int32_t low = reach < 0 ? reach : 0, high = (reach > 0 ? reach : 0) + w * bytes - 1;
        const int32_t lo = -low, hi = kRegion - 1 - high;
        if (lo > hi) continue;
        int32_t offset = lo + int32_t(rng() % uint32_t(hi - lo + 1));
        if (useHint) {
            const int32_t near = hintOffset + int32_t(rng() % 41) - 20;
            if (near >= lo && near <= hi) offset = near;
        }
        out->offset = offset;
        out->stride = stride;
        return true;
    }
    return false;
}

void Fill(std::mt19937 &rng, std::vector<uint8_t> &bytes)
{
    // Random bytes, with repeated pixels in some buffers so a key often matches.
    for (auto &b : bytes) b = uint8_t(rng());
    if (rng() % 3 == 0) {
        const uint32_t pixel = rng();
        for (size_t i = 0; i + 4 <= bytes.size(); i += 4 * (1 + rng() % 5)) std::memcpy(&bytes[i], &pixel, 4);
    }
}

uint32_t PickKey(std::mt19937 &rng, const std::vector<uint8_t> &buffer, int32_t srcOffset, uint32_t srcBytes, uint32_t mask)
{
    uint32_t pixel = 0;
    switch (rng() % 4) {
    case 0:
    case 1: // a pixel of the source's first row
        std::memcpy(&pixel, &buffer[size_t(srcOffset)], srcBytes);
        return pixel & mask;
    case 2: return rng() & mask;
    default: return rng();
    }
}

struct Level {
    int32_t &cell;
    int32_t saved;
    explicit Level(difftest::Context &ctx) : cell(*reinterpret_cast<int32_t *>(ctx.image.At(kCpuLevel))), saved(cell) {}
    ~Level() { cell = saved; }
};

} // namespace

DIFF_TEST(GZConvert_Loops, 0x8c3e20)
{
    Level level(ctx);
    const std::map<uint32_t, LoopInfo> info = ReadTable(ctx);
    if (info.size() != kGZConvertLoopCount) {
        ctx.Fail("the original's table holds %zu loops, ours %zu", info.size(), kGZConvertLoopCount);
        return;
    }
    std::mt19937 rng(0x8c38e0);
    for (int cpu : {0, 2}) {
        level.cell = cpu;
        for (size_t index = 0; index < kGZConvertLoopCount; ++index) {
            const GZConvertLoop &loop = kGZConvertLoops[index];
            const auto found = info.find(loop.rva);
            if (found == info.end()) {
                ctx.Fail("loop %x is not in the original's table", loop.rva);
                return;
            }
            const uint32_t srcBytes = BytesOf(found->second.srcFormat), dstBytes = BytesOf(found->second.dstFormat);
            const uint32_t mask = cGZBuffer::FormatMask(found->second.srcFormat);
            const GZConvertLoopFn original = ctx.Original<GZConvertLoopFn>(loop.rva);
            for (int trial = 0; trial < 1200; ++trial) {
                const int32_t w = 1 + int32_t(rng() % 13), h = 1 + int32_t(rng() % 5);
                const bool together = rng() % 3 == 0;
                Region s, d;
                if (!Place(rng, w, h, int32_t(srcBytes), &s, false, 0)) continue;
                if (!Place(rng, w, h, int32_t(dstBytes), &d, together, s.offset)) continue;
                std::vector<uint8_t> a(together ? kRegion : 2 * kRegion);
                Fill(rng, a);
                std::vector<uint8_t> b = a;
                const int32_t dstBase = together ? 0 : kRegion;
                const bool keyed = (rng() & 1) != 0;
                // Both runs get the same block: addresses into their own buffer.
                GZConvertArgs block = {};
                block.width = block.width2 = block.width3 = w;
                block.height = block.height2 = block.height3 = h;
                block.dstStride = d.stride;
                block.srcStride = s.stride;
                block.dstFormat = found->second.dstFormat;
                block.srcFormat = found->second.srcFormat;
                block.dstBytes = dstBytes;
                block.srcBytes = srcBytes;
                for (uint32_t &word : block.unused) word = rng();
                block.hasKey = keyed ? 1 : 0;
                block.key = keyed ? PickKey(rng, a, s.offset, srcBytes, mask) : rng();
                GZConvertArgs blockA = block, blockB = block;
                blockA.src = a.data() + s.offset;
                blockA.dst = a.data() + dstBase + d.offset;
                blockB.src = b.data() + s.offset;
                blockB.dst = b.data() + dstBase + d.offset;
                original(blockA);
                loop.fn(blockB);
                ++ctx.cases;
                if (a != b) {
                    size_t at = 0;
                    while (a[at] == b[at]) ++at;
                    ctx.Fail("loop %x (%u <- %u, %s, cpu %d): %dx%d, dst stride %d src stride %d, %s: byte %zu original %02x ours %02x",
                             loop.rva, found->second.dstFormat, found->second.srcFormat, keyed ? "keyed" : "plain", cpu, w, h,
                             d.stride, s.stride, together ? "one buffer" : "two buffers", at, a[at], b[at]);
                    return;
                }
            }
        }
    }
}

namespace {

// Runs one loop's original and ours over `count` source pixels and compares the destinations.
bool Sweep(difftest::Context &ctx, const GZConvertLoop &loop, const LoopInfo &info, const GZConvertLoopFn original,
           const std::vector<uint8_t> &src, const std::vector<uint8_t> &dstInit, uint32_t count, uint32_t key, bool keyed,
           const char *what)
{
    std::vector<uint8_t> a = dstInit, b = dstInit;
    GZConvertArgs block = {};
    block.width = block.width2 = block.width3 = int32_t(count);
    block.height = block.height2 = block.height3 = 1;
    block.dstFormat = info.dstFormat;
    block.srcFormat = info.srcFormat;
    block.dstBytes = BytesOf(info.dstFormat);
    block.srcBytes = BytesOf(info.srcFormat);
    block.src = src.data();
    block.hasKey = keyed ? 1 : 0;
    block.key = key;
    GZConvertArgs blockA = block, blockB = block;
    blockA.dst = a.data();
    blockB.dst = b.data();
    original(blockA);
    loop.fn(blockB);
    ++ctx.cases;
    if (a == b) return true;
    size_t at = 0;
    while (a[at] == b[at]) ++at;
    const uint32_t bytes = BytesOf(info.srcFormat);
    const size_t pixel = at / BytesOf(info.dstFormat);
    uint32_t value = 0;
    std::memcpy(&value, &src[pixel * bytes], bytes);
    ctx.Fail("loop %x (%u <- %u, %s, key %x, %s): pixel %zu (source %x) byte %zu original %02x ours %02x", loop.rva,
             info.dstFormat, info.srcFormat, keyed ? "keyed" : "plain", key, what, pixel, value, at, a[at], b[at]);
    return false;
}

} // namespace

DIFF_TEST(GZConvert_Exhaustive, 0x8c3e20)
{
    Level level(ctx);
    const std::map<uint32_t, LoopInfo> info = ReadTable(ctx);
    std::mt19937 rng(0x8c4dc0);
    for (size_t index = 0; index < kGZConvertLoopCount; ++index) {
        const GZConvertLoop &loop = kGZConvertLoops[index];
        const LoopInfo &li = info.at(loop.rva);
        const uint32_t srcBytes = BytesOf(li.srcFormat), dstBytes = BytesOf(li.dstFormat);
        const uint32_t mask = cGZBuffer::FormatMask(li.srcFormat);
        const GZConvertLoopFn original = ctx.Original<GZConvertLoopFn>(loop.rva);
        // The keyed loops: a handful of keys (a colour that occurs, 0, the mask, random ones) over the whole sweep.
        const uint32_t keys[] = {0, mask, rng() & mask, rng() & mask, rng() & mask, 0x5a5a5a5a & mask};
        const bool keyedLoop = [&] {
            // A loop serves keyed calls when the table's keyed slot for its pair names it.
            const uint32_t *table = ctx.image.At<uint32_t>(kTable);
            const uint8_t *base = ctx.image.At<uint8_t>(0);
            const uint32_t to = li.dstFormat - 4, from = li.srcFormat - 4;
            return uint32_t(reinterpret_cast<const uint8_t *>(uintptr_t(table[to * 12 + from * 2 + 1])) - base) == loop.rva;
        }();
        const uint32_t keyCount = keyedLoop ? sizeof(keys) / sizeof(keys[0]) : 1;
        if (srcBytes == 2) {
            // Every 16-bit value as a pixel.
            std::vector<uint8_t> src(65536 * 2);
            for (uint32_t v = 0; v < 65536; ++v) {
                const uint16_t t = uint16_t(v);
                std::memcpy(&src[v * 2], &t, 2);
            }
            std::vector<uint8_t> dst(65536 * dstBytes);
            for (auto &x : dst) x = uint8_t(rng());
            for (uint32_t k = 0; k < keyCount; ++k)
                if (!Sweep(ctx, loop, li, original, src, dst, 65536, keys[k], keyedLoop, "all 16-bit values")) return;
            // Keys that occur: every value of the colour bits as the key over a short row.
            if (keyedLoop) {
                for (uint32_t v = 0; v < 65536; v += 257) {
                    if (!Sweep(ctx, loop, li, original, src, dst, 65536, v & mask, true, "keys sweep")) return;
                }
            }
        } else {
            // 32-bit pixels: every 24-bit colour (the alpha byte from a hash of it), then every alpha byte, 65536 at a time.
            std::vector<uint8_t> src(65536 * 4);
            std::vector<uint8_t> dst(65536 * dstBytes);
            for (auto &x : dst) x = uint8_t(rng());
            for (uint32_t chunk = 0; chunk < 256; ++chunk) {
                for (uint32_t i = 0; i < 65536; ++i) {
                    const uint32_t colour = chunk << 16 | i;
                    const uint32_t alpha = (colour * 2654435761u) >> 24;
                    const uint32_t pixel = alpha << 24 | colour;
                    std::memcpy(&src[i * 4], &pixel, 4);
                }
                if (!Sweep(ctx, loop, li, original, src, dst, 65536, keyedLoop ? (chunk << 16 | (rng() & 0xffff)) : 0, keyedLoop,
                           "24-bit colours"))
                    return;
            }
            for (uint32_t pass = 0; pass < 64; ++pass) {
                const uint32_t colour = rng() & 0xffffff;
                for (uint32_t i = 0; i < 65536; ++i) {
                    const uint32_t pixel = (i & 0xff) << 24 | ((i >> 8) & 1 ? colour : (rng() & 0xffffff));
                    std::memcpy(&src[i * 4], &pixel, 4);
                }
                if (!Sweep(ctx, loop, li, original, src, dst, 65536, keyedLoop ? colour : 0, keyedLoop, "alpha bytes")) return;
            }
        }
    }
}

DIFF_TEST(GZConvert_Pixels, 0x8c6b10)
{
    Level level(ctx);
    using Fn = bool(__cdecl *)(uint8_t *, int32_t, uint32_t, const uint8_t *, int32_t, uint32_t, int32_t, int32_t, const uint32_t *);
    const Fn original = ctx.Original<Fn>(0x8c6b10);
    std::mt19937 rng(0x8c6b10);
    for (int cpu : {0, 2}) {
        level.cell = cpu;
        for (uint32_t to = 0; to < 12; ++to)
            for (uint32_t from = 0; from < 12; ++from) {
                const bool known = BytesOf(to) != 0 && BytesOf(from) != 0;
                for (int trial = 0; trial < (known ? 2500 : 20); ++trial) {
                    const uint32_t srcBytes = BytesOf(from) ? BytesOf(from) : 2, dstBytes = BytesOf(to) ? BytesOf(to) : 2;
                    const int32_t w = 1 + int32_t(rng() % 13), h = 1 + int32_t(rng() % 5);
                    const bool together = rng() % 2 == 0;
                    Region s, d;
                    if (!Place(rng, w, h, int32_t(srcBytes), &s, false, 0)) continue;
                    if (!Place(rng, w, h, int32_t(dstBytes), &d, together, s.offset)) continue;
                    std::vector<uint8_t> a(together ? kRegion : 2 * kRegion);
                    Fill(rng, a);
                    std::vector<uint8_t> b = a;
                    const int32_t dstBase = together ? 0 : kRegion;
                    const bool keyed = (rng() & 1) != 0;
                    uint32_t key = 0;
                    if (keyed) {
                        key = rng() % 3 ? PickKey(rng, a, s.offset, srcBytes, 0xffffffffu) : rng();
                        // The raw pixel (alpha and all): the converter masks it itself.
                    }
                    const uint32_t keyA = key, keyB = key;
                    const bool ra = original(a.data() + dstBase + d.offset, d.stride, to, a.data() + s.offset, s.stride, from, w,
                                             h, keyed ? &keyA : nullptr);
                    const bool rb = GZConvertPixels(b.data() + dstBase + d.offset, d.stride, to, b.data() + s.offset, s.stride,
                                                    from, w, h, keyed ? &keyB : nullptr);
                    ++ctx.cases;
                    if (ra != rb || a != b) {
                        size_t at = 0;
                        while (at < a.size() && a[at] == b[at]) ++at;
                        ctx.Fail("%u <- %u, %s key %08x, cpu %d, %dx%d, dst stride %d src stride %d, %s: result original %d ours %d, "
                                 "first differing byte %zu",
                                 to, from, keyed ? "keyed" : "plain", key, cpu, w, h, d.stride, s.stride,
                                 together ? "one buffer" : "two buffers", ra, rb, at);
                        return;
                    }
                }
            }
    }
}
