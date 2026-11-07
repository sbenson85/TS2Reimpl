// Differential tests for src/app/ReportWrite.*: the report's dumps. The hex-dump writer (0x1bd744),
// the manual stack walk (0x1bcc6c), the instruction bytes (0x1bd064) and the stack bytes (0x1bd1de).
//
// The report file is an object whose Write slot (16) records everything written. The import table is
// faked where the code reaches the OS: IsBadWritePtr / IsBadReadPtr (scripted answers), VirtualQuery
// and GetModuleFileNameA (the module lookup, over a fake PE image). The frames of the manual walk are
// an array in the test; the instruction bytes come from pages with partly no-access protection (the
// reads that fault are caught by the code under test); the stack bytes from a stand-in stack: the
// thread information block's stack base and limit are pointed at a buffer around each call, and, for
// the read that faults, the limit is moved below the real stack into reserved memory (so the
// exception dispatcher still finds the frames in range). Both sides run on the same script and
// the writes are compared.
#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#include "app/ReportWrite.h"
#include "difftest.h"
#include "game/game.h"
#include "rz/cRZString.h"
#include "rz/cRZTextFile.h"

#include <windows.h>
#include <intrin.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kHexDump = 0x1bd744, kManualStack = 0x1bcc6c, kInstructionData = 0x1bd064, kStackData = 0x1bd1de;
constexpr uint32_t kIatGetModuleFileNameA = 0xd9a16c, kIatVirtualQuery = 0xd9a1cc, kIatIsBadReadPtr = 0xd9a1d0, kIatIsBadWritePtr = 0xd9a1d4;
constexpr uint32_t kSystemCodePageCache = 0xff23a4;

std::vector<std::string> gLog;

void Log(const char *fmt, ...)
{
    char buf[900];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    gLog.emplace_back(buf);
}

std::string Joined()
{
    std::string out;
    for (const std::string &s : gLog) out += s + "|";
    return out;
}

void ExpectSame(difftest::Context &ctx, const char *what, const std::string &a, const std::string &b)
{
    ++ctx.cases;
    if (a != b) {
        size_t at = 0;
        while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
        size_t from = at > 60 ? at - 60 : 0;
        ctx.Fail("%s: differ at %zu\n o: ..%.150s\n u: ..%.150s", what, at, a.c_str() + from, b.c_str() + from);
    }
}

class Slots {
public:
    explicit Slots(difftest::Context &c) : ctx(c) {}
    ~Slots()
    {
        for (auto it = saved.rbegin(); it != saved.rend(); ++it) *ctx.image.At<void *>(it->first) = it->second;
    }
    void Set(uint32_t rva, void *value)
    {
        void **slot = ctx.image.At<void *>(rva);
        saved.emplace_back(rva, *slot);
        *slot = value;
    }

private:
    difftest::Context &ctx;
    std::vector<std::pair<uint32_t, void *>> saved;
};

// ---- the report file ----

void *gFileVt[40];
uint8_t __fastcall FakeWrite(void *, void *, const void *data, uint32_t length)
{
    Log("W%u[%.*s]", length, static_cast<int>(length), static_cast<const char *>(data));
    return 1;
}

struct Reporter {
    alignas(8) uint8_t raw[appbase::ExceptionReporterObject::kSize];
    Reporter(int32_t charSize, uint8_t filler)
    {
        std::memset(raw, filler, sizeof(raw));
        *reinterpret_cast<void **>(raw + 0xa0) = gFileVt;
        *reinterpret_cast<int32_t *>(raw + 0xa0 + 0x9c) = charSize;
    }
    appbase::ExceptionReporterObject *get() { return reinterpret_cast<appbase::ExceptionReporterObject *>(raw); }
};

// ---- the fake OS ----

// A fake PE image the module lookup reads: two sections.
alignas(16) uint8_t gImage[0x1000];

void BuildImage()
{
    std::memset(gImage, 0, sizeof(gImage));
    const uint32_t ntAt = 0x80;
    *reinterpret_cast<uint32_t *>(gImage + 0x3c) = ntAt;
    *reinterpret_cast<uint16_t *>(gImage + ntAt + 6) = 2;
    *reinterpret_cast<uint16_t *>(gImage + ntAt + 0x14) = 0xe0;
    uint8_t *h = gImage + ntAt + 0x18 + 0xe0;
    std::memcpy(h, ".text", 6);
    *reinterpret_cast<uint32_t *>(h + 8) = 0x300;
    *reinterpret_cast<uint32_t *>(h + 0xc) = 0x100;
    *reinterpret_cast<uint32_t *>(h + 0x10) = 0x200;
    h += 0x28;
    std::memcpy(h, ".data", 6);
    *reinterpret_cast<uint32_t *>(h + 8) = 0x100;
    *reinterpret_cast<uint32_t *>(h + 0xc) = 0x500;
    *reinterpret_cast<uint32_t *>(h + 0x10) = 0x400;
}

