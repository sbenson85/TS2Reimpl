// ts2host.exe: runs the game image in our own process instead of the original exe (docs/roadmap.md,
// stage 2). It maps the unpacked image with the difftest loader (harness/image.cpp), resolves its
// imports for real, gives it implicit TLS on every thread through ts2tls.dll, and calls its entry
// point. From here our replacements can be applied before the game's startup runs, and the image
// can shrink as we take subsystems over.
//
// Lives in the game's TSBin folder (it reads paths relative to the exe). The image defaults to
// Sims2EP9.unpacked.exe next to the host; TS2HOST_IMAGE overrides it. The game's command line is
// passed through unchanged. Log: ts2host.log next to the host.
#include "control.h"
#include "image.h"
#include "instance.h"
#include "app/WinMainInfra.h"
#include "app/StaticInit.h"
#include "app/WinMainStart.h"
#include "game/game.h"
#include "game/espcheck.h"
#include "hook/analytics.h"
#include "hook/coverage.h"
#include "hook/faultlog.h"
#include "hook/hooks.h"
#include "hook/log.h"
#include "hook/quiet.h"
#include "hook/traps.h"

#include <windows.h>
#include <winternl.h>
#include <intrin.h>

#include <corecrt_startup.h> // _initterm_e: our toolchain's, run over the image's XI table (OwnStart)

#include <share.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

extern "C" {
__declspec(dllimport) ULONG Ts2TlsIndex(void);
__declspec(dllimport) unsigned char *Ts2TlsGameData(void);
__declspec(dllimport) ULONG Ts2TlsSize(void);
}

namespace {

std::wstring gDir;          // the host's folder (TSBin), with a trailing backslash
std::wstring gOriginalExe;  // where the original exe lives: what the game sees as its own path
HMODULE gImage = nullptr;   // the mapped image, as the game's HMODULE
uintptr_t gImageEnd = 0;
FILE *gLog = nullptr;

} // namespace

