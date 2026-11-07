// Differential test for nGZLog::cService's constructor, 0xc9c309 (src/rz/nGZLog.cpp). Each run builds
// a service from random bytes with the original and with ours, then compares the raw bytes of the
// service (except pointers), a canonical dump of everything it points to (record pool, formatter,
// filter, alias table, time stamp, process probe, thread signal and thread state), the heap block
// sizes, and the ordered log of every import and vtable call the constructor made. Imports are
// faked through the game's import slots; the thread's start and naming slots (2 and 12 of the
// cRZThread vtable) are replaced in the mapped image with recorders so no thread is created.
#include "difftest.h"
#include "game/vtable.h"
#include "gzlog_support.h"
#include "rz/nGZLog.h"

#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <ctime>
#include <deque>
#include <map>

using namespace gzlog_test;
using namespace nGZLog;

namespace {

constexpr uint32_t kServiceCtor = 0xc9c309;
constexpr uint32_t kFoldedSetLevelLimit = 0x14a260, kFoldedSetFrame = 0x470710; // shared_vf_ bodies of slots 8 and 26
constexpr uint32_t kIatGetCurrentThreadId = 0xd9a194, kIatGetCurrentProcess = 0xd9a1b8, kIatGetSystemInfo = 0xd9a1c4;
constexpr uint32_t kIatLoadLibraryA = 0xd9a128, kIatGetProcAddress = 0xd9a124;

std::vector<std::string> gLog;
const uint8_t *gBase = nullptr;
uint32_t gThreadIdAnswer = 0, gProcessAnswer = 0, gSysInfoSeed = 0;
HMODULE gModuleAnswer = nullptr;
bool gProcAnswers = false;
uintptr_t gHandleCounter = 0;

void Log(const char *fmt, ...)
{
    char buf[200];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    gLog.push_back(buf);
}

DWORD WINAPI FakeThreadId()
{
    Log("GetCurrentThreadId");
    return gThreadIdAnswer;
}

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

HANDLE WINAPI FakeCreateEvent(LPSECURITY_ATTRIBUTES attrs, BOOL manual, BOOL initial, LPCSTR name)
{
    Log("CreateEventA(%p,%d,%d,%p)", static_cast<void *>(attrs), manual, initial, static_cast<const void *>(name));
    return reinterpret_cast<HANDLE>(0x5000 + 4 * gHandleCounter++);
}

uint32_t Rel(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<const uint8_t *>(p) - gBase);
}

void __fastcall FakeThreadStart(void *, void *, void *callback, void *param)
{
    Log("thread Start(callback %p, param this+%x)", callback, Rel(param));
}

void __fastcall FakeThreadSetName(void *, void *, const char *name)
{
    Log("thread SetName(%s)", name);
}

// ---- canonical dump -------------------------------------------------------------------------------

struct Dumper {
    const OriginalImage &image;
    const uint8_t *service;
    const uint32_t *oursVTables; // our class's three vtables (side b), null for the game's side
    std::string out;

    static constexpr uint32_t kFaceOffsets[3] = {0, 4, 0x1c};
    static constexpr uint32_t kGameVTables[3] = {cLogService::kVTable, cLogService::kUnknownVTable, cLogService::kServiceVTable};

    Dumper(const OriginalImage &i, const uint8_t *s, const uint32_t *ours) : image(i), service(s), oursVTables(ours) {}

    // A pointer as a stable token: image addresses by RVA, heap pointers by kind.
    std::string Ptr(const void *p, const char *kind = "heap") const
    {
        if (!p) return "null";
        auto a = reinterpret_cast<uintptr_t>(p);
        if (a >= image.Base() && a < image.Base() + image.Size()) {
            char b[24];
            std::snprintf(b, sizeof(b), "img+%x", static_cast<unsigned>(a - image.Base()));
            return b;
        }
        if (a >= reinterpret_cast<uintptr_t>(service) && a < reinterpret_cast<uintptr_t>(service) + cLogService::kSize) {
            char b[24];
            std::snprintf(b, sizeof(b), "svc+%x", static_cast<unsigned>(a - reinterpret_cast<uintptr_t>(service)));
            return b;
        }
        char b[40];
        std::snprintf(b, sizeof(b), "%s(%zx)", kind, HeapSize(GetProcessHeap(), 0, const_cast<void *>(p)));
        return b;
    }

