// Differential tests for the helper objects of nGZLog::cService and the small members around its
// constructor (src/rz/nGZLog.cpp): the time stamp, the process-times probe, the record, the text
// formatter and the group level filter (constructors), the alias-table insert, the formatter's two
// setters, and the output thread start. Win32 imports are faked through the game's import slots;
// calls the code makes through vtables go to recording fakes, so the call sequences are compared.
#include "difftest.h"
#include "game/vtable.h"
#include "gzlog_support.h"
#include "rz/nGZLog.h"

#include <windows.h>

#include <algorithm>
#include <ctime>
#include <map>
#include <memory>

using namespace gzlog_test;
using namespace nGZLog;

namespace {

constexpr uint32_t kTimestampCtor = 0xc9c291, kProcessInfoCtor = 0xc9c221, kRecordCtor = 0xc9c1ed;
constexpr uint32_t kFormatterCtor = 0xc9c164, kFilterCtor = 0xc9c113, kAddAlias = 0xc9ce86;
constexpr uint32_t kSetAliasMap = 0xc9e509, kSetTimestamp = 0xc9e822, kStartThread = 0xc9e880;
constexpr uint32_t kIatGetCurrentProcess = 0xd9a1b8, kIatGetSystemInfo = 0xd9a1c4, kIatLoadLibraryA = 0xd9a128, kIatGetProcAddress = 0xd9a124;

std::vector<std::string> gLog;

void Log(const char *fmt, ...)
{
    char buf[160];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    gLog.push_back(buf);
}

std::string Joined()
{
    std::string s;
    for (const std::string &l : gLog) s += l + "; ";
    return s;
}

// ---- Win32 fakes ---------------------------------------------------------------------------------

uint32_t gProcessAnswer = 0, gSysInfoSeed = 0;
HMODULE gModuleAnswer = nullptr;
bool gProcAnswers = false;

HANDLE WINAPI FakeGetCurrentProcess()
{
    Log("GetCurrentProcess");
    return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(gProcessAnswer));
}

void WINAPI FakeGetSystemInfo(LPSYSTEM_INFO info)
{
    Log("GetSystemInfo");
    std::mt19937 rng(gSysInfoSeed);
    uint32_t *w = reinterpret_cast<uint32_t *>(info);
    for (int k = 0; k < 9; ++k) w[k] = rng();
    if (gSysInfoSeed % 3 == 0) info->dwNumberOfProcessors = 1 + gSysInfoSeed % 32;
}

HMODULE WINAPI FakeLoadLibraryA(const char *name)
{
    Log("LoadLibraryA(%s)", name);
    return gModuleAnswer;
}

FARPROC WINAPI FakeGetProcAddress(HMODULE module, const char *name)
{
    Log("GetProcAddress(%p,%s)", static_cast<void *>(module), name);
    return gProcAnswers ? reinterpret_cast<FARPROC>(0x7777) : nullptr;
}

// ---- vtable fakes --------------------------------------------------------------------------------

const uint8_t *gBase = nullptr; // `this` of the object under test, for relative addresses

uint32_t Rel(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<const uint8_t *>(p) - gBase);
}

uint32_t __fastcall FakeServiceAddRef(void *self, void *)
{
    Log("AddRef(this+%x)", Rel(self));
    return 1;
}

void __fastcall FakeSetFormat(void *self, void *, uint32_t index, const char *name)
{
    Log("SetFormat(this+%x,%u,%p)", Rel(self), index, static_cast<const void *>(name));
}

void __fastcall FakeThreadSetName(void *self, void *, const char *name)
{
    Log("SetName(this+%x,%s)", Rel(self), name);
}

void __fastcall FakeThreadStart(void *self, void *, void *callback, void *param)
{
    Log("Start(this+%x,%p,this+%x)", Rel(self), callback, Rel(param));
}

// ---- hash container dumps ------------------------------------------------------------------------

std::string Dump(const AliasMap &m)
{
    std::map<std::string, std::string> sorted;
    for (const auto &e : m) sorted[e.first.mString] = e.second.mString;
    std::string s = "n=" + std::to_string(m.size()) + " b=" + std::to_string(m.bucket_count()) + ":";
    for (const auto &e : sorted) s += e.first + "=>" + e.second + "|";
    return s;
}

} // namespace

