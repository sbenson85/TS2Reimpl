// Differential tests for cTSIniFile (src/rz/cTSIniFile.cpp) and the application's ini command-line step
// (src/app/AppServices3.cpp):
//   0xcb2a20 constructor, 0xcb2cc0 destructor body, 0xcb2ce0 deleting destructor, 0xcb2e90 GetString (slot 6),
//   0xcb2d00 section walk (slot 13), 0xcb30f0 HasSection with prefixes (slot 20), 0x9c6a0e HasSection (cRZIniFile
//   slot 15), 0xc2b078 AppendIniCommandLine.
//
// Object pairs: side A is built, used and destroyed by the original code, side B by ours; both carry the original
// vtable, so the slots the code calls through it (load, clear, the base section walk) are the game's on both sides.
// The framework (game global 0x10c9378) and the cTSAppCommonImpl object (cache global 0x10cc040) are fakes with
// recording vtables; each side has its own sandboxed copy of the same ini text.
#include "app/AppInit.h"
#include "app/AppServices3.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game/vtable.h"
#include "rz/cRZFile.h"
#include "rz/cRZIniFile.h"
#include "rz/cRZString.h"
#include "rz/cTSIniFile.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

using CtorFn = void *(__thiscall *)(void *, const cIGZString *, const cRZString *);
using DtorFn = void(__thiscall *)(void *);
using DeletingFn = void *(__thiscall *)(void *, uint32_t);
using GetStringFn = bool(__thiscall *)(void *, const cIGZString *, const cIGZString *, cIGZString *);
using WalkFn = bool(__thiscall *)(void *, const cIGZString *, void *, void *);
using HasFn = bool(__thiscall *)(void *, const cIGZString *);
using UintThiscall0 = uint32_t(__thiscall *)(void *);

constexpr uint32_t kCtor = 0xcb2a20, kDtor = 0xcb2cc0, kDeleting = 0xcb2ce0, kGetString = 0xcb2e90, kWalk = 0xcb2d00, kHasPrefixed = 0xcb30f0, kHasBase = 0x9c6a0e;
constexpr uint32_t kAppend = 0xc2b078;
constexpr uint32_t kFrameworkGlobal = 0x10c9378, kCommonCache = 0x10cc040;

std::string ToUtf8(const std::wstring &w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), n, nullptr, nullptr);
    return s;
}

bool WriteFileBytes(const std::wstring &path, const std::string &text)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size();
    CloseHandle(h);
    return ok;
}

// The game fills these globals with its wide file functions at start-up; the sandbox provides the same
// functions, so the original's file code runs against the sandbox.
const uint32_t kWideGlobals[] = {0x10cb5e0, 0x10cb5e4, 0x10cb5e8, 0x10cb5ec, 0x10cb5f0, 0x10cb5f4};
const char *const kWideNames[] = {"CreateFileW", "CopyFileW", "MoveFileExW", "FindFirstFileW", "DeleteFileW", "GetFileAttributesW"};

void InstallWideFunctions(difftest::Context &ctx)
{
    for (int i = 0; i < 6; ++i) *ctx.image.At<void *>(kWideGlobals[i]) = difftest::SandboxFunction(kWideNames[i]);
}

struct Str {
    alignas(cRZString) unsigned char raw[sizeof(cRZString)];
    explicit Str(const std::string &text = "")
    {
        std::memset(raw, 0xcd, sizeof(raw));
        S().Construct();
        S().mString = text;
    }
    ~Str() { S().Destruct(); }
    Str(const Str &) = delete;
    cRZString &S() { return *reinterpret_cast<cRZString *>(raw); }
    const cIGZString *I() { return S().AsInterface(); }
};

bool SameString(const cRZString &a, const cRZString &b)
{
    return a.mVTable == b.mVTable && a.mString == b.mString && a.mString.capacity() == b.mString.capacity() && a.mRefCount == b.mRefCount;
}

