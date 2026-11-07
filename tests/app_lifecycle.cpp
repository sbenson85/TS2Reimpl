#define _CRT_SECURE_NO_WARNINGS
// Differential tests for ts::cTSAppFW's OnTick (0xc28fcc) and Shutdown (0xc2a1e3), lane FM: both routes (own vtable:
// each side's application from its own constructor, called through its own vtable; adapter: game-built applications,
// the adapter called, its own slot trapped in the copy of the game's vtable).
//
// The world is fakes on both sides: the framework service (g_Framework 0x10c9378: GetSystemService slot 5,
// RemoveSystemService slot 10, CommandLine slot 21), the services it answers, the cached getters (0x20dde0, 0x20de51 and
// ours; 0x20df9a, 0x211da3), Sleep, RZMalloc (0x1be3b0 and ours) with the standard message's game vtable patched to log,
// the message server (0x9c5a89), the memory snapshot sampler 0x1bbf19, the message-id walk 0x1c081e (and ours), the
// configuration's save / destructor (0xcb28b0 / 0xcb0ad0) and the exception uploader 0xc28b7d. Every call is logged with
// its receiver and arguments; the outcome is al, the log, and the application digest (tests/app_fixture.h).
#include "rz/RZMessageServer.h"
#include "app_fixture.h"

#include "gz/cIGZPropertySet.h"
#include "rz/MiscHelpersB.h"
#include "rz/StaticGetters2.h"
#include "ts/cTSAppPropertyFiles.h"

#include <utility>

