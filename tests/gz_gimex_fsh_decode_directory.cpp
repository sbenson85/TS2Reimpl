// Differential tests for the FSH back end's directory side (lane MX; src/gz/GimexFshDirectory.size.cpp): the record copier, the entry
// parser, the directory builder and the stream reader. Each runs alone over seeded synthetic input, the original (by RVA, in the mapped
// image) against ours, with the codecs' allocator patched to a recording fake on both sides (every block carries 512 guard bytes,
// compared with the block) and the stack cleared before each call.
//
// The inputs are built the way the files are: records of every class the type byte gives (shape, palette, pairs, text, marker) with
// the sizes the record-size function (0x886b4e, verified on its own) gives them, chained by the length word at +4; shapes with mipmap
// counts of 1..4, plain, block-coded or RefPack-coded (a stream the compressor made); chains ended by a zero length or by a record the
// classifier does not know. Files for the reader come in the three layouts it reads: the old one (a 16-byte head, an entry count,
// offset / name pairs), chunk files ('ShpF' and 'FntF' chunks, a font chunk giving the offset of its records) and a bare record. Compared:
// results, the entry or directory (every word, pointers as allocation ordinals), the bytes of every block left allocated, the allocator's
// log, and for the reader the stream position and stray calls.
//
// Kept out, where the original crashes or reads memory the test does not control (ours does the same there):
// - a reader call that leaves the directory null (the face splitter 0x88800e then reads its count through null): files of under 16
//   bytes, a platform past 19 without a font head, a file with no chunk, a failed allocation;
// - a chunk whose records do not parse (the slot stays uninitialised and the splitter reads it);
// - a file cut inside a chunk header (the font chunk's chain offset is then read from the allocator's filler: a wild pointer) or
//   inside a RefPack record (the inflater has no bound). Truncated chains are covered by the entry parser's test.
#include "gz_gimex_fsh_decode_common.h"
#include "gz_gimex_stream.h"

#include "gz/GimexFshBlocks.h"

#include <algorithm>

