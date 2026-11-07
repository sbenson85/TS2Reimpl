// Differential tests for src/gz/GimexFsh.size.cpp: Gimex's FSH codec (row 1 of the codec table): about, is, open, info,
// read, close, wopen, write and wclose.
//
// Two kinds of test:
// - each entry alone (Gimex_FshOpen ... Gimex_FshCloseWriter): the back end it calls (the directory reader 0x887a06, the
//   directory frees 0x885f08 / 0x88a105, the shape decoder 0x886e83, the encoder 0x88905f, the directory writer 0x88c719)
//   is patched on both sides to a recording fake with scripted answers, and the codecs' allocator (0x87da65 / 0x87da72,
//   and ours) to a recording fake that can fail a scripted call. Info runs the real back-end helpers it calls (0x886956,
//   0x8866ae, 0x886652, 0x88675f, 0x886680, 0x8881f8 and the palette converters) over synthetic directories: every shape
//   type 0..0xff, every palette type, attached blocks of every kind (block counts past 0x400 included), names with
//   trailing spaces. Compared: results, the instance block, the info block (all 0x2a7c bytes), the directory entry
//   after the call (info sets its flags), the fakes' logs.
// - round trips through the back end (Gimex_FshRoundTrip): synthetic frames (random sizes, source bits 4 / 8 / 15 /
//   16 / 24 / 32, alpha bits, kinds 0..5, names; the block kinds 3 / 5 flat and opaque, the GM kind 4 in multiples of
//   16) written by each side's wopen / write / wclose (sometimes one write too many) to its own seekable fake
//   cIGZFile, then the file read back by each side's open / info / read / close: as written, with bytes after it, and
//   with its directory names damaged. Compared: results, the bytes written, the info blocks, the decoded pixels (the
//   whole destination, sentinel-filled; not for kinds 4 and 5, whose decoders leave stack garbage in what they write:
//   see the comment in the test), the stream positions and stray calls, the allocator log and the blocks left allocated.
//
// Inputs kept out because the original reads garbage or crashes on them (ours does the same there):
// - a negative frame for info (0x87dc98 indexes the directory with it unchecked and reads before the block) or for read
//   other than -1 (0x87eb11 does the same);
// - a directory reader that answers non-zero with no directory (0x87ea9f then reads the count through null);
// - an info whose name is longer than the block's 0x2620 bytes from 0x45c (copied unbounded) or a text block longer than
//   0x2420 from 0x65c (0x886652 copies it unbounded);
// - in round trips: wopen frame counts the real allocator can't give (operator new throws); a file under 16 bytes, or
//   any the directory reader 0x887a06 can't read into a directory (it passes the null directory on to 0x88800e,
//   which reads its count: an access to 0 at 0x888024); a file cut short (the decoder copies a shape's declared size
//   past the end of the file buffer: memcpy 0x826830 faults, and short of that the pixels differ by heap garbage);
//   damaged offsets, sizes or shape headers (reads through garbage, 0x87db28 in the readers); kinds (info+0x42c) 2..5,
//   the compressed records: the same original back end, given the same arguments on both sides, wrote different
//   files and decoded different pixels, and which rounds differed changed from run to run (its compressors read
//   memory the test doesn't control). The back end below the glue is the original on both sides, so these are its
//   behaviour, not the glue's.
#define NOMINMAX
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "gz/GimexFormats.h"
#include "gz/GimexFshDecode.h"
#include "gz/GimexFshWrite.h"
#include "gz_gimex_stream.h"
#include "shape.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace gimexstream;

constexpr uint32_t kAbout = 0x87dbad;
constexpr uint32_t kIs = 0x87ea4e;
constexpr uint32_t kOpen = 0x87ea9f;
constexpr uint32_t kInfo = 0x87dc98;
constexpr uint32_t kRead = 0x87eb11;
constexpr uint32_t kClose = 0x87dc79;
constexpr uint32_t kWOpen = 0x87eb94;
constexpr uint32_t kWrite = 0x87ebf5;
constexpr uint32_t kWClose = 0x87eb4b;
constexpr uint32_t kAllocate = 0x87da65;
constexpr uint32_t kFreeMemory = 0x87da72;
constexpr uint32_t kReadDirectory = 0x887a06;
constexpr uint32_t kFreeDirectory = 0x885f08;
constexpr uint32_t kFreeWritten = 0x88a105;
constexpr uint32_t kDecode = 0x886e83;
constexpr uint32_t kEncode = 0x88905f;
constexpr uint32_t kWriteDirectory = 0x88c719;

constexpr uint32_t kInfoBytes = 0x2a7c;
constexpr uint32_t kEntryBytes = 0x4c;
constexpr uint32_t kFourcc = 0x53687046;

using AboutFn = gimex::About *(__stdcall *)();
using IsFn = int32_t(__stdcall *)(ifaces::cIGZFile *);
using OpenFn = int32_t(__stdcall *)(gimex::Instance **, ifaces::cIGZFile *, const char *, int32_t);
using InfoFn = gimex::Info *(__stdcall *)(gimex::Instance *, int32_t);
using ReadFn = int32_t(__stdcall *)(gimex::Instance *, gimex::Info *, void *, int32_t);
using CloseFn = int32_t(__stdcall *)(gimex::Instance *);
using WriteFn = int32_t(__stdcall *)(gimex::Instance *, const gimex::Info *, const void *, int32_t);

std::vector<uint32_t> gLog;

// ---- the recording allocator (both sides' allocator and free) ----

struct Heap {
    std::vector<void *> blocks; // every block handed out, in order (null once freed)
    int failAt = -1;            // the allocation (0-based) that fails; -1 none
    int calls = 0;
};
Heap gHeap;

void *__stdcall FakeAllocate(uint32_t size)
{
    const int index = gHeap.calls++;
    gLog.push_back(0x1000000u);
    gLog.push_back(size);
    if (index == gHeap.failAt || size > 0x4000000) return nullptr;
    const size_t bytes = size != 0 ? size : 1;
    void *const block = std::malloc(bytes);
    std::memset(block, 0xa5, bytes);
    gHeap.blocks.push_back(block);
    return block;
}

uint8_t __stdcall FakeRelease(void *block)
{
    uint32_t ordinal = 0;
    for (size_t i = 0; i < gHeap.blocks.size(); ++i) {
        if (gHeap.blocks[i] == block && block != nullptr) ordinal = static_cast<uint32_t>(i) + 1;
    }
    gLog.push_back(0x2000000u);
    gLog.push_back(block == nullptr ? 0xffffffffu : ordinal);
    if (ordinal != 0) {
        std::free(block);
        gHeap.blocks[ordinal - 1] = nullptr;
    }
    return 1;
}