    void Add(const char *label, const std::string &value)
    {
        out += label;
        out += '=';
        out += value;
        out += '\n';
    }
    void Add(const char *label, uint64_t value) { Add(label, std::to_string(value)); }
    void Str(const char *label, const cRZString &s)
    {
        Add(label, "'" + s.mString + "' ref" + std::to_string(s.mRefCount) + " vt" + Ptr(s.mVTable));
    }
    void Lock(const char *label, const cRZCriticalSection &l)
    {
        Add(label, Ptr(l.mVTable) + " block" + Ptr(l.mBlock) + " count" + std::to_string(l.mBlock ? l.mBlock->lockCount : -1));
    }
    void Unknown(const char *label, const RZUnknownLayout &u) { Add(label, Ptr(u.mVTable) + " ref" + std::to_string(u.mRefCount)); }
    template <typename Map> void Hash(const char *label, const Map &m)
    {
        Add(label, "n" + std::to_string(m.size()) + " buckets" + std::to_string(m.bucket_count()));
    }

    void Record(const cLogRecord *r, const char *label)
    {
        std::string key = label;
        Add((key + ".vt").c_str(), Ptr(r->mVTable) + " " + Ptr(r->mUnknown.mVTable));
        Add((key + ".ref").c_str(), r->mUnknown.mRefCount);
        Str((key + ".text").c_str(), r->mText);
        Str((key + ".text2").c_str(), r->mText2);
    }