std::string Describe(const cRZString &s)
{
    char buf[200];
    std::snprintf(buf, sizeof(buf), "\"%.60s\" size %zu cap %zu refs %u", s.mString.c_str(), s.mString.size(), s.mString.capacity(), s.mRefCount);
    return buf;
}

// ---- the fakes ---------------------------------------------------------------------------------------------

struct Call {
    int what;
    std::string a, b;
    uint32_t x = 0, y = 0;
    bool operator==(const Call &o) const { return what == o.what && a == o.a && b == o.b && x == o.x && y == o.y; }
};

std::vector<Call> gCalls;
bool gContextPresent = false;
std::string gContextValue;
bool gDirFlag = false;       // the answer of the 0x4bcfce5e query
std::string gDirText;        // the text it writes
std::string gPathText;       // what the 0x6c253729 query writes (the ini path of the current side)
int32_t gArgc = 0;           // the fake command line's argument count

std::string Seen(const cRZString *s)
{
    std::string seen = s->mVTable ? "vptr:" : "novptr:";
    seen += s->mString;
    seen += s->mString.capacity() == 15 ? "|sso" : "|big";
    return seen;
}

// cTSAppCommonImpl slot 4 (ret 0x14).
uint32_t __fastcall CommonSlot4(void *, void *, uint32_t id, cRZString *text, uint32_t c, uint32_t d, uint32_t e)
{
    gCalls.push_back({4, Seen(text), "", id, (c << 8) ^ (d << 4) ^ e});
    if (id == 0x4bcfce5e) {
        text->mString = gDirText;
        return gDirFlag ? 0xABCDEF01u : 0xABCDEF00u;
    }
    if (id == 0x6c253729) {
        text->mString = gPathText;
        return 0x55;
    }
    return 0x66;
}

// cIGZCmdLine slot 3 (ArgC), slot 8 (IsSwitchPresent(name, param, trimQuotes), ret 0xc), slot 13 (InsertArgument).
int32_t __fastcall CmdArgC(void *, void *)
{
    gCalls.push_back({3, "", "", 0, 0});
    return gArgc;
}

uint32_t __fastcall CmdIsSwitchPresent(void *, void *, cRZString *name, cRZString *param, uint32_t trim)
{
    gCalls.push_back({8, Seen(name), Seen(param), trim, 0});
    if (name->mString == "iniContext" && gContextPresent) {
        param->mString = gContextValue;
        return 0xABCDEF01u;
    }
    return 0xABCDEF00u;
}

uint32_t __fastcall CmdInsertArgument(void *, void *, cRZString *argument, int32_t index)
{
    gCalls.push_back({13, Seen(argument), "", static_cast<uint32_t>(index), 0});
    ++gArgc;
    return 0xABCDEF01u;
}

struct FakeObject {
    void **vtable;
    uint32_t pad[3];
};

void *__fastcall FrameworkCommandLine(void *, void *);

void *gFwVtable[64];
void *gCmdVtable[16];
void *gCommonVtable[16];
FakeObject gFramework, gCmd, gCommon;

void *__fastcall FrameworkCommandLine(void *, void *)
{
    gCalls.push_back({21, "", "", 0, 0});
    return &gCmd;
}

void SetupFakes(difftest::Context &ctx)
{
    std::memset(gFwVtable, 0, sizeof(gFwVtable));
    std::memset(gCmdVtable, 0, sizeof(gCmdVtable));
    std::memset(gCommonVtable, 0, sizeof(gCommonVtable));
    gFwVtable[21] = reinterpret_cast<void *>(&FrameworkCommandLine);
    gCmdVtable[3] = reinterpret_cast<void *>(&CmdArgC);
    gCmdVtable[8] = reinterpret_cast<void *>(&CmdIsSwitchPresent);
    gCmdVtable[13] = reinterpret_cast<void *>(&CmdInsertArgument);
    gCommonVtable[4] = reinterpret_cast<void *>(&CommonSlot4);
    gFramework.vtable = gFwVtable;
    gCmd.vtable = gCmdVtable;
    gCommon.vtable = gCommonVtable;
    *ctx.image.At<void *>(kFrameworkGlobal) = &gFramework;
    *ctx.image.At<void *>(kCommonCache) = &gCommon;
}

