// Differential tests for the FSH back end's record helpers (lane MX; src/gz/GimexFshRecords.size.cpp, GimexFshRecords.cpp): the
// classifiers, sizes, mipmap counts, attachment readers, the six-face layout and the directory splitter and free. Each function
// runs alone over seeded synthetic records (every type byte 0..255 against the platforms 0..0x14, 0x21 and -1, attribute words
// drawn to hit each bit, sizes and dimensions from the edge values, the codec magics at +0x20), the original (by RVA, in the
// mapped image) against ours. Compared: results (the whole register where the original sets it), every byte written (a record
// the call changes, the info block, out parameters), the allocator's log (the sizes asked for, the frees) and the blocks left.
//
// Kept out: a record chain the original reads past the end of its buffer (the buffers here are 0x600 bytes: the sizes in the
// header can pass that, and ours reads the same bytes the original does there, but the test would read heap garbage).
#include "gz_gimex_fsh_decode_common.h"

#include <cmath>

namespace {

using namespace fshdecode;
using namespace gimex::fsh;

constexpr uint32_t kClassifyRecord = 0x88677e;
constexpr uint32_t kShapeBits = 0x886956;
constexpr uint32_t kHasCodec = 0x88888f;
constexpr uint32_t kRecordCodec = 0x8888e7;
constexpr uint32_t kMipLevels = 0x88659e;
constexpr uint32_t kMipLevelsAt14 = 0x887ea7;
constexpr uint32_t kRecordLength = 0x88663b;
constexpr uint32_t kIsSwizzled = 0x88824b;
constexpr uint32_t kIsKnownRecord = 0x888265;
constexpr uint32_t kIsCode = 0x88827a;
constexpr uint32_t kPaletteEntries = 0x8881f8;
constexpr uint32_t kRecordSize = 0x886b4e;
constexpr uint32_t kReadShapeText = 0x886652;
constexpr uint32_t kReadShapeExtra = 0x886680;
constexpr uint32_t kReadShapePairs = 0x8866ae;
constexpr uint32_t kReadShapeMarker = 0x88675f;
constexpr uint32_t kFaceOffset = 0x885d54;
constexpr uint32_t kNextLevelOffset = 0x885e69;
constexpr uint32_t kAllocateFaces = 0x885c25;
constexpr uint32_t kBlockBits = 0x885cba;
constexpr uint32_t kIsTiled = 0x885def;
constexpr uint32_t kSplitFaces = 0x88800e;
constexpr uint32_t kFreeDirectory = 0x885f08;
constexpr uint32_t kRowBytes = 0x885e88;
constexpr uint32_t kImageBytes = 0x885eee;
constexpr uint32_t kRoundToByte = 0x885d30;

std::vector<int32_t> Platforms()
{
    std::vector<int32_t> all;
    for (int32_t p : kPlatforms) all.push_back(p);
    return all;
}

// Runs `body(type, platform, record)` over every type byte and platform with a fresh random record each.
template <class Body>
void ForEveryRecord(std::mt19937 &rng, int repeats, Body body)
{
    for (int round = 0; round < repeats; ++round)
        for (int type = 0; type < 256; ++type)
            for (int32_t platform : kPlatforms) {
                Buf record = MakeRecord(rng, type, platform == 5 || platform == 7 || platform == 0xf || platform == 0x11 ? rng() % 8 != 0 : rng() % 8 == 0);
                body(type, platform, record);
            }
}

} // namespace

// 0x88677e: the record's class by its type byte.
DIFF_TEST(GimexFshRecords_ClassifyRecord, 0x88677e)
{
    using Fn = int32_t(__cdecl *)(const uint8_t *, int32_t);
    const Fn original = ctx.Original<Fn>(kClassifyRecord);
    std::mt19937 rng(0x88677e);
    ForEveryRecord(rng, 1, [&](int type, int32_t platform, Buf &record) {
        ++ctx.cases;
        ClearStack();
        const int32_t a = original(record.data(), platform);
        ClearStack();
        const int32_t b = ClassifyRecord(record.data(), platform);
        if (a != b) ctx.Fail("type %02x platform %d: original %d, ours %d", type, platform, a, b);
    });
}

// 0x886956: the bits per pixel by type byte.
DIFF_TEST(GimexFshRecords_ShapeBits, 0x886956)
{
    using Fn = int32_t(__cdecl *)(const uint8_t *, int32_t);
    const Fn original = ctx.Original<Fn>(kShapeBits);
    std::mt19937 rng(0x886956);
    ForEveryRecord(rng, 1, [&](int type, int32_t platform, Buf &record) {
        ++ctx.cases;
        ClearStack();
        const int32_t a = original(record.data(), platform);
        ClearStack();
        const int32_t b = ShapeBits(record.data(), platform);
        if (a != b) ctx.Fail("type %02x platform %d: original %d, ours %d", type, platform, a, b);
    });
}

