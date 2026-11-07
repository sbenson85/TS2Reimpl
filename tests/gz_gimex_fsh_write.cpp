// Differential tests for the FSH back end's encode side, the directory writer's helpers and shape assembly (lane MY;
// src/gz/GimexFshWrite.size.cpp and GimexFshWriteMip.size.cpp: 0x889917..0x88a9ad). Each function runs alone over seeded and crafted input,
// the original (by RVA, in the mapped image) against ours, the stack cleared before each call (alternately 0x00 and 0xff), comparing the
// return value, every byte written and the allocator's effect (blocks left, sizes of the blocks freed). Directories are built from the codecs'
// allocator, run on a copy for each side and compared as a whole: every entry field, every block's bytes, the name.
#define NOMINMAX
#include "difftest.h"
#include "free_recorder.h"
#include "gz/Gimex.h"
#include "gz/GimexFshBlocks.h"
#include "gz/GimexFshCluster.h"
#include "gz/GimexFshConvert.h"
#include "gz/GimexFshEncode.h"
#include "gz/GimexFshLeaves.h"
#include "gz/GimexFshWrite.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace gimex::fsh;
using gimex::Info;

namespace {

constexpr uint32_t kRowBytes = 0x8899a3;
constexpr uint32_t kPlaneBytes = 0x8899fc;
constexpr uint32_t kMipLevels = 0x88a187;
constexpr uint32_t kShapeBits = 0x88a1ed;
constexpr uint32_t kFreeWrittenDirectory = 0x88a105;
constexpr uint32_t kPadShapes = 0x889917;
constexpr uint32_t kChainBlock = 0x88a962;
constexpr uint32_t kChainEntryBlocks = 0x88a9ad;
constexpr uint32_t kSetMipLevels = 0x88a8f6;
constexpr uint32_t kTilePixels = 0x88a5b2;
constexpr uint32_t kTileBlocks = 0x88a731;
constexpr uint32_t kExpandCubeMaps = 0x88a3c9;
constexpr uint32_t kMergeMipmaps = 0x889a2d;

__declspec(noinline) void ClearStack(uint8_t value)
{
    volatile uint8_t buffer[0x4000];
    for (size_t i = 0; i < sizeof(buffer); ++i) buffer[i] = value;
}

struct Fill {
    uint32_t state;
    explicit Fill(uint32_t seed) : state(seed * 2654435761u + 0x9e3779b9u) {}
    uint32_t Next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    int32_t Range(int32_t lo, int32_t hi) { return lo + static_cast<int32_t>(Next() % static_cast<uint32_t>(hi - lo + 1)); }
    void Bytes(uint8_t *bytes, size_t count)
    {
        for (size_t i = 0; i < count; ++i) bytes[i] = static_cast<uint8_t>(Next() >> 11);
    }
    void Bytes(std::vector<uint8_t> &bytes) { Bytes(bytes.data(), bytes.size()); }
    bool Chance(int32_t percent) { return Range(0, 99) < percent; }
};

uint8_t StackByte(int iter)
{
    return (iter & 1) ? 0xff : 0x00;
}

#define CHECK(ctx, cond, ...)                                                                                                            \
    do {                                                                                                                                   \
        ++(ctx).cases;                                                                                                                     \
        if (!(cond)) (ctx).Fail(__VA_ARGS__);                                                                                              \
    } while (0)

// What a call did to the heap, as seen from the recorder and the counter.
struct HeapEffect {
    long long blocks = 0;
    size_t frees = 0;
    size_t sizes[64] = {};
    size_t sizeCount = 0;
    bool operator==(const HeapEffect &o) const
    {
        return blocks == o.blocks && frees == o.frees && sizeCount == o.sizeCount && std::memcmp(sizes, o.sizes, sizeof sizes) == 0;
    }
};

template <typename Call> HeapEffect Observe(uint8_t stack, Call call)
{
    HeapEffect effect;
    difftest::AllocationCounter counter;
    difftest::FreeRecorder recorder;
    ClearStack(stack);
    call();
    recorder.Stop();
    counter.Stop();
    effect.blocks = counter.Blocks();
    effect.frees = recorder.Count();
    effect.sizeCount = recorder.SortedSizes(effect.sizes, 64);
    return effect;
}

const int32_t kPlatforms[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, -1};

// ---- directories ----

struct EntrySpec {
    std::vector<uint8_t> shape, palette, marker, userData, pairs, text;
    std::string name;
    bool hasName = false;
    uint32_t flags = 0;
    // The recorded sizes (default: the vectors' sizes).
    int64_t shapeBytes = -1, paletteBytes = -1, markerBytes = -1, userDataBytes = -1, pairsBytes = -1, textBytes = -1;
};

uint8_t *Block(const std::vector<uint8_t> &bytes)
{
    if (bytes.empty()) return nullptr;
    uint8_t *p = static_cast<uint8_t *>(gimex::Allocate(static_cast<uint32_t>(bytes.size())));
    std::memcpy(p, bytes.data(), bytes.size());
    return p;
}

uint32_t SizeOf(int64_t recorded, const std::vector<uint8_t> &bytes)
{
    return recorded >= 0 ? static_cast<uint32_t>(recorded) : static_cast<uint32_t>(bytes.size());
}

Directory *Build(const std::vector<EntrySpec> &specs)
{
    const uint32_t bytes = kDirectoryHead + static_cast<uint32_t>(specs.size()) * sizeof(Entry);
    Directory *directory = static_cast<Directory *>(gimex::Allocate(bytes));
    std::memset(directory, 0, bytes);
    directory->count = static_cast<int32_t>(specs.size());
    Entry *entries = EntriesOf(directory);
    for (size_t i = 0; i < specs.size(); ++i) {
        const EntrySpec &s = specs[i];
        Entry &e = entries[i];
        e.shape = Block(s.shape);
        e.shapeBytes = SizeOf(s.shapeBytes, s.shape);
        e.palette = Block(s.palette);
        e.paletteBytes = SizeOf(s.paletteBytes, s.palette);
        e.marker = Block(s.marker);
        e.markerBytes = SizeOf(s.markerBytes, s.marker);
        e.userData = Block(s.userData);
        e.userDataBytes = SizeOf(s.userDataBytes, s.userData);
        e.pairs = Block(s.pairs);
        e.pairsBytes = SizeOf(s.pairsBytes, s.pairs);
        e.text = Block(s.text);
        e.textBytes = SizeOf(s.textBytes, s.text);
        if (s.hasName) {
            e.name = static_cast<char *>(gimex::Allocate(static_cast<uint32_t>(s.name.size() + 1)));
            std::memcpy(e.name, s.name.c_str(), s.name.size() + 1);
        }
        e.flags = s.flags;
        e.kind = 0x1234 + static_cast<uint32_t>(i);
        e.imageBytes = 0x777 + static_cast<uint32_t>(i);
        e.tag = 0x3131 + static_cast<uint32_t>(i);
    }
    return directory;
}

void Append(std::vector<uint8_t> &out, const void *p, size_t n)
{
    const uint8_t *b = static_cast<const uint8_t *>(p);
    out.insert(out.end(), b, b + n);
}

void AppendBlock(std::vector<uint8_t> &out, const uint8_t *p, uint32_t bytes)
{
    const uint32_t present = p != nullptr;
    Append(out, &present, 4);
    Append(out, &bytes, 4);
    if (p != nullptr && bytes <= 0x100000) Append(out, p, bytes);
}

// Everything observable about a directory: the fields (pointers as present / absent) and each block's bytes.
std::vector<uint8_t> Serialize(const Directory *directory)
{
    std::vector<uint8_t> out;
    Append(out, &directory->count, 4);
    const Entry *entries = EntriesOf(const_cast<Directory *>(directory));
    for (int32_t i = 0; i < directory->count; ++i) {
        const Entry &e = entries[i];
        AppendBlock(out, e.shape, e.shapeBytes);
        AppendBlock(out, e.palette, e.paletteBytes);
        AppendBlock(out, e.marker, e.markerBytes);
        AppendBlock(out, e.userData, e.userDataBytes);
        AppendBlock(out, e.pairs, e.pairsBytes);
        AppendBlock(out, e.text, e.textBytes);
        const uint32_t named = e.name != nullptr;
        Append(out, &named, 4);
        if (named) Append(out, e.name, std::strlen(e.name) + 1);
        Append(out, &e.flags, 4);
        Append(out, &e.kind, 4);
        Append(out, &e.imageBytes, 4);
        Append(out, &e.tag, 4);
        Append(out, &e.frameDepth, 8);
    }
    return out;
}

std::vector<uint8_t> RandomBytes(Fill &fill, size_t n)
{
    std::vector<uint8_t> v(n);
    fill.Bytes(v);
    return v;
}

} // namespace

