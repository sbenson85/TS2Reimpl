// Differential tests for more of nGZLog::cService (src/rz/nGZLog.cpp): the level names (0xc9e8eb, 0xc9dc38
// and the slots 3 and 4 that forward to them, 0xc9e943 and 0xc9dd21), the record's SetLevel (0xc9e5e6), the
// log slot (27, 0xc9db7e), Init / Shutdown / the thread stop (0xc9db60, 0xc9e82f, 0xc9e8b9) and the
// formatter and filter creating slots (6 and 10, 0xc9cb65 and 0xc9caf5). The objects the service talks to
// are fakes whose vtable slots log their calls (with `this` relative to the service block where it is part
// of it); the framework global at 0x10c9378 and the level text globals in the mapped image are pointed at
// fakes and restored after.
#include "difftest.h"
#include "free_recorder.h"
#include "game/vtable.h"
#include "gzlog_support.h"
#include "rz/nGZLog.h"

#include <windows.h>

#include <cstdarg>
#include <deque>
#include <map>
#include <memory>
#include <optional>

using namespace gzlog_test;
using namespace nGZLog;

namespace {

constexpr uint32_t kLevelName = 0xc9e8eb, kLevelNameSlot = 0xc9e943, kParseLevel = 0xc9dc38, kParseLevelSlot = 0xc9dd21;
constexpr uint32_t kSetLevel = 0xc9e5e6, kLog = 0xc9db7e, kInit = 0xc9db60, kShutdown = 0xc9e82f, kStop = 0xc9e8b9;
constexpr uint32_t kAddFormatter = 0xc9cb65, kAddFilter = 0xc9caf5;
constexpr uint32_t kFrameworkGlobal = 0x10c9378;
constexpr uint32_t kLevelGlobals[7] = {0x1085d94, 0x1085d6c, 0x1085d74, 0x1085d7c, 0x1085d84, 0x1085d8c, 0x1085d64}; // levels 0, 2, 3, 4, 5, 6, 7
constexpr int kLevelOfGlobal[7] = {0, 2, 3, 4, 5, 6, 7};
constexpr size_t kSub = cLogService::kServiceSubobjectOffset;

std::vector<std::string> gLog;
const uint8_t *gBase = nullptr;

void Log(const char *fmt, ...)
{
    char buf[120];
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

uint32_t Rel(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<const uint8_t *>(p) - gBase);
}

// Saves and restores 32-bit pointer globals of the mapped image.
class Globals {
public:
    explicit Globals(difftest::Context &c) : ctx(c) {}
    ~Globals()
    {
        for (auto it = saved.rbegin(); it != saved.rend(); ++it) *ctx.image.At<uint32_t>(it->first) = it->second;
    }
    void Set(uint32_t rva, const void *pointer)
    {
        uint32_t *slot = ctx.image.At<uint32_t>(rva);
        saved.emplace_back(rva, *slot);
        *slot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer));
    }
    void SetByte(uint32_t rva, uint8_t value)
    {
        uint8_t *slot = ctx.image.At<uint8_t>(rva);
        savedBytes.emplace_back(rva, *slot);
        *slot = value;
        (void)saved;
    }
    void RestoreBytes()
    {
        for (auto it = savedBytes.rbegin(); it != savedBytes.rend(); ++it) *ctx.image.At<uint8_t>(it->first) = it->second;
        savedBytes.clear();
    }

private:
    difftest::Context &ctx;
    std::vector<std::pair<uint32_t, uint32_t>> saved;
    std::vector<std::pair<uint32_t, uint8_t>> savedBytes;
};

const char *const kLevelTexts[] = {"Fatal", "Error", "Warning", "Info", "Detail", "Debug", "Trace", "All", "ALPHA", "beta", "Gamma", "delta7", ""};
constexpr size_t kLevelTextCount = sizeof(kLevelTexts) / sizeof(kLevelTexts[0]);

} // namespace