DIFF_TEST(GZLog_TimestampCtor, kTimestampCtor)
{
    // The wall clock and the performance counter are scripted for both sides, so every byte is comparable.
    Clocks clocks;
    ClockScope scope(ctx, clocks);
    auto original = ctx.Original<cLogTimestamp *(__thiscall *)(void *)>(kTimestampCtor);
    std::mt19937 rng(kTimestampCtor);
    float *scale = ctx.image.At<float>(cRZStopwatch::kScalesRva + 8); // mode 4's scale
    uint64_t *overhead = ctx.image.At<uint64_t>(cRZStopwatch::kOverheadRva);
    const float savedScale = *scale;
    const uint64_t savedOverhead = *overhead;
    for (int i = 0; i < 120; ++i) {
        *scale = i % 3 == 0 ? 0.0f : i % 3 == 1 ? 1.0f : 0.0000025f;
        *overhead = i % 4 == 0 ? 0 : rng() % 50;
        // Seconds since 1970 as a file time; the first two counter reads are the start and the elapsed reading.
        uint64_t seconds = i % 5 == 0 ? 0x100000000ull + rng() : rng();
        clocks.fileTime = 0x19db1ded53e8000ull + seconds * 10000000ull + rng() % 10000000;
        uint64_t start = i % 7 == 0 ? 1 : 0x100000000ull * (rng() % 4) + rng();
        uint64_t span = i % 6 == 0 ? rng() % 60 : rng() % 100000000;
        clocks.counter = {start, start + span};
        Pair p(cLogTimestamp::kSize, rng);
        clocks.Rewind();
        auto *ra = original(p.a);
        int calls = clocks.fileTimeCalls;
        clocks.Rewind();
        auto *rb = reinterpret_cast<cLogTimestamp *>(p.b)->Construct();
        ++ctx.cases;
        auto *a = reinterpret_cast<cLogTimestamp *>(p.a);
        auto *b = reinterpret_cast<cLogTimestamp *>(p.b);
        if (ra != a || rb != b) ctx.Fail("returns this");
        int at = FirstDifference(p.a, p.b, cLogTimestamp::kSize);
        if (at >= 0) ctx.Fail("bytes differ at 0x%x (scale %g, span %llu)", at, *scale, span);
        if (calls != 1 || clocks.fileTimeCalls != 1) ctx.Fail("wall clock read %d/%d times", calls, clocks.fileTimeCalls);
        if (b->mTime != static_cast<uint32_t>(seconds) || b->mStopwatch.mStart != start) ctx.Fail("time %u / start %llu not taken from the clocks", b->mTime, b->mStopwatch.mStart);
        if (!p.CanariesIntact()) ctx.Fail("wrote past the object");
    }
    *scale = savedScale;
    *overhead = savedOverhead;
}

DIFF_TEST(GZLog_ProcessInfoCtor, kProcessInfoCtor)
{
    Slots slots(ctx);
    slots.Set(kIatGetCurrentProcess, reinterpret_cast<void *>(&FakeGetCurrentProcess));
    slots.Set(kIatGetSystemInfo, reinterpret_cast<void *>(&FakeGetSystemInfo));
    slots.Set(kIatLoadLibraryA, reinterpret_cast<void *>(&FakeLoadLibraryA));
    slots.Set(kIatGetProcAddress, reinterpret_cast<void *>(&FakeGetProcAddress));
    auto original = ctx.Original<cLogProcessInfo *(__thiscall *)(void *)>(kProcessInfoCtor);
    std::mt19937 rng(kProcessInfoCtor);
    int found = 0;
    for (int i = 0; i < 160; ++i) {
        gProcessAnswer = i % 7 == 0 ? 0xffffffffu : rng();
        gSysInfoSeed = i < 4 ? i : rng();
        gModuleAnswer = rng() % 3 == 0 ? nullptr : reinterpret_cast<HMODULE>(0x20000000 + 0x10000 * (rng() % 64));
        gProcAnswers = rng() % 2 == 0;
        Pair p(cLogProcessInfo::kSize, rng);
        gLog.clear();
        auto *ra = original(p.a);
        std::string logA = Joined();
        gLog.clear();
        auto *rb = reinterpret_cast<cLogProcessInfo *>(p.b)->Construct();
        std::string logB = Joined();
        ++ctx.cases;
        auto *a = reinterpret_cast<cLogProcessInfo *>(p.a);
        auto *b = reinterpret_cast<cLogProcessInfo *>(p.b);
        if (ra != a || rb != b) ctx.Fail("returns this");
        if (logA != logB) ctx.Fail("calls differ: %s / %s", logA.c_str(), logB.c_str());
        int at = FirstDifference(p.a, p.b, cLogProcessInfo::kSize);
        if (at >= 0) ctx.Fail("bytes differ at 0x%x (processors %u, flag %d/%d)", at, a->mProcessors, a->mHasProcessTimes, b->mHasProcessTimes);
        if (b->mHasProcessTimes != ((gModuleAnswer && gProcAnswers) ? 1 : 0)) ctx.Fail("flag %d with module %p, proc %d", b->mHasProcessTimes, static_cast<void *>(gModuleAnswer), gProcAnswers);
        if (b->mScaled != static_cast<int64_t>(static_cast<int32_t>(b->mProcessors)) * 10000) ctx.Fail("scaled count");
        if (b->mHasProcessTimes) ++found;
        if (!p.CanariesIntact()) ctx.Fail("wrote past the object");
    }
    if (found < 20) ctx.Fail("only %d runs found GetProcessTimes", found);
}

