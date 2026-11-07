// Differential tests for the framework (src/gz/Framework.*): cGZFramework / cCTGFramework.
//
// The standalone shape (lane ER): gz::cGZFramework's bodies are tested on two routes (`Route`). Vtable route: side A an
// object from the original base constructor 0x141d76 (the game's base vtables), side B ours from our constructor (the
// 0x141d76 hook's adapter), every slot called through the pristine slot of our vtable (gOurVt / gOurW32Vt; the object's
// own vtable pointer is a recording copy). Adapter route: both objects from the original cCTGFramework constructor
// 0xc2ccdb (what runs in game: the live object is built before the hooks), ours called through the hooks' adapters
// (gz::cGZFrameworkAdapters, gz::cCTGFrameworkAdapters) with trap vtables matched to each slot's arity. cCTGFramework
// route (lane EX, item F3): side A from the original 0xc2ccdb, side B from our gz::cCTGFramework constructor, every slot
// through our cCTGFramework vtables (gOurCtgVt / gOurCtgW32Vt / gOurCtgTargetVt), the base's overridden slots 41..46 and
// 48 by name. The base tests run on all three routes; the cCTGFramework ones (bands, DoMessage, MainLoop, Run/Shutdown)
// on the adapter and cCTGFramework routes, the base's bands and main loop (FW_BaseBands, FW_MainLoopScripted) on all
// three. The cIGZUnknown thunks and forwards of our vtables (the compiler's: the originals are 0x1431f4, 0x142347,
// 0x143273 for the base's +4 face; cCTGFramework's 0xc2d995 / 0xc2ceeb / 0xc2d9ad and their +4 / +0x180 thunks) are
// called in FW_Basics.
//
// Each case builds two framework objects (per route, as above) from the same random
// bytes and gives each its own set of recording fakes (services, hooks, an application, a message
// server). The same random setup (through the original functions) puts both in the same state; then
// the original runs one operation on side A and ours runs it on side B. Compared: the return value,
// the ordered log of everything the fakes and the lock saw, and a structural snapshot of both
// objects (fields, every container in order with the fakes as indices, the id sets, each fake's
// reference count and running flag).
//
// The framework's lock is replaced by a recording vtable (lock/unlock events) and its own vtable by a
// copy whose called slots record, so the order of locking, virtual calls and calls into services is
// pinned. Imports the framework uses (Steam callbacks, the message box, SetEnvironmentVariableA) are
// read from the game's import table by both sides, so the table is pointed at recorders. The message
// pump and PostQuitMessage are the real Windows functions on the test thread's queue (the original's
// table entries are pointed at them too).
#define NOMINMAX
#include "game_allocator.h"
#include "difftest.h"
#include "game/game.h"
#include "gz/Framework.h"
#include "gz/FrameworkShape.h"
#include "rz/GeneralAllocator.h"
#include "rz/RZMemory.h"
#include "rz/cRZMessage2Standard.h"

#include <windows.h>

#include <algorithm>
#include <malloc.h>

#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using Fw = cCTGFramework; // the field layout both routes share (gz::cGZFramework's is the same, checked below)
using Shape = gz::cGZFramework;
static_assert(offsetof(Shape, mDeferQuit) == offsetof(Fw, mDeferQuit) && offsetof(Shape, mMainHWND) == offsetof(Fw, mMainHWND),
              "shape and hook-shaped layouts");

// Adapters: both objects from the original cCTGFramework constructor, ours called through the hooks' adapters.
// Vtable: base objects (0x141d76 / our gz::cGZFramework), ours through our base vtable.
// Ctg: cCTGFramework objects (0xc2ccdb / our gz::cCTGFramework constructor), ours through our cCTGFramework vtables.
enum class Route { Adapters, Vtable, Ctg };

// The base tests' seeds: 1..n on the adapter route, n+1..2n on the base vtable route, 2n+1..3n on the cCTGFramework one.
Route RouteOf(uint32_t seed, uint32_t n)
{
    return seed > 2 * n ? Route::Ctg : seed > n ? Route::Vtable : Route::Adapters;
}

// Our classes' vtables as our constructors install them (captured from the first object we build).
void *const *gOurVt = nullptr;
void *const *gOurW32Vt = nullptr;
void *const *gOurCtgVt = nullptr;
void *const *gOurCtgW32Vt = nullptr;
void *const *gOurCtgTargetVt = nullptr;

// ---------------------------------------------------------------------------------------------
// Event log

enum : int { kWhoLock = -2, kWhoFw = -3, kWhoImport = -4 };
enum : int { kEvLock = 1000, kEvUnlock = 1001 };

// Every event also carries the framework's busy bytes (+0xe4..+0xe8) and nesting counts at the moment of
// the call, so a fake that is called while the framework is mid-operation exposes the flags.
struct Event {
    int who;
    int what;
    uint32_t a, b, c;
    uint32_t flags, flags2, flags3;
    bool operator==(const Event &o) const
    {
        return who == o.who && what == o.what && a == o.a && b == o.b && c == o.c && flags == o.flags && flags2 == o.flags2 && flags3 == o.flags3;
    }
};

struct Fake;
struct Side;
struct Ops;
Side *gSide = nullptr;

void Log(int who, int what, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0);

// ---------------------------------------------------------------------------------------------
// Fakes

struct Fake {
    const void *vtable; // first
    int index = 0;
    int32_t refs = 0;
    uint32_t id[3] = {0, 0, 0}; // GetServiceID answers in call order (the last repeats)
    int idCalls = 0;
    int32_t priority = 0, tickPriority = 0;
    uint8_t running = 0, initOk = 1, shutdownOk = 1;
    uint32_t value = 0; // GZRun result for apps, the id for messages
    std::vector<std::pair<uint32_t, Fake *>> qi; // interface id -> object handed out
    std::function<void(Side &, Fake &)> action[16];
};

struct Side {
    bool original = false;
    Route route = Route::Adapters; // Vtable: base objects (0x141d76 / ours), no cCTGFramework part
    Fw *fw = nullptr;
    std::vector<std::unique_ptr<Fake>> fakes;
    std::vector<Event> log;
    void *fwVt[49];
    void *w32Vt[7];
    void *targetVt[4];
    void *lockVt[8];
    Fake *app = nullptr;
    Fake *messageServer = nullptr;
    void *hookOwner = nullptr; // what g_AppForMsgHook points to
    void *comVt[16];
    Ops *ops = nullptr; // the operations of this side, for fakes that re-enter the framework
    int IndexOf(const void *p) const
    {
        if (!p) return -1;
        for (const auto &f : fakes)
            if (f.get() == p) return f->index;
        return -100;
    }
};

void Log(int who, int what, uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t flags = 0, flags2 = 0, flags3 = 0;
    if (const Fw *fw = gSide->fw) {
        flags = fw->mServicesBusy | (fw->mLookupBusy << 8) | (fw->mIdleBusy << 16) | (static_cast<uint32_t>(fw->mTickBusy) << 24);
        flags2 = fw->mHooksBusy | ((static_cast<uint32_t>(fw->mIdleNesting) & 0xff) << 8) | ((static_cast<uint32_t>(fw->mTickNesting) & 0xff) << 16) |
                 (static_cast<uint32_t>(fw->mQuitPending) << 24);
        flags3 = fw->mState;
    }
    gSide->log.push_back({who, what, a, b, c, flags, flags2, flags3});
}

uint32_t Idx(const void *p)
{
    return static_cast<uint32_t>(gSide->IndexOf(p));
}

// Generic cIGZUnknown slots.
uint8_t __fastcall FakeQI(Fake *f, void *, uint32_t iid, void **ppv)
{
    Log(f->index, 0, iid);
    for (auto &entry : f->qi) {
        if (entry.first == iid) {
            *ppv = entry.second;
            ++entry.second->refs;
            return 1;
        }
    }
    return 0;
}
uint32_t __fastcall FakeAddRef(Fake *f, void *)
{
    Log(f->index, 1);
    return static_cast<uint32_t>(++f->refs);
}
uint32_t __fastcall FakeRelease(Fake *f, void *)
{
    Log(f->index, 2);
    return static_cast<uint32_t>(--f->refs);
}

void Act(Fake *f, int slot)
{
    if (f->action[slot]) f->action[slot](*gSide, *f);
}

// cIGZSystemService slots 3..12.
uint32_t __fastcall SvcGetId(Fake *f, void *)
{
    Log(f->index, 3);
    uint32_t v = f->id[std::min(f->idCalls, 2)];
    ++f->idCalls;
    return v;
}
void __fastcall SvcSetId(Fake *f, void *, uint32_t v) { Log(f->index, 4, v); }
int32_t __fastcall SvcGetPriority(Fake *f, void *)
{
    Log(f->index, 5);
    return f->priority;
}
uint8_t __fastcall SvcIsRunning(Fake *f, void *)
{
    Log(f->index, 6);
    return f->running;
}
void __fastcall SvcSetRunning(Fake *f, void *, uint32_t v)
{
    Log(f->index, 7, v);
    f->running = static_cast<uint8_t>(v);
}
uint8_t __fastcall SvcInit(Fake *f, void *)
{
    Log(f->index, 8);
    Act(f, 8);
    return f->initOk;
}
uint8_t __fastcall SvcShutdown(Fake *f, void *)
{
    Log(f->index, 9);
    Act(f, 9);
    return f->shutdownOk;
}
void __fastcall SvcOnTick(Fake *f, void *, uint32_t c)
{
    Log(f->index, 10, c);
    Act(f, 10);
}
void __fastcall SvcOnIdle(Fake *f, void *, uint32_t c)
{
    Log(f->index, 11, c);
    Act(f, 11);
}
int32_t __fastcall SvcGetTickPriority(Fake *f, void *)
{
    Log(f->index, 12);
    return f->tickPriority;
}
const void *const kSvcVt[13] = {
    (void *)&FakeQI, (void *)&FakeAddRef, (void *)&FakeRelease, (void *)&SvcGetId, (void *)&SvcSetId, (void *)&SvcGetPriority,
    (void *)&SvcIsRunning, (void *)&SvcSetRunning, (void *)&SvcInit, (void *)&SvcShutdown, (void *)&SvcOnTick, (void *)&SvcOnIdle,
    (void *)&SvcGetTickPriority};

// cIGZFrameWorkHooks slots 3..10 (no arguments).
template <int S> void __fastcall HookRec(Fake *f, void *)
{
    Log(f->index, S);
    Act(f, S);
}
const void *const kHookVt[11] = {(void *)&FakeQI,       (void *)&FakeAddRef,    (void *)&FakeRelease,   (void *)&HookRec<3>,
                                 (void *)&HookRec<4>,   (void *)&HookRec<5>,   (void *)&HookRec<6>,   (void *)&HookRec<7>,
                                 (void *)&HookRec<8>,   (void *)&HookRec<9>,   (void *)&HookRec<10>};

// A callback object for EnumSystemServices: slot 3 (unknown, a, b).
void __fastcall CbCall(Fake *f, void *, void *unknown, uint32_t a, uint32_t b)
{
    Log(f->index, 3, Idx(unknown), a, b);
}
const void *const kCbVt[4] = {(void *)&FakeQI, (void *)&FakeAddRef, (void *)&FakeRelease, (void *)&CbCall};

// A message (cIGZMessage2 face): slot 4 is its id.
uint32_t __fastcall MsgGetId(Fake *f, void *)
{
    Log(f->index, 4);
    return f->value;
}
const void *const kMsgVt[5] = {(void *)&FakeQI, (void *)&FakeAddRef, (void *)&FakeRelease, (void *)&HookRec<3>, (void *)&MsgGetId};

// The application (cIGZApp face): slots 3..14, no arguments; slot 8 (GZRun) answers `value`.
template <int S> uint8_t __fastcall AppRec(Fake *f, void *)
{
    Log(f->index, S);
    Act(f, S);
    return S == 8 ? static_cast<uint8_t>(f->value) : 0;
}
const void *const kAppVt[15] = {(void *)&FakeQI,     (void *)&FakeAddRef,   (void *)&FakeRelease,  (void *)&AppRec<3>,  (void *)&AppRec<4>,
                                (void *)&AppRec<5>,  (void *)&AppRec<6>,   (void *)&AppRec<7>,    (void *)&AppRec<8>,  (void *)&AppRec<9>,
                                (void *)&AppRec<10>, (void *)&AppRec<11>,  (void *)&AppRec<12>,   (void *)&AppRec<13>, (void *)&AppRec<14>};

// The application's message hook face (cITSAppFW at +0x40 of the object g_AppForMsgHook points to):
// slot 3 takes a MSG pointer (or null) and answers a byte.
std::vector<uint8_t> gHookAnswers; // consumed in order, then 0
size_t gHookAnswerPos = 0;
uint8_t __fastcall HookFaceMessage(Fake *f, void *, const MSG *msg)
{
    Log(f->index, 3, msg ? msg->message : 0xffffffffu, msg ? static_cast<uint32_t>(msg->wParam) : 0);
    return gHookAnswerPos < gHookAnswers.size() ? gHookAnswers[gHookAnswerPos++] : 0;
}
const void *const kHookFaceVt[4] = {(void *)&FakeQI, (void *)&FakeAddRef, (void *)&FakeRelease, (void *)&HookFaceMessage};

// The message server: slot 3 receives a message, slots 5 and 6 (arg, key) subscribe / unsubscribe.
void __fastcall MsSend(Fake *f, void *, void *msg)
{
    Log(f->index, 3, msg ? *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(msg) + 0xc) : 0);
}
uint32_t FwOffset(uint32_t pointer)
{
    return pointer - static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gSide->fw));
}
void __fastcall MsSub5(Fake *f, void *, uint32_t a, uint32_t b) { Log(f->index, 5, FwOffset(a), b); }
void __fastcall MsSub6(Fake *f, void *, uint32_t a, uint32_t b) { Log(f->index, 6, FwOffset(a), b); }
void __fastcall MsNop(Fake *f, void *) { Log(f->index, 4); }
const void *const kMsVt[7] = {(void *)&FakeQI, (void *)&FakeAddRef, (void *)&FakeRelease, (void *)&MsSend, (void *)&MsNop, (void *)&MsSub5, (void *)&MsSub6};

// A stream: its QueryInterface hands out itself for the ids it accepts.
const void *const kStreamVt[3] = {(void *)&FakeQI, (void *)&FakeAddRef, (void *)&FakeRelease};

Fake *NewFake(Side &s, const void *const *vt)
{
    s.fakes.push_back(std::make_unique<Fake>());
    Fake *f = s.fakes.back().get();
    f->vtable = vt;
    f->index = static_cast<int>(s.fakes.size()) - 1;
    return f;
}

// ---------------------------------------------------------------------------------------------
// Framework slot recorders (the vtable copy) and the lock

int32_t __fastcall LockRec(void *, void *)
{
    Log(kWhoLock, kEvLock);
    return 1;
}
int32_t __fastcall UnlockRec(void *, void *)
{
    Log(kWhoLock, kEvUnlock);
    return 0;
}

uint32_t __fastcall FwAddRefRec(void *, void *)
{
    Log(kWhoFw, 1);
    return 1;
}
uint32_t __fastcall FwReleaseRec(void *, void *)
{
    Log(kWhoFw, 2);
    return 1;
}
void __fastcall ComRealShutdownRec(void *, void *)
{
    Log(kWhoFw, 200);
}
void __fastcall FwOnTickRec(void *, void *, uint32_t c)
{
    Log(kWhoFw, 15, c);
}
uint8_t __fastcall FwAddSvcRec(void *, void *, void *svc)
{
    Log(kWhoFw, 3, Idx(svc));
    return 1;
}
std::vector<uint8_t> gTickEnabledAnswers; // consumed in order, then 1
size_t gTickEnabledPos = 0;
uint8_t __fastcall FwIsTickEnabledRec(void *, void *)
{
    Log(kWhoFw, 17);
    return gTickEnabledPos < gTickEnabledAnswers.size() ? gTickEnabledAnswers[gTickEnabledPos++] : 1;
}
void __fastcall FwOnIdleSlotRec(void *, void *)
{
    Log(kWhoFw, 16);
}
void __fastcall FwToggleRec(void *, void *, uint32_t v)
{
    Log(kWhoFw, 18, v);
}
void *gStreams[3];
void *__fastcall FwGetDebugStreamRec(void *, void *)
{
    Log(kWhoFw, 25);
    return reinterpret_cast<void *>(0x5a5a5a5a);
}
void *__fastcall FwStdOutRec(void *, void *)
{
    Log(kWhoFw, 31);
    return gStreams[0];
}
void *__fastcall FwStdErrRec(void *, void *)
{
    Log(kWhoFw, 32);
    return gStreams[1];
}
void *__fastcall FwStdInRec(void *, void *)
{
    Log(kWhoFw, 33);
    return gStreams[2];
}
void *__fastcall FwApplicationRec(void *, void *)
{
    Log(kWhoFw, 37);
    return gSide->app;
}
template <int S> uint8_t __fastcall FwBandRec(void *, void *)
{
    Log(kWhoFw, S);
    return 1;
}

