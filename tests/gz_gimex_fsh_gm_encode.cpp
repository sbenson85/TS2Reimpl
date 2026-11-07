// Differential tests for the FSH back end's GM encoder (lane MV; src/gz/GimexFshGmEncode.size.cpp, 0x89d938..0x89e722). Each function runs
// alone over seeded input, the original (by RVA, in the mapped image) against ours, with the stack cleared before each call on both sides.
//
// The original keeps the encoder's state in image globals 0x10ebee8..0x10ecc1c; ours is gGmEncodeState with the same layout (static_asserts in
// the header). A relocation scan of the image (references into 0x10ebee0..0x10ecc40) finds only the encoder's own functions there, plus an
// unrelated use of 0x10ebee0 in 0x899d55 and of 0x10ecc20.. in 0x8b7f30 (both outside the state), so the state is ours. Each case therefore
//  - primes both sides with the same state (the tables from the original's own build, random blocks, predictors and bit position) and the
//    same output buffer, runs the original, then ours,
//  - compares the whole state bit for bit, the buffer, and the return value,
//  - checks the original wrote nothing in 0x10eb000..0x10ed000 outside the state (ours writes nothing in the image).
//
// The code tables, the zigzag order and the quantiser matrix stay in the image and are read by both. The code writers print a warning for an
// out-of-range value (a DC difference over 255, a run over 63, a level of 0 or over 255); the printing goes through imports the harness traps,
// so no case supplies such a value to the original (the quantiser clamps levels to 255; the end-to-end cases run ours first and skip any input
// for which it warned).
#define NOMINMAX
#include "difftest.h"
#include "game/game.h"
#include "gz/GimexFshGm.h"
#include "gz/GimexFshGmEncode.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using gimex::fsh::GmEncodeState;

constexpr uint32_t kStateRva = 0x10ebee8;
constexpr uint32_t kWideRva = 0x10eb000;
constexpr size_t kWideBytes = 0x2000;
constexpr size_t kStateBytes = sizeof(GmEncodeState);
constexpr size_t kBufferBytes = 0x8000;
constexpr size_t kExtraBytes = 0x2000;

constexpr uint32_t kBitsWritten = 0x89d938;
constexpr uint32_t kWriteBlock = 0x89d947;
constexpr uint32_t kDctBlock = 0x89d9c9;
constexpr uint32_t kDctColumn = 0x89db37;
constexpr uint32_t kDctRow = 0x89dc7b;
constexpr uint32_t kWriteMacroblock = 0x89dd9b;
constexpr uint32_t kInitTables = 0x89de18;
constexpr uint32_t kWriteRunLevel = 0x89dfd7;
constexpr uint32_t kWriteDc = 0x89e098;
constexpr uint32_t kWriteAddress = 0x89e102;
constexpr uint32_t kWriteHeaderCode = 0x89e14a;
constexpr uint32_t kQuantiseBlock = 0x89e16f;
constexpr uint32_t kConvert = 0x89e20d;
constexpr uint32_t kPutBits = 0x89e3d7;
constexpr uint32_t kFlushBits = 0x89e445;
constexpr uint32_t kResetBits = 0x89e45f;
constexpr uint32_t kEncode = 0x89e47a;
constexpr uint32_t kLumaDcCodes = 0x105e2f8;
constexpr uint32_t kChromaDcCodes = 0x105e328;
constexpr uint32_t kQuantMatrix = 0x105e238;

__declspec(noinline) void ClearStack(uint8_t value = 0)
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
    double Unit() { return static_cast<double>(Next() >> 8) / 16777216.0; }
    int32_t Range(int32_t lo, int32_t hi) { return lo + static_cast<int32_t>(Next() % static_cast<uint32_t>(hi - lo + 1)); }
};

GmEncodeState &Ours()
{
    return gimex::fsh::gGmEncodeState;
}

uint8_t *OriginalState()
{
    return GameAddress<uint8_t>(kStateRva);
}

std::vector<uint8_t> WideSnapshot()
{
    const uint8_t *data = GameAddress<const uint8_t>(kWideRva);
    return std::vector<uint8_t>(data, data + kWideBytes);
}

// The tables as the original builds them (taken once; the "good" scale factors and rotation constants every case primes both sides with).
const GmEncodeState &Reference()
{
    static GmEncodeState reference;
    static bool taken = false;
    if (!taken) {
        std::memset(OriginalState(), 0, kStateBytes);
        ClearStack();
        reinterpret_cast<void(__cdecl *)()>(GameAddress<uint8_t>(kInitTables))();
        std::memcpy(&reference, OriginalState(), kStateBytes);
        reference.tablesReady = 1;
        taken = true;
    }
    return reference;
}

// One side's run: the state and buffers it ended with, its return value, and (original only) the first byte it wrote outside the state.
struct Side {
    GmEncodeState state;
    int64_t outputOffset;
    uint8_t buffer[kBufferBytes];
    uint8_t extra[kExtraBytes];
    uint64_t result;
    uint32_t warnings;
    int64_t outside;
};

struct Start {
    GmEncodeState state;
    uint8_t buffer[kBufferBytes];
    uint8_t extra[kExtraBytes];
};

void PrimeState(Fill &fill, GmEncodeState &s)
{
    s = Reference();
    for (int32_t &p : s.dcPredictor) p = fill.Range(-200, 200);
    for (auto &block : s.pixels)
        for (float &v : block) v = static_cast<float>(fill.Range(-128, 127));
    for (auto &block : s.levels)
        for (int32_t &v : block) v = fill.Range(-300, 300);
    s.accumulator = fill.Next();
    s.bitCount = fill.Range(0, 7);
    s.bytePosition = fill.Range(0, 15);
    s.output = nullptr;
}

void PrimeBuffers(Fill &fill, Start &start)
{
    for (uint8_t &b : start.buffer) b = static_cast<uint8_t>(fill.Next());
    for (uint8_t &b : start.extra) b = static_cast<uint8_t>(fill.Next());
}

