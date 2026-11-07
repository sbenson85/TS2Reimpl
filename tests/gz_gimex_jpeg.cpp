// Differential tests for src/gz/GimexJpeg.size.cpp: Gimex's JPEG codec through the libjpeg in the image (row 2 of the codec
// table): about, is, open, info, read, write and close, and the helpers (row converters, the CMYK conversion, the gray
// palette test, the colour-space fix, the Photoshop-marker handler and the two error exits).
//
// The library itself (jpeg_std_error, jpeg_read_scanlines, ...) is the image's code on both sides: it needs no imports, so
// the tests run it for real. Valid inputs are made by the original writer from seeded synthetic images, then corrupted
// (truncated, bytes flipped in the compressed data, a tag broken, an Adobe marker inserted to make CMYK and YCCK streams,
// garbage). The codecs' allocator (0x87da65, 0x87da72) is replaced on both sides by a recording fake that can fail a
// scripted call; the library's default output_message (0x88d7c9) is replaced by a fake that records the message code.
// A fake cIGZFile (tests/gz_gimex_stream.h) is the stream, one per side with the same bytes. Compared: results, the
// allocator's and the library's message log, the info block (all 0x2a70 bytes), the decoded pixels (the whole destination,
// sentinel-filled), the bytes written, the stream's position and its stray calls.
//
// Inputs kept out because the original reads garbage on them (ours does something defined there):
// - a stream that carries the "GIMEXARGB" marker but is not four components (the row converter reads four bytes a pixel
//   from a shorter row), and a two-component stream (the converter reads three);
// - a segment shorter than the nine-byte tag in the marker handler (the original compares an uninitialised buffer).
#define NOMINMAX
#include "code_patch.h"
#include "difftest.h"
#include "game/game.h"
#include "game/original.h"
#include "gz/GimexFormats.h"
#include "gz_gimex_stream.h"
#include "shape.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace gimexstream;

constexpr uint32_t kAbout = 0x87ec26;
constexpr uint32_t kClose = 0x87ed09;
constexpr uint32_t kInfo = 0x87ed18;
constexpr uint32_t kIs = 0x87ef8c;
constexpr uint32_t kOpen = 0x87efdb;
constexpr uint32_t kRead = 0x87f013;
constexpr uint32_t kWrite = 0x87f413;
constexpr uint32_t kMarker = 0x87f177;
constexpr uint32_t kReadRow = 0x87f255;
constexpr uint32_t kFixColor = 0x87f333;
constexpr uint32_t kCmyk = 0x87f353;
constexpr uint32_t kDecompressExit = 0x87f3b5;
constexpr uint32_t kCompressExit = 0x87f3c9;
constexpr uint32_t kWriteRow = 0x87f62d;
constexpr uint32_t kGrayPalette = 0x87f761;
constexpr uint32_t kAllocate = 0x87da65;
constexpr uint32_t kFreeMemory = 0x87da72;
constexpr uint32_t kOutputMessage = 0x88d7c9; // the library's default output_message
constexpr uint32_t kAlphaCell = 0x10e6944;

constexpr uint32_t kInfoBytes = 0x2a70;

// ---- the recording fakes ----

// What the fakes note, in order: the allocator's requests and releases, the library's messages. Both sides' logs are compared.
std::vector<uint32_t> gLog;

struct Heap {
    std::vector<void *> blocks;
    int calls = 0;
    int failAt = 0;
};
Heap gHeap;

void *__stdcall FakeAllocate(uint32_t size)
{
    const int index = ++gHeap.calls;
    gLog.push_back(0x1000000u);
    gLog.push_back(size);
    if (index == gHeap.failAt || size > 0x4000000) return nullptr;
    const size_t bytes = size != 0 ? size : 1;
    void *const block = std::malloc(bytes);
    std::memset(block, 0xa5, bytes);
    gHeap.blocks.push_back(block);
    return block;
}

int32_t __stdcall FakeRelease(void *block)
{
    uint32_t ordinal = 0;
    for (size_t i = 0; i < gHeap.blocks.size(); ++i) {
        if (gHeap.blocks[i] == block && block != nullptr) ordinal = static_cast<uint32_t>(i) + 1;
    }
    gLog.push_back(0x2000000u);
    gLog.push_back(ordinal);
    if (ordinal != 0) {
        std::free(block);
        gHeap.blocks[ordinal - 1] = nullptr;
    }
    return 1;
}

// The library's output_message (a cdecl function of the compress / decompress object).
void __cdecl FakeOutputMessage(void *cinfo)
{
    const uint8_t *const err = *reinterpret_cast<uint8_t *const *>(cinfo);
    uint32_t code;
    std::memcpy(&code, err + 0x14, 4);
    gLog.push_back(0x3000000u);
    gLog.push_back(code);
}

struct Fakes {
    shape::CodePatch allocate{kAllocate, reinterpret_cast<const void *>(&FakeAllocate)};
    shape::CodePatch release{kFreeMemory, reinterpret_cast<const void *>(&FakeRelease)};
    // Ours call our allocator (lane MP), so it gets the same fake.
    shape::CodePatch ourAllocate{reinterpret_cast<void *>(&gimex::Allocate), reinterpret_cast<const void *>(&FakeAllocate)};
    shape::CodePatch ourRelease{reinterpret_cast<void *>(&gimex::FreeMemory), reinterpret_cast<const void *>(&FakeRelease)};
    shape::CodePatch message{kOutputMessage, reinterpret_cast<const void *>(&FakeOutputMessage)};
};