SIZE_T WINAPI FakeVirtualQuery(const void *address, MEMORY_BASIC_INFORMATION *info, SIZE_T length)
{
    Log("VQ(%p)", address);
    const uint8_t *p = static_cast<const uint8_t *>(address);
    std::memset(info, 0x6b, sizeof(*info));
    if (p >= gImage && p < gImage + 0x1000) {
        info->AllocationBase = gImage;
        return sizeof(*info);
    }
    return 0;
}

DWORD WINAPI FakeGetModuleFileName(void *module, char *buffer, DWORD size)
{
    Log("GMFN(%p,%u)", module, size);
    const char name[] = "C:\\Games\\Sims 2\\TSBin\\Sims2EP9.exe";
    std::memcpy(buffer, name, sizeof(name));
    return sizeof(name) - 1;
}

// Frames for the manual walk.
alignas(16) uint32_t gArena[320];
std::vector<uint32_t> gBadWrite; // arena indices IsBadWritePtr refuses
BOOL gBadRead = FALSE;

BOOL WINAPI FakeIsBadWritePtr(void *p, UINT_PTR size)
{
    const uintptr_t a = reinterpret_cast<uintptr_t>(p);
    const uintptr_t lo = reinterpret_cast<uintptr_t>(gArena), hi = lo + sizeof(gArena);
    BOOL bad = a < lo || a >= hi;
    if (!bad)
        for (uint32_t index : gBadWrite)
            if (a == lo + index * 4) bad = TRUE;
    Log("IBW(%u,%u)", static_cast<uint32_t>((a - lo) / 4), static_cast<uint32_t>(size));
    return bad;
}

BOOL WINAPI FakeIsBadRead(const void *p, UINT_PTR size)
{
    Log("IBR(%u)", static_cast<uint32_t>(size));
    return gBadRead;
}

// Original: the calling thread's stack bounds, replaced for a call.
struct StackBounds {
    uint32_t base, limit;
    StackBounds(uint32_t newBase, uint32_t newLimit)
    {
        base = __readfsdword(4);
        limit = __readfsdword(8);
        __writefsdword(4, newBase);
        __writefsdword(8, newLimit);
    }
    ~StackBounds()
    {
        __writefsdword(4, base);
        __writefsdword(8, limit);
    }
};

uint32_t Address(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

} // namespace

// ---- 0x1bd744 ----

DIFF_TEST(ReportWrite_WriteHexDump, kHexDump)
{
    Slots slots(ctx);
    std::memcpy(gFileVt, ctx.image.At<void>(cRZTextFile::kVTable), sizeof(gFileVt));
    gFileVt[16] = reinterpret_cast<void *>(&FakeWrite);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t, const uint8_t *, uint32_t, int32_t, int32_t, int32_t)>(kHexDump);
    std::mt19937 rng(0x1bd744);
    static const int32_t kGroups[] = {1, 2, 4, 8, 0x20, 3, 0, -1};
    static const int32_t kBytes[] = {1, 2, 4, 3, 8, 1, 4};
    static const int32_t kAlign[] = {1, 2, 4, 8, 16, 3, 5, 1, 4};
    for (int run = 0; run < 1500; ++run) {
        uint8_t data[300];
        for (uint8_t &b : data) b = static_cast<uint8_t>(rng());
        uint32_t address = rng() % 3 == 0 ? static_cast<uint32_t>(rng()) : rng() % 0x1000;
        if (rng() % 7 == 0) address = 0xfffffff0u + rng() % 16;
        if (rng() % 7 == 0) address = 0x7ffffff0u + rng() % 32;
        const uint32_t count = rng() % 5 == 0 ? rng() % 4 : rng() % 290;
        const int32_t groups = kGroups[rng() % 8];
        const int32_t bytes = kBytes[rng() % 7];
        const int32_t align = kAlign[rng() % 9];
        const int32_t charSize = rng() % 4 == 0 ? 2 : 0;
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            Reporter reporter(charSize, 0x31);
            if (side == 0) orig(reporter.raw, address, data, count, groups, bytes, align);
            else appbase::WriteHexDump(reporter.get(), nullptr, address, data, count, groups, bytes, align);
            results[side] = Joined();
        }
        char label[96];
        std::snprintf(label, sizeof(label), "hex dump run %d (%u bytes, %d x %d, align %d)", run, count, groups, bytes, align);
        ExpectSame(ctx, label, results[0], results[1]);
    }
}

