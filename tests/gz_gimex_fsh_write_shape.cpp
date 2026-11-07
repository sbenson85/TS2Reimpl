// Differential tests for the top of the FSH back end's encode side (lane MZ; src/gz/GimexFshWriteShape.size.cpp and
// GimexFshWriteDirectory.size.cpp: the directory writer 0x88c719, the shape compressor 0x88aa6e and the shape encoder 0x88905f). Each runs alone over
// seeded and crafted input, the original (by RVA, in the mapped image) against ours, the stack cleared before each call (alternately 0x00 and 0xff),
// comparing the return value, every byte written (the output buffer whole, the stream's bytes and its call log, the directory entry whole) and the
// allocator's effect (blocks left, sizes of the blocks freed).
//
// Inputs kept out, and why:
// - The directory writer's placeholder header: its first write (layout 0) is 16 bytes of the original's uninitialised stack, written before the table
//   (0x88c719 passes a local buffer it never fills to the stream); when the writing succeeds the header is written again over it, and the comparison
//   covers those 16 bytes then. When a write fails first they stay the stack's, so the first 16 bytes are left out of that comparison (the log still
//   has that write's position and length).
// - Block-compressed images (kinds 3 and 5) are quiet (flat, opaque): the colour encoders read uninitialised stack tables of the original for blocks
//   with several colours and some alpha (lane MS, tests/gz_gimex_fsh_encode_colour.cpp).
// - The GM compressor (kind 4) gets sizes that are multiples of 16 (as its own test: lane MY).
// - The encoder (MakeSource, `full`): a platform outside 1..9 and a kind outside 0..5 only with no shape built (the original sizes the shape buffer by
//   the record's bit count, which those leave at 0, and overflows it); indexed frames under the block kinds 3 and 5, and a source of 0 bits (or of 1
//   bit on platform 4), give the original's pixel packer (0x88bd83) a clear sized for another record type than the buffer: kept off those inputs. The
//   page-order surfaces of platforms 5 and 6 are 128 or 256 wide and high (ReorderImage writes past its buffer for any other size, as the
//   PackShapePixels test knows). Overflows that still happen (51 of 12000 frames) land in the slack the test allocator leaves after every block, and
//   the slack is compared.
// - The game's platform is always 2; the other platforms are covered because the code is there and the original's behaviour on them is a fact of
//   the binary, but they are not what the game runs.
#define NOMINMAX
#include <windows.h>
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "shape.h"
#include "gz/Gimex.h"
#include "gz/GimexFshBlocks.h"
#include "gz/GimexFshConvert.h"
#include "gz/GimexFshDecode.h"
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

constexpr uint32_t kWriteDirectoryRva = 0x88c719;
constexpr uint32_t kCompressShapeRva = 0x88aa6e;
constexpr uint32_t kEncodeShapeRva = 0x88905f;
constexpr size_t kInfoBytes = 0x2b00;
constexpr uint32_t kAllocateRva = 0x87da65;

// The codecs' allocator, filling what it hands out with 0xa5: the original's compressor copies the RefPack buffer to the 16-byte boundary past the
// stream it holds (a memcpy of the rounded count from a block it never cleared), and the original's heap garbage and ours would differ there; with
// both allocators patched to this one, the bytes are the same on both sides and the whole record is compared.
//
// Every block comes with a slack of kSlack bytes after it, filled with 0xcd: the original's pixel packer clears an area sized for another record
// type than the buffer it was given for some inputs (ORIGINAL-BUG: a heap overflow of up to 64 KB), which would corrupt the heap and kill the
// test. The slack takes the overflow, and the bytes that landed in it are compared between the two sides along with everything else.
constexpr uint32_t kSlack = 0x20000;
const uint8_t *gBlockPtr[64];
uint32_t gBlockSize[64];
int gBlockCount = 0;
void *__stdcall FillAllocate(uint32_t size)
{
    uint8_t *block = static_cast<uint8_t *>(::operator new[](size + kSlack));
    std::memset(block, 0xa5, size);
    std::memset(block + size, 0xcd, kSlack);
    if (gBlockCount < 64) {
        gBlockPtr[gBlockCount] = block;
        gBlockSize[gBlockCount++] = size;
    }
    return block;
}

// The slack of the newest block handed out at `block` (what the call wrote past its end, up to kSlack bytes); the list of blocks starts over.
std::vector<uint8_t> TakeSlack(const void *block)
{
    std::vector<uint8_t> slack;
    for (int n = gBlockCount - 1; n >= 0 && slack.empty(); --n)
        if (gBlockPtr[n] == block) slack.assign(gBlockPtr[n] + gBlockSize[n], gBlockPtr[n] + gBlockSize[n] + kSlack);
    return slack;
}
void ForgetBlocks() { gBlockCount = 0; }

