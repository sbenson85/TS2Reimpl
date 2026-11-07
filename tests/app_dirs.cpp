#define _CRT_SECURE_NO_WARNINGS
// Differential tests for ts::cTSAppFW's PreFrameworkInit (0xc290d8) and SetupUserDataDirectories (0xc2a7de), lane FM:
// both routes (own vtable: each side's application from its own constructor, called through its own vtable; adapter:
// game-built applications, the adapter called, its own slot trapped in the copy of the game's vtable).
//
// Nothing real is reached: the option store's slots the bodies call (AddOption 3, GetOptionText 4, GetOptionList 5,
// RegisterRegistryHandlers 17) are fakes in the copies of each side's vtable, and the cached cITSAppCommon of the
// forwarders 0x1d0c00 / 0x1d0c60 / 0x1d0d10 (0x10cc040) is the side's own face; the framework service (0x10c9378: slots
// 3 AddSystemService, 21 CommandLine), the language service (cache 0x10f51c8), the director's framework object (0x1ae18d
// and ours), the allocator service (0x10c95d4, for the option string lists: HeapAlloc / HeapFree) are fakes; the file
// system (PathExists 0x1c1242, DirectoryExists 0x1c4f94, AbsolutePathString 0x1c1760, EnsureDirectory 0x1c5031, the
// special folders 0x1c50d6, the registry's product name 0xc27a16, cRZFile's constructor 0x1c0c8b) is redirected to
// recording fakes on both sides (ours too where the body calls ours), as are the base SetupUserDataDirectories 0xca8710,
// ReadResourceConfiguration 0xca6c10, the configuration object's constructor and loaders (0xcb0890, 0xcb0f40, 0xcb18a0,
// 0xcb1790), the exception uploader 0xc28b7d and the message-id walk 0x1c0458. The reporter's SetDirectory / AddListener
// run for real (its words are masked in the digest; the listener's references show in the application's count).
#include "app_fixture.h"

#include "app/AppServices2.h"
#include "app/IniFile.h"
#include "app/SpecialFolders.h"
#include "misc/UtilitiesC.h"
#include "rz/StaticGetters.h"
#include "rz/cRZFile.h"
#include "ts/cTSAppCommonImpl.h"
#include "ts/cTSAppPropertyFiles.h"

#include <utility>

