// Differential tests for src/gz/GimexPng.size.cpp: Gimex's PNG codec through the libpng 1.0.5 / zlib in the image (row 4 of
// the codec table): about, is, open, info, read, write and close, the read and write rows, the library's four callbacks
// and the gray palette test.
//
// The library itself (png_create_read_struct_2, png_read_image, inflate, deflate, ...) is the image's code on both sides: it
// needs no imports besides the CRT's setjmp / longjmp, so the tests run it for real. Valid files are made by a generator
// in this file (chunks with their CRCs, stored deflate blocks with their Adler checksum; every colour type and bit depth
// PNG allows, palettes, transparency, pixel density, text, random filter bytes), then corrupted (truncated, bytes flipped,
// chunks removed, header fields damaged with the CRC fixed, garbage). The codecs' allocator (0x87da65, 0x87da72) is
// replaced on both sides by a recording fake that can fail a scripted call; the library's default error function
// (0x8939f0, which prints through fprintf) is replaced by a fake that records the message and then jumps with the image's
// own longjmp, as the original does. A fake cIGZFile (tests/gz_gimex_stream.h) is the stream, one per side with the same
// bytes. Compared: results, the allocator's and the library's message log, the info block (all 0x2a70 bytes but the text
// pointer, which is only tested for null), the decoded pixels (the whole destination, sentinel-filled), the bytes
// written, the stream's position and its stray calls.
//
// Inputs kept out because the original reads garbage on them (ours does something defined there):
// - streams of fewer than eight bytes in the format test (the original checks the signature against an uninitialised
//   buffer past the bytes read);
// - a failing allocation that the library makes (through the callback 0x8800b4): png_zalloc writes through the null block.
//   Failures are scripted only for the glue's own allocations and the library's two structures (found by a run without
//   failures, by the caller of the allocator); in a write only the two structures (the glue's palette copy and
//   transparency array are not checked, and when the rows or the pixel buffer fail the library's end-of-image error
//   jumps into the returned frame of the row writer's setjmp);
// - a header claiming an image whose blocks the allocator cannot give (the library writes through the null answer);
// - a write stream that stops accepting bytes after the header (the library's error then jumps into the row writer's
//   returned setjmp frame, in the original and in ours);
// - an info whose height is not the file's (the row array is sized from it and the library fills the file's rows).
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
#include <intrin.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace gimexstream;

constexpr uint32_t kAbout = 0x8800be;
constexpr uint32_t kClose = 0x880168;
constexpr uint32_t kInfo = 0x88019a;
constexpr uint32_t kIs = 0x8803e9;
constexpr uint32_t kOpen = 0x880435;
constexpr uint32_t kRead = 0x880536;
constexpr uint32_t kReadCallback = 0x88062e;
constexpr uint32_t kReadRows = 0x88065c;
constexpr uint32_t kWrite = 0x8807bb;
constexpr uint32_t kWriteCallback = 0x880a3a;
constexpr uint32_t kWriteRows = 0x880a68;
constexpr uint32_t kGrayPalette = 0x880c5d;
constexpr uint32_t kAllocateCallback = 0x8800b4;
constexpr uint32_t kFreeCallback = 0x8800aa;
constexpr uint32_t kAllocate = 0x87da65;
constexpr uint32_t kFreeMemory = 0x87da72;
constexpr uint32_t kDefaultError = 0x8939f0; // the library's default error function
constexpr uint32_t kLongjmp = 0xc49d03;      // the image's longjmp

constexpr uint32_t kInfoBytes = 0x2a70;
constexpr uint32_t kTextCell = 0x65c;

difftest::Context *gContext = nullptr;

// ---- the recording fakes ----

std::vector<uint32_t> gLog;

struct Heap {
    std::vector<void *> blocks;
    int calls = 0;
    int failAt = 0;
    std::vector<uint8_t> library; // per call: made through the library's allocation callback (original's side only)
    std::vector<uint32_t> sizes;  // per call: the size asked
};
Heap gHeap;

