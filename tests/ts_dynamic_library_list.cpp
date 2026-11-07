// Differential test for DynamicLibraryList (src/ts/DynamicLibraryList.size.cpp), lane LV: the plug-in library list
// cTSAppFW::AddDynamicLibrariesHere builds. Side A is built, used and destroyed by the original members, side B by ours
// (the hooks' adapters), on the same random scripts.
//
// The world is stand-ins on both sides (shape::StandIn: the image's function and ours): the ini file cTSIniFile
// (constructor 0xcb2a20, destructor 0xcb2cc0, HasSectionPrefixed 0xcb30f0, SectionWalk 0xcb2d00: scripted sections
// whose walk calls the callback it is given with scripted lines), cRZFile::PathExists 0x1c1242, cRZVersion (0x1c98ab,
// GetFileVersion 0x1c9b6d, 0x1c9901), GetDirectorFrameworkObject 0x1ae18d (a COM double whose AddLibrary, slot 5,
// answers from the script), the option getter 0x1d0c00, CombinePathString 0x1c17bd and cRZFileName (0x1c7ef4,
// 0x1c0eb2: the real one completes relative paths from the working directory, an import). Every call is logged with
// the texts of its string arguments. The cRZString helpers (copy, lower case, the comparisons through the strings'
// vtables) run for real (the image's on side A, ours on side B). The game's operator new / sized delete are ours for the test, so both sides' containers live on one heap.
//
// Compared after every call: the result (al), the log, and the list read from memory (the vector's entries with their
// string words, required and loaded bytes, its count and capacity; both maps in order with their sizes). Report
// 0xcb3a80's dropped lines (a local string assigned and appended per entry, then freed: no other effect) are not
// compared beyond the state.
#include "code_patch.h"
#include "difftest.h"
#include "game/game.h"
#include "gz/cIGZString.h"
#include "rz/cRZString.h"
#include "rz/cRZFile.h"
#include "rz/cRZFileName.h"
#include "rz/cTSIniFile.h"
#include "rz/StaticGetters.h"
#include "app/AppInit.h"
#include "app/IniFile.h"
#include "app/ReportFinish.h"
#include "ts/DynamicLibraryList.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr uint32_t kConstruct = 0xcb3640;
constexpr uint32_t kDestruct = 0xcb36f0;
constexpr uint32_t kAdd = 0xcb3740;
constexpr uint32_t kReadConfiguration = 0xcb3870;
constexpr uint32_t kReport = 0xcb3a80;
constexpr uint32_t kLoadAll = 0xcb3b70;
constexpr uint32_t kRequireVersion = 0xcb3e10;
constexpr uint32_t kGameNew = 0x824632;
constexpr uint32_t kGameSizedDelete = 0x824624;

void *__cdecl GameNew(size_t n)
{
    return ::operator new(n);
}
void __cdecl GameDelete(void *p, size_t n)
{
    ::operator delete(p, n);
}

// ---- the log

