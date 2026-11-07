// Differential test for the image's start: `entry` 0x8248a3 (the /GS cookie, then
// __scrt_common_main_seh 0x824727) against winmainstart::RunImageStart.
//
// Every callee of the original is a CRT routine that works on the image CRT's own state (or WinMain),
// and the harness maps the image without initialising that CRT, so each one is a recorder on both
// sides: the original's entries are redirected to it, ours is bound to it through ImageCrt. What is
// compared is the sequencer itself: which steps run, in which order, with which arguments, what it
// does with each answer (only the low byte of the bool answers counts; the recorders put junk in the
// upper bytes), the native start-up state variable (0x10df7e8, the image's), the result, and how the
// run ends (returned, a non-returning exit, or an exception passed on).
//
// The dynamic-TLS callback slots (0x110a388 init, 0x110a384 destructor) are the image's own; the
// original reads them through two six-byte getters, ours through the binding. In this game both slots
// are zero (in .data); the scenarios also fill them with recorders to exercise both branches.
#define NOMINMAX
#include "app/WinMainStart.h"
#include "difftest.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kEntry = 0x8248a3, kCommonMain = 0x824727;
constexpr uint32_t kSecurityInitCookie = 0x8258a9, kInitializeCrt = 0x824af8, kAcquireLock = 0x824ac6, kReleaseLock = 0x824c4c;
constexpr uint32_t kInittermE = 0x84353f, kInitterm = 0x843514, kIsNonwritable = 0x824bb8, kRegisterTlsDtor = 0x82f699;
constexpr uint32_t kShowWindowMode = 0x825a71, kNarrowCommandLine = 0x8434c1, kWinMain = 0xc2b87c, kIsManagedApp = 0x825aa2;
constexpr uint32_t kCexit = 0x82f674, kUninitializeCrt = 0x824c69, kExit = 0x82f6bf, kExitNow = 0x82f683, kCExitQuick = 0x82f665;
constexpr uint32_t kFastFail = 0x82595c, kSehFilterExe = 0x842b14;
constexpr uint32_t kNativeStartupState = 0x10df7e8, kTlsInitSlot = 0x110a388, kTlsDtorSlot = 0x110a384;
constexpr uint32_t kXiFirst = 0xd9b3ac, kXiLast = 0xd9b3d8, kXcFirst = 0xd9a724, kXcLast = 0xd9b3a8;
constexpr unsigned long kCppExceptionCode = 0xE06D7363;

struct Stop {}; // a non-returning CRT exit, as seen by the test

std::vector<std::string> gLog;
uint32_t gBase = 0;

void Log(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    gLog.emplace_back(buf);
}

uint32_t Rva(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - gBase); }

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
    uint32_t initializeCrt;      // eax of __scrt_initialize_crt (only al counts)
    uint32_t nested;             // eax of the lock
    int32_t startState;          // the native start-up state before the run
    int32_t xiResult;
    bool tlsInit, tlsDtor;       // fill the slots
    uint32_t initNonwritable;    // eax of the section check, per slot
    uint32_t dtorNonwritable;
    uint32_t show;               // eax of the show-mode getter (only ax counts)
    int winMainOutcome;          // 0 return, 1 raise an SEH exception, 2 throw Stop (a nested exit)
    int32_t winMainResult;
    unsigned long raised;        // the code raised by WinMain
    uint32_t managed;            // eax of __scrt_is_managed_app
    int32_t filterAnswer;        // what _seh_filter_exe answers for a non-test exception
};
Script gScript;
char gCommandLine[] = "Sims2EP9.exe -w";

// ---- recorders ----

