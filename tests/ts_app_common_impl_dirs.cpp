// Differential tests for the directory slots of ts::cTSAppCommonImpl (src/ts/cTSAppCommonImplDirs.cpp, lane EV). Our side:
// objects built by our constructor, slots called through our vtable; original side: the original constructor 0xca4cb0 and
// the game's vtable 0xeb5468. Each side's vtable is copied (the side's own slots) and patched: slot 13 (InitOptions,
// _purecall in both) answers the case's value, slot 3 (AddOption) logs its arguments and forwards to the side's own.
//
// Nothing real is read: the framework global 0x10c9378 is a fake whose command line (slot 21) answers the case's switches
// (cIGZCmdLine slot 8), the product paths 0xcb4690 (and ours) are redirected to a logging fake (the original calls it at that
// address), and the cached cITSAppCommon of the forwarders 0x1d0c00 / 0x1d0c60 (0x10cc040) is the side's own object, whose
// store the case fills with synthesised directories (missing, odd and ordinary paths).
#define NOMINMAX
#include "app_debugger_fakes.h"
#include "difftest.h"
#include "game/game.h"
#include "gz/cIGZString.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"
#include "ts/cTSAppCommonImpl.h"
#include "app/AppServices2.h"
#include "app/ProductInfo.h"
#include "app/IniFile.h"
#include "rz/cTSIniFile.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace {

using ts::cTSAppCommonImpl;
using Adapters = ts::cTSAppCommonImplAdapters;

constexpr uint32_t kCtor = 0xca4cb0, kDtor = 0xca4de0, kGameVTable = 0xeb5468;
constexpr uint32_t kSetupUserData = 0xca8710, kFindProductDirs = 0xca75c0, kReadResourceConfig = 0xca6c10;
constexpr uint32_t kRegistryLookup = 0xcb4690;
constexpr uint32_t kFrameworkGlobal = 0x10c9378, kCommonImplCache = 0x10cc040;
constexpr int kSlots = 18;
enum Slot { sAddOption = 3, sGetText = 4, sInitOptions = 13, sFindProductDirs = 14, sSetupDirs = 15, sReadResourceConfig = 16 };

std::string Fmt(const char *fmt, ...)
{
    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    return buffer;
}

template <typename R, typename... Args> R CallSlot(void *object, int slot, Args... args)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, Args...)>((*reinterpret_cast<void *const *const *>(object))[slot]);
    return fn(object, args...);
}

