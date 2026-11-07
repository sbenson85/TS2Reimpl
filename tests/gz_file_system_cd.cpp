// Differential test for cGZFileSystem::CheckCDDrive (src/app/AppServices.size.cpp), slot 14 of the primary vtable,
// 0xc76702: whether the drive in mCDAppDrive holds the game, by Maxis.ini (mUseCDIniFile), by the expected volume name
// and application path, or by searching the drive for the executable (mAllowRecursiveCDSearch).
//
// Side A is an object built by the original constructor (0xc75ccd) and checked by 0xc76702; side B is built by our
// constructor's adapter and checked through slot 14 of our own primary vtable. Each side carries copies of its own
// vtables with the file-system face's DataDirectory / PlugInDirectory (slots 7 and 8) replaced by the same scripted fakes.
// The callees are stand-ins on both sides (the image's function and, where we have one, ours): cRZFile::PathExists
// (0x1c1242), the cRZIniFile constructor / GetString / destructor (0x9c5d11, 0x9c679f, 0x9c5e5d), the directory scan
// (0x1c48da constructor, 0x1c63d4 SetPattern, 0x1c5bcb file list, 0x1c49eb destructor: the image's and ours, lane LY) and
// the volume name (0x1c20d8). The framework global (0x10c9378) holds a fake whose slot 37 answers a fake application
// whose slot 5 is the module name. GetModuleDirectory (0x1c1c8c) and _stricmp run for real (ours / the image's).
#include "app/AppServices.h"
#include "code_patch.h"
#include "difftest.h"
#include "game/game.h"
#include "rz/DirectoryScan.h"
#include "rz/cRZFile.h"
#include "rz/cRZIniFile.h"
#include "rz/cRZString.h"
#include "service_shape.h"

#include <windows.h>

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kCheck = 0xc76702;
constexpr uint32_t kConstruct = 0xc75ccd, kDestruct = 0xc75d9b;
constexpr uint32_t kFrameworkGlobal = 0x10c9378;
constexpr uint32_t kStringOffsets[] = {0x24, 0x44, 0x64, 0x84, 0xa4, 0xc4, 0xe4, 0x108, 0x128, 0x148, 0x168, 0x188, 0x1a8};
constexpr uint32_t kVTableOffsets[] = {0, 4, cGZFileSystem::kFace};

std::vector<std::string> gLog;

void Log(const char *fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    gLog.emplace_back(buf);
}

std::string Joined()
{
    std::string out;
    for (const std::string &s : gLog) out += s + "|";
    return out;
}

const std::string &TextOf(const void *string)
{
    return static_cast<const cRZString *>(string)->mString;
}

// ---- the script ----

struct Script {
    std::vector<uint32_t> exists;  // PathExists answers in call order (eax; only al counts); then 0
    size_t existsAt = 0;
    std::string moduleName;
    uint32_t found[3] = {};        // GetString answers for App, Data, PlugIn (eax)
    std::string values[3];
    uint32_t volumeResult = 0;     // 0x1c20d8's eax
    std::string volume;
    uint32_t listResult = 0;       // 0x1c5bcb's eax
    std::vector<std::string> files;
    std::string dataDirectory, plugInDirectory;
};
Script gScript;

// The section and key GetString was asked for, by key text.
int KeyIndex(const std::string &key)
{
    return key == "App" ? 0 : key == "Data" ? 1 : key == "PlugIn" ? 2 : -1;
}

uint32_t __cdecl StandPathExists(const cIGZString *path)
{
    uint32_t answer = gScript.existsAt < gScript.exists.size() ? gScript.exists[gScript.existsAt] : 0;
    ++gScript.existsAt;
    Log("exists(%s)=%x", TextOf(path).c_str(), answer);
    return answer;
}

void *__fastcall StandIniConstruct(void *self, void *, const cIGZString *path)
{
    Log("ini(%s)", TextOf(path).c_str());
    std::memset(self, 0x6b, 0x40);
    return self;
}

uint32_t __fastcall StandIniGetString(void *self, void *, const cIGZString *section, const cIGZString *key, cIGZString *out)
{
    int k = KeyIndex(TextOf(key));
    uint32_t answer = k >= 0 ? gScript.found[k] : 0;
    Log("get(%02x,%s,%s,%s)=%x", *static_cast<uint8_t *>(self), TextOf(section).c_str(), TextOf(key).c_str(), TextOf(out).c_str(), answer);
    // The original writes the value only when it finds the key.
    if (k >= 0 && (answer & 0xff)) reinterpret_cast<cRZString *>(out)->mString = gScript.values[k];
    return answer;
}

void __fastcall StandIniDestruct(void *self, void *)
{
    Log("~ini(%02x)", *static_cast<uint8_t *>(self));
}

