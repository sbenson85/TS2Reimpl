#pragma once
// The application fixture shared by the cTSAppFW difftests (tests/app_ctor.cpp, tests/app_*.cpp): the test layout,
// the import fakes (Env), the command-line double, vtable codes, redirects and LiveApp (one side's application on either
// route, its six vtables replaced by copies so single slots can be faked or trapped). Everything is in an anonymous
// namespace: each test file has its own copy (log, switches, learned vtables).
#define NOMINMAX
#include "app/AppCtor.h"
#include "gz/cGZApp.h"
#include "rz/RZSystemServiceBase.h"
#include "ts/cTSAppFW.h"
#include "app/AppDebugger.h"
#include "app/AppInit.h"
#include "app/AppServices.h"
#include "app/AppServices3.h"
#include "gz/cGZClipboardManager.h"
#include "gz/cGZRegistry.h"
#include "gz/cGZTimerService.h"
#include "hook/hooks.h" // MemberAddress
#include "rz/RZMemory.h"
#include "rz/cGZAllocatorService.h"
#include "rz/cRZCallbackSystemService.h"
#include "rz/cRZNetManager.h"
#include "rz/nGZLog.h"
#include "ts/cTSString.h"
#include "ts_string_fake.h"
#include "difftest.h"
#include "game/game.h"
#include "rz/cRZMutex.h"
#include "rz/cRZString.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <functional>
#include <list>
#include <memory>
#include <new>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

using appctor::ListNode;
using appctor::TreeNode;

namespace {

// The application object as the tests read it: the old hook-shaped layout (the class itself is ts::cTSAppFW).
struct cTSAppFW {
    static constexpr uint32_t kSize = 0x268;
    static constexpr uint32_t kVTable = 0xe98464;        // +0x00 cGZApp
    static constexpr uint32_t kUnknownMTVTable = 0xe9849c; // +0x04
    static constexpr uint32_t kAppVTable = 0xe984b8;     // +0x1c cIGZApp
    static constexpr uint32_t kAppFWVTable = 0xe984f8;   // +0x40 cITSAppFW
    static constexpr uint32_t kCommonImplVTable = 0xe98510; // +0x44 cTSAppCommonImpl
    static constexpr uint32_t kMessageTargetVTable = 0xe9855c; // +0x5c cIGZMessageTarget2
    static constexpr uint32_t kTempAppFWVTable = 0xe9844c, kTempMessageTargetVTable = 0xda49bc;