void ResetSide(int failAt)
{
    for (void *block : gHeap.blocks) std::free(block);
    gHeap = Heap{};
    gHeap.failAt = failAt;
    gLog.clear();
    *GameAddress<uint32_t>(kAlphaCell) = 0;
}

// ---- the images ----

struct Image {
    int width = 1;
    int height = 1;
    int kind = 0;
    int pitch = 4;
    std::vector<uint8_t> info = std::vector<uint8_t>(kInfoBytes);
    Bytes pixels;
    gimex::Info *header() { return reinterpret_cast<gimex::Info *>(info.data()); }
};

enum Kind { kRgb32, kGray8, kPalette8, kAlpha32, kAlphaPalette8, kOddBits, kKinds };

void FillPixels(std::mt19937 &r, Image &image, size_t bytes)
{
    image.pixels.clear();
    if (Roll(r, 2) == 0) {
        RandomBytes(r, image.pixels, bytes);
    } else {
        // smooth: compresses well, so the files have long runs and few bits
        const uint32_t a = Roll(r, 7), b = Roll(r, 5), base = r();
        for (size_t i = 0; i < bytes; ++i) {
            image.pixels.push_back(static_cast<uint8_t>(base + (i / 4) * a + (i / (4 * 8)) * b + Roll(r, 3)));
        }
    }
}

Image MakeImage(std::mt19937 &r, int forcedKind = -1)
{
    Image image;
    image.kind = forcedKind >= 0 ? forcedKind : static_cast<int>(Roll(r, kKinds));
    image.width = 1 + static_cast<int>(Roll(r, 40));
    image.height = 1 + static_cast<int>(Roll(r, 32));
    if (Roll(r, 12) == 0) {
        image.width = 40 + static_cast<int>(Roll(r, 60));
        image.height = 20 + static_cast<int>(Roll(r, 40));
    }
    gimex::Info *const info = image.header();
    info->width = image.width;
    info->height = image.height;
    info->jpegFlag = Roll(r, 3) == 0 ? 0 : 1 + Roll(r, 3);
    static const uint32_t kQualities[] = {0, 1, 10, 50, 75, 90, 100, 101, 255, 0xffffffffu};
    info->quality = Roll(r, 3) == 0 ? kQualities[Roll(r, 10)] : 1 + Roll(r, 100);
    static const float kDpi[] = {0.0f, 0.0f, 72.0f, 96.5f, 300.0f, 1.0f, -3.0f, 65536.0f, 1e10f, 0.4f};
    info->dotsPerInch = kDpi[Roll(r, 10)];
    if (Roll(r, 40) == 0) info->dotsPerInch = std::nanf("");
    const bool alpha = image.kind == kAlpha32 || image.kind == kAlphaPalette8;
    info->variant = alpha ? 1 + 2 * Roll(r, 3) : 2 * Roll(r, 2);
    bool palette = image.kind != kRgb32 && image.kind != kAlpha32;
    info->bitsPerPixel = palette ? 8 : 32;
    if (image.kind == kOddBits) {
        static const int32_t kBits[] = {24, 16, 0, 33, -1, 1, 31};
        info->bitsPerPixel = kBits[Roll(r, 7)];
        info->variant = 2 * Roll(r, 2);
    }
    info->paletteCount = 256;
    if (palette) {
        const bool gray = image.kind == kGray8 || ((image.kind == kAlphaPalette8 || image.kind == kOddBits) && Roll(r, 2));
        for (int i = 0; i < 256; ++i) {
            const uint8_t v = static_cast<uint8_t>(r());
            info->palette[i][0] = info->palette[i][1] = info->palette[i][2] = v;
            info->palette[i][3] = static_cast<uint8_t>(r());
            if (!gray) {
                info->palette[i][0] = static_cast<uint8_t>(r());
                info->palette[i][1] = static_cast<uint8_t>(r());
            }
        }
        if (image.kind == kGray8) {
            // The count the gray test looks at: all of it, none (the test passes), or one entry past the last gray one.
            const uint32_t how = Roll(r, 6);
            info->paletteCount = how == 0 ? 0 : how == 1 ? 1 + Roll(r, 256) : 256;
            if (how == 2) {
                const uint32_t at = Roll(r, 256);
                info->paletteCount = at + 1;
                info->palette[at][1] ^= 1; // not gray, and it is the last one counted
            }
            if (how == 3) {
                info->paletteCount = 1 + Roll(r, 255);
                info->palette[info->paletteCount][0] ^= 0x20; // not gray, but one past the count
            }
            if (how == 4) info->paletteCount = static_cast<uint32_t>(-1 - static_cast<int>(Roll(r, 5))); // negative
        }
    }
    const int bytesPerPixel = info->bitsPerPixel == 32 ? 4 : 1;
    const int rowBytes = bytesPerPixel * image.width;
    image.pitch = rowBytes + static_cast<int>(Roll(r, 6) == 0 ? Roll(r, 17) : 0);
    FillPixels(r, image, static_cast<size_t>(image.pitch) * static_cast<size_t>(image.height) + 64);
    return image;
}

// ---- running one side ----

struct Record {
    std::vector<uint32_t> log;
    Bytes pixels; // the decoded pixels (read), else empty
    Bytes written; // the stream's bytes (write)
};

void LogBytes(const void *data, size_t size)
{
    uint32_t hash = 2166136261u;
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    gLog.push_back(0x4000000u);
    gLog.push_back(hash);
    gLog.push_back(static_cast<uint32_t>(size));
}