std::vector<std::string> gLog;
void Log(const char *fmt, ...)
{
    char line[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    gLog.emplace_back(line);
}

const char *Text(const void *s)
{
    return s ? static_cast<const cRZString *>(s)->mString.c_str() : "(null)";
}

const void *gList = nullptr; // the list under test (the walks' context)

// ---- scripted answers (the same queue on both sides)

std::deque<uint32_t> gAnswers;
uint32_t Next(uint32_t fallback)
{
    if (gAnswers.empty()) return fallback;
    const uint32_t v = gAnswers.front();
    gAnswers.pop_front();
    return v;
}
std::deque<std::string> gVersionTexts;
std::string gDirectoryOption;

// ---- the ini file

bool gHasConfigurations, gHasLibraries;
std::vector<std::pair<std::string, std::string>> gConfigurationLines, gLibraryLines;

void *__fastcall IniConstruct(void *self, void *, const cIGZString *path, const cRZString *extra)
{
    Log("ini(%s, %s)", Text(path), Text(extra));
    return self;
}
void __fastcall IniDestruct(void *, void *)
{
    Log("ini.Destruct");
}
uint32_t __fastcall IniHas(void *, void *, const cIGZString *section)
{
    const std::string name = Text(section);
    Log("ini.Has(%s)", name.c_str());
    const bool has = name == "Configurations" ? gHasConfigurations : name == "DLLs" ? gHasLibraries : false;
    return 0x6600u | (has ? 1u : 0u);
}
uint32_t __fastcall IniWalk(void *, void *, const cIGZString *section, void *callback, void *context)
{
    const std::string name = Text(section);
    Log("ini.Walk(%s, %s)", name.c_str(), context == gList ? "list" : "other");
    const auto &lines = name == "Configurations" ? gConfigurationLines : gLibraryLines;
    using Callback = void(__cdecl *)(const cRZString *, const cRZString *, void *);
    for (const auto &line : lines) {
        alignas(cRZString) unsigned char k[sizeof(cRZString)], v[sizeof(cRZString)];
        cRZString *key = reinterpret_cast<cRZString *>(k);
        cRZString *value = reinterpret_cast<cRZString *>(v);
        key->Construct(line.first.c_str());
        value->Construct(line.second.c_str());
        reinterpret_cast<Callback>(callback)(key, value, context);
        value->Destruct();
        key->Destruct();
    }
    return 0x6700u | 1u;
}

// ---- files, versions, options, paths

uint32_t __cdecl FakePathExists(const cIGZString *path)
{
    Log("PathExists(%s)", Text(path));
    return Next(0x8800u);
}
void *__fastcall VersionConstruct(void *self, void *, const cRZString *path)
{
    Log("version(%s)", Text(path));
    return self;
}
uint32_t __fastcall VersionGetFileVersion(void *, void *, cRZString *out)
{
    std::string text = gVersionTexts.empty() ? std::string("0.0.0.0") : gVersionTexts.front();
    if (!gVersionTexts.empty()) gVersionTexts.pop_front();
    Log("version.GetFileVersion -> %s", text.c_str());
    out->mString = text;
    return 0x5501;
}
void __fastcall VersionDestruct(void *, void *)
{
    Log("version.Destruct");
}
uint32_t __cdecl FakeGetOption(uint32_t key, uint32_t out, uint32_t c, uint32_t d, uint32_t e)
{
    auto *s = reinterpret_cast<cRZString *>(static_cast<uintptr_t>(out));
    Log("GetOption(%x, [%s], %u, %u, %x)", key, Text(s), c, d, e);
    s->mString = gDirectoryOption;
    return 0x7701;
}
uint32_t __cdecl FakeCombine(cRZString *out, const cRZString *in, const cRZString *base)
{
    Log("Combine([%s], %s, %s)", Text(out), Text(in), Text(base));
    out->mString = (base ? base->mString : std::string()) + "|" + in->mString;
    return 0x4401;
}

// cRZFileName (0xa0 bytes): the stand-in fills only the name the list reads (+0x40, GetName): the text after the last
// separator up to its last dot. The real one completes relative paths from the working directory (an import).
constexpr uint32_t kFileNameName = 0x40;
void *__fastcall FileNameConstruct(void *self, void *, const cRZString *path)
{
    std::string text = path->mString;
    const size_t slash = text.find_last_of("/\\");
    if (slash != std::string::npos) text.erase(0, slash + 1);
    const size_t dot = text.rfind('.');
    if (dot != std::string::npos) text.erase(dot);
    Log("filename(%s) -> %s", Text(path), text.c_str());
    std::memset(self, 0xab, 0xa0);
    cRZString *name = reinterpret_cast<cRZString *>(static_cast<uint8_t *>(self) + kFileNameName);
    name->Construct(text.c_str());
    return self;
}
void __fastcall FileNameDestruct(void *self, void *)
{
    Log("filename.Destruct");
    reinterpret_cast<cRZString *>(static_cast<uint8_t *>(self) + kFileNameName)->Destruct();
}

// ---- the COM director

uint32_t __fastcall DirectorAddLibrary(void *, void *, const cIGZString *path)
{
    Log("com.AddLibrary(%s)", Text(path));
    return Next(0x3300u);
}
uint32_t __fastcall DirectorTrap(void *, void *)
{
    Log("com.TRAP");
    return 0;
}
void *gDirectorVt[16];
struct {
    void **vt;
} gDirector{gDirectorVt};
void *__cdecl FakeDirectorObject()
{
    Log("GetDirectorFrameworkObject");
    return &gDirector;
}

// ---- the list read from memory

uint32_t Word(const void *p, uint32_t offset)
{
    uint32_t w;
    std::memcpy(&w, static_cast<const uint8_t *>(p) + offset, 4);
    return w;
}
const uint8_t *Ptr(const void *p, uint32_t offset)
{
    return reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(Word(p, offset)));
}

