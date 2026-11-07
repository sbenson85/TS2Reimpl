// Differential tests for src/gz/FrameworkStartup.*: the command line's assignment (0x1420d2), the
// program-path step (0x1aeb57) and GZFrameWork_Startup (0x144514).
//
// Assignment and parser tests build two identical command lines from the same recipe and run the
// original on one and ours on the other, then compare a full description (reference count,
// argument count and capacity, every argument and the two strings with their capacities).
// The Startup test builds two cCTGFramework objects (side A from the original constructor; side B from the original
// one or ours, gz::cCTGFramework), gives each a copy of its vtable whose called slots record (so the order of every
// call and the framework's state at the time is pinned), a fake application, a fake COM object, a fake system service
// and a fake hook, and compares the log, the return value, g_ExitCode and the framework's command line. The band
// slots 41..43 are recorders, or run for real on registered fake services (band membership by priority, the +0x18c
// set initialised but not started, the application's failed Init aborting): on side B through the hooks' adapters on
// an original-built object, or through our vtable on ours.
#define NOMINMAX
#include "difftest.h"
#include "game/game.h"
#include "gz/FrameworkStartup.h"
#include "gz/FrameworkShape.h"
#include "rz/StopwatchMore.h"
#include "rz/cRZCmdLine.h"

#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kAssign = 0x1420d2, kParse = 0x1aeb57, kStartup = 0x144514;
constexpr uint32_t kIatGetModuleFileNameA = 0xd9a16c;
constexpr uint32_t kModuleFileNameWResolved = 0x10caeb5, kModuleFileNameWPointer = 0x10caeb8;

std::vector<std::string> gLog;

void Log(const char *fmt, ...)
{
    char buf[256];
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
        std::string x = a.size() > 300 ? a.substr(0, 300) : a;
        std::string y = b.size() > 300 ? b.substr(0, 300) : b;
        for (char &c : x) if (c == '\n') c = '|';
        for (char &c : y) if (c == '\n') c = '|';
        ctx.Fail("%s:\n  original: %s\n  ours:     %s", what, x.c_str(), y.c_str());
    }
}

// ---- command lines ----

cRZCmdLine *NewCmd(const char *text)
{
    auto *cmd = static_cast<cRZCmdLine *>(::operator new(sizeof(cRZCmdLine)));
    std::memset(cmd, 0xCD, sizeof(cRZCmdLine));
    cmd->Construct(text);
    return cmd;
}

void FreeCmd(cRZCmdLine *cmd)
{
    cmd->Destruct();
    ::operator delete(cmd);
}

std::string Str(const std::string &s)
{
    return "[" + s + "]" + std::to_string(s.size()) + "/" + std::to_string(s.capacity());
}

std::string Describe(const cRZCmdLine *cmd)
{
    std::string out = "ref=" + std::to_string(cmd->mRefCount) + " n=" + std::to_string(cmd->mArgs.Count()) +
                      " cap=" + std::to_string(cmd->mArgs.mEnd - cmd->mArgs.mFirst) + " args:";
    for (const cRZString *a = cmd->mArgs.mFirst; a != cmd->mArgs.mLast; ++a) out += " " + Str(a->mString);
    out += " text=" + Str(cmd->mText.mString) + " empty=" + Str(cmd->mEmpty.mString);
    return out;
}

const char *kWords[] = {"-w", "/nosound", "Sims2EP9.exe", "C:\\Games\\The", "-lang:en", "\"quoted arg\"", "a", "longer_argument_to_force_heap_strings_in_every_element", "x.y.z"};

std::string RandomText(std::mt19937 &rng, int maxWords)
{
    std::string text;
    int n = static_cast<int>(rng() % (maxWords + 1));
    for (int i = 0; i < n; ++i) {
        if (i) text += ' ';
        text += kWords[rng() % (sizeof(kWords) / sizeof(kWords[0]))];
    }
    return text;
}

// ---- 0x1420d2 ----

} // namespace

DIFF_TEST(FrameworkStartup_AssignCmdLine, kAssign)
{
    std::mt19937 rng(0x1420d2);
    for (int i = 0; i < 1500; ++i) {
        std::string srcText = RandomText(rng, i % 12);
        std::string dstText = RandomText(rng, static_cast<int>(rng() % 12));
        uint32_t srcRef = rng(), dstRef = rng();
        int erase = static_cast<int>(rng() % 4); // shrink dst's size without losing capacity
        bool junk = rng() & 1;
        bool self = (i % 17) == 0;
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            cRZCmdLine *src = NewCmd(srcText.c_str());
            cRZCmdLine *dst = NewCmd(dstText.c_str());
            src->mRefCount = srcRef;
            dst->mRefCount = dstRef;
            for (int e = 0; e < erase && dst->ArgC() > 0; ++e) dst->EraseArgument(0);
            if (junk) {
                src->mEmpty.mString.assign("an empty string that is not empty and long enough for the heap");
                dst->mEmpty.mString.assign("zz");
            }
            cRZCmdLine *target = self ? src : dst;
            cRZCmdLine *returned;
            if (side == 0) returned = ctx.Original<cRZCmdLine *(__thiscall *)(cRZCmdLine *, const cRZCmdLine *)>(kAssign)(target, src);
            else returned = fw_startup::AssignCmdLine(target, nullptr, src);
            results[side] = std::string(returned == target ? "this " : "other ") + Describe(src) + " | " + Describe(dst);
            FreeCmd(src);
            FreeCmd(dst);
        }
        ExpectSame(ctx, "assign", results[0], results[1]);
    }
}