uint32_t Ordinal(const void *block)
{
    for (size_t i = 0; i < gHeap.blocks.size(); ++i)
        if (gHeap.blocks[i] == block && block != nullptr) return static_cast<uint32_t>(i) + 1;
    return 0;
}

void ResetHeap(int failAt)
{
    for (void *block : gHeap.blocks) std::free(block);
    gHeap = Heap{};
    gHeap.failAt = failAt;
    gLog.clear();
}

struct HeapFakes {
    shape::CodePatch allocate{kAllocate, reinterpret_cast<const void *>(&FakeAllocate)};
    shape::CodePatch release{kFreeMemory, reinterpret_cast<const void *>(&FakeRelease)};
    shape::CodePatch ourAllocate{reinterpret_cast<void *>(&gimex::Allocate), reinterpret_cast<const void *>(&FakeAllocate)};
    shape::CodePatch ourRelease{reinterpret_cast<void *>(&gimex::FreeMemory), reinterpret_cast<const void *>(&FakeRelease)};
    ~HeapFakes() { ResetHeap(-1); }
};

void CompareLogs(difftest::Context &ctx, const char *what, int index, const std::vector<uint32_t> &a,
                 const std::vector<uint32_t> &b)
{
    if (a == b) return;
    ctx.Fail("%s case %d: the logs differ (original %zu words, ours %zu)", what, index, a.size(), b.size());
    for (size_t i = 0; i < a.size() || i < b.size(); ++i) {
        const uint32_t x = i < a.size() ? a[i] : 0xdeadbeef;
        const uint32_t y = i < b.size() ? b[i] : 0xdeadbeef;
        if (x != y) {
            ctx.Fail("  word %zu: original %08x, ours %08x", i, x, y);
            break;
        }
    }
}

