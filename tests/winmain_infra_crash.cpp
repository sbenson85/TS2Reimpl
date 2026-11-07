// Contract tests for WinMain's crash-reporter start-up (0xc2b206, docs/contracts/crash-reporter-start.md)
// and launch analytics (0xc2df67, docs/contracts/analytics.md).
//
// These are `replaced` components: ours deliberately does less than the original. The tests are
// differential on the KEPT effects only. The original runs with every operating-system entry point it
// reaches faked in the import table (ours reads the same fakes through winmaininfra::OsTable()), and
// stops at the first effect the contract drops, by a fake that throws a sentinel. Each fake that is
// part of a dropped effect logs a line starting with "DROPPED"; the comparison is
//   original log without DROPPED lines == ours log
// and a second check that the DROPPED lines are exactly the ones the contract lists (so a drop that
// quietly turned into a kept effect, or the reverse, shows up).
#define NOMINMAX
#include "app/WinMainInfra.h"
#include "difftest.h"
#include "game/game.h"

#include <windows.h>
#include <objbase.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kStartCrashReporter = 0xc2b206, kSendAnalytics = 0xc2df67;

constexpr uint32_t kIatGetModuleFileNameA = 0xd9a16c, kIatGetFileVersionInfoSizeA = 0xd9a5f0;
constexpr uint32_t kIatCoInitializeSecurity = 0xd9a6e0, kIatCoInitializeEx = 0xd9a6e4, kIatCoCreateInstance = 0xd9a6ec;
constexpr uint32_t kIatInternetCloseHandle = 0xd9a5f8, kIatInternetConnectA = 0xd9a5fc, kIatInternetReadFile = 0xd9a600, kIatInternetWriteFile = 0xd9a604,
                   kIatHttpOpenRequestA = 0xd9a608, kIatHttpAddRequestHeadersA = 0xd9a60c, kIatHttpSendRequestExA = 0xd9a610, kIatHttpEndRequestA = 0xd9a614,
                   kIatHttpQueryInfoA = 0xd9a618, kIatInternetOpenA = 0xd9a61c;
constexpr uint32_t kIatGetSystemTimeAsFileTime = 0xd9a2a4;
constexpr uint32_t kCrashDatabaseInit = 0xc4ea10; // CrashReportDatabase::Initialize wrapper: the first file effect

// The analytics event buffer and its write-only statics (0xc2df67 / 0xc2de96): 0x10fe048..0x10fe447 is
// the body (vsprintf_s with size 0x400), 0x10fe448..0x10fe457 the statics and their guards.
constexpr uint32_t kAnalyticsBuffer = 0x10fe048, kAnalyticsStatics = 0x10fe448, kAnalyticsEnd = 0x10fe458;

struct Stop {};

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

// The log with or without the DROPPED lines, one line per entry.
std::string Joined(bool dropped)
{
    std::string out;
    for (const std::string &s : gLog) {
        bool isDropped = s.compare(0, 7, "DROPPED") == 0;
        if (isDropped == dropped) out += s + "\n";
    }
    return out;
}

void ExpectSame(difftest::Context &ctx, const char *what, const std::string &expected, const std::string &actual)
{
    ++ctx.cases;
    if (expected != actual) {
        size_t at = 0;
        while (at < expected.size() && at < actual.size() && expected[at] == actual[at]) ++at;
        size_t from = at > 60 ? at - 60 : 0;
        std::string x = expected.substr(from, 160), y = actual.substr(from, 160);
        for (char &c : x) if (c == '\n') c = '|';
        for (char &c : y) if (c == '\n') c = '|';
        ctx.Fail("%s:\n  expected: %s\n  actual:   %s", what, x.c_str(), y.c_str());
    }
}

class Imports {
public:
    explicit Imports(difftest::Context &c) : ctx(c) {}
    ~Imports()
    {
        for (auto it = saved.rbegin(); it != saved.rend(); ++it) *ctx.image.At<void *>(it->first) = it->second;
    }
    void Set(uint32_t iatRva, void *fake)
    {
        void **slot = ctx.image.At<void *>(iatRva);
        saved.emplace_back(iatRva, *slot);
        *slot = fake;
    }

private:
    difftest::Context &ctx;
    std::vector<std::pair<uint32_t, void *>> saved;
};

// Replaces the first five bytes of an original function with a jump to a fake (restored afterwards).
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

// ---- crash reporter start-up ----

HRESULT gCoInitializeResult = S_OK, gSecurityResult = S_OK;