// The code stays writable after the restore: putting back the old protection VirtualProtect reported (the first page's)
// narrows both pages when the five bytes straddle a page boundary, and an earlier redirect on the second page then
// faults when it restores its bytes (lane MW, tests/app_fixture.h).
struct Redirect {
    uint8_t *site; uint8_t saved[5];
    Redirect(void *entry, void *target) : site(static_cast<uint8_t *>(entry))
    {
        std::memcpy(saved, site, 5);
        DWORD old; VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        int32_t relative = int32_t(reinterpret_cast<uintptr_t>(target) - reinterpret_cast<uintptr_t>(site) - 5);
        site[0] = 0xe9; std::memcpy(site + 1, &relative, 4);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    ~Redirect()
    {
        DWORD old; VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(site, saved, 5);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    Redirect(const Redirect &) = delete;
};

// ---- the case's world

std::string gLog;
uint32_t gInitAnswer = 1;
std::map<std::string, std::string> gSwitches;
const void *gOwnAddOption = nullptr;

// The adapters' runs (every fourth case): our side is a game-built object whose own slot under test is this trap, so an
// adapter that dispatched virtually (in game: into its own hook) shows.
uint32_t __fastcall Trap0(void *, void *)
{
    gLog += "TRAP ";
    return 0;
}

uint32_t __fastcall FakeInitOptions(void *, void *)
{
    gLog += "init ";
    return gInitAnswer;
}

uint32_t __fastcall RecordingAddOption(void *self, void *, uint32_t key, uint32_t alias, const char *text, uint32_t toOverrides, int32_t mask)
{
    gLog += Fmt("add(%x,%x,%s,%x,%x) ", key, alias, text ? Fmt("'%s'", text).c_str() : "null", toOverrides, uint32_t(mask));
    return reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, const char *, uint32_t, int32_t)>(gOwnAddOption)(self, key, alias, text, toOverrides, mask);
}

uint32_t __fastcall FakeIsSwitchPresent(void *, void *, cIGZString *name, cIGZString *parameter, uint32_t trim)
{
    std::string key = name->ToChar();
    gLog += Fmt("switch(%s,%x) ", key.c_str(), trim & 0xff);
    auto it = gSwitches.find(key);
    if (it == gSwitches.end()) return 0x12345600; // al false, junk above
    parameter->FromChar(it->second.c_str());
    return 0xabcdef01;
}

uint32_t __cdecl FakeRegistry(const char *exe, char *path, uint32_t *pathSize, char *version, uint32_t *versionSize, uint32_t a, uint32_t b, uint32_t c)
{
    gLog += Fmt("registry(%s,%s,%x,%s,%x,%x,%x,%x) ", exe, path && path[0] == 0 ? "empty" : "?", pathSize ? *pathSize : 0xdead,
                version ? (version[0] ? "?" : "empty") : "null", versionSize ? *versionSize : 0xdead, a, b, c);
    if (path && pathSize && *pathSize > 8) std::strcpy(path, "C:\\Reg\\");
    return 1;
}

struct FakeCmdLine {
    const void *const *vt;
    const void *slots[32];
    FakeCmdLine() : vt(slots)
    {
        for (auto &s : slots) s = nullptr;
        slots[8] = reinterpret_cast<const void *>(&FakeIsSwitchPresent);
    }
};
FakeCmdLine gCmdLine;
void *__fastcall FwCommandLine(void *, void *) { return &gCmdLine; }
struct FakeFramework {
    const void *const *vt;
    const void *slots[64];
    FakeFramework() : vt(slots)
    {
        for (auto &s : slots) s = nullptr;
        slots[21] = reinterpret_cast<const void *>(&FwCommandLine);
    }
};
FakeFramework gFramework;

const void *OurVTable()
{
    alignas(8) static uint8_t memory[sizeof(cTSAppCommonImpl)];
    static const void *vt = [] {
        auto *p = Adapters::Construct(memory, nullptr);
        const void *v = *reinterpret_cast<void **>(memory);
        Adapters::Destruct(p, nullptr);
        return v;
    }();
    return vt;
}

// One side's object with its patched vtable copy.
struct Object {
    difftest::Context &ctx;
    bool original;
    uint8_t *mem;
    const void *slots[kSlots];
    Object(difftest::Context &c, bool o) : ctx(c), original(o), mem(static_cast<uint8_t *>(::operator new(0x18)))
    {
        if (original) ctx.Original<void *(__thiscall *)(void *)>(kCtor)(mem);
        else Adapters::Construct(mem, nullptr);
        std::memcpy(slots, *reinterpret_cast<const void **>(mem), sizeof(slots));
        gOwnAddOption = slots[sAddOption];
        slots[sAddOption] = reinterpret_cast<const void *>(&RecordingAddOption);
        slots[sInitOptions] = reinterpret_cast<const void *>(&FakeInitOptions);
        *reinterpret_cast<const void **>(mem) = slots;
    }
    ~Object()
    {
        *reinterpret_cast<const void **>(mem) = original ? ctx.image.At(kGameVTable) : OurVTable();
        if (original) ctx.Original<void(__thiscall *)(void *)>(kDtor)(mem);
        else Adapters::Destruct(reinterpret_cast<cTSAppCommonImpl *>(mem), nullptr);
        ::operator delete(mem);
    }
    Object(const Object &) = delete;
    // Seeds the store through the side's own AddOption (not logged).
    void Add(uint32_t key, uint32_t alias, const char *text, uint32_t toOverrides, int32_t mask)
    {
        reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, const char *, uint32_t, int32_t)>(gOwnAddOption)(mem, key, alias, text, toOverrides, mask);
    }
};

const char *const kPaths[] = {
    "C:\\Program Files\\EA GAMES\\The Sims 2\\TSData\\", "C:/Games/Sims2/TSData/", "D:\\TS2\\TSData", "relative/dir/", "TSData",
    "\\\\server\\share\\Sims\\TSData\\", "/", "\\", "C:", "C:\\", "C:\\a b\\c.d\\", "x//y\\\\z//", "",
    "C:\\Very Long Directory Name That Is Over Sixteen\\And Another Long Component\\TSData\\",
};

std::string RandomPath(std::mt19937 &rng)
{
    if (rng() % 4) return kPaths[rng() % (sizeof(kPaths) / sizeof(kPaths[0]))];
    std::string s;
    unsigned n = rng() % 30;
    static const char alphabet[] = "abcXYZ019 ._-\\/:";
    for (unsigned i = 0; i < n; ++i) s += alphabet[rng() % (sizeof(alphabet) - 1)];
    return s;
}

} // namespace

// ---- 0xca8710 SetupUserDataDirectories (slot 15)