    void Run(const cLogService *s)
    {
        Add("id", s->mServiceId);
        Add("prio", std::to_string(s->mPriority) + "/" + std::to_string(s->mTickPriority));
        Add("sysref", static_cast<uint32_t>(s->mRefCount));
        Add("byte18", s->mRunning);
        // The three faces' vtables: ours (b) are named as the game's they stand for.
        std::string vt;
        for (int i = 0; i < 3; ++i) {
            const uint32_t word = *reinterpret_cast<const uint32_t *>(service + kFaceOffsets[i]);
            vt += (oursVTables && word == oursVTables[i] ? Ptr(image.At(kGameVTables[i])) : Ptr(reinterpret_cast<const void *>(static_cast<uintptr_t>(word)), "unknown")) + " ";
        }
        Add("vtables", vt);
        Add("f20/24", std::to_string(s->mField20) + "/" + std::to_string(s->mField24));
        Lock("lock28", s->mLock28);
        Hash("names", s->mNames);
        Add("f50", s->mQueueCount);
        Add("list54", std::to_string(s->mQueue.size()) + "/" + std::to_string(s->mQueue.capacity()) + " " + Ptr(s->mQueue.data()));
        Add("outputs", s->mOutputs.size());
        Add("filters", s->mFilters.size());
        Add("formatters", s->mFormatters.size());
        Add("f84", s->mField84);
        Lock("lock90", s->mLock90);
        Add("threadId", s->mThreadId);
        Add("f9c", s->mOutputThreadId);
        Lock("lockA0", s->mLockA0);
        Add("signal", Ptr(s->mSignal.mVTable) + " state" + Ptr(s->mSignal.mState));
        if (s->mSignal.mState) {
            Add("signal.event", reinterpret_cast<uintptr_t>(s->mSignal.mState->mEvent));
            Add("signal.manual", s->mSignal.mState->mManualReset);
            Str("signal.name", s->mSignal.mState->mName);
            Lock("signal.lock", s->mSignal.mState->mLock);
        }
        Add("thread", Ptr(s->mThread.mVTable) + " state" + Ptr(s->mThread.mState));
        if (s->mThread.mState) {
            const cRZThreadState *t = s->mThread.mState;
            Add("thread.state", std::to_string(t->mField0) + " " + std::to_string(t->mField4) + " id" + std::to_string(t->mThreadId) + " magic" + std::to_string(t->mMagic) + " ref" + std::to_string(t->mRefCount));
            Str("thread.name", t->mName);
            Lock("thread.lock", t->mLock);
        }
        Add("running", s->mOutputRunning);
        Add("recordCount", static_cast<uint32_t>(s->mRecordCount));
        Add("fC0", s->mFrame);
        Add("mapC4", s->mNesting.size());
        Lock("lockCC", s->mLockCC);
        Lock("lockE0", s->mLockE0);
        Add("flagF4", s->mCollectGroups);

        const cLogTimestamp *t = s->mTimestamp;
        Add("timestamp", Ptr(t));
        if (t) {
            Add("ts.flags", std::to_string(t->mFlag0) + std::to_string(t->mFlag1) + std::to_string(t->mFlag2)); // the other bytes are heap garbage
            Add("ts.time", std::to_string(t->mTime));
            Add("ts.elapsed", std::to_string(t->mElapsedLow) + " " + std::to_string(t->mElapsedHigh));
            Add("ts.stopwatch", std::to_string(t->mStopwatch.mMode) + " start" + std::to_string(t->mStopwatch.mStart) + " acc" + std::to_string(t->mStopwatch.mAccumulated));
        }
        const cLogProcessInfo *p = s->mProcessInfo;
        Add("procinfo", Ptr(p));
        if (p) {
            Add("pi.a", std::to_string(p->mField0) + " " + std::to_string(p->mField4) + " has" + std::to_string(p->mHasProcessTimes));
            Add("pi.b", std::to_string(p->mKernelTimeLow) + " " + std::to_string(p->mKernelTimeHigh) + " " + std::to_string(p->mUserTimeLow) + " " + std::to_string(p->mUserTimeHigh));
            Add("pi.c", std::to_string(p->mProcess) + " procs" + std::to_string(p->mProcessors) + " scaled" + std::to_string(p->mScaled));
        }

        // The record pool.
        Add("records", std::to_string(s->mRecords.size()) + "/" + std::to_string(s->mRecords.capacity()) + " " + Ptr(s->mRecords.data()));
        for (size_t i = 0; i < s->mRecords.size(); ++i) {
            char label[24];
            std::snprintf(label, sizeof(label), "rec%zu", i);
            Add(label, Ptr(s->mRecords[i]));
            if (i < 3 || i + 1 == s->mRecords.size()) Record(static_cast<const cLogRecord *>(s->mRecords[i]), label);
            else {
                // The middle records are checked by a digest.
                const cLogRecord *r = static_cast<const cLogRecord *>(s->mRecords[i]);
                Add("recdigest", Ptr(r->mVTable) + std::to_string(r->mUnknown.mRefCount) + r->mText.mString + r->mText2.mString);
            }
        }

        // The alias table.
        const AliasMap *aliases = s->mAliases;
        Add("aliases", Ptr(aliases));
        if (aliases) {
            std::map<std::string, std::string> sorted;
            for (const auto &e : *aliases) sorted[e.first.mString] = e.second.mString;
            Hash("aliases.hash", *aliases);
            for (const auto &e : sorted) Add(("alias." + e.first).c_str(), e.second);
        }

        // The default formatter (and the formatters list).
        const cLogFormatterText *f = static_cast<const cLogFormatterText *>(s->mFormatter.mObject);
        Add("formatter", Ptr(f));
        Add("formatters[0]", s->mFormatters.empty() ? "none" : (s->mFormatters[0].mObject == f ? "same" : "other"));
        if (f) {
            Add("fmt.vt", Ptr(f->mVTable) + " " + Ptr(f->mUnknown.mVTable));
            Add("fmt.ref", f->mUnknown.mRefCount);
            Str("fmt.s0c", f->mName);
            for (int i = 0; i < 5; ++i) {
                Str(("fmt.fa" + std::to_string(i)).c_str(), f->mFormats[i]);
                Str(("fmt.fb" + std::to_string(i)).c_str(), f->mPresets[i]);
            }
            Str("fmt.s16c", f->mOutput);
            Str("fmt.s18c", f->mScratch);
            Add("fmt.timestamp", f->mTimestamp == s->mTimestamp ? "service's" : Ptr(f->mTimestamp));
            Add("fmt.aliases", f->mAliases == s->mAliases ? "service's" : Ptr(f->mAliases));
            Lock("fmt.lock", f->mLock);
        }

        // The default filter (and the filters list).
        const cLogFilterGroupLevels *g = static_cast<const cLogFilterGroupLevels *>(s->mFilter.mObject);
        Add("filter", Ptr(g));
        Add("filters[0]", s->mFilters.empty() ? "none" : (s->mFilters[0].mObject == g ? "same" : "other"));
        if (g) {
            Add("flt.vt", Ptr(g->mVTable) + " " + Ptr(g->mUnknown.mVTable));
            Add("flt.ref", g->mUnknown.mRefCount);
            Str("flt.name", g->mName);
            Add("flt.f2c/30", std::to_string(g->mNoGroupLevel) + "/" + std::to_string(g->mAnyGroupLevel));
            Hash("flt.levels", g->mLevels);
        }
    }
};

