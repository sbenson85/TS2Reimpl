// Differential tests for ts::cTSAppPropertyFiles (src/ts/cTSAppPropertyFiles.cpp) and PathAttributesMatch
// (src/rz/PathAttributes.cpp), lane MA.
//
// Covered, each run on the original by RVA and on ours: 0xcb0890 Construct, 0xcb0ad0 Destruct, 0xcb0f40 SetupPaths,
// 0xcb18a0 LoadProperties, 0xcb1780 LoadGlobalInto, 0xcb1790 LoadConfigProperties, 0xcb28b0 Save, 0xcb1e60
// LoadUserProperties, 0xcb2020 LoadPropertyList, 0xcb2240 LoadSavedKeyNames, 0xcb0da0 CopyAll, 0xcb0e50 CopyListed,
// 0xcb1cf0 LoadXml, 0xcb2400 ReadKeyNames, 0x1c1931 PathAttributesMatch.
//
// Each side builds its own object with its own constructor (the original's 0xcb0890 or ours). Real on both sides: the
// property sets (ours: the service's set, the targets; the original's inline-built locals run the game's set code), the
// saved-key text files (sandbox files read through cRZTextFile: InstallWideFunctions), the string helpers, the
// allocator service (svcfx::AllocatorScope). Recording fakes, the same on both sides: the framework (0x10c9378: slot 5
// GetSystemService, slot 21 CommandLine) and its command line (slots 8 / 9), the application's option store (the
// forwarders' cache 0x10cc040: slots 4 / 5), and stand-ins over both sides' PathExists (0x1c1242), DirectoryExists
// (0x1c4f94), PathAttributesMatch (0x1c1931, except in its own test), the file streams' constructors and destructors
// (0x1b4cdc / 0x1b528c, 0x1b4dd2 / 0x1b52d8) and the XML serializer's Read / Write (0x555d50 / 0x556a30 and our bodies):
// the XML files are scripts of (key, uint32) properties a fake Read sets on the set it is given, and Write logs the set.
// Compared: the call log, results (al), the object's flags, saved-key names, service, refcount and paths, and the sets.
#include "difftest.h"
#include "app_debugger_fakes.h" // adt::InstallWideFunctions
#include "code_patch.h"
#include "free_recorder.h"
#include "ts_services_fixture.h"

#include "app/AppServices2.h"
#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"
#include "gz/cGZPropertySet.h"
#include "gz/cGZPropertySetSerializer.h"
#include "rz/PathAttributes.h"
#include "rz/RZFileStreams.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"
#include "ts/cTSAppCommonImpl.h"
#include "ts/cTSAppPropertyFiles.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace {

using ts::cTSAppPropertyFiles;
using ts::KeyNameSet;

constexpr uint32_t kConstruct = 0xcb0890;
constexpr uint32_t kDestruct = 0xcb0ad0;
constexpr uint32_t kSetupPaths = 0xcb0f40;
constexpr uint32_t kLoadProperties = 0xcb18a0;
constexpr uint32_t kLoadGlobalInto = 0xcb1780;
constexpr uint32_t kLoadConfig = 0xcb1790;
constexpr uint32_t kSave = 0xcb28b0;
constexpr uint32_t kLoadUser = 0xcb1e60;
constexpr uint32_t kLoadList = 0xcb2020;
constexpr uint32_t kLoadNames = 0xcb2240;
constexpr uint32_t kCopyAll = 0xcb0da0;
constexpr uint32_t kCopyListed = 0xcb0e50;
constexpr uint32_t kLoadXml = 0xcb1cf0;
constexpr uint32_t kReadNames = 0xcb2400;
constexpr uint32_t kAttributes = 0x1c1931;

constexpr uint32_t kFrameworkCell = 0x10c9378;
constexpr uint32_t kCommonCacheCell = 0x10cc040;
constexpr uint32_t kIatGetFileAttributesA = 0xd9a204;
constexpr uint32_t kWideGetFileAttributesCell = 0x10cb5f4;
constexpr uint32_t kOriginalXmlRead = 0x555d50;
constexpr uint32_t kOriginalXmlWrite = 0x556a30;

using ConstructFn = void *(__thiscall *)(void *, uint32_t);
using ThisFn = void(__thiscall *)(void *);
using ThisArgFn = void(__thiscall *)(void *, void *);
using LoadUserFn = uint32_t(__thiscall *)(void *, void *, const cRZString *);
using LoadListFn = void(__thiscall *)(void *, void *, const cRZString *);
using CopyAllFn = void(__stdcall *)(void *, void *);
using CopyListedFn = void(__stdcall *)(void *, void *, KeyNameSet *);
using LoadXmlFn = uint32_t(__stdcall *)(void *, const cRZString *);
using ReadNamesFn = void(__stdcall *)(const char *, KeyNameSet *);
using AttributesFn = uint32_t(__cdecl *)(const cIGZString *, uint32_t);

std::string Hex(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof(b), "%x", v);
    return b;
}

const std::string &Text(const cIGZString *s)
{
    return reinterpret_cast<const cRZString *>(s)->mString;
}

// ---- the script both sides run against

struct XmlFile {
    bool readable = true;
    std::vector<std::pair<std::string, uint32_t>> properties;
};

struct Script {
    bool framework = true;     // the framework cell set
    bool serviceGiven = true;  // GetSystemService answers
    std::vector<std::string> dataDirectories, configDirectories;
    std::string userDirectory;
    std::set<std::string> directories;   // DirectoryExists answers true
    std::map<std::string, XmlFile> xml;  // PathExists answers true for these
    std::set<std::string> readOnly;      // PathAttributesMatch(2) answers false
    std::map<std::string, uint8_t> switches; // IsSwitchPresent's al by name (absent: 0)
    bool haveList = false;
    std::string listText;
};
Script gScript;
std::vector<std::string> *gLog = nullptr;
::cGZPropertySetString *gService = nullptr; // the side's property-set service

void Log(const std::string &line)
{
    if (gLog) gLog->push_back(line);
}

// A property set's keys and values in iteration order (values: AnyUint32 as their number, others as their type).
std::string Dump(void *set)
{
    if (!set) return "(null)";
    std::string out;
    GZServiceString key;
    void *value = nullptr;
    using FirstFn = bool(__thiscall *)(void *, GZServiceString *, void **);
    for (bool more = VirtualCall<FirstFn, 6>(set, &key, &value); more; more = VirtualCall<FirstFn, 7>(set, &key, &value)) {
        auto *any = static_cast<cIGZAny *>(value);
        out += "{" + std::string(key.c_str()) + "=";
        out += any->GetType() == 0xeb61e4f7 ? Hex(static_cast<gz::AnyUint32 *>(any)->mValue) : "type " + Hex(any->GetType());
        out += "}";
    }
    return out;
}