uint32_t Word(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

// ---- the back end's fakes ----

struct Script {
    int32_t answer = 0;              // the fake's answer
    void *directory = nullptr;       // what the directory reader leaves in *out
    bool leaveOut = true;            // the reader writes *out
};
Script gScript;

int32_t __cdecl FakeReadDirectory(ifaces::cIGZFile *stream, void **out, int32_t a3, int32_t a4, uint32_t a5,
                                  uint32_t a6, int32_t a7)
{
    gLog.insert(gLog.end(), {0x3000000u, Word(stream), *reinterpret_cast<uint32_t *>(out), static_cast<uint32_t>(a3),
                             static_cast<uint32_t>(a4), a5, a6, static_cast<uint32_t>(a7)});
    if (gScript.leaveOut) *out = gScript.directory;
    return gScript.answer;
}

void __cdecl FakeFreeDirectory(void *directory)
{
    gLog.insert(gLog.end(), {0x3100000u, Word(directory)});
}

void __cdecl FakeFreeWritten(void *directory)
{
    gLog.insert(gLog.end(), {0x3200000u, Word(directory)});
}

int32_t __cdecl FakeDecode(const void *shape, void *pixels, int32_t pitch, int32_t platform)
{
    gLog.insert(gLog.end(), {0x3300000u, Word(shape), Word(pixels), static_cast<uint32_t>(pitch),
                             static_cast<uint32_t>(platform)});
    return gScript.answer;
}

int32_t __cdecl FakeEncode(void *directory, const void *info, const void *pixels, int32_t pitch, int32_t index,
                           int32_t platform, int32_t option)
{
    gLog.insert(gLog.end(), {0x3400000u, Word(directory), Word(info), Word(pixels), static_cast<uint32_t>(pitch),
                             static_cast<uint32_t>(index), static_cast<uint32_t>(platform), static_cast<uint32_t>(option)});
    return gScript.answer;
}

// Ours takes no seventh argument: the original's glue passed the constant 0 there (the encoder never reads it: its
// argument 7 is not referenced in 0x88905f), so the log carries that 0 for our side.
int32_t __cdecl FakeEncodeOurs(void *directory, const void *info, const void *pixels, int32_t pitch, int32_t index,
                               int32_t platform)
{
    return FakeEncode(directory, info, pixels, pitch, index, platform, 0);
}

int32_t __cdecl FakeWriteDirectory(ifaces::cIGZFile *stream, int32_t *directory, int32_t a3, int32_t a4, int32_t a5,
                                   uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9, int32_t a10, uint32_t a11)
{
    gLog.insert(gLog.end(), {0x3500000u, Word(stream), Word(directory), static_cast<uint32_t>(*directory),
                             static_cast<uint32_t>(a3), static_cast<uint32_t>(a4), static_cast<uint32_t>(a5), a6, a7,
                             a8, a9, static_cast<uint32_t>(a10), a11});
    return gScript.answer;
}

// ---- synthetic directories ----

// Test-owned memory that stays put while a case runs.
struct Arena {
    std::vector<std::vector<uint8_t>> blocks;
    uint8_t *New(size_t bytes, std::mt19937 &rng)
    {
        blocks.emplace_back(bytes);
        for (uint8_t &b : blocks.back()) b = static_cast<uint8_t>(rng());
        return blocks.back().data();
    }
};

void Put32At(uint8_t *at, uint32_t value)
{
    std::memcpy(at, &value, 4);
}

uint32_t Pick(std::mt19937 &rng, std::initializer_list<uint32_t> values)
{
    return values.begin()[rng() % values.size()];
}

// The palette and record type codes info and the back-end helpers it calls distinguish (a list of codes, not bytes of
// the image; the other rounds take every type 0..0xff).
constexpr uint8_t kPaletteTypes[] = {' ', '!', '"', '#', '$', ')', '*', ',', '-', '.', '0', '1', '2', '3', '8', '9'};
constexpr uint8_t kShapeTypes[] = {0x1, 0x2, 0x3, 0x4, 0x5, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x18, 0x19, 0x1a,
                                   0x1c, 0x1e, 0x20, 0x21, 0x22, 0x23, 0x24, 0x29, 0x2a, 0x2d, 0x38, 0x39, 0x40, 0x41,
                                   0x43, 0x44, 0x58, 0x5a, 0x5d, 0x5e, 0x60, 0x61, 0x62, 0x64, 0x65, 0x66, 0x67, 0x68,
                                   0x6a, 0x6c, 0x6d, 0x78, 0x79, 0x7b, 0x7d, 0x7e, 0x7f};

// A shape header (0x40 bytes): type, attributes, origins, size, the two mark bytes.
uint8_t *MakeShape(Arena &arena, std::mt19937 &rng, int round)
{
    uint8_t *shape = arena.New(0x40, rng);
    shape[0] = round < 256 ? static_cast<uint8_t>(round)
                           : (rng() % 4 ? kShapeTypes[rng() % sizeof(kShapeTypes)] | (rng() % 4 ? 0 : 0x80)
                                        : static_cast<uint8_t>(rng()));
    if (rng() % 3 == 0) {
        shape[0x20] = 0x1a;
        shape[0x21] = 0xfb;
    }
    if (rng() % 2) Put32At(shape + 0x18, rng() % 0x400);
    if (rng() % 2) Put32At(shape + 0x1c, rng() % 0x400);
    return shape;
}

// A palette (header and entries): type, attributes (the swizzle bit), its count word at +0x18, the 0x31d check word.
uint8_t *MakePalette(Arena &arena, std::mt19937 &rng)
{
    uint8_t *palette = arena.New(0x20 + 0x400 + 0x40, rng);
    palette[0] = rng() % 8 ? kPaletteTypes[rng() % sizeof(kPaletteTypes)] : static_cast<uint8_t>(rng());
    if (rng() % 2) palette[2] &= 0xbf;
    Put32At(palette + 0x18, Pick(rng, {0, 1, 2, 16, 0x100, 0x101, 0xffffffff, 0x80000000, 7, 0x20}) +
                                (rng() % 3 == 0 ? rng() % 0x100 : 0));
    if (rng() % 2) {
        palette[0x31d] = 0;
        palette[0x31e] = 0;
        palette[0x31f] = rng() % 2 ? 0 : static_cast<uint8_t>(1 + rng() % 0xff); // all three bytes are the check
    }
    return palette;
}

// The block 0x8866ae reads (+0x10, +0x14, a count at +0x1c and that many pairs from +0x20).
uint8_t *MakeBlock14(Arena &arena, std::mt19937 &rng)
{
    const uint32_t count = Pick(rng, {0, 1, 3, 0x40, 0x400, 0x401, 0x7fffffff, 0x80000000, 0xffffffff, 2});
    uint8_t *block = arena.New(0x20 + 8 * 0x400 + 8, rng);
    Put32At(block + 0x1c, count);
    return block;
}

uint8_t *MakeText(Arena &arena, std::mt19937 &rng)
{
    uint8_t *text = arena.New(0x10 + 0x50, rng);
    const uint32_t length = rng() % 0x40;
    for (uint32_t i = 0; i < length; ++i) text[0x10 + i] = static_cast<uint8_t>('a' + rng() % 26);
    text[0x10 + length] = 0;
    return text;
}

char *MakeName(Arena &arena, std::mt19937 &rng)
{
    static const char kChars[] = "ab  1z";
    const uint32_t length = rng() % 9;
    char *name = reinterpret_cast<char *>(arena.New(length + 1, rng));
    for (uint32_t i = 0; i < length; ++i) name[i] = kChars[rng() % (sizeof(kChars) - 1)];
    name[length] = 0;
    return name;
}

struct Directory {
    std::vector<uint8_t> bytes;
    int32_t count = 0;
    uint8_t *Entry(int32_t i) { return bytes.data() + 4 + kEntryBytes * i; }
};

void MakeDirectory(Directory &dir, Arena &arena, std::mt19937 &rng, int round)
{
    dir.count = 1 + static_cast<int32_t>(rng() % 4);
    dir.bytes.assign(4 + kEntryBytes * dir.count + 8, 0);
    Put32At(dir.bytes.data(), static_cast<uint32_t>(dir.count));
    for (int32_t i = 0; i < dir.count; ++i) {
        uint8_t *e = dir.Entry(i);
        for (uint32_t k = 0; k < kEntryBytes; ++k) e[k] = static_cast<uint8_t>(rng());
        Put32At(e + 0x00, rng() % 16 ? Word(MakeShape(arena, rng, round)) : 0);
        Put32At(e + 0x0c, rng() % 3 ? Word(MakePalette(arena, rng)) : 0);
        Put32At(e + 0x14, rng() % 3 == 0 ? Word(MakeBlock14(arena, rng)) : 0);
        Put32At(e + 0x1c, rng() % 3 == 0 ? Word(MakeText(arena, rng)) : 0);
        Put32At(e + 0x24, rng() % 3 == 0 ? Word(arena.New(4, rng)) : 0);
        Put32At(e + 0x2c, rng() % 3 == 0 ? Word(arena.New(0x10, rng)) : 0);
        Put32At(e + 0x34, rng() % 4 ? Word(MakeName(arena, rng)) : 0);
        Put32At(e + 0x38, rng() % 2 ? rng() : 0);
        Put32At(e + 0x3c, Pick(rng, {0, 1, 2, 3, 4, 5, 3, 3}) + (rng() % 8 == 0 ? rng() : 0));
        Put32At(e + 0x40, rng());
    }
}

gimex::Instance MakeInstance(const Directory &dir, uint32_t frameCount)
{
    gimex::Instance instance{};
    instance.fourcc = kFourcc;
    instance.size = 0x18;
    instance.frameCount = frameCount;
    instance.stream = reinterpret_cast<ifaces::cIGZFile *>(static_cast<uintptr_t>(0x5eed0000));
    instance.unknown14 = Word(dir.bytes.data());
    return instance;
}

} // namespace

// 0x87dbad: the description (all 0x100 bytes).
DIFF_TEST(Gimex_FshAbout, 0x87dbad)
{
    HeapFakes heap;
    for (int failAt = -1; failAt < 1; ++failAt) {
        ResetHeap(failAt);
        gimex::About *a = ctx.Original<AboutFn>(kAbout)();
        const std::vector<uint32_t> logA = gLog;
        gHeap.calls = 0;
        gLog.clear();
        gimex::About *b = gimex::fsh::NewAbout();
        ++ctx.cases;
        if ((a == nullptr) != (b == nullptr)) {
            ctx.Fail("fail at %d: original %s, ours %s", failAt, a ? "a description" : "null", b ? "a description" : "null");
        } else if (a != nullptr && std::memcmp(a, b, sizeof(gimex::About)) != 0) {
            for (size_t i = 0; i < sizeof(gimex::About); ++i) {
                if (reinterpret_cast<uint8_t *>(a)[i] != reinterpret_cast<uint8_t *>(b)[i]) {
                    ctx.Fail("the descriptions differ first at +0x%zx", i);
                    break;
                }
            }
        }
        CompareLogs(ctx, "About", failAt, logA, gLog);
    }
}