// A call under structured exception handling: the original faults on some inputs (it reads or writes far outside what it was given) and the
// answer is then "faulted", not a dead test.
using EncodeFn = int32_t(__cdecl *)(Directory *, const Info *, const uint8_t *, int32_t, int32_t, int32_t);
int32_t GuardedCall(EncodeFn fn, bool *faulted, Directory *d, const Info *i, const uint8_t *p, int32_t pitch, int32_t index, int32_t platform)
{
    __try {
        return fn(d, i, p, pitch, index, platform);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *faulted = true;
        return -99;
    }
}

struct FillAllocators {
    shape::CodePatch original{kAllocateRva, reinterpret_cast<const void *>(&FillAllocate)};
    shape::CodePatch ours{reinterpret_cast<void *>(&gimex::Allocate), reinterpret_cast<const void *>(&FillAllocate)};
};

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
    template <typename T, size_t N> T Pick(const T (&pool)[N]) { return pool[Next() % N]; }
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

void SetWord(std::vector<uint8_t> &info, uint32_t offset, uint32_t value)
{
    std::memcpy(info.data() + offset, &value, 4);
}

uint32_t GetWord(const std::vector<uint8_t> &info, uint32_t offset)
{
    uint32_t value;
    std::memcpy(&value, info.data() + offset, 4);
    return value;
}

// ---- directories ----

struct EntrySpec {
    std::vector<uint8_t> shape, palette, marker, userData, pairs, text;
    std::string name;
    bool hasName = false;
    uint32_t flags = 0;
    uint32_t frameDepth = 0;
};

