// Differential tests for the codecs' shared helpers in src/gz/Gimex.size.cpp (lane MP): the allocator 0x87da65 and
// free 0x87da72, the stream helpers 0x87da81 / 0x87da8f / 0x87dab6 / 0x87dacd and the number readers 0x87daf4 /
// 0x87db43.
//
// The stream helpers run on a scripted cIGZFile: slots 7 Length, 12 SeekToPosition, 15 ReadWithCount and 17
// WriteWithCount log their arguments (and the count they are handed) and answer from a script (the count they leave
// too); any other slot is a stray call (logged). Compared: the result, the log. The allocator's blocks are watched by
// difftest::AllocationCounter (blocks and bytes) and the free's by difftest::FreeRecorder.
#include "difftest.h"
#include "free_recorder.h"
#include "gz/Gimex.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <random>
#include <utility>
#include <vector>

namespace {

constexpr uint32_t kAllocate = 0x87da65;
constexpr uint32_t kFreeMemory = 0x87da72;
constexpr uint32_t kStreamLength = 0x87da81;
constexpr uint32_t kStreamRead = 0x87da8f;
constexpr uint32_t kStreamSeek = 0x87dab6;
constexpr uint32_t kStreamWrite = 0x87dacd;
constexpr uint32_t kReadLittleEndian = 0x87daf4;
constexpr uint32_t kReadBigEndian = 0x87db43;

// The scripted stream: what each slot answers, and what it saw.
struct Script {
    uint32_t length = 0;      // Length's answer
    int32_t seekAnswer = 0;   // SeekToPosition's answer
    uint32_t countLeft = 0;   // the count ReadWithCount / WriteWithCount leave
    uint8_t ioAnswer = 0;     // their answer
};
Script gScript;
std::vector<uint32_t> gLog;

struct ScriptedFile {
    void **vtable;
};

constexpr int kSlots = 64;
void *gVTable[kSlots];

uint32_t __fastcall SLength(ScriptedFile *, void *)
{
    gLog.push_back(7);
    return gScript.length;
}
int32_t __fastcall SSeek(ScriptedFile *, void *, int32_t position)
{
    gLog.push_back(12);
    gLog.push_back(static_cast<uint32_t>(position));
    return gScript.seekAnswer;
}
uint8_t __fastcall SRead(ScriptedFile *, void *, void *buffer, uint32_t *length)
{
    gLog.push_back(15);
    gLog.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buffer)));
    gLog.push_back(*length);
    *length = gScript.countLeft;
    return gScript.ioAnswer;
}
uint8_t __fastcall SWrite(ScriptedFile *, void *, void *buffer, uint32_t *length)
{
    gLog.push_back(17);
    gLog.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buffer)));
    gLog.push_back(*length);
    *length = gScript.countLeft;
    return gScript.ioAnswer;
}
template <int Slot> uint32_t __fastcall SStray(ScriptedFile *, void *)
{
    gLog.push_back(0x100 + Slot);
    return 0;
}

template <int... Slots> void FillStrays(std::integer_sequence<int, Slots...>)
{
    ((gVTable[Slots] = reinterpret_cast<void *>(&SStray<Slots>)), ...);
}

ifaces::cIGZFile *MakeFile(ScriptedFile &file)
{
    FillStrays(std::make_integer_sequence<int, kSlots>{});
    gVTable[7] = reinterpret_cast<void *>(&SLength);
    gVTable[12] = reinterpret_cast<void *>(&SSeek);
    gVTable[15] = reinterpret_cast<void *>(&SRead);
    gVTable[17] = reinterpret_cast<void *>(&SWrite);
    file.vtable = gVTable;
    return reinterpret_cast<ifaces::cIGZFile *>(&file);
}

uint32_t Pick(std::mt19937 &rng, std::initializer_list<uint32_t> values)
{
    return values.begin()[rng() % values.size()];
}

void CompareLogs(difftest::Context &ctx, const char *what, int index, const std::vector<uint32_t> &a,
                 const std::vector<uint32_t> &b)
{
    if (a == b) return;
    ctx.Fail("%s case %d: the slot logs differ (original %zu words, ours %zu)", what, index, a.size(), b.size());
    for (size_t i = 0; i < a.size() || i < b.size(); ++i) {
        const uint32_t x = i < a.size() ? a[i] : 0xdeadbeef;
        const uint32_t y = i < b.size() ? b[i] : 0xdeadbeef;
        if (x != y) {
            ctx.Fail("  word %zu: original %08x, ours %08x", i, x, y);
            break;
        }
    }
}

} // namespace

