// Differential test for src/app/ReportWrite.* Report (0x1bd69a): the unhandled-exception filter's
// body, which runs the whole reporter: the previous filter, the file names, the broadcasts, the
// minidump, the text report, the filter stub again, the stopwatch wait and the verdict.
//
// The pieces below it have their own tests; this one checks the order and arguments of their calls and
// the verdict. The report file is an object whose slots record; the import table is faked for the
// filter functions, the directory check, the minidump file functions, the sleeps and clock (a
// scripted counter feeds the stopwatch) and the process termination; the clock of the file names is
// the real one with the image's time conversion redirected to ours (a run is repeated when the second
// changes between the two sides). The report's level 0 is off, so the system information is not
// needed (WriteReport's own test covers it).
#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#include "game_allocator.h"
#include "app/ReportWrite.h"
#include "app/ReportText.h"
#include "difftest.h"
#include "game/game.h"
#include "rz/GeneralAllocator.h"
#include "rz/RZMemory.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZStopwatch.h"
#include "rz/cRZString.h"
#include "rz/cRZTextFile.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <list>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kReport = 0x1bd69a, kStub = 0x1bd687, kMalloc = 0x1be3b0;
constexpr uint32_t kIatSleep = 0xd9a144, kIatQpc = 0xd9a24c, kIatCloseHandle = 0xd9a17c, kIatGetModuleFileNameA = 0xd9a16c;
constexpr uint32_t kIatGetCurrentThreadId = 0xd9a194, kIatCreateFileA = 0xd9a1ac, kIatSetFilePointer = 0xd9a1b0, kIatSetFilter = 0xd9a1b4;
constexpr uint32_t kIatGetCurrentProcess = 0xd9a1b8, kIatGetCurrentProcessId = 0xd9a1bc, kIatTerminateProcess = 0xd9a1c0, kIatVirtualQuery = 0xd9a1cc;
constexpr uint32_t kIatFindClose = 0xd9a1f4, kIatFindFirstFileA = 0xd9a1f8;
constexpr uint32_t kModuleResolved = 0x10caeb5, kModulePointer = 0x10caeb8, kSystemCodePageCache = 0xff23a4;
constexpr uint32_t kEpochGlobal = 0x10cafe0, kTimeZoneGlobal = 0x10dfc98, kDaylightGlobal = 0x10dfc9c;
constexpr uint32_t kDebuggerFunction = 0x10cb074, kDebuggerAnswer = 0x10cb078;

std::vector<std::string> gLog;