uint8_t *Block(const std::vector<uint8_t> &bytes)
{
    if (bytes.empty()) return nullptr;
    uint8_t *p = static_cast<uint8_t *>(gimex::Allocate(static_cast<uint32_t>(bytes.size())));
    std::memcpy(p, bytes.data(), bytes.size());
    return p;
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
        e.shapeBytes = static_cast<uint32_t>(s.shape.size());
        e.palette = Block(s.palette);
        e.paletteBytes = static_cast<uint32_t>(s.palette.size());
        e.marker = Block(s.marker);
        e.markerBytes = static_cast<uint32_t>(s.marker.size());
        e.userData = Block(s.userData);
        e.userDataBytes = static_cast<uint32_t>(s.userData.size());
        e.pairs = Block(s.pairs);
        e.pairsBytes = static_cast<uint32_t>(s.pairs.size());
        e.text = Block(s.text);
        e.textBytes = static_cast<uint32_t>(s.text.size());
        if (s.hasName) {
            e.name = static_cast<char *>(gimex::Allocate(static_cast<uint32_t>(s.name.size() + 1)));
            std::memcpy(e.name, s.name.c_str(), s.name.size() + 1);
        }
        e.flags = s.flags;
        e.kind = 0x1234 + static_cast<uint32_t>(i);
        e.imageBytes = 0x777 + static_cast<uint32_t>(i);
        e.tag = 0x3131 + static_cast<uint32_t>(i);
        e.frameDepth = s.frameDepth;
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

// ---- a recording write stream ----

void *gWriteVTable[64];

struct RecFile {
    void **vtable;
    std::vector<uint8_t> bytes;
    uint32_t pos = 0;
    int writes = 0;
    int failAt = -1;
    bool partial = false;
    int headerRewrites = 0;
    std::vector<uint32_t> log; // per write: position, requested length, a hash of the bytes; per seek: a marker and the position
    bool skipFirstHash = false;
    RecFile();
    ifaces::cIGZFile *Get() { return reinterpret_cast<ifaces::cIGZFile *>(this); }
};

uint32_t __fastcall RecFileLength(RecFile *f, void *)
{
    f->log.push_back(0x4c454e47);
    return static_cast<uint32_t>(f->bytes.size());
}
uint32_t __fastcall RecFileSeek(RecFile *f, void *, uint32_t position)
{
    f->log.push_back(0x5345454b);
    f->log.push_back(position);
    if (position > f->bytes.size()) return 0xffffffffu;
    f->pos = position;
    return position;
}
uint8_t __fastcall RecFileWrite(RecFile *f, void *, const void *src, uint32_t *length)
{
    const int index = f->writes++;
    uint32_t hash = 2166136261u;
    if (!(f->skipFirstHash && index == 0))
        for (uint32_t k = 0; k < *length; ++k) hash = (hash ^ static_cast<const uint8_t *>(src)[k]) * 16777619u;
    f->log.push_back(f->pos);
    f->log.push_back(*length);
    f->log.push_back(hash);
    if (index == f->failAt) {
        *length = f->partial && *length != 0 ? *length - 1 : 0;
        return 1;
    }
    if (f->pos == 0 && *length == 16 && index > 0) ++f->headerRewrites;
    if (f->pos + *length > f->bytes.size()) f->bytes.resize(f->pos + *length);
    if (*length != 0) std::memcpy(f->bytes.data() + f->pos, src, *length);
    f->pos += *length;
    return 1;
}
uint32_t __fastcall RecFileStray(RecFile *f, void *)
{
    f->log.push_back(0x53545259);
    return 0;
}

RecFile::RecFile() : vtable(gWriteVTable)
{
    for (void *&slot : gWriteVTable) slot = reinterpret_cast<void *>(&RecFileStray);
    gWriteVTable[7] = reinterpret_cast<void *>(&RecFileLength);
    gWriteVTable[12] = reinterpret_cast<void *>(&RecFileSeek);
    gWriteVTable[17] = reinterpret_cast<void *>(&RecFileWrite);
}

// ---- the shape info and pixel generators ----

struct Source {
    std::vector<uint8_t> info;
    std::vector<uint8_t> pixels;
    int32_t stride = 0;
};

// An info the encoder and compressor read. `blocks` makes the pixels quiet (flat, opaque), as the block compressor's colour encoders need.
Source MakeSource(Fill &fill, int iter, bool full, int32_t platform)
{
    Source s;
    s.info.assign(kInfoBytes, 0);
    fill.Bytes(s.info);
    const int32_t kinds[] = {0, 0, 1, 2, 2, 3, 3, 4, 5, 5, 6, -1, 0, 3};
    int32_t kind = fill.Pick(kinds);
    int32_t width, height;
    const int32_t sizes[] = {16, 32, 64, 8, 4};
    if (kind == 4) {
        width = 16 * fill.Range(1, 3);
        height = 16 * fill.Range(1, 3);
    } else if (fill.Chance(30)) {
        width = fill.Pick(sizes);
        height = fill.Pick(sizes);
    } else {
        width = fill.Range(1, 40);
        height = fill.Range(1, 40);
    }
    SetWord(s.info, 0x10, static_cast<uint32_t>(width));
    SetWord(s.info, 0x14, static_cast<uint32_t>(height));
    const int32_t depths[] = {8, 32, 32, 24, 16, 8, 32};
    const int32_t bpp = fill.Pick(depths);
    // The block kinds (3, 5) are the true-colour ones: an indexed frame under them makes the original clear a pixel area sized for 16 bits in a buffer
    // sized for fewer (PackShapePixels, 0x88bd83) and overflow it, so the encoder test gives indexed frames a plain kind.
    if (full && (kind == 3 || kind == 5) && bpp == 8) kind = 0;
    SetWord(s.info, 0x18, static_cast<uint32_t>(bpp));
    const int32_t sourceBits[] = {1, 4, 5, 8, 9, 15, 16, 17, 19, 20, 24, 25, 32, 8, 32, 16, 0};
    int32_t sourceChoice = fill.Pick(sourceBits);
    // Platform 4 makes a 1-bit record of a source of one bit or none, a type its pixel packer does not size (it clears the area of a 4-bit one and
    // the original's buffer is the 1-bit one's: the same overflow as the 1-bit type in ReorderImage). The encoder test keeps sources off it.
    if (full && platform == 4 && sourceChoice <= 1) sourceChoice = 4;
    // A source of no bits at all is no image (the game's smallest is 1): the other platforms' packers size it the same unusable way.
    if (full && sourceChoice == 0) sourceChoice = 8;
    SetWord(s.info, 0x1c, static_cast<uint32_t>(sourceChoice));
    SetWord(s.info, 0x42c, static_cast<uint32_t>(kind));
    // The first palette entry (info + 0x20): the original's encoder runs IsGreyImage over the palette from it (and ignores the answer; ours doesn't
    // call it), which faults when this is garbage. It is the entry the palette starts at, so small.
    {
        const uint32_t firsts[] = {0, 0, 0, 1, 5};
        SetWord(s.info, 0x20, fill.Pick(firsts));
    }
    const int32_t qualityPool[] = {-50, 0, 10, 25, 50, 75, 90, 99, 100, 101, 120, 1000};
    SetWord(s.info, 0x430, static_cast<uint32_t>(fill.Pick(qualityPool)));
    uint32_t variant = 0;
    const uint32_t bitsPool[] = {1, 2, 4, 8, 0x2000, 0x100000, 0x400000, 0x1000, 0x1000000, 0x2000000, 0x4000000, 0x20000, 0x10000, 0x40000, 0x200000, 0x8000};
    for (const uint32_t bit : bitsPool) {
        // The no-shape bit stays off for the compressor (it is the encoder's) and mostly off for the encoder.
        const int32_t chance = bit == 2 ? (full ? 6 : 0) : (bit == 0x2000 ? 35 : 25);
        if (fill.Chance(chance)) variant |= bit;
    }
    // A kind past the five the formats define (6, -1) is only compared with no shape built (the original sizes and fills such a frame as it likes).
    if (full && (kind < 0 || kind > 5)) variant |= 2;
    SetWord(s.info, 0x428, variant);
    // The PS2 reorder lays pixels out in 128-pixel pages: a surface of any other size (the 16-pixel steps PageSize rounds to) makes the original's
    // ReorderImage write past its buffer (the same inputs the PackShapePixels test keeps out; ORIGINAL-BUG class), so the page-order frames of
    // platform 6 are 128 or 256 wide and high. (The game's platform is always 2.)
    if ((platform == 6 || platform == 5) && (variant & 0x2000) != 0 && kind != 4) {
        width = 128 << fill.Range(0, 1);
        height = 128 << fill.Range(0, 1);
        SetWord(s.info, 0x10, static_cast<uint32_t>(width));
        SetWord(s.info, 0x14, static_cast<uint32_t>(height));
    }
    const int32_t bitsPoolCh[] = {0, 1, 2, 3, 4, 5, 6, 8, 10};
    for (const uint32_t offset : {0x438u, 0x43cu, 0x440u, 0x444u}) SetWord(s.info, offset, static_cast<uint32_t>(fill.Pick(bitsPoolCh)));
    if (fill.Chance(55)) {
        const int32_t combos[][4] = {{8, 8, 8, 8}, {0, 8, 8, 8}, {0, 5, 6, 5}, {1, 5, 5, 5}, {4, 4, 4, 4}, {0, 5, 5, 5}, {0, 6, 5, 5}, {8, 5, 6, 5}, {2, 10, 10, 10},
                                     {6, 6, 6, 6}, {0, 3, 3, 2}, {0, 2, 8, 2}, {0, 4, 8, 4}, {8, 8, 0, 0}, {8, 0, 0, 0}, {5, 8, 0, 0}, {5, 0, 8, 0}, {5, 0, 0, 8},
                                     {5, 0, 0, 0}, {3, 4, 0, 0}, {4, 0, 0, 0}, {3, 0, 0, 0}, {8, 0, 8, 0}, {8, 0, 0, 8}, {6, 8, 8, 8}, {2, 5, 6, 5}, {3, 5, 6, 5}};
        const int32_t *c = combos[fill.Next() % (sizeof combos / sizeof combos[0])];
        for (int k = 0; k < 4; ++k) SetWord(s.info, 0x438u + 4u * static_cast<uint32_t>(k), static_cast<uint32_t>(c[k]));
    }
    const int32_t origins[] = {0, 1, -1, 5, -7, 100, 2047, -2048, 4095, 0x12345, -0x10000};
    SetWord(s.info, 0x450, static_cast<uint32_t>(fill.Pick(origins)));
    SetWord(s.info, 0x454, static_cast<uint32_t>(fill.Pick(origins)));
    if (full) {
        // The encoder's own reads: the palette count, the name, the text, the pairs and the user data.
        SetWord(s.info, 0x24, (iter % 7 == 0) ? static_cast<uint32_t>(fill.Range(-2, 3)) : static_cast<uint32_t>(fill.Range(1, 300)));
        const int32_t length = fill.Chance(15) ? 0 : fill.Range(1, 12);
        for (int32_t k = 0; k < length; ++k) s.info[0x45c + static_cast<size_t>(k)] = static_cast<uint8_t>(fill.Chance(15) ? ' ' : fill.Range(1, 255));
        s.info[0x45c + static_cast<size_t>(length)] = 0;
        const int32_t textLength = fill.Chance(30) ? 0 : fill.Range(1, 40);
        for (int32_t k = 0; k < textLength; ++k) s.info[0x65c + static_cast<size_t>(k)] = static_cast<uint8_t>(fill.Range(1, 255));
        s.info[0x65c + static_cast<size_t>(textLength)] = 0;
        const int32_t pairCounts[] = {0, 0, 1, 2, 3, 10};
        SetWord(s.info, 0x458, static_cast<uint32_t>(fill.Pick(pairCounts)));
        const uint32_t infoSizes[] = {0x2a70, 0x2a7b, 0x2a7c, 0x2a7c, 0x2a80, 0x100};
        SetWord(s.info, 4, fill.Pick(infoSizes));
        SetWord(s.info, 0, fill.Chance(70) ? 0x53687046u : fill.Next());
        const int32_t userCounts[] = {0, 1, 5, 21, 22, 40, 100, 400, -1};
        SetWord(s.info, 0x2a78, static_cast<uint32_t>(fill.Pick(userCounts)));
        const uint32_t depthWords[] = {0, 0, 1, 2, 4, 16, 0xffffffffu};
        SetWord(s.info, 0x2a70, fill.Pick(depthWords));
        if (fill.Chance(25)) {
            // A grey ramp palette.
            for (size_t k = 0; k < 256; ++k) s.info[0x28 + 4 * k] = s.info[0x29 + 4 * k] = s.info[0x2a + 4 * k] = static_cast<uint8_t>(k);
        }
    }
    // The pixels: quiet for the block kinds, random (some flat) otherwise.
    const bool indexed = bpp == 8;
    const int32_t pixelBytes = indexed ? 1 : 4;
    s.stride = width * pixelBytes + pixelBytes * fill.Range(0, 3);
    s.pixels.assign(static_cast<size_t>(s.stride) * static_cast<size_t>(std::max(height, 128) * 3 + 40) + 65536, 0);
    if (kind == 3 || kind == 5 || fill.Chance(15)) {
        const uint32_t base = fill.Next() | 0xff000000u;
        for (size_t k = 0; k + 4 <= s.pixels.size(); k += 4) std::memcpy(&s.pixels[k], &base, 4);
    } else {
        fill.Bytes(s.pixels);
        if (fill.Chance(30)) {
            for (size_t k = 0; k < s.pixels.size(); ++k) s.pixels[k] = (s.pixels[k] & 0x0f) ? s.pixels[k] : static_cast<uint8_t>(fill.Chance(50) ? 0xff : 0);
        }
    }
    return s;
}

const int32_t kPlatformPool[] = {2, 2, 2, 5, 5, 6, 6, 8, 8, 1, 3, 4, 7, 9, 0, 10, -1};

} // namespace

