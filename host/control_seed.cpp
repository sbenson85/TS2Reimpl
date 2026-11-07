// Seed control for scripted runs (roadmap milestone B). Like host/control.cpp it is test control,
// not a replacement: it fixes an input and leaves the implementation under test alone.
//
// The game seeds its generators from the clock by passing -1 (cRZRandom and cRZRandomSuper: the
// constructors and Seed; the implementation then mixes time() and clock()). With TS2HOST_SEED=<n>
// set, a small detour at each of those four entry points replaces a -1 argument with a fixed value
// computed from n, the caller's return address and how many seeds that caller has had (so callers
// that interleave differently between runs still get the same seeds), and continues into whatever
// runs there: the original
// code on reference runs (TS2HOST_NOHOOKS), our replacement otherwise. So both kinds of run get the
// same seeds, while neither shares the other's generator. Seeds are logged with the caller's return
// address, so the order of seeding and seeds taken from elsewhere can be compared between runs.
//
// Our own code also seeds internally with -1 (not through the entry points); it takes the clock
// reading from gRZRandomClockSeedOverride (src/rz/cRZRandom.h), which this sets. Where our code
// names the original's call site (cRZRandom::ConstructAt; so far cGZCOM's constructor, the only
// internal seed seen in-game), the seed is keyed by that site exactly as the original's call
// through the entry point is, so reference and replacement runs get the same seed. Others are
// keyed per generator kind.
//
// Eleven callers pass a clock reading (clock() or rdtsc) as the seed instead of -1; those seeds are
// replaced the same way. Seeds read from save files are left alone.
//
// GUID time: cGZCOM::CreateGuid and CreateGuids (0x145325, 0x1453ab; original code, not replaced)
// build GUIDs from time() and the local seconds since 2 Feb 1997 (SecondsSinceGuidEpoch 0x145774).
// Their eight calls to those two are redirected to a fixed clock that advances one second per
// time() call, so new GUIDs repeat between runs. Only these call sites are changed: the rest of the
// game keeps the real clock, and SecondsSinceGuidEpoch itself (verified by its differential test)
// is not exercised by GUID creation in controlled runs. CreateGuid and CreateGuids are ours since lane EN
// (2026-10-18): our cGZCOM reads the same fixed clock through gGZCOMGuidTimeOverride / gGZCOMGuidSecondsOverride
// (until 2026-11-04 it read the real clock, so controlled saves' GUIDs varied while this log said "8 of 8").
#include "control.h"

#include "game/game.h"
#include "hook/log.h"
#include "gz/GZCOM.h"
#include "rz/cRZRandom.h"

#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

// Entry points taking the seed as their first stack argument (__thiscall), with the length of
// whole, position-independent instructions at their start (copied into the trampoline when the
// site still holds the original code). RVAs are 1.18.0.187.
struct SeedSite {
    const char *name;
    uint32_t rva;
    uint32_t prologue;
};
constexpr SeedSite kSites[] = {
    {"cRZRandom::cRZRandom", 0x1b8bc8, 5},      // push esi; push [esp+8]
    {"cRZRandomSuper::cRZRandomSuper", 0x1b8bde, 5}, // push esi; push [esp+8]
    {"cRZRandom::Seed", 0x1b8ecb, 5},           // mov edx,[esp+4]; push edi
    {"cRZRandomSuper::Seed", 0x1b8efa, 6},      // push ecx; push ecx; mov eax,[esp+0xc]
};

// Callers that pass a clock reading as the seed instead of -1 (return addresses; established from
// the code before each call: every call to the four entry points within 48 bytes after a call to
// clock(), ReadTsc or time(), checked by hand): clock() (0x1ba00b) or the stopwatch's rdtsc (0x1c9244).
// Two of these are in code that is ours now (0xb72623 cTSSkinCatalog's constructor, 0x5049ef cTSCASSystem::Init):
// ours seeds through ClockSeedAt (rz/cRZRandom.h) with the same site, which gRZRandomClockSeedOverride keys alike.
// A replacement of any other function here must do the same.
constexpr uint32_t kClockSeedCallers[] = {
    0xb72623, // clock(), object at +0xb0 of 0xb72310's object
    0x267fe0, // clock(), local generator in 0x267ee0
    0xb9f344, // clock(), the global generator at 0x10fc6c8
    0x5049ef, // clock(), nTSCASCore::cTSCASSystem::Init (CAS: generated appearance)
    0x766af1, // rdtsc, local generator in 0x766a92
    0x1cd112, // rdtsc, cTSWebService::Init
    0xb5e974, // clock(), Seed of the global generator at 0x10fc544 (built in 0xb5e910)
    0xb5e2d9, // clock(), the global generator at 0x10fc560 (built in 0xb5e100)
    0x28bc90, // clock(), local generator in 0x28bb20
    0x73baff, // clock(), local generator (function containing 0x73baf1; no db record yet)
    0xba051d, // clock(), the global generator at 0x10fc6e8
};