// Also the log for src/hook/quiet.cpp (hook/log.h).
void Log(const char *fmt, ...)
{
    if (!gLog) return;
    static std::mutex lock;
    std::lock_guard<std::mutex> guard(lock);
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(gLog, "%02u:%02u:%02u.%03u ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    vfprintf(gLog, fmt, args);
    va_end(args);
    fputc('\n', gLog);
    fflush(gLog);
}

namespace {

bool InImage(const void *p)
{
    auto a = reinterpret_cast<uintptr_t>(p);
    return a >= reinterpret_cast<uintptr_t>(gImage) && a < gImageEnd;
}

// The game asks Windows about "its own module" with a null name; that has to be the image, not us.
HMODULE WINAPI HostGetModuleHandleA(LPCSTR name) { return name ? GetModuleHandleA(name) : gImage; }
HMODULE WINAPI HostGetModuleHandleW(LPCWSTR name) { return name ? GetModuleHandleW(name) : gImage; }

BOOL WINAPI HostGetModuleHandleExW(DWORD flags, LPCWSTR name, HMODULE *module)
{
    bool fromAddress = (flags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) != 0;
    if ((!fromAddress && !name) || (fromAddress && InImage(name))) {
        *module = gImage;
        return TRUE;
    }
    return GetModuleHandleExW(flags, name, module);
}

DWORD WINAPI HostGetModuleFileNameW(HMODULE module, LPWSTR buffer, DWORD size)
{
    if (module && module != gImage) return GetModuleFileNameW(module, buffer, size);
    if (size == 0) return 0;
    DWORD n = static_cast<DWORD>(gOriginalExe.size());
    DWORD copy = n < size ? n : size - 1;
    std::memcpy(buffer, gOriginalExe.c_str(), copy * sizeof(wchar_t));
    buffer[copy] = 0;
    if (copy < n) SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return copy;
}

DWORD WINAPI HostGetModuleFileNameA(HMODULE module, LPSTR buffer, DWORD size)
{
    if (module && module != gImage) return GetModuleFileNameA(module, buffer, size);
    if (size == 0) return 0;
    std::string path(MAX_PATH * 2, '\0');
    int n = WideCharToMultiByte(CP_ACP, 0, gOriginalExe.c_str(), -1, path.data(), static_cast<int>(path.size()), nullptr, nullptr);
    DWORD len = n > 0 ? static_cast<DWORD>(n - 1) : 0;
    DWORD copy = len < size ? len : size - 1;
    std::memcpy(buffer, path.data(), copy);
    buffer[copy] = 0;
    if (copy < len) SetLastError(ERROR_INSUFFICIENT_BUFFER);
    return copy;
}

void *Override(const char *dll, const char *fn)
{
    if (_stricmp(dll, "KERNEL32.dll") != 0 || reinterpret_cast<uintptr_t>(fn) < 0x10000) return nullptr;
    struct { const char *name; void *fn; } const table[] = {
        {"GetModuleHandleA", reinterpret_cast<void *>(&HostGetModuleHandleA)},
        {"GetModuleHandleW", reinterpret_cast<void *>(&HostGetModuleHandleW)},
        {"GetModuleHandleExW", reinterpret_cast<void *>(&HostGetModuleHandleExW)},
        {"GetModuleFileNameA", reinterpret_cast<void *>(&HostGetModuleFileNameA)},
        {"GetModuleFileNameW", reinterpret_cast<void *>(&HostGetModuleFileNameW)},
    };
    for (const auto &e : table)
        if (std::strcmp(fn, e.name) == 0) return e.fn;
    return nullptr;
}

// System DLLs come from System32 only, so DLLs in TSBin that proxy them (the ASI loader installed
// as wininet.dll) never load into the host. The rest (steam_api.dll) come from TSBin.
void *ResolveImport(const char *dll, const char *fn)
{
    if (void *o = InstanceImport(dll, fn)) return o; // instance mode, the Steam stub (host/instance.h)
    if (void *o = Override(dll, fn)) return o;
    if (void *o = DeviceLogImport(dll, fn)) return o; // TS2HOST_DEVLOG: the logging Direct3DCreate9 (host/control_devlog.cpp)
    std::wstring name(dll, dll + std::strlen(dll));
    HMODULE m = nullptr;
    // TS2HOST_D3D9_DLL=<path>: d3d9.dll from that path instead of System32, for a call tracer such as apitrace's wrapper
    // (docs/subsystems/rendering.md). Off unless set; the wrapper itself loads the real one from System32.
    wchar_t d3d9Path[MAX_PATH];
    if (_stricmp(dll, "d3d9.dll") == 0 && GetEnvironmentVariableW(L"TS2HOST_D3D9_DLL", d3d9Path, MAX_PATH) - 1u < MAX_PATH - 1u) {
        m = LoadLibraryW(d3d9Path);
        Log("d3d9.dll from TS2HOST_D3D9_DLL: %s", m ? "loaded" : "failed, using System32's");
    }
    if (!m) m = LoadLibraryExW(name.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!m) m = LoadLibraryW((gDir + name).c_str());
    if (!m) {
        Log("cannot load %s", dll);
        return nullptr;
    }
    void *p = reinterpret_cast<void *>(GetProcAddress(m, fn));
    if (p && _stricmp(dll, "WININET.dll") == 0 && reinterpret_cast<uintptr_t>(fn) >= 0x10000 && std::strcmp(fn, "InternetOpenA") == 0)
        return AnalyticsFilterFor(p); // blocks the launch analytics event (hook/analytics.cpp)
    if (!p) {
        if (reinterpret_cast<uintptr_t>(fn) < 0x10000) Log("missing import %s!#%u", dll, static_cast<unsigned>(reinterpret_cast<uintptr_t>(fn)));
        else Log("missing import %s!%s", dll, fn);
    }
    return p;
}

std::string WideToAnsi(const std::wstring &s)
{
    std::string out(s.size() * 2 + 1, '\0');
    int n = WideCharToMultiByte(CP_ACP, 0, s.c_str(), -1, out.data(), static_cast<int>(out.size()), nullptr, nullptr);
    out.resize(n > 0 ? n - 1 : 0);
    return out;
}

// Logs the hooks' call counts every 5 s when they change, so scenarios can
// confirm the replacements run.
DWORD WINAPI HookCountReporter(LPVOID)
{
    for (;;) {
        Sleep(5000);
        LogHookCountsIfChanged();
    }
}

// Diagnostic: logs each DLL the loader maps from now on, and whether it has implicit TLS.
struct DllNotification {
    ULONG flags;
    const UNICODE_STRING *fullName;
    const UNICODE_STRING *baseName;
    void *base;
    ULONG size;
};
void CALLBACK OnDllNotification(ULONG reason, const DllNotification *data, void *)
{
    if (reason != 1) return; // loaded
    auto *dos = static_cast<const IMAGE_DOS_HEADER *>(data->base);
    auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(static_cast<const uint8_t *>(data->base) + dos->e_lfanew);
    bool tls = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].Size != 0;
    Log("dll loaded: %.*ls%s (thread %lu)", data->baseName->Length / 2, data->baseName->Buffer, tls ? " [TLS]" : "", GetCurrentThreadId());
}
void WatchDllLoads()
{
    using Register = LONG(NTAPI *)(ULONG, void(CALLBACK *)(ULONG, const DllNotification *, void *), void *, void **);
    auto reg = reinterpret_cast<Register>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "LdrRegisterDllNotification"));
    static void *cookie;
    if (reg) reg(0, &OnDllNotification, nullptr, &cookie);
}

