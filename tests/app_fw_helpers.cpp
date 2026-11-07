// Differential tests for the application's helpers (lane MB, foundations F4e): the free functions of
// src/ts/cTSAppFWHelpers.cpp (0xc27961, 0xb417c0, 0xc27b49, 0x1b1a3f, 0xc2896d) and the cTSAppFW members 0xc28b7d,
// 0xc28de0, 0xc28e32, 0xc2a3d6 (src/ts/cTSAppFW.size.cpp).
//
// Every callee outside the function under test is a logging fake: the Win32 imports through their import-table slots,
// the game's own functions and ours through stand-ins on both sides (shape::StandIn), the original-only callees
// (0xc0b314, 0x9cce18, 0x1c99d2) through a patch of the image. The objects the functions reach are fakes whose slots
// log. Compared: the result (al / eax) and the log of every call with its arguments.
#define NOMINMAX
#include "app/AppInit.h"
#include "app/ExceptionReporter.h"
#include "app/ReportFinish.h"
#include "app/WinMain.h"
#include "app_debugger_fakes.h"
#include "code_patch.h"
#include "difftest.h"
#include "game/game.h"
#include "gz/cIGZString.h"
#include "rz/DirectoryScan.h"
#include "rz/RZTextConvert.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters2.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"
#include "ts/cTSAppFW.h"
#include "ts/cTSAppFWHelpers.h"

#include <windows.h>
#include <process.h>
#include <psapi.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kAppendEditionSuffix = 0xc27961;
constexpr uint32_t kBodyShopProductName = 0xb417c0;
constexpr uint32_t kPagefileUsage = 0xc27b49;
constexpr uint32_t kSpawnAndWait = 0x1b1a3f;
constexpr uint32_t kIsUserAdministrator = 0xc2896d;
constexpr uint32_t kLaunchUploader = 0xc28b7d;
constexpr uint32_t kReportFatal = 0xc28de0;
constexpr uint32_t kAddCrashData = 0xc28e32;
constexpr uint32_t kUnlockNeighborhood = 0xc2a3d6;

constexpr uint32_t kFrameworkGlobal = 0x10c9378;
constexpr uint32_t kLanguageCache = 0x10f51c8;
constexpr uint32_t kPackagesCache = 0x10faf6c;
constexpr uint32_t kAppCaption = 0x10fda54;
constexpr uint32_t kBodyShopNames = 0x10fb08c;
constexpr uint32_t kTlsIndex = 0x10df814;
constexpr uint32_t kPsapiModule = 0x10fdb40;
constexpr uint32_t kPsapiModuleGuard = 0x10fdb44;
constexpr uint32_t kMemoryInfoFn = 0x10fdb48;
constexpr uint32_t kMemoryInfoFnGuard = 0x10fdb4c;

constexpr uint32_t kSpawnv = 0x837b52;
constexpr uint32_t kCommonImplSlot4 = 0x1d0c00;
constexpr uint32_t kPathExists = 0x1c1242;
constexpr uint32_t kConvertStringToString = 0x1b025d;
constexpr uint32_t kShowFatalMessage = 0xc2da5a;
constexpr uint32_t kAddExtraData = 0x1bac12;
constexpr uint32_t kVersionConstruct = 0x1c98d6;
constexpr uint32_t kVersionDestruct = 0x1c9901;
constexpr uint32_t kVersionHostName = 0x1c99d2;
constexpr uint32_t kGetCachedSlot23 = 0x20dde0;
constexpr uint32_t kGetCachedSingleton = 0x20ddfb;
constexpr uint32_t kNeighborhoodDirectory = 0xc0b314;
constexpr uint32_t kWildcardMatch = 0x9cce18;
constexpr uint32_t kScanConstruct = 0x1c48da;
constexpr uint32_t kScanDestruct = 0x1c49eb;
constexpr uint32_t kScanEntryAt = 0x1c5047;
constexpr uint32_t kEntryFullPath = 0x1c509f;

constexpr uint32_t kIatGetVersion = 0xd9a11c;
constexpr uint32_t kIatLoadLibraryA = 0xd9a128;
constexpr uint32_t kIatGetProcAddress = 0xd9a124;
constexpr uint32_t kIatGetCurrentProcess = 0xd9a1b8;

std::string gLog;
std::mt19937 gR;

std::string Fmt(const char *fmt, ...)
{
    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    return buffer;
}

void Log(const std::string &line) { gLog += line + " "; }

std::string Hex(const std::string &text)
{
    std::string out;
    for (unsigned char c : text) out += Fmt("%02x", c);
    return out;
}

std::string Differ(const std::string &a, const std::string &b)
{
    size_t at = 0;
    while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
    at = at > 60 ? at - 60 : 0;
    return Fmt("from offset %u:\n  original: %.500s\n  ours:     %.500s", unsigned(at), a.c_str() + at, b.c_str() + at);
}

void Compare(difftest::Context &ctx, unsigned run, const std::string results[2])
{
    ++ctx.cases;
    if (results[0] != results[1]) ctx.Fail("run %u, %s", run, Differ(results[0], results[1]).c_str());
}

uint32_t Address(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// A cRZString on the stack (its own members build and tear it down).
struct Text {
    union {
        cRZString s;
    };
    explicit Text(const std::string &text = "") { s.Construct(text.c_str()); }
    Text(const Text &) = delete;
    ~Text() { s.Destruct(); }
    cIGZString *Face() { return s.AsInterface(); }
};

// A fake object: a vtable of up to 64 slots (the unset ones null: a call through one crashes the test, loudly) and a
// name, which its ToChar (slot 5, when set) answers.
struct Fake {
    const void *const *vt;
    const void *slots[64];
    const char *name;
    explicit Fake(const char *n) : vt(slots), name(n) { Clear(); }
    void Clear()
    {
        for (auto &s : slots) s = nullptr;
    }
    template <typename F> void Set(int slot, F fn) { slots[slot] = reinterpret_cast<const void *>(fn); }
};

const char *NameOf(const void *object) { return object ? static_cast<const Fake *>(object)->name : "null"; }
const char *__fastcall FakeToChar(void *self, void *) { return static_cast<Fake *>(self)->name; }

// al with junk above it, as the game's functions leave eax.
uint32_t Al(bool value) { return (gR() & 0xffffff00u) | (value ? 1u : 0u); }

// The thread's epoch of the CRT's thread-safe statics: a guard at or below it reads as initialised.
int32_t StaticsEpoch(difftest::Context &ctx)
{
    auto *block = static_cast<uint32_t *>(reinterpret_cast<void **>(__readfsdword(0x2c))[*ctx.image.At<uint32_t>(kTlsIndex)]);
    return static_cast<int32_t>(block[1]);
}

} // namespace

