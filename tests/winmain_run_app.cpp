// Differential test for RunApp (0xc2d23b): RunFramework, or RunFrameworkGuarded inside an SEH
// __try whose filter calls the in-process exception reporter's Report (0x1bd69a) for the first
// exception, and whose handler ends in ExitProcess(0).
//
// RunFramework and RunFrameworkGuarded are verified (tests/winmain.cpp); this test runs the same
// kind of scenario (a real framework with recording slots, fake application objects, the same
// import fakes) through RunApp, so what is compared is RunApp's own work: which of the two it
// calls, the SEH filter and handler, the arguments of Report and the process end.
//
// Report is too large to run here (DbgHelp, files, the clock). The original's first five bytes at
// 0x1bd69a are replaced with a jump to a recording fake (restored afterwards), and our filter calls
// the same fake through winmain::sRunAppReportOverride. The fake can raise a second exception, as
// the real reporter might: the filter then runs again for it and must skip the report. ExitProcess
// is faked in the import table to throw a sentinel the test catches (the original follows the call
// with an int3 and ours with __debugbreak, which are never reached).
//
// The crash path's unwind is part of what is compared. When the filter selects the handler, the
// original's _except_handler3 (0x826a30, entered through __SEH_prolog 0x824dc0; both byte-matched
// MSVC 14.40 library code) destroys a C++ exception object (CallDestructExceptionObject 0x829a19),
// then unwinds every frame below RunApp with _global_unwind2 (0x829a60), which passes RtlUnwind no
// exception record: the frames see STATUS_UNWIND, so /EHs C++ frames run no destructors while SEH
// __finally blocks do run. Ours, built /GS- like the original, gets the same __SEH_prolog and
// _except_handler3 from our toolchain (with /GS it would be SEH4, whose unwind passes the record and
// runs the C++ destructors). The scenarios make each of these visible in the log: a thrown object
// whose destructor logs, a C++ guard and a __finally in the frames between the throw and RunApp,
// RunFramework's own face guard, and a reporter that raises a second exception (SEH or C++).
#define NOMINMAX
#include "app/WinMain.h"
#include "difftest.h"
#include "game/game.h"
#include "gz/Framework.h"
#include "gz/FrameworkShape.h"
#include "rz/cRZCmdLine.h"
#include "rz/cRZString.h"

#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Fw = cCTGFramework;

constexpr uint32_t kRunApp = 0xc2d23b, kReportRva = 0x1bd69a;
constexpr uint32_t kIatGetModuleFileNameA = 0xd9a16c, kIatMessageBoxA = 0xd9a568, kIatGetCurrentProcess = 0xd9a1b8, kIatTerminateProcess = 0xd9a1c0,
                   kIatExitProcess = 0xd9a2d8;
constexpr uint32_t kModuleFileNameWResolved = 0x10caeb5, kModuleFileNameWPointer = 0x10caeb8;
constexpr uint32_t kGAppForHook = 0x10fdfe8;
constexpr uint32_t kReporterOffset = 0x98, kFlagByteOffset = 0x19b;

std::vector<std::string> gLog;

void Log(const char *fmt, ...)
{
    char buf[1024];
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
        if (std::getenv("RUNAPP_DUMP")) std::fprintf(stderr, "=== %s\n--- original\n%s--- ours\n%s", what, a.c_str(), b.c_str());
        // Failure messages are cut at about 500 characters: show the head and the first difference.
        size_t at = 0;
        while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
        size_t from = at > 100 ? at - 100 : 0;
        std::string x = a.substr(std::min(from, a.size()), 140);
        std::string y = b.substr(std::min(from, b.size()), 140);
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

struct Globals {
    difftest::Context &ctx;
    uint32_t saved[5];
    static constexpr uint32_t kRvas[5] = {0x10c9378, 0x10c937c, 0x10c9380, 0x10c9384, kGAppForHook};
    explicit Globals(difftest::Context &c) : ctx(c)
    {
        for (int i = 0; i < 5; ++i) saved[i] = *ctx.image.At<uint32_t>(kRvas[i]);
    }
    ~Globals()
    {
        for (int i = 0; i < 5; ++i) *ctx.image.At<uint32_t>(kRvas[i]) = saved[i];
    }
    uint32_t Get(uint32_t rva) const { return *ctx.image.At<uint32_t>(rva); }
    void Set(uint32_t rva, uint32_t v) const { *ctx.image.At<uint32_t>(rva) = v; }
    std::string Describe() const
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "fw=%x fw2=%x app=%x exit=%d hook=%x", Get(0x10c9378), Get(0x10c937c), Get(0x10c9380), static_cast<int>(Get(0x10c9384)),
                      Get(kGAppForHook));
        return buf;
    }
};