void SetUint(void *set, const std::string &name, uint32_t value)
{
    const GZServiceString key(name.c_str());
    gz::AnyUint32 any(value);
    VirtualCall<bool(__thiscall *)(void *, const GZServiceString *, void *), 3>(set, &key, &any);
}

std::string Names(const KeyNameSet &names)
{
    std::string out = std::to_string(names.size()) + ":";
    for (const GZServiceString &n : names) out += "[" + std::string(n.c_str()) + "]";
    return out;
}

// ---- fakes over code (both sides)

bool __cdecl FakePathExists(const cIGZString *path)
{
    Log("PathExists " + Text(path));
    return gScript.xml.count(Text(path)) != 0;
}

bool __cdecl FakeDirectoryExists(cRZString *path)
{
    Log("DirectoryExists " + path->mString);
    return gScript.directories.count(path->mString) != 0;
}

uint32_t __cdecl FakeAttributes(const cIGZString *path, uint32_t flags)
{
    Log("Attributes " + Text(path) + " " + Hex(flags));
    const bool met = !(flags & 2) || gScript.readOnly.count(Text(path)) == 0;
    return 0xab00u | (met ? 1u : 0u); // callers read al
}

// The streams: the fake writes all 0x18 bytes, a mark and the index of its path.
constexpr uint32_t kStreamMark = 0x5157ea70;
std::vector<std::string> gStreamPaths;

std::string StreamPath(const void *stream)
{
    const auto *w = static_cast<const uint32_t *>(stream);
    if (w[0] != kStreamMark || w[1] >= gStreamPaths.size()) return "(not a stream)";
    return gStreamPaths[w[1]];
}

void *Mark(void *self, const cIGZString *path, uint32_t size)
{
    std::memset(self, 0, size);
    auto *w = static_cast<uint32_t *>(self);
    w[0] = kStreamMark;
    w[1] = static_cast<uint32_t>(gStreamPaths.size());
    gStreamPaths.push_back(Text(path));
    return self;
}

void *__fastcall FakeInConstruct(void *self, void *, const cIGZString *path, uint32_t binary, uint32_t order)
{
    Log("InStream " + Text(path) + " " + Hex(binary & 0xff) + " " + Hex(order));
    return Mark(self, path, sizeof(cRZIFStream));
}
void __fastcall FakeInDestruct(void *self, void *) { Log("InStream closed " + StreamPath(self)); }

void *__fastcall FakeOutConstruct(void *self, void *, const cIGZString *path, uint32_t binary, uint32_t order, uint32_t autoFlush,
                                  uint32_t append)
{
    Log("OutStream " + Text(path) + " " + Hex(binary & 0xff) + " " + Hex(order) + " " + Hex(autoFlush & 0xff) + " " + Hex(append & 0xff));
    return Mark(self, path, sizeof(cRZOFStream));
}
void __fastcall FakeOutDestruct(void *self, void *) { Log("OutStream closed " + StreamPath(self)); }

uint32_t __fastcall FakeRead(void *, void *, void *stream, void *set)
{
    const std::string path = StreamPath(stream);
    Log("Read " + path);
    auto it = gScript.xml.find(path);
    if (it == gScript.xml.end()) return 0xcd00;
    // An unreadable file fails after setting what it read (a parse error part way through the file).
    for (const auto &p : it->second.properties) SetUint(set, p.first, p.second);
    return it->second.readable ? 0xcd01 : 0xcd00; // callers read al
}

uint32_t __fastcall FakeWrite(void *, void *, void *stream, void *set, uint32_t binary)
{
    Log("Write " + StreamPath(stream) + " " + Hex(binary & 0xff) + " " + Dump(set));
    return 0xcd01;
}

// ---- the framework, its command line, the option store

void *gFrameworkVt[24];
void *gCommandLineVt[12];
void *gOptionsVt[8];
struct FakeObject {
    void **vt;
};
FakeObject gFramework{gFrameworkVt}, gCommandLine{gCommandLineVt}, gOptions{gOptionsVt};

uint32_t __fastcall FwGetSystemService(void *, void *, uint32_t clsid, uint32_t iid, void **out)
{
    Log("GetSystemService " + Hex(clsid) + " " + Hex(iid));
    if (!gScript.serviceGiven || !gService) return 0x4400;
    gService->AddRef();
    *out = static_cast<cIGZPropertySetString *>(gService);
    return 0x4401;
}

void *__fastcall FwCommandLine(void *, void *)
{
    Log("CommandLine");
    return &gCommandLine;
}

uint32_t __fastcall CmdSwitchPresent(void *, void *, const cIGZString *name)
{
    Log("Switch " + Text(name));
    auto it = gScript.switches.find(Text(name));
    return 0x7700u | (it == gScript.switches.end() ? 0u : it->second);
}

uint32_t __fastcall CmdSwitchValue(void *, void *, const cIGZString *name, cIGZString *value, uint32_t trim)
{
    Log("SwitchValue " + Text(name) + " " + Hex(trim));
    if (!gScript.haveList) return 0x7700;
    reinterpret_cast<cRZString *>(value)->mString = gScript.listText;
    return 0x7701;
}

uint32_t __fastcall OptionText(void *, void *, uint32_t key, cRZString *out, uint32_t a, uint32_t b, uint32_t c)
{
    Log("OptionText " + Hex(key) + " " + Hex(a) + " " + Hex(b) + " " + Hex(c));
    out->mString = gScript.userDirectory;
    return 0x6601;
}

uint32_t __fastcall OptionList(void *, void *, uint32_t key, ts::OptionStringList *out, uint32_t a, uint32_t b, uint32_t c)
{
    Log("OptionList " + Hex(key) + " " + Hex(a) + " " + Hex(b) + " " + Hex(c));
    const auto &list = key == cTSAppPropertyFiles::kDataDirectoriesOption ? gScript.dataDirectories : gScript.configDirectories;
    for (const std::string &s : list) out->push_back(ts::OptionString(s.c_str()));
    return 0x6601;
}