DIFF_TEST(GZLog_OutputBackEndCtor, 0xc9f372)
{
    auto original = ctx.Original<cLogOutputBackEnd *(__thiscall *)(void *)>(0xc9f372);
    std::mt19937 rng(0xc9f372);
    for (int i = 0; i < 60; ++i) {
        Pair p(cLogOutputBackEnd::kSize, rng);
        auto *ra = original(p.a);
        auto *rb = reinterpret_cast<cLogOutputBackEnd *>(p.b)->Construct();
        ++ctx.cases;
        if (ra != reinterpret_cast<cLogOutputBackEnd *>(p.a) || rb != reinterpret_cast<cLogOutputBackEnd *>(p.b)) ctx.Fail("returns this");
        int at = FirstDifference(p.a, p.b, cLogOutputBackEnd::kSize);
        if (at >= 0) ctx.Fail("bytes differ at 0x%x", at);
        auto *b = reinterpret_cast<cLogOutputBackEnd *>(p.b);
        if (b->mVTable != ctx.image.At(cLogOutputBackEnd::kVTable) || b->mInterval != 1 || b->mEnabled != 0 || b->mFlag != 0) ctx.Fail("fields");
        if (!p.CanariesIntact()) ctx.Fail("wrote past the object");
    }
}

DIFF_TEST(GZLog_CaptionOutputBackEndCtor, 0xc9b998)
{
    auto original = ctx.Original<cLogCaptionOutputBackEnd *(__thiscall *)(void *, const char *)>(0xc9b998);
    std::mt19937 rng(0xc9b998);
    for (int i = 0; i < 100; ++i) {
        std::string text(i % 7 == 0 ? 0 : rng() % 40, 'a');
        for (char &c : text) c = static_cast<char>('A' + rng() % 50);
        Pair p(cLogCaptionOutputBackEnd::kSize, rng);
        auto *ra = original(p.a, text.c_str());
        auto *rb = reinterpret_cast<cLogCaptionOutputBackEnd *>(p.b)->Construct(text.c_str());
        ++ctx.cases;
        auto *a = reinterpret_cast<cLogCaptionOutputBackEnd *>(p.a);
        auto *b = reinterpret_cast<cLogCaptionOutputBackEnd *>(p.b);
        std::string why;
        if (ra != a || rb != b) ctx.Fail("returns this");
        // A long text is on the heap: its pointer differs, the content is compared instead.
        std::vector<std::pair<size_t, size_t>> skip = {{offsetof(cLogCaptionOutputBackEnd, mLock) + 4, 4}};
        if (text.size() > 15) skip.push_back({0x18, 4});
        int at = FirstDifference(p.a, p.b, cLogCaptionOutputBackEnd::kSize, skip);
        if (at >= 0) ctx.Fail("bytes differ at 0x%x", at);
        if (a->mBase.mText.mString != text || b->mBase.mText.mString != text) ctx.Fail("text '%s' / '%s'", a->mBase.mText.mString.c_str(), b->mBase.mText.mString.c_str());
        if (!SameLock(a->mLock, b->mLock, why)) ctx.Fail("lock: %s", why.c_str());
        if (b->mBase.mVTable != ctx.image.At(cLogCaptionOutputBackEnd::kVTable)) ctx.Fail("vtable");
        if (!p.CanariesIntact()) ctx.Fail("wrote past the object");
        a->mLock.Destruct();
        b->mLock.Destruct();
    }
}