// 0x88888f: whether the pixels are coded the way a codec says.
DIFF_TEST(GimexFshRecords_HasCodec, 0x88888f)
{
    using Fn = uint32_t(__cdecl *)(const uint8_t *, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kHasCodec);
    std::mt19937 rng(0x88888f);
    for (int round = 0; round < 60000; ++round) {
        const int32_t platform = kPlatforms[rng() % (sizeof(kPlatforms) / 4)];
        Buf record = MakeRecord(rng, rng() & 0xff, rng() % 2 == 0);
        const int32_t codec = static_cast<int32_t>(Pick(rng, {0, 1, 2, 3, 4, 5, 2, 3, 4, 0xffffffff}));
        ++ctx.cases;
        ClearStack();
        const uint32_t a = original(record.data(), codec, platform);
        ClearStack();
        const uint32_t b = HasCodec(record.data(), codec, platform);
        if (a != b) ctx.Fail("codec %d platform %d (attr %02x%02x%02x, magic %02x%02x): original %u, ours %u", codec, platform,
                              record[1], record[2], record[3], record[0x20], record[0x21], a, b);
    }
}

// 0x8888e7: which codec the record uses.
DIFF_TEST(GimexFshRecords_RecordCodec, 0x8888e7)
{
    using Fn = int32_t(__cdecl *)(const uint8_t *, int32_t);
    const Fn original = ctx.Original<Fn>(kRecordCodec);
    std::mt19937 rng(0x8888e7);
    for (int round = 0; round < 60000; ++round) {
        const int32_t platform = kPlatforms[rng() % (sizeof(kPlatforms) / 4)];
        Buf record = MakeRecord(rng, rng() & 0xff, rng() % 2 == 0);
        const uint8_t *pointer = round % 50 == 0 ? nullptr : record.data();
        ++ctx.cases;
        ClearStack();
        const int32_t a = original(pointer, platform);
        ClearStack();
        const int32_t b = RecordCodec(pointer, platform);
        if (a != b) ctx.Fail("platform %d: original %d, ours %d", platform, a, b);
    }
}

// 0x88659e: the mipmap count (and the store of a new count into the attribute word).
DIFF_TEST(GimexFshRecords_MipLevels, 0x88659e)
{
    using Fn = int32_t(__cdecl *)(uint8_t *, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kMipLevels);
    std::mt19937 rng(0x88659e);
    for (int round = 0; round < 80000; ++round) {
        const int32_t platform = kPlatforms[rng() % (sizeof(kPlatforms) / 4)];
        Buf a = MakeRecord(rng, rng() & 0xff, rng() % 2 == 0);
        Buf b = a;
        if (rng() % 2) {
            const uint32_t attr = rng();
            const bool big = platform == 5 || platform == 7 || platform == 0xf || platform == 0x11;
            Put(&a[1], attr & 0xffffff, 3, big);
            b = a;
        }
        const int32_t set = static_cast<int32_t>(Pick(rng, {0, 0, 0, 1, 2, 3, 4, 5, 8, 15, 16, 0xffffffff, 100}));
        const bool null = round % 100 == 0;
        ++ctx.cases;
        ClearStack();
        const int32_t x = original(null ? nullptr : a.data(), set, platform);
        ClearStack();
        const int32_t y = MipLevels(null ? nullptr : b.data(), set, platform);
        if (x != y) ctx.Fail("set %d platform %d: original %d, ours %d", set, platform, x, y);
        CompareBytes(ctx, "MipLevels", round, a.data(), b.data(), a.size());
    }
}

// 0x887ea7: the mipmap count read from +0x14.
DIFF_TEST(GimexFshRecords_MipLevelsAt14, 0x887ea7)
{
    using Fn = int32_t(__cdecl *)(const uint8_t *, int32_t);
    const Fn original = ctx.Original<Fn>(kMipLevelsAt14);
    std::mt19937 rng(0x887ea7);
    for (int round = 0; round < 80000; ++round) {
        const int32_t platform = kPlatforms[rng() % (sizeof(kPlatforms) / 4)];
        Buf record = MakeRecord(rng, rng() & 0xff, rng() % 2 == 0);
        const bool big = platform == 5 || platform == 7 || platform == 0xf || platform == 0x11;
        if (rng() % 2) Put(&record[0x14], rng(), 4, big);
        const uint8_t *pointer = round % 100 == 0 ? nullptr : record.data();
        ++ctx.cases;
        ClearStack();
        const int32_t a = original(pointer, platform);
        ClearStack();
        const int32_t b = MipLevelsAt14(pointer, platform);
        if (a != b) ctx.Fail("platform %d: original %d, ours %d", platform, a, b);
    }
}

// 0x88663b: the word at +4.
DIFF_TEST(GimexFshRecords_RecordLength, 0x88663b)
{
    using Fn = uint32_t(__cdecl *)(const uint8_t *, int32_t);
    const Fn original = ctx.Original<Fn>(kRecordLength);
    std::mt19937 rng(0x88663b);
    ForEveryRecord(rng, 1, [&](int type, int32_t platform, Buf &record) {
        ++ctx.cases;
        ClearStack();
        const uint32_t a = original(record.data(), platform);
        ClearStack();
        const uint32_t b = RecordLength(record.data(), platform);
        if (a != b) ctx.Fail("type %02x platform %d: original %08x, ours %08x", type, platform, a, b);
    });
}