namespace {

// ---- 0x1aeb57 ----

std::wstring gWide;
std::string gNarrow;

// Like the API: a name longer than the buffer is cut to fit (and terminated).
DWORD WINAPI FakeWide(void *, wchar_t *buffer, DWORD size)
{
    Log("GetModuleFileNameW");
    size_t n = std::min<size_t>(gWide.size(), size - 1);
    std::wmemcpy(buffer, gWide.c_str(), n);
    buffer[n] = 0;
    return static_cast<DWORD>(n);
}
DWORD WINAPI FakeNarrow(void *, char *buffer, DWORD size)
{
    Log("GetModuleFileNameA");
    size_t n = std::min<size_t>(gNarrow.size(), size - 1);
    std::memcpy(buffer, gNarrow.c_str(), n);
    buffer[n] = 0;
    return static_cast<DWORD>(n);
}

// A cIGZString whose length and data slots record and can disagree.
struct FakeString {
    const void *vtable;
    const char *data;
    uint32_t length;
};
void *gFakeStringVt[34];
const char *__fastcall FakeStringData(FakeString *self, void *)
{
    Log("string.Data");
    return self->data;
}
uint32_t __fastcall FakeStringLength(FakeString *self, void *)
{
    Log("string.Strlen");
    return self->length;
}
int __fastcall FakeStringTrap(void *, void *)
{
    Log("string: unexpected slot");
    return 0;
}

struct ModulePathFakes {
    difftest::Context &ctx;
    uint8_t savedResolved;
    void *savedCached, *savedNarrow;
    explicit ModulePathFakes(difftest::Context &c) : ctx(c)
    {
        savedResolved = *ctx.image.At<uint8_t>(kModuleFileNameWResolved);
        savedCached = *ctx.image.At<void *>(kModuleFileNameWPointer);
        savedNarrow = *ctx.image.At<void *>(kIatGetModuleFileNameA);
        *ctx.image.At<uint8_t>(kModuleFileNameWResolved) = 1;
        *ctx.image.At<void *>(kModuleFileNameWPointer) = reinterpret_cast<void *>(&FakeWide);
        *ctx.image.At<void *>(kIatGetModuleFileNameA) = reinterpret_cast<void *>(&FakeNarrow);
    }
    ~ModulePathFakes()
    {
        *ctx.image.At<uint8_t>(kModuleFileNameWResolved) = savedResolved;
        *ctx.image.At<void *>(kModuleFileNameWPointer) = savedCached;
        *ctx.image.At<void *>(kIatGetModuleFileNameA) = savedNarrow;
    }
};

std::wstring Widen(const std::string &s)
{
    return std::wstring(s.begin(), s.end());
}

std::string RandomName(std::mt19937 &rng)
{
    static const char alphabet[] = "abcXYZ019 .\\/:_-";
    std::string s;
    int n = static_cast<int>(rng() % 22);
    for (int i = 0; i < n; ++i) s += alphabet[rng() % (sizeof(alphabet) - 1)];
    return s;
}

} // namespace

DIFF_TEST(FrameworkStartup_ParseCommandLine, kParse)
{
    ModulePathFakes fakes(ctx);
    for (auto &slot : gFakeStringVt) slot = reinterpret_cast<void *>(&FakeStringTrap);
    gFakeStringVt[6] = reinterpret_cast<void *>(&FakeStringData);
    gFakeStringVt[7] = reinterpret_cast<void *>(&FakeStringLength);

    std::mt19937 rng(0x1aeb57);
    static const char *const kPaths[] = {"C:\\Games\\The Sims 2\\TSBin\\Sims2EP9.exe", "d:Sims2EP9.exe", "/usr/bin/sims2ep9", "Sims2EP9", "C:\\a.b\\c.d.e", "noextension", "trailing.", ".hidden", "C:\\dir\\", ""};
    static const char *const kFirst[] = {"sims2ep9", "SIMS2EP9.EXE", "x/sims2ep9.bin", "Sims2EP9.exe", "other.exe", "c.d", "c", "C:\\a.b\\c.d.e", "noextension", "trailing", "hidden", "a", ""};
    for (int i = 0; i < 4000; ++i) {
        std::string path = (i % 3 == 0) ? RandomName(rng) : kPaths[rng() % (sizeof(kPaths) / sizeof(kPaths[0]))];
        if (i % 29 == 0) path = std::string(300, 'q') + "\\" + std::string(150, 'r') + ".ext"; // long, still under 512
        std::string first = (i % 4 == 0) ? RandomName(rng) : kFirst[rng() % (sizeof(kFirst) / sizeof(kFirst[0]))];
        std::string rest = RandomText(rng, static_cast<int>(rng() % 4));
        int mode = static_cast<int>(rng() % 5); // 0,1: module path; 2: real string; 3: fake string with a shorter length; 4: empty string
        int argcKind = static_cast<int>(rng() % 6); // 0: no arguments, otherwise the first is `first`
        std::string cmdText = argcKind == 0 ? "" : (first.find(' ') == std::string::npos && !first.empty() ? first : std::string("zz")) + " " + rest;
        bool emptyFirst = argcKind != 0 && first.empty();
        gWide = Widen(mode <= 1 ? path : "");
        gNarrow = (mode <= 1 && (i % 7 == 0)) ? "N:\\narrow\\path.exe" : "";
        size_t shorten = (path.size() > 3 && rng() % 3 == 0) ? 3 : 0;
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            cRZCmdLine *cmd = NewCmd(cmdText.c_str());
            if (emptyFirst) {
                cRZString empty;
                empty.Construct();
                cmd->InsertArgument(*empty.AsInterface(), 0); // an empty first argument, which the text parser never makes
                empty.Destruct();
            }
            cRZString text;
            text.Construct(path.c_str());
            FakeString fake{gFakeStringVt, path.c_str(), static_cast<uint32_t>(path.size() - shorten)};
            cIGZString *arg = nullptr;
            if (mode == 2) arg = text.AsInterface();
            else if (mode == 3) arg = reinterpret_cast<cIGZString *>(&fake);
            else if (mode == 4) {
                text.mString.clear();
                arg = text.AsInterface();
            }
            if (side == 0) ctx.Original<void(__cdecl *)(cIGZCmdLine *, cIGZString *)>(kParse)(reinterpret_cast<cIGZCmdLine *>(cmd), arg);
            else fw_startup::ParseCommandLine(reinterpret_cast<cIGZCmdLine *>(cmd), arg);
            results[side] = Describe(cmd) + "\n" + Joined();
            text.Destruct();
            FreeCmd(cmd);
        }
        ExpectSame(ctx, "parse", results[0], results[1]);
    }
}