DIFF_TEST(GZLog_StdAdapterCtor, 0xc9c2e2)
{
    auto original = ctx.Original<cLogStdAdapter *(__thiscall *)(void *, uint32_t, const char *)>(0xc9c2e2);
    std::mt19937 rng(0xc9c2e2);
    for (int i = 0; i < 100; ++i) {
        std::string text(i % 7 == 0 ? 0 : rng() % 40, 'a');
        for (char &c : text) c = static_cast<char>('A' + rng() % 50);
        uint32_t target = rng();
        Pair p(cLogStdAdapter::kSize, rng);
        auto *ra = original(p.a, target, text.c_str());
        auto *rb = reinterpret_cast<cLogStdAdapter *>(p.b)->Construct(target, text.c_str());
        ++ctx.cases;
        auto *a = reinterpret_cast<cLogStdAdapter *>(p.a);
        auto *b = reinterpret_cast<cLogStdAdapter *>(p.b);
        if (ra != a || rb != b) ctx.Fail("returns this");
        std::vector<std::pair<size_t, size_t>> skip;
        if (text.size() > 15) skip.push_back({0x18, 4});
        int at = FirstDifference(p.a, p.b, cLogStdAdapter::kSize, skip);
        if (at >= 0) ctx.Fail("bytes differ at 0x%x", at);
        if (a->mName.mString != text || b->mName.mString != text || b->mTarget != target) ctx.Fail("name or target");
        if (b->mVTable != ctx.image.At(cLogStdAdapter::kVTable) || b->mField8 != 1) ctx.Fail("vtable or flag");
        if (!p.CanariesIntact()) ctx.Fail("wrote past the object");
    }
}

DIFF_TEST(GZLog_RecordCtor, kRecordCtor)
{
    auto original = ctx.Original<cLogRecord *(__thiscall *)(void *)>(kRecordCtor);
    std::mt19937 rng(kRecordCtor);
    for (int i = 0; i < 40; ++i) {
        Pair p(cLogRecord::kSize, rng);
        auto *ra = original(p.a);
        auto *rb = reinterpret_cast<cLogRecord *>(p.b)->Construct();
        ++ctx.cases;
        if (ra != reinterpret_cast<cLogRecord *>(p.a) || rb != reinterpret_cast<cLogRecord *>(p.b)) ctx.Fail("returns this");
        int at = FirstDifference(p.a, p.b, cLogRecord::kSize);
        if (at >= 0) ctx.Fail("bytes differ at 0x%x", at);
        auto *b = reinterpret_cast<cLogRecord *>(p.b);
        if (b->mVTable != ctx.image.At(cLogRecord::kVTable) || b->mUnknown.mVTable != ctx.image.At(cLogRecord::kUnknownVTable) || b->mUnknown.mRefCount != 0) ctx.Fail("vtables or count");
        if (!p.CanariesIntact()) ctx.Fail("wrote past the object");
    }
}

DIFF_TEST(GZLog_FormatterCtor, kFormatterCtor)
{
    auto original = ctx.Original<cLogFormatterText *(__thiscall *)(void *)>(kFormatterCtor);
    std::mt19937 rng(kFormatterCtor);
    for (int i = 0; i < 40; ++i) {
        Pair p(cLogFormatterText::kSize, rng);
        auto *ra = original(p.a);
        auto *rb = reinterpret_cast<cLogFormatterText *>(p.b)->Construct();
        ++ctx.cases;
        auto *a = reinterpret_cast<cLogFormatterText *>(p.a);
        auto *b = reinterpret_cast<cLogFormatterText *>(p.b);
        std::string why;
        if (ra != a || rb != b) ctx.Fail("returns this");
        int at = FirstDifference(p.a, p.b, cLogFormatterText::kSize, {{offsetof(cLogFormatterText, mLock) + 4, 4}});
        if (at >= 0) ctx.Fail("bytes differ at 0x%x", at);
        if (!SameLock(a->mLock, b->mLock, why)) ctx.Fail("lock: %s", why.c_str());
        if (b->mVTable != ctx.image.At(cLogFormatterText::kVTable) || b->mUnknown.mVTable != ctx.image.At(cLogFormatterText::kUnknownVTable)) ctx.Fail("vtables");
        if (b->mTimestamp != nullptr || b->mAliases != nullptr) ctx.Fail("pointers not cleared");
        if (!p.CanariesIntact()) ctx.Fail("wrote past the object");
        a->mLock.Destruct();
        b->mLock.Destruct();
    }
}