namespace {

uint32_t P(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

const char *Text(const cRZString *s) { return s ? s->mString.c_str() : "(null)"; }

// ---- the option store's slots (in the copy of each side's +0x44 vtable)

uint32_t __fastcall FakeAddOption(void *self, void *, uint32_t key, uint32_t alias, const char *text, uint32_t overrides, int32_t mask)
{
    Log("AddOption@%d(%x, %x, %s, %u, %x)", AppOff(self), key, alias, text ? text : "(null)", overrides, mask);
    return 0x5500;
}

// GetOptionText answers: per key, a text and al.
struct OptionAnswer {
    uint32_t key;
    std::string text;
    uint8_t al;
};
std::vector<OptionAnswer> gOptionTexts;
uint32_t __fastcall FakeGetOptionText(void *self, void *, uint32_t key, cRZString *out, uint32_t a, uint32_t b, int32_t c)
{
    Log("GetOptionText@%d(%x, %u, %u, %x)", AppOff(self), key, a, b, c);
    for (const OptionAnswer &o : gOptionTexts)
        if (o.key == key) {
            out->mString = o.text;
            return 0x6600u | o.al;
        }
    return 0x6600;
}
std::vector<std::string> gDirectories;
uint32_t __fastcall FakeGetOptionList(void *self, void *, uint32_t key, ts::OptionStringList *list, uint32_t a, uint32_t b, int32_t c)
{
    Log("GetOptionList@%d(%x, %u, %u, %x)", AppOff(self), key, a, b, c);
    for (const std::string &d : gDirectories) list->emplace_back(d.c_str());
    return 0x6601;
}
uint32_t __fastcall FakeRegisterHandlers(void *self, void *)
{
    Log("RegisterRegistryHandlers@%d", AppOff(self));
    return 0;
}

// ---- the services

struct FakeObject2 {
    void **vt;
    const char *name;
};
void *gFwVt[32], *gLanguageVt[32], *gDirectorVt[32], *gAllocatorVt[8], *gFileVt[8], *gImeVt[8];
FakeObject2 gFw{gFwVt, "fw"}, gLanguage{gLanguageVt, "language"}, gDirector{gDirectorVt, "director"};
FakeObject2 gAllocator{gAllocatorVt, "allocator"}, gImeA{gImeVt, "imeA"}, gImeB{gImeVt, "imeB"};
CmdLineDouble gCmd;

uint32_t __fastcall FwAddService(void *, void *, FakeObject2 *service)
{
    Log("fw.AddSystemService(%s)", service ? service->name : "null");
    return 1;
}
void *__fastcall FwCommandLine(void *, void *)
{
    Log("fw.CommandLine");
    return &gCmd;
}
std::string gLanguageName;
void __fastcall FakeLanguageName(void *, void *, cRZString *out, uint32_t b)
{
    Log("language.s15(%u)", b);
    out->mString = gLanguageName;
}
uint32_t gImeMask; // bit 0: first answers, bit 1: second answers, bits 2/3: with a null object
uint32_t __fastcall FakeDirectorGet(void *, void *, uint32_t id, uint32_t iid, void **out)
{
    Log("director.s3(%x, %x)", id, iid);
    const int which = id == 0x2ac4516b ? 0 : 1;
    if (!((gImeMask >> which) & 1)) return 0x100;
    *out = (gImeMask >> (2 + which)) & 1 ? nullptr : (which ? &gImeB : &gImeA);
    return 0x101;
}
void *__cdecl FakeDirectorObject()
{
    Log("GetDirectorFrameworkObject");
    return &gDirector;
}
uint32_t __fastcall FakeImeAddRef(void *self, void *)
{
    Log("%s.AddRef", static_cast<FakeObject2 *>(self)->name);
    return 2;
}
uint32_t __fastcall FakeImeRelease(void *self, void *)
{
    Log("%s.Release", static_cast<FakeObject2 *>(self)->name);
    return 1;
}
void *__fastcall FakeAllocate(void *, void *, uint32_t size) { return HeapAlloc(GetProcessHeap(), 0, size); }
void __fastcall FakeFree(void *, void *, void *p)
{
    if (p) HeapFree(GetProcessHeap(), 0, p);
}

// ---- the file system

std::vector<std::string> gExisting; // paths PathExists / DirectoryExists answer true for
bool Exists(const std::string &p)
{
    for (const std::string &e : gExisting)
        if (e == p) return true;
    return false;
}
uint32_t __cdecl FakePathExists(const cRZString *path)
{
    Log("PathExists(%s)", Text(path));
    return 0x7700u | (Exists(path->mString) ? 1 : 0);
}
uint32_t __cdecl FakeDirectoryExists(const cRZString *path)
{
    Log("DirectoryExists(%s)", Text(path));
    return 0x7700u | (Exists(path->mString) ? 1 : 0);
}
uint32_t __cdecl FakeAbsolute(cRZString *out, const cRZString *in)
{
    Log("AbsolutePathString(%s)", Text(in));
    out->mString = "D:\\abs\\" + in->mString;
    return 1;
}
uint32_t __cdecl FakeEnsure(cRZString *path)
{
    Log("EnsureDirectory(%s)", Text(path));
    return 1;
}
uint32_t __cdecl FakeSpecialFolder(int folder, cRZString *out, uint32_t create)
{
    Log("SpecialFolder(%d, %u)", folder, create);
    out->mString = folder == 8 ? "C:\\Users\\sim\\Documents" : "C:\\ProgramData\\";
    return 1;
}
std::string gProductName;
void __fastcall FakeProductName(void *self, void *, cRZString *out)
{
    Log("ProductName@%d(%s)", AppOff(self), Text(out));
    if (!gProductName.empty()) out->mString = gProductName;
}
uint32_t gBaseAnswer;
uint32_t __fastcall FakeBaseSetup(void *self, void *)
{
    Log("cTSAppCommonImpl::SetupUserDataDirectories@%d", AppOff(self));
    return 0x3300u | gBaseAnswer;
}
uint32_t __fastcall FakeReadResources(void *self, void *)
{
    Log("ReadResourceConfiguration@%d", AppOff(self));
    return 0x3301;
}
void *__fastcall FakeFileConstruct(void *self, void *, const char *path)
{
    Log("cRZFile(%s)", path);
    *static_cast<void ***>(self) = gFileVt;
    return self;
}
uint32_t __fastcall FakeFileOpen(void *, void *, uint32_t a, uint32_t b, uint32_t c)
{
    Log("file.Open(%u, %u, %u)", a, b, c);
    return 1;
}
uint32_t __fastcall FakeFileAddRef(void *, void *)
{
    Log("file.AddRef");
    return 2;
}
uint32_t __fastcall FakeFileRelease(void *, void *)
{
    Log("file.Release");
    return 1;
}
void __fastcall FakeUploader(void *self, void *, uint32_t a, uint32_t b, uint32_t c)
{
    Log("uploader@%d(%u, %u, %u)", AppOff(self), a & 0xff, b & 0xff, c & 0xff); // the callee reads bytes
}

// ---- the configuration object at +0x60

void *__fastcall FakeConfigConstruct(void *self, void *, uint32_t a)
{
    Log("config(%u)", a);
    return self;
}
uint8_t *gApp;
const char *IsConfig(void *self) { return self && self == *reinterpret_cast<void **>(gApp + 0x60) ? "config" : self ? "?" : "null"; }
void __fastcall FakeConfigA(void *self, void *) { Log("config.cb0f40(%s)", IsConfig(self)); }
void __fastcall FakeConfigB(void *self, void *) { Log("config.cb18a0(%s)", IsConfig(self)); }
void __fastcall FakeConfigC(void *self, void *) { Log("config.cb1790(%s)", IsConfig(self)); }
void __fastcall FakeWalk5(void *self, void *, uint32_t arg, void *obj)
{
    Log("walk5(%d, %d, %s)", AppOff(self), AppOff(reinterpret_cast<void *>(static_cast<uintptr_t>(arg))), obj ? "obj" : "null");
}

// Our cTSAppCommonImpl's own definition of a slot (the base our bodies call qualified), from a standalone object's
// vtable: a pointer to a virtual member would be a vcall thunk.
void *OurCommonImplSlot(int slot)
{
    alignas(8) static uint8_t memory[sizeof(ts::cTSAppCommonImpl)];
    static void **vt = [] {
        auto *p = ts::cTSAppCommonImplAdapters::Construct(memory, nullptr);
        void **v = *reinterpret_cast<void ***>(memory);
        ts::cTSAppCommonImplAdapters::Destruct(p, nullptr);
        return v;
    }();
    return vt[slot];
}

struct World {
    difftest::Context &ctx;
    std::vector<std::unique_ptr<Redirect>> redirects;
    std::vector<std::pair<void **, void *>> globals;
    explicit World(difftest::Context &c) : ctx(c)
    {
        gFwVt[3] = reinterpret_cast<void *>(&FwAddService);
        gFwVt[21] = reinterpret_cast<void *>(&FwCommandLine);
        gLanguageVt[15] = reinterpret_cast<void *>(&FakeLanguageName);
        gDirectorVt[3] = reinterpret_cast<void *>(&FakeDirectorGet);
        gImeVt[1] = reinterpret_cast<void *>(&FakeImeAddRef);
        gImeVt[2] = reinterpret_cast<void *>(&FakeImeRelease);
        gAllocatorVt[3] = reinterpret_cast<void *>(&FakeAllocate);
        gAllocatorVt[4] = reinterpret_cast<void *>(&FakeFree);
        gFileVt[1] = reinterpret_cast<void *>(&FakeFileAddRef);
        gFileVt[2] = reinterpret_cast<void *>(&FakeFileRelease);
        gFileVt[3] = reinterpret_cast<void *>(&FakeFileOpen);
        Set(0x10c9378, &gFw);
        Set(0x10f51c8, &gLanguage);
        Set(0x10c95d4, &gAllocator);
        Set(0x10c95d8, reinterpret_cast<void *>(static_cast<uintptr_t>(0x80000000u))); // the guard: initialised
        Add(c.image.At(0x1ae18d), &FakeDirectorObject);
        Add(reinterpret_cast<void *>(&GetDirectorFrameworkObject), &FakeDirectorObject);
        Add(c.image.At(0x1c1242), &FakePathExists);
        Add(reinterpret_cast<void *>(&cRZFile::PathExists), &FakePathExists);
        Add(c.image.At(0x1c4f94), &FakeDirectoryExists);
        Add(reinterpret_cast<void *>(&as2::DirectoryExists), &FakeDirectoryExists);
        Add(c.image.At(0x1c1760), &FakeAbsolute);
        Add(reinterpret_cast<void *>(&inifile::AbsolutePathString), &FakeAbsolute);
        Add(c.image.At(0x1c5031), &FakeEnsure);
        Add(reinterpret_cast<void *>(&folders::EnsureDirectory), &FakeEnsure); // ours (lane LY)
        Add(c.image.At(0x1c50d6), &FakeSpecialFolder);
        Add(reinterpret_cast<void *>(&folders::GetSpecialFolderPath), &FakeSpecialFolder);
        Add(c.image.At(0xc27a16), &FakeProductName);
        Add(MemberAddress<&ts::cTSAppFW::RegisteredProductName>(), &FakeProductName);
        Add(c.image.At(0xca8710), &FakeBaseSetup);
        Add(OurCommonImplSlot(15), &FakeBaseSetup); // a virtual member: its pointer would be a vcall thunk
        Add(c.image.At(0xca6c10), &FakeReadResources);
        Add(OurCommonImplSlot(16), &FakeReadResources);
        Add(c.image.At(0x1c0c8b), &FakeFileConstruct);
        Add(MemberAddress(static_cast<cRZFile *(cRZFile::*)(const char *)>(&cRZFile::Construct)), &FakeFileConstruct);
        Add(c.image.At(0xc28b7d), &FakeUploader);
        Add(MemberAddress<&ts::cTSAppFW::LaunchExceptionUploader>(), &FakeUploader); // ours (lane MB)
        Add(c.image.At(0xcb0890), &FakeConfigConstruct);
        Add(c.image.At(0xcb0f40), &FakeConfigA);
        Add(c.image.At(0xcb18a0), &FakeConfigB);
        Add(c.image.At(0xcb1790), &FakeConfigC);
        Add(MemberAddress(&ts::cTSAppPropertyFiles::Construct), &FakeConfigConstruct); // ours (lane MA)
        Add(MemberAddress(&ts::cTSAppPropertyFiles::SetupPaths), &FakeConfigA);
        Add(MemberAddress(&ts::cTSAppPropertyFiles::LoadProperties), &FakeConfigB);
        Add(MemberAddress(&ts::cTSAppPropertyFiles::LoadConfigProperties), &FakeConfigC);
        Add(c.image.At(0x1c0458), &FakeWalk5);
        Add(MemberAddress(&KeyMapSlot5Walker::CallSlot5ForEachKey), &FakeWalk5);
        gMoreSwitches.clear();
        for (const char *name : {"userDataMode", "activeUserDataDir", "templatedev", "nhdev", "activeAllUsersDataDir", "IME"})
            gMoreSwitches.push_back({name, Switch{}});
    }
    ~World()
    {
        while (!redirects.empty()) redirects.pop_back(); // in reverse (two of ours may share code)
        for (auto it = globals.rbegin(); it != globals.rend(); ++it) *it->first = it->second;
        gMoreSwitches.clear();
    }
    template <class F> void Add(void *at, F *fake) { redirects.push_back(std::make_unique<Redirect>(at, reinterpret_cast<void *>(fake))); }
    void Set(uint32_t rva, void *value)
    {
        void **slot = ctx.image.At<void *>(rva);
        globals.push_back({slot, *slot});
        *slot = value;
    }
};

Switch &More(const char *name)
{
    for (auto &s : gMoreSwitches)
        if (s.first == name) return s.second;
    return gMoreSwitches[0].second;
}

// A switch answered (al odd) with a value, or not (al 0), or al even and non-zero (the value written: the callers test al
// as a bool, so this counts as answered).
void Script(std::mt19937 &r, Switch &s, const std::string &value, int oneIn)
{
    s.al = static_cast<uint8_t>(r() % oneIn == 0 ? (r() | 1) : r() % 2 ? 0 : ((r() & 0xfe) | 2));
    s.value = value;
}

std::string RouteName(Route route) { return route == Route::Adapter ? "adapter" : "own vtable"; }

// The application's option cache (the forwarders') is the side's own +0x44 face for the call.
struct CommonCache {
    void **slot;
    void *saved;
    CommonCache(difftest::Context &ctx, void *face) : slot(ctx.image.At<void *>(0x10cc040)), saved(*slot) { *slot = face; }
    ~CommonCache() { *slot = saved; }
};

void FakeOptionSlots(LiveApp &app)
{
    app.SetSlot(4, 3, reinterpret_cast<const void *>(&FakeAddOption));
    app.SetSlot(4, 4, reinterpret_cast<const void *>(&FakeGetOptionText));
    app.SetSlot(4, 5, reinterpret_cast<const void *>(&FakeGetOptionList));
    app.SetSlot(4, 17, reinterpret_cast<const void *>(&FakeRegisterHandlers));
}

const char *const kPaths[] = {"", "C:/Games/TS2/TSData/Res/", "D:\\abs\\x", "C:/Users/sim/Documents/EA Games/The Sims 2/"};

} // namespace

// ---- 0xc2a7de SetupUserDataDirectories (cITSAppCommon slot 15)

DIFF_TEST(cTSAppFW_SetupUserDataDirectories, 0xc2a7de)
{
    Env env(ctx);
    SetUpCommandLine(ctx);
    LearnOurVTables();
    World world(ctx);
    for (Route route : {Route::OwnVTable, Route::Adapter}) {
        for (int run = 0; run < 300; ++run) {
            std::string results[2];
            for (int side = 0; side < 2; ++side) {
                std::mt19937 r(run * 5153 + static_cast<int>(route));
                const bool original = side == 0;
                LiveApp app(ctx, original, route, "The Sims 2");
                FakeOptionSlots(app);
                if (route == Route::Adapter) app.SetSlot(4, 15, reinterpret_cast<const void *>(&TrapNoArgs));
                *reinterpret_cast<uint32_t *>(app.raw + 0x70) = r();
                gBaseAnswer = run % 10 == 0 ? 0 : (r() | 1) & 0xff;
                gProductName = r() % 3 ? "The Sims 2 Legacy" : "";
                const char *const modes[] = {"install", "develop", "DEVELOP", "Install", "other", ""};
                Script(r, More("userDataMode"), modes[r() % 6], 2);
                gOptionTexts.clear();
                for (uint32_t key : {0xcce45144u, 0x2bcfce83u, 0xebcfce8eu, 0xade2e3bau, 0x4d4896bau})
                    gOptionTexts.push_back({key, kPaths[r() % 4], static_cast<uint8_t>(r() % 3 ? 1 : 0)});
                Script(r, More("activeUserDataDir"), r() % 3 ? std::string(kPaths[r() % 4]) : "C:/Games/TS2/TSData/Res/", 2);
                if (r() % 3 == 0) gOptionTexts[0].text = "D:\\abs\\" + More("activeUserDataDir").value; // equal after AbsolutePathString
                Script(r, More("templatedev"), "", 3);
                Script(r, More("nhdev"), "", 3);
                Script(r, More("activeAllUsersDataDir"), kPaths[r() % 4], 2);
                gExisting.clear();
                for (const char *p : kPaths)
                    if (r() % 2) gExisting.push_back(p);
                gLog.clear();
                uint32_t result;
                {
                    CommonCache cache(ctx, app.Face(4));
                    if (route == Route::Adapter)
                        result = original ? ctx.Original<uint32_t(__thiscall *)(void *)>(0xc2a7de)(app.Face(4))
                                          : ts::cTSAppFWAdapters::SetupUserDataDirectories(reinterpret_cast<ts::cTSAppCommonImpl *>(app.Face(4)), nullptr);
                    else
                        result = CallSlot<uint32_t>(app.Face(4), 15);
                }
                results[side] = Outcome(result, app.Digest());
            }
            ExpectSame(ctx, (RouteName(route) + " SetupUserDataDirectories " + std::to_string(run)).c_str(), results[0], results[1]);
        }
    }
}

// ---- 0xc290d8 PreFrameworkInit (cIGZApp slot 6)

DIFF_TEST(cTSAppFW_PreFrameworkInit, 0xc290d8)
{
    Env env(ctx);
    SetUpCommandLine(ctx);
    LearnOurVTables();
    World world(ctx);
    const char *const dirs[] = {"C:/Games/TS2/TSData/Res/UI/", "C:/Games/EP1/TSData/Res/UI/", "C:/Games/EP2/TSData/Res/UI/",
                                "a directory name long enough to need the heap/"};
    for (Route route : {Route::OwnVTable, Route::Adapter}) {
        for (int run = 0; run < 300; ++run) {
            std::string results[2];
            for (int side = 0; side < 2; ++side) {
                std::mt19937 r(run * 3301 + static_cast<int>(route));
                const bool original = side == 0;
                LiveApp app(ctx, original, route, "The Sims 2");
                gApp = app.raw;
                FakeOptionSlots(app);
                if (route == Route::Adapter) app.SetSlot(2, 6, reinterpret_cast<const void *>(&TrapNoArgs));
                gDirectories.clear();
                for (int n = static_cast<int>(r() % 4); n > 0; --n) gDirectories.push_back(dirs[r() % 4]);
                gExisting.clear();
                for (const char *d : dirs)
                    for (const char *name : {"eCAS.package", "uitext.package"})
                        if (r() % 2) gExisting.push_back(std::string(d) + name);
                gOptionTexts.clear();
                gOptionTexts.push_back({0x4d4896ba, r() % 4 ? "C:/Users/sim/Documents/EA Games/The Sims 2/Logs/" : "", 1});
                gSwitches[2].al = static_cast<uint8_t>(r() % 2 ? r() | 1 : r() & 0xfe); // noExceptionUpload
                gLanguageName = r() % 2 ? "English" : "Portuguese (Brazil)";
                const char *const imes[] = {"on", "ON", "enabled", "off", "Off", "disabled", "DISABLED", "maybe", ""};
                Script(r, More("IME"), imes[r() % 9], 2);
                gImeMask = r() & 0xf;
                gLog.clear();
                uint32_t result;
                {
                    CommonCache cache(ctx, app.Face(4));
                    if (route == Route::Adapter)
                        result = original ? ctx.Original<uint32_t(__thiscall *)(void *)>(0xc290d8)(app.Face(2))
                                          : ts::cTSAppFWAdapters::PreFrameworkInit(reinterpret_cast<cIGZApp *>(app.Face(2)), nullptr);
                    else
                        result = CallSlot<uint32_t>(app.Face(2), 6);
                }
                // The configuration block (the side's operator new): present or not, then freed by the side's delete.
                void *configuration = *reinterpret_cast<void **>(app.raw + 0x60);
                *reinterpret_cast<void **>(app.raw + 0x60) = nullptr;
                results[side] = Outcome(result, app.Digest()) + (configuration ? "config" : "no config") + " list " +
                                std::to_string(*reinterpret_cast<uint32_t *>(app.raw + 0x7c));
                // The reporter's words are heap words (masked in the digest): its directory and listeners by value.
                auto &reporter = reinterpret_cast<cTSAppFW *>(app.raw)->mReporter;
                results[side] += " reporter [" + reporter.mDirectory.mString + "] listeners";
                for (void *listener : reporter.mListeners) results[side] += " " + std::to_string(AppOff(listener));
                if (configuration) {
                    if (original) ctx.Original<void(__cdecl *)(void *, size_t)>(0x824624)(configuration, 0xf0);
                    else ::operator delete(configuration, 0xf0);
                }
            }
            ExpectSame(ctx, (RouteName(route) + " PreFrameworkInit " + std::to_string(run)).c_str(), results[0], results[1]);
        }
    }
}