HRESULT WINAPI FakeCoInitializeEx(void *reserved, DWORD flags)
{
    Log("CoInitializeEx(%p, %u)", reserved, flags);
    return gCoInitializeResult;
}
HRESULT WINAPI FakeCoInitializeSecurity(PSECURITY_DESCRIPTOR sd, LONG authServices, SOLE_AUTHENTICATION_SERVICE *list, void *reserved1, DWORD authnLevel, DWORD impLevel,
                                        void *authList, DWORD capabilities, void *reserved3)
{
    Log("CoInitializeSecurity(%p, %d, %p, %p, %u, %u, %p, %u, %p)", sd, authServices, list, reserved1, authnLevel, impLevel, authList, capabilities, reserved3);
    return gSecurityResult;
}
// Dropped effects the original reaches, each ending the original run.
HRESULT WINAPI FakeCoCreateInstance(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID *)
{
    Log("DROPPED CoCreateInstance (WMI locator)");
    throw Stop{};
}
void *__cdecl FakeDatabaseInit(void *, void *)
{
    Log("DROPPED CrashReportDatabase::Initialize");
    throw Stop{};
}
// Dropped effects before COM (the version annotation).
DWORD WINAPI FakeModuleFileNameA(HMODULE, char *buffer, DWORD)
{
    Log("DROPPED GetModuleFileNameA (version annotation)");
    std::memcpy(buffer, "C:\\game\\TS2.exe", 16);
    return 15;
}
DWORD WINAPI FakeVersionInfoSize(const char *, DWORD *handle)
{
    Log("DROPPED GetFileVersionInfoSizeA (version annotation)");
    if (handle) *handle = 0;
    return 0; // no version resource: the annotation is skipped
}

} // namespace

DIFF_TEST(WinMainInfra_StartCrashReporter, kStartCrashReporter)
{
    Imports imports(ctx);
    imports.Set(kIatCoInitializeEx, reinterpret_cast<void *>(&FakeCoInitializeEx));
    imports.Set(kIatCoInitializeSecurity, reinterpret_cast<void *>(&FakeCoInitializeSecurity));
    imports.Set(kIatCoCreateInstance, reinterpret_cast<void *>(&FakeCoCreateInstance));
    imports.Set(kIatGetModuleFileNameA, reinterpret_cast<void *>(&FakeModuleFileNameA));
    imports.Set(kIatGetFileVersionInfoSizeA, reinterpret_cast<void *>(&FakeVersionInfoSize));
    CodeRedirect database(ctx, kCrashDatabaseInit, reinterpret_cast<void *>(&FakeDatabaseInit));

    winmaininfra::Os &os = winmaininfra::OsTable();
    const winmaininfra::Os saved = os;
    os.CoInitializeEx = &FakeCoInitializeEx;
    os.CoInitializeSecurity = &FakeCoInitializeSecurity;

    // S_OK, S_FALSE (already initialised, same mode) and the failure codes (RPC_E_CHANGED_MODE, E_FAIL,
    // the extremes): only the sign counts.
    static const HRESULT kResults[] = {S_OK, S_FALSE, 2, 0x7fffffff, RPC_E_CHANGED_MODE, E_FAIL, static_cast<HRESULT>(0x80000000), -1};
    const std::string url = "placeholder-url-for-tests";
    const std::wstring handler = L"C:\\game\\handler.exe", databasePath = L"C:\\tmp\\TS2Legacy";
    for (HRESULT co : kResults) {
        for (HRESULT sec : kResults) {
            gCoInitializeResult = co;
            gSecurityResult = sec;
            // original, stopped at its first dropped effect after the COM calls
            gLog.clear();
            try {
                ctx.Original<bool(__cdecl *)(const std::string &, const std::wstring &, const std::wstring &)>(kStartCrashReporter)(url, handler, databasePath);
                ctx.Fail("original returned instead of reaching a dropped effect (co %x sec %x)", co, sec);
            } catch (const Stop &) {
            }
            std::string originalKept = Joined(false), originalDropped = Joined(true);
            // ours
            gLog.clear();
            bool result = winmaininfra::StartCrashReporter(url, handler, databasePath);
            std::string oursKept = Joined(false), oursDropped = Joined(true);

            char label[96];
            std::snprintf(label, sizeof(label), "CoInitializeEx %08x, CoInitializeSecurity %08x", static_cast<unsigned>(co), static_cast<unsigned>(sec));
            ExpectSame(ctx, label, originalKept, oursKept);
            ExpectSame(ctx, "ours does nothing the contract drops", "", oursDropped);
            // What the original did that we drop: the version annotation first, then (COM both
            // fine) the WMI query or (otherwise) the crash database; nothing else is reachable in this run.
            std::string expected = "DROPPED GetModuleFileNameA (version annotation)\nDROPPED GetFileVersionInfoSizeA (version annotation)\n";
            expected += (co >= 0 && sec >= 0) ? "DROPPED CoCreateInstance (WMI locator)\n" : "DROPPED CrashReportDatabase::Initialize\n";
            ExpectSame(ctx, "the dropped effects of the original", expected, originalDropped);
            ++ctx.cases;
            if (result) ctx.Fail("StartCrashReporter returned true: no handler is ever started");
        }
    }
    // The exact kept sequence once: both calls, arguments as the original passes them.
    gCoInitializeResult = gSecurityResult = S_OK;
    gLog.clear();
    winmaininfra::StartCrashReporter(url, handler, databasePath);
    ExpectSame(ctx, "kept calls and arguments",
               "CoInitializeEx(00000000, 0)\n"
               "CoInitializeSecurity(00000000, -1, 00000000, 00000000, 0, 3, 00000000, 0, 00000000)\n",
               Joined(false));
    os = saved;
}

