#include "app/WinMain.h"

#include "app/ReportWrite.h"
#include "game/game.h"
#include "game/vtable.h"
#include "gz/Framework.h"
#include "gz/FrameworkStartup.h"
#include "rz/RZTextConvert.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/cRZString.h"

#define NOMINMAX
#include <windows.h>

namespace winmain {
namespace {

// The game's import table: the original calls these through its IAT slots, so does this code.
template <typename Fn> Fn Import(uint32_t iatRva)
{
    return *GameAddress<Fn>(iatRva);
}
constexpr uint32_t kIatGetModuleFileNameW = 0xd9a328;
constexpr uint32_t kIatSteamInit = 0xd9a70c;
constexpr uint32_t kIatGetModuleHandleA = 0xd9a114;
constexpr uint32_t kIatLoadLibraryA = 0xd9a128;
constexpr uint32_t kIatGetProcAddress = 0xd9a124;
constexpr uint32_t kIatMessageBoxA = 0xd9a568;
constexpr uint32_t kIatGetCurrentProcess = 0xd9a1b8;
constexpr uint32_t kIatTerminateProcess = 0xd9a1c0;

constexpr uint32_t kSteamInterfaces = 0xe985a8; // "SteamUtils010\0STEAMAPPS_INTERFACE_VERSION008\0..."
constexpr uint32_t kImmName = 0xe98904;         // "imm32"
constexpr uint32_t kImmFunction = 0xe9890c;     // "ImmDisableTextFrameService"
constexpr uint32_t kZeroWidthSpace = 0xda51b4;  // U+200B in UTF-8

// Game strings (cRZString statics built before WinMain runs).
constexpr uint32_t kMessageCaption = 0x10fda54;
constexpr uint32_t kErrorFormat = 0x10fda74;
constexpr uint32_t kMessageCode1 = 0x10fda94;
constexpr uint32_t kMessageCode2 = 0x10fdab4;
constexpr uint32_t kMessageCode0 = 0x10fdad4;
constexpr uint32_t kMessageOther = 0x10fdaf4;
constexpr uint32_t kGlobalAppForMessageHook = 0x10fdfe8;

using Unknown = uint32_t(__thiscall *)(void *);

const char *ToChar(uint32_t stringRva)
{
    return GameAddress<cRZString>(stringRva)->ToChar();
}

// A cRZString local, built and torn down as the original's stack objects are.
class Str {
public:
    explicit Str(const char *text) { S().Construct(text); }
    Str() { S().Construct(); }
    ~Str() { S().Destruct(); }
    Str(const Str &) = delete;
    Str &operator=(const Str &) = delete;
    cRZString &S() { return *reinterpret_cast<cRZString *>(mRaw); }

private:
    alignas(cRZString) unsigned char mRaw[sizeof(cRZString)];
};

// Releases the cIGZApp face RunFramework holds, on every way out.
struct FaceRelease {
    void *mFace = nullptr;
    ~FaceRelease()
    {
        if (mFace) VirtualCall<Unknown, 2>(mFace);
    }
};

// Cuts every occurrence of `separator` (3 bytes) out of `text`, searching on from the cut.
void CutAll(cRZString &text, const cRZString &separator)
{
    int32_t at = text.Find(*separator.AsInterface(), 0, true);
    while (at != -1) {
        text.Erase(static_cast<uint32_t>(at), 3);
        at = text.Find(*separator.AsInterface(), static_cast<uint32_t>(at), true);
    }
}

} // namespace

// ---- 0xc27b04 ----

uint32_t __cdecl GetModuleDirectoryW(wchar_t *buffer, uint32_t size)
{
    uint32_t length = Import<DWORD(__stdcall *)(HMODULE, wchar_t *, DWORD)>(kIatGetModuleFileNameW)(nullptr, buffer, size);
    if (length == 0 || length >= size) {
        buffer[0] = 0;
        return 0;
    }
    wchar_t *at = buffer + (length - 1);
    for (;;) {
        if (*at == L'/' || *at == L'\\') return length;
        *at = 0;
        --at;
        if (--length == 0) return 0;
    }
}

// ---- 0xc2a358 ----

uint32_t __cdecl SteamInit()
{
    int32_t result = Import<int32_t(__cdecl *)(const char *, void *)>(kIatSteamInit)(GameAddress<const char>(kSteamInterfaces), nullptr);
    // neg eax; sbb al, al; inc al: only al is rewritten, the rest of eax is the negated result.
    return (0u - static_cast<uint32_t>(result) & ~0xFFu) | (result == 0 ? 1u : 0u);
}

// ---- 0xc2a3a4 ----

uint32_t __cdecl DisableImeTextFrameService()
{
    const char *name = GameAddress<const char>(kImmName);
    HMODULE module = Import<HMODULE(__stdcall *)(const char *)>(kIatGetModuleHandleA)(name);
    if (!module) {
        module = Import<HMODULE(__stdcall *)(const char *)>(kIatLoadLibraryA)(name);
        if (!module) return 0;
    }
    auto function = Import<void *(__stdcall *)(HMODULE, const char *)>(kIatGetProcAddress)(module, GameAddress<const char>(kImmFunction));
    if (!function) return 0;
    return reinterpret_cast<uint32_t(__stdcall *)(int32_t)>(function)(-1);
}

// ---- 0x1446fc / 0x14470b ----

uint32_t __cdecl SetGlobalApp(void *app)
{
    *GameAddress<void *>(cGZFramework::kGlobalApp) = app;
    return VirtualCall<Unknown, 1>(app);
}

uint32_t __cdecl SetGlobalFramework(void *framework)
{
    ts2_rva_10c937c = framework;
    ts2_rva_10c9378 = framework;
    VirtualCall<Unknown, 1>(framework);
    void *again = ts2_rva_10c937c;
    return VirtualCall<Unknown, 1>(again);
}

// ---- 0x1bbb09 ----

uint8_t __fastcall ExceptionHandlingEnabled(const void *self, void *)
{
    return *(static_cast<const uint8_t *>(self) + 0x19b);
}

// ---- 0xc2ccb6 ----

RZAutoRef *__fastcall AcquireWindowOwner(RZAutoRef *holder, void *)
{
    holder->mObject = nullptr;
    void *service = GetServiceObject();
    if (service) {
        using Query = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
        VirtualCall<Query, 5>(service, 0xfb6372ee, 0xbb6377fa, &holder->mObject);
    }
    return holder;
}

// ---- 0xc2da5a ----

namespace {

// What ShowFatalMessage and AskYesNo share: both texts copied, every U+200B cut out of each, both converted to the
// system code page, MessageBoxA with `flags` owned by the window service's slot 40, the service released. The box's
// answer.
int ShowMessageBox(const char *text, const char *caption, UINT flags)
{
    Str message(text);
    Str title(caption);

    // The separator is a function-local static in each original (ShowFatalMessage: guard 0x10fe01c, cRZString at
    // 0x10fdffc; AskYesNo: guard 0x10fe040, cRZString at 0x10fe020; both destroyed at exit, both the same text); here
    // one is built on first use and left alone.
    static cRZString *const separator = [] {
        auto *s = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
        s->Construct(GameAddress<const char>(kZeroWidthSpace));
        return s;
    }();

    CutAll(message.S(), *separator);
    CutAll(title.S(), *separator);
    RZText::ConvertStringToString(*message.S().AsInterface(), *message.S().AsInterface(), -1);
    RZText::ConvertStringToString(*title.S().AsInterface(), *title.S().AsInterface(), -1);

    RZAutoRef owner;
    AcquireWindowOwner(&owner, nullptr);
    void *hwnd = reinterpret_cast<void *>(static_cast<uintptr_t>(VirtualCall<Unknown, 40>(owner.mObject)));
    const int answer = Import<int(__stdcall *)(HWND, const char *, const char *, UINT)>(kIatMessageBoxA)(
        static_cast<HWND>(hwnd), message.S().mString.c_str(), title.S().mString.c_str(), flags);
    owner.ReleaseObject();
    return answer;
}

} // namespace

void __cdecl ShowFatalMessage(const char *text, const char *caption)
{
    ShowMessageBox(text, caption, MB_OK | MB_ICONERROR | MB_TOPMOST);
}

// ---- 0xc2dbf1 ----

bool __cdecl AskYesNo(const char *text, const char *caption)
{
    return ShowMessageBox(text, caption, MB_YESNO | MB_ICONERROR | MB_TOPMOST) == IDYES;
}

// ---- 0xc2d0a8 ----

void __cdecl RunFramework(void *framework, void *app, cRZCmdLine *commandLine)
{
    SetGlobalFramework(framework);
    FaceRelease face;
    using Query = uint8_t(__thiscall *)(void *, uint32_t, void **);
    if (VirtualCall<Query, 0>(app, 0x66, &face.mFace)) SetGlobalApp(face.mFace);
    *GameAddress<void *>(kGlobalAppForMessageHook) = app;
    if (fw_startup::GZFrameWork_Startup(commandLine, 0)) GZFrameWork_Run();
    GZFrameWork_Shutdown();
}

// ---- 0xc2d121 ----

namespace {
// What both handlers end with: g_ExitCode = -1, then TerminateProcess(GetCurrentProcess(), -1).
void EndProcess()
{
    *GameAddress<int32_t>(cGZFramework::kGlobalExitCode) = -1;
    HANDLE self = Import<HANDLE(__stdcall *)()>(kIatGetCurrentProcess)();
    Import<BOOL(__stdcall *)(HANDLE, UINT)>(kIatTerminateProcess)(self, static_cast<UINT>(-1));
}
} // namespace

void __cdecl RunFrameworkGuarded(void *framework, void *app, cRZCmdLine *commandLine)
{
    try {
        RunFramework(framework, app, commandLine);
    } catch (const nGZGraphic4::cError &error) {
        const char *caption = ToChar(kMessageCaption);
        uint32_t message;
        switch (error.mCode) {
        case 0: message = kMessageCode0; break;
        case 1: message = kMessageCode1; break;
        case 2: message = kMessageCode2; break;
        default: message = kMessageOther; break;
        }
        ShowFatalMessage(ToChar(message), caption);
        EndProcess();
    } catch (const nRZGraphic4::cD3DException &error) {
        Str text;
        uint32_t value = VirtualCall<uint32_t(__thiscall *)(const void *), 0>(&error);
        text.S().Sprintf(ToChar(kErrorFormat), value);
        ShowFatalMessage(text.S().ToChar(), ToChar(kMessageCaption));
        EndProcess();
    }
}

// ---- 0xc2d23b ----

RunAppReportFn sRunAppReportOverride = nullptr;

namespace {
constexpr uint32_t kIatExitProcess = 0xd9a2d8;
constexpr uint32_t kReporterInApp = 0x98; // 0x418be0 returns app + 0x98

// The __except filter (0xc2d29e). Only the first exception reports; one raised while the reporter
// runs (the filter then runs again for it) goes straight to the handler. Always selects it.
int RunAppFilter(int *seen, void *app, EXCEPTION_POINTERS *info)
{
    int previous = (*seen)++;
    if (previous == 0) {
        void *reporter = static_cast<uint8_t *>(app) + kReporterInApp;
        if (sRunAppReportOverride) sRunAppReportOverride(reporter, nullptr, info);
        else appbase::Report(static_cast<appbase::ExceptionReporterObject *>(reporter), nullptr, info);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
} // namespace

// Built /GS- like the original, so this __try is an SEH3 frame (__SEH_prolog, _except_handler3: the same library code
// the original uses). That fixes the crash path's unwind: _global_unwind2 passes RtlUnwind no exception record, so the
// /EHs frames below run no destructors before ExitProcess. An SEH4 frame (/GS) would pass the record and run them
// (tests/winmain_run_app.cpp tells the two apart).
int32_t __cdecl RunApp(void *framework, void *app, cRZCmdLine *commandLine)
{
    if (!ExceptionHandlingEnabled(static_cast<uint8_t *>(app) + kReporterInApp, nullptr)) {
        RunFramework(framework, app, commandLine);
    } else {
        int seen = 0;
        __try {
            RunFrameworkGuarded(framework, app, commandLine);
        } __except (RunAppFilter(&seen, app, GetExceptionInformation())) {
            Import<void(__stdcall *)(UINT)>(kIatExitProcess)(0);
            __debugbreak();
        }
    }
    return *GameAddress<int32_t>(cGZFramework::kGlobalExitCode);
}

} // namespace winmain