DIFF_TEST(CommonImplDirs_SetupUserDataDirectories, kSetupUserData)
{
    adt::GlobalOverride<uint32_t> framework(ctx, kFrameworkGlobal, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gFramework)));
    adt::GlobalOverride<uint32_t> cache(ctx, kCommonImplCache);
    Redirect registry(ctx.image.At(kRegistryLookup), reinterpret_cast<void *>(&FakeRegistry));
    Redirect registryOurs(reinterpret_cast<void *>(&product::GetProductPaths), reinterpret_cast<void *>(&FakeRegistry));
    std::mt19937 rng(kSetupUserData);
    for (unsigned i = 0; i < 400; ++i) {
        gInitAnswer = i % 13 == 0 ? 0 : (i % 13 == 1 ? 0x100 : (rng() % 2 ? 1 : 0xffffff01));
        gSwitches.clear();
        if (rng() % 3 == 0) gSwitches["TS2DownloadsDir"] = RandomPath(rng);
        if (rng() % 3 == 0) gSwitches["logDir"] = RandomPath(rng);
        if (rng() % 2) {
            static const char *modes[] = {"install", "INSTALL", "Install ", "documents", "", "installed"};
            gSwitches["userDataMode"] = modes[rng() % 6];
        }
        bool haveRoot = rng() % 5 != 0;
        std::string root = RandomPath(rng);
        uint32_t rootMask = rng() % 4 ? 0xffffffffu : (rng() % 2 ? 1u : 0x20000u);
        bool rootOverride = rng() % 3 == 0;
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            const bool viaAdapter = side == 1 && i % 4 == 3;
            Object o(ctx, side == 0 || viaAdapter);
            if (viaAdapter) o.slots[sSetupDirs] = reinterpret_cast<const void *>(&Trap0);
            if (haveRoot) o.Add(0x4bcfce5e, 0, root.c_str(), rootOverride, int32_t(rootMask));
            *ctx.image.At<uint32_t>(kCommonImplCache) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(o.mem));
            gLog.clear();
            uint32_t r = (viaAdapter ? uint32_t(Adapters::SetupUserDataDirectories(reinterpret_cast<cTSAppCommonImpl *>(o.mem), nullptr))
                                     : CallSlot<uint32_t>(o.mem, sSetupDirs)) & 0xff;
            results[side] = Fmt("%x | ", r) + gLog;
        }
        ++ctx.cases;
        if (results[0] != results[1]) ctx.Fail("case %u (root '%s'):\n  original: %s\n  ours:     %s", i, root.c_str(), results[0].c_str(), results[1].c_str());
    }
    (void)kFindProductDirs; (void)sGetText; (void)sFindProductDirs;
}

// ---- 0xca6c10 ReadResourceConfiguration (slot 16), with its section callbacks 0xca74b0, 0xca7020, 0xca7130

namespace {

constexpr uint32_t kIniProcessSection = 0x9c5ef2, kPathExists = 0x1c1242;
constexpr uint32_t kWordResourceFactories = 0x1085efc, kWordDBSegments = 0x1085ef8;
constexpr uint32_t kCallbacks[] = {0xca74b0, 0xca7020, 0xca7130};

std::vector<std::string> gExisting;
std::map<std::string, std::vector<std::pair<std::string, std::string>>> gSections;
uint32_t gMakeAnswer = 1;
const uint8_t *gContext = nullptr;

bool __cdecl FakePathExists(const cIGZString &path)
{
    std::string p = path.ToChar();
    gLog += Fmt("exists(%s) ", p.c_str());
    for (const std::string &e : gExisting)
        if (e == p) return true;
    return false;
}

struct FakeString {
    union {
        cRZString s;
    };
    explicit FakeString(const std::string &text) { s.Construct(text.c_str()); }
    ~FakeString() { s.Destruct(); }
};

uint32_t __fastcall FakeProcessSection(void *ini, void *, const cIGZString *section, void (__cdecl *callback)(cIGZString *, cIGZString *, void *), void *context)
{
    const cRZString *path = reinterpret_cast<const cRZString *>(static_cast<uint8_t *>(ini) + 4);
    std::string name = section->ToChar();
    gLog += Fmt("section(%s, path '%s', context %+d) ", name.c_str(), path->mString.c_str(), int(static_cast<const uint8_t *>(context) - gContext));
    for (const auto &kv : gSections[name]) {
        FakeString key(kv.first), value(kv.second);
        callback(key.s.AsInterface(), value.s.AsInterface(), context);
    }
    return 1;
}

uint32_t __fastcall SvcMakeEntry(void *, void *, uint32_t table, cIGZString *key, cIGZString *value, cIGZString *entry)
{
    std::string t = table == 0x11110001 ? "RF" : table == 0x22220002 ? "DBS" : Fmt("text '%s'", reinterpret_cast<const char *>(uintptr_t(table)));
    gLog += Fmt("make(%s, %s=%s, entry '%s') ", t.c_str(), key->ToChar(), value->ToChar(), entry->ToChar());
    entry->FromChar(Fmt("%s|%s|a long enough entry to leave the small buffer", key->ToChar(), value->ToChar()).c_str());
    return gMakeAnswer;
}
uint32_t __fastcall SvcAdd(void *, void *, cIGZString *entry)
{
    gLog += Fmt("add('%s') ", entry->ToChar());
    return 0x5a5a5a01;
}
uint32_t __fastcall SvcRelease(void *, void *)
{
    gLog += "release ";
    return 0;
}
struct FakeService {
    const void *const *vt;
    const void *slots[16];
    FakeService() : vt(slots)
    {
        for (auto &s : slots) s = nullptr;
        slots[2] = reinterpret_cast<const void *>(&SvcRelease);
        slots[3] = reinterpret_cast<const void *>(&SvcAdd);
        slots[6] = reinterpret_cast<const void *>(&SvcMakeEntry);
    }
};
FakeService gService;
uint32_t __fastcall FwGetSystemService(void *, void *, uint32_t id, uint32_t iid, void **out)
{
    gLog += Fmt("service(%x,%x,%s) ", id, iid, *out ? "set" : "null");
    *out = &gService;
    return 1;
}

} // namespace