// ---- sizes ----

DIFF_TEST(GimexFshWrite_RowBytes, kRowBytes)
{
    using Fn = int32_t(__cdecl *)(int32_t, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kRowBytes);
    const int32_t bitsPool[] = {0, 1, 4, 7, 8, 12, 15, 16, 24, 32, 33, 64, -8};
    int iter = 0;
    for (const int32_t platform : kPlatforms)
        for (const int32_t bits : bitsPool)
            for (int32_t width = -9; width <= 300; ++width) {
                ClearStack(StackByte(iter));
                const int32_t a = original(width, bits, platform);
                ClearStack(StackByte(iter++));
                const int32_t b = EncodeRowBytes(width, bits, platform);
                CHECK(ctx, a == b, "EncodeRowBytes(%d, %d, %d): original %d, ours %d", width, bits, platform, a, b);
            }
    Fill fill(0xa001);
    for (int i = 0; i < 20000; ++i) {
        const int32_t width = static_cast<int32_t>(fill.Next()), bits = fill.Range(-3, 70), platform = fill.Range(-2, 12);
        ClearStack(StackByte(i));
        const int32_t a = original(width, bits, platform);
        ClearStack(StackByte(i));
        const int32_t b = EncodeRowBytes(width, bits, platform);
        CHECK(ctx, a == b, "EncodeRowBytes(%d, %d, %d): original %d, ours %d", width, bits, platform, a, b);
    }
}

DIFF_TEST(GimexFshWrite_PlaneBytes, kPlaneBytes)
{
    using Fn = int32_t(__cdecl *)(uint32_t, uint32_t, int32_t, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kPlaneBytes);
    Fill fill(0xa002);
    const int32_t bitsPool[] = {1, 4, 8, 12, 15, 16, 24, 32};
    for (int i = 0; i < 60000; ++i) {
        const uint32_t width = (i % 4 == 0) ? fill.Next() : static_cast<uint32_t>(fill.Range(0, 300));
        const uint32_t height = (i % 4 == 0) ? fill.Next() : static_cast<uint32_t>(fill.Range(0, 300));
        const int32_t bits = (i % 7 == 0) ? fill.Range(-2, 70) : bitsPool[fill.Next() % 8];
        const int32_t platform = fill.Range(-1, 11);
        const int32_t pad = (i % 3 == 0) ? 0 : (i % 3 == 1 ? 1 : fill.Range(-5, 5));
        ClearStack(StackByte(i));
        const int32_t a = original(width, height, bits, platform, pad);
        ClearStack(StackByte(i));
        const int32_t b = PlaneBytes(width, height, bits, platform, pad);
        CHECK(ctx, a == b, "PlaneBytes(%u, %u, %d, %d, %d): original %d, ours %d", width, height, bits, platform, pad, a, b);
    }
}

DIFF_TEST(GimexFshWrite_MipLevels, kMipLevels)
{
    using Fn = int32_t(__cdecl *)(const uint8_t *, int32_t);
    const Fn original = ctx.Original<Fn>(kMipLevels);
    Fill fill(0xa003);
    {
        ClearStack(0);
        const int32_t a = original(nullptr, 2);
        ClearStack(0);
        const int32_t b = EncodeMipLevels(nullptr, 2);
        CHECK(ctx, a == b, "EncodeMipLevels(null): original %d, ours %d", a, b);
    }
    for (int i = 0; i < 60000; ++i) {
        uint8_t shape[0x30];
        fill.Bytes(shape, sizeof shape);
        const int32_t platform = fill.Range(-1, 11);
        const uint32_t declared = (i % 5 == 0) ? 15u : static_cast<uint32_t>(fill.Range(0, 15));
        PutWord(shape + 1, (ReadWord(shape + 1, 3, platform) & 0xff0fffffu) | (declared << 20), 3, platform);
        const int32_t width = (i % 3 == 0) ? static_cast<int32_t>(fill.Next()) : (1 << fill.Range(0, 11)) + fill.Range(-1, 1);
        PutWord(shape + 0x18, static_cast<uint32_t>(width), 4, platform);
        ClearStack(StackByte(i));
        const int32_t a = original(shape, platform);
        ClearStack(StackByte(i));
        const int32_t b = EncodeMipLevels(shape, platform);
        CHECK(ctx, a == b, "EncodeMipLevels(declared %u, width %d, platform %d): original %d, ours %d", declared, width, platform, a, b);
    }
}

DIFF_TEST(GimexFshWrite_ShapeBits, kShapeBits)
{
    using Fn = int32_t(__cdecl *)(const uint8_t *, int32_t);
    const Fn original = ctx.Original<Fn>(kShapeBits);
    int iter = 0;
    for (const int32_t platform : kPlatforms)
        for (int32_t type = 0; type < 256; ++type) {
            uint8_t shape[0x30] = {};
            shape[0] = static_cast<uint8_t>(type);
            ClearStack(StackByte(iter));
            const int32_t a = original(shape, platform);
            ClearStack(StackByte(iter++));
            const int32_t b = EncodeShapeBits(shape, platform);
            CHECK(ctx, a == b, "EncodeShapeBits(type %#x, platform %d): original %d, ours %d", type, platform, a, b);
        }
}

DIFF_TEST(GimexFshWrite_SetMipLevels, kSetMipLevels)
{
    using Fn = void(__cdecl *)(uint8_t *, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kSetMipLevels);
    Fill fill(0xa004);
    // The compressed test (0x88888f with kind 3): attribute bit 2 and a block compressed header at +0x20.
    const uint8_t blockHeaders[][4] = {{0x1a, 0xfb, 0, 0}, {0x1c, 0xfb, 0, 0}, {0x10, 0xfb, 0, 0}, {0x11, 0xfb, 0, 0}, {0x00, 0x00, 0, 0}};
    for (int i = 0; i < 40000; ++i) {
        uint8_t a[0x48], b[0x48];
        fill.Bytes(a, sizeof a);
        const int32_t platform = fill.Range(-1, 11);
        if (fill.Chance(70)) {
            const uint8_t *header = blockHeaders[fill.Next() % 5];
            std::memcpy(a + 0x20, header, 4);
            if (fill.Chance(30)) a[0x20] = static_cast<uint8_t>(fill.Next());
            if (fill.Chance(80)) PutWord(a + 1, ReadWord(a + 1, 3, platform) | 2u, 3, platform);
        }
        std::memcpy(b, a, sizeof a);
        const int32_t count = (i % 4 == 0) ? static_cast<int32_t>(fill.Next()) : fill.Range(-2, 20);
        ClearStack(StackByte(i));
        original(a, count, platform);
        ClearStack(StackByte(i));
        SetMipLevels(b, count, platform);
        CHECK(ctx, std::memcmp(a, b, sizeof a) == 0, "SetMipLevels(count %d, platform %d): bytes differ", count, platform);
    }
    {
        ClearStack(0);
        original(nullptr, 3, 2);
        ClearStack(0);
        SetMipLevels(nullptr, 3, 2);
        ++ctx.cases;
    }
}

// ---- the directory's blocks ----

namespace {

std::vector<EntrySpec> RandomEntries(Fill &fill, int32_t maxEntries, size_t minBlock)
{
    std::vector<EntrySpec> specs(static_cast<size_t>(fill.Range(0, maxEntries)));
    for (EntrySpec &s : specs) {
        auto maybe = [&](std::vector<uint8_t> &v, int32_t percent) {
            if (fill.Chance(percent)) v = RandomBytes(fill, static_cast<size_t>(fill.Range(static_cast<int32_t>(minBlock), 70)));
        };
        maybe(s.shape, 80);
        maybe(s.palette, 60);
        maybe(s.marker, 40);
        maybe(s.userData, 40);
        maybe(s.pairs, 40);
        maybe(s.text, 50);
        s.hasName = fill.Chance(70);
        s.name = "name" + std::to_string(fill.Next() % 1000);
        s.flags = fill.Next();
    }
    return specs;
}

} // namespace