// ---- 0xc27961 AppendEditionSuffix: every language id 0..40 (the table's and others), sources with and without a "2",
// short and long, and out strings that held something before.

DIFF_TEST(AppFW_AppendEditionSuffix, kAppendEditionSuffix)
{
    using Fn = void(__cdecl *)(const cIGZString *, cIGZString *, uint32_t);
    auto original = ctx.Original<Fn>(kAppendEditionSuffix);
    static const char *const kSources[] = {"The Sims 2",           "Les Sims 2 : Deluxe", "Die Sims", "", "2", "22 and 2",
                                           "The Sims 2 Legacy with a tail longer than the small buffer"};
    static const char *const kStarts[] = {"", "old", "an old text longer than the small string buffer"};
    unsigned run = 0;
    for (uint32_t language = 0; language <= 40; ++language) {
        for (const char *source : kSources) {
            for (const char *start : kStarts) {
                std::string results[2];
                for (int side = 0; side < 2; ++side) {
                    Text in(source), out(start);
                    (side == 0 ? original : &appfw::AppendEditionSuffix)(in.Face(), out.Face(), language);
                    results[side] = Hex(out.s.mString) + " / " + in.s.mString;
                }
                Compare(ctx, run++, results);
            }
        }
    }
    // A language id with high bits (the original compares the whole word).
    std::string results[2];
    for (int side = 0; side < 2; ++side) {
        Text in("The Sims 2"), out;
        (side == 0 ? original : &appfw::AppendEditionSuffix)(in.Face(), out.Face(), 0x10003);
        results[side] = Hex(out.s.mString);
    }
    Compare(ctx, run, results);
}

// ---- 0xb417c0 BodyShopProductName: the twenty name globals hold distinct texts (one long), the language service's
// slot 7 answers every id 0..40 and a few large ones; compared: al, out's text and the language calls.

namespace {

uint32_t gLanguageId = 0;
uint32_t __fastcall LanguageId(void *self, void *)
{
    Log(Fmt("%s.id", NameOf(self)));
    return gLanguageId;
}
Fake gLanguage("language");

} // namespace

DIFF_TEST(AppFW_BodyShopProductName, kBodyShopProductName)
{
    gLanguage.Clear();
    gLanguage.Set(7, &LanguageId);
    adt::GlobalOverride<uint32_t> languageCache(ctx, kLanguageCache, Address(&gLanguage));
    constexpr int kNames = 20;
    uint8_t saved[kNames * 0x20];
    std::memcpy(saved, ctx.image.At<uint8_t>(kBodyShopNames), sizeof(saved));
    for (int i = 0; i < kNames; ++i) {
        auto *name = reinterpret_cast<cRZString *>(ctx.image.At<uint8_t>(kBodyShopNames + 0x20 * i));
        name->Construct(i == 7 ? Fmt("Body Shop %d, a name past the small string buffer", i).c_str() : Fmt("Body Shop %d", i).c_str());
    }
    using Fn = uint8_t(__cdecl *)(cIGZString *);
    auto original = ctx.Original<Fn>(kBodyShopProductName);
    std::vector<uint32_t> ids;
    for (uint32_t i = 0; i <= 40; ++i) ids.push_back(i);
    for (uint32_t id : {0x7fffffffu, 0x80000000u, 0xffffffffu, 0x100u, 0x103u}) ids.push_back(id);
    unsigned run = 0;
    for (uint32_t id : ids) {
        for (const char *start : {"", "previous text past the small string buffer"}) {
            gLanguageId = id;
            std::string results[2];
            for (int side = 0; side < 2; ++side) {
                gLog.clear();
                Text out(start);
                const uint8_t al = side == 0 ? original(out.Face()) : static_cast<uint8_t>(appfw::BodyShopProductName(out.Face()));
                results[side] = Fmt("al=%u [%s] ", al, out.s.mString.c_str()) + gLog;
            }
            Compare(ctx, run++, results);
        }
    }
    for (int i = 0; i < kNames; ++i) reinterpret_cast<cRZString *>(ctx.image.At<uint8_t>(kBodyShopNames + 0x20 * i))->Destruct();
    std::memcpy(ctx.image.At<uint8_t>(kBodyShopNames), saved, sizeof(saved));
}

// ---- 0xc27b49 ProcessPagefileUsage. The image's two thread-safe statics cannot run their first-use path here (the
// image's CRT is not initialised), so they are set as that path leaves them (psapi's handle, the function) with their
// guards at the thread's epoch; ours runs its first use once, which is checked against what the original's path does
// (GetVersion, then LoadLibraryA("psapi.dll") on the NT line, then GetProcAddress(psapi, "GetProcessMemoryInfo")). Then
// both sides run with the call failing or answering scripted counters.
// Not covered on our side: the Win9x line and a psapi without the function (our statics are set once per process).

namespace {

constexpr uint32_t kPsapiHandle = 0x5a5a0000;
bool gMemoryInfoOk = true;
SIZE_T gPagefileUsage = 0;

DWORD WINAPI FakeGetVersion()
{
    Log("GetVersion");
    return 0x23f00206;
}
HMODULE WINAPI FakeLoadLibraryA(const char *name)
{
    Log(Fmt("LoadLibraryA(%s)", name));
    return reinterpret_cast<HMODULE>(static_cast<uintptr_t>(kPsapiHandle));
}
BOOL WINAPI FakeGetProcessMemoryInfo(HANDLE process, PROCESS_MEMORY_COUNTERS *counters, DWORD size)
{
    bool zeroed = true;
    for (DWORD i = sizeof(DWORD); i < sizeof(PROCESS_MEMORY_COUNTERS); ++i) zeroed = zeroed && reinterpret_cast<uint8_t *>(counters)[i] == 0;
    Log(Fmt("GetProcessMemoryInfo(%p, cb=%x, zeroed=%d, %x)", static_cast<void *>(process), counters->cb, int(zeroed), size));
    // A failing call may still have written the buffer (its contents are undefined then): the answer must not come from it.
    counters->PagefileUsage = gPagefileUsage;
    if (!gMemoryInfoOk) return FALSE;
    counters->PageFaultCount = 0x11;
    counters->WorkingSetSize = 0x22;
    counters->PagefileUsage = gPagefileUsage;
    counters->PeakPagefileUsage = 0x44;
    return TRUE;
}
FARPROC WINAPI FakeGetProcAddress(HMODULE module, const char *name)
{
    Log(Fmt("GetProcAddress(%p, %s)", static_cast<void *>(module), name));
    return reinterpret_cast<FARPROC>(&FakeGetProcessMemoryInfo);
}
HANDLE WINAPI FakeGetCurrentProcess()
{
    Log("GetCurrentProcess");
    return reinterpret_cast<HANDLE>(static_cast<intptr_t>(-1));
}

} // namespace

