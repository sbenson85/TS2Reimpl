// Differential tests for the cRZIniFile read path (src/rz/cRZIniFileRead.cpp) and the two cGZFileSystem
// directory getters that use it (src/app/IniFile.cpp): constructor 0x9c5d11, destructor 0x9c5e5d, deleting
// destructor 0x9c5eac, GetString 0x9c679f, the string helpers 0x1ad11d / 0x1ad307 / 0x1ad555 / 0x145ca4
// (0x1ad83c is library code and is covered through GetString), DataDirectory 0xc75ff9 and PlugInDirectory
// 0xc763f0.
//
// The object pairs: side A is built, read and destroyed by the original code, side B by ours; both carry the
// original vtable, so the slots the read calls through it (Clear, load, ReadLine) are the game's on both sides.
// Each side has its own sandboxed copy of the same ini text. File access goes through the harness sandbox.
#include "app/AppServices.h"
#include "app/IniFile.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game/vtable.h"
#include "rz/cRZFile.h"
#include "rz/cRZIniFile.h"
#include "rz/cRZString.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

using UintThiscall0 = uint32_t(__thiscall *)(void *);
using GetStringFn = bool(__thiscall *)(void *, const cIGZString *, const cIGZString *, cIGZString *);
using ConstructFn = void *(__thiscall *)(void *, const cIGZString *);
using DestructFn = void(__thiscall *)(void *);
using DeletingFn = void *(__thiscall *)(void *, uint32_t);
using TrimFn = void(__thiscall *)(void *);
using SplitFn = void *(__thiscall *)(void *, void *, const char *);
using DirectoryFn = const char *(__fastcall *)(void *, void *);

constexpr uint32_t kConstruct = 0x9c5d11, kDestruct = 0x9c5e5d, kDeleting = 0x9c5eac, kGetString = 0x9c679f;
constexpr uint32_t kTrimLeading = 0x1ad11d, kTrimTrailing = 0x1ad307, kSplit = 0x1ad555;
constexpr uint32_t kDataDirectory = 0xc75ff9, kPlugInDirectory = 0xc763f0;
constexpr size_t kObjectBytes = 0x40;

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

// The game fills these globals with its wide file functions at start-up (as on NT); the sandbox provides the
// same functions, so the original's file code runs against the sandbox.
const uint32_t kWideGlobals[] = {0x10cb5e0, 0x10cb5e4, 0x10cb5e8, 0x10cb5ec, 0x10cb5f0, 0x10cb5f4};
const char *const kWideNames[] = {"CreateFileW", "CopyFileW", "MoveFileExW", "FindFirstFileW", "DeleteFileW", "GetFileAttributesW"};

void InstallWideFunctions(difftest::Context &ctx)
{
    for (int i = 0; i < 6; ++i) *ctx.image.At<void *>(kWideGlobals[i]) = difftest::SandboxFunction(kWideNames[i]);
}

// A cRZString with a given text (original vtable), destroyed at the end of scope.
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

// ---- the string helpers -------------------------------------------------------------------------------

std::string RandomBlankText(std::mt19937 &rng)
{
    static const char alphabet[] = "ab \t= =;[x\r";
    std::string s;
    int n = static_cast<int>(rng() % 14);
    for (int i = 0; i < n; ++i) s += alphabet[rng() % (sizeof(alphabet) - 1)];
    return s;
}

} // namespace