// Byte ranges of the service that hold heap pointers (compared through the dump instead).
const std::vector<std::pair<size_t, size_t>> kPointerRanges = {
    {0, 8}, {0x1c, 4}, // the faces' vtables (compared in the dump)
    {0x28 + 4, 4}, {0x30 + 4, 4}, {0x30 + 0xc, 12}, {0x54, 12}, {0x60, 12}, {0x6c, 12}, {0x78, 12}, {0x88, 8},
    {0x90 + 4, 4}, {0xa0 + 4, 4}, {0xa8 + 4, 4}, {0xb0 + 4, 4}, {0xc4, 4}, {0xcc + 4, 4}, {0xd4, 8}, {0xe0 + 4, 4},
    {0xe8, 12}, {0xf8, 4},
};

std::string Context(const std::string &a, const std::string &b, size_t &at)
{
    at = 0;
    while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
    size_t lineStart = a.rfind('\n', at ? at - 1 : 0);
    lineStart = lineStart == std::string::npos ? 0 : lineStart + 1;
    size_t lineEnd = a.find('\n', at);
    size_t lineEndB = b.find('\n', lineStart);
    return a.substr(lineStart, std::min<size_t>(lineEnd == std::string::npos ? a.size() : lineEnd, lineStart + 110) - lineStart) + " | " +
           b.substr(lineStart, std::min<size_t>(lineEndB == std::string::npos ? b.size() : lineEndB, lineStart + 110) - lineStart);
}