DIFF_TEST(GimexFshWrite_FreeWrittenDirectory, kFreeWrittenDirectory)
{
    using Fn = void(__cdecl *)(Directory *);
    const Fn original = ctx.Original<Fn>(kFreeWrittenDirectory);
    Fill fill(0xa005);
    for (int i = 0; i < 4000; ++i) {
        const std::vector<EntrySpec> specs = RandomEntries(fill, 5, 1);
        Directory *da = Build(specs);
        Directory *db = Build(specs);
        const HeapEffect a = Observe(StackByte(i), [&] { original(da); });
        const HeapEffect b = Observe(StackByte(i), [&] { FreeWrittenDirectory(db); });
        CHECK(ctx, a == b, "FreeWrittenDirectory iter %d (%zu entries): original %lld blocks %zu frees, ours %lld blocks %zu frees", i, specs.size(),
              a.blocks, a.frees, b.blocks, b.frees);
    }
}

DIFF_TEST(GimexFshWrite_PadShapes, kPadShapes)
{
    using Fn = void(__cdecl *)(Directory *, int32_t, uint32_t);
    const Fn original = ctx.Original<Fn>(kPadShapes);
    Fill fill(0xa006);
    for (int i = 0; i < 6000; ++i) {
        std::vector<EntrySpec> specs = RandomEntries(fill, 5, 1);
        for (EntrySpec &s : specs) {
            if (fill.Chance(10)) s.shapeBytes = 0;
            if (fill.Chance(10)) s.paletteBytes = 0;
        }
        const uint32_t masks[] = {0, 1, 3, 7, 15, 0x1f, 0x3f, 0x7f, 0xff, 5, 12};
        const uint32_t mask = (i % 5 == 0) ? static_cast<uint32_t>(fill.Range(0, 100)) : masks[fill.Next() % 11];
        Directory *da = Build(specs);
        Directory *db = Build(specs);
        const HeapEffect a = Observe(StackByte(i), [&] { original(da, 0x55, mask); });
        const HeapEffect b = Observe(StackByte(i), [&] { PadShapes(db, 0x55, mask); });
        const std::vector<uint8_t> sa = Serialize(da), sb = Serialize(db);
        CHECK(ctx, a == b && sa == sb, "PadShapes iter %d (mask %#x, %zu entries): heap %s, bytes %s", i, mask, specs.size(), a == b ? "equal" : "differ",
              sa == sb ? "equal" : "differ");
        FreeWrittenDirectory(da);
        FreeWrittenDirectory(db);
    }
}

DIFF_TEST(GimexFshWrite_ChainBlock, kChainBlock)
{
    using Fn = void(__cdecl *)(uint8_t **, uint32_t *, uint8_t *, uint32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kChainBlock);
    Fill fill(0xa007);
    for (int i = 0; i < 20000; ++i) {
        std::vector<uint8_t> blockA = RandomBytes(fill, 16), tailA = RandomBytes(fill, 16);
        std::vector<uint8_t> blockB = blockA, tailB = tailA;
        const bool noBlock = fill.Chance(20), noTail = fill.Chance(30);
        const uint32_t tailBytes = fill.Next(), bytes = fill.Next();
        const int32_t platform = fill.Range(-1, 11);
        uint8_t *pa = noTail ? nullptr : tailA.data(), *pb = noTail ? nullptr : tailB.data();
        uint32_t ba = tailBytes, bb = tailBytes;
        ClearStack(StackByte(i));
        original(&pa, &ba, noBlock ? nullptr : blockA.data(), bytes, platform);
        ClearStack(StackByte(i));
        ChainBlock(&pb, &bb, noBlock ? nullptr : blockB.data(), bytes, platform);
        const bool sameTail = (pa == nullptr) == (pb == nullptr) && (pa == nullptr || (pa == tailA.data()) == (pb == tailB.data())) &&
                              (pa == nullptr || pa != blockA.data()) == (pb == nullptr || pb != blockB.data());
        CHECK(ctx, blockA == blockB && tailA == tailB && ba == bb && sameTail, "ChainBlock iter %d (platform %d, block %d, tail %d)", i, platform,
              !noBlock, !noTail);
    }
}

DIFF_TEST(GimexFshWrite_ChainEntryBlocks, kChainEntryBlocks)
{
    using Fn = int32_t(__cdecl *)(Directory *, int32_t);
    const Fn original = ctx.Original<Fn>(kChainEntryBlocks);
    Fill fill(0xa008);
    for (int i = 0; i < 6000; ++i) {
        const std::vector<EntrySpec> specs = RandomEntries(fill, 5, 8);
        const int32_t platform = fill.Range(-1, 11);
        Directory *da = Build(specs);
        Directory *db = Build(specs);
        int32_t ra = 0, rb = 0;
        const HeapEffect a = Observe(StackByte(i), [&] { ra = original(da, platform); });
        const HeapEffect b = Observe(StackByte(i), [&] { rb = ChainEntryBlocks(db, platform); });
        const std::vector<uint8_t> sa = Serialize(da), sb = Serialize(db);
        CHECK(ctx, ra == rb && a == b && sa == sb, "ChainEntryBlocks iter %d (platform %d): result %d / %d, bytes %s", i, platform, ra, rb,
              sa == sb ? "equal" : "differ");
        FreeWrittenDirectory(da);
        FreeWrittenDirectory(db);
    }
}

// ---- pixel tilings ----

DIFF_TEST(GimexFshWrite_TilePixels, kTilePixels)
{
    using Fn = void(__cdecl *)(int32_t, int32_t, int32_t, const uint8_t *, uint8_t *);
    const Fn original = ctx.Original<Fn>(kTilePixels);
    Fill fill(0xa009);
    for (int i = 0; i < 20000; ++i) {
        const int32_t width = (i < 400) ? (i % 20) : fill.Range(0, 40), height = (i < 400) ? (i / 20) : fill.Range(0, 40);
        const int32_t stride = width * 4 + 4 * fill.Range(0, 3);
        std::vector<uint8_t> source(static_cast<size_t>(stride) * static_cast<size_t>(height + 8) + 128);
        fill.Bytes(source);
        const size_t destBytes = static_cast<size_t>(((width + 3) & ~3) + 4) * static_cast<size_t>(((height + 3) & ~3) + 4) * 4 + 64;
        std::vector<uint8_t> a(destBytes), b;
        fill.Bytes(a);
        b = a;
        ClearStack(StackByte(i));
        original(width, height, stride, source.data(), a.data() + 16);
        ClearStack(StackByte(i));
        TilePixels(width, height, stride, source.data(), b.data() + 16);
        CHECK(ctx, a == b, "TilePixels(%d x %d, stride %d): bytes differ", width, height, stride);
    }
}

DIFF_TEST(GimexFshWrite_TileBlocks, kTileBlocks)
{
    using Fn = void(__cdecl *)(uint32_t, int32_t, int32_t, int32_t, int32_t, const uint8_t *, uint8_t *);
    const Fn original = ctx.Original<Fn>(kTileBlocks);
    Fill fill(0xa00a);
    for (int i = 0; i < 12000; ++i) {
        const uint32_t paddedWidth = 4u << fill.Range(0, 4), paddedHeight = 4u << fill.Range(0, 4);
        const int32_t width = (i % 4 == 0) ? static_cast<int32_t>(paddedWidth) : fill.Range(0, static_cast<int32_t>(paddedWidth));
        const int32_t height = (i % 4 == 1) ? static_cast<int32_t>(paddedHeight) : fill.Range(0, static_cast<int32_t>(paddedHeight));
        const int32_t stride = width * 4 + 4 * fill.Range(0, 3);
        std::vector<uint8_t> source(static_cast<size_t>(stride + static_cast<int32_t>(paddedWidth) * 4) * static_cast<size_t>(paddedHeight + 8) + 256);
        fill.Bytes(source);
        const size_t side = std::max(paddedWidth, paddedHeight);
        const size_t destBytes = side * side * 4 + 128;
        std::vector<uint8_t> a(destBytes), b;
        fill.Bytes(a);
        b = a;
        ClearStack(StackByte(i));
        original(paddedWidth, static_cast<int32_t>(paddedHeight), width, height, stride, source.data(), a.data() + 16);
        ClearStack(StackByte(i));
        TileBlocks(paddedWidth, static_cast<int32_t>(paddedHeight), width, height, stride, source.data(), b.data() + 16);
        CHECK(ctx, a == b, "TileBlocks(padded %u x %u, %d x %d, stride %d): bytes differ", paddedWidth, paddedHeight, width, height, stride);
    }
}