// The scan object: its 0x5c bytes are filled with a marker so the calls after the constructor can check `this`.
void *__fastcall StandScanConstruct(void *self, void *, const void *directory, uint32_t a, uint32_t b, uint32_t c)
{
    Log("scan(%s,%x,%x,%x)", TextOf(directory).c_str(), a, b, c);
    std::memset(self, 0x5c, 0x5c);
    return self;
}

void __fastcall StandScanDestruct(void *self, void *)
{
    Log("~scan(%02x)", *static_cast<uint8_t *>(self));
}

void __fastcall StandSetPattern(void *self, void *, const void *pattern, uint32_t scanNow)
{
    Log("pattern(%02x,%s,%x)", *static_cast<uint8_t *>(self), TextOf(pattern).c_str(), scanNow);
}

// An MSVC std::list: the head node and the size; nodes are {next, prev, value}.
struct ListNode {
    ListNode *next;
    ListNode *prev;
    unsigned char value[0x20];
};
struct ListShape {
    ListNode *head;
    uint32_t size;
};

uint32_t __fastcall StandListFiles(void *self, void *, const void *directory, ListShape *list, uint32_t recursive)
{
    Log("list(%02x,%s,%u,%x)=%x", *static_cast<uint8_t *>(self), TextOf(directory).c_str(), list->size, recursive, gScript.listResult);
    for (const std::string &file : gScript.files) {
        auto *node = static_cast<ListNode *>(::operator new(sizeof(ListNode)));
        reinterpret_cast<cRZString *>(node->value)->Construct(file.c_str());
        ListNode *head = list->head;
        node->next = head;
        node->prev = head->prev;
        head->prev->next = node;
        head->prev = node;
        ++list->size;
    }
    return gScript.listResult;
}

uint32_t __cdecl StandVolumeName(const void *root, void *out)
{
    Log("volume(%s,%s)=%x", TextOf(root).c_str(), TextOf(out).c_str(), gScript.volumeResult);
    if (gScript.volumeResult & 0xff) static_cast<cRZString *>(out)->mString = gScript.volume;
    return gScript.volumeResult;
}

// The face's slots 7 and 8.
const char *__fastcall FakeDataDirectory(void *, void *)
{
    Log("data");
    return gScript.dataDirectory.c_str();
}

const char *__fastcall FakePlugInDirectory(void *, void *)
{
    Log("plugin");
    return gScript.plugInDirectory.c_str();
}

// The framework and the application.
const char *__fastcall FakeModuleName(void *, void *)
{
    Log("module");
    return gScript.moduleName.c_str();
}
void *gAppVTable[8];
struct FakeApp {
    void **vtable = gAppVTable;
} gApp;

void *__fastcall FakeApplication(void *, void *)
{
    Log("app");
    return &gApp;
}
void *gFrameworkVTable[40];
struct FakeFramework {
    void **vtable = gFrameworkVTable;
} gFramework;

// ---- the objects ----

struct SideVTables {
    void *real[3] = {};
    void *primary[15], *unknown[6], *face[23];
};
SideVTables gGame, gOurs;
svc::CodeMap gGameCodes, gOurCodes;

void Patch(SideVTables &v)
{
    std::memcpy(v.primary, v.real[0], sizeof(v.primary));
    std::memcpy(v.unknown, v.real[1], sizeof(v.unknown));
    std::memcpy(v.face, v.real[2], sizeof(v.face));
    v.face[7] = reinterpret_cast<void *>(&FakeDataDirectory);
    v.face[8] = reinterpret_cast<void *>(&FakePlugInDirectory);
}

void SetupVTables(difftest::Context &ctx)
{
    gGame.real[0] = ctx.image.At(cGZFileSystem::kVTable);
    gGame.real[1] = ctx.image.At(cGZFileSystem::kUnknownVTable);
    gGame.real[2] = ctx.image.At(cGZFileSystem::kFileSystemVTable);
    alignas(8) static unsigned char probe[cGZFileSystem::kSize];
    cGZFileSystem *ours = cGZFileSystemAdapters::Construct(probe, nullptr);
    for (int i = 0; i < 3; ++i) gOurs.real[i] = svc::VTableAt(probe, kVTableOffsets[i]);
    ours->cGZFileSystem::~cGZFileSystem();
    Patch(gGame);
    Patch(gOurs);
    for (uint32_t i = 0; i < 3; ++i) {
        gGameCodes.Add(gGame.real[i], 1 + i);
        gOurCodes.Add(gOurs.real[i], 1 + i);
    }
    gGameCodes.Add(gGame.primary, 1);
    gGameCodes.Add(gGame.unknown, 2);
    gGameCodes.Add(gGame.face, 3);
    gOurCodes.Add(gOurs.primary, 1);
    gOurCodes.Add(gOurs.unknown, 2);
    gOurCodes.Add(gOurs.face, 3);
}