void ClearFakes(difftest::Context &ctx)
{
    *ctx.image.At<void *>(kFrameworkGlobal) = nullptr;
    *ctx.image.At<void *>(kCommonCache) = nullptr;
}

std::string DescribeCalls(const std::vector<Call> &calls)
{
    std::string s;
    for (const Call &c : calls) s += std::to_string(c.what) + "(" + c.a + "|" + c.b + "|" + std::to_string(c.x) + "," + std::to_string(c.y) + ") ";
    return s;
}

// ---- the files -----------------------------------------------------------------------------------------

std::wstring SandboxFileW(char side, int index)
{
    std::string n = std::string(1, side) + "t" + std::to_string(index) + ".ini";
    return difftest::SandboxPath(std::wstring(n.begin(), n.end()));
}

std::string SandboxFile(char side, int index)
{
    return ToUtf8(SandboxFileW(side, index));
}

const char *const kIniLines[] = {
    "[Sims]\r\n",         "Width=12\r\n",      "Path=%GameDataDir%\\Data\r\n",   "[Pre-Sims]\r\n",       "Width=34\r\n",      "Path=%gamedatadir%\\a\\%GAMEDATADIR%\r\n",
    "[Alt-Sims]\r\n",     "Width=56\r\n",      "[Options]\r\n",                  "Volume=21\r\n",        "[Pre-Options]\r\n",  "Volume=7\r\n",
    "[AltPre-Options]\r\n", "Volume=9\r\n",     "[x]\r\n",                        "k =  7 \r\n",          "[Pre-x]\r\n",        "k=2\r\n",
    "; comment\r\n",      "\r\n",              "Key=Value %GameDataDir% twice %GameDataDir%\r\n", "[ExtraPrefixLongerThanFifteen-x]\r\n", "k=long\r\n",
    "[SIMS]\r\n",         "Width=99\r\n",      "NAME=Mixed Case\r\n",            "[Pre-Sims]\r\n",       "Width=35\r\n",      "last=no newline"};

std::string RandomIniText(std::mt19937 &rng)
{
    std::string text;
    int n = 2 + static_cast<int>(rng() % 16);
    for (int i = 0; i < n; ++i) text += kIniLines[rng() % (sizeof(kIniLines) / sizeof(kIniLines[0]))];
    return text;
}

const std::vector<std::string> kSections = {"Sims", "SIMS", "sims", "Options", "x", "Missing", "", "Sims", "Options", "x", "Pre-Sims"};
const std::vector<std::string> kKeys = {"Width", "WIDTH", "Path", "Volume", "k", "name", "Key", "last", "missing", "", "Width", "Path"};
const char *const kExtras[] = {nullptr, "Pre", "", "Alt", "ExtraPrefixLongerThanFifteen", "Pre", nullptr};
// The values of the iniContext switch; "AltPre" is what the glued-together tokens look like (see ORIGINAL-BUG).
const char *const kContexts[] = {"Alt", "Alt;Pre", ";;Alt;;Pre;;", ";", "", "Pre;Alt;Pre;x;y;z;w;v;u;t", "Alt;", ";Alt", "A;B;C;D;E;F;G;H;I", "LongPrefixNumberOne;LongPrefixNumberTwo"};

// ---- comparison --------------------------------------------------------------------------------------------