namespace {

uint32_t P(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
alignas(8) uint8_t gMessageBlock[0x40];

// ---- generic fake objects: every slot logs; arity-specific slots are patched in

struct Fake {
    void **vt;
    const char *name;
    uint32_t answer[48];
};

void *gFakeVts[8][48];
Fake gFakes[8];
enum FakeId { kFw, kSettings, kState, kHousehold, kHolder, kStepper, kPainting, kView };
const char *const kFakeNames[8] = {"fw", "settings", "state", "household", "holder", "stepper", "painting", "view"};
Fake gEsper, gObj80, gServer2;
void *gEsperVt[48], *gObj80Vt[48], *gServer2Vt[48];

std::string Who(uint32_t p)
{
    const auto *q = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(p));
    if (gAppBase && q >= gAppBase && q < gAppBase + 0x268) return "@" + std::to_string(q - gAppBase);
    for (auto &f : gFakes)
        if (q == reinterpret_cast<const uint8_t *>(&f)) return f.name;
    for (Fake *f : {&gEsper, &gObj80, &gServer2})
        if (q == reinterpret_cast<const uint8_t *>(f)) return f->name;
    if (q == gMessageBlock) return "message";
    char b[16];
    std::snprintf(b, sizeof(b), "%x", p);
    return b;
}

template <int N> uint32_t __fastcall S0(void *self, void *)
{
    auto *f = static_cast<Fake *>(self);
    Log("%s.s%d", f->name, N);
    return f->answer[N];
}
template <int N> uint32_t __fastcall S1(void *self, void *, uint32_t a)
{
    auto *f = static_cast<Fake *>(self);
    Log("%s.s%d(%s)", f->name, N, Who(a).c_str());
    return f->answer[N];
}
template <size_t... I> void FillS0(void **vt, std::index_sequence<I...>)
{
    ((vt[I] = reinterpret_cast<void *>(&S0<int(I)>)), ...);
}

// The framework's GetSystemService (slot 5): the fakes it knows, each present or not this run.
struct Known {
    uint32_t id, iid;
    Fake *fake;
    bool present;
};
Known gKnown[] = {
    {0x0ac1887e, 0x4bb0d269, &gFakes[kSettings], true},
    {0xfb6372ee, 0x5b637209, &gFakes[kView], true},
    {0x8d127822, 0xad127830, &gEsper, true},
};
uint32_t __fastcall FwGetService(void *, void *, uint32_t id, uint32_t iid, void **out)
{
    Log("fw.GetSystemService(%x, %x)", id, iid);
    for (Known &k : gKnown)
        if (k.id == id && k.iid == iid && k.present) {
            *out = k.fake;
            return 1;
        }
    return 0;
}
CmdLineDouble gCmd;
void *__fastcall FwCommandLine(void *, void *)
{
    Log("fw.CommandLine");
    return &gCmd;
}

// The settings service's slot 9: (const service string *name, cIGZAny *value) -> found; the value's game vtable checked.
uint32_t gSleepValue;
uint32_t __fastcall SettingsGet(void *self, void *, const GZServiceString *name, uint32_t *any)
{
    auto *f = static_cast<Fake *>(self);
    const bool uintVt = any[0] == P(GameAddress<void>(0xd9f534));
    Log("settings.s9(%s, %s %u)", name->c_str(), uintVt ? "AnyUint32" : "?", any[1]);
    if (f->answer[9] & 1) any[1] = gSleepValue;
    return f->answer[9];
}

void WINAPI FakeSleep(DWORD ms) { Log("Sleep(%u)", ms); }

bool gHolderPresent;
void *__cdecl FakeGetSlot23()
{
    Log("GetCachedSlot23");
    return &gFakes[kState];
}
void *__cdecl FakeGetHolder()
{
    Log("GetCachedSingletonB");
    return gHolderPresent ? &gFakes[kHolder] : nullptr;
}
void *__cdecl FakeGetHousehold()
{
    Log("0x20df9a");
    return &gFakes[kHousehold];
}
void *__cdecl FakeGetPainting()
{
    Log("0x211da3");
    return &gFakes[kPainting];
}

// ---- Shutdown's pieces

uint32_t __fastcall FakeSnapshot(void *self, void *, void *out)
{
    const auto *w = static_cast<const uint32_t *>(self);
    bool zero = w[1] == 0 && w[2] == 0 && w[3] == 0 && w[4] == 0 && w[5] == 0;
    Log("snapshot(%s %s, %p)", w[0] == P(GameAddress<void>(0xda31e0)) ? "vt" : "?", zero ? "zero" : "data", out);
    return 0x3300;
}
void *__cdecl FakeMalloc2(uint32_t bytes)
{
    Log("Malloc(%x)", bytes);
    std::memset(gMessageBlock, 0x77, sizeof(gMessageBlock));
    return gMessageBlock;
}
uint32_t __fastcall FakeMessageAddRef2(void *self, void *)
{
    Log("%s.AddRef", Who(P(self)).c_str());
    return 1;
}
uint32_t __fastcall FakeMessageRelease2(void *self, void *)
{
    Log("%s.Release", Who(P(self)).c_str());
    return 0;
}
uint32_t __fastcall FakeMessageSetId(void *self, void *, uint32_t id)
{
    Log("%s.SetId(%x)", Who(P(self)).c_str(), id);
    return 0;
}
void *__cdecl FakeGetServer2()
{
    Log("GetMessageServer");
    return &gServer2;
}
void __fastcall FakeWalk6(void *self, void *, uint32_t arg, void *obj)
{
    Log("walk6(%s, %s, %s)", Who(P(self)).c_str(), Who(arg).c_str(), obj ? "obj" : "null");
}
void *gConfiguration;
void __fastcall FakeConfigSave(void *self, void *)
{
    Log("config.Save(%s)", self == gConfiguration ? "config" : self ? "?" : "null");
}
void __fastcall FakeConfigDestruct(void *self, void *)
{
    Log("config.~(%s)", self == gConfiguration ? "config" : "?");
}
void __fastcall FakeUploader(void *self, void *, uint32_t a, uint32_t b, uint32_t c)
{
    Log("uploader(%s, %u, %u, %u)", Who(P(self)).c_str(), a & 0xff, b & 0xff, c & 0xff); // the callee reads bytes
}

// The world: fakes set up once per test, redirects for its life.
struct World {
    difftest::Context &ctx;
    std::vector<std::unique_ptr<Redirect>> redirects;
    void **framework;
    void *savedFramework;
    IatPatch sleep;
    explicit World(difftest::Context &c) : ctx(c), sleep(c, 0xd9a144, reinterpret_cast<const void *>(&FakeSleep))
    {
        for (int i = 0; i < 8; ++i) {
            FillS0(gFakeVts[i], std::make_index_sequence<48>());
            gFakes[i] = Fake{gFakeVts[i], kFakeNames[i], {}};
        }
        FillS0(gEsperVt, std::make_index_sequence<48>());
        FillS0(gObj80Vt, std::make_index_sequence<48>());
        FillS0(gServer2Vt, std::make_index_sequence<48>());
        gEsper = Fake{gEsperVt, "esper", {}};
        gObj80 = Fake{gObj80Vt, "object80", {}};
        gServer2 = Fake{gServer2Vt, "server", {}};
        gServer2Vt[3] = reinterpret_cast<void *>(&S1<3>);
        gFakeVts[kFw][5] = reinterpret_cast<void *>(&FwGetService);
        gFakeVts[kFw][10] = reinterpret_cast<void *>(&S1<10>);
        gFakeVts[kFw][21] = reinterpret_cast<void *>(&FwCommandLine);
        gFakeVts[kSettings][9] = reinterpret_cast<void *>(&SettingsGet);
        Add(c.image.At(0x20dde0), &FakeGetSlot23);
        Add(reinterpret_cast<void *>(&GetCachedSlot23), &FakeGetSlot23);
        Add(c.image.At(0x20de51), &FakeGetHolder);
        Add(reinterpret_cast<void *>(&GetCachedSingletonB), &FakeGetHolder);
        Add(c.image.At(0x20df9a), &FakeGetHousehold);
        Add(c.image.At(0x211da3), &FakeGetPainting);
        Add(c.image.At(0x1bbf19), &FakeSnapshot);
        Add(c.image.At(0x1be3b0), &FakeMalloc2);
        Add(reinterpret_cast<void *>(&RZMemory::Malloc), &FakeMalloc2);
        Add(c.image.At(0x9c5a89), &FakeGetServer2);
        Add(reinterpret_cast<void *>(&GetCachedMessageServer), &FakeGetServer2); // ours
        Add(c.image.At(0x1c081e), &FakeWalk6);
        Add(MemberAddress(&KeyMapWalker::CallSlot6ForEachKey), &FakeWalk6);
        Add(c.image.At(0xcb28b0), &FakeConfigSave);
        Add(c.image.At(0xcb0ad0), &FakeConfigDestruct);
        Add(MemberAddress(&ts::cTSAppPropertyFiles::Save), &FakeConfigSave); // ours (lane MA)
        Add(MemberAddress(&ts::cTSAppPropertyFiles::Destruct), &FakeConfigDestruct);
        Add(c.image.At(0xc28b7d), &FakeUploader);
        Add(MemberAddress<&ts::cTSAppFW::LaunchExceptionUploader>(), &FakeUploader); // ours (lane MB)
        framework = c.image.At<void *>(0x10c9378);
        savedFramework = *framework;
        *framework = &gFakes[kFw];
        void **messageVt = c.image.At<void *>(0xda4064);
        std::memcpy(savedMessageVt, messageVt, sizeof(savedMessageVt));
        messageVt[1] = reinterpret_cast<void *>(&FakeMessageAddRef2);
        messageVt[2] = reinterpret_cast<void *>(&FakeMessageRelease2);
        messageVt[5] = reinterpret_cast<void *>(&FakeMessageSetId);
        gMoreSwitches.clear();
        gMoreSwitches.push_back({"EsperSession", Switch{}});
    }
    ~World()
    {
        std::memcpy(ctx.image.At<void *>(0xda4064), savedMessageVt, sizeof(savedMessageVt));
        *framework = savedFramework;
        while (!redirects.empty()) redirects.pop_back(); // in reverse (two of ours may share code)
        gMoreSwitches.clear();
    }
    template <class F> void Add(void *at, F *fake) { redirects.push_back(std::make_unique<Redirect>(at, reinterpret_cast<void *>(fake))); }
    void *savedMessageVt[12];
};

std::string RouteName(Route route) { return route == Route::Adapter ? "adapter" : "own vtable"; }

} // namespace