// ---- shape assembly ----

namespace {

// A shape header with the given type, size and attributes.
std::vector<uint8_t> MakeShape(Fill &fill, uint8_t type, int32_t width, int32_t height, uint32_t attributes, int32_t platform, size_t total)
{
    std::vector<uint8_t> shape(total);
    fill.Bytes(shape);
    shape[0] = type;
    PutWord(shape.data() + 1, attributes, 3, platform);
    PutWord(shape.data() + 0x18, static_cast<uint32_t>(width), 4, platform);
    PutWord(shape.data() + 0x1c, static_cast<uint32_t>(height), 4, platform);
    return shape;
}

const uint8_t kShapeTypes[] = {0x7d, 0x7f, 0x78, 0x7e, 0x01, 0x2a, 0x60, 0x61, 0x62, 0x1e, 0x40, 0x79, 0x7b, 0x16, 0x20, 0x44, 0x1c, 0x00, 0x03};

// Whether the original's chain for this cube map holds every level it copies. It sizes the chain from the unaligned sum of the levels while
// AlignedOffset aligns the absolute offset (the 0x20-byte header included), so a face whose levels do not add up to a multiple of the alignment
// makes the original write past its block (ORIGINAL-BUG: heap overflow). Real cube maps are powers of two, where it never happens; those inputs
// are kept out of the test by replaying the offsets with the verified leaves.
bool CubeFits(const std::vector<uint8_t> &shape, int32_t platform, uint32_t align)
{
    const uint8_t *header = shape.data();
    const int32_t levels = EncodeMipLevels(header, platform);
    if (levels <= 1) return true;
    const int32_t width = static_cast<int32_t>(ReadWord(header + 0x18, 4, platform));
    const int32_t height = static_cast<int32_t>(ReadWord(header + 0x1c, 4, platform));
    int32_t bits = EncodeShapeBits(header, platform), kind = 0;
    const uint32_t type = ReadWord(header, 1, platform);
    DetectPixelFormat(header, header, ((type >= 0x60 && type <= 0x62) || type == 0x1e) ? 1 : 0, &bits, &kind, platform);
    int32_t chainBytes = 0;
    void *chain = AllocateMipChain(width, height, bits, &chainBytes, levels, align);
    gimex::FreeMemory(chain);
    int32_t offset = 0x20, levelSize = 0;
    for (int32_t face = 0; face < 6; ++face)
        for (int32_t level = 0; level < levels; ++level) {
            MipChainSize(level, face, 0x1234, align, levels, &levelSize, width, height, bits);
            if (static_cast<int64_t>(offset) + levelSize > chainBytes) return false;
            offset = AlignedOffset(level, offset, 0x1234, align, levels, levelSize);
        }
    return true;
}

} // namespace

DIFF_TEST(GimexFshWrite_ExpandCubeMaps, kExpandCubeMaps)
{
    using Fn = void(__cdecl *)(Directory *, int32_t, uint32_t, uint32_t);
    const Fn original = ctx.Original<Fn>(kExpandCubeMaps);
    Fill fill(0xa00b);
    int expanded = 0;
    for (int i = 0; i < 15000; ++i) {
        std::vector<EntrySpec> specs(static_cast<size_t>(fill.Range(0, 3)));
        const int32_t platform = (i % 3 == 0) ? fill.Range(0, 9) : 2;
        const uint32_t align = (1u << fill.Range(0, 6)) - 1;
        for (EntrySpec &s : specs) {
            const int32_t levels = fill.Range(0, 5);
            const int32_t width = (1 << fill.Range(2, 5)), height = 6 * (1 << fill.Range(2, 5));
            const uint32_t declared = fill.Chance(10) ? 15u : static_cast<uint32_t>(levels);
            const uint32_t attributes = (declared << 20) | (fill.Next() & 0xf0ffffu);
            const uint8_t type = kShapeTypes[fill.Next() % sizeof kShapeTypes];
            s.shape = MakeShape(fill, type, width, height, attributes, platform, static_cast<size_t>(width) * static_cast<size_t>(height) * 6 * 16 * 2 + 0x400);
            s.palette = fill.Chance(50) ? RandomBytes(fill, 24) : std::vector<uint8_t>();
            s.flags = fill.Chance(75) ? 0x40000u : 0u;
            if (s.flags != 0 && !CubeFits(s.shape, platform, align)) s.flags = 0;
            expanded += s.flags != 0 && EncodeMipLevels(s.shape.data(), platform) > 1;
            s.hasName = true;
            s.name = "cube";
        }
        Directory *da = Build(specs);
        Directory *db = Build(specs);
        const HeapEffect a = Observe(StackByte(i), [&] { original(da, platform, 0x1234, align); });
        const HeapEffect b = Observe(StackByte(i), [&] { ExpandCubeMaps(db, platform, 0x1234, align); });
        const std::vector<uint8_t> sa = Serialize(da), sb = Serialize(db);
        CHECK(ctx, a == b && sa == sb, "ExpandCubeMaps iter %d (platform %d, align %u, %zu entries): heap %s (%lld / %lld blocks), bytes %s", i, platform,
              align, specs.size(), a == b ? "equal" : "differ", a.blocks, b.blocks, sa == sb ? "equal" : "differ");
        FreeWrittenDirectory(da);
        FreeWrittenDirectory(db);
    }
    CHECK(ctx, expanded > 500, "only %d cube maps were expanded; the test input is too narrow", expanded);
}

namespace {

// A RefPack stream (our encoder) of `bytes` compressible bytes, after a 0x20-byte header whose attribute bit 2 is set.
std::vector<uint8_t> MakePackedShape(Fill &fill, uint8_t type, int32_t width, int32_t height, uint32_t attributes, int32_t platform, size_t payload)
{
    // The decoder trusts the stream's own size and the merged buffer is sized from the header's dimensions and pixel format, so a stream
    // bigger than that buffer overruns the original too (the pixel format the merger settles on can be one the generator did not expect).
    // A payload under the buffer's floor (0x400 bytes past the header) never does.
    payload = std::min<size_t>(payload, 0x300);
    std::vector<uint8_t> plain(payload);
    for (size_t k = 0; k < payload; ++k) plain[k] = static_cast<uint8_t>((k / 5) * 7 + (fill.Next() % 3 == 0 ? fill.Next() : 0));
    std::vector<uint8_t> packed(payload * 2 + 256);
    const int32_t n = RefPackEncodeWithHeader(packed.data(), plain.data(), static_cast<int32_t>(payload), 0);
    std::vector<uint8_t> shape = MakeShape(fill, type, width, height, attributes | 2u, platform, 0x20);
    shape.insert(shape.end(), packed.begin(), packed.begin() + n);
    shape.resize(shape.size() + 64, 0);
    return shape;
}

} // namespace