// 0x87ea4e: the score of a stream's first 0x20 bytes ('ShpF' and 'FntF' read big-endian).
DIFF_TEST(Gimex_FshIs, 0x87ea4e)
{
    std::mt19937 rng(0x87ea4e);
    static const uint8_t kHeads[][4] = {{'S', 'h', 'p', 'F'}, {'F', 'n', 't', 'F'}, {'S', 'H', 'P', 'I'},
                                        {'F', 'p', 'h', 'S'}, {'F', 't', 'n', 'F'}, {0, 0, 0, 0}};
    for (int i = 0; i < 400; ++i) {
        Bytes data;
        const uint32_t length = i < 40 ? static_cast<uint32_t>(i) : (rng() % 3 ? 0x20 + rng() % 0x40 : rng() % 0x24);
        RandomBytes(rng, data, length);
        if (length >= 4 && rng() % 4) std::memcpy(data.data(), kHeads[rng() % 6], 4);
        Stream sa(data), sb(data);
        sa.file.pos = sb.file.pos = static_cast<uint32_t>(rng() % (length + 1));
        const int32_t a = ctx.Original<IsFn>(kIs)(sa.Get());
        const int32_t b = gimex::fsh::Probe(sb.Get());
        ++ctx.cases;
        if (a != b || sa.file.pos != sb.file.pos || sa.file.stray != sb.file.stray)
            ctx.Fail("case %d (%u bytes): original %d at %u (%d strays), ours %d at %u (%d strays)", i, length, a,
                     sa.file.pos, sa.file.stray, b, sb.file.pos, sb.file.stray);
    }
}

// 0x87ea9f: the directory reader (fake) and the instance block (allocator fake, failing too).
DIFF_TEST(Gimex_FshOpen, 0x87ea9f)
{
    HeapFakes heap;
    shape::CodePatch reader{kReadDirectory, reinterpret_cast<const void *>(&FakeReadDirectory)};
    shape::CodePatch ourReader{reinterpret_cast<void *>(&gimex::fsh::ReadDirectoryStream),
                               reinterpret_cast<const void *>(&FakeReadDirectory)};
    std::mt19937 rng(0x87ea9f);
    int32_t directory[4];
    for (int i = 0; i < 300; ++i) {
        directory[0] = static_cast<int32_t>(Pick(rng, {0, 1, 2, 5, 0x7fffffff, 0xffffffff}));
        gScript.answer = static_cast<int32_t>(Pick(rng, {0, 1, 1, 2, 0xffffffff}));
        gScript.leaveOut = rng() % 4 != 0;
        gScript.directory = rng() % 4 || gScript.answer == 0 ? directory : nullptr;
        if (gScript.answer != 0 && gScript.directory == nullptr) gScript.directory = directory; // kept out (see the top)
        if (gScript.answer != 0 && !gScript.leaveOut) gScript.leaveOut = true;                // the same: *out stays null
        const int failAt = rng() % 3 == 0 ? 0 : -1;
        const int32_t flag = static_cast<int32_t>(rng());
        auto *stream = reinterpret_cast<ifaces::cIGZFile *>(static_cast<uintptr_t>(0x5eed0000 + (rng() & 0xff0)));
        int32_t result[2];
        gimex::Instance copy[2]{};
        uint32_t out[2];
        std::vector<uint32_t> logs[2];
        for (int side = 0; side < 2; ++side) {
            ResetHeap(failAt);
            gimex::Instance *instance = reinterpret_cast<gimex::Instance *>(static_cast<uintptr_t>(0xdddd0000));
            result[side] = side == 0 ? ctx.Original<OpenFn>(kOpen)(&instance, stream, "x.fsh", flag)
                                     : gimex::fsh::Open(&instance, stream, "x.fsh", flag);
            out[side] = Word(instance);
            if (out[side] != 0xdddd0000 && instance != nullptr) {
                copy[side] = *instance;
                out[side] = 0x100 + Ordinal(instance);
            }
            logs[side] = gLog;
        }
        ++ctx.cases;
        if (result[0] != result[1] || out[0] != out[1] || std::memcmp(&copy[0], &copy[1], sizeof(copy[0])) != 0)
            ctx.Fail("case %d (reader %d, count %d, fail %d): original %d out %08x, ours %d out %08x", i, gScript.answer,
                     directory[0], failAt, result[0], out[0], result[1], out[1]);
        CompareLogs(ctx, "Open", i, logs[0], logs[1]);
    }
}

// 0x87dc98: synthetic directories through the real back-end helpers.
DIFF_TEST(Gimex_FshInfo, 0x87dc98)
{
    HeapFakes heap;
    std::mt19937 rng(0x87dc98);
    for (int round = 0; round < 3000; ++round) {
        Arena arena;
        Directory dir;
        MakeDirectory(dir, arena, rng, round % 1000);
        const int32_t frame = static_cast<int32_t>(rng() % 8 ? rng() % dir.count : dir.count + rng() % 3);
        const uint32_t frameCount = rng() % 6 ? static_cast<uint32_t>(dir.count) : rng() % 3;
        const int failAt = rng() % 10 == 0 ? 0 : -1;
        const std::vector<uint8_t> before = dir.bytes;
        std::vector<uint8_t> info[2], after[2];
        std::vector<uint32_t> logs[2];
        bool null[2];
        for (int side = 0; side < 2; ++side) {
            dir.bytes = before;
            ResetHeap(failAt);
            gimex::Instance instance = MakeInstance(dir, frameCount);
            gimex::Info *got = side == 0 ? ctx.Original<InfoFn>(kInfo)(&instance, frame) : gimex::fsh::GetInfo(&instance, frame);
            null[side] = got == nullptr;
            if (got != nullptr) {
                const uint8_t *p = reinterpret_cast<const uint8_t *>(got);
                info[side].assign(p, p + kInfoBytes);
                logs[side].push_back(0x4000000u + Ordinal(got));
            }
            after[side] = dir.bytes;
            logs[side].insert(logs[side].begin(), gLog.begin(), gLog.end());
        }
        dir.bytes = before;
        ++ctx.cases;
        if (null[0] != null[1]) {
            ctx.Fail("round %d frame %d: original %s, ours %s", round, frame, null[0] ? "null" : "an info",
                     null[1] ? "null" : "an info");
        } else if (info[0] != info[1]) {
            for (size_t i = 0; i < kInfoBytes; ++i) {
                if (info[0][i] != info[1][i]) {
                    const uint8_t *shape = reinterpret_cast<const uint8_t *>(
                        static_cast<uintptr_t>(*reinterpret_cast<const uint32_t *>(dir.Entry(frame))));
                    const uint8_t *palette = reinterpret_cast<const uint8_t *>(
                        static_cast<uintptr_t>(*reinterpret_cast<const uint32_t *>(dir.Entry(frame) + 0xc)));
                    ctx.Fail("round %d frame %d (shape type %02x, palette type %02x): the infos differ first at +0x%zx "
                             "(original %02x, ours %02x)",
                             round, frame, shape ? shape[0] : 0, palette ? palette[0] : 0, i, info[0][i], info[1][i]);
                    break;
                }
            }
        }
        if (after[0] != after[1]) ctx.Fail("round %d frame %d: the directories differ after the call", round, frame);
        CompareLogs(ctx, "Info", round, logs[0], logs[1]);
    }
}

