// Contract tests for the terminate and purecall handlers (docs/contracts/terminate-handlers.md):
//   0xcac170  the handler (ours: winmaininfra::ReportAndReturn)
//   0xcac1f0  the installer (ours: InstallTerminateAndPurecallHandlers with the image CRT seam)
//
// Both are compared with the original on everything the contract keeps. The operating-system entry
// points are faked in the original's import table; ours reads the same fakes through OsTable().
// The handler's SEH needs the harness fixture used by tests/winmain_run_app.cpp: the CRT's SEH
// registration validator (0x829c90) is redirected to "valid" because the harness maps the image
// without the module table it checks.
#define NOMINMAX
#include "app/WinMainInfra.h"
#include "difftest.h"
#include "game/game.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kHandler = 0xcac170, kInstaller = 0xcac1f0;
constexpr uint32_t kIatSetUnhandledExceptionFilter = 0xd9a1b4, kIatUnhandledExceptionFilter = 0xd9a2b0, kIatGetLastError = 0xd9a118, kIatSetLastError = 0xd9a140;
constexpr uint32_t kValidateRegistration = 0x829c90;
constexpr uint32_t kFlsWrapper = 0x8480bf;           // the CRT's FlsGetValue(index) (stdcall, 1 argument)
constexpr uint32_t kFlsIndex = 0x104f89c, kPurecallVariable = 0x10dfb80;
constexpr size_t kPtdSize = 0x364, kPtdTerminate = 0xc;

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