// The object's state: the vtable words as codes, every string's size, capacity and text, the other bytes.
std::string Dump(const unsigned char *object, const svc::CodeMap &codes)
{
    std::string out;
    std::vector<bool> skip(cGZFileSystem::kSize, false);
    for (uint32_t off : kVTableOffsets) {
        uint32_t word;
        std::memcpy(&word, object + off, 4);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "vt%x=%x ", off, codes.Code(word));
        out += buf;
        for (uint32_t i = off; i < off + 4; ++i) skip[i] = true;
    }
    for (uint32_t off : kStringOffsets) {
        const std::string &s = reinterpret_cast<const cRZString *>(object + off)->mString;
        out += std::to_string(off) + ":" + std::to_string(s.size()) + "/" + std::to_string(s.capacity()) + ":" + s + "|";
        for (uint32_t i = off + 4; i < off + 0x1c; ++i) skip[i] = true;
    }
    for (uint32_t i = 0; i < cGZFileSystem::kSize; ++i) {
        if (skip[i]) continue;
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%02x", object[i]);
        out += buf;
    }
    return out;
}

struct Case {
    std::string cdDrive, appName, appExtension, expectedName, expectedPath, cdApp, cdData, cdPlugIn;
    uint8_t useIni = 0, allowSearch = 0;
};

// Builds the side's object over `fill`, sets the case's fields, checks the drive and returns "ret | log | state".
std::string RunSide(difftest::Context &ctx, int side, const Case &c, const Script &script, uint8_t fill)
{
    alignas(8) unsigned char object[cGZFileSystem::kSize];
    std::memset(object, fill, sizeof(object));
    if (side == 0) ctx.Original<void *(__thiscall *)(void *)>(kConstruct)(object);
    else cGZFileSystemAdapters::Construct(object, nullptr);
    auto *fs = reinterpret_cast<cGZFileSystem *>(object);
    fs->mCDAppDrive.mString = c.cdDrive;
    fs->mAppName.mString = c.appName;
    fs->mAppExtension.mString = c.appExtension;
    fs->mExpectedCDName.mString = c.expectedName;
    fs->mExpectedCDAppPath.mString = c.expectedPath;
    fs->mCDAppDirectory.mString = c.cdApp;
    fs->mCDDataDirectory.mString = c.cdData;
    fs->mCDPlugInDirectory.mString = c.cdPlugIn;
    fs->mUseCDIniFile = c.useIni;
    fs->mAllowRecursiveCDSearch = c.allowSearch;
    SideVTables &v = side == 0 ? gGame : gOurs;
    void *words[3] = {v.primary, v.unknown, v.face};
    for (int i = 0; i < 3; ++i) std::memcpy(object + kVTableOffsets[i], &words[i], 4);
    gScript = script;
    gLog.clear();
    using CheckFn = uint32_t(__thiscall *)(void *);
    // Side B through slot 14 of our own primary vtable (the real one, not the patched copy's: they hold the same there).
    CheckFn check = side == 0 ? ctx.Original<CheckFn>(kCheck) : reinterpret_cast<CheckFn>(static_cast<void **>(gOurs.real[0])[14]);
    uint32_t result = check(object) & 0xff;
    std::string out = std::to_string(result) + " | " + Joined() + " | " + Dump(object, side == 0 ? gGameCodes : gOurCodes);
    if (side == 0) ctx.Original<void(__thiscall *)(void *)>(kDestruct)(object);
    else fs->cGZFileSystem::~cGZFileSystem();
    return out;
}

const char *Pick(std::mt19937 &rng, std::initializer_list<const char *> options)
{
    return options.begin()[rng() % options.size()];
}

uint32_t Answer(std::mt19937 &rng)
{
    // al decides; the upper bytes are garbage in some answers.
    switch (rng() % 4) {
    case 0: return 0;
    case 1: return 1;
    case 2: return 0xabcd0100u; // al 0
    default: return 0x12345601u;
    }
}

} // namespace