// ---- 0x1bcc6c ----

DIFF_TEST(ReportWrite_WriteManualCallStack, kManualStack)
{
    BuildImage();
    Slots slots(ctx);
    slots.Set(kIatVirtualQuery, reinterpret_cast<void *>(&FakeVirtualQuery));
    slots.Set(kIatGetModuleFileNameA, reinterpret_cast<void *>(&FakeGetModuleFileName));
    slots.Set(kIatIsBadWritePtr, reinterpret_cast<void *>(&FakeIsBadWritePtr));
    int32_t *codePage = ctx.image.At<int32_t>(kSystemCodePageCache);
    const int32_t savedCodePage = *codePage;
    std::memcpy(gFileVt, ctx.image.At<void>(cRZTextFile::kVTable), sizeof(gFileVt));
    gFileVt[16] = reinterpret_cast<void *>(&FakeWrite);
    auto orig = ctx.Original<void(__thiscall *)(void *, const CONTEXT *)>(kManualStack);
    std::mt19937 rng(0x1bcc6c);
    const uint32_t arenaBase = Address(gArena);
    auto returnAddress = [&]() -> uint32_t {
        const uint32_t image = Address(gImage);
        switch (rng() % 5) {
        case 0: return image + 0x100 + rng() % 0x300; // in .text
        case 1: return image + 0x500 + rng() % 0x100; // in .data
        case 2: return image + 0x800 + rng() % 0x100; // in the image, in no section
        case 3: return image + 0x100;                 // a section's first byte
        default: return static_cast<uint32_t>(rng());  // not in the image
        }
    };
    for (int run = 0; run < 600; ++run) {
        // The chain: frames at increasing dword indices; each links to the next one, or breaks in one of the ways the walk stops.
        std::memset(gArena, 0, sizeof(gArena));
        gBadWrite.clear();
        std::vector<uint32_t> frames;
        uint32_t at = 4 * (rng() % 4);
        const int count = static_cast<int>(rng() % 9);
        for (int i = 0; i < count; ++i) {
            frames.push_back(at);
            at += 8 + 4 * (rng() % 12);
        }
        if (frames.empty()) frames.push_back(at);
        for (size_t i = 0; i < frames.size(); ++i) {
            uint32_t *f = gArena + frames[i] / 4;
            f[1] = returnAddress();
            if (i + 1 < frames.size()) {
                f[0] = arenaBase + frames[i + 1];
                if (rng() % 9 == 0) gBadWrite.push_back(frames[i + 1] / 4);
            } else {
                switch (rng() % 6) {
                case 0: f[0] = 0; break;
                case 1: f[0] = arenaBase + frames[i] + 1 + rng() % 3; break;      // not aligned
                case 2: f[0] = arenaBase + frames[i]; break;                      // does not grow
                case 3: f[0] = arenaBase + (frames[i] > 4 ? frames[i] - 4 : 0); break; // goes back
                case 4: f[0] = arenaBase + frames.back() + 8 + 4 * (rng() % 20); break; // grows, to a zeroed frame
                default: f[0] = static_cast<uint32_t>(rng()) & ~3u; break;         // anywhere
                }
            }
        }
        CONTEXT context;
        for (size_t i = 0; i < sizeof(context); ++i) reinterpret_cast<uint8_t *>(&context)[i] = static_cast<uint8_t>(rng());
        context.Ebp = arenaBase + frames[0];
        context.Eip = returnAddress();
        const int32_t charSize = rng() % 5 == 0 ? 2 : 0;
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            *codePage = -3;
            Reporter reporter(charSize, 0x44);
            if (side == 0) orig(reporter.raw, &context);
            else appbase::WriteManualCallStack(reporter.get(), nullptr, &context);
            results[side] = Joined();
        }
        char label[48];
        std::snprintf(label, sizeof(label), "manual call stack run %d", run);
        ExpectSame(ctx, label, results[0], results[1]);
    }
    *codePage = savedCodePage;
}

// ---- 0x1bd064 ----