DIFF_TEST(GimexFshWrite_MergeMipmaps, kMergeMipmaps)
{
    using Fn = void(__cdecl *)(Directory *, int32_t, uint32_t, uint32_t);
    const Fn original = ctx.Original<Fn>(kMergeMipmaps);
    Fill fill(0xa00c);
    const uint8_t types[] = {0x7d, 0x7f, 0x78, 0x7e, 0x01, 0x2a, 0x60, 0x61, 0x62, 0x1e, 0x40, 0x79, 0x16, 0x20, 0x44, 0x1c, 0x00, 0x03, 0x7b};
    int mergedOnPlatform[10] = {};
    const char *const mipNames[] ={"mm1", "mm2", "base_mm3", "x_mm4", "mm", "_mm", "base", "nomip", ""};
    for (int i = 0; i < 6000; ++i) {
        const int32_t platform = (i % 2 == 0) ? 2 : fill.Range(0, 9);
        const int32_t runs = fill.Range(1, 3);
        std::vector<EntrySpec> specs;
        // The original sizes the merged shape from the base entry alone and copies every following mip-named entry into it, so the entries
        // that follow a base must be the right size for their level (a larger one makes the original write past its block). The names and
        // types are drawn first; an entry's size then follows from the chain it ends up in (a name that is not a mip name, or another bit
        // depth, starts a new chain).
        struct Proto {
            std::string name;
            uint8_t type;
            bool packed;
            uint32_t attributes;
            int32_t width, height;
        };
        std::vector<Proto> protos;
        for (int32_t run = 0; run < runs; ++run) {
            const uint8_t type = types[fill.Next() % sizeof types];
            const int32_t width = 1 << fill.Range(2, 5), height = 1 << fill.Range(2, 5);
            const bool packed = fill.Chance(25);
            const uint32_t attributes = (fill.Chance(30) ? 0x4000u : 0u) | (fill.Next() & 0x00f0fe00u);
            const int32_t levels = fill.Range(0, 4);
            for (int32_t level = 0; level <= levels; ++level) {
                Proto p;
                p.type = (level != 0 && fill.Chance(8)) ? types[fill.Next() % sizeof types] : type;
                p.packed = packed;
                p.attributes = attributes;
                p.width = width;
                p.height = height;
                p.name = level == 0 ? (fill.Chance(10) ? "mm1" : "base") : (fill.Chance(88) ? mipNames[fill.Next() % 4] : mipNames[fill.Next() % 9]);
                protos.push_back(p);
            }
        }
        int32_t baseWidth = 0, baseHeight = 0, baseBits = 0, depth = 0;
        for (size_t j = 0; j < protos.size(); ++j) {
            Proto &p = protos[j];
            const int32_t bits = EncodeShapeBits(&p.type, platform);
            const bool member = j != 0 && IsMipmapName(p.name.c_str(), 0) && bits == baseBits;
            if (member) {
                ++depth;
                p.width = std::max(baseWidth >> depth, 1);
                p.height = std::max(baseHeight >> depth, 1);
            } else {
                baseWidth = p.width;
                baseHeight = p.height;
                baseBits = bits;
                depth = 0;
            }
            EntrySpec s;
            const size_t room = static_cast<size_t>(p.width) * static_cast<size_t>(p.height) * 16 + 0x400;
            if (p.packed)
                s.shape = MakePackedShape(fill, p.type, p.width, p.height, p.attributes, platform,
                                          static_cast<size_t>(PlaneBytes(static_cast<uint32_t>(p.width), static_cast<uint32_t>(p.height), bits, platform, 0)));
            else s.shape = MakeShape(fill, p.type, p.width, p.height, p.attributes, platform, room);
            s.palette = fill.Chance(20) ? RandomBytes(fill, 24) : std::vector<uint8_t>();
            // The other blocks an entry owns are freed (and its sizes zeroed) when it is merged away.
            if (fill.Chance(25)) s.text = RandomBytes(fill, 16);
            if (fill.Chance(15)) s.marker = RandomBytes(fill, 32);
            if (fill.Chance(15)) s.pairs = RandomBytes(fill, 16);
            s.hasName = true;
            s.name = p.name;
            specs.push_back(s);
        }
        // A last entry that holds only an attachment survives the clean-up (it has no shape to merge into: it must not be followed by a mip name).
        if (fill.Chance(20)) {
            EntrySpec tail;
            tail.hasName = true;
            tail.name = "tail";
            switch (fill.Next() % 3) {
            case 0: tail.marker = RandomBytes(fill, 32); break;
            case 1: tail.pairs = RandomBytes(fill, 16); break;
            default: tail.text = RandomBytes(fill, 16); break;
            }
            specs.push_back(tail);
        }
        const uint32_t align = (1u << fill.Range(0, 6)) - 1;
        Directory *da = Build(specs);
        Directory *db = Build(specs);
        const HeapEffect a = Observe(StackByte(i), [&] { original(da, platform, 0x1234, align); });
        const HeapEffect b = Observe(StackByte(i), [&] { MergeMipmaps(db, platform, 0x1234, align); });
        const std::vector<uint8_t> sa = Serialize(da), sb = Serialize(db);
        CHECK(ctx, a == b && sa == sb, "MergeMipmaps iter %d (platform %d, align %u, %zu entries): heap %s (%lld / %lld blocks, %zu / %zu frees), bytes %s (count %d / %d)",
              i, platform, align, specs.size(), a == b ? "equal" : "differ", a.blocks, b.blocks, a.frees, b.frees, sa == sb ? "equal" : "differ", da->count,
              db->count);
        if (platform >= 0 && platform < 10) mergedOnPlatform[platform] += static_cast<int>(specs.size()) - da->count;
        FreeWrittenDirectory(da);
        FreeWrittenDirectory(db);
    }
    for (int p = 0; p < 10; ++p) CHECK(ctx, mergedOnPlatform[p] > 20, "platform %d merged only %d entries; the test input is too narrow", p, mergedOnPlatform[p]);
}

// ---- the attachment builders ----

namespace {

constexpr uint32_t kBuildPalette = 0x88b405;
constexpr uint32_t kBuildTextBlock = 0x88b9e1;
constexpr uint32_t kBuildUserDataBlock = 0x88ba81;
constexpr uint32_t kBuildPairsBlock = 0x88bb19;
constexpr uint32_t kBuildMetalBinBlock = 0x88bc6b;
constexpr uint32_t kCopyShapeName = 0x88bd45;

constexpr size_t kInfoBytes = 0x2b00;

void SetWord(std::vector<uint8_t> &info, uint32_t offset, uint32_t value)
{
    std::memcpy(info.data() + offset, &value, 4);
}

// A block a builder returned, with what it did to the heap.
struct Built {
    std::vector<uint8_t> bytes;
    bool present = false;
    uint32_t size = 0;
    HeapEffect heap;
    bool operator==(const Built &o) const { return bytes == o.bytes && present == o.present && size == o.size && heap == o.heap; }
};

template <typename Call> Built RunBuilder(uint8_t stack, Call call)
{
    Built built;
    uint8_t *block = reinterpret_cast<uint8_t *>(0x5a5a5a5a);
    uint32_t size = 0x6b6b6b6b;
    built.heap = Observe(stack, [&] { call(&block, &size); });
    built.present = block != nullptr;
    built.size = size;
    if (block != nullptr) {
        built.bytes.assign(block, block + size);
        gimex::FreeMemory(block);
    }
    return built;
}

} // namespace