DIFF_TEST(GZFileSystem_CheckCDDrive, kCheck)
{
    SetupVTables(ctx);
    gAppVTable[5] = reinterpret_cast<void *>(&FakeModuleName);
    gFrameworkVTable[37] = reinterpret_cast<void *>(&FakeApplication);
    void **framework = ctx.image.At<void *>(kFrameworkGlobal);
    void *savedFramework = *framework;
    *framework = &gFramework;

    shape::StandIn exists(0x1c1242, &cRZFile::PathExists, &StandPathExists);
    shape::StandIn iniConstruct(0x9c5d11, &cRZIniFile::Construct, &StandIniConstruct);
    shape::StandIn iniGetString(0x9c679f, &cRZIniFile::GetString, &StandIniGetString);
    shape::StandIn iniDestruct(0x9c5e5d, &cRZIniFile::Destruct, &StandIniDestruct);
    shape::StandIn scanConstruct(0x1c48da, &DirectoryScanAdapters::Construct, &StandScanConstruct);
    shape::StandIn scanDestruct(0x1c49eb, &DirectoryScanAdapters::Destruct, &StandScanDestruct);
    shape::StandIn setPattern(0x1c63d4, &DirectoryScan::SetPattern, &StandSetPattern);
    shape::StandIn listFiles(0x1c5bcb, &DirectoryScan::ListFilesBelow, &StandListFiles);
    shape::CodePatch volumeName(0x1c20d8, reinterpret_cast<const void *>(&StandVolumeName));

    const char *longText = "a text long enough to sit in a heap buffer of its own";
    std::mt19937 rng(kCheck);
    int branches[5] = {};
    for (int run = 0; run < 3000; ++run) {
        Case c;
        Script s;
        c.cdDrive = run % 50 == 0 ? "" : Pick(rng, {"E:\\", "f:\\", "Z:\\games\\", "C:"});
        c.appName = Pick(rng, {"Sims2EP9", "", "x"});
        c.appExtension = Pick(rng, {".exe", "", ".longer-extension-text"});
        c.cdApp = Pick(rng, {"", "old app", longText});
        c.cdData = Pick(rng, {"", "old data", longText});
        c.cdPlugIn = Pick(rng, {"", "old plug-in", longText});
        // The branch: Maxis.ini, no search, search by name / path, search for the executable.
        int branch = run % 5;
        c.useIni = branch == 0 ? static_cast<uint8_t>(rng() % 3 == 0 ? 0x80 : 1) : 0;
        c.allowSearch = branch == 1 ? 0 : static_cast<uint8_t>(rng() % 4 == 0 ? 0x40 : 1);
        if (branch == 2 || branch == 3) {
            int which = static_cast<int>(rng() % 3); // name only, path only, both
            if (which != 1) c.expectedName = Pick(rng, {"SIMS2", "TheSims2", "", "x"});
            if (which != 0) c.expectedPath = Pick(rng, {"TSBin\\Sims2EP9.exe", "a\\b\\c.exe", "plain.exe"});
            if (c.expectedName.empty() && c.expectedPath.empty()) c.expectedPath = "TSBin\\Sims2EP9.exe";
        }
        // Mostly scripts that pass the early checks.
        for (int i = 0; i < 3; ++i) s.exists.push_back(rng() % 4 == 0 ? Answer(rng) : (rng() & 1 ? 1u : 0x0f01u));
        s.moduleName = rng() % 6 == 0 ? "" : Pick(rng, {"Sims2EP9", "The Sims 2", "s"});
        for (int i = 0; i < 3; ++i) {
            s.found[i] = Answer(rng);
            s.values[i] = Pick(rng, {"", "TSBin\\", "Data", longText});
        }
        s.volumeResult = rng() % 5 == 0 ? Answer(rng) : 1;
        // A volume name equal to the expected one (in another case) half the time.
        if (rng() & 1) {
            s.volume = c.expectedName;
            for (char &ch : s.volume) ch = static_cast<char>(rng() & 1 ? std::tolower(static_cast<unsigned char>(ch)) : std::toupper(static_cast<unsigned char>(ch)));
        } else {
            s.volume = Pick(rng, {"SIMS2", "OTHER", "", "SIMS"});
        }
        s.listResult = rng() % 5 == 0 ? Answer(rng) : 1;
        int files = static_cast<int>(rng() % 4); // 0..3
        for (int i = 0; i < files; ++i) s.files.push_back(Pick(rng, {"E:\\TSBin\\Sims2EP9.exe", "E:\\x.exe", "E:\\a\\very\\deep\\folder\\with a long name\\Sims2EP9.exe", "nofolder.exe"}));
        s.dataDirectory = Pick(rng, {"C:\\Data\\", "", longText});
        s.plugInDirectory = Pick(rng, {"C:\\PlugIns\\", "", longText});
        const uint8_t fill = static_cast<uint8_t>(rng());
        std::string a = RunSide(ctx, 0, c, s, fill);
        std::string b = RunSide(ctx, 1, c, s, fill);
        ++ctx.cases;
        if (a[0] == '1') ++branches[branch];
        if (a != b) ctx.Fail("run %d (branch %d):\n  orig %s\n  ours %s", run, branch, a.c_str(), b.c_str());
    }
    *framework = savedFramework;
    // Each branch must succeed in some runs (branch 1, no search allowed, never does).
    for (int branch : {0, 2, 3, 4})
        if (branches[branch] < 20) ctx.Fail("branch %d succeeded only %d times", branch, branches[branch]);
    if (branches[1] != 0) ctx.Fail("the no-search branch succeeded");
    if (!HeapValidate(GetProcessHeap(), 0, nullptr)) ctx.Fail("process heap corrupted");
}