DIFF_TEST(GZLog_LevelName, kLevelName)
{
    Globals globals(ctx);
    auto original = ctx.Original<const char *(__cdecl *)(int32_t)>(kLevelName);
    auto slot = ctx.Original<const char *(__thiscall *)(void *, int32_t)>(kLevelNameSlot);
    std::mt19937 rng(kLevelName);
    for (int round = 0; round < 20; ++round) {
        for (uint32_t g : kLevelGlobals) globals.Set(g, kLevelTexts[rng() % kLevelTextCount]);
        for (int32_t level : {-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 100, 0x7fffffff, static_cast<int32_t>(0x80000000), 0x100, 0x102}) {
            ++ctx.cases;
            const char *ra = original(level), *rb = LevelName(level);
            if (ra != rb) ctx.Fail("level %d: %p vs %p", level, static_cast<const void *>(ra), static_cast<const void *>(rb));
            const char *sa = slot(nullptr, level), *sb = ServiceLevelName(nullptr, nullptr, level);
            if (sa != sb || sa != ra) ctx.Fail("level %d: slot %p / %p", level, static_cast<const void *>(sa), static_cast<const void *>(sb));
        }
    }
}

DIFF_TEST(GZLog_ParseLevelName, kParseLevel)
{
    Globals globals(ctx);
    auto original = ctx.Original<int32_t(__cdecl *)(const char *)>(kParseLevel);
    auto slot = ctx.Original<int32_t(__thiscall *)(void *, const char *)>(kParseLevelSlot);
    std::mt19937 rng(kParseLevel);
    for (int round = 0; round < 400; ++round) {
        // Level texts that overlap sometimes ("Info" inside "Information"), so the search order shows.
        for (uint32_t g : kLevelGlobals) globals.Set(g, kLevelTexts[rng() % kLevelTextCount]);
        std::string text;
        int parts = rng() % 4;
        for (int p = 0; p < parts; ++p) {
            std::string piece = rng() % 3 == 0 ? std::string("xx") : kLevelTexts[rng() % kLevelTextCount];
            if (rng() % 2)
                for (char &c : piece) c = static_cast<char>(rng() % 2 ? toupper(static_cast<unsigned char>(c)) : tolower(static_cast<unsigned char>(c)));
            text += piece;
        }
        const char *argument = round % 17 == 0 ? nullptr : text.c_str();
        ++ctx.cases;
        int32_t ra = original(argument), rb = ParseLevelName(argument);
        int32_t sa = slot(nullptr, argument), sb = ServiceParseLevelName(nullptr, nullptr, argument);
        if (ra != rb || sa != ra || sb != ra) ctx.Fail("\"%s\": %d / %d / slot %d / %d", argument ? argument : "(null)", ra, rb, sa, sb);
    }
}

DIFF_TEST(GZLog_RecordSetLevel, kSetLevel)
{
    auto original = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t)>(kSetLevel);
    std::mt19937 rng(kSetLevel);
    for (int i = 0; i < 100; ++i) {
        Pair p(cLogRecord::kSize, rng);
        uint32_t level = rng(), arg = rng();
        original(p.a, level, arg);
        reinterpret_cast<cLogRecord *>(p.b)->SetLevel(level, arg);
        ++ctx.cases;
        if (FirstDifference(p.a, p.b, cLogRecord::kSize) >= 0) ctx.Fail("bytes differ");
        auto *b = reinterpret_cast<cLogRecord *>(p.b);
        if (b->mLevel != level || b->mGroup != arg || b->mMessage != 0 || !p.CanariesIntact()) ctx.Fail("fields or canary");
    }
}

// ---- fakes for the log slot and the lifecycle ----------------------------------------------------------