DIFF_TEST(CommonImplDirs_ReadResourceConfiguration, kReadResourceConfig)
{
    gFramework.slots[5] = reinterpret_cast<const void *>(&FwGetSystemService);
    adt::AllocatorFake allocator(ctx); // the option lists' nodes and strings (allocator service)
    adt::GlobalOverride<uint32_t> framework(ctx, kFrameworkGlobal, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gFramework)));
    adt::GlobalOverride<uint32_t> cache(ctx, kCommonImplCache);
    adt::GlobalOverride<uint32_t> wordA(ctx, kWordResourceFactories, 0x11110001), wordB(ctx, kWordDBSegments, 0x22220002);
    Redirect process(ctx.image.At(kIniProcessSection), reinterpret_cast<void *>(&FakeProcessSection));
    // Ours calls our section walk (cRZIniFile::ForEachKey, 0x9c5ef2's replacement): the same fake.
    auto forEachKey = &cRZIniFile::ForEachKey;
    void *ourForEachKey;
    std::memcpy(&ourForEachKey, &forEachKey, sizeof(ourForEachKey));
    Redirect processOurs(ourForEachKey, reinterpret_cast<void *>(&FakeProcessSection));
    Redirect existsGame(ctx.image.At(kPathExists), reinterpret_cast<void *>(&FakePathExists));
    Redirect existsOurs(reinterpret_cast<void *>(&cRZFile::PathExists), reinterpret_cast<void *>(&FakePathExists));
    std::mt19937 rng(kReadResourceConfig);
    static const char *sectionNames[] = {"ResourceFactories", "DBSegments", "DBSegmentDirectoryFiles"};
    for (unsigned i = 0; i < 300; ++i) {
        std::vector<std::string> lists[2];
        gExisting.clear();
        for (auto &l : lists) {
            unsigned n = rng() % 4;
            for (unsigned k = 0; k < n; ++k) {
                l.push_back(RandomPath(rng) + (rng() % 2 ? "ReleaseResource.ini" : "Default.ini"));
                if (rng() % 3 == 0) gExisting.push_back(l.back());
            }
        }
        gSections.clear();
        for (const char *s : sectionNames) {
            unsigned n = rng() % 3;
            for (unsigned k = 0; k < n; ++k) gSections[s].push_back({Fmt("key%u", unsigned(rng() % 100)), RandomPath(rng)});
        }
        gMakeAnswer = rng() % 2 ? 1 : (rng() % 2 ? 0 : 0x100);
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            const bool viaAdapter = side == 1 && i % 4 == 3;
            Object o(ctx, side == 0 || viaAdapter);
            if (viaAdapter) o.slots[sReadResourceConfig] = reinterpret_cast<const void *>(&Trap0);
            for (const std::string &p : lists[0]) o.Add(0x0be7523c, 0, p.c_str(), 0, -1);
            for (const std::string &p : lists[1]) o.Add(0x4be75253, 0, p.c_str(), 0, -1);
            *ctx.image.At<uint32_t>(kCommonImplCache) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(o.mem));
            gContext = o.mem;
            gLog.clear();
            uint32_t r = (viaAdapter ? uint32_t(Adapters::ReadResourceConfiguration(reinterpret_cast<cTSAppCommonImpl *>(o.mem), nullptr))
                                     : CallSlot<uint32_t>(o.mem, sReadResourceConfig)) & 0xff;
            results[side] = Fmt("%x | ", r) + gLog;
        }
        allocator.Release();
        ++ctx.cases;
        if (results[0] != results[1]) ctx.Fail("case %u:\n  original: %s\n  ours:     %s", i, results[0].c_str(), results[1].c_str());
    }
    gFramework.slots[5] = nullptr;
    (void)kCallbacks;
}

