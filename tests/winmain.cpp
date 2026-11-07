// Differential tests for src/app/WinMain.*: the small functions at the top of the start-up path.
//
// The original calls Windows and Steam through its import table, where the harness has traps. Each
// test points the slots it needs at scripted fakes for the life of a guard object (restored
// afterwards); our code reads the same slots, so both sides see the same fakes. Every fake logs its
// arguments, and each side's log is compared along with return values, buffers and globals.
//
// Objects the code calls through vtables (the application, the framework, the service) are fakes with
// recording slots, built identically for the two sides.
#define NOMINMAX
#include "app/AppBase.h"
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
#include <functional>
#include <new>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kGetModuleDirectoryW = 0xc27b04, kSteamInit = 0xc2a358, kDisableIme = 0xc2a3a4;
constexpr uint32_t kSetGlobalApp = 0x1446fc, kSetGlobalFramework = 0x14470b, kExceptionHandling = 0x1bbb09;
constexpr uint32_t kAcquireWindowOwner = 0xc2ccb6, kShowFatalMessage = 0xc2da5a;
constexpr uint32_t kRunFramework = 0xc2d0a8, kRunFrameworkGuarded = 0xc2d121;

constexpr uint32_t kIatGetModuleFileNameW = 0xd9a328, kIatSteamInit = 0xd9a70c;
constexpr uint32_t kIatGetModuleHandleA = 0xd9a114, kIatLoadLibraryA = 0xd9a128, kIatGetProcAddress = 0xd9a124;
constexpr uint32_t kIatMessageBoxA = 0xd9a568, kIatGetCurrentProcess = 0xd9a1b8, kIatTerminateProcess = 0xd9a1c0;
constexpr uint32_t kGAppForHook = 0x10fdfe8;

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
        std::string x = a.size() > 400 ? a.substr(0, 400) : a;
        std::string y = b.size() > 400 ? b.substr(0, 400) : b;
        for (char &c : x) if (c == '\n') c = '|';
        for (char &c : y) if (c == '\n') c = '|';
        ctx.Fail("%s:\n  original: %s\n  ours:     %s", what, x.c_str(), y.c_str());
    }
}

// Points import slots at fakes; restores them on destruction.
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

// Saves and restores the game globals the tests move.
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

// ---- 0xc27b04 GetModuleDirectoryW ----

std::wstring gScriptPath;
DWORD gScriptLength = 0;
bool gScriptWrites = true;

DWORD WINAPI FakeGetModuleFileNameW(HMODULE module, wchar_t *buffer, DWORD size)
{
    Log("GetModuleFileNameW(%p, size %u)", static_cast<void *>(module), size);
    if (gScriptWrites) {
        size_t n = gScriptPath.size() < size ? gScriptPath.size() : size;
        std::memcpy(buffer, gScriptPath.data(), n * sizeof(wchar_t));
    }
    return gScriptLength;
}

} // namespace

DIFF_TEST(WinMain_GetModuleDirectoryW, kGetModuleDirectoryW)
{
    Imports imports(ctx);
    imports.Set(kIatGetModuleFileNameW, reinterpret_cast<void *>(&FakeGetModuleFileNameW));
    std::mt19937 rng(0xc27b04);
    static const wchar_t alphabet[] = L"abcXYZ019 .\\/:_-\\/";
    for (int i = 0; i < 6000; ++i) {
        uint32_t size = 1 + rng() % 40;
        if (i % 11 == 0) size = 260;
        size_t pathLength = rng() % (size + 6);
        if (i % 7 == 0) pathLength = size - 1; // fits exactly
        if (i % 13 == 0) pathLength = size;    // fills the buffer
        gScriptPath.clear();
        for (size_t k = 0; k < pathLength; ++k) gScriptPath += alphabet[rng() % (sizeof(alphabet) / sizeof(wchar_t) - 1)];
        if (i % 5 == 0 && pathLength > 2) gScriptPath[0] = L'C'; // no particular reason; varies the first character
        if (i % 17 == 0) gScriptPath.assign(pathLength, L'x');   // no separator at all
        gScriptLength = static_cast<DWORD>(pathLength);
        switch (i % 9) {
        case 4: gScriptLength = 0; break;                       // the call fails
        case 6: gScriptLength = static_cast<DWORD>(size + rng() % 5); break; // the call claims more than fits
        case 8: gScriptLength = static_cast<DWORD>(pathLength + 1); break;   // length/path mismatch
        default: break;
        }
        gScriptWrites = (i % 19) != 0;
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            std::vector<wchar_t> buffer(size + 8, 0xCDCD);
            uint32_t r;
            if (side == 0) r = ctx.Original<uint32_t(__cdecl *)(wchar_t *, uint32_t)>(kGetModuleDirectoryW)(buffer.data(), size);
            else r = winmain::GetModuleDirectoryW(buffer.data(), size);
            std::string s = "ret=" + std::to_string(r) + " buf=";
            for (wchar_t c : buffer) { char t[8]; std::snprintf(t, sizeof(t), "%x,", c); s += t; }
            results[side] = s + "\n" + Joined();
        }
        ExpectSame(ctx, "module directory", results[0], results[1]);
    }
}

namespace {

// ---- 0xc2a358 SteamInit ----

int32_t gSteamResult = 0;

int __cdecl FakeSteamInit(const char *versions, void *errorMessage)
{
    Log("SteamInternal_SteamAPI_Init(%s, %s, %p)", versions, versions + std::strlen(versions) + 1, errorMessage);
    return gSteamResult;
}

} // namespace

