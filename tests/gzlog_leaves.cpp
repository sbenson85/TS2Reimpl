// Differential tests for the leaf functions under nGZLog::cService's constructor (src/rz/nGZLog.cpp,
// src/rz/cRZThreadSignal.cpp): the thread-id store, the string hash and the thread signal.
// Imports (GetCurrentThreadId, CreateEventA) are faked through the game's import slots, which both the
// original and ours call through.
#include "difftest.h"
#include "free_recorder.h"
#include "game/vtable.h"
#include "gzlog_support.h"
#include "rz/cRZThreadSignal.h"
#include "rz/nGZLog.h"

#include <windows.h>

#include <exception>
#include <memory>
#include <stdexcept>

using namespace gzlog_test;

namespace {

constexpr uint32_t kStoreThreadId = 0x1b9cad, kHashChars = 0x9cbcdb, kSignalState = 0x9cca8b, kSignalCtor = 0x9ccaa9;
constexpr uint32_t kIatGetCurrentThreadId = 0xd9a194, kTime64 = 0x82fbf6;

DWORD gThreadIdAnswer = 0;
DWORD WINAPI FakeThreadId() { return gThreadIdAnswer; }

uintptr_t gHandleCounter = 0;
std::vector<std::string> gLog;
HANDLE WINAPI FakeCreateEvent(LPSECURITY_ATTRIBUTES attrs, BOOL manual, BOOL initial, LPCSTR name)
{
    char buf[80];
    std::snprintf(buf, sizeof(buf), "event(%p,%d,%d,%p)", static_cast<void *>(attrs), manual, initial, static_cast<const void *>(name));
    gLog.push_back(buf);
    return reinterpret_cast<HANDLE>(0x5000 + 4 * gHandleCounter++);
}

// Fake counted objects: a vtable whose slots 1 and 2 record the object and the call.
struct FakeRef {
    const void *vtable;
    int id;
};
std::vector<int> gRefEvents; // +id for AddRef, -id for Release (ids start at 1)
uint32_t __fastcall FakeAddRef(FakeRef *self, void *)
{
    gRefEvents.push_back(self->id);
    return 1;
}
uint32_t __fastcall FakeRelease(FakeRef *self, void *)
{
    gRefEvents.push_back(-self->id);
    return 1;
}
void *gFakeRefVTable[3] = {nullptr, reinterpret_cast<void *>(&FakeAddRef), reinterpret_cast<void *>(&FakeRelease)};

std::string Events(const std::vector<int> &v)
{
    std::string s;
    for (int e : v) s += std::to_string(e) + " ";
    return s;
}

} // namespace

DIFF_TEST(GZLog_StoreThreadId, kStoreThreadId)
{
    Slots slots(ctx);
    slots.Set(kIatGetCurrentThreadId, reinterpret_cast<void *>(&FakeThreadId));
    auto original = ctx.Original<void(__cdecl *)(uint32_t *)>(kStoreThreadId);
    std::mt19937 rng(kStoreThreadId);
    for (int i = 0; i < 300; ++i) {
        gThreadIdAnswer = i % 5 == 0 ? 0xffffffffu : rng();
        uint32_t a[4], b[4];
        for (int k = 0; k < 4; ++k) a[k] = b[k] = rng();
        original(&a[1]);
        nGZLog::StoreCurrentThreadId(&b[1]);
        ++ctx.cases;
        if (std::memcmp(a, b, sizeof(a)) != 0 || b[1] != gThreadIdAnswer) ctx.Fail("thread id: stored %08x/%08x (want %08x), neighbours changed: %d", a[1], b[1], gThreadIdAnswer, std::memcmp(a, b, sizeof(a)) != 0);
    }
}