// ---------------------------------------------------------------------------------------------
// Imports (the original reads the same table slots)

uint32_t gSteamCalls = 0;
void __cdecl SteamStub()
{
    Log(kWhoImport, 1);
    ++gSteamCalls;
}
// A checksum of a C string (the strings are the game's, so tests compare checksums, not text).
uint32_t Fnv(const char *s)
{
    if (!s) return 0xdeadbeef;
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
    return h;
}
int __stdcall MessageBoxStub(HWND hwnd, const char *text, const char *caption, UINT type)
{
    Log(kWhoImport, 2, Fnv(text), Fnv(caption), type + (hwnd ? 1000u : 0u));
    return 1;
}
int __stdcall SetEnvStub(const char *name, const char *value)
{
    Log(kWhoImport, 3, Fnv(name), Fnv(value));
    return 1;
}

struct ImportPatch {
    difftest::Context &ctx;
    struct Saved {
        uint32_t rva;
        void *value;
    };
    std::vector<Saved> saved;
    explicit ImportPatch(difftest::Context &c) : ctx(c) {}
    void Set(uint32_t rva, void *fn)
    {
        void **slot = ctx.image.At<void *>(rva);
        saved.push_back({rva, *slot});
        DWORD old;
        VirtualProtect(slot, 4, PAGE_EXECUTE_READWRITE, &old);
        *slot = fn;
    }
    ~ImportPatch()
    {
        for (auto &s : saved) *ctx.image.At<void *>(s.rva) = s.value;
    }
};

// ---------------------------------------------------------------------------------------------
// Operations

// An original function at its RVA (__thiscall on the object).
template <typename R, typename... A> void BindOrig(std::function<R(Fw *, A...)> &f, difftest::Context &ctx, uint32_t rva)
{
    auto fn = ctx.Original<R(__thiscall *)(void *, A...)>(rva);
    f = [fn](Fw *fw, A... a) { return fn(fw, a...); };
}
// A slot of our class's pristine vtable (`*table`, read at call time: captured from the first object our constructor
// builds); the object's own vtable is a recording copy.
template <typename R, typename... A> void BindSlot(std::function<R(Fw *, A...)> &f, int slot, void *const *const *table = &gOurVt)
{
    f = [slot, table](Fw *fw, A... a) { return reinterpret_cast<R(__thiscall *)(void *, A...)>((*table)[slot])(fw, a...); };
}
// One of the hooks' adapters (__fastcall: the face in ecx, the dummy edx, the stack arguments).
template <typename R, typename... A, typename F> void BindAdapter(std::function<R(Fw *, A...)> &f, F adapter)
{
    auto fn = reinterpret_cast<R(__fastcall *)(void *, void *, A...)>(adapter);
    f = [fn](Fw *fw, A... a) { return fn(fw, nullptr, a...); };
}
Shape *Our(Fw *fw)
{
    return reinterpret_cast<Shape *>(fw);
}

struct Ops {
    std::function<bool(Fw *, uint32_t, void **)> QueryInterface;
    std::function<uint32_t(Fw *)> AddRef, Release;
    std::function<bool(Fw *, void *)> AddSystemService, RemoveSystemService, AddHook, RemoveHook, AddToTick, RemoveFromTick, AddToOnIdle,
        RemoveFromOnIdle;
    std::function<bool(Fw *, uint32_t, uint32_t, void **)> GetSystemService;
    std::function<bool(Fw *, void *, uint32_t, uint32_t)> EnumSystemServices;
    std::function<uint32_t(Fw *)> GetOnIdleInterval;
    std::function<void(Fw *, int32_t)> SetOnIdleInterval;
    std::function<void(Fw *, uint32_t)> OnTickBase, OnTickCTG;
    std::function<void(Fw *)> OnIdle;
    std::function<uint8_t(Fw *)> IsTickEnabled, IsInstall;
    std::function<void(Fw *, uint32_t)> ToggleTick, Quit, AbortiveQuit, SetInstallFlag;
    std::function<void *(Fw *)> CommandLine, GetCOMObject, GetDebugStream, DefaultDebugStream, DebugStream, ExceptionNotificationObj, Application;
    std::function<void *(Fw *)> StdOut, StdErr, StdIn;
    std::function<uint32_t(Fw *)> GetState, GetDebugLevel;
    std::function<void(Fw *, void *)> SetDebugStream, SetApplication;
    std::function<bool(Fw *, int32_t)> SetDebugLevel;
    std::function<bool(Fw *, uint32_t, uint32_t, void **)> GetStream;
    std::function<bool(Fw *, uint32_t, void *)> SetStream;
    std::function<void(Fw *, const char *)> ReportException;
    std::function<void(Fw *, std::list<FwRef> &)> CopyHooks;
    std::function<void(Fw *, std::list<FwRef> &, FwWalk)> BuildServiceList;
    std::function<bool(Fw *)> RunHooks[7];
    std::function<bool(Fw *)> ClearServiceContainers, ClearHooks;
    std::function<void(Fw *)> ShutdownCOMObject;
    std::function<bool(Fw *)> InitHigh, InitAppBand, InitLow, ShutdownLow, ShutdownAppBand, ShutdownHigh;
    std::function<uint32_t(Fw *)> MainLoop;
    // The base's slots 41..46 and 48 (gz::cGZFramework's own bodies, overridden by cCTGFramework).
    std::function<bool(Fw *)> BaseInitHigh, BaseInitAppBand, BaseInitLow, BaseShutdownLow, BaseShutdownAppBand, BaseShutdownHigh;
    std::function<uint32_t(Fw *)> BaseMainLoop;
    std::function<bool(Fw *, void *)> DoMessage;
    std::function<bool()> Run, Shutdown;
    std::function<void()> ReleaseGlobals;
};

// The original's functions (`original`), or ours on `route`.
Ops MakeOps(difftest::Context &ctx, bool original, Route route = Route::Adapters)
{
    using A = gz::cGZFrameworkAdapters;
    Ops o;
    if (original) {
        BindOrig(o.QueryInterface, ctx, 0x1431b6);
        BindOrig(o.AddRef, ctx, 0x142321);
        BindOrig(o.Release, ctx, 0x143244);
        BindOrig(o.AddSystemService, ctx, 0x14234f);
        BindOrig(o.RemoveSystemService, ctx, 0x143431);
        BindOrig(o.GetSystemService, ctx, 0x1428e2);
        BindOrig(o.EnumSystemServices, ctx, 0x14271c);
        BindOrig(o.AddHook, ctx, 0x14229a);
        BindOrig(o.RemoveHook, ctx, 0x1433cd);
        BindOrig(o.AddToTick, ctx, 0x1424fe);
        BindOrig(o.RemoveFromTick, ctx, 0x143324);
        BindOrig(o.AddToOnIdle, ctx, 0x142474);
        BindOrig(o.RemoveFromOnIdle, ctx, 0x14327b);
        BindOrig(o.GetOnIdleInterval, ctx, 0x142860);
        BindOrig(o.SetOnIdleInterval, ctx, 0x1436e6);
        BindOrig(o.OnTickBase, ctx, 0x142eca);
        BindOrig(o.OnTickCTG, ctx, 0xc2d427);
        BindOrig(o.OnIdle, ctx, 0x142dbb);
        BindOrig(o.IsTickEnabled, ctx, 0x142c23);
        BindOrig(o.IsInstall, ctx, 0x142c1c);
        BindOrig(o.ToggleTick, ctx, 0x1438d7);
        BindOrig(o.Quit, ctx, 0x1431fc);
        BindOrig(o.AbortiveQuit, ctx, 0x142270);
        BindOrig(o.SetInstallFlag, ctx, 0x14368c);
        BindOrig(o.CommandLine, ctx, 0x142710);
        BindOrig(o.GetCOMObject, ctx, 0x142840);
        BindOrig(o.GetDebugStream, ctx, 0x142850);
        BindOrig(o.DefaultDebugStream, ctx, 0x142717);
        BindOrig(o.DebugStream, ctx, 0x142717); // slot 27 holds the same function
        BindOrig(o.ExceptionNotificationObj, ctx, 0x1427a0);
        BindOrig(o.Application, ctx, 0x1426ec);
        BindOrig(o.StdOut, ctx, 0x143887);
        BindOrig(o.StdErr, ctx, 0x1437e7);
        BindOrig(o.StdIn, ctx, 0x143837);
        BindOrig(o.GetState, ctx, 0x142870);
        BindOrig(o.GetDebugLevel, ctx, 0x142844);
        BindOrig(o.SetDebugStream, ctx, 0x1436cc);
        BindOrig(o.SetApplication, ctx, 0x14363e);
        BindOrig(o.SetDebugLevel, ctx, 0x143699);
        BindOrig(o.GetStream, ctx, 0x142877);
        BindOrig(o.SetStream, ctx, 0x1436fb);
        BindOrig(o.ReportException, ctx, 0x14358c);
        BindOrig(o.CopyHooks, ctx, 0x142c2a);
        BindOrig(o.BuildServiceList, ctx, 0x142cb2);
        const uint32_t runners[7] = {0x142b97, 0x142ad7, 0x1429b7, 0x142b37, 0x142a17, 0x142a77, 0x142957};
        for (int i = 0; i < 7; ++i) BindOrig(o.RunHooks[i], ctx, runners[i]);
        BindOrig(o.ClearServiceContainers, ctx, 0x1427bb);
        BindOrig(o.ClearHooks, ctx, 0x142bf7);
        BindOrig(o.ShutdownCOMObject, ctx, 0x1437c5);
        BindOrig(o.InitHigh, ctx, 0xc2d6e6);
        BindOrig(o.InitAppBand, ctx, 0xc2cf03);
        BindOrig(o.InitLow, ctx, 0xc2d506);
        BindOrig(o.ShutdownLow, ctx, 0xc2d8d2);
        BindOrig(o.ShutdownAppBand, ctx, 0xc2cfde);
        BindOrig(o.ShutdownHigh, ctx, 0xc2d5bb);
        BindOrig(o.MainLoop, ctx, 0xc2d9c5);
        BindOrig(o.BaseInitHigh, ctx, 0x14307f);
        BindOrig(o.BaseInitAppBand, ctx, 0x1425c7);
        BindOrig(o.BaseInitLow, ctx, 0x142f87);
        BindOrig(o.BaseShutdownLow, ctx, 0x143133);
        BindOrig(o.BaseShutdownAppBand, ctx, 0x142662);
        BindOrig(o.BaseShutdownHigh, ctx, 0x142ffb);
        BindOrig(o.BaseMainLoop, ctx, 0x1435c2);
        auto doMessage = ctx.Original<bool(__thiscall *)(void *, void *)>(0xc2d2ca);
        o.DoMessage = [doMessage](Fw *fw, void *msg) { return doMessage(&fw->mTarget, msg); };
        auto run = ctx.Original<bool(__cdecl *)()>(0x1446ba);
        auto shutdown = ctx.Original<bool(__cdecl *)()>(0x14472b);
        o.Run = [run]() { return run(); };
        o.Shutdown = [shutdown]() { return shutdown(); };
        auto release = ctx.Original<void(__cdecl *)()>(0x143782);
        o.ReleaseGlobals = [release]() { release(); };
        return o;
    }
    if (route == Route::Vtable || route == Route::Ctg) {
        // Every slot through our vtable (the base's on base objects, cCTGFramework's on cCTGFramework objects); the
        // non-virtual members called directly.
        void *const *const *vt = route == Route::Ctg ? &gOurCtgVt : &gOurVt;
        BindSlot(o.QueryInterface, 0, vt);
        BindSlot(o.AddRef, 1, vt);
        BindSlot(o.Release, 2, vt);
        BindSlot(o.AddSystemService, 3, vt);
        BindSlot(o.RemoveSystemService, 4, vt);
        BindSlot(o.GetSystemService, 5, vt);
        BindSlot(o.EnumSystemServices, 6, vt);
        BindSlot(o.AddHook, 7, vt);
        BindSlot(o.RemoveHook, 8, vt);
        BindSlot(o.AddToTick, 9, vt);
        BindSlot(o.RemoveFromTick, 10, vt);
        BindSlot(o.AddToOnIdle, 11, vt);
        BindSlot(o.RemoveFromOnIdle, 12, vt);
        BindSlot(o.GetOnIdleInterval, 13, vt);
        BindSlot(o.SetOnIdleInterval, 14, vt);
        BindSlot(o.OnIdle, 16, vt);
        BindSlot(o.IsTickEnabled, 17, vt);
        BindSlot(o.ToggleTick, 18, vt);
        BindSlot(o.Quit, 19, vt);
        BindSlot(o.AbortiveQuit, 20, vt);
        BindSlot(o.CommandLine, 21, vt);
        BindSlot(o.IsInstall, 22, vt);
        BindSlot(o.GetCOMObject, 23, vt);
        BindSlot(o.GetState, 24, vt);
        BindSlot(o.GetDebugStream, 25, vt);
        BindSlot(o.DefaultDebugStream, 26, vt);
        BindSlot(o.DebugStream, 27, vt);
        BindSlot(o.SetDebugStream, 28, vt);
        BindSlot(o.SetDebugLevel, 29, vt);
        BindSlot(o.GetDebugLevel, 30, vt);
        BindSlot(o.StdOut, 31, vt);
        BindSlot(o.StdErr, 32, vt);
        BindSlot(o.StdIn, 33, vt);
        BindSlot(o.GetStream, 34, vt);
        BindSlot(o.SetStream, 35, vt);
        BindSlot(o.SetApplication, 36, vt);
        BindSlot(o.Application, 37, vt);
        BindSlot(o.ReportException, 38, vt);
        BindSlot(o.ExceptionNotificationObj, 39, vt);
        if (route == Route::Vtable) {
            BindSlot(o.OnTickBase, 15);
            BindSlot(o.BaseInitHigh, 41);
            BindSlot(o.BaseInitAppBand, 42);
            BindSlot(o.BaseInitLow, 43);
            BindSlot(o.BaseShutdownLow, 44);
            BindSlot(o.BaseShutdownAppBand, 45);
            BindSlot(o.BaseShutdownHigh, 46);
            BindSlot(o.BaseMainLoop, 48);
        } else {
            // cCTGFramework's overrides through its vtables; the base's overridden bodies called by name.
            BindSlot(o.OnTickCTG, 15, vt);
            BindSlot(o.InitHigh, 41, vt);
            BindSlot(o.InitAppBand, 42, vt);
            BindSlot(o.InitLow, 43, vt);
            BindSlot(o.ShutdownLow, 44, vt);
            BindSlot(o.ShutdownAppBand, 45, vt);
            BindSlot(o.ShutdownHigh, 46, vt);
            BindSlot(o.MainLoop, 48, vt);
            o.DoMessage = [](Fw *fw, void *msg) { return reinterpret_cast<bool(__thiscall *)(void *, void *)>(gOurCtgTargetVt[3])(&fw->mTarget, msg); };
            o.OnTickBase = [](Fw *fw, uint32_t c) { Our(fw)->Shape::OnTick(c); };
            o.BaseInitHigh = [](Fw *fw) { return Our(fw)->Shape::InitHighPriorityServices(); };
            o.BaseInitAppBand = [](Fw *fw) { return Our(fw)->Shape::InitAppBandServices(); };
            o.BaseInitLow = [](Fw *fw) { return Our(fw)->Shape::InitLowPriorityServices(); };
            o.BaseShutdownLow = [](Fw *fw) { return Our(fw)->Shape::ShutdownLowPriorityServices(); };
            o.BaseShutdownAppBand = [](Fw *fw) { return Our(fw)->Shape::ShutdownAppBandServices(); };
            o.BaseShutdownHigh = [](Fw *fw) { return Our(fw)->Shape::ShutdownHighPriorityServices(); };
            o.BaseMainLoop = [](Fw *fw) { return Our(fw)->Shape::MainLoop(); };
        }
        o.SetInstallFlag = [](Fw *fw, uint32_t v) { Our(fw)->SetInstallFlag(v); };
        o.CopyHooks = [](Fw *fw, std::list<FwRef> &out) { Our(fw)->CopyHooks(out); };
        o.BuildServiceList = [](Fw *fw, std::list<FwRef> &out, FwWalk walk) { Our(fw)->BuildServiceList(out, walk); };
        o.RunHooks[0] = [](Fw *fw) { return Our(fw)->RunPreFrameWorkInitHooks(); };
        o.RunHooks[1] = [](Fw *fw) { return Our(fw)->RunPreAppInitHooks(); };
        o.RunHooks[2] = [](Fw *fw) { return Our(fw)->RunPostAppInitHooks(); };
        o.RunHooks[3] = [](Fw *fw) { return Our(fw)->RunPreAppShutdownHooks(); };
        o.RunHooks[4] = [](Fw *fw) { return Our(fw)->RunPostAppShutdownHooks(); };
        o.RunHooks[5] = [](Fw *fw) { return Our(fw)->RunPostSystemServiceShutdownHooks(); };
        o.RunHooks[6] = [](Fw *fw) { return Our(fw)->RunAbortiveQuitHooks(); };
        o.ClearServiceContainers = [](Fw *fw) { return Our(fw)->ClearServiceContainers(); };
        o.ClearHooks = [](Fw *fw) { return Our(fw)->ClearHooks(); };
        o.ShutdownCOMObject = [](Fw *fw) { Our(fw)->ShutdownCOMObject(); };
    } else {
        // The hooks' adapters; the folded slots (not hooked: the game's shared bodies run for the live object) as our
        // members, called without dispatch.
        BindAdapter(o.QueryInterface, &A::QueryInterface);
        BindAdapter(o.AddRef, &A::AddRef);
        BindAdapter(o.Release, &A::Release);
        BindAdapter(o.AddSystemService, &A::AddSystemService);
        BindAdapter(o.RemoveSystemService, &A::RemoveSystemService);
        BindAdapter(o.GetSystemService, &A::GetSystemService);
        BindAdapter(o.EnumSystemServices, &A::EnumSystemServices);
        BindAdapter(o.AddHook, &A::AddHook);
        BindAdapter(o.RemoveHook, &A::RemoveHook);
        BindAdapter(o.AddToTick, &A::AddToTick);
        BindAdapter(o.RemoveFromTick, &A::RemoveFromTick);
        BindAdapter(o.AddToOnIdle, &A::AddToOnIdle);
        BindAdapter(o.RemoveFromOnIdle, &A::RemoveFromOnIdle);
        o.GetOnIdleInterval = [](Fw *fw) { return Our(fw)->Shape::GetOnIdleInterval(); };
        BindAdapter(o.SetOnIdleInterval, &A::SetOnIdleInterval);
        BindAdapter(o.OnTickBase, &A::OnTick);
        BindAdapter(o.OnIdle, &A::OnIdle);
        BindAdapter(o.IsTickEnabled, &A::IsTickEnabled);
        BindAdapter(o.ToggleTick, &A::ToggleTick);
        BindAdapter(o.Quit, &A::Quit);
        BindAdapter(o.AbortiveQuit, &A::AbortiveQuit);
        o.CommandLine = [](Fw *fw) -> void * { return Our(fw)->Shape::CommandLine(); };
        BindAdapter(o.IsInstall, &A::IsInstall);
        o.GetCOMObject = [](Fw *fw) -> void * { return Our(fw)->Shape::GetCOMObject(); };
        o.GetState = [](Fw *fw) { return Our(fw)->Shape::GetState(); };
        o.GetDebugStream = [](Fw *fw) { return Our(fw)->Shape::GetDebugStream(); };
        BindAdapter(o.DefaultDebugStream, &A::DefaultDebugStream);
        BindAdapter(o.DebugStream, &A::DefaultDebugStream); // slot 27 is the same function in the game
        o.SetDebugStream = [](Fw *fw, void *stream) { Our(fw)->Shape::SetDebugStream(stream); };
        BindAdapter(o.SetDebugLevel, &A::SetDebugLevel);
        o.GetDebugLevel = [](Fw *fw) { return static_cast<uint32_t>(Our(fw)->Shape::GetDebugLevel()); };
        BindAdapter(o.StdOut, &A::StdOut);
        BindAdapter(o.StdErr, &A::StdErr);
        BindAdapter(o.StdIn, &A::StdIn);
        BindAdapter(o.GetStream, &A::GetStream);
        BindAdapter(o.SetStream, &A::SetStream);
        BindAdapter(o.SetApplication, &A::SetApplication);
        BindAdapter(o.Application, &A::Application);
        BindAdapter(o.ReportException, &A::ReportException);
        o.ExceptionNotificationObj = [](Fw *fw) { return Our(fw)->Shape::ExceptionNotificationObj(); };
        BindAdapter(o.SetInstallFlag, &A::SetInstallFlag);
        BindAdapter(o.CopyHooks, &A::CopyHooks);
        BindAdapter(o.BuildServiceList, &A::BuildServiceList);
        BindAdapter(o.RunHooks[0], &A::RunPreFrameWorkInitHooks);
        BindAdapter(o.RunHooks[1], &A::RunPreAppInitHooks);
        BindAdapter(o.RunHooks[2], &A::RunPostAppInitHooks);
        BindAdapter(o.RunHooks[3], &A::RunPreAppShutdownHooks);
        BindAdapter(o.RunHooks[4], &A::RunPostAppShutdownHooks);
        BindAdapter(o.RunHooks[5], &A::RunPostSystemServiceShutdownHooks);
        BindAdapter(o.RunHooks[6], &A::RunAbortiveQuitHooks);
        BindAdapter(o.ClearServiceContainers, &A::ClearServiceContainers);
        BindAdapter(o.ClearHooks, &A::ClearHooks);
        BindAdapter(o.ShutdownCOMObject, &A::ShutdownCOMObject);
        BindAdapter(o.BaseInitHigh, &A::InitHighPriorityServices);
        BindAdapter(o.BaseInitAppBand, &A::InitAppBandServices);
        BindAdapter(o.BaseInitLow, &A::InitLowPriorityServices);
        BindAdapter(o.BaseShutdownLow, &A::ShutdownLowPriorityServices);
        BindAdapter(o.BaseShutdownAppBand, &A::ShutdownAppBandServices);
        BindAdapter(o.BaseShutdownHigh, &A::ShutdownHighPriorityServices);
        BindAdapter(o.BaseMainLoop, &A::MainLoop);
        using C = gz::cCTGFrameworkAdapters;
        BindAdapter(o.OnTickCTG, &C::OnTick);
        BindAdapter(o.InitHigh, &C::InitHighPriorityServices);
        BindAdapter(o.InitAppBand, &C::InitAppBandServices);
        BindAdapter(o.InitLow, &C::InitLowPriorityServices);
        BindAdapter(o.ShutdownLow, &C::ShutdownLowPriorityServices);
        BindAdapter(o.ShutdownAppBand, &C::ShutdownAppBandServices);
        BindAdapter(o.ShutdownHigh, &C::ShutdownHighPriorityServices);
        BindAdapter(o.MainLoop, &C::MainLoop);
        // DoMessage's adapter takes the cIGZMessageTarget2 face (+0x180), as the game's vtable hands it.
        o.DoMessage = [](Fw *fw, void *msg) { return C::DoMessage(reinterpret_cast<cIGZMessageTarget2 *>(&fw->mTarget), nullptr, msg); };
    }
    o.Run = []() { return GZFrameWork_Run(); };
    o.Shutdown = []() { return GZFrameWork_Shutdown(); };
    o.ReleaseGlobals = []() { ReleaseFrameworkGlobals(); };
    return o;
}