// 0x87eb11: the frame's shape to the decoder (fake), or 0 for frame -1 or no shape.
DIFF_TEST(Gimex_FshRead, 0x87eb11)
{
    shape::CodePatch decoder{kDecode, reinterpret_cast<const void *>(&FakeDecode)};
    shape::CodePatch ourDecoder{reinterpret_cast<void *>(&gimex::fsh::DecodeShape),
                                reinterpret_cast<const void *>(&FakeDecode)};
    std::mt19937 rng(0x87eb11);
    for (int i = 0; i < 300; ++i) {
        Arena arena;
        Directory dir;
        MakeDirectory(dir, arena, rng, 1000);
        gimex::Instance instance = MakeInstance(dir, static_cast<uint32_t>(dir.count));
        gimex::Info info{};
        info.unknownC = rng() % 5 == 0 ? 0xffffffffu : static_cast<uint32_t>(rng() % dir.count);
        gScript.answer = static_cast<int32_t>(Pick(rng, {0, 1, 2, 0xffffffff, 0x12345}));
        uint8_t pixels[16];
        const int32_t pitch = static_cast<int32_t>(rng() % 3 ? rng() % 0x400 : rng());
        int32_t result[2];
        std::vector<uint32_t> logs[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            result[side] = side == 0 ? ctx.Original<ReadFn>(kRead)(&instance, &info, pixels, pitch)
                                     : gimex::fsh::ReadFrame(&instance, &info, pixels, pitch);
            logs[side] = gLog;
        }
        ++ctx.cases;
        if (result[0] != result[1]) ctx.Fail("case %d frame %d: original %d, ours %d", i, info.unknownC, result[0], result[1]);
        CompareLogs(ctx, "Read", i, logs[0], logs[1]);
    }
}

// 0x87dc79: the directory to its free (fake), the instance to the codecs' free; 1 also for null.
DIFF_TEST(Gimex_FshClose, 0x87dc79)
{
    HeapFakes heap;
    shape::CodePatch freer{kFreeDirectory, reinterpret_cast<const void *>(&FakeFreeDirectory)};
    shape::CodePatch ourFreer{reinterpret_cast<void *>(&gimex::fsh::FreeDirectory),
                              reinterpret_cast<const void *>(&FakeFreeDirectory)};
    std::mt19937 rng(0x87dc79);
    for (int i = 0; i < 60; ++i) {
        const bool null = i % 4 == 1;
        const uint32_t directory = rng();
        int32_t result[2];
        std::vector<uint32_t> logs[2];
        for (int side = 0; side < 2; ++side) {
            ResetHeap(-1);
            gimex::Instance *instance = null ? nullptr : static_cast<gimex::Instance *>(FakeAllocate(0x18));
            if (instance != nullptr) instance->unknown14 = directory;
            gLog.clear();
            result[side] = side == 0 ? ctx.Original<CloseFn>(kClose)(instance) : gimex::fsh::Close(instance);
            logs[side] = gLog;
        }
        ++ctx.cases;
        if (result[0] != result[1]) ctx.Fail("case %d: original %d, ours %d", i, result[0], result[1]);
        CompareLogs(ctx, "Close", i, logs[0], logs[1]);
    }
}

// 0x87eb94: the zeroed directory of `frames` entries and the instance (allocator fake, failing too).
DIFF_TEST(Gimex_FshOpenWriter, 0x87eb94)
{
    HeapFakes heap;
    std::mt19937 rng(0x87eb94);
    for (int i = 0; i < 300; ++i) {
        const int32_t frames = static_cast<int32_t>(
            i < 10 ? Pick(rng, {0, 1, 2, 0xffffffff, 0x80000000, 0x7fffffff, 0x40000000, 0xc0000000, 100, 3}) : rng() % 40);
        const int failAt = rng() % 4 == 0 ? static_cast<int>(rng() % 2) : -1;
        auto *stream = reinterpret_cast<ifaces::cIGZFile *>(static_cast<uintptr_t>(0x5eed0000 + (rng() & 0xff0)));
        int32_t result[2];
        uint32_t out[2];
        gimex::Instance copy[2]{};
        std::vector<uint8_t> directory[2];
        std::vector<uint32_t> logs[2];
        for (int side = 0; side < 2; ++side) {
            ResetHeap(failAt);
            gimex::Instance *instance = reinterpret_cast<gimex::Instance *>(static_cast<uintptr_t>(0xdddd0000));
            result[side] = side == 0 ? ctx.Original<OpenFn>(kWOpen)(&instance, stream, "x.fsh", frames)
                                     : gimex::fsh::OpenWriter(&instance, stream, "x.fsh", frames);
            out[side] = Word(instance);
            if (out[side] != 0xdddd0000) {
                copy[side] = *instance;
                out[side] = 0x100 + Ordinal(instance);
                const uint8_t *d = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(instance->unknown14));
                copy[side].unknown14 = Ordinal(d);
                directory[side].assign(d, d + 4 + kEntryBytes * static_cast<uint32_t>(frames));
            }
            logs[side] = gLog;
            logs[side].push_back(static_cast<uint32_t>(gHeap.blocks.size()));
        }
        ++ctx.cases;
        if (result[0] != result[1] || out[0] != out[1] || std::memcmp(&copy[0], &copy[1], sizeof(copy[0])) != 0 ||
            directory[0] != directory[1])
            ctx.Fail("case %d (%d frames, fail %d): original %d out %08x, ours %d out %08x", i, frames, failAt, result[0],
                     out[0], result[1], out[1]);
        CompareLogs(ctx, "OpenWriter", i, logs[0], logs[1]);
    }
}

