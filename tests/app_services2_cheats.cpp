// Differential tests for src/app/AppServices2.*: the testing-cheats flag of cTSGlobals (slots 31 and 56) and
// the helpers it uses (the string tidy 0x1bf753, the allocator free 0x1bf84e, the service query 0x1cbace).
//
// The framework (game global 0x10c9378), the cheats object, its property set and the allocator service are
// fakes with recording slots; the game's statics (guards and cached pointers) are reset before each side
// runs. The string literal arrives in the property-set slots as a std::string: its text, size and capacity
// are logged, and the allocator fake frees the buffers for real, so a missed or doubled free shows.
#include "app/AppServices2.h"
#include "difftest.h"
#include "game/game.h"

#include <windows.h>

#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kGet = 0xc2cba3, kSet = 0xc2c9f6, kTidy = 0x1bf753, kFree = 0x1bf84e, kAcquire = 0x1cbace;
constexpr uint32_t kFramework = 0x10c9378, kAllocCache = 0x10c95d4, kAllocGuard = 0x10c95d8;
constexpr uint32_t kGetObj = 0x10fdfd8, kGetGuard = 0x10fdfdc, kSetObj = 0x10fdfe0, kSetGuard = 0x10fdfe4;

std::vector<std::string> gLog;

void Log(const char *fmt, ...)
{
    char buf[512];
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

struct Obj {
    void **vtable;
};

std::string Describe(const void *str)
{
    const auto *r = static_cast<const as2::RawString *>(str);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s/%u/%u", r->mCapacity > 15 ? r->mPtr : r->mBuf, r->mSize, r->mCapacity);
    return buf;
}

// Script for the fakes.
struct Script {
    uint32_t hasResult = 1; // slot 10 of the property set
    uint8_t getValue = 1;   // what slot 9 writes
    int throwAt = 0;        // 1 cheats slot 3, 2 has, 3 get, 4 set, 5 framework query
    bool provide = true;    // the framework hands out the cheats object
} gScript;

Obj gFramework, gCheats, gProps, gAlloc;
void *gFrameworkVT[8], *gCheatsVT[8], *gPropsVT[12], *gAllocVT[8];

void MaybeThrow(int where)
{
    if (gScript.throwAt == where) throw 7;
}

uint32_t __fastcall FwQuery(Obj *, void *, uint32_t a, uint32_t b, void **out)
{
    Log("q %x %x", a, b);
    MaybeThrow(5);
    if (a == 0x3ae4bea3) *out = &gAlloc; // GetAllocatorService
    else *out = gScript.provide ? &gCheats : nullptr;
    return 1;
}
uint32_t __fastcall CheatsRelease(Obj *, void *)
{
    Log("release");
    return 0;
}
void *__fastcall CheatsSlot3(Obj *, void *)
{
    Log("c3");
    MaybeThrow(1);
    return &gProps;
}
std::string AnyText(const void *any)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "vt+%x v%u", static_cast<unsigned>(*static_cast<const uintptr_t *>(any) - GameBase()), static_cast<const uint8_t *>(any)[4]);
    return buf;
}
uint32_t __fastcall PropHas(Obj *, void *, void *name)
{
    Log("has %s", Describe(name).c_str());
    MaybeThrow(2);
    return gScript.hasResult;
}
void __fastcall PropGet(Obj *, void *, void *name, void *any)
{
    Log("get %s %s", Describe(name).c_str(), AnyText(any).c_str());
    MaybeThrow(3);
    static_cast<uint8_t *>(any)[4] = gScript.getValue;
}
void __fastcall PropSet(Obj *, void *, void *name, void *any)
{
    Log("set %s %s", Describe(name).c_str(), AnyText(any).c_str());
    MaybeThrow(4);
}
void *__fastcall AllocAlloc(Obj *, void *, uint32_t size)
{
    Log("alloc %u", size);
    return HeapAlloc(GetProcessHeap(), 0, size);
}
void __fastcall AllocFree(Obj *, void *, void *mem)
{
    Log("free");
    HeapFree(GetProcessHeap(), 0, mem);
}