DIFF_TEST(WinMain_SteamInit, kSteamInit)
{
    Imports imports(ctx);
    imports.Set(kIatSteamInit, reinterpret_cast<void *>(&FakeSteamInit));
    static const int32_t kResults[] = {0, 1, -1, 2, 100, 0x7fffffff, static_cast<int32_t>(0x80000000), 0x100, -256, 0x1234500};
    for (int32_t result : kResults) {
        gSteamResult = result;
        uint32_t r[2];
        std::string logs[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            r[side] = side == 0 ? ctx.Original<uint32_t(__cdecl *)()>(kSteamInit)() : winmain::SteamInit();
            logs[side] = Joined();
        }
        ++ctx.cases;
        if (r[0] != r[1] || logs[0] != logs[1]) ctx.Fail("steam init result %d: eax %08x/%08x\n%s\n%s", result, r[0], r[1], logs[0].c_str(), logs[1].c_str());
    }
}

namespace {

// ---- 0xc2a3a4 DisableImeTextFrameService ----

struct ImmScript {
    HMODULE existing;  // GetModuleHandleA's answer
    HMODULE loaded;    // LoadLibraryA's answer
    void *function;    // GetProcAddress's answer (null, or the fake below)
    uint32_t functionResult;
};
ImmScript gImm;

HMODULE WINAPI FakeGetModuleHandleA(const char *name)
{
    Log("GetModuleHandleA(%s)", name);
    return gImm.existing;
}
HMODULE WINAPI FakeLoadLibraryA(const char *name)
{
    Log("LoadLibraryA(%s)", name);
    return gImm.loaded;
}
void *WINAPI FakeGetProcAddress(HMODULE module, const char *name)
{
    Log("GetProcAddress(%p, %s)", static_cast<void *>(module), name);
    return gImm.function;
}
uint32_t WINAPI FakeImmDisable(int32_t arg)
{
    Log("ImmDisableTextFrameService(%d)", arg);
    return gImm.functionResult;
}

} // namespace

DIFF_TEST(WinMain_DisableImeTextFrameService, kDisableIme)
{
    Imports imports(ctx);
    imports.Set(kIatGetModuleHandleA, reinterpret_cast<void *>(&FakeGetModuleHandleA));
    imports.Set(kIatLoadLibraryA, reinterpret_cast<void *>(&FakeLoadLibraryA));
    imports.Set(kIatGetProcAddress, reinterpret_cast<void *>(&FakeGetProcAddress));
    for (int bits = 0; bits < 16; ++bits) {
        for (uint32_t result : {0u, 1u, 0xfedcba98u}) {
            gImm.existing = (bits & 1) ? reinterpret_cast<HMODULE>(0x11110000) : nullptr;
            gImm.loaded = (bits & 2) ? reinterpret_cast<HMODULE>(0x22220000) : nullptr;
            gImm.function = (bits & 4) ? reinterpret_cast<void *>(&FakeImmDisable) : nullptr;
            gImm.functionResult = (bits & 8) ? result : 0;
            uint32_t r[2];
            std::string logs[2];
            for (int side = 0; side < 2; ++side) {
                gLog.clear();
                r[side] = side == 0 ? ctx.Original<uint32_t(__cdecl *)()>(kDisableIme)() : winmain::DisableImeTextFrameService();
                logs[side] = Joined();
            }
            ++ctx.cases;
            if (r[0] != r[1] || logs[0] != logs[1]) ctx.Fail("ime bits %d result %x: eax %08x/%08x\n%s\n%s", bits, result, r[0], r[1], logs[0].c_str(), logs[1].c_str());
        }
    }
}

namespace {

// ---- fake reference-counted objects ----

struct Fake {
    const void *const *vtable;
    int id;
};

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

} // namespace

DIFF_TEST(WinMain_SetGlobals, kSetGlobalFramework)
{
    Globals globals(ctx);
    void *vt[8];
    for (auto &s : vt) s = reinterpret_cast<void *>(&FakeTrap);
    vt[1] = reinterpret_cast<void *>(&FakeAddRef);
    vt[2] = reinterpret_cast<void *>(&FakeRelease);
    for (int run = 0; run < 40; ++run) {
        Fake object{vt, run};
        std::string results[2];
        for (int which = 0; which < 2; ++which) { // 0: g_App, 1: g_Framework
            for (int side = 0; side < 2; ++side) {
                gLog.clear();
                uint32_t junk = 0x1000u + static_cast<uint32_t>(run);
                for (uint32_t rva : Globals::kRvas) globals.Set(rva, junk + rva);
                uint32_t r;
                if (which == 0) r = side == 0 ? ctx.Original<uint32_t(__cdecl *)(void *)>(kSetGlobalApp)(&object) : winmain::SetGlobalApp(&object);
                else r = side == 0 ? ctx.Original<uint32_t(__cdecl *)(void *)>(kSetGlobalFramework)(&object) : winmain::SetGlobalFramework(&object);
                char buf[96];
                std::snprintf(buf, sizeof(buf), "ret=%u ", r);
                std::string d = globals.Describe();
                // The globals hold the object's address; make the text comparable.
                char self[16];
                std::snprintf(self, sizeof(self), "%x", static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&object)));
                for (size_t at; (at = d.find(self)) != std::string::npos;) d.replace(at, std::strlen(self), "SELF");
                results[side] += buf + d + "\n" + Joined();
            }
        }
        ExpectSame(ctx, "set globals", results[0], results[1]);
    }
}