namespace {

using namespace fshdecode;
using namespace gimex::fsh;
using gimexstream::FakeFile;
using gimexstream::Stream;

constexpr uint32_t kExtractRecord = 0x887967;
constexpr uint32_t kParseEntry = 0x8871b5;
constexpr uint32_t kBuildDirectory = 0x885f9b;
constexpr uint32_t kReadDirectory = 0x887a06;
constexpr uint32_t kFourcc = 0x53687046;
constexpr uint32_t kFontFourcc = 0x466e7446;

// The chunk readers leave the entry names null, and the builder reads the name of every entry it splits into mipmaps: the original
// crashes there. Files of those layouts hold single-level shapes.
bool gAllowMips = true;

struct Types {
    std::vector<uint8_t> byClass[16];
};

Types ScanTypes(int32_t platform)
{
    Types types;
    uint8_t record[0x40] = {};
    for (int type = 0; type < 256; ++type) {
        record[0] = static_cast<uint8_t>(type);
        const int32_t kind = ClassifyRecord(record, platform);
        if (kind >= 0 && kind < 16) types.byClass[kind].push_back(static_cast<uint8_t>(type));
    }
    return types;
}

bool IsBig(int32_t platform)
{
    return platform == 5 || platform == 7 || platform == 0xf || platform == 0x11;
}

uint8_t PickType(std::mt19937 &rng, const std::vector<uint8_t> &list)
{
    return list[rng() % list.size()];
}

// One record of the class `kind` (1 shape, 2 palette, 3 pairs, 4 text, 8 marker); empty when the platform has no type for it or the
// size the header gives is not reasonable. `pad` extra bytes follow, counted in the length word.
Buf MakeFshRecord(std::mt19937 &rng, int32_t platform, const Types &types, int kind, bool coded)
{
    if (types.byClass[kind].empty()) return {};
    const bool big = IsBig(platform);
    for (int attempt = 0; attempt < 60; ++attempt) {
        Buf header(0x40);
        RandomFill(rng, header.data(), header.size());
        header[0] = PickType(rng, types.byClass[kind]);
        uint32_t attribute = 0;
        int codec = 0; // 2 RefPack, 3 blocks
        switch (kind) {
        case 1: {
            const uint32_t levels = gAllowMips ? Pick(rng, {1, 1, 1, 2, 3, 4}) : 1;
            const uint32_t width = Pick(rng, {1, 2, 3, 4, 5, 8, 16, 17, 32}) << (levels - 1);
            const uint32_t height = Pick(rng, {1, 2, 3, 4, 5, 8, 16, 17, 32});
            Put(&header[0x18], width, 4, big);
            Put(&header[0x1c], height, 4, big);
            if (levels > 1) attribute |= (levels - 1) << 20;
            if (rng() % 8 == 0) attribute |= 0x4000;
            // The word at +0x14 gives the face splitter its level count (top 20 bits) and it rebuilds the shape from the size fields:
            // random values run it off the buffer in the original too. Faces are a cube strip (height six times the width, a power of
            // two) with 2 or 3 levels (also in the attribute word, which the record size follows); every other shape has no level count there.
            Put(&header[0x14], rng() & 0xfff, 4, big);
            if (levels == 1 && gAllowMips && rng() % 8 == 0) {
                const uint32_t side = Pick(rng, {8, 16, 32});
                const uint32_t faceLevels = 2 + rng() % 2;
                attribute |= 0x100 | ((faceLevels - 1) << 20); // the record size counts the levels
                Put(&header[0x18], side, 4, big);
                Put(&header[0x1c], side * 6, 4, big);
                Put(&header[0x14], ((faceLevels - 1) << 12) | (rng() & 0xfff), 4, big);
            }
            if (coded && rng() % 2) {
                attribute |= 2;
                codec = rng() % 2 ? 2 : 3;
            }
            Put(&header[8], 0x20, 4, big);
            break;
        }
        case 2:
            Put(&header[0x18], Pick(rng, {16, 256, 0, 2, 5, 200}), 4, big);
            if (rng() % 6 == 0) attribute |= 0x4000;
            break;
        case 3:
            Put(&header[0x1c], rng() % 6, 4, big);
            break;
        case 4:
            Put(&header[0xc], rng() % 40, 4, big);
            break;
        default:
            break;
        }
        Put(&header[1], attribute, 3, big);
        if (codec == 3) {
            static const uint8_t kMagic[] = {0x18, 0x1a, 0x1c};
            header[0x20] = kMagic[rng() % 3];
            header[0x21] = 0xfb;
        } else if (codec == 2) {
            header[0x20] = 0;
            header[0x21] = 0;
        }
        const int32_t size = RecordSize(header.data(), platform);
        if (size < 8 || size > 0x20000) continue;
        Buf record(static_cast<size_t>(size));
        RandomFill(rng, record.data(), record.size());
        std::memcpy(record.data(), header.data(), std::min<size_t>(record.size(), 0x20));
        if (codec == 3 && record.size() >= 0x28) {
            record[0x20] = header[0x20];
            record[0x21] = 0xfb;
        }
        if (codec == 2) {
            // pixels the record would hold, compressed
            Buf pixels(static_cast<size_t>(size) - 0x20);
            const uint32_t period = 1 + rng() % 9;
            for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = static_cast<uint8_t>(i % period + (rng() % 5 == 0 ? rng() : 0));
            Buf packed(pixels.size() * 2 + 64);
            ResetHeap(-1); // the encoder's tables come from the fake
            const int32_t written = RefPackEncodeWithHeader(packed.data(), pixels.data(), static_cast<int32_t>(pixels.size()), 0);
            ResetHeap(-1);
            if (written <= 5) continue;
            record.resize(0x20 + static_cast<size_t>(written));
            std::memcpy(&record[0x20], packed.data(), static_cast<size_t>(written));
        }
        const uint32_t pad = Pick(rng, {0, 0, 0, 4, 16});
        for (uint32_t i = 0; i < pad; ++i) record.push_back(static_cast<uint8_t>(rng()));
        Put(&record[4], static_cast<uint32_t>(record.size()), 4, big);
        return record;
    }
    return {};
}

// A chain of 1..5 records (the last ended by a zero length or by a record of no known class), with a margin of noise after it.
Buf MakeChain(std::mt19937 &rng, int32_t platform, const Types &types, bool coded, bool plain)
{
    const bool big = IsBig(platform);
    Buf chain;
    const int count = 1 + static_cast<int>(rng() % 5);
    int shapes = 0;
    for (int i = 0; i < count; ++i) {
        static const int kKinds[] = {1, 1, 2, 3, 4, 8, 1};
        int kind = i == 0 && rng() % 5 != 0 ? 1 : kKinds[rng() % 7];
        if (plain && i == 0) kind = 1;
        Buf record = MakeFshRecord(rng, platform, types, kind, coded);
        if (record.empty()) continue;
        if (kind == 1) ++shapes;
        chain.insert(chain.end(), record.begin(), record.end());
    }
    if (chain.empty()) return chain;
    if (rng() % 3 == 0 || types.byClass[0].empty()) {
        // a zero length ends the chain (the last record's own length word)
        size_t at = 0, last = 0;
        while (at + 8 <= chain.size()) {
            last = at;
            const uint32_t length = big ? (chain[at + 4] << 24 | chain[at + 5] << 16 | chain[at + 6] << 8 | chain[at + 7])
                                        : (chain[at + 4] | chain[at + 5] << 8 | chain[at + 6] << 16 | chain[at + 7] << 24);
            if (length == 0 || at + length >= chain.size()) break;
            at += length;
        }
        Put(&chain[last + 4], 0, 4, big);
    } else {
        Buf junk(0x20);
        RandomFill(rng, junk.data(), junk.size());
        junk[0] = PickType(rng, types.byClass[0]);
        chain.insert(chain.end(), junk.begin(), junk.end());
    }
    (void)shapes;
    return chain;
}

// Every word of an entry, pointers as ordinals (EntryWord).
std::vector<uint32_t> DescribeEntry(const Entry &entry)
{
    std::vector<uint32_t> words;
    const uint32_t *p = reinterpret_cast<const uint32_t *>(&entry);
    for (size_t i = 0; i < sizeof(Entry) / 4; ++i) words.push_back(EntryWord(p, i));
    return words;
}

} // namespace