DIFF_TEST(AppFW_ProcessPagefileUsage, kPagefileUsage)
{
    adt::GlobalOverride<void *> getVersion(ctx, kIatGetVersion, reinterpret_cast<void *>(&FakeGetVersion));
    adt::GlobalOverride<void *> loadLibrary(ctx, kIatLoadLibraryA, reinterpret_cast<void *>(&FakeLoadLibraryA));
    adt::GlobalOverride<void *> getProcAddress(ctx, kIatGetProcAddress, reinterpret_cast<void *>(&FakeGetProcAddress));
    adt::GlobalOverride<void *> currentProcess(ctx, kIatGetCurrentProcess, reinterpret_cast<void *>(&FakeGetCurrentProcess));
    adt::GlobalOverride<uint32_t> module(ctx, kPsapiModule, kPsapiHandle);
    adt::GlobalOverride<uint32_t> function(ctx, kMemoryInfoFn, Address(reinterpret_cast<void *>(&FakeGetProcessMemoryInfo)));
    adt::GlobalOverride<int32_t> moduleGuard(ctx, kPsapiModuleGuard, StaticsEpoch(ctx));
    adt::GlobalOverride<int32_t> functionGuard(ctx, kMemoryInfoFnGuard, StaticsEpoch(ctx));

    // Our first use (once per process).
    static bool firstUse = true;
    if (firstUse) {
        firstUse = false;
        gLog.clear();
        gMemoryInfoOk = false;
        appfw::ProcessPagefileUsage();
        const std::string expected = Fmt("GetVersion LoadLibraryA(psapi.dll) GetProcAddress(%p, GetProcessMemoryInfo) ",
                                         reinterpret_cast<void *>(static_cast<uintptr_t>(kPsapiHandle)));
        ++ctx.cases;
        if (gLog.find(expected) != 0) ctx.Fail("first use: %s (expected it to start with %s)", gLog.c_str(), expected.c_str());
    }

    using Fn = uint32_t(__cdecl *)();
    auto original = ctx.Original<Fn>(kPagefileUsage);
    static const SIZE_T kUsages[] = {0, 1, 0x3ff, 0x400, 0x12345678, 0xffffffff};
    unsigned run = 0;
    for (bool ok : {true, false}) {
        for (SIZE_T usage : kUsages) {
            gMemoryInfoOk = ok;
            gPagefileUsage = usage;
            std::string results[2];
            for (int side = 0; side < 2; ++side) {
                gLog.clear();
                const uint32_t value = side == 0 ? original() : appfw::ProcessPagefileUsage();
                results[side] = Fmt("%x ", value) + gLog;
            }
            Compare(ctx, run++, results);
        }
    }
}

// ---- 0x1b1a3f SpawnAndWait: the CRT's _spawnv is a logging stand-in on both sides (the image's copy and ours).

namespace {

intptr_t gSpawnResult = 0;
intptr_t __cdecl FakeSpawnv(int mode, const char *path, const char *const *argv)
{
    std::string line = Fmt("_spawnv(%d, %s", mode, path ? path : "null");
    for (int i = 0; i < 6 && argv && argv[i]; ++i) line += Fmt(", %s", argv[i]);
    Log(line + ")");
    return gSpawnResult;
}

} // namespace

DIFF_TEST(AppFW_SpawnAndWait, kSpawnAndWait)
{
    shape::StandIn spawn{kSpawnv, &_spawnv, &FakeSpawnv};
    using Fn = intptr_t(__cdecl *)(const char *, const char *const *);
    auto original = ctx.Original<Fn>(kSpawnAndWait);
    const char *const argvA[] = {"C:\\x\\ExceptionUploader.exe", "-logDir:\"C:/logs\"", "-noUpload", nullptr};
    const char *const argvB[] = {"a.exe", nullptr};
    unsigned run = 0;
    for (intptr_t result : {intptr_t(0), intptr_t(-1), intptr_t(7)}) {
        for (const char *const *argv : {argvA, argvB}) {
            gSpawnResult = result;
            std::string results[2];
            for (int side = 0; side < 2; ++side) {
                gLog.clear();
                const intptr_t r = (side == 0 ? original : &appfw::SpawnAndWait)(argv[0], argv);
                results[side] = Fmt("%d ", int(r)) + gLog;
            }
            Compare(ctx, run++, results);
        }
    }
}

// ---- 0xc2896d IsUserAdministrator: every import is a logging fake that succeeds or fails as the run's script says
// (each step on its own), handles and buffers named in the log; the version on the NT and Win9x lines, the thread token
// missing (ERROR_NO_TOKEN) or failing otherwise, the access check's answer.