DIFF_TEST(GimexFshWrite_BuildPalette, kBuildPalette)
{
    using Fn = void(__cdecl *)(const Info *, uint32_t, const uint8_t *, uint8_t **, uint32_t *, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kBuildPalette);
    Fill fill(0xa00d);
    int seenTypes[256] = {};
    int perPlatform[12] = {};
    const uint32_t variants[] = {0, 0x400000, 0x4000, 0x404000, 0x200000, 0xffffffff, 0x8000, 0x400001};
    for (int i = 0; i < 40000; ++i) {
        std::vector<uint8_t> info(kInfoBytes);
        fill.Bytes(info);
        const int32_t platform = (i % 12 == 11) ? fill.Range(-3, 14) : (i % 12);
        SetWord(info, 0x428, variants[fill.Next() % 8]);
        SetWord(info, 0x1c, fill.Chance(50) ? 32u : static_cast<uint32_t>(fill.Range(0, 9)));
        const int32_t bitsPool[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 16};
        for (const uint32_t offset : {0x438u, 0x43cu, 0x440u, 0x444u}) SetWord(info, offset, static_cast<uint32_t>(bitsPool[fill.Next() % 11]));
        if (fill.Chance(30)) {
            const int32_t combos[][4] = {{8, 8, 8, 8}, {0, 8, 8, 8}, {0, 5, 6, 5}, {1, 5, 5, 5}, {4, 4, 4, 4}, {0, 5, 5, 5}, {0, 6, 5, 5}, {8, 5, 6, 5}, {2, 10, 10, 10}, {6, 6, 6, 6}, {0, 3, 3, 2}};
            const int32_t *c = combos[fill.Next() % 11];
            for (int k = 0; k < 4; ++k) SetWord(info, 0x438u + 4u * static_cast<uint32_t>(k), static_cast<uint32_t>(c[k]));
        }
        const uint32_t count = (i % 7 == 0) ? static_cast<uint32_t>(fill.Range(-2, 3)) : static_cast<uint32_t>(fill.Range(1, 300));
        std::vector<uint8_t> entries(static_cast<size_t>(std::max<int32_t>(static_cast<int32_t>(count), 1)) * 4 + 16);
        fill.Bytes(entries);
        if (fill.Chance(25)) {
            // A grey ramp: increasing grey entries.
            for (size_t k = 0; k + 4 <= entries.size(); k += 4) entries[k] = entries[k + 1] = entries[k + 2] = static_cast<uint8_t>(k / 4);
        }
        const int32_t skip = fill.Chance(8) ? 1 : 0;
        const Built a = RunBuilder(StackByte(i), [&](uint8_t **out, uint32_t *size) {
            original(reinterpret_cast<const Info *>(info.data()), count, entries.data(), out, size, skip, platform);
        });
        const Built b = RunBuilder(StackByte(i), [&](uint8_t **out, uint32_t *size) {
            BuildPalette(reinterpret_cast<const Info *>(info.data()), count, entries.data(), out, size, skip, platform);
        });
        CHECK(ctx, a == b, "BuildPalette iter %d (platform %d, count %d, skip %d, variant %#x, bits %u/%u/%u/%u): original %s %u bytes, ours %s %u bytes, %s", i,
              platform, static_cast<int32_t>(count), skip, *reinterpret_cast<uint32_t *>(info.data() + 0x428), *reinterpret_cast<uint32_t *>(info.data() + 0x438),
              *reinterpret_cast<uint32_t *>(info.data() + 0x43c), *reinterpret_cast<uint32_t *>(info.data() + 0x440), *reinterpret_cast<uint32_t *>(info.data() + 0x444),
              a.present ? "block" : "none", a.size, b.present ? "block" : "none", b.size, a.bytes == b.bytes ? "same bytes" : "bytes differ");
        if (a.present && !a.bytes.empty()) {
            ++seenTypes[a.bytes[0]];
            if (platform >= 0 && platform < 12) ++perPlatform[platform];
        }
    }
    for (const int type : {0x20, 0x21, 0x22, 0x23, 0x24, 0x27, 0x29, 0x2a, 0x2c, 0x2d, 0x2e, 0x30, 0x31, 0x32, 0x33, 0x38, 0x39})
        CHECK(ctx, seenTypes[type] > 0, "no palette of type %#x was built; the test input is too narrow", type);
    for (int p = 1; p <= 9; ++p) CHECK(ctx, perPlatform[p] > 100, "platform %d built %d palettes", p, perPlatform[p]);
}

DIFF_TEST(GimexFshWrite_BuildTextBlock, kBuildTextBlock)
{
    using Fn = void(__cdecl *)(const Info *, uint8_t **, uint32_t *, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kBuildTextBlock);
    Fill fill(0xa00e);
    for (int i = 0; i < 20000; ++i) {
        std::vector<uint8_t> info(kInfoBytes);
        fill.Bytes(info);
        const int32_t length = (i % 5 == 0) ? 0 : fill.Range(1, 60);
        for (int32_t k = 0; k < length; ++k) info[0x65c + static_cast<size_t>(k)] = static_cast<uint8_t>(fill.Range(1, 255));
        info[0x65c + static_cast<size_t>(length)] = 0;
        const int32_t skip = fill.Chance(10) ? 1 : 0, platform = fill.Range(-1, 11);
        const Built a = RunBuilder(StackByte(i), [&](uint8_t **out, uint32_t *size) { original(reinterpret_cast<const Info *>(info.data()), out, size, skip, platform); });
        const Built b = RunBuilder(StackByte(i), [&](uint8_t **out, uint32_t *size) { BuildTextBlock(reinterpret_cast<const Info *>(info.data()), out, size, skip, platform); });
        CHECK(ctx, a == b, "BuildTextBlock iter %d (length %d, skip %d, platform %d): original %u bytes, ours %u", i, length, skip, platform, a.size, b.size);
    }
}

DIFF_TEST(GimexFshWrite_BuildUserDataBlock, kBuildUserDataBlock)
{
    using Fn = void(__cdecl *)(const Info *, uint8_t **, uint32_t *, int32_t);
    const Fn original = ctx.Original<Fn>(kBuildUserDataBlock);
    Fill fill(0xa00f);
    for (int i = 0; i < 20000; ++i) {
        std::vector<uint8_t> info(kInfoBytes);
        fill.Bytes(info);
        const uint32_t sizes[] = {0x2a70, 0x2a7b, 0x2a7c, 0x2a80, 0x100, 0xffffffffu};
        SetWord(info, 4, sizes[fill.Next() % 6]);
        const int32_t counts[] = {0, 1, 5, 21, 22, 23, 24, 40, 100, 400, -1, -50};
        SetWord(info, 0x2a78, static_cast<uint32_t>(counts[fill.Next() % 12]));
        const int32_t platform = fill.Range(-1, 11);
        const Built a = RunBuilder(StackByte(i), [&](uint8_t **out, uint32_t *size) { original(reinterpret_cast<const Info *>(info.data()), out, size, platform); });
        const Built b = RunBuilder(StackByte(i), [&](uint8_t **out, uint32_t *size) { BuildUserDataBlock(reinterpret_cast<const Info *>(info.data()), out, size, platform); });
        CHECK(ctx, a == b, "BuildUserDataBlock iter %d (platform %d): original %u bytes, ours %u", i, platform, a.size, b.size);
    }
}

DIFF_TEST(GimexFshWrite_BuildPairsBlock, kBuildPairsBlock)
{
    using Fn = void(__cdecl *)(const Info *, uint8_t **, uint32_t *, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kBuildPairsBlock);
    Fill fill(0xa010);
    for (int i = 0; i < 20000; ++i) {
        std::vector<uint8_t> info(kInfoBytes);
        fill.Bytes(info);
        const int32_t counts[] = {0, 0, 1, 2, 3, 10, 50, 100, -1, 0};
        SetWord(info, 0x458, static_cast<uint32_t>(counts[fill.Next() % 10]));
        if (fill.Chance(40)) SetWord(info, 0x448, 0);
        if (fill.Chance(40)) SetWord(info, 0x44c, 0);
        const int32_t skip = fill.Chance(10) ? 1 : 0, platform = fill.Range(-1, 11);
        const Built a = RunBuilder(StackByte(i), [&](uint8_t **out, uint32_t *size) { original(reinterpret_cast<const Info *>(info.data()), out, size, skip, platform); });
        const Built b = RunBuilder(StackByte(i), [&](uint8_t **out, uint32_t *size) { BuildPairsBlock(reinterpret_cast<const Info *>(info.data()), out, size, skip, platform); });
        CHECK(ctx, a == b, "BuildPairsBlock iter %d (skip %d, platform %d): original %u bytes, ours %u", i, skip, platform, a.size, b.size);
    }
}

DIFF_TEST(GimexFshWrite_BuildMetalBinBlock, kBuildMetalBinBlock)
{
    using Fn = void(__cdecl *)(const Info *, uint8_t **, uint32_t *, int32_t);
    const Fn original = ctx.Original<Fn>(kBuildMetalBinBlock);
    Fill fill(0xa011);
    int built = 0;
    for (int i = 0; i < 6000; ++i) {
        std::vector<uint8_t> info(kInfoBytes);
        fill.Bytes(info);
        if (fill.Chance(70)) SetWord(info, 0x428, 0x200000u | (fill.Next() & 0xffffu));
        const int32_t platform = (i % 14) - 2;
        const Built a = RunBuilder(StackByte(i), [&](uint8_t **out, uint32_t *size) { original(reinterpret_cast<const Info *>(info.data()), out, size, platform); });
        const Built b = RunBuilder(StackByte(i), [&](uint8_t **out, uint32_t *size) { BuildMetalBinBlock(reinterpret_cast<const Info *>(info.data()), out, size, platform); });
        CHECK(ctx, a == b, "BuildMetalBinBlock iter %d (platform %d): original %u bytes, ours %u", i, platform, a.size, b.size);
        built += a.present;
    }
    CHECK(ctx, built > 1000, "only %d metal-bin blocks were built", built);
}