namespace {

// ---- launch analytics ----

bool gOpenFails = true;
uint32_t gSteamLow = 0, gSteamHigh = 0;

void *WINAPI FakeInternetOpenA(const char *, DWORD, const char *, const char *, DWORD)
{
    Log("DROPPED InternetOpenA");
    return gOpenFails ? nullptr : reinterpret_cast<void *>(0x1001);
}
void *WINAPI FakeInternetConnectA(void *, const char *, WORD, const char *, const char *, DWORD, DWORD, DWORD_PTR)
{
    Log("DROPPED InternetConnectA");
    return reinterpret_cast<void *>(0x1002);
}
void *WINAPI FakeHttpOpenRequestA(void *, const char *, const char *, const char *, const char *, const char **, DWORD, DWORD_PTR)
{
    Log("DROPPED HttpOpenRequestA");
    return reinterpret_cast<void *>(0x1003);
}
BOOL WINAPI FakeHttpAddRequestHeadersA(void *, const char *, DWORD, DWORD)
{
    Log("DROPPED HttpAddRequestHeadersA");
    return TRUE;
}
bool gSendFails = false;
BOOL WINAPI FakeHttpSendRequestExA(void *, void *, void *, DWORD, DWORD_PTR)
{
    Log("DROPPED HttpSendRequestExA");
    return gSendFails ? FALSE : TRUE;
}
BOOL WINAPI FakeInternetWriteFile(void *, const void *, DWORD length, DWORD *written)
{
    Log("DROPPED InternetWriteFile");
    *written = length;
    return TRUE;
}
BOOL WINAPI FakeHttpEndRequestA(void *, void *, DWORD, DWORD_PTR)
{
    Log("DROPPED HttpEndRequestA");
    return TRUE;
}
BOOL WINAPI FakeInternetReadFile(void *, void *, DWORD, DWORD *read)
{
    Log("DROPPED InternetReadFile");
    *read = 0;
    return TRUE;
}
bool gQueryWorks = false;
BOOL WINAPI FakeHttpQueryInfoA(void *, DWORD, void *buffer, DWORD *length, DWORD *)
{
    Log("DROPPED HttpQueryInfoA");
    if (!gQueryWorks) return FALSE;
    std::memcpy(buffer, "200", 4);
    *length = 3;
    return TRUE;
}
BOOL WINAPI FakeInternetCloseHandle(void *)
{
    Log("DROPPED InternetCloseHandle");
    return TRUE;
}
void WINAPI FakeSystemTime(FILETIME *time)
{
    Log("DROPPED GetSystemTimeAsFileTime (event time stamp)");
    time->dwLowDateTime = 0xd53e8000;
    time->dwHighDateTime = 0x019db1de; // some fixed day in 2020
}

uint32_t Checksum(difftest::Context &ctx, uint32_t from, uint32_t to)
{
    uint32_t sum = 2166136261u;
    for (uint32_t rva = from; rva < to; ++rva) sum = (sum ^ *ctx.image.At<uint8_t>(rva)) * 16777619u;
    return sum;
}

} // namespace