template <class Call> void RunOriginal(const Start &start, Side &side, Call &&call)
{
    std::memcpy(side.buffer, start.buffer, sizeof side.buffer);
    std::memcpy(side.extra, start.extra, sizeof side.extra);
    GmEncodeState s = start.state;
    s.output = side.buffer;
    std::memcpy(OriginalState(), &s, kStateBytes);
    const std::vector<uint8_t> before = WideSnapshot();
    ClearStack();
    side.result = call(side.buffer, side.extra);
    const std::vector<uint8_t> after = WideSnapshot();
    side.outside = -1;
    side.warnings = 0;
    for (size_t i = 0; i < kWideBytes; ++i) {
        const uint32_t rva = kWideRva + static_cast<uint32_t>(i);
        if (rva >= kStateRva && rva < kStateRva + kStateBytes) continue;
        if (before[i] != after[i]) {
            side.outside = rva;
            break;
        }
    }
    std::memcpy(&side.state, OriginalState(), kStateBytes);
    side.outputOffset = side.state.output - side.buffer;
    side.state.output = nullptr;
}

template <class Call> void RunOurs(const Start &start, Side &side, Call &&call)
{
    std::memcpy(side.buffer, start.buffer, sizeof side.buffer);
    std::memcpy(side.extra, start.extra, sizeof side.extra);
    Ours() = start.state;
    Ours().output = side.buffer;
    const uint32_t warned = gimex::fsh::gGmEncodeWarnings;
    ClearStack();
    side.result = call(side.buffer, side.extra);
    side.warnings = gimex::fsh::gGmEncodeWarnings - warned;
    side.state = Ours();
    side.outputOffset = side.state.output - side.buffer;
    side.state.output = nullptr;
    side.outside = -1;
}

char gDetail[128];

// What differs between the two runs, as text ("" when nothing does).
const char *Difference(const Side &a, const Side &b, bool compareExtra)
{
    if (a.outside >= 0) return "the original wrote outside the state";
    if (b.warnings != 0) return "ours printed a range warning where the original ran silently";
    if (a.result != b.result) return "return value";
    if (a.outputOffset != b.outputOffset) return "output pointer";
    if (std::memcmp(&a.state, &b.state, kStateBytes) != 0) return "state";
    if (std::memcmp(a.buffer, b.buffer, sizeof a.buffer) != 0) return "output buffer";
    if (compareExtra && std::memcmp(a.extra, b.extra, sizeof a.extra) != 0) {
        for (size_t i = 0; i < sizeof a.extra; ++i)
            if (a.extra[i] != b.extra[i]) {
                std::snprintf(gDetail, sizeof gDetail, "extra memory at %zu: %02x vs %02x", i, a.extra[i], b.extra[i]);
                break;
            }
        return gDetail;
    }
    return "";
}

#define VERIFY_SIDES(ctx, name, iter, a, b, extra)                                                                                       \
    do {                                                                                                                                   \
        ++(ctx).cases;                                                                                                                     \
        const char *difference_ = Difference((a), (b), (extra));                                                                          \
        if (difference_[0] != 0) (ctx).Fail("%s iter %d: %s differs (original %#llx, ours %#llx)", name, iter, difference_,               \
                                            static_cast<unsigned long long>((a).result), static_cast<unsigned long long>((b).result));     \
    } while (0)

int32_t Pick(Fill &fill, const int32_t *values, size_t count)
{
    return values[fill.Next() % count];
}

// A float for the transform: pixel-like integers, wide floats, extremes, zeros.
float MakeFloat(Fill &fill, uint32_t shape)
{
    switch (shape % 6) {
    case 0: return static_cast<float>(fill.Range(-128, 127));
    case 1: return static_cast<float>(fill.Unit() * 400.0 - 200.0);
    case 2: return (fill.Next() % 2) ? 127.0f : -128.0f;
    case 3: return fill.Next() % 4 == 0 ? static_cast<float>(fill.Unit() * 2.0e5 - 1.0e5) : 0.0f;
    case 4: return static_cast<float>(fill.Unit() * 1.0e-3);
    default: return static_cast<float>(fill.Unit() * 6.0e9 - 3.0e9);
    }
}

// An 8 x 8 BGRA patch generator: flat, gradients, noise, edge colours.
uint32_t MakePixel(Fill &fill, uint32_t shape, int32_t x, int32_t y, uint32_t base)
{
    switch (shape % 6) {
    case 0: return base;
    case 1: return (base & 0xff000000) | (((base & 0xff) + x * 6) & 0xff) | ((((base >> 8) & 0xff) + y * 6) & 0xff) << 8 | ((((base >> 16) & 0xff) + (x + y) * 3) & 0xff) << 16;
    case 2: return fill.Next();
    case 3: return ((fill.Next() & 1) ? 0xff : 0) | ((fill.Next() & 1) ? 0xff00 : 0) | ((fill.Next() & 1) ? 0xff0000 : 0) | (fill.Next() & 0xff000000);
    case 4: return (x < 4) ? base : ~base;
    default: return (base & 0xffffff00) | (fill.Next() & 0xff);
    }
}

} // namespace

// ---- the tables ----

DIFF_TEST(GimexFshGmEncode_InitTables, kInitTables)
{
    Fill fill(0x7001);
    for (int iter = 0; iter < 60; ++iter) {
        Start start;
        PrimeState(fill, start.state);
        // Garbage in the tables on both sides: the build must overwrite every table byte it owns and nothing else.
        for (float &v : start.state.scale) v = static_cast<float>(fill.Next());
        start.state.rotate45 = static_cast<float>(fill.Next());
        start.state.rotateA = static_cast<float>(fill.Next());
        start.state.rotateB = static_cast<float>(fill.Next());
        start.state.rotateC = static_cast<float>(fill.Next());
        start.state.reserved = fill.Next();
        PrimeBuffers(fill, start);
        Side original;
        Side ours;
        RunOriginal(start, original, [&](uint8_t *, uint8_t *) {
            reinterpret_cast<void(__cdecl *)()>(GameAddress<uint8_t>(kInitTables))();
            return uint64_t{0};
        });
        RunOurs(start, ours, [&](uint8_t *, uint8_t *) {
            gimex::fsh::InitEncodeTables();
            return uint64_t{0};
        });
        VERIFY_SIDES(ctx, "InitTables", iter, original, ours, true);
        // The build must reproduce the reference tables exactly.
        if (std::memcmp(original.state.scale, Reference().scale, sizeof Reference().scale) != 0) ctx.Fail("InitTables iter %d: the original's tables differ from the reference", iter);
    }
}