// Our serializer's vtable (for the bodies of Read and Write).
void *const *OurSerializerVTable()
{
    static void *const *vt = [] {
        cGZPropertySetSerializerXmlString probe;
        return *reinterpret_cast<void *const *const *>(&probe);
    }();
    return vt;
}

// Everything the tests share, for one test's scope.
struct World {
    difftest::Context &ctx;
    svcfx::AllocatorScope allocator;
    svcfx::Globals globals;
    shape::StandIn pathExists, directoryExists, inConstruct, inDestruct, outConstruct, outDestruct;
    shape::CodePatch readImage, readOurs, writeImage, writeOurs;
    std::unique_ptr<shape::StandIn> attributes;

    explicit World(difftest::Context &c, bool standInAttributes = true)
        : ctx(c),
          pathExists(0x1c1242, &cRZFile::PathExists, &FakePathExists),
          directoryExists(0x1c4f94, &as2::DirectoryExists, &FakeDirectoryExists),
          inConstruct(0x1b4cdc, static_cast<cRZIFStream *(cRZIFStream::*)(const cIGZString &, uint8_t, uint32_t)>(&cRZIFStream::Construct),
                      &FakeInConstruct),
          inDestruct(0x1b528c, &cRZIFStream::Destruct, &FakeInDestruct),
          outConstruct(0x1b4dd2, static_cast<cRZOFStreamFromString>(&cRZOFStream::Construct), &FakeOutConstruct),
          outDestruct(0x1b52d8, &cRZOFStream::Destruct, &FakeOutDestruct),
          readImage(kOriginalXmlRead, reinterpret_cast<const void *>(&FakeRead)),
          readOurs(shape::VirtualMemberCode(&cGZPropertySetSerializerXmlString::Read, OurSerializerVTable()),
                   reinterpret_cast<const void *>(&FakeRead)),
          writeImage(kOriginalXmlWrite, reinterpret_cast<const void *>(&FakeWrite)),
          writeOurs(shape::VirtualMemberCode(&cGZPropertySetSerializerXmlString::Write, OurSerializerVTable()),
                    reinterpret_cast<const void *>(&FakeWrite))
    {
        if (standInAttributes) attributes = std::make_unique<shape::StandIn>(kAttributes, &PathAttributesMatch, &FakeAttributes);
        adt::InstallWideFunctions(c);
        gFrameworkVt[5] = reinterpret_cast<void *>(&FwGetSystemService);
        gFrameworkVt[21] = reinterpret_cast<void *>(&FwCommandLine);
        gCommandLineVt[8] = reinterpret_cast<void *>(&CmdSwitchValue);
        gCommandLineVt[9] = reinterpret_cast<void *>(&CmdSwitchPresent);
        gOptionsVt[4] = reinterpret_cast<void *>(&OptionText);
        gOptionsVt[5] = reinterpret_cast<void *>(&OptionList);
        globals.Set(kFrameworkCell, &gFramework);
        globals.Set(kCommonCacheCell, &gOptions);
    }
    void Framework(bool present) { *GameAddress<void *>(kFrameworkCell) = present ? &gFramework : nullptr; }
};

// ---- one side's object

struct Side {
    std::vector<std::string> log;
    ::cGZPropertySetString *service = nullptr;
    cTSAppPropertyFiles *object = nullptr;
};

// The side's turn: its log, its service.
struct Turn {
    explicit Turn(Side &s)
    {
        gLog = &s.log;
        gService = s.service;
        gStreamPaths.clear();
    }
    ~Turn()
    {
        gLog = nullptr;
        gService = nullptr;
    }
};

cTSAppPropertyFiles *Build(difftest::Context &ctx, Side &side, bool original, bool enabled)
{
    Turn turn(side);
    void *memory = ::operator new(cTSAppPropertyFiles::kSize);
    std::memset(memory, 0xcd, cTSAppPropertyFiles::kSize);
    if (original)
        ctx.Original<ConstructFn>(kConstruct)(memory, enabled ? 0xee01u : 0xee00u); // the flag is the low byte
    else
        static_cast<cTSAppPropertyFiles *>(memory)->Construct(enabled);
    side.object = static_cast<cTSAppPropertyFiles *>(memory);
    return side.object;
}

void Destroy(difftest::Context &ctx, Side &side, bool original)
{
    if (!side.object) return;
    Turn turn(side);
    if (original)
        ctx.Original<ThisFn>(kDestruct)(side.object);
    else
        side.object->Destruct();
    ::operator delete(side.object);
    side.object = nullptr;
}

std::string Describe(const Side &side)
{
    const cTSAppPropertyFiles *p = side.object;
    std::string s = "save " + Hex(*reinterpret_cast<const uint8_t *>(&p->mSave)) + " user " + Hex(*reinterpret_cast<const uint8_t *>(&p->mLoadUser));
    s += " names " + Names(p->mSavedKeys);
    s += " service " + std::string(!p->mProperties ? "null" : p->mProperties == static_cast<cIGZPropertySetString *>(side.service) ? "ours" : "other");
    if (side.service) s += " refs " + std::to_string(side.service->mRefCount);
    const cRZString *paths[] = {&p->mSavedKeysPath, &p->mUserSavedKeysPath, &p->mUserPropsPath, &p->mGlobalPropsPath,
                                &p->mConfigPropsPath, &p->mDataDirectory, &p->mOverrideDirectory};
    for (const cRZString *path : paths) s += " [" + path->mString + "]";
    return s;
}

// The log lines that start with `prefix`.
int Lines(const std::vector<std::string> &log, const char *prefix)
{
    int n = 0;
    for (const std::string &l : log)
        if (l.rfind(prefix, 0) == 0) ++n;
    return n;
}

std::string Joined(const std::vector<std::string> &log)
{
    std::string s;
    for (const std::string &l : log) s += l + "\n";
    return s;
}

// Both sides' logs and descriptions equal, else a failure naming the first difference.
bool Same(difftest::Context &ctx, const char *what, int i, const Side &a, const Side &b, const std::string &extraA = "",
          const std::string &extraB = "")
{
    const std::string da = Describe(a) + " " + extraA, db = Describe(b) + " " + extraB;
    if (a.log == b.log && da == db) return true;
    ctx.Fail("%s case %d:\n  orig %s\n  ours %s\n  %s", what, i, da.c_str(), db.c_str(), adt::FirstDifference(a.log, b.log).c_str());
    return false;
}

// ---- scripts

const std::string &Pick(std::mt19937 &rng, const std::vector<std::string> &pool)
{
    return pool[rng() % pool.size()];
}