uint32_t gNext;
volatile LONG gLogged;

// Replacement seeds depend on the caller and on how many seeds that caller has had, not on the
// global order, so callers that interleave differently between runs still get the same seeds.
SRWLOCK gCallerLock = SRWLOCK_INIT;
uint32_t gCallers[512], gCallerCounts[512];
uint32_t gCallerUsed;
uint32_t gPassCallers[512], gPassLast[512], gPassUsed; // last passed-through seed per caller (logging)

uint32_t Mix(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

DWORD gMainThread; // the thread that runs the image's entry point (StartSeedControl runs on it)

uint32_t ReplacementSeed(uint32_t caller)
{
    // Some callers seed on the main thread and on worker threads (e.g. 0x234198); their order
    // across threads varies, so the two get separate counts.
    if (GetCurrentThreadId() != gMainThread) caller |= 0x80000000u;
    AcquireSRWLockExclusive(&gCallerLock);
    uint32_t i = 0;
    while (i < gCallerUsed && gCallers[i] != caller) ++i;
    if (i == gCallerUsed && gCallerUsed < 512) gCallers[gCallerUsed++] = caller;
    uint32_t n = i < 512 ? gCallerCounts[i]++ : 0;
    ReleaseSRWLockExclusive(&gCallerLock);
    return Mix(gNext ^ Mix(caller) ^ (n * 0x9e3779b9u));
}

bool IsClockSeedCaller(uint32_t rva)
{
    for (uint32_t c : kClockSeedCallers)
        if (c == rva) return true;
    return false;
}

// Called for every seed passed to the four entry points: replaces -1 and logs the first 2000 calls
// (replaced or not), so seeds the game derives from elsewhere can be compared between runs too. On
// reference runs the original constructors also call Seed through its entry point, so they log two
// lines where replacement runs log one: compare runs of the same kind.
uint32_t __cdecl FilterSeed(uint32_t site, uint32_t returnAddress, uint32_t seed)
{
    uint32_t base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GameAddress<void>(0)));
    uint32_t caller = returnAddress - base;
    const char *note = "";
    if (seed == UINT32_MAX) note = " (replaced -1)";
    else if (IsClockSeedCaller(caller)) note = " (replaced clock reading)";
    if (*note) seed = ReplacementSeed(caller);
    // Replaced seeds are always logged (up to 20000); seeds passed through (e.g. the simulator's,
    // restored from its state every tick) only when they change for that caller.
    bool show = *note != 0;
    if (!show) {
        AcquireSRWLockExclusive(&gCallerLock);
        uint32_t i = 0;
        while (i < gPassUsed && gPassCallers[i] != caller) ++i;
        if (i == gPassUsed && gPassUsed < 512) gPassCallers[gPassUsed++] = caller, gPassLast[i] = ~seed;
        show = i < 512 && gPassLast[i] != seed;
        if (i < 512) gPassLast[i] = seed;
        ReleaseSRWLockExclusive(&gCallerLock);
    }
    if (show && InterlockedIncrement(&gLogged) <= 20000)
        Log("seed: %s from 0x%x (thread %lu): 0x%08x%s", kSites[site].name, caller, GetCurrentThreadId(), seed, note);
    return seed;
}

// ---- GUID time ----
constexpr uint32_t kGameTime = 0x82fbf6;           // game CRT time() (64-bit time_t)
constexpr uint32_t kSecondsSinceGuidEpoch = 0x145774;
constexpr uint32_t kTimeCalls[] = {0x145336, 0x145356, 0x145392, 0x1453c9, 0x1453e9, 0x145434};
constexpr uint32_t kEpochCalls[] = {0x14537e, 0x145402};
constexpr int64_t kFixedTimeStart = 1735689600; // 2025-01-01 00:00:00 UTC
constexpr int64_t kGuidEpoch = 855878400;       // 1997-02-02 00:00:00 UTC (the control's clock is UTC)
volatile LONG gTimeCalls;
int64_t gLastTime = kFixedTimeStart;