// ---- the bit writer ----

DIFF_TEST(GimexFshGmEncode_ResetBits, kResetBits)
{
    using Fn = void(__cdecl *)(uint8_t *);
    const Fn original = ctx.Original<Fn>(kResetBits);
    Fill fill(0x7002);
    for (int iter = 0; iter < 500; ++iter) {
        Start start;
        PrimeState(fill, start.state);
        PrimeBuffers(fill, start);
        const int32_t offset = fill.Range(0, 100);
        Side a;
        Side b;
        RunOriginal(start, a, [&](uint8_t *buffer, uint8_t *) {
            original(buffer + offset);
            return uint64_t{0};
        });
        RunOurs(start, b, [&](uint8_t *buffer, uint8_t *) {
            gimex::fsh::ResetBitWriter(buffer + offset);
            return uint64_t{0};
        });
        if (a.outputOffset != offset) ctx.Fail("ResetBits iter %d: the original did not set the output pointer", iter);
        VERIFY_SIDES(ctx, "ResetBits", iter, a, b, true);
    }
}

DIFF_TEST(GimexFshGmEncode_PutBits, kPutBits)
{
    using Fn = void(__cdecl *)(uint32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kPutBits);
    Fill fill(0x7003);
    for (int iter = 0; iter < 20000; ++iter) {
        Start start;
        PrimeState(fill, start.state);
        PrimeBuffers(fill, start);
        const uint32_t value = (iter % 3 == 0) ? 0xffffffffu : fill.Next();
        const int32_t counts[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 16, 24, 31, 32, -1, -7, 33, 40};
        int32_t count = (iter % 4 == 0) ? Pick(fill, counts, sizeof counts / sizeof counts[0]) : fill.Range(0, 32);
        if (iter < 19 * 8) count = counts[iter % 19];
        Side a;
        Side b;
        RunOriginal(start, a, [&](uint8_t *, uint8_t *) {
            original(value, count);
            return uint64_t{0};
        });
        RunOurs(start, b, [&](uint8_t *, uint8_t *) {
            gimex::fsh::PutBits(value, count);
            return uint64_t{0};
        });
        VERIFY_SIDES(ctx, "PutBits", iter, a, b, true);
    }
}

DIFF_TEST(GimexFshGmEncode_FlushBits, kFlushBits)
{
    using Fn = void(__cdecl *)();
    const Fn original = ctx.Original<Fn>(kFlushBits);
    Fill fill(0x7004);
    for (int iter = 0; iter < 2000; ++iter) {
        Start start;
        PrimeState(fill, start.state);
        PrimeBuffers(fill, start);
        start.state.bitCount = iter % 8;
        Side a;
        Side b;
        RunOriginal(start, a, [&](uint8_t *, uint8_t *) {
            original();
            return uint64_t{0};
        });
        RunOurs(start, b, [&](uint8_t *, uint8_t *) {
            gimex::fsh::FlushBits();
            return uint64_t{0};
        });
        VERIFY_SIDES(ctx, "FlushBits", iter, a, b, true);
    }
}

DIFF_TEST(GimexFshGmEncode_BitsWritten, kBitsWritten)
{
    using Fn = int32_t(__cdecl *)();
    const Fn original = ctx.Original<Fn>(kBitsWritten);
    Fill fill(0x7005);
    for (int iter = 0; iter < 2000; ++iter) {
        Start start;
        PrimeState(fill, start.state);
        PrimeBuffers(fill, start);
        start.state.bitCount = fill.Range(0, 7);
        start.state.bytePosition = fill.Range(0, 100000);
        Side a;
        Side b;
        RunOriginal(start, a, [&](uint8_t *, uint8_t *) { return static_cast<uint64_t>(static_cast<uint32_t>(original())); });
        RunOurs(start, b, [&](uint8_t *, uint8_t *) { return static_cast<uint64_t>(static_cast<uint32_t>(gimex::fsh::BitsWritten())); });
        VERIFY_SIDES(ctx, "BitsWritten", iter, a, b, true);
    }
}

// ---- the code writers ----

DIFF_TEST(GimexFshGmEncode_WriteDc, kWriteDc)
{
    using Fn = void(__cdecl *)(const void *, int32_t);
    const Fn original = ctx.Original<Fn>(kWriteDc);
    Fill fill(0x7006);
    int iter = 0;
    for (const uint32_t tableRva : {kLumaDcCodes, kChromaDcCodes}) {
        const void *table = GameAddress<const uint8_t>(tableRva);
        for (int32_t difference = -255; difference <= 255; ++difference, ++iter) {
            Start start;
            PrimeState(fill, start.state);
            PrimeBuffers(fill, start);
            Side a;
            Side b;
            RunOriginal(start, a, [&](uint8_t *, uint8_t *) {
                original(table, difference);
                return uint64_t{0};
            });
            RunOurs(start, b, [&](uint8_t *, uint8_t *) {
                gimex::fsh::WriteDc(table, difference);
                return uint64_t{0};
            });
            VERIFY_SIDES(ctx, "WriteDc", iter, a, b, true);
        }
    }
    // The tables in the image have no code wider than 8 bits for the sizes a difference can reach (0..8), so the code's high byte never
    // matters there. A table of the caller's own does: 9 entries of a 16-bit code, a length and a spare byte.
    for (int tableIter = 0; tableIter < 400; ++tableIter) {
        uint8_t table[9 * 4];
        for (int32_t size = 0; size < 9; ++size) {
            const int32_t length = fill.Range(1, 16);
            const uint32_t code = fill.Next() & ((1u << length) - 1);
            table[size * 4 + 0] = static_cast<uint8_t>(code);
            table[size * 4 + 1] = static_cast<uint8_t>(code >> 8);
            table[size * 4 + 2] = static_cast<uint8_t>(length);
            table[size * 4 + 3] = static_cast<uint8_t>(fill.Next());
        }
        for (int32_t difference = -255; difference <= 255; difference += 1 + tableIter % 5, ++iter) {
            Start start;
            PrimeState(fill, start.state);
            PrimeBuffers(fill, start);
            Side a;
            Side b;
            RunOriginal(start, a, [&](uint8_t *, uint8_t *) {
                original(table, difference);
                return uint64_t{0};
            });
            RunOurs(start, b, [&](uint8_t *, uint8_t *) {
                gimex::fsh::WriteDc(table, difference);
                return uint64_t{0};
            });
            VERIFY_SIDES(ctx, "WriteDc own table", iter, a, b, true);
        }
    }
}