// 0x88824b: attribute bit 14 (only al).
DIFF_TEST(GimexFshRecords_IsSwizzled, 0x88824b)
{
    using Fn = bool(__cdecl *)(const uint8_t *, int32_t);
    const Fn original = ctx.Original<Fn>(kIsSwizzled);
    std::mt19937 rng(0x88824b);
    for (int round = 0; round < 40000; ++round) {
        const int32_t platform = kPlatforms[rng() % (sizeof(kPlatforms) / 4)];
        Buf record = MakeRecord(rng, rng() & 0xff, rng() % 2 == 0);
        ++ctx.cases;
        ClearStack();
        const bool a = original(record.data(), platform);
        ClearStack();
        const bool b = IsSwizzled(record.data(), platform);
        if (a != b) ctx.Fail("platform %d attr %02x%02x%02x: original %d, ours %d", platform, record[1], record[2], record[3], a, b);
    }
}

// 0x888265: whether the record's class is not 0 (only al).
DIFF_TEST(GimexFshRecords_IsKnownRecord, 0x888265)
{
    using Fn = bool(__cdecl *)(const uint8_t *, int32_t);
    const Fn original = ctx.Original<Fn>(kIsKnownRecord);
    std::mt19937 rng(0x888265);
    ForEveryRecord(rng, 1, [&](int type, int32_t platform, Buf &record) {
        ++ctx.cases;
        ClearStack();
        const bool a = original(record.data(), platform);
        ClearStack();
        const bool b = IsKnownRecord(record.data(), platform);
        if (a != b) ctx.Fail("type %02x platform %d: original %d, ours %d", type, platform, a, b);
    });
}

// 0x88827a: whether the first big-endian word is the code (only al).
DIFF_TEST(GimexFshRecords_IsCode, 0x88827a)
{
    using Fn = bool(__cdecl *)(const uint8_t *, int32_t);
    const Fn original = ctx.Original<Fn>(kIsCode);
    std::mt19937 rng(0x88827a);
    for (int round = 0; round < 60000; ++round) {
        uint8_t word[8];
        RandomFill(rng, word, 8);
        uint32_t code = rng();
        if (round % 3 == 0) code = (word[0] << 24) | (word[1] << 16) | (word[2] << 8) | word[3];
        if (round % 7 == 0) code ^= 1u << (rng() % 32);
        ++ctx.cases;
        ClearStack();
        const bool a = original(word, static_cast<int32_t>(code));
        ClearStack();
        const bool b = IsCode(word, static_cast<int32_t>(code));
        if (a != b) ctx.Fail("code %08x: original %d, ours %d", code, a, b);
    }
}

// 0x8881f8: the entries of a palette record and *start.
DIFF_TEST(GimexFshRecords_PaletteEntries, 0x8881f8)
{
    using Fn = int32_t(__cdecl *)(const uint8_t *, int32_t *, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kPaletteEntries);
    std::mt19937 rng(0x8881f8);
    for (int round = 0; round < 80000; ++round) {
        const int32_t platform = kPlatforms[rng() % (sizeof(kPlatforms) / 4)];
        Buf record = MakeRecord(rng, rng() & 0xff, rng() % 2 == 0);
        const bool big = platform == 5 || platform == 7 || platform == 0xf || platform == 0x11;
        if (rng() % 2) Put(&record[0x18], Pick(rng, {0, 1, 2, 15, 16, 17, 255, 256, 257, 0x1000, 0xffffffff, 0x80000000}), 4, big);
        const int32_t bits = static_cast<int32_t>(Pick(rng, {0, 1, 2, 3, 4, 5, 8, 9, 12, 15, 16, 24, 32, 0xffffffff}));
        int32_t startA = 0x5a5a5a5a, startB = 0x5a5a5a5a;
        ++ctx.cases;
        ClearStack();
        const int32_t a = original(record.data(), &startA, bits, platform);
        ClearStack();
        const int32_t b = PaletteEntries(record.data(), &startB, bits, platform);
        if (a != b || startA != startB)
            ctx.Fail("bits %d platform %d: original %d (start %d), ours %d (start %d)", bits, platform, a, startA, b, startB);
    }
}

// 0x886b4e: the bytes a record takes in the file.
DIFF_TEST(GimexFshRecords_RecordSize, 0x886b4e)
{
    using Fn = int32_t(__cdecl *)(const uint8_t *, int32_t);
    const Fn original = ctx.Original<Fn>(kRecordSize);
    std::mt19937 rng(0x886b4e);
    ForEveryRecord(rng, 6, [&](int type, int32_t platform, Buf &record) {
        ++ctx.cases;
        ClearStack();
        const int32_t a = original(record.data(), platform);
        ClearStack();
        const int32_t b = RecordSize(record.data(), platform);
        if (a != b) ctx.Fail("type %02x platform %d attr %02x%02x%02x: original %d, ours %d", type, platform, record[1], record[2], record[3], a, b);
    });
}

