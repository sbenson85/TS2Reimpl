// Differential tests for three small slots of nGZLog::cService's cIService vtable (src/rz/nGZLog.cpp): the
// alias insert (slot 5, 0xc9ceda), clearing the output back ends (slot 14, 0xc9e0e2) and flushing them
// (slot 29, 0xc9d04d). The service is a block of random bytes with the lock at +0x90 and the output list at
// +0x60 replaced by recording fakes; each side's call log (lock, per-element calls, unlock) and results
// are compared.
#include "difftest.h"
#include "game/vtable.h"
#include "gzlog_support.h"
#include "rz/nGZLog.h"

#include <windows.h>

#include <map>
#include <memory>

using namespace gzlog_test;
using namespace nGZLog;

namespace {

constexpr uint32_t kAddAliasSlot = 0xc9ceda, kClearSlot = 0xc9e0e2, kFlushSlot = 0xc9d04d;

std::vector<std::string> gLog;

void Log(const char *fmt, ...)
{
    char buf[80];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    gLog.push_back(buf);
}

struct FakeOutput {
    const void *vtable;
    int id;
};

uint32_t __fastcall FakeOutputAddRef(FakeOutput *self, void *)
{
    Log("addref %d", self->id);
    return 1;
}
uint32_t __fastcall FakeOutputRelease(FakeOutput *self, void *)
{
    Log("release %d", self->id);
    return 1;
}
void __fastcall FakeOutputFlush(FakeOutput *self, void *)
{
    Log("flush %d", self->id);
}

uint32_t __fastcall FakeLock(void *, void *)
{
    Log("lock");
    return 1;
}
uint32_t gUnlockResult = 0;
uint32_t __fastcall FakeUnlock(void *, void *)
{
    Log("unlock");
    return gUnlockResult;
}

void *gOutputVTable[22];
void *gLockVTable[4];

struct Setup {
    Setup()
    {
        gOutputVTable[1] = reinterpret_cast<void *>(&FakeOutputAddRef);
        gOutputVTable[2] = reinterpret_cast<void *>(&FakeOutputRelease);
        gOutputVTable[21] = reinterpret_cast<void *>(&FakeOutputFlush);
        gLockVTable[1] = reinterpret_cast<void *>(&FakeLock);
        gLockVTable[2] = reinterpret_cast<void *>(&FakeUnlock);
    }
} gSetup;

std::string Joined()
{
    std::string s;
    for (const std::string &l : gLog) s += l + "; ";
    return s;
}

// Builds a service block (random bytes) with the lock fake in place and `count` outputs in the list,
// reserving `extra` more entries.
struct FakeService {
    Pair pair;
    std::vector<FakeOutput> outputs;
    FakeService(std::mt19937 &rng, int count, int extra, const std::vector<int> &ids) : pair(cLogService::kSize, rng), outputs(16)
    {
        for (size_t k = 0; k < outputs.size(); ++k) outputs[k] = {gOutputVTable, static_cast<int>(k + 1)};
        for (uint8_t *obj : {pair.a, pair.b}) {
            *reinterpret_cast<void **>(obj + offsetof(cLogService, mLock90)) = gLockVTable;
            auto *list = new (obj + offsetof(cLogService, mOutputs)) std::vector<RefHolder>();
            for (int id : ids) {
                void *o = id ? &outputs[id - 1] : nullptr;
                list->push_back(reinterpret_cast<const RefHolder &>(o));
            }
            list->reserve(count + extra);
        }
    }
    cLogService *A() { return reinterpret_cast<cLogService *>(pair.a); }
    cLogService *B() { return reinterpret_cast<cLogService *>(pair.b); }
    ~FakeService()
    {
        gLog.clear();
        A()->mOutputs.~vector();
        B()->mOutputs.~vector();
    }
};

std::vector<int> RandomIds(std::mt19937 &rng, bool allowNull)
{
    std::vector<int> ids;
    int n = rng() % 9;
    for (int k = 0; k < n; ++k) ids.push_back(allowNull && rng() % 5 == 0 ? 0 : 1 + rng() % 16);
    return ids;
}

} // namespace