DIFF_TEST(GimexFshGmEncode_WriteRunLevel, kWriteRunLevel)
{
    using Fn = void(__cdecl *)(uint32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kWriteRunLevel);
    Fill fill(0x7007);
    int iter = 0;
    for (uint32_t run = 0; run <= 63; ++run) {
        for (int32_t level = -255; level <= 255; ++level) {
            if (level == 0) continue;
            ++iter;
            Start start;
            PrimeState(fill, start.state);
            PrimeBuffers(fill, start);
            Side a;
            Side b;
            RunOriginal(start, a, [&](uint8_t *, uint8_t *) {
                original(run, level);
                return uint64_t{0};
            });
            RunOurs(start, b, [&](uint8_t *, uint8_t *) {
                gimex::fsh::WriteRunLevel(run, level);
                return uint64_t{0};
            });
            VERIFY_SIDES(ctx, "WriteRunLevel", iter, a, b, true);
        }
    }
}

DIFF_TEST(GimexFshGmEncode_WriteAddress, kWriteAddress)
{
    using Fn = void(__cdecl *)(int32_t);
    const Fn original = ctx.Original<Fn>(kWriteAddress);
    Fill fill(0x7008);
    for (int32_t count = 0; count <= 400; ++count) {
        Start start;
        PrimeState(fill, start.state);
        PrimeBuffers(fill, start);
        Side a;
        Side b;
        RunOriginal(start, a, [&](uint8_t *, uint8_t *) {
            original(count);
            return uint64_t{0};
        });
        RunOurs(start, b, [&](uint8_t *, uint8_t *) {
            gimex::fsh::WriteAddressIncrement(count);
            return uint64_t{0};
        });
        VERIFY_SIDES(ctx, "WriteAddress", count, a, b, true);
    }
}

DIFF_TEST(GimexFshGmEncode_WriteHeaderCode, kWriteHeaderCode)
{
    using Fn = void(__cdecl *)(int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kWriteHeaderCode);
    Fill fill(0x7009);
    int iter = 0;
    for (int32_t group = 0; group <= 1; ++group) {
        for (int32_t index = 0; index < 32; ++index, ++iter) {
            Start start;
            PrimeState(fill, start.state);
            PrimeBuffers(fill, start);
            Side a;
            Side b;
            RunOriginal(start, a, [&](uint8_t *, uint8_t *) {
                original(group, index);
                return uint64_t{0};
            });
            RunOurs(start, b, [&](uint8_t *, uint8_t *) {
                gimex::fsh::WriteHeaderCode(group, index);
                return uint64_t{0};
            });
            VERIFY_SIDES(ctx, "WriteHeaderCode", iter, a, b, true);
        }
    }
}

DIFF_TEST(GimexFshGmEncode_WriteBlock, kWriteBlock)
{
    using Fn = void(__cdecl *)(const int32_t *, uint32_t);
    const Fn original = ctx.Original<Fn>(kWriteBlock);
    Fill fill(0x700a);
    for (int iter = 0; iter < 20000; ++iter) {
        Start start;
        PrimeState(fill, start.state);
        PrimeBuffers(fill, start);
        int32_t levels[64];
        const uint32_t density = fill.Next() % 6;
        for (int32_t &v : levels) {
            switch (density) {
            case 0: v = 0; break;
            case 1: v = (fill.Next() % 8 == 0) ? fill.Range(-3, 3) : 0; break;
            case 2: v = (fill.Next() % 3 == 0) ? fill.Range(-12, 12) : 0; break;
            case 3: v = fill.Range(-255, 255); break;
            case 4: v = (fill.Next() % 2) ? 255 : -255; break;
            default: v = (fill.Next() % 2) ? fill.Range(-45, 45) : 0; break;
            }
        }
        const uint32_t block = fill.Next() % 6;
        // The DC difference must stay within 255.
        const int32_t slot = block < 4 ? 0 : 1 + static_cast<int32_t>(block & 1);
        levels[0] = start.state.dcPredictor[slot] + fill.Range(-255, 255);
        Side a;
        Side b;
        RunOriginal(start, a, [&](uint8_t *, uint8_t *) {
            original(levels, block);
            return uint64_t{0};
        });
        RunOurs(start, b, [&](uint8_t *, uint8_t *) {
            gimex::fsh::WriteBlock(levels, block);
            return uint64_t{0};
        });
        VERIFY_SIDES(ctx, "WriteBlock", iter, a, b, true);
    }
}

// ---- the transform ----

namespace {

void MakeDctInput(Fill &fill, int iter, float *values)
{
    const uint32_t shape = static_cast<uint32_t>(iter);
    const uint32_t mixed = fill.Next();
    for (int i = 0; i < 64; ++i) values[i] = MakeFloat(fill, (mixed % 7 == 0) ? static_cast<uint32_t>(fill.Next()) : shape / 3);
}

} // namespace

DIFF_TEST(GimexFshGmEncode_DctRow, kDctRow)
{
    using Fn = void(__cdecl *)(const float *, float *);
    const Fn original = ctx.Original<Fn>(kDctRow);
    Fill fill(0x700b);
    for (int iter = 0; iter < 20000; ++iter) {
        Start start;
        PrimeState(fill, start.state);
        PrimeBuffers(fill, start);
        float input[64];
        MakeDctInput(fill, iter, input);
        std::memcpy(start.extra, input, sizeof input);
        const int32_t row = fill.Range(0, 7);
        Side a;
        Side b;
        // Input at extra[0..], output at extra[512..] (the eight outputs only; the rest of the area must stay as it was).
        RunOriginal(start, a, [&](uint8_t *, uint8_t *extra) {
            original(reinterpret_cast<const float *>(extra) + row * 8, reinterpret_cast<float *>(extra + 512));
            return uint64_t{0};
        });
        RunOurs(start, b, [&](uint8_t *, uint8_t *extra) {
            gimex::fsh::DctRow(reinterpret_cast<const float *>(extra) + row * 8, reinterpret_cast<float *>(extra + 512));
            return uint64_t{0};
        });
        VERIFY_SIDES(ctx, "DctRow", iter, a, b, true);
    }
}