// 0x87ebf5: the next frame to the encoder (fake) while there is room.
DIFF_TEST(Gimex_FshWrite, 0x87ebf5)
{
    shape::CodePatch encoder{kEncode, reinterpret_cast<const void *>(&FakeEncode)};
    shape::CodePatch ourEncoder{reinterpret_cast<void *>(&gimex::fsh::EncodeShape), reinterpret_cast<const void *>(&FakeEncodeOurs)};
    std::mt19937 rng(0x87ebf5);
    for (int i = 0; i < 300; ++i) {
        gimex::Instance instance{};
        instance.frameCount = Pick(rng, {0, 1, 2, 5, 0xffffffff, 0x80000000, 0x7fffffff});
        instance.unknownC = rng() % 4 ? rng() % 6 : Pick(rng, {0xffffffff, 0x80000000, 0x7fffffff});
        instance.unknown14 = rng();
        gScript.answer = static_cast<int32_t>(Pick(rng, {0, 1, 2, 0xffffffff}));
        gimex::Info info{};
        uint8_t pixels[4];
        const int32_t pitch = static_cast<int32_t>(rng());
        int32_t result[2];
        uint32_t index[2];
        std::vector<uint32_t> logs[2];
        const gimex::Instance start = instance;
        for (int side = 0; side < 2; ++side) {
            instance = start;
            gLog.clear();
            result[side] = side == 0 ? ctx.Original<WriteFn>(kWrite)(&instance, &info, pixels, pitch)
                                     : gimex::fsh::WriteFrame(&instance, &info, pixels, pitch);
            index[side] = instance.unknownC;
            logs[side] = gLog;
        }
        ++ctx.cases;
        if (result[0] != result[1] || index[0] != index[1])
            ctx.Fail("case %d (%u of %u): original %d next %u, ours %d next %u", i, start.unknownC, start.frameCount,
                     result[0], index[0], result[1], index[1]);
        CompareLogs(ctx, "Write", i, logs[0], logs[1]);
    }
}

// 0x87eb4b: the frames written become the directory's count; the directory writer and free (fakes), the instance freed.
DIFF_TEST(Gimex_FshCloseWriter, 0x87eb4b)
{
    HeapFakes heap;
    shape::CodePatch writer{kWriteDirectory, reinterpret_cast<const void *>(&FakeWriteDirectory)};
    shape::CodePatch freer{kFreeWritten, reinterpret_cast<const void *>(&FakeFreeWritten)};
    // Ours: the same fakes at our functions' entries (the glue calls ours now; the original's addresses serve the original side).
    shape::CodePatch ourWriter{reinterpret_cast<void *>(&gimex::fsh::WriteDirectory), reinterpret_cast<const void *>(&FakeWriteDirectory)};
    shape::CodePatch ourFreer{reinterpret_cast<void *>(&gimex::fsh::FreeWrittenDirectory), reinterpret_cast<const void *>(&FakeFreeWritten)};
    std::mt19937 rng(0x87eb4b);
    int32_t directory[2];
    for (int i = 0; i < 60; ++i) {
        const bool null = i % 5 == 2;
        const uint32_t written = rng() % 4 ? rng() % 6 : rng();
        gScript.answer = static_cast<int32_t>(Pick(rng, {0, 1, 2, 0xffffffff}));
        auto *stream = reinterpret_cast<ifaces::cIGZFile *>(static_cast<uintptr_t>(0x5eed0000 + (rng() & 0xff0)));
        int32_t result[2];
        std::vector<uint32_t> logs[2];
        for (int side = 0; side < 2; ++side) {
            ResetHeap(-1);
            directory[0] = 0x77;
            gimex::Instance *instance = null ? nullptr : static_cast<gimex::Instance *>(FakeAllocate(0x18));
            if (instance != nullptr) {
                std::memset(instance, 0, sizeof(*instance));
                instance->unknownC = written;
                instance->frameCount = 9;
                instance->stream = stream;
                instance->unknown14 = Word(directory);
            }
            gLog.clear();
            result[side] = side == 0 ? ctx.Original<CloseFn>(kWClose)(instance) : gimex::fsh::CloseWriter(instance);
            logs[side] = gLog;
            logs[side].push_back(static_cast<uint32_t>(directory[0]));
        }
        ++ctx.cases;
        if (result[0] != result[1]) ctx.Fail("case %d: original %d, ours %d", i, result[0], result[1]);
        CompareLogs(ctx, "CloseWriter", i, logs[0], logs[1]);
    }
}

// ---- round trips through the real back end ----