DIFF_TEST(GZLog_ServiceClearOutputs, kClearSlot)
{
    auto original = ctx.Original<uint32_t(__thiscall *)(void *)>(kClearSlot);
    std::mt19937 rng(kClearSlot);
    for (int i = 0; i < 200; ++i) {
        std::vector<int> ids = RandomIds(rng, true);
        FakeService s(rng, static_cast<int>(ids.size()), rng() % 4, ids);
        gUnlockResult = rng();
        gLog.clear();
        uint32_t ra = original(s.pair.a + cLogService::kServiceSubobjectOffset);
        std::string logA = Joined();
        gLog.clear();
        uint32_t rb = ServiceClearOutputs(s.pair.b + cLogService::kServiceSubobjectOffset, nullptr);
        std::string logB = Joined();
        ++ctx.cases;
        if (ra != rb || ra != gUnlockResult) ctx.Fail("returns %x/%x, want the unlock's %x", ra, rb, gUnlockResult);
        if (logA != logB) ctx.Fail("calls differ: %s / %s", logA.c_str(), logB.c_str());
        auto &a = s.A()->mOutputs;
        auto &b = s.B()->mOutputs;
        if (a.size() != 0 || b.size() != 0 || a.capacity() != b.capacity()) ctx.Fail("list after: size %zu/%zu capacity %zu/%zu", a.size(), b.size(), a.capacity(), b.capacity());
        if (FirstDifference(s.pair.a, s.pair.b, cLogService::kSize, {{offsetof(cLogService, mOutputs), 12}}) >= 0) ctx.Fail("service bytes changed");
    }
}

DIFF_TEST(GZLog_ServiceFlushOutputs, kFlushSlot)
{
    auto original = ctx.Original<uint32_t(__thiscall *)(void *)>(kFlushSlot);
    std::mt19937 rng(kFlushSlot);
    for (int i = 0; i < 200; ++i) {
        std::vector<int> ids = RandomIds(rng, false);
        FakeService s(rng, static_cast<int>(ids.size()), rng() % 4, ids);
        gUnlockResult = rng();
        gLog.clear();
        uint32_t ra = original(s.pair.a + cLogService::kServiceSubobjectOffset);
        std::string logA = Joined();
        gLog.clear();
        uint32_t rb = ServiceFlushOutputs(s.pair.b + cLogService::kServiceSubobjectOffset, nullptr);
        std::string logB = Joined();
        ++ctx.cases;
        if (ra != rb || ra != gUnlockResult) ctx.Fail("returns %x/%x, want the unlock's %x", ra, rb, gUnlockResult);
        if (logA != logB || gLog.size() != ids.size() + 2) ctx.Fail("calls differ: %s / %s", logA.c_str(), logB.c_str());
        if (s.A()->mOutputs.size() != ids.size() || s.B()->mOutputs.size() != ids.size()) ctx.Fail("list changed");
    }
}

DIFF_TEST(GZLog_ServiceAddAlias, kAddAliasSlot)
{
    auto original = ctx.Original<void(__thiscall *)(void *, const char *, const char *)>(kAddAliasSlot);
    std::mt19937 rng(kAddAliasSlot);
    const char *keys[] = {"AllTokens", "OldDebug", "DebugFormat", "", "k4", "k5", "k6", "k7", "k8", "k9", "NoPreset"};
    const char *values[] = {"", "x", "%L: %G", "a longer value that does not fit the small string buffer of a std string", "\xff\x80"};
    for (int run = 0; run < 40; ++run) {
        Pair p(cLogService::kSize, rng);
        auto mapA = std::make_unique<AliasMap>();
        auto mapB = std::make_unique<AliasMap>();
        reinterpret_cast<cLogService *>(p.a)->mAliases = mapA.get();
        reinterpret_cast<cLogService *>(p.b)->mAliases = mapB.get();
        int calls = 1 + rng() % 25;
        for (int k = 0; k < calls; ++k) {
            const char *key = keys[rng() % 11];
            const char *value = rng() % 6 == 0 ? nullptr : values[rng() % 5];
            original(p.a + cLogService::kServiceSubobjectOffset, key, value);
            ServiceAddAlias(p.b + cLogService::kServiceSubobjectOffset, nullptr, key, value);
            ++ctx.cases;
        }
        std::map<std::string, std::string> sa, sb;
        for (const auto &e : *mapA) sa[e.first.mString] = e.second.mString;
        for (const auto &e : *mapB) sb[e.first.mString] = e.second.mString;
        if (sa != sb || mapA->size() != mapB->size() || mapA->bucket_count() != mapB->bucket_count()) ctx.Fail("run %d: tables differ (%zu/%zu entries)", run, mapA->size(), mapB->size());
        if (FirstDifference(p.a, p.b, cLogService::kSize, {{offsetof(cLogService, mAliases), 4}}) >= 0) ctx.Fail("service bytes changed");
        if (reinterpret_cast<cLogService *>(p.b)->mAliases != mapB.get()) ctx.Fail("table pointer changed");
    }
}