int64_t __cdecl FixedTime(int64_t *out)
{
    int64_t t = kFixedTimeStart + InterlockedIncrement(&gTimeCalls) - 1;
    gLastTime = t;
    if (out) *out = t;
    return t;
}

uint32_t __cdecl FixedSecondsSinceGuidEpoch()
{
    return static_cast<uint32_t>(gLastTime - kGuidEpoch);
}

bool RedirectCall(uint32_t rva, uint32_t expected, const void *to)
{
    auto *site = GameAddress<uint8_t>(rva);
    int32_t rel;
    std::memcpy(&rel, site + 1, 4);
    if (site[0] != 0xE8 || site + 5 + rel != GameAddress<uint8_t>(expected)) {
        Log("seed control: call site 0x%x is not a call to 0x%x; not controlled there", rva, expected);
        return false;
    }
    DWORD old;
    VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
    rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - reinterpret_cast<uintptr_t>(site + 5));
    std::memcpy(site + 1, &rel, 4);
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return true;
}

// ---- simulator time ----
// cTSSimulator vf4_10 (0x4827b0, original code) decides how many simulator ticks a frame runs: it
// reads a millisecond clock (the timer service's clock object, slot 0x2c at 0x482a76 once per call
// and slot 0x10 at 0x482be2 after a tick), accumulates the elapsed time in [sim+0xc0], and runs
// ceil(acc / 42 * 0.25) plus the average of the last 10 frames' ticks (at most 125), consuming 42 ms
// per tick; a 250 ms stopwatch budget (cRZStopwatch::Elapsed at 0x482c4a) ends the loop early on
// slow frames. All three make the tick count follow wall time, so the save's per-object tick
// stamps differ between runs. Under seed control the clock reads see a virtual clock that advances
// 42 ms per vf4_10 call (one tick's worth per frame) and the budget check sees 0 ms, so the same
// frame count gives the same ticks. Only these call sites change; nothing else sees the virtual clock.
constexpr uint32_t kSimClockFirst = 0x482a74;  // mov eax,[ecx]; call [eax+0x2c]
constexpr uint32_t kSimClockAfter = 0x482be0;  // mov eax,[ecx]; call [eax+0x10]
constexpr uint32_t kSimBudgetCall = 0x482c4a;  // call cRZStopwatch::Elapsed
constexpr uint32_t kStopwatchElapsed = 0x1c9256;
constexpr uint32_t kSimTickMs = 42;
uint32_t gSimClock = 1000000;

uint32_t __cdecl SimClockFirst()
{
    gSimClock += kSimTickMs;
    return gSimClock;
}

uint32_t __cdecl SimClockAfter()
{
    return gSimClock;
}

uint32_t __cdecl NoElapsed()
{
    return 0;
}

// Replaces five bytes holding exactly `expected` with a call to `to`.
bool ReplaceWithCall(uint32_t rva, const uint8_t (&expected)[5], const void *to)
{
    auto *site = GameAddress<uint8_t>(rva);
    if (std::memcmp(site, expected, 5) != 0) {
        Log("seed control: 0x%x is not the expected code; simulator time not controlled there", rva);
        return false;
    }
    DWORD old;
    VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
    site[0] = 0xE8;
    int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - reinterpret_cast<uintptr_t>(site + 5));
    std::memcpy(site + 1, &rel, 4);
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    return true;
}

// Our replacements seed internally too (e.g. our cGZCOM constructor seeds its generator with -1
// without passing through the image entry points): src/rz/cRZRandom.cpp asks here for the clock
// reading, keyed by the original's call site when our code names it, else per generator kind.
uint32_t __cdecl InternalClockSeed(int kind, uint32_t site)
{
    uint32_t seed = ReplacementSeed(site ? site : 0xf0000000u | static_cast<uint32_t>(kind));
    if (InterlockedIncrement(&gLogged) <= 20000)
        Log("seed: %s internal for 0x%x (thread %lu): 0x%08x (replaced -1 inside our code)",
            kind ? "cRZRandomSuper" : "cRZRandom", site, GetCurrentThreadId(), seed);
    return seed;
}

void Put32(uint8_t *&p, uint32_t v)
{
    std::memcpy(p, &v, 4);
    p += 4;
}

void PutRel(uint8_t *&p, const void *target)
{
    Put32(p, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target) - reinterpret_cast<uintptr_t>(p + 4)));
}