void __cdecl RecCookie() { Log("__security_init_cookie"); }
uint32_t __cdecl RecInitializeCrt(int type)
{
    Log("__scrt_initialize_crt(%d)", type);
    return gScript.initializeCrt;
}
uint32_t __cdecl RecAcquireLock()
{
    Log("acquire startup lock");
    return gScript.nested;
}
void __cdecl RecReleaseLock(uint32_t nested) { Log("release startup lock(%u)", nested & 0xff); }
int __cdecl RecInittermE(void *first, void *last)
{
    Log("_initterm_e(%x, %x)", Rva(first), Rva(last));
    return gScript.xiResult;
}
void __cdecl RecInitterm(void *first, void *last) { Log("_initterm(%x, %x)", Rva(first), Rva(last)); }
void __stdcall RecTlsInit(void *module, unsigned long reason, void *reserved) { Log("dynamic TLS init(%p, %lu, %p)", module, reason, reserved); }
void __stdcall RecTlsDtor(void *, unsigned long, void *) { Log("dynamic TLS destructor called"); }
uint32_t __cdecl RecIsNonwritable(const void *slot)
{
    uint32_t rva = Rva(slot);
    Log("is nonwritable(%x)", rva);
    return rva == kTlsInitSlot ? gScript.initNonwritable : gScript.dtorNonwritable;
}
void __cdecl RecRegisterTlsDtor(void *callback) { Log("register TLS destructor(%s)", callback == reinterpret_cast<void *>(&RecTlsDtor) ? "the slot's" : "other"); }
uint32_t __cdecl RecShowWindowMode()
{
    Log("show window mode");
    return gScript.show;
}
char *__cdecl RecNarrowCommandLine()
{
    Log("narrow command line");
    return gCommandLine;
}
int __stdcall RecWinMain(void *instance, void *previous, char *commandLine, int show)
{
    Log("WinMain(%s, %p, %s, %08x)", Rva(instance) == 0 ? "image base" : "other", previous, commandLine == gCommandLine ? "the CRT's" : "other", static_cast<uint32_t>(show));
    if (gScript.winMainOutcome == 1) RaiseException(gScript.raised, 0, 0, nullptr);
    if (gScript.winMainOutcome == 2) throw Stop{};
    return gScript.winMainResult;
}
uint32_t __cdecl RecIsManagedApp()
{
    Log("is managed app");
    return gScript.managed;
}
void __cdecl RecCexit() { Log("_cexit"); }
uint32_t __cdecl RecUninitializeCrt(uint32_t terminating, uint32_t fromExit)
{
    Log("__scrt_uninitialize_crt(%u, %u)", terminating & 0xff, fromExit & 0xff);
    return 0xdead01;
}
void __cdecl RecExit(int code)
{
    Log("exit(%d)", code);
    throw Stop{};
}
void __cdecl RecExitNow(int code)
{
    Log("_exit(%08x)", static_cast<uint32_t>(code));
    throw Stop{};
}
void __cdecl RecExitQuick() { Log("_c_exit"); }
void __cdecl RecFastFail(unsigned code)
{
    Log("__scrt_fastfail(%u)", code);
    throw Stop{};
}
int __cdecl RecSehFilter(unsigned long code, EXCEPTION_POINTERS *info)
{
    Log("_seh_filter_exe(%08lx, record code %08lx)", code, info->ExceptionRecord->ExceptionCode);
    // The test's own Stop (a C++ exception standing for an exit that does not return) must reach the test.
    if (code == kCppExceptionCode) return EXCEPTION_CONTINUE_SEARCH;
    return gScript.filterAnswer;
}

winmainstart::ImageCrt BindRecorders(difftest::Context &ctx)
{
    winmainstart::ImageCrt crt{};
    crt.initSecurityCookie = &RecCookie;
    crt.initializeCrt = reinterpret_cast<bool(__cdecl *)(int)>(&RecInitializeCrt);
    crt.acquireStartupLock = reinterpret_cast<bool(__cdecl *)()>(&RecAcquireLock);
    crt.releaseStartupLock = reinterpret_cast<void(__cdecl *)(bool)>(&RecReleaseLock);
    crt.nativeStartupState = ctx.image.At<int>(kNativeStartupState);
    crt.xiFirst = ctx.image.At<winmainstart::CheckedInitialiser>(kXiFirst);
    crt.xiLast = ctx.image.At<winmainstart::CheckedInitialiser>(kXiLast);
    crt.xcFirst = ctx.image.At<winmainstart::Initialiser>(kXcFirst);
    crt.xcLast = ctx.image.At<winmainstart::Initialiser>(kXcLast);
    crt.runCheckedInitialisers = reinterpret_cast<int(__cdecl *)(winmainstart::CheckedInitialiser *, winmainstart::CheckedInitialiser *)>(&RecInittermE);
    crt.runInitialisers = reinterpret_cast<void(__cdecl *)(winmainstart::Initialiser *, winmainstart::Initialiser *)>(&RecInitterm);
    crt.dynamicTlsInit = ctx.image.At<winmainstart::TlsCallback>(kTlsInitSlot);
    crt.dynamicTlsDtor = ctx.image.At<winmainstart::TlsCallback>(kTlsDtorSlot);
    crt.isNonwritableInImage = reinterpret_cast<bool(__cdecl *)(const void *)>(&RecIsNonwritable);
    crt.registerTlsDtor = reinterpret_cast<void(__cdecl *)(winmainstart::TlsCallback)>(&RecRegisterTlsDtor);
    crt.showWindowMode = reinterpret_cast<uint16_t(__cdecl *)()>(&RecShowWindowMode);
    crt.narrowCommandLine = &RecNarrowCommandLine;
    crt.instance = ctx.image.At<void>(0);
    crt.winMain = &RecWinMain;
    crt.isManagedApp = reinterpret_cast<bool(__cdecl *)()>(&RecIsManagedApp);
    crt.cexit = &RecCexit;
    crt.uninitializeCrt = reinterpret_cast<bool(__cdecl *)(bool, bool)>(&RecUninitializeCrt);
    crt.exit = &RecExit;
    crt.exitNow = &RecExitNow;
    crt.exitQuick = &RecExitQuick;
    crt.fastFail = &RecFastFail;
    crt.exceptionFilter = &RecSehFilter;
    return crt;
}