// Gives the game's TLS a home in ts2tls.dll's block (see host/tls_slot.c): checks that ts2tls's
// game area sits at the offset the game uses, copies the game's TLS data into ts2tls's template
// (new threads) and into this thread's block, and points the game's TLS index variable at ts2tls's.
bool SetUpTls(const OriginalImage &image)
{
    auto *dos = image.At<IMAGE_DOS_HEADER>(0);
    auto *nt = image.At<IMAGE_NT_HEADERS32>(dos->e_lfanew);
    const auto &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
    if (!dir.Size) return true;
    auto *tls = image.At<IMAGE_TLS_DIRECTORY32>(dir.VirtualAddress);
    auto *callbacks = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(tls->AddressOfCallBacks));
    if (callbacks && *callbacks) {
        Log("game has TLS callbacks; not supported yet");
        return false;
    }
    const uint32_t raw = tls->EndAddressOfRawData - tls->StartAddressOfRawData;
    const auto *game = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(tls->StartAddressOfRawData));

    // ts2tls's block: the CRT's _tls_start byte (and padding) at 0..3, our area from offset 4.
    constexpr uint32_t kArea = 4;
    const ULONG index = Ts2TlsIndex();
    auto **slots = reinterpret_cast<uint8_t **>(__readfsdword(0x2c));
    uint8_t *block = slots[index];
    if (Ts2TlsGameData() != block + kArea) {
        Log("ts2tls layout: area at block+%d, expected +%u", static_cast<int>(Ts2TlsGameData() - block), kArea);
        return false;
    }
    // The game's bytes below the area must be zero (its own _tls_start and padding) so nothing is lost.
    for (uint32_t i = 0; i < kArea && i < raw; ++i) {
        if (game[i]) {
            Log("game TLS byte %u is %02x; ts2tls can't hold it", i, game[i]);
            return false;
        }
    }
    if (raw + tls->SizeOfZeroFill > kArea + Ts2TlsSize()) {
        Log("game TLS is %u bytes, ts2tls.dll holds %u", raw + tls->SizeOfZeroFill, kArea + Ts2TlsSize());
        return false;
    }
    auto *m = reinterpret_cast<uint8_t *>(GetModuleHandleW(L"ts2tls.dll"));
    auto *mnt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(m + reinterpret_cast<const IMAGE_DOS_HEADER *>(m)->e_lfanew);
    auto *td = reinterpret_cast<const IMAGE_TLS_DIRECTORY32 *>(m + mnt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress);
    auto *tmpl = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(td->StartAddressOfRawData));
    if (std::memcmp(tmpl + kArea, "TS2TLS-SLOT-MARKER", 18) != 0) {
        Log("ts2tls template not found");
        return false;
    }
    DWORD old;
    VirtualProtect(tmpl, kArea + Ts2TlsSize(), PAGE_READWRITE, &old);
    for (uint8_t *dst : {tmpl, block}) {
        std::memset(dst + kArea, 0, Ts2TlsSize());
        if (raw > kArea) std::memcpy(dst + kArea, game + kArea, raw - kArea);
    }
    *reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(tls->AddressOfIndex)) = index;
    Log("game TLS in ts2tls slot %lu", index);
    return true;
}