DIFF_TEST(WinMain_ExceptionHandlingEnabled, kExceptionHandling)
{
    std::mt19937 rng(0x1bbb09);
    for (int value = 0; value < 256; ++value) {
        std::vector<uint8_t> object(0x200);
        for (auto &b : object) b = static_cast<uint8_t>(rng());
        object[0x19b] = static_cast<uint8_t>(value);
        // Only al is compared: the rest of eax is whatever the caller had.
        uint8_t a = ctx.Original<uint8_t(__thiscall *)(const void *)>(kExceptionHandling)(object.data());
        uint8_t b = winmain::ExceptionHandlingEnabled(object.data(), nullptr);
        ++ctx.cases;
        if (a != b) ctx.Fail("flag byte %02x: original %02x, ours %02x", value, a, b);
    }
}

namespace {

// ---- the framework object and the service it hands out ----

struct ServiceScript {
    bool present;           // g_Framework is set
    bool answer;            // slot 5 stores a service
    uint32_t hwnd;          // what the service's slot 40 returns
};
ServiceScript gService;
Fake gServiceObject{nullptr, 7};
const void *gServiceVt[64];
Fake gFramework{nullptr, 9};
const void *gFrameworkVt[8];

uint32_t __fastcall FwQuery(Fake *self, void *, uint32_t id, uint32_t iid, void **out)
{
    Log("fw.GetSystemService(%d, %x, %x)", self->id, id, iid);
    if (gService.answer) *out = &gServiceObject;
    return 1;
}
uint32_t __fastcall ServiceWindow(Fake *self, void *)
{
    Log("service.slot40(%d)", self->id);
    return gService.hwnd;
}

void WireService()
{
    for (auto &s : gServiceVt) s = reinterpret_cast<void *>(&FakeTrap);
    gServiceVt[1] = reinterpret_cast<void *>(&FakeAddRef);
    gServiceVt[2] = reinterpret_cast<void *>(&FakeRelease);
    gServiceVt[40] = reinterpret_cast<void *>(&ServiceWindow);
    gServiceObject.vtable = gServiceVt;
    for (auto &s : gFrameworkVt) s = reinterpret_cast<void *>(&FakeTrap);
    gFrameworkVt[1] = reinterpret_cast<void *>(&FakeAddRef);
    gFrameworkVt[2] = reinterpret_cast<void *>(&FakeRelease);
    gFrameworkVt[5] = reinterpret_cast<void *>(&FwQuery);
    gFramework.vtable = gFrameworkVt;
}

} // namespace

DIFF_TEST(WinMain_AcquireWindowOwner, kAcquireWindowOwner)
{
    Globals globals(ctx);
    WireService();
    for (int bits = 0; bits < 4; ++bits) {
        gService.present = bits & 1;
        gService.answer = bits & 2;
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            globals.Set(0x10c9378, gService.present ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gFramework)) : 0);
            RZAutoRef holder{reinterpret_cast<void *>(0xDEADBEEF)};
            RZAutoRef *r = side == 0 ? ctx.Original<RZAutoRef *(__thiscall *)(RZAutoRef *)>(kAcquireWindowOwner)(&holder)
                                     : winmain::AcquireWindowOwner(&holder, nullptr);
            char buf[96];
            std::snprintf(buf, sizeof(buf), "same=%d holder=%s\n", r == &holder, holder.mObject == &gServiceObject ? "service" : holder.mObject ? "other" : "null");
            results[side] = buf + Joined();
        }
        ExpectSame(ctx, "acquire window owner", results[0], results[1]);
    }
}

namespace {

// ---- 0xc2da5a ShowFatalMessage ----

// What the fake box answers (AskYesNo's test varies it).
int gBoxAnswer = 1;

int WINAPI FakeMessageBox(HWND hwnd, const char *text, const char *caption, UINT flags)
{
    std::string t = text, c = caption;
    auto hex = [](const std::string &s) {
        std::string out;
        for (unsigned char ch : s) { char b[4]; std::snprintf(b, sizeof(b), "%02x", ch); out += b; }
        return out;
    };
    Log("MessageBoxA(%p, %s, %s, %x)", static_cast<void *>(hwnd), hex(t).c_str(), hex(c).c_str(), flags);
    return gBoxAnswer;
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

// The game's function-local static (a cRZString at 0x10fdffc, its guard at 0x10fe01c) is built ahead of
// the first call so that the thread-safe static machinery of the (uninitialised) CRT is not needed.
// AskYesNo has its own (0x10fe020, guard 0x10fe040).
constexpr uint32_t kStaticSeparator = 0x10fdffc, kStaticGuard = 0x10fe01c, kTlsIndex = 0x10df814;
constexpr uint32_t kAskStaticSeparator = 0x10fe020;
constexpr uint32_t kAskStaticGuard = 0x10fe040;
void PrepareSeparator(difftest::Context &ctx, uint32_t separatorRva = kStaticSeparator, uint32_t guardRva = kStaticGuard)
{
    static std::set<uint32_t> done;
    if (!done.insert(separatorRva).second) return;
    auto *separator = ctx.image.At<cRZString>(separatorRva);
    std::memset(separator, 0, sizeof(cRZString));
    separator->Construct("\xe2\x80\x8b");
    // The guard reads as "initialised" when it is not greater than the thread's epoch.
    uint32_t *block = static_cast<uint32_t *>(reinterpret_cast<void **>(__readfsdword(0x2c))[*ctx.image.At<uint32_t>(kTlsIndex)]);
    *ctx.image.At<int32_t>(guardRva) = static_cast<int32_t>(block[1]);
}

std::string RandomMessage(std::mt19937 &rng, int run)
{
    static const char *const kPieces[] = {"a", "Hello", " ", "\xe2\x80\x8b", "\xe2\x80\x8b\xe2\x80\x8b", "\xc3\xa9", "\xe4\xb8\xad", "\xe2\x80", "x\xe2\x80\x8bx", "long text that does not fit the small string buffer",
                                          "\r\n", "100%", "\xe2", "\x8b", "\xe2\x80\x8c"};
    std::string s;
    int n = static_cast<int>(rng() % 8);
    if (run % 10 == 0) n = 0;
    for (int i = 0; i < n; ++i) s += kPieces[rng() % (sizeof(kPieces) / sizeof(kPieces[0]))];
    return s;
}

} // namespace

DIFF_TEST(WinMain_ShowFatalMessage, kShowFatalMessage)
{
    Globals globals(ctx);
    Imports imports(ctx);
    imports.Set(kIatMessageBoxA, reinterpret_cast<void *>(&FakeMessageBox));
    WireService();
    PrepareSeparator(ctx);
    std::mt19937 rng(0xc2da5a);
    for (int run = 0; run < 3000; ++run) {
        // Without a service the original reads slot 40 of a null object and crashes (so does ours): that
        // path is not exercised.
        gService.present = true;
        gService.answer = true;
        gService.hwnd = rng() % 3 == 0 ? 0 : static_cast<uint32_t>(rng());
        std::string text = RandomMessage(rng, run), caption = RandomMessage(rng, run + 3);
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            globals.Set(0x10c9378, gService.present ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gFramework)) : 0);
            if (side == 0) ctx.Original<void(__cdecl *)(const char *, const char *)>(kShowFatalMessage)(text.c_str(), caption.c_str());
            else winmain::ShowFatalMessage(text.c_str(), caption.c_str());
            results[side] = Joined();
        }
        ExpectSame(ctx, "fatal message", results[0], results[1]);
    }
}