struct Pair {
    alignas(8) unsigned char rawA[cTSIniFile::kSize];
    alignas(8) unsigned char rawB[cTSIniFile::kSize];
    cTSIniFile &A() { return *reinterpret_cast<cTSIniFile *>(rawA); }
    cTSIniFile &B() { return *reinterpret_cast<cTSIniFile *>(rawB); }
    explicit Pair(std::mt19937 &rng)
    {
        // Both sides' objects start with the same filler (padding at 0x26/0x27 and 0x3c is never written).
        for (size_t i = 0; i < sizeof(rawA); ++i) rawA[i] = rawB[i] = static_cast<unsigned char>(rng());
    }
};

uint32_t TreeSize(const uint8_t *header)
{
    uint32_t size;
    std::memcpy(&size, header + 4, sizeof(size));
    return size;
}

bool SameFile(const cRZFile &a, const cRZFile &b)
{
    return a.mRefCount == b.mRefCount && a.mOpen == b.mOpen && a.mAccess == b.mAccess && a.mCreation == b.mCreation && a.mSharing == b.mSharing && a.mError == b.mError &&
           a.mCachedLength == b.mCachedLength && a.mLengthCached == b.mLengthCached && a.mPosition == b.mPosition && a.mFilePosition == b.mFilePosition &&
           a.mReadBufferSize == b.mReadBufferSize && a.mReadBufferStart == b.mReadBufferStart && a.mReadBufferLength == b.mReadBufferLength &&
           a.mWriteBufferSize == b.mWriteBufferSize && a.mWriteBufferStart == b.mWriteBufferStart && a.mWriteBufferLength == b.mWriteBufferLength && a.mReadBuffer == b.mReadBuffer &&
           a.mWriteBuffer == b.mWriteBuffer && (a.mHandle == INVALID_HANDLE_VALUE) == (b.mHandle == INVALID_HANDLE_VALUE);
}

bool SameVector(const cRZStringVector &a, const cRZStringVector &b)
{
    if ((a.mFirst == nullptr) != (b.mFirst == nullptr) || a.Count() != b.Count() || (a.mEnd - a.mFirst) != (b.mEnd - b.mFirst)) return false;
    for (int32_t i = 0; i < a.Count(); ++i)
        if (!SameString(a.mFirst[i], b.mFirst[i])) return false;
    return true;
}

bool SameIni(const cTSIniFile &a, const cTSIniFile &b)
{
    return a.mVTable == b.mVTable && a.mPath.mString.size() == b.mPath.mString.size() && a.mPath.mString.capacity() == b.mPath.mString.capacity() &&
           a.mPath.mRefCount == b.mPath.mRefCount && a.mPath.mVTable == b.mPath.mVTable && a.mEnabled == b.mEnabled && a.mLoaded == b.mLoaded &&
           TreeSize(a.mEntries) == TreeSize(b.mEntries) && TreeSize(a.mSecondTable) == TreeSize(b.mSecondTable) && (a.mFile == nullptr) == (b.mFile == nullptr) &&
           SameVector(a.mPrefixes, b.mPrefixes);
}

std::string Describe(const cTSIniFile &p)
{
    std::string prefixes;
    for (int32_t i = 0; i < p.mPrefixes.Count(); ++i) prefixes += "[" + p.mPrefixes.mFirst[i].mString + "]";
    char buf[400];
    std::snprintf(buf, sizeof(buf), "path '%s' enabled %d loaded %d tables %u/%u prefixes %d/%d %s", p.mPath.mString.c_str(), p.mEnabled, p.mLoaded, TreeSize(p.mEntries),
                  TreeSize(p.mSecondTable), p.mPrefixes.Count(), static_cast<int>(p.mPrefixes.mEnd - p.mPrefixes.mFirst), prefixes.c_str());
    return buf;
}

// ---- the walk callback -------------------------------------------------------------------------------------

int gCookie;

bool __cdecl WalkCallback(const cRZString *key, const cRZString *value, void *context)
{
    gCalls.push_back({100, Seen(key), Seen(value), static_cast<uint32_t>(context == &gCookie), 0});
    return true;
}

} // namespace