    appbase::cGZApp mApp;                     // +0x00
    const void *mAppFWVTable;                 // +0x40
    appbase::cTSAppCommonImpl mCommonImpl;    // +0x44
    const void *mMessageTargetVTable;         // +0x5c
    void *mPtr60;                             // +0x60 cleared by the constructor
    std::set<uint32_t> mServiceIds;           // +0x64 0xc3, 0xce1505fd, 0xce, 0xcd
    uint8_t mByte6c;                          // +0x6c 1 after construction (OnTick clears it)
    uint8_t mOtherInstance;                   // +0x6d 1 when the mutex "Global\The Sims 2" was already held (or installUserData was not asked...)
    uint8_t mOtherBodyShop;                   // +0x6e 1 when "Global\The Sims 2 Body Shop" was already held
    uint8_t pad6f;
    uint32_t mWord70;                         // +0x70
    cRZMutex *mMutex;                         // +0x74 "Global\The Sims 2", held for the life of the application
    std::list<appctor::AutoRefElement> mList;          // +0x78 counted references, released by the destructor
    cIGZUnknown *mObject80;                   // +0x80 released by the destructor when set
    uint32_t mWord84;                         // +0x84 never written by the constructor
    uint32_t mWord88, mWord8c, mWord90;       // +0x88.. cleared
    uint32_t mShutdownMessage;                // +0x94 RegisterWindowMessageA("EA_LAUNCHER_SHUTDOWN_REQUEST_SIMS2EP")
    appbase::ExceptionReporterObject mReporter; // +0x98
    cRZString mString244;                     // +0x244
    uint8_t mFlags264;                        // +0x264 bit 0 quietCrash, bit 1 noExceptionUpload, bit 2 cleared
    uint8_t pad265[3];
};

static_assert(sizeof(cTSAppFW) == sizeof(ts::cTSAppFW), "the test layout and the class agree");

constexpr uint32_t kAppCtor = 0xc26147, kAppDtor = 0xc266b9, kAppDeleting = 0xc267d9, kThunkUnknownMT = 0xc26780, kThunkCommonImpl = 0xc26788;
constexpr uint32_t kGZAppDtor = 0xc59f91, kCommonImplDtor = 0xca4de0, kEraseSubtree = 0xca49f0, kDestroyList = 0x1d5e50, kDestroyElement = 0x1d8450;
constexpr uint32_t kReporterDtor = 0x1baae3;
constexpr uint32_t kIatSetFilter = 0xd9a1b4, kIatGetModuleFileNameA = 0xd9a16c, kIatRegisterWindowMessageA = 0xd9a4e4, kIatCoInitialize = 0xd9a6d4, kIatCoUninitialize = 0xd9a6e8;
constexpr uint32_t kModuleResolved = 0x10caeb5, kModulePointer = 0x10caeb8, kSystemCodePageCache = 0xff23a4, kDebuggerFunctionCache = 0x10cb074;
constexpr uint32_t kMutexVTable = 0xe9ebf0;

// The log is a fixed buffer: logging must not allocate, or a block the code under test just freed
// can be reused before the test measures it.
struct LogBuffer {
    char text[32768];
    size_t length = 0;
    void clear()
    {
        length = 0;
        text[0] = 0;
    }
} gLog;

void Log(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int n = std::vsnprintf(gLog.text + gLog.length, sizeof(gLog.text) - gLog.length - 2, fmt, args);
    va_end(args);
    if (n > 0) gLog.length += static_cast<size_t>(n);
    gLog.text[gLog.length++] = '\n';
    gLog.text[gLog.length] = 0;
}

std::string Joined()
{
    return gLog.text;
}

std::string Hex(const void *data, size_t n)
{
    std::string out;
    char b[4];
    for (size_t i = 0; i < n; ++i) {
        std::snprintf(b, sizeof(b), "%02x", static_cast<const uint8_t *>(data)[i]);
        out += b;
    }
    return out;
}

void ExpectSame(difftest::Context &ctx, const char *what, const std::string &a, const std::string &b)
{
    ++ctx.cases;
    if (a != b) {
        // From a little before the first difference.
        size_t first = 0;
        while (first < a.size() && first < b.size() && a[first] == b[first]) ++first;
        size_t from = first > 200 ? first - 200 : 0;
        std::string x = a.substr(from, 450);
        std::string y = b.substr(from, 450);
        for (char &c : x) if (c == '\n') c = '|';
        for (char &c : y) if (c == '\n') c = '|';
        ctx.Fail("%s:\n  original: %s\n  ours:     %s", what, x.c_str(), y.c_str());
    }
}

// ---- imports ----

void *WINAPI FakeSetFilter(void *filter)
{
    Log("SetUnhandledExceptionFilter(%s)", filter == nullptr ? "null" : "filter");
    return reinterpret_cast<void *>(0x7000 + (reinterpret_cast<uintptr_t>(filter) & 0xff));
}
const wchar_t *gWideName = L"C:\\Games\\The Sims 2\\EP9\\TSBin\\Sims2EP9.exe";
DWORD WINAPI FakeWide(void *, wchar_t *buffer, DWORD)
{
    std::wcscpy(buffer, gWideName);
    return static_cast<DWORD>(std::wcslen(gWideName));
}
DWORD WINAPI FakeNarrow(void *, char *buffer, DWORD)
{
    buffer[0] = 0;
    return 0;
}
UINT WINAPI FakeRegisterWindowMessage(const char *text)
{
    Log("RegisterWindowMessageA(%s)", text);
    return 0xC000 + static_cast<UINT>(std::strlen(text));
}
long WINAPI FakeCoInitialize(void *reserved)
{
    Log("CoInitialize(%p)", reserved);
    return 0;
}
void WINAPI FakeCoUninitialize()
{
    Log("CoUninitialize");
}

// The cRZMutex vtable entries wrapped to log (name of the mutex) and forward to the real code.
void *gRealMutexSlots[4];
const char *MutexName(void *mutex)
{
    auto *m = static_cast<cRZMutex *>(mutex);
    return m->mData->mName.mString.c_str();
}
uint32_t __fastcall WrapDelete(void *self, void *)
{
    Log("mutex[%s].Delete", MutexName(self));
    return reinterpret_cast<uint32_t(__thiscall *)(void *)>(gRealMutexSlots[0])(self);
}
uint32_t __fastcall WrapUnlock(void *self, void *)
{
    Log("mutex[%s].Unlock", MutexName(self));
    return reinterpret_cast<uint32_t(__thiscall *)(void *)>(gRealMutexSlots[2])(self);
}
uint32_t __fastcall WrapTryLock(void *self, void *)
{
    uint32_t r = reinterpret_cast<uint32_t(__thiscall *)(void *)>(gRealMutexSlots[3])(self);
    Log("mutex[%s].TryLock -> %u", MutexName(self), r);
    return r;
}

// The single-instance mutexes the constructor creates ("Global\The Sims 2", "... Body Shop") are machine-wide: while one
// difftest process holds them (a constructed application, a Holder), another's constructor sees "already running" on one
// side only. Env takes this lock for its life so that runners side by side (difftest --jobs) take turns. A test that
// crashes skips the destructor and keeps it until its runner ends: the wait gives up after a minute and goes on.
class SingleInstanceLock {
public:
    SingleInstanceLock() : mutex(CreateMutexW(nullptr, FALSE, L"Local\\ts2difftest app single-instance mutexes"))
    {
        if (mutex && WaitForSingleObject(mutex, 60000) == WAIT_TIMEOUT) {
            std::fprintf(stderr, "app_fixture: another difftest runner held the single-instance lock for a minute; going on without it\n");
            CloseHandle(mutex);
            mutex = nullptr;
        }
    }
    ~SingleInstanceLock()
    {
        if (!mutex) return;
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    SingleInstanceLock(const SingleInstanceLock &) = delete;
    SingleInstanceLock &operator=(const SingleInstanceLock &) = delete;

private:
    HANDLE mutex;
};

class Env {
public:
    explicit Env(difftest::Context &c) : ctx(c)
    {
        Set(kIatSetFilter, reinterpret_cast<void *>(&FakeSetFilter));
        Set(kIatGetModuleFileNameA, reinterpret_cast<void *>(&FakeNarrow));
        Set(kIatRegisterWindowMessageA, reinterpret_cast<void *>(&FakeRegisterWindowMessage));
        Set(kIatCoInitialize, reinterpret_cast<void *>(&FakeCoInitialize));
        Set(kIatCoUninitialize, reinterpret_cast<void *>(&FakeCoUninitialize));
        // The mutex code's own imports go to the real functions.
        HMODULE k32 = GetModuleHandleA("kernel32.dll");
        Set(0xd9a184, reinterpret_cast<void *>(GetProcAddress(k32, "WaitForSingleObject")));
        Set(0xd9a3f0, reinterpret_cast<void *>(GetProcAddress(k32, "ReleaseMutex")));
        Set(0xd9a17c, reinterpret_cast<void *>(GetProcAddress(k32, "CloseHandle")));
        Set(0xd9a3ec, reinterpret_cast<void *>(GetProcAddress(k32, "CreateMutexA")));
        resolved = ctx.image.At<uint8_t>(kModuleResolved);
        cached = ctx.image.At<void *>(kModulePointer);
        debugger = ctx.image.At<void *>(kDebuggerFunctionCache);
        instance = ctx.image.At<void *>(appbase::ExceptionReporterObject::kInstanceGlobal);
        savedResolved = *resolved;
        savedCached = *cached;
        savedDebugger = *debugger;
        savedInstance = *instance;
        *resolved = 1;
        *cached = reinterpret_cast<void *>(&FakeWide);
        *debugger = reinterpret_cast<void *>(0x1234); // already looked up: no import is called
        void **vt = ctx.image.At<void *>(kMutexVTable);
        for (int i = 0; i < 4; ++i) {
            gRealMutexSlots[i] = vt[i];
            savedSlots[i] = vt[i];
        }
        vt[0] = reinterpret_cast<void *>(&WrapDelete);
        vt[2] = reinterpret_cast<void *>(&WrapUnlock);
        vt[3] = reinterpret_cast<void *>(&WrapTryLock);
    }
    ~Env()
    {
        void **vt = ctx.image.At<void *>(kMutexVTable);
        for (int i = 0; i < 4; ++i) vt[i] = savedSlots[i];
        *resolved = savedResolved;
        *cached = savedCached;
        *debugger = savedDebugger;
        *instance = savedInstance;
        for (auto it = saved.rbegin(); it != saved.rend(); ++it) *ctx.image.At<void *>(it->first) = it->second;
    }

private:
    void Set(uint32_t iatRva, void *fake)
    {
        void **slot = ctx.image.At<void *>(iatRva);
        saved.emplace_back(iatRva, *slot);
        *slot = fake;
    }
    SingleInstanceLock lock; // first member: taken before anything else, released last
    difftest::Context &ctx;
    std::vector<std::pair<uint32_t, void *>> saved;
    uint8_t *resolved;
    void **cached, **debugger, **instance;
    uint8_t savedResolved;
    void *savedCached, *savedDebugger, *savedInstance, *savedSlots[4];
};

// ---- the command line double ----

struct Switch {
    uint8_t al = 0;     // what the lookup answers in al (the rest of eax is garbage)
    std::string value;  // assigned to the value argument when found (slot 8)
};
Switch gSwitches[5]; // ExceptionHandling, quietCrash, noExceptionUpload, installUserData, crashTest
const char *const kSwitchNames[5] = {"ExceptionHandling", "quietCrash", "noExceptionUpload", "installUserData", "crashTest"};

// Other switches a test scripts (lane FM's: userDataMode, IME, EsperSession, ...); cleared by the test.
std::vector<std::pair<std::string, Switch>> gMoreSwitches;

Switch *Find(const std::string &name)
{
    for (int i = 0; i < 5; ++i)
        if (name == kSwitchNames[i]) return &gSwitches[i];
    for (auto &s : gMoreSwitches)
        if (s.first == name) return &s.second;
    return nullptr;
}

uint32_t __fastcall FakeSwitchWithValue(void *, void *, const cRZString *key, cRZString *value, uint32_t trim)
{
    Log("IsSwitchPresent(%s, value, %u)", key->mString.c_str(), trim);
    Switch *s = Find(key->mString);
    if (!s) return 0x5500;
    if (s->al) value->mString = s->value;
    return 0x5500u | s->al;
}
uint32_t __fastcall FakeSwitchPresent(void *, void *, const cRZString *key)
{
    Log("IsSwitchPresent(%s)", key->mString.c_str());
    Switch *s = Find(key->mString);
    return 0x7700u | (s ? s->al : 0);
}

void *gCmdVt[17];
void SetUpCommandLine(difftest::Context &ctx)
{
    std::memcpy(gCmdVt, ctx.image.At<void>(0xd9b4fc), sizeof(gCmdVt)); // (any vtable; only slots 8 and 9 are called)
    gCmdVt[8] = reinterpret_cast<void *>(&FakeSwitchWithValue);
    gCmdVt[9] = reinterpret_cast<void *>(&FakeSwitchPresent);
}
struct CmdLineDouble {
    void **vtable = gCmdVt;
};

// ---- real mutexes held by another thread ----

class Holder {
public:
    explicit Holder(const char *name)
    {
        stop = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        HANDLE ready = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        thread = std::thread([=] {
            HANDLE m = CreateMutexA(nullptr, TRUE, name);
            SetEvent(ready);
            WaitForSingleObject(stop, INFINITE);
            if (m) {
                ReleaseMutex(m);
                CloseHandle(m);
            }
        });
        WaitForSingleObject(ready, INFINITE);
        CloseHandle(ready);
    }
    ~Holder()
    {
        SetEvent(stop);
        thread.join();
        CloseHandle(stop);
    }

private:
    HANDLE stop;
    std::thread thread;
};

bool HandleOpen(void *h)
{
    DWORD flags = 0;
    return h != nullptr && GetHandleInformation(h, &flags) != FALSE;
}

// Zero the words that are heap pointers (or depend on them) in a copy of an application object.
void MaskHeapWords(uint8_t *copy, bool reporterInternals)
{
    auto zero = [&](uint32_t offset) { std::memset(copy + offset, 0, 4); };
    zero(0x24);  // the name's string (its heap pointer when long)
    zero(0x48);  // the two container head nodes
    zero(0x50);
    zero(0x64);  // the id set's head node
    zero(0x78);  // the list's head node
    zero(0x248); // the string at +0x244
    if (reporterInternals) {
        zero(0x98 + 0x1a0);
        for (uint32_t offset = 0; offset <= 0x80; offset += 0x20) zero(0x98 + offset + 4);
    }
}

// ---- vtable words as codes (the only words normalised: each side stores its own vtables) ----

// The vtable kinds an application object holds during and after its life: the six of cTSAppFW, the two interface
// vtables the constructor stores first (cITSAppFW, cIGZMessageTarget2), and what the base destructors store back
// (cGZApp's three, the rz system-service base's two, cRZUnknownMT's, cTSAppCommonImpl's).
struct VtKind {
    uint32_t game;
    const void *ours;
    const char *name;
};
VtKind gVtKinds[] = {
    {0xe98464, nullptr, "fw0"}, {0xe9849c, nullptr, "fw4"}, {0xe984b8, nullptr, "fw1c"}, {0xe984f8, nullptr, "fw40"},
    {0xe98510, nullptr, "fw44"}, {0xe9855c, nullptr, "fw5c"}, {0xe9844c, nullptr, "itsappfw"}, {0xda49bc, nullptr, "imsgtarget2"},
    {0xe9d118, nullptr, "app0"}, {0xe9d150, nullptr, "app4"}, {0xe9d16c, nullptr, "app1c"}, {0xda3058, nullptr, "base0"},
    {0xda3090, nullptr, "base4"}, {0xda2fec, nullptr, "unknown"}, {0xeb5468, nullptr, "common"},
};
constexpr uint32_t kFaceOffsets[] = {0, 4, 0x1c, 0x40, 0x44, 0x5c};

// Learns our vtables from our own constructors (the application's under Env, with no switch answered).
void LearnOurVTables()
{
    if (gVtKinds[0].ours) return;
    alignas(8) static uint8_t memory[0x268];
    for (int i = 0; i < 5; ++i) gSwitches[i].al = 0;
    CmdLineDouble cmd;
    auto *app = ts::cTSAppFWAdapters::Construct(memory, nullptr, 1, "x", reinterpret_cast<cIGZCmdLine *>(&cmd));
    for (int i = 0; i < 6; ++i) gVtKinds[i].ours = *reinterpret_cast<void **>(memory + kFaceOffsets[i]);
    app->ts::cTSAppFW::~cTSAppFW();
    auto *gz = gz::cGZAppAdapters::Construct(memory, nullptr, 1, "x");
    for (int i = 0; i < 3; ++i) gVtKinds[8 + i].ours = *reinterpret_cast<void **>(memory + kFaceOffsets[i]);
    gz::cGZAppAdapters::Destruct(gz, nullptr);
    auto *base = rz::cRZSystemServiceAdapters::Construct(memory, nullptr, 1, 2);
    gVtKinds[11].ours = *reinterpret_cast<void **>(memory);
    gVtKinds[12].ours = *reinterpret_cast<void **>(memory + 4);
    rz::cRZSystemServiceAdapters::Destruct(base, nullptr);
    auto *unknown = rz::cRZUnknownMTAdapters::Construct(memory, nullptr);
    gVtKinds[13].ours = *reinterpret_cast<void **>(memory);
    rz::cRZUnknownMTAdapters::Destruct(unknown, nullptr);
    auto *common = ts::cTSAppCommonImplAdapters::Construct(memory, nullptr);
    gVtKinds[14].ours = *reinterpret_cast<void **>(memory);
    ts::cTSAppCommonImplAdapters::Destruct(common, nullptr);
    gLog.clear();
}

std::string VtCode(difftest::Context &ctx, bool original, const void *word)
{
    for (const VtKind &k : gVtKinds)
        if (k.ours && word == (original ? ctx.image.At<void>(k.game) : k.ours)) return k.name;
    char b[32];
    std::snprintf(b, sizeof(b), "other(%p)", word);
    return b;
}

// Replaces the six face words of a copy by zero and returns their codes.
std::string TakeVtCodes(difftest::Context &ctx, bool original, uint8_t *copy)
{
    std::string codes = "vt";
    for (uint32_t offset : kFaceOffsets) {
        codes += " " + VtCode(ctx, original, *reinterpret_cast<void **>(copy + offset));
        std::memset(copy + offset, 0, 4);
    }
    return codes;
}


// The two routes of our side: Adapter, the hook adapters on an application built by the original constructor (game
// vtables: how the game's live object is served); OwnVTable, an application built by our constructor and destroyed
// through our class (its destructor, or slot 3 of the +4 vtable / slot 10 of the +0x44 vtable: the compiler's thunks).
enum class Route { Adapter, OwnVTable };

template <typename R, typename... Args> R CallSlot(void *face, int slot, Args... args)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, Args...)>((*reinterpret_cast<void *const *const *>(face))[slot]);
    return fn(face, args...);
}