// ---- 0x144514 ----

namespace {

using Fw = cCTGFramework;

constexpr int32_t kKeep = 0x7fffffff;

struct Script {
    bool realInit, queryService, appServices, preInit, appBand;
    int32_t exitAtStart;
    int32_t exitAfterLow;  // g_ExitCode set by InitLowPriorityServices (or kKeep)
    int32_t exitAfterPost; // set by PostFrameworkInit (or kKeep)
    uint8_t ignored12, ignored11, ignored41, ignored43; // return values of the calls whose result is not used
};
Script gScript;
Fw *gFw;

int32_t &ExitCode()
{
    return *GameAddress<int32_t>(cGZFramework::kGlobalExitCode);
}

void Mark(const char *what, uint32_t arg = 0)
{
    Log("%s(%x) state=%u exit=%d tick=%u busy=%u", what, arg, gFw->mState, ExitCode(), gFw->mTickEnabled, gFw->mHooksBusy);
}

struct Obj {
    const void *vtable;
    int refs;
};
Obj gService, gHook, gApp, gCom;
void *gServiceVt[8], *gHookVt[8], *gAppVt[24], *gComVt[16];
void *gFwVt[64], *gLockVt[8];

uint32_t __fastcall SvcAddRef(Obj *self, void *) { ++self->refs; Mark("svc.AddRef"); return self->refs; }
uint32_t __fastcall SvcRelease(Obj *self, void *) { --self->refs; Mark("svc.Release"); return self->refs; }
uint32_t __fastcall HookAddRef(Obj *self, void *) { ++self->refs; return self->refs; }
uint32_t __fastcall HookRelease(Obj *self, void *) { --self->refs; return self->refs; }
void __fastcall HookPreFrameworkInit(Obj *, void *) { Mark("hook.PreFrameWorkInit"); }
void __fastcall HookPreAppInit(Obj *, void *) { Mark("hook.PreAppInit"); }
void __fastcall HookPostAppInit(Obj *, void *) { Mark("hook.PostAppInit"); }

uint8_t __fastcall ComQuery(Obj *, void *, uint32_t iid, void **out)
{
    Mark("com.QueryInterface", iid);
    if (gScript.queryService) *out = &gService;
    return gScript.queryService;
}
uint8_t __fastcall ComRealInit(Obj *, void *) { Mark("com.RealInit"); return gScript.realInit; }
uint8_t __fastcall ComRealShutdown(Obj *, void *) { Mark("com.RealShutdown"); return 1; }
uint32_t __fastcall ComSetRunning(Obj *, void *, uint32_t flag) { Mark("com.SetServiceRunning", flag); return 7; }

uint8_t __fastcall AppPreInit(Obj *, void *) { Mark("app.PreFrameworkInit"); return gScript.preInit; }
void __fastcall AppPostInit(Obj *, void *)
{
    Mark("app.PostFrameworkInit");
    if (gScript.exitAfterPost != kKeep) ExitCode() = gScript.exitAfterPost;
}
uint8_t __fastcall AppDynamicLibs(Obj *, void *) { Mark("app.AddDynamicLibrariesHere"); return gScript.ignored11; }
uint8_t __fastcall AppDirectors(Obj *, void *) { Mark("app.AddCOMDirectorsHere"); return gScript.ignored12; }
uint8_t __fastcall AppServices(Obj *, void *) { Mark("app.AddApplicationServicesHere"); return gScript.appServices; }

uint8_t __fastcall FwAddService(Fw *, void *, void *service)
{
    Mark("fw.AddSystemService", service == &gService ? 1u : 0u);
    return 1;
}
void __fastcall FwToggle(Fw *, void *, uint32_t flag) { Mark("fw.ToggleTick", flag); }
void *__fastcall FwGetCom(Fw *, void *) { Mark("fw.GetCOMObject"); return &gCom; }
uint8_t __fastcall FwInitHigh(Fw *, void *) { Mark("fw.InitHighPriorityServices"); return gScript.ignored41; }
uint8_t __fastcall FwInitApp(Fw *, void *) { Mark("fw.InitAppBandServices"); return gScript.appBand; }
uint8_t __fastcall FwInitLow(Fw *, void *)
{
    Mark("fw.InitLowPriorityServices");
    if (gScript.exitAfterLow != kKeep) ExitCode() = gScript.exitAfterLow;
    return gScript.ignored43;
}
int32_t __fastcall LockRec(void *, void *) { Mark("lock"); return 1; }
int32_t __fastcall UnlockRec(void *, void *) { Mark("unlock"); return 1; }
int __fastcall Trap(void *, void *)
{
    Log("TRAP: unexpected call");
    return 0;
}

// The real bands (BandMode::Real): the framework's own slots 41..43 run on registered system services. Each service
// records what the bands ask it (priority, Init, id, SetServiceRunning) with its index; their ids include ones in the
// framework's +0x18c set (initialised, not started) and their priorities sit on and around the band boundaries. One of
// them may be the application (it answers interface 0x66), whose failed Init aborts the app band. The application hands
// out its system-service face, the message server (found through slot 5, GetSystemService) takes the subscriptions.
struct Svc {
    const void *vtable;
    int refs;
    int index;
    uint32_t id;
    int32_t priority;
    uint8_t initOk, running, isApp;
};
constexpr int kSvcCount = 8;
Svc gSvcs[kSvcCount], gAppSvc, gServerSvc;
Obj gServer;
void *gSvcVt[16], *gServerVt[8];

void SvcMark(Svc *s, const char *what, uint32_t arg = 0)
{
    char name[48];
    std::snprintf(name, sizeof(name), "svc%d.%s", s->index, what);
    Mark(name, arg);
}
uint8_t __fastcall BandQuery(Svc *s, void *, uint32_t iid, void **out)
{
    SvcMark(s, "QueryInterface", iid);
    if (iid == 0x66 && s->isApp) {
        *out = &gApp;
        ++gApp.refs;
        return 1;
    }
    return 0;
}
uint32_t __fastcall BandAddRef(Svc *s, void *) { return ++s->refs; }
uint32_t __fastcall BandRelease(Svc *s, void *) { return --s->refs; }
uint32_t __fastcall BandGetId(Svc *s, void *) { SvcMark(s, "GetServiceID"); return s->id; }
int32_t __fastcall BandPriority(Svc *s, void *) { SvcMark(s, "GetServicePriority"); return s->priority; }
uint8_t __fastcall BandIsRunning(Svc *s, void *) { SvcMark(s, "IsServiceRunning"); return s->running; }
void __fastcall BandSetRunning(Svc *s, void *, uint32_t flag) { SvcMark(s, "SetServiceRunning", flag); s->running = static_cast<uint8_t>(flag); }
uint8_t __fastcall BandInit(Svc *s, void *) { SvcMark(s, "Init"); return s->initOk; }
uint8_t __fastcall BandShutdown(Svc *s, void *) { SvcMark(s, "Shutdown"); return 1; }

uint8_t __fastcall AppQuery(Obj *, void *, uint32_t iid, void **out)
{
    Mark("app.QueryInterface", iid);
    if (iid != 0x287fb697) return 0;
    *out = &gAppSvc;
    ++gAppSvc.refs;
    return 1;
}
uint8_t __fastcall ServerQuery(Obj *, void *, uint32_t iid, void **out)
{
    Mark("server.QueryInterface", iid);
    if (iid != 0x287fb697) return 0;
    *out = &gServerSvc;
    ++gServerSvc.refs;
    return 1;
}
uint32_t __fastcall ServerAddRef(Obj *self, void *) { return ++self->refs; }
uint32_t __fastcall ServerRelease(Obj *self, void *) { return --self->refs; }
uint32_t FwOffset(uint32_t pointer)
{
    return pointer - static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gFw));
}
void __fastcall ServerSubscribe(Obj *, void *, uint32_t target, uint32_t id) { Mark("server.Subscribe", (FwOffset(target) << 16) ^ id); }
void __fastcall ServerUnsubscribe(Obj *, void *, uint32_t target, uint32_t id) { Mark("server.Unsubscribe", (FwOffset(target) << 16) ^ id); }
void __fastcall ServerSend(Obj *, void *, void *) { Mark("server.Send"); }