void ExpectSame(difftest::Context &ctx, const char *what, const std::string &expected, const std::string &actual)
{
    ++ctx.cases;
    if (expected != actual) {
        size_t at = 0;
        while (at < expected.size() && at < actual.size() && expected[at] == actual[at]) ++at;
        size_t from = at > 60 ? at - 60 : 0;
        std::string x = expected.substr(from, 170), y = actual.substr(from, 170);
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

int __cdecl AlwaysValid(void *) { return 1; }

// ---- 0xcac170: the handler ----

LPTOP_LEVEL_EXCEPTION_FILTER gCurrentFilter = nullptr; // what SetUnhandledExceptionFilter hands back
LONG gAnswer = EXCEPTION_EXECUTE_HANDLER;              // what the filters answer

const char *FilterName(void *f);

LONG WINAPI FakeTopFilter(EXCEPTION_POINTERS *info);
LONG WINAPI FakeSystemFilter(EXCEPTION_POINTERS *info);

const char *FilterName(void *f)
{
    if (f == nullptr) return "null";
    if (f == reinterpret_cast<void *>(&FakeTopFilter)) return "top";
    if (f == reinterpret_cast<void *>(&FakeSystemFilter)) return "system";
    return "other";
}

LPTOP_LEVEL_EXCEPTION_FILTER WINAPI FakeSetFilter(LPTOP_LEVEL_EXCEPTION_FILTER filter)
{
    Log("SetUnhandledExceptionFilter(%s)", FilterName(reinterpret_cast<void *>(filter)));
    return gCurrentFilter;
}

LONG Describe(const char *who, EXCEPTION_POINTERS *info)
{
    const EXCEPTION_RECORD *r = info->ExceptionRecord;
    Log("%s filter: code %08x flags %x parameters %u [%u, %x]", who, r->ExceptionCode, r->ExceptionFlags, r->NumberParameters,
        r->NumberParameters > 0 ? static_cast<unsigned>(r->ExceptionInformation[0]) : 0u, r->NumberParameters > 1 ? static_cast<unsigned>(r->ExceptionInformation[1]) : 0u);
    return gAnswer;
}
LONG WINAPI FakeTopFilter(EXCEPTION_POINTERS *info) { return Describe("top", info); }
LONG WINAPI FakeSystemFilter(EXCEPTION_POINTERS *info) { return Describe("system", info); }

// No C++ objects here (SEH): reports whether the call returned, else the exception code.
bool CallGuarded(void(__cdecl *fn)(), DWORD *code)
{
    __try {
        fn();
        return true;
    } __except (*code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string RunHandler(void(__cdecl *fn)())
{
    gLog.clear();
    DWORD code = 0;
    bool returned = CallGuarded(fn, &code);
    char tail[64];
    if (returned) std::snprintf(tail, sizeof(tail), "returned");
    else std::snprintf(tail, sizeof(tail), "exception %08x reached the caller", code);
    return Joined() + tail;
}

} // namespace

DIFF_TEST(WinMainInfra_ReportAndReturn, kHandler)
{
    Imports imports(ctx);
    imports.Set(kIatSetUnhandledExceptionFilter, reinterpret_cast<void *>(&FakeSetFilter));
    imports.Set(kIatUnhandledExceptionFilter, reinterpret_cast<void *>(&FakeSystemFilter));
    CodeRedirect validation(ctx, kValidateRegistration, reinterpret_cast<void *>(&AlwaysValid));

    winmaininfra::Os &os = winmaininfra::OsTable();
    const winmaininfra::Os saved = os;
    os.SetUnhandledExceptionFilter = &FakeSetFilter;
    os.UnhandledExceptionFilter = &FakeSystemFilter;

    for (int topInstalled = 0; topInstalled < 2; ++topInstalled) {
        for (LONG answer : {EXCEPTION_EXECUTE_HANDLER, EXCEPTION_CONTINUE_SEARCH}) {
            for (int repeat = 0; repeat < 3; ++repeat) {
                gCurrentFilter = topInstalled ? &FakeTopFilter : nullptr;
                gAnswer = answer;
                std::string original = RunHandler(ctx.Original<void(__cdecl *)()>(kHandler));
                std::string ours = RunHandler(&winmaininfra::ReportAndReturn);
                char label[96];
                std::snprintf(label, sizeof(label), "top-level filter %s, answer %d", topInstalled ? "installed" : "none", answer);
                ExpectSame(ctx, label, original, ours);
            }
        }
    }
    // The shape of what happened, once, so a pair of equal but wrong logs cannot pass: with a filter
    // installed it is looked up with 0, put back, and called with the access violation at address 0.
    gCurrentFilter = &FakeTopFilter;
    gAnswer = EXCEPTION_EXECUTE_HANDLER;
    ExpectSame(ctx, "log with a filter installed",
               "SetUnhandledExceptionFilter(null)\nSetUnhandledExceptionFilter(top)\ntop filter: code c0000005 flags 0 parameters 2 [0, 0]\nreturned",
               RunHandler(&winmaininfra::ReportAndReturn));
    gCurrentFilter = nullptr;
    ExpectSame(ctx, "log without a filter installed",
               "SetUnhandledExceptionFilter(null)\nsystem filter: code c0000005 flags 0 parameters 2 [0, 0]\nreturned",
               RunHandler(&winmaininfra::ReportAndReturn));
    os = saved;
}

namespace {

// ---- 0xcac1f0: the installer ----

DWORD gLastError = 0;

DWORD WINAPI FakeGetLastError()
{
    Log("GetLastError()");
    return gLastError;
}
void WINAPI FakeSetLastError(DWORD error)
{
    Log("SetLastError(%u)", error);
}
void *WINAPI FakeFls(DWORD index)
{
    Log("FlsGetValue(%u)", index);
    return FlsGetValue(index);
}

} // namespace

DIFF_TEST(WinMainInfra_InstallHandlers, kInstaller)
{
    Imports imports(ctx);
    imports.Set(kIatGetLastError, reinterpret_cast<void *>(&FakeGetLastError));
    imports.Set(kIatSetLastError, reinterpret_cast<void *>(&FakeSetLastError));
    // The CRT's own FlsGetValue wrapper (library code that needs the module's CRT start-up) is
    // redirected to the real FlsGetValue; the per-thread data is a block of our own in the slot.
    CodeRedirect fls(ctx, kFlsWrapper, reinterpret_cast<void *>(&FakeFls));

    winmaininfra::Os &os = winmaininfra::OsTable();
    const winmaininfra::Os saved = os;
    os.GetLastError = &FakeGetLastError;
    os.SetLastError = &FakeSetLastError;
    os.FlsGetValue = &FakeFls;

    DWORD index = FlsAlloc(nullptr);
    uint32_t savedIndex = *ctx.image.At<uint32_t>(kFlsIndex), savedPurecall = *ctx.image.At<uint32_t>(kPurecallVariable);
    *ctx.image.At<uint32_t>(kFlsIndex) = index;
    uint8_t *data = static_cast<uint8_t *>(std::calloc(1, kPtdSize));

    const uint32_t originalHandler = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx.image.At<void>(kHandler)));
    const uint32_t ourHandler = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&winmaininfra::ReportAndReturn));
    for (int variant = 0; variant < 12; ++variant) {
        FlsSetValue(index, data);
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            for (size_t i = 0; i < kPtdSize; ++i) data[i] = static_cast<uint8_t>(i * 7 + variant);
            *reinterpret_cast<uint32_t *>(data + kPtdTerminate) = variant % 3 == 0 ? 0 : 0x11110000u + variant; // the old handler: unset or some other
            *ctx.image.At<uint32_t>(kPurecallVariable) = variant % 4 == 0 ? 0 : 0x22220000u + variant;
            gLastError = 4000 + variant;
            gLog.clear();
            if (side == 0) ctx.Original<void(__cdecl *)()>(kInstaller)();
            else winmaininfra::InstallTerminateAndPurecallHandlers();
            uint32_t ptdHandler = *reinterpret_cast<uint32_t *>(data + kPtdTerminate), purecall = *ctx.image.At<uint32_t>(kPurecallVariable);
            uint32_t mine = side == 0 ? originalHandler : ourHandler;
            char buf[160];
            std::snprintf(buf, sizeof(buf), "terminate %s purecall %s", ptdHandler == mine ? "handler" : "other", purecall == mine ? "handler" : "other");
            std::string rest;
            for (size_t i = 0; i < kPtdSize; ++i) {
                if (i >= kPtdTerminate && i < kPtdTerminate + 4) continue;
                char t[4];
                std::snprintf(t, sizeof(t), "%02x", data[i]);
                rest += t;
            }
            results[side] = Joined() + buf + "\n" + rest;
        }
        ExpectSame(ctx, "installer effects (log, both variables, the rest of the per-thread data)", results[0], results[1]);
    }
    // The shape of the log and the result, once.
    {
        FlsSetValue(index, data);
        gLog.clear();
        winmaininfra::InstallTerminateAndPurecallHandlers();
        ExpectSame(ctx, "installer log", "GetLastError()\nFlsGetValue(" + std::to_string(index) + ")\nSetLastError(" + std::to_string(gLastError) + ")\n", Joined());
    }

    // Deliberate differences, ours only (docs/contracts/terminate-handlers.md): a thread whose data
    // does not exist yet (slot empty, or -1) keeps the original CRT's terminate handler, because the
    // original would build the data and ours does not; the purecall variable is set regardless. An
    // index of FLS_OUT_OF_INDEXES is not looked up.
    for (void *missing : {static_cast<void *>(nullptr), reinterpret_cast<void *>(static_cast<uintptr_t>(-1))}) {
        FlsSetValue(index, missing);
        *ctx.image.At<uint32_t>(kPurecallVariable) = 0;
        gLog.clear();
        winmaininfra::InstallTerminateAndPurecallHandlers();
        ++ctx.cases;
        if (*ctx.image.At<uint32_t>(kPurecallVariable) != ourHandler) ctx.Fail("no per-thread data: the purecall handler was not installed");
    }
    FlsSetValue(index, data);
    *ctx.image.At<uint32_t>(kFlsIndex) = FLS_OUT_OF_INDEXES;
    gLog.clear();
    winmaininfra::InstallTerminateAndPurecallHandlers();
    ExpectSame(ctx, "no FLS index: no lookup", "GetLastError()\nSetLastError(" + std::to_string(gLastError) + ")\n", Joined());

    *ctx.image.At<uint32_t>(kFlsIndex) = savedIndex;
    *ctx.image.At<uint32_t>(kPurecallVariable) = savedPurecall;
    FlsFree(index);
    std::free(data);
    os = saved;
}