// ---- the directory writer ----

DIFF_TEST(GimexFshWrite_WriteDirectory, kWriteDirectoryRva)
{
    using Fn = uint32_t(__cdecl *)(ifaces::cIGZFile *, Directory *, int32_t, int32_t, int32_t, uint32_t, uint32_t, uint32_t, uint32_t, int32_t, uint32_t);
    const Fn original = ctx.Original<Fn>(kWriteDirectoryRva);
    Fill fill(0xb001);
    // The writer's filler blocks (before a record, to reach the alignment) are cleared by the original and then hold "Buy ERTS": handing out 0xa5 on
    // both sides shows a side that leaves them uncleared.
    FillAllocators fills;
    int succeeded = 0, failed = 0, rewrites = 0, fillers = 0, layouts[3] = {};
    const uint32_t masks[] = {0, 1, 3, 7, 15, 15, 15, 31, 63, 5, 12};
    const int32_t layoutPool[] = {0, 0, 0, 0, 1, 1, 2, -1};
    for (int i = 0; i < 12000; ++i) {
        std::vector<EntrySpec> specs(static_cast<size_t>(fill.Range(0, 4)));
        const int32_t layout = fill.Pick(layoutPool);
        const int32_t headers = fill.Chance(80) ? 1 : 0;
        for (EntrySpec &s : specs) {
            // Shapes hold at least a header and the four bytes after it, which RecordCodec reads (the codec of a coded record is told by them): a
            // shorter block is read past its end by the original, whose neighbours' garbage then decides.
            s.shape = RandomBytes(fill, static_cast<size_t>(fill.Range(0x30, 120)));
            if (fill.Chance(50)) {
                const uint8_t signatures[][2] = {{0x10, 0xfb}, {0x11, 0xfb}, {0x90, 0xfb}, {0x18, 0xfb}, {0x1a, 0xfb}, {0x1c, 0xfb}, {0x28, 0xfb}, {0x9c, 0xfb}};
                const uint8_t *signature = signatures[fill.Next() % 8];
                s.shape[0x20] = signature[0];
                s.shape[0x21] = signature[1];
                if (fill.Chance(80)) s.shape[1] |= 2, s.shape[3] |= 2;
            }
            if (fill.Chance(10)) s.shape.clear();
            if (fill.Chance(60)) s.palette = RandomBytes(fill, static_cast<size_t>(fill.Range(1, 50)));
            if (fill.Chance(35)) s.marker = RandomBytes(fill, static_cast<size_t>(fill.Range(1, 40)));
            if (fill.Chance(35)) s.userData = RandomBytes(fill, static_cast<size_t>(fill.Range(1, 40)));
            if (fill.Chance(35)) s.pairs = RandomBytes(fill, static_cast<size_t>(fill.Range(1, 40)));
            if (fill.Chance(45)) s.text = RandomBytes(fill, static_cast<size_t>(fill.Range(1, 40)));
            s.hasName = true;
            const size_t nameLength = fill.Chance(10) ? 0 : static_cast<size_t>(fill.Range(1, 12));
            s.name.assign(nameLength, 'n');
            for (char &c : s.name) c = static_cast<char>(fill.Range(0x21, 0x7e));
            s.flags = fill.Next() & ~0x40000u;
            const uint32_t depths[] = {0, 0, 0, 3, 7, 15, 31, 63, 5};
            s.frameDepth = fill.Pick(depths);
        }
        if (!specs.empty() && fill.Chance(30)) specs[0].flags |= 0x200;
        if (layout == 0 && headers != 0)
            for (EntrySpec &s : specs)
                if (s.shape.empty()) s.shape = RandomBytes(fill, 0x30);
        const int32_t platform = fill.Chance(60) ? 2 : fill.Pick(kPlatformPool);
        const uint32_t fourcc = fill.Chance(50) ? 0x53687046u : fill.Next();
        const uint32_t alignBase = fill.Pick(masks), padMask = fill.Pick(masks);
        // The filler block holds align + 1 bytes and the gap written from it is at most the codec alignment for a coded record: a larger one
        // reads past the block (the neighbouring heap decides what the file gets), so the codec alignment stays within every alignment used.
        uint32_t codecAlign = fill.Pick(masks);
        if (codecAlign > alignBase) codecAlign = alignBase;
        for (const EntrySpec &s : specs)
            if (s.frameDepth != 0 && codecAlign > s.frameDepth) codecAlign = s.frameDepth;
        const int32_t cubeAlign = static_cast<int32_t>(fill.Pick(masks));
        const int failAt = fill.Chance(30) ? fill.Range(0, 14) : -1;
        const bool partial = fill.Chance(50);
        RecFile fa, fb;
        for (RecFile *f : {&fa, &fb}) {
            f->failAt = failAt;
            f->partial = partial;
            f->skipFirstHash = layout == 0 && headers != 0;
        }
        Directory *da = Build(specs);
        Directory *db = Build(specs);
        uint32_t ra = 0, rb = 0;
        const HeapEffect a = Observe(StackByte(i), [&] { ra = original(fa.Get(), da, platform, layout, headers, fourcc, alignBase, padMask, 0x1234, cubeAlign, codecAlign); });
        const HeapEffect b = Observe(StackByte(i), [&] { rb = WriteDirectory(fb.Get(), db, platform, layout, headers, fourcc, alignBase, padMask, 0x1234, cubeAlign, codecAlign); });
        std::vector<uint8_t> bytesA = fa.bytes, bytesB = fb.bytes;
        const bool tabled = layout == 0 && headers != 0;
        if (tabled && fa.headerRewrites == 0) {
            for (size_t k = 0; k < 16 && k < bytesA.size(); ++k) bytesA[k] = 0;
            for (size_t k = 0; k < 16 && k < bytesB.size(); ++k) bytesB[k] = 0;
        }
        const std::vector<uint8_t> sa = Serialize(da), sb = Serialize(db);
        size_t firstDiff = 0;
        while (firstDiff < bytesA.size() && firstDiff < bytesB.size() && bytesA[firstDiff] == bytesB[firstDiff]) ++firstDiff;
        CHECK(ctx, ra == rb && a == b && bytesA == bytesB && fa.log == fb.log && sa == sb && fa.pos == fb.pos && fa.headerRewrites == fb.headerRewrites,
              "WriteDirectory iter %d (%zu entries, platform %d, layout %d, headers %d, fail at %d): result %u / %u, heap %s (%lld / %lld blocks), file %zu / %zu bytes "
              "(first difference at %zu), log %s, directory %s",
              i, specs.size(), platform, layout, headers, failAt, ra, rb, a == b ? "equal" : "differ", a.blocks, b.blocks, bytesA.size(), bytesB.size(), firstDiff,
              fa.log == fb.log ? "equal" : "differs", sa == sb ? "equal" : "differs");
        if (failAt < 0 && ra != 0) ++succeeded; else ++failed;
        rewrites += fa.headerRewrites > 0;
        if (layout >= 0 && layout < 3) ++layouts[layout];
        const char filler[] = "Buy ERTS";
        fillers += std::search(bytesA.begin(), bytesA.end(), filler, filler + 8) != bytesA.end();
        FreeWrittenDirectory(da);
        FreeWrittenDirectory(db);
    }
    CHECK(ctx, fillers > 1000, "only %d files had a filler block", fillers);
    CHECK(ctx, succeeded > 2000, "only %d writes succeeded; the test input is too narrow", succeeded);
    CHECK(ctx, failed > 500, "only %d writes failed", failed);
    CHECK(ctx, rewrites > 1500, "only %d headers were rewritten", rewrites);
    CHECK(ctx, layouts[0] > 4000 && layouts[1] > 1500, "layouts %d / %d / %d", layouts[0], layouts[1], layouts[2]);
}