void InitFakes()
{
    // Every test starts from the default script: a test that ends with throwAt set (the cheats tests' last case throws
    // from the framework query) would otherwise make the next one's first-use fetch throw.
    gScript = Script{};
    gFramework.vtable = gFrameworkVT;
    gFrameworkVT[5] = reinterpret_cast<void *>(&FwQuery);
    gCheats.vtable = gCheatsVT;
    gCheatsVT[2] = reinterpret_cast<void *>(&CheatsRelease);
    gCheatsVT[3] = reinterpret_cast<void *>(&CheatsSlot3);
    gProps.vtable = gPropsVT;
    gPropsVT[3] = reinterpret_cast<void *>(&PropSet);
    gPropsVT[9] = reinterpret_cast<void *>(&PropGet);
    gPropsVT[10] = reinterpret_cast<void *>(&PropHas);
    gAlloc.vtable = gAllocVT;
    gAllocVT[3] = reinterpret_cast<void *>(&AllocAlloc);
    gAllocVT[4] = reinterpret_cast<void *>(&AllocFree);
}

// Saves and restores the game globals the tests move.
struct Globals {
    difftest::Context &ctx;
    uint32_t saved[8];
    static constexpr uint32_t kRvas[8] = {kFramework, kAllocCache, kAllocGuard, kGetObj, kGetGuard, kSetObj, kSetGuard, 0};
    explicit Globals(difftest::Context &c) : ctx(c)
    {
        for (int i = 0; i < 7; ++i) saved[i] = *ctx.image.At<uint32_t>(kRvas[i]);
    }
    ~Globals()
    {
        for (int i = 0; i < 7; ++i) *ctx.image.At<uint32_t>(kRvas[i]) = saved[i];
    }
    void Set(uint32_t rva, uint32_t v) const { *ctx.image.At<uint32_t>(rva) = v; }
    uint32_t Get(uint32_t rva) const { return *ctx.image.At<uint32_t>(rva); }
};

// 0: guard untouched, 1: initialised, 2: neither (aborted back to 0 or still busy)
int GuardClass(uint32_t g)
{
    if (g == 0) return 0;
    if (g == 0xffffffffu) return 3;
    return 1;
}

constexpr int kCached = 0, kCachedNull = 1, kUninit = 2;
constexpr int kMaxThrow = 5;

// state: how the cheat statics start. frameworkPresent: the global points at the fake framework.
void Arm(const Globals &g, uint32_t objRva, uint32_t guardRva, int state, bool frameworkPresent)
{
    g.Set(kFramework, frameworkPresent ? reinterpret_cast<uint32_t>(&gFramework) : 0);
    g.Set(kAllocCache, reinterpret_cast<uint32_t>(&gAlloc));
    g.Set(kAllocGuard, static_cast<uint32_t>(INT_MIN));
    g.Set(objRva, state == kCached ? reinterpret_cast<uint32_t>(&gCheats) : 0);
    g.Set(guardRva, state == kUninit ? 0 : static_cast<uint32_t>(INT_MIN));
}

struct Outcome {
    uint32_t ret = 0;
    std::string log;
    uint32_t object = 0;
    int guard = 0;
    bool threw = false;
};

bool Same(const Outcome &a, const Outcome &b, bool compareRet)
{
    return (!compareRet || a.ret == b.ret) && a.log == b.log && (a.object == b.object || (a.object && b.object)) && a.guard == b.guard && a.threw == b.threw;
}

const uint32_t kHasValues[] = {0, 1, 0x100, 0x1ff, 0xffffff00u, 0x80};
const uint8_t kGetValues[] = {0, 1, 2, 0xff};

} // namespace

