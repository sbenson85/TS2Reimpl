#include "app/WinMainInfra.h"

#include "app/WinMain.h"
#include "game/game.h"
#include "game/vtable.h"
#include "gz/Framework.h"
#include "gz/FrameworkShape.h"
#include "gz/GZCOM.h"
#include "rz/cRZCmdLine.h"
#include "rz/cRZString.h"
#include "ts/cTSAppFW.h"

#include <intrin.h>

#include <cstdlib>
#include <exception>
#include <new>

namespace winmaininfra {

// ---- the operating system ----

Os RealOs()
{
    return Os{&::CoInitializeEx,  &::CoInitializeSecurity, &::SetUnhandledExceptionFilter, &::UnhandledExceptionFilter,
              &::GetLastError,    &::SetLastError,         &::FlsGetValue};
}

Os &OsTable()
{
    static Os table = RealOs();
    return table;
}

// ---- 0xc2b206 ----

bool __cdecl StartCrashReporter(const std::string &, const std::wstring &, const std::wstring &)
{
    // Contract: docs/contracts/crash-reporter-start.md. Nothing here starts a crash handler, writes a
    // crash database, collects hardware information or uploads anything.
    Os &os = OsTable();
    HRESULT result = os.CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (result >= 0) {
        os.CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
    }
    return false;
}

// ---- 0xc2df67 ----

void __cdecl SendAnalyticsEvent(uint32_t, uint32_t)
{
    // Contract: docs/contracts/analytics.md. No network, no buffer, no state.
}

// ---- 0xcac170 ----

void __cdecl ReportAndReturn()
{
    Os &os = OsTable();
    LPTOP_LEVEL_EXCEPTION_FILTER filter = os.SetUnhandledExceptionFilter(nullptr);
    if (filter) {
        os.SetUnhandledExceptionFilter(filter);
    } else {
        filter = reinterpret_cast<LPTOP_LEVEL_EXCEPTION_FILTER>(os.UnhandledExceptionFilter);
    }
    __try {
        // The deliberate crash: a read then a write of address 0, so that the filter sees an access
        // violation (code 0xC0000005, information 0 = read, address 0) as the application's own
        // exception filter does for any crash.
        char *volatile at = nullptr;
        char value = *at;
        *at = value;
    } __except (filter(GetExceptionInformation())) {
    }
}

// ---- 0xcac1f0 ----

namespace {

void OwnSetTerminate(HandlerFn handler)
{
    std::set_terminate(handler);
}

void OwnSetPurecall(HandlerFn handler)
{
    _set_purecall_handler(handler);
}

// The game CRT's globals (RVA, 1.18.0.187). Image dependencies, to go when the CRT does.
constexpr uint32_t kImagePurecallHandler = 0x10dfb80; // _set_purecall_handler's variable
constexpr uint32_t kImageFlsIndex = 0x104f89c;        // the FLS slot of the per-thread data
constexpr uint32_t kPtdTerminateOffset = 0xc;         // _terminate in the per-thread data

void ImageSetPurecall(HandlerFn handler)
{
    _InterlockedExchangePointer(reinterpret_cast<void *volatile *>(GameAddress<void *>(kImagePurecallHandler)), reinterpret_cast<void *>(handler));
}

void ImageSetTerminate(HandlerFn handler)
{
    // The original gets the thread's data with the CRT's own lookup, which keeps the thread's last
    // error across the call. This reads the same slot; a thread without data (the original would
    // create it) is left alone: WinMain's thread has it from the CRT's start-up.
    Os &os = OsTable();
    DWORD savedError = os.GetLastError();
    DWORD index = *GameAddress<DWORD>(kImageFlsIndex);
    void *data = index == FLS_OUT_OF_INDEXES ? nullptr : os.FlsGetValue(index);
    os.SetLastError(savedError);
    if (data == nullptr || data == reinterpret_cast<void *>(static_cast<uintptr_t>(-1))) return;
    *reinterpret_cast<HandlerFn *>(static_cast<uint8_t *>(data) + kPtdTerminateOffset) = handler;
}

} // namespace

const CrtSeam kOwnCrt = {&OwnSetTerminate, &OwnSetPurecall};
const CrtSeam kImageCrt = {&ImageSetTerminate, &ImageSetPurecall};

void InstallHandlers(const CrtSeam &crt)
{
    crt.setTerminate(&ReportAndReturn);
    crt.setPurecall(&ReportAndReturn);
}

void __cdecl InstallTerminateAndPurecallHandlers()
{
    InstallHandlers(kImageCrt);
}

// ---- WinMain 0xc2b87c ----

namespace {

// The game's import table, as in app/WinMain.cpp: WinMain reaches Windows and Steam through its slots.
template <typename Fn> Fn Import(uint32_t iatRva)
{
    return *GameAddress<Fn>(iatRva);
}
constexpr uint32_t kIatGetTempPathW = 0xd9a330, kIatGetCommandLineW = 0xd9a398, kIatMessageBoxA = 0xd9a568;
constexpr uint32_t kIatSteamContextInit = 0xd9a700, kIatSteamShutdown = 0xd9a708;

constexpr uint32_t kUploadUrl = 0xe98a10;         // narrow literal (never copied into this repository)
constexpr uint32_t kHandlerName = 0xe98a7c;       // L"crashpad_handler.exe"
constexpr uint32_t kDatabaseName = 0xe98aa8;      // L"TS2Legacy"
constexpr uint32_t kSteamFailCaption = 0xe98abc;
constexpr uint32_t kSteamFailText = 0xe98adc;
constexpr uint32_t kMemoryReportSwitch = 0xe98b10;
constexpr uint32_t kSteamUserContext = 0x1081a7c; // SteamInternal_ContextInit's argument
constexpr uint32_t kMemoryObject = 0x10fda2c;     // +0x10: the byte WinMain clears without -memoryReport
constexpr uint32_t kFrameworkStorage = 0x10fdb50, kFrameworkGuard = 0x10fdce8;
constexpr uint32_t kAppStorage = 0x10fdcf0, kAppGuard = 0x10fdf58;
constexpr uint32_t kAppNameString = 0x10fda54;
constexpr uint32_t kAppServiceId = 0x5afdaae5;

void ConstructFramework(void *storage)
{
    ::new (storage) gz::cCTGFramework; // our class, our vtables (lane EX)
}

void ConstructApp(void *storage, uint32_t serviceId, const char *name, void *commandLine)
{
    ::new (storage) ts::cTSAppFW(serviceId, name, static_cast<cIGZCmdLine *>(commandLine)); // our class, our vtables (lane FG)
}

int32_t __cdecl RunTheApp(void *framework, void *app, void *commandLine)
{
    return winmain::RunApp(framework, app, static_cast<cRZCmdLine *>(commandLine));
}

int __cdecl RegisterAtExit(void(__cdecl *function)())
{
    return std::atexit(function);
}

void __cdecl ExitProcessWithAtExit(int code)
{
    std::exit(code);
}

} // namespace

WinMainSeams &Seams()
{
    static WinMainSeams seams = {&StartCrashReporter, &SendAnalyticsEvent, &InstallTerminateAndPurecallHandlers, &ConstructFramework,
                                 &ConstructApp,       &RunTheApp,          &RegisterAtExit,                     &ExitProcessWithAtExit};
    return seams;
}

void __cdecl DestroyFramework()
{
    GameAddress<gz::cCTGFramework>(kFrameworkStorage)->gz::cCTGFramework::~cCTGFramework();
}

void __cdecl DestroyApp()
{
    GameAddress<ts::cTSAppFW>(kAppStorage)->ts::cTSAppFW::~cTSAppFW();
}

namespace {

// A cRZString on the stack, built and torn down like the original's locals.
class Str {
public:
    Str() { S().Construct(); }
    explicit Str(const char *text) { S().Construct(text); }
    ~Str() { S().Destruct(); }
    Str(const Str &) = delete;
    Str &operator=(const Str &) = delete;
    cRZString &S() { return *reinterpret_cast<cRZString *>(mRaw); }

private:
    alignas(cRZString) unsigned char mRaw[sizeof(cRZString)];
};

// A function-local static of the original: constructed on first use in storage of the game's, guarded
// by the game's guard word. The compiler's protocol (the thread's epoch, _Init_thread_header and
// _Init_thread_footer in the game's CRT) is not repeated: WinMain runs once, on one thread, so the guard
// only has to say "done" (the original leaves the CRT's epoch number there, nothing reads it).
template <typename Construct> void ConstructOnce(uint32_t guardRva, Construct construct)
{
    int32_t *guard = GameAddress<int32_t>(guardRva);
    if (*guard != 0) return;
    *guard = -1;
    construct();
    *guard = 1;
}

} // namespace

int __stdcall WinMainBody(void *, void *, char *, int)
{
    WinMainSeams &seams = Seams();

    // Crash reporter: its three arguments (inert since the contract) are built as the original builds them.
    std::string uploadUrl(GameAddress<const char>(kUploadUrl));
    wchar_t directory[0x104];
    winmain::GetModuleDirectoryW(directory, 0x104);
    std::wstring handlerPath(directory);
    handlerPath += reinterpret_cast<const wchar_t *>(GameAddress<const uint16_t>(kHandlerName));
    // ORIGINAL (uninitialised buffer): GetTempPathW is trusted to fill it.
    wchar_t temp[0x104];
    Import<DWORD(__stdcall *)(DWORD, wchar_t *)>(kIatGetTempPathW)(0x104, temp);
    std::wstring databasePath(temp);
    databasePath += reinterpret_cast<const wchar_t *>(GameAddress<const uint16_t>(kDatabaseName));
    seams.startCrashReporter(uploadUrl, handlerPath, databasePath);

    if (!(winmain::SteamInit() & 0xff)) {
        Import<int(__stdcall *)(HWND, const char *, const char *, UINT)>(kIatMessageBoxA)(nullptr, GameAddress<const char>(kSteamFailText),
                                                                                           GameAddress<const char>(kSteamFailCaption), 0);
        seams.exit(0);
        __debugbreak(); // exit does not return
    }

    // The player's Steam id, for the analytics call. The user interface comes from the Steam context.
    void **context = Import<void **(__cdecl *)(void *)>(kIatSteamContextInit)(GameAddress<void>(kSteamUserContext));
    void *user = *context;
    uint32_t steamId[2];
    VirtualCall<void(__thiscall *)(void *, uint32_t *), 2>(user, steamId);
    seams.sendAnalytics(steamId[0], steamId[1]);

    seams.installHandlers();
    winmain::DisableImeTextFrameService();

    // The command line as UTF-8 text.
    const wchar_t *wide = Import<const wchar_t *(__stdcall *)()>(kIatGetCommandLineW)();
    uint32_t length = 0;
    while (wide[length] != 0) ++length;
    Str text;
    ConvertBytesToString(wide, length, 0x10, text.S().AsInterface());
    alignas(cRZCmdLine) unsigned char commandLineRaw[sizeof(cRZCmdLine)];
    cRZCmdLine *commandLine = reinterpret_cast<cRZCmdLine *>(commandLineRaw);
    commandLine->Construct(text.S().ToChar());

    bool memoryReport;
    {
        Str name(GameAddress<const char>(kMemoryReportSwitch));
        memoryReport = commandLine->IsSwitchPresent(*name.S().AsInterface());
    }
    if (!memoryReport) *(GameAddress<uint8_t>(kMemoryObject) + 0x10) = 0;

    void *framework = GameAddress<void>(kFrameworkStorage);
    ConstructOnce(kFrameworkGuard, [&] {
        seams.constructFramework(framework);
        seams.registerAtExit(&DestroyFramework);
    });
    void *app = GameAddress<void>(kAppStorage);
    ConstructOnce(kAppGuard, [&] {
        seams.constructApp(app, kAppServiceId, GameAddress<cRZString>(kAppNameString)->ToChar(), commandLine);
        seams.registerAtExit(&DestroyApp);
    });

    int32_t result = seams.runApp(framework, app, commandLine);
    Import<void(__cdecl *)()>(kIatSteamShutdown)();

    commandLine->Destruct();
    return result;
}

} // namespace winmaininfra