// An output back end that records the calls the slots make on it (gOutLog), named "outputQ".
std::string gOutLog;
struct FakeOutput {
    const void *vtable;
    void *formatter = nullptr, *filter = nullptr;
    std::string text;
};
void OutLog(const char *fmt, ...)
{
    char buf[120];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    gOutLog += buf;
    gOutLog += ';';
}
uint8_t __fastcall OutQi(FakeOutput *, void *, uint32_t riid, void **ppv) { OutLog("qi(%x)", riid); if (ppv) *ppv = nullptr; return 0; }
uint32_t __fastcall OutAddRef(FakeOutput *, void *) { OutLog("addref"); return 2; }
uint32_t __fastcall OutRelease(FakeOutput *, void *) { OutLog("release"); return 1; }
uint8_t __fastcall OutLogRecord(FakeOutput *, void *, void *) { OutLog("log"); return 1; }
void *__fastcall OutGetFormatter(FakeOutput *self, void *) { OutLog("getformatter"); return self->formatter; }
void __fastcall OutSetFormatter(FakeOutput *self, void *, void *f) { OutLog("setformatter(%d)", f != nullptr); self->formatter = f; }
void *__fastcall OutGetFilter(FakeOutput *self, void *) { OutLog("getfilter"); return self->filter; }
void __fastcall OutSetFilter(FakeOutput *self, void *, void *f) { OutLog("setfilter(%d)", f != nullptr); self->filter = f; }
void __fastcall OutSetText(FakeOutput *self, void *, const char *text) { OutLog("settext(%s)", text); self->text = text; }
const char *__fastcall OutText(FakeOutput *self, void *) { return self->text.c_str(); }
void __fastcall OutSlot12(FakeOutput *, void *, uint32_t arg) { OutLog("s12(%u)", arg); }
void __fastcall OutSlot14(FakeOutput *, void *, uint32_t arg) { OutLog("s14(%u)", arg); }
void __fastcall OutSlot16(FakeOutput *, void *, uint32_t arg) { OutLog("s16(%u)", arg); }
uint32_t __fastcall OutFlush(FakeOutput *, void *) { OutLog("flush"); return 0; }
void *gOutVTable[24];
const void *OutVTable()
{
    gOutVTable[0] = reinterpret_cast<void *>(&OutQi);
    gOutVTable[1] = reinterpret_cast<void *>(&OutAddRef);
    gOutVTable[2] = reinterpret_cast<void *>(&OutRelease);
    gOutVTable[3] = reinterpret_cast<void *>(&OutLogRecord);
    gOutVTable[6] = reinterpret_cast<void *>(&OutGetFormatter);
    gOutVTable[7] = reinterpret_cast<void *>(&OutSetFormatter);
    gOutVTable[8] = reinterpret_cast<void *>(&OutGetFilter);
    gOutVTable[9] = reinterpret_cast<void *>(&OutSetFilter);
    gOutVTable[10] = reinterpret_cast<void *>(&OutSetText);
    gOutVTable[11] = reinterpret_cast<void *>(&OutText);
    gOutVTable[12] = reinterpret_cast<void *>(&OutSlot12);
    gOutVTable[14] = reinterpret_cast<void *>(&OutSlot14);
    gOutVTable[16] = reinterpret_cast<void *>(&OutSlot16);
    gOutVTable[21] = reinterpret_cast<void *>(&OutFlush);
    return gOutVTable;
}

template <typename Fn, typename... Args> auto Slot(uint8_t *face, int slot, Args... args)
{
    return reinterpret_cast<Fn>((*reinterpret_cast<void ***>(face))[slot])(face, args...);
}

void __stdcall RecordName(void *arg, const char *name)
{
    static_cast<std::string *>(arg)->append(name).append(";");
}