// ---------------------------------------------------------------------------------------------
// Snapshots

struct Snap {
    std::vector<uint32_t> scalars;
    std::vector<int> hooks, idle, idleRemoved, tick, tickRemoved;
    std::vector<std::tuple<int32_t, uint32_t, int>> services;
    std::vector<std::pair<uint32_t, int>> byId;
    std::vector<uint32_t> messageIds, manualIds;
    std::vector<std::tuple<int, int, int>> fakeState; // refs, running, idCalls
    bool operator==(const Snap &o) const
    {
        return scalars == o.scalars && hooks == o.hooks && idle == o.idle && idleRemoved == o.idleRemoved && tick == o.tick &&
               tickRemoved == o.tickRemoved && services == o.services && byId == o.byId && messageIds == o.messageIds &&
               manualIds == o.manualIds && fakeState == o.fakeState;
    }
};

Snap Take(Side &s)
{
    Snap n;
    Fw *fw = s.fw;
    auto idx = [&](const void *p) { return s.IndexOf(p); };
    n.scalars = {fw->mRefCount,           fw->mTickEnabled,     fw->mIdleInterval,    fw->mTickCounter,   fw->mIdleCounter,
                 fw->mInstall,            fw->mState,           fw->mServicesBusy,    fw->mLookupBusy,    fw->mIdleBusy,
                 fw->mTickBusy,           fw->mHooksBusy,       static_cast<uint32_t>(fw->mIdleNesting), static_cast<uint32_t>(fw->mTickNesting),
                 fw->mDeferQuit,          static_cast<uint32_t>(idx(fw->mDebugStream)), static_cast<uint32_t>(idx(fw->mStdOut.mObject)),
                 static_cast<uint32_t>(idx(fw->mStdErr.mObject)), static_cast<uint32_t>(idx(fw->mStdIn.mObject)),
                 static_cast<uint32_t>(fw->mDebugLevel), fw->mQuitPending, fw->mQuitCode, fw->mHInstance,
                 static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fw->mMainHWND))};
    for (auto &r : fw->mHooks) n.hooks.push_back(idx(r.mObject));
    for (auto &r : fw->mIdleList) n.idle.push_back(idx(r.mObject));
    for (auto &r : fw->mIdleRemoved) n.idleRemoved.push_back(idx(r.mObject));
    for (auto &r : fw->mTickList) n.tick.push_back(idx(r.mObject));
    for (auto &r : fw->mTickRemoved) n.tickRemoved.push_back(idx(r.mObject));
    for (auto &e : fw->mServices) n.services.emplace_back(e.first, e.second.mId, idx(e.second.mService.mObject));
    for (auto &e : fw->mServiceById) n.byId.emplace_back(e.first, idx(e.second.mObject));
    std::sort(n.byId.begin(), n.byId.end());
    if (s.route != Route::Vtable) { // cCTGFramework objects
        for (uint32_t v : fw->mTarget.mMessageIds) n.messageIds.push_back(v);
        for (uint32_t v : fw->mTarget.mManualServiceIds) n.manualIds.push_back(v);
    }
    for (auto &f : s.fakes) n.fakeState.emplace_back(f->refs, f->running, f->idCalls);
    return n;
}

std::string Describe(const std::vector<Event> &log, size_t at)
{
    char buf[128];
    if (at >= log.size()) return "(end)";
    const Event &e = log[at];
    std::snprintf(buf, sizeof(buf), "who=%d what=%d a=%u b=%u c=%u f=%08x/%08x/%u", e.who, e.what, e.a, e.b, e.c, e.flags, e.flags2, e.flags3);
    return buf;
}

// ---------------------------------------------------------------------------------------------
// Sides

// The mapped image's game globals the framework code reads, saved and restored around a test.
struct Globals {
    difftest::Context &ctx;
    uint32_t saved[8];
    static constexpr uint32_t kRvas[8] = {0x10c9378, 0x10c937c, 0x10c9380, 0x10c9384, 0x10fdfe8, 0x10f519c, 0x10cb840, 0x10cb844};
    explicit Globals(difftest::Context &c) : ctx(c)
    {
        for (int i = 0; i < 8; ++i) saved[i] = *ctx.image.At<uint32_t>(kRvas[i]);
    }
    ~Globals()
    {
        for (int i = 0; i < 8; ++i) *ctx.image.At<uint32_t>(kRvas[i]) = saved[i];
    }
    void Point(Side &s)
    {
        uint32_t fw = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(s.fw));
        *ctx.image.At<uint32_t>(0x10c9378) = fw;
        *ctx.image.At<uint32_t>(0x10c937c) = fw;
        *ctx.image.At<uint32_t>(0x10c9380) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(s.app));
        *ctx.image.At<uint32_t>(0x10c9384) = 0;
        *ctx.image.At<uint32_t>(0x10fdfe8) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(s.hookOwner));
        *ctx.image.At<uint32_t>(0x10f519c) = 0;
    }
};

struct Pair {
    Side a, b;
    explicit Pair(Route route = Route::Adapters)
    {
        a.original = true;
        a.route = b.route = route;
    }
};

constexpr uint32_t kFwSize = sizeof(cCTGFramework);

// The framework's constructor builds an embedded cRZRandom seeded from the clock (a 64-bit remainder by
// the QueryPerformanceCounter frequency the CRT caches at start-up); the harness doesn't run the CRT's
// start-up tables, so the cache is filled once with the CRT's own initialiser (0x83a409), as a real
// process start would.
void EnsureClock(difftest::Context &ctx)
{
    static bool done = false;
    if (!done) ctx.Original<int(__cdecl *)()>(0x83a409)();
    done = true;
}