struct Script {
    uint8_t flag;       // the reporter's flag byte (+0x19b): zero runs RunFramework directly
    bool realInit, queryService, appServices, preInit, appBand;
    bool gzRun;
    int throwWhere;     // 0: nothing; 1: from QueryInterface; 2: from AddApplicationServicesHere
    int throwKind;      // 0: cError (code in throwCode); 1: cD3DException; 2: std::runtime_error; 3: access violation; 4: breakpoint-free custom SEH code;
                        // 5: LoudError (a C++ object whose destructor logs)
    int32_t throwCode;
    uint32_t d3dValue;
    int nestedKind;     // Report raises a second exception: 0 no, 1 an SEH code, 2 a LoudError
};
Script gScript;
Fw *gFw;

struct Obj {
    const void *const *vtable;
    int id;
    int refs;
};
// The application: a plain object followed by room for the embedded reporter and its flag byte.
struct BigApp : Obj {
    uint8_t rest[0x240 - sizeof(Obj)];
};
BigApp gApp;
Obj gFace{nullptr, 2, 1}, gCom{nullptr, 3, 1};
struct Fake {
    const void *const *vtable;
    int id;
};
Fake gServiceObject{nullptr, 7};
const void *gAppVt[8], *gFaceVt[24], *gComVt[16], *gFwVt[64], *gLockVt[8], *gD3dVt[2], *gServiceVt[64];

void Mark(const char *what, uint32_t arg = 0)
{
    Log("%s(%x) state=%u exit=%d tick=%u", what, arg, gFw->mState, *GameAddress<int32_t>(cGZFramework::kGlobalExitCode), gFw->mTickEnabled);
}

// A thrown object whose destruction shows in the log (who destroys it, and when, on the crash path).
struct LoudError {
    int id;
    ~LoudError() { Log("~LoudError(%d)", id); }
};

// A C++ object with a destructor in a frame between the throw and RunApp (the test is built /EHsc).
struct FrameGuard {
    const char *where;
    ~FrameGuard() { Log("~FrameGuard(%s)", where); }
};

[[noreturn]] void Throw()
{
    switch (gScript.throwKind) {
    case 5: throw LoudError{1};
    case 0: {
        nGZGraphic4::cError e;
        e.mVTable = nullptr;
        e.mCode = gScript.throwCode;
        throw e;
    }
    case 1: {
        nRZGraphic4::cD3DException e;
        e.mVTable = gD3dVt;
        throw e;
    }
    case 2: throw std::runtime_error("other");
    case 3: RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    default: RaiseException(0xE0001234, 0, 0, nullptr);
    }
    std::abort();
}

// An SEH frame between the throw and RunApp: its __finally runs in any unwind that reaches it.
void ThrowThroughFinally()
{
    __try {
        Throw();
    } __finally {
        Log("finally(%d)", AbnormalTermination() ? 1 : 0);
    }
}