// ---- 0xca75c0 FindProductDirectories (slot 14) and 0xca6910 HasFlags (lane FC)
//
// Every callee that reads something real is a fake on both sides: the file-system service's face (global 0x10f51c4: slot 4
// the executable's directory, slot 19 SetDataDirectory logged), the product paths 0xcb4690 and the installed
// products 0xcb4bb0 (fakes at those addresses and over ours; the product list is allocated here and every free
// of its blocks is logged through the game's free 0x831a54 and our CRT's free), PathExists, DirectoryExists and
// AbsolutePathString (original and ours: deterministic answers from a per-case hash of the path), the cTSIniFile
// constructor, GetString and destructor (original and ours: a fake ini whose 'Directories' answers come from the case),
// the command line (the case's switches) and the cached cITSAppCommon of the forwarder 0x1d0c00 (the side's own object,
// holding the product ini's path, option 0x6c253729). The system code page (0xff23a4) is UTF-8, so ConvertBytesToString
// copies.

namespace {

constexpr uint32_t kHasFlags = 0xca6910, kInstalledProducts = 0xcb4bb0, kGameFree = 0x831a54;
constexpr uint32_t kDirectoryExists = 0x1c4f94, kAbsolutePath = 0x1c1760;
constexpr uint32_t kIniConstruct = 0xcb2a20, kIniGetString = 0xcb2e90, kIniDestruct = 0xcb2cc0;
constexpr uint32_t kFileSystemGlobal = 0x10f51c4, kCodePageGlobal = 0xff23a4;

template <typename T> void *MemberEntry(T member)
{
    static_assert(sizeof(T) == sizeof(void *), "single inheritance");
    void *p;
    std::memcpy(&p, &member, sizeof(p));
    return p;
}

uint32_t gSalt = 0;
bool Answer(const std::string &path, uint32_t bias)
{
    uint32_t h = 2166136261u ^ gSalt;
    for (unsigned char c : path) h = (h ^ c) * 16777619u;
    return (h >> 7) % 4 < bias; // bias of 4
}
uint32_t gExistsBias = 2, gDirectoryBias = 2;

bool __cdecl HashPathExists(const cIGZString &path)
{
    std::string p = path.ToChar();
    bool r = Answer(p, gExistsBias);
    gLog += Fmt("exists(%s)=%d ", p.c_str(), int(r));
    return r;
}
bool __cdecl HashDirectoryExists(cRZString *path)
{
    bool r = Answer("dir:" + path->mString, gDirectoryBias);
    gLog += Fmt("dirExists(%s)=%d ", path->mString.c_str(), int(r));
    return r;
}
// "x/y/../" -> "x/", other texts kept; a quarter fail (the string untouched).
bool __cdecl FakeAbsolutePath(cRZString *out, const cRZString *in)
{
    std::string p = in->mString;
    gLog += Fmt("abs(%s) ", p.c_str());
    if (Answer("abs:" + p, 1)) return false;
    if (p.size() >= 3 && p.compare(p.size() - 3, 3, "../") == 0) {
        p.resize(p.size() - 3);
        if (!p.empty() && p.back() == '/') p.pop_back();
        size_t cut = p.find_last_of('/');
        p = cut == std::string::npos ? std::string("Z:/") : p.substr(0, cut + 1);
    }
    if (Answer("backslash:" + p, 2)) // the real MakeAbsolutePath writes backslashes
        for (char &c : p)
            if (c == '/') c = '\\';
    out->mString = p;
    return true;
}

// The file-system face.
std::string gExecutableDir;
const char *__fastcall FsAppDirectory(void *, void *)
{
    gLog += "appDir ";
    return gExecutableDir.c_str();
}
uint32_t __fastcall FsSetDataDirectory(void *, void *, const cIGZString *path)
{
    gLog += Fmt("setData(%s) ", path->ToChar());
    return 0x77777701;
}
struct FakeFileSystem {
    const void *const *vt;
    const void *slots[24];
    FakeFileSystem() : vt(slots)
    {
        for (auto &s : slots) s = nullptr;
        slots[4] = reinterpret_cast<const void *>(&FsAppDirectory);
        slots[19] = reinterpret_cast<const void *>(&FsSetDataDirectory);
    }
};
FakeFileSystem gFileSystem;

// The install registration (0xcb4690): the products' registry key and the base game's directory, its size as given.
std::string gRegistryKey, gBaseDir;
int gBaseSizeMode = 0; // 0 strlen, 1 strlen + 1, 2 zero, 3 left at the buffer size
uint32_t __cdecl FakeRegistration(const char *exe, char *key, uint32_t *keySize, char *base, uint32_t *baseSize, uint32_t a, uint32_t b, uint32_t c)
{
    gLog += Fmt("registration(%s,%s,%x,%s,%x,%x,%x,%x) ", exe, key && !key[0] ? "empty" : "?", keySize ? *keySize : 0xdead,
                base && !base[0] ? "empty" : "?", baseSize ? *baseSize : 0xdead, a, b, c);
    std::strcpy(key, gRegistryKey.c_str());
    *keySize = uint32_t(gRegistryKey.size());
    std::strcpy(base, gBaseDir.c_str());
    const uint32_t sizes[] = {uint32_t(gBaseDir.size()), uint32_t(gBaseDir.size() + 1), 0, *baseSize};
    *baseSize = sizes[gBaseSizeMode];
    return 0x5501;
}

// The installed products (0xcb4bb0): blocks from the process heap (the game's free and ours both release there).
bool gListOk = false;
std::vector<std::pair<bool, std::string>> gListed; // (present, text)
void *gBlocks[64];
int gBlockCount = 0;
char gFreeText[2048];
size_t gFreeTextLength = 0;
uint32_t __cdecl FakeInstalledProducts(const char *key, char ***out, int32_t *count)
{
    gLog += Fmt("installed(%s,%s) ", key, *out ? "set" : "null");
    gBlockCount = 0;
    if (!gListOk) {
        *out = nullptr;
        *count = 0;
        return 0x12345600;
    }
    auto **list = static_cast<char **>(HeapAlloc(GetProcessHeap(), 0, gListed.size() * 4 + 4));
    gBlocks[gBlockCount++] = list;
    for (size_t i = 0; i < gListed.size(); ++i) {
        list[i] = nullptr;
        if (!gListed[i].first) continue;
        list[i] = static_cast<char *>(HeapAlloc(GetProcessHeap(), 0, gListed[i].second.size() + 1));
        std::strcpy(list[i], gListed[i].second.c_str());
        gBlocks[gBlockCount++] = list[i];
    }
    *out = list;
    *count = int32_t(gListed.size());
    return 0x12345601;
}
// Both frees while redirected: nothing here may allocate or free through the CRT.
void __cdecl RecordingCrtFree(void *p)
{
    if (!p) return;
    for (int i = 0; i < gBlockCount; ++i) {
        if (gBlocks[i] != p || gFreeTextLength >= sizeof(gFreeText) - 16) continue;
        gFreeTextLength += size_t(std::snprintf(gFreeText + gFreeTextLength, sizeof(gFreeText) - gFreeTextLength, "free%d ", i));
        gBlocks[i] = nullptr;
    }
    HeapFree(GetProcessHeap(), 0, p);
}

// The product ini (cTSIniFile): construction, lookups and destruction logged; answers from the case.
std::map<std::string, std::string> gIniValues;
void *__fastcall FakeIniConstruct(void *self, void *, const cIGZString *path, const cRZString *extra)
{
    gLog += Fmt("ini(%s,%s) ", path->ToChar(), extra ? "extra" : "null");
    return self;
}
uint32_t __fastcall FakeIniGetString(void *, void *, const cIGZString *section, const cIGZString *key, cIGZString *out)
{
    std::string k = key->ToChar();
    gLog += Fmt("get(%s,%s,'%s') ", section->ToChar(), k.c_str(), out->ToChar());
    auto it = gIniValues.find(k);
    if (it == gIniValues.end()) return 0x4400;
    out->FromChar(it->second.c_str());
    return 0x4401;
}
void __fastcall FakeIniDestruct(void *, void *) { gLog += "~ini "; }

const char *const kExecutables[] = {
    "C:\\Program Files\\EA GAMES\\The Sims 2\\TSBin\\", "C:/Games/Sims2/Fun with Pets/SP9/TSBin/", "D:\\dev\\out\\bin\\",
    "D:/dev/out/", "E:/build/OUT/x/", "C:/a/out/b/out/c/", "relative/TSBin/", "", "/", "C:", "C:\\Very Long Install Path\\With Spaces\\TSBin\\",
    "\\\\server\\share\\Sims\\TSBin\\", "C:/outlet/out", "out/", "x//y\\\\z//",
};
const char *const kEpLists[] = {
    "1,2,3", "17", "3,12,5", "123,4", "0,31,32,-1", "", ",,", "a,b", "1 ,2", " 7", "9", "1,1,1", "12,3", "5,", ",6",
    "2;3", "8,aaaaaaaaaaaaaaaaaaaaaa,4", "4444444444444444444,1", "1,2,3,4,5,6,7,8,9", "0x5,05,+3",
};

} // namespace