int32_t CallWrite(difftest::Context &ctx, bool ours, gimex::Instance *instance, const gimex::Info *info, const void *pixels,
                  int32_t pitch)
{
    return ours ? gimex::jpeg::WriteFrame(instance, info, pixels, pitch)
                : ctx.Original<gimex::WriteFn>(kWrite)(instance, info, pixels, pitch);
}

Record RunWrite(difftest::Context &ctx, bool ours, Image image, int failAt, int64_t writeLimit)
{
    ResetSide(failAt);
    Stream stream(Bytes{});
    stream.file.writeLimit = writeLimit;
    gimex::Instance instance{};
    instance.stream = stream.Get();
    const int32_t result = CallWrite(ctx, ours, &instance, image.header(), image.pixels.data(), image.pitch);
    Record record;
    gLog.push_back(0x5000000u);
    gLog.push_back(static_cast<uint32_t>(result));
    gLog.push_back(static_cast<uint32_t>(stream.file.stray));
    gLog.push_back(static_cast<uint32_t>(stream.file.written.size()));
    LogBytes(image.info.data(), image.info.size()); // the info is untouched
    LogBytes(image.pixels.data(), image.pixels.size());
    record.log = gLog;
    record.written = stream.file.written;
    for (void *block : gHeap.blocks) {
        if (block != nullptr) record.log.push_back(0x6000000u); // a leaked block
    }
    return record;
}

// One decode: open, info, read (with the info the codec answered, or a synthetic one when it answered none).
struct ReadPlan {
    bool callInfo = true;
    bool callRead = true;
    int failAt = 0;
    int heightCut = 0;  // read with the info's height this many rows short (0: as is)
    int pitchSlack = 0; // bytes added to the row size
    bool pitchTight = false;
    bool infoAgain = false; // info twice (the second starts with the stream at its end)
};

Record RunRead(difftest::Context &ctx, bool ours, const Bytes &data, const Image &spec, const ReadPlan &plan)
{
    ResetSide(plan.failAt);
    Stream stream(data);
    gimex::Instance *instance = nullptr;
    const int32_t opened = ours ? gimex::jpeg::Open(&instance, stream.Get(), "x.jpg", 0)
                                : ctx.Original<gimex::OpenFn>(kOpen)(&instance, stream.Get(), "x.jpg", 0);
    gLog.push_back(0x5000000u);
    gLog.push_back(static_cast<uint32_t>(opened));
    Record record;
    std::vector<uint8_t> infoCopy(kInfoBytes);
    gimex::Info *info = reinterpret_cast<gimex::Info *>(infoCopy.data());
    bool haveInfo = false;
    for (int again = 0; again < (plan.infoAgain ? 2 : 1) && opened != 0 && plan.callInfo; ++again) {
        gimex::Info *const answered = ours ? gimex::jpeg::GetInfo(instance, 0) : ctx.Original<gimex::InfoFn>(kInfo)(instance, 0);
        gLog.push_back(0x7000000u);
        gLog.push_back(answered != nullptr);
        gLog.push_back(stream.file.pos);
        if (answered) {
            LogBytes(answered, kInfoBytes);
            std::memcpy(infoCopy.data(), answered, kInfoBytes);
            haveInfo = true;
            ctx.Original<int32_t(__stdcall *)(void *)>(kFreeMemory)(answered);
        }
    }
    if (opened != 0 && plan.callRead) {
        if (!haveInfo) {
            std::memset(infoCopy.data(), 0, kInfoBytes);
            info->width = spec.width;
            info->height = spec.height;
        }
        const int width = info->width > 0 ? info->width : 1;
        const int height = info->height > 0 ? info->height : 0;
        int32_t pitch = 4 * width + plan.pitchSlack;
        if (plan.pitchTight) pitch = width;
        info->height = std::max(0, height - plan.heightCut);
        const size_t bytes = static_cast<size_t>(std::max(pitch, 0)) * static_cast<size_t>(height + 2) + 4u * static_cast<size_t>(width) + 64;
        record.pixels.assign(bytes, 0xc3);
        const int32_t result = ours ? gimex::jpeg::ReadFrame(instance, info, record.pixels.data(), pitch)
                                    : ctx.Original<gimex::ReadFn>(kRead)(instance, info, record.pixels.data(), pitch);
        gLog.push_back(0x8000000u);
        gLog.push_back(static_cast<uint32_t>(result));
        gLog.push_back(stream.file.pos);
        gLog.push_back(*GameAddress<uint32_t>(kAlphaCell));
    }
    gLog.push_back(0x9000000u);
    gLog.push_back(static_cast<uint32_t>(stream.file.stray));
    gLog.push_back(stream.file.pos);
    gLog.push_back(static_cast<uint32_t>(stream.file.written.size()));
    if (instance) {
        const int32_t closed = ours ? gimex::jpeg::Close(instance) : ctx.Original<gimex::CloseFn>(kClose)(instance);
        gLog.push_back(static_cast<uint32_t>(closed));
    }
    record.log = gLog;
    for (void *block : gHeap.blocks) {
        if (block != nullptr) record.log.push_back(0x6000000u);
    }
    return record;
}