uint8_t __fastcall FwGetService(Fw *, void *, uint32_t id, uint32_t iid, void **out)
{
    Mark("fw.GetSystemService", id ^ (iid << 1));
    if (id != 0x04fa845b) return 0;
    *out = &gServer;
    ++gServer.refs;
    return 1;
}
void *__fastcall FwApplication(Fw *, void *) { Mark("fw.Application"); return &gApp; }

void WireBandObjects(std::mt19937 &rng)
{
    for (auto &s : gSvcVt) s = reinterpret_cast<void *>(&Trap);
    gSvcVt[0] = reinterpret_cast<void *>(&BandQuery);
    gSvcVt[1] = reinterpret_cast<void *>(&BandAddRef);
    gSvcVt[2] = reinterpret_cast<void *>(&BandRelease);
    gSvcVt[3] = reinterpret_cast<void *>(&BandGetId);
    gSvcVt[5] = reinterpret_cast<void *>(&BandPriority);
    gSvcVt[6] = reinterpret_cast<void *>(&BandIsRunning);
    gSvcVt[7] = reinterpret_cast<void *>(&BandSetRunning);
    gSvcVt[8] = reinterpret_cast<void *>(&BandInit);
    gSvcVt[9] = reinterpret_cast<void *>(&BandShutdown);
    for (auto &s : gServerVt) s = reinterpret_cast<void *>(&Trap);
    gServerVt[0] = reinterpret_cast<void *>(&ServerQuery);
    gServerVt[1] = reinterpret_cast<void *>(&ServerAddRef);
    gServerVt[2] = reinterpret_cast<void *>(&ServerRelease);
    gServerVt[3] = reinterpret_cast<void *>(&ServerSend);
    gServerVt[5] = reinterpret_cast<void *>(&ServerSubscribe);
    gServerVt[6] = reinterpret_cast<void *>(&ServerUnsubscribe);
    gServer = {gServerVt, 1};
    gAppVt[0] = reinterpret_cast<void *>(&AppQuery);
    // ids: ordinary ones and ones in the +0x18c set (0xeb7d6513, 0xdae3783f, 0x7a3e667d, 0x9b3798f7, 0x1835d96f)
    static const uint32_t kIds[] = {0x10, 0x11, 0x12, 0xeb7d6513, 0xdae3783f, 0x7a3e667d, 0x9b3798f7, 0x1835d96f, 0x3ae4bea3};
    static const int32_t kPriorities[] = {10000010, 5000001, 1, 0, -1999999, -2000000, -2000001, -2500000, -2999999, -3000000, -3000001, -4000000};
    for (int i = 0; i < kSvcCount; ++i) {
        Svc &s = gSvcs[i];
        s = {gSvcVt, 1, i, kIds[rng() % (sizeof(kIds) / sizeof(kIds[0]))], kPriorities[rng() % (sizeof(kPriorities) / sizeof(kPriorities[0]))],
             static_cast<uint8_t>(rng() % 5 != 0), static_cast<uint8_t>(rng() % 2), 0};
    }
    if (rng() % 2) {
        Svc &app = gSvcs[rng() % kSvcCount];
        app.isApp = 1;
        app.priority = rng() % 2 ? -2000000 : -2999999;
    }
    gAppSvc = {gSvcVt, 1, 20, 0x5afdaae5, -2000000, 1, 0, 1};
    gServerSvc = {gSvcVt, 1, 21, 0x04fa845b, 0, 1, 0, 0};
}