DIFF_TEST(IniFile_StringHelpers, kTrimLeading)
{
    auto trimLeading = ctx.Original<TrimFn>(kTrimLeading);
    auto trimTrailing = ctx.Original<TrimFn>(kTrimTrailing);
    auto split = ctx.Original<SplitFn>(kSplit);
    std::mt19937 rng(0x1ad11d);
    const char *delimiters[] = {"=", "==", "", nullptr, " ", "ab", "[", "\t"};
    for (int i = 0; i < 6000; ++i) {
        std::string text = RandomBlankText(rng);
        if (i % 5 == 0) text += std::string(30, ' ') + "tail" + std::string(20, '\t'); // heap-sized strings
        ++ctx.cases;
        {
            Str a(text), b(text);
            trimLeading(&a.S());
            reinterpret_cast<cRZIniStringHelpers *>(&b.S())->TrimLeading();
            if (!SameString(a.S(), b.S())) ctx.Fail("TrimLeading(\"%s\"): %s / %s", text.c_str(), Describe(a.S()).c_str(), Describe(b.S()).c_str());
        }
        {
            Str a(text), b(text);
            trimTrailing(&a.S());
            reinterpret_cast<cRZIniStringHelpers *>(&b.S())->TrimTrailing();
            if (!SameString(a.S(), b.S())) ctx.Fail("TrimTrailing(\"%s\"): %s / %s", text.c_str(), Describe(a.S()).c_str(), Describe(b.S()).c_str());
        }
        {
            // 0x1ad83c (library code) is the two trims in a row.
            Str a(text), b(text);
            trimLeading(&a.S());
            trimTrailing(&a.S());
            reinterpret_cast<cRZIniStringHelpers *>(&b.S())->TrimBoth();
            if (!SameString(a.S(), b.S())) ctx.Fail("TrimBoth(\"%s\"): %s / %s", text.c_str(), Describe(a.S()).c_str(), Describe(b.S()).c_str());
        }
        for (const char *delimiter : delimiters) {
            Str a(text), b(text);
            unsigned char outA[sizeof(cRZString)], outB[sizeof(cRZString)];
            std::memset(outA, 0x3c, sizeof(outA));
            std::memset(outB, 0x3c, sizeof(outB));
            void *ra = split(&a.S(), outA, delimiter);
            void *rb = reinterpret_cast<cRZIniStringHelpers *>(&b.S())->SplitOff(reinterpret_cast<cRZString *>(outB), delimiter);
            ++ctx.cases;
            cRZString &oa = *reinterpret_cast<cRZString *>(outA);
            cRZString &ob = *reinterpret_cast<cRZString *>(outB);
            if (ra != outA || rb != outB || !SameString(oa, ob) || !SameString(a.S(), b.S()))
                ctx.Fail("SplitOff(\"%s\", \"%s\"): out %s / %s rest %s / %s", text.c_str(), delimiter ? delimiter : "(null)", Describe(oa).c_str(), Describe(ob).c_str(),
                         Describe(a.S()).c_str(), Describe(b.S()).c_str());
            oa.Destruct();
            ob.Destruct();
        }
    }
}