std::string Dir() // the sandbox directory, with its trailing backslash (ASCII: the harness makes it under local/)
{
    const std::wstring &w = difftest::SandboxDir();
    std::string s;
    for (wchar_t c : w) s += static_cast<char>(c);
    return s;
}

const std::vector<std::string> kKeys = {"propertyRevision", "Volume", "volume", "VOLUME", "width", "Width", "cheats", "name",
                                        "a", "B", "camera speed", "", "zz"};

std::vector<std::pair<std::string, uint32_t>> RandomProperties(std::mt19937 &rng, int most)
{
    std::vector<std::pair<std::string, uint32_t>> props;
    const int n = static_cast<int>(rng() % (most + 1));
    for (int i = 0; i < n; ++i) props.push_back({Pick(rng, kKeys), rng() % 3 ? rng() % 4 : rng()});
    return props;
}

void RandomFill(std::mt19937 &rng, ::cGZPropertySetString *a, ::cGZPropertySetString *b, int most)
{
    for (const auto &p : RandomProperties(rng, most)) {
        SetUint(a, p.first, p.second);
        SetUint(b, p.first, p.second);
    }
}

// A saved-key names file: names in several cases and spellings, comments, blank and padded lines, both line ends.
std::string RandomNamesText(std::mt19937 &rng)
{
    static const std::vector<std::string> kLines = {"Volume", "volume", "  width\t", "#comment", "# Volume", "", "   ", "\t",
                                                    "cheats", "CHEATS", "name", "a", "zz", "camera speed", "propertyRevision",
                                                    "x#y", "#", "B"};
    std::string text;
    const int n = static_cast<int>(rng() % 9);
    for (int i = 0; i < n; ++i) text += Pick(rng, kLines) + (rng() % 2 ? "\r\n" : "\n");
    if (rng() % 3 == 0) text += Pick(rng, kLines);
    return text;
}

bool WriteText(const std::string &path, const std::string &text)
{
    HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(h);
    return ok && written == text.size();
}

// The names files: present (random text) or missing.
void RandomNamesFile(std::mt19937 &rng, const std::string &path)
{
    if (rng() % 5 == 0)
        DeleteFileA(path.c_str());
    else
        WriteText(path, RandomNamesText(rng));
}

// An XML script for `path`: missing, unreadable, or a few properties.
void RandomXml(std::mt19937 &rng, const std::string &path)
{
    const uint32_t r = rng() % 6;
    if (r == 0) return;
    XmlFile f;
    f.readable = r != 1;
    f.properties = RandomProperties(rng, 6);
    gScript.xml[path] = f;
}

// Sets the same path text on both objects (each side's string assigned by our std::string code: both are MSVC strings).
void SetPath(cRZString cTSAppPropertyFiles::*member, Side &a, Side &b, const std::string &text)
{
    (a.object->*member).mString = text;
    (b.object->*member).mString = text;
}

// Both objects' saved-key names from the same file, each through its own side's ReadKeyNames.
void FillNames(difftest::Context &ctx, std::mt19937 &rng, Side &a, Side &b)
{
    const std::string file = Dir() + "prefill.txt";
    WriteText(file, RandomNamesText(rng));
    {
        Turn t(a);
        ctx.Original<ReadNamesFn>(kReadNames)(file.c_str(), &a.object->mSavedKeys);
    }
    {
        Turn t(b);
        cTSAppPropertyFiles::ReadKeyNames(file.c_str(), &b.object->mSavedKeys);
    }
    a.log.clear();
    b.log.clear();
}

// A fresh pair of objects and services, the same script state on both.
struct Pair {
    difftest::Context &ctx;
    Side a, b;
    Pair(difftest::Context &c, std::mt19937 &rng) : ctx(c)
    {
        a.service = new ::cGZPropertySetString();
        b.service = new ::cGZPropertySetString();
        a.service->AddRef();
        b.service->AddRef();
        RandomFill(rng, a.service, b.service, 4);
        const bool enabled = rng() % 4 != 0;
        Build(ctx, a, true, enabled);
        Build(ctx, b, false, enabled);
        a.log.clear();
        b.log.clear();
    }
    ~Pair()
    {
        Destroy(ctx, a, true);
        Destroy(ctx, b, false);
        a.service->Release();
        b.service->Release();
    }
    // Paths in the sandbox for the files the operations read (each one sometimes empty or elsewhere in the sandbox).
    void RandomPaths(std::mt19937 &rng)
    {
        const std::string dir = Dir();
        SetPath(&cTSAppPropertyFiles::mDataDirectory, a, b, dir);
        SetPath(&cTSAppPropertyFiles::mSavedKeysPath, a, b, dir + (rng() % 6 ? "savedProps.txt" : "userSavedProps.txt"));
        SetPath(&cTSAppPropertyFiles::mUserSavedKeysPath, a, b, dir + (rng() % 6 ? "userSavedProps.txt" : "missing.txt"));
        SetPath(&cTSAppPropertyFiles::mGlobalPropsPath, a, b, dir + "globalProps.xml");
        SetPath(&cTSAppPropertyFiles::mUserPropsPath, a, b, dir + (rng() % 8 ? "userProps.xml" : "globalProps.xml"));
        SetPath(&cTSAppPropertyFiles::mConfigPropsPath, a, b, dir + "Config/globalProps.xml");
        RandomNamesFile(rng, dir + "savedProps.txt");
        RandomNamesFile(rng, dir + "userSavedProps.txt");
        for (const char *name : {"globalProps.xml", "userProps.xml", "Config/globalProps.xml", "list1.xml", "list2.xml"})
            RandomXml(rng, dir + name);
        if (rng() % 3 == 0) gScript.readOnly.insert(dir + "userProps.xml");
    }
    void Flags(std::mt19937 &rng)
    {
        const bool save = rng() % 4 != 0, user = rng() % 4 != 0;
        a.object->mSave = b.object->mSave = save;
        a.object->mLoadUser = b.object->mLoadUser = user;
    }
};

void ResetScript()
{
    gScript = Script{};
}

} // namespace

// ---- 0xcb0890 Construct, 0xcb0ad0 Destruct

