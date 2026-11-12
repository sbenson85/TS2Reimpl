#pragma once
// Shared by the tests of the FSH back end's decode side (tests/gz_gimex_fsh_decode_*.cpp; lane MX): seeded input, a stack cleaner,
// the recording allocator (the codecs' allocator 0x87da65 / 0x87da72 and ours, patched to one fake) and buffer comparison.
#define NOMINMAX
#include "code_patch.h"
#include "difftest.h"
#include "game/game.h"
#include "gz/GimexFshConvert.h"
#include "gz/GimexFshDecode.h"
#include "shape.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

namespace fshdecode {

constexpr uint32_t kAllocate = 0x87da65;
constexpr uint32_t kFreeMemory = 0x87da72;

using Buf = std::vector<uint8_t>;

// Zeroes a stretch of the stack below the caller, so a function that reads an uninitialised local reads the same on both sides.
__declspec(noinline) inline void ClearStack()
{
    volatile uint8_t scratch[0x4000];
    for (size_t i = 0; i < sizeof(scratch); ++i) scratch[i] = 0;
}

// Bytes the fake allocator keeps past every block.
constexpr uint32_t kSlack = 512;

// Bytes allocated (and filled) past every block, of which kSlack are compared: a test whose input makes the code read well past
// a block (a shape shorter than the image the builder copies) raises it so both sides read the same filler.
inline uint32_t gGuard = kSlack;

inline std::vector<uint32_t> gLog;
inline std::vector<void *> gBlocks;
// Every block handed out since ResetHeap, freed or not (by ordinal - 1). A released block is only filled (0xdd), not
// freed, until ResetHeap (lane RK): with malloc reusing a freed block's address, a pointer left to a freed block took the
// ordinal of whichever later block got its address, on one side and not the other (BuildDirectory's flake), and a read
// of freed memory read the heap's bookkeeping. Now every address means one block on each side.
inline std::vector<void *> gAllBlocks;
inline std::vector<uint32_t> gSizes;
inline int gFailAt = -1;
inline int gCalls = 0;

inline void *__stdcall FakeAllocate(uint32_t size)
{
    const int index = gCalls++;
    gLog.push_back(0x1000000u);
    gLog.push_back(size);
    if (index == gFailAt || size > 0x8000000) return nullptr;
    // kSlack bytes of guard after every block, compared with the block: a small overrun is the same on both sides or a failure
    void *const block = std::malloc(size + gGuard);
    std::memset(block, 0xa5, size + gGuard);
    gBlocks.push_back(block);
    gAllBlocks.push_back(block);
    gSizes.push_back(size);
    return block;
}

inline uint8_t __stdcall FakeRelease(void *block)
{
    uint32_t ordinal = 0;
    for (size_t i = 0; i < gBlocks.size(); ++i)
        if (gBlocks[i] == block && block != nullptr) ordinal = static_cast<uint32_t>(i) + 1;
    gLog.push_back(0x2000000u);
    gLog.push_back(block == nullptr ? 0xffffffffu : ordinal);
    if (ordinal != 0) {
        std::memset(block, 0xdd, gSizes[ordinal - 1] + gGuard);
        gBlocks[ordinal - 1] = nullptr;
    }
    return 1;
}

inline void ResetHeap(int failAt)
{
    for (void *block : gAllBlocks) std::free(block);
    gAllBlocks.clear();
    gBlocks.clear();
    gSizes.clear();
    gLog.clear();
    gCalls = 0;
    gFailAt = failAt;
}

// The ordinal (1-based) of a block the fake handed out, 0 for any other pointer.
inline uint32_t Ordinal(const void *block)
{
    for (size_t i = 0; i < gBlocks.size(); ++i)
        if (gBlocks[i] == block && block != nullptr) return static_cast<uint32_t>(i) + 1;
    return 0;
}

// A word as the snapshots log it: a live block's start as 0x80000000 | its ordinal, a released block's as 0xc0000000 |
// its ordinal (a pointer left to a freed block), anything else as it is.
inline uint32_t BlockWord(uint32_t value)
{
    const void *const p = reinterpret_cast<const void *>(static_cast<uintptr_t>(value));
    if (const uint32_t ordinal = Ordinal(p)) return 0x80000000u | ordinal;
    for (size_t i = 0; i < gAllBlocks.size(); ++i)
        if (gAllBlocks[i] == p && p != nullptr) return 0xc0000000u | static_cast<uint32_t>(i + 1);
    return value;
}

// Word `w` of a directory entry as the snapshots log it: the pointer fields (shape, palette, pairs, text, marker, user
// data, name) through BlockWord, the counts and info words as they are. A count or info word holds the input's random
// bytes on both sides, and through BlockWord it read as a block on the side whose heap happened to put one at that
// address (261111a: BuildDirectory round 3817, entry 7's kind 0x1bfff200, block 25's address on ours in one run).
inline uint32_t EntryWord(const uint32_t *words, size_t w)
{
    static_assert(sizeof(gimex::fsh::Entry) == 0x4c, "the fields below are the entry's");
    const size_t at = w * 4;
    const bool pointer = at == offsetof(gimex::fsh::Entry, shape) || at == offsetof(gimex::fsh::Entry, palette) ||
                         at == offsetof(gimex::fsh::Entry, pairs) || at == offsetof(gimex::fsh::Entry, text) ||
                         at == offsetof(gimex::fsh::Entry, marker) || at == offsetof(gimex::fsh::Entry, userData) ||
                         at == offsetof(gimex::fsh::Entry, name);
    return pointer ? BlockWord(words[w]) : words[w];
}

struct HeapFakes {
    shape::CodePatch allocate{kAllocate, reinterpret_cast<const void *>(&FakeAllocate)};
    shape::CodePatch release{kFreeMemory, reinterpret_cast<const void *>(&FakeRelease)};
    shape::CodePatch ourAllocate{reinterpret_cast<void *>(&gimex::Allocate), reinterpret_cast<const void *>(&FakeAllocate)};
    shape::CodePatch ourRelease{reinterpret_cast<void *>(&gimex::FreeMemory), reinterpret_cast<const void *>(&FakeRelease)};
    ~HeapFakes() { ResetHeap(-1); }
};

inline bool CompareLogs(difftest::Context &ctx, const char *what, int index, const std::vector<uint32_t> &a,
                        const std::vector<uint32_t> &b)
{
    if (a == b) return true;
    ctx.Fail("%s case %d: the allocator logs differ (original %zu words, ours %zu)", what, index, a.size(), b.size());
    for (size_t i = 0; i < a.size() || i < b.size(); ++i) {
        const uint32_t x = i < a.size() ? a[i] : 0xdeadbeef;
        const uint32_t y = i < b.size() ? b[i] : 0xdeadbeef;
        if (x != y) {
            ctx.Fail("  word %zu: original %08x, ours %08x", i, x, y);
            break;
        }
    }
    return false;
}

inline bool CompareBytes(difftest::Context &ctx, const char *what, int index, const uint8_t *a, const uint8_t *b, size_t count)
{
    if (std::memcmp(a, b, count) == 0) return true;
    size_t first = 0;
    while (a[first] == b[first]) ++first;
    ctx.Fail("%s case %d: bytes differ first at +0x%zx (original %02x, ours %02x)", what, index, first, a[first], b[first]);
    return false;
}

inline void Put(uint8_t *at, uint32_t value, int bytes, bool big)
{
    for (int i = 0; i < bytes; ++i) at[big ? bytes - 1 - i : i] = static_cast<uint8_t>(value >> (8 * i));
}

inline uint32_t Pick(std::mt19937 &rng, std::initializer_list<int64_t> values)
{
    return static_cast<uint32_t>(values.begin()[rng() % values.size()]);
}

inline void RandomFill(std::mt19937 &rng, uint8_t *bytes, size_t count)
{
    for (size_t i = 0; i < count; ++i) bytes[i] = static_cast<uint8_t>(rng());
}

// The interleaved layout of the untiler (every platform but 5, 6, 0xf and 0x10) puts pixel (x, y) at a Morton-like index that passes
// width * height when a side is not a power of two: the original then writes past its buffer. Real swizzled images are powers of two.
inline bool NeedsPowerOfTwo(int32_t platform)
{
    return platform != 5 && platform != 6 && platform != 0xf && platform != 0x10;
}

// The platforms the back end distinguishes, and a few it does not.
constexpr int32_t kPlatforms[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 0x21, -1};

// A record: 0x20-byte header, then pixels. The type byte `type`; fields drawn to hit every branch the readers have. `big` is the
// byte order of the fields (the platform decides which order is read, so half the draws are read as garbage, which is also wanted).
inline Buf MakeRecord(std::mt19937 &rng, int type, bool big, size_t size = 0x600)
{
    Buf b(size);
    RandomFill(rng, b.data(), size);
    b[0] = static_cast<uint8_t>(type);
    for (int i = 1; i <= 3; ++i) {
        switch (rng() % 4) {
        case 0: b[i] = 0; break;
        case 1: b[i] = 0xff; break;
        case 2: b[i] = static_cast<uint8_t>(1u << (rng() % 8)); break;
        default: break;
        }
    }
    static const uint32_t kSizes[] = {0, 1, 2, 3, 4, 5, 7, 8, 15, 16, 17, 31, 32, 33, 63, 64, 100, 127, 128, 255, 256, 300, 513};
    Put(&b[4], rng() % 3 ? kSizes[rng() % (sizeof(kSizes) / 4)] + 0x20 : rng() % 0x600, 4, big);
    Put(&b[0x18], rng() % 8 ? kSizes[rng() % (sizeof(kSizes) / 4)] : rng(), 4, big);
    Put(&b[0x1c], rng() % 8 ? kSizes[rng() % (sizeof(kSizes) / 4)] : rng(), 4, big);
    if (rng() % 3 != 0) {
        static const uint8_t kMagic[][2] = {{0x10, 0xfb}, {0x11, 0xfb}, {0x90, 0xfb}, {0x91, 0xfb}, {0x18, 0xfb}, {0x1a, 0xfb},
                                            {0x1c, 0xfb}, {0x28, 0xfb}, {0x98, 0xfb}, {0x9a, 0xfb}, {0x9c, 0xfb}, {0xa8, 0xfb},
                                            {'G', 'M'},   {'g', 'm'},   {0x19, 0xfb}, {0, 0}};
        const uint8_t *magic = kMagic[rng() % (sizeof(kMagic) / 2)];
        b[0x20] = magic[0];
        b[0x21] = magic[1];
    }
    return b;
}

// Every live block's ordinal and bytes, and the directory's entries with pointers as ordinals.
inline std::vector<uint32_t> Snapshot(const gimex::fsh::Directory *directory, bool directoryAlive)
{
    std::vector<uint32_t> out;
    const uint32_t directoryOrdinal = directory != nullptr ? Ordinal(directory) : 0;
    for (size_t i = 0; i < gBlocks.size(); ++i) {
        if (gBlocks[i] == nullptr) continue;
        out.push_back(static_cast<uint32_t>(i) + 1);
        out.push_back(gSizes[i]);
        const uint8_t *p = static_cast<const uint8_t *>(gBlocks[i]);
        uint32_t hash = 2166136261u;
        // the directory block holds pointers, which differ between sides (its entries follow, as ordinals)
        for (uint32_t k = 0; k < gSizes[i] + kSlack && i + 1 != directoryOrdinal; ++k) hash = (hash ^ p[k]) * 16777619u;
        out.push_back(hash);
    }
    if (directoryAlive && directory != nullptr) {
        out.push_back(static_cast<uint32_t>(directory->count));
        for (int32_t i = 0; i < directory->count; ++i) {
            const uint32_t *words = reinterpret_cast<const uint32_t *>(&directory->entries[i]);
            for (size_t w = 0; w < sizeof(gimex::fsh::Entry) / 4; ++w) {
                out.push_back(EntryWord(words, w));
            }
        }
    }
    return out;
}

} // namespace fshdecode