// ---- 0xc28fcc OnTick (cIGZSystemService slot 10)

DIFF_TEST(cTSAppFW_OnTick, 0xc28fcc)
{
    Env env(ctx);
    SetUpCommandLine(ctx);
    LearnOurVTables();
    World world(ctx);
    for (Route route : {Route::OwnVTable, Route::Adapter}) {
        for (int run = 0; run < 300; ++run) {
            std::string results[2];
            for (int side = 0; side < 2; ++side) {
                std::mt19937 r(run * 7717 + static_cast<int>(route));
                const bool original = side == 0;
                LiveApp app(ctx, original, route, "The Sims 2");
                if (route == Route::Adapter) app.SetSlot(0, 10, reinterpret_cast<const void *>(&TrapOneArg));
                app.raw[0x6c] = r() % 3 == 0 ? static_cast<uint8_t>(r() | 1) : 0;
                gFakes[kSettings].answer[9] = r() % 3 ? 0x5500u | (r() & 0xff) : 0x5500;
                gSleepValue = r() % 50;
                gFakes[kState].answer[5] = r() % 2 ? 0xbea28c2 : r() % 2 ? 0xbea28c3 : r();
                gHolderPresent = r() % 4 != 0;
                gFakes[kHolder].answer[36] = r() % 3 ? P(&gFakes[kStepper]) : 0;
                gFakes[kView].answer[31] = r() & 0x1f;
                const uint32_t counter = r();
                gLog.clear();
                uint32_t result;
                if (route == Route::Adapter)
                    result = original ? ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0xc28fcc)(app.raw, counter)
                                      : ts::cTSAppFWAdapters::OnTick(reinterpret_cast<ts::cTSAppFW *>(app.raw), nullptr, counter);
                else
                    result = CallSlot<uint32_t>(app.Face(0), 10, counter);
                results[side] = Outcome(result, app.Digest());
            }
            ExpectSame(ctx, (RouteName(route) + " OnTick " + std::to_string(run)).c_str(), results[0], results[1]);
        }
    }
}