DIFF_TEST(PropertyFiles_Construct, kConstruct)
{
    // With and without a framework and an answer from it; the flag byte's other bytes set.
    World world(ctx);
    std::mt19937 rng(kConstruct);
    for (int i = 0; i < 200; ++i) {
        ResetScript();
        gScript.framework = rng() % 4 != 0;
        gScript.serviceGiven = rng() % 4 != 0;
        world.Framework(gScript.framework);
        const bool enabled = rng() % 2 != 0;
        Side a, b;
        a.service = new ::cGZPropertySetString();
        b.service = new ::cGZPropertySetString();
        a.service->AddRef();
        b.service->AddRef();
        Build(ctx, a, true, enabled);
        Build(ctx, b, false, enabled);
        ++ctx.cases;
        Same(ctx, "Construct", i, a, b);
        Destroy(ctx, a, true);
        Destroy(ctx, b, false);
        const std::string ra = std::to_string(a.service->mRefCount), rb = std::to_string(b.service->mRefCount);
        if (ra != rb) ctx.Fail("Construct case %d: refcount after Destruct %s / %s", i, ra.c_str(), rb.c_str());
        a.service->Release();
        b.service->Release();
    }
    world.Framework(true);
}

DIFF_TEST(PropertyFiles_Destruct, kDestruct)
{
    // Objects with names, long and short paths and a service (or none): the service released, every block freed.
    World world(ctx);
    std::mt19937 rng(kDestruct);
    const std::vector<std::string> kTexts = {"", "short", std::string(40, 'p'), std::string(15, 'q'), std::string(16, 'r')};
    for (int i = 0; i < 200; ++i) {
        ResetScript();
        gScript.serviceGiven = rng() % 4 != 0;
        Side a, b;
        a.service = new ::cGZPropertySetString();
        b.service = new ::cGZPropertySetString();
        a.service->AddRef();
        b.service->AddRef();
        Build(ctx, a, true, true);
        Build(ctx, b, false, true);
        FillNames(ctx, rng, a, b);
        cRZString cTSAppPropertyFiles::*members[] = {&cTSAppPropertyFiles::mSavedKeysPath, &cTSAppPropertyFiles::mUserSavedKeysPath,
                                                      &cTSAppPropertyFiles::mUserPropsPath, &cTSAppPropertyFiles::mGlobalPropsPath,
                                                      &cTSAppPropertyFiles::mConfigPropsPath, &cTSAppPropertyFiles::mDataDirectory,
                                                      &cTSAppPropertyFiles::mOverrideDirectory};
        for (auto member : members) SetPath(member, a, b, Pick(rng, kTexts));
        ++ctx.cases;
        size_t sizesA[64] = {}, sizesB[64] = {};
        difftest::FreeRecorder freesA;
        {
            Turn t(a);
            ctx.Original<ThisFn>(kDestruct)(a.object);
        }
        freesA.Stop();
        difftest::FreeRecorder freesB;
        {
            Turn t(b);
            b.object->Destruct();
        }
        freesB.Stop();
        const size_t na = freesA.SortedSizes(sizesA, 64), nb = freesB.SortedSizes(sizesB, 64);
        std::string fa, fb;
        for (size_t k = 0; k < na && k < 64; ++k) fa += Hex(static_cast<uint32_t>(sizesA[k])) + " ";
        for (size_t k = 0; k < nb && k < 64; ++k) fb += Hex(static_cast<uint32_t>(sizesB[k])) + " ";
        const std::string ra = std::to_string(a.service->mRefCount), rb = std::to_string(b.service->mRefCount);
        if (fa != fb || ra != rb || a.log != b.log)
            ctx.Fail("Destruct case %d: frees %s / %s, refs %s / %s", i, fa.c_str(), fb.c_str(), ra.c_str(), rb.c_str());
        ::operator delete(a.object);
        ::operator delete(b.object);
        a.object = b.object = nullptr;
        a.service->Release();
        b.service->Release();
    }
}

// ---- 0xcb0f40 SetupPaths

DIFF_TEST(PropertyFiles_SetupPaths, kSetupPaths)
{
    // Directory lists with none, one or several existing directories, the user directory option, the override directory
    // (set directly: nothing in the image sets it).
    World world(ctx);
    std::mt19937 rng(kSetupPaths);
    const std::vector<std::string> kDirs = {"C:\\Data\\", "C:\\Other\\", "D:\\Games\\Sims\\", "", "rel\\", std::string(30, 'd') + "\\"};
    int found = 0, overridden = 0;
    for (int i = 0; i < 300; ++i) {
        ResetScript();
        for (int n = static_cast<int>(rng() % 4); n > 0; --n) gScript.dataDirectories.push_back(Pick(rng, kDirs));
        for (int n = static_cast<int>(rng() % 4); n > 0; --n) gScript.configDirectories.push_back(Pick(rng, kDirs));
        for (const std::string &d : kDirs)
            if (rng() % 2) gScript.directories.insert(d);
        gScript.userDirectory = Pick(rng, kDirs);
        Pair pair(ctx, rng);
        if (rng() % 5 == 0) {
            SetPath(&cTSAppPropertyFiles::mOverrideDirectory, pair.a, pair.b, Pick(rng, kDirs));
            ++overridden;
        }
        if (rng() % 3 == 0) SetPath(&cTSAppPropertyFiles::mDataDirectory, pair.a, pair.b, "previous\\");
        {
            Turn t(pair.a);
            ctx.Original<ThisFn>(kSetupPaths)(pair.a.object);
        }
        {
            Turn t(pair.b);
            pair.b.object->SetupPaths();
        }
        ++ctx.cases;
        if (!pair.a.object->mConfigPropsPath.mString.empty()) ++found;
        Same(ctx, "SetupPaths", i, pair.a, pair.b);
    }
    if (found < 80 || overridden < 30) ctx.Fail("coverage: %d config directories found, %d overridden", found, overridden);
}

// ---- 0xcb0da0 CopyAll, 0xcb0e50 CopyListed

DIFF_TEST(PropertyFiles_CopyAll, kCopyAll)
{
    World world(ctx);
    std::mt19937 rng(kCopyAll);
    for (int i = 0; i < 200; ++i) {
        ::cGZPropertySetString fromA, fromB, toA, toB;
        RandomFill(rng, &fromA, &fromB, 8);
        RandomFill(rng, &toA, &toB, 4);
        ctx.Original<CopyAllFn>(kCopyAll)(static_cast<cIGZPropertySetString *>(&fromA), static_cast<cIGZPropertySetString *>(&toA));
        cTSAppPropertyFiles::CopyAll(&fromB, &toB);
        ++ctx.cases;
        const std::string da = Dump(&fromA) + " -> " + Dump(&toA), db = Dump(&fromB) + " -> " + Dump(&toB);
        if (da != db) ctx.Fail("CopyAll case %d:\n  %s\n  %s", i, da.c_str(), db.c_str());
    }
}

