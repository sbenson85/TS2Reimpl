// Differential tests for src/app/ReportWrite.*: the stopwatch wait (0x1c9211) and the text report
// (WriteReport 0x1bb410) as a whole.
//
// WriteReport calls everything below it, so the fakes are the union of the pieces' tests: the report
// file is an object whose slots (SetPath, Open, Length, Flush, Close, Write) record; the import
// table is faked for the local time, the thread id, the module lookup (over a fake PE image), the
// exception-code text, the filter removal, and everything the system information asks (a scripted
// version resource); the DbgHelp table points at scripted StackWalk / Toolhelp / SymLoadModule
// fakes, or the table fails to load (the manual stack walk then runs, over an array of frames); the
// stack bytes come from a stand-in stack (the thread information block's stack bounds are pointed at
// a buffer around the call); the broadcast messages go to scripted listeners (the message class's
// slots and the allocator are replaced as in the exception reporter's own test). Both sides run on
// the same script and the log of every call and every write is compared.
//
// Not tested here (the pieces have their own tests): faulting reads in the dumps.
#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#include "game_allocator.h"
#include "app/ReportFinish.h"
#include "app/ReportWrite.h"
#include "difftest.h"
#include "game/game.h"
#include "rz/GeneralAllocator.h"
#include "rz/RZMemory.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZStopwatch.h"
#include "rz/cRZString.h"
#include "rz/cRZTextFile.h"

#include <windows.h>
#include <tlhelp32.h>
#include <intrin.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <list>
#include <map>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kWait = 0x1c9211, kWriteReport = 0x1bb410;
constexpr uint32_t kIatSleep = 0xd9a144, kIatQpc = 0xd9a24c;
constexpr uint32_t kIatGetModuleHandleA = 0xd9a114, kIatFormatMessageA = 0xd9a134, kIatGetModuleFileNameA = 0xd9a16c, kIatGetCurrentThreadId = 0xd9a194;
constexpr uint32_t kIatCloseHandle = 0xd9a17c, kIatLoadLibraryA = 0xd9a128, kIatGetProcAddress = 0xd9a124;
constexpr uint32_t kIatSetFilter = 0xd9a1b4, kIatGetCurrentProcess = 0xd9a1b8, kIatTerminateProcess = 0xd9a1c0, kIatGetLocalTime = 0xd9a1c8;
constexpr uint32_t kIatVirtualQuery = 0xd9a1cc, kIatIsBadReadPtr = 0xd9a1d0, kIatIsBadWritePtr = 0xd9a1d4;
constexpr uint32_t kIatVerSetConditionMask = 0xd9a268, kIatGlobalMemoryStatusEx = 0xd9a26c, kIatVerifyVersionInfoW = 0xd9a274;
constexpr uint32_t kIatWsprintfA = 0xd9a5bc, kIatGetFileVersionInfoA = 0xd9a5e8, kIatVerQueryValueA = 0xd9a5ec, kIatGetFileVersionInfoSizeA = 0xd9a5f0;
constexpr uint32_t kModuleResolved = 0x10caeb5, kModulePointer = 0x10caeb8, kSystemCodePageCache = 0xff23a4;
constexpr uint32_t kMemoryTotal = 0x10cb6a8, kMemoryAvailable = 0x10cb6ac;
constexpr uint32_t kMalloc = 0x1be3b0;

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