bool Compare(difftest::Context &ctx, const Record &a, const Record &b, const char *what, uint32_t seed)
{
    ++ctx.cases;
    if (a.log != b.log) {
        size_t i = 0;
        while (i < a.log.size() && i < b.log.size() && a.log[i] == b.log[i]) ++i;
        ctx.Fail("%s (seed %08x): the logs differ at %zu of %zu / %zu (original %08x, ours %08x)", what, seed, i, a.log.size(),
                 b.log.size(), i < a.log.size() ? a.log[i] : 0u, i < b.log.size() ? b.log[i] : 0u);
        std::string dump;
        for (const auto *log : {&a.log, &b.log}) {
            dump += "   ";
            for (size_t k = 0; k < log->size() && k < 24; ++k) {
                char word[16];
                std::snprintf(word, sizeof(word), " %x", (*log)[k]);
                dump += word;
            }
        }
        ctx.Fail("%s", dump.c_str());
        return false;
    }
    if (a.written != b.written) {
        size_t i = 0;
        while (i < a.written.size() && i < b.written.size() && a.written[i] == b.written[i]) ++i;
        ctx.Fail("%s (seed %08x): the bytes written differ at %zu of %zu / %zu", what, seed, i, a.written.size(), b.written.size());
        return false;
    }
    if (a.pixels != b.pixels) {
        size_t i = 0;
        while (i < a.pixels.size() && i < b.pixels.size() && a.pixels[i] == b.pixels[i]) ++i;
        ctx.Fail("%s (seed %08x): the pixels differ at %zu of %zu / %zu (original %02x, ours %02x)", what, seed, i, a.pixels.size(),
                 b.pixels.size(), i < a.pixels.size() ? a.pixels[i] : 0u, i < b.pixels.size() ? b.pixels[i] : 0u);
        return false;
    }
    return true;
}

// ---- making and breaking files ----

// A file from the original writer.
Bytes MakeFile(difftest::Context &ctx, Image image)
{
    return RunWrite(ctx, false, image, 0, -1).written;
}

// The offset of the compressed data (just after the first start-of-scan header), or 0.
size_t ScanStart(const Bytes &file)
{
    for (size_t i = 2; i + 4 < file.size(); ++i) {
        if (file[i] == 0xff && file[i + 1] == 0xda) return i + 2 + ((file[i + 2] << 8) | file[i + 3]);
    }
    return 0;
}

Bytes Corrupt(std::mt19937 &r, Bytes file, int kind, bool alphaFile)
{
    switch (kind) {
    case 0: return file;
    case 1: file.resize(Roll(r, static_cast<uint32_t>(file.size() + 1))); return file; // truncated anywhere
    case 2: {                                                                          // bytes flipped in the compressed data
        const size_t start = ScanStart(file);
        if (start == 0 || start + 1 >= file.size()) return file;
        for (uint32_t n = 1 + Roll(r, 4); n > 0; --n) file[start + Roll(r, static_cast<uint32_t>(file.size() - start - 1))] ^= static_cast<uint8_t>(1 + Roll(r, 255));
        return file;
    }
    case 3: { // truncated inside the compressed data
        const size_t start = ScanStart(file);
        if (start == 0 || start >= file.size()) return file;
        file.resize(start + Roll(r, static_cast<uint32_t>(file.size() - start)));
        return file;
    }
    case 4: // the marker's tag broken (a four-component stream with no alpha flag reads as CMYK)
        if (alphaFile) {
            for (size_t i = 0; i + 9 < file.size(); ++i) {
                if (std::memcmp(&file[i], "GIMEXARGB", 9) == 0) {
                    file[i + Roll(r, 9)] ^= 0x20;
                    break;
                }
            }
        }
        return file;
    case 5: { // an Adobe marker saying YCCK (transform 2), or CMYK (0), after the start of image; the tag broken too
        static const uint8_t kAdobe[] = {0xff, 0xee, 0x00, 0x0e, 'A', 'd', 'o', 'b', 'e', 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x02};
        if (!alphaFile || file.size() < 4) return file;
        Bytes adobe(kAdobe, kAdobe + sizeof(kAdobe));
        adobe.back() = Roll(r, 2) ? 2 : 0;
        file.insert(file.begin() + 2, adobe.begin(), adobe.end());
        for (size_t i = 0; i + 9 < file.size(); ++i) {
            if (std::memcmp(&file[i], "GIMEXARGB", 9) == 0) {
                file[i] = 'H';
                break;
            }
        }
        return file;
    }
    case 6: { // garbage after the start of image
        Bytes garbage = {0xff, 0xd8};
        for (uint32_t n = Roll(r, 60); n > 0; --n) garbage.push_back(static_cast<uint8_t>(r()));
        return garbage;
    }
    case 7: // trailing garbage
        for (uint32_t n = Roll(r, 20); n > 0; --n) file.push_back(static_cast<uint8_t>(r()));
        return file;
    case 8: // a tables-only stream: everything before the frame header, then the end of image (the library says no image)
        for (size_t i = 2; i + 1 < file.size(); ++i) {
            if (file[i] == 0xff && (file[i + 1] == 0xc0 || file[i + 1] == 0xc1 || file[i + 1] == 0xc2)) {
                file.resize(i);
                file.push_back(0xff);
                file.push_back(0xd9);
                break;
            }
        }
        return file;
    default: return file;
    }
}

// ---- the cases ----