// 0x887967: a record copied out of the file (RefPack inflated).
DIFF_TEST(GimexFshDirectory_ExtractRecord, 0x887967)
{
    using Fn = uint8_t *(__cdecl *)(const uint8_t *, const uint8_t *, int32_t *, int32_t);
    const Fn original = ctx.Original<Fn>(kExtractRecord);
    HeapFakes heap;
    std::mt19937 rng(0x887967);
    int refpack = 0, blocks = 0, tiny = 0, one = 0;
    for (int round = 0; round < 6000; ++round) {
        const int32_t platform = kPlatforms[rng() % (sizeof(kPlatforms) / 4)];
        const Types types = ScanTypes(platform);
        const int kind = static_cast<int>(Pick(rng, {1, 1, 1, 2, 3, 4, 8}));
        Buf record = MakeFshRecord(rng, platform, types, kind, true);
        if (record.empty()) continue;
        // a shape of damaged size fields: the size the record gives itself may be tiny, zero or negative (nothing is copied then)
        if (kind == 1 && rng() % 4 == 0 && RecordCodec(record.data(), platform) == 0) {
            static const int64_t kSides[] = {1, 2, 31, -1, -2, -31, 0, 5, 64};
            Put(&record[0x18], static_cast<uint32_t>(kSides[rng() % 9]), 4, IsBig(platform));
            Put(&record[0x1c], static_cast<uint32_t>(kSides[rng() % 9]), 4, IsBig(platform));
            Put(&record[1], 0, 3, IsBig(platform));
            if (rng() % 3 == 0) {
                // 31 x -1 pixels of 8 bits: the record is one byte long
                for (uint8_t type : types.byClass[1]) {
                    record[0] = type;
                    if (ShapeBits(record.data(), platform) == 8) {
                        Put(&record[0x18], 31, 4, IsBig(platform));
                        Put(&record[0x1c], 0xffffffffu, 4, IsBig(platform));
                        break;
                    }
                }
            }
            if (RecordSize(record.data(), platform) == 1) ++one;
            if (RecordSize(record.data(), platform) > static_cast<int32_t>(record.size())) continue;
            if (RecordSize(record.data(), platform) <= 1) ++tiny;
        }
        Buf file = record;
        file.resize(file.size() + 0x800);
        for (size_t i = record.size(); i < file.size(); ++i) file[i] = static_cast<uint8_t>(rng());
        // the end of the data bounds a block-coded record's copy (its size is 1.5 times the record's, plus the palette's tail)
        const uint8_t *end = file.data() + record.size() + Pick(rng, {0, 0, 0, 16, 100, 0x7ff});
        if (rng() % 8 == 0 && record.size() > 40) end = file.data() + record.size() - rng() % 30;
        const int codec = RecordCodec(file.data(), platform);
        if (codec == 2) ++refpack;
        if (codec == 3) ++blocks;
        const int failAt = rng() % 7 == 0 ? 0 : -1;
        int32_t bytes[2] = {0x5a5a5a5a, 0x5a5a5a5a};
        uint8_t *result[2];
        std::vector<uint32_t> snap[2], logs[2];
        for (int side = 0; side < 2; ++side) {
            ResetHeap(failAt);
            ClearStack();
            result[side] = side == 0 ? original(file.data(), end, &bytes[0], platform) : ExtractRecord(file.data(), end, &bytes[1], platform);
            logs[side] = gLog;
            snap[side] = Snapshot(nullptr, false);
            snap[side].push_back(Ordinal(result[side]));
        }
        ++ctx.cases;
        if ((result[0] == nullptr) != (result[1] == nullptr) || bytes[0] != bytes[1])
            ctx.Fail("round %d platform %d: original %s (%d bytes), ours %s (%d bytes)", round, platform, result[0] ? "a copy" : "null",
                     bytes[0], result[1] ? "a copy" : "null", bytes[1]);
        if (snap[0] != snap[1]) ctx.Fail("round %d platform %d: the blocks differ", round, platform);
        CompareLogs(ctx, "ExtractRecord", round, logs[0], logs[1]);
    }
    if (refpack < 100 || blocks < 100) ctx.Fail("coded records drawn too rarely: RefPack %d, blocks %d", refpack, blocks);
    if (one < 3) ctx.Fail("records of size 1 drawn only %d times", one);
    if (tiny < 5) ctx.Fail("records of size 1 or less drawn only %d times", tiny);
}

