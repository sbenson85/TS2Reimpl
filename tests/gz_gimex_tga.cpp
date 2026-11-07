// Differential tests for src/gz/GimexTga.size.cpp: Gimex's TGA codec (row 6 of the codec table).
//
// The codec runs against a fake cIGZFile (tests/gz_gimex_stream.h) holding a synthetic file, one fake per side with the same
// bytes. The stream helpers and the allocator are each side's own (ours: lane MP). Compared: results, the info block (all
// 0x2a70 bytes), the decoded pixels (the whole destination, sentinel-filled), the bytes written, the stream's position.
//
// Inputs kept out because the original reads or writes garbage on them (ours does something defined there):
// - a stream of 1..17 bytes (the header buffer is uninitialised);
// - a file cut inside the colour map or the pixel data (the palette table and the row buffer are uninitialised);
// - a colour map that loads more entries than the palette or the table holds (more than 256 from `first`, or more than
//   the entry count the image type reads);
// - run-length packets that run past the row, or end of file inside a row;
// - a write of a palette of more than 256 entries (the original's 256-byte map), of fewer than one for an 8-bit image,
//   or of an index at or past the palette's count (the map's bytes there are uninitialised).
//
// The helpers (row flip, row decoder, run count, row encoder) are reached through the entry points; their RVAs are named
// in the read and write tests.
#include "difftest.h"
#include "game/game.h"
#include "game/original.h"
#include "gz/GimexFormats.h"
#include "gz_gimex_stream.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

