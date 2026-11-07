#pragma once
#include "gz/CommandParser.h"
#include "game/owned.h"
#include "rz/RZAutoRef.h"
#include "rz/cRZCmdLine.h"

#include <cstdint>

// The top of the start-up path (RVAs 1.18.0.187, docs/startup-map.md sections 3 and 4.3): the pieces
// of WinMain (0xc2b87c) and of the run sequence it reaches that are small enough to stand alone.
//
// Not here, and why:
//  * WinMain itself (0xc2b87c) starts the crash reporter (0xc2b206: WMI, COM, a child process) and
//    sends the analytics event (0xc2df67: WinINet); neither is reimplemented, and a replacement may
//    not call original code, so WinMain stays `mapped`.
//  * RunApp (0xc2d23b) is done (below); its SEH filter calls the in-process exception reporter
//    (0x1bd69a, appbase::Report).
//  * The application constructor (0xc26147) and the cTSAppFW slots are listed in the status file.

// The exceptions RunFrameworkGuarded catches. Named as in the game: the catch matches by decorated
// name, so exceptions thrown by game code reach these handlers and ours reach the game's.
// cError's vtable (owned static: the image's in the hooked game; our ts2_own_vt_d9cd70, defined with its one slot in
// src/misc/SmallVirtuals.cpp, aliased in an image-free build). cGraphicSystemDX and cDeviceDX throw a cError carrying it.
TS2_OWNED_STATIC(const OwnedVTable, d9cd70);

namespace nGZGraphic4 {
// Only the layout the handler reads: the game's cError is a cIGZExceptionBase (vtable pointer, here a
// plain field, see CommandParser.h) followed by an error code (+4). Its real size is not known;
// nothing here copies or builds one except tests.
class cError : public cIGZExceptionBase {
public:
    int32_t mCode; // +4 which message to show
};
static_assert(offsetof(cError, mCode) == 4, "cError layout");
} // namespace nGZGraphic4

namespace nRZGraphic4 {
// A Direct3D failure: its vtable slot 0 gives the error value the handler formats.
class cD3DException {
public:
    const void *mVTable; // +0 slot 0: uint32_t (__thiscall) the error value
};
} // namespace nRZGraphic4

namespace winmain {

// 0xc27b04 (__cdecl, buffer, size in characters): the directory of the program. Reads the module
// path into `buffer` (GetModuleFileNameW(0, buffer, size)). When that fails (0 characters) or fills
// the buffer (size or more), the buffer's first character is cleared and 0 is returned. Otherwise
// the characters after the last '/' or '\' are overwritten with zeros, and the number of characters
// up to and including that separator is returned (0 when there is none; then all of the path was
// cleared).
uint32_t __cdecl GetModuleDirectoryW(wchar_t *buffer, uint32_t size);

// 0xc2a358 (__cdecl): SteamInternal_SteamAPI_Init(interfaceVersions, 0) through the import table.
// Returns eax as the original leaves it: al is 1 when the call returned 0 (Steam's "OK"), else 0, and
// the upper 24 bits are those of the negated result.
uint32_t __cdecl SteamInit();

// 0xc2a3a4 (__cdecl): tells imm32 not to use the text frame service, if the DLL has the function:
// ImmDisableTextFrameService(-1). Finds the module with GetModuleHandleA, loading it when it is not
// loaded yet. Returns what the last call returned (0 when the module or the function is missing).
uint32_t __cdecl DisableImeTextFrameService();

// 0x1446fc (__cdecl): g_App = app, then app->AddRef() (slot 1, a tail call: its result is returned).
uint32_t __cdecl SetGlobalApp(void *app);

// 0x14470b (__cdecl): g_Framework and its second reference global = framework, then AddRef twice
// (slot 1); the second is a tail call and its result is returned.
uint32_t __cdecl SetGlobalFramework(void *framework);

// 0x1bbb09 (__thiscall, no arguments): the byte at +0x19b of the exception reporter. RunApp reads it
// to decide whether to run the framework under the reporter's guard. The byte itself is returned.
uint8_t __fastcall ExceptionHandlingEnabled(const void *self, void *edx);

// 0xc2ccb6 (__thiscall, no arguments; `this` is the holder): clears *this, then asks the service
// object (g_Framework) through slot 5 for the service with id 0xfb6372ee, interface 0xbb6377fa. The
// service is stored in *this. Returns this. It is the object whose slot 40 gives the window to own
// message boxes.
RZAutoRef *__fastcall AcquireWindowOwner(RZAutoRef *holder, void *edx);

// 0xc2da5a (__cdecl, text, caption): shows an error box. Both texts are copied, every U+200B (the
// zero-width space, 3 bytes in UTF-8) is cut out of each, and both are converted to the system code
// page. The box (MessageBoxA, flags 0x40010) is owned by the window the service from
// AcquireWindowOwner gives (slot 40); the service is released afterwards (swallowing C++ exceptions).
void __cdecl ShowFatalMessage(const char *text, const char *caption);

// 0xc2dbf1 (__cdecl, text, caption; lane MB): ShowFatalMessage's question twin: the same cutting, conversion and owner,
// MessageBoxA with flags 0x40014 (Yes / No); true when the answer was Yes (IDYES). cTSAppFW::Init asks it when the user
// data's drive is short of space.
bool __cdecl AskYesNo(const char *text, const char *caption);

// 0xc2d0a8 (__cdecl): RunFramework(framework, app, commandLine). Makes `framework` the global
// framework, asks the application for its cIGZApp face (QueryInterface with id 0x66) and makes it
// g_App when it has one, records the application for the message hook (0x10fdfe8), then
// GZFrameWork_Startup(commandLine, 0), GZFrameWork_Run when that passed, and GZFrameWork_Shutdown
// always. The face reference is released on the way out, also when something throws. The return
// value is not defined (no caller uses it).
void __cdecl RunFramework(void *framework, void *app, cRZCmdLine *commandLine);

// 0xc2d121 (__cdecl, same arguments): RunFramework inside a C++ try. A nGZGraphic4::cError shows
// the message picked by its code (0: string 0x10fdad4, 1: 0x10fda94, 2: 0x10fdab4, else 0x10fdaf4;
// the caption is 0x10fda54); an nRZGraphic4::cD3DException shows the caption's format string
// (0x10fda74) with its error value. Either way g_ExitCode becomes -1 and the process is ended with
// TerminateProcess(GetCurrentProcess(), -1); the function returns if that call does.
void __cdecl RunFrameworkGuarded(void *framework, void *app, cRZCmdLine *commandLine);

// 0xc2d23b (__cdecl, same arguments): RunApp. The application embeds the exception reporter at
// +0x98. When the reporter's flag byte (+0x19b, ExceptionHandlingEnabled) is zero this is
// RunFramework; otherwise RunFrameworkGuarded inside an SEH __try whose filter calls
// Report(reporter, exception pointers) for the first exception it sees only (a second one, raised
// while the reporter runs, skips the report) and always selects the handler: ExitProcess(0), then
// an int3. Returns g_ExitCode.
int32_t __cdecl RunApp(void *framework, void *app, cRZCmdLine *commandLine);

// Test hook: when set, RunApp's filter calls this instead of appbase::Report (a test cannot
// run the whole reporter, and the original's call is redirected the same way). Null in the game.
using RunAppReportFn = uint32_t(__fastcall *)(void *reporter, void *edx, void *exceptionPointers);
extern RunAppReportFn sRunAppReportOverride;

} // namespace winmain