// Trap vtables for the adapter route of the base tests (TrapScope): every slot of both faces' vtable copies is a trap
// with the slot's arity (stack arguments, so a trap that is reached returns cleanly) that logs its slot; the tests then
// put recorders into the slots the original legitimately calls. An adapter that dispatched virtually instead of calling
// our member (in game: back into its own hook) shows up in the log as a trap or recorder event the original doesn't make.
constexpr int kArity[49] = {2, 0, 0, 1, 1, 3, 3, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 0,
                            0, 0, 0, 1, 1, 0, 0, 0, 0, 3, 2, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr int kW32Arity[7] = {2, 0, 0, 0, 0, 0, 1};
constexpr int kTargetArity[4] = {2, 0, 0, 1}; // cIGZMessageTarget2: QueryInterface, AddRef, Release, DoMessage
template <int Who> uint32_t __fastcall Trap0(void *, void *)
{
    Log(kWhoFw, Who);
    return 0;
}
template <int Who> uint32_t __fastcall Trap1(void *, void *, uint32_t)
{
    Log(kWhoFw, Who);
    return 0;
}
template <int Who> uint32_t __fastcall Trap2(void *, void *, uint32_t, uint32_t)
{
    Log(kWhoFw, Who);
    return 0;
}
template <int Who> uint32_t __fastcall Trap3(void *, void *, uint32_t, uint32_t, uint32_t)
{
    Log(kWhoFw, Who);
    return 0;
}
template <int Who> void *TrapOf(int arity)
{
    switch (arity) {
    case 0: return reinterpret_cast<void *>(&Trap0<Who>);
    case 1: return reinterpret_cast<void *>(&Trap1<Who>);
    case 2: return reinterpret_cast<void *>(&Trap2<Who>);
    default: return reinterpret_cast<void *>(&Trap3<Who>);
    }
}
template <size_t... I> void FillTraps(void **vt, std::index_sequence<I...>)
{
    ((vt[I] = TrapOf<500 + static_cast<int>(I)>(kArity[I])), ...);
}
template <size_t... I> void FillW32Traps(void **vt, std::index_sequence<I...>)
{
    ((vt[I] = TrapOf<600 + static_cast<int>(I)>(kW32Arity[I])), ...);
}
template <size_t... I> void FillTargetTraps(void **vt, std::index_sequence<I...>)
{
    ((vt[I] = TrapOf<700 + static_cast<int>(I)>(kTargetArity[I])), ...);
}
bool gTraps = false;
struct TrapScope {
    TrapScope() { gTraps = true; }
    ~TrapScope() { gTraps = false; }
};
// The cCTGFramework tests' traps (CtgTrapScope): on the adapter route only the slots cCTGFramework overrides (15 OnTick,
// 40..48) and the +0x180 face's DoMessage are traps; the rest keep the game's functions, which the original's own virtual
// calls (through g_Framework to the message server, for one) need. An adapter that dispatched virtually reaches a trap.
bool gCtgTraps = false;
struct CtgTrapScope {
    CtgTrapScope() { gCtgTraps = true; }
    ~CtgTrapScope() { gCtgTraps = false; }
};

// Builds a framework object from `seed` bytes: on the adapter route with the original cCTGFramework constructor (both
// sides), on the vtable route with the original base constructor 0x141d76 (side A) or ours (side B, the hook's adapter),
// on the cCTGFramework route with the original 0xc2ccdb (side A) or ours (side B).
// The object's own vtable is then replaced by a copy (tests patch recording slots into it).
void BuildFw(difftest::Context &ctx, Side &s, uint32_t seed)
{
    EnsureClock(ctx);
    auto *mem = static_cast<uint8_t *>(::operator new(kFwSize));
    std::mt19937 rng(seed);
    for (uint32_t i = 0; i < kFwSize; ++i) mem[i] = static_cast<uint8_t>(rng());
    if (s.route == Route::Adapters || (s.route == Route::Ctg && s.original)) {
        ctx.Original<void *(__thiscall *)(void *)>(0xc2ccdb)(mem);
    } else if (s.route == Route::Ctg) {
        auto *ours = gz::cCTGFrameworkAdapters::Construct(mem, nullptr);
        gOurCtgVt = *reinterpret_cast<void *const *const *>(static_cast<cIGZFrameWork *>(ours));
        gOurCtgW32Vt = *reinterpret_cast<void *const *const *>(static_cast<cIGZFrameWorkW32 *>(ours));
        gOurCtgTargetVt = *reinterpret_cast<void *const *const *>(static_cast<cIGZMessageTarget2 *>(ours));
    } else if (s.original) {
        ctx.Original<void *(__thiscall *)(void *)>(0x141d76)(mem);
    } else {
        gz::cGZFrameworkAdapters::Construct(mem, nullptr);
        auto *ours = reinterpret_cast<Shape *>(mem);
        gOurVt = *reinterpret_cast<void *const *const *>(static_cast<cIGZFrameWork *>(ours));
        gOurW32Vt = *reinterpret_cast<void *const *const *>(static_cast<cIGZFrameWorkW32 *>(ours));
    }
    s.fw = reinterpret_cast<Fw *>(mem);
    std::memcpy(s.fwVt, s.fw->mVTable, sizeof(s.fwVt));
    s.fw->mVTable = s.fwVt;
    if (s.route != Route::Vtable) {
        std::memcpy(s.targetVt, s.fw->mTarget.mVTable, sizeof(s.targetVt));
        s.fw->mTarget.mVTable = s.targetVt;
    }
    if (gTraps && s.route == Route::Adapters) {
        FillTraps(s.fwVt, std::make_index_sequence<49>());
        FillW32Traps(s.w32Vt, std::make_index_sequence<7>());
        FillTargetTraps(s.targetVt, std::make_index_sequence<4>());
        s.fw->mW32VTable = s.w32Vt;
    } else if (gCtgTraps && s.route == Route::Adapters) {
        void *traps[49], *targetTraps[4];
        FillTraps(traps, std::make_index_sequence<49>());
        FillTargetTraps(targetTraps, std::make_index_sequence<4>());
        s.fwVt[15] = traps[15];
        for (int slot = 40; slot <= 48; ++slot) s.fwVt[slot] = traps[slot];
        s.targetVt[3] = targetTraps[3];
    }
    // cRZCriticalSection vtable: 0 Release, 1 Lock, 2 Unlock, 3 TryLock, 4 -, 5 IsLocked, 6 DeletingDestructor.
    std::memcpy(s.lockVt, ctx.image.At(cRZCriticalSection::kVTable), sizeof(void *) * 7);
    s.lockVt[1] = reinterpret_cast<void *>(&LockRec);
    s.lockVt[2] = reinterpret_cast<void *>(&UnlockRec);
    s.fw->mLock.mVTable = s.lockVt;
    // The embedded COM object (its own class; ours carries our cGZCOM vtable): its RealShutdown (slot 10) records.
    std::memcpy(s.comVt, *reinterpret_cast<void **>(s.fw->mCOM), sizeof(void *) * 14);
    s.comVt[10] = reinterpret_cast<void *>(&ComRealShutdownRec);
    *reinterpret_cast<void **>(s.fw->mCOM) = s.comVt;
}

// The recording slots of the framework's vtable copy, all of them (tests pick what they need).
void RecordFwSlots(Side &s, bool addRef, bool onTick, bool addService, bool tickEnabled, bool onIdle, bool toggle, bool streams, bool application, bool bands)
{
    if (addRef) s.fwVt[1] = reinterpret_cast<void *>(&FwAddRefRec);
    if (addService) s.fwVt[3] = reinterpret_cast<void *>(&FwAddSvcRec);
    if (onTick) s.fwVt[15] = reinterpret_cast<void *>(&FwOnTickRec);
    if (onIdle) s.fwVt[16] = reinterpret_cast<void *>(&FwOnIdleSlotRec);
    if (tickEnabled) s.fwVt[17] = reinterpret_cast<void *>(&FwIsTickEnabledRec);
    if (toggle) s.fwVt[18] = reinterpret_cast<void *>(&FwToggleRec);
    if (streams) {
        s.fwVt[25] = reinterpret_cast<void *>(&FwGetDebugStreamRec);
        s.fwVt[31] = reinterpret_cast<void *>(&FwStdOutRec);
        s.fwVt[32] = reinterpret_cast<void *>(&FwStdErrRec);
        s.fwVt[33] = reinterpret_cast<void *>(&FwStdInRec);
    }
    if (application) s.fwVt[37] = reinterpret_cast<void *>(&FwApplicationRec);
    if (bands) {
        s.fwVt[44] = reinterpret_cast<void *>(&FwBandRec<44>);
        s.fwVt[45] = reinterpret_cast<void *>(&FwBandRec<45>);
        s.fwVt[46] = reinterpret_cast<void *>(&FwBandRec<46>);
        s.fwVt[48] = reinterpret_cast<void *>(&FwBandRec<48>);
    }
}

// Pools the random setups draw from.
const uint32_t kIds[] = {0x10, 0x11, 0x12, 0x13, 0x056b906e, 0xeb7d6513, 0xdae3783f, 0x3ae4bea3, 0x04fa845b, 0xffffffff, 0};
const int32_t kPriorities[] = {10000010, 5000001, 999999, 1, 0, -1, -999999, -1999999, -2000000, -2000001, -2999999, -3000000, -3000001, -4000000};
const int32_t kTickPriorities[] = {-5, -1, 0, 0, 1, 7, 100};

// A 32-bit argument: often one of the values that sit on a boundary, otherwise random.
uint32_t Interesting(std::mt19937 &rng)
{
    const uint32_t edge[] = {0, 1, 2, 3, 0xffffffffu, 0x80000000u, 0x7fffffffu, 0x100, 0xff};
    return rng() % 3 == 0 ? edge[rng() % (sizeof(edge) / sizeof(edge[0]))] : static_cast<uint32_t>(rng());
}

Fake *NewService(Side &s, std::mt19937 &rng)
{
    Fake *f = NewFake(s, kSvcVt);
    f->id[0] = kIds[rng() % (sizeof(kIds) / sizeof(kIds[0]))];
    f->id[1] = rng() % 8 == 0 ? kIds[rng() % (sizeof(kIds) / sizeof(kIds[0]))] : f->id[0];
    f->id[2] = f->id[1];
    f->priority = kPriorities[rng() % (sizeof(kPriorities) / sizeof(kPriorities[0]))];
    f->tickPriority = kTickPriorities[rng() % (sizeof(kTickPriorities) / sizeof(kTickPriorities[0]))];
    f->running = static_cast<uint8_t>(rng() % 2);
    f->initOk = rng() % 5 != 0;
    f->shutdownOk = rng() % 5 != 0;
    f->qi.push_back({1, f});
    f->qi.push_back({0x287fb697, f});
    if (rng() % 2) f->qi.push_back({0x66, f});
    return f;
}

Fake *NewHook(Side &s)
{
    Fake *f = NewFake(s, kHookVt);
    f->qi.push_back({1, f});
    return f;
}

// Runs `op` on both sides (A: original, B: ours) with the globals pointed at each side's objects,
// and compares the result, the log and the snapshots. Returns a description of the first difference.
std::string RunBoth(difftest::Context &ctx, Globals &globals, Pair &p, Ops &orig, Ops &mine, const std::function<uint64_t(Side &, Ops &)> &op,
                    const std::function<void(Side &)> &between = nullptr)
{
    uint64_t r[2];
    Snap snap[2];
    for (int k = 0; k < 2; ++k) {
        Side &s = k == 0 ? p.a : p.b;
        gSide = &s;
        globals.Point(s);
        if (between) between(s);
        s.log.clear();
        s.ops = k == 0 ? &orig : &mine;
        r[k] = op(s, *s.ops);
        snap[k] = Take(s);
    }
    gSide = nullptr;
    ++ctx.cases;
    if (r[0] != r[1]) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "result %llx vs %llx", static_cast<unsigned long long>(r[0]), static_cast<unsigned long long>(r[1]));
        return buf;
    }
    const auto &la = p.a.log, &lb = p.b.log;
    size_t n = std::min(la.size(), lb.size());
    for (size_t i = 0; i < n; ++i) {
        if (!(la[i] == lb[i])) {
            if (GetEnvironmentVariableA("FWDUMP", nullptr, 0)) {
                for (size_t k = 0; k < std::max(la.size(), lb.size()); ++k)
                    std::fprintf(stderr, "%3zu  %-34s %-34s\n", k, Describe(la, k).c_str(), Describe(lb, k).c_str());
            }
            return "log differs at " + std::to_string(i) + ": " + Describe(la, i) + " | " + Describe(lb, i);
        }
    }
    if (la.size() != lb.size()) return "log length " + std::to_string(la.size()) + " vs " + std::to_string(lb.size()) + " next: " + Describe(la.size() > n ? la : lb, n);
    if (!(snap[0] == snap[1])) {
        const Snap &x = snap[0], &y = snap[1];
        std::string w = "snapshot:";
        if (x.scalars != y.scalars) {
            for (size_t i = 0; i < x.scalars.size(); ++i)
                if (x.scalars[i] != y.scalars[i]) w += " scalar#" + std::to_string(i) + "=" + std::to_string(x.scalars[i]) + "/" + std::to_string(y.scalars[i]);
        }
        if (x.hooks != y.hooks) w += " hooks";
        if (x.idle != y.idle) w += " idle";
        if (x.idleRemoved != y.idleRemoved) w += " idleRemoved";
        if (x.tick != y.tick) w += " tick";
        if (x.tickRemoved != y.tickRemoved) w += " tickRemoved";
        if (x.services != y.services) w += " services";
        if (x.byId != y.byId) w += " byId";
        if (x.messageIds != y.messageIds || x.manualIds != y.manualIds) w += " idsets";
        if (x.fakeState != y.fakeState) {
            w += " fakes";
            for (size_t i = 0; i < x.fakeState.size(); ++i)
                if (x.fakeState[i] != y.fakeState[i])
                    w += " [" + std::to_string(i) + "] refs " + std::to_string(std::get<0>(x.fakeState[i])) + "/" + std::to_string(std::get<0>(y.fakeState[i])) +
                         " run " + std::to_string(std::get<1>(x.fakeState[i])) + "/" + std::to_string(std::get<1>(y.fakeState[i]));
        }
        return w;
    }
    return "";
}