// 0x8871b5: one entry from a chain of records.
DIFF_TEST(GimexFshDirectory_ParseEntry, 0x8871b5)
{
    using Fn = int32_t(__cdecl *)(const uint8_t *, const uint8_t *, const uint8_t *, int32_t, const uint8_t *, Entry *, int32_t);
    const Fn original = ctx.Original<Fn>(kParseEntry);
    HeapFakes heap;
    std::mt19937 rng(0x8871b5);
    int successes = 0, palettes = 0, shapes = 0;
    for (int round = 0; round < 12000; ++round) {
        const int32_t platform = kPlatforms[rng() % (sizeof(kPlatforms) / 4)];
        const Types types = ScanTypes(platform);
        Buf chain = MakeChain(rng, platform, types, true, false);
        if (chain.empty()) continue;
        const size_t lead = rng() % 3 == 0 ? 0x10 : 0;
        Buf file(lead + chain.size() + 0x800);
        RandomFill(rng, file.data(), file.size());
        std::memcpy(&file[lead], chain.data(), chain.size());
        const size_t valid = lead + chain.size() - (rng() % 6 == 0 ? rng() % std::min<size_t>(chain.size(), 24) : 0);
        const uint8_t *base = file.data();
        const uint8_t *end = file.data() + valid;
        int32_t fileBytes = static_cast<int32_t>(valid);
        if (rng() % 10 == 0) fileBytes = static_cast<int32_t>(Pick(rng, {0, 1, 8, 0x20, static_cast<int64_t>(valid) + 5, 0xffffffff}));
        Buf palette;
        const uint8_t *defaultPalette = nullptr;
        if (rng() % 3 == 0) {
            palette = MakeFshRecord(rng, platform, types, 2, false);
            palette.resize(palette.size() + 0x800, 0);
            if (palette.size() > 0x800) defaultPalette = palette.data();
        }
        // a failed allocation of the filler shape is written through null by the original: only when a shape comes first
        const int failAt = rng() % 8 == 0 && ClassifyRecord(chain.data(), platform) == 1 ? static_cast<int>(rng() % 3) : -1;
        Entry init;
        RandomFill(rng, reinterpret_cast<uint8_t *>(&init), sizeof(init));
        Entry entry[2] = {init, init};
        int32_t result[2];
        std::vector<uint32_t> snap[2], logs[2], described[2];
        for (int side = 0; side < 2; ++side) {
            ResetHeap(failAt);
            ClearStack();
            result[side] = side == 0 ? original(file.data() + lead, base, end, fileBytes, defaultPalette, &entry[0], platform)
                                     : ParseEntry(file.data() + lead, base, end, fileBytes, defaultPalette, &entry[1], platform);
            logs[side] = gLog;
            snap[side] = Snapshot(nullptr, false);
            described[side] = DescribeEntry(entry[side]);
        }
        ++ctx.cases;
        if (result[0] != 0) ++successes;
        if (entry[0].palette != nullptr) ++palettes;
        if (entry[0].shape != nullptr) ++shapes;
        if (result[0] != result[1]) ctx.Fail("round %d platform %d: original %d, ours %d", round, platform, result[0], result[1]);
        if (described[0] != described[1]) {
            size_t first = 0;
            while (described[0][first] == described[1][first]) ++first;
            ctx.Fail("round %d platform %d: the entries differ first at +0x%zx (original %08x, ours %08x)", round, platform, first * 4,
                     described[0][first], described[1][first]);
        }
        if (snap[0] != snap[1]) ctx.Fail("round %d platform %d: the blocks differ", round, platform);
        CompareLogs(ctx, "ParseEntry", round, logs[0], logs[1]);
    }
    if (successes < 3000 || palettes < 500 || shapes < 3000)
        ctx.Fail("entries drawn too rarely: %d parsed, %d with palettes, %d with shapes", successes, palettes, shapes);
}