std::string Hex(const void *data, size_t n)
{
    std::string out;
    char b[4];
    for (size_t i = 0; i < n; ++i) {
        std::snprintf(b, sizeof(b), "%02x", static_cast<const uint8_t *>(data)[i]);
        out += b;
    }
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

uint32_t Address(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

// ---- 0x1c9211: the stopwatch wait ----

uint64_t gNow = 0;
uint64_t gStep = 1;
int gQpcCalls = 0;

BOOL WINAPI FakeQpc(LARGE_INTEGER *out)
{
    ++gQpcCalls;
    out->QuadPart = static_cast<int64_t>(gNow);
    return TRUE;
}

void FakeCounter(uint64_t *out)
{
    ++gQpcCalls;
    *out = gNow;
}

void WINAPI FakeSleep(DWORD ms)
{
    Log("Sleep(%u) now %llu", ms, gNow);
    gNow += gStep;
}

} // namespace

DIFF_TEST(ReportWrite_WaitStopwatch, kWait)
{
    Slots slots(ctx);
    slots.Set(kIatSleep, reinterpret_cast<void *>(&FakeSleep));
    slots.Set(kIatQpc, reinterpret_cast<void *>(&FakeQpc));
    cRZStopwatch::sPerformanceCounterOverride = &FakeCounter;
    const uint64_t savedOverhead = *ctx.image.At<uint64_t>(cRZStopwatch::kOverheadRva);
    float savedScales[6];
    std::memcpy(savedScales, ctx.image.At<float>(cRZStopwatch::kScalesRva), sizeof(savedScales));
    auto orig = ctx.Original<void(__thiscall *)(const void *, uint32_t)>(kWait);
    std::mt19937 rng(0x1c9211);
    for (int run = 0; run < 400; ++run) {
        *ctx.image.At<uint64_t>(cRZStopwatch::kOverheadRva) = rng() % 4 == 0 ? 0 : rng() % 50;
        for (int i = 0; i < 5; ++i) ctx.image.At<float>(cRZStopwatch::kScalesRva)[i] = (1 + rng() % 40) / 8.0f;
        ctx.image.At<float>(cRZStopwatch::kScalesRva)[5] = 3.0f;
        cRZStopwatch mode;
        mode.mMode = 1 + static_cast<int32_t>(rng() % 6);
        mode.mUnused = static_cast<uint32_t>(rng());
        mode.mStart = rng();
        mode.mAccumulated = rng();
        const uint32_t units = rng() % 7 == 0 ? 0 : rng() % 400;
        // (A counter reading 0 at Start means "not started" to Elapsed, which then stays 0: the wait never ends.)
        const uint64_t start = rng() % 5 == 0 ? 1 : 1 + rng() % 100000;
        gStep = 1 + rng() % 30;
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            gNow = start;
            gQpcCalls = 0;
            if (side == 0) orig(&mode, units);
            else appbase::WaitStopwatch(&mode, nullptr, units);
            results[side] = "qpc " + std::to_string(gQpcCalls) + " end " + std::to_string(gNow) + "|" + Joined();
        }
        char label[64];
        std::snprintf(label, sizeof(label), "wait run %d mode %d units %u", run, mode.mMode, units);
        ExpectSame(ctx, label, results[0], results[1]);
    }
    cRZStopwatch::sPerformanceCounterOverride = nullptr;
    *ctx.image.At<uint64_t>(cRZStopwatch::kOverheadRva) = savedOverhead;
    std::memcpy(ctx.image.At<float>(cRZStopwatch::kScalesRva), savedScales, sizeof(savedScales));
}