uint32_t __fastcall ObjAddRef(Obj *self, void *)
{
    ++self->refs;
    Log("AddRef(%d)->%d", self->id, self->refs);
    return static_cast<uint32_t>(self->refs);
}
uint32_t __fastcall ObjRelease(Obj *self, void *)
{
    --self->refs;
    Log("Release(%d)->%d", self->id, self->refs);
    return static_cast<uint32_t>(self->refs);
}
int __fastcall ObjTrap(Obj *self, void *)
{
    Log("TRAP(%d)", self->id);
    return 0;
}
uint32_t __fastcall FakeAddRef(Fake *self, void *)
{
    Log("AddRef(%d)", self->id);
    return 100 + static_cast<uint32_t>(self->id);
}
uint32_t __fastcall FakeRelease(Fake *self, void *)
{
    Log("Release(%d)", self->id);
    return 200 + static_cast<uint32_t>(self->id);
}
uint32_t __fastcall FakeTrap(Fake *self, void *)
{
    Log("TRAP(%d)", self->id);
    return 0;
}
uint32_t __fastcall ServiceWindow(Fake *self, void *)
{
    Log("service.slot40(%d)", self->id);
    return 0x777;
}

uint8_t __fastcall AppQuery(Obj *, void *, uint32_t iid, void **out)
{
    Log("app.QueryInterface(%x)", iid);
    FrameGuard guard{"query"};
    if (gScript.throwWhere == 1) Throw();
    *out = &gFace;
    return 1;
}
uint8_t __fastcall FacePreInit(Obj *, void *) { Mark("face.PreFrameworkInit"); return gScript.preInit; }
void __fastcall FacePostInit(Obj *, void *) { Mark("face.PostFrameworkInit"); }
uint8_t __fastcall FaceGZRun(Obj *, void *) { Mark("face.GZRun"); return gScript.gzRun; }
void __fastcall FacePreShutdown(Obj *, void *) { Mark("face.PreFrameworkShutdown"); }
uint8_t __fastcall FaceDynamicLibs(Obj *, void *) { Mark("face.AddDynamicLibrariesHere"); return 1; }
uint8_t __fastcall FaceDirectors(Obj *, void *) { Mark("face.AddCOMDirectorsHere"); return 1; }
uint8_t __fastcall FaceServices(Obj *, void *)
{
    Mark("face.AddApplicationServicesHere");
    FrameGuard guard{"services"};
    if (gScript.throwWhere == 2) ThrowThroughFinally();
    return gScript.appServices;
}
uint8_t __fastcall ComRealInit(Obj *, void *) { Mark("com.RealInit"); return gScript.realInit; }
uint8_t __fastcall ComRealShutdown(Obj *, void *) { Mark("com.RealShutdown"); return 1; }
uint32_t __fastcall ComSetRunning(Obj *, void *, uint32_t flag) { Mark("com.SetServiceRunning", flag); return 7; }
uint8_t __fastcall ComQuery(Obj *, void *, uint32_t iid, void **out)
{
    Mark("com.QueryInterface", iid);
    if (gScript.queryService) *out = &gServiceObject;
    return gScript.queryService;
}
uint8_t __fastcall FwAddService(Fw *, void *, void *) { Mark("fw.AddSystemService"); return 1; }
void __fastcall FwToggle(Fw *, void *, uint32_t flag) { Mark("fw.ToggleTick", flag); }
void *__fastcall FwGetCom(Fw *, void *) { Mark("fw.GetCOMObject"); return &gCom; }
uint8_t __fastcall FwInitHigh(Fw *, void *) { Mark("fw.InitHighPriorityServices"); return 1; }
uint8_t __fastcall FwInitApp(Fw *, void *) { Mark("fw.InitAppBandServices"); return gScript.appBand; }
uint8_t __fastcall FwInitLow(Fw *, void *) { Mark("fw.InitLowPriorityServices"); return 1; }
uint8_t __fastcall FwShutLow(Fw *, void *) { Mark("fw.ShutdownLowPriorityServices"); return 1; }
uint8_t __fastcall FwShutApp(Fw *, void *) { Mark("fw.ShutdownAppBandServices"); return 1; }
uint8_t __fastcall FwShutHigh(Fw *, void *) { Mark("fw.ShutdownHighPriorityServices"); return 1; }
uint32_t __fastcall FwMainLoop(Fw *, void *) { Mark("fw.MainLoop"); return 0; }
uint32_t __fastcall FwRelease(Fw *, void *) { Mark("fw.Release"); return 1; }
uint32_t __fastcall FwAddRefSlot(Fw *, void *) { Mark("fw.AddRef"); return 5; }
uint32_t __fastcall FwQueryService(Fw *, void *, uint32_t id, uint32_t, void **out)
{
    Mark("fw.GetSystemService", id);
    *out = &gServiceObject;
    return 1;
}
int32_t __fastcall LockRec(void *, void *) { Mark("lock"); return 1; }
int32_t __fastcall UnlockRec(void *, void *) { Mark("unlock"); return 1; }
uint32_t __fastcall D3dValue(void *, void *)
{
    Log("d3d.slot0");
    return gScript.d3dValue;
}