// Slot 31 (0xc2cba3): the flag getter.
DIFF_TEST(AppServices2_TestingCheatsEnabled, kGet)
{
    InitFakes();
    Globals g(ctx);
    auto original = ctx.Original<uint32_t(__thiscall *)(void *)>(kGet);
    for (int state = kCached; state <= kUninit; ++state) {
        for (int fw = 0; fw < 2; ++fw) {
            for (int provide = 0; provide < 2; ++provide) {
                for (int throwAt = 0; throwAt <= kMaxThrow; ++throwAt) {
                    for (uint32_t has : kHasValues) {
                        for (uint8_t value : kGetValues) {
                            gScript = Script{};
                            gScript.hasResult = has;
                            gScript.getValue = value;
                            gScript.throwAt = throwAt;
                            gScript.provide = provide != 0;
                            Outcome out[2];
                            for (int side = 0; side < 2; ++side) {
                                Arm(g, kGetObj, kGetGuard, state, fw != 0);
                                gLog.clear();                                try {
                                    out[side].ret = (side == 0) ? original(reinterpret_cast<void *>(0x1234))
                                                                : as2::TestingCheatsEnabled(reinterpret_cast<void *>(0x1234), nullptr);
                                } catch (int) {
                                    out[side].threw = true;
                                }
                                out[side].ret &= 0xff; // only al is defined
                                out[side].log = Joined();
                                out[side].object = g.Get(kGetObj);
                                out[side].guard = GuardClass(g.Get(kGetGuard));
                                if (state == kUninit && out[side].guard == 1) out[side].guard = 4; // initialised
                            }
                            ++ctx.cases;
                            if (!Same(out[0], out[1], !out[0].threw)) {
                                ctx.Fail("get state %d fw %d provide %d throw %d has %x value %u: ret %x/%x guard %d/%d obj %d/%d threw %d/%d\n  orig: %s\n  ours: %s",
                                         state, fw, provide, throwAt, has, value, out[0].ret, out[1].ret, out[0].guard, out[1].guard, out[0].object != 0, out[1].object != 0,
                                         out[0].threw, out[1].threw, out[0].log.c_str(), out[1].log.c_str());
                            }
                        }
                    }
                }
            }
        }
    }
}

// Slot 56 (0xc2c9f6): the flag setter.
DIFF_TEST(AppServices2_SetTestingCheatsEnabled, kSet)
{
    InitFakes();
    Globals g(ctx);
    auto original = ctx.Original<void(__thiscall *)(void *, uint32_t)>(kSet);
    const uint32_t kValues[] = {0, 1, 2, 0x1ff, 0xffffff00u, 0xabcdef80u};
    for (int state = kCached; state <= kUninit; ++state) {
        for (int fw = 0; fw < 2; ++fw) {
            for (int provide = 0; provide < 2; ++provide) {
                for (int throwAt = 0; throwAt <= kMaxThrow; ++throwAt) {
                    for (uint32_t value : kValues) {
                        gScript = Script{};
                        gScript.throwAt = throwAt;
                        gScript.provide = provide != 0;
                        Outcome out[2];
                        for (int side = 0; side < 2; ++side) {
                            Arm(g, kSetObj, kSetGuard, state, fw != 0);
                            gLog.clear();
                            try {
                                if (side == 0) original(reinterpret_cast<void *>(0x1234), value);
                                else as2::SetTestingCheatsEnabled(reinterpret_cast<void *>(0x1234), nullptr, value);
                            } catch (int) {
                                out[side].threw = true;
                            }
                            out[side].log = Joined();
                            out[side].object = g.Get(kSetObj);
                            out[side].guard = GuardClass(g.Get(kSetGuard));
                            if (state == kUninit && out[side].guard == 1) out[side].guard = 4;
                        }
                        ++ctx.cases;
                        if (!Same(out[0], out[1], false)) {
                            ctx.Fail("set state %d fw %d provide %d throw %d value %x: guard %d/%d obj %d/%d threw %d/%d\n  orig: %s\n  ours: %s", state, fw, provide, throwAt, value,
                                     out[0].guard, out[1].guard, out[0].object != 0, out[1].object != 0, out[0].threw, out[1].threw, out[0].log.c_str(), out[1].log.c_str());
                        }
                    }
                }
            }
        }
    }
}