DIFF_TEST(WinMainInfra_SendAnalyticsEvent, kSendAnalytics)
{
    Imports imports(ctx);
    imports.Set(kIatInternetOpenA, reinterpret_cast<void *>(&FakeInternetOpenA));
    imports.Set(kIatInternetConnectA, reinterpret_cast<void *>(&FakeInternetConnectA));
    imports.Set(kIatHttpOpenRequestA, reinterpret_cast<void *>(&FakeHttpOpenRequestA));
    imports.Set(kIatHttpAddRequestHeadersA, reinterpret_cast<void *>(&FakeHttpAddRequestHeadersA));
    imports.Set(kIatHttpSendRequestExA, reinterpret_cast<void *>(&FakeHttpSendRequestExA));
    imports.Set(kIatInternetWriteFile, reinterpret_cast<void *>(&FakeInternetWriteFile));
    imports.Set(kIatHttpEndRequestA, reinterpret_cast<void *>(&FakeHttpEndRequestA));
    imports.Set(kIatInternetReadFile, reinterpret_cast<void *>(&FakeInternetReadFile));
    imports.Set(kIatHttpQueryInfoA, reinterpret_cast<void *>(&FakeHttpQueryInfoA));
    imports.Set(kIatInternetCloseHandle, reinterpret_cast<void *>(&FakeInternetCloseHandle));
    imports.Set(kIatGetSystemTimeAsFileTime, reinterpret_cast<void *>(&FakeSystemTime));

    // The path the host's analytics filter forces (src/hook/analytics.cpp: InternetOpenA fails):
    // the original stops after that one call and leaves every byte of the buffer and the statics as
    // it was. Ours does nothing, which leaves them as they were too.
    static const uint32_t kIds[][2] = {{0, 0}, {1, 0}, {0x01234567, 0x01100001}, {0xffffffffu, 0xffffffffu}, {0x7fffffffu, 0x80000000u}};
    for (const auto &id : kIds) {
        gOpenFails = true;
        uint32_t before = Checksum(ctx, kAnalyticsBuffer, kAnalyticsEnd);
        gLog.clear();
        ctx.Original<void(__cdecl *)(uint32_t, uint32_t)>(kSendAnalytics)(id[0], id[1]);
        std::string originalLog = Joined(true) + Joined(false);
        uint32_t afterOriginal = Checksum(ctx, kAnalyticsBuffer, kAnalyticsEnd);
        gLog.clear();
        winmaininfra::SendAnalyticsEvent(id[0], id[1]);
        std::string oursLog = Joined(true) + Joined(false);
        uint32_t afterOurs = Checksum(ctx, kAnalyticsBuffer, kAnalyticsEnd);
        ExpectSame(ctx, "blocked path: the original's only call", "DROPPED InternetOpenA\n", originalLog);
        ExpectSame(ctx, "blocked path: ours makes no call", "", oursLog);
        ++ctx.cases;
        if (before != afterOriginal || before != afterOurs) ctx.Fail("blocked path: event buffer or statics changed (%08x -> %08x original, %08x ours)", before, afterOriginal, afterOurs);
    }
    // The ordinary path with every WinINet entry faked: the original sends the event. What it leaves
    // behind in the image is the zeroed buffer (wiped after the send) and the write-only statics;
    // ours leaves the buffer as it found it (zeros) and never writes the statics. Nothing reads
    // either (docs/contracts/analytics.md).
    gOpenFails = false;
    uint8_t savedStatics[kAnalyticsEnd - kAnalyticsStatics];
    std::memcpy(savedStatics, ctx.image.At<uint8_t>(kAnalyticsStatics), sizeof(savedStatics));
    uint32_t zeroSum = 2166136261u;
    for (uint32_t i = kAnalyticsBuffer; i < kAnalyticsStatics; ++i) zeroSum = zeroSum * 16777619u;
    // Three ways a send ends: the status query fails (nothing cached), the status query works (the
    // first status byte is cached), the send itself fails (the error code is cached).
    for (int mode = 0; mode < 3; ++mode) {
        gQueryWorks = mode == 1;
        gSendFails = mode == 2;
        std::memcpy(ctx.image.At<uint8_t>(kAnalyticsStatics), savedStatics, sizeof(savedStatics));
        gLog.clear();
        ctx.Original<void(__cdecl *)(uint32_t, uint32_t)>(kSendAnalytics)(0x01234567, 0x01100001);
        std::string sent = Joined(true);
        ++ctx.cases;
        if (sent.find("DROPPED HttpSendRequestExA") == std::string::npos || (mode != 2 && sent.find("DROPPED InternetWriteFile") == std::string::npos))
            ctx.Fail("sending path (mode %d) did not behave as expected: %s", mode, sent.c_str());
        ++ctx.cases;
        if (Checksum(ctx, kAnalyticsBuffer, kAnalyticsStatics) != zeroSum) ctx.Fail("after a send the event buffer is not all zeros");
        // The statics the original writes: nothing reads them, ours leaves them alone.
        ++ctx.cases;
        bool changed = std::memcmp(ctx.image.At<uint8_t>(kAnalyticsStatics), savedStatics, sizeof(savedStatics)) != 0;
        if (changed != (mode != 0)) ctx.Fail("the original's write-only statics: changed %d in mode %d", changed, mode);
        std::memcpy(ctx.image.At<uint8_t>(kAnalyticsStatics), savedStatics, sizeof(savedStatics));
        gLog.clear();
        winmaininfra::SendAnalyticsEvent(0x01234567, 0x01100001);
        ExpectSame(ctx, "ours sends nothing", "", Joined(true) + Joined(false));
        ++ctx.cases;
        if (std::memcmp(ctx.image.At<uint8_t>(kAnalyticsStatics), savedStatics, sizeof(savedStatics)) != 0) ctx.Fail("ours wrote the statics");
    }
    std::memcpy(ctx.image.At<uint8_t>(kAnalyticsStatics), savedStatics, sizeof(savedStatics));
}