struct ExitSignal {
    UINT code;
};

void WINAPI FakeExitProcess(UINT code)
{
    Log("ExitProcess(%u)", code);
    throw ExitSignal{code};
}

// Report (0x1bd69a, thiscall ret 4) as seen by both sides.
uint32_t __fastcall FakeReport(void *self, void *, EXCEPTION_POINTERS *info)
{
    // The record as the filter hands it over: code, flags (0x10 for a nested one), parameters (for a C++
    // exception the magic number and the throw information; the object's address depends on the stack),
    // where it was raised (both sides raise from the same test code), and the context's presence.
    const EXCEPTION_RECORD *r = info->ExceptionRecord;
    Log("Report(app+%x, code %08x, flags %x, params %u, p0 %x, p2 %p, at %p, context %d)",
        static_cast<unsigned>(static_cast<uint8_t *>(self) - reinterpret_cast<uint8_t *>(&gApp)), r->ExceptionCode, r->ExceptionFlags, r->NumberParameters,
        r->NumberParameters > 0 ? static_cast<unsigned>(r->ExceptionInformation[0]) : 0u,
        r->NumberParameters > 2 ? reinterpret_cast<void *>(r->ExceptionInformation[2]) : nullptr, r->ExceptionAddress, info->ContextRecord != nullptr);
    if (gScript.nestedKind == 1) {
        Log("Report raises");
        RaiseException(0xE0005678, 0, 0, nullptr);
    }
    if (gScript.nestedKind == 2) {
        Log("Report throws");
        throw LoudError{2};
    }
    Log("Report returns");
    return 1;
}

int WINAPI FakeMessageBox(HWND hwnd, const char *text, const char *caption, UINT flags)
{
    Log("MessageBoxA(%p, %s, %s, %x)", static_cast<void *>(hwnd), text, caption, flags);
    return 1;
}
HANDLE WINAPI FakeGetCurrentProcess()
{
    Log("GetCurrentProcess()");
    return reinterpret_cast<HANDLE>(0x4242);
}
BOOL WINAPI FakeTerminateProcess(HANDLE process, UINT code)
{
    Log("TerminateProcess(%p, %x)", static_cast<void *>(process), code);
    return TRUE;
}

constexpr uint32_t kValidateRegistration = 0x829c90;
int __cdecl AlwaysValid(void *) { return 1; }

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

constexpr uint32_t kStaticSeparator = 0x10fdffc, kStaticGuard = 0x10fe01c, kTlsIndex = 0x10df814;
void PrepareSeparator(difftest::Context &ctx)
{
    static bool done = false;
    if (done) return;
    done = true;
    auto *separator = ctx.image.At<cRZString>(kStaticSeparator);
    std::memset(separator, 0, sizeof(cRZString));
    separator->Construct("\xe2\x80\x8b");
    uint32_t *block = static_cast<uint32_t *>(reinterpret_cast<void **>(__readfsdword(0x2c))[*ctx.image.At<uint32_t>(kTlsIndex)]);
    *ctx.image.At<int32_t>(kStaticGuard) = static_cast<int32_t>(block[1]);
}