// 0x87da65: a block of the size asked for, from the process heap on both sides.
DIFF_TEST(Gimex_Allocate, 0x87da65)
{
    using Fn = void *(__stdcall *)(uint32_t);
    using FreeFn = int32_t(__stdcall *)(void *);
    std::mt19937 rng(0x87da65);
    std::vector<uint32_t> sizes = {0, 1, 2, 3, 4, 0x18, 0x100, 0x2a7c, 0x10000, 0x123457};
    for (int i = 0; i < 60; ++i) sizes.push_back(rng() % 0x5000);
    for (const uint32_t size : sizes) {
        long long blocks[2], bytes[2];
        void *block[2];
        for (int side = 0; side < 2; ++side) {
            difftest::AllocationCounter counter;
            block[side] = side == 0 ? ctx.Original<Fn>(kAllocate)(size) : gimex::Allocate(size);
            counter.Stop();
            blocks[side] = counter.Blocks();
            bytes[side] = counter.Bytes();
            if (block[side] != nullptr && !counter.Allocated(block[side])) ctx.Fail("size %u, side %d: not a new block", size, side);
            if (block[side] != nullptr && size != 0) std::memset(block[side], 0x5a, size);
        }
        ++ctx.cases;
        if ((block[0] == nullptr) != (block[1] == nullptr) || blocks[0] != blocks[1] || bytes[0] != bytes[1])
            ctx.Fail("size %u: original %s (%lld blocks, %lld bytes), ours %s (%lld blocks, %lld bytes)", size,
                     block[0] ? "a block" : "null", blocks[0], bytes[0], block[1] ? "a block" : "null", blocks[1], bytes[1]);
        // Each side's block goes back through the other side's free: both are the process heap's.
        if (block[0]) gimex::FreeMemory(block[0]);
        if (block[1]) ctx.Original<FreeFn>(kFreeMemory)(block[1]);
    }
}

// 0x87da72: frees the block (or nothing for null). Only al is compared: the original's answer is `mov al, 1` after
// the call to operator delete[] (0x6c4b50), so eax's upper bytes are whatever operator delete left there.
DIFF_TEST(Gimex_FreeMemory, 0x87da72)
{
    using Fn = int32_t(__stdcall *)(void *);
    using AllocateFn = void *(__stdcall *)(uint32_t);
    std::mt19937 rng(0x87da72);
    for (int i = 0; i < 80; ++i) {
        const uint32_t size = i % 10 == 0 ? 0 : rng() % 0x3000;
        const bool null = i % 7 == 3;
        uint8_t answer[2];
        size_t frees[2];
        bool freed[2];
        long long blocks[2];
        for (int side = 0; side < 2; ++side) {
            void *block = null ? nullptr
                               : (rng() % 2 ? ctx.Original<AllocateFn>(kAllocate)(size) : gimex::Allocate(size));
            difftest::AllocationCounter counter;
            difftest::FreeRecorder recorder;
            answer[side] = static_cast<uint8_t>(side == 0 ? ctx.Original<Fn>(kFreeMemory)(block) : gimex::FreeMemory(block));
            recorder.Stop();
            counter.Stop();
            frees[side] = recorder.Count();
            freed[side] = block != nullptr && recorder.Freed(block);
            blocks[side] = counter.Blocks();
        }
        ++ctx.cases;
        if (answer[0] != answer[1] || frees[0] != frees[1] || freed[0] != freed[1] || blocks[0] != blocks[1])
            ctx.Fail("case %d (%s, %u bytes): original al %u, %zu frees%s, %lld blocks; ours al %u, %zu frees%s, %lld blocks",
                     i, null ? "null" : "a block", size, answer[0], frees[0], freed[0] ? " (the block)" : "", blocks[0],
                     answer[1], frees[1], freed[1] ? " (the block)" : "", blocks[1]);
    }
}

// 0x87da81: Length (slot 7) as 64 bits.
DIFF_TEST(Gimex_StreamLength, 0x87da81)
{
    using Fn = uint64_t(__stdcall *)(ifaces::cIGZFile *);
    ScriptedFile file;
    ifaces::cIGZFile *stream = MakeFile(file);
    std::mt19937 rng(0x87da81);
    for (int i = 0; i < 200; ++i) {
        gScript.length = i < 8 ? Pick(rng, {0, 1, 0x7fffffff, 0x80000000, 0xffffffff, 8, 0x10, 0xfffffff8}) : rng();
        gLog.clear();
        const uint64_t a = ctx.Original<Fn>(kStreamLength)(stream);
        const std::vector<uint32_t> logA = gLog;
        gLog.clear();
        const uint64_t b = gimex::StreamLength(stream);
        ++ctx.cases;
        if (a != b) ctx.Fail("length %08x: original %016llx, ours %016llx", gScript.length, a, b);
        CompareLogs(ctx, "StreamLength", i, logA, gLog);
    }
}