std::string StringWords(const uint8_t *s)
{
    const cRZString *str = reinterpret_cast<const cRZString *>(s);
    char b[64];
    std::snprintf(b, sizeof(b), "%s/%u", str->mVTable == GameAddress<void>(cRZString::kVTable) ? "vt" : "vt?", str->mRefCount);
    return "\"" + str->mString + "\" " + b;
}

void DumpMap(std::vector<std::string> &out, const char *what, const uint8_t *map)
{
    const uint8_t *head = Ptr(map, 0);
    const uint32_t size = Word(map, 4);
    out.push_back(std::string(what) + " size " + std::to_string(size) + (head[0xd] ? "" : " head?"));
    const uint8_t *node = Ptr(head, 0); // leftmost
    uint32_t n = 0;
    while (node != head && n <= size) {
        out.push_back(std::string(what) + " " + StringWords(node + 0x10) + " = " + StringWords(node + 0x30) + (node[0xc] ? " black" : " red"));
        ++n;
        const uint8_t *right = Ptr(node, 8);
        if (!right[0xd]) {
            node = right;
            while (!Ptr(node, 0)[0xd]) node = Ptr(node, 0);
        } else {
            const uint8_t *parent = Ptr(node, 4);
            while (!parent[0xd] && node == Ptr(parent, 8)) {
                node = parent;
                parent = Ptr(node, 4);
            }
            node = parent;
        }
    }
    if (n != size) out.push_back(std::string(what) + " walked " + std::to_string(n));
}

std::vector<std::string> State(const void *list)
{
    std::vector<std::string> out;
    const uint8_t *begin = Ptr(list, 0), *end = Ptr(list, 4), *cap = Ptr(list, 8);
    out.push_back("libraries " + std::to_string((end - begin) / 0x24) + " capacity " + std::to_string((cap - begin) / 0x24));
    for (const uint8_t *e = begin; e && e < end; e += 0x24) {
        out.push_back("library " + StringWords(e) + " required " + std::to_string(e[0x20]) + " loaded " + std::to_string(e[0x21]));
    }
    DumpMap(out, "configuration", static_cast<const uint8_t *>(list) + 0xc);
    DumpMap(out, "version", static_cast<const uint8_t *>(list) + 0x14);
    return out;
}

void CompareLines(difftest::Context &ctx, int i, int k, const char *what, const std::vector<std::string> &a, const std::vector<std::string> &b)
{
    if (a == b) return;
    size_t n = 0;
    while (n < a.size() && n < b.size() && a[n] == b[n]) ++n;
    ctx.Fail("case %d call %d %s line %zu (of %zu / %zu): \"%.120s\" / \"%.120s\"", i, k, what, n, a.size(), b.size(),
             n < a.size() ? a[n].c_str() : "(none)", n < b.size() ? b[n].c_str() : "(none)");
}

// ---- strings for the calls