DIFF_TEST(GimexFshGmEncode_DctColumn, kDctColumn)
{
    using Fn = void(__cdecl *)(const float *, float *);
    const Fn original = ctx.Original<Fn>(kDctColumn);
    Fill fill(0x700c);
    for (int iter = 0; iter < 20000; ++iter) {
        Start start;
        PrimeState(fill, start.state);
        PrimeBuffers(fill, start);
        float input[64];
        MakeDctInput(fill, iter, input);
        std::memcpy(start.extra, input, sizeof input);
        const int32_t column = fill.Range(0, 7);
        Side a;
        Side b;
        // Output at extra[512..] with a stride of 8 floats.
        RunOriginal(start, a, [&](uint8_t *, uint8_t *extra) {
            original(reinterpret_cast<const float *>(extra) + column, reinterpret_cast<float *>(extra + 512) + column);
            return uint64_t{0};
        });
        RunOurs(start, b, [&](uint8_t *, uint8_t *extra) {
            gimex::fsh::DctColumn(reinterpret_cast<const float *>(extra) + column, reinterpret_cast<float *>(extra + 512) + column);
            return uint64_t{0};
        });
        VERIFY_SIDES(ctx, "DctColumn", iter, a, b, true);
    }
}

DIFF_TEST(GimexFshGmEncode_DctBlock, kDctBlock)
{
    using Fn = void(__cdecl *)(const float *, int32_t *);
    const Fn original = ctx.Original<Fn>(kDctBlock);
    Fill fill(0x700d);
    for (int iter = 0; iter < 20000; ++iter) {
        Start start;
        PrimeState(fill, start.state);
        PrimeBuffers(fill, start);
        float input[64];
        MakeDctInput(fill, iter, input);
        std::memcpy(start.extra, input, sizeof input);
        Side a;
        Side b;
        RunOriginal(start, a, [&](uint8_t *, uint8_t *extra) {
            original(reinterpret_cast<const float *>(extra), reinterpret_cast<int32_t *>(extra + 512));
            return uint64_t{0};
        });
        RunOurs(start, b, [&](uint8_t *, uint8_t *extra) {
            gimex::fsh::DctBlock(reinterpret_cast<const float *>(extra), reinterpret_cast<int32_t *>(extra + 512));
            return uint64_t{0};
        });
        VERIFY_SIDES(ctx, "DctBlock", iter, a, b, true);
    }
}

// The original reads no stack it does not set (the same call with the stack at 0x00 and 0xff gives the same state).
DIFF_TEST(GimexFshGmEncode_DctBlockStack, kDctBlock)
{
    using Fn = void(__cdecl *)(const float *, int32_t *);
    const Fn original = ctx.Original<Fn>(kDctBlock);
    Fill fill(0x700e);
    for (int iter = 0; iter < 3000; ++iter) {
        float input[64];
        MakeDctInput(fill, iter, input);
        int32_t zero[64];
        int32_t ones[64];
        std::memcpy(OriginalState(), &Reference(), kStateBytes);
        ClearStack(0x00);
        original(input, zero);
        ClearStack(0xff);
        original(input, ones);
        ++ctx.cases;
        if (std::memcmp(zero, ones, sizeof zero) != 0) ctx.Fail("DctBlock stack iter %d: the original depends on the stack", iter);
    }
}

DIFF_TEST(GimexFshGmEncode_QuantiseBlock, kQuantiseBlock)
{
    using Fn = void(__cdecl *)(const int32_t *, int32_t *, uint8_t, const uint8_t *, int32_t);
    const Fn original = ctx.Original<Fn>(kQuantiseBlock);
    Fill fill(0x700f);
    for (int iter = 0; iter < 30000; ++iter) {
        Start start;
        PrimeState(fill, start.state);
        PrimeBuffers(fill, start);
        int32_t source[64];
        const uint32_t shape = fill.Next() % 6;
        for (int32_t &v : source) {
            switch (shape) {
            case 0: v = fill.Range(-3000, 3000); break;
            case 1: v = fill.Range(-40, 40); break;
            case 2: v = (fill.Next() % 3 == 0) ? fill.Range(-9000, 9000) : 0; break;
            case 3: v = 0; break;
            case 4: v = (fill.Next() % 2) ? 20000 : -20000; break;
            default: v = fill.Range(-300, 300); break;
            }
        }
        std::memcpy(start.extra, source, sizeof source);
        // The matrix: the image's own, or random steps of 1..255 (never 0).
        uint8_t matrix[64];
        const uint8_t *imageMatrix = GameAddress<const uint8_t>(kQuantMatrix);
        const bool own = fill.Next() % 2 == 0;
        for (int i = 0; i < 64; ++i) matrix[i] = own ? imageMatrix[i] : static_cast<uint8_t>(fill.Range(1, 255));
        const uint8_t shift = static_cast<uint8_t>(fill.Next() % 4);
        const int32_t quantiser = (iter % 2) ? fill.Range(1, 64) : 2 * fill.Range(1, 31);
        const bool inPlace = fill.Next() % 2 == 0;
        Side a;
        Side b;
        RunOriginal(start, a, [&](uint8_t *, uint8_t *extra) {
            int32_t *dest = reinterpret_cast<int32_t *>(extra + (inPlace ? 0 : 512));
            original(reinterpret_cast<const int32_t *>(extra), dest, shift, matrix, quantiser);
            return uint64_t{0};
        });
        RunOurs(start, b, [&](uint8_t *, uint8_t *extra) {
            int32_t *dest = reinterpret_cast<int32_t *>(extra + (inPlace ? 0 : 512));
            gimex::fsh::QuantiseBlock(reinterpret_cast<const int32_t *>(extra), dest, shift, matrix, quantiser);
            return uint64_t{0};
        });
        VERIFY_SIDES(ctx, "QuantiseBlock", iter, a, b, true);
    }
}