namespace {

struct FakeOut {
    const void *vtable;
    int id;
};

std::vector<uint8_t> gAccept; // per output id: what slot 3 answers (the whole al)

const void *gRecordBuffer = nullptr; // the heap buffer an output gave the record text (must be freed with the record)
std::optional<difftest::FreeRecorder> gRecordFrees; // started when the buffer is made, stopped after the call
bool RecordBufferLeaked()
{
    if (gRecordFrees) gRecordFrees->Stop();
    bool leaked = gRecordBuffer && !(gRecordFrees && gRecordFrees->Freed(gRecordBuffer));
    gRecordFrees.reset();
    return leaked;
}

uint8_t __fastcall OutAccept(FakeOut *self, void *, cLogRecord *record)
{
    Log("out%d.accept(level %u, arg %x, text '%s')", self->id, record->mLevel, record->mGroup, record->mText.mString.c_str());
    if (self->id % 2 == 0) {
        record->mText.mString.assign(60, 'x'); // text too long for the small buffer, so the record owns a heap block
        gRecordBuffer = record->mText.mString.data();
        if (!gRecordFrees) gRecordFrees.emplace(); // from here to the end of the call: was the buffer freed
    }
    return gAccept[self->id];
}
uint32_t __fastcall OutAddRef(FakeOut *self, void *) { Log("out%d.addref", self->id); return 1; }
uint32_t __fastcall OutRelease(FakeOut *self, void *) { Log("out%d.release", self->id); return 1; }
uint32_t __fastcall LockFn(void *self, void *) { Log("lock(this+%x)", Rel(self)); return 1; }
uint32_t gUnlockResult = 0;
uint32_t __fastcall UnlockFn(void *self, void *) { Log("unlock(this+%x)", Rel(self)); return gUnlockResult; }

// The service itself (AddRef / Release through its primary vtable), its cIService part (slot 29), the
// signal (slot 1), the thread (slot 8) and the framework (slots 9 and 10).
uint32_t __fastcall SvcAddRef(void *self, void *) { Log("service.addref(this+%x)", Rel(self)); return 1; }
uint32_t gReleaseResult = 0;
uint32_t __fastcall SvcRelease(void *self, void *) { Log("service.release(this+%x)", Rel(self)); return gReleaseResult; }
uint32_t __fastcall SubFlush(void *self, void *) { Log("service.flush(this+%x)", Rel(self)); return 5; }
uint32_t __fastcall SignalSet(void *self, void *) { Log("signal.set(this+%x)", Rel(self)); return 6; }
uint32_t __fastcall ThreadWait(void *self, void *, uint32_t timeout) { Log("thread.wait(this+%x, %x)", Rel(self), timeout); return 7; }
void __fastcall FrameworkRegister(void *, void *, void *service) { Log("framework.register(this+%x)", Rel(service)); }
void __fastcall FrameworkUnregister(void *, void *, void *service) { Log("framework.unregister(this+%x)", Rel(service)); }

void *gOutVTable[24], *gLockVTable[4], *gSvcVTable[4], *gSubVTable[32], *gSignalVTable[4], *gThreadVTable[12], *gFrameworkVTable[12];

struct Setup {
    Setup()
    {
        gOutVTable[1] = reinterpret_cast<void *>(&OutAddRef);
        gOutVTable[2] = reinterpret_cast<void *>(&OutRelease);
        gOutVTable[3] = reinterpret_cast<void *>(&OutAccept);
        gLockVTable[1] = reinterpret_cast<void *>(&LockFn);
        gLockVTable[2] = reinterpret_cast<void *>(&UnlockFn);
        gSvcVTable[1] = reinterpret_cast<void *>(&SvcAddRef);
        gSvcVTable[2] = reinterpret_cast<void *>(&SvcRelease);
        gSubVTable[29] = reinterpret_cast<void *>(&SubFlush);
        gSignalVTable[1] = reinterpret_cast<void *>(&SignalSet);
        gThreadVTable[8] = reinterpret_cast<void *>(&ThreadWait);
        gFrameworkVTable[9] = reinterpret_cast<void *>(&FrameworkRegister);
        gFrameworkVTable[10] = reinterpret_cast<void *>(&FrameworkUnregister);
    }
} gSetup;

struct FrameworkFake {
    const void *vtable = gFrameworkVTable;
} gFramework;

// A service block (random bytes) with the fakes in place and the three lists holding `outputs` fakes each.
struct Fixture {
    Pair pair;
    std::deque<FakeOut> fakes;
    explicit Fixture(std::mt19937 &rng, int outputs, int filters = 0, int formatters = 0) : pair(cLogService::kSize, rng)
    {
        for (int k = 0; k < outputs + filters + formatters; ++k) fakes.push_back({gOutVTable, k + 1});
        for (uint8_t *obj : {pair.a, pair.b}) {
            *reinterpret_cast<void **>(obj) = gSvcVTable;
            *reinterpret_cast<void **>(obj + kSub) = gSubVTable;
            *reinterpret_cast<void **>(obj + offsetof(cLogService, mSignal)) = gSignalVTable;
            *reinterpret_cast<void **>(obj + offsetof(cLogService, mThread)) = gThreadVTable;
            *reinterpret_cast<void **>(obj + offsetof(cLogService, mLock90)) = gLockVTable;
            auto *s = reinterpret_cast<cLogService *>(obj);
            new (&s->mOutputs) std::vector<RefHolder>();
            new (&s->mFilters) std::vector<RefHolder>();
            new (&s->mFormatters) std::vector<RefHolder>();
            int next = 0;
            auto fill = [&](std::vector<RefHolder> &list, int n) {
                for (int k = 0; k < n; ++k) {
                    void *o = &fakes[next++];
                    list.push_back(reinterpret_cast<const RefHolder &>(o));
                }
            };
            fill(s->mOutputs, outputs);
            fill(s->mFilters, filters);
            fill(s->mFormatters, formatters);
        }
    }
    cLogService *A() { return reinterpret_cast<cLogService *>(pair.a); }
    cLogService *B() { return reinterpret_cast<cLogService *>(pair.b); }
    ~Fixture()
    {
        gLog.clear();
        for (cLogService *s : {A(), B()}) {
            s->mOutputs.~vector();
            s->mFilters.~vector();
            s->mFormatters.~vector();
        }
    }
};

std::string ListSizes(cLogService *s)
{
    return std::to_string(s->mOutputs.size()) + "/" + std::to_string(s->mOutputs.capacity()) + " " + std::to_string(s->mFilters.size()) + "/" + std::to_string(s->mFilters.capacity()) + " " +
           std::to_string(s->mFormatters.size()) + "/" + std::to_string(s->mFormatters.capacity());
}

const std::vector<std::pair<size_t, size_t>> kListRanges = {{0x60, 36}};

} // namespace