DIFF_TEST(CommonImplDirs_FindProductDirectories, kFindProductDirs)
{
    adt::GlobalOverride<uint32_t> framework(ctx, kFrameworkGlobal, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gFramework)));
    adt::GlobalOverride<uint32_t> fileSystem(ctx, kFileSystemGlobal, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gFileSystem)));
    adt::GlobalOverride<int32_t> codePage(ctx, kCodePageGlobal, 8); // UTF-8, the default encoding: no conversion
    adt::GlobalOverride<uint32_t> cache(ctx, kCommonImplCache);
    Redirect registration(ctx.image.At(kRegistryLookup), reinterpret_cast<void *>(&FakeRegistration));
    Redirect installed(ctx.image.At(kInstalledProducts), reinterpret_cast<void *>(&FakeInstalledProducts));
    Redirect registrationOurs(reinterpret_cast<void *>(&product::GetProductPaths), reinterpret_cast<void *>(&FakeRegistration));
    Redirect installedOurs(reinterpret_cast<void *>(&product::InstalledProductDirectories), reinterpret_cast<void *>(&FakeInstalledProducts));
    Redirect existsGame(ctx.image.At(kPathExists), reinterpret_cast<void *>(&HashPathExists));
    Redirect existsOurs(reinterpret_cast<void *>(&cRZFile::PathExists), reinterpret_cast<void *>(&HashPathExists));
    Redirect dirGame(ctx.image.At(kDirectoryExists), reinterpret_cast<void *>(&HashDirectoryExists));
    Redirect dirOurs(reinterpret_cast<void *>(&as2::DirectoryExists), reinterpret_cast<void *>(&HashDirectoryExists));
    Redirect absGame(ctx.image.At(kAbsolutePath), reinterpret_cast<void *>(&FakeAbsolutePath));
    Redirect absOurs(reinterpret_cast<void *>(&inifile::AbsolutePathString), reinterpret_cast<void *>(&FakeAbsolutePath));
    Redirect iniCtorGame(ctx.image.At(kIniConstruct), reinterpret_cast<void *>(&FakeIniConstruct));
    Redirect iniCtorOurs(MemberEntry(&cTSIniFile::Construct), reinterpret_cast<void *>(&FakeIniConstruct));
    Redirect iniGetGame(ctx.image.At(kIniGetString), reinterpret_cast<void *>(&FakeIniGetString));
    Redirect iniGetOurs(MemberEntry(&cTSIniFile::GetString), reinterpret_cast<void *>(&FakeIniGetString));
    Redirect iniDtorGame(ctx.image.At(kIniDestruct), reinterpret_cast<void *>(&FakeIniDestruct));
    Redirect iniDtorOurs(MemberEntry(&cTSIniFile::Destruct), reinterpret_cast<void *>(&FakeIniDestruct));
    std::mt19937 rng(kFindProductDirs);
    static const uint32_t kFlagWords[] = {1, 3, 0x80000001, 0xffffffff, 2, 0x80000000, 0xfffffffe};
    for (unsigned i = 0; i < 600; ++i) {
        const uint32_t flags = i % 6 == 5 ? kFlagWords[rng() % 7] : (rng() % 8 == 0 ? 2u : 0u);
        gSalt = rng();
        gExistsBias = rng() % 5;    // 0 (nothing exists) .. 4 (everything)
        gDirectoryBias = rng() % 5;
        gExecutableDir = rng() % 3 ? kExecutables[rng() % (sizeof(kExecutables) / sizeof(kExecutables[0]))] : RandomPath(rng);
        gSwitches.clear();
        const unsigned epMode = rng() % 4;
        if (epMode == 1) gSwitches["ep"] = kEpLists[rng() % (sizeof(kEpLists) / sizeof(kEpLists[0]))];
        if (epMode == 2) {
            std::string list;
            for (unsigned n = rng() % 6; n; --n) list += Fmt("%u,", unsigned(rng() % 35));
            gSwitches["ep"] = list;
        }
        for (unsigned n = rng() % 4; n; --n) gSwitches[Fmt("ep%udir", unsigned(rng() % 33))] = rng() % 4 ? RandomPath(rng) : "";
        if (rng() % 4 == 0) gSwitches["ep17dir"] = RandomPath(rng);
        if (rng() % 6 == 0) gSwitches["ep0dir"] = RandomPath(rng);
        gIniValues.clear();
        for (unsigned n = rng() % 5; n; --n) gIniValues[Fmt("ep%udir", unsigned(rng() % 33))] = rng() % 5 ? RandomPath(rng) : "";
        const bool haveIniPath = rng() % 3 != 0;
        const std::string iniPath = RandomPath(rng) + "Products.ini";
        const uint32_t iniPathOverride = rng() % 2;
        gRegistryKey = rng() % 4 ? "Software\\Electronic Arts\\The Sims 2 Ultimate Collection 25\\" : "";
        gBaseDir = rng() % 4 ? RandomPath(rng) : "";
        gBaseSizeMode = int(rng() % 4);
        gListOk = rng() % 4 != 0;
        gListed.clear();
        const unsigned listSize = rng() % 3 == 0 ? rng() % 40 : rng() % 20;
        for (unsigned k = 0; k < listSize; ++k) {
            const unsigned kind = rng() % 6;
            gListed.push_back({kind != 0, kind == 1 ? std::string() : RandomPath(rng) + (kind == 2 ? "\xe9t\xe9/" : "")});
        }
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            const bool viaAdapter = side == 1 && i % 4 == 3;
            Object o(ctx, side == 0 || viaAdapter);
            if (viaAdapter) o.slots[sFindProductDirs] = reinterpret_cast<const void *>(&Trap0);
            *reinterpret_cast<uint32_t *>(o.mem + 0x14) = flags;
            if (haveIniPath) o.Add(0x6c253729, 0, iniPath.c_str(), iniPathOverride, -1);
            *ctx.image.At<uint32_t>(kCommonImplCache) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(o.mem));
            gLog.clear();
            gFreeTextLength = 0;
            uint32_t r;
            {
                Redirect freeGame(ctx.image.At(kGameFree), reinterpret_cast<void *>(&RecordingCrtFree));
                Redirect freeOurs(reinterpret_cast<void *>(&::free), reinterpret_cast<void *>(&RecordingCrtFree));
                r = (viaAdapter ? uint32_t(Adapters::FindProductDirectories(reinterpret_cast<cTSAppCommonImpl *>(o.mem), nullptr))
                                : CallSlot<uint32_t>(o.mem, sFindProductDirs)) & 0xff;
            }
            int leaked = 0;
            for (int b = 0; b < gBlockCount; ++b) {
                if (gBlocks[b]) HeapFree(GetProcessHeap(), 0, gBlocks[b]), ++leaked;
            }
            gBlockCount = 0;
            results[side] = Fmt("%x | ", r) + gLog + "| " + std::string(gFreeText, gFreeTextLength) + Fmt("| leaked %d", leaked);
        }
        ++ctx.cases;
        if (results[0] != results[1]) {
            size_t at = 0;
            while (at < results[0].size() && at < results[1].size() && results[0][at] == results[1][at]) ++at;
            at = at > 60 ? at - 60 : 0;
            ctx.Fail("case %u (flags %x, exe '%s'), from offset %u:\n  original: %.300s\n  ours:     %.300s", i, flags, gExecutableDir.c_str(),
                     unsigned(at), results[0].c_str() + at, results[1].c_str() + at);
        }
    }
}

DIFF_TEST(CommonImplDirs_HasFlags, kHasFlags)
{
    std::mt19937 rng(kHasFlags);
    for (unsigned i = 0; i < 400; ++i) {
        const uint32_t flags = rng() % 3 ? rng() : (rng() % 2 ? 0u : 1u << (rng() % 32));
        const uint32_t mask = rng() % 3 ? rng() : (rng() % 2 ? 0u : 1u << (rng() % 32));
        alignas(8) uint8_t object[0x18] = {};
        *reinterpret_cast<uint32_t *>(object + 0x14) = flags;
        const uint32_t original = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(kHasFlags)(object, mask) & 0xff;
        const uint32_t ours = i % 2 ? uint32_t(Adapters::HasFlags(reinterpret_cast<cTSAppCommonImpl *>(object), nullptr, mask))
                                    : uint32_t(reinterpret_cast<cTSAppCommonImpl *>(object)->HasFlags(mask));
        ++ctx.cases;
        if (original != ours) ctx.Fail("case %u: flags %x mask %x: original %u, ours %u", i, flags, mask, original, ours);
    }
}