namespace {

uint8_t *AllocFilled(std::mt19937 &rng, uint32_t size)
{
    uint8_t *block = static_cast<uint8_t *>(FakeAllocate(size));
    RandomFill(rng, block, size);
    return block;
}

char *AllocName(std::mt19937 &rng)
{
    static const char kChars[] = "abcXYZ019_ ";
    const uint32_t length = rng() % 12;
    char *name = static_cast<char *>(FakeAllocate(length + 1));
    for (uint32_t i = 0; i < length; ++i) name[i] = kChars[rng() % (sizeof(kChars) - 1)];
    name[length] = 0;
    return name;
}

// A directory of 1..4 entries with shapes of 1..4 mipmap levels, all blocks from the recording allocator.
Directory *MakeDirectory(std::mt19937 &rng, int32_t platform, const Types &types)
{
    const bool big = IsBig(platform);
    const int count = 1 + static_cast<int>(rng() % 4);
    Directory *directory = reinterpret_cast<Directory *>(FakeAllocate(4 + sizeof(Entry) * count));
    std::memset(directory, 0, 4 + sizeof(Entry) * count);
    directory->count = count;
    for (int i = 0; i < count; ++i) {
        Entry &entry = directory->entries[i];
        RandomFill(rng, reinterpret_cast<uint8_t *>(&entry), sizeof(Entry));
        const uint32_t levels = Pick(rng, {1, 1, 2, 3, 4});
        uint8_t *record = AllocFilled(rng, 0x10000);
        std::vector<uint8_t> list = types.byClass[1];
        if (rng() % 4 == 0) {
            static const uint8_t kBlockTypes[] = {0x60, 0x61, 0x62, 0x1e};
            list.assign(kBlockTypes, kBlockTypes + 4);
        }
        record[0] = PickType(rng, list);
        uint32_t attribute = 0;
        if (levels > 1) attribute |= (levels - 1) << 20;
        if (rng() % 4 == 0) {
            attribute |= 2; // block-coded
            static const uint8_t kMagic[] = {0x18, 0x1a, 0x1c, 0x28};
            record[0x20] = kMagic[rng() % 4];
            record[0x21] = 0xfb;
        }
        if (rng() % 8 == 0) attribute |= 0x4000;
        Put(record + 1, attribute, 3, big);
        Put(record + 0x18, Pick(rng, {1, 2, 3, 4, 8, 16, 17, 32}) << (levels - 1), 4, big);
        Put(record + 0x1c, Pick(rng, {1, 2, 3, 4, 8, 16, 17, 32}), 4, big);
        entry.shape = record;
        entry.name = AllocName(rng);
        entry.palette = rng() % 3 ? AllocFilled(rng, 0x440) : nullptr;
        entry.paletteBytes = entry.palette != nullptr && rng() % 5 ? 0x400 + rng() % 0x40 : 0;
        if (entry.palette != nullptr && entry.paletteBytes > 0x440) entry.paletteBytes = 0x440;
        entry.pairs = rng() % 3 == 0 ? AllocFilled(rng, 0x40) : nullptr;
        entry.text = rng() % 3 == 0 ? AllocFilled(rng, 0x40) : nullptr;
        entry.marker = rng() % 3 == 0 ? AllocFilled(rng, 0x10) : nullptr;
        entry.userData = rng() % 3 == 0 ? AllocFilled(rng, 0x10) : nullptr;
    }
    return directory;
}

} // namespace