DIFF_TEST(GZLog_Time64, kTime64)
{
    Clocks clocks;
    ClockScope scope(ctx, clocks);
    auto original = ctx.Original<int64_t(__cdecl *)(int64_t *)>(kTime64);
    std::mt19937 rng(kTime64);
    constexpr uint64_t kEpoch = 0x19db1ded53e8000ull, kLimit = 0x793582aff;
    std::vector<uint64_t> fileTimes = {
        0, 1, kEpoch - 1, kEpoch, kEpoch + 9999999, kEpoch + 10000000, kEpoch - 10000000, kEpoch - 10000001,
        kEpoch + kLimit * 10000000ull, kEpoch + kLimit * 10000000ull + 9999999, kEpoch + (kLimit + 1) * 10000000ull,
        kEpoch + (kLimit + 1) * 10000000ull - 1, kEpoch + 0x700000000ull * 10000000ull, 0xffffffffffffffffull, 0x8000000000000000ull,
        0x7fffffffffffffffull, kEpoch + 0xffffffffull * 10000000ull,
    };
    for (int i = 0; i < 4000; ++i) {
        if (i >= static_cast<int>(fileTimes.size())) {
            uint64_t v = (static_cast<uint64_t>(rng()) << 32) | rng();
            // Mostly near the epoch and the limit, where the sign and the clamp change.
            if (i % 3 == 0) v = kEpoch + (rng() % 3 == 0 ? kLimit : 0) * 10000000ull + (static_cast<int64_t>(rng() % 20000000) - 10000000);
            else if (i % 3 == 1) v = kEpoch + (static_cast<uint64_t>(rng()) << 8) % (kLimit * 2 * 10000000ull);
            fileTimes.push_back(v);
        }
        clocks.fileTime = fileTimes[i];
        // A guarded output slot (or none) so stray writes show.
        int64_t slotA[3] = {0x1111, 0x2222, 0x3333}, slotB[3] = {0x1111, 0x2222, 0x3333};
        bool withOut = i % 5 != 4;
        clocks.Rewind();
        int64_t ra = original(withOut ? &slotA[1] : nullptr);
        int callsA = clocks.fileTimeCalls;
        clocks.Rewind();
        int64_t rb = nGZLog::Time64(withOut ? &slotB[1] : nullptr);
        ++ctx.cases;
        if (ra != rb) ctx.Fail("file time %llx: %lld vs %lld", fileTimes[i], ra, rb);
        if (std::memcmp(slotA, slotB, sizeof(slotA)) != 0 || (withOut && slotB[1] != rb)) ctx.Fail("file time %llx: output slot %lld vs %lld", fileTimes[i], slotA[1], slotB[1]);
        if (callsA != 1 || clocks.fileTimeCalls != 1) ctx.Fail("clock reads %d/%d", callsA, clocks.fileTimeCalls);
    }
}

DIFF_TEST(GZLog_HashChars, kHashChars)
{
    auto original = ctx.Original<uint32_t(__cdecl *)(const char *)>(kHashChars);
    std::mt19937 rng(kHashChars);
    const char *fixed[] = {"", "a", "Default", "DebugFormat", "\x80\xff\x01", "NoPreset", "AllTokens"};
    for (const char *s : fixed) {
        ++ctx.cases;
        uint32_t ra = original(s), rb = nGZLog::HashChars(s);
        if (ra != rb) ctx.Fail("hash of \"%s\": %08x vs %08x", s, ra, rb);
    }
    for (int i = 0; i < 4000; ++i) {
        char text[80];
        size_t n = rng() % 70;
        for (size_t k = 0; k < n; ++k) {
            // High bytes are signed characters in the original's movsx.
            text[k] = static_cast<char>(i % 3 == 0 ? 1 + rng() % 255 : 'A' + rng() % 58);
        }
        text[n] = 0;
        ++ctx.cases;
        uint32_t ra = original(text), rb = nGZLog::HashChars(text);
        if (ra != rb) ctx.Fail("hash differs (length %zu): %08x vs %08x", n, ra, rb);
    }
}