void PrepareMessages(difftest::Context &ctx)
{
    struct Entry {
        uint32_t rva;
        const char *text;
    };
    static const Entry entries[] = {{0x10fda54, "Caption \xe2\x80\x8b" "of the box"}, {0x10fda74, "D3D error %08x"}, {0x10fda94, "message one"},
                                    {0x10fdab4, "message two \xe2\x80\x8b!"}, {0x10fdad4, "message zero is a rather long one"}, {0x10fdaf4, "message other"}};
    for (const Entry &e : entries) {
        auto *s = ctx.image.At<cRZString>(e.rva);
        std::memset(s, 0, sizeof(cRZString));
        s->Construct(e.text);
    }
}

void BuildFw(difftest::Context &ctx, Fw *&out)
{
    static bool clock = false;
    if (!clock) ctx.Original<int(__cdecl *)()>(0x83a409)();
    clock = true;
    auto *mem = static_cast<uint8_t *>(::operator new(sizeof(Fw)));
    std::memset(mem, 0xCD, sizeof(Fw));
    ctx.Original<void *(__thiscall *)(void *)>(0xc2ccdb)(mem);
    out = reinterpret_cast<Fw *>(mem);
}

void WireObjects(uint8_t flag)
{
    for (auto &s : gAppVt) s = reinterpret_cast<void *>(&ObjTrap);
    gAppVt[0] = reinterpret_cast<void *>(&AppQuery);
    gAppVt[1] = reinterpret_cast<void *>(&ObjAddRef);
    gAppVt[2] = reinterpret_cast<void *>(&ObjRelease);
    for (auto &s : gFaceVt) s = reinterpret_cast<void *>(&ObjTrap);
    gFaceVt[1] = reinterpret_cast<void *>(&ObjAddRef);
    gFaceVt[2] = reinterpret_cast<void *>(&ObjRelease);
    gFaceVt[6] = reinterpret_cast<void *>(&FacePreInit);
    gFaceVt[7] = reinterpret_cast<void *>(&FacePostInit);
    gFaceVt[8] = reinterpret_cast<void *>(&FaceGZRun);
    gFaceVt[9] = reinterpret_cast<void *>(&FacePreShutdown);
    gFaceVt[11] = reinterpret_cast<void *>(&FaceDynamicLibs);
    gFaceVt[12] = reinterpret_cast<void *>(&FaceDirectors);
    gFaceVt[13] = reinterpret_cast<void *>(&FaceServices);
    for (auto &s : gComVt) s = reinterpret_cast<void *>(&ObjTrap);
    gComVt[0] = reinterpret_cast<void *>(&ComQuery);
    gComVt[9] = reinterpret_cast<void *>(&ComRealInit);
    gComVt[10] = reinterpret_cast<void *>(&ComRealShutdown);
    gComVt[11] = reinterpret_cast<void *>(&ComSetRunning);
    gD3dVt[0] = reinterpret_cast<void *>(&D3dValue);
    std::memset(&gApp, 0xA5, sizeof(gApp)); // the reporter's other bytes are not zero: only the flag byte may matter
    gApp.vtable = gAppVt;
    gApp.id = 1;
    gApp.refs = 1;
    reinterpret_cast<uint8_t *>(&gApp)[kReporterOffset + kFlagByteOffset] = flag;
    gFace = {gFaceVt, 2, 1};
    gCom = {gComVt, 3, 1};
    gServiceObject.vtable = gServiceVt;
    for (auto &s : gServiceVt) s = reinterpret_cast<void *>(&FakeTrap);
    gServiceVt[1] = reinterpret_cast<void *>(&FakeAddRef);
    gServiceVt[2] = reinterpret_cast<void *>(&FakeRelease);
    gServiceVt[40] = reinterpret_cast<void *>(&ServiceWindow);
}