namespace {

// ---- 0x1bb410: the environment ----

struct Script {
    // the report file
    bool openOk = true;
    uint32_t length = 0;
    // time and ids
    SYSTEMTIME time{};
    uint32_t threadId = 0;
    // the exception
    uint32_t code = 0xc0000005;
    uint32_t address = 0;
    std::string formatted = "scripted message text.\r\n";
    bool formatFails = false;
    // system information
    uint32_t size = 0x120;
    bool translationOk = true;
    std::map<std::string, std::pair<bool, std::string>> values;
    bool versionOk = true;
    uint64_t totalBytes = 0x80000000ull, availableBytes = 0x30000000ull;
    // DbgHelp
    bool tableLoads = true;
    bool version51 = true;
    std::vector<uint32_t> frames;     // StackWalk answers these pcs, then fails
    std::vector<std::string> modules; // the Toolhelp snapshot lists these
    // listeners
    int listeners = 0;
} gS;

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
void WINAPI FakeGetLocalTime(SYSTEMTIME *time)
{
    *time = gS.time;
}
DWORD WINAPI FakeThreadId()
{
    return gS.threadId;
}
void *WINAPI FakeSetFilter(void *filter)
{
    Log("SetFilter(%p)", filter);
    return nullptr;
}
HANDLE WINAPI FakeGetCurrentProcess()
{
    Log("GetCurrentProcess");
    return reinterpret_cast<HANDLE>(0x1234);
}
BOOL WINAPI FakeTerminateProcess(HANDLE process, UINT code)
{
    Log("TerminateProcess(%p,%u)", process, code);
    return 1;
}
HMODULE WINAPI FakeGetModuleHandleA(const char *name)
{
    Log("GetModuleHandleA(%s)", name ? name : "(null)");
    return reinterpret_cast<HMODULE>(0x400000);
}
DWORD WINAPI FakeFormatMessageA(DWORD flags, const void *source, DWORD id, DWORD language, char *buffer, DWORD size, void *args)
{
    Log("Format(%x,%p,%x,%x,%u,%p,first %02x)", flags, source, id, language, size, args, static_cast<uint8_t>(buffer[0]));
    if (gS.formatFails) return 0;
    std::memcpy(buffer, gS.formatted.c_str(), gS.formatted.size());
    return static_cast<DWORD>(gS.formatted.size());
}

// The fake PE image of the module lookup.
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

// The version resource (the system information).
uint16_t gTranslation[2];
std::string gValueText;
DWORD WINAPI FakeVersionSize(const char *, DWORD *handle)
{
    *handle = 0x77;
    return gS.size;
}
BOOL WINAPI FakeVersionInfo(const char *, DWORD, DWORD size, void *data)
{
    for (DWORD i = 0; i < size; ++i) static_cast<uint8_t *>(data)[i] = static_cast<uint8_t>(i * 5 + 1);
    return TRUE;
}
BOOL WINAPI FakeQuery(const void *, const char *subBlock, void **value, UINT *length)
{
    if (!std::strcmp(subBlock, "\\VarFileInfo\\Translation")) {
        if (!gS.translationOk) return FALSE;
        gTranslation[0] = 0x0409;
        gTranslation[1] = 0x04b0;
        *value = gTranslation;
        *length = 4;
        return TRUE;
    }
    const char *name = std::strrchr(subBlock, '\\');
    auto it = gS.values.find(name ? name + 1 : "");
    if (it == gS.values.end() || !it->second.first) return FALSE;
    gValueText = it->second.second;
    *value = const_cast<char *>(gValueText.c_str());
    *length = static_cast<UINT>(gValueText.size() + 1);
    return TRUE;
}
int __cdecl FakeWsprintf(char *buffer, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int n = std::vsprintf(buffer, format, args);
    va_end(args);
    return n;
}
BOOL WINAPI FakeMemoryStatus(MEMORYSTATUSEX *status)
{
    std::memset(status, 0x3c, sizeof(*status));
    status->dwLength = 0x20;
    status->ullTotalPhys = gS.totalBytes;
    status->ullAvailPhys = gS.availableBytes;
    return TRUE;
}
ULONGLONG WINAPI FakeSetCondition(ULONGLONG mask, DWORD typeMask, BYTE condition)
{
    return mask * 31 + typeMask * 7 + condition + 1;
}
BOOL WINAPI FakeVerify(OSVERSIONINFOEXW *, DWORD, ULONGLONG)
{
    return gS.versionOk;
}
DWORD WINAPI FakeModuleFileNameW(void *, wchar_t *buffer, DWORD)
{
    std::wcscpy(buffer, L"C:\\Games\\The Sims 2\\EP9\\TSBin\\Sims2EP9.exe");
    return 44;
}

// DbgHelp.
int gWalkCalls = 0;
BOOL WINAPI FakeStackWalk(DWORD, void *, void *, appbase::StackFrame32 *frame, CONTEXT *, void *, void *, void *, void *)
{
    const int call = gWalkCalls++;
    Log("StackWalk(%d)", call);
    if (call >= static_cast<int>(gS.frames.size())) return FALSE;
    frame->pc.offset = gS.frames[call];
    frame->frame.offset = 0x12340 + call * 16;
    frame->stack.offset = 0x1000;
    return TRUE;
}
BOOL WINAPI FakeSymFromAddr(void *, DWORD address, DWORD *displacement, appbase::ImageHelpSymbol *symbol)
{
    Log("SymFromAddr(%x)", address);
    if (address & 1) return FALSE;
    std::strcpy(symbol->name, "?Func@@YAXH@Z");
    *displacement = address & 0xff;
    return TRUE;
}
BOOL gSnapshotMore;
size_t gModuleIndex;
BOOL FillModule(MODULEENTRY32 *e)
{
    if (gModuleIndex >= gS.modules.size()) return FALSE;
    std::strcpy(e->szModule, gS.modules[gModuleIndex].c_str());
    std::strcpy(e->szExePath, ("C:\\Games\\" + gS.modules[gModuleIndex]).c_str());
    e->modBaseAddr = reinterpret_cast<BYTE *>(0x400000 + 0x10000 * gModuleIndex);
    e->modBaseSize = 0x8000 + static_cast<DWORD>(gModuleIndex);
    ++gModuleIndex;
    return TRUE;
}
void *WINAPI FakeSnapshot(DWORD, DWORD)
{
    return reinterpret_cast<void *>(0x5150);
}
BOOL WINAPI FakeFirst(void *, MODULEENTRY32 *e)
{
    gModuleIndex = 0;
    return FillModule(e);
}
BOOL WINAPI FakeNext(void *, MODULEENTRY32 *e)
{
    return FillModule(e);
}
BOOL WINAPI FakeCloseHandle(void *)
{
    return TRUE;
}
DWORD WINAPI FakeSymOptions(DWORD options)
{
    Log("SymOptions(%x)", options);
    return options;
}
DWORD WINAPI FakeSymLoad(void *, void *, const char *image, const char *name, DWORD base, DWORD size)
{
    Log("SymLoad([%s],[%s],%x,%u)", image, name, base, size);
    return base;
}
void *WINAPI FakeLoadLibrary(const char *)
{
    return reinterpret_cast<void *>(0x1000);
}
void *WINAPI FakeGetProcAddress(void *, const char *)
{
    return nullptr;
}

// Frames of the manual walk and the readability answers.
alignas(16) uint32_t gArena[64];
BOOL WINAPI FakeIsBadWritePtr(void *p, UINT_PTR size)
{
    const uintptr_t a = reinterpret_cast<uintptr_t>(p), lo = reinterpret_cast<uintptr_t>(gArena);
    const BOOL bad = a < lo || a >= lo + sizeof(gArena);
    Log("IBW(%d,%u)", bad, static_cast<uint32_t>(size));
    return bad;
}
BOOL gBadRead = FALSE;
BOOL WINAPI FakeIsBadRead(const void *, UINT_PTR size)
{
    Log("IBR(%u)", static_cast<uint32_t>(size));
    return gBadRead;
}

// The code the instruction dump reads and the stand-in stack.
alignas(16) uint8_t gCode[0x400];
alignas(16) uint8_t gStack[0x2000];

// ---- the broadcast: listeners, message slots, allocator ----

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
appbase::ExceptionReporterObject *gReporter = nullptr;
uint32_t __fastcall FakeNotify(void *self, void *, void *message)
{
    Log("Notify %u id %x", static_cast<Listener *>(self)->tag, message ? *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(message) + 0xc) : 0xffffffffu);
    if (static_cast<Listener *>(self)->tag == 0 && gReporter) gReporter->AddExtraData("Label", "value", 0); // a listener adding its data
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
    CodeRedirect(difftest::Context &ctx, uint32_t rva, void *target) : site(ctx.image.At<uint8_t>(rva))
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

// Everything installed for the life of a test.
struct Env {
    difftest::Context &ctx;
    Slots slots;
    uint8_t savedTable[0x74];
    uint8_t savedDone, savedResolved;
    void *savedCached;
    uint32_t savedTotal, savedAvailable;
    int32_t savedCodePage;
    CodeRedirect malloc;
    GeneralAllocator *allocator;
    void **messageVtable;
    void *savedSlots[3];