namespace {

struct AdminScript {
    bool win9x = false;
    uint32_t okBits = ~0u; // bit i: step i succeeds
    DWORD lastError = ERROR_NO_TOKEN;
    BOOL accessStatus = TRUE;
    BOOL groupAnswer = TRUE;
};
AdminScript gAdmin;
uint8_t gBuffers[2][256];
int gBufferCount = 0;
uint8_t gSid[16];

bool Step(int i) { return (gAdmin.okBits >> i) & 1; }

std::string Name(const void *p)
{
    if (!p) return "null";
    if (p == gBuffers[0]) return "buf0";
    if (p == gBuffers[1]) return "buf1";
    if (p == gSid) return "sid";
    return Fmt("%p", p);
}

DWORD WINAPI AdminGetVersion()
{
    Log("GetVersion");
    return gAdmin.win9x ? 0xc0000a04 : 0x23f00206;
}
HANDLE WINAPI AdminGetCurrentThread()
{
    Log("GetCurrentThread");
    return reinterpret_cast<HANDLE>(static_cast<intptr_t>(-2));
}
HANDLE WINAPI AdminGetCurrentProcess()
{
    Log("GetCurrentProcess");
    return reinterpret_cast<HANDLE>(static_cast<intptr_t>(-1));
}
DWORD WINAPI AdminGetLastError()
{
    Log("GetLastError");
    return gAdmin.lastError;
}
BOOL WINAPI AdminOpenThreadToken(HANDLE thread, DWORD access, BOOL self, PHANDLE out)
{
    Log(Fmt("OpenThreadToken(%p, %x, %d, %x)", static_cast<void *>(thread), access, self, Address(*out)));
    if (!Step(0)) return FALSE;
    *out = reinterpret_cast<HANDLE>(0x1001);
    return TRUE;
}
BOOL WINAPI AdminOpenProcessToken(HANDLE process, DWORD access, PHANDLE out)
{
    Log(Fmt("OpenProcessToken(%p, %x, %x)", static_cast<void *>(process), access, Address(*out)));
    if (!Step(1)) return FALSE;
    *out = reinterpret_cast<HANDLE>(0x1002);
    return TRUE;
}
BOOL WINAPI AdminDuplicateToken(HANDLE token, SECURITY_IMPERSONATION_LEVEL level, PHANDLE out)
{
    Log(Fmt("DuplicateToken(%p, %d, %x)", static_cast<void *>(token), int(level), Address(*out)));
    if (!Step(2)) return FALSE;
    *out = reinterpret_cast<HANDLE>(0x1003);
    return TRUE;
}
BOOL WINAPI AdminAllocateSid(PSID_IDENTIFIER_AUTHORITY authority, BYTE count, DWORD s0, DWORD s1, DWORD s2, DWORD s3, DWORD s4, DWORD s5,
                             DWORD s6, DWORD s7, PSID *out)
{
    std::string a;
    for (BYTE b : authority->Value) a += Fmt("%02x", b);
    Log(Fmt("AllocateAndInitializeSid(%s, %u, %x %x %x %x %x %x %x %x, %x)", a.c_str(), count, s0, s1, s2, s3, s4, s5, s6, s7, Address(*out)));
    if (!Step(3)) return FALSE;
    *out = gSid;
    return TRUE;
}
HLOCAL WINAPI AdminLocalAlloc(UINT flags, SIZE_T size)
{
    const int which = gBufferCount++;
    Log(Fmt("LocalAlloc(%x, %x)", flags, unsigned(size)));
    if (!Step(4 + which)) return nullptr;
    return gBuffers[which & 1];
}
BOOL WINAPI AdminInitSd(PSECURITY_DESCRIPTOR sd, DWORD revision)
{
    Log(Fmt("InitializeSecurityDescriptor(%s, %u)", Name(sd).c_str(), revision));
    return Step(6);
}
DWORD WINAPI AdminGetLengthSid(PSID sid)
{
    Log(Fmt("GetLengthSid(%s)", Name(sid).c_str()));
    return 12;
}
BOOL WINAPI AdminInitAcl(PACL acl, DWORD size, DWORD revision)
{
    Log(Fmt("InitializeAcl(%s, %x, %u)", Name(acl).c_str(), size, revision));
    return Step(7);
}
BOOL WINAPI AdminAddAce(PACL acl, DWORD revision, DWORD mask, PSID sid)
{
    Log(Fmt("AddAccessAllowedAce(%s, %u, %x, %s)", Name(acl).c_str(), revision, mask, Name(sid).c_str()));
    return Step(8);
}
BOOL WINAPI AdminSetDacl(PSECURITY_DESCRIPTOR sd, BOOL present, PACL acl, BOOL defaulted)
{
    Log(Fmt("SetSecurityDescriptorDacl(%s, %d, %s, %d)", Name(sd).c_str(), present, Name(acl).c_str(), defaulted));
    return Step(9);
}
BOOL WINAPI AdminSetGroup(PSECURITY_DESCRIPTOR sd, PSID sid, BOOL defaulted)
{
    Log(Fmt("SetSecurityDescriptorGroup(%s, %s, %d)", Name(sd).c_str(), Name(sid).c_str(), defaulted));
    return gAdmin.groupAnswer;
}
BOOL WINAPI AdminSetOwner(PSECURITY_DESCRIPTOR sd, PSID sid, BOOL defaulted)
{
    Log(Fmt("SetSecurityDescriptorOwner(%s, %s, %d)", Name(sd).c_str(), Name(sid).c_str(), defaulted));
    return !gAdmin.groupAnswer;
}
BOOL WINAPI AdminIsValid(PSECURITY_DESCRIPTOR sd)
{
    Log(Fmt("IsValidSecurityDescriptor(%s)", Name(sd).c_str()));
    return Step(10);
}
BOOL WINAPI AdminAccessCheck(PSECURITY_DESCRIPTOR sd, HANDLE token, DWORD desired, PGENERIC_MAPPING mapping, PPRIVILEGE_SET privileges,
                             LPDWORD privilegesLength, LPDWORD granted, LPBOOL status)
{
    Log(Fmt("AccessCheck(%s, %p, %x, {%x %x %x %x}, %s, %x, %s, %x)", Name(sd).c_str(), static_cast<void *>(token), desired,
            mapping->GenericRead, mapping->GenericWrite, mapping->GenericExecute, mapping->GenericAll, privileges ? "set" : "null",
            *privilegesLength, granted ? "set" : "null", *status));
    if (!Step(11)) {
        *status = 0x55; // what a failing call might leave: the original then clears it
        return FALSE;
    }
    *granted = 3;
    *status = gAdmin.accessStatus;
    return TRUE;
}
HLOCAL WINAPI AdminLocalFree(HLOCAL p)
{
    Log(Fmt("LocalFree(%s)", Name(p).c_str()));
    return nullptr;
}
PVOID WINAPI AdminFreeSid(PSID sid)
{
    Log(Fmt("FreeSid(%s)", Name(sid).c_str()));
    return nullptr;
}
BOOL WINAPI AdminCloseHandle(HANDLE h)
{
    Log(Fmt("CloseHandle(%p)", static_cast<void *>(h)));
    return TRUE;
}

struct AdminImport {
    uint32_t rva;
    const void *fake;
};

} // namespace