DIFF_TEST(GimexFshWrite_CopyShapeName, kCopyShapeName)
{
    using Fn = void(__cdecl *)(const Info *, char **);
    const Fn original = ctx.Original<Fn>(kCopyShapeName);
    Fill fill(0xa012);
    for (int i = 0; i < 10000; ++i) {
        std::vector<uint8_t> info(kInfoBytes);
        fill.Bytes(info);
        const int32_t length = (i % 6 == 0) ? 0 : fill.Range(1, 120);
        for (int32_t k = 0; k < length; ++k) info[0x45c + static_cast<size_t>(k)] = static_cast<uint8_t>(fill.Range(1, 255));
        info[0x45c + static_cast<size_t>(length)] = 0;
        const Built a = RunBuilder(StackByte(i), [&](uint8_t **out, uint32_t *size) {
            *size = static_cast<uint32_t>(length) + 1;
            original(reinterpret_cast<const Info *>(info.data()), reinterpret_cast<char **>(out));
        });
        const Built b = RunBuilder(StackByte(i), [&](uint8_t **out, uint32_t *size) {
            *size = static_cast<uint32_t>(length) + 1;
            CopyShapeName(reinterpret_cast<const Info *>(info.data()), reinterpret_cast<char **>(out));
        });
        CHECK(ctx, a == b && a.present, "CopyShapeName iter %d (length %d)", i, length);
    }
}

// ---- pixel packing and compression ----

namespace {

constexpr uint32_t kPackPixel = 0x88c0e9;
constexpr uint32_t kPackShapePixels = 0x88bd83;
constexpr uint32_t kBlockCompress = 0x88bf12;
constexpr uint32_t kGmCompress = 0x88c070;

// The record types PackPixel knows (every other value writes nothing).
const int32_t kPackTypes[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x18, 0x19, 0x40, 0x41, 0x42, 0x43, 0x44, 0x58, 0x59, 0x5a,
                              0x5b, 0x5c, 0x5d, 0x5e, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x6a, 0x6d, 0x78, 0x79, 0x7a, 0x7b, 0x7d, 0x7e, 0x7f};

void RandomInfo(Fill &fill, std::vector<uint8_t> &info)
{
    fill.Bytes(info);
    const int32_t depths[] = {8, 32, 24, 9, 7, 0, 8, 32};
    SetWord(info, 0x18, static_cast<uint32_t>(depths[fill.Next() % 8]));
    const int32_t bitsPool[] = {0, 1, 2, 5, 8};
    for (const uint32_t offset : {0x438u, 0x43cu, 0x440u, 0x444u}) SetWord(info, offset, static_cast<uint32_t>(bitsPool[fill.Next() % 5]));
}

} // namespace

DIFF_TEST(GimexFshWrite_PackPixel, kPackPixel)
{
    using Fn = void(__cdecl *)(const Info *, const uint8_t *, uint8_t *, int32_t *, int32_t *, int32_t, int32_t *, uint32_t *, int32_t);
    const Fn original = ctx.Original<Fn>(kPackPixel);
    Fill fill(0xa013);
    for (int i = 0; i < 120000; ++i) {
        std::vector<uint8_t> info(kInfoBytes);
        RandomInfo(fill, info);
        const int32_t type = (i % 3 == 0) ? fill.Range(-140, 270) : ((i % 3 == 1) ? kPackTypes[fill.Next() % sizeof kPackTypes / sizeof kPackTypes[0]] : (i / 3) % 256);
        std::vector<uint8_t> source(96);
        fill.Bytes(source);
        if (fill.Chance(30)) for (uint8_t &v : source) v = static_cast<uint8_t>(v & 0x0f ? v : (fill.Chance(50) ? 0xff : 0));
        std::vector<uint8_t> destA(64), destB;
        fill.Bytes(destA);
        destB = destA;
        const int32_t width = fill.Range(1, 20);
        int32_t srcA = fill.Range(0, 16), dstA = fill.Range(0, 16), colA = fill.Range(0, width);
        int32_t srcB = srcA, dstB = dstA, colB = colA;
        uint32_t rowA = fill.Next(), rowB = rowA;
        ClearStack(StackByte(i));
        original(reinterpret_cast<const Info *>(info.data()), source.data(), destA.data(), &srcA, &dstA, type, &colA, &rowA, width);
        ClearStack(StackByte(i));
        PackPixel(reinterpret_cast<const Info *>(info.data()), source.data(), destB.data(), &srcB, &dstB, type, &colB, &rowB, width);
        CHECK(ctx, destA == destB && srcA == srcB && dstA == dstB && colA == colB && rowA == rowB,
              "PackPixel iter %d (type %#x, width %d): offsets original %d/%d/%d ours %d/%d/%d, row %u/%u, bytes %s", i, type, width, srcA, dstA, colA, srcB, dstB,
              colB, rowA, rowB, destA == destB ? "equal" : "differ");
    }
}

DIFF_TEST(GimexFshWrite_PackShapePixels, kPackShapePixels)
{
    using Fn = void(__cdecl *)(const uint8_t *, uint8_t *, const Info *, const uint8_t *, int32_t, int32_t, uint32_t, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kPackShapePixels);
    Fill fill(0xa014);
    for (int i = 0; i < 12000; ++i) {
        std::vector<uint8_t> info(kInfoBytes);
        RandomInfo(fill, info);
        const bool reorder = fill.Chance(35);
        const int32_t reorderPlatforms[] = {2, 5, 6, 8};
        const int32_t platform = reorder ? reorderPlatforms[fill.Next() % 4] : ((i % 3 == 0) ? fill.Range(-1, 11) : 2);
        // The console surfaces are whole pages (platform 5: 32 x 16 tiles, 6: swizzle pages); the frame sits inside the surface.
        const int32_t sizes[] = {16, 32, 64};
        const int32_t surfaceWidth = platform == 6 ? 128 << fill.Range(0, 1) : platform == 5 ? 32 << fill.Range(0, 1) : sizes[fill.Next() % 3];
        const int32_t surfaceHeight = platform == 6 ? 128 << fill.Range(0, 1) : platform == 5 ? 16 << fill.Range(0, 2) : sizes[fill.Next() % 3];
        const bool console = platform == 5 || platform == 6;
        int32_t width = reorder ? (console ? fill.Range(1, surfaceWidth) : surfaceWidth) : fill.Range(1, 40);
        int32_t height = reorder ? (console ? fill.Range(1, surfaceHeight) : surfaceHeight) : fill.Range(1, 40);
        SetWord(info, 0x10, static_cast<uint32_t>(width));
        SetWord(info, 0x14, static_cast<uint32_t>(height));
        uint8_t shape[0x20] = {};
        fill.Bytes(shape, sizeof shape);
        shape[0] = static_cast<uint8_t>(fill.Chance(8) ? static_cast<int32_t>(fill.Next() & 0xff) : kPackTypes[fill.Next() % (sizeof kPackTypes / sizeof kPackTypes[0])]);
        // ORIGINAL-BUG (see GimexFshConvert): with the reorder flag the original lets ReorderImage tile formats 0x16 and 0x1e with a stride equal to
        // the width, which writes 0x21 bytes into a 0x20-byte margin; those two types stay out of the reordered cases.
        if (reorder && ((shape[0] & 0x7f) == 0x16 || (shape[0] & 0x7f) == 0x1e)) shape[0] = 0x7d;
        // The 1-bit type (0x44) is not one ReorderImage can lay out (the original overflows its buffer on the interleaved platforms).
        if (reorder && (shape[0] & 0x7f) == 0x44) shape[0] = 0x7d;
        const int32_t stride = width * 4 + 4 * fill.Range(0, 3);
        std::vector<uint8_t> source(static_cast<size_t>(stride) * static_cast<size_t>(height + 2) + 256);
        fill.Bytes(source);
        // Off the console platforms the original ignores the size it is given (the frame's own is used); a different one proves it.
        const bool other = !console && fill.Chance(50);
        const int32_t reorderWidth = (reorder && console) ? surfaceWidth : other ? fill.Range(1, 64) : width;
        const int32_t reorderHeight = (reorder && console) ? surfaceHeight : other ? fill.Range(1, 64) : height;
        const uint32_t flags = reorder ? 0x2000u : 0u;
        std::vector<uint8_t> destA(static_cast<size_t>(std::max(width, reorderWidth)) * static_cast<size_t>(std::max(height, reorderHeight)) * 8 + 512), destB;
        fill.Bytes(destA);
        destB = destA;
        const HeapEffect a = Observe(StackByte(i), [&] {
            original(shape, destA.data(), reinterpret_cast<const Info *>(info.data()), source.data(), stride, platform, flags, reorderWidth, reorderHeight);
        });
        const HeapEffect b = Observe(StackByte(i), [&] {
            PackShapePixels(shape, destB.data(), reinterpret_cast<const Info *>(info.data()), source.data(), stride, platform, flags, reorderWidth, reorderHeight);
        });
        CHECK(ctx, a == b && destA == destB, "PackShapePixels iter %d (type %#x, %d x %d, platform %d, reorder %d, %d x %d): heap %s, bytes %s", i, shape[0], width,
              height, platform, reorder, reorderWidth, reorderHeight, a == b ? "equal" : "differ", destA == destB ? "equal" : "differ");
    }
}