DIFF_TEST(GZLog_ServiceLog, kLog)
{
    Globals globals(ctx);
    auto original = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, int32_t)>(kLog);
    std::mt19937 rng(kLog);
    int offered = 0;
    for (int i = 0; i < 400; ++i) {
        int outputs = rng() % 6;
        Fixture f(rng, outputs);
        gAccept.assign(outputs + 1, 0);
        for (int k = 1; k <= outputs; ++k) gAccept[k] = rng() % 3 == 0 ? static_cast<uint8_t>(1 + rng() % 255) : 0;
        int32_t limit = static_cast<int32_t>(rng() % 12) - 2;
        f.A()->mField24 = f.B()->mField24 = static_cast<uint32_t>(limit);
        int32_t level = static_cast<int32_t>(rng() % 14) - 3;
        if (i % 9 == 0) level = limit; // the boundary
        if (i % 11 == 0) level = limit + 1;
        uint32_t arg = rng();
        globals.SetByte(cLogService::kLoggingDisabledRva, i % 5 == 0 ? static_cast<uint8_t>(1 + rng() % 255) : 0);
        gBase = f.pair.a;
        gLog.clear();
        gRecordBuffer = nullptr;
        gRecordFrees.reset();
        uint8_t ra = original(f.pair.a + kSub, arg, level);
        bool leakedA = RecordBufferLeaked();
        std::string logA = Joined();
        gBase = f.pair.b;
        gLog.clear();
        gRecordBuffer = nullptr;
        gRecordFrees.reset();
        uint8_t rb = ServiceLog(f.pair.b + kSub, nullptr, arg, level);
        bool leakedB = RecordBufferLeaked();
        std::string logB = Joined();
        globals.RestoreBytes();
        ++ctx.cases;
        if (ra != rb) ctx.Fail("level %d limit %d: result %u vs %u", level, limit, ra, rb);
        if (leakedA || leakedB) ctx.Fail("level %d limit %d: record text buffer not freed (%d/%d)", level, limit, leakedA, leakedB);
        if (logA != logB) ctx.Fail("level %d limit %d: calls differ: %.150s / %.150s", level, limit, logA.c_str(), logB.c_str());
        if (FirstDifference(f.pair.a, f.pair.b, cLogService::kSize, kListRanges) >= 0) ctx.Fail("service bytes changed");
        if (ListSizes(f.A()) != ListSizes(f.B())) ctx.Fail("lists changed");
        if (logA.find("accept") != std::string::npos) ++offered;
    }
    if (offered < 50) ctx.Fail("only %d runs offered a record to an output", offered);
}

