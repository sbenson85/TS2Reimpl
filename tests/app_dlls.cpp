#define _CRT_SECURE_NO_WARNINGS
// Differential test for ts::cTSAppFW::AddDynamicLibrariesHere (0xc26c30), lane FW: both routes (own vtable: each side's
// application from its own constructor, called through its own cIGZApp vtable slot 11; adapter: game-built applications,
// the adapter called, its own slot trapped in the copy of the game's vtable).
//
// The world is fakes on both sides: the framework service (g_Framework 0x10c9378: CommandLine slot 21) and the command
// line double (slots 3 ArgC, 9 IsSwitchPresent, 13 InsertArgument), the library list's seven members (0xcb3640 ...
// 0xcb36f0 and ours, lane LY), cRZVersion (0x1c98d6 / 0x1c9b6d / 0x1c9901 and ours: the module path and the
// version resource are imports), the option getter 0x1d0c00 (and ours) and cRZFile::PathExists 0x1c1242 (and ours).
// Every call is logged with its receiver and the texts of its string arguments; the outcome is al, the log and the
// application digest (tests/app_fixture.h).
#include "app_fixture.h"

#include "app/ReportFinish.h"
#include "rz/cRZFile.h"
#include "ts/DynamicLibraryList.h"

namespace {

uint32_t P(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

const void *gList = nullptr;    // the list the constructor fake saw
const void *gVersion = nullptr; // the cRZVersion the constructor fake saw
const char *ListName(const void *self) { return self == gList ? "list" : "list?"; }
const char *VersionName(const void *self) { return self == gVersion ? "version" : "version?"; }
const char *Text(const cRZString *s) { return s ? s->mString.c_str() : "(null)"; }

// ---- the list (0x1c bytes on the caller's stack)

uint8_t gReadAnswer, gLoadAnswer, gDestructAnswer;
void *__fastcall ListConstruct(void *self, void *)
{
    gList = self;
    std::memset(self, 0x3c, 0x1c);
    Log("list.Construct");
    return self; // our caller uses the adapter's result (the original's caller ignores it)
}
void __fastcall ListRequireVersion(void *self, void *, cRZString *name, cRZString *version, uint32_t required)
{
    Log("%s.RequireVersion(%s, %s, %u)", ListName(self), Text(name), Text(version), required);
}
uint32_t __fastcall ListReadConfiguration(void *self, void *, cRZString *path, cRZString *configuration)
{
    Log("%s.ReadConfiguration(%s, %s)", ListName(self), Text(path), Text(configuration));
    return 0x6600u | gReadAnswer;
}
void __fastcall ListAdd(void *self, void *, cRZString *path, uint32_t required)
{
    Log("%s.Add(%s, %u)", ListName(self), Text(path), required);
}
uint32_t __fastcall ListLoadAll(void *self, void *)
{
    Log("%s.LoadAll", ListName(self));
    return 0x6700u | gLoadAnswer;
}
void __fastcall ListReport(void *self, void *)
{
    Log("%s.Report", ListName(self));
}
uint32_t __fastcall ListDestruct(void *self, void *)
{
    Log("%s.Destruct", ListName(self));
    return 0x4400u | gDestructAnswer;
}

// ---- cRZVersion

std::string gFileVersion;
void *__fastcall VersionConstruct(void *self, void *)
{
    gVersion = self;
    Log("version.Construct");
    return self;
}
uint32_t __fastcall VersionGetFileVersion(void *self, void *, cRZString *out)
{
    Log("%s.GetFileVersion", VersionName(self));
    out->mString = gFileVersion;
    return 0x5501;
}
void __fastcall VersionDestruct(void *self, void *)
{
    Log("%s.Destruct", VersionName(self));
}

// ---- options, files, the command line

std::string gConfigurationOption, gDirectoryOption;
uint32_t __cdecl FakeGetOption(uint32_t key, uint32_t out, uint32_t c, uint32_t d, uint32_t e)
{
    auto *s = reinterpret_cast<cRZString *>(static_cast<uintptr_t>(out));
    Log("GetOption(%x, [%s], %u, %u, %x)", key, Text(s), c, d, e);
    if (key == 0x6c253729) s->mString = gConfigurationOption;
    else if (key == 0x6bcfce93) s->mString = gDirectoryOption;
    return 0x7701;
}
uint8_t gPathExists;
uint32_t __cdecl FakePathExists(const cRZString *path)
{
    Log("PathExists(%s)", Text(path));
    return 0x8800u | gPathExists;
}
int32_t gArgC;
int32_t __fastcall FakeArgC(void *, void *)
{
    Log("cmd.ArgC");
    return gArgC;
}
uint32_t __fastcall FakeInsertArgument(void *, void *, const cRZString *argument, int32_t index)
{
    Log("cmd.InsertArgument(%s, %d)", Text(argument), index);
    return 0x9901;
}

struct Fake {
    void **vt;
};
void *gFwVt[48];
Fake gFw{gFwVt};
CmdLineDouble gCmd;
void *__fastcall FwCommandLine(void *, void *)
{
    Log("fw.CommandLine");
    return &gCmd;
}
uint32_t __fastcall FwTrap(void *, void *)
{
    Log("fw.TRAP");
    return 0;
}

constexpr const char *kSwitches[] = {"benchLengthMS", "benchLengthSimTicks", "metrics", "perf", "perfTrackExecution", "nosound"};

struct World {
    difftest::Context &ctx;
    std::vector<std::unique_ptr<Redirect>> redirects;
    void **framework;
    void *savedFramework;
    explicit World(difftest::Context &c) : ctx(c)
    {
        for (void *&slot : gFwVt) slot = reinterpret_cast<void *>(&FwTrap);
        gFwVt[21] = reinterpret_cast<void *>(&FwCommandLine);
        gCmdVt[3] = reinterpret_cast<void *>(&FakeArgC);
        gCmdVt[13] = reinterpret_cast<void *>(&FakeInsertArgument);
        Add(c.image.At(0xcb3640), &ListConstruct);
        Add(c.image.At(0xcb3e10), &ListRequireVersion);
        Add(c.image.At(0xcb3870), &ListReadConfiguration);
        Add(c.image.At(0xcb3740), &ListAdd);
        Add(c.image.At(0xcb3b70), &ListLoadAll);
        Add(c.image.At(0xcb3a80), &ListReport);
        Add(c.image.At(0xcb36f0), &ListDestruct);
        // Ours (src/ts/DynamicLibraryList.h, lane LV; our caller switched to it by lane LY): the adapters it builds and
        // destroys the list through, and the members it calls.
        Add(reinterpret_cast<void *>(&DynamicLibraryListAdapters::Construct), &ListConstruct);
        Add(MemberAddress(&DynamicLibraryList::RequireVersion), &ListRequireVersion);
        Add(MemberAddress(&DynamicLibraryList::ReadConfiguration), &ListReadConfiguration);
        Add(MemberAddress(&DynamicLibraryList::Add), &ListAdd);
        Add(MemberAddress(&DynamicLibraryList::LoadAll), &ListLoadAll);
        Add(MemberAddress(&DynamicLibraryList::Report), &ListReport);
        Add(reinterpret_cast<void *>(&DynamicLibraryListAdapters::Destruct), &ListDestruct);
        Add(c.image.At(0x1c98d6), &VersionConstruct);
        Add(MemberAddress(&appbase::cRZVersion::Construct), &VersionConstruct);
        Add(c.image.At(0x1c9b6d), &VersionGetFileVersion);
        Add(MemberAddress(&appbase::cRZVersion::GetFileVersion), &VersionGetFileVersion);
        Add(c.image.At(0x1c9901), &VersionDestruct);
        Add(MemberAddress(&appbase::cRZVersion::Destruct), &VersionDestruct);
        Add(c.image.At(0x1d0c00), &FakeGetOption);
        Add(reinterpret_cast<void *>(&appinit::CommonImplSlot4), &FakeGetOption);
        Add(c.image.At(0x1c1242), &FakePathExists);
        Add(reinterpret_cast<void *>(&cRZFile::PathExists), &FakePathExists);
        framework = c.image.At<void *>(0x10c9378);
        savedFramework = *framework;
        *framework = &gFw;
        gMoreSwitches.clear();
        for (const char *name : kSwitches) gMoreSwitches.push_back({name, Switch{}});
    }
    ~World()
    {
        *framework = savedFramework;
        while (!redirects.empty()) redirects.pop_back();
        gMoreSwitches.clear();
    }
    template <class F> void Add(void *at, F *fake) { redirects.push_back(std::make_unique<Redirect>(at, reinterpret_cast<void *>(fake))); }
};

std::string RouteName(Route route) { return route == Route::Adapter ? "adapter" : "own vtable"; }

// A path-like text: empty, short (in the string's own buffer) or long (on the heap).
std::string PathText(std::mt19937 &r, const char *stem)
{
    switch (r() % 4) {
    case 0: return "";
    case 1: return std::string(stem).substr(0, 1 + r() % 6);
    default: return std::string("C:/Program Files/EA GAMES/") + stem + std::to_string(r() % 1000) + "/";
    }
}

uint8_t SwitchAnswer(std::mt19937 &r)
{
    switch (r() % 3) {
    case 0: return 0;
    case 1: return static_cast<uint8_t>(r() | 1);
    default: return static_cast<uint8_t>((r() | 2) & 0xfe);
    }
}

} // namespace

// ---- 0xc26c30 AddDynamicLibrariesHere (cIGZApp slot 11)

DIFF_TEST(cTSAppFW_AddDynamicLibrariesHere, 0xc26c30)
{
    Env env(ctx);
    SetUpCommandLine(ctx);
    LearnOurVTables();
    World world(ctx);
    for (Route route : {Route::OwnVTable, Route::Adapter}) {
        for (int run = 0; run < 300; ++run) {
            std::string results[2];
            for (int side = 0; side < 2; ++side) {
                std::mt19937 r(run * 6151 + static_cast<int>(route));
                const bool original = side == 0;
                LiveApp app(ctx, original, route, "The Sims 2");
                if (route == Route::Adapter) app.SetSlot(2, 11, reinterpret_cast<const void *>(&TrapNoArgs));
                gFileVersion = r() % 4 ? "1.18.0." + std::to_string(r() % 400) : "";
                gConfigurationOption = PathText(r, "Config/DLLs.ini");
                gDirectoryOption = PathText(r, "TSBin");
                gPathExists = static_cast<uint8_t>(r() % 2 ? 0 : r() % 2 ? 1 : 0x80);
                gReadAnswer = static_cast<uint8_t>(r() % 2 ? 0 : r() % 2 ? 1 : 0x40);
                gLoadAnswer = static_cast<uint8_t>(r() % 2);
                gDestructAnswer = static_cast<uint8_t>(r() % 2);
                gArgC = static_cast<int32_t>(r() % 9);
                for (auto &s : gMoreSwitches) s.second.al = r() % 2 ? 0 : SwitchAnswer(r);
                gList = gVersion = nullptr;
                gLog.clear();
                uint32_t result;
                if (route == Route::Adapter)
                    result = original ? ctx.Original<uint32_t(__thiscall *)(void *)>(0xc26c30)(app.Face(2))
                                      : ts::cTSAppFWAdapters::AddDynamicLibrariesHere(reinterpret_cast<cIGZApp *>(app.Face(2)), nullptr);
                else
                    result = CallSlot<uint32_t>(app.Face(2), 11);
                results[side] = Outcome(result, app.Digest());
            }
            ExpectSame(ctx, (RouteName(route) + " AddDynamicLibrariesHere " + std::to_string(run)).c_str(), results[0], results[1]);
        }
    }
}