DIFF_TEST(GZLog_FilterCtor, kFilterCtor)
{
    auto original = ctx.Original<cLogFilterGroupLevels *(__thiscall *)(void *)>(kFilterCtor);
    std::mt19937 rng(kFilterCtor);
    for (int i = 0; i < 40; ++i) {
        Pair p(cLogFilterGroupLevels::kSize, rng);
        auto *ra = original(p.a);
        auto *rb = reinterpret_cast<cLogFilterGroupLevels *>(p.b)->Construct();
        ++ctx.cases;
        auto *a = reinterpret_cast<cLogFilterGroupLevels *>(p.a);
        auto *b = reinterpret_cast<cLogFilterGroupLevels *>(p.b);
        if (ra != a || rb != b) ctx.Fail("returns this");
        // The level map's pointers (list head, bucket vector) differ; the rest is byte for byte.
        int at = FirstDifference(p.a, p.b, cLogFilterGroupLevels::kSize, {{0x34 + 4, 4}, {0x34 + 0xc, 12}});
        if (at >= 0) ctx.Fail("bytes differ at 0x%x", at);
        HashShape sa = Inspect(p.a + 0x34), sb = Inspect(p.b + 0x34);
        if (!sa.linksOk || !sb.linksOk || !sa.bucketsOk || !sb.bucketsOk || sa.mask != 7 || sb.mask != 7 || sa.headBlock != sb.headBlock) ctx.Fail("level map shape");
        if (b->mVTable != ctx.image.At(cLogFilterGroupLevels::kVTable) || b->mUnknown.mVTable != ctx.image.At(cLogFilterGroupLevels::kUnknownVTable)) ctx.Fail("vtables");
        if (b->mNoGroupLevel != 0 || b->mAnyGroupLevel != 7) ctx.Fail("fields");
        if (!p.CanariesIntact()) ctx.Fail("wrote past the object");
        FreeHash(p.a + 0x34);
        FreeHash(p.b + 0x34);
    }
}

DIFF_TEST(GZLog_AddAlias, kAddAlias)
{
    auto original = ctx.Original<void(__thiscall *)(AliasMap *, const char *, const char *)>(kAddAlias);
    std::mt19937 rng(kAddAlias);
    const char *keys[] = {"AllTokens", "OldDebug", "DebugFormat", "", "k4", "k5", "k6", "k7", "k8", "k9", "k10", "k11", "k12", "k13", "k14", "k15", "NoPreset"};
    const char *values[] = {"", "x", "%L: %G\n%M\n", "a longer value that does not fit the small string buffer of a std string", "\xff\x80"};
    for (int run = 0; run < 60; ++run) {
        // Both sides' maps are our container type; the original works on its memory directly.
        auto a = std::make_unique<AliasMap>();
        auto b = std::make_unique<AliasMap>();
        int calls = 1 + rng() % 40;
        std::string hist;
        for (int k = 0; k < calls; ++k) {
            const char *key = keys[rng() % (run % 3 == 0 ? 17 : 4)];
            const char *value = rng() % 6 == 0 ? nullptr : values[rng() % 5];
            std::string longKey;
            if (rng() % 9 == 0) { // keys beyond the small buffer
                longKey = std::string(20 + rng() % 30, 'q') + std::to_string(rng() % 4);
                key = longKey.c_str();
            }
            hist += std::string("[") + key + "=" + (value ? value : "NULL") + "]";
            original(a.get(), key, value);
            AddAliasText(b.get(), nullptr, key, value);
            ++ctx.cases;
            std::string da = Dump(*a), db = Dump(*b);
            if (da != db) {
                ctx.Fail("run %d call %d: %.150s ; %.100s ; %.100s", run, k, hist.c_str(), da.c_str(), db.c_str());
                break;
            }
        }
    }
}