// ---- the colour conversion ----

DIFF_TEST(GimexFshGmEncode_ConvertToBlocks, kConvert)
{
    using Fn = void(__cdecl *)(const uint32_t *, int32_t, int32_t, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kConvert);
    Fill fill(0x7010);
    for (int iter = 0; iter < 20000; ++iter) {
        Start start;
        PrimeState(fill, start.state);
        PrimeBuffers(fill, start);
        const int32_t stride = fill.Range(8, 24);
        uint32_t pixels[8 * 24];
        const uint32_t shape = fill.Next() % 6;
        const uint32_t base = fill.Next();
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 24; ++x) pixels[y * stride + x] = MakePixel(fill, shape, x & 7, y, base);
        if (iter % 11 == 0)
            for (int i = 0; i < 8 * 24; ++i) pixels[i] = (iter % 22 == 0) ? 0xffffffffu : 0x00000000u;
        const int32_t block = fill.Range(0, 5);
        const int32_t rows[] = {0, 4, 0, 4, 1, 3};
        const int32_t row = rows[fill.Next() % 6];
        const int32_t column = rows[fill.Next() % 6];
        Side a;
        Side b;
        RunOriginal(start, a, [&](uint8_t *, uint8_t *) {
            original(pixels, stride, block, row, column);
            return uint64_t{0};
        });
        RunOurs(start, b, [&](uint8_t *, uint8_t *) {
            gimex::fsh::ConvertToBlocks(pixels, stride, block, row, column);
            return uint64_t{0};
        });
        VERIFY_SIDES(ctx, "ConvertToBlocks", iter, a, b, true);
    }
}

DIFF_TEST(GimexFshGmEncode_ConvertToBlocksStack, kConvert)
{
    using Fn = void(__cdecl *)(const uint32_t *, int32_t, int32_t, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kConvert);
    Fill fill(0x7011);
    for (int iter = 0; iter < 3000; ++iter) {
        uint32_t pixels[64];
        for (uint32_t &p : pixels) p = fill.Next();
        std::memcpy(OriginalState(), &Reference(), kStateBytes);
        ClearStack(0x00);
        original(pixels, 8, iter % 4, 0, 0);
        std::vector<uint8_t> zero(OriginalState(), OriginalState() + kStateBytes);
        std::memcpy(OriginalState(), &Reference(), kStateBytes);
        ClearStack(0xff);
        original(pixels, 8, iter % 4, 0, 0);
        ++ctx.cases;
        if (std::memcmp(zero.data(), OriginalState(), kStateBytes) != 0) ctx.Fail("ConvertToBlocks stack iter %d: the original depends on the stack", iter);
    }
}

// ---- a macroblock ----

DIFF_TEST(GimexFshGmEncode_WriteMacroblock, kWriteMacroblock)
{
    using Fn = void(__cdecl *)(int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kWriteMacroblock);
    Fill fill(0x7012);
    int skipped = 0;
    for (int iter = 0; iter < 8000; ++iter) {
        Start start;
        PrimeState(fill, start.state);
        PrimeBuffers(fill, start);
        const uint32_t shape = fill.Next() % 5;
        for (auto &block : start.state.pixels) {
            const float base = static_cast<float>(fill.Range(-110, 110));
            for (int i = 0; i < 64; ++i) {
                switch (shape) {
                case 0: block[i] = base; break;
                case 1: block[i] = base * 0.5f + static_cast<float>((i & 7) * 3 - (i >> 3) * 2); break;
                case 2: block[i] = static_cast<float>(fill.Range(-128, 127)); break;
                case 3: block[i] = (fill.Next() % 2) ? 127.0f : -128.0f; break;
                default: block[i] = static_cast<float>(fill.Range(-128, 127)) * 0.25f; break;
                }
            }
        }
        for (int32_t &p : start.state.dcPredictor) p = fill.Range(-100, 100);
        const int32_t first = fill.Next() % 2;
        const int32_t quantiser = (iter % 3 == 0) ? fill.Range(1, 31) : fill.Range(1, 4);
        Side a;
        Side b;
        RunOurs(start, b, [&](uint8_t *, uint8_t *) {
            const uint32_t before = gimex::fsh::gGmEncodeWarnings;
            gimex::fsh::WriteMacroblock(first, quantiser);
            return static_cast<uint64_t>(gimex::fsh::gGmEncodeWarnings - before);
        });
        if (b.result != 0) {
            ++skipped;
            continue;
        }
        RunOriginal(start, a, [&](uint8_t *, uint8_t *) {
            original(first, quantiser);
            return uint64_t{0};
        });
        VERIFY_SIDES(ctx, "WriteMacroblock", iter, a, b, true);
    }
    if (skipped > 400) ctx.Fail("WriteMacroblock: too many inputs skipped for a warning (%d)", skipped);
}

// ---- the whole encoder ----

namespace {

struct Image {
    int32_t width;
    int32_t height;
    int32_t stride;       // in bytes
    uint32_t shape;       // MakePixel's pattern (0 = flat)
    std::vector<uint32_t> pixels;
};

void MakeImage(Fill &fill, int iter, Image &image)
{
    image.width = fill.Range(1, 40);
    image.height = fill.Range(1, 40);
    if (iter % 5 == 0) {
        image.width = 16 * fill.Range(1, 3);
        image.height = 16 * fill.Range(1, 3);
    }
    const int32_t alignedWidth = (image.width + 15) & ~15;
    const int32_t alignedHeight = (image.height + 15) & ~15;
    // The conversions read rows of the aligned width, so the buffer is at least that wide; a few cases use a wider stride (which makes the
    // original step over the image differently, see the source) and the buffer then covers every read.
    image.stride = alignedWidth * 4;
    if (iter % 9 == 4) image.stride += 4 * fill.Range(1, 40);
    if (iter % 13 == 6) image.stride = alignedWidth * 4 + 2;
    const size_t rows = static_cast<size_t>(alignedHeight) + 64;
    const size_t perRow = static_cast<size_t>(image.stride / 4) + alignedWidth + 64;
    image.pixels.assign(rows * perRow + 4096, 0);
    const uint32_t shape = (iter % 6 == 0) ? 0 : fill.Next() % 6;
    image.shape = shape;
    const uint32_t base = fill.Next();
    for (size_t i = 0; i < image.pixels.size(); ++i) {
        const int32_t x = static_cast<int32_t>(i % perRow);
        const int32_t y = static_cast<int32_t>(i / perRow);
        image.pixels[i] = MakePixel(fill, shape, x, y, base);
    }
    if (iter % 17 == 3) std::fill(image.pixels.begin(), image.pixels.end(), 0x80808080u);
}

} // namespace