DIFF_TEST(GZLog_ServiceInit, kInit)
{
    Globals globals(ctx);
    globals.Set(kFrameworkGlobal, &gFramework);
    auto original = ctx.Original<uint8_t(__thiscall *)(void *)>(kInit);
    std::mt19937 rng(kInit);
    for (int i = 0; i < 100; ++i) {
        Fixture f(rng, 0);
        uint8_t flag = i % 3 == 0 ? 0 : static_cast<uint8_t>(rng());
        f.A()->mField20 = f.B()->mField20 = flag;
        gBase = f.pair.a;
        gLog.clear();
        uint8_t ra = original(f.pair.a);
        std::string logA = Joined();
        gBase = f.pair.b;
        gLog.clear();
        uint8_t rb = cLogServiceAdapters::Init(f.B(), nullptr);
        std::string logB = Joined();
        ++ctx.cases;
        if (ra != rb || ra != 1) ctx.Fail("flag %u: result %u/%u", flag, ra, rb);
        if (logA != logB || (flag == 0) != (gLog.size() == 1)) ctx.Fail("flag %u: calls differ: %s / %s", flag, logA.c_str(), logB.c_str());
        if (FirstDifference(f.pair.a, f.pair.b, cLogService::kSize, kListRanges) >= 0) ctx.Fail("flag %u: bytes differ", flag);
        if (flag == 0 && f.B()->mField20 != 1) ctx.Fail("registered flag not set");
    }
}

DIFF_TEST(GZLog_ServiceStopOutputThread, kStop)
{
    auto original = ctx.Original<uint32_t(__thiscall *)(void *)>(kStop);
    std::mt19937 rng(kStop);
    for (int i = 0; i < 100; ++i) {
        Fixture f(rng, rng() % 3);
        gReleaseResult = rng();
        gBase = f.pair.a;
        gLog.clear();
        uint32_t ra = original(f.pair.a);
        std::string logA = Joined();
        gBase = f.pair.b;
        gLog.clear();
        uint32_t rb = f.B()->StopOutputThread();
        std::string logB = Joined();
        ++ctx.cases;
        if (ra != rb || ra != gReleaseResult) ctx.Fail("result %x/%x, want the release's %x", ra, rb, gReleaseResult);
        if (logA != logB || gLog.size() != 4) ctx.Fail("calls differ: %s / %s", logA.c_str(), logB.c_str());
        if (FirstDifference(f.pair.a, f.pair.b, cLogService::kSize, kListRanges) >= 0) ctx.Fail("bytes differ");
        if (f.B()->mOutputRunning != 0) ctx.Fail("running flag not cleared");
    }
}