// ---- draw counting (diagnostic) ----
// With TS2HOST_DRAWS set as well, every call into the generators' draw functions is counted per
// (generator object, caller, entry point, main thread or not); the `draws` control command logs the
// entries that changed since the last `draws`, so runs can be compared at fixed frames. Replacement
// runs only: the sites must hold our hooks (the original's draw functions call each other, so
// counts on reference runs would mix nested calls in).
struct DrawSite {
    const char *name;
    uint32_t rva;
};
constexpr DrawSite kDrawSites[] = {
    {"R::Uint32", 0x1b8d75},   {"R::Uint32(n)", 0x1b8d5c}, {"R::Sint32Range", 0x1b8d47},
    {"R::Double", 0x1b8c78},   {"R::DoubleRange", 0x1b8c48}, {"S::Uint32", 0x1b8d9b},
    {"S::Uint32(n)", 0x1b8d8d}, {"S::Double", 0x1b8cba},     {"S::DoubleRange", 0x1b8c60},
};
struct DrawKey {
    uint32_t generator, caller, site, worker;
};
constexpr uint32_t kMaxDrawKeys = 4096;
DrawKey gDrawKeys[kMaxDrawKeys];
uint32_t gDrawCounts[kMaxDrawKeys], gDrawShown[kMaxDrawKeys], gDrawUsed;
volatile LONG gDrawOverflow;

void __cdecl CountDraw(uint32_t site, uint32_t returnAddress, uint32_t generator)
{
    uint32_t base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GameAddress<void>(0)));
    DrawKey key{generator, returnAddress - base, site, GetCurrentThreadId() != gMainThread};
    AcquireSRWLockExclusive(&gCallerLock);
    uint32_t i = 0;
    while (i < gDrawUsed && std::memcmp(&gDrawKeys[i], &key, sizeof(key)) != 0) ++i;
    if (i == gDrawUsed && gDrawUsed < kMaxDrawKeys) gDrawKeys[gDrawUsed++] = key;
    if (i < kMaxDrawKeys) ++gDrawCounts[i];
    else InterlockedIncrement(&gDrawOverflow);
    ReleaseSRWLockExclusive(&gCallerLock);
}

void InstallDrawCounters(uint8_t *&p)
{
    int installed = 0;
    for (uint32_t i = 0; i < sizeof(kDrawSites) / sizeof(kDrawSites[0]); ++i) {
        auto *site = GameAddress<uint8_t>(kDrawSites[i].rva);
        if (site[0] != 0xE9) {
            Log("draws: 0x%x holds no hook; not counted", kDrawSites[i].rva);
            continue;
        }
        int32_t rel;
        std::memcpy(&rel, site + 1, 4);
        const uint8_t *target = site + 5 + rel;
        uint8_t *detour = p;
        // push ecx; push ecx (generator); push [esp+8] (return address); push i
        const uint8_t save[] = {0x51, 0x51, 0xFF, 0x74, 0x24, 0x08, 0x6A};
        std::memcpy(p, save, sizeof(save));
        p += sizeof(save);
        *p++ = static_cast<uint8_t>(i);
        *p++ = 0xE8; // call CountDraw
        PutRel(p, reinterpret_cast<const void *>(&CountDraw));
        const uint8_t restore[] = {0x83, 0xC4, 0x0C, 0x59}; // add esp,12; pop ecx
        std::memcpy(p, restore, sizeof(restore));
        p += sizeof(restore);
        *p++ = 0xE9; // jmp target
        PutRel(p, target);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(detour) - reinterpret_cast<uintptr_t>(site + 5));
        std::memcpy(site + 1, &rel, 4);
        VirtualProtect(site, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
        ++installed;
    }
    Log("draws: counting at %d of 9 draw entry points", installed);
}

} // namespace

void LogDraws()
{
    AcquireSRWLockExclusive(&gCallerLock);
    uint32_t changed = 0;
    for (uint32_t i = 0; i < gDrawUsed; ++i) {
        if (gDrawCounts[i] == gDrawShown[i]) continue;
        const DrawKey &k = gDrawKeys[i];
        Log("draws: %s gen %08x from 0x%x%s: %u (+%u)", kDrawSites[k.site].name, k.generator, k.caller,
            k.worker ? " (worker)" : "", gDrawCounts[i], gDrawCounts[i] - gDrawShown[i]);
        gDrawShown[i] = gDrawCounts[i];
        ++changed;
    }
    Log("draws: %u entries changed, %u keys, %ld overflowed", changed, gDrawUsed, gDrawOverflow);
    ReleaseSRWLockExclusive(&gCallerLock);
}