// ---- [Startup] OwnStart: our start of the image (docs/subsystems/startup.md, E1) ----
//
// 0 (or absent): the image's entry point, as always. 1: our start sequencer (winmainstart::RunImageStart)
// with the game's WinMain. 2: the sequencer with our WinMain (winmaininfra::WinMainBody), whose atexit and
// exit seams are bound to the image CRT's. Every image address the sequencer needs is bound here: these
// are the image-CRT dependencies that go with the image.
namespace ownstart {

// The image CRT (1.18.0.187 RVAs; names from db/library-matches.jsonl, except exit, which the db calls _loaddll).
constexpr uint32_t kSecurityInitCookie = 0x8258a9, kInitializeCrt = 0x824af8, kAcquireStartupLock = 0x824ac6;
constexpr uint32_t kReleaseStartupLock = 0x824c4c, kNativeStartupState = 0x10df7e8;
constexpr uint32_t kXiFirst = 0xd9b3ac, kXiLast = 0xd9b3d8, kXcFirst = 0xd9a724, kXcLast = 0xd9b3a8;
constexpr uint32_t kDynamicTlsInit = 0x110a388, kDynamicTlsDtor = 0x110a384, kIsNonwritable = 0x824bb8, kRegisterTlsDtor = 0x82f699;
constexpr uint32_t kShowWindowMode = 0x825a71, kNarrowCommandLine = 0x8434c1, kIsManagedApp = 0x825aa2;
constexpr uint32_t kCexit = 0x82f674, kUninitializeCrt = 0x824c69, kExit = 0x82f6bf, kExitNow = 0x82f683, kCExitQuick = 0x82f665;
constexpr uint32_t kFastFail = 0x82595c, kSehFilterExe = 0x842b14, kAtExit = 0x824cbe;
constexpr uint32_t kGameWinMain = 0xc2b87c;

uintptr_t gBase = 0;
template <typename T> T At(uint32_t rva) { return reinterpret_cast<T>(gBase + rva); }

using WinMainFn = int(__stdcall *)(void *, void *, char *, int);
WinMainFn gWinMain = nullptr;
int gMode = 0;

// Our toolchain's _initterm_e over the image's XI table (the same library function the image's CRT has), and
// _initterm's loop over the XC table with the owned slots running ours (winmainstart::RunInitialisersOwned).
int __cdecl RunCheckedInitialisers(winmainstart::CheckedInitialiser *first, winmainstart::CheckedInitialiser *last)
{
    int n = 0;
    for (auto *p = first; p != last; ++p) n += *p != nullptr;
    Log("own start: %d XI initialisers", n);
    int result = _initterm_e(reinterpret_cast<_PIFV *>(first), reinterpret_cast<_PIFV *>(last));
    if (result) Log("own start: an XI initialiser failed (%d)", result);
    return result;
}

// [Startup] OwnStatics (default 1): the XC entries our build owns (staticinit::OwnedInitialisers, stage 3) run
// ours in their slots; 0 runs the image's for all of them (to bisect a failure in modes 1/2).
bool gOwnStatics = true;

void __cdecl RunInitialisers(winmainstart::Initialiser *first, winmainstart::Initialiser *last)
{
    int n = 0;
    for (auto *p = first; p != last; ++p) n += *p != nullptr;
    size_t ownedCount = 0;
    const winmainstart::OwnedInitialiser *owned = gOwnStatics ? staticinit::OwnedInitialisers(&ownedCount) : nullptr;
    Log("own start: %d XC initialisers (%u owned by us)", n, static_cast<unsigned>(ownedCount));
    winmainstart::XcRunCounts c = winmainstart::RunInitialisersOwned(first, last, gBase, owned, ownedCount);
    Log("own start: XC initialisers done (%d image, %d ours, %d mismatched, %d unused)", c.image, c.owned,
        c.mismatched, c.unused);
}

int __stdcall LoggedWinMain(void *instance, void *previous, char *commandLine, int show)
{
    Log("own start: calling %s WinMain", gMode == 2 ? "our" : "the game's");
    return gWinMain(instance, previous, commandLine, show);
}

// Mode 2: WinMainBody's seams, bound to the image CRT, with log lines around the two statics.
winmaininfra::WinMainSeams gOurSeams{};

void LoggedConstructFramework(void *storage)
{
    Log("own start: constructing the framework (ours) at %p", storage);
    gOurSeams.constructFramework(storage);
}

void LoggedConstructApp(void *storage, uint32_t serviceId, const char *name, void *commandLine)
{
    Log("own start: constructing the application (ours) at %p", storage);
    gOurSeams.constructApp(storage, serviceId, name, commandLine);
}

void __cdecl LoggedDestroyFramework()
{
    Log("own start: atexit destroys the framework");
    winmaininfra::DestroyFramework();
}

void __cdecl LoggedDestroyApp()
{
    Log("own start: atexit destroys the application");
    winmaininfra::DestroyApp();
}

int __cdecl ImageAtExit(void(__cdecl *function)())
{
    if (function == &winmaininfra::DestroyFramework) function = &LoggedDestroyFramework;
    else if (function == &winmaininfra::DestroyApp) function = &LoggedDestroyApp;
    return At<int(__cdecl *)(void(__cdecl *)())>(kAtExit)(function);
}

void __cdecl ImageExit(int code)
{
    Log("own start: exit(%d) through the image's CRT", code);
    At<void(__cdecl *)(int)>(kExit)(code);
}

int ReadMode(const std::wstring &dir)
{
    gOwnStatics = GetPrivateProfileIntW(L"Startup", L"OwnStatics", 1, (dir + L"ts2reimpl.ini").c_str()) != 0;
    // Default 2 (our start sequencer and WinMain) since milestone E was accepted (2026-10-18); 0 = the image's own start.
    // A reference run (TS2HOST_NOHOOKS) is the original as is: its default is 0, so it never runs our start code.
    wchar_t env[8];
    const int fallback = GetEnvironmentVariableW(L"TS2HOST_NOHOOKS", env, 8) ? 0 : 2;
    return static_cast<int>(GetPrivateProfileIntW(L"Startup", L"OwnStart", fallback, (dir + L"ts2reimpl.ini").c_str()));
}

int Run(uintptr_t base, int mode)
{
    gBase = base;
    gMode = mode;
    winmainstart::ImageCrt crt{};
    crt.initSecurityCookie = At<void(__cdecl *)()>(kSecurityInitCookie);
    crt.initializeCrt = At<bool(__cdecl *)(int)>(kInitializeCrt);
    crt.acquireStartupLock = At<bool(__cdecl *)()>(kAcquireStartupLock);
    crt.releaseStartupLock = At<void(__cdecl *)(bool)>(kReleaseStartupLock);
    crt.nativeStartupState = At<int *>(kNativeStartupState);
    crt.xiFirst = At<winmainstart::CheckedInitialiser *>(kXiFirst);
    crt.xiLast = At<winmainstart::CheckedInitialiser *>(kXiLast);
    crt.xcFirst = At<winmainstart::Initialiser *>(kXcFirst);
    crt.xcLast = At<winmainstart::Initialiser *>(kXcLast);
    crt.runCheckedInitialisers = &RunCheckedInitialisers;
    crt.runInitialisers = &RunInitialisers;
    crt.dynamicTlsInit = At<winmainstart::TlsCallback *>(kDynamicTlsInit);
    crt.dynamicTlsDtor = At<winmainstart::TlsCallback *>(kDynamicTlsDtor);
    crt.isNonwritableInImage = At<bool(__cdecl *)(const void *)>(kIsNonwritable);
    crt.registerTlsDtor = At<void(__cdecl *)(winmainstart::TlsCallback)>(kRegisterTlsDtor);
    crt.showWindowMode = At<uint16_t(__cdecl *)()>(kShowWindowMode);
    crt.narrowCommandLine = At<char *(__cdecl *)()>(kNarrowCommandLine);
    crt.instance = reinterpret_cast<void *>(base);
    crt.winMain = &LoggedWinMain;
    crt.isManagedApp = At<bool(__cdecl *)()>(kIsManagedApp);
    crt.cexit = At<void(__cdecl *)()>(kCexit);
    crt.uninitializeCrt = At<bool(__cdecl *)(bool, bool)>(kUninitializeCrt);
    crt.exit = &ImageExit;
    crt.exitNow = At<void(__cdecl *)(int)>(kExitNow);
    crt.exitQuick = At<void(__cdecl *)()>(kCExitQuick);
    crt.fastFail = At<void(__cdecl *)(unsigned)>(kFastFail);
    crt.exceptionFilter = At<int(__cdecl *)(unsigned long, EXCEPTION_POINTERS *)>(kSehFilterExe);
    // The owned XC entries register their destructors with the image CRT's atexit, in their slots (stage 3).
    staticinit::BindAtExit(&ImageAtExit);

    if (mode == 2) {
        winmaininfra::WinMainSeams &seams = winmaininfra::Seams();
        gOurSeams = seams;
        seams.constructFramework = &LoggedConstructFramework;
        seams.constructApp = &LoggedConstructApp;
        seams.registerAtExit = &ImageAtExit;
        seams.exit = &ImageExit;
        gWinMain = &winmaininfra::WinMainBody;
    } else {
        gWinMain = At<WinMainFn>(kGameWinMain);
    }
    Log("own start: mode %d (%s WinMain)", mode, mode == 2 ? "our" : "the game's");
    return winmainstart::RunImageStart(crt);
}

} // namespace ownstart

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    // The image's exception handlers live in private (non-image) memory, which Windows only
    // dispatches to with DEP off (linked /NXCOMPAT:NO too). Mapping it as an image section instead
    // would keep DEP on; a later step.
    SetProcessDEPPolicy(0);

    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    gDir = self;
    gDir.resize(gDir.find_last_of(L'\\') + 1);
    gOriginalExe = gDir + L"Sims2EP9.exe";
    gLog = _wfsopen((gDir + L"ts2host.log").c_str(), L"w", _SH_DENYNO); // readable while running
    // [Instance] and [Steam] in ts2reimpl.ini (tools/instances.ps1); neither is set in local/game.
    if (!LoadInstanceSettings(gDir)) return 1;
    PatchHostImportsForInstance();

    ConfigureDeviceLog(gDir); // TS2HOST_DEVLOG, read while the image's imports are resolved

    std::wstring imagePath = gDir + L"Sims2EP9.unpacked.exe";
    wchar_t env[MAX_PATH];
    if (GetEnvironmentVariableW(L"TS2HOST_IMAGE", env, MAX_PATH)) imagePath = env;

    static OriginalImage image; // lives for the whole process
    std::string error;
    if (!image.Load(imagePath, error, &ResolveImport)) {
        Log("cannot map the image: %s", error.c_str());
        return 1;
    }
    if (image.Base() != kGameBase) { // the build is linked against this exact base; no fallback
        Log("the image is at %p, not at the required base %p", reinterpret_cast<void *>(image.Base()), reinterpret_cast<void *>(kGameBase));
        return 1;
    }
    gImage = reinterpret_cast<HMODULE>(image.Base());
    gImageEnd = image.Base() + image.Size();
    Log("image mapped at %p (%u bytes)", reinterpret_cast<void *>(image.Base()), image.Size());
    if (!GetEnvironmentVariableW(L"TS2HOST_NOTLS", env, MAX_PATH) && !SetUpTls(image)) return 1;

    // Our replacements, installed before any game code runs. TS2HOST_NOHOOKS=1 runs the image as is.
    auto *base = reinterpret_cast<uint8_t *>(image.Base());
    SetGameBase(image.Base());
    InstallEspCheckReporter([](const char *line) { Log("%s", line); }, image.Base(), image.Base() + image.Size());
    if (!IsSupportedGame(base)) {
        Log("unsupported game image");
        return 1;
    }
    if (!GetEnvironmentVariableW(L"TS2HOST_NOHOOKS", env, MAX_PATH)) {
        int installed = InstallHooks(base, WideToAnsi(gDir + L"ts2reimpl.ini").c_str());
        Log("%d of %zu hooks installed", installed, RegisteredHooks().size());
        CreateThread(nullptr, 0, &HookCountReporter, nullptr, 0, nullptr);
    } else {
        Log("TS2HOST_NOHOOKS: no hooks installed");
    }

    auto *nt = image.At<IMAGE_NT_HEADERS32>(image.At<IMAGE_DOS_HEADER>(0)->e_lfanew);
    auto entry = reinterpret_cast<int(__cdecl *)()>(image.Base() + nt->OptionalHeader.AddressOfEntryPoint);
    StartQuietModeIfRequested(); // TS2REIMPL_QUIET
    StartSeedControl();          // TS2HOST_SEED (host/control_seed.cpp)
    StartControl(gDir);          // ts2control.txt (host/control.cpp)
    if (!GetEnvironmentVariableW(L"TS2HOST_NOFAULTLOG", env, MAX_PATH)) StartFaultLog(image.Base(), image.Size());
    if (GetEnvironmentVariableW(L"TS2HOST_TRACEDLLS", env, MAX_PATH)) WatchDllLoads();
    const int ownStart = ownstart::ReadMode(gDir); // [Startup] OwnStart (default 2); 0 = the image's own start
    // Originals replaced through their callers are trapped while their replacement is in use (hook/traps.h; the table
    // comes from db/status "in_effect": "callers"). Only switches reported here can arm a trap.
    SetTrapSwitch("Startup", "OwnStart", ownStart);
    // [Startup] OwnStatics (read by ReadMode, default 1): our XC runner skips the image's owned static initialisers only
    // with it on and OwnStart 1 or 2, so their traps are "Startup.OwnStart=1,2&Startup.OwnStatics=1".
    SetTrapSwitch("Startup", "OwnStatics", ownstart::gOwnStatics ? 1 : 0);
    const TrapsArmed traps = ArmTraps(base);
    if (traps.armed || traps.failed) Log("%d traps armed, %d not armed", traps.armed, traps.failed);
    if (traps.failed) {
        Log("stopping: an original that must not run any more can't be guarded");
        return 1;
    }
    // TS2HOST_FRAMECLOCK (host/control_frameclock.cpp): after the traps (it rewrites the image's rdtsc instructions, which
    // the traps' checks must not see) and the fault log (its exception handler goes ahead of the log's).
    StartFrameClock(image.Base());
    // TS2HOST_COVERAGE (src/hook/coverage.h): first-call breakpoints on the original functions, a diagnostic. Last, so
    // every hook, trap and control patch is in and is left alone.
    StartCoverage(base, image.Size(), gDir.c_str());
    // TS2HOST_X87PROBE (host/control_x87probe.cpp): the x87 control word of the game's threads, a diagnostic. After the
    // hooks (it probes hooked sites only, in front of their jumps).
    StartX87Probe(image.Base(), image.Size());
    if (ownStart == 1 || ownStart == 2) return ownstart::Run(image.Base(), ownStart);
    Log("calling the image's entry point");
    return entry(); // the game's CRT runs WinMain and exits the process itself
}
