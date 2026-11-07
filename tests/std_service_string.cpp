// Differential test for src/std/GZServiceString.h: the service-allocator string's buffer allocation (0x177110).
//
// The allocator service is a fake whose slot 3 logs the size and hands out a heap block; the framework (game global
// 0x10c9378) is a fake whose slot 5 hands out that service, so the first-use path (cache 0x10c95d4 empty, guard
// 0x10c95d8 at 0) goes through GetAllocatorService as in the game. Compared: the returned block's presence, the
// capacity after the call, the log, the cached service and the guard's state. A throwing service checks that the
// capacity stays incremented.
#include "difftest.h"
#include "std/GZServiceString.h"

#include <windows.h>

#include <climits>
#include <cstdarg>
#include <cstdio>
#include <random>
#include <string>

namespace {

constexpr uint32_t kRva = 0x177110;
constexpr uint32_t kFramework = 0x10c9378;
constexpr uint32_t kAllocCache = 0x10c95d4;
constexpr uint32_t kAllocGuard = 0x10c95d8;
constexpr uint32_t kAllocatorServiceId = 0x3ae4bea3;

std::string gLog;
bool gThrow = false;

void Log(const char *fmt, ...)
{
    char buf[128];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    gLog += buf;
    gLog += "|";
}

struct Obj {
    void **vtable;
};

Obj gFramework, gAlloc;
void *gFrameworkVT[8], *gAllocVT[8];

uint32_t __fastcall FwQuery(Obj *, void *, uint32_t a, uint32_t b, void **out)
{
    Log("query %x %x", a, b);
    *out = a == kAllocatorServiceId ? &gAlloc : nullptr;
    return 1;
}

void *__fastcall AllocAlloc(Obj *, void *, uint32_t size)
{
    Log("alloc %u", size);
    if (gThrow) throw 5;
    return HeapAlloc(GetProcessHeap(), 0, size);
}

void *__fastcall Unexpected(Obj *, void *)
{
    Log("unexpected slot");
    return nullptr;
}

void InitFakes()
{
    for (void *&slot : gFrameworkVT) slot = reinterpret_cast<void *>(&Unexpected);
    for (void *&slot : gAllocVT) slot = reinterpret_cast<void *>(&Unexpected);
    gFramework.vtable = gFrameworkVT;
    gFrameworkVT[5] = reinterpret_cast<void *>(&FwQuery);
    gAlloc.vtable = gAllocVT;
    gAllocVT[3] = reinterpret_cast<void *>(&AllocAlloc);
}

// Saves the three game globals and puts them back.
struct Globals {
    difftest::Context &ctx;
    uint32_t saved[3];
    static constexpr uint32_t kRvas[3] = {kFramework, kAllocCache, kAllocGuard};
    explicit Globals(difftest::Context &c) : ctx(c)
    {
        for (int i = 0; i < 3; ++i) saved[i] = *ctx.image.At<uint32_t>(kRvas[i]);
    }
    ~Globals()
    {
        for (int i = 0; i < 3; ++i) *ctx.image.At<uint32_t>(kRvas[i]) = saved[i];
    }
    void Set(uint32_t rva, uint32_t v) const { *ctx.image.At<uint32_t>(rva) = v; }
    uint32_t Get(uint32_t rva) const { return *ctx.image.At<uint32_t>(rva); }
};

// 0: untouched, 1: initialised (an epoch), 2: still marked busy (-1)
int GuardClass(uint32_t g)
{
    if (g == 0) return 0;
    if (g == 0xffffffffu) return 2;
    return 1;
}

struct Outcome {
    bool gotBlock = false;
    bool threw = false;
    uint32_t capacity = 0;
    std::string log;
    uint32_t cached = 0;
    int guard = 0;
    bool operator==(const Outcome &o) const
    {
        return gotBlock == o.gotBlock && threw == o.threw && capacity == o.capacity && log == o.log &&
               cached == o.cached && guard == o.guard;
    }
};

std::string Describe(const Outcome &o)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), "block %d threw %d capacity %u cached %x guard %d log %s", o.gotBlock, o.threw,
                  o.capacity, o.cached, o.guard, o.log.c_str());
    return buf;
}

} // namespace

DIFF_TEST(GZServiceString_AllocateForCapacity, kRva)
{
    InitFakes();
    Globals g(ctx);
    auto original = ctx.Original<char *(__cdecl *)(void *, uint32_t *)>(kRva);
    std::mt19937 rng(0x177110);
    const uint32_t fixed[] = {0, 1, 15, 16, 31, 32, 0x100, 0xfff, 0x1000, 0x7ffffffe};
    for (int round = 0; round < 60; ++round) {
        const uint32_t start = round < 10 ? fixed[round] : rng() % 0x10000;
        for (int firstUse = 0; firstUse < 2; ++firstUse) {
            for (int fail = 0; fail < 2; ++fail) {
                Outcome out[2];
                char *blocks[2] = {};
                for (int side = 0; side < 2; ++side) {
                    g.Set(kFramework, reinterpret_cast<uint32_t>(&gFramework));
                    g.Set(kAllocCache, firstUse ? 0 : reinterpret_cast<uint32_t>(&gAlloc));
                    g.Set(kAllocGuard, firstUse ? 0 : static_cast<uint32_t>(INT_MIN));
                    gLog.clear();
                    gThrow = fail != 0;
                    uint32_t capacity = start;
                    uint8_t allocator = 0x5a;
                    try {
                        blocks[side] = side == 0 ? original(&allocator, &capacity)
                                                 : GZServiceString::AllocateForCapacity(&allocator, &capacity);
                    } catch (int) {
                        out[side].threw = true;
                    }
                    out[side].gotBlock = blocks[side] != nullptr;
                    out[side].capacity = capacity;
                    out[side].log = gLog;
                    out[side].cached = g.Get(kAllocCache);
                    out[side].guard = GuardClass(g.Get(kAllocGuard));
                }
                gThrow = false;
                ++ctx.cases;
                if (!(out[0] == out[1]))
                    ctx.Fail("capacity %u firstUse %d fail %d\n  orig: %s\n  ours: %s", start, firstUse, fail,
                             Describe(out[0]).c_str(), Describe(out[1]).c_str());
                for (char *block : blocks)
                    if (block) HeapFree(GetProcessHeap(), 0, block);
            }
        }
    }
}