// 0x885f9b: the directory with each mipmap level an entry of its own.
DIFF_TEST(GimexFshDirectory_BuildDirectory, 0x885f9b)
{
    using Fn = Directory *(__cdecl *)(Directory *, int32_t, uint32_t);
    const Fn original = ctx.Original<Fn>(kBuildDirectory);
    HeapFakes heap;
    std::mt19937 rng(0x885f9b);
    int expanded = 0;
    for (int round = 0; round < 12000; ++round) {
        const int32_t platform = kPlatforms[rng() % (sizeof(kPlatforms) / 4)];
        const Types types = ScanTypes(platform);
        if (types.byClass[1].empty()) continue;
        static const uint32_t kMasks[] = {0, 1, 3, 7, 0xf, 0x1f, 0xff};
        const uint32_t alignMask = kMasks[rng() % 7];
        const int failAt = rng() % 9 == 0 ? 0 : -1;
        const uint32_t seed = rng();
        Directory *result[2];
        std::vector<uint32_t> snap[2], logs[2], raw[2], where[2];
        uint32_t ordinals[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 local(seed);
            ResetHeap(-1);
            Directory *directory = MakeDirectory(local, platform, types);
            gFailAt = failAt;
            gCalls = 0;
            gLog.clear();
            ClearStack();
            result[side] = side == 0 ? original(directory, platform, alignMask) : BuildDirectory(directory, platform, alignMask);
            logs[side] = gLog;
            ordinals[side] = Ordinal(result[side]);
            snap[side] = Snapshot(result[side], true);
            // For a failure's detail: the result's words raw, and where each lies among the side's blocks.
            raw[side].clear();
            where[side].clear();
            if (result[side] != nullptr) {
                const uint32_t *w = reinterpret_cast<const uint32_t *>(result[side]->entries);
                for (size_t k = 0; k < sizeof(Entry) / 4 * static_cast<size_t>(result[side]->count); ++k) {
                    raw[side].push_back(w[k]);
                    uint32_t found = 0xffffffffu;
                    for (size_t b = 0; b < gAllBlocks.size(); ++b) {
                        const uintptr_t at = reinterpret_cast<uintptr_t>(gAllBlocks[b]);
                        if (w[k] >= at && w[k] <= at + gSizes[b] + gGuard + 0x40)
                            found = static_cast<uint32_t>(b + 1) << 20 | (w[k] - static_cast<uint32_t>(at));
                    }
                    where[side].push_back(found);
                }
            }
        }
        ++ctx.cases;
        if (logs[0].size() > 40) ++expanded;
        if (ordinals[0] != ordinals[1]) ctx.Fail("round %d platform %d: original returned block %u, ours %u", round, platform, ordinals[0], ordinals[1]);
        if (snap[0] != snap[1]) {
            size_t first = 0;
            while (first < snap[0].size() && first < snap[1].size() && snap[0][first] == snap[1][first]) ++first;
            ctx.Fail("round %d platform %d: the directories or blocks differ afterwards (word %zu of %zu/%zu: %08x vs %08x)", round,
                     platform, first, snap[0].size(), snap[1].size(), first < snap[0].size() ? snap[0][first] : 0,
                     first < snap[1].size() ? snap[1][first] : 0);
            // The entry word it is (the snapshot ends with the entries) and its raw value and place on each side.
            const size_t fromEnd = snap[0].size() - first;
            if (snap[0].size() == snap[1].size() && fromEnd <= raw[0].size() && raw[0].size() == raw[1].size()) {
                const size_t k = raw[0].size() - fromEnd;
                char place[2][40];
                for (int side = 0; side < 2; ++side) {
                    if (where[side][k] == 0xffffffffu)
                        std::snprintf(place[side], sizeof(place[side]), "in no block");
                    else
                        std::snprintf(place[side], sizeof(place[side]), "block %u +0x%x", where[side][k] >> 20, where[side][k] & 0xfffff);
                }
                ctx.Fail("  entry %zu +0x%zx: original %08x (%s), ours %08x (%s)", k / (sizeof(Entry) / 4), k % (sizeof(Entry) / 4) * 4,
                         raw[0][k], place[0], raw[1][k], place[1]);
            }
        }
        CompareLogs(ctx, "BuildDirectory", round, logs[0], logs[1]);
    }
    if (expanded < 1000) ctx.Fail("only %d directories were expanded", expanded);
}