std::string DescribeServices()
{
    std::string out;
    char buf[64];
    for (const Svc *s : {&gSvcs[0], &gSvcs[1], &gSvcs[2], &gSvcs[3], &gSvcs[4], &gSvcs[5], &gSvcs[6], &gSvcs[7], &gAppSvc, &gServerSvc}) {
        std::snprintf(buf, sizeof(buf), " [%d run=%u refs=%d]", s->index, s->running, s->refs);
        out += buf;
    }
    std::snprintf(buf, sizeof(buf), " app=%d server=%d", gApp.refs, gServer.refs);
    return out + buf;
}

// The framework object: the original cCTGFramework constructor (0xc2ccdb), or ours (`ours`).
void BuildFw(difftest::Context &ctx, Fw *&out, const std::string &prefill, int hooks, bool ours)
{
    static bool clock = false;
    if (!clock) ctx.Original<int(__cdecl *)()>(0x83a409)();
    clock = true;
    auto *mem = static_cast<uint8_t *>(::operator new(sizeof(Fw)));
    std::memset(mem, 0xCD, sizeof(Fw));
    if (ours) gz::cCTGFrameworkAdapters::Construct(mem, nullptr);
    else ctx.Original<void *(__thiscall *)(void *)>(0xc2ccdb)(mem);
    out = reinterpret_cast<Fw *>(mem);
    // The embedded command line starts with some content, so the assignment has a capacity to reuse.
    auto *cmd = reinterpret_cast<cRZCmdLine *>(out->mCmdLine);
    cmd->Destruct();
    cmd->Construct(prefill.c_str());
    for (int i = 0; i < hooks; ++i) out->mHooks.push_back(FwRef(&gHook));
}

void WireObjects()
{
    for (auto &s : gServiceVt) s = reinterpret_cast<void *>(&Trap);
    gServiceVt[1] = reinterpret_cast<void *>(&SvcAddRef);
    gServiceVt[2] = reinterpret_cast<void *>(&SvcRelease);
    for (auto &s : gHookVt) s = reinterpret_cast<void *>(&Trap);
    gHookVt[1] = reinterpret_cast<void *>(&HookAddRef);
    gHookVt[2] = reinterpret_cast<void *>(&HookRelease);
    gHookVt[3] = reinterpret_cast<void *>(&HookPreFrameworkInit);
    gHookVt[4] = reinterpret_cast<void *>(&HookPreAppInit);
    gHookVt[5] = reinterpret_cast<void *>(&HookPostAppInit);
    for (auto &s : gAppVt) s = reinterpret_cast<void *>(&Trap);
    gAppVt[6] = reinterpret_cast<void *>(&AppPreInit);
    gAppVt[7] = reinterpret_cast<void *>(&AppPostInit);
    gAppVt[11] = reinterpret_cast<void *>(&AppDynamicLibs);
    gAppVt[12] = reinterpret_cast<void *>(&AppDirectors);
    gAppVt[13] = reinterpret_cast<void *>(&AppServices);
    for (auto &s : gComVt) s = reinterpret_cast<void *>(&Trap);
    gComVt[0] = reinterpret_cast<void *>(&ComQuery);
    gComVt[9] = reinterpret_cast<void *>(&ComRealInit);
    gComVt[10] = reinterpret_cast<void *>(&ComRealShutdown);
    gComVt[11] = reinterpret_cast<void *>(&ComSetRunning);
    gService = {gServiceVt, 1};
    gHook = {gHookVt, 0};
    gApp = {gAppVt, 1};
    gCom = {gComVt, 1};
}

// How the band slots 41..43 are served: recorders (Stub), the object's own (Real: the game's on an original-built object,
// ours on ours), or the hooks' adapters on an original-built object (Adapters: what runs in game, where the live object
// keeps the game's vtable and its slots lead to the hooks).
enum class Bands { Stub, Real, Adapters };