// 0x885e88 / 0x885eee: the bytes of a row and of an image.
DIFF_TEST(GimexFshRecords_RowBytes, 0x885e88)
{
    using Fn = int32_t(__cdecl *)(int32_t, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kRowBytes);
    std::mt19937 rng(0x885e88);
    for (int round = 0; round < 100000; ++round) {
        const int32_t width = static_cast<int32_t>(round < 2000 ? round : Pick(rng, {0, 1, 7, 8, 9, 15, 16, 17, 100, 0x7fffffff, 0x80000000, 0xffffffff, rng()}));
        const int32_t bits = static_cast<int32_t>(round % 64 < 40 ? round % 64 : rng() % 80);
        const int32_t platform = kPlatforms[rng() % (sizeof(kPlatforms) / 4)];
        ++ctx.cases;
        ClearStack();
        const int32_t a = original(width, bits, platform);
        ClearStack();
        const int32_t b = RowBytes(width, bits, platform);
        if (a != b) ctx.Fail("width %d bits %d platform %d: original %d, ours %d", width, bits, platform, a, b);
    }
}

DIFF_TEST(GimexFshRecords_ImageBytes, 0x885eee)
{
    using Fn = int32_t(__cdecl *)(int32_t, int32_t, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kImageBytes);
    std::mt19937 rng(0x885eee);
    for (int round = 0; round < 100000; ++round) {
        const int32_t width = static_cast<int32_t>(Pick(rng, {0, 1, 7, 8, 9, 15, 16, 17, 100, 255, 256, 0x1000, 0x7fffffff, 0xffffffff, rng() % 70}));
        const int32_t rows = static_cast<int32_t>(Pick(rng, {0, 1, 2, 3, 100, 0x7fffffff, 0xffffffff, rng() % 70}));
        const int32_t bits = static_cast<int32_t>(round % 64 < 40 ? round % 64 : rng() % 80);
        const int32_t platform = kPlatforms[rng() % (sizeof(kPlatforms) / 4)];
        ++ctx.cases;
        ClearStack();
        const int32_t a = original(width, rows, bits, platform);
        ClearStack();
        const int32_t b = ImageBytes(width, rows, bits, platform);
        if (a != b) ctx.Fail("width %d rows %d bits %d platform %d: original %d, ours %d", width, rows, bits, platform, a, b);
    }
}

// 0x885d30 (speed-built): value + 0.5 truncated, clamped to 0..255.
DIFF_TEST(GimexFshRecords_RoundToByte, 0x885d30)
{
    using Fn = int32_t(__cdecl *)(float);
    const Fn original = ctx.Original<Fn>(kRoundToByte);
    std::mt19937 rng(0x885d30);
    for (int round = 0; round < 200000; ++round) {
        float value;
        switch (round % 5) {
        case 0: value = static_cast<float>(static_cast<int32_t>(rng() % 700) - 200) / 2.0f; break;
        case 1: value = static_cast<float>(static_cast<int32_t>(rng() % 70000) - 20000) / 100.0f; break;
        case 2: {
            const uint32_t bits = rng();
            std::memcpy(&value, &bits, 4);
            break;
        }
        case 3: value = static_cast<float>(round / 5 % 600) - 100.0f + (rng() % 2 ? 0.5f : -0.5f); break;
        default: value = static_cast<float>(rng() % 3000) / 7.0f - 100.0f; break;
        }
        ++ctx.cases;
        ClearStack();
        const int32_t a = original(value);
        ClearStack();
        const int32_t b = ClampRound(value); // MV's 0x885d30 (MX's duplicate removed at merge)
        if (a != b) ctx.Fail("value %g: original %d, ours %d", value, a, b);
    }
}

namespace {

// A directory of `count` entries over the buffers the caller keeps; the info block for the attachment readers.
struct Dir {
    Buf bytes;
    Directory *Get() { return reinterpret_cast<Directory *>(bytes.data()); }
    explicit Dir(int count) : bytes(4 + sizeof(Entry) * count + 8, 0) { *reinterpret_cast<int32_t *>(bytes.data()) = count; }
    Entry &At(int i) { return Get()->entries[i]; }
};

} // namespace

// 0x886652: the text record's string to info + 0x65c.
DIFF_TEST(GimexFshRecords_ReadShapeText, 0x886652)
{
    using Fn = void(__cdecl *)(const Directory *, int32_t, uint8_t *);
    const Fn original = ctx.Original<Fn>(kReadShapeText);
    std::mt19937 rng(0x886652);
    for (int round = 0; round < 3000; ++round) {
        Dir dir(3);
        std::vector<Buf> texts;
        for (int i = 0; i < 3; ++i) {
            Buf text(0x10 + 0x200);
            RandomFill(rng, text.data(), text.size());
            const size_t length = rng() % 0x100;
            for (size_t k = 0; k < length; ++k) text[0x10 + k] = static_cast<uint8_t>(1 + rng() % 255);
            text[0x10 + length] = 0;
            texts.push_back(text);
        }
        for (int i = 0; i < 3; ++i) dir.At(i).text = rng() % 4 ? texts[i].data() : nullptr;
        Buf infoA(0x2a7c), infoB;
        RandomFill(rng, infoA.data(), infoA.size());
        infoB = infoA;
        const int32_t frame = static_cast<int32_t>(rng() % 3);
        ++ctx.cases;
        ClearStack();
        original(dir.Get(), frame, infoA.data());
        ClearStack();
        ReadShapeText(dir.Get(), frame, infoB.data());
        CompareBytes(ctx, "ReadShapeText", round, infoA.data(), infoB.data(), infoA.size());
    }
}