DIFF_TEST(GZLog_ServiceShutdown, kShutdown)
{
    Globals globals(ctx);
    globals.Set(kFrameworkGlobal, &gFramework);
    auto original = ctx.Original<uint8_t(__thiscall *)(void *)>(kShutdown);
    std::mt19937 rng(kShutdown);
    for (int i = 0; i < 120; ++i) {
        int outputs = rng() % 4, filters = rng() % 3, formatters = rng() % 3;
        Fixture f(rng, outputs, filters, formatters);
        uint8_t flag = i % 3 == 0 ? 0 : static_cast<uint8_t>(rng());
        f.A()->mField20 = f.B()->mField20 = flag;
        gReleaseResult = rng();
        gUnlockResult = rng();
        gBase = f.pair.a;
        gLog.clear();
        uint8_t ra = original(f.pair.a);
        std::string logA = Joined();
        gBase = f.pair.b;
        gLog.clear();
        uint8_t rb = cLogServiceAdapters::Shutdown(f.B(), nullptr);
        std::string logB = Joined();
        ++ctx.cases;
        if (ra != rb || ra != 1) ctx.Fail("result %u/%u", ra, rb);
        if (logA != logB) ctx.Fail("flag %u: calls differ: %.170s / %.170s", flag, logA.c_str(), logB.c_str());
        if (FirstDifference(f.pair.a, f.pair.b, cLogService::kSize, kListRanges) >= 0) ctx.Fail("flag %u: bytes differ", flag);
        if (ListSizes(f.A()) != ListSizes(f.B()) || f.B()->mOutputs.size() || f.B()->mFilters.size() || f.B()->mFormatters.size()) ctx.Fail("lists after: %s / %s", ListSizes(f.A()).c_str(), ListSizes(f.B()).c_str());
        if (f.B()->mField20 != 0) ctx.Fail("flag not cleared");
    }
}

// ---- creating a formatter or a filter ----------------------------------------------------------------------

namespace {

struct Named {
    const void *vtable;
    int id;
    std::string name;
};

uint32_t __fastcall NamedAddRef(Named *self, void *) { Log("obj%d.addref", self->id); return 1; }
uint32_t __fastcall NamedRelease(Named *self, void *) { Log("obj%d.release", self->id); return 1; }
const char *__fastcall NamedName(Named *self, void *) { return self->name.c_str(); }
void *gNamedVTable[12];

struct NamedSetup {
    NamedSetup()
    {
        gNamedVTable[1] = reinterpret_cast<void *>(&NamedAddRef);
        gNamedVTable[2] = reinterpret_cast<void *>(&NamedRelease);
        gNamedVTable[7] = reinterpret_cast<void *>(&NamedName);
    }
} gNamedSetup;

std::string StringsOf(const cLogFormatterText *f)
{
    std::string s = "'" + f->mName.mString + "'";
    for (const cRZString &x : f->mFormats) s += " '" + x.mString + "'";
    for (const cRZString &x : f->mPresets) s += " '" + x.mString + "'";
    return s + " '" + f->mOutput.mString + "' '" + f->mScratch.mString + "'";
}

} // namespace