    explicit Env(difftest::Context &c) : ctx(c), slots(c), malloc(c, kMalloc, reinterpret_cast<void *>(&TestMalloc))
    {
        slots.Set(kIatGetLocalTime, reinterpret_cast<void *>(&FakeGetLocalTime));
        slots.Set(kIatGetCurrentThreadId, reinterpret_cast<void *>(&FakeThreadId));
        slots.Set(kIatSetFilter, reinterpret_cast<void *>(&FakeSetFilter));
        slots.Set(kIatGetCurrentProcess, reinterpret_cast<void *>(&FakeGetCurrentProcess));
        slots.Set(kIatTerminateProcess, reinterpret_cast<void *>(&FakeTerminateProcess));
        slots.Set(kIatGetModuleHandleA, reinterpret_cast<void *>(&FakeGetModuleHandleA));
        slots.Set(kIatFormatMessageA, reinterpret_cast<void *>(&FakeFormatMessageA));
        slots.Set(kIatVirtualQuery, reinterpret_cast<void *>(&FakeVirtualQuery));
        slots.Set(kIatGetModuleFileNameA, reinterpret_cast<void *>(&FakeGetModuleFileName));
        slots.Set(kIatVerSetConditionMask, reinterpret_cast<void *>(&FakeSetCondition));
        slots.Set(kIatGlobalMemoryStatusEx, reinterpret_cast<void *>(&FakeMemoryStatus));
        slots.Set(kIatVerifyVersionInfoW, reinterpret_cast<void *>(&FakeVerify));
        slots.Set(kIatWsprintfA, reinterpret_cast<void *>(&FakeWsprintf));
        slots.Set(kIatGetFileVersionInfoA, reinterpret_cast<void *>(&FakeVersionInfo));
        slots.Set(kIatVerQueryValueA, reinterpret_cast<void *>(&FakeQuery));
        slots.Set(kIatGetFileVersionInfoSizeA, reinterpret_cast<void *>(&FakeVersionSize));
        slots.Set(kIatCloseHandle, reinterpret_cast<void *>(&FakeCloseHandle));
        slots.Set(kIatLoadLibraryA, reinterpret_cast<void *>(&FakeLoadLibrary));
        slots.Set(kIatGetProcAddress, reinterpret_cast<void *>(&FakeGetProcAddress));
        slots.Set(kIatIsBadReadPtr, reinterpret_cast<void *>(&FakeIsBadRead));
        slots.Set(kIatIsBadWritePtr, reinterpret_cast<void *>(&FakeIsBadWritePtr));
        std::memcpy(savedTable, ctx.image.At<void>(appbase::DbgHelpTable::kGlobal), sizeof(savedTable));
        savedDone = *ctx.image.At<uint8_t>(appbase::DbgHelpTable::kDoneFlag);
        savedResolved = *ctx.image.At<uint8_t>(kModuleResolved);
        savedCached = *ctx.image.At<void *>(kModulePointer);
        savedTotal = *ctx.image.At<uint32_t>(kMemoryTotal);
        savedAvailable = *ctx.image.At<uint32_t>(kMemoryAvailable);
        savedCodePage = *ctx.image.At<int32_t>(kSystemCodePageCache);
        *ctx.image.At<uint8_t>(kModuleResolved) = 1;
        *ctx.image.At<void *>(kModulePointer) = reinterpret_cast<void *>(&FakeModuleFileNameW);

        std::memcpy(gFileVt, ctx.image.At<void>(cRZTextFile::kVTable), sizeof(gFileVt));
        gFileVt[3] = reinterpret_cast<void *>(&FakeOpen);
        gFileVt[5] = reinterpret_cast<void *>(&FakeClose);
        gFileVt[7] = reinterpret_cast<void *>(&FakeLength);
        gFileVt[16] = reinterpret_cast<void *>(&FakeWrite);
        gFileVt[18] = reinterpret_cast<void *>(&FakeFlush);
        gFileVt[29] = reinterpret_cast<void *>(&FakeSetPath);

        // The broadcast.
        gListenerVt[0] = nullptr;
        gListenerVt[1] = reinterpret_cast<void *>(&FakeAddRef);
        gListenerVt[2] = reinterpret_cast<void *>(&FakeRelease);
        gListenerVt[3] = reinterpret_cast<void *>(&FakeNotify);
        for (uint32_t i = 0; i < 3; ++i) gListeners[i] = {gListenerVt, i};
        allocator = static_cast<GeneralAllocator *>(::operator new(sizeof(GeneralAllocator)));
        allocator->Construct(nullptr, 0, 0, 0, nullptr, nullptr);
        RZMemory::testGlobalAllocator = allocator;
        messageVtable = ctx.image.At<void *>(cRZMessage2Standard::kVTable);
        savedSlots[0] = messageVtable[1];
        savedSlots[1] = messageVtable[2];
        savedSlots[2] = messageVtable[5];
        DWORD old;
        VirtualProtect(messageVtable, 64, PAGE_READWRITE, &old);
        messageVtable[1] = reinterpret_cast<void *>(&MsgAddRef);
        messageVtable[2] = reinterpret_cast<void *>(&MsgRelease);
        messageVtable[5] = reinterpret_cast<void *>(&MsgSetId);
        BuildImage();
    }
    ~Env()
    {
        messageVtable[1] = savedSlots[0];
        messageVtable[2] = savedSlots[1];
        messageVtable[5] = savedSlots[2];
        RZMemory::testGlobalAllocator = nullptr;
        difftest_alloc::ShutdownAndRelease(allocator);
        ::operator delete(allocator);
        std::memcpy(ctx.image.At<void>(appbase::DbgHelpTable::kGlobal), savedTable, sizeof(savedTable));
        *ctx.image.At<uint8_t>(appbase::DbgHelpTable::kDoneFlag) = savedDone;
        *ctx.image.At<uint8_t>(kModuleResolved) = savedResolved;
        *ctx.image.At<void *>(kModulePointer) = savedCached;
        *ctx.image.At<uint32_t>(kMemoryTotal) = savedTotal;
        *ctx.image.At<uint32_t>(kMemoryAvailable) = savedAvailable;
        *ctx.image.At<int32_t>(kSystemCodePageCache) = savedCodePage;
    }