void WireFw(Fw *fw, Bands bands)
{
    std::memcpy(gFwVt, fw->mVTable, sizeof(void *) * 49);
    gFwVt[3] = reinterpret_cast<void *>(&FwAddService);
    gFwVt[18] = reinterpret_cast<void *>(&FwToggle);
    gFwVt[23] = reinterpret_cast<void *>(&FwGetCom);
    if (bands == Bands::Stub) {
        gFwVt[41] = reinterpret_cast<void *>(&FwInitHigh);
        gFwVt[42] = reinterpret_cast<void *>(&FwInitApp);
        gFwVt[43] = reinterpret_cast<void *>(&FwInitLow);
    } else {
        gFwVt[5] = reinterpret_cast<void *>(&FwGetService);
        gFwVt[37] = reinterpret_cast<void *>(&FwApplication);
    }
    if (bands == Bands::Adapters) {
        gFwVt[41] = reinterpret_cast<void *>(&gz::cCTGFrameworkAdapters::InitHighPriorityServices);
        gFwVt[42] = reinterpret_cast<void *>(&gz::cCTGFrameworkAdapters::InitAppBandServices);
        gFwVt[43] = reinterpret_cast<void *>(&gz::cCTGFrameworkAdapters::InitLowPriorityServices);
    }
    fw->mVTable = gFwVt;
    for (auto &s : gLockVt) s = reinterpret_cast<void *>(&Trap);
    gLockVt[1] = reinterpret_cast<void *>(&LockRec);
    gLockVt[2] = reinterpret_cast<void *>(&UnlockRec);
    fw->mLock.mVTable = gLockVt;
}

} // namespace

DIFF_TEST(FrameworkStartup_Startup, kStartup)
{
    ModulePathFakes fakes(ctx);
    uint32_t savedFw = *ctx.image.At<uint32_t>(cGZFramework::kGlobalFramework);
    uint32_t savedApp = *ctx.image.At<uint32_t>(cGZFramework::kGlobalApp);
    int32_t savedExit = *ctx.image.At<int32_t>(cGZFramework::kGlobalExitCode);

    uint32_t savedServer = *ctx.image.At<uint32_t>(0x10f519c);

    std::mt19937 rng(0x144514);
    static const char *const kPaths[] = {"C:\\Games\\The Sims 2\\TSBin\\Sims2EP9.exe", "d:other.exe", ""};
    static const int32_t kExit[] = {0, -1, 5};
    // mode 0: band recorders, side B's object from the original constructor (even variants) or ours (odd);
    // mode 1: real bands, side B the hooks' adapters on an original-built object; mode 2: real bands on our object.
    for (int bits = 0; bits < 3 * 32; ++bits) {
        int mode = bits / 32;
        for (int variant = 0; variant < 6; ++variant) {
            Script script{};
            script.realInit = bits & 1;
            script.queryService = bits & 2;
            script.appServices = bits & 4;
            script.preInit = bits & 8;
            script.appBand = bits & 16;
            script.exitAtStart = kExit[rng() % 3];
            script.exitAfterLow = (variant & 1) ? kExit[rng() % 3] : kKeep;
            script.exitAfterPost = (variant & 2) ? kExit[rng() % 3] : kKeep;
            script.ignored12 = static_cast<uint8_t>(rng() % 3);
            script.ignored11 = static_cast<uint8_t>(rng() % 3);
            script.ignored41 = static_cast<uint8_t>(rng() % 3);
            script.ignored43 = static_cast<uint8_t>(rng() % 3);
            std::string argText = RandomText(rng, static_cast<int>(rng() % 6));
            std::string prefill = RandomText(rng, static_cast<int>(rng() % 8));
            std::string path = kPaths[rng() % 3];
            uint32_t install = (variant & 4) ? static_cast<uint32_t>(rng()) : (rng() % 2);
            int hooks = static_cast<int>(rng() % 3);
            uint32_t startState = rng() % 16;
            uint32_t refAtStart = rng();
            uint32_t bandSeed = static_cast<uint32_t>(rng());
            gWide = Widen(path);
            gNarrow = "";
            std::string results[2];
            for (int side = 0; side < 2; ++side) {
                gScript = script;
                gLog.clear();
                Fw *fw;
                WireObjects();
                bool ours = side == 1 && (mode == 2 || (mode == 0 && (variant & 1)));
                BuildFw(ctx, fw, prefill, hooks, ours);
                gFw = fw;
                *ctx.image.At<uint32_t>(0x10f519c) = 0; // the message server is looked up afresh
                Bands bands = mode == 0 ? Bands::Stub : (mode == 1 && side == 1) ? Bands::Adapters : Bands::Real;
                if (bands != Bands::Stub) {
                    // The services, registered through the original AddSystemService on the object's own vtable.
                    std::mt19937 bandRng(bandSeed);
                    WireBandObjects(bandRng);
                    *ctx.image.At<Fw *>(cGZFramework::kGlobalFramework) = fw;
                    for (Svc &s : gSvcs) ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x14234f)(fw, &s);
                    gLog.clear();
                }
                WireFw(fw, bands);
                fw->mState = startState;
                cRZCmdLine *arg = NewCmd(argText.c_str());
                arg->mRefCount = refAtStart;
                *ctx.image.At<Fw *>(cGZFramework::kGlobalFramework) = fw;
                *ctx.image.At<Obj *>(cGZFramework::kGlobalApp) = &gApp;
                ExitCode() = script.exitAtStart;
                uint8_t ok;
                if (side == 0) ok = ctx.Original<uint8_t(__cdecl *)(cRZCmdLine *, uint32_t)>(kStartup)(arg, install);
                else ok = fw_startup::GZFrameWork_Startup(arg, install);
                char tail[200];
                std::snprintf(tail, sizeof(tail), "ret=%u exit=%d state=%u install=%u svcrefs=%d hookrefs=%d", ok, ExitCode(), fw->mState, fw->mDeferQuit, gService.refs, gHook.refs);
                results[side] = Joined() + tail + "\n" + Describe(reinterpret_cast<cRZCmdLine *>(fw->mCmdLine)) + "\n" + Describe(arg);
                if (bands != Bands::Stub) results[side] += "\n" + DescribeServices();
                FreeCmd(arg);
                reinterpret_cast<cRZCmdLine *>(fw->mCmdLine)->Destruct();
                fw->mHooks.clear();
                // The rest of the framework (the embedded COM object's tables) is left for the process to reclaim.
            }
            ExpectSame(ctx, "startup", results[0], results[1]);
        }
    }
    *ctx.image.At<uint32_t>(cGZFramework::kGlobalFramework) = savedFw;
    *ctx.image.At<uint32_t>(cGZFramework::kGlobalApp) = savedApp;
    *ctx.image.At<int32_t>(cGZFramework::kGlobalExitCode) = savedExit;
    *ctx.image.At<uint32_t>(0x10f519c) = savedServer;
}