// The constructor, then random operations on the pair, then the destructors.
DIFF_TEST(AppServices3_TSIniFile, kCtor)
{
    InstallWideFunctions(ctx);
    SetupFakes(ctx);
    auto construct = ctx.Original<CtorFn>(kCtor);
    auto destruct = ctx.Original<DtorFn>(kDtor);
    auto deleting = ctx.Original<DeletingFn>(kDeleting);
    auto getString = ctx.Original<GetStringFn>(kGetString);
    auto walk = ctx.Original<WalkFn>(kWalk);
    auto hasPrefixed = ctx.Original<HasFn>(kHasPrefixed);
    auto hasBase = ctx.Original<HasFn>(kHasBase);
    std::mt19937 rng(0xcb2a20);
    int found = 0, replaced = 0, sections = 0, walked = 0, prefixedHits = 0;
    for (int sequence = 0; sequence < 1200; ++sequence) {
        std::string text = RandomIniText(rng);
        bool exists = rng() % 12 != 0;
        if (exists) {
            WriteFileBytes(SandboxFileW('A', 0), text);
            WriteFileBytes(SandboxFileW('B', 0), text);
        } else {
            DeleteFileW(SandboxFileW('A', 0).c_str());
            DeleteFileW(SandboxFileW('B', 0).c_str());
        }
        Str pathA(SandboxFile('A', 0)), pathB(SandboxFile('B', 0));
        const char *extraText = kExtras[rng() % (sizeof(kExtras) / sizeof(kExtras[0]))];
        std::string extraString = extraText ? extraText : "";
        Str extraA(extraString), extraB(extraString);
        gContextPresent = rng() % 3 != 0;
        gContextValue = kContexts[rng() % (sizeof(kContexts) / sizeof(kContexts[0]))];

        Pair pair(rng);
        cTSIniFile &a = pair.A();
        cTSIniFile &b = pair.B();
        gCalls.clear();
        void *ra = construct(&a, pathA.I(), extraText ? &extraA.S() : nullptr);
        std::vector<Call> callsA = gCalls;
        gCalls.clear();
        void *rb = b.Construct(*pathB.I(), extraText ? &extraB.S() : nullptr);
        ++ctx.cases;
        if (ra != &a || rb != &b) ctx.Fail("Construct returns %p/%p (this %p/%p)", ra, rb, static_cast<void *>(&a), static_cast<void *>(&b));
        if (callsA != gCalls) ctx.Fail("Construct: calls differ: %s | %s", DescribeCalls(callsA).c_str(), DescribeCalls(gCalls).c_str());
        if (!SameIni(a, b) || std::memcmp(pair.rawA + 0x26, pair.rawB + 0x26, 2) != 0 || std::memcmp(pair.rawA + 0x3c, pair.rawB + 0x3c, 4) != 0)
            ctx.Fail("Construct: state differs: %s | %s", Describe(a).c_str(), Describe(b).c_str());
        if (!SameFile(*static_cast<cRZFile *>(a.mFile), *static_cast<cRZFile *>(b.mFile))) ctx.Fail("Construct: file differs");
        if (!SameString(extraA.S(), extraB.S())) ctx.Fail("Construct changed the extra argument");
        if (a.mPrefixes.Count() > 1) ++prefixedHits;

        if (rng() % 3 == 0) {
            a.mEnabled = false;
            b.mEnabled = false;
        }
        int steps = 1 + static_cast<int>(rng() % 10);
        for (int step = 0; step < steps; ++step) {
            const std::string &section = kSections[rng() % kSections.size()];
            const std::string &key = kKeys[rng() % kKeys.size()];
            gDirFlag = rng() % 3 != 0;
            static const char *const dirTexts[] = {"", "C:\\Game", "a directory name that is longer than fifteen characters\\x", "D"};
            gDirText = dirTexts[rng() % 4];
            switch (rng() % 5) {
            case 0:
            case 1:
            case 2: {
                std::string previous = rng() % 2 ? "" : "previous value long enough to be on the heap for sure";
                Str sA(section), sB(section), kA(key), kB(key), outA(previous), outB(previous);
                gCalls.clear();
                bool resA = getString(&a, sA.I(), kA.I(), outA.S().AsInterface());
                std::vector<Call> logA = gCalls;
                gCalls.clear();
                bool resB = b.GetString(*sB.I(), *kB.I(), outB.S().AsInterface());
                ++ctx.cases;
                if (resA) ++found;
                if (resA && outA.S().mString.size() != 0 && outA.S().mString.find("%") == std::string::npos && gDirFlag) ++replaced;
                if (resA != resB) ctx.Fail("GetString(\"%s\", \"%s\"): returns %d vs %d", section.c_str(), key.c_str(), resA, resB);
                if (logA != gCalls) ctx.Fail("GetString(\"%s\", \"%s\"): calls %s | %s", section.c_str(), key.c_str(), DescribeCalls(logA).c_str(), DescribeCalls(gCalls).c_str());
                if (!SameString(outA.S(), outB.S())) ctx.Fail("GetString(\"%s\", \"%s\"): out %s | %s", section.c_str(), key.c_str(), Describe(outA.S()).c_str(), Describe(outB.S()).c_str());
                if (!SameString(sA.S(), sB.S()) || !SameString(kA.S(), kB.S())) ctx.Fail("GetString(\"%s\", \"%s\"): arguments changed", section.c_str(), key.c_str());
                break;
            }
            case 3: {
                Str sA(section), sB(section);
                gCalls.clear();
                bool resA = hasPrefixed(&a, sA.I());
                bool resB = b.HasSectionPrefixed(*sB.I());
                bool baseA = hasBase(&a, sA.I());
                bool baseB = b.HasSection(*sB.I());
                ++ctx.cases;
                if (resA) ++sections;
                if (resA != resB || baseA != baseB) ctx.Fail("HasSection(\"%s\"): prefixed %d/%d base %d/%d", section.c_str(), resA, resB, baseA, baseB);
                if (!SameString(sA.S(), sB.S())) ctx.Fail("HasSection(\"%s\"): argument changed", section.c_str());
                break;
            }
            default: {
                Str sA(section), sB(section);
                gCalls.clear();
                bool resA = walk(&a, sA.I(), reinterpret_cast<void *>(&WalkCallback), &gCookie);
                std::vector<Call> logA = gCalls;
                gCalls.clear();
                bool resB = b.SectionWalk(*sB.I(), reinterpret_cast<void *>(&WalkCallback), &gCookie);
                ++ctx.cases;
                if (logA.size() > 1) ++walked;
                if (resA != resB) ctx.Fail("SectionWalk(\"%s\"): returns %d vs %d", section.c_str(), resA, resB);
                if (logA != gCalls) ctx.Fail("SectionWalk(\"%s\"): callbacks %s | %s", section.c_str(), DescribeCalls(logA).c_str(), DescribeCalls(gCalls).c_str());
                if (!SameString(sA.S(), sB.S())) ctx.Fail("SectionWalk(\"%s\"): argument changed", section.c_str());
                break;
            }
            }
            if (!SameIni(a, b)) ctx.Fail("step %d: state differs: %s | %s", step, Describe(a).c_str(), Describe(b).c_str());
            if (!SameFile(*static_cast<cRZFile *>(a.mFile), *static_cast<cRZFile *>(b.mFile))) ctx.Fail("step %d: file state differs", step);
        }

        // The destructors. Hold a second reference on each side's file to watch the count drop; remember the
        // prefix array and the heap buffers of long prefixes to see them freed.
        void *fileA = a.mFile, *fileB = b.mFile;
        VirtualSlot<UintThiscall0>(fileA, 1)(fileA);
        VirtualSlot<UintThiscall0>(fileB, 1)(fileB);
        void *arrayA = a.mPrefixes.mFirst, *arrayB = b.mPrefixes.mFirst;
        std::vector<const void *> buffersA, buffersB;
        for (int32_t i = 0; i < a.mPrefixes.Count(); ++i) {
            if (a.mPrefixes.mFirst[i].mString.capacity() > 15) buffersA.push_back(a.mPrefixes.mFirst[i].mString.data());
            if (b.mPrefixes.mFirst[i].mString.capacity() > 15) buffersB.push_back(b.mPrefixes.mFirst[i].mString.data());
        }
        int how = static_cast<int>(rng() % 3);
        bool stillA, stillB;
        void *da = nullptr, *db = nullptr;
        unsigned char *ha = nullptr, *hb = nullptr;
        // A block is "still" allocated when the FreeRecorder across that side's call did not see it freed.
        auto still = [](const difftest::FreeRecorder &rec, const void *array, const std::vector<const void *> &buffers) {
            bool s = array && !rec.Freed(array);
            for (const void *p : buffers) s = s || !rec.Freed(p);
            return s;
        };
        if (how == 0) {
            difftest::FreeRecorder recA;
            destruct(&a);
            recA.Stop();
            stillA = still(recA, arrayA, buffersA);
            difftest::FreeRecorder recB;
            b.Destruct();
            recB.Stop();
            stillB = still(recB, arrayB, buffersB);
        } else {
            // The deleting destructors free the 0x4c bytes: give them heap blocks (shallow copies; the stack
            // objects are not destroyed again).
            ha = static_cast<unsigned char *>(::operator new(cTSIniFile::kSize));
            hb = static_cast<unsigned char *>(::operator new(cTSIniFile::kSize));
            std::memcpy(ha, pair.rawA, cTSIniFile::kSize);
            std::memcpy(hb, pair.rawB, cTSIniFile::kSize);
            difftest::FreeRecorder recA;
            da = deleting(ha, how == 1 ? 1u : 0u);
            recA.Stop();
            bool liveA = !recA.Freed(ha);
            stillA = still(recA, arrayA, buffersA);
            difftest::FreeRecorder recB;
            db = reinterpret_cast<cTSIniFile *>(hb)->DeletingDestructor(how == 1 ? 1u : 0u);
            recB.Stop();
            bool liveB = !recB.Freed(hb);
            stillB = still(recB, arrayB, buffersB);
            if (liveA != liveB || liveA != (how != 1)) ctx.Fail("deleting destructor (flags %u): block live %d/%d", how == 1 ? 1u : 0u, liveA, liveB);
            if (da != ha || db != hb) ctx.Fail("deleting destructor returns %p/%p (this %p/%p)", da, db, static_cast<void *>(ha), static_cast<void *>(hb));
            if (std::memcmp(ha + 0x40, hb + 0x40, 12) != 0) ctx.Fail("deleting destructor: prefix vector left differently");
        }
        ++ctx.cases;
        if (stillA || stillB) ctx.Fail("destructor (%d): prefix memory still allocated (original %d, ours %d)", how, stillA, stillB);
        auto *fa = static_cast<cRZFile *>(fileA);
        auto *fb = static_cast<cRZFile *>(fileB);
        if (fa->mRefCount != fb->mRefCount || fa->mOpen != fb->mOpen) ctx.Fail("destructor: file refs %u/%u open %d/%d", fa->mRefCount, fb->mRefCount, fa->mOpen, fb->mOpen);
        VirtualSlot<UintThiscall0>(fileA, 2)(fileA);
        VirtualSlot<UintThiscall0>(fileB, 2)(fileB);
        if (how == 0 && std::memcmp(pair.rawA + 0x38, pair.rawB + 0x38, 0x14) != 0) ctx.Fail("destructor: fields left %p/%p", a.mFile, b.mFile);
        if (how == 2) {
            ::operator delete(ha);
            ::operator delete(hb);
        }
    }
    ClearFakes(ctx);
    if (found < 150 || replaced < 50 || sections < 100 || walked < 100 || prefixedHits < 300)
        ctx.Fail("coverage: found %d replaced %d sections %d walked %d multi-prefix objects %d", found, replaced, sections, walked, prefixedHits);
    if (!HeapValidate(GetProcessHeap(), 0, nullptr)) ctx.Fail("process heap corrupted");
}