void WireFw(Fw *fw)
{
    std::memcpy(gFwVt, fw->mVTable, sizeof(void *) * 64);
    gFwVt[1] = reinterpret_cast<void *>(&FwAddRefSlot);
    gFwVt[2] = reinterpret_cast<void *>(&FwRelease);
    gFwVt[3] = reinterpret_cast<void *>(&FwAddService);
    gFwVt[5] = reinterpret_cast<void *>(&FwQueryService);
    gFwVt[18] = reinterpret_cast<void *>(&FwToggle);
    gFwVt[23] = reinterpret_cast<void *>(&FwGetCom);
    gFwVt[41] = reinterpret_cast<void *>(&FwInitHigh);
    gFwVt[42] = reinterpret_cast<void *>(&FwInitApp);
    gFwVt[43] = reinterpret_cast<void *>(&FwInitLow);
    gFwVt[44] = reinterpret_cast<void *>(&FwShutLow);
    gFwVt[45] = reinterpret_cast<void *>(&FwShutApp);
    gFwVt[46] = reinterpret_cast<void *>(&FwShutHigh);
    gFwVt[48] = reinterpret_cast<void *>(&FwMainLoop);
    fw->mVTable = gFwVt;
    for (auto &s : gLockVt) s = reinterpret_cast<void *>(&ObjTrap);
    gLockVt[1] = reinterpret_cast<void *>(&LockRec);
    gLockVt[2] = reinterpret_cast<void *>(&UnlockRec);
    fw->mLock.mVTable = gLockVt;
    *reinterpret_cast<const void *const **>(fw->mCOM) = gComVt;
}

DWORD WINAPI FakeWideName(void *, wchar_t *buffer, DWORD size)
{
    const wchar_t name[] = L"C:\\Games\\Sims2EP9.exe";
    size_t n = std::min<size_t>(wcslen(name), size - 1);
    std::wmemcpy(buffer, name, n);
    buffer[n] = 0;
    return static_cast<DWORD>(n);
}
DWORD WINAPI FakeNarrowName(void *, char *buffer, DWORD)
{
    buffer[0] = 0;
    return 0;
}

cRZCmdLine *NewCmd(const char *text)
{
    auto *cmd = static_cast<cRZCmdLine *>(::operator new(sizeof(cRZCmdLine)));
    std::memset(cmd, 0xCD, sizeof(cRZCmdLine));
    cmd->Construct(text);
    return cmd;
}

void Replace(std::string &text, uint32_t address, const char *name)
{
    char hex[16];
    std::snprintf(hex, sizeof(hex), "%x", address);
    for (size_t at; (at = text.find(hex)) != std::string::npos;) text.replace(at, std::strlen(hex), name);
}

// An SEH exception that gets out of RunApp (the unguarded path lets everything through), caught here
// so it is a result rather than a crash of the test. C++ exceptions pass through to the callers.
using RunAppFn = int32_t(__cdecl *)(void *, void *, cRZCmdLine *);
DWORD gSehCode;
int SehFilter(DWORD code)
{
    if (code == 0xE06D7363) return EXCEPTION_CONTINUE_SEARCH;
    gSehCode = code;
    return EXCEPTION_EXECUTE_HANDLER;
}
bool CallProtected(RunAppFn fn, void *fw, void *app, cRZCmdLine *cmd, int32_t *result)
{
    gSehCode = 0;
    __try {
        *result = fn(fw, app, cmd);
    } __except (SehFilter(GetExceptionCode())) {
        return false;
    }
    return true;
}