// ---- the compressor ----

DIFF_TEST(GimexFshWrite_CompressShape, kCompressShapeRva)
{
    using Fn = uint32_t(__cdecl *)(uint8_t *, const Info *, int32_t, const uint8_t *, uint32_t, uint32_t, int32_t, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kCompressShapeRva);
    Fill fill(0xb002);
    FillAllocators fills;
    int types[256] = {};
    int refpacked = 0, coded = 0;
    const int32_t bitsPool[] = {1, 4, 8, 12, 15, 16, 16, 24, 32, 32, 8, 16, 0, 20};
    for (int i = 0; i < 12000; ++i) {
        const int32_t platform = fill.Chance(30) ? 2 : fill.Pick(kPlatformPool);
        Source s = MakeSource(fill, i, false, platform);
        int32_t bits = fill.Pick(bitsPool);
        const uint32_t noPack = fill.Chance(20) ? static_cast<uint32_t>(fill.Range(1, 3)) : 0u;
        // The plane selectors come with the bit counts the encoder gives them (a 16-bit record with a 4-bit alpha plane is not one it makes: the
        // original tiles it by the 16-bit page sizes and writes past the surface of the 8-bit record type it picks).
        int32_t aux8 = 0, aux9 = 0;
        if (fill.Chance(20)) {
            const int32_t plans[][3] = {{8, 8, 0}, {8, 0, 4}, {16, 0, 8}, {8, 0, 8}, {8, 0, 9}, {16, 0, 9}};
            const int32_t *plan = plans[fill.Next() % 6];
            bits = plan[0];
            aux8 = plan[1];
            aux9 = plan[2];
        }
        // Room for any surface the pixel formats can lay out (a 32-bit record of the padded size, 4 bytes a pixel, with plenty over).
        const size_t outBytes = std::max<size_t>(65536, static_cast<size_t>(GetWord(s.info, 0x10)) * GetWord(s.info, 0x14) * 16 + 4096);
        std::vector<uint8_t> outA(outBytes, static_cast<uint8_t>(fill.Next())), outB;
        outB = outA;
        uint32_t ra = 0, rb = 0;
        const HeapEffect a = Observe(StackByte(i), [&] {
            ra = original(outA.data(), reinterpret_cast<const Info *>(s.info.data()), bits, s.pixels.data(), static_cast<uint32_t>(s.stride), noPack, platform, aux8, aux9);
        });
        const HeapEffect b = Observe(StackByte(i), [&] {
            rb = CompressShape(outB.data(), reinterpret_cast<const Info *>(s.info.data()), bits, s.pixels.data(), static_cast<uint32_t>(s.stride), noPack, platform, aux8, aux9);
        });
        size_t firstDiff = 0;
        while (firstDiff < outA.size() && outA[firstDiff] == outB[firstDiff]) ++firstDiff;
        CHECK(ctx, ra == rb && a == b && outA == outB,
              "CompressShape iter %d (%u x %u, platform %d, bits %d, kind %d, variant %#x, channels %u/%u/%u/%u, bpp %u, aux %d/%d): result %u / %u, heap %s, bytes %s "
              "(first difference at %zu: %#x / %#x)",
              i, GetWord(s.info, 0x10), GetWord(s.info, 0x14), platform, bits, static_cast<int32_t>(GetWord(s.info, 0x42c)), GetWord(s.info, 0x428),
              GetWord(s.info, 0x438), GetWord(s.info, 0x43c), GetWord(s.info, 0x440), GetWord(s.info, 0x444), GetWord(s.info, 0x18), aux8, aux9, ra, rb,
              a == b ? "equal" : "differ", outA == outB ? "equal" : "differ", firstDiff, firstDiff < outA.size() ? outA[firstDiff] : 0u,
              firstDiff < outA.size() ? outB[firstDiff] : 0u);
        ++types[outA[0]];
        refpacked += a.frees > 0;
        coded += (outA[1] & 2) != 0;
    }
    int distinct = 0;
    for (const int n : types) distinct += n > 0;
    CHECK(ctx, distinct >= 40, "only %d record types were produced; the test input is too narrow", distinct);
    CHECK(ctx, refpacked > 300, "only %d shapes went through RefPack", refpacked);
    CHECK(ctx, coded > 300, "only %d shapes came out coded", coded);
}