// AppendIniCommandLine: the ini path query, the [CommandLine] Append= value, the arguments inserted at ArgC.
DIFF_TEST(AppServices3_AppendIniCommandLine, kAppend)
{
    InstallWideFunctions(ctx);
    SetupFakes(ctx);
    auto original = ctx.Original<void(__cdecl *)()>(kAppend);
    std::mt19937 rng(kAppend);
    static const char *const kAppendValues[] = {"",           "-w",         "-w -nosound",      "\"a b\" c",   "   spaced   args  ", "/x:1 /y:2 /z:3 /w:4 /v:5 /u:6 /t:7",
                                                "single",     "\"quoted\"", "one two three four five six seven eight nine", ";", "a;b"};
    int inserted = 0, withFile = 0;
    for (int i = 0; i < 400; ++i) {
        std::string ini;
        if (rng() % 5 == 0) ini += "[Other]\r\nAppend=ignored\r\n";
        bool section = rng() % 6 != 0;
        if (section) ini += "[CommandLine]\r\n";
        bool key = rng() % 5 != 0;
        if (key) ini += std::string(rng() % 4 == 0 ? "append" : "Append") + "=" + kAppendValues[rng() % (sizeof(kAppendValues) / sizeof(kAppendValues[0]))] + "\r\n";
        if (rng() % 3 == 0) ini += "[Pre-CommandLine]\r\nAppend=-prefixed -args\r\n";
        if (rng() % 4 == 0) ini += "[CommandLine-CommandLine]\r\nAppend=-self-prefixed\r\n"; // only an extra prefix named "CommandLine" finds this
        bool exists = rng() % 5 != 0;
        if (exists) {
            WriteFileBytes(SandboxFileW('A', 1), ini);
            WriteFileBytes(SandboxFileW('B', 1), ini);
            ++withFile;
        } else {
            DeleteFileW(SandboxFileW('A', 1).c_str());
            DeleteFileW(SandboxFileW('B', 1).c_str());
        }
        gContextPresent = rng() % 3 == 0;
        gContextValue = kContexts[rng() % 3];
        gDirFlag = rng() % 2;
        gDirText = "C:\\Game";
        int32_t startArgc = static_cast<int32_t>(rng() % 5);

        gArgc = startArgc;
        gPathText = SandboxFile('A', 1);
        gCalls.clear();
        original();
        std::vector<Call> callsA = gCalls;
        int32_t argcA = gArgc;
        gArgc = startArgc;
        gPathText = SandboxFile('B', 1);
        gCalls.clear();
        appservices3::AppendIniCommandLine();
        ++ctx.cases;
        // The path text differs between the sides (the file name); blank it for the comparison.
        auto blank = [](std::vector<Call> &calls) {
            for (Call &c : calls)
                if (c.what == 4 && c.x == 0x6c253729) c.a.clear();
        };
        blank(callsA);
        blank(gCalls);
        if (callsA != gCalls || argcA != gArgc) ctx.Fail("run %d (file %d, section %d, key %d): calls differ: %s | %s", i, exists, section, key, DescribeCalls(callsA).c_str(), DescribeCalls(gCalls).c_str());
        for (const Call &c : callsA)
            if (c.what == 13) ++inserted;
    }
    ClearFakes(ctx);
    if (inserted < 300 || withFile < 250) ctx.Fail("coverage: %d insertions, %d runs with a file", inserted, withFile);
    if (!HeapValidate(GetProcessHeap(), 0, nullptr)) ctx.Fail("process heap corrupted");
}