// 0x87da8f and 0x87dacd: a count (negative ones too) handed to slot 15 / 17; the count the slot leaves comes back.
template <bool Write> void ReadWriteCases(difftest::Context &ctx, uint32_t rva, uint32_t seed)
{
    using ReadFn = int32_t(__stdcall *)(ifaces::cIGZFile *, void *, int32_t);
    using WriteFn = int32_t(__stdcall *)(ifaces::cIGZFile *, const void *, int32_t);
    ScriptedFile file;
    ifaces::cIGZFile *stream = MakeFile(file);
    std::mt19937 rng(seed);
    uint8_t buffer[16];
    for (int i = 0; i < 300; ++i) {
        const int32_t count = static_cast<int32_t>(
            i < 12 ? Pick(rng, {0, 1, 0x10, 0x7fffffff, 0x80000000, 0xffffffff, 0xfffffff0, 0x20, 3, 0x40000000, 2, 0x4c})
                   : (rng() % 3 ? rng() % 0x1000 : rng()));
        gScript.countLeft = rng() % 4 == 0 ? rng() : (rng() % 2 ? static_cast<uint32_t>(count) : rng() % 0x100);
        gScript.ioAnswer = static_cast<uint8_t>(rng() % 3);
        void *at = rng() % 4 ? static_cast<void *>(buffer + rng() % 8) : nullptr;
        int32_t result[2];
        std::vector<uint32_t> logs[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            if constexpr (Write) {
                result[side] = side == 0 ? ctx.Original<WriteFn>(rva)(stream, at, count) : gimex::StreamWrite(stream, at, count);
            } else {
                result[side] = side == 0 ? ctx.Original<ReadFn>(rva)(stream, at, count) : gimex::StreamRead(stream, at, count);
            }
            logs[side] = gLog;
        }
        ++ctx.cases;
        if (result[0] != result[1])
            ctx.Fail("count %d, the slot leaving %u: original %d, ours %d", count, gScript.countLeft, result[0], result[1]);
        CompareLogs(ctx, Write ? "StreamWrite" : "StreamRead", i, logs[0], logs[1]);
    }
}

DIFF_TEST(Gimex_StreamRead, 0x87da8f)
{
    ReadWriteCases<false>(ctx, kStreamRead, 0x87da8f);
}

DIFF_TEST(Gimex_StreamWrite, 0x87dacd)
{
    ReadWriteCases<true>(ctx, kStreamWrite, 0x87dacd);
}

// 0x87dab6: SeekToPosition (slot 12) to the low word; true when it answers the same. Only al is compared: the original
// answers with `sete al` and leaves eax's upper bytes as the slot's answer left them (callers test al).
DIFF_TEST(Gimex_StreamSeek, 0x87dab6)
{
    using Fn = uint8_t(__stdcall *)(ifaces::cIGZFile *, uint32_t, int32_t);
    ScriptedFile file;
    ifaces::cIGZFile *stream = MakeFile(file);
    std::mt19937 rng(0x87dab6);
    for (int i = 0; i < 300; ++i) {
        const uint32_t low = i < 6 ? Pick(rng, {0, 1, 0x7fffffff, 0x80000000, 0xffffffff, 0x10}) : (rng() % 2 ? rng() % 0x400 : rng());
        const int32_t high = static_cast<int32_t>(rng() % 3 ? 0 : rng());
        const uint32_t choice = rng() % 4;
        gScript.seekAnswer = static_cast<int32_t>(choice == 0 ? rng() : choice == 1 ? low + 1 : choice == 2 ? 0xffffffffu : low);
        uint8_t result[2];
        std::vector<uint32_t> logs[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            result[side] = side == 0 ? ctx.Original<Fn>(kStreamSeek)(stream, low, high)
                                     : static_cast<uint8_t>(gimex::StreamSeek(stream, low, high));
            logs[side] = gLog;
        }
        ++ctx.cases;
        if (result[0] != result[1])
            ctx.Fail("seek %08x:%08x answered %08x: original %u, ours %u", high, low, gScript.seekAnswer, result[0], result[1]);
        CompareLogs(ctx, "StreamSeek", i, logs[0], logs[1]);
    }
}

// 0x87daf4 and 0x87db43: numbers of every width (in range and out of it) over random bytes.
template <bool Big> void NumberCases(difftest::Context &ctx, uint32_t rva, uint32_t seed)
{
    using Fn = uint32_t(__cdecl *)(const void *, int32_t);
    std::mt19937 rng(seed);
    uint8_t bytes[8];
    for (int i = 0; i < 400; ++i) {
        for (uint8_t &b : bytes) b = static_cast<uint8_t>(i < 20 ? Pick(rng, {0, 0xff, 0x80, 0x7f, 1}) : rng());
        const int32_t width = i < 16 ? i - 6 : static_cast<int32_t>(rng() % 12 == 0 ? rng() : rng() % 7);
        const uint32_t a = ctx.Original<Fn>(rva)(bytes, width);
        const uint32_t b = Big ? gimex::ReadBigEndian(bytes, width) : gimex::ReadLittleEndian(bytes, width);
        ++ctx.cases;
        if (a != b)
            ctx.Fail("%s width %d over %02x %02x %02x %02x: original %08x, ours %08x", Big ? "big" : "little", width,
                     bytes[0], bytes[1], bytes[2], bytes[3], a, b);
    }
}

DIFF_TEST(Gimex_ReadLittleEndian, 0x87daf4)
{
    NumberCases<false>(ctx, kReadLittleEndian, 0x87daf4);
}

DIFF_TEST(Gimex_ReadBigEndian, 0x87db43)
{
    NumberCases<true>(ctx, kReadBigEndian, 0x87db43);
}
