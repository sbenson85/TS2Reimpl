// Differential test for WinMain (0xc2b87c) against winmaininfra::WinMainBody.
//
// WinMain's own work is compared: what it builds for the crash reporter, the Steam check and its
// failure path, the Steam id it hands to analytics, the order of the start-up calls, the command line
// it builds and the switch it tests, the two function-local statics (framework, application) with
// their guards and destructor registrations, and the final calls (RunApp, SteamAPI_Shutdown).
//
// The pieces that cannot run in the harness, or that are replaced by contract, are fixtures on both
// sides, logging their arguments:
//   * the crash reporter start (0xc2b206), analytics (0xc2df67) and the handler installer (0xcac1f0):
//     recorders (the originals' first bytes are redirected; ours goes through WinMainSeams);
//   * the framework constructor (0xc2ccdb), the application constructor (0xc26147) and RunApp
//     (0xc2d23b): recorders, because each is verified separately and heavy to run;
//   * the CRT's atexit (0x824cbe) and exit (0x82f6bf; exit, which runs the atexit list, not _exit): the
//     game CRT's exit machinery is not initialised in the harness. This is the exact place where WinMain
//     depends on the game CRT; the host binds both seams to the image's under [Startup] OwnStart=2
//     (docs/subsystems/startup.md, E1).
// The operating system and Steam are faked in the import table; ours reads the same slots.
#define NOMINMAX
#include "app/WinMain.h"
#include "app/WinMainInfra.h"
#include "difftest.h"
#include "game/game.h"
#include "rz/cRZCmdLine.h"
#include "rz/cRZString.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kWinMain = 0xc2b87c;
constexpr uint32_t kCrashStart = 0xc2b206, kAnalytics = 0xc2df67, kHandlers = 0xcac1f0, kFrameworkCtor = 0xc2ccdb, kAppCtor = 0xc26147, kRunApp = 0xc2d23b;
constexpr uint32_t kAtExit = 0x824cbe, kExit = 0x82f6bf;
constexpr uint32_t kIatGetModuleFileNameW = 0xd9a328, kIatGetTempPathW = 0xd9a330, kIatGetCommandLineW = 0xd9a398, kIatMessageBoxA = 0xd9a568;
constexpr uint32_t kIatSteamInit = 0xd9a70c, kIatContextInit = 0xd9a700, kIatSteamShutdown = 0xd9a708;
constexpr uint32_t kIatGetModuleHandleA = 0xd9a114, kIatLoadLibraryA = 0xd9a128, kIatGetProcAddress = 0xd9a124;
constexpr uint32_t kMemoryObject = 0x10fda2c, kFrameworkStorage = 0x10fdb50, kFrameworkGuard = 0x10fdce8, kAppStorage = 0x10fdcf0, kAppGuard = 0x10fdf58;
constexpr uint32_t kTlsIndex = 0x10df814, kUserContext = 0x1081a7c;
constexpr uint32_t kAtExitFramework = 0xd988fa, kAtExitApp = 0xd988f0;

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

std::string Joined()
{
    std::string out;
    for (const std::string &s : gLog) out += s + "\n";
    return out;
}