    // The DbgHelp table, loaded (done flag) or not.
    void SetTable()
    {
        uint8_t *table = ctx.image.At<uint8_t>(appbase::DbgHelpTable::kGlobal);
        std::memset(table, 0, 0x74);
        auto set = [&](uint32_t offset, void *value) { std::memcpy(table + offset, &value, 4); };
        *reinterpret_cast<uint32_t *>(table + 0) = 0xbeef;
        *reinterpret_cast<uint32_t *>(table + 4) = 0x4321;
        table[8] = gS.version51 ? 1 : 0;
        set(0x28, reinterpret_cast<void *>(&FakeSymOptions));
        set(0x2c, reinterpret_cast<void *>(&FakeStackWalk));
        set(0x30, reinterpret_cast<void *>(0x30303030));
        set(0x34, reinterpret_cast<void *>(0x34343434));
        set(0x38, reinterpret_cast<void *>(&FakeSymFromAddr));
        set(0x40, reinterpret_cast<void *>(&FakeSymLoad));
        set(0x50, reinterpret_cast<void *>(&FakeSnapshot));
        set(0x54, reinterpret_cast<void *>(&FakeFirst));
        set(0x58, reinterpret_cast<void *>(&FakeNext));
        *ctx.image.At<uint8_t>(appbase::DbgHelpTable::kDoneFlag) = gS.tableLoads ? 1 : 0;
    }
    void Reset(bool overrideMemory, uint32_t total, uint32_t available)
    {
        *ctx.image.At<int32_t>(kSystemCodePageCache) = -3;
        *ctx.image.At<uint32_t>(kMemoryTotal) = overrideMemory ? total : 0;
        *ctx.image.At<uint32_t>(kMemoryAvailable) = overrideMemory ? available : 0;
        gLog.clear();
        gWalkCalls = 0;
        gModuleIndex = 0;
        SetTable();
    }
};

const uint32_t kCodes[] = {0xc0000005, 0xc0000094, 0x80000003, 0xc00000fd, 0xc000001d, 0xc0000374, 0xe06d7363, 0x40010005, 0x12345678};

} // namespace