DIFF_TEST(PropertyFiles_CopyListed, kCopyListed)
{
    // The names in other cases than the keys: the set compares without case.
    World world(ctx);
    std::mt19937 rng(kCopyListed);
    int copied = 0;
    for (int i = 0; i < 300; ++i) {
        ::cGZPropertySetString fromA, fromB, toA, toB;
        RandomFill(rng, &fromA, &fromB, 8);
        RandomFill(rng, &toA, &toB, 3);
        KeyNameSet names; // only read by both
        for (int n = static_cast<int>(rng() % 5); n > 0; --n) names.insert(GZServiceString(Pick(rng, kKeys).c_str()));
        ctx.Original<CopyListedFn>(kCopyListed)(static_cast<cIGZPropertySetString *>(&fromA), static_cast<cIGZPropertySetString *>(&toA),
                                                &names);
        cTSAppPropertyFiles::CopyListed(&fromB, &toB, &names);
        ++ctx.cases;
        const std::string da = Dump(&toA), db = Dump(&toB);
        if (da != Dump(&fromA)) ++copied;
        if (da != db) ctx.Fail("CopyListed case %d (%s):\n  %s\n  %s", i, Names(names).c_str(), da.c_str(), db.c_str());
    }
    if (copied < 100) ctx.Fail("coverage: %d partial copies", copied);
}

// ---- 0xcb1cf0 LoadXml

DIFF_TEST(PropertyFiles_LoadXml, kLoadXml)
{
    // Missing, read-only (flag 1 asks nothing), unreadable and readable scripts into a set that has properties already.
    World world(ctx);
    std::mt19937 rng(kLoadXml);
    int read = 0;
    for (int i = 0; i < 300; ++i) {
        ResetScript();
        const std::string path = Dir() + (rng() % 2 ? "a.xml" : "b.xml");
        RandomXml(rng, path);
        if (rng() % 3 == 0) gScript.readOnly.insert(path);
        Side a, b;
        ::cGZPropertySetString targetA, targetB;
        RandomFill(rng, &targetA, &targetB, 3);
        cRZString pathA, pathB;
        pathA.Construct(path.c_str());
        pathB.Construct(path.c_str());
        uint32_t ra, rb;
        {
            Turn t(a);
            ra = ctx.Original<LoadXmlFn>(kLoadXml)(static_cast<cIGZPropertySetString *>(&targetA), &pathA) & 0xff;
        }
        {
            Turn t(b);
            rb = cTSAppPropertyFiles::LoadXml(&targetB, &pathB);
        }
        pathA.Destruct();
        pathB.Destruct();
        ++ctx.cases;
        if (ra) ++read;
        const std::string da = Hex(ra) + " " + Dump(&targetA), db = Hex(rb) + " " + Dump(&targetB);
        if (da != db || a.log != b.log)
            ctx.Fail("LoadXml case %d:\n  %s\n  %s\n  %s", i, da.c_str(), db.c_str(), adt::FirstDifference(a.log, b.log).c_str());
    }
    if (read < 100) ctx.Fail("coverage: %d read", read);
}

// ---- 0xcb2400 ReadKeyNames, 0xcb2240 LoadSavedKeyNames

DIFF_TEST(PropertyFiles_ReadKeyNames, kReadNames)
{
    // Random files (names in several cases: the first spelling read stays), comments, padding, a missing file; the set
    // has names already (cleared first).
    World world(ctx);
    std::mt19937 rng(kReadNames);
    int named = 0;
    for (int i = 0; i < 300; ++i) {
        const std::string path = Dir() + "names.txt";
        RandomNamesFile(rng, path);
        KeyNameSet namesA, namesB;
        for (int n = static_cast<int>(rng() % 3); n > 0; --n) {
            const std::string name = Pick(rng, kKeys);
            namesA.insert(GZServiceString(name.c_str()));
            namesB.insert(GZServiceString(name.c_str()));
        }
        ctx.Original<ReadNamesFn>(kReadNames)(path.c_str(), &namesA);
        cTSAppPropertyFiles::ReadKeyNames(path.c_str(), &namesB);
        ++ctx.cases;
        const std::string da = Names(namesA), db = Names(namesB);
        if (da != db) ctx.Fail("ReadKeyNames case %d:\n  %s\n  %s", i, da.c_str(), db.c_str());
        if (!namesA.empty()) ++named;
    }
    if (named < 150) ctx.Fail("coverage: %d files with names", named);
}

DIFF_TEST(PropertyFiles_LoadSavedKeyNames, kLoadNames)
{
    // Empty or filled sets; both files, either, neither; the same file twice.
    World world(ctx);
    std::mt19937 rng(kLoadNames);
    int named = 0;
    for (int i = 0; i < 300; ++i) {
        ResetScript();
        Pair pair(ctx, rng);
        pair.RandomPaths(rng);
        if (rng() % 4 == 0) FillNames(ctx, rng, pair.a, pair.b);
        {
            Turn t(pair.a);
            ctx.Original<ThisArgFn>(kLoadNames)(pair.a.object, &pair.a.object->mSavedKeys);
        }
        {
            Turn t(pair.b);
            pair.b.object->LoadSavedKeyNames(&pair.b.object->mSavedKeys);
        }
        ++ctx.cases;
        Same(ctx, "LoadSavedKeyNames", i, pair.a, pair.b);
        if (!pair.a.object->mSavedKeys.empty()) ++named;
    }
    if (named < 150) ctx.Fail("coverage: %d with names", named);
}

// ---- 0xcb1e60 LoadUserProperties, 0xcb2020 LoadPropertyList, 0xcb1780 LoadGlobalInto, 0xcb1790 LoadConfigProperties