// The library's allocations (through the callback 0x8800b4, the original's code) do not check the answer: png_zalloc writes
// through a null block. A failure is only scripted for the glue's own allocations, found by a run without failures.
__declspec(noinline) void *__stdcall FakeAllocate(uint32_t size)
{
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const uintptr_t callback = reinterpret_cast<uintptr_t>(OriginalAddress<void (*)(), 0x8800b4>());
    gHeap.library.push_back(caller >= callback && caller < callback + 10);
    gHeap.sizes.push_back(size);
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

uint32_t HashOf(const void *data, size_t size)
{
    uint32_t hash = 2166136261u;
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

void LogBytes(const void *data, size_t size)
{
    gLog.push_back(0x4000000u);
    gLog.push_back(HashOf(data, size));
    gLog.push_back(static_cast<uint32_t>(size));
}

// The library's default error function (a cdecl function of the png_struct and the message): note it, jump as it does.
void __cdecl FakeDefaultError(void *png, const char *message)
{
    gLog.push_back(0x3000000u);
    LogBytes(message, std::strlen(message));
    gContext->Original<void(__cdecl *)(void *, int32_t)>(kLongjmp)(png, 1);
}

struct Fakes {
    shape::CodePatch allocate{kAllocate, reinterpret_cast<const void *>(&FakeAllocate)};
    shape::CodePatch release{kFreeMemory, reinterpret_cast<const void *>(&FakeRelease)};
    // Ours call our allocator (lane MP), so it gets the same fake.
    shape::CodePatch ourAllocate{reinterpret_cast<void *>(&gimex::Allocate), reinterpret_cast<const void *>(&FakeAllocate)};
    shape::CodePatch ourRelease{reinterpret_cast<void *>(&gimex::FreeMemory), reinterpret_cast<const void *>(&FakeRelease)};
    shape::CodePatch error{kDefaultError, reinterpret_cast<const void *>(&FakeDefaultError)};
};

// A failing call index among the glue's own allocations of a run without failures (0: none, no failure).
int PickFailAt(std::mt19937 &r, bool structuresOnly)
{
    // The library's two structures are made through the callback but their creation checks the answer.
    constexpr uint32_t kPngStructBytes = 0x228;
    constexpr uint32_t kInfoStructBytes = 0xb8;
    std::vector<int> glue;
    bool seenPng = false;
    bool seenInfo = false;
    for (size_t i = 0; i < gHeap.library.size(); ++i) {
        // The first block of each structure's size (a later block may have the size by chance).
        bool structure = false;
        if (gHeap.library[i] && gHeap.sizes[i] == kPngStructBytes && !seenPng) structure = seenPng = true;
        if (gHeap.library[i] && gHeap.sizes[i] == kInfoStructBytes && !seenInfo) structure = seenInfo = true;
        if (structure || (!structuresOnly && !gHeap.library[i])) glue.push_back(static_cast<int>(i) + 1);
    }
    if (glue.empty()) return 0;
    return glue[r() % glue.size()];
}

void ResetSide(int failAt)
{
    for (void *block : gHeap.blocks) std::free(block);
    gHeap = Heap{};
    gHeap.failAt = failAt;
    gLog.clear();
}

// ---- making PNG files ----

uint32_t Crc32(const uint8_t *data, size_t size)
{
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        ready = true;
    }
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    return crc ^ 0xffffffffu;
}

uint32_t Adler32(const Bytes &data)
{
    uint32_t a = 1, b = 0;
    for (const uint8_t byte : data) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

void PutBig(Bytes &out, uint32_t value)
{
    for (int i = 3; i >= 0; --i) out.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

void Chunk(Bytes &out, const char *type, const Bytes &data)
{
    PutBig(out, static_cast<uint32_t>(data.size()));
    Bytes body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    PutBig(out, Crc32(body.data(), body.size()));
}

struct PngSpec {
    int width = 1;
    int height = 1;
    int depth = 8;
    int type = 2;
    int paletteCount = 0;
    int trns = 0;          // bytes of tRNS (0: none)
    uint32_t phys = 0;     // pixels per metre (0: no pHYs)
    bool text = false;
    bool badFilter = false;
    Bytes palette;
};

int Channels(int type)
{
    switch (type) {
    case 0: return 1;
    case 2: return 3;
    case 3: return 1;
    case 4: return 2;
    default: return 4;
    }
}

PngSpec RandomSpec(std::mt19937 &r)
{
    static const int kCombos[][2] = {{0, 1}, {0, 2}, {0, 4}, {0, 8}, {0, 16}, {2, 8}, {2, 16}, {3, 1}, {3, 2}, {3, 4}, {3, 8},
                                     {4, 8}, {4, 16}, {6, 8}, {6, 16}, {2, 8}, {6, 8}, {3, 8}, {0, 8}, {4, 8}};
    PngSpec spec;
    const auto &combo = kCombos[Roll(r, 20)];
    spec.type = combo[0];
    spec.depth = combo[1];
    spec.width = 1 + static_cast<int>(Roll(r, 40));
    spec.height = 1 + static_cast<int>(Roll(r, 30));
    if (Roll(r, 14) == 0) {
        spec.width = 40 + static_cast<int>(Roll(r, 60));
        spec.height = 20 + static_cast<int>(Roll(r, 40));
    }
    if (spec.type == 3) {
        spec.paletteCount = 1 + static_cast<int>(Roll(r, 1u << spec.depth));
        for (int i = 0; i < spec.paletteCount * 3; ++i) spec.palette.push_back(static_cast<uint8_t>(r()));
        if (Roll(r, 2)) spec.trns = 1 + static_cast<int>(Roll(r, static_cast<uint32_t>(spec.paletteCount)));
    } else if (spec.type == 0 && Roll(r, 4) == 0) {
        spec.trns = 2;
    } else if (spec.type == 2 && Roll(r, 4) == 0) {
        spec.trns = 6;
    }
    if (Roll(r, 2)) spec.phys = Roll(r, 6) == 0 ? r() : 1 + Roll(r, 20000);
    spec.text = Roll(r, 4) == 0;
    spec.badFilter = Roll(r, 25) == 0;
    return spec;
}

Bytes MakePng(std::mt19937 &r, const PngSpec &spec)
{
    Bytes file = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    Bytes header;
    PutBig(header, static_cast<uint32_t>(spec.width));
    PutBig(header, static_cast<uint32_t>(spec.height));
    header.push_back(static_cast<uint8_t>(spec.depth));
    header.push_back(static_cast<uint8_t>(spec.type));
    header.push_back(0);
    header.push_back(0);
    header.push_back(0);
    Chunk(file, "IHDR", header);
    if (spec.type == 3) Chunk(file, "PLTE", spec.palette);
    if (spec.trns != 0) {
        Bytes trns;
        RandomBytes(r, trns, static_cast<size_t>(spec.trns));
        if (spec.type == 0) trns = {0, static_cast<uint8_t>(Roll(r, 1u << std::min(spec.depth, 8)))};
        if (spec.type == 2) trns = {0, static_cast<uint8_t>(r()), 0, static_cast<uint8_t>(r()), 0, static_cast<uint8_t>(r())};
        Chunk(file, "tRNS", trns);
    }
    if (spec.phys != 0) {
        Bytes phys;
        PutBig(phys, spec.phys);
        PutBig(phys, spec.phys);
        phys.push_back(1);
        Chunk(file, "pHYs", phys);
    }
    if (spec.text) {
        const char text[] = "Comment\0made by the test";
        Chunk(file, "tEXt", Bytes(text, text + sizeof(text) - 1));
    }
    const size_t rowBytes = (static_cast<size_t>(spec.width) * static_cast<size_t>(Channels(spec.type)) * static_cast<size_t>(spec.depth) + 7) / 8;
    Bytes raw;
    const int badRow = spec.badFilter ? static_cast<int>(Roll(r, static_cast<uint32_t>(spec.height))) : -1;
    for (int y = 0; y < spec.height; ++y) {
        raw.push_back(y == badRow ? static_cast<uint8_t>(5 + Roll(r, 250)) : static_cast<uint8_t>(Roll(r, 5)));
        for (size_t x = 0; x < rowBytes; ++x) {
            // half the files are smooth, the other half random
            raw.push_back(static_cast<uint8_t>(spec.badFilter || (spec.width & 1) ? r() : (x * 3 + y * 5 + Roll(r, 3))));
        }
    }
    Bytes zlib = {0x78, 0x01};
    size_t at = 0;
    do {
        const size_t take = std::min<size_t>(raw.size() - at, 1 + Roll(r, 3) * 40000 + 100);
        const bool last = at + take >= raw.size();
        zlib.push_back(last ? 1 : 0);
        zlib.push_back(static_cast<uint8_t>(take));
        zlib.push_back(static_cast<uint8_t>(take >> 8));
        zlib.push_back(static_cast<uint8_t>(~take));
        zlib.push_back(static_cast<uint8_t>(~take >> 8));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(at), raw.begin() + static_cast<std::ptrdiff_t>(at + take));
        at += take;
    } while (at < raw.size());
    PutBig(zlib, Adler32(raw));
    // The compressed stream in one to three IDAT chunks.
    size_t from = 0;
    uint32_t pieces = 1 + Roll(r, 3);
    while (from < zlib.size()) {
        const size_t take = pieces <= 1 ? zlib.size() - from : std::min<size_t>(zlib.size() - from, 1 + Roll(r, static_cast<uint32_t>(zlib.size() / 2 + 1)));
        Chunk(file, "IDAT", Bytes(zlib.begin() + static_cast<std::ptrdiff_t>(from), zlib.begin() + static_cast<std::ptrdiff_t>(from + take)));
        from += take;
        if (pieces > 0) --pieces;
    }
    Chunk(file, "IEND", Bytes{});
    return file;
}

// The offsets of each chunk (its length field) in a file.
std::vector<size_t> ChunkStarts(const Bytes &file)
{
    std::vector<size_t> starts;
    size_t at = 8;
    while (at + 12 <= file.size()) {
        const size_t length = (static_cast<size_t>(file[at]) << 24) | (file[at + 1] << 16) | (file[at + 2] << 8) | file[at + 3];
        if (at + 12 + length > file.size()) break; // a damaged length: the rest is not a chunk
        starts.push_back(at);
        at += 12 + length;
    }
    return starts;
}

void FixCrcs(Bytes &file)
{
    for (const size_t at : ChunkStarts(file)) {
        const size_t length = (static_cast<size_t>(file[at]) << 24) | (file[at + 1] << 16) | (file[at + 2] << 8) | file[at + 3];
        const uint32_t crc = Crc32(&file[at + 4], 4 + length);
        for (int i = 0; i < 4; ++i) file[at + 8 + length + static_cast<size_t>(i)] = static_cast<uint8_t>(crc >> (24 - 8 * i));
    }
}

Bytes Corrupt(std::mt19937 &r, Bytes file, int kind)
{
    const std::vector<size_t> starts = ChunkStarts(file);
    switch (kind) {
    case 0: return file;
    case 1: file.resize(Roll(r, static_cast<uint32_t>(file.size() + 1))); return file; // truncated anywhere
    case 2:                                                                            // bytes flipped anywhere
        for (uint32_t n = 1 + Roll(r, 4); n > 0; --n) file[Roll(r, static_cast<uint32_t>(file.size()))] ^= static_cast<uint8_t>(1 + Roll(r, 255));
        return file;
    case 3: { // bytes flipped inside the pixel data, the checksums fixed (the decompressor or the filters complain)
        for (uint32_t n = 1 + Roll(r, 3); n > 0; --n) file[std::min<size_t>(file.size() - 1, 40 + Roll(r, static_cast<uint32_t>(file.size() - 40)))] ^= static_cast<uint8_t>(1 + Roll(r, 255));
        FixCrcs(file);
        return file;
    }
    case 4: // a chunk removed
        if (starts.size() > 1) {
            const size_t k = Roll(r, static_cast<uint32_t>(starts.size()));
            const size_t end = k + 1 < starts.size() ? starts[k + 1] : file.size();
            file.erase(file.begin() + static_cast<std::ptrdiff_t>(starts[k]), file.begin() + static_cast<std::ptrdiff_t>(end));
        }
        return file;
    case 5: // header fields damaged, the CRC fixed
        if (file.size() > 33) {
            const Bytes before = file;
            for (uint32_t n = 1 + Roll(r, 3); n > 0; --n) file[16 + Roll(r, 13)] = static_cast<uint8_t>(Roll(r, 3) == 0 ? r() : Roll(r, 7));
            // A header claiming an enormous image makes the library ask for a block no allocator gives, and the original
            // writes through the null answer: those sizes are kept out.
            const uint64_t width = (static_cast<uint64_t>(file[16]) << 24) | (file[17] << 16) | (file[18] << 8) | file[19];
            const uint64_t height = (static_cast<uint64_t>(file[20]) << 24) | (file[21] << 16) | (file[22] << 8) | file[23];
            if (width * height * 8 > 0x1000000 || width > 0x100000 || height > 0x100000) {
                std::copy(before.begin() + 16, before.begin() + 24, file.begin() + 16);
            }
            FixCrcs(file);
        }
        return file;
    case 6: { // garbage after a good signature
        Bytes garbage(file.begin(), file.begin() + 8);
        for (uint32_t n = Roll(r, 60); n > 0; --n) garbage.push_back(static_cast<uint8_t>(r()));
        return garbage;
    }
    case 7: // trailing garbage
        for (uint32_t n = Roll(r, 20); n > 0; --n) file.push_back(static_cast<uint8_t>(r()));
        return file;
    case 8: file[Roll(r, 8)] ^= static_cast<uint8_t>(1 + Roll(r, 255)); return file; // signature broken
    default: return file;
    }
}

// ---- the images the writer is given ----

struct Image {
    int width = 1;
    int height = 1;
    int kind = 0;
    int pitch = 4;
    std::vector<uint8_t> info = std::vector<uint8_t>(kInfoBytes);
    Bytes pixels;
    gimex::Info *header() { return reinterpret_cast<gimex::Info *>(info.data()); }
};

enum Kind { kRgb32, kRgba32, kGray8, kGrayAlpha8, kPalette8, kPaletteAlpha8, kOddBits, kKinds };

Image MakeImage(std::mt19937 &r)
{
    Image image;
    image.kind = static_cast<int>(Roll(r, kKinds));
    image.width = 1 + static_cast<int>(Roll(r, 40));
    image.height = 1 + static_cast<int>(Roll(r, 24));
    if (Roll(r, 14) == 0) {
        image.width = 40 + static_cast<int>(Roll(r, 60));
        image.height = 20 + static_cast<int>(Roll(r, 40));
    }
    gimex::Info *const info = image.header();
    info->width = image.width;
    info->height = image.height;
    static const float kDpi[] = {0.0f, 0.0f, 72.0f, 96.5f, 300.0f, 1.0f, -3.0f, 65536.0f, 1e10f, 0.4f};
    info->dotsPerInch = kDpi[Roll(r, 10)];
    if (Roll(r, 40) == 0) info->dotsPerInch = std::nanf("");
    const bool alpha = image.kind == kRgba32 || image.kind == kGrayAlpha8 || image.kind == kPaletteAlpha8 || (image.kind == kOddBits && Roll(r, 2));
    info->alphaBits = alpha ? (Roll(r, 4) == 0 ? 1 + r() : 8) : 0;
    const bool direct = image.kind == kRgb32 || image.kind == kRgba32;
    info->bitsPerPixel = direct ? 32 : 8;
    if (image.kind == kOddBits) {
        static const int32_t kBits[] = {24, 16, 0, 33, -1, 1, 31, 32, 8};
        info->bitsPerPixel = kBits[Roll(r, 9)];
    }
    static const int32_t kSourceBits[] = {1, 2, 3, 4, 5, 6, 7, 8, 8, 8, 0, -1, 9, 16, 100};
    info->sourceBits = Roll(r, 3) == 0 ? kSourceBits[Roll(r, 15)] : (Roll(r, 2) ? 8 : 1 << Roll(r, 4));
    info->paletteCount = 256;
    if (!direct) {
        const bool gray = image.kind == kGray8 || image.kind == kGrayAlpha8 || (image.kind == kOddBits && Roll(r, 2));
        info->paletteCount = 1 + Roll(r, 256);
        if (Roll(r, 20) == 0) info->paletteCount = Roll(r, 3) == 0 ? 0 : 257 + Roll(r, 40);
        for (int i = 0; i < 256; ++i) {
            const uint8_t v = static_cast<uint8_t>(r());
            info->palette[i][0] = info->palette[i][1] = info->palette[i][2] = v;
            info->palette[i][3] = static_cast<uint8_t>(r());
            if (!gray) {
                info->palette[i][0] = static_cast<uint8_t>(r());
                info->palette[i][1] = static_cast<uint8_t>(r());
            }
        }
        // The entry that is not gray: where the count's check stops.
        if (gray && Roll(r, 4) == 0 && info->paletteCount > 0 && info->paletteCount <= 256) {
            info->palette[Roll(r, info->paletteCount)][Roll(r, 3)] ^= static_cast<uint8_t>(1 + Roll(r, 255));
        }
        if (gray && Roll(r, 4) == 0 && info->paletteCount < 256) info->palette[info->paletteCount][0] ^= 0x40; // one past the count
    }
    const int bytesPerPixel = info->bitsPerPixel == 32 ? 4 : 1;
    image.pitch = bytesPerPixel * image.width + static_cast<int>(Roll(r, 6) == 0 ? Roll(r, 17) : 0);
    const size_t bytes = static_cast<size_t>(image.pitch) * static_cast<size_t>(image.height) + 64;
    image.pixels.clear();
    if (Roll(r, 2)) {
        RandomBytes(r, image.pixels, bytes);
    } else {
        const uint32_t a = Roll(r, 7), b = Roll(r, 5), base = r();
        for (size_t i = 0; i < bytes; ++i) image.pixels.push_back(static_cast<uint8_t>(base + (i / 4) * a + (i / 32) * b + Roll(r, 3)));
    }
    return image;
}

// Will the writer allocate the palette's RGB copy (and the transparency array)? Then those allocations must not fail.
bool NeedsPaletteAllocation(const gimex::Info *info)
{
    if (info->bitsPerPixel == 32) return false;
    for (int32_t i = 0; i < static_cast<int32_t>(info->paletteCount); ++i) {
        if (info->palette[i][2] != info->palette[i][1] || info->palette[i][2] != info->palette[i][0]) return true;
    }
    return false;
}

// ---- running one side ----

struct Record {
    std::vector<uint32_t> log;
    Bytes pixels;  // the decoded pixels (read), else empty
    Bytes written; // the stream's bytes (write)
};

Record Finish()
{
    Record record;
    record.log = gLog;
    for (void *block : gHeap.blocks) {
        if (block != nullptr) record.log.push_back(0x6000000u); // a leaked block
    }
    return record;
}

Record RunWrite(difftest::Context &ctx, bool ours, Image image, int failAt, int64_t writeLimit, bool written)
{
    ResetSide(failAt);
    Stream stream(Bytes{});
    stream.file.writeLimit = writeLimit;
    gimex::Instance instance{};
    instance.stream = stream.Get();
    instance.unknownC = written ? 1 : 0;
    const int32_t result = ours ? gimex::png::WriteFrame(&instance, image.header(), image.pixels.data(), image.pitch)
                                : ctx.Original<gimex::WriteFn>(kWrite)(&instance, image.header(), image.pixels.data(), image.pitch);
    gLog.push_back(0x5000000u);
    gLog.push_back(static_cast<uint32_t>(result));
    gLog.push_back(instance.unknownC);
    gLog.push_back(static_cast<uint32_t>(stream.file.stray));
    gLog.push_back(static_cast<uint32_t>(stream.file.written.size()));
    LogBytes(image.info.data(), image.info.size()); // the info is untouched
    LogBytes(image.pixels.data(), image.pixels.size());
    Record record = Finish();
    record.written = stream.file.written;
    return record;
}

struct ReadPlan {
    bool callInfo = true;
    bool callRead = true;
    int failAt = 0;
    int pitchSlack = 0; // bytes added to the row size
    uint32_t startPos = 0; // where the stream stands when the open is called (the open seeks to the start)
    bool infoAgain = false;
};

Record RunRead(difftest::Context &ctx, bool ours, const Bytes &data, const PngSpec &spec, const ReadPlan &plan)
{
    ResetSide(plan.failAt);
    Stream stream(data);
    stream.file.pos = plan.startPos;
    gimex::Instance *instance = nullptr;
    const int32_t opened = ours ? gimex::png::Open(&instance, stream.Get(), "x.png", 0)
                                : ctx.Original<gimex::OpenFn>(kOpen)(&instance, stream.Get(), "x.png", 0);
    gLog.push_back(0x5000000u);
    gLog.push_back(static_cast<uint32_t>(opened));
    gLog.push_back(stream.file.pos);
    Record record;
    if (opened != 0 && instance != nullptr) {
        LogBytes(instance, 0x10);
        gLog.push_back(instance->stream == stream.Get());
    }
    std::vector<uint8_t> infoCopy(kInfoBytes);
    gimex::Info *info = reinterpret_cast<gimex::Info *>(infoCopy.data());
    bool haveInfo = false;
    for (int again = 0; again < (plan.infoAgain ? 2 : 1) && opened != 0 && plan.callInfo; ++again) {
        gimex::Info *const answered = ours ? gimex::png::GetInfo(instance, 0) : ctx.Original<gimex::InfoFn>(kInfo)(instance, 0);
        gLog.push_back(0x7000000u);
        gLog.push_back(answered != nullptr);
        gLog.push_back(stream.file.pos);
        if (answered) {
            uint8_t copy[kInfoBytes];
            std::memcpy(copy, answered, kInfoBytes);
            uint32_t textPointer;
            std::memcpy(&textPointer, copy + kTextCell, 4);
            std::memset(copy + kTextCell, 0, 4);
            gLog.push_back(textPointer != 0);
            LogBytes(copy, kInfoBytes);
            std::memcpy(infoCopy.data(), answered, kInfoBytes);
            haveInfo = true;
            ctx.Original<int32_t(__stdcall *)(void *)>(kFreeMemory)(answered);
        }
    }
    // A damaged header can claim an enormous image: no destination of that size is made (nothing is read then).
    const bool tooBig = static_cast<uint64_t>(std::max(info->width, 1)) * static_cast<uint64_t>(std::max(info->height, 0) + 2) * 4u > 0x2000000u;
    if (opened != 0 && plan.callRead && !(haveInfo && tooBig)) {
        if (!haveInfo) {
            // No info asked for: one made from the file's own header (the codec's own rules for the pixel size).
            std::memset(infoCopy.data(), 0, kInfoBytes);
            info->width = spec.width;
            info->height = spec.height;
            info->bitsPerPixel = (spec.type == 2 || spec.type == 4 || spec.type == 6) ? 32 : 8;
        }
        const int width = info->width > 0 ? info->width : 1;
        const int height = info->height > 0 ? info->height : 0;
        const int32_t pitch = (info->bitsPerPixel == 32 ? 4 : 1) * width + plan.pitchSlack;
        const size_t bytes = static_cast<size_t>(std::max(pitch, 0)) * static_cast<size_t>(height + 2) + 4u * static_cast<size_t>(width) + 64;
        record.pixels.assign(bytes, 0xc3);
        const int32_t result = ours ? gimex::png::ReadFrame(instance, info, record.pixels.data(), pitch)
                                    : ctx.Original<gimex::ReadFn>(kRead)(instance, info, record.pixels.data(), pitch);
        gLog.push_back(0x8000000u);
        gLog.push_back(static_cast<uint32_t>(result));
        gLog.push_back(stream.file.pos);
    }
    gLog.push_back(0x9000000u);
    gLog.push_back(static_cast<uint32_t>(stream.file.stray));
    gLog.push_back(stream.file.pos);
    gLog.push_back(static_cast<uint32_t>(stream.file.written.size()));
    if (instance) {
        const int32_t closed = ours ? gimex::png::Close(instance) : ctx.Original<gimex::CloseFn>(kClose)(instance);
        gLog.push_back(static_cast<uint32_t>(closed));
    }
    Record done = Finish();
    done.pixels = record.pixels;
    return done;
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
        const bool failing = Roll(r, 8) == 0;
        // A stream that stops accepting bytes while the signature and the header are written (a later failure makes the
        // library jump into the returned frame of the row writer's setjmp in the original).
        const int64_t limit = Roll(r, 6) == 0 ? static_cast<int64_t>(Roll(r, 33)) : -1;
        const bool written = Roll(r, 12) == 0;
        ctx.Mark("write");
        int failAt = 0;
        if (failing) {
            RunWrite(ctx, false, image, 0, limit, written);
            failAt = PickFailAt(r, true);        }
        const Record original = RunWrite(ctx, false, image, failAt, limit, written);
        const Record mine = RunWrite(ctx, true, image, failAt, limit, written);
        char what[64];
        std::snprintf(what, sizeof(what), "write case %d (kind %d)", c, image.kind);
        if (Compare(ctx, original, mine, what, caseSeed)) {
            if (!original.written.empty()) ++wrote;
        } else {
            const gimex::Info *h = image.header();
            ctx.Fail("   %dx%d bits %d source %d count %u alpha %u dpi %g pitch %d fail %d limit %lld", h->width, h->height,
                     h->bitsPerPixel, h->sourceBits, h->paletteCount, h->alphaBits, h->dotsPerInch, image.pitch, failAt,
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
        const PngSpec spec = RandomSpec(r);
        const Bytes clean = MakePng(r, spec);
        const int kind = Roll(r, 3) == 0 ? static_cast<int>(Roll(r, 9)) : 0;
        const Bytes file = Corrupt(r, clean, kind);
        ReadPlan plan;
        const bool failing = Roll(r, 8) == 0;
        plan.callRead = !infoOnly;
        plan.callInfo = infoOnly || Roll(r, 4) != 0;
        plan.infoAgain = Roll(r, 4) == 0;
        plan.pitchSlack = Roll(r, 4) == 0 ? static_cast<int>(Roll(r, 9)) : 0;
        plan.startPos = Roll(r, 3) == 0 ? 1 + Roll(r, static_cast<uint32_t>(file.size() + 1)) : 0;
        ctx.Mark("read");
        if (failing) {
            RunRead(ctx, false, file, spec, plan);
            plan.failAt = PickFailAt(r, false);
        }
        const Record original = RunRead(ctx, false, file, spec, plan);
        const Record mine = RunRead(ctx, true, file, spec, plan);
        char what[80];
        std::snprintf(what, sizeof(what), "%s case %d (type %d depth %d, corruption %d)", infoOnly ? "info" : "read", c, spec.type, spec.depth, kind);
        if (Compare(ctx, original, mine, what, caseSeed) && kind == 0 && plan.failAt == 0) ++decoded;
        if (ctx.failures.size() >= 10) return;
    }
    if (decoded < cases / 4) ctx.Fail("only %d of %d clean decodes ran", decoded, cases);
}

// ---- about, is, open, close ----

void SimpleCases(difftest::Context &ctx, uint32_t seed, int cases)
{
    Fakes fakes;
    std::mt19937 rng(seed);
    for (int c = 0; c < cases; ++c) {
        const uint32_t caseSeed = rng();
        auto side = [&](bool ours) {
            std::mt19937 r(caseSeed);
            const uint32_t what = Roll(r, 4);
            // Only the glue's own allocations (the about's, the instance's and the library structures'; the library's
            // later ones are not checked by the original) can fail.
            const bool failing = Roll(r, 4) == 0;
            const int failAt = !failing ? 0 : what == 0 ? 1 : what == 2 ? 1 + static_cast<int>(Roll(r, 4)) : 0;
            ResetSide(failAt);
            if (what == 0) { // about
                gimex::About *about = ours ? gimex::png::NewAbout() : ctx.Original<gimex::AboutFn>(kAbout)();
                gLog.push_back(0x5000000u);
                gLog.push_back(about != nullptr);
                if (about) LogBytes(about, sizeof(gimex::About));
            } else if (what == 1) { // is: a stream of eight bytes or more, the signature right or damaged
                static const uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
                Bytes data(kSignature, kSignature + 8);
                const uint32_t damage = Roll(r, 6);
                if (damage == 1) data[Roll(r, 8)] ^= static_cast<uint8_t>(1 + Roll(r, 255));
                if (damage == 2) data[4 + Roll(r, 4)] ^= static_cast<uint8_t>(1 + Roll(r, 255)); // the first four are fine
                if (damage == 3) data[Roll(r, 4)] ^= static_cast<uint8_t>(1 + Roll(r, 255));     // the first four are not
                if (damage == 4) {
                    data.clear();
                    RandomBytes(r, data, 8);
                }
                RandomBytes(r, data, Roll(r, 20));
                Stream stream(data);
                if (Roll(r, 3) == 0) stream.file.pos = Roll(r, static_cast<uint32_t>(data.size() + 1)); // not at its start
                const int32_t result = ours ? gimex::png::Probe(stream.Get()) : ctx.Original<gimex::IsFn>(kIs)(stream.Get());
                gLog.push_back(0x5000000u);
                gLog.push_back(static_cast<uint32_t>(result));
                gLog.push_back(stream.file.pos);
                gLog.push_back(static_cast<uint32_t>(stream.file.stray));
            } else if (what == 2) { // open (a file, or random bytes), then close
                Bytes data;
                if (Roll(r, 2)) {
                    data = MakePng(r, RandomSpec(r));
                    if (Roll(r, 3) == 0) data = Corrupt(r, data, 1 + static_cast<int>(Roll(r, 8)));
                } else {
                    RandomBytes(r, data, Roll(r, 40));
                }
                Stream stream(data);
                if (Roll(r, 3) == 0) stream.file.pos = Roll(r, static_cast<uint32_t>(data.size() + 1));
                gimex::Instance *marker = reinterpret_cast<gimex::Instance *>(0x12345678);
                gimex::Instance *instance = marker;
                const int32_t opened = ours ? gimex::png::Open(&instance, stream.Get(), "x.png", static_cast<int32_t>(r()))
                                            : ctx.Original<gimex::OpenFn>(kOpen)(&instance, stream.Get(), "x.png", static_cast<int32_t>(r()));
                gLog.push_back(0x5000000u);
                gLog.push_back(static_cast<uint32_t>(opened));
                gLog.push_back(instance == marker);
                gLog.push_back(stream.file.pos);
                gLog.push_back(static_cast<uint32_t>(stream.file.stray));
                if (opened != 0 && instance != marker) {
                    LogBytes(instance, 0x10);
                    gLog.push_back(instance->stream == stream.Get());
                    const int32_t closed = ours ? gimex::png::Close(instance) : ctx.Original<gimex::CloseFn>(kClose)(instance);
                    gLog.push_back(static_cast<uint32_t>(closed));
                }
            } else { // close of nothing
                const int32_t closed = ours ? gimex::png::Close(nullptr) : ctx.Original<gimex::CloseFn>(kClose)(nullptr);
                gLog.push_back(0x5000000u);
                gLog.push_back(static_cast<uint32_t>(closed));
            }
            return Finish();
        };
        const Record original = side(false);
        const Record mine = side(true);
        char label[48];
        std::snprintf(label, sizeof(label), "simple case %d", c);
        Compare(ctx, original, mine, label, caseSeed);
        if (ctx.failures.size() >= 10) return;
    }
}

// ---- the small helpers, called directly ----

using GrayFn = int32_t(__cdecl *)(const uint8_t *, int32_t);

void HelperCases(difftest::Context &ctx, uint32_t seed, int cases)
{
    std::mt19937 rng(seed);
    for (int c = 0; c < cases; ++c) {
        const uint32_t caseSeed = rng();
        std::mt19937 r(caseSeed);
        ctx.Mark("helpers");
        static const int32_t kCounts[] = {-1, 0, 1, 2, 3, 255, 256, 257};
        const int32_t count = Roll(r, 2) ? kCounts[Roll(r, 8)] : static_cast<int32_t>(Roll(r, 300)) - 20;
        uint8_t palette[1024];
        for (int i = 0; i < 256; ++i) {
            palette[4 * i] = palette[4 * i + 1] = palette[4 * i + 2] = static_cast<uint8_t>(r());
            palette[4 * i + 3] = static_cast<uint8_t>(r());
        }
        // One entry made not gray: the first, the last counted, the first one past the count, or anywhere.
        const int32_t counted = std::max(std::min(count, 256), 0);
        const int32_t flipAt[] = {0, counted - 1, counted, static_cast<int32_t>(Roll(r, 256))};
        const int32_t flip = flipAt[Roll(r, 4)];
        if (Roll(r, 2) && flip >= 0 && flip < 256) palette[4 * flip + Roll(r, 3)] ^= static_cast<uint8_t>(1 + Roll(r, 255));
        const int32_t a = ctx.Original<GrayFn>(kGrayPalette)(palette, std::min(count, 256));
        const int32_t b = gimex::png::IsGrayPalette(palette, std::min(count, 256));
        ++ctx.cases;
        if (a != b) ctx.Fail("gray palette (count %d): original %d, ours %d", count, a, b);
        if (ctx.failures.size() >= 10) return;
    }
}

// The allocation and free callbacks the library is given, called directly (they forward to the codecs' allocator).
void CallbackCases(difftest::Context &ctx, uint32_t seed, int cases)
{
    Fakes fakes;
    std::mt19937 rng(seed);
    for (int c = 0; c < cases; ++c) {
        const uint32_t caseSeed = rng();
        std::mt19937 r(caseSeed);
        ctx.Mark("callbacks");
        const uint32_t size = Roll(r, 4) == 0 ? Roll(r, 2) * 0x5000000u : Roll(r, 5000);
        const int failAt = Roll(r, 4) == 0 ? 1 : 0;
        const bool doFree = Roll(r, 4) != 0;
        Record sides[2];
        for (int side = 0; side < 2; ++side) {
            ResetSide(failAt);
            void *const block = side == 0 ? ctx.Original<void *(__cdecl *)(void *, uint32_t)>(kAllocateCallback)(nullptr, size)
                                          : gimex::png::AllocateCallback(nullptr, size);
            gLog.push_back(0x5000000u);
            gLog.push_back(block != nullptr);
            if (block != nullptr && doFree) {
                if (side == 0) {
                    ctx.Original<void(__cdecl *)(void *, void *)>(kFreeCallback)(nullptr, block);
                } else {
                    gimex::png::FreeCallback(nullptr, block);
                }
            }
            sides[side] = Finish();
        }
        Compare(ctx, sides[0], sides[1], "callback case", caseSeed);
        if (ctx.failures.size() >= 10) return;
    }
}

} // namespace

// The about 0x8800be, is 0x8803e9, open 0x880435 and close 0x880168.
DIFF_TEST(Gimex_PngSimple, 0x8803e9)
{
    gContext = &ctx;
    SimpleCases(ctx, 0x8803e9, 1500);
}

// The gray palette test 0x880c5d and the library's allocation callback 0x8800b4 and free callback 0x8800aa, called directly.
DIFF_TEST(Gimex_PngHelpers, 0x880c5d)
{
    gContext = &ctx;
    HelperCases(ctx, 0x880c5d, 3000);
    CallbackCases(ctx, 0x8800b4, 600);
}

// The write 0x8807bb, with the rows 0x880a68 and the library's write callback 0x880a3a reached through it.
DIFF_TEST(Gimex_PngWrite, 0x8807bb)
{
    gContext = &ctx;
    WriteCases(ctx, 0x8807bb, 1200);
}

// The info 0x88019a (with the open 0x880435, the read callback 0x88062e reached through it).
DIFF_TEST(Gimex_PngInfo, 0x88019a)
{
    gContext = &ctx;
    ReadCases(ctx, 0x88019a, 900, true);
}

// The read 0x880536, with the rows 0x88065c and the library's read callback 0x88062e reached through it.
DIFF_TEST(Gimex_PngRead, 0x880536)
{
    gContext = &ctx;
    ReadCases(ctx, 0x880536, 1200, false);
}