void WriteCases(difftest::Context &ctx, uint32_t seed, int cases)
{
    Fakes fakes;
    std::mt19937 rng(seed);
    int wrote = 0;
    for (int c = 0; c < cases; ++c) {
        const uint32_t caseSeed = rng();
        std::mt19937 r(caseSeed);
        Image image = MakeImage(r);
        // Sometimes an empty or oversized image (the library refuses it), an allocation or a stream write failing. (A width
        // of zero or less is kept out: the library rounds it up to zero and divides by it.)
        if (Roll(r, 25) == 0) image.header()->height = static_cast<int32_t>(Roll(r, 3)) - 1;
        if (Roll(r, 60) == 0) image.header()->width = 70000;
        const int failAt = Roll(r, 8) == 0 ? 1 : 0;
        const int64_t limit = Roll(r, 8) == 0 ? static_cast<int64_t>(Roll(r, 600)) : -1;
        ctx.Mark("write");
        const Record original = RunWrite(ctx, false, image, failAt, limit);
        const Record mine = RunWrite(ctx, true, image, failAt, limit);
        char what[64];
        std::snprintf(what, sizeof(what), "write case %d (kind %d)", c, image.kind);
        if (Compare(ctx, original, mine, what, caseSeed)) {
            if (!original.written.empty()) ++wrote;
        } else {
            const gimex::Info *h = image.header();
            ctx.Fail("   %dx%d bits %d count %u variant %u flag %u quality %u dpi %g pitch %d fail %d limit %lld", h->width, h->height,
                     h->bitsPerPixel, h->paletteCount, h->variant, h->jpegFlag, h->quality, h->dotsPerInch, image.pitch, failAt,
                     static_cast<long long>(limit));
        }
        if (ctx.failures.size() >= 10) return;
    }
    if (wrote < cases / 4) ctx.Fail("only %d of %d writes produced a file", wrote, cases);
}

void ReadCases(difftest::Context &ctx, uint32_t seed, int cases, bool infoOnly)
{
    Fakes fakes;
    std::mt19937 rng(seed);
    int decoded = 0;
    for (int c = 0; c < cases; ++c) {
        const uint32_t caseSeed = rng();
        std::mt19937 r(caseSeed);
        const int forced = Roll(r, 3) == 0 ? static_cast<int>(Roll(r, 2) ? kAlpha32 : kAlphaPalette8) : -1;
        Image image = MakeImage(r, forced);
        const bool alphaFile = (image.header()->variant & 1) != 0;
        const Bytes clean = MakeFile(ctx, image);
        if (clean.empty()) continue;
        const int kind = Roll(r, 4) == 0 ? static_cast<int>(Roll(r, 9)) : 0;
        const Bytes file = Corrupt(r, clean, kind, alphaFile);
        ReadPlan plan;
        plan.failAt = Roll(r, 8) == 0 ? 1 + static_cast<int>(Roll(r, 2)) : 0;
        plan.callRead = !infoOnly;
        plan.callInfo = infoOnly || Roll(r, 4) != 0;
        plan.infoAgain = Roll(r, 4) == 0;
        plan.heightCut =Roll(r, 6) == 0 ? 1 + static_cast<int>(Roll(r, 3)) : 0;
        plan.pitchSlack = Roll(r, 4) == 0 ? static_cast<int>(Roll(r, 9)) : 0;
        ctx.Mark("read");
        const Record original = RunRead(ctx, false, file, image, plan);
        const Record mine = RunRead(ctx, true, file, image, plan);
        char what[64];
        std::snprintf(what, sizeof(what), "%s case %d (kind %d, corruption %d)", infoOnly ? "info" : "read", c, image.kind, kind);
        if (Compare(ctx, original, mine, what, caseSeed) && kind == 0 && plan.failAt == 0) ++decoded;
        if (ctx.failures.size() >= 10) return;
    }
    if (decoded < cases / 4) ctx.Fail("only %d of %d clean decodes ran", decoded, cases);
}

// ---- about, is, open, close ----