// ---- 0xc2dbf1 AskYesNo (lane MB): as ShowFatalMessage, plus the box's answer (IDYES, IDNO, others) and al.

DIFF_TEST(WinMain_AskYesNo, 0xc2dbf1)
{
    Globals globals(ctx);
    Imports imports(ctx);
    imports.Set(kIatMessageBoxA, reinterpret_cast<void *>(&FakeMessageBox));
    WireService();
    PrepareSeparator(ctx, kAskStaticSeparator, kAskStaticGuard);
    static const int kAnswers[] = {IDYES, IDNO, IDOK, IDCANCEL, 0};
    std::mt19937 rng(0xc2dbf1);
    for (int run = 0; run < 2000; ++run) {
        gService.present = true;
        gService.answer = true;
        gService.hwnd = rng() % 3 == 0 ? 0 : static_cast<uint32_t>(rng());
        gBoxAnswer = kAnswers[rng() % 5];
        std::string text = RandomMessage(rng, run), caption = RandomMessage(rng, run + 7);
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            globals.Set(0x10c9378, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gFramework)));
            const uint8_t yes = side == 0 ? ctx.Original<uint8_t(__cdecl *)(const char *, const char *)>(0xc2dbf1)(text.c_str(), caption.c_str())
                                          : static_cast<uint8_t>(winmain::AskYesNo(text.c_str(), caption.c_str()));
            results[side] = "al=" + std::to_string(yes) + " " + Joined();
        }
        ExpectSame(ctx, "yes/no question", results[0], results[1]);
    }
    gBoxAnswer = 1;
}

// ---- RunFramework (0xc2d0a8) and RunFrameworkGuarded (0xc2d121) ----
//
// A real framework (the original's constructor, 0xc2ccdb) whose called slots are recording fakes, so
// the verified GZFrameWork_Startup, _Run and _Shutdown run for real on both sides (the original's
// copies on one, ours on the other) and what is compared is RunFramework's own work: the globals, the
// QueryInterface, the reference held on the application face and when it is released, and the
// exceptions the guarded version catches.