DIFF_TEST(GimexFshGmEncode_Encode, kEncode)
{
    using Fn = uint32_t(__cdecl *)(const uint32_t *, uint16_t *, int32_t, int32_t, uint32_t, uint32_t, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kEncode);
    Fill fill(0x7013);
    int skipped = 0;
    int decoded = 0;
    int flatDecoded = 0;
    for (int iter = 0; iter < 1500; ++iter) {
        Start start;
        PrimeState(fill, start.state);
        PrimeBuffers(fill, start);
        // The first call finds the tables not built (and builds them on both sides); the later ones find them ready.
        if (iter % 4 == 0) {
            start.state.tablesReady = 0;
            std::memset(start.state.scale, 0, sizeof start.state.scale);
            start.state.rotate45 = start.state.rotateA = start.state.rotateB = start.state.rotateC = 0.0f;
        }
        Image image;
        MakeImage(fill, iter, image);
        const uint32_t flags = (iter % 3 == 0) ? static_cast<uint32_t>(fill.Range(0, 31)) : static_cast<uint32_t>(fill.Range(0, 1));
        const int32_t alphaMode = (iter % 7 == 0) ? fill.Range(0, 3) : fill.Range(0, 1);
        const int32_t quantiser = (iter % 3 == 0) ? fill.Range(1, 31) : fill.Range(1, 8);
        Side a;
        Side b;
        RunOurs(start, b, [&](uint8_t *buffer, uint8_t *) {
            const uint32_t before = gimex::fsh::gGmEncodeWarnings;
            gimex::fsh::EncodeGm(image.pixels.data(), reinterpret_cast<uint16_t *>(buffer), image.width, image.height, static_cast<uint32_t>(image.stride), flags,
                                 quantiser, alphaMode);
            return static_cast<uint64_t>(gimex::fsh::gGmEncodeWarnings - before);
        });
        if (b.result != 0) {
            ++skipped;
            continue;
        }
        RunOurs(start, b, [&](uint8_t *buffer, uint8_t *) {
            return static_cast<uint64_t>(gimex::fsh::EncodeGm(image.pixels.data(), reinterpret_cast<uint16_t *>(buffer), image.width, image.height,
                                                              static_cast<uint32_t>(image.stride), flags, quantiser, alphaMode));
        });
        RunOriginal(start, a, [&](uint8_t *buffer, uint8_t *) {
            return static_cast<uint64_t>(original(image.pixels.data(), reinterpret_cast<uint16_t *>(buffer), image.width, image.height,
                                                  static_cast<uint32_t>(image.stride), flags, quantiser, alphaMode));
        });
        VERIFY_SIDES(ctx, "Encode", iter, a, b, true);
        // MR's decoder reads what ours wrote (the dimensions it finds and a pass through the whole stream).
        if (flags == 0 && image.stride == ((image.width + 15) & ~15) * 4 && iter % 6 == 0) {
            int32_t w = 0;
            int32_t h = 0;
            int32_t f = 0;
            int32_t q = 0;
            int32_t size = 0;
            if (gimex::fsh::ParseGmHeader(b.buffer, &w, &h, &f, &q, &size) == 0 || w != ((image.width + 15) & ~15) || h != ((image.height + 15) & ~15) ||
                size != static_cast<int32_t>(b.result) || q != quantiser)
                ctx.Fail("Encode iter %d: the decoder's header parse disagrees (w %d h %d size %d quality %d)", iter, w, h, size, q);
            // A flat image comes back within a few levels per channel (the format is lossy, the quantiser is small here).
            if (image.shape == 0 && iter % 17 != 3 && quantiser <= 8) {
                const int32_t aw = (image.width + 15) & ~15;
                const int32_t ah = (image.height + 15) & ~15;
                std::vector<uint8_t> decodedPixels(static_cast<size_t>(aw) * ah * 4 + 64, 0);
                gimex::fsh::DecodeGm(decodedPixels.data(), aw * 4, b.buffer, aw, ah, 0);
                uint32_t worst = 0;
                const uint32_t source = image.pixels[0];
                for (int32_t i = 0; i < aw * ah; ++i)
                    for (int c = 0; c < 3; ++c) {
                        const int32_t d = static_cast<int32_t>(decodedPixels[i * 4 + c]) - static_cast<int32_t>((source >> (8 * c)) & 0xff);
                        worst = std::max<uint32_t>(worst, static_cast<uint32_t>(d < 0 ? -d : d));
                    }
                if (worst > 24) ctx.Fail("Encode iter %d: the decoder returns a flat image off by %u", iter, worst);
                ++flatDecoded;
            }
            ++decoded;
        }
    }
    if (decoded == 0 || flatDecoded == 0) ctx.Fail("Encode: no case was passed to the decoder");
    if (skipped > 400) ctx.Fail("Encode: too many inputs skipped for a warning (%d)", skipped);
}