// 0x886680 (speed-built): info + 0x2a78 = the extra record's word at +0xc.
DIFF_TEST(GimexFshRecords_ReadShapeExtra, 0x886680)
{
    using Fn = void(__cdecl *)(const Directory *, int32_t, uint8_t *, int32_t);
    const Fn original = ctx.Original<Fn>(kReadShapeExtra);
    std::mt19937 rng(0x886680);
    for (int round = 0; round < 3000; ++round) {
        Dir dir(3);
        std::vector<Buf> extras;
        for (int i = 0; i < 3; ++i) {
            Buf extra(0x20);
            RandomFill(rng, extra.data(), extra.size());
            extras.push_back(extra);
        }
        for (int i = 0; i < 3; ++i) dir.At(i).userData = rng() % 4 ? extras[i].data() : nullptr;
        Buf infoA(0x2a7c), infoB;
        RandomFill(rng, infoA.data(), infoA.size());
        infoB = infoA;
        const int32_t frame = static_cast<int32_t>(rng() % 3);
        const int32_t platform = kPlatforms[rng() % (sizeof(kPlatforms) / 4)];
        ++ctx.cases;
        ClearStack();
        original(dir.Get(), frame, infoA.data(), platform);
        ClearStack();
        ReadShapeExtra(dir.Get(), frame, infoB.data(), platform);
        CompareBytes(ctx, "ReadShapeExtra", round, infoA.data(), infoB.data(), infoA.size());
    }
}

// 0x8866ae: the block of pairs into the info.
DIFF_TEST(GimexFshRecords_ReadShapePairs, 0x8866ae)
{
    using Fn = int32_t(__cdecl *)(const Directory *, int32_t, uint8_t *, int32_t);
    const Fn original = ctx.Original<Fn>(kReadShapePairs);
    std::mt19937 rng(0x8866ae);
    for (int round = 0; round < 6000; ++round) {
        Dir dir(3);
        std::vector<Buf> blocks;
        const int32_t platform = kPlatforms[rng() % (sizeof(kPlatforms) / 4)];
        const bool big = platform == 5 || platform == 7 || platform == 0xf || platform == 0x11;
        for (int i = 0; i < 3; ++i) {
            Buf block(0x20 + 8 * 0x400 + 16);
            RandomFill(rng, block.data(), block.size());
            Put(&block[0x1c], Pick(rng, {0, 1, 2, 3, 0x40, 0x3ff, 0x400, 0x401, 0x7fffffff, 0x80000000, 0xffffffff, rng() % 20}), 4, big);
            blocks.push_back(block);
        }
        for (int i = 0; i < 3; ++i) dir.At(i).pairs = rng() % 5 ? blocks[i].data() : nullptr;
        Buf infoA(0x2a7c), infoB;
        RandomFill(rng, infoA.data(), infoA.size());
        infoB = infoA;
        const int32_t frame = static_cast<int32_t>(rng() % 3);
        ++ctx.cases;
        ClearStack();
        const int32_t a = original(dir.Get(), frame, infoA.data(), platform);
        ClearStack();
        const int32_t b = ReadShapePairs(dir.Get(), frame, infoB.data(), platform);
        if (a != b) ctx.Fail("round %d: original %d, ours %d", round, a, b);
        CompareBytes(ctx, "ReadShapePairs", round, infoA.data(), infoB.data(), infoA.size());
    }
}

// 0x88675f: info flag 0x200000 for a marker.
DIFF_TEST(GimexFshRecords_ReadShapeMarker, 0x88675f)
{
    using Fn = void(__cdecl *)(const Directory *, int32_t, uint8_t *);
    const Fn original = ctx.Original<Fn>(kReadShapeMarker);
    std::mt19937 rng(0x88675f);
    for (int round = 0; round < 2000; ++round) {
        Dir dir(3);
        Buf marker(8);
        for (int i = 0; i < 3; ++i) dir.At(i).marker = rng() % 2 ? marker.data() : nullptr;
        Buf infoA(0x2a7c), infoB;
        RandomFill(rng, infoA.data(), infoA.size());
        infoB = infoA;
        const int32_t frame = static_cast<int32_t>(rng() % 3);
        ++ctx.cases;
        ClearStack();
        original(dir.Get(), frame, infoA.data());
        ClearStack();
        ReadShapeMarker(dir.Get(), frame, infoB.data());
        CompareBytes(ctx, "ReadShapeMarker", round, infoA.data(), infoB.data(), infoA.size());
    }
}