namespace {

using Fw = cCTGFramework;

constexpr uint32_t kIatGetModuleFileNameA = 0xd9a16c;
constexpr uint32_t kModuleFileNameWResolved = 0x10caeb5, kModuleFileNameWPointer = 0x10caeb8;

struct Script {
    bool qiOk;          // the application hands out its cIGZApp face
    bool realInit, queryService, appServices, preInit, appBand;
    bool gzRun;         // the face's GZRun answer; false runs the framework's own main loop
    int throwWhere;     // 0: nothing; 1: from QueryInterface; 2: from AddApplicationServicesHere
    int throwKind;      // 0: cError (code in throwCode); 1: cD3DException; 2: std::runtime_error
    int32_t throwCode;
    uint32_t d3dValue;
};
Script gScript;
Fw *gFw;

struct Obj {
    const void *const *vtable;
    int id;
    int refs;
};
Obj gApp{nullptr, 1, 1}, gFace{nullptr, 2, 1}, gCom{nullptr, 3, 1};
const void *gAppVt[8], *gFaceVt[24], *gComVt[16], *gFwVt[64], *gLockVt[8], *gD3dVt[2];

void Mark(const char *what, uint32_t arg = 0)
{
    Log("%s(%x) state=%u exit=%d tick=%u", what, arg, gFw->mState, *GameAddress<int32_t>(cGZFramework::kGlobalExitCode), gFw->mTickEnabled);
}

[[noreturn]] void Throw()
{
    switch (gScript.throwKind) {
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
    default: throw std::runtime_error("other");
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

uint8_t __fastcall AppQuery(Obj *, void *, uint32_t iid, void **out)
{
    Log("app.QueryInterface(%x)", iid);
    if (gScript.throwWhere == 1) Throw();
    if (gScript.qiOk) *out = &gFace;
    return gScript.qiOk;
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
    if (gScript.throwWhere == 2) Throw();
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
uint32_t __fastcall FwAddRef(Fw *, void *) { Mark("fw.AddRef"); return 5; }
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

void WireObjects()
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
    gApp = {gAppVt, 1, 1};
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
    gFwVt[1] = reinterpret_cast<void *>(&FwAddRef);
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
    // The embedded COM object's RealShutdown (called by the framework's shutdown) records too.
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

// Text for the game's message strings, which the harness leaves zeroed.
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

void Replace(std::string &text, uint32_t address, const char *name)
{
    char hex[16];
    std::snprintf(hex, sizeof(hex), "%x", address);
    for (size_t at; (at = text.find(hex)) != std::string::npos;) text.replace(at, std::strlen(hex), name);
}

std::string RunScenario(difftest::Context &ctx, Globals &globals, const Script &script, int side, bool guarded, const char *cmdText)
{
    gScript = script;
    gLog.clear();
    WireObjects();
    Fw *fw;
    BuildFw(ctx, fw);
    gFw = fw;
    WireFw(fw);
    cRZCmdLine *cmd = NewCmd(cmdText);
    // A stale application reference in g_App, as when an earlier run left one: used if the QueryInterface fails.
    globals.Set(0x10c9378, 0);
    globals.Set(0x10c937c, 0);
    globals.Set(0x10c9380, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gFace)));
    globals.Set(0x10c9384, 0);
    globals.Set(kGAppForHook, 0);
    std::string thrown = "none";
    try {
        if (side == 0) ctx.Original<void(__cdecl *)(void *, void *, cRZCmdLine *)>(guarded ? kRunFrameworkGuarded : kRunFramework)(fw, &gApp, cmd);
        else if (guarded) winmain::RunFrameworkGuarded(fw, &gApp, cmd);
        else winmain::RunFramework(fw, &gApp, cmd);
    } catch (const nGZGraphic4::cError &e) {
        thrown = "cError " + std::to_string(e.mCode);
    } catch (const nRZGraphic4::cD3DException &) {
        thrown = "d3d";
    } catch (const std::runtime_error &) {
        thrown = "runtime_error";
    }
    std::string d = globals.Describe();
    Replace(d, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fw)), "FW");
    Replace(d, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gApp)), "APP");
    Replace(d, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gFace)), "FACE");
    char tail[160];
    std::snprintf(tail, sizeof(tail), "thrown=%s state=%u refs app=%d face=%d com=%d\n", thrown.c_str(), fw->mState, gApp.refs, gFace.refs, gCom.refs);
    std::string out = d + "\n" + tail + Joined();
    reinterpret_cast<cRZCmdLine *>(fw->mCmdLine)->Destruct();
    cmd->Destruct();
    ::operator delete(cmd);
    return out;
}

struct Fakes {
    Imports imports;
    uint8_t savedResolved;
    void *savedCached;
    difftest::Context &ctx;
    explicit Fakes(difftest::Context &c) : imports(c), ctx(c)
    {
        savedResolved = *ctx.image.At<uint8_t>(kModuleFileNameWResolved);
        savedCached = *ctx.image.At<void *>(kModuleFileNameWPointer);
        *ctx.image.At<uint8_t>(kModuleFileNameWResolved) = 1;
        *ctx.image.At<void *>(kModuleFileNameWPointer) = reinterpret_cast<void *>(&FakeWideName);
        imports.Set(kIatGetModuleFileNameA, reinterpret_cast<void *>(&FakeNarrowName));
        imports.Set(kIatMessageBoxA, reinterpret_cast<void *>(&FakeMessageBox));
        imports.Set(kIatGetCurrentProcess, reinterpret_cast<void *>(&FakeGetCurrentProcess));
        imports.Set(kIatTerminateProcess, reinterpret_cast<void *>(&FakeTerminateProcess));
        PrepareMessages(ctx);
        PrepareSeparator(ctx);
    }
    ~Fakes()
    {
        *ctx.image.At<uint8_t>(kModuleFileNameWResolved) = savedResolved;
        *ctx.image.At<void *>(kModuleFileNameWPointer) = savedCached;
    }
};

void RunMatrix(difftest::Context &ctx, bool guarded)
{
    Globals globals(ctx);
    Fakes fakes(ctx);
    std::mt19937 rng(guarded ? 0xc2d121 : 0xc2d0a8);
    static const char *const kCmds[] = {"", "-w", "/nosound -lang:en", "Sims2EP9.exe a b"};
    for (int bits = 0; bits < 32; ++bits) {
        for (int variant = 0; variant < 12; ++variant) {
            Script s{};
            s.qiOk = variant % 3 != 2;
            s.realInit = bits & 1;
            s.queryService = bits & 2;
            s.appServices = bits & 4;
            s.preInit = bits & 8;
            s.appBand = bits & 16;
            s.gzRun = (variant & 1) != 0;
            s.throwWhere = variant >= 6 ? 1 + static_cast<int>(rng() % 2) : 0;
            s.throwKind = static_cast<int>(rng() % 3);
            s.throwCode = static_cast<int32_t>(rng() % 5);
            s.d3dValue = static_cast<uint32_t>(rng());
            if (!guarded && s.throwKind != 2 && (rng() % 2)) s.throwKind = 2; // unguarded: mostly plain exceptions, some game ones
            const char *cmd = kCmds[rng() % 4];
            std::string results[2];
            for (int side = 0; side < 2; ++side) results[side] = RunScenario(ctx, globals, s, side, guarded, cmd);
            char what[96];
            std::snprintf(what, sizeof(what), "bits %d variant %d throw %d/%d code %d", bits, variant, s.throwWhere, s.throwKind, s.throwCode);
            ExpectSame(ctx, what, results[0], results[1]);
        }
    }
}

} // namespace