DIFF_TEST(GimexFshGmEncode_EncodeStack, kEncode)
{
    using Fn = uint32_t(__cdecl *)(const uint32_t *, uint16_t *, int32_t, int32_t, uint32_t, uint32_t, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kEncode);
    Fill fill(0x7014);
    for (int iter = 0; iter < 200; ++iter) {
        Image image;
        MakeImage(fill, iter * 5, image);
        std::vector<uint8_t> zero(kBufferBytes);
        std::vector<uint8_t> ones(kBufferBytes);
        std::memcpy(OriginalState(), &Reference(), kStateBytes);
        ClearStack(0x00);
        const uint32_t a = original(image.pixels.data(), reinterpret_cast<uint16_t *>(zero.data()), image.width, image.height, static_cast<uint32_t>(image.stride), 0, 3, 0);
        std::memcpy(OriginalState(), &Reference(), kStateBytes);
        ClearStack(0xff);
        const uint32_t b = original(image.pixels.data(), reinterpret_cast<uint16_t *>(ones.data()), image.width, image.height, static_cast<uint32_t>(image.stride), 0, 3, 0);
        ++ctx.cases;
        if (a != b || std::memcmp(zero.data(), ones.data(), kBufferBytes) != 0) ctx.Fail("Encode stack iter %d: the original depends on the stack", iter);
    }
}

// ---- out-of-range values (ours only) ----
// The original prints a warning for these, through imports the harness traps, so it cannot be run on them. What it does after the warning
// is read from its code (0x89e098: the size is taken from 255, the difference is written as it is; 0x89dfd7: the magnitude becomes 255, which
// no table holds, so the pair is escaped), and checked here against ours composed from the bit writer the cases above verified.

DIFF_TEST(GimexFshGmEncode_WriteDcRange, kWriteDc)
{
    Fill fill(0x7015);
    int iter = 0;
    for (const uint32_t tableRva : {kLumaDcCodes, kChromaDcCodes}) {
        const uint8_t *table = GameAddress<const uint8_t>(tableRva);
        for (int32_t difference = -3000; difference <= 3000; difference += 7, ++iter) {
            if (difference >= -255 && difference <= 255) continue;
            Start start;
            PrimeState(fill, start.state);
            PrimeBuffers(fill, start);
            Side a;
            Side b;
            RunOurs(start, a, [&](uint8_t *, uint8_t *) {
                gimex::fsh::WriteDc(table, difference);
                return uint64_t{0};
            });
            RunOurs(start, b, [&](uint8_t *, uint8_t *) {
                const uint8_t *entry = table + 8 * 4;
                gimex::fsh::PutBits(static_cast<uint32_t>(entry[0] | (entry[1] << 8)), static_cast<int8_t>(entry[2]));
                gimex::fsh::PutBits(static_cast<uint32_t>(difference < 0 ? difference - 1 + 256 : difference), 8);
                return uint64_t{0};
            });
            ++ctx.cases;
            if (a.warnings != 1) ctx.Fail("WriteDcRange %d: %u warnings, expected 1", difference, a.warnings);
            a.warnings = b.warnings = 0;
            const char *difference_ = Difference(b, a, true);
            if (difference_[0] != 0) ctx.Fail("WriteDcRange %d: %s differs", difference, difference_);
        }
    }
}

DIFF_TEST(GimexFshGmEncode_WriteRunLevelRange, kWriteRunLevel)
{
    Fill fill(0x7016);
    const uint32_t runs[] = {0, 1, 2, 5, 31, 63, 64, 100, 0xffffffffu};
    const int32_t levels[] = {0, 1, -1, 5, -5, 40, 127, 128, -127, -128, 255, -255, 256, -256, 300, -300, 1000, -100000};
    int iter = 0;
    for (const uint32_t run : runs) {
        for (const int32_t level : levels) {
            const bool valid = run <= 63 && level != 0 && level >= -255 && level <= 255;
            if (valid) continue;
            ++iter;
            Start start;
            PrimeState(fill, start.state);
            PrimeBuffers(fill, start);
            Side a;
            Side b;
            RunOurs(start, a, [&](uint8_t *, uint8_t *) {
                gimex::fsh::WriteRunLevel(run, level);
                return uint64_t{0};
            });
            RunOurs(start, b, [&](uint8_t *, uint8_t *) {
                gimex::fsh::PutBits(1, 6);
                gimex::fsh::PutBits(run, 6);
                if (level > 127) gimex::fsh::PutBits(0, 8);
                else if (level < -127) gimex::fsh::PutBits(0x80, 8);
                gimex::fsh::PutBits(static_cast<uint32_t>(level), 8);
                return uint64_t{0};
            });
            ++ctx.cases;
            if (a.warnings != 1) ctx.Fail("WriteRunLevelRange run %u level %d: %u warnings, expected 1", run, level, a.warnings);
            a.warnings = b.warnings = 0;
            const char *difference_ = Difference(b, a, true);
            if (difference_[0] != 0) ctx.Fail("WriteRunLevelRange run %u level %d: %s differs", run, level, difference_);
        }
    }
}

// ---- a stream over 1 MB (the header's size field is 20 bits) ----

DIFF_TEST(GimexFshGmEncode_EncodeLarge, kEncode)
{
    using Fn = uint32_t(__cdecl *)(const uint32_t *, uint16_t *, int32_t, int32_t, uint32_t, uint32_t, int32_t, int32_t);
    const Fn original = ctx.Original<Fn>(kEncode);
    Fill fill(0x7017);
    constexpr int32_t kSide = 1024;
    std::vector<uint32_t> pixels(static_cast<size_t>(kSide) * kSide + 8192);
    for (uint32_t &p : pixels) p = fill.Next();
    std::vector<uint8_t> expected(12u << 20, 0xa5);
    std::vector<uint8_t> actual(12u << 20, 0xa5);
    std::memcpy(OriginalState(), &Reference(), kStateBytes);
    ClearStack();
    const uint32_t a = original(pixels.data(), reinterpret_cast<uint16_t *>(expected.data()), kSide, kSide, kSide * 4, 1, 1, 0);
    GmEncodeState state = Reference();
    Ours() = state;
    ClearStack();
    const uint32_t b = gimex::fsh::EncodeGm(pixels.data(), reinterpret_cast<uint16_t *>(actual.data()), kSide, kSide, kSide * 4, 1, 1, 0);
    ++ctx.cases;
    if (a < 0x100000) ctx.Fail("EncodeLarge: the stream is only %u bytes: the test does not reach the size field's top bits", a);
    if (a != b) ctx.Fail("EncodeLarge: size %u vs %u", a, b);
    if (std::memcmp(expected.data(), actual.data(), expected.size()) != 0) ctx.Fail("EncodeLarge: the streams differ");
}