std::string RunScenario(difftest::Context &ctx, Globals &globals, const Script &script, int side, const char *cmdText)
{
    gScript = script;
    gLog.clear();
    WireObjects(script.flag);
    Fw *fw;
    BuildFw(ctx, fw);
    gFw = fw;
    WireFw(fw);
    cRZCmdLine *cmd = NewCmd(cmdText);
    globals.Set(0x10c9378, 0);
    globals.Set(0x10c937c, 0);
    globals.Set(0x10c9380, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gFace)));
    globals.Set(0x10c9384, 0);
    globals.Set(kGAppForHook, 0);
    std::string how = "returned";
    int32_t result = 0x5eed;
    try {
        RunAppFn fn = side == 0 ? ctx.Original<RunAppFn>(kRunApp) : &winmain::RunApp;
        if (!CallProtected(fn, fw, &gApp, cmd, &result)) {
            char b[40];
            std::snprintf(b, sizeof(b), "seh %08lx", gSehCode);
            how = b;
        }
    } catch (const ExitSignal &e) {
        how = "exit " + std::to_string(e.code);
    } catch (const nGZGraphic4::cError &e) {
        how = "cError " + std::to_string(e.mCode);
    } catch (const nRZGraphic4::cD3DException &) {
        how = "d3d";
    } catch (const std::runtime_error &) {
        how = "runtime_error";
    } catch (...) {
        how = "unknown C++ exception";
    }
    // Compared as they are, nothing normalised (the unwind on the crash path included: see the top of the file).
    std::string d = globals.Describe();
    Replace(d, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fw)), "FW");
    Replace(d, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gApp)), "APP");
    Replace(d, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gFace)), "FACE");
    char tail[200];
    std::snprintf(tail, sizeof(tail), "%s result=%d state=%u refs app=%d face=%d com=%d\n", how.c_str(), result, fw->mState, gApp.refs, gFace.refs, gCom.refs);
    std::string joined = Joined();
    std::string out = d + "\n" + tail + joined;
    reinterpret_cast<cRZCmdLine *>(fw->mCmdLine)->Destruct();
    cmd->Destruct();
    ::operator delete(cmd);
    return out;
}

} // namespace