DIFF_TEST(WinMain_RunFramework, kRunFramework)
{
    RunMatrix(ctx, false);
}

DIFF_TEST(WinMain_RunFrameworkGuarded, kRunFrameworkGuarded)
{
    RunMatrix(ctx, true);
}

// ---- the application's bases and the exception reporter's small members (src/app/AppBase.*) ----

namespace {

constexpr uint32_t kGZAppCtor = 0xc59f5a, kCommonImplCtor = 0xca4cb0, kStringEquals = 0x1c3cb0;
constexpr uint32_t kIdSetCtor = 0x1c02fb, kInsertId = 0x1c04a4;
constexpr uint32_t kSetEnabled = 0x1bd3d8, kSetNames = 0x1bd467, kSetFlag = 0x1bd48e, kSetLevel = 0x1baff8, kDebuggerPresent = 0x1bd019;
constexpr uint32_t kIatSetFilter = 0xd9a1b4, kIatFreeLibrary = 0xd9a120;

std::string Hex(const void *data, size_t n)
{
    std::string out;
    for (size_t i = 0; i < n; ++i) {
        char b[4];
        std::snprintf(b, sizeof(b), "%02x", static_cast<const uint8_t *>(data)[i]);
        out += b;
    }
    return out;
}

std::string StringState(const std::string &s)
{
    return "[" + s + "]" + std::to_string(s.size()) + "/" + std::to_string(s.capacity());
}

} // namespace

DIFF_TEST(WinMain_cGZAppCtor, kGZAppCtor)
{
    std::mt19937 rng(0xc59f5a);
    static const char *const kNames[] = {"", "The Sims 2", "exactly15chars!!", "exactly 15 char", "a considerably longer application name than fits in the small buffer"};
    for (int run = 0; run < 300; ++run) {
        uint32_t id = run % 3 == 0 ? 0x5afdaae5u : static_cast<uint32_t>(rng());
        const char *name = kNames[rng() % 5];
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            alignas(8) uint8_t raw[0x40];
            std::memset(raw, 0xCD, sizeof(raw));
            void *returned;
            if (side == 0) returned = ctx.Original<void *(__thiscall *)(void *, uint32_t, const char *)>(kGZAppCtor)(raw, id, name);
            else returned = reinterpret_cast<appbase::cGZApp *>(raw)->Construct(id, name);
            auto *app = reinterpret_cast<appbase::cGZApp *>(raw);
            // The first 0x20 bytes and the string's vtable, refcount and text.
            results[side] = std::string(returned == raw ? "this " : "other ") + Hex(raw, 0x20) + " name " + Hex(raw + 0x20, 4) + " " + StringState(app->mName.mString) +
                            " ref " + std::to_string(app->mName.mRefCount);
            if (app->mName.mString.size() < 16) results[side] += " raw " + Hex(raw + 0x24, 0x18);
            app->mName.Destruct();
        }
        ExpectSame(ctx, "cGZApp ctor", results[0], results[1]);
    }
}

DIFF_TEST(WinMain_cTSAppCommonImplCtor, kCommonImplCtor)
{
    for (int run = 0; run < 100; ++run) {
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            alignas(8) uint8_t raw[0x18 + 8];
            std::memset(raw, 0xA0 + run, sizeof(raw));
            void *returned;
            if (side == 0) returned = ctx.Original<void *(__thiscall *)(void *)>(kCommonImplCtor)(raw);
            else returned = reinterpret_cast<appbase::cTSAppCommonImpl *>(raw)->Construct();
            auto *impl = reinterpret_cast<appbase::cTSAppCommonImpl *>(raw);
            auto node = [](void *head) {
                auto *p = static_cast<void **>(head);
                char buf[200];
                std::snprintf(buf, sizeof(buf), "left%d parent%d right%d flags %04x", p[0] == head, p[1] == head, p[2] == head, *reinterpret_cast<uint16_t *>(&p[3]));
                return std::string(buf);
            };
            results[side] = std::string(returned == raw ? "this " : "other ") + "vt " + Hex(&impl->mVTable, 4) + " sizes " + std::to_string(impl->mSizeA) + "," + std::to_string(impl->mSizeB) +
                            " w14 " + std::to_string(impl->mWord14) + " A:" + node(impl->mHeadA) + " B:" + node(impl->mHeadB) + " distinct " + std::to_string(impl->mHeadA != impl->mHeadB) +
                            " tail " + Hex(raw + 0x18, 8) + " heap " + std::to_string(HeapValidate(GetProcessHeap(), 0, impl->mHeadA)) + std::to_string(HeapValidate(GetProcessHeap(), 0, impl->mHeadB)) +
                            " sizeA " + std::to_string(HeapSize(GetProcessHeap(), 0, impl->mHeadA)) + " sizeB " + std::to_string(HeapSize(GetProcessHeap(), 0, impl->mHeadB));
            ::operator delete(impl->mHeadA);
            ::operator delete(impl->mHeadB);
        }
        ExpectSame(ctx, "cTSAppCommonImpl ctor", results[0], results[1]);
    }
}