uint32_t HashOf(const void *data, size_t size)
{
    uint32_t hash = 2166136261u;
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

void SimpleCases(difftest::Context &ctx, uint32_t seed, int cases)
{
    Fakes fakes;
    std::mt19937 rng(seed);
    for (int c = 0; c < cases; ++c) {
        const uint32_t caseSeed = rng();
        auto side = [&](bool ours) {
            std::mt19937 r(caseSeed);
            const int failAt = Roll(r, 4) == 0 ? 1 : 0;
            ResetSide(failAt);
            const uint32_t what = Roll(r, 4);
            Record record;
            if (what == 0) { // about
                gimex::About *about = ours ? gimex::jpeg::NewAbout() : ctx.Original<gimex::AboutFn>(kAbout)();
                gLog.push_back(0x5000000u);
                gLog.push_back(about != nullptr);
                if (about) LogBytes(about, sizeof(gimex::About));
            } else if (what == 1) { // is: a stream of a few bytes, the marker right or one bit off
                Bytes data;
                static const uint32_t kMarkers[] = {0xffd8ffe0, 0xffd8ffe1, 0xffd8ffed, 0xffd8ffe2, 0xffd8ffee, 0xffd8ffe0 ^ 0x100,
                                                    0xe0ffd8ff, 0xffd9ffe0, 0, 0xffffffff};
                const uint32_t marker = Roll(r, 3) == 0 ? r() : kMarkers[Roll(r, 10)];
                for (int i = 3; i >= 0; --i) data.push_back(static_cast<uint8_t>(marker >> (8 * i)));
                data.resize(Roll(r, 6) == 0 ? Roll(r, 4) : 4 + Roll(r, 6));
                if (data.size() > 4 || Roll(r, 2)) RandomBytes(r, data, Roll(r, 5));
                Stream stream(data);
                if (Roll(r, 3) == 0) stream.file.pos = Roll(r, static_cast<uint32_t>(data.size() + 1)); // not at its start
                const int32_t result = ours ? gimex::jpeg::Probe(stream.Get()) : ctx.Original<gimex::IsFn>(kIs)(stream.Get());
                gLog.push_back(0x5000000u);
                gLog.push_back(static_cast<uint32_t>(result));
                gLog.push_back(stream.file.pos);
                gLog.push_back(static_cast<uint32_t>(stream.file.stray));
            } else if (what == 2) { // open, then close
                Bytes data;
                RandomBytes(r, data, Roll(r, 10));
                Stream stream(data);
                gimex::Instance *instance = nullptr;
                gimex::Instance *marker = reinterpret_cast<gimex::Instance *>(0x12345678);
                instance = marker;
                const int32_t opened = ours ? gimex::jpeg::Open(&instance, stream.Get(), "x.jpg", static_cast<int32_t>(r()))
                                            : ctx.Original<gimex::OpenFn>(kOpen)(&instance, stream.Get(), "x.jpg", static_cast<int32_t>(r()));
                gLog.push_back(0x5000000u);
                gLog.push_back(static_cast<uint32_t>(opened));
                gLog.push_back(instance == marker);
                gLog.push_back(stream.file.pos);
                gLog.push_back(static_cast<uint32_t>(stream.file.stray));
                if (opened != 0 && instance != marker) {
                    LogBytes(instance, 0x10);
                    gLog.push_back(instance->unknown14);
                    gLog.push_back(instance->stream == stream.Get());
                    const int32_t closed = ours ? gimex::jpeg::Close(instance) : ctx.Original<gimex::CloseFn>(kClose)(instance);
                    gLog.push_back(static_cast<uint32_t>(closed));
                }
            } else { // close of nothing
                const int32_t closed = ours ? gimex::jpeg::Close(nullptr) : ctx.Original<gimex::CloseFn>(kClose)(nullptr);
                gLog.push_back(0x5000000u);
                gLog.push_back(static_cast<uint32_t>(closed));
            }
            record.log = gLog;
            for (void *block : gHeap.blocks) {
                if (block != nullptr) record.log.push_back(0x6000000u);
            }
            return record;
        };
        const Record original = side(false);
        const Record mine = side(true);
        char label[48];
        std::snprintf(label, sizeof(label), "simple case %d", c);
        Compare(ctx, original, mine, label, caseSeed);
        if (ctx.failures.size() >= 10) return;
    }
}

// ---- the helpers, called directly ----

using ReadRowFn = void(__cdecl *)(const uint8_t *, uint8_t *, int32_t, int32_t, uint32_t, int32_t);
using WriteRowFn = void(__cdecl *)(const uint8_t *, uint8_t *, int32_t, uint32_t, const gimex::Info *);
using CmykFn = void(__cdecl *)(const uint8_t *, uint8_t *);
using GrayFn = int32_t(__cdecl *)(const uint8_t *, int32_t);
using FixFn = void(__cdecl *)(void *);

void RowCases(difftest::Context &ctx, uint32_t seed, int cases)
{
    std::mt19937 rng(seed);
    for (int c = 0; c < cases; ++c) {
        const uint32_t caseSeed = rng();
        std::mt19937 r(caseSeed);
        ctx.Mark("rows");
        const int32_t count = Roll(r, 8) == 0 ? 0 : static_cast<int32_t>(Roll(r, 40));
        Bytes source;
        RandomBytes(r, source, static_cast<size_t>(count) * 4 + 16);
        // read row: every colour-space code, the alpha-first flag set or not
        static const uint32_t kSpaces[] = {0, 1, 2, 3, 4, 5, 6, 0xffffffffu};
        const uint32_t space = kSpaces[Roll(r, 8)];
        const int32_t flag = Roll(r, 3) == 0 ? static_cast<int32_t>(r() | (Roll(r, 2) ? 0 : 0x100)) : 0;
        const int32_t components = static_cast<int32_t>(Roll(r, 6));
        Bytes a(static_cast<size_t>(count) * 4 + 16, 0xc3), b = a;
        ctx.Original<ReadRowFn>(kReadRow)(source.data(), a.data(), count, components, space, flag);
        gimex::jpeg::ConvertReadRow(source.data(), b.data(), count, components, space, flag);
        ++ctx.cases;
        if (a != b) ctx.Fail("read row (count %d space %u flag %d): the rows differ (seed %08x)", count, space, flag, caseSeed);
        // write row: 32-bit or palette, every colour-space code
        std::vector<uint8_t> infoBytes(kInfoBytes);
        gimex::Info *info = reinterpret_cast<gimex::Info *>(infoBytes.data());
        static const int32_t kBits[] = {32, 32, 8, 8, 24, 0, 33};
        info->bitsPerPixel = kBits[Roll(r, 7)];
        for (int i = 0; i < 256; ++i) {
            for (int k = 0; k < 4; ++k) info->palette[i][k] = static_cast<uint8_t>(r());
        }
        const uint32_t writeSpace = Roll(r, 6);
        Bytes c1(static_cast<size_t>(count) * 4 + 16, 0xc3), d1 = c1;
        ctx.Original<WriteRowFn>(kWriteRow)(source.data(), c1.data(), count, writeSpace, info);
        gimex::jpeg::ConvertWriteRow(source.data(), d1.data(), count, writeSpace, info);
        ++ctx.cases;
        if (c1 != d1) ctx.Fail("write row (count %d space %u bits %d): the rows differ (seed %08x)", count, writeSpace, info->bitsPerPixel, caseSeed);
        // cmyk
        uint8_t cmyk[4];
        for (uint8_t &v : cmyk) v = Roll(r, 4) == 0 ? (Roll(r, 2) ? 0 : 0xff) : static_cast<uint8_t>(r());
        if (Roll(r, 3) == 0) cmyk[3] = static_cast<uint8_t>(0xff - cmyk[Roll(r, 3)]); // the sum at the clamp
        uint8_t x[8], y[8];
        std::memset(x, 0xc3, sizeof(x));
        std::memset(y, 0xc3, sizeof(y));
        ctx.Original<CmykFn>(kCmyk)(cmyk, x);
        gimex::jpeg::CmykToRgb(cmyk, y);
        ++ctx.cases;
        if (std::memcmp(x, y, 8) != 0) ctx.Fail("cmyk %02x %02x %02x %02x: original %02x%02x%02x ours %02x%02x%02x", cmyk[0], cmyk[1], cmyk[2], cmyk[3], x[0], x[1], x[2], y[0], y[1], y[2]);
        // gray palette
        static const int32_t kCounts[] = {-1, 0, 1, 2, 3, 255, 256, 257};
        const int32_t palCount = Roll(r, 2) ? kCounts[Roll(r, 8)] : static_cast<int32_t>(Roll(r, 300)) - 20;
        uint8_t palette[1024];
        for (int i = 0; i < 256; ++i) {
            palette[4 * i] = palette[4 * i + 1] = palette[4 * i + 2] = static_cast<uint8_t>(r());
            palette[4 * i + 3] = static_cast<uint8_t>(r());
        }
        // One entry made not gray: the first, the last counted, the first one past the count, or anywhere.
        const int32_t counted = std::max(std::min(palCount, 256), 0);
        const int32_t flipAt[] = {0, counted - 1, counted, static_cast<int32_t>(Roll(r, 256))};
        const int32_t flip = flipAt[Roll(r, 4)];
        if (Roll(r, 2) && flip >= 0 && flip < 256) palette[4 * flip + Roll(r, 3)] ^= static_cast<uint8_t>(1 + Roll(r, 255));
        const int32_t ga = ctx.Original<GrayFn>(kGrayPalette)(palette, std::min(palCount, 256));
        const int32_t gb = gimex::jpeg::IsGrayPalette(palette, std::min(palCount, 256));
        ++ctx.cases;
        if (ga != gb) ctx.Fail("gray palette (count %d): original %d, ours %d", palCount, ga, gb);
        // colour-space fix
        uint8_t objA[0x40], objB[0x40];
        for (uint8_t &v : objA) v = static_cast<uint8_t>(r());
        const uint32_t jpegSpace = Roll(r, 8);
        std::memcpy(&objA[0x24], &jpegSpace, 4);
        std::memcpy(objB, objA, sizeof(objA));
        ctx.Original<FixFn>(kFixColor)(objA);
        gimex::jpeg::FixColorSpace(objB);
        ++ctx.cases;
        if (std::memcmp(objA, objB, sizeof(objA)) != 0) ctx.Fail("color fix (space %u): the objects differ", jpegSpace);
        if (ctx.failures.size() >= 10) return;
    }
}

// ---- the marker handler, on a fake source manager ----

struct FakeSource {
    const uint8_t *next;    // 0x00
    uint32_t left;          // 0x04
    void *init;             // 0x08
    void *fill;             // 0x0c
    void *skip;             // 0x10
};
struct FakeObject {
    uint8_t head[0x14];
    FakeSource *source; // 0x14
    uint8_t tail[0x40];
};

struct MarkerWorld {
    Bytes data;
    std::vector<size_t> chunkEnds; // where the buffer ends, in order: the fills hand them out
    size_t chunk = 0;
    size_t chunkStart = 0;
    bool failFill = false;
    int failAfter = 0;
    int fills = 0;
    FakeSource source{};
    FakeObject object{};
    std::vector<uint32_t> log;
    std::vector<Bytes> store; // each buffer in a block of its own, so a refill moves the source's pointer (not just its count)
    void Split()
    {
        size_t from = 0;
        for (const size_t end : chunkEnds) {
            store.emplace_back(data.begin() + static_cast<std::ptrdiff_t>(from), data.begin() + static_cast<std::ptrdiff_t>(end));
            store.back().push_back(0); // never empty: the pointer is real
            from = end;
        }
    }
    void Load()
    {
        source.next = store[chunk].data();
        source.left = static_cast<uint32_t>(chunkEnds[chunk] - chunkStart);
    }
    // How many stream bytes the source has consumed.
    size_t Consumed() const { return chunkStart + static_cast<size_t>(source.next - store[chunk].data()); }
};
MarkerWorld gWorld;

int32_t __cdecl WorldFill(void *cinfo)
{
    gWorld.log.push_back(0xf1110000u | (cinfo == &gWorld.object));
    ++gWorld.fills;
    if (gWorld.failFill && gWorld.fills > gWorld.failAfter) return 0;
    if (gWorld.chunk + 1 >= gWorld.chunkEnds.size()) return 0;
    gWorld.chunkStart = gWorld.chunkEnds[gWorld.chunk];
    ++gWorld.chunk;
    gWorld.Load();
    return 1;
}

void __cdecl WorldSkip(void *cinfo, int32_t count)
{
    gWorld.log.push_back(0x5c1b0000u | (cinfo == &gWorld.object));
    gWorld.log.push_back(static_cast<uint32_t>(count));
    if (count > 0) {
        const uint32_t step = std::min(static_cast<uint32_t>(count), gWorld.source.left);
        gWorld.source.next += step;
        gWorld.source.left -= step;
    }
}

void MarkerCases(difftest::Context &ctx, uint32_t seed, int cases)
{
    std::mt19937 rng(seed);
    for (int c = 0; c < cases; ++c) {
        const uint32_t caseSeed = rng();
        std::vector<uint32_t> logs[2];
        uint32_t results[2];
        size_t consumed[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(caseSeed);
            gWorld = MarkerWorld{};
            Bytes &data = gWorld.data;
            // The segment: a length (counting itself) and a payload, the nine-byte tag first or one byte off.
            const uint32_t payload = Roll(r, 8) == 0 ? 9 : 9 + Roll(r, 30);
            const uint32_t length = payload + 2;
            data.push_back(static_cast<uint8_t>(length >> 8));
            data.push_back(static_cast<uint8_t>(length));
            const char tag[] = "GIMEXARGB";
            for (int i = 0; i < 9; ++i) data.push_back(static_cast<uint8_t>(tag[i]));
            if (Roll(r, 3) == 0) data[2 + Roll(r, 9)] ^= static_cast<uint8_t>(1 + Roll(r, 255));
            RandomBytes(r, data, payload - 9 + Roll(r, 6));
            if (Roll(r, 10) == 0) data.resize(Roll(r, static_cast<uint32_t>(data.size()))); // cut short: the fill runs dry
            if (Roll(r, 12) == 0) data[0] = static_cast<uint8_t>(Roll(r, 3)), data[1] = static_cast<uint8_t>(r()); // long or odd lengths
            // Chunks: the buffer the manager holds, split anywhere.
            size_t at = 0;
            if (Roll(r, 3) == 0) gWorld.chunkEnds.push_back(0); // an empty first buffer: the first fill brings the data
            while (at < data.size()) {
                at = std::min(data.size(), at + 1 + Roll(r, Roll(r, 2) ? 4 : 40));
                gWorld.chunkEnds.push_back(at);
            }
            if (gWorld.chunkEnds.empty()) gWorld.chunkEnds.push_back(0);
            gWorld.failFill = Roll(r, 8) == 0;
            gWorld.failAfter = static_cast<int>(Roll(r, 4));
            gWorld.Split();
            gWorld.source.fill = reinterpret_cast<void *>(&WorldFill);
            gWorld.source.skip = reinterpret_cast<void *>(&WorldSkip);
            gWorld.Load();
            gWorld.object.source = &gWorld.source;
            *GameAddress<uint32_t>(kAlphaCell) = Roll(r, 4) == 0 ? 1 : 0;
            // Lengths that leave the payload shorter than the tag are kept out (the original compares an uninitialised buffer).
            const uint32_t declared = (data.size() >= 2) ? ((data[0] << 8) | data[1]) : 0;
            const bool kept = declared >= 11 || data.size() < 2;
            results[side] = kept ? (side == 0 ? static_cast<uint32_t>(ctx.Original<int32_t(__cdecl *)(void *)>(kMarker)(&gWorld.object))
                                              : static_cast<uint32_t>(gimex::jpeg::AlphaMarkerHandler(&gWorld.object)))
                                 : 0u;
            logs[side] = gWorld.log;
            logs[side].push_back(*GameAddress<uint32_t>(kAlphaCell));
            logs[side].push_back(static_cast<uint32_t>(gWorld.Consumed()));
            logs[side].push_back(gWorld.source.left);
            logs[side].push_back(static_cast<uint32_t>(gWorld.fills));
            logs[side].push_back(kept);
            consumed[side] = gWorld.Consumed();
        }
        ++ctx.cases;
        if (results[0] != results[1] || logs[0] != logs[1] || consumed[0] != consumed[1]) {
            ctx.Fail("marker case %d (seed %08x): result %u / %u, logs %zu / %zu words", c, caseSeed, results[0], results[1],
                     logs[0].size(), logs[1].size());
            if (ctx.failures.size() >= 10) return;
        }
    }
}

} // namespace