// Builds both sides with the same random fakes and a random setup (through the original functions),
// optionally with the lock/vtable recorders. `setup` runs on each side with the same rng seed.
void Populate(difftest::Context &ctx, Globals &globals, Pair &p, Ops &orig, uint32_t seed, int services, int hooks, bool addServices, bool tickIdle, bool addHooks)
{
    for (int k = 0; k < 2; ++k) {
        Side &s = k == 0 ? p.a : p.b;
        BuildFw(ctx, s, seed);
        s.fw->mRefCount = 1;
        std::mt19937 rng(seed * 7919u + 13u);
        gSide = &s;
        globals.Point(s);
        std::vector<Fake *> svc, hk;
        for (int i = 0; i < services; ++i) svc.push_back(NewService(s, rng));
        for (int i = 0; i < hooks; ++i) hk.push_back(NewHook(s));
        // setup runs unlocked and unlogged (the lock vtable is the original's until recording starts)
        for (Fake *f : svc) {
            if (addServices && rng() % 4 != 0) orig.AddSystemService(s.fw, f);
            if (tickIdle && rng() % 2) orig.AddToTick(s.fw, f);
            if (tickIdle && rng() % 2) orig.AddToOnIdle(s.fw, f);
        }
        for (Fake *f : hk)
            if (addHooks && rng() % 4 != 0) orig.AddHook(s.fw, f);
        for (auto &f : s.fakes) f->idCalls = 0;
        s.log.clear();
    }
    gSide = nullptr;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Tests

#define FAIL_IF(diff, ...)                                                         \
    do {                                                                           \
        if (!(diff).empty()) {                                                     \
            ctx.Fail(__VA_ARGS__);                                                 \
            ctx.Fail("   %s", (diff).c_str());                                     \
            return;                                                                \
        }                                                                          \
    } while (0)

DIFF_TEST(FW_Services, 0x14234f)
{
    TrapScope traps;
    Ops orig = MakeOps(ctx, true), mineA = MakeOps(ctx, false, Route::Adapters), mineV = MakeOps(ctx, false, Route::Vtable),
        mineC = MakeOps(ctx, false, Route::Ctg);
    Globals globals(ctx);
    for (uint32_t seed = 1; seed <= 3 * 400; ++seed) {
        Route route = RouteOf(seed, 400);
        Ops &mine = route == Route::Ctg ? mineC : route == Route::Vtable ? mineV : mineA;
        Pair p(route);
        Populate(ctx, globals, p, orig, seed, 6, 0, true, true, false);
        std::mt19937 rng(seed * 31u + 5u);
        for (int step = 0; step < 6; ++step) {
            int which = static_cast<int>(rng() % 8);
            size_t fakeIndex = rng() % 6; // the service fakes (later ones are callbacks)
            uint32_t id = kIds[rng() % (sizeof(kIds) / sizeof(kIds[0]))];
            uint32_t iid = rng() % 3 == 0 ? 0x287fb697u : (rng() % 2 ? 1u : 0x1234u);
            uint32_t arg1 = static_cast<uint32_t>(rng()), arg2 = static_cast<uint32_t>(rng());
            bool nullArg = rng() % 10 == 0;
            uint32_t busy = rng() % 6;
            if (GetEnvironmentVariableA("FWTRACE", nullptr, 0)) std::fprintf(stderr, "seed %u step %d op %d fake %zu null %d\n", seed, step, which, fakeIndex, nullArg);
            std::string diff = RunBoth(ctx, globals, p, orig, mine, [&](Side &s, Ops &o) -> uint64_t {
                void *svc = nullArg ? nullptr : s.fakes[fakeIndex].get();
                void *out = reinterpret_cast<void *>(0x7777);
                Fake *cb = nullptr;
                switch (which) {
                case 0: return o.AddSystemService(s.fw, svc);
                case 1: return o.RemoveSystemService(s.fw, svc);
                case 2: {
                    bool ok = o.GetSystemService(s.fw, id, iid, &out);
                    Log(kWhoFw, 99, Idx(out), ok);
                    return ok;
                }
                case 3: {
                    cb = NewFake(s, kCbVt);
                    return o.EnumSystemServices(s.fw, cb, arg1, arg2);
                }
                case 4: {
                    std::list<FwRef> l;
                    o.BuildServiceList(s.fw, l, FwWalk::Descending);
                    for (auto &r : l) Log(kWhoFw, 98, Idx(r.mObject));
                    return l.size();
                }
                case 5: {
                    std::list<FwRef> l;
                    o.BuildServiceList(s.fw, l, FwWalk::Ascending);
                    for (auto &r : l) Log(kWhoFw, 98, Idx(r.mObject));
                    return l.size();
                }
                case 6: {
                    // a busy flag set by an interrupted operation
                    s.fw->mServicesBusy = busy == 1;
                    s.fw->mLookupBusy = busy == 2;
                    uint64_t r = o.AddSystemService(s.fw, svc);
                    r = r * 2 + o.RemoveSystemService(s.fw, svc);
                    return r;
                }
                default: {
                    uint64_t r = o.AddSystemService(s.fw, svc);
                    r = r * 2 + o.AddSystemService(s.fw, svc);
                    return r;
                }
                }
            });
            FAIL_IF(diff, "seed %u step %d op %d", seed, step, which);
        }
    }
}

int gDepth = 0; // re-entrancy depth of the fakes' actions

DIFF_TEST(FW_Hooks, 0x14229a)
{
    TrapScope traps;
    Ops orig = MakeOps(ctx, true), mineA = MakeOps(ctx, false, Route::Adapters), mineV = MakeOps(ctx, false, Route::Vtable),
        mineC = MakeOps(ctx, false, Route::Ctg);
    Globals globals(ctx);
    for (uint32_t seed = 1; seed <= 3 * 400; ++seed) {
        Route route = RouteOf(seed, 400);
        Ops &mine = route == Route::Ctg ? mineC : route == Route::Vtable ? mineV : mineA;
        Pair p(route);
        Populate(ctx, globals, p, orig, seed, 0, 5, false, false, true);
        // Some hooks re-enter the framework from inside a runner: they remove themselves, add another
        // hook, or run the same runner again (nested copies).
        std::mt19937 rng(seed * 97u + 3u);
        for (Side *s : {&p.a, &p.b}) {
            std::mt19937 r(seed * 97u + 3u);
            for (size_t i = 0; i < s->fakes.size(); ++i) {
                int mode = static_cast<int>(r() % 4);
                size_t other = r() % s->fakes.size();
                for (int slot = 3; slot <= 9; ++slot) {
                    if (mode == 1) s->fakes[i]->action[slot] = [i](Side &sd, Fake &) { sd.ops->RemoveHook(sd.fw, sd.fakes[i].get()); };
                    if (mode == 2) s->fakes[i]->action[slot] = [other](Side &sd, Fake &) { sd.ops->AddHook(sd.fw, sd.fakes[other].get()); };
                    if (mode == 3) s->fakes[i]->action[slot] = [slot](Side &sd, Fake &) {
                        if (gDepth < 2) {
                            ++gDepth;
                            sd.ops->RunHooks[slot - 3](sd.fw);
                            --gDepth;
                        }
                    };
                }
            }
        }
        for (int step = 0; step < 6; ++step) {
            int which = static_cast<int>(rng() % 11);
            size_t fakeIndex = rng() % 5;
            uint32_t busy = rng() % 5;
            std::string diff = RunBoth(ctx, globals, p, orig, mine, [&](Side &s, Ops &o) -> uint64_t {
                void *hook = s.fakes[fakeIndex].get();
                switch (which) {
                case 0: return o.AddHook(s.fw, hook);
                case 1: return o.RemoveHook(s.fw, hook);
                case 2: {
                    std::list<FwRef> l;
                    o.CopyHooks(s.fw, l);
                    for (auto &r : l) Log(kWhoFw, 98, Idx(r.mObject));
                    return l.size();
                }
                case 3: case 4: case 5: case 6: case 7: case 8: case 9: return o.RunHooks[which - 3](s.fw);
                case 10: {
                    s.fw->mHooksBusy = busy == 0;
                    uint64_t r = o.AddHook(s.fw, hook);
                    r = r * 2 + o.RemoveHook(s.fw, hook);
                    r = r * 2 + o.RunHooks[static_cast<int>(busy) % 7](s.fw);
                    return r;
                }
                default: return 0;
                }
            });
            FAIL_IF(diff, "seed %u step %d op %d", seed, step, which);
        }
    }
}

DIFF_TEST(FW_Clear, 0x1427bb)
{
    TrapScope traps;
    Ops orig = MakeOps(ctx, true), mineA = MakeOps(ctx, false, Route::Adapters), mineV = MakeOps(ctx, false, Route::Vtable),
        mineC = MakeOps(ctx, false, Route::Ctg);
    Globals globals(ctx);
    for (uint32_t seed = 1; seed <= 3 * 200; ++seed) {
        Route route = RouteOf(seed, 200);
        Ops &mine = route == Route::Ctg ? mineC : route == Route::Vtable ? mineV : mineA;
        Pair p(route);
        Populate(ctx, globals, p, orig, seed, 7, 4, true, true, true);
        std::mt19937 rng(seed);
        int which = static_cast<int>(rng() % 3);
        std::string diff = RunBoth(ctx, globals, p, orig, mine, [&](Side &s, Ops &o) -> uint64_t {
            // Park some removed services too, so every list has something to release.
            s.fw->mTickNesting = 1;
            s.fw->mIdleNesting = 1;
            for (size_t i = 0; i < 3; ++i) {
                o.RemoveFromTick(s.fw, s.fakes[i].get());
                o.RemoveFromOnIdle(s.fw, s.fakes[i].get());
            }
            s.fw->mTickNesting = 0;
            s.fw->mIdleNesting = 0;
            s.log.clear();
            if (which == 0) return o.ClearServiceContainers(s.fw);
            if (which == 1) return o.ClearHooks(s.fw);
            return o.ClearServiceContainers(s.fw) * 2 + o.ClearHooks(s.fw);
        });
        FAIL_IF(diff, "seed %u op %d", seed, which);
    }
}

DIFF_TEST(FW_TickIdle, 0x1424fe)
{
    TrapScope traps;
    Ops orig = MakeOps(ctx, true), mineA = MakeOps(ctx, false, Route::Adapters), mineV = MakeOps(ctx, false, Route::Vtable),
        mineC = MakeOps(ctx, false, Route::Ctg);
    Globals globals(ctx);
    ImportPatch patch(ctx);
    patch.Set(0xd9a6f8, reinterpret_cast<void *>(&SteamStub));
    for (uint32_t seed = 1; seed <= 3 * 600; ++seed) {
        Route route = RouteOf(seed, 600);
        Ops &mine = route == Route::Ctg ? mineC : route == Route::Vtable ? mineV : mineA;
        Pair p(route);
        Populate(ctx, globals, p, orig, seed, 6, 0, false, true, false);
        // Services that re-enter the framework from inside OnTick / OnIdle.
        for (Side *s : {&p.a, &p.b}) {
            std::mt19937 r(seed * 131u + 9u);
            for (size_t i = 0; i < s->fakes.size(); ++i) {
                int mode = static_cast<int>(r() % 6);
                size_t other = r() % s->fakes.size();
                for (int slot : {10, 11}) {
                    bool tick = slot == 10;
                    if (mode == 1) s->fakes[i]->action[slot] = [i, tick](Side &sd, Fake &) {
                        tick ? sd.ops->RemoveFromTick(sd.fw, sd.fakes[i].get()) : sd.ops->RemoveFromOnIdle(sd.fw, sd.fakes[i].get());
                    };
                    if (mode == 2) s->fakes[i]->action[slot] = [other, tick](Side &sd, Fake &) {
                        tick ? sd.ops->RemoveFromTick(sd.fw, sd.fakes[other].get()) : sd.ops->RemoveFromOnIdle(sd.fw, sd.fakes[other].get());
                    };
                    if (mode == 3) s->fakes[i]->action[slot] = [other, tick](Side &sd, Fake &) {
                        tick ? sd.ops->AddToTick(sd.fw, sd.fakes[other].get()) : sd.ops->AddToOnIdle(sd.fw, sd.fakes[other].get());
                    };
                    if (mode == 4) s->fakes[i]->action[slot] = [tick](Side &sd, Fake &) {
                        if (gDepth < 3) {
                            ++gDepth;
                            // (base objects have no cCTGFramework OnTick: the base's re-enters there)
                            if (!tick) sd.ops->OnIdle(sd.fw);
                            else if (sd.route == Route::Vtable) sd.ops->OnTickBase(sd.fw, 99);
                            else sd.ops->OnTickCTG(sd.fw, 99);
                            --gDepth;
                        }
                    };
                }
            }
        }
        std::mt19937 rng(seed * 53u + 1u);
        for (int step = 0; step < 6; ++step) {
            int which = static_cast<int>(rng() % 13);
            size_t fakeIndex = rng() % 6;
            uint32_t counter = Interesting(rng);
            uint32_t tickCounter = rng() % 12, interval = rng() % 6;
            uint32_t flags = rng() % 8;
            std::string diff = RunBoth(ctx, globals, p, orig, mine, [&](Side &s, Ops &o) -> uint64_t {
                RecordFwSlots(s, false, true, false, false, false, false, false, false, false);
                void *svc = s.fakes[fakeIndex].get();
                s.fw->mTickCounter = tickCounter;

                s.fw->mIdleInterval = interval ? interval : 1; // never 0: OnIdle divides by it
                switch (which) {
                case 0: return o.AddToTick(s.fw, svc);
                case 1: return o.RemoveFromTick(s.fw, svc);
                case 2: return o.AddToOnIdle(s.fw, svc);
                case 3: return o.RemoveFromOnIdle(s.fw, svc);
                case 4: o.OnTickBase(s.fw, counter); return 0;
                case 5: // cCTGFramework's OnTick (0xc2d427: running services only); base objects have none
                    if (s.route == Route::Vtable) return 0;
                    o.OnTickCTG(s.fw, counter);
                    return 0;
                case 6: case 7: o.OnIdle(s.fw); return s.fw->mIdleCounter;
                case 8: {
                    s.fw->mTickBusy = flags & 1;
                    s.fw->mIdleBusy = (flags >> 1) & 1;
                    s.fw->mTickNesting = flags & 2 ? 1 : 0;
                    s.fw->mIdleNesting = flags & 4 ? 1 : 0;
                    uint64_t r = o.AddToTick(s.fw, svc);
                    r = r * 2 + o.RemoveFromTick(s.fw, svc);
                    r = r * 2 + o.AddToOnIdle(s.fw, svc);
                    r = r * 2 + o.RemoveFromOnIdle(s.fw, svc);
                    return r;
                }
                case 9: {
                    // a service queued twice, then removed twice
                    uint64_t r = o.AddToTick(s.fw, svc);
                    r = r * 2 + o.AddToTick(s.fw, svc);
                    r = r * 2 + o.RemoveFromTick(s.fw, svc);
                    r = r * 2 + o.RemoveFromTick(s.fw, svc);
                    return r;
                }
                case 10: {
                    s.fw->mTickNesting = 2;
                    s.fw->mIdleNesting = 2;
                    uint64_t r = o.RemoveFromTick(s.fw, svc);
                    r = r * 2 + o.RemoveFromOnIdle(s.fw, svc);
                    return r;
                }
                case 11: {
                    o.SetOnIdleInterval(s.fw, static_cast<int32_t>(counter));
                    return o.GetOnIdleInterval(s.fw);
                }
                default: {
                    o.OnIdle(s.fw);
                    o.OnIdle(s.fw);
                    return s.fw->mIdleCounter * 7 + s.fw->mTickCounter;
                }
                }
            });
            FAIL_IF(diff, "seed %u step %d op %d", seed, step, which);
        }
    }
    FAIL_IF(std::string(gSteamCalls == 0 ? "the Steam callbacks were never called" : ""), "OnIdle");
}

namespace {

// Random values for the plain fields, the same on both sides.
void Perturb(Side &s, uint32_t seed)
{
    std::mt19937 rng(seed);
    Fw *fw = s.fw;
    fw->mRefCount = rng() % 5 == 0 ? static_cast<uint32_t>(rng()) : rng() % 4;
    fw->mTickEnabled = static_cast<uint8_t>(rng());
    fw->mIdleInterval = rng();
    fw->mInstall = static_cast<uint8_t>(rng());
    fw->mState = rng();
    fw->mDeferQuit = static_cast<uint8_t>(rng());
    fw->mDebugStream = reinterpret_cast<void *>(static_cast<uintptr_t>(rng()));
    fw->mDebugLevel = static_cast<int32_t>(rng());
    fw->mQuitPending = static_cast<uint8_t>(rng());
    fw->mQuitCode = rng();
    fw->mHInstance = rng();
    fw->mMainHWND = reinterpret_cast<void *>(static_cast<uintptr_t>(rng()));
}

uint64_t Offset(const Fw *fw, const void *p)
{
    uintptr_t base = reinterpret_cast<uintptr_t>(fw), v = reinterpret_cast<uintptr_t>(p);
    return v >= base && v < base + kFwSize ? v - base : 0x10000000u + static_cast<uint32_t>(v);
}

} // namespace

DIFF_TEST(FW_Basics, 0x1431b6)
{
    TrapScope traps;
    Ops orig = MakeOps(ctx, true), mineA = MakeOps(ctx, false, Route::Adapters), mineV = MakeOps(ctx, false, Route::Vtable);
    Globals globals(ctx);
    ImportPatch patch(ctx);
    patch.Set(0xd9a168, reinterpret_cast<void *>(&SetEnvStub));
    auto origHInstance = ctx.Original<uint32_t(__thiscall *)(void *)>(0x142950);
    auto origGetHwnd = ctx.Original<void *(__thiscall *)(void *)>(0x142857);
    auto origSetHwnd = ctx.Original<void(__thiscall *)(void *, void *)>(0x1436d9);
    auto origAsFw = ctx.Original<void *(__thiscall *)(void *)>(0x142700);
    auto origSlot47 = ctx.Original<void(__thiscall *)(void *)>(0x140fe0);
    // The +4 face's cIGZUnknown thunks (ours are the compiler's, in our vtable): 0x1431f4 QI, 0x142347 AddRef, 0x143273 Release.
    auto origW32Qi = ctx.Original<bool(__thiscall *)(void *, uint32_t, void **)>(0x1431f4);
    auto origW32AddRef = ctx.Original<uint32_t(__thiscall *)(void *)>(0x142347);
    auto origW32Release = ctx.Original<uint32_t(__thiscall *)(void *)>(0x143273);
    // cCTGFramework's own cIGZUnknown entries (the cCTGFramework route's original side): the forwards 0xc2d995 QI,
    // 0xc2ceeb AddRef, 0xc2d9ad Release (5-byte jumps to the base's), the +4 face's thunks 0xc2d99a / 0xc2cef0 / 0xc2d9b2
    // and the +0x180 face's 0xc2d9a2 / 0xc2cef8 / 0xc2d9ba. Ours are the compiler's, in our cCTGFramework vtables.
    Ops origC = orig;
    BindOrig(origC.QueryInterface, ctx, 0xc2d995);
    BindOrig(origC.AddRef, ctx, 0xc2ceeb);
    BindOrig(origC.Release, ctx, 0xc2d9ad);
    Ops mineC = MakeOps(ctx, false, Route::Ctg);
    auto ctgW32Qi = ctx.Original<bool(__thiscall *)(void *, uint32_t, void **)>(0xc2d99a);
    auto ctgW32AddRef = ctx.Original<uint32_t(__thiscall *)(void *)>(0xc2cef0);
    auto ctgW32Release = ctx.Original<uint32_t(__thiscall *)(void *)>(0xc2d9b2);
    auto ctgTargetQi = ctx.Original<bool(__thiscall *)(void *, uint32_t, void **)>(0xc2d9a2);
    auto ctgTargetAddRef = ctx.Original<uint32_t(__thiscall *)(void *)>(0xc2cef8);
    auto ctgTargetRelease = ctx.Original<uint32_t(__thiscall *)(void *)>(0xc2d9ba);
    using A = gz::cGZFrameworkAdapters;
    // Our +4 face's slots: through our W32 vtable on the vtable routes; the adapters (hooked) or our members without
    // dispatch (folded, not hooked) on the adapter route; the thunks there are the game's, which jump to the hooked
    // primaries, i.e. the primary adapters.
    const uint32_t iids[] = {1, 0x65, 0x23ea70a1, 0, 0x66, 0x64, 0x23ea70a0, 0x23ea70a2, 0xffffffff, 0x287fb697};
    for (uint32_t seed = 1; seed <= 3 * 400; ++seed) {
        Route route = seed > 800 ? Route::Ctg : seed > 400 ? Route::Vtable : Route::Adapters;
        Ops &mine = route == Route::Ctg ? mineC : route == Route::Vtable ? mineV : mineA;
        Ops &original = route == Route::Ctg ? origC : orig;
        Pair p(route);
        Populate(ctx, globals, p, orig, seed, 2, 0, false, false, false);
        std::mt19937 rng(seed * 17u + 2u);
        for (int step = 0; step < 8; ++step) {
            int which = static_cast<int>(rng() % 30);
            uint32_t pert = static_cast<uint32_t>(rng());
            uint32_t riid = rng() % 4 == 0 ? static_cast<uint32_t>(rng()) : iids[rng() % (sizeof(iids) / sizeof(iids[0]))];
            uint32_t value = Interesting(rng);
            std::string diff = RunBoth(
                ctx, globals, p, original, mine,
                [&](Side &s, Ops &o) -> uint64_t {
                    RecordFwSlots(s, true, false, false, false, false, false, true, false, false);
                    Fw *fw = s.fw;
                    Fake *obj = s.fakes[0].get();
                    char *w32 = reinterpret_cast<char *>(fw) + 4;
                    char *target = reinterpret_cast<char *>(&fw->mTarget);
                    bool ctg = s.route == Route::Ctg;
                    bool vt = s.route != Route::Adapters;
                    auto w32Slot = [ctg](int slot) { return (ctg ? gOurCtgW32Vt : gOurW32Vt)[slot]; };
                    Shape *our = reinterpret_cast<Shape *>(fw);
                    switch (which) {
                    case 0: case 1: case 2: {
                        void *out = reinterpret_cast<void *>(0x1234);
                        bool ok = o.QueryInterface(fw, riid, &out);
                        return static_cast<uint64_t>(ok) | (Offset(fw, out) << 8);
                    }
                    case 3: return o.AddRef(fw);
                    case 4: return o.Release(fw);
                    case 5: return o.IsTickEnabled(fw);
                    case 6: return o.IsInstall(fw);
                    case 7: return o.GetState(fw);
                    case 8: return o.GetDebugLevel(fw);
                    case 9: return o.GetOnIdleInterval(fw);
                    case 10: return Offset(fw, o.CommandLine(fw)) * 1000003u + Offset(fw, o.GetCOMObject(fw));
                    case 11: return Offset(fw, o.GetDebugStream(fw)) + Offset(fw, o.DefaultDebugStream(fw)) * 1000003u + Offset(fw, o.DebugStream(fw)) * 998244353u;
                    case 12: o.SetDebugStream(fw, reinterpret_cast<void *>(static_cast<uintptr_t>(value))); return Offset(fw, fw->mDebugStream);
                    case 13: o.SetOnIdleInterval(fw, static_cast<int32_t>(value)); return fw->mIdleInterval;
                    case 14: o.ToggleTick(fw, value); return fw->mTickEnabled;
                    case 15: o.SetInstallFlag(fw, value); return fw->mDeferQuit;
                    case 16: return o.ExceptionNotificationObj(fw) == fw->mExceptionNotification.mObject && fw->mExceptionNotification.mObject != nullptr; // a heap object
                    case 17: {
                        o.SetApplication(fw, obj);
                        return Idx(o.Application(fw));
                    }
                    case 18: {
                        bool ok = o.SetDebugLevel(fw, static_cast<int32_t>(value));
                        return static_cast<uint64_t>(ok) | (static_cast<uint64_t>(fw->mDebugLevel) << 8);
                    }
                    case 19: {
                        if (s.original) return reinterpret_cast<uintptr_t>(reinterpret_cast<void *>(origHInstance(w32)));
                        if (vt) return reinterpret_cast<uintptr_t>(reinterpret_cast<void *(__thiscall *)(void *)>(w32Slot(4))(w32));
                        return reinterpret_cast<uintptr_t>(our->Shape::GetWindowsInstance());
                    }
                    case 20: {
                        if (s.original) return Offset(fw, origGetHwnd(w32));
                        if (vt) return Offset(fw, reinterpret_cast<void *(__thiscall *)(void *)>(w32Slot(5))(w32));
                        return Offset(fw, A::GetMainHWND(reinterpret_cast<cIGZFrameWorkW32 *>(w32), nullptr));
                    }
                    case 21: {
                        void *hwnd = reinterpret_cast<void *>(static_cast<uintptr_t>(value));
                        if (s.original) origSetHwnd(w32, hwnd);
                        else if (vt) reinterpret_cast<void(__thiscall *)(void *, void *)>(w32Slot(6))(w32, hwnd);
                        else A::SetMainHWND(reinterpret_cast<cIGZFrameWorkW32 *>(w32), nullptr, hwnd);
                        return Offset(fw, fw->mMainHWND);
                    }
                    case 22: {
                        if (s.original) return Offset(fw, origAsFw(w32));
                        if (vt) return Offset(fw, reinterpret_cast<void *(__thiscall *)(void *)>(w32Slot(3))(w32));
                        return Offset(fw, our->Shape::AsIGZFrameWork());
                    }
                    case 23: {
                        // the empty slot 47: nothing changes
                        Snap before = Take(s);
                        if (s.original) origSlot47(fw);
                        else if (vt) reinterpret_cast<void(__thiscall *)(void *)>((ctg ? gOurCtgVt : gOurVt)[47])(fw);
                        else our->Shape::Slot47();
                        return before == Take(s);
                    }
                    case 24: {
                        // QueryInterface through the +4 face
                        void *out = reinterpret_cast<void *>(0x1234);
                        bool ok;
                        if (s.original) ok = ctg ? ctgW32Qi(w32, riid, &out) : origW32Qi(w32, riid, &out);
                        else if (vt) ok = reinterpret_cast<bool(__thiscall *)(void *, uint32_t, void **)>(w32Slot(0))(w32, riid, &out);
                        else ok = A::QueryInterface(reinterpret_cast<cIGZFrameWork *>(fw), nullptr, riid, &out);
                        return static_cast<uint64_t>(ok) | (Offset(fw, out) << 8);
                    }
                    case 25: {
                        if (s.original) return ctg ? ctgW32AddRef(w32) : origW32AddRef(w32);
                        if (vt) return reinterpret_cast<uint32_t(__thiscall *)(void *)>(w32Slot(1))(w32);
                        return A::AddRef(reinterpret_cast<cIGZFrameWork *>(fw), nullptr);
                    }
                    case 26: {
                        if (s.original) return ctg ? ctgW32Release(w32) : origW32Release(w32);
                        if (vt) return reinterpret_cast<uint32_t(__thiscall *)(void *)>(w32Slot(2))(w32);
                        return A::Release(reinterpret_cast<cIGZFrameWork *>(fw), nullptr);
                    }
                    case 27: case 28: case 29: {
                        // The +0x180 face's cIGZUnknown thunks (cCTGFramework objects only). On the adapter route the
                        // game's thunks jump to the hooked primaries, i.e. the primary adapters.
                        if (s.route == Route::Vtable) return 0;
                        if (which == 27) {
                            void *out = reinterpret_cast<void *>(0x1234);
                            bool ok;
                            if (s.original) ok = ctgTargetQi(target, riid, &out);
                            else if (ctg) ok = reinterpret_cast<bool(__thiscall *)(void *, uint32_t, void **)>(gOurCtgTargetVt[0])(target, riid, &out);
                            else ok = A::QueryInterface(reinterpret_cast<cIGZFrameWork *>(fw), nullptr, riid, &out);
                            return static_cast<uint64_t>(ok) | (Offset(fw, out) << 8);
                        }
                        if (which == 28) {
                            if (s.original) return ctgTargetAddRef(target);
                            if (ctg) return reinterpret_cast<uint32_t(__thiscall *)(void *)>(gOurCtgTargetVt[1])(target);
                            return A::AddRef(reinterpret_cast<cIGZFrameWork *>(fw), nullptr);
                        }
                        if (s.original) return ctgTargetRelease(target);
                        if (ctg) return reinterpret_cast<uint32_t(__thiscall *)(void *)>(gOurCtgTargetVt[2])(target);
                        return A::Release(reinterpret_cast<cIGZFrameWork *>(fw), nullptr);
                    }
                    default: return 0;
                    }
                },
                [&](Side &s) { Perturb(s, pert); });
            FAIL_IF(diff, "seed %u step %d op %d", seed, step, which);
        }
    }
}

namespace {

// A stream fake: accepts the standard-out/err and standard-in interface ids as configured.
Fake *NewStream(Side &s, std::mt19937 &rng)
{
    Fake *f = NewFake(s, kStreamVt);
    f->qi.push_back({1, f});
    if (rng() % 3 != 0) f->qi.push_back({0xc51b11cb, f});
    if (rng() % 3 != 0) f->qi.push_back({0x051b11bd, f});
    return f;
}

void DrainMessages()
{
    MSG m;
    while (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE)) {
    }
}

} // namespace