DIFF_TEST(WinMain_StringEqualsCString, kStringEquals)
{
    std::mt19937 rng(0x1c3cb0);
    static const char *const kTexts[] = {"", "a", "enabled", "off", "disabled", "ExceptionHandling", "exactly15chars!!", "a string that is certainly longer than fifteen characters", "ENABLED", "enable"};
    auto orig = ctx.Original<uint8_t(__thiscall *)(const std::string *, const char *)>(kStringEquals);
    for (int run = 0; run < 4000; ++run) {
        std::string s = kTexts[rng() % 10];
        std::string t = kTexts[rng() % 10];
        if (run % 4 == 0) t = s;
        if (run % 7 == 0 && !t.empty()) t.back() ^= 1;                  // differs in the last byte
        if (run % 11 == 0) s.push_back('x');                            // one longer
        if (run % 13 == 0 && !s.empty()) { s.pop_back(); }              // one shorter
        if (run % 17 == 0 && s.size() > 2) s[1] = '\0';                 // embedded NUL: only the C string's length counts
        if (run % 19 == 0) s.reserve(100);                              // heap storage for a short text
        uint8_t a = orig(&s, t.c_str());
        uint8_t b = appbase::StringEqualsCString(&s, nullptr, t.c_str());
        ++ctx.cases;
        if (a != b) ctx.Fail("string [%s] vs [%s]: %u/%u", s.c_str(), t.c_str(), a, b);
    }
}

namespace {

// The shape of a set's tree: head, then every node in order with its colour and nil flag.
std::string TreeShape(const std::set<uint32_t> &set)
{
    struct Node {
        Node *left, *parent, *right;
        uint8_t color, isnil;
        uint32_t key;
    };
    struct Raw {
        Node *head;
        size_t size;
    };
    const Raw *raw = reinterpret_cast<const Raw *>(&set);
    std::string out = "size " + std::to_string(raw->size) + " head " + std::to_string(raw->head->isnil) + std::to_string(raw->head->color) + ":";
    struct Walker {
        static void Walk(const Node *n, std::string &o)
        {
            if (n->isnil) { o += "."; return; }
            o += "(" + std::to_string(n->key) + "c" + std::to_string(n->color);
            Walk(n->left, o);
            Walk(n->right, o);
            o += ")";
        }
    };
    Walker::Walk(raw->head->parent, out);
    out += raw->head->left == raw->head ? " L=head" : " L=" + std::to_string(raw->head->left->key);
    out += raw->head->right == raw->head ? " R=head" : " R=" + std::to_string(raw->head->right->key);
    return out;
}

} // namespace

DIFF_TEST(WinMain_IdSet, kInsertId)
{
    std::mt19937 rng(0x1c04a4);
    for (int run = 0; run < 400; ++run) {
        std::vector<uint32_t> ids;
        int n = static_cast<int>(rng() % 40);
        uint32_t range = run % 3 == 0 ? 8 : (run % 3 == 1 ? 100 : 0xffffffffu);
        for (int i = 0; i < n; ++i) ids.push_back(range == 0xffffffffu ? static_cast<uint32_t>(rng()) : static_cast<uint32_t>(rng() % range));
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            alignas(8) uint8_t raw[sizeof(std::set<uint32_t>)];
            std::memset(raw, 0xCD, sizeof(raw));
            auto *set = reinterpret_cast<std::set<uint32_t> *>(raw);
            void *returned;
            if (side == 0) returned = ctx.Original<void *(__thiscall *)(void *)>(kIdSetCtor)(raw);
            else returned = appbase::ConstructIdSet(set, nullptr);
            std::string s = std::string(returned == raw ? "this " : "other ") + TreeShape(*set) + "\n";
            for (uint32_t id : ids) {
                uint8_t fresh = side == 0 ? ctx.Original<uint8_t(__thiscall *)(void *, uint32_t)>(kInsertId)(raw, id) : appbase::InsertId(set, nullptr, id);
                s += std::to_string(fresh) + " " + TreeShape(*set) + "\n";
            }
            results[side] = s;
            set->~set();
        }
        ExpectSame(ctx, "id set", results[0], results[1]);
    }
}

namespace {

void *gFilterNow = nullptr;

void *WINAPI FakeSetFilter(void *filter)
{
    Log("SetUnhandledExceptionFilter(%p)", filter);
    void *previous = gFilterNow;
    gFilterNow = filter;
    return reinterpret_cast<void *>(0x7000 + (reinterpret_cast<uintptr_t>(filter) & 0xff));
}

} // namespace

DIFF_TEST(WinMain_ReporterSetEnabled, kSetEnabled)
{
    Imports imports(ctx);
    imports.Set(kIatSetFilter, reinterpret_cast<void *>(&FakeSetFilter));
    std::mt19937 rng(0x1bd3d8);
    static const uint32_t kArgs[] = {0, 1, 2, 0x100, 0xffffff00u, 0xffffffffu, 0x12345600u, 0x80};
    static const uint8_t kStates[] = {0, 1, 2, 0xff};
    for (int run = 0; run < 600; ++run) {
        uint32_t arg = kArgs[rng() % 8];
        uint8_t state = kStates[rng() % 4];
        uint32_t previous = static_cast<uint32_t>(rng());
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            uint8_t raw[0x1b0];
            for (auto &b : raw) b = static_cast<uint8_t>(0x55 + run);
            raw[0x19b] = state;
            *reinterpret_cast<uint32_t *>(raw + 0x1a8) = previous;
            uint8_t r = side == 0 ? ctx.Original<uint8_t(__thiscall *)(void *, uint32_t)>(kSetEnabled)(raw, arg)
                                  : reinterpret_cast<appbase::cExceptionReporter *>(raw)->SetEnabled(arg);
            std::string log = Joined();
            // The filter is an absolute address in the image: make the text comparable.
            char filter[16];
            std::snprintf(filter, sizeof(filter), "%p", ctx.image.At<void>(0x1bd687));
            for (size_t at; (at = log.find(filter)) != std::string::npos;) log.replace(at, std::strlen(filter), "FILTER");
            results[side] = "al=" + std::to_string(r) + " " + Hex(raw, sizeof(raw)) + "\n" + log;
        }
        ExpectSame(ctx, "reporter set enabled", results[0], results[1]);
    }
}