void Log(const char *fmt, ...)
{
    char buf[1100];
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

// ---- the script ----

struct Script {
    bool openOk = true;
    uint32_t length = 0;
    uint32_t previous = 0;     // 0: no previous filter; else the answer of the previous filter plus one
    bool flag19a = false;
    bool debugger = false;
    bool hasDump = true;
    uint32_t createResult = 0x5151; // CreateFileA's answer
    uint64_t step = 1;
    int listeners = 0;
    bool directoryExists = true;
} gS;

void *gStub = nullptr; // the filter stub in the image, for the log

// The report file.
void *gFileVt[40];
void __fastcall FakeSetPath(void *, void *, const void *path)
{
    Log("SetPath[%s]", static_cast<const cRZString *>(path)->mString.c_str());
}
uint8_t __fastcall FakeOpen(void *, void *, uint32_t access, uint32_t creation, uint32_t sharing)
{
    Log("Open(%u,%u,%u)", access, creation, sharing);
    return gS.openOk ? 1 : 0;
}
uint32_t __fastcall FakeLength(void *, void *)
{
    Log("Length");
    return gS.length;
}
void __fastcall FakeFlush(void *, void *)
{
    Log("Flush");
}
void __fastcall FakeClose(void *, void *)
{
    Log("Close");
}
uint8_t __fastcall FakeWrite(void *, void *, const void *data, uint32_t length)
{
    Log("W%u[%.*s]", length, static_cast<int>(length), static_cast<const char *>(data));
    return 1;
}

// Imports.
void *WINAPI FakeSetFilter(void *filter)
{
    if (filter == gStub) Log("SetFilter(STUB)");
    else Log("SetFilter(%p)", filter);
    return nullptr;
}
HANDLE WINAPI FakeGetCurrentProcess()
{
    Log("GetCurrentProcess");
    return reinterpret_cast<HANDLE>(0x1234);
}
DWORD WINAPI FakeGetCurrentProcessId()
{
    Log("GetCurrentProcessId");
    return 0x4321;
}
DWORD WINAPI FakeThreadId()
{
    Log("GetCurrentThreadId");
    return 0x777;
}
BOOL WINAPI FakeTerminateProcess(HANDLE process, UINT code)
{
    Log("TerminateProcess(%p,%u)", process, code);
    return 1;
}
HANDLE WINAPI FakeFindFirst(const char *pattern, WIN32_FIND_DATAA *data)
{
    Log("FindFirstFileA(%s)", pattern);
    if (!gS.directoryExists) return INVALID_HANDLE_VALUE;
    std::memset(data, 0, sizeof(*data));
    data->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
    std::strcpy(data->cFileName, ".");
    return reinterpret_cast<HANDLE>(0x4242);
}
BOOL WINAPI FakeFindClose(HANDLE)
{
    Log("FindClose");
    return TRUE;
}
void *WINAPI FakeCreateFile(const char *name, DWORD access, DWORD share, void *security, DWORD creation, DWORD flags, void *templ)
{
    Log("CreateFileA(%s,%x,%x,%p,%u,%x,%p)", name, access, share, security, creation, flags, templ);
    return reinterpret_cast<void *>(static_cast<uintptr_t>(gS.createResult));
}
DWORD WINAPI FakeSetFilePointer(void *file, LONG distance, LONG *high, DWORD method)
{
    Log("SetFilePointer(%p,%d,%p,%u)", file, distance, high, method);
    return 0;
}
BOOL WINAPI FakeCloseHandle(void *handle)
{
    Log("CloseHandle(%p)", handle);
    return TRUE;
}
struct DumpException {
    DWORD threadId;
    EXCEPTION_POINTERS *pointers;
    BOOL clientPointers;
};
EXCEPTION_POINTERS *gPointers = nullptr;
BOOL WINAPI FakeMiniDump(void *process, DWORD pid, void *file, DWORD type, DumpException *exception, void *user, void *callback)
{
    Log("MiniDump(%p,%u,%p,%u,thread %x ptrs %d client %d,%p,%p)", process, pid, file, type, exception->threadId, exception->pointers == gPointers, exception->clientPointers, user, callback);
    return TRUE;
}
LONG WINAPI FakePreviousFilter(EXCEPTION_POINTERS *info)
{
    Log("PreviousFilter(%d)", info == gPointers);
    return static_cast<LONG>(gS.previous - 1);
}

// The module lookup of WriteReport.
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
SIZE_T WINAPI FakeVirtualQuery(const void *address, MEMORY_BASIC_INFORMATION *info, SIZE_T)
{
    const uint8_t *p = static_cast<const uint8_t *>(address);
    std::memset(info, 0x6b, sizeof(*info));
    if (p >= gImage && p < gImage + 0x1000) {
        info->AllocationBase = gImage;
        return sizeof(*info);
    }
    return 0;
}
DWORD WINAPI FakeGetModuleFileName(void *, char *buffer, DWORD)
{
    const char name[] = "C:\\Games\\Sims 2\\TSBin\\Sims2EP9.exe";
    std::memcpy(buffer, name, sizeof(name));
    return sizeof(name) - 1;
}
DWORD WINAPI FakeModuleFileNameW(void *, wchar_t *buffer, DWORD)
{
    std::wcscpy(buffer, L"C:\\Games\\The Sims 2\\EP9\\TSBin\\Sims2EP9.exe");
    return 44;
}

// The clock for the stopwatch.
uint64_t gNow = 1;
BOOL WINAPI FakeQpc(LARGE_INTEGER *out)
{
    out->QuadPart = static_cast<int64_t>(gNow);
    return TRUE;
}
void FakeCounter(uint64_t *out)
{
    *out = gNow;
}
void WINAPI FakeSleep(DWORD ms)
{
    Log("Sleep(%u)", ms);
    gNow += gS.step;
}

// The broadcast.
void *gListenerVt[8];
void *gMessageVt[40];
struct Listener {
    void *vtable;
    uint32_t tag;
};
Listener gListeners[3];
uint32_t __fastcall FakeAddRef(void *self, void *)
{
    Log("AddRef %u", static_cast<Listener *>(self)->tag);
    return 1;
}
uint32_t __fastcall FakeRelease(void *self, void *)
{
    Log("Release %u", static_cast<Listener *>(self)->tag);
    return 1;
}
uint32_t __fastcall FakeNotify(void *self, void *, void *message)
{
    Log("Notify %u id %x", static_cast<Listener *>(self)->tag, message ? *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(message) + 0xc) : 0xffffffffu);
    return 0;
}
uint32_t __fastcall MsgAddRef(void *, void *)
{
    Log("message AddRef");
    return 1;
}
uint32_t __fastcall MsgRelease(void *, void *)
{
    Log("message Release");
    return 0;
}
void __fastcall MsgSetId(void *self, void *, uint32_t id)
{
    Log("message SetId %x", id);
    *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(self) + 0xc) = id;
}
void *__cdecl TestMalloc(uint32_t size)
{
    void *p = HeapAlloc(GetProcessHeap(), 0, size);
    std::memset(p, 0xcd, size);
    return p;
}
struct CodeRedirect {
    uint8_t *site;
    uint8_t saved[5];
    CodeRedirect(uint8_t *at, void *target) : site(at)
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    ~CodeRedirect() { std::memcpy(site, saved, 5); }
};