// ---- one run ----

int TestFilter(unsigned long code, unsigned long *seen)
{
    if (code == kCppExceptionCode) return EXCEPTION_CONTINUE_SEARCH; // Stop: caught by RunScenario
    *seen = code;
    return EXCEPTION_EXECUTE_HANDLER;
}

// No C++ objects here: __try needs a frame without unwinding.
int CallSide(difftest::Context *ctx, const winmainstart::ImageCrt *crt, int side, unsigned long *escaped)
{
    __try {
        if (side == 0) return ctx->Original<int(__cdecl *)()>(kEntry)();
        return winmainstart::RunImageStart(*crt);
    } __except (TestFilter(GetExceptionCode(), escaped)) {
        return -77777;
    }
}

std::string RunScenario(difftest::Context &ctx, const winmainstart::ImageCrt &crt, int side)
{
    *ctx.image.At<int32_t>(kNativeStartupState) = gScript.startState;
    *ctx.image.At<void *>(kTlsInitSlot) = gScript.tlsInit ? reinterpret_cast<void *>(&RecTlsInit) : nullptr;
    *ctx.image.At<void *>(kTlsDtorSlot) = gScript.tlsDtor ? reinterpret_cast<void *>(&RecTlsDtor) : nullptr;
    gLog.clear();
    unsigned long escaped = 0;
    int result = -12345;
    bool stopped = false;
    try {
        result = CallSide(&ctx, &crt, side, &escaped);
    } catch (const Stop &) {
        stopped = true;
    }
    std::string out;
    for (const std::string &s : gLog) out += s + "\n";
    char tail[160];
    std::snprintf(tail, sizeof(tail), "%s result %d escaped %08lx state %d", stopped ? "stopped" : "returned", result, escaped,
                  *ctx.image.At<int32_t>(kNativeStartupState));
    return out + tail;
}

void ExpectSame(difftest::Context &ctx, const char *what, const std::string &a, const std::string &b)
{
    ++ctx.cases;
    if (a != b) {
        size_t at = 0;
        while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
        size_t from = at > 60 ? at - 60 : 0;
        std::string x = a.substr(from, 140), y = b.substr(from, 140);
        for (char &c : x) if (c == '\n') c = '|';
        for (char &c : y) if (c == '\n') c = '|';
        ctx.Fail("%s:\n  original: %s\n  ours:     %s", what, x.c_str(), y.c_str());
    }
}

} // namespace