namespace {

// A write stream that honours seeks: the directory writer 0x88c719 first writes a placeholder header from an
// uninitialised 16-byte stack buffer, then the shapes, then seeks back to 0 and writes the real header (with the
// stream's Length as the file size). The file compared is the file as positioned, so the placeholder's stack bytes
// (different on each side) are overwritten as in a real file.
struct SeekFile {
    void **vtable;
    Bytes bytes;
    uint32_t pos = 0;
    int stray = 0;
    SeekFile();
    ifaces::cIGZFile *Get() { return reinterpret_cast<ifaces::cIGZFile *>(this); }
};
void *gSeekVTable[kSlots];

uint32_t __fastcall SeekLength(SeekFile *f, void *)
{
    return static_cast<uint32_t>(f->bytes.size());
}
uint32_t __fastcall SeekTo(SeekFile *f, void *, uint32_t position)
{
    if (position > f->bytes.size()) return 0xffffffffu;
    f->pos = position;
    return position;
}
uint8_t __fastcall SeekWrite(SeekFile *f, void *, const void *src, uint32_t *length)
{
    if (f->pos + *length > f->bytes.size()) f->bytes.resize(f->pos + *length);
    if (*length != 0) std::memcpy(f->bytes.data() + f->pos, src, *length);
    f->pos += *length;
    return 1;
}
uint32_t __fastcall SeekStray(SeekFile *f, void *)
{
    ++f->stray;
    return 0;
}

SeekFile::SeekFile() : vtable(gSeekVTable)
{
    for (void *&slot : gSeekVTable) slot = reinterpret_cast<void *>(&SeekStray);
    gSeekVTable[7] = reinterpret_cast<void *>(&SeekLength);
    gSeekVTable[12] = reinterpret_cast<void *>(&SeekTo);
    gSeekVTable[17] = reinterpret_cast<void *>(&SeekWrite);
}

using WOpenFn = int32_t(__stdcall *)(gimex::Instance **, ifaces::cIGZFile *, const char *, int32_t);

struct Frame {
    std::vector<uint8_t> info; // the writer's info (kInfoBytes, so the encoder may read all of it)
    std::vector<uint8_t> pixels;
    int32_t pitch = 0;
};

Frame MakeFrame(std::mt19937 &rng, uint32_t kind)
{
    Frame f;
    f.info.assign(kInfoBytes, 0);
    gimex::Info info{};
    info.size = gimex::kWriteInfoSize;
    info.version = gimex::kWriteInfoVersion;
    info.width = static_cast<int32_t>(1 + rng() % 24);
    info.height = static_cast<int32_t>(1 + rng() % 24);
    // The GM compressor (kind 4) takes sizes that are multiples of 16 (its own test pins it so).
    if (kind == 4) {
        info.width = static_cast<int32_t>(16 * (1 + rng() % 2));
        info.height = static_cast<int32_t>(16 * (1 + rng() % 2));
    }
    info.bitsPerPixel = 32;
    info.sourceBits = static_cast<int32_t>(Pick(rng, {4, 8, 15, 16, 24, 32}));
    info.jpegFlag = kind;
    info.quality = rng() % 101;
    info.alphaBits = Pick(rng, {0, 1, 4, 8});
    info.redBits = Pick(rng, {5, 8});
    info.greenBits = Pick(rng, {5, 6, 8});
    info.blueBits = Pick(rng, {5, 8});
    for (int i = 0; i < 8; ++i)
        info.unknown45c[i] = rng() % 3 ? static_cast<uint8_t>('a' + rng() % 26) : static_cast<uint8_t>(rng() % 2 ? 0 : ' ');
    std::memcpy(f.info.data(), &info, sizeof(info));
    f.pitch = info.width * 4 + static_cast<int32_t>(4 * (rng() % 3));
    f.pixels.resize(static_cast<size_t>(f.pitch) * static_cast<size_t>(info.height));
    if (kind >= 3) {
        // The block compressors' colour encoders read uninitialised stack tables of the original for blocks with several colours and some alpha
        // (lane MS): the block kinds get quiet images, flat and opaque.
        const uint32_t base = (rng() & 0x00ffffffu) | 0xff000000u;
        for (size_t k = 0; k + 4 <= f.pixels.size(); k += 4) std::memcpy(&f.pixels[k], &base, 4);
    } else {
        for (uint8_t &b : f.pixels) b = static_cast<uint8_t>(rng());
    }
    return f;
}

struct ReadBack {
    int32_t open = 0;
    uint32_t frames = 0;
    std::vector<std::vector<uint8_t>> infos;
    std::vector<int32_t> reads;
    std::vector<std::vector<uint8_t>> pixels;
    int32_t close = 0;
    uint32_t pos = 0;
    int stray = 0;
};

// Each side reads `file` back through its own open / info / read / close.
ReadBack Read(difftest::Context &ctx, int side, const Bytes &file)
{
    ReadBack r;
    Stream stream(file);
    gimex::Instance *instance = nullptr;
    r.open = side == 0 ? ctx.Original<OpenFn>(kOpen)(&instance, stream.Get(), "x.fsh", 0)
                       : gimex::fsh::Open(&instance, stream.Get(), "x.fsh", 0);
    if (instance != nullptr) {
        r.frames = instance->frameCount;
        for (int32_t frame = 0; frame < static_cast<int32_t>(instance->frameCount) + 1; ++frame) {
            gimex::Info *info = side == 0 ? ctx.Original<InfoFn>(kInfo)(instance, frame) : gimex::fsh::GetInfo(instance, frame);
            if (info == nullptr) {
                r.infos.emplace_back();
                continue;
            }
            const uint8_t *p = reinterpret_cast<const uint8_t *>(info);
            r.infos.emplace_back(p, p + kInfoBytes);
            if (info->width <= 0 || info->height <= 0 || info->width > 0x400 || info->height > 0x400) {
                r.reads.push_back(-999); // a damaged size: not decoded (the infos are compared)
                FakeRelease(info);
                continue;
            }
            const int32_t bytes = info->bitsPerPixel > 8 ? 4 : 1;
            const int32_t pitch = info->width * bytes + 8;
            std::vector<uint8_t> pixels(static_cast<size_t>(pitch) * static_cast<size_t>(info->height) + 0x100, 0xcd);
            r.reads.push_back(side == 0 ? ctx.Original<ReadFn>(kRead)(instance, info, pixels.data(), pitch)
                                        : gimex::fsh::ReadFrame(instance, info, pixels.data(), pitch));
            r.pixels.push_back(std::move(pixels));
            FakeRelease(info);
        }
        r.close = side == 0 ? ctx.Original<CloseFn>(kClose)(instance) : gimex::fsh::Close(instance);
    }
    r.pos = stream.file.pos;
    r.stray = stream.file.stray;
    return r;
}

uint32_t Live()
{
    uint32_t n = 0;
    for (void *block : gHeap.blocks) n += block != nullptr;
    return n;
}

uint32_t gRoundKind = 0;

void CompareReads(difftest::Context &ctx, int round, const char *what, const ReadBack &a, const ReadBack &b)
{
    if (a.open != b.open || a.frames != b.frames || a.close != b.close || a.pos != b.pos || a.stray != b.stray ||
        a.reads != b.reads || a.infos.size() != b.infos.size()) {
        ctx.Fail("round %d %s: original open %d frames %u reads %zu close %d pos %u stray %d, ours open %d frames %u "
                 "reads %zu close %d pos %u stray %d",
                 round, what, a.open, a.frames, a.reads.size(), a.close, a.pos, a.stray, b.open, b.frames, b.reads.size(),
                 b.close, b.pos, b.stray);
        return;
    }
    for (size_t f = 0; f < a.infos.size(); ++f) {
        if (a.infos[f] == b.infos[f]) continue;
        size_t at = 0;
        while (at < a.infos[f].size() && at < b.infos[f].size() && a.infos[f][at] == b.infos[f][at]) ++at;
        ctx.Fail("round %d %s frame %zu: the infos differ first at +0x%zx", round, what, f, at);
    }
    if (a.pixels != b.pixels) {
        size_t frame = 0, at = 0;
        while (frame < a.pixels.size() && frame < b.pixels.size() && a.pixels[frame] == b.pixels[frame]) ++frame;
        if (frame < a.pixels.size() && frame < b.pixels.size())
            while (at < a.pixels[frame].size() && a.pixels[frame][at] == b.pixels[frame][at]) ++at;
        ctx.Fail("round %d (kind %u) %s: the decoded pixels differ (frame %zu, first at %zu of %zu: %02x / %02x)", round, gRoundKind, what, frame, at,
                 frame < a.pixels.size() ? a.pixels[frame].size() : 0, frame < a.pixels.size() && at < a.pixels[frame].size() ? a.pixels[frame][at] : 0,
                 frame < b.pixels.size() && at < b.pixels[frame].size() ? b.pixels[frame][at] : 0);
    }
}

} // namespace