void StartSeedControl()
{
    char text[32];
    if (!GetEnvironmentVariableA("TS2HOST_SEED", text, sizeof(text))) return;
    gNext = static_cast<uint32_t>(std::strtoul(text, nullptr, 0));
    gMainThread = GetCurrentThreadId();
    auto *code = static_cast<uint8_t *>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    uint8_t *p = code;
    for (uint32_t i = 0; i < sizeof(kSites) / sizeof(kSites[0]); ++i) {
        auto *site = GameAddress<uint8_t>(kSites[i].rva);
        // Where the call continues: a hook's jump target, or a copy of the original prologue.
        const uint8_t *target;
        if (site[0] == 0xE9) {
            int32_t rel;
            std::memcpy(&rel, site + 1, 4);
            target = site + 5 + rel;
        } else {
            target = p;
            std::memcpy(p, site, kSites[i].prologue);
            p += kSites[i].prologue;
            *p++ = 0xE9;
            PutRel(p, site + kSites[i].prologue);
        }
        uint8_t *detour = p;
        // push ecx; push [esp+8] (seed); push [esp+8] (return address); push i
        const uint8_t save[] = {0x51, 0xFF, 0x74, 0x24, 0x08, 0xFF, 0x74, 0x24, 0x08, 0x6A};
        std::memcpy(p, save, sizeof(save));
        p += sizeof(save);
        *p++ = static_cast<uint8_t>(i);
        *p++ = 0xE8; // call FilterSeed
        PutRel(p, reinterpret_cast<const void *>(&FilterSeed));
        const uint8_t restore[] = {0x83, 0xC4, 0x0C, 0x59, 0x89, 0x44, 0x24, 0x04}; // add esp,12; pop ecx; mov [esp+4],eax
        std::memcpy(p, restore, sizeof(restore));
        p += sizeof(restore);
        *p++ = 0xE9; // jmp target
        PutRel(p, target);

        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        site[0] = 0xE9;
        int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(detour) - reinterpret_cast<uintptr_t>(site + 5));
        std::memcpy(site + 1, &rel, 4);
        VirtualProtect(site, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    if (GetEnvironmentVariableA("TS2HOST_DRAWS", text, sizeof(text))) InstallDrawCounters(p);
    DWORD old;
    VirtualProtect(code, 4096, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), code, 4096);
    gRZRandomClockSeedOverride = &InternalClockSeed;
    int redirected = 0;
    for (uint32_t rva : kTimeCalls) redirected += RedirectCall(rva, kGameTime, reinterpret_cast<const void *>(&FixedTime));
    for (uint32_t rva : kEpochCalls)
        redirected += RedirectCall(rva, kSecondsSinceGuidEpoch, reinterpret_cast<const void *>(&FixedSecondsSinceGuidEpoch));
    // Our cGZCOM (hooked at 0x145325 / 0x1453ab) reads the same fixed clock through its override; the redirected
    // sites above cover the original's code when it is in effect.
    gGZCOMGuidTimeOverride = &FixedTime;
    gGZCOMGuidSecondsOverride = &FixedSecondsSinceGuidEpoch;
    // The line's text is part of lotcheck's fixture binding (its "seed control:" lines): keep it unchanged.
    Log("seed control: clock seeds replaced, starting at 0x%08x; GUID time fixed at %d of 8 call sites", gNext, redirected);
    Log("seed clock: our cGZCOM's GUID creation reads the same fixed clock");
    constexpr uint8_t kFirst[5] = {0x8B, 0x01, 0xFF, 0x50, 0x2C}, kAfter[5] = {0x8B, 0x01, 0xFF, 0x50, 0x10};
    int sim = ReplaceWithCall(kSimClockFirst, kFirst, reinterpret_cast<const void *>(&SimClockFirst));
    sim += ReplaceWithCall(kSimClockAfter, kAfter, reinterpret_cast<const void *>(&SimClockAfter));
    sim += RedirectCall(kSimBudgetCall, kStopwatchElapsed, reinterpret_cast<const void *>(&NoElapsed));
    Log("seed control: simulator time fixed at %d of 3 sites (%u ms per frame)", sim, kSimTickMs);
}