DIFF_TEST(AppFW_IsUserAdministrator, kIsUserAdministrator)
{
    const AdminImport imports[] = {
        {0xd9a11c, reinterpret_cast<const void *>(&AdminGetVersion)},
        {0xd9a190, reinterpret_cast<const void *>(&AdminGetCurrentThread)},
        {0xd9a1b8, reinterpret_cast<const void *>(&AdminGetCurrentProcess)},
        {0xd9a118, reinterpret_cast<const void *>(&AdminGetLastError)},
        {0xd9a04c, reinterpret_cast<const void *>(&AdminOpenThreadToken)},
        {0xd9a05c, reinterpret_cast<const void *>(&AdminOpenProcessToken)},
        {0xd9a014, reinterpret_cast<const void *>(&AdminDuplicateToken)},
        {0xd9a018, reinterpret_cast<const void *>(&AdminAllocateSid)},
        {0xd9a3e8, reinterpret_cast<const void *>(&AdminLocalAlloc)},
        {0xd9a004, reinterpret_cast<const void *>(&AdminInitSd)},
        {0xd9a00c, reinterpret_cast<const void *>(&AdminGetLengthSid)},
        {0xd9a008, reinterpret_cast<const void *>(&AdminInitAcl)},
        {0xd9a01c, reinterpret_cast<const void *>(&AdminAddAce)},
        {0xd9a050, reinterpret_cast<const void *>(&AdminSetDacl)},
        {0xd9a038, reinterpret_cast<const void *>(&AdminSetGroup)},
        {0xd9a034, reinterpret_cast<const void *>(&AdminSetOwner)},
        {0xd9a060, reinterpret_cast<const void *>(&AdminIsValid)},
        {0xd9a020, reinterpret_cast<const void *>(&AdminAccessCheck)},
        {0xd9a130, reinterpret_cast<const void *>(&AdminLocalFree)},
        {0xd9a010, reinterpret_cast<const void *>(&AdminFreeSid)},
        {0xd9a17c, reinterpret_cast<const void *>(&AdminCloseHandle)},
    };
    std::vector<std::unique_ptr<adt::GlobalOverride<const void *>>> patches;
    for (const AdminImport &i : imports) patches.push_back(std::make_unique<adt::GlobalOverride<const void *>>(ctx, i.rva, i.fake));

    using Fn = uint32_t(__cdecl *)();
    auto original = ctx.Original<Fn>(kIsUserAdministrator);
    std::mt19937 rng(kIsUserAdministrator);
    for (unsigned run = 0; run < 3000; ++run) {
        gAdmin = AdminScript{};
        gAdmin.win9x = run % 50 == 7;
        if (run < 14) {
            gAdmin.okBits = run < 12 ? ~(1u << run) : ~0u; // each step failing alone, then none
        } else {
            gAdmin.okBits = 0;
            for (int i = 0; i < 12; ++i)
                if (rng() % 10) gAdmin.okBits |= 1u << i;
        }
        static const DWORD kErrors[] = {ERROR_NO_TOKEN, ERROR_NO_TOKEN, ERROR_ACCESS_DENIED, ERROR_INVALID_HANDLE};
        gAdmin.lastError = kErrors[rng() % 4];
        gAdmin.accessStatus = rng() % 2;
        gAdmin.groupAnswer = rng() % 2;
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            gBufferCount = 0;
            const uint32_t status = side == 0 ? original() : appfw::IsUserAdministrator();
            results[side] = Fmt("%x ", status) + gLog;
        }
        Compare(ctx, run, results);
    }
}

// ---- 0xc28b7d LaunchExceptionUploader: the options (0x1d0c00 and ours) answer directories with '/' and '\', PathExists
// (0x1c1242 and ours) answers as scripted, the conversion (0x1b025d and ours) marks what it converted, and the spawn
// (0x1b1a3f and ours) logs the path and argv. Every flag combination; `this` is junk (not read).

namespace {

std::string gAppDirectory, gLogsDirectory;
bool gUploaderExists = true;

uint32_t __cdecl FakeOption(uint32_t key, uint32_t out, uint32_t c, uint32_t d, uint32_t e)
{
    auto *text = reinterpret_cast<cRZString *>(static_cast<uintptr_t>(out));
    Log(Fmt("option(%x, [%s], %x, %x, %x)", key, text->mString.c_str(), c, d, e));
    if (key == 0x6bcfce93) text->mString = gAppDirectory;
    else if (key == 0x4d4896ba) text->mString = gLogsDirectory;
    else if (key == 0x4c4324c5) text->mString = gLogsDirectory + "Neighborhoods/";
    else text->mString = Fmt("option%x/", key);
    return Al(true);
}
bool __cdecl FakePathExists(const cIGZString &path)
{
    Log(Fmt("exists(%s)", path.ToChar()));
    return gUploaderExists;
}
bool __cdecl FakeConvert(const cIGZString &src, cIGZString &dst, int32_t encoding)
{
    const std::string text = src.ToChar();
    Log(Fmt("convert(%s, %s, %d)", text.c_str(), &src == &dst ? "same" : "other", encoding));
    dst.FromChar((text + "~").c_str());
    return true;
}
intptr_t __cdecl FakeSpawn(const char *path, const char *const *argv)
{
    std::string line = Fmt("spawn(%s", path);
    for (int i = 0; i < 5; ++i) line += Fmt(", %s", argv[i] ? argv[i] : "null");
    Log(line + ")");
    return 0;
}

} // namespace

DIFF_TEST(cTSAppFW_LaunchExceptionUploader, kLaunchUploader)
{
    shape::StandIn option{kCommonImplSlot4, &appinit::CommonImplSlot4, &FakeOption};
    shape::StandIn exists{kPathExists, &cRZFile::PathExists, &FakePathExists};
    shape::StandIn convert{kConvertStringToString, &RZText::ConvertStringToString, &FakeConvert};
    shape::StandIn spawn{kSpawnAndWait, &appfw::SpawnAndWait, &FakeSpawn};
    using Fn = void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
    auto original = ctx.Original<Fn>(kLaunchUploader);
    auto ours = reinterpret_cast<Fn>(shape::CodeOf(&ts::cTSAppFW::LaunchExceptionUploader));
    static const char *const kAppDirectories[] = {"C:/Program Files/EA/TS2/TSBin/", "D:\\Games\\Sims\\", "", "a/b/c/d/e/f/g/h/i/j/k/l/m/n/"};
    static const char *const kLogsDirectories[] = {"C:/Users/sim/Documents/EA Games/The Sims 2/Logs/", "", "L\\"};
    unsigned run = 0;
    for (const char *app : kAppDirectories) {
        for (const char *logs : kLogsDirectories) {
            // Every 0 / 1 combination (bits 0..2; bit 3: the uploader missing), then noUpload bytes other than 0 / 1 with
            // the other two 0 (what PreFrameworkInit can pass: its switch answer's byte; the original indexes argv with
            // it, see the source), and junk above the bytes (the callee reads bytes).
            for (unsigned flags = 0; flags < 20; ++flags) {
                gAppDirectory = app;
                gLogsDirectory = logs;
                gUploaderExists = (flags & 8) == 0;
                uint32_t noUpload = flags & 1, noLog = (flags >> 1) & 1, esper = (flags >> 2) & 1;
                if (flags >= 16) {
                    static const uint32_t kOdd[] = {2, 0xff, 0x80, 0x7e};
                    noUpload = kOdd[flags - 16];
                    noLog = esper = 0;
                    gUploaderExists = true;
                }
                const uint32_t junk = 0xa5c3e100u * (run % 3);
                std::string results[2];
                for (int side = 0; side < 2; ++side) {
                    gLog.clear();
                    gR.seed(run);
                    (side == 0 ? original : ours)(reinterpret_cast<void *>(0x0bad0044), noUpload | junk, noLog | junk, esper | junk);
                    results[side] = gLog;
                }
                Compare(ctx, run++, results);
            }
        }
    }
}