DIFF_TEST(GZLog_ServiceAddFormatter, kAddFormatter)
{
    auto original = ctx.Original<void(__thiscall *)(void *, const char *)>(kAddFormatter);
    std::mt19937 rng(kAddFormatter);
    int created = 0;
    for (int i = 0; i < 200; ++i) {
        Fixture f(rng, 0);
        std::deque<Named> named;
        int existing = rng() % 5;
        for (int k = 0; k < existing; ++k) named.push_back({gNamedVTable, 100 + k, k % 2 ? "Default" : (k == 2 ? "Alt" : "plain")});
        for (cLogService *s : {f.A(), f.B()})
            for (Named &n : named) {
                void *o = &n;
                s->mFormatters.push_back(reinterpret_cast<const RefHolder &>(o));
            }
        // The alias table and time stamp the new formatter is given (a real table, so the formatter's own
        // format slot has something to look names up in).
        auto aliasesA = std::make_unique<AliasMap>(), aliasesB = std::make_unique<AliasMap>();
        for (AliasMap *m : {aliasesA.get(), aliasesB.get()}) {
            AddAliasText(m, nullptr, "DebugFormat", "#%r %t");
            AddAliasText(m, nullptr, "NoPreset", "");
        }
        f.A()->mAliases = aliasesA.get();
        f.B()->mAliases = aliasesB.get();
        f.A()->mTimestamp = f.B()->mTimestamp = reinterpret_cast<cLogTimestamp *>(static_cast<uintptr_t>(0x5000 + rng() % 0x1000));
        static const char *const names[] = {"Default", "default", "Alt", "plain", "brand-new", "Another", ""};
        std::string name = names[rng() % 7];
        gLog.clear();
        original(f.pair.a + kSub, name.c_str());
        std::string logA = Joined();
        gLog.clear();
        ServiceAddFormatter(f.pair.b + kSub, nullptr, name.c_str());
        std::string logB = Joined();
        ++ctx.cases;
        if (logA != logB) ctx.Fail("\"%s\": calls differ: %.150s / %.150s", name.c_str(), logA.c_str(), logB.c_str());
        if (ListSizes(f.A()) != ListSizes(f.B())) ctx.Fail("\"%s\": lists %s / %s", name.c_str(), ListSizes(f.A()).c_str(), ListSizes(f.B()).c_str());
        if (FirstDifference(f.pair.a, f.pair.b, cLogService::kSize, {{0x60, 36}, {0xd4, 4}, {0xf8, 4}}) >= 0) ctx.Fail("service bytes changed");
        bool made = f.A()->mFormatters.size() > static_cast<size_t>(existing);
        if (made != f.B()->mFormatters.size() > static_cast<size_t>(existing)) ctx.Fail("\"%s\": created differently", name.c_str());
        if (made) {
            ++created;
            auto *a = static_cast<cLogFormatterText *>(f.A()->mFormatters.back().mObject);
            auto *b = static_cast<cLogFormatterText *>(f.B()->mFormatters.back().mObject);
            if (StringsOf(a) != StringsOf(b)) ctx.Fail("\"%s\": strings differ: %.120s / %.120s", name.c_str(), StringsOf(a).c_str(), StringsOf(b).c_str());
            if (a->mUnknown.mRefCount != b->mUnknown.mRefCount || a->mUnknown.mRefCount != 1) ctx.Fail("\"%s\": reference counts %u/%u (want 1)", name.c_str(), a->mUnknown.mRefCount, b->mUnknown.mRefCount);
            if (a->mAliases != aliasesA.get() || b->mAliases != aliasesB.get() || a->mTimestamp != f.A()->mTimestamp || b->mTimestamp != f.B()->mTimestamp) ctx.Fail("\"%s\": table or time stamp not handed over", name.c_str());
            if (a->mVTable != b->mVTable) ctx.Fail("vtable");
        }
        gLog.clear();
        // Release the real objects the lists hold (through the game's own vtable) before the fixture goes.
        f.A()->mFormatters.clear();
        f.B()->mFormatters.clear();
    }
    if (created < 40) ctx.Fail("only %d runs created a formatter", created);
}