constexpr int kFaceSlots[6] = {13, 6, 15, 5, 18, 4};

// The code stays writable after the restore (as shape::CodePatch's): VirtualProtect reports only the first page's old
// protection, so putting it back narrows both pages when the five bytes straddle a page boundary, and an earlier
// redirect on the second page then faults when it restores its bytes (lane MW: cRZFile::Construct at the end of a page,
// cRZFile::PathExists on the next; whether that happens depends on difftest.exe's link layout).
struct Redirect {
    uint8_t *site;
    uint8_t saved[5];
    Redirect(void *entry, void *target) : site(static_cast<uint8_t *>(entry))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        int32_t relative = int32_t(reinterpret_cast<uintptr_t>(target) - reinterpret_cast<uintptr_t>(site) - 5);
        site[0] = 0xe9;
        std::memcpy(site + 1, &relative, 4);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    ~Redirect()
    {
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old); // writable whatever another patch's restore left
        std::memcpy(site, saved, 5);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    Redirect(const Redirect &) = delete;
};

// An import slot pointed at a fake for the life of the object.
struct IatPatch {
    void **slot;
    void *old;
    IatPatch(difftest::Context &ctx, uint32_t rva, const void *fake) : slot(ctx.image.At<void *>(rva)), old(*slot) { *slot = const_cast<void *>(fake); }
    ~IatPatch() { *slot = old; }
    IatPatch(const IatPatch &) = delete;
};