namespace {

enum class Layout { Old, Chunks, Font, Bare };

void PutLe32(Buf &b, size_t at, uint32_t v)
{
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<uint8_t>(v >> (8 * i));
}

// A chain that does not come out empty (a platform with no usable shape type gives none).
Buf MakePlainChain(std::mt19937 &rng, int32_t platform, const Types &types)
{
    for (int attempt = 0; attempt < 20; ++attempt) {
        Buf chain = MakeChain(rng, platform, types, true, true);
        if (!chain.empty() && ClassifyRecord(chain.data(), platform) == 1) return chain;
    }
    return {};
}

// A file of `layout` for `platform`, every entry's records parsing.
Buf MakeFile(std::mt19937 &rng, int32_t platform, const Types &types, Layout layout, int &entries)
{
    const bool big = IsBig(platform);
    entries = 1 + static_cast<int>(rng() % 4);
    Buf file;
    if (layout == Layout::Bare) {
        entries = 1;
        return MakePlainChain(rng, platform, types);
    }
    if (layout == Layout::Old) {
        file.assign(16, 0);
        RandomFill(rng, file.data(), 16);
        file[0] = 'S'; file[1] = 'h'; file[2] = 'p'; file[3] = 'F';
        std::vector<Buf> chains;
        std::vector<std::string> names;
        for (int i = 0; i < entries; ++i) {
            chains.push_back(MakePlainChain(rng, platform, types));
            if (chains.back().empty()) return {};
            std::string name;
            const uint32_t length = rng() % 10;
            for (uint32_t k = 0; k < length; ++k) name.push_back(static_cast<char>('a' + rng() % 26));
            names.push_back(name);
        }
        // the table, then the chains
        size_t table = 16;
        for (int i = 0; i < entries; ++i) table += 8 + names[i].size() + 1;
        size_t at = (table + 3) & ~size_t(3);
        std::vector<size_t> offsets;
        for (int i = 0; i < entries; ++i) {
            offsets.push_back(at);
            at += chains[i].size();
            at = (at + 3) & ~size_t(3);
        }
        const uint32_t declared = entries;
        for (int k = 0; k < 4; ++k) file[8 + k] = static_cast<uint8_t>(declared >> (24 - 8 * k)); // big-endian count
        for (int i = 0; i < entries; ++i) {
            const uint8_t off[4] = {static_cast<uint8_t>(offsets[i] >> 24), static_cast<uint8_t>(offsets[i] >> 16),
                                    static_cast<uint8_t>(offsets[i] >> 8), static_cast<uint8_t>(offsets[i])};
            file.insert(file.end(), off, off + 4);
            for (int k = 0; k < 4; ++k) file.push_back(static_cast<uint8_t>(rng()));
            file.insert(file.end(), names[i].begin(), names[i].end());
            file.push_back(0);
        }
        file.resize(at, 0);
        for (int i = 0; i < entries; ++i) std::memcpy(&file[offsets[i]], chains[i].data(), chains[i].size());
        return file;
    }
    // chunks
    for (int i = 0; i < entries; ++i) {
        Buf chain = MakePlainChain(rng, platform, types);
        if (chain.empty()) return {};
        const bool font = layout == Layout::Font || (i != 0 && rng() % 4 == 0);
        const size_t header = font ? 0x40 : 8;
        Buf chunk(header + chain.size());
        RandomFill(rng, chunk.data(), header);
        const uint32_t code = font ? kFontFourcc : kFourcc;
        for (int k = 0; k < 4; ++k) chunk[k] = static_cast<uint8_t>(code >> (24 - 8 * k));
        if (i == 0 && layout == Layout::Font) {
            chunk[0] = 'F'; chunk[1] = 'n'; chunk[2] = 't'; chunk[3] = 'F';
        }
        while (chunk.size() % 4 != 0) chunk.push_back(0);
        PutLe32(chunk, 4, static_cast<uint32_t>(chunk.size()));
        if (font) Put(&chunk[0x30], static_cast<uint32_t>(header), 4, big);
        std::memcpy(&chunk[header], chain.data(), chain.size());
        file.insert(file.end(), chunk.begin(), chunk.end());
    }
    return file;
}

} // namespace