// ---- 0x1bb410 ----

DIFF_TEST(ReportWrite_WriteReport, kWriteReport)
{
    Env env(ctx);
    auto orig = ctx.Original<void(__thiscall *)(void *, EXCEPTION_POINTERS *)>(kWriteReport);
    std::mt19937 rng(0x1bb410);
    static const char *const kValues[] = {"", "1.18.0.187", "The Sims 2", "a value long enough to need the heap, certainly more than fifteen characters"};
    static const char *const kNames[] = {"FileDescription", "FileVersion", "Debug Version"};
    static const char *const kModuleNames[] = {"Sims2EP9.exe", "kernel32.dll", "d3d9.dll", "a very long module name that needs the heap.dll"};
    // Parts of the report the runs must have reached (counted in the original's log).
    std::map<std::string, int> seen;
    for (const char *marker : {"Exception time", "Exception code", "scripted message", "Current thread ID", "Version information", "Application/module path", "Exception module",
                               "Exception address", "Registers", "EAX:", "Flags:", "Loaded Modules", "0x00400000", "SymLoad", "Call stack:", "Func@@YAXH@Z()", "load failed",
                               "Stack data: (ESP", "Instruction data: (EIP", "TerminateProcess", "Notify", "Extra Exception Data End", "Close|", "IBR(1)"})
        seen[marker] = 0;
    for (int run = 0; run < 700; ++run) {
        gS = Script();
        gS.openOk = rng() % 12 != 0;
        gS.length = rng() % 9 == 0 ? 0x30d40 + rng() % 3 : rng() % 0x30d00;
        if (rng() % 15 == 0) gS.length = 0x30d3f;
        gS.time.wYear = static_cast<WORD>(1990 + rng() % 60);
        gS.time.wMonth = static_cast<WORD>(1 + rng() % 12);
        gS.time.wDay = static_cast<WORD>(1 + rng() % 28);
        gS.time.wHour = static_cast<WORD>(rng() % 24);
        gS.time.wMinute = static_cast<WORD>(rng() % 60);
        gS.time.wSecond = static_cast<WORD>(rng() % 60);
        gS.time.wMilliseconds = static_cast<WORD>(rng() % 1000);
        gS.time.wDayOfWeek = static_cast<WORD>(rng() % 7);
        gS.threadId = static_cast<uint32_t>(rng());
        gS.code = kCodes[rng() % 9];
        gS.formatFails = rng() % 5 == 0;
        const uint32_t image = Address(gImage);
        switch (rng() % 4) {
        case 0: gS.address = image + 0x100 + rng() % 0x300; break;
        case 1: gS.address = image + 0x500 + rng() % 0x100; break;
        case 2: gS.address = image + 0x800 + rng() % 0x100; break;
        default: gS.address = static_cast<uint32_t>(rng()); break;
        }
        gS.size = rng() % 6 == 0 ? 0 : 1 + rng() % 200;
        gS.translationOk = rng() % 6 != 0;
        gS.versionOk = rng() % 2;
        for (const char *name : kNames) gS.values[name] = {rng() % 4 != 0, kValues[rng() % 4]};
        gS.tableLoads = rng() % 6 != 0;
        gS.version51 = rng() % 4 != 0;
        const int frames = static_cast<int>(rng() % 4);
        for (int i = 0; i < frames; ++i) gS.frames.push_back(image + 0x100 + rng() % 0x300);
        const int modules = static_cast<int>(rng() % 4);
        for (int i = 0; i < modules; ++i) gS.modules.push_back(kModuleNames[rng() % 4]);
        gS.listeners = static_cast<int>(rng() % 3);
        gBadRead = rng() % 10 == 0;
        uint8_t levels[6];
        for (uint8_t &l : levels) l = rng() % 5 == 0 ? 0 : (rng() % 6 == 0 ? 7 : 1);
        if (run % 4 == 0) std::memset(levels, 1, sizeof(levels));
        const uint8_t extraStarted = static_cast<uint8_t>(rng() % 3 == 0 ? 1 : 0);
        const int32_t charSize = rng() % 5 == 0 ? 2 : 0;
        const bool overrideMemory = rng() % 7 == 0;
        const uint32_t overrideTotal = 1 + rng() % 5000, overrideAvailable = rng() % 5000;

        // The arena of frames for the manual walk: a short chain.
        std::memset(gArena, 0, sizeof(gArena));
        const uint32_t arenaBase = Address(gArena);
        const int chain = static_cast<int>(rng() % 4);
        for (int i = 0; i < chain; ++i) {
            gArena[i * 4] = i + 1 < chain ? arenaBase + (i + 1) * 16 : (rng() % 2 ? 0 : arenaBase + i * 16 + 2);
            gArena[i * 4 + 1] = image + 0x100 + rng() % 0x300;
        }
        for (uint8_t &b : gCode) b = static_cast<uint8_t>(rng());
        for (uint8_t &b : gStack) b = static_cast<uint8_t>(rng());
        const uint32_t limit = Address(gStack), base = limit + sizeof(gStack);

        CONTEXT context;
        for (size_t i = 0; i < sizeof(context); ++i) reinterpret_cast<uint8_t *>(&context)[i] = static_cast<uint8_t>(rng());
        context.Ebp = chain ? arenaBase : arenaBase + 0x30; // a readable frame (zeroed) when the chain is empty
        context.Eip = Address(gCode) + 0x90 + rng() % 0x70;
        context.Esp = limit + 0x300 + rng() % 0x1000;
        EXCEPTION_RECORD record;
        std::memset(&record, 0x3d, sizeof(record));
        record.ExceptionCode = gS.code;
        record.ExceptionAddress = reinterpret_cast<void *>(static_cast<uintptr_t>(gS.address));
        EXCEPTION_POINTERS pointers = {&record, &context};

        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            env.Reset(overrideMemory, overrideTotal, overrideAvailable);
            std::memset(gTranslation, 0, sizeof(gTranslation));
            alignas(8) uint8_t raw[appbase::ExceptionReporterObject::kSize];
            std::memset(raw, 0x99, sizeof(raw));
            auto *obj = reinterpret_cast<appbase::ExceptionReporterObject *>(raw);
            obj->mReportFile.Construct("Exception Report.txt");
            *reinterpret_cast<void **>(raw + 0xa0) = gFileVt;
            *reinterpret_cast<int32_t *>(raw + 0xa0 + 0x9c) = charSize;
            std::memcpy(obj->mLevels, levels, sizeof(levels));
            obj->mExtraStarted = extraStarted;
            new (&obj->mListeners) std::list<void *>();
            for (int i = 0; i < gS.listeners; ++i) obj->mListeners.push_back(&gListeners[i]);
            gReporter = obj;
            {
                StackBounds bounds(base, limit);
                if (side == 0) orig(raw, &pointers);
                else appbase::WriteReport(obj, nullptr, &pointers);
            }
            std::string log = Joined();
            // The original's allocator allocations are not logged (TestMalloc does not log); nothing to strip.
            results[side] = "extra " + std::to_string(obj->mExtraStarted) + " " + log;
            obj->mListeners.~list();
            obj->mReportFile.Destruct();
        }
        char label[64];
        std::snprintf(label, sizeof(label), "write report run %d", run);
        ExpectSame(ctx, label, results[0], results[1]);
        for (auto &marker : seen)
            if (results[0].find(marker.first) != std::string::npos) ++marker.second;
    }
    for (auto &marker : seen)
        if (!marker.second) ctx.Fail("the script never reached: %s", marker.first.c_str());
    gBadRead = FALSE;
}