// A round of cIService slot calls through the face's vtable (the game's on the original's service, ours on ours), each
// slot with arguments that tell it from the slots of the same signature; returns what the slots answered. The service
// state they leave is compared by the caller.
std::string SlotRound(uint8_t *face, uint32_t seed)
{
    std::mt19937 rng(seed);
    std::string out;
    auto add = [&](const char *what, uint64_t v) { out += what + std::string("=") + std::to_string(v) + " "; };
    using NameFn = void(__thiscall *)(void *, const char *);
    for (int32_t level : {-1, 0, 1, 2, 3, 4, 7, static_cast<int32_t>(rng() % 9)}) {
        const char *name = Slot<const char *(__thiscall *)(void *, int32_t)>(face, 3, level);
        out += std::string("level") + std::to_string(level) + "=" + (name ? name : "(null)") + " ";
    }
    for (const char *text : {"Error", "warning", "INFO", "Debug", "Fatal", "bogus", ""})
        add("parse", static_cast<uint32_t>(Slot<int32_t(__thiscall *)(void *, const char *)>(face, 4, text)));
    const std::string key = "k" + std::to_string(rng() % 100), value = std::string(rng() % 30, 'v');
    Slot<void(__thiscall *)(void *, const char *, const char *)>(face, 5, key.c_str(), value.c_str());
    Slot<NameFn>(face, 6, "formatterX");                                  // AddFormatter
    Slot<void(__thiscall *)(void *, const char *, uint32_t, const char *)>(face, 7, "formatterX", rng() % 5, "%m");
    Slot<NameFn>(face, 10, "filterY");                                    // AddFilter
    add("groupLevel", Slot<uint8_t(__thiscall *)(void *, const char *, const char *, int32_t)>(face, 9, "filterY", "groupZ", static_cast<int32_t>(rng() % 6)));
    add("outputGroupLevel", Slot<uint8_t(__thiscall *)(void *, const char *, const char *, int32_t)>(face, 20, "none", "groupZ", 2));
    void *found = reinterpret_cast<void *>(1);
    add("query", Slot<uint8_t(__thiscall *)(void *, const char *, void **)>(face, 12, "none", &found));
    add("queried", reinterpret_cast<uintptr_t>(found));
    add("remove", Slot<bool(__thiscall *)(void *, const char *)>(face, 13, "none"));

    // A named output, configured through the output slots.
    static std::deque<FakeOutput> outputs; // the service keeps references: they outlive the run
    outputs.push_back({OutVTable()});
    FakeOutput *output = &outputs.back();
    using OutArgFn = void(__thiscall *)(void *, const char *, uint32_t);
    using OutNameFn = void(__thiscall *)(void *, const char *, const char *);
    gOutLog.clear();
    add("addOutput", Slot<uint8_t(__thiscall *)(void *, const char *, void *)>(face, 11, "outputQ", output));
    Slot<OutNameFn>(face, 16, "outputQ", "filterY");
    Slot<OutNameFn>(face, 17, "outputQ", "formatterX");
    Slot<OutArgFn>(face, 15, "outputQ", 15 + rng() % 4);
    Slot<OutArgFn>(face, 18, "outputQ", 18 + rng() % 4);
    Slot<OutArgFn>(face, 19, "outputQ", 19 + rng() % 4);
    add("outputGroupLevelQ", Slot<uint8_t(__thiscall *)(void *, const char *, const char *, int32_t)>(face, 20, "outputQ", "groupZ", 3));
    found = nullptr;
    add("queryQ", Slot<uint8_t(__thiscall *)(void *, const char *, void **)>(face, 12, "OUTPUTQ", &found));
    add("queriedQ", found == output);
    add("flush", Slot<uint32_t(__thiscall *)(void *)>(face, 29));
    std::string names;
    Slot<void(__thiscall *)(void *, void *, NameCallback)>(face, 25, &names, &RecordName);
    out += "names=" + names + " ";
    const uint32_t limit = rng() % 6;
    Slot<void(__thiscall *)(void *, uint32_t)>(face, 8, limit);
    for (int32_t level : {0, 2, 5, 9}) add("log", Slot<uint8_t(__thiscall *)(void *, uint32_t, int32_t)>(face, 27, rng(), level));
    Slot<void(__thiscall *)(void *, uint32_t)>(face, 26, rng());
    add("removeQ", Slot<bool(__thiscall *)(void *, const char *)>(face, 13, "outputQ"));
    // Clear with an output present (an empty list can't tell clear from flush), then flush: nothing left to reach.
    add("addOutputR", Slot<uint8_t(__thiscall *)(void *, const char *, void *)>(face, 11, "outputR", output));
    add("clear", Slot<uint32_t(__thiscall *)(void *)>(face, 14));
    add("flushAfterClear", Slot<uint32_t(__thiscall *)(void *)>(face, 29));
    out += "output: " + gOutLog;
    return out;
}

} // namespace