// The image's localtime wrapper (0x82fb8c) is redirected to our C runtime's, as in tests/report_blockers.cpp.
int __cdecl StubLocalTime(struct tm *out, const __time64_t *value)
{
    return _localtime64_s(out, value);
}

} // namespace

DIFF_TEST(ReportWrite_Report, kReport)
{
    Slots slots(ctx);
    slots.Set(kIatSleep, reinterpret_cast<void *>(&FakeSleep));
    slots.Set(kIatQpc, reinterpret_cast<void *>(&FakeQpc));
    slots.Set(kIatCloseHandle, reinterpret_cast<void *>(&FakeCloseHandle));
    slots.Set(kIatGetModuleFileNameA, reinterpret_cast<void *>(&FakeGetModuleFileName));
    slots.Set(kIatGetCurrentThreadId, reinterpret_cast<void *>(&FakeThreadId));
    slots.Set(kIatCreateFileA, reinterpret_cast<void *>(&FakeCreateFile));
    slots.Set(kIatSetFilePointer, reinterpret_cast<void *>(&FakeSetFilePointer));
    slots.Set(kIatSetFilter, reinterpret_cast<void *>(&FakeSetFilter));
    slots.Set(kIatGetCurrentProcess, reinterpret_cast<void *>(&FakeGetCurrentProcess));
    slots.Set(kIatGetCurrentProcessId, reinterpret_cast<void *>(&FakeGetCurrentProcessId));
    slots.Set(kIatTerminateProcess, reinterpret_cast<void *>(&FakeTerminateProcess));
    slots.Set(kIatVirtualQuery, reinterpret_cast<void *>(&FakeVirtualQuery));
    slots.Set(kIatFindClose, reinterpret_cast<void *>(&FakeFindClose));
    slots.Set(kIatFindFirstFileA, reinterpret_cast<void *>(&FakeFindFirst));
    slots.Set(kModulePointer, reinterpret_cast<void *>(&FakeModuleFileNameW));
    BuildImage();
    gStub = ctx.image.At(kStub);
    cRZStopwatch::sPerformanceCounterOverride = &FakeCounter;

    // State saved for the life of the test.
    uint8_t savedTable[0x74];
    std::memcpy(savedTable, ctx.image.At<void>(appbase::DbgHelpTable::kGlobal), sizeof(savedTable));
    const uint8_t savedDone = *ctx.image.At<uint8_t>(appbase::DbgHelpTable::kDoneFlag), savedResolved = *ctx.image.At<uint8_t>(kModuleResolved);
    const int32_t savedCodePage = *ctx.image.At<int32_t>(kSystemCodePageCache);
    const uint64_t savedOverhead = *ctx.image.At<uint64_t>(cRZStopwatch::kOverheadRva);
    float savedScales[6];
    std::memcpy(savedScales, ctx.image.At<float>(cRZStopwatch::kScalesRva), sizeof(savedScales));
    const uint32_t savedEpoch = *ctx.image.At<uint32_t>(kEpochGlobal);
    const int32_t savedZone = *ctx.image.At<int32_t>(kTimeZoneGlobal), savedDaylight = *ctx.image.At<int32_t>(kDaylightGlobal);
    void *const savedDebuggerFunction = *ctx.image.At<void *>(kDebuggerFunction);
    const uint8_t savedDebuggerAnswer = *ctx.image.At<uint8_t>(kDebuggerAnswer);
    __time64_t zero = 0; // let our C runtime run its one-time time-zone initialisation first
    struct tm scratch;
    _localtime64_s(&scratch, &zero);
    const int32_t ourZone = *__timezone(), ourDaylight = *__daylight();
    CodeRedirect localTime(ctx.image.At<uint8_t>(0x82fb8c), reinterpret_cast<void *>(&StubLocalTime));
    CodeRedirect malloc(ctx.image.At<uint8_t>(kMalloc), reinterpret_cast<void *>(&TestMalloc));
    *ctx.image.At<uint32_t>(kEpochGlobal) = appbase::cRZDate::kEpochDay;
    *ctx.image.At<uint8_t>(kModuleResolved) = 1;

    // The broadcast's message class and allocator.
    gListenerVt[1] = reinterpret_cast<void *>(&FakeAddRef);
    gListenerVt[2] = reinterpret_cast<void *>(&FakeRelease);
    gListenerVt[3] = reinterpret_cast<void *>(&FakeNotify);
    for (uint32_t i = 0; i < 3; ++i) gListeners[i] = {gListenerVt, i};
    auto *allocator = static_cast<GeneralAllocator *>(::operator new(sizeof(GeneralAllocator)));
    allocator->Construct(nullptr, 0, 0, 0, nullptr, nullptr);
    RZMemory::testGlobalAllocator = allocator;
    void **messageVtable = ctx.image.At<void *>(cRZMessage2Standard::kVTable);
    void *const savedSlots[3] = {messageVtable[1], messageVtable[2], messageVtable[5]};
    DWORD old;
    VirtualProtect(messageVtable, 64, PAGE_READWRITE, &old);
    messageVtable[1] = reinterpret_cast<void *>(&MsgAddRef);
    messageVtable[2] = reinterpret_cast<void *>(&MsgRelease);
    messageVtable[5] = reinterpret_cast<void *>(&MsgSetId);

    std::memcpy(gFileVt, ctx.image.At<void>(cRZTextFile::kVTable), sizeof(gFileVt));
    gFileVt[3] = reinterpret_cast<void *>(&FakeOpen);
    gFileVt[5] = reinterpret_cast<void *>(&FakeClose);
    gFileVt[7] = reinterpret_cast<void *>(&FakeLength);
    gFileVt[16] = reinterpret_cast<void *>(&FakeWrite);
    gFileVt[18] = reinterpret_cast<void *>(&FakeFlush);
    gFileVt[29] = reinterpret_cast<void *>(&FakeSetPath);

    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, EXCEPTION_POINTERS *)>(kReport);
    std::mt19937 rng(0x1bd69a);
    static const int32_t zones[] = {28800, 0, -3600, 18000};
    int verdicts[4] = {0, 0, 0, 0};
    for (int run = 0; run < 400; ++run) {
        gS = Script();
        gS.openOk = rng() % 8 != 0;
        gS.length = rng() % 8 == 0 ? 0x30d41 : 0;
        gS.previous = rng() % 3 == 0 ? 0 : 1 + rng() % 5;
        gS.flag19a = rng() % 3 == 0;
        gS.debugger = rng() % 2;
        gS.hasDump = rng() % 5 != 0;
        gS.createResult = rng() % 6 == 0 ? 0 : (rng() % 6 == 0 ? 0xffffffffu : 0x5151);
        gS.listeners = static_cast<int>(rng() % 3);
        gS.directoryExists = true;
        gS.step = 1 + rng() % 400;
        const uint8_t levelFive = rng() % 2;
        // The scale of mode 4 and the counter's overhead.
        const uint64_t overhead = rng() % 3 == 0 ? 0 : rng() % 20;
        const float scale = (1 + rng() % 16) / 4.0f;
        const int32_t charSize = rng() % 5 == 0 ? 2 : 0;
        const int32_t zone = zones[rng() % 4];
        const int32_t daylight = static_cast<int32_t>(rng() % 3);
        *ctx.image.At<int32_t>(kTimeZoneGlobal) = zone;
        *ctx.image.At<int32_t>(kDaylightGlobal) = daylight;
        *__timezone() = zone;
        *__daylight() = daylight;
        *ctx.image.At<uint64_t>(cRZStopwatch::kOverheadRva) = overhead;
        for (int i = 0; i < 5; ++i) ctx.image.At<float>(cRZStopwatch::kScalesRva)[i] = scale * (i == 2 ? 1.0f : 0.5f + 0.25f * i); // mode 4 is the third
        *ctx.image.At<void *>(kDebuggerFunction) = reinterpret_cast<void *>(0x1111); // looked up already
        *ctx.image.At<uint8_t>(kDebuggerAnswer) = gS.debugger ? 1 : 0;

        CONTEXT context;
        for (size_t i = 0; i < sizeof(context); ++i) reinterpret_cast<uint8_t *>(&context)[i] = static_cast<uint8_t>(rng());
        EXCEPTION_RECORD record;
        std::memset(&record, 0x3d, sizeof(record));
        record.ExceptionCode = 0xc0000005;
        record.ExceptionAddress = gImage + 0x100 + rng() % 0x300;
        EXCEPTION_POINTERS pointers = {&record, &context};
        gPointers = &pointers;

        uint32_t results32[2] = {0, 0};
        std::string results[2];
        for (int attempt = 0;; ++attempt) {
            const __time64_t before = _time64(nullptr);
            for (int side = 0; side < 2; ++side) {
                gLog.clear();
                gNow = 1;
                *ctx.image.At<int32_t>(kSystemCodePageCache) = -3;
                // The table: loaded, with or without MiniDumpWriteDump.
                uint8_t *table = ctx.image.At<uint8_t>(appbase::DbgHelpTable::kGlobal);
                std::memset(table, 0, 0x74);
                *reinterpret_cast<uint32_t *>(table + 0) = 0xbeef;
                *reinterpret_cast<uint32_t *>(table + 4) = 0x4321;
                if (gS.hasDump) {
                    void *fn = reinterpret_cast<void *>(&FakeMiniDump);
                    std::memcpy(table + 0x48, &fn, 4);
                }
                *ctx.image.At<uint8_t>(appbase::DbgHelpTable::kDoneFlag) = 1;

                alignas(8) uint8_t raw[appbase::ExceptionReporterObject::kSize];
                std::memset(raw, 0x99, sizeof(raw));
                auto *obj = reinterpret_cast<appbase::ExceptionReporterObject *>(raw);
                obj->mDirectory.Construct("C:\\Games\\Sims\\Logs\\");
                obj->mReportName.Construct("Exception Report");
                obj->mDumpName.Construct("Minidump");
                obj->mReportFile.Construct("old report file name that is long enough for the heap");
                obj->mDumpFile.Construct("old");
                *reinterpret_cast<void **>(raw + 0xa0) = gFileVt;
                *reinterpret_cast<int32_t *>(raw + 0xa0 + 0x9c) = charSize;
                std::memset(obj->mLevels, 0, sizeof(obj->mLevels));
                obj->mLevels[5] = levelFive;
                obj->mFlag19a = gS.flag19a ? 1 : 0;
                obj->mExtraStarted = 0;
                obj->mPreviousFilter = gS.previous ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&FakePreviousFilter)) : 0;
                new (&obj->mListeners) std::list<void *>();
                for (int i = 0; i < gS.listeners; ++i) obj->mListeners.push_back(&gListeners[i]);
                const uint32_t verdict = side == 0 ? orig(raw, &pointers) : appbase::Report(obj, nullptr, &pointers);
                results32[side] = verdict;
                results[side] = "verdict " + std::to_string(verdict) + " report \"" + obj->mReportFile.mString + "\" dump \"" + obj->mDumpFile.mString + "\" " + Joined();
                obj->mListeners.~list();
                obj->mDirectory.Destruct();
                obj->mReportName.Destruct();
                obj->mDumpName.Destruct();
                obj->mReportFile.Destruct();
                obj->mDumpFile.Destruct();
            }
            const __time64_t after = _time64(nullptr);
            // Both sides read the clock for the file names: retry when the second changed between them.
            if (results[0] == results[1] || before == after || attempt >= 6) break;
        }
        char label[64];
        std::snprintf(label, sizeof(label), "report run %d", run);
        ExpectSame(ctx, label, results[0], results[1]);
        // The verdict classes seen: flag, debugger with previous filter, debugger without, terminate.
        ++verdicts[gS.flag19a ? 0 : gS.debugger ? (gS.previous ? 1 : 2) : 3];
    }
    for (int v : verdicts)
        if (!v) ctx.Fail("a verdict path was never reached: %d %d %d %d", verdicts[0], verdicts[1], verdicts[2], verdicts[3]);

    messageVtable[1] = savedSlots[0];
    messageVtable[2] = savedSlots[1];
    messageVtable[5] = savedSlots[2];
    RZMemory::testGlobalAllocator = nullptr;
    difftest_alloc::ShutdownAndRelease(allocator);
    ::operator delete(allocator);
    cRZStopwatch::sPerformanceCounterOverride = nullptr;
    std::memcpy(ctx.image.At<void>(appbase::DbgHelpTable::kGlobal), savedTable, sizeof(savedTable));
    *ctx.image.At<uint8_t>(appbase::DbgHelpTable::kDoneFlag) = savedDone;
    *ctx.image.At<uint8_t>(kModuleResolved) = savedResolved;
    *ctx.image.At<int32_t>(kSystemCodePageCache) = savedCodePage;
    *ctx.image.At<uint64_t>(cRZStopwatch::kOverheadRva) = savedOverhead;
    std::memcpy(ctx.image.At<float>(cRZStopwatch::kScalesRva), savedScales, sizeof(savedScales));
    *ctx.image.At<uint32_t>(kEpochGlobal) = savedEpoch;
    *ctx.image.At<int32_t>(kTimeZoneGlobal) = savedZone;
    *ctx.image.At<int32_t>(kDaylightGlobal) = savedDaylight;
    *ctx.image.At<void *>(kDebuggerFunction) = savedDebuggerFunction;
    *ctx.image.At<uint8_t>(kDebuggerAnswer) = savedDebuggerAnswer;
    *__timezone() = ourZone;
    *__daylight() = ourDaylight;
}