// ---- 0xc28de0 ReportFatal: an application-shaped block whose +0x244 text is a fake (ToChar, slot 5) and whose flag
// byte +0x264 takes every value of its three low bits; the framework's slot 21 logs; the error box (0xc2da5a and ours)
// and the uploader (0xc28b7d and ours) log their arguments.

namespace {

void *__fastcall FwCommandLine(void *self, void *)
{
    Log(Fmt("%s.commandLine", NameOf(self)));
    return reinterpret_cast<void *>(0xc0de0021);
}
Fake gFramework("framework");

uint8_t *gAppBlock = nullptr;
std::string AppOffset(const void *p)
{
    if (gAppBlock && p >= gAppBlock && p < gAppBlock + 0x268) return Fmt("app+%x", unsigned(static_cast<const uint8_t *>(p) - gAppBlock));
    return Fmt("%p", p);
}
void __cdecl FakeFatalMessage(const char *text, const char *caption)
{
    Log(Fmt("fatal(%s, %s)", text, caption));
}
void __fastcall FakeUploader(void *self, void *, uint32_t noUpload, uint32_t noLog, uint32_t esper)
{
    Log(Fmt("uploader(%s, %u, %u, %u)", AppOffset(self).c_str(), noUpload & 0xff, noLog & 0xff, esper & 0xff)); // bytes are read
}

// The application-shaped block: 0x268 bytes of junk, the text fake at +0x244, the flag byte at +0x264.
struct AppBlock {
    alignas(8) uint8_t bytes[0x268];
    AppBlock(Fake *text, uint8_t flags)
    {
        for (unsigned i = 0; i < sizeof(bytes); ++i) bytes[i] = static_cast<uint8_t>(0xa5 ^ i);
        *reinterpret_cast<Fake **>(bytes + 0x244) = nullptr;
        std::memcpy(bytes + 0x244, text, sizeof(void *)); // the fake's vtable pointer: +0x244 acts as the text object
        bytes[0x264] = flags;
        gAppBlock = bytes;
    }
    ~AppBlock() { gAppBlock = nullptr; }
};

const char *__fastcall AppTextToChar(void *self, void *)
{
    Log(Fmt("text(%s).ToChar", AppOffset(self).c_str()));
    return "Something went wrong.";
}
Fake gAppText("app text");

} // namespace

DIFF_TEST(cTSAppFW_ReportFatal, kReportFatal)
{
    gFramework.Clear();
    gFramework.Set(21, &FwCommandLine);
    gAppText.Clear();
    gAppText.Set(5, &AppTextToChar);
    adt::GlobalOverride<uint32_t> framework(ctx, kFrameworkGlobal, Address(&gFramework));
    uint8_t savedCaption[0x20];
    std::memcpy(savedCaption, ctx.image.At<uint8_t>(kAppCaption), sizeof(savedCaption));
    reinterpret_cast<cRZString *>(ctx.image.At<uint8_t>(kAppCaption))->Construct("The Sims 2 Legacy");
    {
        shape::StandIn fatal{kShowFatalMessage, &winmain::ShowFatalMessage, &FakeFatalMessage};
        shape::StandIn uploader{kLaunchUploader, &ts::cTSAppFW::LaunchExceptionUploader, &FakeUploader};
        using Fn = void(__thiscall *)(void *);
        auto original = ctx.Original<Fn>(kReportFatal);
        auto ours = reinterpret_cast<Fn>(shape::CodeOf(&ts::cTSAppFW::ReportFatal));
        for (unsigned flags = 0; flags < 256; flags += 1) {
            std::string results[2];
            for (int side = 0; side < 2; ++side) {
                gLog.clear();
                AppBlock app(&gAppText, static_cast<uint8_t>(flags));
                (side == 0 ? original : ours)(app.bytes);
                results[side] = gLog;
            }
            Compare(ctx, flags, results);
        }
    }
    reinterpret_cast<cRZString *>(ctx.image.At<uint8_t>(kAppCaption))->Destruct();
    std::memcpy(ctx.image.At<uint8_t>(kAppCaption), savedCaption, sizeof(savedCaption));
}

// ---- 0xc28e32 AddCrashData: the reporter's AddExtraData (0x1bac12 and ours) logs; cRZVersion's constructor and
// destructor (0x1c98d6 / 0x1c9901 and ours) log; its host name (0x1c99d2, the original on both sides) writes the run's
// name; the page file usage (0xc27b49 and ours) answers the run's value; the game state (0x20dde0 and ours) is null or a
// fake whose neighbourhood, lot and family are there or not.

namespace {

struct CrashScript {
    uint32_t usage = 0;
    bool state = true, neighborhood = true, lot = true, family = true;
    const char *host = "SIMSPC";
};
CrashScript gCrash;

void __fastcall FakeAddExtraData(void *self, void *, const char *label, const char *value, uint32_t unused)
{
    Log(Fmt("extra(%s, %s, %s, %x)", AppOffset(self).c_str(), label, value, unused));
}
void *__fastcall FakeVersionConstruct(void *self, void *)
{
    Log("version()");
    std::memset(self, 0, sizeof(appbase::cRZVersion));
    return self;
}
void __fastcall FakeVersionDestruct(void *, void *) { Log("~version"); }
uint32_t __fastcall FakeHostName(void *, void *, cRZString *out, const cRZString *fallback)
{
    Log(Fmt("hostName([%s], %s)", out->mString.c_str(), fallback ? "fallback" : "null"));
    out->mString = gCrash.host;
    return Al(true);
}
uint32_t __cdecl FakeUsage()
{
    Log("usage");
    return gCrash.usage;
}

Fake gState("state"), gNeighborhood("neighborhood"), gLot("lot"), gFamily("family");
Fake gNeighborhoodName("Pleasantview"), gNeighborhoodPrefix("N001"), gLotName("Goth House"), gFamilyName("Goth");

void *__fastcall StateNeighborhood(void *, void *)
{
    Log("state.neighborhood");
    return gCrash.neighborhood ? &gNeighborhood : nullptr;
}
void *__fastcall StateLot(void *, void *)
{
    Log("state.lot");
    return gCrash.lot ? &gLot : nullptr;
}
void *__fastcall StateFamily(void *, void *)
{
    Log("state.family");
    return gCrash.family ? &gFamily : nullptr;
}
void *__fastcall NeighborhoodName(void *, void *)
{
    Log("neighborhood.name");
    return &gNeighborhoodName;
}
void *__fastcall NeighborhoodPrefix(void *, void *)
{
    Log("neighborhood.prefix");
    return &gNeighborhoodPrefix;
}
void *__fastcall LotName(void *, void *)
{
    Log("lot.name");
    return &gLotName;
}
void *__fastcall FamilyName(void *, void *)
{
    Log("family.name");
    return &gFamilyName;
}
void *__cdecl FakeGameState()
{
    Log("gameState");
    return gCrash.state ? &gState : nullptr;
}

} // namespace