// ---- 0xc2a1e3 Shutdown (cIGZSystemService slot 9)

DIFF_TEST(cTSAppFW_Shutdown, 0xc2a1e3)
{
    Env env(ctx);
    SetUpCommandLine(ctx);
    LearnOurVTables();
    World world(ctx);
    for (Route route : {Route::OwnVTable, Route::Adapter}) {
        for (int run = 0; run < 300; ++run) {
            std::string results[2];
            for (int side = 0; side < 2; ++side) {
                std::mt19937 r(run * 4421 + static_cast<int>(route));
                const bool original = side == 0;
                LiveApp app(ctx, original, route, "The Sims 2");
                if (route == Route::Adapter) app.SetSlot(0, 9, reinterpret_cast<const void *>(&TrapNoArgs));
                *reinterpret_cast<void **>(app.raw + 0x80) = r() % 2 ? &gObj80 : nullptr;
                // The configuration: a block of the side's own operator new (the side's code deletes it), or none (the
                // save is a fake here; the original's 0xcb28b0 would fault on a null object).
                void *configuration = nullptr;
                if (r() % 4 != 0)
                    configuration = original ? ctx.Original<void *(__cdecl *)(size_t)>(0x824632)(0xf0) : ::operator new(0xf0);
                gConfiguration = configuration;
                *reinterpret_cast<void **>(app.raw + 0x60) = configuration;
                gMoreSwitches[0].second.al = static_cast<uint8_t>(r() % 2 ? 0 : r() | 1);
                gKnown[2].present = r() % 3 != 0;
                std::memset(gMessageBlock, 0x55, sizeof(gMessageBlock));
                gLog.clear();
                uint32_t result;
                if (route == Route::Adapter)
                    result = original ? ctx.Original<uint32_t(__thiscall *)(void *)>(0xc2a1e3)(app.raw)
                                      : ts::cTSAppFWAdapters::Shutdown(reinterpret_cast<ts::cTSAppFW *>(app.raw), nullptr);
                else
                    result = CallSlot<uint32_t>(app.Face(0), 9);
                uint8_t block[0x2c];
                std::memcpy(block, gMessageBlock, sizeof(block));
                const bool gameVt = *reinterpret_cast<void **>(block) == ctx.image.At<void>(0xda4064);
                std::memset(block, 0, 4);
                results[side] = Outcome(result, app.Digest()) + (gameVt ? "msgvt " : "other ") + Hex(block, sizeof(block));
            }
            ExpectSame(ctx, (RouteName(route) + " Shutdown " + std::to_string(run)).c_str(), results[0], results[1]);
        }
    }
}