// 0x887a06: the stream reader over files of all three layouts.
DIFF_TEST(GimexFshDirectory_ReadDirectoryStream, 0x887a06)
{
    using Fn = int32_t(__cdecl *)(ifaces::cIGZFile *, Directory **, int32_t, int32_t, uint32_t, uint32_t, uint32_t);
    const Fn original = ctx.Original<Fn>(kReadDirectory);
    HeapFakes heap;
    gGuard = 0x10000; // the builder copies whole padded images out of records that hold less
    struct Restore { ~Restore() { gGuard = kSlack; } } restore;
    std::mt19937 rng(0x887a06);
    int perLayout[4] = {0, 0, 0, 0};
    for (int round = 0; round < 6000; ++round) {
        const Layout layout = static_cast<Layout>(rng() % 4);
        int32_t platform;
        switch (layout) {
        case Layout::Old: platform = static_cast<int32_t>(rng() % 10); break;
        case Layout::Chunks: platform = 10 + static_cast<int32_t>(rng() % 10); break;
        case Layout::Font: platform = static_cast<int32_t>(rng() % 24); break;
        default: platform = 10 + static_cast<int32_t>(rng() % 10); break;
        }
        const Types types = ScanTypes(platform);
        if (types.byClass[1].empty()) continue;
        int entries = 0;
        gAllowMips = layout == Layout::Old;
        Buf file = MakeFile(rng, platform, types, layout, entries);
        if (file.size() < 16) continue;
        const uint32_t alignMask = Pick(rng, {0, 0, 0, 3, 0xf, 0x1f});
        const uint32_t seedOffset = static_cast<uint32_t>(rng() % 5);
        Directory *out[2] = {nullptr, nullptr};
        int32_t result[2];
        std::vector<uint32_t> snap[2], logs[2];
        for (int side = 0; side < 2; ++side) {
            Stream stream(file);
            stream.file.pos = seedOffset;
            ResetHeap(-1);
            ClearStack();
            result[side] = side == 0 ? original(stream.Get(), &out[0], 1, platform, kFourcc, kFontFourcc, alignMask)
                                     : ReadDirectoryStream(stream.Get(), &out[1], 1, platform, kFourcc, kFontFourcc, alignMask);
            logs[side] = gLog;
            snap[side] = Snapshot(out[side], true);
            snap[side].push_back(Ordinal(out[side]));
            snap[side].push_back(stream.file.pos);
            snap[side].push_back(static_cast<uint32_t>(stream.file.stray));
        }
        ++ctx.cases;
        ++perLayout[static_cast<int>(layout)];
        if (result[0] != result[1]) ctx.Fail("round %d (layout %d platform %d): original %d, ours %d", round, static_cast<int>(layout), platform, result[0], result[1]);
        if (snap[0] != snap[1]) {
            size_t first = 0;
            while (first < snap[0].size() && first < snap[1].size() && snap[0][first] == snap[1][first]) ++first;
            const size_t block = first - first % 3;
            ctx.Fail("round %d (layout %d platform %d, %zu bytes): the directories, blocks or stream differ (word %zu of %zu/%zu: %08x vs %08x; "
                     "at block %u of size %u)",
                     round, static_cast<int>(layout), platform, file.size(), first, snap[0].size(), snap[1].size(),
                     first < snap[0].size() ? snap[0][first] : 0, first < snap[1].size() ? snap[1][first] : 0,
                     block + 1 < snap[0].size() ? snap[0][block] : 0, block + 1 < snap[0].size() ? snap[0][block + 1] : 0);
        }
        CompareLogs(ctx, "ReadDirectoryStream", round, logs[0], logs[1]);
    }
    for (int i = 0; i < 4; ++i)
        if (perLayout[i] < 300) ctx.Fail("layout %d drawn only %d times", i, perLayout[i]);
}