DIFF_TEST(WinMain_ReporterSmallSetters, kSetNames)
{
    std::mt19937 rng(0x1bd467);
    static const char *const kTexts[] = {"", "short", "exactly15chars!!", "a name long enough to need the heap for sure, certainly more than 15"};
    for (int run = 0; run < 400; ++run) {
        const char *textA = kTexts[rng() % 4], *textB = kTexts[rng() % 4], *textFirst = kTexts[rng() % 4], *textSecond = kTexts[rng() % 4];
        uint8_t flag = static_cast<uint8_t>(rng());
        uint32_t index = rng() % 8;
        uint8_t level = static_cast<uint8_t>(rng());
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            alignas(8) uint8_t raw[0x1b0];
            std::memset(raw, 0x3C, sizeof(raw));
            auto *a = new (raw + 0x24) std::string(textA);
            auto *b = new (raw + 0x44) std::string(textB);
            if (run % 5 == 0) a->reserve(80);
            cRZString first, second;
            first.Construct(textFirst);
            second.Construct(textSecond);
            if (run % 7 == 0) first.mString = *a; // same text already there
            if (side == 0) ctx.Original<void(__thiscall *)(void *, const void *, const void *)>(kSetNames)(raw, &first, &second);
            else reinterpret_cast<appbase::cExceptionReporter *>(raw)->SetNames(&first, &second);
            if (side == 0) {
                ctx.Original<void(__thiscall *)(void *, uint8_t)>(kSetFlag)(raw, flag);
                ctx.Original<void(__thiscall *)(void *, uint32_t, uint8_t)>(kSetLevel)(raw, index, level);
            } else {
                reinterpret_cast<appbase::cExceptionReporter *>(raw)->SetFlag19a(flag);
                reinterpret_cast<appbase::cExceptionReporter *>(raw)->SetLevel(index, level);
            }
            results[side] = StringState(*a) + StringState(*b) + " bytes " + Hex(raw + 0x194, 0x10) + " rest " + Hex(raw, 0x24) + Hex(raw + 0x64, 0x130);
            a->~basic_string();
            b->~basic_string();
            first.Destruct();
            second.Destruct();
        }
        ExpectSame(ctx, "reporter setters", results[0], results[1]);
    }
}

namespace {

struct DebuggerScript {
    HMODULE module;
    void *function;
    uint32_t answer;
};
DebuggerScript gDebugger;

HMODULE WINAPI FakeLoadLibraryKernel(const char *name)
{
    Log("LoadLibraryA(%s)", name);
    return gDebugger.module;
}
void *WINAPI FakeGetProcDebugger(HMODULE module, const char *name)
{
    Log("GetProcAddress(%p, %s)", static_cast<void *>(module), name);
    return gDebugger.function;
}
BOOL WINAPI FakeFreeLibrary(HMODULE module)
{
    Log("FreeLibrary(%p)", static_cast<void *>(module));
    return TRUE;
}
uint32_t WINAPI FakeIsDebuggerPresent()
{
    Log("IsDebuggerPresent()");
    return gDebugger.answer;
}

} // namespace

DIFF_TEST(WinMain_DebuggerPresentCached, kDebuggerPresent)
{
    Imports imports(ctx);
    imports.Set(kIatLoadLibraryA, reinterpret_cast<void *>(&FakeLoadLibraryKernel));
    imports.Set(kIatGetProcAddress, reinterpret_cast<void *>(&FakeGetProcDebugger));
    imports.Set(kIatFreeLibrary, reinterpret_cast<void *>(&FakeFreeLibrary));
    uint8_t savedAnswer = *ctx.image.At<uint8_t>(0x10cb078);
    void *savedFunction = *ctx.image.At<void *>(0x10cb074);
    for (int bits = 0; bits < 64; ++bits) {
        gDebugger.module = (bits & 1) ? reinterpret_cast<HMODULE>(0x5000) : nullptr;
        gDebugger.function = (bits & 2) ? reinterpret_cast<void *>(&FakeIsDebuggerPresent) : nullptr;
        gDebugger.answer = (bits & 4) ? 0x100u : 0u; // non-zero but zero in the low byte
        void *cachedFunction = (bits & 8) ? reinterpret_cast<void *>(0x1234) : nullptr;
        uint8_t cachedAnswer = (bits & 16) ? ((bits & 32) ? 0x7f : 1) : 0;
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            *ctx.image.At<void *>(0x10cb074) = cachedFunction;
            *ctx.image.At<uint8_t>(0x10cb078) = cachedAnswer;
            uint8_t r = side == 0 ? ctx.Original<uint8_t(__cdecl *)()>(kDebuggerPresent)() : appbase::DebuggerPresentCached();
            // Two calls: the second finds the cache (when the function was found).
            uint8_t r2 = side == 0 ? ctx.Original<uint8_t(__cdecl *)()>(kDebuggerPresent)() : appbase::DebuggerPresentCached();
            char buf[96];
            std::snprintf(buf, sizeof(buf), "r=%u,%u cacheFn=%d cacheAns=%u\n", r, r2, *ctx.image.At<void *>(0x10cb074) == gDebugger.function && gDebugger.function != nullptr,
                          *ctx.image.At<uint8_t>(0x10cb078));
            results[side] = buf + Joined();
        }
        ExpectSame(ctx, "debugger present", results[0], results[1]);
    }
    *ctx.image.At<uint8_t>(0x10cb078) = savedAnswer;
    *ctx.image.At<void *>(0x10cb074) = savedFunction;
}