DIFF_TEST(PropertyFiles_LoadUserProperties, kLoadUser)
{
    // The target with or without a revision, the file with the same, another or none; saved names loaded or there.
    World world(ctx);
    std::mt19937 rng(kLoadUser);
    int taken = 0, refused = 0;
    for (int i = 0; i < 400; ++i) {
        ResetScript();
        Pair pair(ctx, rng);
        pair.RandomPaths(rng);
        const std::string path = pair.a.object->mUserPropsPath.mString;
        const uint32_t revision = rng() % 3;
        if (rng() % 2) {
            SetUint(pair.a.service, "propertyRevision", revision);
            SetUint(pair.b.service, "propertyRevision", revision);
        }
        auto it = gScript.xml.find(path);
        if (it != gScript.xml.end() && rng() % 2) it->second.properties.push_back({rng() % 2 ? "propertyRevision" : "PROPERTYREVISION", rng() % 3});
        if (rng() % 4 == 0) FillNames(ctx, rng, pair.a, pair.b);
        uint32_t ra, rb;
        {
            Turn t(pair.a);
            ra = ctx.Original<LoadUserFn>(kLoadUser)(pair.a.object, static_cast<cIGZPropertySetString *>(pair.a.service),
                                                     &pair.a.object->mUserPropsPath) & 0xff;
        }
        {
            Turn t(pair.b);
            rb = pair.b.object->LoadUserProperties(pair.b.service, &pair.b.object->mUserPropsPath);
        }
        ++ctx.cases;
        (ra ? taken : refused)++;
        Same(ctx, "LoadUserProperties", i, pair.a, pair.b, Hex(ra) + " " + Dump(pair.a.service), Hex(rb) + " " + Dump(pair.b.service));
    }
    if (taken < 100 || refused < 100) ctx.Fail("coverage: %d taken, %d refused", taken, refused);
}

DIFF_TEST(PropertyFiles_LoadPropertyList, kLoadList)
{
    // ';'-separated lists: absolute paths, relative names (taken in the data directory), empty items, leading and doubled
    // delimiters.
    World world(ctx);
    std::mt19937 rng(kLoadList);
    int reads = 0;
    for (int i = 0; i < 300; ++i) {
        ResetScript();
        Pair pair(ctx, rng);
        pair.RandomPaths(rng);
        const std::string dir = Dir();
        const std::vector<std::string> kItems = {dir + "list1.xml", "list2.xml", "", "globalProps.xml", dir + "globalProps.xml",
                                                 "Config/globalProps.xml", dir + "missing.xml", "..\\x.xml"};
        std::string list;
        for (int n = static_cast<int>(rng() % 5); n > 0; --n) list += Pick(rng, kItems) + (rng() % 4 ? ";" : ";;");
        if (rng() % 2 && !list.empty()) list.pop_back();
        cRZString listA, listB;
        listA.Construct(list.c_str());
        listB.Construct(list.c_str());
        {
            Turn t(pair.a);
            ctx.Original<LoadListFn>(kLoadList)(pair.a.object, static_cast<cIGZPropertySetString *>(pair.a.service), &listA);
        }
        {
            Turn t(pair.b);
            pair.b.object->LoadPropertyList(pair.b.service, &listB);
        }
        ++ctx.cases;
        Same(ctx, "LoadPropertyList", i, pair.a, pair.b, listA.mString + " " + Dump(pair.a.service), listB.mString + " " + Dump(pair.b.service));
        reads += Lines(pair.a.log, "Read ");
        listA.Destruct();
        listB.Destruct();
    }
    if (reads < 200) ctx.Fail("coverage: %d files read", reads);
}

DIFF_TEST(PropertyFiles_LoadGlobalInto, kLoadGlobalInto)
{
    World world(ctx);
    std::mt19937 rng(kLoadGlobalInto);
    int reads = 0;
    for (int i = 0; i < 200; ++i) {
        ResetScript();
        Pair pair(ctx, rng);
        pair.RandomPaths(rng);
        ::cGZPropertySetString targetA, targetB;
        RandomFill(rng, &targetA, &targetB, 3);
        {
            Turn t(pair.a);
            ctx.Original<ThisArgFn>(kLoadGlobalInto)(pair.a.object, static_cast<cIGZPropertySetString *>(&targetA));
        }
        {
            Turn t(pair.b);
            pair.b.object->LoadGlobalInto(&targetB);
        }
        ++ctx.cases;
        Same(ctx, "LoadGlobalInto", i, pair.a, pair.b, Dump(&targetA), Dump(&targetB));
        reads += Lines(pair.a.log, "Read ");
    }
    if (reads < 80) ctx.Fail("coverage: %d files read", reads);
}

DIFF_TEST(PropertyFiles_LoadConfigProperties, kLoadConfig)
{
    // The Config file's revision dropped before the copy.
    World world(ctx);
    std::mt19937 rng(kLoadConfig);
    int reads = 0;
    for (int i = 0; i < 200; ++i) {
        ResetScript();
        Pair pair(ctx, rng);
        pair.RandomPaths(rng);
        auto it = gScript.xml.find(pair.a.object->mConfigPropsPath.mString);
        if (it != gScript.xml.end() && rng() % 2) it->second.properties.push_back({"propertyRevision", 7});
        {
            Turn t(pair.a);
            ctx.Original<ThisFn>(kLoadConfig)(pair.a.object);
        }
        {
            Turn t(pair.b);
            pair.b.object->LoadConfigProperties();
        }
        ++ctx.cases;
        Same(ctx, "LoadConfigProperties", i, pair.a, pair.b, Dump(pair.a.service), Dump(pair.b.service));
        reads += Lines(pair.a.log, "Read ");
    }
    if (reads < 80) ctx.Fail("coverage: %d files read", reads);
}

// ---- 0xcb18a0 LoadProperties, 0xcb28b0 Save

DIFF_TEST(PropertyFiles_LoadProperties, kLoadProperties)
{
    // The switches (each answered with al set or not, the upper bytes set), a property list or the global and user files.
    World world(ctx);
    std::mt19937 rng(kLoadProperties);
    int lists = 0, users = 0;
    for (int i = 0; i < 400; ++i) {
        ResetScript();
        for (const char *name : {"propsNoSave", "propsNoLoad", "cardConfig"})
            if (rng() % 4 == 0) gScript.switches[name] = static_cast<uint8_t>(rng() % 2 ? 1 : 0);
        gScript.haveList = rng() % 4 == 0;
        gScript.listText = "list1.xml;" + Dir() + "list2.xml";
        Pair pair(ctx, rng);
        pair.RandomPaths(rng);
        pair.Flags(rng);
        {
            Turn t(pair.a);
            ctx.Original<ThisFn>(kLoadProperties)(pair.a.object);
        }
        {
            Turn t(pair.b);
            pair.b.object->LoadProperties();
        }
        ++ctx.cases;
        if (gScript.haveList) ++lists;
        if (!gScript.haveList && pair.a.object->mLoadUser) ++users;
        Same(ctx, "LoadProperties", i, pair.a, pair.b, Dump(pair.a.service), Dump(pair.b.service));
    }
    if (lists < 50 || users < 100) ctx.Fail("coverage: %d lists, %d user files", lists, users);
}