namespace {

// ---- the object pairs -----------------------------------------------------------------------------------

std::string Name(char side, int index)
{
    return std::string(1, side) + "r" + std::to_string(index) + ".ini";
}

std::wstring SandboxFileW(char side, int index)
{
    std::string n = Name(side, index);
    return difftest::SandboxPath(std::wstring(n.begin(), n.end()));
}

std::string SandboxFile(char side, int index)
{
    return ToUtf8(SandboxFileW(side, index));
}

const char *const kIniLines[] = {"[Sims]\r\n",         "Width=12\r\n",         "  Height = -3  \r\n", "[Options]\r\n",      "Volume=21\r\n",    "; comment\r\n",
                                 "  ; indented comment\r\n", "\r\n",            "   \r\n",           "[x]\r\n",           "k =  7 \r\n",       "junk line\r\n",
                                 "Width=\r\n",         "NAME=Mixed Case Value\r\n", "name2\t=\ttabbed\r\n", "[ Padded ]\r\n", "k=a=b=c\r\n",     "[SIMS]\r\n",
                                 "WIDTH=99\r\n",       "=nokey\r\n",            "KEY WITH SPACE=1\r\n", "[sims]\r\n",    "\tIndented=1\r\n", "last=no newline"};

std::string RandomIniText(std::mt19937 &rng)
{
    std::string text;
    int n = static_cast<int>(rng() % 14);
    for (int i = 0; i < n; ++i) text += kIniLines[rng() % (sizeof(kIniLines) / sizeof(kIniLines[0]))];
    return text;
}

const std::vector<std::string> kSections = {"Sims", "SIMS", "sims", "Options", "x", " Padded ", "Sims", "x", "Options", "missing", ""};
const std::vector<std::string> kKeys = {"Width", "WIDTH", "Height", "Volume", "k", "name", "NAME2", "name2", "Indented", "last", "key with space", "nokey", "k", "Width", "missing", ""};

bool SameFile(const cRZFile &a, const cRZFile &b)
{
    auto tail = [](const std::string &s) { return s.size() < 7 ? s : s.substr(s.size() - 7); };
    return a.mRefCount == b.mRefCount && a.mOpen == b.mOpen && tail(a.mPath.mString) == tail(b.mPath.mString).replace(0, 1, tail(a.mPath.mString).substr(0, 1)) && a.mAccess == b.mAccess &&
           a.mCreation == b.mCreation && a.mSharing == b.mSharing && a.mError == b.mError && a.mCachedLength == b.mCachedLength && a.mLengthCached == b.mLengthCached &&
           a.mPosition == b.mPosition && a.mFilePosition == b.mFilePosition && a.mReadBufferSize == b.mReadBufferSize && a.mReadBufferStart == b.mReadBufferStart &&
           a.mReadBufferLength == b.mReadBufferLength && a.mWriteBufferSize == b.mWriteBufferSize && a.mWriteBufferStart == b.mWriteBufferStart &&
           a.mWriteBufferLength == b.mWriteBufferLength && a.mReadBuffer == b.mReadBuffer && a.mWriteBuffer == b.mWriteBuffer &&
           (a.mHandle == INVALID_HANDLE_VALUE) == (b.mHandle == INVALID_HANDLE_VALUE);
}

uint32_t TreeSize(const uint8_t *header)
{
    uint32_t size;
    std::memcpy(&size, header + 4, sizeof(size));
    return size;
}

bool SameIni(const cRZIniFile &a, const cRZIniFile &b)
{
    return a.mVTable == b.mVTable && a.mPath.mString.size() == b.mPath.mString.size() && a.mPath.mString.capacity() == b.mPath.mString.capacity() && a.mPath.mRefCount == b.mPath.mRefCount &&
           a.mPath.mVTable == b.mPath.mVTable && a.mEnabled == b.mEnabled && a.mLoaded == b.mLoaded && TreeSize(a.mEntries) == TreeSize(b.mEntries) &&
           TreeSize(a.mSecondTable) == TreeSize(b.mSecondTable) && (a.mFile == nullptr) == (b.mFile == nullptr);
}

std::string Describe(const cRZIniFile &p)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), "path '%s' enabled %d loaded %d tables %u/%u", p.mPath.mString.c_str(), p.mEnabled, p.mLoaded, TreeSize(p.mEntries), TreeSize(p.mSecondTable));
    return buf;
}

} // namespace