// 0x885d54: the offset of a face at a level in the six-face layout.
DIFF_TEST(GimexFshRecords_FaceOffset, 0x885d54)
{
    using Fn = int32_t(__cdecl *)(int32_t, int32_t, uint32_t, int32_t, int32_t *, int32_t, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kFaceOffset);
    std::mt19937 rng(0x885d54);
    for (int round = 0; round < 100000; ++round) {
        const int32_t levelCount = static_cast<int32_t>(Pick(rng, {1, 2, 3, 5, 8, 12, 0, 0xffffffff, rng() % 14}));
        const int32_t level = static_cast<int32_t>(Pick(rng, {0, 1, 2, 3, 4, 7, 11, 0xffffffff, rng() % 14}));
        const int32_t face = static_cast<int32_t>(rng() % 8);
        const uint32_t alignMask = Pick(rng, {0, 1, 3, 7, 0xf, 0x1f, 0x7f, 0xff, 0x3f});
        const int32_t width = static_cast<int32_t>(Pick(rng, {0, 1, 2, 3, 4, 8, 16, 33, 64, 128, 256, 1024, rng() % 300}));
        const int32_t height = static_cast<int32_t>(Pick(rng, {0, 1, 2, 3, 4, 8, 16, 33, 64, 128, 256, 1024, rng() % 300}));
        const int32_t bits = static_cast<int32_t>(Pick(rng, {0, 4, 8, 12, 15, 16, 24, 32, 1, 2, 64, rng() % 40}));
        int32_t bytesA = 0x5a5a5a5a, bytesB = 0x5a5a5a5a;
        ++ctx.cases;
        ClearStack();
        const int32_t a = original(level, face, alignMask, levelCount, &bytesA, width, height, bits);
        ClearStack();
        const int32_t b = FaceOffset(level, face, alignMask, levelCount, &bytesB, width, height, bits);
        if (a != b || bytesA != bytesB)
            ctx.Fail("level %d/%d face %d mask %x %dx%d bits %d: original %d (%d bytes), ours %d (%d bytes)", level, levelCount, face, alignMask,
                     width, height, bits, a, bytesA, b, bytesB);
    }
}

// 0x885e69: the next level's offset in the flat layout.
DIFF_TEST(GimexFshRecords_NextLevelOffset, 0x885e69)
{
    using Fn = uint32_t(__cdecl *)(int32_t, int32_t, uint32_t, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kNextLevelOffset);
    std::mt19937 rng(0x885e69);
    for (int round = 0; round < 100000; ++round) {
        const int32_t level = static_cast<int32_t>(Pick(rng, {0, 1, 2, 3, 0xffffffff, rng() % 8}));
        const int32_t offset = static_cast<int32_t>(Pick(rng, {0, 1, 7, 100, 0x7fffffff, 0xffffffff, rng() % 5000}));
        const uint32_t alignMask = Pick(rng, {0, 1, 3, 7, 0xf, 0x1f, 0xff, 0xffffffff});
        const int32_t levelCount = static_cast<int32_t>(Pick(rng, {0, 1, 2, 3, 4, 0xffffffff, rng() % 8}));
        const int32_t bytes = static_cast<int32_t>(Pick(rng, {0, 1, 5, 64, 0x7fffffff, 0xffffffff, rng() % 5000}));
        ++ctx.cases;
        ClearStack();
        const uint32_t a = original(level, offset, alignMask, levelCount, bytes);
        ClearStack();
        const uint32_t b = NextLevelOffset(level, offset, alignMask, levelCount, bytes);
        if (a != b) ctx.Fail("level %d/%d offset %d mask %x bytes %d: original %u, ours %u", level, levelCount, offset, alignMask, bytes, a, b);
    }
}

// 0x885c25: the six-face buffer.
DIFF_TEST(GimexFshRecords_AllocateFaces, 0x885c25)
{
    using Fn = void *(__cdecl *)(int32_t, int32_t, int32_t, int32_t *, int32_t, uint32_t);
    const Fn original = ctx.Original<Fn>(kAllocateFaces);
    HeapFakes heap;
    std::mt19937 rng(0x885c25);
    for (int round = 0; round < 20000; ++round) {
        const int32_t width = static_cast<int32_t>(Pick(rng, {0, 1, 2, 4, 8, 16, 33, 64, 256, rng() % 200}));
        const int32_t height = static_cast<int32_t>(Pick(rng, {0, 6, 12, 18, 24, 60, 96, 600, rng() % 700}));
        const int32_t bits = static_cast<int32_t>(Pick(rng, {0, 4, 8, 12, 15, 16, 24, 32, rng() % 40}));
        const int32_t levels = static_cast<int32_t>(Pick(rng, {1, 2, 3, 4, 5, 8, 0, rng() % 10}));
        const uint32_t alignMask = Pick(rng, {0, 1, 3, 7, 0xf, 0x1f, 0xff});
        const int failAt = rng() % 6 == 0 ? 0 : -1;
        int32_t bytes[2] = {0x5a5a5a5a, 0x5a5a5a5a};
        void *result[2];
        Buf contents[2];
        std::vector<uint32_t> logs[2];
        for (int side = 0; side < 2; ++side) {
            ResetHeap(failAt);
            ClearStack();
            result[side] = side == 0 ? original(width, height, bits, &bytes[0], levels, alignMask)
                                     : AllocateFaces(width, height, bits, &bytes[1], levels, alignMask);
            logs[side] = gLog;
            if (result[side] != nullptr && gSizes.size() == 1) {
                const uint8_t *p = static_cast<const uint8_t *>(result[side]);
                contents[side].assign(p, p + gSizes[0] + kSlack);
            }
        }
        ++ctx.cases;
        if ((result[0] == nullptr) != (result[1] == nullptr) || bytes[0] != bytes[1])
            ctx.Fail("round %d: original %s (%d bytes), ours %s (%d bytes)", round, result[0] ? "a block" : "null", bytes[0],
                     result[1] ? "a block" : "null", bytes[1]);
        if (contents[0] != contents[1]) ctx.Fail("round %d: the buffers differ", round);
        CompareLogs(ctx, "AllocateFaces", round, logs[0], logs[1]);
    }
}