// ---- the stopwatch methods cRZStopwatch.h leaves out ----
// As tests/rz_stopwatch.cpp: the original's QueryPerformanceCounter import slot is pointed at a
// scripted fake and ours reads the same script through the stopwatch's override pointer. Mode 0
// reads the time-stamp counter, which cannot be scripted, so it is compared back to back.

namespace {

constexpr uint32_t kIatQpc = 0xd9a24c;
constexpr uint32_t kStopwatchCtor = 0x1c9030, kElapsedFloat = 0x1c9368, kElapsed64 = 0x1c948d, kStop = 0x1c9842;

uint64_t gNow = 0;
int gQpcCalls = 0;

BOOL WINAPI FakeQpc(LARGE_INTEGER *out)
{
    ++gQpcCalls;
    out->QuadPart = static_cast<int64_t>(gNow);
    return TRUE;
}
void FakeCounter(uint64_t *out)
{
    ++gQpcCalls;
    *out = gNow;
}

struct ScriptedClock {
    difftest::Context &ctx;
    void *saved;
    explicit ScriptedClock(difftest::Context &c) : ctx(c)
    {
        void **slot = ctx.image.At<void *>(kIatQpc);
        saved = *slot;
        *slot = reinterpret_cast<void *>(&FakeQpc);
        cRZStopwatch::sPerformanceCounterOverride = &FakeCounter;
    }
    ~ScriptedClock()
    {
        *ctx.image.At<void *>(kIatQpc) = saved;
        cRZStopwatch::sPerformanceCounterOverride = nullptr;
    }
};

void SetCalibration(difftest::Context &ctx, uint64_t overhead, const float (&scales)[5])
{
    *ctx.image.At<uint64_t>(cRZStopwatch::kOverheadRva) = overhead;
    for (int i = 0; i < 5; ++i) ctx.image.At<float>(cRZStopwatch::kScalesRva)[i] = scales[i];
    ctx.image.At<float>(cRZStopwatch::kScalesRva)[5] = 3.0f; // not a scale: a mode that read it would show
}

uint64_t Rand64(std::mt19937_64 &rng, int bits)
{
    return rng() >> (64 - bits);
}

// One random stopwatch state shared by the three tests: a mode (including unknown ones), scale
// factors (including ones that push counts past 64 bits' worth of precision), an overhead, an
// accumulated count, a start and the counter's value now.
struct Setup {
    int32_t mode;
    float scales[5];
    uint64_t overhead, accumulated, start, now;
    bool started;
};

Setup RandomSetup(std::mt19937_64 &rng, int run)
{
    Setup s;
    s.mode = run % 9;
    if (s.mode == 0) s.mode = 1;
    if (run % 50 == 7) s.mode = -2;
    for (float &f : s.scales) {
        uint32_t pick = rng() % 6;
        f = pick == 0 ? 0.0f : pick == 1 ? -0.001f * (rng() % 100) : pick == 2 ? 1.0f : 0.000001f * (1 + rng() % 5000000) / 1000.0f;
        if (pick == 5) f = 0.001f * (rng() % 90000);
    }
    s.overhead = run % 7 == 0 ? 0 : Rand64(rng, 1 + rng() % 40);
    s.accumulated = run % 4 == 0 ? 0 : Rand64(rng, 1 + rng() % 50);
    bool tie = false;
    if (run % 8 == 3 && s.mode >= 2 && s.mode <= 6) {
        // Counts beyond a double's 53 bits, with scales that keep the product inside 64 bits; half
        // the time a float tie plus one (converting to double first rounds the extra bit away).
        s.accumulated = Rand64(rng, 54 + rng() % 11);
        const float bigScales[] = {0.0f, 0.25f, 0.5f, 0.001f, -0.5f, 0.125f};
        for (float &f : s.scales) f = bigScales[rng() % 6];
        if (rng() % 2) {
            uint32_t k = 30 + rng() % 32;
            s.accumulated = (1ull << k) + (1ull << (k - 24)) + 1;
            tie = true;
        }
    }
    s.started = run % 5 != 0 && !tie;
    s.start = s.started ? 1 + Rand64(rng, 1 + rng() % 62) : 0;
    switch (rng() % 8) {
    case 0: s.now = s.start + s.overhead; break;
    case 1: s.now = s.start + s.overhead + 1; break;
    case 2: s.now = s.start + (s.overhead ? s.overhead - 1 : 0); break;
    case 3: s.now = s.start; break;
    case 4: s.now = s.start - 1 - rng() % 100; break; // before the start: the span wraps around
    case 5: s.now = s.start + Rand64(rng, 1 + rng() % 40); break;
    case 6: s.now = s.start + s.overhead + Rand64(rng, 1 + rng() % 30); break;
    default: s.now = Rand64(rng, 1 + rng() % 63); break;
    }
    return s;
}

void Prepare(difftest::Context &ctx, const Setup &s, cRZStopwatch &a, cRZStopwatch &b)
{
    SetCalibration(ctx, s.overhead, s.scales);
    ctx.Original<cRZStopwatch *(__thiscall *)(void *, int32_t)>(kStopwatchCtor)(&a, s.mode);
    b.Construct(s.mode);
    a.mUnused = b.mUnused = 0x1234;
    a.mAccumulated = b.mAccumulated = s.accumulated;
    a.mStart = b.mStart = s.start;
    gNow = s.now;
}

} // namespace