DIFF_TEST(GZLog_ServiceAddFilter, kAddFilter)
{
    auto original = ctx.Original<void(__thiscall *)(void *, const char *)>(kAddFilter);
    std::mt19937 rng(kAddFilter);
    int created = 0;
    for (int i = 0; i < 200; ++i) {
        Fixture f(rng, 0);
        std::deque<Named> named;
        int existing = rng() % 5;
        for (int k = 0; k < existing; ++k) named.push_back({gNamedVTable, 100 + k, k % 2 ? "Default" : (k == 2 ? "Alt" : "plain")});
        for (cLogService *s : {f.A(), f.B()})
            for (Named &n : named) {
                void *o = &n;
                s->mFilters.push_back(reinterpret_cast<const RefHolder &>(o));
            }
        static const char *const names[] = {"Default", "default", "Alt", "plain", "brand-new", "Another", ""};
        std::string name = names[rng() % 7];
        gLog.clear();
        original(f.pair.a + kSub, name.c_str());
        std::string logA = Joined();
        gLog.clear();
        ServiceAddFilter(f.pair.b + kSub, nullptr, name.c_str());
        std::string logB = Joined();
        ++ctx.cases;
        if (logA != logB) ctx.Fail("\"%s\": calls differ: %.150s / %.150s", name.c_str(), logA.c_str(), logB.c_str());
        if (ListSizes(f.A()) != ListSizes(f.B())) ctx.Fail("\"%s\": lists %s / %s", name.c_str(), ListSizes(f.A()).c_str(), ListSizes(f.B()).c_str());
        if (FirstDifference(f.pair.a, f.pair.b, cLogService::kSize, {{0x60, 36}}) >= 0) ctx.Fail("service bytes changed");
        bool made = f.A()->mFilters.size() > static_cast<size_t>(existing);
        if (made != f.B()->mFilters.size() > static_cast<size_t>(existing)) ctx.Fail("\"%s\": created differently", name.c_str());
        if (made) {
            ++created;
            auto *a = static_cast<cLogFilterGroupLevels *>(f.A()->mFilters.back().mObject);
            auto *b = static_cast<cLogFilterGroupLevels *>(f.B()->mFilters.back().mObject);
            if (a->mName.mString != b->mName.mString || a->mName.mString != name) ctx.Fail("\"%s\": name '%s' / '%s'", name.c_str(), a->mName.mString.c_str(), b->mName.mString.c_str());
            if (a->mUnknown.mRefCount != b->mUnknown.mRefCount || a->mUnknown.mRefCount != 1) ctx.Fail("\"%s\": reference counts %u/%u (want 1)", name.c_str(), a->mUnknown.mRefCount, b->mUnknown.mRefCount);
            if (a->mNoGroupLevel != b->mNoGroupLevel || a->mAnyGroupLevel != b->mAnyGroupLevel || a->mVTable != b->mVTable || a->mLevels.size() != b->mLevels.size()) ctx.Fail("\"%s\": fields differ", name.c_str());
        }
        gLog.clear();
        f.A()->mFilters.clear();
        f.B()->mFilters.clear();
    }
    if (created < 40) ctx.Fail("only %d runs created a filter", created);
}

// ---- enumerating the name table ------------------------------------------------------------------------------

namespace {

void __stdcall NameCallbackFake(void *arg, const char *name)
{
    Log("name(arg %p, '%s')", arg, name);
}

} // namespace

DIFF_TEST(GZLog_ServiceEnumerateNames, 0xc9d374)
{
    auto original = ctx.Original<void(__thiscall *)(void *, void *, NameCallback)>(0xc9d374);
    std::mt19937 rng(0xc9d374);
    int visited = 0;
    for (int i = 0; i < 200; ++i) {
        Fixture f(rng, 0);
        int count = i % 6 == 0 ? 0 : rng() % 30; // enough names to rehash the table
        for (cLogService *s : {f.A(), f.B()}) {
            new (&s->mNames) NameSet();
            *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(s) + offsetof(cLogService, mLock28)) = gLockVTable;
        }
        for (int k = 0; k < count; ++k) {
            std::string name = std::string(rng() % 24, 'n') + std::to_string(k);
            f.A()->mNames.insert(LogString(name.c_str()));
            f.B()->mNames.insert(LogString(name.c_str()));
        }
        void *arg = reinterpret_cast<void *>(static_cast<uintptr_t>(rng()));
        NameCallback callback = i % 9 == 0 ? nullptr : &NameCallbackFake;
        gBase = f.pair.a;
        gLog.clear();
        original(f.pair.a + kSub, arg, callback);
        std::string logA = Joined();
        gBase = f.pair.b;
        gLog.clear();
        ServiceEnumerateNames(f.pair.b + kSub, nullptr, arg, callback);
        std::string logB = Joined();
        ++ctx.cases;
        if (logA != logB) ctx.Fail("%d names: calls differ: %.150s / %.150s", count, logA.c_str(), logB.c_str());
        if (callback && gLog.size() != static_cast<size_t>(count) + 2) ctx.Fail("%d names: %zu calls", count, gLog.size());
        if (!callback && !gLog.empty()) ctx.Fail("null callback still took the lock");
        visited += count;
        gLog.clear();
        f.A()->mNames.~NameSet();
        f.B()->mNames.~NameSet();
    }
    if (visited < 300) ctx.Fail("only %d names visited", visited);
}
