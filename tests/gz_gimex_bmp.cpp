// Differential tests for src/gz/GimexBmp.size.cpp: Gimex's BMP codec (row 5 of the codec table) and the instance helpers the
// rows 2..6 share (wopen 0x87f3e4, free 0x87fe5d).
//
// The codec runs against a fake cIGZFile (slots 7 Length, 12 SeekToPosition, 15 ReadWithCount, 17 WriteWithCount; any
// other slot is a stray call and fails the case) holding a synthetic file: one fake per side, same bytes. The stream
// helpers (0x87da8f and the others) and the allocator (0x87da65) are each side's own (ours: lane MP). Compared: results,
// the info block (all 0x2a70 bytes), the decoded pixels (the whole destination, sentinel-filled), the bytes written, and
// the stream's position.
//
// Inputs kept out because the original reads or writes garbage on them (ours does something defined there):
// - an info header larger than 0x6c bytes (the original reads it into a 0x7a-byte stack buffer);
// - a file cut inside the palette or the pixel data (the palette table and the row buffer are uninitialised);
// - a one-byte stream starting with 'B' (is: the signature's second byte is stack garbage);
// - a colour mask of zero where the decoder divides by its channel's range, and 16-bit masks above bit 15 (the
//   original reads a whole word per 16-bit pixel);
// - run-length data that runs past the row, a delta code, or an end of file inside a row;
// - a write of a palette of more than 256 entries (the original's 1 KB table).
//
// The helpers (channel bits, channel shift, row decoder, run length, row encoder) are reached through the entry points
// below; their RVAs are named in the tests that reach them.
#include <windows.h>

#include "difftest.h"
#include "game/game.h"
#include "game/original.h"
#include "gz/GimexFormats.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