// Written by each side's wopen / write / wclose, read back by each side's open / info / read / close, also cut short
// and damaged. The writer's back end (shape encoder, directory writer, compressor) and the reader's are ours on our side, the original on the other.
DIFF_TEST(Gimex_FshRoundTrip, 0x87eb94)
{
    HeapFakes heap;
    // Reaches wopen 0x87eb94, write 0x87ebf5, wclose 0x87eb4b, open 0x87ea9f, info 0x87dc98, read 0x87eb11 and
    // close 0x87dc79 on each side.
    std::mt19937 rng(0x87ebf5eb);
    int decoded = 0, readOk = 0, skippedPixels = 0;
    for (int round = 0; round < 210; ++round) {
        const int32_t frames = static_cast<int32_t>(1 + rng() % 3);
        const int32_t writes = static_cast<int32_t>(rng() % 4 ? frames : frames + 1); // one too many sometimes
        const uint32_t kind = Pick(rng, {0, 0, 1, 2, 3, 4, 5});
        gRoundKind = kind;
        std::vector<Frame> input;
        for (int32_t i = 0; i < writes; ++i) input.push_back(MakeFrame(rng, kind));
        Bytes written[2];
        int32_t results[2][8] = {};
        std::vector<uint32_t> logs[2];
        uint32_t live[2];
        for (int side = 0; side < 2; ++side) {
            ResetHeap(-1);
            SeekFile stream;
            gimex::Instance *instance = nullptr;
            results[side][0] = side == 0 ? ctx.Original<WOpenFn>(kWOpen)(&instance, stream.Get(), "x.fsh", frames)
                                         : gimex::fsh::OpenWriter(&instance, stream.Get(), "x.fsh", frames);
            for (int32_t i = 0; i < writes; ++i) {
                const auto *info = reinterpret_cast<const gimex::Info *>(input[i].info.data());
                results[side][1 + i] = side == 0 ? ctx.Original<WriteFn>(kWrite)(instance, info, input[i].pixels.data(),
                                                                                  input[i].pitch)
                                                 : gimex::fsh::WriteFrame(instance, info, input[i].pixels.data(),
                                                                          input[i].pitch);
            }
            results[side][7] = side == 0 ? ctx.Original<CloseFn>(kWClose)(instance) : gimex::fsh::CloseWriter(instance);
            written[side] = stream.bytes;
            logs[side] = gLog;
            logs[side].push_back(stream.pos);
            logs[side].push_back(static_cast<uint32_t>(stream.stray));
            live[side] = Live();
        }
        ++ctx.cases;
        if (std::memcmp(results[0], results[1], sizeof(results[0])) != 0 || live[0] != live[1])
            ctx.Fail("round %d (%d frames, %d writes, kind %u): the writers' results or blocks left differ", round, frames,
                     writes, kind);
        if (written[0] != written[1]) {
            size_t at = 0;
            while (at < written[0].size() && at < written[1].size() && written[0][at] == written[1][at]) ++at;
            ctx.Fail("round %d: the files written differ (original %zu bytes, ours %zu) first at 0x%zx (%02x / %02x)",
                     round, written[0].size(), written[1].size(), at, at < written[0].size() ? written[0][at] : 0,
                     at < written[1].size() ? written[1][at] : 0);
        }
        CompareLogs(ctx, "RoundTrip write", round, logs[0], logs[1]);

        // Read back: the file as written, then with bytes after it, then with damaged names. Not cut short and no
        // damaged offsets, sizes or shapes: see the top. The file: 'ShpF', its size, the count (big-endian), a word;
        // then per entry its offset and size (big-endian) and a NUL-terminated name.
        std::vector<size_t> names; // the name bytes' positions (the NULs left alone)
        if (written[0].size() >= 0x10) {
            const uint32_t count = static_cast<uint32_t>(written[0][8]) << 24 | written[0][9] << 16 | written[0][10] << 8 |
                                   written[0][11];
            size_t at = 0x10;
            for (uint32_t e = 0; e < count && at + 8 < written[0].size(); ++e) {
                at += 8;
                while (at < written[0].size() && written[0][at] != 0) names.push_back(at++);
                ++at;
            }
        }
        for (int variant = 0; variant < 3 && written[0].size() > 0x10; ++variant) {
            Bytes file = written[0];
            if (variant == 1) RandomBytes(rng, file, 1 + rng() % 0x40);
            if (variant == 2) {
                for (size_t k : names)
                    if (rng() % 2) file[k] = static_cast<uint8_t>(rng() % 3 ? ' ' : 0x21 + rng() % 0xde);
            }
            ReadBack back[2];
            for (int side = 0; side < 2; ++side) {
                ResetHeap(-1);
                back[side] = Read(ctx, side, file);
                logs[side] = gLog;
                live[side] = Live();
            }
            // The decoders of kinds 4 (GM) and 5 (PS2 blocks) leave some of what they write to uninitialised stack on both sides (the last row's
            // padding of a GM frame; some pixels of a block frame; reading one file twice on one side gives different bytes there, with the stack
            // cleared differently or not), so the decoded pixels of those two kinds are not compared: the decoders have their own tests
            // (tests/gz_gimex_fsh_gm.cpp, gz_gimex_fsh_decode_*.cpp), and what this test pins for them is the file written (compared below), the
            // infos read back and the results, the logs and the blocks left.
            if (kind == 4 || kind == 5) {
                for (ReadBack &r : back) r.pixels.clear();
                ++skippedPixels;
            }
            ++ctx.cases;
            const char *what = variant == 0 ? "read" : variant == 1 ? "longer" : "names damaged";
            CompareReads(ctx, round, what, back[0], back[1]);
            for (size_t f = 0; f < back[0].infos.size(); ++f) decoded += !back[0].infos[f].empty();
            for (int32_t read : back[0].reads) readOk += read > 0;
            if (live[0] != live[1]) ctx.Fail("round %d %s: blocks left %u vs %u", round, what, live[0], live[1]);
            CompareLogs(ctx, what, round, logs[0], logs[1]);
        }
    }
    // The comparison means something only if frames came back.
    if (decoded < 200 || readOk < 200) ctx.Fail("only %d infos and %d decoded frames came back", decoded, readOk);
    if (skippedPixels > 300) ctx.Fail("%d of the reads left their pixels out of the comparison; too many", skippedPixels);
}
