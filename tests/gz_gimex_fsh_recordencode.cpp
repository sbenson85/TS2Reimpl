// Differential test for the FSH back end's record encoder (lane MT; src/gz/GimexFshRecordEncode.size.cpp, 0x89d7b1). It runs alone over
// seeded input, the original against ours, the stack cleared before each call on both sides. The block encoder 0x8988f3 is stood in for on
// both sides (shape::StandIn: the original calls the image's, ours calls ours, both land on the stand-in), which records every argument and
// writes a pattern at the destination, so what is compared is the header bytes, the arguments the block encoder is given (including the
// weights it is handed and where its destination lies), its result plus 8, and that nothing else is written (sentinel margins) or changed
// (the flags). The image is never read, so counts over the 16,000,000 boundary cost nothing.
#define NOMINMAX
#include "difftest.h"
#include "code_patch.h"
#include "game/game.h"
#include "gz/GimexFshCluster.h"
#include "gz/GimexFshEncode.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr uint32_t kRecord = 0x89d7b1;
constexpr uint32_t kBlocks = 0x8988f3;
constexpr int kBytes = 40;
constexpr uint8_t kSentinel = 0xcd;

using RecordFn = int32_t(__stdcall *)(uint8_t *, const uint8_t *, int32_t, uint32_t *);

struct BlocksCall {
    int32_t width;
    int32_t rows;
    uint32_t pitch;
    const uint8_t *source;
    uint8_t *dest;
    uint32_t flags;
    float weights[3];
    int32_t form4444;
    int32_t bigEndian;
    int32_t calls;
};

BlocksCall gCall;

int32_t __cdecl StandBlocks(int32_t width, int32_t rows, uint32_t pitch, const uint8_t *source, uint8_t *dest, uint32_t flags, const float *weights,
                            int32_t form4444, int32_t bigEndian)
{
    gCall.width = width;
    gCall.rows = rows;
    gCall.pitch = pitch;
    gCall.source = source;
    gCall.dest = dest;
    gCall.flags = flags;
    gCall.weights[0] = weights[0];
    gCall.weights[1] = weights[1];
    gCall.weights[2] = weights[2];
    gCall.form4444 = form4444;
    gCall.bigEndian = bigEndian;
    ++gCall.calls;
    dest[0] = 0x5a;
    dest[1] = static_cast<uint8_t>(rows);
    return rows * 3 + static_cast<int32_t>(flags & 0xff) + 11;
}

__declspec(noinline) void ClearStack()
{
    volatile uint8_t buffer[0x4000];
    for (size_t i = 0; i < sizeof(buffer); ++i) buffer[i] = 0;
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
};

struct Run {
    int32_t result;
    uint8_t out[kBytes];
    BlocksCall call;
    uint32_t flagsAfter;
};

const int32_t kCounts[] = {0,        1,        15,         16,         17,         255,        256,        65535,      65536,    0xf42400 - 16,
                           0xf42400 - 1, 0xf42400, 0xf42401, 0xf42410, 0xf4241f, 0x1000000, 0x7fffffff, -1,       -15,      -16,
                           -17,      -0x1000,  static_cast<int32_t>(0x80000000), 0x123456, 0xabcdef, 1000000, 20000000};

void Execute(RecordFn fn, uint8_t *outBase, const uint8_t *image, int32_t count, uint32_t *flags, Run &run)
{
    std::memset(run.out, kSentinel, sizeof run.out);
    gCall = BlocksCall{};
    ClearStack();
    run.result = fn(run.out + 8, image, count, flags);
    run.call = gCall;
    run.flagsAfter = flags ? *flags : 0;
    (void)outBase;
}

} // namespace

DIFF_TEST(GimexFshRecordEncode_Record, kRecord)
{
    const RecordFn original = ctx.Original<RecordFn>(kRecord);
    shape::StandIn stand(kBlocks, &gimex::fsh::EncodeBlocks, &StandBlocks);
    uint8_t image[16] = {};
    Fill fill(0x6101);
    for (int iter = 0; iter < 20000; ++iter) {
        const int32_t count = (iter % 3 == 0) ? kCounts[fill.Next() % (sizeof kCounts / sizeof kCounts[0])]
                                              : static_cast<int32_t>(fill.Next() >> (fill.Next() % 32));
        uint32_t flagsValue;
        switch (fill.Next() % 4) {
        case 0: flagsValue = fill.Next() & 0x30c; break;           // the four flags that matter
        case 1: flagsValue = fill.Next(); break;
        case 2: flagsValue = fill.Next() & 0x3ff; break;
        default: flagsValue = (fill.Next() % 3) << 8; break;
        }
        const bool nullFlags = (iter % 29) == 0;
        uint32_t flagsA = flagsValue;
        uint32_t flagsB = flagsValue;
        Run expected;
        Run actual;
        Execute(original, nullptr, image, count, nullFlags ? nullptr : &flagsA, expected);
        Execute(&gimex::fsh::EncodeRecord, nullptr, image, count, nullFlags ? nullptr : &flagsB, actual);
        ++ctx.cases;
        // The destination pointer is relative to each run's own buffer: compare the offset.
        const ptrdiff_t destA = expected.call.dest - expected.out;
        const ptrdiff_t destB = actual.call.dest - actual.out;
        const bool same = expected.result == actual.result && std::memcmp(expected.out, actual.out, sizeof expected.out) == 0 &&
                          destA == destB && expected.call.calls == actual.call.calls && expected.call.width == actual.call.width &&
                          expected.call.rows == actual.call.rows && expected.call.pitch == actual.call.pitch &&
                          expected.call.source == actual.call.source && expected.call.flags == actual.call.flags &&
                          std::memcmp(expected.call.weights, actual.call.weights, sizeof expected.call.weights) == 0 &&
                          expected.call.form4444 == actual.call.form4444 && expected.call.bigEndian == actual.call.bigEndian &&
                          expected.flagsAfter == actual.flagsAfter;
        if (!same)
            ctx.Fail("record iter %d count %d flags %08x%s: result %d vs %d, header %02x %02x %02x %02x %02x %02x %02x vs %02x %02x %02x %02x %02x %02x %02x, "
                     "blocks rows %d vs %d flags %x vs %x form4444 %d vs %d bigEndian %d vs %d",
                     iter, count, flagsValue, nullFlags ? " (null)" : "", expected.result, actual.result, expected.out[8], expected.out[9],
                     expected.out[10], expected.out[11], expected.out[12], expected.out[13], expected.out[14], actual.out[8], actual.out[9],
                     actual.out[10], actual.out[11], actual.out[12], actual.out[13], actual.out[14], expected.call.rows, actual.call.rows,
                     expected.call.flags, actual.call.flags, expected.call.form4444, actual.call.form4444, expected.call.bigEndian,
                     actual.call.bigEndian);
    }
}