void ExpectSame(difftest::Context &ctx, const char *what, const std::string &a, const std::string &b)
{
    ++ctx.cases;
    if (a != b) {
        size_t at = 0;
        while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
        size_t from = at > 80 ? at - 80 : 0;
        std::string x = a.substr(from, 150), y = b.substr(from, 150);
        for (char &c : x) if (c == '\n') c = '|';
        for (char &c : y) if (c == '\n') c = '|';
        ctx.Fail("%s:\n  original: %s\n  ours:     %s", what, x.c_str(), y.c_str());
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

// ---- the script ----

struct Script {
    std::wstring modulePath;     // GetModuleFileNameW
    DWORD moduleLength;          // what it returns (0: fails)
    std::wstring tempPath;       // GetTempPathW
    std::wstring commandLine;    // GetCommandLineW
    int32_t steamResult;         // SteamInternal_SteamAPI_Init
    uint32_t steamLow, steamHigh;
    int32_t runAppResult;
    uint8_t memoryByte;          // the byte at +0x10 of the memory object before the run
    bool frameworkDone, appDone; // the statics' guards before the run
};
Script gScript;

uint32_t gBase = 0;
uint32_t Rva(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - gBase); }

std::string Narrow(const std::wstring &w)
{
    std::string out;
    for (wchar_t c : w) {
        char t[8];
        if (c >= 0x20 && c < 0x7f) out += static_cast<char>(c);
        else { std::snprintf(t, sizeof(t), "\\x%x", c); out += t; }
    }
    return out;
}

// ---- operating-system fakes ----

DWORD WINAPI FakeGetModuleFileNameW(HMODULE module, wchar_t *buffer, DWORD size)
{
    Log("GetModuleFileNameW(%p, %u)", static_cast<void *>(module), size);
    size_t n = gScript.modulePath.size() < size ? gScript.modulePath.size() : size;
    std::memcpy(buffer, gScript.modulePath.data(), n * sizeof(wchar_t));
    return gScript.moduleLength;
}
DWORD WINAPI FakeGetTempPathW(DWORD size, wchar_t *buffer)
{
    Log("GetTempPathW(%u)", size);
    std::memcpy(buffer, gScript.tempPath.c_str(), (gScript.tempPath.size() + 1) * sizeof(wchar_t));
    return static_cast<DWORD>(gScript.tempPath.size());
}
const wchar_t *WINAPI FakeGetCommandLineW()
{
    Log("GetCommandLineW()");
    return gScript.commandLine.c_str();
}
int WINAPI FakeMessageBoxA(HWND hwnd, const char *text, const char *caption, UINT flags)
{
    Log("MessageBoxA(%p, %s, %s, %x)", static_cast<void *>(hwnd), text, caption, flags);
    return 1;
}
int __cdecl FakeSteamInit(const char *versions, void *errorMessage)
{
    Log("SteamInternal_SteamAPI_Init(%s, %p)", versions, errorMessage);
    return gScript.steamResult;
}
void __fastcall FakeGetSteamID(void *self, void *, uint32_t *out)
{
    Log("ISteamUser::GetSteamID()");
    out[0] = gScript.steamLow;
    out[1] = gScript.steamHigh;
}
const void *const kUserVTable[3] = {nullptr, nullptr, reinterpret_cast<const void *>(&FakeGetSteamID)};
struct FakeUser {
    const void *const *vtable = kUserVTable;
} gUser;
void *gUserHolder = &gUser;
void **__cdecl FakeContextInit(void *context)
{
    Log("SteamInternal_ContextInit(rva %x)", Rva(context));
    return &gUserHolder;
}
void __cdecl FakeSteamShutdown()
{
    Log("SteamAPI_Shutdown()");
}
HMODULE WINAPI FakeGetModuleHandleA(const char *name)
{
    Log("GetModuleHandleA(%s)", name);
    return nullptr;
}
HMODULE WINAPI FakeLoadLibraryA(const char *name)
{
    Log("LoadLibraryA(%s)", name);
    return nullptr;
}
void *WINAPI FakeGetProcAddress(HMODULE, const char *name)
{
    Log("GetProcAddress(%s)", name);
    return nullptr;
}

// ---- recorders for the callees that are fixtures on both sides ----

std::string Hash(const std::string &s)
{
    uint32_t h = 2166136261u;
    for (unsigned char c : s) h = (h ^ c) * 16777619u;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "len %zu hash %08x", s.size(), h);
    return buf;
}

bool __cdecl RecStartCrash(const std::string &url, const std::wstring &handler, const std::wstring &database)
{
    // The upload address is logged by length and hash only.
    Log("StartCrashReporter(url %s, handler %s, database %s)", Hash(url).c_str(), Narrow(handler).c_str(), Narrow(database).c_str());
    return false;
}
void __cdecl RecAnalytics(uint32_t low, uint32_t high)
{
    Log("SendAnalyticsEvent(%08x, %08x)", low, high);
}
void __cdecl RecHandlers()
{
    Log("InstallTerminateAndPurecallHandlers()");
}

std::string DescribeCommandLine(void *commandLine)
{
    auto *cmd = static_cast<cRZCmdLine *>(commandLine);
    std::string out = "argc " + std::to_string(cmd->ArgC());
    for (int i = 0; i < cmd->ArgC(); ++i) out += std::string(" [") + cmd->ArgV(i) + "]";
    return out;
}
std::string Where(const void *p) // image storage by name, anything else by "other"
{
    uint32_t rva = Rva(p);
    if (rva == kFrameworkStorage) return "framework storage";
    if (rva == kAppStorage) return "app storage";
    return "other";
}