// Constructor, GetString and the destructors, as random sequences on a pair of objects (A: original code, B: ours).
DIFF_TEST(IniFile_IniFileRead, kGetString)
{
    InstallWideFunctions(ctx);
    auto construct = ctx.Original<ConstructFn>(kConstruct);
    auto destruct = ctx.Original<DestructFn>(kDestruct);
    auto deleting = ctx.Original<DeletingFn>(kDeleting);
    auto getString = ctx.Original<GetStringFn>(kGetString);
    std::mt19937 rng(0x9c679f);
    int hits = 0, found = 0;
    for (int sequence = 0; sequence < 1500; ++sequence) {
        std::string text = RandomIniText(rng);
        bool exists = rng() % 10 != 0;
        if (exists) {
            WriteFileBytes(SandboxFileW('A', 0), text);
            WriteFileBytes(SandboxFileW('B', 0), text);
        } else {
            DeleteFileW(SandboxFileW('A', 0).c_str());
            DeleteFileW(SandboxFileW('B', 0).c_str());
        }
        Str pathA(SandboxFile('A', 0)), pathB(SandboxFile('B', 0));
        // Both sides' objects start with the same filler (padding at 0x26/0x27 and the tail is never written).
        unsigned char rawA[kObjectBytes], rawB[kObjectBytes];
        for (size_t i = 0; i < kObjectBytes; ++i) rawA[i] = rawB[i] = static_cast<unsigned char>(rng());
        auto *a = reinterpret_cast<cRZIniFile *>(rawA);
        auto *b = reinterpret_cast<cRZIniFile *>(rawB);
        void *ra = construct(a, pathA.I());
        void *rb = nullptr;
        try {
            rb = b->Construct(*pathB.I());
        } catch (const std::exception &e) {
            ctx.Fail("Construct threw: %s", e.what());
            return;
        }
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("Construct returns %p/%p (this %p/%p)", ra, rb, static_cast<void *>(a), static_cast<void *>(b));
        if (!SameIni(*a, *b) || std::memcmp(rawA + 0x26, rawB + 0x26, 2) != 0 || std::memcmp(rawA + 0x3c, rawB + 0x3c, 4) != 0)
            ctx.Fail("Construct: state differs: %s | %s", Describe(*a).c_str(), Describe(*b).c_str());
        if (!SameFile(*static_cast<cRZFile *>(a->mFile), *static_cast<cRZFile *>(b->mFile))) ctx.Fail("Construct: file differs");

        bool enabled = rng() & 1;
        if (!enabled) {
            a->mEnabled = false;
            b->mEnabled = false;
        }
        int steps = 1 + static_cast<int>(rng() % 14);
        for (int step = 0; step < steps; ++step) {
            const std::string &section = kSections[rng() % kSections.size()];
            const std::string &key = kKeys[rng() % kKeys.size()];
            std::string previous = rng() % 2 ? "" : "previous value long enough to be on the heap for sure";
            Str sA(section), sB(section), kA(key), kB(key), outA(previous), outB(previous);
            bool resA = getString(a, sA.I(), kA.I(), outA.S().AsInterface());
            bool resB = b->GetString(*sB.I(), *kB.I(), outB.S().AsInterface());
            ++ctx.cases;
            if (resA) ++found;
            if (resA != resB) ctx.Fail("GetString(\"%s\", \"%s\"): returns %d vs %d", section.c_str(), key.c_str(), resA, resB);
            if (!SameString(outA.S(), outB.S())) ctx.Fail("GetString(\"%s\", \"%s\"): out %s | %s", section.c_str(), key.c_str(), Describe(outA.S()).c_str(), Describe(outB.S()).c_str());
            if (!SameString(sA.S(), sB.S()) || !SameString(kA.S(), kB.S())) ctx.Fail("GetString(\"%s\", \"%s\"): arguments changed", section.c_str(), key.c_str());
            if (!SameIni(*a, *b)) ctx.Fail("GetString(\"%s\", \"%s\"): state differs: %s | %s", section.c_str(), key.c_str(), Describe(*a).c_str(), Describe(*b).c_str());
            if (!SameFile(*static_cast<cRZFile *>(a->mFile), *static_cast<cRZFile *>(b->mFile))) ctx.Fail("GetString(\"%s\", \"%s\"): file state differs", section.c_str(), key.c_str());
            if (resA) ++hits;
        }

        // The destructors: the file is released (and closed when open) and the tables freed. Hold a second
        // reference on each side's file to watch the count drop and the file close.
        void *fileA = a->mFile, *fileB = b->mFile;
        VirtualSlot<UintThiscall0>(fileA, 1)(fileA);
        VirtualSlot<UintThiscall0>(fileB, 1)(fileB);
        int how = static_cast<int>(rng() % 3);
        if (how == 0) {
            destruct(a);
            b->Destruct();
        } else {
            // The deleting destructors free the 0x40 bytes: give them heap blocks.
            auto *ha = static_cast<unsigned char *>(::operator new(kObjectBytes));
            auto *hb = static_cast<unsigned char *>(::operator new(kObjectBytes));
            std::memcpy(ha, rawA, kObjectBytes);
            std::memcpy(hb, rawB, kObjectBytes);
            // The copies share the stack objects' tables, so the stack objects are not destroyed again.
            // Frees recorded across each side's call (FreeRecorder).
            difftest::FreeRecorder recA;
            void *da = deleting(ha, how == 1 ? 1u : 0u);
            recA.Stop();
            bool liveA = !recA.Freed(ha);
            difftest::FreeRecorder recB;
            void *db = reinterpret_cast<cRZIniFile *>(hb)->DeletingDestructor(how == 1 ? 1u : 0u);
            recB.Stop();
            bool liveB = !recB.Freed(hb);
            if (liveA != liveB || liveA != (how != 1)) ctx.Fail("deleting destructor (flags %u): block live %d/%d", how == 1 ? 1u : 0u, liveA, liveB);
            if (da != ha || db != hb) ctx.Fail("deleting destructor returns %p/%p (this %p/%p)", da, db, static_cast<void *>(ha), static_cast<void *>(hb));
            if (how == 2) {
                ::operator delete(ha);
                ::operator delete(hb);
            }
        }
        ++ctx.cases;
        auto *fa = static_cast<cRZFile *>(fileA);
        auto *fb = static_cast<cRZFile *>(fileB);
        if (fa->mRefCount != fb->mRefCount || fa->mOpen != fb->mOpen) ctx.Fail("destructor: file refs %u/%u open %d/%d", fa->mRefCount, fb->mRefCount, fa->mOpen, fb->mOpen);
        VirtualSlot<UintThiscall0>(fileA, 2)(fileA);
        VirtualSlot<UintThiscall0>(fileB, 2)(fileB);
        if (how == 0 && std::memcmp(rawA + 0x38, rawB + 0x38, 4) != 0) ctx.Fail("destructor: file pointer left %p/%p", a->mFile, b->mFile);
    }
    if (found < 100) ctx.Fail("only %d successful lookups", found);
    (void)hits;
    if (!HeapValidate(GetProcessHeap(), 0, nullptr)) ctx.Fail("process heap corrupted");
}