struct Str {
    alignas(cRZString) unsigned char raw[sizeof(cRZString)];
    explicit Str(const std::string &text)
    {
        std::memset(raw, 0xcd, sizeof(raw));
        S().Construct(text.c_str());
    }
    ~Str() { S().Destruct(); }
    Str(const Str &) = delete;
    Str &operator=(const Str &) = delete;
    cRZString &S() { return *reinterpret_cast<cRZString *>(raw); }
};

const char *const kFiles[] = {"TSDebugD.dll", "C:/Games/Plug/GZLogMFCWinD.dll", "plug\\EdithEditorD.DLL", "tsdebugd.dll",
                              "x", "", "D:\\a.b\\TSAutomation.dll", "Lib.dll"};
const char *const kNames[] = {"tsdebugd", "TSDebugD.dll", "C:/x/gzlogmfcwind.dll", "EdithEditorD", "lib.DLL", "", "x.y"};
const char *const kVersions[] = {"1.18.0.187", "1.0.0.0", "", "1.18.0.187 ", "a", "A"};
const char *const kKeys[] = {"required", "Required", "REQUIRED", "optional", "req", "requiredx", ""};
const char *const kConfigurations[] = {"debug", "Release", "x", "", "debug2"};
const char *const kDirectories[] = {"bin", "C:/D", "", "a\\b\\", "Plugins/Debug"};

template <class T, size_t N> const T &Pick(std::mt19937 &r, const T (&items)[N])
{
    return items[r() % N];
}

std::string LibraryValue(std::mt19937 &r)
{
    std::string v = Pick(r, kFiles);
    switch (r() % 5) {
    case 0: return v;
    case 1: return v + "," + Pick(r, kConfigurations);
    case 2: return "," + std::string(Pick(r, kConfigurations));
    case 3: return v + "," + Pick(r, kConfigurations) + "," + Pick(r, kConfigurations);
    default: return v + ",";
    }
}

uint32_t ByteArg(std::mt19937 &r)
{
    const uint32_t low = r() % 3 == 0 ? 0u : r() % 2 ? 1u : 0x80u;
    return (r() & 0xffffff00u) | low;
}

uint32_t BoolAnswer(std::mt19937 &r)
{
    return (r() & 0xffffff00u) | (r() % 3 == 0 ? 0u : 1u);
}

enum Op : int { kOpAdd, kOpRequire, kOpRead, kOpLoadAll, kOpReport, kOpCount };

using ConstructFn = void *(__thiscall *)(void *);
using DestructFn = uint32_t(__thiscall *)(void *);
using AddFn = void(__thiscall *)(void *, const cRZString *, uint32_t);
using ReadFn = uint32_t(__thiscall *)(void *, const cIGZString *, const cRZString *);
using RequireFn = void(__thiscall *)(void *, const cRZString *, const cRZString *, uint32_t);
using LoadAllFn = uint32_t(__thiscall *)(void *);
using ReportFn = void(__thiscall *)(void *);

template <class Fn> Fn Choose(difftest::Context &ctx, bool ours, uint32_t rva, const void *adapter)
{
    return ours ? reinterpret_cast<Fn>(const_cast<void *>(adapter)) : ctx.Original<Fn>(rva);
}
#define ADAPTER(name) reinterpret_cast<const void *>(&DynamicLibraryListAdapters::name)

// One call's script: its arguments and the world's answers, set up the same for both sides.
struct Call {
    int op = 0;
    std::string a, b;
    uint32_t word = 0;
    std::vector<uint32_t> answers;
    std::vector<std::string> versions;
    bool hasConfigurations = false, hasLibraries = false;
    std::vector<std::pair<std::string, std::string>> configurationLines, libraryLines;
    std::string directory;
};

