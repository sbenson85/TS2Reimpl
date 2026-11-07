#pragma once
// WinMain's start-up infrastructure, held to written contracts (status `replaced`; docs/roadmap.md,
// docs/contracts/). These are not reimplementations of the original code: they are our own
// implementations of what the game needs from three pieces of WinMain (0xc2b87c), and each
// deliberately drops effects the contracts name.
//
//   docs/contracts/crash-reporter-start.md   0xc2b206  StartCrashReporter
//   docs/contracts/analytics.md              0xc2df67  SendAnalyticsEvent
//   docs/contracts/terminate-handlers.md     0xcac1f0  InstallTerminateAndPurecallHandlers
//                                            0xcac170  the handler they install
//
// The code reaches the operating system through winmaininfra::Os (real functions by default), so
// the contract tests can fake it; none of it reads the original image except the hybrid CRT seam
// (kImageCrt), which exists only while the game's own CRT is still in the process.
#define NOMINMAX
#include <windows.h>
#include <objbase.h>

#include <cstdint>
#include <string>

namespace winmaininfra {

// The operating-system entry points used here. Defaults are the real functions.
struct Os {
    decltype(&::CoInitializeEx) CoInitializeEx;
    decltype(&::CoInitializeSecurity) CoInitializeSecurity;
    decltype(&::SetUnhandledExceptionFilter) SetUnhandledExceptionFilter;
    decltype(&::UnhandledExceptionFilter) UnhandledExceptionFilter;
    decltype(&::GetLastError) GetLastError;
    decltype(&::SetLastError) SetLastError;
    decltype(&::FlsGetValue) FlsGetValue;
};
// The table in use. Tests overwrite entries and put them back.
Os &OsTable();
Os RealOs();

// ---- 0xc2b206: crash reporter start-up ----
//
// __cdecl(const std::string &uploadUrl, const std::wstring &handlerPath, const std::wstring
// &databasePath), returns bool in al (the original: whether the child process was started; WinMain
// ignores it). Ours never starts one and returns false. It keeps the process-wide COM set-up the
// original did on the way: CoInitializeEx(nullptr, COINIT_MULTITHREADED) on the calling thread and,
// when that succeeded (any non-negative result), CoInitializeSecurity(nullptr, -1, nullptr, nullptr,
// RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr). The three
// arguments are not used: they only fed the upload.
bool __cdecl StartCrashReporter(const std::string &uploadUrl, const std::wstring &handlerPath, const std::wstring &databasePath);

// ---- 0xc2df67: launch analytics ----
//
// __cdecl(uint32_t steamIdLow, uint32_t steamIdHigh), no result (the original leaves eax undefined
// and WinMain ignores it). Sends nothing and changes nothing.
void __cdecl SendAnalyticsEvent(uint32_t steamIdLow, uint32_t steamIdHigh);

// ---- 0xcac170 / 0xcac1f0: terminate and purecall handlers ----
using HandlerFn = void(__cdecl *)();

// 0xcac170, the handler itself (__cdecl, no arguments, no result). Takes the process's current
// top-level exception filter (SetUnhandledExceptionFilter(nullptr), then puts it back; with none, the
// system's UnhandledExceptionFilter), then deliberately reads and writes address 0 inside a
// __try/__except whose filter expression is "that filter, called with the new exception". A
// filter that answers EXCEPTION_EXECUTE_HANDLER makes this return normally; one that answers
// EXCEPTION_CONTINUE_SEARCH lets the access violation go on to outer handlers. The crash reporter
// (the application's exception filter) writes its report from inside that call.
void __cdecl ReportAndReturn();

// Where the handlers are installed: the two CRT setters. kOwnCrt is this program's CRT (the
// standalone build); kImageCrt writes the game CRT's state in the mapped image (while it is still
// there): _set_purecall_handler's global (RVA 0x10dfb80) and the calling thread's _terminate field
// (+0xc of its per-thread data, found through the FLS index at RVA 0x104f89c).
struct CrtSeam {
    void (*setTerminate)(HandlerFn);
    void (*setPurecall)(HandlerFn);
};
extern const CrtSeam kOwnCrt;
extern const CrtSeam kImageCrt;

// The order of the original: terminate handler first, then purecall handler, both ReportAndReturn.
void InstallHandlers(const CrtSeam &crt);

// 0xcac1f0 (__cdecl, no arguments, no result): InstallHandlers(kImageCrt).
void __cdecl InstallTerminateAndPurecallHandlers();

// ---- WinMain (0xc2b87c): a candidate, not hooked ----
//
// The whole of WinMain written from the disassembly with the replaced pieces above, the verified
// functions it calls (winmain::GetModuleDirectoryW, SteamInit, DisableImeTextFrameService, RunApp,
// the framework and application constructors, cRZCmdLine) and the game's own storage: the two
// function-local statics at RVA 0x10fdb50 (cCTGFramework, guard 0x10fdce8) and 0x10fdcf0 (cTSAppFW,
// guard 0x10fdf58). It is differential-tested (tests/winmain_infra_main.cpp) with the pieces
// that cannot run in the harness behind WinMainSeams, but it is NOT hooked: the host calls it from its
// own start sequencer instead (app/WinMainStart.h, [Startup] OwnStart=2), with the seams below bound to
// the image CRT (db/status/WinMain.jsonl, 0xc2b87c; docs/subsystems/startup.md, E1).
//
// registerAtExit and exit default to our own CRT (the standalone build). While the image's CRT is in the
// process, whoever calls WinMainBody binds them to the image CRT's atexit and exit (the host does, under
// [Startup] OwnStart=2: docs/subsystems/startup.md, E1), so that the two destructors run from the game's
// atexit list in the original's order and the Steam-failure exit runs that list as the original's does.
struct WinMainSeams {
    bool(__cdecl *startCrashReporter)(const std::string &, const std::wstring &, const std::wstring &);
    void(__cdecl *sendAnalytics)(uint32_t, uint32_t);
    void(__cdecl *installHandlers)();
    void (*constructFramework)(void *storage);                                                    // 0xc2ccdb
    void (*constructApp)(void *storage, uint32_t serviceId, const char *name, void *commandLine); // 0xc26147
    int32_t(__cdecl *runApp)(void *framework, void *app, void *commandLine);                      // 0xc2d23b
    int(__cdecl *registerAtExit)(void(__cdecl *)());                                              // the CRT's atexit
    void(__cdecl *exit)(int); // the CRT's exit (0x82f6bf in the original): runs the atexit list, then ends the process
};
WinMainSeams &Seams();

// The destructors WinMain registers with atexit for the two statics (ours; the original registers the
// image's thunks at 0xd988fa and 0xd988f0, which tail-jump to the destructor bodies).
void __cdecl DestroyFramework();
void __cdecl DestroyApp();

// __stdcall(instance, previous, commandLine, show), ret 0x10; returns RunApp's exit code. All four
// arguments are unused (the original ignores them too).
int __stdcall WinMainBody(void *instance, void *previous, char *commandLine, int show);

} // namespace winmaininfra