DIFF_TEST(ReportWrite_WriteInstructionData, kInstructionData)
{
    Slots slots(ctx);
    slots.Set(kIatIsBadReadPtr, reinterpret_cast<void *>(&FakeIsBadRead));
    std::memcpy(gFileVt, ctx.image.At<void>(cRZTextFile::kVTable), sizeof(gFileVt));
    gFileVt[16] = reinterpret_cast<void *>(&FakeWrite);
    auto orig = ctx.Original<void(__thiscall *)(void *, const CONTEXT *)>(kInstructionData);
    std::mt19937 rng(0x1bd064);
    // Three pages in a row: the middle one is no-access in the first layout (the readable bytes end at
    // its start), the first and last in the second (they begin at the middle one's start).
    uint8_t *memory = static_cast<uint8_t *>(VirtualAlloc(nullptr, 0x3000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    for (int i = 0; i < 0x3000; ++i) memory[i] = static_cast<uint8_t>(rng());
    DWORD old;
    for (int run = 0; run < 400; ++run) {
        const int layout = run % 3;
        // layout 0: [rw][no][rw]; 1: [no][rw][rw]; 2: [rw][rw][no]
        VirtualProtect(memory, 0x3000, PAGE_READWRITE, &old);
        if (layout == 0) VirtualProtect(memory + 0x1000, 0x1000, PAGE_NOACCESS, &old);
        if (layout == 1) VirtualProtect(memory, 0x1000, PAGE_NOACCESS, &old);
        if (layout == 2) VirtualProtect(memory + 0x2000, 0x1000, PAGE_NOACCESS, &old);
        const uint32_t boundary = layout == 0 ? (rng() % 2 ? 0x1000 : 0x2000) : layout == 1 ? 0x1000 : 0x2000;
        int32_t delta;
        switch (rng() % 4) {
        case 0: delta = static_cast<int32_t>(rng() % 0x121) - 0x90; break;
        case 1: delta = static_cast<int32_t>(rng() % 0x20) - 0x10; break;
        case 2: delta = static_cast<int32_t>(rng() % 0x600) - 0x300; break;
        default: delta = static_cast<int32_t>(rng() % 0x9) - 0x84; break; // the window's start next to the boundary
        }
        CONTEXT context;
        for (size_t i = 0; i < sizeof(context); ++i) reinterpret_cast<uint8_t *>(&context)[i] = static_cast<uint8_t>(rng());
        context.Eip = Address(memory) + boundary + delta;
        gBadRead = rng() % 12 == 0;
        const int32_t charSize = rng() % 5 == 0 ? 2 : 0;
        char label[64];
        std::snprintf(label, sizeof(label), "instruction data run %d layout %d delta %d", run, layout, delta);

        // Which bytes of the window can be read.
        const uint32_t windowStart = context.Eip - 0x80;
        std::vector<uint8_t> readable;
        uint32_t shownStart = windowStart;
        bool any = false, faults = false;
        for (uint32_t i = 0; i < 0x100; ++i) {
            const uint32_t offset = windowStart + i - Address(memory);
            const bool ok = layout == 0 ? !(offset >= 0x1000 && offset < 0x2000) : layout == 1 ? offset >= 0x1000 : offset < 0x2000;
            if (ok) {
                readable.push_back(memory[offset]);
                any = true;
            } else {
                faults = true;
                if (!any) ++shownStart;
            }
        }

        if (faults && !gBadRead) {
            // The original's SEH handler does not run in the harness (the exception would end the run), so
            // only ours runs; the expectation is the header followed by the original's hex dump of the
            // readable bytes (the leading unreadable ones skipped from the start address).
            std::string expected, ours;
            for (int side = 0; side < 2; ++side) {
                gLog.clear();
                Reporter reporter(charSize, 0x55);
                if (side == 0) {
                    Log("IBR(1)");
                    char header[64];
                    std::snprintf(header, sizeof(header), "\r\nInstruction data: (EIP is 0x%08x)", context.Eip);
                    appbase::WriteLine(&reporter.get()->mLog, nullptr, header, static_cast<uint32_t>(std::strlen(header)));
                    auto hex = ctx.Original<void(__thiscall *)(void *, uint32_t, const uint8_t *, uint32_t, int32_t, int32_t, int32_t)>(kHexDump);
                    hex(reporter.raw, shownStart, readable.data(), static_cast<uint32_t>(readable.size()), 0x20, 1, 1);
                    expected = Joined();
                } else {
                    appbase::WriteInstructionData(reporter.get(), nullptr, &context);
                    ours = Joined();
                }
            }
            ExpectSame(ctx, label, expected, ours);
            continue;
        }
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            Reporter reporter(charSize, 0x55);
            if (side == 0) orig(reporter.raw, &context);
            else appbase::WriteInstructionData(reporter.get(), nullptr, &context);
            results[side] = Joined();
        }
        ExpectSame(ctx, label, results[0], results[1]);
    }
    VirtualFree(memory, 0, MEM_RELEASE);
    gBadRead = FALSE;
}

// ---- 0x1bd1de ----

DIFF_TEST(ReportWrite_WriteStackData, kStackData)
{
    Slots slots(ctx);
    std::memcpy(gFileVt, ctx.image.At<void>(cRZTextFile::kVTable), sizeof(gFileVt));
    gFileVt[16] = reinterpret_cast<void *>(&FakeWrite);
    auto orig = ctx.Original<void(__thiscall *)(void *, const CONTEXT *)>(kStackData);
    std::mt19937 rng(0x1bd1de);
    // A stand-in stack: 0x1000 bytes of random data.
    alignas(16) static uint8_t stack[0x1000];
    for (uint8_t &b : stack) b = static_cast<uint8_t>(rng());
    const uint32_t limit = Address(stack), base = limit + sizeof(stack);
    for (int run = 0; run < 700; ++run) {
        uint32_t esp;
        switch (rng() % 10) {
        case 0: esp = limit - 1 - rng() % 3; break;
        case 1: esp = limit + rng() % 4; break;                     // the window starts at the limit
        case 2: esp = limit + 0x17c + rng() % 8; break;             // the lower edge of the window next to the limit
        case 3: esp = base - 0x180 - 4 + rng() % 8; break;          // the upper edge next to the base
        case 4: esp = base - 1 - rng() % 3; break;
        case 5: esp = base + rng() % 3; break;
        case 6: esp = base + 0x1000; break;
        case 7: esp = static_cast<uint32_t>(rng()); break;
        default: esp = limit + rng() % sizeof(stack); break;
        }
        CONTEXT context;
        for (size_t i = 0; i < sizeof(context); ++i) reinterpret_cast<uint8_t *>(&context)[i] = static_cast<uint8_t>(rng());
        context.Esp = esp;
        const int32_t charSize = rng() % 5 == 0 ? 2 : 0;
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            Reporter reporter(charSize, 0x66);
            {
                StackBounds bounds(base, limit);
                if (side == 0) orig(reporter.raw, &context);
                else appbase::WriteStackData(reporter.get(), nullptr, &context);
            }
            results[side] = Joined();
        }
        char label[64];
        std::snprintf(label, sizeof(label), "stack data run %d esp-limit %d", run, static_cast<int32_t>(esp - limit));
        ExpectSame(ctx, label, results[0], results[1]);
    }

    // A read that faults: the limit moved below the real stack, into reserved memory (not into the guard
    // page, which would be consumed by the first side). The exception dispatcher still finds the
    // frames (they lie between the new limit and the real base).
    const uint32_t realBase = __readfsdword(4), realLimit = __readfsdword(8);
    const uint32_t lowLimit = (realLimit - 0x10000) & ~0xfffu;
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(reinterpret_cast<void *>(static_cast<uintptr_t>(lowLimit)), &mbi, sizeof(mbi)) && mbi.State == MEM_RESERVE) {
        // The original's SEH handler does not run in the harness (the exception would end the run), so
        // only ours runs; the expectation is the two lines the handler writes.
        for (int run = 0; run < 12; ++run) {
            CONTEXT context;
            std::memset(&context, 0, sizeof(context));
            context.Esp = lowLimit + 0x180 + rng() % 0x100; // the window starts in the unreadable memory
            const int32_t charSize = run % 3 == 0 ? 2 : 0;
            std::string expected, ours;
            for (int side = 0; side < 2; ++side) {
                gLog.clear();
                Reporter reporter(charSize, 0x66);
                if (side == 0) {
                    char header[64];
                    std::snprintf(header, sizeof(header), "\r\nStack data: (ESP is 0x%08x)", context.Esp);
                    appbase::WriteLine(&reporter.get()->mLog, nullptr, header, static_cast<uint32_t>(std::strlen(header)));
                    const char *const note = "   Stack data is not readable -- exception during read.";
                    appbase::WriteLine(&reporter.get()->mLog, nullptr, note, static_cast<uint32_t>(std::strlen(note)));
                    expected = Joined();
                } else {
                    StackBounds bounds(realBase, lowLimit);
                    appbase::WriteStackData(reporter.get(), nullptr, &context);
                    ours = Joined();
                }
            }
            ExpectSame(ctx, "stack data, faulting read", expected, ours);
        }
    }
}