Call RandomCall(std::mt19937 &r)
{
    Call c;
    const uint32_t roll = r() % 20;
    c.op = roll < 6 ? kOpAdd : roll < 10 ? kOpRequire : roll < 13 ? kOpRead : roll < 18 ? kOpLoadAll : kOpReport;
    switch (c.op) {
    case kOpAdd:
        c.a = Pick(r, kFiles);
        c.word = ByteArg(r);
        break;
    case kOpRequire:
        c.a = r() % 2 ? Pick(r, kNames) : Pick(r, kFiles);
        c.b = Pick(r, kVersions);
        c.word = ByteArg(r);
        break;
    case kOpRead: {
        c.a = r() % 2 ? "C:/Game/TSData/Res/Config/DLLConfig.ini" : "";
        c.b = Pick(r, kConfigurations);
        c.word = r() % 4; // 0: configuration given, else a null configuration when 3
        c.hasConfigurations = r() % 3 != 0;
        c.hasLibraries = r() % 4 != 0;
        const int nConfigurations = int(r() % 5), nLibraries = int(r() % 6);
        for (int i = 0; i < nConfigurations; ++i) c.configurationLines.emplace_back(Pick(r, kConfigurations), Pick(r, kDirectories));
        for (int i = 0; i < nLibraries; ++i) c.libraryLines.emplace_back(Pick(r, kKeys), LibraryValue(r));
        c.directory = Pick(r, kDirectories);
        break;
    }
    default:
        break;
    }
    for (int i = 0; i < 24; ++i) c.answers.push_back(BoolAnswer(r));
    for (int i = 0; i < 12; ++i) c.versions.push_back(Pick(r, kVersions));
    return c;
}

std::string Run(difftest::Context &ctx, bool ours, void *list, const Call &c)
{
    gAnswers.assign(c.answers.begin(), c.answers.end());
    gVersionTexts.assign(c.versions.begin(), c.versions.end());
    gHasConfigurations = c.hasConfigurations;
    gHasLibraries = c.hasLibraries;
    gConfigurationLines = c.configurationLines;
    gLibraryLines = c.libraryLines;
    gDirectoryOption = c.directory;
    gList = list;
    char b[32];
    switch (c.op) {
    case kOpAdd: {
        Str path(c.a);
        Choose<AddFn>(ctx, ours, kAdd, ADAPTER(Add))(list, &path.S(), c.word);
        return "add";
    }
    case kOpRequire: {
        Str path(c.a), version(c.b);
        Choose<RequireFn>(ctx, ours, kRequireVersion, ADAPTER(RequireVersion))(list, &path.S(), &version.S(), c.word);
        return "require";
    }
    case kOpRead: {
        Str path(c.a), configuration(c.b);
        const uint32_t r = Choose<ReadFn>(ctx, ours, kReadConfiguration, ADAPTER(ReadConfiguration))(
            list, path.S().AsInterface(), c.word == 3 ? nullptr : &configuration.S());
        std::snprintf(b, sizeof(b), "read %u", r & 0xff);
        return b;
    }
    case kOpLoadAll: {
        const uint32_t r = Choose<LoadAllFn>(ctx, ours, kLoadAll, ADAPTER(LoadAll))(list);
        std::snprintf(b, sizeof(b), "load %u", r & 0xff);
        return b;
    }
    default:
        Choose<ReportFn>(ctx, ours, kReport, ADAPTER(Report))(list);
        return "report";
    }
}