// 0x1cbace: the query helper.
DIFF_TEST(AppServices2_AcquireTestingService, kAcquire)
{
    InitFakes();
    Globals g(ctx);
    auto original = ctx.Original<ScopedRef *(__thiscall *)(ScopedRef *)>(kAcquire);
    for (int fw = 0; fw < 2; ++fw) {
        for (int provide = 0; provide < 2; ++provide) {
            gScript = Script{};
            gScript.provide = provide != 0;
            for (int fill = 0; fill < 3; ++fill) {
                Outcome out[2];
                ScopedRef refs[2] = {{reinterpret_cast<void *>(0x5a5a5a5a * (fill + 1))}, {reinterpret_cast<void *>(0x5a5a5a5a * (fill + 1))}};
                ScopedRef *ret[2];
                for (int side = 0; side < 2; ++side) {
                    g.Set(kFramework, fw ? reinterpret_cast<uint32_t>(&gFramework) : 0);
                    gLog.clear();
                    ret[side] = (side == 0) ? original(&refs[0]) : as2::AcquireTestingService(&refs[1], nullptr);
                    out[side].log = Joined();
                }
                ++ctx.cases;
                if (ret[0] != &refs[0] || ret[1] != &refs[1] || refs[0].mObject != refs[1].mObject || out[0].log != out[1].log)
                    ctx.Fail("acquire fw %d provide %d: obj %p/%p\n  orig: %s\n  ours: %s", fw, provide, refs[0].mObject, refs[1].mObject, out[0].log.c_str(), out[1].log.c_str());
            }
        }
    }
}

// 0x1bf753: the string tidy that frees through the allocator service.
DIFF_TEST(AppServices2_TidyString, kTidy)
{
    InitFakes();
    Globals g(ctx);
    auto original = ctx.Original<void(__thiscall *)(void *)>(kTidy);
    const size_t kLengths[] = {0, 1, 14, 15, 16, 17, 31, 32, 47, 100, 300};
    for (size_t length : kLengths) {
        for (int fill = 0; fill < 3; ++fill) {
            as2::RawString strs[2];
            Outcome out[2];
            for (int side = 0; side < 2; ++side) {
                std::memset(&strs[side], 0x30 + fill * 7, sizeof(strs[side]));
                // A heap string (capacity above 15, from our assign) or the in-place form; fill 2 shrinks the size.
                std::string text(length, static_cast<char>('a' + fill));
                g.Set(kAllocCache, reinterpret_cast<uint32_t>(&gAlloc));
                g.Set(kAllocGuard, static_cast<uint32_t>(INT_MIN));
                std::memset(strs[side].mBuf, 0, sizeof(strs[side].mBuf));
                strs[side].mSize = strs[side].mCapacity = 0;
                as2::AssignServiceString(&strs[side], nullptr, text.c_str(), static_cast<uint32_t>(length));
                if (fill == 2) strs[side].mSize = static_cast<uint32_t>(length / 2);
                gLog.clear();
                if (side == 0) original(&strs[0]);
                else as2::TidyString(&strs[1], nullptr);
                out[side].log = Joined();
            }
            ++ctx.cases;
            if (std::memcmp(&strs[0].mSize, &strs[1].mSize, 8) != 0 || strs[0].mBuf[0] != strs[1].mBuf[0] || strs[0].mBuf[0] != 0 || out[0].log != out[1].log)
                ctx.Fail("tidy length %zu fill %d: size %u/%u cap %u/%u\n  orig: %s\n  ours: %s", length, fill, strs[0].mSize, strs[1].mSize, strs[0].mCapacity, strs[1].mCapacity,
                         out[0].log.c_str(), out[1].log.c_str());
        }
    }
}

// Capacities at the edge of the heap form: 15 is in place (never freed, the pointer bytes are text), 16 and up
// free the buffer (hand-made strings; the library never produces capacity 16).
DIFF_TEST(AppServices2_TidyStringEdges, kTidy)
{
    InitFakes();
    Globals g(ctx);
    auto original = ctx.Original<void(__thiscall *)(void *)>(kTidy);
    for (uint32_t capacity : {14u, 15u, 16u, 17u, 31u}) {
        std::string logs[2];
        as2::RawString strs[2];
        for (int side = 0; side < 2; ++side) {
            g.Set(kAllocCache, reinterpret_cast<uint32_t>(&gAlloc));
            g.Set(kAllocGuard, static_cast<uint32_t>(INT_MIN));
            void *block = HeapAlloc(GetProcessHeap(), 0, 64);
            std::memset(&strs[side], 0, sizeof(strs[side]));
            strs[side].mPtr = static_cast<char *>(block);
            strs[side].mSize = 5;
            strs[side].mCapacity = capacity;
            gLog.clear();
            if (side == 0) original(&strs[0]);
            else as2::TidyString(&strs[1], nullptr);
            logs[side] = Joined();
            if (capacity <= 15) HeapFree(GetProcessHeap(), 0, block); // not freed by tidy: the pointer bytes were overwritten, free the saved block
        }
        ++ctx.cases;
        if (logs[0] != logs[1] || strs[0].mSize != strs[1].mSize || strs[0].mCapacity != strs[1].mCapacity)
            ctx.Fail("tidy edge cap %u: %s / %s", capacity, logs[0].c_str(), logs[1].c_str());
    }
}