namespace {

// ---- DataDirectory / PlugInDirectory --------------------------------------------------------------------

std::vector<std::string> gSlotLog;
std::string gAppDirectory, gAppName;

const char *__fastcall FakeAppDirectory(void *, void *)
{
    gSlotLog.push_back("dir");
    return gAppDirectory.c_str();
}

const char *__fastcall FakeAppName(void *, void *)
{
    gSlotLog.push_back("name");
    return gAppName.c_str();
}

// The face's vtable: slots 4 (AppDirectory) and 5 (AppName) are fakes; the rest are never called.
void *gFaceVTable[23];

struct FileSystemObject {
    alignas(8) unsigned char raw[cGZFileSystem::kSize];
    cGZFileSystem &Fs() { return *reinterpret_cast<cGZFileSystem *>(raw); }
    void *Face() { return raw + cGZFileSystem::kFace; }
    FileSystemObject(const FileSystemObject &) = delete;
    FileSystemObject(std::mt19937 &rng, bool dataSet, bool plugSet, const std::string &data, const std::string &plug, const std::string &plugName)
    {
        for (size_t i = 0; i < sizeof(raw); ++i) raw[i] = static_cast<unsigned char>(rng());
        *reinterpret_cast<void **>(raw + cGZFileSystem::kFace) = gFaceVTable; // the face's vtable word
        Fs().mDataDirectorySet = dataSet;
        Fs().mPlugInDirectorySet = plugSet;
        Fs().mDataDirectory.Construct();
        Fs().mDataDirectory.mString = data;
        Fs().mPlugInDirectory.Construct();
        Fs().mPlugInDirectory.mString = plug;
        Fs().mPluginDirectoryName.Construct();
        Fs().mPluginDirectoryName.mString = plugName;
    }
    ~FileSystemObject()
    {
        Fs().mDataDirectory.Destruct();
        Fs().mPlugInDirectory.Destruct();
        Fs().mPluginDirectoryName.Destruct();
    }
};

const char *const kDirectoryValues[] = {"", "", "Data\\", "..\\Data", "C:\\Games\\Sims2\\Data\\", "D:/x/../y/", "\\\\srv\\share\\Data", ".\\a\\.\\b", "relative/dir", "C:\\"};
const char *const kPlugNames[] = {"PlugIns", "", "Plug\\Ins", "p"};

std::string RandomDirectoriesIni(std::mt19937 &rng)
{
    std::string text;
    if (rng() % 4 == 0) text += "[Other]\r\nData=ignored\r\n";
    text += "[Directories]\r\n";
    int variant = static_cast<int>(rng() % 6);
    if (variant != 0) text += std::string("Data=") + kDirectoryValues[rng() % (sizeof(kDirectoryValues) / sizeof(kDirectoryValues[0]))] + "\r\n";
    if (variant != 1) text += std::string("PlugIn=") + kDirectoryValues[rng() % (sizeof(kDirectoryValues) / sizeof(kDirectoryValues[0]))] + "\r\n";
    if (rng() % 3 == 0) text += "  data = D:\\Other\\Place  \r\n";
    return text;
}

bool SameFs(FileSystemObject &a, FileSystemObject &b)
{
    return SameString(a.Fs().mDataDirectory, b.Fs().mDataDirectory) && SameString(a.Fs().mPlugInDirectory, b.Fs().mPlugInDirectory) &&
           SameString(a.Fs().mPluginDirectoryName, b.Fs().mPluginDirectoryName) && a.Fs().mDataDirectorySet == b.Fs().mDataDirectorySet &&
           a.Fs().mPlugInDirectorySet == b.Fs().mPlugInDirectorySet && std::memcmp(a.raw, b.raw, 0x24) == 0 && std::memcmp(a.raw + 0xe4 + 0x20, b.raw + 0xe4 + 0x20, sizeof(a.raw) - 0x104) == 0;
}

void DirectoryTest(difftest::Context &ctx, uint32_t rva, bool plugIn)
{
    InstallWideFunctions(ctx);
    auto original = ctx.Original<DirectoryFn>(rva);
    for (int slot = 0; slot < 23; ++slot) gFaceVTable[slot] = nullptr;
    gFaceVTable[4] = reinterpret_cast<void *>(&FakeAppDirectory);
    gFaceVTable[5] = reinterpret_cast<void *>(&FakeAppName);
    std::mt19937 rng(rva);
    // "<sandbox>\" and a name: the ini is <directory><name>.ini.
    std::string probe = ToUtf8(difftest::SandboxPath(L"probe.ini"));
    std::string directory = probe.substr(0, probe.size() - std::strlen("probe.ini"));
    int resolved = 0;
    for (int i = 0; i < 700; ++i) {
        std::string text = RandomDirectoriesIni(rng);
        bool exists = rng() % 8 != 0;
        std::wstring fileName = difftest::SandboxPath(L"dirs.ini");
        if (exists) WriteFileBytes(fileName, text);
        else DeleteFileW(fileName.c_str());
        gAppName = "dirs";
        gAppDirectory = directory;
        if (rng() % 6 == 0) gAppName = "dirs";
        bool alreadySet = rng() % 6 == 0;
        std::string data = rng() % 2 ? "" : kDirectoryValues[rng() % (sizeof(kDirectoryValues) / sizeof(kDirectoryValues[0]))];
        std::string plug = rng() % 2 ? "" : kDirectoryValues[rng() % (sizeof(kDirectoryValues) / sizeof(kDirectoryValues[0]))];
        std::string plugName = kPlugNames[rng() % 4];
        std::mt19937 fill(i);
        std::mt19937 fill2(i);
        FileSystemObject a(fill, !plugIn && alreadySet, plugIn && alreadySet, data, plug, plugName);
        FileSystemObject b(fill2, !plugIn && alreadySet, plugIn && alreadySet, data, plug, plugName);
        gSlotLog.clear();
        const char *ra = original(a.Face(), nullptr);
        std::vector<std::string> logA = gSlotLog;
        std::string textA = ra;
        gSlotLog.clear();
        const char *rb = plugIn ? b.Fs().cGZFileSystem::PlugInDirectory() : b.Fs().cGZFileSystem::DataDirectory();
        ++ctx.cases;
        std::string textB = rb;
        if (textA != textB || logA != gSlotLog || !SameFs(a, b)) {
            std::string logs;
            for (const std::string &s : logA) logs += s + ",";
            logs += " vs ";
            for (const std::string &s : gSlotLog) logs += s + ",";
            ctx.Fail("%s (ini %d \"%.70s\", preset \"%s\"/\"%s\", set %d): \"%s\" vs \"%s\" slots %s same state %d", plugIn ? "PlugInDirectory" : "DataDirectory", exists, text.c_str(),
                     data.c_str(), plug.c_str(), alreadySet, textA.c_str(), textB.c_str(), logs.c_str(), SameFs(a, b));
        }
        if (!alreadySet && exists) ++resolved;
        // The returned pointer is the cached string's own text.
        const cRZString &cached = plugIn ? b.Fs().mPlugInDirectory : b.Fs().mDataDirectory;
        if (rb != cached.mString.c_str()) ctx.Fail("returned pointer is not the cached string's text");
    }
    if (resolved < 200) ctx.Fail("only %d runs read an existing ini", resolved);
    if (!HeapValidate(GetProcessHeap(), 0, nullptr)) ctx.Fail("process heap corrupted");
}

} // namespace

DIFF_TEST(IniFile_DataDirectory, kDataDirectory)
{
    DirectoryTest(ctx, kDataDirectory, false);
}

DIFF_TEST(IniFile_PlugInDirectory, kPlugInDirectory)
{
    DirectoryTest(ctx, kPlugInDirectory, true);
}