DIFF_TEST(FW_Streams, 0x142877)
{
    TrapScope traps;
    Ops orig = MakeOps(ctx, true), mineA = MakeOps(ctx, false, Route::Adapters), mineV = MakeOps(ctx, false, Route::Vtable),
        mineC = MakeOps(ctx, false, Route::Ctg);
    Globals globals(ctx);
    ImportPatch patch(ctx);
    patch.Set(0xd9a568, reinterpret_cast<void *>(&MessageBoxStub));
    patch.Set(0xd9a56c, reinterpret_cast<void *>(GetProcAddress(GetModuleHandleA("user32.dll"), "PostQuitMessage")));
    for (uint32_t seed = 1; seed <= 3 * 500; ++seed) {
        Route route = RouteOf(seed, 500);
        Ops &mine = route == Route::Ctg ? mineC : route == Route::Vtable ? mineV : mineA;
        Pair p(route);
        Populate(ctx, globals, p, orig, seed, 0, 0, false, false, false);
        for (Side *s : {&p.a, &p.b}) {
            std::mt19937 r(seed + 99u);
            for (int i = 0; i < 6; ++i) NewStream(*s, r);
        }
        std::mt19937 rng(seed * 29u + 7u);
        for (int step = 0; step < 8; ++step) {
            int which = static_cast<int>(rng() % 11);
            uint32_t kind = rng() % 5;
            uint32_t iid = rng() % 2 ? (rng() % 2 ? 0xc51b11cbu : 0x051b11bdu) : static_cast<uint32_t>(rng() % 3);
            bool withOut = rng() % 6 != 0, withStream = rng() % 5 != 0;
            size_t streamIndex = rng() % 6;
            uint32_t code = static_cast<uint32_t>(rng() % 100);
            bool nullMessage = rng() % 3 == 0;
            uint32_t pert = static_cast<uint32_t>(rng());
            std::string diff = RunBoth(
                ctx, globals, p, orig, mine,
                [&](Side &s, Ops &o) -> uint64_t {
                    RecordFwSlots(s, false, false, false, false, false, true, true, false, false);
                    for (int i = 0; i < 3; ++i) gStreams[i] = s.fakes[static_cast<size_t>(i)].get();
                    Fw *fw = s.fw;
                    switch (which) {
                    case 0: case 1: {
                        void *out = reinterpret_cast<void *>(0x4321);
                        bool ok = o.GetStream(fw, kind, iid, withOut ? &out : nullptr);
                        Log(kWhoFw, 97, Idx(out));
                        return ok;
                    }
                    case 2: case 3: {
                        void *stream = withStream ? s.fakes[streamIndex].get() : nullptr;
                        bool ok = o.SetStream(fw, kind, stream);
                        Log(kWhoFw, 96, Idx(fw->mStdOut.mObject), Idx(fw->mStdErr.mObject), Idx(fw->mStdIn.mObject));
                        return ok;
                    }
                    case 4: o.ReportException(fw, nullMessage ? nullptr : "A test message"); return 0;
                    case 5: {
                        DrainMessages();
                        o.Quit(fw, code);
                        MSG m;
                        uint64_t r = 0;
                        if (PeekMessageA(&m, nullptr, 0, 0, PM_REMOVE) && m.message == WM_QUIT) {
                            Log(kWhoImport, 4, static_cast<uint32_t>(m.wParam));
                            r = 1;
                        }
                        DrainMessages();
                        return r;
                    }
                    case 8: case 9: case 10: {
                        // StdOut / StdErr / StdIn (0x143887, 0x1437e7, 0x143837), twice: an empty slot gets a stream
                        // object (built by the original stream constructors on both sides), referenced once by the
                        // slot; the second call returns the same object. A slot already holding a fake returns it.
                        auto &get = which == 8 ? o.StdOut : which == 9 ? o.StdErr : o.StdIn;
                        void *first = get(fw);
                        void *second = get(fw);
                        int idx = s.IndexOf(first);
                        uint64_t r = (first == second ? 1u : 0u) | (static_cast<uint64_t>(static_cast<uint32_t>(idx)) << 1);
                        if (first && idx == -100) {
                            // Every field of the new object (its vtable is the game's stream class on both sides; +4 the
                            // count: 1; +8 and +0x10 are bytes, the rest of their words is the allocator's garbage).
                            const uint32_t *words = static_cast<const uint32_t *>(first);
                            for (int i = 0; i < (which == 10 ? 5 : 4); ++i) r = r * 1000003u + (i == 2 || i == 4 ? words[i] & 0xff : words[i]);
                            r = r * 1000003u + _msize(first); // the allocation's size (0x10 / 0x14)
                        }
                        Log(kWhoFw, 96, Idx(fw->mStdOut.mObject), Idx(fw->mStdErr.mObject), Idx(fw->mStdIn.mObject));
                        return r;
                    }
                    default: {
                        // a stream set, then fetched
                        void *stream = s.fakes[streamIndex].get();
                        uint64_t r = o.SetStream(fw, kind, stream);
                        gStreams[0] = gStreams[1] = gStreams[2] = stream;
                        void *out = nullptr;
                        r = r * 2 + o.GetStream(fw, kind, iid, &out);
                        Log(kWhoFw, 97, Idx(out));
                        return r;
                    }
                    }
                },
                [&](Side &s) { Perturb(s, pert); });
            FAIL_IF(diff, "seed %u step %d op %d", seed, step, which);
        }
    }
}

namespace {

// Service ids for the band tests: ordinary ones, ones in the framework's manual set (not auto-started),
// the application's own, and odd values. 0x056b906e (the persist manager, which triggers a message
// after Init) and 0x04fa845b (the message server) are left to their own tests.
const uint32_t kBandIds[] = {0x10, 0x11, 0x12, 0x13, 0xeb7d6513, 0xdae3783f, 0x7a3e667d, 0x9b3798f7, 0x1835d96f, 0x3ae4bea3, 0, 0xffffffff};
const int32_t kBandPriorities[] = {10000010, 5000001, 1, 0, -1, -1999999, -2000000, -2000000, -2000001, -2500000, -2999999, -2999999,
                                   -3000000, -3000001, -4000000};

// Builds both sides with an application, a message server (found by id through the hash map only, so
// the band loops don't see it) and `count` random services registered through the original.
void BuildBand(difftest::Context &ctx, Globals &globals, Pair &p, Ops &orig, uint32_t seed, int count, bool allowPersist, bool withMessageServer = true)
{
    for (int k = 0; k < 2; ++k) {
        Side &s = k == 0 ? p.a : p.b;
        BuildFw(ctx, s, seed);
        s.fw->mRefCount = 1;
        gSide = &s;
        globals.Point(s);
        s.app = NewFake(s, kAppVt);                  // 0
        Fake *appService = NewFake(s, kSvcVt);       // 1
        appService->id[0] = appService->id[1] = appService->id[2] = 0x5afdaae5;
        appService->priority = -2000000;
        appService->qi = {{1, appService}, {0x66, appService}};
        s.app->qi = {{1, s.app}, {0x287fb697, appService}};
        s.messageServer = NewFake(s, kMsVt);         // 2
        Fake *face = NewFake(s, kSvcVt);             // 3
        face->qi = {{1, face}};
        s.messageServer->qi = {{0x652294c7, s.messageServer}, {0x287fb697, face}};
        if (withMessageServer) s.fw->mServiceById[0x04fa845b].Assign(s.messageServer);
        std::mt19937 rng(seed * 7919u + 13u);
        for (int i = 0; i < count; ++i) {
            Fake *f = NewService(s, rng);
            do {
                f->id[0] = kBandIds[rng() % (sizeof(kBandIds) / sizeof(kBandIds[0]))];
            } while (!allowPersist && (f->id[0] == 0x056b906e));
            if (allowPersist && i == 0) f->id[0] = 0x056b906e;
            f->id[1] = f->id[2] = f->id[0];
            f->priority = kBandPriorities[rng() % (sizeof(kBandPriorities) / sizeof(kBandPriorities[0]))];
            if (allowPersist && i == 0) f->priority = 5000000;
            orig.AddSystemService(s.fw, f);
        }
        // Init / Shutdown that change the service set while the framework walks its copy.
        std::mt19937 r2(seed * 131u + 5u);
        for (size_t i = 4; i < s.fakes.size(); ++i) {
            int mode = static_cast<int>(r2() % 5);
            size_t other = 4 + r2() % static_cast<size_t>(count);
            for (int slot : {8, 9}) {
                if (mode == 1) s.fakes[i]->action[slot] = [other](Side &sd, Fake &) { sd.ops->RemoveSystemService(sd.fw, sd.fakes[other].get()); };
                if (mode == 2) s.fakes[i]->action[slot] = [i](Side &sd, Fake &) { sd.ops->RemoveSystemService(sd.fw, sd.fakes[i].get()); };
            }
        }
        for (auto &f : s.fakes) f->idCalls = 0;
        s.log.clear();
    }
    gSide = nullptr;
}

} // namespace