// ---- the standalone seam and the order ----

namespace {

std::vector<std::string> gOrder;
void RecordTerminate(winmaininfra::HandlerFn h) { gOrder.push_back(h == &winmaininfra::ReportAndReturn ? "terminate:ours" : "terminate:other"); }
void RecordPurecall(winmaininfra::HandlerFn h) { gOrder.push_back(h == &winmaininfra::ReportAndReturn ? "purecall:ours" : "purecall:other"); }

} // namespace

DIFF_TEST(WinMainInfra_InstallHandlersSeams, kInstaller)
{
    // The order of the original: terminate first, then purecall, both our handler.
    gOrder.clear();
    winmaininfra::InstallHandlers(winmaininfra::CrtSeam{&RecordTerminate, &RecordPurecall});
    ++ctx.cases;
    if (gOrder.size() != 2 || gOrder[0] != "terminate:ours" || gOrder[1] != "purecall:ours") ctx.Fail("install order or handler wrong (%zu calls)", gOrder.size());

    // The standalone build: this program's own CRT.
    std::terminate_handler oldTerminate = std::get_terminate();
    _purecall_handler oldPurecall = _get_purecall_handler();
    winmaininfra::InstallHandlers(winmaininfra::kOwnCrt);
    ++ctx.cases;
    if (std::get_terminate() != &winmaininfra::ReportAndReturn) ctx.Fail("own CRT: terminate handler not installed");
    ++ctx.cases;
    if (_get_purecall_handler() != &winmaininfra::ReportAndReturn) ctx.Fail("own CRT: purecall handler not installed");
    std::set_terminate(oldTerminate);
    _set_purecall_handler(oldPurecall);
}