void RecConstructFramework(void *storage)
{
    Log("ConstructFramework(%s)", Where(storage).c_str());
}
void *__fastcall RecConstructFrameworkThiscall(void *self, void *)
{
    RecConstructFramework(self);
    return self;
}
void RecConstructApp(void *storage, uint32_t id, const char *name, void *commandLine)
{
    Log("ConstructApp(%s, id %08x, name %s, %s)", Where(storage).c_str(), id, name, DescribeCommandLine(commandLine).c_str());
}
void *__fastcall RecConstructAppThiscall(void *self, void *, uint32_t id, const char *name, void *commandLine)
{
    RecConstructApp(self, id, name, commandLine);
    return self;
}
int32_t __cdecl RecRunApp(void *framework, void *app, void *commandLine)
{
    Log("RunApp(%s, %s, %s)", Where(framework).c_str(), Where(app).c_str(), DescribeCommandLine(commandLine).c_str());
    return gScript.runAppResult;
}
int __cdecl RecAtExit(void(__cdecl *function)())
{
    // The original registers the image's thunks to the destructor bodies, ours registers its own
    // functions that call the verified destructors: both are named by what they destroy.
    const char *what = "other";
    uint32_t rva = Rva(reinterpret_cast<const void *>(function));
    if (rva == kAtExitFramework || function == &winmaininfra::DestroyFramework) what = "framework destructor";
    else if (rva == kAtExitApp || function == &winmaininfra::DestroyApp) what = "app destructor";
    Log("atexit(%s)", what);
    return 0;
}
void __cdecl RecExit(int code)
{
    Log("exit(%d)", code);
    throw Stop{};
}

// ---- running one scenario ----

std::string StateAfter(difftest::Context &ctx)
{
    auto guard = [&](uint32_t rva) {
        int32_t g = *ctx.image.At<int32_t>(rva);
        return g == 0 ? "0" : g == -1 ? "claimed" : "done"; // the original leaves the CRT's epoch number, ours 1
    };
    char buf[160];
    std::snprintf(buf, sizeof(buf), "guards %s/%s memory byte %02x", guard(kFrameworkGuard), guard(kAppGuard), *ctx.image.At<uint8_t>(kMemoryObject + 0x10));
    return buf;
}

std::string RunScenario(difftest::Context &ctx, int side)
{
    *ctx.image.At<int32_t>(kFrameworkGuard) = gScript.frameworkDone ? 7 : 0;
    *ctx.image.At<int32_t>(kAppGuard) = gScript.appDone ? 7 : 0;
    *ctx.image.At<uint8_t>(kMemoryObject + 0x10) = gScript.memoryByte;
    // The original's inline guard test compares with the thread's epoch (TLS block of the image's
    // index, offset 4); a fresh thread has the minimum there, so that a guard of 0 means "not built".
    auto **array = reinterpret_cast<int **>(__readfsdword(0x2c));
    int *block = array[*ctx.image.At<uint32_t>(kTlsIndex)];
    block[1] = static_cast<int>(0x80000000);
    gLog.clear();
    int result = -12345;
    bool stopped = false;
    try {
        if (side == 0) result = ctx.Original<int(__stdcall *)(void *, void *, char *, int)>(kWinMain)(nullptr, nullptr, nullptr, 0);
        else result = winmaininfra::WinMainBody(nullptr, nullptr, nullptr, 0);
    } catch (const Stop &) {
        stopped = true;
    }
    char tail[96];
    std::snprintf(tail, sizeof(tail), "result %d%s", result, stopped ? " (stopped by exit)" : "");
    return Joined() + tail + "\n" + StateAfter(ctx);
}

} // namespace