namespace {

using namespace gimexstream;

constexpr uint32_t kAbout = 0x882862;
constexpr uint32_t kIs = 0x882db5;
constexpr uint32_t kOpen = 0x882e91;
constexpr uint32_t kInfo = 0x882958;
constexpr uint32_t kRead = 0x882ec9;
constexpr uint32_t kWrite = 0x8833ac;
constexpr uint32_t kWOpen = 0x87f3e4;
constexpr uint32_t kFree = 0x87fe5d;
constexpr uint32_t kFreeMemory = 0x87da72;

using FreeFn = int32_t(__stdcall *)(void *);

// ---- the file builder ----

struct Header {
    uint8_t idLength = 0;
    uint8_t colorMapType = 0;
    uint8_t imageType = 2;
    uint32_t first = 0;
    uint32_t length = 0;
    uint8_t entryBits = 0;
    uint32_t originX = 0;
    uint32_t originY = 0;
    uint32_t width = 1;
    uint32_t height = 1;
    uint8_t depth = 24;
    uint8_t descriptor = 0;
};

void PutHeader(Bytes &b, const Header &h)
{
    b.push_back(h.idLength);
    b.push_back(h.colorMapType);
    b.push_back(h.imageType);
    Put16(b, h.first);
    Put16(b, h.length);
    b.push_back(h.entryBits);
    Put16(b, h.originX);
    Put16(b, h.originY);
    Put16(b, h.width);
    Put16(b, h.height);
    b.push_back(h.depth);
    b.push_back(h.descriptor);
}

int EntryBytes(int entryBits)
{
    return (entryBits + 7) >> 3;
}

// The bytes the decoder takes for `count` pixels of `depth` (16 is 15 as the info says).
int PixelBytes(int depth)
{
    return depth == 15 || depth == 16 ? 2 : depth / 8;
}

void MakeRleRow(std::mt19937 &r, Bytes &out, int width, int depth)
{
    const int bytes = PixelBytes(depth);
    int placed = 0;
    while (placed < width) {
        const int left = width - placed;
        const int count = 1 + static_cast<int>(Roll(r, static_cast<uint32_t>(left < 128 ? left : 128)));
        if (Roll(r, 2)) {
            out.push_back(static_cast<uint8_t>(0x80 | (count - 1)));
            RandomBytes(r, out, static_cast<size_t>(bytes));
        } else {
            out.push_back(static_cast<uint8_t>(count - 1));
            RandomBytes(r, out, static_cast<size_t>(count) * static_cast<size_t>(bytes));
        }
        placed += count;
    }
}

struct Spec {
    Header h;
    bool rle = false;
    int bits = 24;
};

// A well-formed file for the reading tests.
Bytes MakeFile(std::mt19937 &r, Spec &s)
{
    Header &h = s.h;
    h = Header();
    const uint32_t kind = Roll(r, 4); // color-mapped, true color, gray, true color with a (skipped) map
    s.rle = Roll(r, 3) == 0;
    h.idLength = Roll(r, 3) == 0 ? static_cast<uint8_t>(Roll(r, 7)) : 0;
    h.width = (s.rle ? 1 : 0) + Roll(r, 40);
    if (s.rle && Roll(r, 4) == 0) h.width = 65 + Roll(r, 100); // packets above 64 pixels (count bit 6)
    h.height = Roll(r, 9);
    h.originX = Roll(r, 2) ? Roll(r, 50) : static_cast<uint32_t>(0x10000 - Roll(r, 50));
    h.originY = Roll(r, 2) ? Roll(r, 50) : static_cast<uint32_t>(0x10000 - Roll(r, 50));
    h.descriptor = static_cast<uint8_t>((Roll(r, 2) ? 0x20 : 0) | (Roll(r, 2) ? 0x10 : 0) | Roll(r, 16));
    Bytes colorMap;
    if (kind == 0) {
        static const uint8_t kEntries[] = {15, 16, 24, 32, 16, 24, 32};
        h.imageType = 1;
        h.depth = 8;
        h.colorMapType = 1;
        h.entryBits = kEntries[Roll(r, 7)];
        h.length = 1 + Roll(r, 256);
        h.first = Roll(r, 257 - h.length);
        RandomBytes(r, colorMap, static_cast<size_t>(h.length) * static_cast<size_t>(EntryBytes(h.entryBits)));
    } else if (kind == 2) {
        h.imageType = 3;
        h.depth = 8;
        if (Roll(r, 5) == 0) {
            h.colorMapType = 1;
            h.entryBits = Roll(r, 2) ? 24 : 32;
            h.length = 256;
            h.first = 0;
            RandomBytes(r, colorMap, static_cast<size_t>(h.length) * static_cast<size_t>(EntryBytes(h.entryBits)));
        }
    } else {
        static const uint8_t kDepths[] = {15, 16, 24, 32, 24, 32, 16};
        h.imageType = 2;
        h.depth = kDepths[Roll(r, 7)];
        if (kind == 3 && Roll(r, 2)) {
            h.colorMapType = 1;
            h.entryBits = static_cast<uint8_t>(Roll(r, 40));
            h.length = Roll(r, 12);
            h.first = Roll(r, 300);
            RandomBytes(r, colorMap, static_cast<size_t>(h.length) * static_cast<size_t>(EntryBytes(h.entryBits)));
        }
    }
    if (s.rle) h.imageType = static_cast<uint8_t>(h.imageType | 8);
    s.bits = h.depth;
    Bytes b;
    PutHeader(b, h);
    RandomBytes(r, b, h.idLength);
    b.insert(b.end(), colorMap.begin(), colorMap.end());
    const size_t dataStart = b.size();
    if (s.rle) {
        for (uint32_t y = 0; y < h.height; ++y) MakeRleRow(r, b, static_cast<int>(h.width), h.depth);
    } else {
        const size_t rowBytes = static_cast<size_t>(PixelBytes(h.depth)) * h.width;
        for (uint32_t y = 0; y < h.height; ++y) RandomBytes(r, b, rowBytes);
        // A file cut inside the pixels, after the first row (a failed first row decodes the allocator's garbage, and a
        // run-length packet's header is read into an uninitialised byte, so those are not made): the read stops there.
        if (h.height >= 2 && rowBytes > 0 && Roll(r, 6) == 0) {
            b.resize(dataStart + rowBytes * (1 + Roll(r, h.height - 1)) + Roll(r, static_cast<uint32_t>(rowBytes)));
            return b;
        }
    }
    RandomBytes(r, b, Roll(r, 10));
    return b;
}

struct Pixels {
    Bytes data;
    int pitch = 0;
    Pixels(int pitchBytes, int rows)
        : data(static_cast<size_t>(pitchBytes) * static_cast<size_t>(rows) + 64, 0xa5), pitch(pitchBytes)
    {
    }
};

// ---- about, is ----

DIFF_TEST(Gimex_TgaAbout, 0x882862)
{
    for (int round = 0; round < 3; ++round) {
        gimex::About *a = ctx.Original<gimex::AboutFn>(kAbout)();
        gimex::About *b = gimex::tga::NewAbout();
        ++ctx.cases;
        if ((a == nullptr) != (b == nullptr)) {
            ctx.Fail("About: original %s, ours %s", a ? "a block" : "null", b ? "a block" : "null");
        } else if (a != nullptr && !SameBytes(a, b, sizeof(gimex::About))) {
            ctx.Fail("About: the blocks differ");
        }
        if (a) ctx.Original<FreeFn>(kFreeMemory)(a);
        if (b) ctx.Original<FreeFn>(kFreeMemory)(b);
    }
}

DIFF_TEST(Gimex_TgaIs, 0x882db5)
{
    std::mt19937 r(0x882db5);
    static const uint32_t kBig[] = {0, 1, 2, 0x7ff, 0x800, 0x801, 0xfff, 0x1000, 0x1001, 0xff, 0x100, 0x101, 0xffff};
    for (int round = 0; round < 3000; ++round) {
        Header h;
        h.idLength = static_cast<uint8_t>(r());
        h.colorMapType = Roll(r, 4) == 0 ? static_cast<uint8_t>(Roll(r, 4)) : static_cast<uint8_t>(Roll(r, 2));
        static const uint8_t kTypes[] = {0, 1, 2, 3, 4, 8, 9, 10, 11, 12, 255, 2, 2, 10, 1, 3};
        h.imageType = kTypes[Roll(r, 16)];
        h.first = Roll(r, 4) == 0 ? kBig[Roll(r, 13)] : Roll(r, 0x100);
        h.length = Roll(r, 4) == 0 ? kBig[Roll(r, 13)] : Roll(r, 0x101);
        h.entryBits = Roll(r, 4) == 0 ? static_cast<uint8_t>(0x1f + Roll(r, 4)) : static_cast<uint8_t>(Roll(r, 0x21));
        h.originX = Roll(r, 4) == 0 ? kBig[Roll(r, 13)] : Roll(r, 0x1001);
        h.originY = Roll(r, 4) == 0 ? kBig[Roll(r, 13)] : Roll(r, 0x801);
        h.width = Roll(r, 4) == 0 ? kBig[Roll(r, 13)] : 1 + Roll(r, 0x1000);
        h.height = Roll(r, 4) == 0 ? kBig[Roll(r, 13)] : 1 + Roll(r, 0x800);
        static const uint8_t kDepths[] = {8, 15, 16, 24, 32, 8, 24, 32, 0, 1, 7, 9, 17, 31, 33, 255};
        h.depth = kDepths[Roll(r, 16)];
        h.descriptor = static_cast<uint8_t>(r());
        Bytes data;
        if (Roll(r, 12) != 0) {
            PutHeader(data, h);
            RandomBytes(r, data, Roll(r, 6));
        }
        Stream a(data), b(data);
        a.file.pos = Roll(r, 3);
        b.file.pos = a.file.pos;
        const int32_t ra = ctx.Original<gimex::IsFn>(kIs)(a.Get());
        const int32_t rb = gimex::tga::Probe(b.Get());
        ++ctx.cases;
        if (ra != rb || a.file.pos != b.file.pos || a.file.stray != 0 || b.file.stray != 0) {
            ctx.Fail("Is(%zu bytes): original %d (pos %u), ours %d (pos %u), strays %d / %d", data.size(), ra, a.file.pos, rb,
                     b.file.pos, a.file.stray, b.file.stray);
        }
    }
}

// ---- wopen, open, close ----

DIFF_TEST(Gimex_TgaOpenFree, 0x882e91)
{
    for (int round = 0; round < 6; ++round) {
        Stream a(Bytes{}), b(Bytes{});
        gimex::Instance *ia = reinterpret_cast<gimex::Instance *>(0x5e5e5e5e);
        gimex::Instance *ib = reinterpret_cast<gimex::Instance *>(0x5e5e5e5e);
        const int32_t ra = ctx.Original<gimex::OpenFn>(kOpen)(&ia, a.Get(), "x.tga", round);
        const int32_t rb = gimex::tga::Open(&ib, b.Get(), "x.tga", round);
        ++ctx.cases;
        if (ra != rb) {
            ctx.Fail("open: original %d, ours %d", ra, rb);
            continue;
        }
        gimex::Instance ca = *ia;
        gimex::Instance cb = *ib;
        const bool streamsOk = ca.stream == a.Get() && cb.stream == b.Get();
        ca.stream = nullptr;
        cb.stream = nullptr;
        if (!streamsOk || !SameBytes(&ca, &cb, sizeof(ca))) ctx.Fail("open: the instances differ");
        ctx.Original<gimex::CloseFn>(kFree)(ia);
        gimex::FreeInstance(ib);
    }
}

// ---- info ----

// A file for the info test: a random header, an identification field and a tail long enough for any colour map.
Bytes MakeInfoFile(std::mt19937 &r, Header &h, bool &cut)
{
    h = Header();
    cut = false;
    h.idLength = Roll(r, 3) == 0 ? static_cast<uint8_t>(Roll(r, 6)) : 0;
    h.colorMapType = Roll(r, 3) == 0 ? 0 : 1;
    if (Roll(r, 10) == 0) h.colorMapType = static_cast<uint8_t>(2 + Roll(r, 3));
    static const uint8_t kTypes[] = {0, 1, 2, 3, 9, 10, 11, 1, 3, 2, 4, 7, 8, 255};
    h.imageType = kTypes[Roll(r, 14)];
    static const uint8_t kEntries[] = {15, 16, 24, 32, 16, 24, 32, 0, 8, 12, 33};
    h.entryBits = kEntries[Roll(r, 11)];
    h.originX = Roll(r, 2) ? Roll(r, 300) : static_cast<uint32_t>(0x10000 - Roll(r, 300));
    h.originY = Roll(r, 2) ? Roll(r, 300) : static_cast<uint32_t>(0x10000 - Roll(r, 300));
    h.width = Roll(r, 5) == 0 ? Roll(r, 0x10000) : Roll(r, 300);
    h.height = Roll(r, 5) == 0 ? Roll(r, 0x10000) : Roll(r, 300);
    static const uint8_t kDepths[] = {8, 15, 16, 24, 32, 8, 8, 0, 1, 4, 7, 14, 17, 31, 33, 40, 255};
    h.depth = kDepths[Roll(r, 17)];
    h.descriptor = static_cast<uint8_t>(r());
    h.first = Roll(r, 257);
    // The colour map's length: no more than the entries the image type loads and the palette holds from `first`.
    uint32_t depth = h.depth == 0 ? 8 : h.depth;
    uint32_t count = depth < 0xf ? 1u << depth : 0;
    const uint32_t kind = h.imageType & 7u;
    uint32_t most = 256 - (h.first < 256 ? h.first : 256);
    if (kind == 3) {
        count = 0x100;
    } else if (kind != 1 && count > 0x100) {
        count = 0x100;
    }
    if (kind == 1 || depth >= 0xf) {
        h.length = Roll(r, most + 1);
    } else {
        h.length = Roll(r, (count < most ? count : most) + 1);
    }
    if (depth >= 0xf) h.length = Roll(r, 300);
    Bytes b;
    PutHeader(b, h);
    RandomBytes(r, b, h.idLength);
    RandomBytes(r, b, 1100);
    // A cut file: anywhere from the header's end when no table is read, else inside the identification field or at its end.
    const bool tableRead = depth < 0xf && h.colorMapType != 0 &&
                           (h.entryBits == 15 || h.entryBits == 16 || h.entryBits == 24 || h.entryBits == 32);
    if (Roll(r, 6) == 0) {
        const size_t minCut = 18;
        const size_t idEnd = 18 + h.idLength;
        size_t at = tableRead ? minCut + Roll(r, h.idLength + 1u) : minCut + Roll(r, static_cast<uint32_t>(b.size() - minCut));
        if (tableRead && at > idEnd) at = idEnd;
        b.resize(at);
        cut = true;
    }
    return b;
}

DIFF_TEST(Gimex_TgaInfo, 0x882958)
{
    std::mt19937 r(0x882958);
    for (int round = 0; round < 3000; ++round) {
        Header h;
        bool cut;
        const Bytes data = MakeInfoFile(r, h, cut);
        char label[96];
        std::snprintf(label, sizeof(label), "info %d type%d depth%d cm%d eb%d len%u first%u", round, h.imageType, h.depth,
                      h.colorMapType, h.entryBits, h.length, h.first);
        ctx.Mark(label);
        Side a(data), b(data);
        if (ctx.Original<gimex::OpenFn>(kOpen)(&a.instance, a.stream.Get(), "", 0) != 1) continue;
        if (gimex::tga::Open(&b.instance, b.stream.Get(), "", 0) != 1) continue;
        gimex::Info *ia = ctx.Original<gimex::InfoFn>(kInfo)(a.instance, 0);
        gimex::Info *ib = gimex::tga::GetInfo(b.instance, 0);
        ++ctx.cases;
        if ((ia == nullptr) != (ib == nullptr)) {
            ctx.Fail("round %d (type %d depth %d cm %d eb %d len %u first %u, %zu bytes): original %s, ours %s", round,
                     h.imageType, h.depth, h.colorMapType, h.entryBits, h.length, h.first, data.size(), ia ? "an info" : "null",
                     ib ? "an info" : "null");
        } else if (ia != nullptr && !SameBytes(ia, ib, gimex::kWriteInfoSize)) {
            const uint8_t *pa = reinterpret_cast<const uint8_t *>(ia);
            const uint8_t *pb = reinterpret_cast<const uint8_t *>(ib);
            size_t at = 0;
            while (pa[at] == pb[at]) ++at;
            ctx.Fail("round %d (type %d depth %d cm %d eb %d len %u first %u desc %02x): infos differ at 0x%zx (original %02x, "
                     "ours %02x)",
                     round, h.imageType, h.depth, h.colorMapType, h.entryBits, h.length, h.first, h.descriptor, at, pa[at],
                     pb[at]);
        }
        if (a.stream.file.pos != b.stream.file.pos || a.stream.file.stray != 0 || b.stream.file.stray != 0) {
            ctx.Fail("round %d: stream position %u / %u, strays %d / %d", round, a.stream.file.pos, b.stream.file.pos,
                     a.stream.file.stray, b.stream.file.stray);
        }
        if (ia) ctx.Original<FreeFn>(kFreeMemory)(ia);
        if (ib) ctx.Original<FreeFn>(kFreeMemory)(ib);
        ctx.Original<gimex::CloseFn>(kFree)(a.instance);
        gimex::FreeInstance(b.instance);
    }
}

// ---- read ----
DIFF_TEST(Gimex_TgaRead, 0x882ec9)
{
    // Reaches the row decoder 0x8823cf and the row reverser 0x882380.
    std::mt19937 r(0x882ec9);
    for (int round = 0; round < 2500; ++round) {
        Spec spec;
        const Bytes data = MakeFile(r, spec);
        char label[96];
        std::snprintf(label, sizeof(label), "read %d type%d depth%d %ux%u desc%02x", round, spec.h.imageType, spec.h.depth,
                      spec.h.width, spec.h.height, spec.h.descriptor);
        ctx.Mark(label);
        Side a(data), b(data);
        if (ctx.Original<gimex::OpenFn>(kOpen)(&a.instance, a.stream.Get(), "", 0) != 1) continue;
        if (gimex::tga::Open(&b.instance, b.stream.Get(), "", 0) != 1) continue;
        gimex::Info *ia = ctx.Original<gimex::InfoFn>(kInfo)(a.instance, 0);
        gimex::Info *ib = gimex::tga::GetInfo(b.instance, 0);
        if (ia == nullptr || ib == nullptr) {
            ++ctx.cases;
            ctx.Fail("round %d: info answered null (original %p, ours %p)", round, static_cast<void *>(ia), static_cast<void *>(ib));
            if (ia) ctx.Original<FreeFn>(kFreeMemory)(ia);
            if (ib) ctx.Original<FreeFn>(kFreeMemory)(ib);
            continue;
        }
        // The caller may set alpha bits on the info it reads into: a 15 / 16-bit pixel's top bit is then the alpha.
        if (spec.h.depth == 15 || spec.h.depth == 16) {
            if (Roll(r, 3) == 0) ia->alphaBits = ib->alphaBits = 1;
        }
        const bool indexed = ia->bitsPerPixel == 8;
        const int minPitch = ia->width * (indexed ? 1 : 4);
        const int pitch = minPitch + static_cast<int>(Roll(r, 3) == 0 ? Roll(r, 9) : 0);
        Pixels pa(pitch, ia->height);
        Pixels pb(pitch, ib->height);
        const int32_t ra = ctx.Original<gimex::ReadFn>(kRead)(a.instance, ia, pa.data.data(), pitch);
        const int32_t rb = gimex::tga::ReadFrame(b.instance, ib, pb.data.data(), pitch);
        ++ctx.cases;
        if (ra != rb) {
            ctx.Fail("round %d (type %d depth %d %ux%u desc %02x): original %d, ours %d", round, spec.h.imageType, spec.h.depth,
                     spec.h.width, spec.h.height, spec.h.descriptor, ra, rb);
        } else if (!SameBytes(pa.data.data(), pb.data.data(), pa.data.size())) {
            size_t at = 0;
            while (pa.data[at] == pb.data[at]) ++at;
            ctx.Fail("round %d (type %d depth %d %ux%u desc %02x): pixels differ at %zu (original %02x, ours %02x)", round,
                     spec.h.imageType, spec.h.depth, spec.h.width, spec.h.height, spec.h.descriptor, at, pa.data[at],
                     pb.data[at]);
        }
        if (a.stream.file.pos != b.stream.file.pos || a.stream.file.stray != 0 || b.stream.file.stray != 0) {
            ctx.Fail("round %d: stream position %u / %u, strays %d / %d", round, a.stream.file.pos, b.stream.file.pos,
                     a.stream.file.stray, b.stream.file.stray);
        }
        ctx.Original<FreeFn>(kFreeMemory)(ia);
        ctx.Original<FreeFn>(kFreeMemory)(ib);
        ctx.Original<gimex::CloseFn>(kFree)(a.instance);
        gimex::FreeInstance(b.instance);
    }
}

// ---- write ----

DIFF_TEST(Gimex_TgaWrite, 0x8833ac)
{
    // Reaches the row encoder 0x88302f and the run counter 0x882fe3.
    std::mt19937 r(0x8833ac);
    int rleRan = 0;
    for (int round = 0; round < 4000; ++round) {
        gimex::Info info;
        std::memset(&info, 0, sizeof(info));
        info.signature = 0x2e544741;
        info.size = gimex::kWriteInfoSize;
        info.width = static_cast<int32_t>(Roll(r, 41));
        info.height = static_cast<int32_t>(Roll(r, 9));
        const bool wide = Roll(r, 12) == 0; // rows past the 128-pixel packet limit
        if (wide) {
            info.width = 125 + static_cast<int32_t>(Roll(r, 180));
            info.height = 1 + static_cast<int32_t>(Roll(r, 2));
        }
        info.jpegFlag = Roll(r, 3) == 0 ? 0 : Roll(r, 3);
        info.originX = static_cast<int32_t>(Roll(r, 200)) - 100;
        info.originY = static_cast<int32_t>(Roll(r, 200)) - 100;
        const bool indexed = Roll(r, 2);
        info.bitsPerPixel = indexed ? 8 : 32;
        info.sourceBits = indexed ? static_cast<int32_t>(Roll(r, 14)) - 1 : 32;
        if (!indexed && Roll(r, 8) == 0) info.sourceBits = 9 + static_cast<int32_t>(Roll(r, 20));
        info.unknown20 = Roll(r, 3) == 0 ? Roll(r, 256) : 0;
        info.paletteCount = Roll(r, 4) == 0 ? Roll(r, 257) : 1u << Roll(r, 9);
        if (info.paletteCount > 256) info.paletteCount = 256;
        if (indexed && info.paletteCount == 0) info.paletteCount = 1 + Roll(r, 256);
        // random, gray with full alpha, gray with a clear alpha, gray with one entry's blue or red off, one entry's alpha off
        const uint32_t paletteKind = Roll(r, 6);
        for (int i = 0; i < 256; ++i) {
            for (int c = 0; c < 4; ++c) info.palette[i][c] = static_cast<uint8_t>(r());
            if (paletteKind != 0) {
                info.palette[i][1] = info.palette[i][2] = info.palette[i][0];
                if (paletteKind != 2) info.palette[i][3] = 0xff;
            }
        }
        if (paletteKind >= 3 && info.paletteCount > 0) {
            uint8_t *odd = info.palette[Roll(r, info.paletteCount)];
            if (paletteKind == 3) odd[0] = static_cast<uint8_t>(odd[0] + 1);     // blue (entry 0) differs
            else if (paletteKind == 4) odd[2] = static_cast<uint8_t>(odd[2] + 1); // red differs
            else odd[3] = 0xfe;
        }
        info.alphaBits = Roll(r, 3) == 0 ? 0 : Roll(r, 9);
        info.redBits = Roll(r, 9);
        info.greenBits = Roll(r, 9);
        info.blueBits = Roll(r, 9);
        const uint32_t layout = Roll(r, 4);
        if (layout == 0) {
            info.redBits = info.greenBits = info.blueBits = 8;
            if (Roll(r, 2)) info.alphaBits = 8;
        } else if (layout == 1) {
            info.redBits = info.greenBits = info.blueBits = 5;
            info.alphaBits = Roll(r, 3);
        }
        const bool pixelsAreIndices = info.sourceBits < 9;
        const int pitch = info.width * 4 + static_cast<int>(Roll(r, 3) == 0 ? Roll(r, 12) : 0);
        Bytes pixels(static_cast<size_t>(pitch) * static_cast<size_t>(info.height) + 16);
        const uint32_t pattern = wide ? Roll(r, 6) : Roll(r, 4); // 4: one value, 5: long runs
        for (size_t i = 0; i < pixels.size(); ++i) {
            // Random, run-heavy or few distinct values, so the run-length coder meets runs of every length.
            uint8_t v = pattern == 4 ? static_cast<uint8_t>(0x33) : pattern == 5 ? static_cast<uint8_t>(i / 700) : pattern == 0 ? static_cast<uint8_t>(r()) : pattern == 1 ? static_cast<uint8_t>(Roll(r, 3))
                        : pattern == 2 ? static_cast<uint8_t>((i / (1 + Roll(r, 6))) & 1) : static_cast<uint8_t>(i / 7);
            if (pixelsAreIndices) v = static_cast<uint8_t>(v % info.paletteCount);
            pixels[i] = v;
        }
        char label[96];
        std::snprintf(label, sizeof(label), "write %d src%d %dx%d flag%u pal%u", round, info.sourceBits, info.width, info.height,
                      info.jpegFlag, info.paletteCount);
        ctx.Mark(label);
        Side a(Bytes{}), b(Bytes{});
        if (Roll(r, 6) == 0) {
            const int64_t limit = static_cast<int64_t>(Roll(r, 300));
            a.stream.file.writeLimit = limit;
            b.stream.file.writeLimit = limit;
        }
        if (ctx.Original<gimex::WOpenFn>(kWOpen)(&a.instance, a.stream.Get(), "", 0) != 1) continue;
        if (gimex::OpenWriter(&b.instance, b.stream.Get(), "", 0) != 1) continue;
        gimex::Info ia = info;
        gimex::Info ib = info;
        const int32_t ra = ctx.Original<gimex::WriteFn>(kWrite)(a.instance, &ia, pixels.data(), pitch);
        const int32_t rb = gimex::tga::WriteFrame(b.instance, &ib, pixels.data(), pitch);
        ++ctx.cases;
        if (info.jpegFlag != 0 && a.stream.file.written.size() > 2 && (a.stream.file.written[2] & 8) != 0) ++rleRan;
        if (ra != rb) {
            ctx.Fail("round %d (src %d %dx%d flag %u alpha %u rgb %u %u %u pal %u): original %d, ours %d", round,
                     info.sourceBits, info.width, info.height, info.jpegFlag, info.alphaBits, info.redBits, info.greenBits,
                     info.blueBits, info.paletteCount, ra, rb);
        } else if (a.stream.file.written != b.stream.file.written) {
            const Bytes &wa = a.stream.file.written;
            const Bytes &wb = b.stream.file.written;
            size_t at = 0;
            while (at < wa.size() && at < wb.size() && wa[at] == wb[at]) ++at;
            ctx.Fail("round %d (src %d %dx%d flag %u alpha %u rgb %u %u %u pal %u): written bytes differ at %zu (original %zu "
                     "bytes, ours %zu bytes)",
                     round, info.sourceBits, info.width, info.height, info.jpegFlag, info.alphaBits, info.redBits,
                     info.greenBits, info.blueBits, info.paletteCount, at, wa.size(), wb.size());
        }
        if (!SameBytes(&ia, &ib, sizeof(ia)) || !SameBytes(&ia, &info, sizeof(ia))) ctx.Fail("round %d: write changed its info", round);
        if (a.stream.file.stray != 0 || b.stream.file.stray != 0) ctx.Fail("round %d: stray stream calls", round);
        ctx.Original<gimex::CloseFn>(kFree)(a.instance);
        gimex::FreeInstance(b.instance);
    }
    if (rleRan < 200) ctx.Fail("only %d run-length coded writes ran", rleRan);
}

} // namespace