const uint8_t *gAppBase = nullptr;
int AppOff(const void *p)
{
    return p ? static_cast<int>(static_cast<const uint8_t *>(p) - gAppBase) : -1;
}

uint32_t __fastcall TrapNoArgs(void *self, void *)
{
    Log("TRAP@%d", AppOff(self));
    return 0;
}
uint32_t __fastcall TrapOneArg(void *self, void *, void *)
{
    Log("TRAP@%d", AppOff(self));
    return 0;
}
uint32_t __fastcall TrapTwoArgs(void *self, void *, uint32_t, void *)
{
    Log("TRAP@%d", AppOff(self));
    return 0;
}

struct FakeObject {
    void **vtable;
};

// One side's application for one case, its six vtables replaced by copies (so single slots can be faked or trapped).
class LiveApp {
public:
    LiveApp(difftest::Context &c, bool original, Route route, const char *name) : ctx(c), mOriginal(original), mRoute(route)
    {
        raw = static_cast<uint8_t *>(::operator new(0x268));
        std::memset(raw, 0x99, 0x268);
        for (int i = 0; i < 5; ++i) gSwitches[i].al = 0;
        CmdLineDouble cmd;
        if (GameBuilt())
            ctx.Original<void *(__thiscall *)(void *, uint32_t, const char *, void *)>(kAppCtor)(raw, 0x1234, name, &cmd);
        else
            ts::cTSAppFWAdapters::Construct(raw, nullptr, 0x1234, name, reinterpret_cast<cIGZCmdLine *>(&cmd));
        for (int f = 0; f < 6; ++f) {
            void **vt = *reinterpret_cast<void ***>(raw + kFaceOffsets[f]);
            saved[f] = vt;
            copies[f][0] = vt[-1];
            for (int s = 0; s < kFaceSlots[f]; ++s) copies[f][1 + s] = vt[s];
            *reinterpret_cast<void ***>(raw + kFaceOffsets[f]) = &copies[f][1];
        }
        gAppBase = raw;
        gLog.clear();
    }
    ~LiveApp()
    {
        for (int f = 0; f < 6; ++f) *reinterpret_cast<void ***>(raw + kFaceOffsets[f]) = saved[f];
        *reinterpret_cast<void **>(raw + 0x80) = nullptr; // the delegate double is not the teardown's business
        *reinterpret_cast<void **>(raw + 0x60) = nullptr;
        if (GameBuilt()) ctx.Original<void(__thiscall *)(void *)>(kAppDtor)(raw);
        else reinterpret_cast<ts::cTSAppFW *>(raw)->ts::cTSAppFW::~cTSAppFW();
        ::operator delete(raw, 0x268);
        gLog.clear();
    }
    LiveApp(const LiveApp &) = delete;
    bool GameBuilt() const { return mOriginal || mRoute == Route::Adapter; }
    void SetSlot(int face, int slot, const void *fn) { copies[face][1 + slot] = const_cast<void *>(fn); }
    uint8_t *Face(int face) { return raw + kFaceOffsets[face]; }
    // The object as compared: the vtable words as codes of the side's own vtables, heap words masked, the strings' texts.
    std::string Digest()
    {
        uint8_t copy[0x268];
        std::memcpy(copy, raw, sizeof(copy));
        for (int f = 0; f < 6; ++f) std::memcpy(copy + kFaceOffsets[f], &saved[f], 4);
        std::string s = TakeVtCodes(ctx, GameBuilt(), copy); // the builder's vtables (the game's on the adapter route)
        MaskHeapWords(copy, true);
        std::memset(copy + 0x74, 0, 4); // the mutex object
        auto *app = reinterpret_cast<cTSAppFW *>(raw);
        s += " name [" + app->mApp.mName.mString + "] s244 [" + app->mString244.mString + "] " + Hex(copy, sizeof(copy));
        // The option store's two trees (cTSAppCommonImpl +4 / +0xc: head nodes), in order: keys and list sizes.
        for (uint32_t head : {0x48u, 0x50u}) s += " tree" + Tree(*reinterpret_cast<TreeNode **>(raw + head));
        return s;
    }
    static std::string Tree(TreeNode *head)
    {
        std::string out;
        char b[48];
        std::function<void(TreeNode *)> walk = [&](TreeNode *n) {
            if (n->mIsNil) return;
            walk(n->mLeft);
            std::snprintf(b, sizeof(b), " %x/%x:%u", n->mKey[0], n->mKey[1], n->mListSize);
            out += b;
            walk(n->mRight);
        };
        walk(head->mParent);
        return out;
    }
    uint8_t *raw;

private:
    difftest::Context &ctx;
    bool mOriginal;
    Route mRoute;
    void **saved[6];
    void *copies[6][20];
};

std::string Outcome(uint32_t result, const std::string &digest)
{
    char b[32];
    std::snprintf(b, sizeof(b), "al %02x\n", result & 0xff);
    return b + Joined() + digest + "\n";
}

} // namespace