DIFF_TEST(WinMainInfra_WinMain, kWinMain)
{
    gBase = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx.image.At<void>(0)));
    Imports imports(ctx);
    imports.Set(kIatGetModuleFileNameW, reinterpret_cast<void *>(&FakeGetModuleFileNameW));
    imports.Set(kIatGetTempPathW, reinterpret_cast<void *>(&FakeGetTempPathW));
    imports.Set(kIatGetCommandLineW, reinterpret_cast<void *>(&FakeGetCommandLineW));
    imports.Set(kIatMessageBoxA, reinterpret_cast<void *>(&FakeMessageBoxA));
    imports.Set(kIatSteamInit, reinterpret_cast<void *>(&FakeSteamInit));
    imports.Set(kIatContextInit, reinterpret_cast<void *>(&FakeContextInit));
    imports.Set(kIatSteamShutdown, reinterpret_cast<void *>(&FakeSteamShutdown));
    imports.Set(kIatGetModuleHandleA, reinterpret_cast<void *>(&FakeGetModuleHandleA));
    imports.Set(kIatLoadLibraryA, reinterpret_cast<void *>(&FakeLoadLibraryA));
    imports.Set(kIatGetProcAddress, reinterpret_cast<void *>(&FakeGetProcAddress));
    CodeRedirect r1(ctx, kCrashStart, reinterpret_cast<void *>(&RecStartCrash));
    CodeRedirect r2(ctx, kAnalytics, reinterpret_cast<void *>(&RecAnalytics));
    CodeRedirect r3(ctx, kHandlers, reinterpret_cast<void *>(&RecHandlers));
    CodeRedirect r4(ctx, kFrameworkCtor, reinterpret_cast<void *>(&RecConstructFrameworkThiscall));
    CodeRedirect r5(ctx, kAppCtor, reinterpret_cast<void *>(&RecConstructAppThiscall));
    CodeRedirect r6(ctx, kRunApp, reinterpret_cast<void *>(&RecRunApp));
    CodeRedirect r7(ctx, kAtExit, reinterpret_cast<void *>(&RecAtExit));
    CodeRedirect r8(ctx, kExit, reinterpret_cast<void *>(&RecExit));

    winmaininfra::WinMainSeams &seams = winmaininfra::Seams();
    const winmaininfra::WinMainSeams savedSeams = seams;
    seams.startCrashReporter = &RecStartCrash;
    seams.sendAnalytics = &RecAnalytics;
    seams.installHandlers = &RecHandlers;
    seams.constructFramework = &RecConstructFramework;
    seams.constructApp = &RecConstructApp;
    seams.runApp = &RecRunApp;
    seams.registerAtExit = &RecAtExit;
    seams.exit = &RecExit;

    uint32_t savedGuards[2] = {*ctx.image.At<uint32_t>(kFrameworkGuard), *ctx.image.At<uint32_t>(kAppGuard)};
    uint8_t savedMemoryByte = *ctx.image.At<uint8_t>(kMemoryObject + 0x10);
    // The application name is a cRZString static the harness has not built: give it a text.
    uint8_t savedName[sizeof(cRZString)];
    std::memcpy(savedName, ctx.image.At<uint8_t>(0x10fda54), sizeof(savedName));
    std::memset(ctx.image.At<uint8_t>(0x10fda54), 0, sizeof(savedName));
    ctx.image.At<cRZString>(0x10fda54)->Construct("The Sims 2 test name");

    static const wchar_t *const kCommandLines[] = {L"", L"Sims2EP9.exe -w", L"Sims2EP9.exe -memoryReport", L"\"C:\\Games\\The Sims 2\\Sims2EP9.exe\" /memoryReport -lang:en",
                                                  L"x -MEMORYREPORT:1 -memoryReportExtra", L"\u00e9 \u00fc \u4e2d -x \"a b\""};
    static const wchar_t *const kModulePaths[] = {L"C:\\Games\\The Sims 2\\Sims2EP9.exe", L"Sims2EP9.exe", L"C:\\a/b\\c.exe", L"/x"};
    static const wchar_t *const kTempPaths[] = {L"C:\\Users\\player\\AppData\\Local\\Temp\\", L"D:\\t\\", L""};
    std::mt19937 rng(0xc2b87c);
    for (int i = 0; i < 160; ++i) {
        Script &s = gScript;
        s.modulePath = kModulePaths[rng() % 4];
        s.moduleLength = (i % 7 == 3) ? 0 : static_cast<DWORD>(s.modulePath.size());
        s.tempPath = kTempPaths[rng() % 3];
        s.commandLine = kCommandLines[rng() % 6];
        s.steamResult = (i % 5 == 0) ? static_cast<int32_t>(rng() % 3) + 1 : 0; // 0 = Steam fine
        s.steamLow = static_cast<uint32_t>(rng());
        s.steamHigh = (rng() % 2) ? 0x01100001u : static_cast<uint32_t>(rng());
        s.runAppResult = static_cast<int32_t>(rng() % 5) - 1;
        s.memoryByte = static_cast<uint8_t>(rng());
        s.frameworkDone = (i % 11) == 4;
        s.appDone = (i % 13) == 5;
        std::string results[2];
        for (int side = 0; side < 2; ++side) results[side] = RunScenario(ctx, side);
        char label[96];
        std::snprintf(label, sizeof(label), "scenario %d (steam %d, cmd %s)", i, s.steamResult, Narrow(s.commandLine).c_str());
        ExpectSame(ctx, label, results[0], results[1]);
    }

    *ctx.image.At<uint32_t>(kFrameworkGuard) = savedGuards[0];
    *ctx.image.At<uint32_t>(kAppGuard) = savedGuards[1];
    *ctx.image.At<uint8_t>(kMemoryObject + 0x10) = savedMemoryByte;
    std::memcpy(ctx.image.At<uint8_t>(0x10fda54), savedName, sizeof(savedName));
    seams = savedSeams;
}