DIFF_TEST(cTSAppFW_AddCrashData, kAddCrashData)
{
    gState.Clear();
    gState.Set(6, &StateNeighborhood);
    gState.Set(9, &StateLot);
    gState.Set(14, &StateFamily);
    gNeighborhood.Clear();
    gNeighborhood.Set(4, &NeighborhoodName);
    gNeighborhood.Set(8, &NeighborhoodPrefix);
    gLot.Clear();
    gLot.Set(19, &LotName);
    gFamily.Clear();
    gFamily.Set(37, &FamilyName);
    for (Fake *name : {&gNeighborhoodName, &gNeighborhoodPrefix, &gLotName, &gFamilyName}) {
        name->Clear();
        name->Set(5, &FakeToChar);
    }
    shape::StandIn extra{kAddExtraData, &appbase::ExceptionReporterObject::AddExtraData, &FakeAddExtraData};
    shape::StandIn versionConstruct{kVersionConstruct, &appbase::cRZVersion::Construct, &FakeVersionConstruct};
    shape::StandIn versionDestruct{kVersionDestruct, &appbase::cRZVersion::Destruct, &FakeVersionDestruct};
    shape::CodePatch hostName{kVersionHostName, reinterpret_cast<const void *>(&FakeHostName)};
    shape::StandIn usage{kPagefileUsage, &appfw::ProcessPagefileUsage, &FakeUsage};
    shape::StandIn gameState{kGetCachedSlot23, &GetCachedSlot23, &FakeGameState};
    using Fn = void(__thiscall *)(void *);
    auto original = ctx.Original<Fn>(kAddCrashData);
    auto ours = reinterpret_cast<Fn>(shape::CodeOf(&ts::cTSAppFW::AddCrashData));
    static const uint32_t kUsages[] = {0, 0x3ff, 0x400, 0x7654321, 0xffffffff};
    static const char *const kHosts[] = {"SIMSPC", "", "a-host-name-longer-than-the-small-string-buffer"};
    std::mt19937 rng(kAddCrashData);
    for (unsigned run = 0; run < 600; ++run) {
        gCrash.usage = kUsages[run % 5];
        gCrash.host = kHosts[(run / 5) % 3];
        const unsigned bits = run < 32 ? run : rng();
        gCrash.state = bits & 1;
        gCrash.neighborhood = bits & 2;
        gCrash.lot = bits & 4;
        gCrash.family = bits & 8;
        const uint8_t flags = static_cast<uint8_t>(run < 32 ? (bits & 16 ? 4 : 0) : rng());
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            gR.seed(run);
            AppBlock app(&gAppText, flags);
            (side == 0 ? original : ours)(app.bytes);
            results[side] = gLog;
        }
        Compare(ctx, run, results);
    }
}

// ---- 0xc2a3d6 UnlockNeighborhoodFiles: the settings service (framework slot 5) finds the setting or not and answers
// its byte; the globals' slot 31 (0x20ddfb and ours) and the command line's switch (slot 9) as scripted; the
// neighbourhood's directory (0xc0b314, original on both sides) is written by a fake; the directory scan (0x1c48da,
// 0x1c49eb, 0x1c5047, 0x1c509f and ours) is a fake with the run's entries (some null); the wildcard match (0x9cce18,
// original on both sides) answers per name; the package service (cache 0x10faf6c) logs its slot 27; the options
// (0x1d0c00 and ours) answer the neighbourhoods directory.

namespace {

struct UnlockScript {
    bool settingFound = false;
    uint8_t settingValue = 0;
    bool globalsAllow = false;
    bool switchPresent = false;
    uint32_t count = 3;
    uint32_t nullMask = 0;  // bit i: entry i is null
    uint32_t matchMask = ~0u; // bit i: entry i matches
};
UnlockScript gUnlock;

Fake gSettings("settings"), gCommandLine("commandLine"), gGlobals("globals"), gPackages("packages");

uint32_t __fastcall SettingsGet(void *self, void *, const void *name, void *value)
{
    const uint32_t vt = *static_cast<const uint32_t *>(value);
    Log(Fmt("%s.get(name, value vt %x, byte %x)", NameOf(self), vt, static_cast<const uint8_t *>(value)[4]));
    if (gUnlock.settingFound) static_cast<uint8_t *>(value)[4] = gUnlock.settingValue;
    (void)name;
    return Al(gUnlock.settingFound);
}
uint32_t __fastcall SettingsRelease(void *self, void *)
{
    Log(Fmt("%s.release", NameOf(self)));
    return 0;
}
void *__fastcall FwCommandLineFake(void *self, void *)
{
    Log(Fmt("%s.commandLine", NameOf(self)));
    return &gCommandLine;
}
uint32_t __fastcall FwGetSystemService(void *self, void *, uint32_t id, uint32_t iid, void **out)
{
    Log(Fmt("%s.service(%x, %x)", NameOf(self), id, iid));
    *out = &gSettings;
    return Al(true);
}
uint32_t __fastcall CommandLineSwitch(void *self, void *, const cIGZString *name)
{
    Log(Fmt("%s.switch(%s)", NameOf(self), name->ToChar()));
    return Al(gUnlock.switchPresent);
}
uint32_t __fastcall GlobalsSlot31(void *self, void *)
{
    Log(Fmt("%s.slot31", NameOf(self)));
    return Al(gUnlock.globalsAllow);
}
void *__cdecl FakeGlobals()
{
    Log("globalsGetter");
    return &gGlobals;
}
uint32_t __fastcall PackagesUnlock(void *self, void *, cIGZString *path)
{
    Log(Fmt("%s.unlock(%s)", NameOf(self), path->ToChar()));
    return Al(true);
}
uint32_t __cdecl FakeNeighborhoodDirectory(void *name, cIGZString *out, uint32_t flag, uint32_t mask)
{
    Log(Fmt("hoodDirectory(%s, [%s], %x, %x)", NameOf(name), out->ToChar(), flag, mask));
    out->FromChar("C:/Users/sim/Neighborhoods/N001/");
    return Al(true);
}
uint32_t __cdecl FakeWildcardMatch(const cIGZString *name, const cIGZString *pattern, uint32_t caseSensitive)
{
    const std::string n = name->ToChar();
    const unsigned index = n.empty() ? 0 : unsigned(n.back() - '0');
    Log(Fmt("match(%s, %s, %x)", n.c_str(), pattern->ToChar(), caseSensitive & 0xff));
    return Al((gUnlock.matchMask >> index) & 1);
}

// The allocator service (cache 0x10c95d4, guard 0x10c95d8) the setting's name string allocates through: slot 3
// allocates, slot 4 frees, AddRef / Release do nothing; not logged (how the string keeps its text is not the function's).
void *__fastcall AllocatorAllocate(void *, void *, uint32_t size) { return std::malloc(size); }
void __fastcall AllocatorFree(void *, void *, void *p) { std::free(p); }
uint32_t __fastcall AllocatorCount(void *, void *) { return 1; }
Fake gAllocator("allocator");

// The scan's stand-ins: a block whose count is the run's, entries built once (names "entry0".."entry9").
struct EntryBlock {
    DirectoryScan *scan;
    union {
        cRZString name;
    };
    uint32_t kind;
    EntryBlock() {}
    ~EntryBlock() {}
};
EntryBlock gEntries[10];
bool gEntriesBuilt = false;

DirectoryScan *__fastcall FakeScanConstruct(void *memory, void *, const cRZString *directory, bool scanNow, bool keepDotDot, bool keepDot)
{
    Log(Fmt("scan(%s, %d, %d, %d)", directory->mString.c_str(), int(scanNow), int(keepDotDot), int(keepDot)));
    std::memset(memory, 0, DirectoryScan::kSize);
    static_cast<uint32_t *>(memory)[2] = gUnlock.count;
    return static_cast<DirectoryScan *>(memory);
}
void __fastcall FakeScanDestruct(DirectoryScan *, void *) { Log("~scan"); }
dscan::Entry *__fastcall FakeEntryAt(DirectoryScan *, void *, uint32_t index)
{
    Log(Fmt("entryAt(%u)", index));
    if (index >= 10 || ((gUnlock.nullMask >> index) & 1)) return nullptr;
    return reinterpret_cast<dscan::Entry *>(&gEntries[index]);
}
void __fastcall FakeFullPath(const dscan::Entry *entry, void *, cRZString *out)
{
    const auto *block = reinterpret_cast<const EntryBlock *>(entry);
    Log(Fmt("fullPath(%s, [%s])", block->name.mString.c_str(), out->mString.c_str()));
    out->mString = "C:/Users/sim/Neighborhoods/N001/" + block->name.mString;
}

} // namespace