// cCTGFramework's bands (the +0x18c manual set initialised but not started, the message server's subscriptions and
// service, the app's registration): seeds 1..600 adapters on original-built objects, 601..1200 our vtable on ours.
DIFF_TEST(FW_Bands, 0xc2d6e6)
{
    CtgTrapScope traps;
    Ops orig = MakeOps(ctx, true), mineA = MakeOps(ctx, false, Route::Adapters), mineC = MakeOps(ctx, false, Route::Ctg);
    Globals globals(ctx);
    for (uint32_t seed = 1; seed <= 2 * 600; ++seed) {
        Route route = seed > 600 ? Route::Ctg : Route::Adapters;
        Ops &mine = route == Route::Ctg ? mineC : mineA;
        Pair p(route);
        BuildBand(ctx, globals, p, orig, seed, 8, false);
        std::mt19937 rng(seed * 61u + 11u);
        for (int step = 0; step < 4; ++step) {
            int which = static_cast<int>(rng() % 9);
            std::string diff = RunBoth(ctx, globals, p, orig, mine, [&](Side &s, Ops &o) -> uint64_t {
                RecordFwSlots(s, false, false, true, false, false, false, false, true, false);
                switch (which) {
                case 0: return o.InitHigh(s.fw);
                case 1: return o.InitAppBand(s.fw);
                case 2: return o.InitLow(s.fw);
                case 3: return o.ShutdownLow(s.fw);
                case 4: return o.ShutdownAppBand(s.fw);
                case 5: return o.ShutdownHigh(s.fw);
                case 6: {
                    uint64_t r = o.InitHigh(s.fw);
                    r = r * 2 + o.InitAppBand(s.fw);
                    r = r * 2 + o.InitLow(s.fw);
                    return r;
                }
                case 7: {
                    uint64_t r = o.ShutdownLow(s.fw);
                    r = r * 2 + o.ShutdownAppBand(s.fw);
                    r = r * 2 + o.ShutdownHigh(s.fw);
                    return r;
                }
                default: {
                    // the services flipped by the start/stop messages, then a full cycle
                    s.fw->mServicesBusy = (which & 1);
                    uint64_t r = o.InitHigh(s.fw);
                    r = r * 2 + o.ShutdownHigh(s.fw);
                    return r;
                }
                }
            });
            FAIL_IF(diff, "seed %u step %d op %d", seed, step, which);
        }
    }
}

// The base's bands (slots 41..46 of gz::cGZFramework, 0x14307f / 0x1425c7 / 0x142f87 / 0x143133 / 0x142662 / 0x142ffb):
// every service started, no manual set and no message server. Seeds 1..300 the base adapters on original-built
// cCTGFramework objects (trap vtables), 301..600 our base vtable on base objects, 601..900 our members by name on our
// cCTGFramework objects.
DIFF_TEST(FW_BaseBands, 0x14307f)
{
    TrapScope traps;
    Ops orig = MakeOps(ctx, true), mineA = MakeOps(ctx, false, Route::Adapters), mineV = MakeOps(ctx, false, Route::Vtable),
        mineC = MakeOps(ctx, false, Route::Ctg);
    Globals globals(ctx);
    for (uint32_t seed = 1; seed <= 3 * 300; ++seed) {
        Route route = RouteOf(seed, 300);
        Ops &mine = route == Route::Ctg ? mineC : route == Route::Vtable ? mineV : mineA;
        Pair p(route);
        BuildBand(ctx, globals, p, orig, seed, 8, true);
        std::mt19937 rng(seed * 67u + 13u);
        for (int step = 0; step < 4; ++step) {
            int which = static_cast<int>(rng() % 9);
            std::string diff = RunBoth(ctx, globals, p, orig, mine, [&](Side &s, Ops &o) -> uint64_t {
                RecordFwSlots(s, false, false, true, false, false, false, false, true, false);
                switch (which) {
                case 0: return o.BaseInitHigh(s.fw);
                case 1: return o.BaseInitAppBand(s.fw);
                case 2: return o.BaseInitLow(s.fw);
                case 3: return o.BaseShutdownLow(s.fw);
                case 4: return o.BaseShutdownAppBand(s.fw);
                case 5: return o.BaseShutdownHigh(s.fw);
                case 6: {
                    uint64_t r = o.BaseInitHigh(s.fw);
                    r = r * 2 + o.BaseInitAppBand(s.fw);
                    r = r * 2 + o.BaseInitLow(s.fw);
                    return r;
                }
                case 7: {
                    uint64_t r = o.BaseShutdownLow(s.fw);
                    r = r * 2 + o.BaseShutdownAppBand(s.fw);
                    r = r * 2 + o.BaseShutdownHigh(s.fw);
                    return r;
                }
                default: {
                    uint64_t r = o.BaseInitHigh(s.fw);
                    r = r * 2 + o.BaseShutdownHigh(s.fw);
                    return r;
                }
                }
            });
            FAIL_IF(diff, "seed %u step %d op %d", seed, step, which);
        }
    }
}

DIFF_TEST(FW_DoMessage, 0xc2d2ca)
{
    CtgTrapScope traps;
    Ops orig = MakeOps(ctx, true), mineA = MakeOps(ctx, false, Route::Adapters), mineC = MakeOps(ctx, false, Route::Ctg);
    Globals globals(ctx);
    const uint32_t ids[] = {0xcd, 0xce, 0xcd, 0xce, 0xcc, 0xcf, 0, 0xffffffff};
    for (uint32_t seed = 1; seed <= 2 * 600; ++seed) {
        Route route = seed > 600 ? Route::Ctg : Route::Adapters;
        Ops &mine = route == Route::Ctg ? mineC : mineA;
        Pair p(route);
        BuildBand(ctx, globals, p, orig, seed, 8, false);
        for (Side *s : {&p.a, &p.b}) {
            Fake *m = NewFake(*s, kMsgVt);
            std::mt19937 r(seed + 3u);
            m->value = ids[r() % (sizeof(ids) / sizeof(ids[0]))];
            if (r() % 6 != 0) m->qi = {{0x65297976, m}};
        }
        std::mt19937 rng(seed * 43u + 1u);
        for (int step = 0; step < 4; ++step) {
            bool nullMessage = rng() % 8 == 0;
            uint32_t newId = ids[rng() % (sizeof(ids) / sizeof(ids[0]))];
            std::string diff = RunBoth(ctx, globals, p, orig, mine, [&](Side &s, Ops &o) -> uint64_t {
                Fake *m = s.fakes.back().get();
                m->value = newId;
                return o.DoMessage(s.fw, nullMessage ? nullptr : m);
            });
            FAIL_IF(diff, "seed %u step %d", seed, step);
        }
    }
}

namespace {

// The CRT-level seam of the persist test: the original's RZMalloc (0x1be3b0, cdecl) is redirected to
// the process heap for the duration, and the message objects' vtable (in the mapped image) to
// recorders, so neither the game's allocator nor the message class's code is involved.
void *__cdecl TestMalloc(uint32_t size)
{
    void *p = HeapAlloc(GetProcessHeap(), 0, size);
    std::memset(p, 0xcd, size);
    return p;
}

uint32_t __fastcall MsgAddRefRec(void *self, void *)
{
    // The bytes the constructor wrote (the padding at +9..+0xb is not written by it).
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < 0x2c; ++i) {
        if (i >= 9 && i <= 0xb) continue;
        uint8_t b = static_cast<uint8_t *>(self)[i];
        if (i < 4) b = 0; // the vtable pointer differs per side's copy: compared separately
        h = (h ^ b) * 16777619u;
    }
    Log(kWhoImport, 10, h);
    return 1;
}
uint32_t __fastcall MsgReleaseRec(void *, void *)
{
    Log(kWhoImport, 12);
    return 0;
}
void __fastcall MsgSetIdRec(void *, void *, uint32_t id)
{
    Log(kWhoImport, 11, id);
}

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

} // namespace

DIFF_TEST(FW_InitHighPersist, 0xc2d6e6)
{
    CtgTrapScope traps;
    Ops orig = MakeOps(ctx, true), mineA = MakeOps(ctx, false, Route::Adapters), mineC = MakeOps(ctx, false, Route::Ctg);
    Globals globals(ctx);
    CodeRedirect malloc(ctx, 0x1be3b0, reinterpret_cast<void *>(&TestMalloc));
    // Ours allocates from a real allocator object (the original's global one is unconstructed here).
    auto *allocator = static_cast<GeneralAllocator *>(::operator new(sizeof(GeneralAllocator)));
    allocator->Construct(nullptr, 0, 0, 0, nullptr, nullptr);
    RZMemory::testGlobalAllocator = allocator;
    // The message class's vtable slots 1, 2 and 5 (AddRef, Release, SetId) record.
    void **vtable = ctx.image.At<void *>(cRZMessage2Standard::kVTable);
    void *savedSlots[3] = {vtable[1], vtable[2], vtable[5]};
    DWORD old;
    VirtualProtect(vtable, 64, PAGE_READWRITE, &old);
    vtable[1] = reinterpret_cast<void *>(&MsgAddRefRec);
    vtable[2] = reinterpret_cast<void *>(&MsgReleaseRec);
    vtable[5] = reinterpret_cast<void *>(&MsgSetIdRec);
    for (uint32_t seed = 1; seed <= 2 * 150; ++seed) {
        Route route = seed > 150 ? Route::Ctg : Route::Adapters;
        Ops &mine = route == Route::Ctg ? mineC : mineA;
        Pair p(route);
        BuildBand(ctx, globals, p, orig, seed, 5, true);
        std::string diff = RunBoth(ctx, globals, p, orig, mine, [&](Side &s, Ops &o) -> uint64_t {
            RecordFwSlots(s, false, false, true, false, false, false, false, true, false);
            if (!s.original) {
                // Fresh pages are zero, so make the memory our allocator hands out dirty (the redirected
                // original allocator fills its blocks with 0xcd): a field the constructor leaves unwritten
                // then shows up in the object's checksum.
                void *blocks[8];
                for (void *&b : blocks) {
                    b = RZMemory::Malloc(cRZMessage2Standard::kSize);
                    std::memset(b, 0xcd, cRZMessage2Standard::kSize);
                }
                for (void *b : blocks) RZMemory::Free(b);
            }
            uint64_t r = o.InitHigh(s.fw);
            Log(kWhoFw, 95, *GameAddress<uint32_t>(0x10f519c) != 0);
            return r;
        });
        if (!diff.empty()) {
            ctx.Fail("seed %u: %s", seed, diff.c_str());
            break;
        }
    }
    vtable[1] = savedSlots[0];
    vtable[2] = savedSlots[1];
    vtable[5] = savedSlots[2];
    RZMemory::testGlobalAllocator = nullptr;
    difftest_alloc::ShutdownAndRelease(allocator);
    ::operator delete(allocator);
}

namespace {

struct HookOwner {
    uint8_t pad[0x40];
    Fake face;
};

LRESULT CALLBACK FwTestWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (gSide) Log(kWhoImport, 20, message, static_cast<uint32_t>(wParam));
    return DefWindowProcA(window, message, wParam, lParam);
}

} // namespace

DIFF_TEST(FW_MainLoop, 0xc2d9c5)
{
    CtgTrapScope traps;
    Ops orig = MakeOps(ctx, true), mineA = MakeOps(ctx, false, Route::Adapters), mineC = MakeOps(ctx, false, Route::Ctg);
    Globals globals(ctx);
    ImportPatch patch(ctx);
    HMODULE user32 = GetModuleHandleA("user32.dll");
    patch.Set(0xd9a574, reinterpret_cast<void *>(GetProcAddress(user32, "WaitMessage")));
    patch.Set(0xd9a578, reinterpret_cast<void *>(GetProcAddress(user32, "PeekMessageA")));
    patch.Set(0xd9a57c, reinterpret_cast<void *>(GetProcAddress(user32, "DispatchMessageA")));
    patch.Set(0xd9a580, reinterpret_cast<void *>(GetProcAddress(user32, "TranslateMessage")));
    DWORD thread = GetCurrentThreadId();
    // A message-only window whose procedure logs what it is sent: messages the application hook lets through
    // are translated (a key-down makes WM_CHAR) and dispatched to it.
    WNDCLASSA windowClass = {};
    windowClass.lpfnWndProc = &FwTestWndProc;
    windowClass.hInstance = GetModuleHandleA(nullptr);
    windowClass.lpszClassName = "FwTestWindow";
    RegisterClassA(&windowClass);
    HWND window = CreateWindowExA(0, "FwTestWindow", "", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, windowClass.hInstance, nullptr);
    for (uint32_t seed = 1; seed <= 2 * 300; ++seed) {
        Route route = seed > 300 ? Route::Ctg : Route::Adapters;
        Ops &mine = route == Route::Ctg ? mineC : mineA;
        Pair p(route);
        Populate(ctx, globals, p, orig, seed, 1, 0, false, false, false);
        std::mt19937 rng(seed * 5u + 1u);
        int messages = static_cast<int>(rng() % 5);
        std::vector<int> kinds;
        for (int i = 0; i < 5; ++i) kinds.push_back(static_cast<int>(rng() % 3));
        // 0: quit posted first; 1: OnIdle posts the quit; 2: tick disabled at first; 3: no application hook object, quit posted first
        int mode = static_cast<int>(rng() % 4);
        uint32_t code = static_cast<uint32_t>(rng() % 200);
        std::vector<uint8_t> answers, tick;
        for (int i = 0; i < messages; ++i) answers.push_back(static_cast<uint8_t>(rng() % 2));
        for (int i = 0; i < 8; ++i) tick.push_back(static_cast<uint8_t>(rng() % 2));
        if (mode == 1) tick[1] = 1; // the idle check after the first pump
        if (messages == 0 && mode != 0) tick[0] = 1;
        if (mode == 2) {
            tick[0] = 0;
            tick[1] = 1;
            if (messages == 0) messages = 1, answers.push_back(0);
        }
        std::string diff = RunBoth(
            ctx, globals, p, orig, mine,
            [&](Side &s, Ops &o) -> uint64_t {
                RecordFwSlots(s, false, false, false, true, true, false, false, false, false);
                DrainMessages();
                gHookAnswers = answers;
                gHookAnswerPos = 0;
                gTickEnabledAnswers = tick;
                gTickEnabledPos = 0;
                auto owner = std::make_unique<HookOwner>();
                owner->face.vtable = kHookFaceVt;
                owner->face.index = 50;
                s.hookOwner = mode == 3 ? nullptr : owner.get();
                *ctx.image.At<uint32_t>(0x10fdfe8) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(s.hookOwner));
                for (int i = 0; i < messages; ++i) {
                    UINT id = WM_APP + static_cast<UINT>(i);
                    if (kinds[static_cast<size_t>(i)] == 0) PostThreadMessageA(thread, id, static_cast<WPARAM>(i + 1), 0);
                    else if (kinds[static_cast<size_t>(i)] == 1) PostMessageA(window, id, static_cast<WPARAM>(i + 1), 0);
                    else PostMessageA(window, WM_KEYDOWN, 'A', 0x001e0001);
                }
                if (mode == 0 || mode == 2 || mode == 3) PostQuitMessage(static_cast<int>(code));
                // mode 1: the first OnIdle (slot 16) posts the quit
                s.fwVt[16] = reinterpret_cast<void *>(&FwOnIdleSlotRec);
                static int idleQuitCode;
                idleQuitCode = static_cast<int>(code);
                struct Poster {
                    static void __fastcall OnIdle(void *, void *)
                    {
                        Log(kWhoFw, 16);
                        PostQuitMessage(idleQuitCode);
                    }
                };
                if (mode == 1) s.fwVt[16] = reinterpret_cast<void *>(&Poster::OnIdle);
                uint32_t result = o.MainLoop(s.fw);
                uint32_t exitCode = *ctx.image.At<uint32_t>(0x10c9384);
                DrainMessages();
                s.hookOwner = nullptr;
                return static_cast<uint64_t>(result) | (static_cast<uint64_t>(exitCode) << 32);
            },
            nullptr);
        if (!diff.empty()) DestroyWindow(window);
        FAIL_IF(diff, "seed %u (messages %d, mode %d)", seed, messages, mode);
    }
    DrainMessages();
    DestroyWindow(window);
}