DIFF_TEST(GZLog_FormatterSetAliasMap, kSetAliasMap)
{
    const uint32_t kPreset = 0x1085da0;
    auto original = ctx.Original<void(__thiscall *)(void *, AliasMap *)>(kSetAliasMap);
    std::mt19937 rng(kSetAliasMap);
    void *fakeVTable[8] = {};
    fakeVTable[3] = reinterpret_cast<void *>(&FakeSetFormat);
    uint32_t *global = ctx.image.At<uint32_t>(kPreset);
    uint32_t savedGlobal = *global;
    for (int i = 0; i < 100; ++i) {
        *global = rng();
        Pair p(cLogFormatterText::kSize, rng);
        *reinterpret_cast<void **>(p.a) = fakeVTable;
        *reinterpret_cast<void **>(p.b) = fakeVTable;
        auto *map = reinterpret_cast<AliasMap *>(static_cast<uintptr_t>(rng() | 1));
        gBase = p.a;
        gLog.clear();
        original(p.a, map);
        std::string logA = Joined();
        gBase = p.b;
        gLog.clear();
        reinterpret_cast<cLogFormatterText *>(p.b)->SetAliasMap(map);
        std::string logB = Joined();
        ++ctx.cases;
        if (logA != logB || gLog.size() != 1) ctx.Fail("calls differ: %s / %s", logA.c_str(), logB.c_str());
        int at = FirstDifference(p.a, p.b, cLogFormatterText::kSize);
        if (at >= 0) ctx.Fail("bytes differ at 0x%x", at);
        if (reinterpret_cast<cLogFormatterText *>(p.b)->mAliases != map) ctx.Fail("map not stored");
        if (!p.CanariesIntact()) ctx.Fail("wrote past the object");
    }
    *global = savedGlobal;
}

DIFF_TEST(GZLog_FormatterSetTimestamp, kSetTimestamp)
{
    auto original = ctx.Original<void(__thiscall *)(void *, cLogTimestamp *)>(kSetTimestamp);
    std::mt19937 rng(kSetTimestamp);
    for (int i = 0; i < 100; ++i) {
        Pair p(cLogFormatterText::kSize, rng);
        auto *ts = reinterpret_cast<cLogTimestamp *>(static_cast<uintptr_t>(rng()));
        original(p.a, ts);
        reinterpret_cast<cLogFormatterText *>(p.b)->SetTimestamp(ts);
        ++ctx.cases;
        int at = FirstDifference(p.a, p.b, cLogFormatterText::kSize);
        if (at >= 0) ctx.Fail("bytes differ at 0x%x", at);
        if (reinterpret_cast<cLogFormatterText *>(p.b)->mTimestamp != ts) ctx.Fail("timestamp not stored");
        if (!p.CanariesIntact()) ctx.Fail("wrote past the object");
    }
}

DIFF_TEST(GZLog_StartOutputThread, kStartThread)
{
    auto original = ctx.Original<void(__thiscall *)(void *)>(kStartThread);
    std::mt19937 rng(kStartThread);
    void *serviceVTable[4] = {};
    serviceVTable[1] = reinterpret_cast<void *>(&FakeServiceAddRef);
    void *threadVTable[17] = {};
    threadVTable[2] = reinterpret_cast<void *>(&FakeThreadStart);
    threadVTable[12] = reinterpret_cast<void *>(&FakeThreadSetName);
    for (int i = 0; i < 60; ++i) {
        Pair p(cLogService::kSize, rng);
        for (uint8_t *obj : {p.a, p.b}) {
            *reinterpret_cast<void **>(obj) = serviceVTable;
            *reinterpret_cast<void **>(obj + offsetof(cLogService, mThread)) = threadVTable;
        }
        gBase = p.a;
        gLog.clear();
        original(p.a);
        std::string logA = Joined();
        gBase = p.b;
        gLog.clear();
        reinterpret_cast<cLogService *>(p.b)->StartOutputThread();
        std::string logB = Joined();
        ++ctx.cases;
        if (logA != logB || gLog.size() != 3) ctx.Fail("calls differ: %s / %s", logA.c_str(), logB.c_str());
        int at = FirstDifference(p.a, p.b, cLogService::kSize);
        if (at >= 0) ctx.Fail("bytes differ at 0x%x", at);
        if (!p.CanariesIntact()) ctx.Fail("wrote past the object");
    }
}