DIFF_TEST(cTSAppFW_UnlockNeighborhoodFiles, kUnlockNeighborhood)
{
    gFramework.Clear();
    gFramework.Set(5, &FwGetSystemService);
    gFramework.Set(21, &FwCommandLineFake);
    gSettings.Clear();
    gSettings.Set(2, &SettingsRelease);
    gSettings.Set(9, &SettingsGet);
    gCommandLine.Clear();
    gCommandLine.Set(9, &CommandLineSwitch);
    gGlobals.Clear();
    gGlobals.Set(31, &GlobalsSlot31);
    gPackages.Clear();
    gPackages.Set(27, &PackagesUnlock);
    if (!gEntriesBuilt) {
        gEntriesBuilt = true;
        for (int i = 0; i < 10; ++i) {
            gEntries[i].scan = nullptr;
            gEntries[i].name.Construct(Fmt("entry%d", i).c_str());
            gEntries[i].kind = 8;
        }
    }
    Fake neighborhood("Pleasantview");
    adt::GlobalOverride<uint32_t> framework(ctx, kFrameworkGlobal, Address(&gFramework));
    adt::GlobalOverride<uint32_t> packages(ctx, kPackagesCache, Address(&gPackages));
    gAllocator.Clear();
    gAllocator.Set(1, &AllocatorCount);
    gAllocator.Set(2, &AllocatorCount);
    gAllocator.Set(3, &AllocatorAllocate);
    gAllocator.Set(4, &AllocatorFree);
    adt::GlobalOverride<uint32_t> allocator(ctx, 0x10c95d4, Address(&gAllocator));
    adt::GlobalOverride<int32_t> allocatorGuard(ctx, 0x10c95d8, INT32_MIN);
    shape::StandIn globals{kGetCachedSingleton, &GetCachedSingleton, &FakeGlobals};
    shape::StandIn option{kCommonImplSlot4, &appinit::CommonImplSlot4, &FakeOption};
    shape::CodePatch hoodDirectory{kNeighborhoodDirectory, reinterpret_cast<const void *>(&FakeNeighborhoodDirectory)};
    shape::CodePatch wildcard{kWildcardMatch, reinterpret_cast<const void *>(&FakeWildcardMatch)};
    shape::StandIn scanConstruct{kScanConstruct, &DirectoryScanAdapters::Construct, &FakeScanConstruct};
    shape::StandIn scanDestruct{kScanDestruct, &DirectoryScanAdapters::Destruct, &FakeScanDestruct};
    shape::StandIn entryAt{kScanEntryAt, &DirectoryScanAdapters::EntryAt, &FakeEntryAt};
    shape::StandIn fullPath{kEntryFullPath, &DirectoryScanAdapters::FullPath, &FakeFullPath};
    gLogsDirectory = "C:/Users/sim/";
    using Fn = void(__thiscall *)(void *, void *);
    auto original = ctx.Original<Fn>(kUnlockNeighborhood);
    auto ours = reinterpret_cast<Fn>(shape::CodeOf(&ts::cTSAppFW::UnlockNeighborhoodFiles));
    std::mt19937 rng(kUnlockNeighborhood);
    for (unsigned run = 0; run < 1500; ++run) {
        gUnlock = UnlockScript{};
        const unsigned bits = run < 32 ? run : rng();
        gUnlock.settingFound = bits & 1;
        gUnlock.settingValue = bits & 2 ? static_cast<uint8_t>(1 + rng() % 255) : 0;
        gUnlock.globalsAllow = bits & 4;
        gUnlock.switchPresent = bits & 8;
        gUnlock.count = run < 32 ? (bits & 16 ? 3 : 0) : rng() % 11;
        gUnlock.nullMask = run < 32 ? 0 : rng() & rng();
        gUnlock.matchMask = run < 32 ? ~0u : rng() | rng();
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            gR.seed(run);
            (side == 0 ? original : ours)(reinterpret_cast<void *>(0x0bad0044), &neighborhood);
            results[side] = gLog;
        }
        Compare(ctx, run, results);
    }
}