// 0x1be72c: assign(text, length) of the service-allocated strings; every length around the in-place limit
// and the growth steps, objects pre-filled with garbage (the original does not look at them), the
// length_error above 0x7fffffff (raised before the object or the allocator is touched).
DIFF_TEST(AppServices2_AssignServiceString, 0x1be72c)
{
    InitFakes();
    Globals g(ctx);
    auto original = ctx.Original<void(__thiscall *)(void *, const char *, uint32_t)>(0x1be72c);
    std::string source(1200, 'x');
    for (size_t i = 0; i < source.size(); ++i) source[i] = static_cast<char>('A' + i % 53);
    std::vector<uint32_t> lengths;
    for (uint32_t n = 0; n <= 80; ++n) lengths.push_back(n);
    for (uint32_t n : {95u, 96u, 100u, 127u, 128u, 200u, 255u, 256u, 500u, 1023u, 1024u, 1100u}) lengths.push_back(n);
    for (uint32_t n : lengths) {
        for (int fill = 0; fill < 2; ++fill) {
            as2::RawString strs[2];
            Outcome out[2];
            bool threw[2] = {false, false};
            for (int side = 0; side < 2; ++side) {
                std::memset(&strs[side], fill ? 0xcd : 0x00, sizeof(strs[side]));
                Arm(g, kGetObj, kGetGuard, kCached, true);
                gLog.clear();
                if (side == 0) original(&strs[0], source.c_str(), n);
                else as2::AssignServiceString(&strs[1], nullptr, source.c_str(), n);
                out[side].log = Joined();
            }
            ++ctx.cases;
            const bool inPlace = n <= 15;
            const char *a = inPlace ? strs[0].mBuf : strs[0].mPtr, *b = inPlace ? strs[1].mBuf : strs[1].mPtr;
            if (strs[0].mSize != strs[1].mSize || strs[0].mCapacity != strs[1].mCapacity || std::memcmp(a, b, n + 1) != 0 || out[0].log != out[1].log)
                ctx.Fail("assign %u fill %d: size %u/%u cap %u/%u\n  orig: %s\n  ours: %s", n, fill, strs[0].mSize, strs[1].mSize, strs[0].mCapacity, strs[1].mCapacity,
                         out[0].log.c_str(), out[1].log.c_str());
            if (inPlace && std::memcmp(strs[0].mBuf, strs[1].mBuf, 16) != 0) ctx.Fail("assign %u fill %d: in-place bytes differ beyond the terminator", n, fill);
            if (!inPlace) {
                HeapFree(GetProcessHeap(), 0, strs[0].mPtr);
                HeapFree(GetProcessHeap(), 0, strs[1].mPtr);
            }
        }
    }
    // length_error: both throw, and nothing is allocated or written.
    for (uint32_t n : {0x80000000u, 0xffffffffu, 0x80000001u}) {
        bool threw[2] = {false, false};
        as2::RawString strs[2];
        for (int side = 0; side < 2; ++side) {
            std::memset(&strs[side], 0xcd, sizeof(strs[side]));
            gLog.clear();
            try {
                if (side == 0) original(&strs[0], source.c_str(), n);
                else as2::AssignServiceString(&strs[1], nullptr, source.c_str(), n);
            } catch (...) {
                threw[side] = true;
            }
        }
        ++ctx.cases;
        if (!threw[0] || !threw[1] || std::memcmp(&strs[0], &strs[1], sizeof(strs[0])) != 0 || !gLog.empty()) ctx.Fail("assign %x: threw %d/%d", n, threw[0], threw[1]);
    }
}