// 0x885cba: *bits for block-coded records.
DIFF_TEST(GimexFshRecords_BlockBits, 0x885cba)
{
    using Fn = void(__cdecl *)(const uint8_t *, const uint8_t *, int32_t, int32_t *, int32_t);
    const Fn original = ctx.Original<Fn>(kBlockBits);
    std::mt19937 rng(0x885cba);
    for (int round = 0; round < 60000; ++round) {
        const int32_t platform = kPlatforms[rng() % (sizeof(kPlatforms) / 4)];
        Buf record = MakeRecord(rng, rng() & 0xff, rng() % 2 == 0);
        Buf typeSource(8);
        RandomFill(rng, typeSource.data(), 8);
        if (rng() % 2) typeSource[0] = static_cast<uint8_t>(Pick(rng, {0x60, 0x61, 0x62, 0x1e}));
        const int32_t forced = static_cast<int32_t>(Pick(rng, {0, 0, 1, 2, 0xffffffff}));
        int32_t a = 0x77, b = 0x77;
        ++ctx.cases;
        ClearStack();
        original(record.data(), typeSource.data(), forced, &a, platform);
        ClearStack();
        BlockBits(record.data(), typeSource.data(), forced, &b, platform);
        if (a != b) ctx.Fail("forced %d platform %d: original %d, ours %d", forced, platform, a, b);
    }
}

// 0x885def: the tiled test.
DIFF_TEST(GimexFshRecords_IsTiled, 0x885def)
{
    using Fn = uint32_t(__cdecl *)(const uint8_t *, int32_t, uint32_t, uint32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kIsTiled);
    std::mt19937 rng(0x885def);
    for (int round = 0; round < 100000; ++round) {
        const int32_t platform = kPlatforms[rng() % (sizeof(kPlatforms) / 4)];
        Buf record = MakeRecord(rng, rng() & 0xff, rng() % 2 == 0);
        const bool big = platform == 5 || platform == 7 || platform == 0xf || platform == 0x11;
        if (rng() % 2) Put(&record[1], rng() % 2 ? 0x4000 : rng() & 0xffffff, 3, big);
        const uint32_t width = Pick(rng, {0, 1, 2, 3, 4, 5, 8, 16, 24, 32, 64, 0x80000000, 0xffffffff, rng() % 300});
        const uint32_t height = Pick(rng, {0, 1, 2, 3, 4, 5, 8, 16, 24, 32, 64, 0x80000000, 0xffffffff, rng() % 300});
        const int32_t bits = static_cast<int32_t>(Pick(rng, {0, 4, 8, 0xc, 0xf, 0x10, 0x20, 1, 24, 0xffffffff, rng() % 40}));
        ++ctx.cases;
        ClearStack();
        const uint32_t a = original(record.data(), platform, width, height, bits);
        ClearStack();
        const uint32_t b = IsTiled(record.data(), platform, width, height, bits);
        if (a != b) ctx.Fail("platform %d %ux%u bits %d: original %x, ours %x", platform, width, height, bits, a, b);
    }
}

namespace {

// A directory of `count` entries and the blocks it owns, all from the recording allocator (so frees show up by ordinal). The
// same seed builds the same directory on both sides.
struct Built {
    Directory *directory = nullptr;
};

uint8_t *AllocFilled(std::mt19937 &rng, uint32_t size)
{
    uint8_t *block = static_cast<uint8_t *>(FakeAllocate(size));
    RandomFill(rng, block, size);
    return block;
}

} // namespace