// The about 0x87ec26, is 0x87ef8c, open 0x87efdb and close 0x87ed09 (close of row 2 and PNG's wclose).
DIFF_TEST(Gimex_JpegSimple, 0x87ef8c)
{
    SimpleCases(ctx, 0x87ef8c, 1500);
}

// The row converters 0x87f255 (read) and 0x87f62d (write), the CMYK conversion 0x87f353, the gray palette test 0x87f761
// and the colour-space fix 0x87f333, called directly: every colour-space code, both pixel layouts, widths from 0.
DIFF_TEST(Gimex_JpegRows, 0x87f255)
{
    RowCases(ctx, 0x87f255, 4000);
}

// The Photoshop-marker handler 0x87f177 on a fake source manager: a segment cut anywhere by the buffer, the fill failing,
// the tag right or one byte off, the skip of the rest. Kept out: a segment shorter than the tag.
DIFF_TEST(Gimex_JpegMarker, 0x87f177)
{
    MarkerCases(ctx, 0x87f177, 3000);
}

// The write 0x87f413 (with the row converter 0x87f62d, the gray palette test 0x87f761 and the compress error exit
// 0x87f3c9 reached through it): RGB, gray (palette all gray), palette-colour, alpha-first four-component and odd bit
// counts; quality, density, empty or oversized images, an allocation and a stream write failing.
DIFF_TEST(Gimex_JpegWrite, 0x87f413)
{
    WriteCases(ctx, 0x87f413, 1200);
}

// The info 0x87ed18 (with the decompress error exit 0x87f3b5 and the marker handler 0x87f177 reached through it): the
// info block of whole decodes of clean and corrupted files.
DIFF_TEST(Gimex_JpegInfo, 0x87ed18)
{
    ReadCases(ctx, 0x87ed18, 900, true);
}

// The read 0x87f013 (with the row converter 0x87f255, the colour fix 0x87f333 and the CMYK conversion 0x87f353 reached
// through it): the pixels of clean and corrupted files, rows cut short, the pitch.
DIFF_TEST(Gimex_JpegRead, 0x87f013)
{
    ReadCases(ctx, 0x87f013, 1200, false);
}