DIFF_TEST(GimexFshWrite_BlockCompress, kBlockCompress)
{
    using Fn = uint32_t(__cdecl *)(const Info *, int32_t, const uint8_t *, uint8_t *, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kBlockCompress);
    Fill fill(0xa015);
    for (int i = 0; i < 2500; ++i) {
        std::vector<uint8_t> info(kInfoBytes);
        RandomInfo(fill, info);
        // Every 7th image is wide, so that the width's high byte (written to the header) is not always zero.
        const bool wide = i % 7 == 3;
        const int32_t width = wide ? fill.Range(250, 700) : (i % 4 == 0) ? 4 * fill.Range(1, 12) : fill.Range(1, 40);
        const int32_t height = wide ? fill.Range(1, 6) : (i % 4 == 0) ? 4 * fill.Range(1, 12) : fill.Range(1, 40);
        SetWord(info, 0x10, static_cast<uint32_t>(width));
        SetWord(info, 0x14, static_cast<uint32_t>(height));
        const int32_t alphaPool[] = {0, 1, 2, 3, 4, 8};
        SetWord(info, 0x438, static_cast<uint32_t>(alphaPool[fill.Next() % 6]));
        SetWord(info, 0x42c, fill.Chance(50) ? 3u : static_cast<uint32_t>(fill.Range(0, 2)));
        const int32_t platform = (i % 3 == 0) ? 5 : ((i % 3 == 1) ? 2 : fill.Range(0, 9));
        const int32_t drop = fill.Chance(30) ? 1 : 0;
        const int32_t stride = width * 4 + 4 * fill.Range(0, 3);
        const int32_t paddedWidth = (width + 7) & ~7, paddedHeight = (height + 7) & ~7;
        std::vector<uint8_t> source(static_cast<size_t>(stride + paddedWidth * 4) * static_cast<size_t>(paddedHeight + 8) + 512);
        // Smooth, flat and noisy images give every encoder branch a turn.
        // The colour encoders (lane MS, tests/gz_gimex_fsh_encode_colour.cpp) read uninitialised stack tables of the original for blocks with
        // more than two colours and some alpha, so, as there, only quiet images are compared: opaque, or one colour (the pad pixels are
        // transparent zero, a second colour). Gradients and few-colour images need a size with no pad (multiples of 4, not platform 5).
        const bool padded = (width % 4) != 0 || (height % 4) != 0 || platform == 5;
        const int32_t kind = padded ? (i % 2) * 4 : i % 5;
        const uint32_t base = fill.Next();
        const uint32_t colours[6] = {fill.Next(), fill.Next(), fill.Next(), fill.Next(), fill.Next(), fill.Next()};
        const uint32_t columns = static_cast<uint32_t>(stride / 4);
        for (size_t k = 0; k + 4 <= source.size(); k += 4) {
            const uint32_t pixel = static_cast<uint32_t>(k / 4);
            const uint32_t x = pixel % columns, y = pixel / columns;
            uint32_t value;
            if (kind == 0 || kind == 4) value = base;
            else if (kind == 1) value = colours[fill.Next() % 2];
            else if (kind == 2) value = (x * 0x0a0a0a + y * 0x101010 + base) & 0xffffff;
            else value = colours[fill.Next() % (3 + base % 4)];
            value |= 0xff000000u;
            std::memcpy(&source[k], &value, 4);
        }
        std::vector<uint8_t> destA(static_cast<size_t>(paddedWidth) * static_cast<size_t>(paddedHeight) * 8 + 1024), destB;
        fill.Bytes(destA);
        destB = destA;
        uint32_t ra = 0, rb = 0;
        const HeapEffect a = Observe(StackByte(i), [&] { ra = original(reinterpret_cast<const Info *>(info.data()), stride, source.data(), destA.data(), platform, drop); });
        const HeapEffect b = Observe(StackByte(i), [&] { rb = BlockCompress(reinterpret_cast<const Info *>(info.data()), stride, source.data(), destB.data(), platform, drop); });
        size_t firstDiff = 0;
        while (firstDiff < destA.size() && destA[firstDiff] == destB[firstDiff]) ++firstDiff;
        CHECK(ctx, ra == rb && a == b && destA == destB, "BlockCompress iter %d (%d x %d, platform %d, drop %d, compression %u, alpha %u): result %u / %u, heap %s, bytes %s (first difference at %zu: %#x / %#x)", i,
              width, height, platform, drop, *reinterpret_cast<uint32_t *>(info.data() + 0x42c), *reinterpret_cast<uint32_t *>(info.data() + 0x438), ra, rb,
              a == b ? "equal" : "differ", destA == destB ? "equal" : "differ", firstDiff, firstDiff < destA.size() ? destA[firstDiff] : 0u,
              firstDiff < destA.size() ? destB[firstDiff] : 0u);
    }
}

DIFF_TEST(GimexFshWrite_GmCompress, kGmCompress)
{
    using Fn = uint32_t(__cdecl *)(const Info *, uint32_t, const uint32_t *, uint16_t *);
    const Fn original = ctx.Original<Fn>(kGmCompress);
    Fill fill(0xa016);
    for (int i = 0; i < 400; ++i) {
        std::vector<uint8_t> info(kInfoBytes);
        RandomInfo(fill, info);
        const int32_t width = 16 * fill.Range(1, 3), height = 16 * fill.Range(1, 3);
        SetWord(info, 0x10, static_cast<uint32_t>(width));
        SetWord(info, 0x14, static_cast<uint32_t>(height));
        const int32_t alphaPool[] = {0, 1, 2, 4, 5, 8};
        SetWord(info, 0x438, static_cast<uint32_t>(alphaPool[fill.Next() % 6]));
        const int32_t qualityPool[] = {-50, 0, 10, 25, 50, 75, 90, 99, 100, 101, 120, 1000};
        SetWord(info, 0x430, static_cast<uint32_t>(qualityPool[fill.Next() % 12]));
        const uint32_t stride = static_cast<uint32_t>(width) * 4;
        std::vector<uint32_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) + 4096);
        for (size_t k = 0; k < pixels.size(); ++k) pixels[k] = (i % 3 == 0) ? fill.Next() : (0x80402010u + static_cast<uint32_t>(k) * 0x01030507u + (fill.Next() & 0x07070707u));
        std::vector<uint8_t> outA(static_cast<size_t>(width) * static_cast<size_t>(height) * 8 + 4096), outB;
        fill.Bytes(outA);
        outB = outA;
        uint32_t ra = 0, rb = 0;
        const HeapEffect a = Observe(StackByte(i), [&] { ra = original(reinterpret_cast<const Info *>(info.data()), stride, pixels.data(), reinterpret_cast<uint16_t *>(outA.data())); });
        const HeapEffect b = Observe(StackByte(i), [&] { rb = GmCompress(reinterpret_cast<const Info *>(info.data()), stride, pixels.data(), reinterpret_cast<uint16_t *>(outB.data())); });
        CHECK(ctx, ra == rb && a == b && outA == outB, "GmCompress iter %d (%d x %d, quality %d, alpha %u): result %u / %u, heap %s, bytes %s", i, width, height,
              static_cast<int32_t>(*reinterpret_cast<uint32_t *>(info.data() + 0x430)), *reinterpret_cast<uint32_t *>(info.data() + 0x438), ra, rb,
              a == b ? "equal" : "differ", outA == outB ? "equal" : "differ");
    }
}