namespace {

// The scripted message pump (FW_MainLoopScripted): WaitMessage, PeekMessageA, TranslateMessage and DispatchMessageA
// through the import table on both sides, so the queue is a script rather than the thread's real one. Each PeekMessageA
// call takes the next entry: 0 an empty queue, otherwise a message id (WM_QUIT among them); past the end, WM_QUIT with
// gQuitWParam (so every loop ends).
std::vector<uint32_t> gPeekScript;
size_t gPeekPos = 0;
uint32_t gQuitWParam = 0;
BOOL WINAPI PeekRec(MSG *msg, HWND window, UINT first, UINT last, UINT remove)
{
    Log(kWhoImport, 40, window == nullptr, first | (last << 16), remove);
    uint32_t id = gPeekPos < gPeekScript.size() ? gPeekScript[gPeekPos] : WM_QUIT;
    uint32_t wParam = gPeekPos < gPeekScript.size() ? static_cast<uint32_t>(gPeekPos) * 3 + 1 : gQuitWParam;
    ++gPeekPos;
    if (!id) return FALSE;
    std::memset(msg, 0, sizeof(*msg));
    msg->message = id;
    msg->wParam = wParam;
    msg->lParam = static_cast<LPARAM>(id * 7);
    return TRUE;
}
BOOL WINAPI WaitRec()
{
    Log(kWhoImport, 41);
    return TRUE;
}
BOOL WINAPI TranslateRec(const MSG *msg)
{
    Log(kWhoImport, 42, msg->message, static_cast<uint32_t>(msg->wParam), static_cast<uint32_t>(msg->lParam));
    return TRUE;
}
LRESULT WINAPI DispatchRec(const MSG *msg)
{
    Log(kWhoImport, 43, msg->message, static_cast<uint32_t>(msg->wParam), static_cast<uint32_t>(msg->lParam));
    return 0;
}

// OnIdle (slot 16): logs, then applies the next idle step to the framework's fields: the tick byte (the base loop reads
// it directly), the deferred quit, the pending quit and its code (the base loop ends on both flags).
struct IdleStep {
    uint8_t tick, deferQuit, quitPending;
    uint32_t code;
};
std::vector<IdleStep> gIdleScript;
size_t gIdlePos = 0;
void __fastcall ScriptedOnIdle(void *, void *)
{
    Log(kWhoFw, 16);
    if (gIdlePos < gIdleScript.size() && gSide) {
        const IdleStep &step = gIdleScript[gIdlePos++];
        gSide->fw->mTickEnabled = step.tick;
        gSide->fw->mDeferQuit = step.deferQuit;
        gSide->fw->mQuitPending = step.quitPending;
        gSide->fw->mQuitCode = step.code;
    }
}

} // namespace

// Both main loops on a scripted queue: the base's (slot 48, 0x1435c2: the tick byte read directly, no message hook, ends
// on a deferred quit as well as WM_QUIT) and cCTGFramework's (0xc2d9c5: the tick flag through slot 17, the application's
// message hook for every message and once per empty queue). Variants by seed: base on the adapter route (trap vtables),
// on the base vtable route and by name on our cCTGFramework; cCTGFramework's on the adapter route and on our vtable.
DIFF_TEST(FW_MainLoopScripted, 0x1435c2)
{
    TrapScope traps;
    Ops orig = MakeOps(ctx, true), mineA = MakeOps(ctx, false, Route::Adapters), mineV = MakeOps(ctx, false, Route::Vtable),
        mineC = MakeOps(ctx, false, Route::Ctg);
    Globals globals(ctx);
    ImportPatch patch(ctx);
    patch.Set(0xd9a574, reinterpret_cast<void *>(&WaitRec));
    patch.Set(0xd9a578, reinterpret_cast<void *>(&PeekRec));
    patch.Set(0xd9a57c, reinterpret_cast<void *>(&DispatchRec));
    patch.Set(0xd9a580, reinterpret_cast<void *>(&TranslateRec));
    const uint32_t messageIds[] = {0, 0, 0, WM_APP, WM_APP + 1, WM_KEYDOWN, WM_CHAR, WM_TIMER, WM_PAINT, WM_QUIT};
    for (uint32_t seed = 1; seed <= 5 * 200; ++seed) {
        int variant = static_cast<int>((seed - 1) / 200); // 0..2 the base's loop, 3..4 cCTGFramework's
        bool base = variant < 3;
        Route route = variant == 0 || variant == 3 ? Route::Adapters : variant == 1 ? Route::Vtable : Route::Ctg;
        Ops &mine = route == Route::Ctg ? mineC : route == Route::Vtable ? mineV : mineA;
        Pair p(route);
        Populate(ctx, globals, p, orig, seed, 1, 0, false, false, false);
        std::mt19937 rng(seed * 101u + 7u);
        std::vector<uint32_t> peeks;
        size_t peekCount = rng() % 12;
        for (size_t i = 0; i < peekCount; ++i) {
            uint32_t id = messageIds[rng() % (sizeof(messageIds) / sizeof(messageIds[0]))];
            if (id == WM_QUIT && rng() % 2) id = 0; // keep quits rarer
            peeks.push_back(id);
        }
        std::vector<IdleStep> idle;
        for (int i = 0; i < 6; ++i) {
            uint32_t bits = static_cast<uint32_t>(rng());
            idle.push_back({static_cast<uint8_t>(bits & 1 ? (bits & 2 ? 1 : 0x80) : 0), static_cast<uint8_t>((bits >> 2) % 3 == 0),
                            static_cast<uint8_t>((bits >> 4) % 3 == 0), static_cast<uint32_t>(rng() % 1000)});
        }
        std::vector<uint8_t> answers, tick;
        for (int i = 0; i < 16; ++i) answers.push_back(static_cast<uint8_t>(rng() % 3 == 0));
        for (int i = 0; i < 8; ++i) tick.push_back(static_cast<uint8_t>(rng() % 3 != 0));
        uint8_t tickByte = static_cast<uint8_t>(rng() % 3 == 0 ? 0 : 1);
        uint8_t deferQuit = static_cast<uint8_t>(rng() % 2), quitPending = static_cast<uint8_t>(rng() % 2);
        uint32_t quitCode = static_cast<uint32_t>(rng() % 500), quitWParam = static_cast<uint32_t>(rng() % 300);
        std::string diff = RunBoth(ctx, globals, p, orig, mine, [&](Side &s, Ops &o) -> uint64_t {
            RecordFwSlots(s, false, false, false, true, false, false, false, false, false);
            s.fwVt[16] = reinterpret_cast<void *>(&ScriptedOnIdle);
            gPeekScript = peeks;
            gPeekPos = 0;
            gQuitWParam = quitWParam;
            gIdleScript = idle;
            gIdlePos = 0;
            gHookAnswers = answers;
            gHookAnswerPos = 0;
            gTickEnabledAnswers = tick;
            gTickEnabledPos = 0;
            s.fw->mTickEnabled = tickByte;
            s.fw->mDeferQuit = deferQuit;
            s.fw->mQuitPending = quitPending;
            s.fw->mQuitCode = quitCode;
            auto owner = std::make_unique<HookOwner>();
            owner->face.vtable = kHookFaceVt;
            owner->face.index = 50;
            s.hookOwner = owner.get();
            *ctx.image.At<uint32_t>(0x10fdfe8) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(s.hookOwner));
            uint32_t result = base ? o.BaseMainLoop(s.fw) : o.MainLoop(s.fw);
            uint32_t exitCode = *ctx.image.At<uint32_t>(0x10c9384);
            Log(kWhoFw, 93, static_cast<uint32_t>(gPeekPos), static_cast<uint32_t>(gIdlePos), static_cast<uint32_t>(gTickEnabledPos));
            s.hookOwner = nullptr;
            return static_cast<uint64_t>(result) | (static_cast<uint64_t>(exitCode) << 32);
        });
        FAIL_IF(diff, "seed %u (variant %d, %zu peeks)", seed, variant, peeks.size());
    }
}

DIFF_TEST(FW_RunShutdown, 0x1446ba)
{
    CtgTrapScope traps;
    Ops orig = MakeOps(ctx, true), mineA = MakeOps(ctx, false, Route::Adapters), mineC = MakeOps(ctx, false, Route::Ctg);
    Globals globals(ctx);
    for (uint32_t seed = 1; seed <= 2 * 400; ++seed) {
        Route route = seed > 400 ? Route::Ctg : Route::Adapters;
        Ops &mine = route == Route::Ctg ? mineC : mineA;
        Pair p(route);
        BuildBand(ctx, globals, p, orig, seed, 5, false);
        for (Side *s : {&p.a, &p.b}) {
            gSide = s;
            globals.Point(*s);
            std::mt19937 r(seed + 17u);
            for (int i = 0; i < 3; ++i) {
                Fake *h = NewHook(*s);
                if (r() % 4 != 0) orig.AddHook(s->fw, h);
            }
            s->log.clear();
        }
        gSide = nullptr;
        std::mt19937 rng(seed * 11u + 3u);
        uint32_t exitCode = rng() % 3 == 0 ? 5u : 0u;
        bool gzRun = rng() % 2 != 0;
        int which = static_cast<int>(rng() % 4);
        std::string diff = RunBoth(ctx, globals, p, orig, mine, [&](Side &s, Ops &o) -> uint64_t {
            RecordFwSlots(s, false, false, false, false, false, true, false, true, true);
            s.fwVt[2] = reinterpret_cast<void *>(&FwReleaseRec);
            s.app->value = gzRun;
            *ctx.image.At<uint32_t>(0x10c9384) = exitCode;
            uint64_t r = 0;
            if (which == 3) {
                // just the globals' release: g_App, g_Framework, g_Framework2, each released and cleared
                s.fwVt[2] = reinterpret_cast<void *>(&FwReleaseRec);
                if (exitCode == 5) *ctx.image.At<uint32_t>(0x10c937c) = 0; // the second reference absent
                o.ReleaseGlobals();
                return (*ctx.image.At<uint32_t>(0x10c9378) != 0) + 2 * (*ctx.image.At<uint32_t>(0x10c937c) != 0) + 4 * (*ctx.image.At<uint32_t>(0x10c9380) != 0);
            }
            if (which == 0 || which == 2) r = o.Run();
            // the state field after Run, then a full shutdown
            r = r * 16 + s.fw->mState;
            if (which == 1 || which == 2) {
                uint32_t state = s.fw->mState;
                bool ok = o.Shutdown();
                Log(kWhoFw, 94, state, ok, (*ctx.image.At<uint32_t>(0x10c9378) != 0) + 2 * (*ctx.image.At<uint32_t>(0x10c937c) != 0) + 4 * (*ctx.image.At<uint32_t>(0x10c9380) != 0));
                r = r * 2 + ok;
            }
            return r;
        });
        FAIL_IF(diff, "seed %u op %d", seed, which);
    }
}

DIFF_TEST(FW_ShutdownCOMObject, 0x1437c5)
{
    TrapScope traps;
    Ops orig = MakeOps(ctx, true), mineA = MakeOps(ctx, false, Route::Adapters), mineV = MakeOps(ctx, false, Route::Vtable),
        mineC = MakeOps(ctx, false, Route::Ctg);
    Globals globals(ctx);
    for (uint32_t seed = 1; seed <= 3 * 20; ++seed) {
        Route route = RouteOf(seed, 20);
        Ops &mine = route == Route::Ctg ? mineC : route == Route::Vtable ? mineV : mineA;
        Pair p(route);
        Populate(ctx, globals, p, orig, seed, 1, 0, false, false, false);
        std::string diff = RunBoth(ctx, globals, p, orig, mine, [&](Side &s, Ops &o) -> uint64_t {
            o.ShutdownCOMObject(s.fw);
            return 0;
        });
        FAIL_IF(diff, "seed %u", seed);
    }
}

namespace {

// AbortiveQuit's exits: OutputDebugStringA (through the import table on both sides) and exit, which the original reaches
// at 0x82f6bf (the game CRT's exit, redirected here) and ours through cGZFramework::sExit. exit never returns: the
// recorder jumps back to the case.
std::jmp_buf gExitJump;
void WINAPI DebugStringRec(const char *text)
{
    Log(kWhoImport, 5, Fnv(text));
}
void __cdecl ExitRec(int code)
{
    Log(kWhoImport, 6, static_cast<uint32_t>(code));
    std::longjmp(gExitJump, 1);
}

struct ExitSeam {
    void(__cdecl *saved)(int) = Shape::sExit;
    ExitSeam() { Shape::sExit = &ExitRec; }
    ~ExitSeam() { Shape::sExit = saved; }
};

} // namespace

// 0x142270: OutputDebugStringA, the AbortiveQuit hooks (slot 9, in order; none when the hook list is busy), a second
// OutputDebugStringA, then exit(code). Hooks that remove themselves or run the runner again are included.
DIFF_TEST(FW_AbortiveQuit, 0x142270)
{
    TrapScope traps;
    Ops orig = MakeOps(ctx, true), mineA = MakeOps(ctx, false, Route::Adapters), mineV = MakeOps(ctx, false, Route::Vtable),
        mineC = MakeOps(ctx, false, Route::Ctg);
    Globals globals(ctx);
    ImportPatch patch(ctx);
    patch.Set(0xd9a110, reinterpret_cast<void *>(&DebugStringRec));
    CodeRedirect exitRedirect(ctx, 0x82f6bf, reinterpret_cast<void *>(&ExitRec));
    ExitSeam seam;
    for (uint32_t seed = 1; seed <= 3 * 150; ++seed) {
        Route route = RouteOf(seed, 150);
        Ops &mine = route == Route::Ctg ? mineC : route == Route::Vtable ? mineV : mineA;
        Pair p(route);
        Populate(ctx, globals, p, orig, seed, 0, 5, false, false, true);
        for (Side *s : {&p.a, &p.b}) {
            std::mt19937 r(seed * 61u + 7u);
            for (size_t i = 0; i < s->fakes.size(); ++i) {
                int mode = static_cast<int>(r() % 4);
                if (mode == 1) s->fakes[i]->action[9] = [i](Side &sd, Fake &) { sd.ops->RemoveHook(sd.fw, sd.fakes[i].get()); };
                if (mode == 2) s->fakes[i]->action[9] = [](Side &sd, Fake &) {
                    if (gDepth < 1) {
                        ++gDepth;
                        sd.ops->RunHooks[6](sd.fw);
                        --gDepth;
                    }
                };
            }
        }
        std::mt19937 rng(seed * 43u + 11u);
        uint32_t code = Interesting(rng);
        bool busy = rng() % 6 == 0;
        std::string diff = RunBoth(ctx, globals, p, orig, mine, [&](Side &s, Ops &o) -> uint64_t {
            s.fw->mHooksBusy = busy;
            if (setjmp(gExitJump) == 0) {
                o.AbortiveQuit(s.fw, code);
                return 0xbad; // exit returned
            }
            gDepth = 0;
            return 1;
        });
        FAIL_IF(diff, "seed %u", seed);
    }
}