DIFF_TEST(GZLog_SignalStateCtor, kSignalState)
{
    auto original = ctx.Original<cRZThreadSignalState *(__thiscall *)(void *)>(kSignalState);
    std::mt19937 rng(kSignalState);
    for (int i = 0; i < 60; ++i) {
        Pair p(cRZThreadSignalState::kSize, rng);
        auto *ra = original(p.a);
        auto *rb = reinterpret_cast<cRZThreadSignalState *>(p.b)->Construct();
        ++ctx.cases;
        auto *a = reinterpret_cast<cRZThreadSignalState *>(p.a);
        auto *b = reinterpret_cast<cRZThreadSignalState *>(p.b);
        std::string why;
        if (ra != a || rb != b) ctx.Fail("returns this: %p/%p", static_cast<void *>(ra), static_cast<void *>(rb));
        int at = FirstDifference(p.a, p.b, cRZThreadSignalState::kSize, {{offsetof(cRZThreadSignalState, mLock) + 4, 4}});
        if (at >= 0) ctx.Fail("state bytes differ at 0x%x", at);
        if (!SameLock(a->mLock, b->mLock, why)) ctx.Fail("lock: %s", why.c_str());
        if (a->mEvent != nullptr || a->mManualReset != 0) ctx.Fail("event/manual not cleared");
        if (!p.CanariesIntact()) ctx.Fail("wrote past the object");
        a->mLock.Destruct();
        b->mLock.Destruct();
    }
}

DIFF_TEST(GZLog_SignalCtor, kSignalCtor)
{
    Slots slots(ctx);
    slots.Set(cRZThreadSignal::kIatCreateEventA, reinterpret_cast<void *>(&FakeCreateEvent));
    auto original = ctx.Original<cRZThreadSignal *(__thiscall *)(void *, uint32_t)>(kSignalCtor);
    std::mt19937 rng(kSignalCtor);
    const uint32_t args[] = {0, 1, 2, 0xffffffffu, 0x100, 0x80000000u};
    for (int i = 0; i < 90; ++i) {
        uint32_t arg = i < 6 ? args[i] : (i % 3 == 0 ? 0 : rng());
        Pair p(sizeof(cRZThreadSignal), rng);
        gHandleCounter = i;
        gLog.clear();
        auto *ra = original(p.a, arg);
        std::vector<std::string> logA = gLog;
        gHandleCounter = i;
        gLog.clear();
        auto *rb = reinterpret_cast<cRZThreadSignal *>(p.b)->Construct(arg);
        ++ctx.cases;
        auto *a = reinterpret_cast<cRZThreadSignal *>(p.a);
        auto *b = reinterpret_cast<cRZThreadSignal *>(p.b);
        if (ra != a || rb != b) ctx.Fail("returns this");
        if (logA != gLog || logA.size() != 1) ctx.Fail("arg %08x: event calls differ: %s / %s", arg, logA.empty() ? "-" : logA[0].c_str(), gLog.empty() ? "-" : gLog[0].c_str());
        if (a->mVTable != b->mVTable || a->mVTable != ctx.image.At(cRZThreadSignal::kVTable)) ctx.Fail("vtable");
        if (!a->mState || !b->mState) {
            ctx.Fail("no state block");
            continue;
        }
        std::string why;
        if (a->mState->mEvent != b->mState->mEvent || a->mState->mManualReset != arg || b->mState->mManualReset != arg) ctx.Fail("arg %08x: event %p/%p, stored %08x/%08x", arg, a->mState->mEvent, b->mState->mEvent, a->mState->mManualReset, b->mState->mManualReset);
        if (FirstDifference(reinterpret_cast<uint8_t *>(a->mState), reinterpret_cast<uint8_t *>(b->mState), cRZThreadSignalState::kSize, {{offsetof(cRZThreadSignalState, mLock) + 4, 4}}) >= 0) ctx.Fail("state bytes differ");
        if (!SameLock(a->mState->mLock, b->mState->mLock, why)) ctx.Fail("lock: %s", why.c_str());
        if (HeapSize(GetProcessHeap(), 0, a->mState) != cRZThreadSignalState::kSize || HeapSize(GetProcessHeap(), 0, b->mState) != cRZThreadSignalState::kSize) ctx.Fail("state block size");
        for (cRZThreadSignal *s : {a, b}) {
            s->mState->mLock.Destruct();
            s->mState->mName.Destruct();
            ::operator delete(s->mState);
        }
    }
}