// 0x1bf795: the allocation through the service, cached or fetched on first use.
DIFF_TEST(AppServices2_AllocateViaAllocatorService, 0x1bf795)
{
    InitFakes();
    Globals g(ctx);
    auto original = ctx.Original<void *(__stdcall *)(uint32_t, uint32_t)>(0x1bf795);
    for (int firstUse = 0; firstUse < 2; ++firstUse) {
        for (uint32_t size : {1u, 16u, 17u, 32u, 1000u}) {
            void *got[2];
            Outcome out[2];
            uint32_t cached[2] = {};
            for (int side = 0; side < 2; ++side) {
                g.Set(kFramework, reinterpret_cast<uint32_t>(&gFramework));
                g.Set(kAllocCache, firstUse ? 0 : reinterpret_cast<uint32_t>(&gAlloc));
                g.Set(kAllocGuard, firstUse ? 0 : static_cast<uint32_t>(INT_MIN));
                gLog.clear();
                got[side] = side == 0 ? original(size, 0x5a5a5a5a) : as2::AllocateViaAllocatorService(size, 0x5a5a5a5a);
                out[side].log = Joined();
                cached[side] = g.Get(kAllocCache);
                out[side].guard = GuardClass(g.Get(kAllocGuard));
            }
            ++ctx.cases;
            if (!got[0] || !got[1] || cached[0] != cached[1] || out[0].log != out[1].log || out[0].guard != out[1].guard || (firstUse && out[0].guard != 1))
                ctx.Fail("allocate firstUse %d size %u: cached %x/%x guard %d/%d\n  orig: %s\n  ours: %s", firstUse, size, cached[0], cached[1], out[0].guard, out[1].guard,
                         out[0].log.c_str(), out[1].log.c_str());
            HeapFree(GetProcessHeap(), 0, got[0]);
            HeapFree(GetProcessHeap(), 0, got[1]);
        }
    }
}

// 0x1bf84e: the free through the allocator service, with the service cached or fetched on first use.
DIFF_TEST(AppServices2_FreeViaAllocatorService, kFree)
{
    InitFakes();
    Globals g(ctx);
    auto original = ctx.Original<void(__stdcall *)(void *, uint32_t)>(kFree);
    for (int firstUse = 0; firstUse < 2; ++firstUse) {
        for (int fw = 0; fw < 2; ++fw) {
            for (int i = 0; i < 6; ++i) {
                gScript = Script{};
                Outcome out[2];
                uint32_t cached[2] = {};
                for (int side = 0; side < 2; ++side) {
                    g.Set(kFramework, reinterpret_cast<uint32_t>(&gFramework));
                    g.Set(kAllocCache, firstUse ? 0 : reinterpret_cast<uint32_t>(&gAlloc));
                    g.Set(kAllocGuard, firstUse ? 0 : static_cast<uint32_t>(INT_MIN));
                    if (!fw) g.Set(kFramework, 0);
                    void *mem = HeapAlloc(GetProcessHeap(), 0, 64);
                    gLog.clear();
                    if (firstUse && !fw && side == 0) { /* the original's own static instance answers; skipped below */ }
                    if (firstUse && !fw) {
                        // Without a framework GetAllocatorService builds its own static service: leave that path to the
                        // GetAllocatorService tests (it registers an exit handler).
                        HeapFree(GetProcessHeap(), 0, mem);
                        continue;
                    }
                    if (side == 0) original(mem, 0x5a5a5a5a);
                    else as2::FreeViaAllocatorService(mem, 0x5a5a5a5a);
                    out[side].log = Joined();
                    cached[side] = g.Get(kAllocCache);
                    out[side].guard = GuardClass(g.Get(kAllocGuard));
                }
                if (firstUse && !fw) continue;
                ++ctx.cases;
                if (cached[0] != cached[1] || out[0].log != out[1].log || out[0].guard != out[1].guard || (firstUse && out[0].guard != 1))
                    ctx.Fail("free firstUse %d fw %d: cached %x/%x guard %d/%d\n  orig: %s\n  ours: %s", firstUse, fw, cached[0], cached[1], out[0].guard, out[1].guard, out[0].log.c_str(),
                             out[1].log.c_str());
            }
        }
    }
}