namespace {

constexpr uint32_t kAbout = 0x881267;
constexpr uint32_t kIs = 0x881730;
constexpr uint32_t kOpen = 0x88176a;
constexpr uint32_t kInfo = 0x881316;
constexpr uint32_t kRead = 0x8817a2;
constexpr uint32_t kWrite = 0x881d40;
constexpr uint32_t kWOpen = 0x87f3e4;
constexpr uint32_t kFree = 0x87fe5d;
constexpr uint32_t kFreeMemory = 0x87da72;

using Bytes = std::vector<uint8_t>;
using FreeFn = int32_t(__stdcall *)(void *);

// ---- the fake stream ----

struct FakeFile {
    void **vtable;
    Bytes data;
    uint32_t pos = 0;
    Bytes written;
    int64_t writeLimit = -1; // the bytes it accepts; a write that would pass it takes nothing (count 0)
    int stray = 0;
};

constexpr int kSlots = 64;
void *gVTable[kSlots];

uint32_t __fastcall FLength(FakeFile *f, void *)
{
    return static_cast<uint32_t>(f->data.size());
}
uint32_t __fastcall FSeek(FakeFile *f, void *, uint32_t position)
{
    if (position > f->data.size()) return 0xffffffffu;
    f->pos = position;
    return position;
}
uint8_t __fastcall FRead(FakeFile *f, void *, void *dst, uint32_t *length)
{
    const uint32_t left = static_cast<uint32_t>(f->data.size()) - f->pos;
    const uint32_t n = *length < left ? *length : left;
    if (n != 0) std::memcpy(dst, f->data.data() + f->pos, n);
    f->pos += n;
    *length = n;
    return n != 0;
}
uint8_t __fastcall FWrite(FakeFile *f, void *, const void *src, uint32_t *length)
{
    if (f->writeLimit >= 0 && static_cast<int64_t>(f->written.size()) + *length > f->writeLimit) {
        *length = 0;
        return 0;
    }
    const uint8_t *bytes = static_cast<const uint8_t *>(src);
    f->written.insert(f->written.end(), bytes, bytes + *length);
    return 1;
}
uint32_t __fastcall FStray(FakeFile *f, void *)
{
    ++f->stray;
    return 0;
}

void InitVTable()
{
    for (void *&slot : gVTable) slot = reinterpret_cast<void *>(&FStray);
    gVTable[7] = reinterpret_cast<void *>(&FLength);
    gVTable[12] = reinterpret_cast<void *>(&FSeek);
    gVTable[15] = reinterpret_cast<void *>(&FRead);
    gVTable[17] = reinterpret_cast<void *>(&FWrite);
}

struct Stream {
    FakeFile file;
    explicit Stream(const Bytes &data)
    {
        InitVTable();
        file.vtable = gVTable;
        file.data = data;
    }
    ifaces::cIGZFile *Get() { return reinterpret_cast<ifaces::cIGZFile *>(&file); }
};

// ---- the file builder ----

void Put16(Bytes &b, uint32_t v)
{
    b.push_back(static_cast<uint8_t>(v));
    b.push_back(static_cast<uint8_t>(v >> 8));
}
void Put32(Bytes &b, uint32_t v)
{
    Put16(b, v);
    Put16(b, v >> 16);
}
void Set32(Bytes &b, size_t at, uint32_t v)
{
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<uint8_t>(v >> (8 * i));
}

uint32_t Roll(std::mt19937 &r, uint32_t n)
{
    return r() % n;
}

constexpr int kVariantInfo = 0;
constexpr int kVariantCore = 1;
constexpr int kVariantV4 = 2;
constexpr int kVariantBitfields = 3;

struct Spec {
    int variant = 0;
    int bits = 8;        // the file's bit count
    int storedBits = 8;  // what a pixel row holds (0 -> 8, 15 -> 16)
    int width = 1;
    int height = 1;
    bool topDown = false;
    bool rle = false;
    uint32_t compression = 0;
    uint32_t colorsUsed = 0;
    int paletteEntries = 0; // in the file
    bool zeroOffset = false;
    uint32_t masks[4] = {0, 0, 0, 0}; // R, G, B, A
    uint32_t headerSize = 40;
    uint32_t ppm = 0;
    bool readOverflows = false; // the original's reader would write past its row buffer (ORIGINAL-BUG): not run on it
};

int StoredOf(int bits)
{
    return bits == 15 ? 16 : bits == 0 ? 8 : bits;
}

// Channel masks of the given widths, placed from the low bit in random order with random gaps, inside `total` bits.
void MakeMasks(std::mt19937 &r, int total, bool alpha, uint32_t out[4])
{
    int widths[4];
    for (;;) {
        const int cap = total == 16 ? 6 : 10;
        int sum = 0;
        for (int i = 0; i < 3; ++i) {
            widths[i] = 1 + static_cast<int>(Roll(r, cap));
            sum += widths[i];
        }
        widths[3] = alpha ? 1 + static_cast<int>(Roll(r, 8)) : 0;
        sum += widths[3];
        if (sum <= total) break;
    }
    int order[4] = {0, 1, 2, 3};
    for (int i = 3; i > 0; --i) std::swap(order[i], order[Roll(r, i + 1)]);
    int slack = total;
    for (int i = 0; i < 4; ++i) slack -= widths[i];
    const int firstGap = static_cast<int>(Roll(r, static_cast<uint32_t>(slack + 1)));
    int at = firstGap;
    slack -= firstGap;
    for (int i = 0; i < 4; ++i) {
        const int c = order[i];
        out[c] = widths[c] == 0 ? 0 : static_cast<uint32_t>(((1ull << widths[c]) - 1) << at);
        at += widths[c];
        if (slack > 0 && Roll(r, 3) == 0) {
            const int gap = static_cast<int>(Roll(r, static_cast<uint32_t>(slack + 1)));
            at += gap;
            slack -= gap;
        }
    }
}

void RandomBytes(std::mt19937 &r, Bytes &b, size_t n)
{
    for (size_t i = 0; i < n; ++i) b.push_back(static_cast<uint8_t>(r()));
}

// One row of run-length data for `width` pixels of `bits` (1, 4, 8, 24) bits; `consumed` follows the decoder's count of
// the row's bytes so that the row is padded to a word the way the decoder skips.
// `overflows` is set when the original's read would write past its row buffer (ORIGINAL-BUG): the decoder leaves its
// buffer cursor after the data of a last absolute run, and the row's padding is read there; `bufferBytes` is the buffer's size.
void MakeRleRow(std::mt19937 &r, Bytes &out, int width, int bits, bool first, int bufferBytes, bool &overflows)
{
    int consumed = 0;
    int cursor = 0; // the original's moving buffer offset after the last token
    while (Roll(r, 3) == 0 && !(first && Roll(r, 2))) {
        out.push_back(0);
        out.push_back(static_cast<uint8_t>(Roll(r, 3))); // end of line, end of bitmap or a jump: all three skipped alike
        consumed += 2;
        cursor = 0;
        if (Roll(r, 2)) break;
    }
    int placed = 0;
    const bool cutShort = Roll(r, 12) == 0 && width > 3;
    const int goal = cutShort ? 1 + static_cast<int>(Roll(r, static_cast<uint32_t>(width - 1))) : width;
    while (placed < goal) {
        const int left = width - placed;
        // An absolute run needs 3 pixels or more; the decoder's run of length 0..2 is an escape.
        if (left >= 3 && Roll(r, 2) && !(cutShort && goal < width)) {
            const int len = 3 + static_cast<int>(Roll(r, static_cast<uint32_t>(left - 2)));
            const int bytes = ((len * bits + 15) >> 3) & ~1;
            out.push_back(0);
            out.push_back(static_cast<uint8_t>(len));
            RandomBytes(r, out, static_cast<size_t>(bytes));
            consumed += 2 + bytes;
            placed += len;
            cursor = bits == 1 ? len / 8 : bits == 4 ? len / 2 : bits == 8 ? len : len * 3;
        } else {
            cursor = 0;
            const int len = 1 + static_cast<int>(Roll(r, static_cast<uint32_t>(left)));
            out.push_back(static_cast<uint8_t>(len));
            out.push_back(static_cast<uint8_t>(r()));
            consumed += 2;
            if (bits == 24) {
                out.push_back(static_cast<uint8_t>(r()));
                out.push_back(static_cast<uint8_t>(r()));
                consumed += 2;
            }
            placed += len;
        }
    }
    if (cutShort && placed < width) {
        out.push_back(0);
        out.push_back(0);
        consumed += 2;
        cursor = 0;
    }
    int pad = 0;
    while ((consumed & 3) != 0) {
        out.push_back(static_cast<uint8_t>(r()));
        ++consumed;
        ++pad;
    }
    if (pad != 0 && cursor + pad > bufferBytes) overflows = true;
}

void MakeAlphaPalette(std::mt19937 &r, uint8_t *table, int entries, int stride)
{
    const uint32_t mode = Roll(r, 3); // all clear, all opaque, mixed
    for (int i = 0; i < entries; ++i) {
        table[i * stride] = static_cast<uint8_t>(r());
        table[i * stride + 1] = static_cast<uint8_t>(r());
        table[i * stride + 2] = static_cast<uint8_t>(r());
        if (stride == 4) {
            table[i * stride + 3] = mode == 0 ? 0 : mode == 1 ? 0xff : static_cast<uint8_t>(r());
        }
    }
}

// A well-formed file for the reading tests.
Bytes MakeFile(std::mt19937 &r, Spec &s, bool allowWeirdHeaders)
{
    s.variant = static_cast<int>(Roll(r, 4));
    static const int kPlain[] = {1, 4, 8, 24, 8, 4, 1, 24, 16, 32, 15, 2, 5, 12, 0};
    static const int kCore[] = {1, 4, 8, 24, 8, 24, 16, 32};
    static const int kWide[] = {16, 32, 15, 24, 8, 16, 32};
    if (s.variant == kVariantCore) {
        s.bits = kCore[Roll(r, 8)];
    } else if (s.variant == kVariantInfo) {
        s.bits = kPlain[Roll(r, 15)];
    } else {
        s.bits = kWide[Roll(r, 7)];
        if (Roll(r, 6) == 0) s.bits = kPlain[Roll(r, 15)];
    }
    s.storedBits = StoredOf(s.bits);
    s.width = static_cast<int>(Roll(r, 41));
    s.height = static_cast<int>(Roll(r, 9));
    s.topDown = Roll(r, 4) == 0;
    // Run-length coding: 1, 4, 8 and 24 bit pixels; widths from 1.
    s.rle = s.variant != kVariantCore && s.width > 0 && (s.bits == 8 || s.bits == 4 || s.bits == 1 || s.bits == 24) &&
            Roll(r, 4) == 0;
    s.compression = 0;
    if (s.rle) s.compression = Roll(r, 2) ? 1 : 2;
    else if (allowWeirdHeaders && Roll(r, 8) == 0) s.compression = 4 + Roll(r, 3);
    if (s.variant == kVariantBitfields) s.compression = 3;
    s.rle = s.rle && s.variant != kVariantBitfields;
    if (s.variant == kVariantBitfields) s.rle = false;
    s.headerSize = s.variant == kVariantCore ? 12 : s.variant == kVariantV4 ? 108 : 40;
    s.ppm = Roll(r, 3) == 0 ? 0 : Roll(r, 20000);
    // The masks.
    if (s.variant >= kVariantV4 && (s.bits == 16 || s.bits == 15 || s.bits == 32)) {
        MakeMasks(r, s.bits == 32 ? 32 : 16, s.bits == 32 && s.variant == kVariantV4 && Roll(r, 2), s.masks);
    }
    // The palette.
    const int effective = s.bits == 0 ? 8 : s.bits;
    s.paletteEntries = 0;
    s.colorsUsed = 0;
    if (effective <= 8) {
        const int colors = 1 << effective;
        s.paletteEntries = colors;
        if (s.variant != kVariantCore && Roll(r, 3) == 0) {
            s.colorsUsed = 1 + Roll(r, static_cast<uint32_t>(colors < 256 ? colors : 300));
            s.paletteEntries = s.colorsUsed > 256 ? 256 : static_cast<int>(s.colorsUsed);
        }
    }
    const bool zeroOk = s.variant == kVariantCore || s.variant == kVariantInfo ||
                        (s.variant == kVariantV4 && effective > 8) || (s.variant == kVariantBitfields && effective > 8);
    s.zeroOffset = zeroOk && Roll(r, 4) == 0;
    const bool palette = effective <= 8;

    Bytes b;
    Put16(b, 0x4d42);
    Put32(b, 0); // file size, patched
    Put32(b, 0);
    Put32(b, 0); // data offset, patched
    Put32(b, s.headerSize);
    if (s.variant == kVariantCore) {
        Put16(b, static_cast<uint32_t>(s.width));
        Put16(b, static_cast<uint32_t>(s.height));
        Put16(b, 1);
        Put16(b, static_cast<uint32_t>(s.bits));
    } else {
        Put32(b, static_cast<uint32_t>(s.width));
        Put32(b, static_cast<uint32_t>(s.topDown ? -s.height : s.height));
        Put16(b, 1);
        Put16(b, static_cast<uint32_t>(s.bits));
        Put32(b, s.compression);
        Put32(b, 0);
        Put32(b, s.ppm);
        Put32(b, s.ppm);
        Put32(b, s.colorsUsed);
        Put32(b, 0);
        if (s.variant == kVariantBitfields) {
            Put32(b, s.masks[0]);
            Put32(b, s.masks[1]);
            Put32(b, s.masks[2]);
        } else if (s.variant == kVariantV4) {
            Put32(b, s.masks[0]);
            Put32(b, s.masks[1]);
            Put32(b, s.masks[2]);
            Put32(b, s.masks[3]);
            Put32(b, 1);
            while (b.size() < 14 + 108) b.push_back(0);
        }
    }
    if (palette) {
        Bytes table(static_cast<size_t>(s.paletteEntries) * 4);
        MakeAlphaPalette(r, table.data(), s.paletteEntries, s.variant == kVariantCore ? 3 : 4);
        b.insert(b.end(), table.begin(), table.begin() + static_cast<long>(s.paletteEntries) * (s.variant == kVariantCore ? 3 : 4));
    }
    Set32(b, 10, s.zeroOffset ? 0 : static_cast<uint32_t>(b.size()));
    // The pixels.
    if (s.rle) {
        const int bufferBytes = (s.storedBits * s.width + 31) >> 3 & ~3;
        for (int y = 0; y < s.height; ++y) MakeRleRow(r, b, s.width, s.bits == 0 ? 8 : s.bits, y == 0, bufferBytes, s.readOverflows);
    } else {
        const size_t rowBytes = static_cast<size_t>((s.storedBits * s.width + 31) >> 3 & ~3);
        for (int y = 0; y < s.height; ++y) RandomBytes(r, b, rowBytes);
    }
    if (Roll(r, 2)) RandomBytes(r, b, Roll(r, 20));
    Set32(b, 2, static_cast<uint32_t>(b.size()));
    return b;
}

// The pixel destination: sentinel-filled, `pitch` bytes per row.
// It ends at an inaccessible page, so a write past the end faults (and names the writer) instead of corrupting the heap.
struct Guarded {
    uint8_t *base = nullptr;
    uint8_t *start = nullptr;
    size_t length = 0;
    explicit Guarded(size_t n) : length(n)
    {
        const size_t pages = (n + 4095) / 4096 + 1;
        base = static_cast<uint8_t *>(VirtualAlloc(nullptr, pages * 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        DWORD old;
        VirtualProtect(base + (pages - 1) * 4096, 4096, PAGE_NOACCESS, &old);
        start = base + (pages - 1) * 4096 - n;
        std::memset(start, 0xa5, n);
    }
    Guarded(const Guarded &) = delete;
    Guarded &operator=(const Guarded &) = delete;
    ~Guarded() { VirtualFree(base, 0, MEM_RELEASE); }
    uint8_t *data() { return start; }
    size_t size() const { return length; }
    uint8_t &operator[](size_t i) { return start[i]; }
};

struct Pixels {
    Guarded data;
    int pitch = 0;
    Pixels(int pitchBytes, int rows) : data(static_cast<size_t>(pitchBytes) * static_cast<size_t>(rows)), pitch(pitchBytes) {}
};

bool SameBytes(const void *a, const void *b, size_t n)
{
    return std::memcmp(a, b, n) == 0;
}

const gimex::Info *AsInfo(const void *p)
{
    return static_cast<const gimex::Info *>(p);
}

// Opens through the original's and our entry points; false when the case should stop.
struct Side {
    Stream stream;
    gimex::Instance *instance = nullptr;
    explicit Side(const Bytes &data) : stream(data) {}
};

// ---- about, is ----

DIFF_TEST(Gimex_BmpAbout, 0x881267)
{
    for (int round = 0; round < 3; ++round) {
        gimex::About *a = ctx.Original<gimex::AboutFn>(kAbout)();
        gimex::About *b = gimex::bmp::NewAbout();
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

DIFF_TEST(Gimex_BmpIs, 0x881730)
{
    std::mt19937 r(881730);
    for (int round = 0; round < 400; ++round) {
        Bytes data;
        const uint32_t kind = Roll(r, 6);
        const size_t length = kind == 0 ? 0 : kind == 1 ? 2 + Roll(r, 3) : Roll(r, 24);
        for (size_t i = 0; i < length; ++i) data.push_back(static_cast<uint8_t>(r()));
        if (length >= 2 && Roll(r, 3) != 0) {
            data[0] = 'B';
            data[1] = Roll(r, 4) ? 'M' : static_cast<uint8_t>('M' + 1 + Roll(r, 3));
        }
        if (length == 1 && data[0] == 'B') data[0] = 'C';
        Stream a(data), b(data);
        a.file.pos = Roll(r, 3);
        b.file.pos = a.file.pos;
        const int32_t ra = ctx.Original<gimex::IsFn>(kIs)(a.Get());
        const int32_t rb = gimex::bmp::Probe(b.Get());
        ++ctx.cases;
        if (ra != rb || a.file.pos != b.file.pos || a.file.stray != 0 || b.file.stray != 0) {
            ctx.Fail("Is(%zu bytes): original %d (pos %u), ours %d (pos %u), strays %d / %d", length, ra, a.file.pos, rb,
                     b.file.pos, a.file.stray, b.file.stray);
        }
    }
}

// ---- wopen 0x87f3e4, open 0x88176a, close 0x87fe5d ----
// The close entry 0x87fe5d is the codecs' close and wclose for rows 2..6.

DIFF_TEST(Gimex_BmpOpenWriterFree, 0x87f3e4)
{
    for (int round = 0; round < 20; ++round) {
        Stream a(Bytes{}), b(Bytes{});
        gimex::Instance *ia = reinterpret_cast<gimex::Instance *>(0x5e5e5e5e);
        gimex::Instance *ib = reinterpret_cast<gimex::Instance *>(0x5e5e5e5e);
        const bool writer = round % 2 == 0;
        const int32_t ra = writer ? ctx.Original<gimex::WOpenFn>(kWOpen)(&ia, a.Get(), "x.bmp", round)
                                  : ctx.Original<gimex::OpenFn>(kOpen)(&ia, a.Get(), "x.bmp", round);
        const int32_t rb = writer ? gimex::OpenWriter(&ib, b.Get(), "x.bmp", round)
                                  : gimex::bmp::Open(&ib, b.Get(), "x.bmp", round);
        ++ctx.cases;
        if (ra != rb) {
            ctx.Fail("%s: original %d, ours %d", writer ? "wopen" : "open", ra, rb);
            continue;
        }
        gimex::Instance ca = *ia;
        gimex::Instance cb = *ib;
        const bool streamsOk = ca.stream == a.Get() && cb.stream == b.Get();
        ca.stream = nullptr;
        cb.stream = nullptr;
        if (!streamsOk || !SameBytes(&ca, &cb, sizeof(ca))) {
            ctx.Fail("%s: the instances differ (streams %s)", writer ? "wopen" : "open", streamsOk ? "ok" : "wrong");
        }
        const int32_t fa = ctx.Original<gimex::CloseFn>(kFree)(ia);
        const int32_t fb = gimex::FreeInstance(ib);
        ++ctx.cases;
        if (fa != fb) ctx.Fail("close: original %d, ours %d", fa, fb);
    }
    ++ctx.cases;
    const int32_t na = ctx.Original<gimex::CloseFn>(kFree)(nullptr);
    const int32_t nb = gimex::FreeInstance(nullptr);
    if (na != nb) ctx.Fail("close(null): original %d, ours %d", na, nb);
}

// ---- info ----
// A file for the info test: the reading tests' files, sometimes with another header size or cut inside the header.
Bytes MakeInfoFile(std::mt19937 &r, Spec &s)
{
    Bytes b = MakeFile(r, s, true);
    if (Roll(r, 5) == 0 && s.variant != kVariantCore) {
        // Another info header size: the 40-byte block cut or padded; the palette and pixels follow it.
        static const uint32_t kSizes[] = {0, 4, 8, 16, 24, 36, 44, 52, 64, 100};
        const uint32_t size = kSizes[Roll(r, 10)];
        if (size != 40 && size != 108 && s.variant == kVariantInfo) {
            Bytes c(b.begin(), b.begin() + 14);
            Bytes header(b.begin() + 14, b.begin() + 14 + 40);
            header.resize(size);
            if (size >= 4) Set32(header, 0, size);
            c.insert(c.end(), header.begin(), header.end());
            c.insert(c.end(), b.begin() + 14 + 40, b.end());
            // Layout bytes: the header block is `size` bytes now; the length of a 0-size header block cannot hold a size word.
            if (size < 4) {
                c.resize(14);
                Put32(c, size);
            }
            b = c;
        }
    }
    if (Roll(r, 8) == 0) {
        // Arbitrary masks and bit counts: only the info reads them.
        if (b.size() >= 14 + 108 && s.variant == kVariantV4) {
            for (size_t at = 0x36; at < 0x46; at += 4) Set32(b, at, static_cast<uint32_t>(r()) & (Roll(r, 2) ? 0xffffffffu : 0xff00ff));
        }
    }
    if (Roll(r, 6) == 0) {
        // Cut inside the headers (not inside the palette or pixels).
        const size_t headerEnd = static_cast<size_t>(14 + (s.variant == kVariantCore ? 12 : s.variant == kVariantV4 ? 108 : 40) +
                                                    (s.variant == kVariantBitfields ? 12 : 0));
        const size_t cut = Roll(r, 4) == 0 ? headerEnd : Roll(r, static_cast<uint32_t>(headerEnd) + 1);
        if (cut < b.size()) b.resize(cut);
    }
    return b;
}

DIFF_TEST(Gimex_BmpInfo, 0x881316)
{
    // Reaches the mask bit counter 0x880c8e.
    std::mt19937 r(881316);
    for (int round = 0; round < 1500; ++round) {
        ctx.Mark("info");
        Spec spec;
        const Bytes data = MakeInfoFile(r, spec);
        Side a(data), b(data);
        if (ctx.Original<gimex::OpenFn>(kOpen)(&a.instance, a.stream.Get(), "", 0) != 1) continue;
        if (gimex::bmp::Open(&b.instance, b.stream.Get(), "", 0) != 1) continue;
        gimex::Info *ia = ctx.Original<gimex::InfoFn>(kInfo)(a.instance, 0);
        gimex::Info *ib = gimex::bmp::GetInfo(b.instance, 0);
        ++ctx.cases;
        if ((ia == nullptr) != (ib == nullptr)) {
            ctx.Fail("round %d (variant %d bits %d, %zu bytes): original %s, ours %s", round, spec.variant, spec.bits, data.size(),
                     ia ? "an info" : "null", ib ? "an info" : "null");
        } else if (ia != nullptr && !SameBytes(ia, ib, gimex::kWriteInfoSize)) {
            const uint8_t *pa = reinterpret_cast<const uint8_t *>(ia);
            const uint8_t *pb = reinterpret_cast<const uint8_t *>(ib);
            size_t at = 0;
            while (pa[at] == pb[at]) ++at;
            ctx.Fail("round %d (variant %d bits %d): the infos differ at 0x%zx (original %02x, ours %02x)", round, spec.variant,
                     spec.bits, at, pa[at], pb[at]);
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
DIFF_TEST(Gimex_BmpRead, 0x8817a2)
{
    // Reaches the row decoder 0x880cbf and the mask shift 0x880caa.
    std::mt19937 r(0x8817a2);
    int skippedOverflow = 0;
    int rleRan = 0;
    for (int round = 0; round < 2500; ++round) {
        Spec spec;
        const Bytes data = MakeFile(r, spec, false);
        char label[96];
        std::snprintf(label, sizeof(label), "read %d v%d b%d %dx%d rle%d td%d z%d", round, spec.variant, spec.bits, spec.width,
                      spec.height, spec.rle, spec.topDown, spec.zeroOffset);
        ctx.Mark(label);
        Side a(data), b(data);
        if (ctx.Original<gimex::OpenFn>(kOpen)(&a.instance, a.stream.Get(), "", 0) != 1) continue;
        if (gimex::bmp::Open(&b.instance, b.stream.Get(), "", 0) != 1) continue;
        gimex::Info *ia = ctx.Original<gimex::InfoFn>(kInfo)(a.instance, 0);
        gimex::Info *ib = gimex::bmp::GetInfo(b.instance, 0);
        if (ia == nullptr || ib == nullptr) {
            ++ctx.cases;
            ctx.Fail("round %d: info answered null (original %p, ours %p)", round, static_cast<void *>(ia), static_cast<void *>(ib));
            if (ia) ctx.Original<FreeFn>(kFreeMemory)(ia);
            if (ib) ctx.Original<FreeFn>(kFreeMemory)(ib);
            continue;
        }
        // Sometimes the caller changed channel widths in the info before reading.
        if (spec.variant != kVariantCore && Roll(r, 5) == 0) {
            const uint32_t alpha = Roll(r, 9);
            const uint32_t red = 1 + Roll(r, 10);
            const uint32_t green = 1 + Roll(r, 10);
            const uint32_t blue = 1 + Roll(r, 10);
            for (gimex::Info *info : {ia, ib}) {
                info->alphaBits = alpha;
                info->redBits = red;
                info->greenBits = green;
                info->blueBits = blue;
            }
        }
        const bool indexed = ia->bitsPerPixel == 8;
        const int minPitch = ia->width * (indexed ? 1 : 4);
        const int pitch = minPitch + static_cast<int>(Roll(r, 3) == 0 ? Roll(r, 9) : 0);
        Pixels pa(pitch, ia->height);
        Pixels pb(pitch, ib->height);
        if (spec.readOverflows) {
            // ORIGINAL-BUG: the original reads a run-length row's padding at its moving buffer cursor, which after a last
            // absolute run can sit at the end of the row buffer: up to three bytes land past it (heap corruption).
            // Not run on the original; ours still runs, to check it stays inside its buffer.
            ++skippedOverflow;
            gimex::bmp::ReadFrame(b.instance, ib, pb.data.data(), pitch);
            ctx.Original<FreeFn>(kFreeMemory)(ia);
            ctx.Original<FreeFn>(kFreeMemory)(ib);
            ctx.Original<gimex::CloseFn>(kFree)(a.instance);
            gimex::FreeInstance(b.instance);
            continue;
        }
        if (spec.rle) ++rleRan;
        const int32_t ra = ctx.Original<gimex::ReadFn>(kRead)(a.instance, ia, pa.data.data(), pitch);
        const int32_t rb = gimex::bmp::ReadFrame(b.instance, ib, pb.data.data(), pitch);
        ++ctx.cases;
        if (ra != rb) {
            ctx.Fail("round %d (variant %d bits %d %dx%d rle %d top-down %d zero-offset %d): original %d, ours %d", round,
                     spec.variant, spec.bits, spec.width, spec.height, spec.rle, spec.topDown, spec.zeroOffset, ra, rb);
        } else if (!SameBytes(pa.data.data(), pb.data.data(), pa.data.size())) {
            size_t at = 0;
            while (pa.data[at] == pb.data[at]) ++at;
            ctx.Fail("round %d (variant %d bits %d %dx%d rle %d top-down %d zero-offset %d): pixels differ at %zu (original %02x, "
                     "ours %02x)",
                     round, spec.variant, spec.bits, spec.width, spec.height, spec.rle, spec.topDown, spec.zeroOffset, at,
                     pa.data[at], pb.data[at]);
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
    if (rleRan < 100) ctx.Fail("only %d run-length coded reads ran on the original (%d skipped)", rleRan, skippedOverflow);
}

// ---- write ----
// Reaches the row encoder 0x881a04 and the run length 0x8819dd.

float RandomDpi(std::mt19937 &r)
{
    switch (Roll(r, 6)) {
    case 0: return 0.0f;
    case 1: return 72.0f;
    case 2: return 96.0f;
    case 3: return static_cast<float>(Roll(r, 100000)) / 100.0f;
    case 4: return static_cast<float>(Roll(r, 4000)) + 0.5f;
    default: return static_cast<float>(Roll(r, 1000)) / 7.0f;
    }
}

uint32_t Le32(const Bytes &b, size_t at)
{
    return b[at] | b[at + 1] << 8 | b[at + 2] << 16 | static_cast<uint32_t>(b[at + 3]) << 24;
}

// Whether the original's clearing of its row buffer passes the end of the buffer for this output (a file written by ours):
// run-length coded, with a last row more than 4 bytes longer than the plain row.
bool OriginalOverflows(const Bytes &out)
{
    if (out.size() < 0x36 || Le32(out, 0x1e) != 1) return false;
    const uint32_t bits = out[0x1c];
    const uint32_t width = Le32(out, 0x12);
    const uint32_t height = Le32(out, 0x16);
    size_t at = Le32(out, 10);
    size_t lastStart = at;
    for (uint32_t row = 0; row < height; ++row) {
        lastStart = at;
        while (at + 1 < out.size()) {
            const uint8_t count = out[at];
            const uint8_t value = out[at + 1];
            if (count != 0) {
                at += 2;
            } else if (value == 0) {
                at += 2;
                break;
            } else {
                at += 2 + value + (value & 1);
            }
        }
    }
    const size_t last = at - lastStart;
    const uint32_t full = bits * width + 31;
    const size_t plain = (full & 0xffffffe0u) >> 3;
    return last * 3 + 0x10 > plain * 3 + 0x1c;
}

DIFF_TEST(Gimex_BmpWrite, 0x881d40)
{
    // Reaches the row encoder 0x881a04 and the run length 0x8819dd.
    std::mt19937 r(0x881d40);
    int skippedOverflow = 0;
    int rleRan = 0;
    for (int round = 0; round < 3000; ++round) {
        ctx.Mark("write");
        gimex::Info info;
        std::memset(&info, 0, sizeof(info));
        info.signature = 0x2e424d50;
        info.size = gimex::kWriteInfoSize;
        info.width = static_cast<int32_t>(Roll(r, 41));
        info.height = static_cast<int32_t>(Roll(r, 9));
        if (Roll(r, 20) == 0) {
            // A row around the 255-pixel limit of an absolute run.
            const uint32_t pick = Roll(r, 4);
            info.width = pick == 0 ? 255 : pick == 1 ? 256 : 250 + static_cast<int32_t>(Roll(r, 12));
            info.height = 1 + static_cast<int32_t>(Roll(r, 2));
        }
        info.variant = Roll(r, 6);
        if (Roll(r, 3) == 0) info.variant = Roll(r, 2) ? 2 : 3;
        info.jpegFlag = Roll(r, 3);
        info.dotsPerInch = RandomDpi(r);
        const bool indexed = Roll(r, 2);
        info.bitsPerPixel = indexed ? 8 : 32;
        info.sourceBits = indexed ? static_cast<int32_t>(Roll(r, 14)) - 1 : 32;
        info.paletteCount = indexed ? Roll(r, 4) == 0 ? Roll(r, 257) : 1u << Roll(r, 9) : 0;
        if (info.paletteCount > 256) info.paletteCount = 256;
        for (int i = 0; i < 256; ++i)
            for (int c = 0; c < 4; ++c) info.palette[i][c] = static_cast<uint8_t>(r());
        info.alphaBits = Roll(r, 3) == 0 ? 0 : Roll(r, 9);
        info.redBits = Roll(r, 9);
        info.greenBits = Roll(r, 9);
        info.blueBits = Roll(r, 9);
        if (Roll(r, 3) == 0) {
            info.redBits = info.greenBits = info.blueBits = 8;
            if (Roll(r, 2)) info.alphaBits = 8;
        }
        if (Roll(r, 6) == 0) {
            info.redBits = 5;
            info.greenBits = Roll(r, 2) ? 5 : 6;
            info.blueBits = 5;
            info.alphaBits = Roll(r, 4) == 0 ? 1 : 0;
        }
        const int pitch = info.width * 4 + static_cast<int>(Roll(r, 3) == 0 ? Roll(r, 12) : 0);
        Bytes pixels(static_cast<size_t>(pitch) * static_cast<size_t>(info.height) + 16);
        const uint32_t pattern = info.width > 100 && Roll(r, 2) ? 4 : Roll(r, 4); // 4: one value (runs at the 255 cap)
        for (size_t i = 0; i < pixels.size(); ++i) {
            if (pattern == 4) {
                pixels[i] = 0x5a;
                continue;
            }
            // Random, run-heavy, or few distinct values, so the run-length coder meets runs of every length.
            pixels[i] = pattern == 0 ? static_cast<uint8_t>(r()) : pattern == 1 ? static_cast<uint8_t>(Roll(r, 3))
                        : pattern == 2 ? static_cast<uint8_t>((i / (1 + Roll(r, 6))) & 1) : static_cast<uint8_t>(i / 7);
        }
        const bool limited = Roll(r, 6) == 0;
        {
            // The original clears three times the last encoded row's length plus 16 bytes of a buffer sized from the
            // plain row: a run-length coded last row that is more than 4 bytes longer runs it off the end of the heap
            // block (ours stops at the end), so those cases are not run on the original.
            Side scratch(Bytes{});
            gimex::OpenWriter(&scratch.instance, scratch.stream.Get(), "", 0);
            gimex::Info copy = info;
            gimex::bmp::WriteFrame(scratch.instance, &copy, pixels.data(), pitch);
            gimex::FreeInstance(scratch.instance);
            if (OriginalOverflows(scratch.stream.file.written)) {
                ++skippedOverflow;
                continue;
            }
            const Bytes &made = scratch.stream.file.written;
            if (made.size() >= 0x22 && Le32(made, 0x1e) == 1) ++rleRan;
        }
        Side a(Bytes{}), b(Bytes{});
        if (limited) {
            const int64_t limit = static_cast<int64_t>(Roll(r, 200));
            a.stream.file.writeLimit = limit;
            b.stream.file.writeLimit = limit;
        }
        if (ctx.Original<gimex::WOpenFn>(kWOpen)(&a.instance, a.stream.Get(), "", 0) != 1) continue;
        if (gimex::OpenWriter(&b.instance, b.stream.Get(), "", 0) != 1) continue;
        gimex::Info ia = info;
        gimex::Info ib = info;
        const int32_t ra = ctx.Original<gimex::WriteFn>(kWrite)(a.instance, &ia, pixels.data(), pitch);
        const int32_t rb = gimex::bmp::WriteFrame(b.instance, &ib, pixels.data(), pitch);
        ++ctx.cases;
        if (ra != rb) {
            ctx.Fail("round %d (variant %u bits %d/%d %dx%d flag %u): original %d, ours %d", round, info.variant,
                     info.bitsPerPixel, info.sourceBits, info.width, info.height, info.jpegFlag, ra, rb);
        } else if (a.stream.file.written != b.stream.file.written) {
            const Bytes &wa = a.stream.file.written;
            const Bytes &wb = b.stream.file.written;
            size_t at = 0;
            while (at < wa.size() && at < wb.size() && wa[at] == wb[at]) ++at;
            ctx.Fail("round %d (variant %u bits %d/%d %dx%d flag %u alpha %u rgb %u %u %u): written bytes differ at %zu "
                     "(original %zu bytes, ours %zu bytes)",
                     round, info.variant, info.bitsPerPixel, info.sourceBits, info.width, info.height, info.jpegFlag,
                     info.alphaBits, info.redBits, info.greenBits, info.blueBits, at, wa.size(), wb.size());
        }
        if (!SameBytes(&ia, &ib, sizeof(ia)) || !SameBytes(&ia, &info, sizeof(ia))) {
            ctx.Fail("round %d: write changed its info", round);
        }
        if (a.stream.file.stray != 0 || b.stream.file.stray != 0) ctx.Fail("round %d: stray stream calls", round);
        ctx.Original<gimex::CloseFn>(kFree)(a.instance);
        gimex::FreeInstance(b.instance);
    }
    // The run-length coder must have been reached on the original too.
    if (rleRan < 100) ctx.Fail("only %d run-length coded writes ran on the original (%d skipped)", rleRan, skippedOverflow);
}

} // namespace