DIFF_TEST(FrameworkStartup_StopwatchElapsedFloat, kElapsedFloat)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, float *)>(kElapsedFloat);
    ScriptedClock clock(ctx);
    std::mt19937_64 rng(0x1c9368);
    for (int run = 0; run < 20000; ++run) {
        Setup s = RandomSetup(rng, run);
        cRZStopwatch a, b;
        Prepare(ctx, s, a, b);
        uint32_t fa = 0x7fc00001, fb = 0x7fc00001; // both sides start from the same NaN pattern
        gQpcCalls = 0;
        orig(&a, reinterpret_cast<float *>(&fa));
        int callsA = gQpcCalls;
        gQpcCalls = 0;
        stopwatch_more::ElapsedAsFloat(&b, nullptr, reinterpret_cast<float *>(&fb));
        ++ctx.cases;
        if (fa != fb || callsA != gQpcCalls || callsA != (s.started ? 1 : 0) || std::memcmp(&a, &b, sizeof(a)) != 0)
            ctx.Fail("mode %d start %llx acc %llx now %llx overhead %llx: %08x/%08x, counter reads %d/%d", s.mode, s.start, s.accumulated, s.now, s.overhead, fa, fb, callsA, gQpcCalls);
    }
    // Mode 0 (time-stamp counter), started a moment ago: the two results are close.
    SetCalibration(ctx, 0, {1, 1, 1, 1, 1});
    for (int run = 0; run < 50; ++run) {
        cRZStopwatch a, b;
        ctx.Original<cRZStopwatch *(__thiscall *)(void *, int32_t)>(kStopwatchCtor)(&a, 0);
        b.Construct(0);
        uint64_t t = 0;
        cRZStopwatch::ReadTsc(&t);
        a.mStart = b.mStart = t - 1000000;
        a.mAccumulated = b.mAccumulated = 0;
        float fa = 0, fb = 0;
        orig(&a, &fa);
        stopwatch_more::ElapsedAsFloat(&b, nullptr, &fb);
        ++ctx.cases;
        if (fa < 1000000.0f || fb < fa || fb - fa > 2.0e9f) ctx.Fail("mode 0: %f then %f", fa, fb);
    }
}

DIFF_TEST(FrameworkStartup_StopwatchElapsed64, kElapsed64)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint64_t *)>(kElapsed64);
    ScriptedClock clock(ctx);
    std::mt19937_64 rng(0x1c948d);
    for (int run = 0; run < 20000; ++run) {
        Setup s = RandomSetup(rng, run);
        cRZStopwatch a, b;
        Prepare(ctx, s, a, b);
        uint64_t ra = 0x1111222233334444ull, rb = ra;
        gQpcCalls = 0;
        orig(&a, &ra);
        int callsA = gQpcCalls;
        gQpcCalls = 0;
        stopwatch_more::ElapsedAs64(&b, nullptr, &rb);
        ++ctx.cases;
        if (ra != rb || callsA != gQpcCalls || callsA != (s.started ? 1 : 0) || std::memcmp(&a, &b, sizeof(a)) != 0)
            ctx.Fail("mode %d start %llx acc %llx now %llx overhead %llx: %llx/%llx, counter reads %d/%d", s.mode, s.start, s.accumulated, s.now, s.overhead, ra, rb, callsA, gQpcCalls);
    }
    SetCalibration(ctx, 0, {1, 1, 1, 1, 1});
    for (int run = 0; run < 50; ++run) {
        cRZStopwatch a, b;
        ctx.Original<cRZStopwatch *(__thiscall *)(void *, int32_t)>(kStopwatchCtor)(&a, 0);
        b.Construct(0);
        uint64_t t = 0;
        cRZStopwatch::ReadTsc(&t);
        a.mStart = b.mStart = t - 1000000;
        a.mAccumulated = b.mAccumulated = 0;
        uint64_t ra = 0, rb = 0;
        orig(&a, &ra);
        stopwatch_more::ElapsedAs64(&b, nullptr, &rb);
        ++ctx.cases;
        if (ra < 1000000 || rb < ra || rb - ra > 2000000000ull) ctx.Fail("mode 0: %llu then %llu", ra, rb);
    }
}

DIFF_TEST(FrameworkStartup_StopwatchStop, kStop)
{
    auto orig = ctx.Original<void(__thiscall *)(void *)>(kStop);
    ScriptedClock clock(ctx);
    std::mt19937_64 rng(0x1c9842);
    for (int run = 0; run < 20000; ++run) {
        Setup s = RandomSetup(rng, run);
        if (run % 9 == 8) s.mode = 0; // Stop reads the performance counter in mode 0 too
        cRZStopwatch a, b;
        Prepare(ctx, s, a, b);
        gQpcCalls = 0;
        orig(&a);
        int callsA = gQpcCalls;
        gQpcCalls = 0;
        stopwatch_more::Stop(&b, nullptr);
        ++ctx.cases;
        if (callsA != gQpcCalls || callsA != (s.started ? 1 : 0) || std::memcmp(&a, &b, sizeof(a)) != 0)
            ctx.Fail("mode %d start %llx acc %llx now %llx overhead %llx: result %llx/%llx start %llx/%llx, counter reads %d/%d", s.mode, s.start, s.accumulated, s.now, s.overhead, a.mAccumulated, b.mAccumulated, a.mStart, b.mStart, callsA, gQpcCalls);
    }
}