// ---- the encoder ----

DIFF_TEST(GimexFshWrite_EncodeShape, kEncodeShapeRva)
{
    using Fn = int32_t(__cdecl *)(Directory *, const Info *, const uint8_t *, int32_t, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kEncodeShapeRva);
    Fill fill(0xb003);
    FillAllocators fills;
    int noShape = 0, withPalette = 0, withName = 0, faulted = 0, overflowed = 0;
    for (int i = 0; i < 12000; ++i) {
        const int32_t platform = fill.Chance(30) ? 2 : fill.Pick(kPlatformPool);
        Source s = MakeSource(fill, i, true, platform);
        const int32_t count = fill.Range(1, 3);
        const int32_t index = fill.Range(0, count - 1);
        std::vector<EntrySpec> specs(static_cast<size_t>(count));
        // A platform that none of the encoder's cases names (0, 10 and up, negative) leaves the record's bit count at 0, so the shape buffer is the
        // 0x20 bytes PlaneBytes(.., 0) rounds to, which the header alone fills; the original then writes the pixels past it (heap corruption: the
        // pixel packing of an image under the platform's default type). Those platforms are only compared with no shape built (the info's flag 2).
        if (platform < 1 || platform > 9) SetWord(s.info, 0x428, GetWord(s.info, 0x428) | 2u);
        Directory *da = Build(specs);
        Directory *db = Build(specs);
        int32_t ra = 0, rb = 0;
        bool faultedA = false, faultedB = false;
        ForgetBlocks();
        const HeapEffect a = Observe(StackByte(i), [&] {
            ra = GuardedCall(original, &faultedA, da, reinterpret_cast<const Info *>(s.info.data()), s.pixels.data(), s.stride, index, platform);
        });
        const std::vector<uint8_t> slackA = TakeSlack(EntriesOf(da)[index].shape);
        // The original faults on a few inputs it cannot take (a pixel packer that clears an area sized for a record type other than the one it was
        // given reaches past the slack); there is nothing to compare on those, and the test requires them to be rare.
        if (faultedA) {
            ++faulted;
            continue;
        }
        ForgetBlocks();
        const HeapEffect b = Observe(StackByte(i), [&] {
            rb = GuardedCall(EncodeShape, &faultedB, db, reinterpret_cast<const Info *>(s.info.data()), s.pixels.data(), s.stride, index, platform);
        });
        const std::vector<uint8_t> slackB = TakeSlack(EntriesOf(db)[index].shape);
        const std::vector<uint8_t> sa = Serialize(da), sb = Serialize(db);
        size_t firstDiff = 0;
        while (firstDiff < sa.size() && firstDiff < sb.size() && sa[firstDiff] == sb[firstDiff]) ++firstDiff;
        // The blocks' sizes: what each side allocated is what it frees.
        const HeapEffect fa = Observe(StackByte(i), [&] { FreeWrittenDirectory(da); });
        const HeapEffect fb = Observe(StackByte(i), [&] { FreeWrittenDirectory(db); });
        CHECK(ctx, !faultedB && ra == rb && a == b && sa == sb && fa == fb && slackA == slackB,
              "EncodeShape iter %d (%u x %u, platform %d, kind %d, variant %#x, source bits %u, channels %u/%u/%u/%u, bpp %u): result %d / %d%s, heap %s (%lld / %lld blocks), "
              "directory %s (first difference at %zu of %zu / %zu), blocks freed %s, bytes past the shape %s",
              i, GetWord(s.info, 0x10), GetWord(s.info, 0x14), platform, static_cast<int32_t>(GetWord(s.info, 0x42c)), GetWord(s.info, 0x428), GetWord(s.info, 0x1c),
              GetWord(s.info, 0x438), GetWord(s.info, 0x43c), GetWord(s.info, 0x440), GetWord(s.info, 0x444), GetWord(s.info, 0x18), ra, rb, faultedB ? " (ours faulted)" : "",
              a == b ? "equal" : "differ", a.blocks, b.blocks, sa == sb ? "equal" : "differs", firstDiff, sa.size(), sb.size(), fa == fb ? "equal" : "differ",
              slackA == slackB ? "equal" : "differ");
        noShape += (GetWord(s.info, 0x428) & 2) != 0;
        withName += GetWord(s.info, 0x45c) != 0;
        withPalette += a.blocks > 3;
        for (const uint8_t byte : slackA)
            if (byte != 0xcd) {
                ++overflowed;
                break;
            }
    }
    CHECK(ctx, faulted == 0, "the original faulted on %d of the inputs", faulted);
    CHECK(ctx, noShape > 200, "only %d frames had no shape", noShape);
    CHECK(ctx, withName > 5000, "only %d frames had a name", withName);
    CHECK(ctx, withPalette > 1000, "only %d frames allocated four or more blocks", withPalette);
    // The inputs that overflow the shape buffer (the original's pixel packer sizes its clear for another record type): the bytes landing in the slack
    // are compared above, so a mirrored overflow is part of what the test pins; some must occur.
    CHECK(ctx, overflowed > 10, "only %d frames overflowed their shape buffer", overflowed);
}