// 0x88800e: the face splitter over directories of records with and without the face bit, mipmaps and block types.
DIFF_TEST(GimexFshRecords_SplitFaces, 0x88800e)
{
    using Fn = int32_t(__cdecl *)(Directory *, int32_t, uint32_t);
    const Fn original = ctx.Original<Fn>(kSplitFaces);
    HeapFakes heap;
    int rebuilt = 0;
    for (int round = 0; round < 6000; ++round) {
        const int32_t platform = kPlatforms[round % (sizeof(kPlatforms) / 4)];
        const bool big = platform == 5 || platform == 7 || platform == 0xf || platform == 0x11;
        static const uint32_t kMasks[] = {0, 1, 3, 7, 0xf, 0x1f, 0xff};
        const uint32_t alignMask = kMasks[(round / 23) % 7];
        const int failAt = round % 7 == 3 ? static_cast<int>(round % 5) + 1 : -1;
        int32_t result[2];
        std::vector<uint32_t> snap[2], logs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 rng(round * 7919 + 13);
            ResetHeap(-1);
            const int count = 1 + static_cast<int>(rng() % 3);
            Directory *directory = reinterpret_cast<Directory *>(FakeAllocate(4 + sizeof(Entry) * count));
            std::memset(directory, 0, 4 + sizeof(Entry) * count);
            directory->count = count;
            for (int i = 0; i < count; ++i) {
                const int type = Pick(rng, {0x7b, 0x7d, 0x60, 0x61, 0x62, 0x1e, 0x2d, 0x78, 0x40, 0x41, rng() & 0xff});
                Buf header = MakeRecord(rng, type, big, 0x40);
                uint8_t *record = AllocFilled(rng, 0x20000);
                std::memcpy(record, header.data(), 0x40);
                const uint32_t attr = (rng() % 4 ? 0x100u : 0u) | (rng() % 3 == 0 ? 0x2u : 0u) | (rng() % 8 == 0 ? 0x4000u : 0u);
                Put(record + 1, attr, 3, big);
                const uint32_t w = Pick(rng, {1, 2, 4, 8, 16, 32, 5, 0, 64, 3});
                const uint32_t h = Pick(rng, {6, 12, 24, 48, 96, 192, 0, 7, 60});
                Put(record + 0x18, w, 4, big);
                Put(record + 0x1c, h, 4, big);
                Put(record + 0x14, (Pick(rng, {0, 1, 2, 3, 4, 5, 0xf, 7}) << 12) | (rng() & 0xfff), 4, big);
                directory->entries[i].shape = record;
                directory->entries[i].shapeBytes = rng();
            }
            gFailAt = failAt;
            gCalls = 0;
            gLog.clear();
            ClearStack();
            result[side] = side == 0 ? original(directory, platform, alignMask) : SplitFaces(directory, platform, alignMask);
            snap[side] = Snapshot(directory, true);
            logs[side] = gLog;
            if (side == 0)
                for (int i = 0; i < directory->count; ++i)
                    if (Ordinal(directory->entries[i].shape) > static_cast<uint32_t>(directory->count) + 1) ++rebuilt;
        }
        ++ctx.cases;
        if (result[0] != result[1]) ctx.Fail("round %d platform %d: original %d, ours %d", round, platform, result[0], result[1]);
        if (snap[0] != snap[1]) {
            size_t first = 0;
            while (first < snap[0].size() && first < snap[1].size() && snap[0][first] == snap[1][first]) ++first;
            ctx.Fail("round %d platform %d: the directories or blocks differ afterwards (word %zu of %zu/%zu: %08x vs %08x)", round,
                     platform, first, snap[0].size(), snap[1].size(), first < snap[0].size() ? snap[0][first] : 0,
                     first < snap[1].size() ? snap[1][first] : 0);
        }
        CompareLogs(ctx, "SplitFaces", round, logs[0], logs[1]);
    }
    if (rebuilt < 500) ctx.Fail("only %d records were rebuilt: the generator no longer reaches the splitter", rebuilt);
}

// 0x885f08: the directory free.
DIFF_TEST(GimexFshRecords_FreeDirectory, 0x885f08)
{
    using Fn = void(__cdecl *)(Directory *);
    const Fn original = ctx.Original<Fn>(kFreeDirectory);
    HeapFakes heap;
    for (int round = 0; round < 2000; ++round) {
        std::vector<uint32_t> snap[2], logs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 rng(round * 31 + 5);
            ResetHeap(-1);
            const int count = static_cast<int>(rng() % 4);
            Directory *directory = reinterpret_cast<Directory *>(FakeAllocate(4 + sizeof(Entry) * (count + 1)));
            std::memset(directory, 0, 4 + sizeof(Entry) * (count + 1));
            directory->count = count;
            for (int i = 0; i < count; ++i) {
                Entry &e = directory->entries[i];
                uint8_t **slots[] = {&e.shape, &e.palette, &e.pairs, &e.marker, &e.userData, &e.text};
                for (uint8_t **slot : slots)
                    if (rng() % 3) *slot = AllocFilled(rng, 8);
                if (rng() % 3) e.name = reinterpret_cast<char *>(AllocFilled(rng, 8));
                e.flags = rng();
            }
            gLog.clear();
            ClearStack();
            if (side == 0) original(directory); else FreeDirectory(directory);
            snap[side] = Snapshot(nullptr, false);
            logs[side] = gLog;
        }
        ++ctx.cases;
        if (snap[0] != snap[1]) ctx.Fail("round %d: the blocks left differ", round);
        CompareLogs(ctx, "FreeDirectory", round, logs[0], logs[1]);
    }
}