DIFF_TEST(GZLog_ServiceCtor, kServiceCtor)
{
    Slots slots(ctx);
    slots.Set(kIatGetCurrentThreadId, reinterpret_cast<void *>(&FakeThreadId));
    slots.Set(kIatGetCurrentProcess, reinterpret_cast<void *>(&FakeGetCurrentProcess));
    slots.Set(kIatGetSystemInfo, reinterpret_cast<void *>(&FakeGetSystemInfo));
    slots.Set(kIatLoadLibraryA, reinterpret_cast<void *>(&FakeLoadLibraryA));
    slots.Set(kIatGetProcAddress, reinterpret_cast<void *>(&FakeGetProcAddress));
    slots.Set(cRZThreadSignal::kIatCreateEventA, reinterpret_cast<void *>(&FakeCreateEvent));
    // No real thread: slots 2 and 12 of the thread vtable (in the mapped image, so both sides use them).
    slots.Set(cRZThread::kVTable + 4 * 2, reinterpret_cast<void *>(&FakeThreadStart));
    slots.Set(cRZThread::kVTable + 4 * 12, reinterpret_cast<void *>(&FakeThreadSetName));

    Clocks clocks; // the wall clock and performance counter, scripted for both sides
    ClockScope clockScope(ctx, clocks);
    auto original = ctx.Original<cLogService *(__thiscall *)(void *)>(kServiceCtor);
    std::mt19937 rng(kServiceCtor);
    float *scale = ctx.image.At<float>(cRZStopwatch::kScalesRva + 8);
    float savedScale = *scale;
    // The preset-name pointer the formatter and alias table read from the game's data.
    uint32_t *presetGlobal = ctx.image.At<uint32_t>(cLogService::kNoPresetPointerRva);
    uint32_t savedPreset = *presetGlobal;
    uint32_t oursVTables[3] = {};
    for (int run = 0; run < 12; ++run) {
        *scale = run % 2 ? 1000000.0f : 0.0f;
        *presetGlobal = savedPreset;
        if (run >= 4) {
            // A different preset name (a pointer to a string that outlives the run).
            static std::string names[4] = {"NoPreset", "Custom", "", "OldDebug"};
            *presetGlobal = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(names[run % 4].c_str()));
        }
        gThreadIdAnswer = run == 0 ? 0 : rng();
        gProcessAnswer = rng();
        gSysInfoSeed = rng();
        gModuleAnswer = rng() % 3 == 0 ? nullptr : reinterpret_cast<HMODULE>(0x20000000 + 0x10000 * (rng() % 64));
        gProcAnswers = rng() % 2 == 0;
        clocks.fileTime = 0x19db1ded53e8000ull + (1000000000ull + rng() % 1000000000) * 10000000ull + rng() % 10000000;
        const uint64_t counterStart = 1000 + rng();
        clocks.counter = {counterStart, counterStart + 1 + rng() % 5000000};

        Pair p(cLogService::kSize, rng);
        gBase = p.a;
        gHandleCounter = run;
        gLog.clear();
        clocks.Rewind();
        auto *ra = original(p.a);
        std::vector<std::string> logA = gLog;
        gBase = p.b;
        gHandleCounter = run;
        gLog.clear();
        clocks.Rewind();
        auto *rb = cLogServiceAdapters::Construct(p.b, nullptr);
        std::vector<std::string> logB = gLog;
        ++ctx.cases;

        auto *a = reinterpret_cast<cLogService *>(p.a);
        auto *b = reinterpret_cast<cLogService *>(p.b);
        if (ra != a || rb != b) ctx.Fail("run %d: returns this %p/%p", run, static_cast<void *>(ra), static_cast<void *>(rb));
        if (logA != logB) {
            size_t k = 0;
            while (k < logA.size() && k < logB.size() && logA[k] == logB[k]) ++k;
            ctx.Fail("run %d: call %zu differs (%zu/%zu calls): %.90s | %.90s", run, k, logA.size(), logB.size(), k < logA.size() ? logA[k].c_str() : "-", k < logB.size() ? logB[k].c_str() : "-");
        }
        if (!p.CanariesIntact()) ctx.Fail("run %d: wrote past the object", run);

        int at = FirstDifference(p.a, p.b, cLogService::kSize, kPointerRanges);
        if (at >= 0) ctx.Fail("run %d: service bytes differ at 0x%x", run, at);

        // Our class's vtables, as our constructor leaves them in the first run; each must lie outside the image and stay
        // the same in every run (the dump names them as the game's they stand for).
        if (run == 0)
            for (int i = 0; i < 3; ++i) oursVTables[i] = *reinterpret_cast<const uint32_t *>(p.b + Dumper::kFaceOffsets[i]);
        for (int i = 0; i < 3; ++i) {
            const uintptr_t word = oursVTables[i];
            if (word >= ctx.image.Base() && word < ctx.image.Base() + ctx.image.Size()) ctx.Fail("run %d: face %d keeps a game vtable", run, i);
        }
        Dumper da(ctx.image, p.a, nullptr), db(ctx.image, p.b, oursVTables);
        da.Run(a);
        db.Run(b);
        if (da.out != db.out) {
            size_t where;
            std::string ctxText = Context(da.out, db.out, where);
            ctx.Fail("run %d: object graph differs at %zu: %s", run, where, ctxText.c_str());
        }
        // The two folded slots of the cIService face (8: the level limit at +0x24, 26: the frame at +0xc0), the game's
        // bodies 0x14a260 / 0x470710 on side a, slots 8 and 26 of our vtable on side b.
        for (int k = 0; k < 4; ++k) {
            const uint32_t level = rng(), frame = k == 3 ? 0 : rng();
            uint8_t *faceA = p.a + 0x1c, *faceB = p.b + 0x1c;
            ctx.Original<void(__thiscall *)(void *, uint32_t)>(kFoldedSetLevelLimit)(faceA, level);
            ctx.Original<void(__thiscall *)(void *, uint32_t)>(kFoldedSetFrame)(faceA, frame);
            void **ourSlots = *reinterpret_cast<void ***>(faceB);
            reinterpret_cast<void(__thiscall *)(void *, uint32_t)>(ourSlots[8])(faceB, level);
            reinterpret_cast<void(__thiscall *)(void *, uint32_t)>(ourSlots[26])(faceB, frame);
            ++ctx.cases;
            at = FirstDifference(p.a, p.b, cLogService::kSize, kPointerRanges);
            if (at >= 0) ctx.Fail("run %d: after slots 8/26 (%x, %x) bytes differ at 0x%x", run, level, frame, at);
            if (b->mField24 != level || b->mFrame != frame) ctx.Fail("run %d: slots 8/26 left %x/%x", run, b->mField24, b->mFrame);
        }
        // Sanity of what the dump compared: the pool and the lists have their contents.
        if (b->mRecords.size() != 100 || b->mRecords.capacity() != 200 || b->mQueue.capacity() != 200 || b->mFormatters.size() != 1 || b->mFilters.size() != 1) ctx.Fail("run %d: pool sizes", run);
        if (logA.size() < 7) ctx.Fail("run %d: only %zu logged calls", run, logA.size());

        // The cIService slots through each side's vtable: what they answer, then the service state they leave.
        const uint32_t roundSeed = rng();
        const std::string answersA = SlotRound(p.a + 0x1c, roundSeed), answersB = SlotRound(p.b + 0x1c, roundSeed);
        ++ctx.cases;
        if (answersA != answersB) {
            size_t where;
            ctx.Fail("run %d: slot answers differ: %s", run, Context(answersA, answersB, where).c_str());
        }
        Dumper ra2(ctx.image, p.a, nullptr), rb2(ctx.image, p.b, oursVTables);
        ra2.Run(a);
        rb2.Run(b);
        if (ra2.out != rb2.out) {
            size_t where;
            ctx.Fail("run %d: after the slot round the object graph differs: %s", run, Context(ra2.out, rb2.out, where).c_str());
        }
        if (FirstDifference(p.a, p.b, cLogService::kSize, kPointerRanges) >= 0) ctx.Fail("run %d: after the slot round the bytes differ", run);
        // The sub-objects of both sides are left alive: each run is a few kilobytes.
    }
    *scale = savedScale;
    *presetGlobal = savedPreset;
}