DIFF_TEST(PropertyFiles_Save, kSave)
{
    // Saving on or off, names from the files or there already, none; the user's file missing, writable or read-only.
    World world(ctx);
    std::mt19937 rng(kSave);
    int written = 0;
    for (int i = 0; i < 400; ++i) {
        ResetScript();
        Pair pair(ctx, rng);
        pair.RandomPaths(rng);
        pair.Flags(rng);
        if (rng() % 4 == 0) FillNames(ctx, rng, pair.a, pair.b);
        {
            Turn t(pair.a);
            ctx.Original<ThisFn>(kSave)(pair.a.object);
        }
        {
            Turn t(pair.b);
            pair.b.object->Save();
        }
        ++ctx.cases;
        for (const std::string &l : pair.a.log)
            if (l.rfind("Write ", 0) == 0) ++written;
        Same(ctx, "Save", i, pair.a, pair.b, Dump(pair.a.service), Dump(pair.b.service));
    }
    if (written < 100) ctx.Fail("coverage: %d written", written);
}

// ---- 0x1c1931 PathAttributesMatch

namespace {

std::vector<std::string> *gAnsiLog = nullptr;
DWORD WINAPI FakeGetFileAttributesA(LPCSTR path)
{
    using WideFn = DWORD(WINAPI *)(LPCWSTR);
    auto wide = reinterpret_cast<WideFn>(difftest::SandboxFunction("GetFileAttributesW"));
    const std::wstring w(path, path + std::strlen(path));
    const DWORD attributes = wide(w.c_str());
    if (gAnsiLog) gAnsiLog->push_back(std::string("GetFileAttributesA ") + path + " " + Hex(attributes));
    return attributes;
}

int32_t gShortcutAnswer = 0;
int32_t __cdecl FakeResolveShortcut(const char *path, char *target, int32_t size)
{
    if (gAnsiLog) gAnsiLog->push_back(std::string("ResolveShortcutA ") + path + " " + std::to_string(size));
    if (target && size > 0) target[0] = 0;
    return gShortcutAnswer;
}

} // namespace

DIFF_TEST(PropertyFiles_PathAttributes, kAttributes)
{
    // Sandbox files with each attribute (read-only, hidden, system, archive, a directory, missing), every flag the code
    // tests in random combinations, the wide function and (its global cleared) GetFileAttributesA; the shortcut flag
    // with the shortcut resolver answering 0, -1 or a length.
    World world(ctx, false);
    shape::StandIn shortcut(0x1c3059, &cRZFile::ResolveShortcutA, &FakeResolveShortcut);
    void **iat = ctx.image.At<void *>(kIatGetFileAttributesA);
    void *savedIat = *iat;
    *iat = reinterpret_cast<void *>(&FakeGetFileAttributesA);
    void **wideCell = ctx.image.At<void *>(kWideGetFileAttributesCell);
    void *wide = *wideCell;
    const std::string dir = Dir();
    struct Entry {
        const char *name;
        DWORD attributes; // 0: a directory; ~0: missing
    };
    const Entry entries[] = {{"plain.txt", FILE_ATTRIBUTE_NORMAL}, {"ro.txt", FILE_ATTRIBUTE_READONLY}, {"hidden.txt", FILE_ATTRIBUTE_HIDDEN},
                             {"system.txt", FILE_ATTRIBUTE_SYSTEM}, {"archive.txt", FILE_ATTRIBUTE_ARCHIVE},
                             {"all.txt", FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE},
                             {"folder", 0}, {"missing.txt", ~0u}};
    for (const Entry &e : entries) {
        const std::string path = dir + e.name;
        if (e.attributes == 0)
            CreateDirectoryA(path.c_str(), nullptr);
        else if (e.attributes != ~0u) {
            WriteText(path, "x");
            SetFileAttributesA(path.c_str(), e.attributes);
        }
    }
    const uint32_t kBits[] = {1, 2, 8, 0x10, 0x20, 0x40, 0x80, 0x400, 0x1000, 0x4, 0x100};
    std::mt19937 rng(kAttributes);
    int met = 0, unmet = 0;
    for (int i = 0; i < 2000; ++i) {
        const Entry &e = entries[rng() % (sizeof(entries) / sizeof(entries[0]))];
        uint32_t flags = 0;
        for (int n = static_cast<int>(rng() % 4); n > 0; --n) flags |= kBits[rng() % (sizeof(kBits) / sizeof(kBits[0]))];
        const bool ansi = rng() % 3 == 0;
        *wideCell = ansi ? nullptr : wide;
        const int32_t answers[] = {0, -1, 5};
        gShortcutAnswer = answers[rng() % 3];
        cRZString path;
        path.Construct((dir + e.name).c_str());
        std::vector<std::string> logA, logB;
        gAnsiLog = &logA;
        const uint32_t ra = ctx.Original<AttributesFn>(kAttributes)(path.AsInterface(), flags);
        gAnsiLog = &logB;
        const uint32_t rb = PathAttributesMatch(path.AsInterface(), flags);
        gAnsiLog = nullptr;
        path.Destruct();
        ++ctx.cases;
        ((ra & 0xff) ? met : unmet)++;
        // al only: the original's upper bytes are what its last flag test left in eax, and every caller reads al
        // (0xcb1cf0 / 0xcb28b0 `test al, al` after the call, as do 0x1de470, 0x1e36f0, 0x1d2d50, 0x1d4220, 0x4d22b0).
        if ((ra & 0xff) != (rb & 0xff) || logA != logB)
            ctx.Fail("PathAttributes case %d (%s, %x, %s): %x / %x\n  %s", i, e.name, flags, ansi ? "ansi" : "wide", ra, rb,
                     adt::FirstDifference(logA, logB).c_str());
    }
    *wideCell = wide;
    *iat = savedIat;
    for (const Entry &e : entries) {
        const std::string path = dir + e.name;
        if (e.attributes == 0)
            RemoveDirectoryA(path.c_str());
        else if (e.attributes != ~0u) {
            SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_NORMAL);
            DeleteFileA(path.c_str());
        }
    }
    if (met < 500 || unmet < 500) ctx.Fail("coverage: %d met, %d not", met, unmet);
}