struct World {
    shape::CodePatch gameNew{kGameNew, reinterpret_cast<const void *>(&GameNew)};
    shape::CodePatch gameDelete{kGameSizedDelete, reinterpret_cast<const void *>(&GameDelete)};
    shape::StandIn iniConstruct{0xcb2a20, &cTSIniFile::Construct, &IniConstruct};
    shape::StandIn iniDestruct{0xcb2cc0, &cTSIniFile::Destruct, &IniDestruct};
    shape::StandIn iniHas{0xcb30f0, &cTSIniFile::HasSectionPrefixed, &IniHas};
    shape::StandIn iniWalk{0xcb2d00, &cTSIniFile::SectionWalk, &IniWalk};
    shape::StandIn exists{0x1c1242, &cRZFile::PathExists, &FakePathExists};
    shape::StandIn version{0x1c98ab, &appbase::cRZVersion::ConstructFromPath, &VersionConstruct};
    shape::StandIn fileVersion{0x1c9b6d, &appbase::cRZVersion::GetFileVersion, &VersionGetFileVersion};
    shape::StandIn versionDestruct{0x1c9901, &appbase::cRZVersion::Destruct, &VersionDestruct};
    shape::StandIn director{0x1ae18d, &GetDirectorFrameworkObject, &FakeDirectorObject};
    shape::StandIn option{0x1d0c00, &appinit::CommonImplSlot4, &FakeGetOption};
    shape::StandIn combine{0x1c17bd, &inifile::CombinePathString, &FakeCombine};
    shape::StandIn fileName{0x1c7ef4, &cRZFileName::Construct, &FileNameConstruct};
    shape::StandIn fileNameDestruct{0x1c0eb2, &cRZFileName::Destruct, &FileNameDestruct};
    World()
    {
        for (void *&slot : gDirectorVt) slot = reinterpret_cast<void *>(&DirectorTrap);
        gDirectorVt[5] = reinterpret_cast<void *>(&DirectorAddLibrary);
    }
};

} // namespace

// The list under random scripts: built by 0xcb3640 (filled memory), then Add 0xcb3740, RequireVersion 0xcb3e10,
// ReadConfiguration 0xcb3870 (its walks call back 0xcb3f90 for the Configurations lines and 0xcb3fd0 for the DLLs
// lines), LoadAll 0xcb3b70 and Report 0xcb3a80; destroyed by 0xcb36f0. Results, the logs and the list after every call.
DIFF_TEST(DynamicLibraryList_Script, kLoadAll)
{
    World world;
    std::mt19937 rng(4471);
    for (int i = 0; i < 1200 && ctx.failures.size() < 20; ++i) {
        const uint8_t fill = static_cast<uint8_t>(rng());
        const int calls = int(rng() % 24);
        alignas(8) unsigned char memory[2][DynamicLibraryList::kSize];
        std::vector<std::string> logs[2];
        for (int side = 0; side < 2; ++side) {
            std::memset(memory[side], fill, sizeof(memory[side]));
            gLog.clear();
            ctx.Mark(side ? "list ours: construct" : "list original: construct");
            Choose<ConstructFn>(ctx, side == 1, kConstruct, ADAPTER(Construct))(memory[side]);
            logs[side] = gLog;
        }
        ++ctx.cases;
        CompareLines(ctx, i, -1, "built", State(memory[0]), State(memory[1]));
        CompareLines(ctx, i, -1, "build calls", logs[0], logs[1]);
        for (int k = 0; k < calls && ctx.failures.size() < 20; ++k) {
            const Call c = RandomCall(rng);
            std::string results[2];
            for (int side = 0; side < 2; ++side) {
                gLog.clear();
                ctx.Mark(side ? "list ours" : "list original");
                results[side] = Run(ctx, side == 1, memory[side], c);
                logs[side] = gLog;
            }
            ++ctx.cases;
            if (results[0] != results[1]) ctx.Fail("case %d call %d op %d: %s / %s", i, k, c.op, results[0].c_str(), results[1].c_str());
            CompareLines(ctx, i, k, "state", State(memory[0]), State(memory[1]));
            CompareLines(ctx, i, k, "calls", logs[0], logs[1]);
        }
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            ctx.Mark(side ? "list ours: destruct" : "list original: destruct");
            Choose<DestructFn>(ctx, side == 1, kDestruct, ADAPTER(Destruct))(memory[side]);
            logs[side] = gLog;
        }
        ++ctx.cases;
        CompareLines(ctx, i, 99, "destruct calls", logs[0], logs[1]);
    }
}