DIFF_TEST(WinMain_RunApp, kRunApp)
{
    Globals globals(ctx);
    Imports imports(ctx);
    uint8_t savedResolved = *ctx.image.At<uint8_t>(kModuleFileNameWResolved);
    void *savedCached = *ctx.image.At<void *>(kModuleFileNameWPointer);
    *ctx.image.At<uint8_t>(kModuleFileNameWResolved) = 1;
    *ctx.image.At<void *>(kModuleFileNameWPointer) = reinterpret_cast<void *>(&FakeWideName);
    imports.Set(kIatGetModuleFileNameA, reinterpret_cast<void *>(&FakeNarrowName));
    imports.Set(kIatMessageBoxA, reinterpret_cast<void *>(&FakeMessageBox));
    imports.Set(kIatGetCurrentProcess, reinterpret_cast<void *>(&FakeGetCurrentProcess));
    imports.Set(kIatTerminateProcess, reinterpret_cast<void *>(&FakeTerminateProcess));
    imports.Set(kIatExitProcess, reinterpret_cast<void *>(&FakeExitProcess));
    PrepareMessages(ctx);
    PrepareSeparator(ctx);
    CodeRedirect redirect(ctx, kReportRva, reinterpret_cast<void *>(&FakeReport));
    // The original's SEH handler (_except_handler3, 0x826a30) first validates the registration with
    // 0x829c90, which checks the scope table lies in a section of a loaded image (VirtualQuery says
    // MEM_IMAGE, the PE sections). The harness maps the image without that, the validation fails and
    // the exception dispatch ends the process. The check itself is not under test: it says "valid".
    CodeRedirect validation(ctx, kValidateRegistration, reinterpret_cast<void *>(&AlwaysValid));

    std::mt19937 rng(0xc2d23b);
    static const char *const kCmds[] = {"", "-w", "/nosound -lang:en", "Sims2EP9.exe a b"};
    static const uint8_t kFlags[] = {0, 1, 2, 0x80, 0xff};
    const int kBits[] = {0, 5, 31};
    // Paths the comparison must have reached (counted on the original's output).
    enum { kCaughtError, kCaughtD3d, kUnguardedCpp, kUnguardedSeh, kCrashCpp, kCrashSeh, kCrashObjectDestroyed, kCrashFinallyNoGuard, kNestedSeh,
           kNestedCpp, kPaths };
    static const char *const kPathNames[kPaths] = {"cError caught",
                                                   "D3D caught",
                                                   "C++ exception out of the unguarded path",
                                                   "SEH exception out of the unguarded path",
                                                   "C++ exception to the filter",
                                                   "SEH exception to the filter",
                                                   "thrown object destroyed before ExitProcess",
                                                   "__finally run but no C++ destructor in the crash unwind",
                                                   "nested SEH exception from the reporter",
                                                   "nested C++ exception from the reporter"};
    int reached[kPaths] = {};
    for (uint8_t flag : kFlags) {
        for (int bits : kBits) {
            for (int variant = 0; variant < 26; ++variant) {
                Script s{};
                s.flag = flag;
                s.realInit = bits & 1;
                s.queryService = bits & 2;
                s.appServices = bits & 4;
                s.preInit = bits & 8;
                s.appBand = bits & 16;
                s.gzRun = (variant & 1) != 0;
                // variant / 2: 0 -> nothing, then 1..12: each of the six exceptions at each of the two places.
                int what = variant / 2;
                s.throwWhere = what == 0 ? 0 : 1 + ((what - 1) & 1);
                s.throwKind = what == 0 ? 0 : (what - 1) / 2;
                s.throwCode = static_cast<int32_t>(rng() % 5);
                s.d3dValue = static_cast<uint32_t>(rng());
                s.nestedKind = static_cast<int>(rng() % 3);
                const char *cmd = kCmds[rng() % 4];
                std::string results[2];
                for (int side = 0; side < 2; ++side) {
                    winmain::sRunAppReportOverride = side == 1 ? reinterpret_cast<winmain::RunAppReportFn>(&FakeReport) : nullptr;
                    results[side] = RunScenario(ctx, globals, s, side, cmd);
                }
                winmain::sRunAppReportOverride = nullptr;
                char label[128];
                std::snprintf(label, sizeof(label), "flag %02x bits %d variant %d throw %d/%d code %d nested %d", flag, bits, variant, s.throwWhere, s.throwKind, s.throwCode,
                              s.nestedKind);
                ExpectSame(ctx, label, results[0], results[1]);

                const std::string &o = results[0];
                bool exited = o.find("exit 0 result") != std::string::npos;
                bool cpp = s.throwKind == 2 || s.throwKind == 5;
                if (s.throwWhere) {
                    bool terminated = o.find("TerminateProcess(") != std::string::npos;
                    if (flag && s.throwKind == 0 && terminated) ++reached[kCaughtError];
                    if (flag && s.throwKind == 1 && terminated) ++reached[kCaughtD3d];
                    if (!flag && cpp && !exited) ++reached[kUnguardedCpp];
                    if (!flag && !cpp && s.throwKind >= 3 && o.find("seh ") != std::string::npos) ++reached[kUnguardedSeh];
                    if (flag && exited && cpp) ++reached[kCrashCpp];
                    if (flag && exited && !cpp) ++reached[kCrashSeh];
                    size_t exitAt = o.find("ExitProcess(");
                    if (flag && exited && s.throwKind == 5 && s.nestedKind == 0 && o.find("~LoudError(1)") < exitAt) ++reached[kCrashObjectDestroyed];
                    if (flag && exited && cpp && s.throwWhere == 2 && o.find("finally(1)") < exitAt && o.find("~FrameGuard(services)") == std::string::npos)
                        ++reached[kCrashFinallyNoGuard];
                    if (flag && exited && o.find("Report raises") != std::string::npos) ++reached[kNestedSeh];
                    if (flag && exited && o.find("Report throws") != std::string::npos && o.find("~LoudError(2)") < exitAt) ++reached[kNestedCpp];
                }
            }
        }
    }
    for (int i = 0; i < kPaths; ++i) {
        ++ctx.cases;
        if (!reached[i]) ctx.Fail("path never reached: %s", kPathNames[i]);
    }
    *ctx.image.At<uint8_t>(kModuleFileNameWResolved) = savedResolved;
    *ctx.image.At<void *>(kModuleFileNameWPointer) = savedCached;
}