// The original's own entry (kEntry) and its common main (kCommonMain) are run together: entry jumps
// into 0x824727, which holds every step compared here.
DIFF_TEST(WinMainStart_Entry, kEntry)
{
    static_assert(kCommonMain == 0x824727, "entry's jump target");
    gBase = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx.image.At<void>(0)));
    const int32_t savedState = *ctx.image.At<int32_t>(kNativeStartupState);
    void *const savedSlots[2] = {*ctx.image.At<void *>(kTlsInitSlot), *ctx.image.At<void *>(kTlsDtorSlot)};

    CodeRedirect r01(ctx, kSecurityInitCookie, reinterpret_cast<void *>(&RecCookie));
    CodeRedirect r02(ctx, kInitializeCrt, reinterpret_cast<void *>(&RecInitializeCrt));
    CodeRedirect r03(ctx, kAcquireLock, reinterpret_cast<void *>(&RecAcquireLock));
    CodeRedirect r04(ctx, kReleaseLock, reinterpret_cast<void *>(&RecReleaseLock));
    CodeRedirect r05(ctx, kInittermE, reinterpret_cast<void *>(&RecInittermE));
    CodeRedirect r06(ctx, kInitterm, reinterpret_cast<void *>(&RecInitterm));
    CodeRedirect r07(ctx, kIsNonwritable, reinterpret_cast<void *>(&RecIsNonwritable));
    CodeRedirect r08(ctx, kRegisterTlsDtor, reinterpret_cast<void *>(&RecRegisterTlsDtor));
    CodeRedirect r09(ctx, kShowWindowMode, reinterpret_cast<void *>(&RecShowWindowMode));
    CodeRedirect r10(ctx, kNarrowCommandLine, reinterpret_cast<void *>(&RecNarrowCommandLine));
    CodeRedirect r11(ctx, kWinMain, reinterpret_cast<void *>(&RecWinMain));
    CodeRedirect r12(ctx, kIsManagedApp, reinterpret_cast<void *>(&RecIsManagedApp));
    CodeRedirect r13(ctx, kCexit, reinterpret_cast<void *>(&RecCexit));
    CodeRedirect r14(ctx, kUninitializeCrt, reinterpret_cast<void *>(&RecUninitializeCrt));
    CodeRedirect r15(ctx, kExit, reinterpret_cast<void *>(&RecExit));
    CodeRedirect r16(ctx, kExitNow, reinterpret_cast<void *>(&RecExitNow));
    CodeRedirect r17(ctx, kCExitQuick, reinterpret_cast<void *>(&RecExitQuick));
    CodeRedirect r18(ctx, kFastFail, reinterpret_cast<void *>(&RecFastFail));
    CodeRedirect r19(ctx, kSehFilterExe, reinterpret_cast<void *>(&RecSehFilter));
    const winmainstart::ImageCrt crt = BindRecorders(ctx);

    std::mt19937 rng(0x8248a3);
    auto junk = [&] { return static_cast<uint32_t>(rng()) & 0xffffff00u; }; // upper bytes the callers must ignore
    auto boolean = [&](int percentTrue) { return junk() | ((rng() % 100) < static_cast<uint32_t>(percentTrue) ? 1u : 0u); };
    static const int32_t kStates[] = {0, 0, 0, 0, 1, 2, 2, 5};
    static const unsigned long kRaised[] = {0xC0000005, 0xC0000094, 0x40010005, 0xE0001234};
    static const char *const kPathMarks[] = {"__scrt_fastfail(7)\nstopped",   // the CRT failed to start (outside the try)
                                             "__scrt_fastfail(7)\n_seh_filter_exe", // state 1: inside the try
                                             "_initterm(",                     // the initialisers ran
                                             "result 255",                     // an XI entry failed
                                             "release startup lock(1)",        // nested lock handed back
                                             "dynamic TLS init(",              "register TLS destructor(the slot's)",
                                             "\nexit(",                        // native exit after WinMain
                                             "_exit(",                         // native exit from the handler
                                             "_cexit\n__scrt_uninitialize_crt", // managed return, first start
                                             "is managed app\n__scrt_uninitialize_crt", // managed, already started
                                             "_c_exit",                        // managed handler path
                                             "escaped c",                      // an exception passed on
                                             "state 2"};
    constexpr size_t kPaths = sizeof(kPathMarks) / sizeof(kPathMarks[0]);
    int reached[kPaths] = {};
    for (int i = 0; i < 600; ++i) {
        Script &s = gScript;
        s.initializeCrt = (i % 17 == 3) ? junk() : boolean(100);
        s.nested = boolean(30);
        s.startState = kStates[rng() % 8];
        s.xiResult = (i % 9 == 4) ? static_cast<int32_t>(rng() % 3) - 1 | 1 : 0;
        s.tlsInit = rng() % 3 == 0;
        s.tlsDtor = rng() % 3 == 0;
        s.initNonwritable = boolean(50);
        s.dtorNonwritable = boolean(50);
        s.show = static_cast<uint32_t>(rng());
        s.winMainOutcome = static_cast<int>(rng() % 5) < 3 ? 0 : static_cast<int>(rng() % 2) + 1;
        s.winMainResult = static_cast<int32_t>(rng() % 7) - 2;
        s.raised = kRaised[rng() % 4];
        s.managed = boolean(i % 4 == 1 ? 100 : 0);
        s.filterAnswer = (rng() % 2) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
        std::string results[2];
        for (int side = 0; side < 2; ++side) results[side] = RunScenario(ctx, crt, side);
        char label[128];
        std::snprintf(label, sizeof(label), "scenario %d (state %d, xi %d, winmain %d, managed %u)", i, s.startState, s.xiResult, s.winMainOutcome,
                      s.managed & 0xff);
        ExpectSame(ctx, label, results[0], results[1]);
        for (size_t k = 0; k < kPaths; ++k)
            if (results[0].find(kPathMarks[k]) != std::string::npos) ++reached[k];
    }
    // Every path of the original was taken at least once (else the scenarios have a gap).
    for (size_t k = 0; k < kPaths; ++k)
        if (reached[k] == 0) ctx.Fail("no scenario reached '%s'", kPathMarks[k]);

    *ctx.image.At<int32_t>(kNativeStartupState) = savedState;
    *ctx.image.At<void *>(kTlsInitSlot) = savedSlots[0];
    *ctx.image.At<void *>(kTlsDtorSlot) = savedSlots[1];
}
