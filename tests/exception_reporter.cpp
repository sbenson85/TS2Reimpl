#define _CRT_SECURE_NO_WARNINGS
// Differential tests for src/app/ExceptionReporter.*: the reporter's constructor and destructor and
// the helpers they use (path split, module directory, string concatenation, the text file's
// default constructor).
//
// The constructor installs the process-wide exception filter and asks for the module path through
// imports; the tests point those import slots at scripted fakes for the life of a guard (restored
// afterwards). Both sides see the same fakes and their logs are compared.
#define NOMINMAX
#include "game_allocator.h"
#include "app/ExceptionReporter.h"
#include "difftest.h"
#include "game/game.h"
#include "rz/GeneralAllocator.h"
#include "rz/RZMemory.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZString.h"


#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <list>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kSplitPath = 0x1c3a97, kModuleDirectory = 0x1c1c8c, kTextFileCtor = 0x1c3f3e;
constexpr uint32_t kReporterCtor = 0x1ba914, kReporterDtor = 0x1baae3;
constexpr uint32_t kIatSetFilter = 0xd9a1b4, kIatGetModuleFileNameA = 0xd9a16c;
constexpr uint32_t kModuleResolved = 0x10caeb5, kModulePointer = 0x10caeb8, kSystemCodePageCache = 0xff23a4;

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
    for (const std::string &s : gLog) out += s + "\n";
    return out;
}

std::string Hex(const void *data, size_t n)
{
    std::string out;
    char b[4];
    for (size_t i = 0; i < n; ++i) {
        std::snprintf(b, sizeof(b), "%02x", static_cast<const uint8_t *>(data)[i]);
        out += b;
    }
    return out;
}

void ExpectSame(difftest::Context &ctx, const char *what, const std::string &a, const std::string &b)
{
    ++ctx.cases;
    if (a != b) {
        std::string x = a.size() > 500 ? a.substr(0, 500) : a;
        std::string y = b.size() > 500 ? b.substr(0, 500) : b;
        for (char &c : x) if (c == '\n') c = '|';
        for (char &c : y) if (c == '\n') c = '|';
        ctx.Fail("%s:\n  original: %s\n  ours:     %s", what, x.c_str(), y.c_str());
    }
}

class Imports {
public:
    explicit Imports(difftest::Context &c) : ctx(c) {}
    ~Imports()
    {
        for (auto it = saved.rbegin(); it != saved.rend(); ++it) *ctx.image.At<void *>(it->first) = it->second;
    }
    void Set(uint32_t iatRva, void *fake)
    {
        void **slot = ctx.image.At<void *>(iatRva);
        saved.emplace_back(iatRva, *slot);
        *slot = fake;
    }

private:
    difftest::Context &ctx;
    std::vector<std::pair<uint32_t, void *>> saved;
};

// ---- random paths ----

std::string RandomPath(std::mt19937 &rng)
{
    static const char *const kPieces[] = {"C:", "d:", "\\", "/", "\\\\", "dir", "The Sims 2", ".", "..", "a.b", ".hidden", "file", "game.exe", "x.y.z", ":", "é", " "};
    std::string path;
    int count = rng() % 8;
    for (int i = 0; i < count; ++i) path += kPieces[rng() % (sizeof(kPieces) / sizeof(kPieces[0]))];
    if (rng() % 9 == 0) path += std::string(240 + rng() % 40, 'q'); // parts around the 255-character limit
    if (rng() % 9 == 0) path.insert(path.size() / 2, std::string(250 + rng() % 12, 'd') + "\\");
    if (rng() % 11 == 0) path += "." + std::string(250 + rng() % 12, 'e');
    return path;
}

} // namespace

// ---- 0x1c3a97 SplitPath ----

DIFF_TEST(ExceptionReporter_SplitPath, kSplitPath)
{
    std::mt19937 rng(0x1c3a97);
    auto orig = ctx.Original<void(__cdecl *)(const char *, char *, char *, char *, char *)>(kSplitPath);
    for (int run = 0; run < 6000; ++run) {
        std::string path = run < 12 ? std::string(run == 0 ? "" : run == 1 ? "C:" : run == 2 ? "C:\\" : run == 3 ? "." : run == 4 ? "a" : run == 5 ? "\\" : run == 6 ? ".\\.x" : run == 7 ? "a.b\\c" : run == 8 ? "C:a" : run == 9 ? "C:a.b" : run == 10 ? "/x/y.z/" : "x:y\\z.q") : RandomPath(rng);
        unsigned nullMask = rng() % 16 == 0 ? rng() % 16 : 0; // drive, dir, name, ext may be null
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            uint8_t drive[8], dir[0x110], name[0x110], ext[0x110];
            std::memset(drive, 0xA1, sizeof(drive));
            std::memset(dir, 0xA2, sizeof(dir));
            std::memset(name, 0xA3, sizeof(name));
            std::memset(ext, 0xA4, sizeof(ext));
            char *pd = nullMask & 1 ? nullptr : reinterpret_cast<char *>(drive);
            char *pdir = nullMask & 2 ? nullptr : reinterpret_cast<char *>(dir);
            char *pn = nullMask & 4 ? nullptr : reinterpret_cast<char *>(name);
            char *pe = nullMask & 8 ? nullptr : reinterpret_cast<char *>(ext);
            if (side == 0) orig(path.c_str(), pd, pdir, pn, pe);
            else appbase::SplitPath(path.c_str(), pd, pdir, pn, pe);
            results[side] = Hex(drive, sizeof(drive)) + Hex(dir, sizeof(dir)) + Hex(name, sizeof(name)) + Hex(ext, sizeof(ext));
        }
        ExpectSame(ctx, ("split " + path.substr(0, 40)).c_str(), results[0], results[1]);
    }
}

// ---- 0x1c3f3e text file default constructor ----

DIFF_TEST(ExceptionReporter_TextFileCtor, kTextFileCtor)
{
    for (int run = 0; run < 100; ++run) {
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            alignas(8) uint8_t raw[cRZTextFile::kSize + 8];
            std::memset(raw, 0x5A + run, sizeof(raw));
            void *r = side == 0 ? ctx.Original<void *(__thiscall *)(void *)>(kTextFileCtor)(raw) : appbase::ConstructTextFile(reinterpret_cast<cRZTextFile *>(raw), nullptr);
            results[side] = std::string(r == raw ? "this " : "other ") + Hex(raw, sizeof(raw));
            reinterpret_cast<cRZTextFile *>(raw)->Destruct();
        }
        ExpectSame(ctx, "text file ctor", results[0], results[1]);
    }
}

// ---- 0x1c1c8c GetModuleDirectory ----

namespace {
// A path string whose ToChar (slot 5) is recorded, and an output string whose FromChar (slot 3) is.
void *gPathVt[40];
void *gOutVt[40];
const char *(__fastcall *gRealToChar)(void *, void *) = nullptr;
void(__fastcall *gRealFromChar)(void *, void *, const char *, uint32_t) = nullptr;
const char *__fastcall RecordToChar(void *self, void *edx)
{
    Log("ToChar");
    return gRealToChar(self, edx);
}
void __fastcall RecordFromChar(void *self, void *edx, const char *text, uint32_t length)
{
    Log("FromChar len=%u [%s]", length, std::string(text, length).c_str());
    gRealFromChar(self, edx, text, length);
}
} // namespace

DIFF_TEST(ExceptionReporter_GetModuleDirectory, kModuleDirectory)
{
    std::memcpy(gPathVt, ctx.image.At<void>(cRZString::kVTable), sizeof(gPathVt));
    std::memcpy(gOutVt, gPathVt, sizeof(gOutVt));
    gRealToChar = reinterpret_cast<decltype(gRealToChar)>(gPathVt[5]);
    gRealFromChar = reinterpret_cast<decltype(gRealFromChar)>(gOutVt[3]);
    gPathVt[5] = reinterpret_cast<void *>(&RecordToChar);
    gOutVt[3] = reinterpret_cast<void *>(&RecordFromChar);
    std::mt19937 rng(0x1c1c8c);
    auto orig = ctx.Original<uint8_t(__cdecl *)(void *, void *)>(kModuleDirectory);
    for (int run = 0; run < 3000; ++run) {
        std::string text = run == 0 ? "" : run == 1 ? "C:\\" : run == 2 ? "game.exe" : RandomPath(rng);
        const char *old = rng() % 2 ? "old contents" : "";
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            cRZString path, out;
            path.Construct(text.c_str());
            out.Construct(old);
            path.mVTable = gPathVt;
            out.mVTable = gOutVt;
            uint8_t r = side == 0 ? orig(&path, &out) : appbase::GetModuleDirectory(path.AsInterface(), out.AsInterface());
            results[side] = "al=" + std::to_string(r) + " [" + out.mString + "] " + Joined();
            path.mVTable = ctx.image.At<void>(cRZString::kVTable);
            out.mVTable = path.mVTable;
            path.Destruct();
            out.Destruct();
        }
        ExpectSame(ctx, "module directory", results[0], results[1]);
    }
}

// ---- 0x1ba914 / 0x1baae3 reporter constructor and destructor ----

namespace {

void *gFilterNow = nullptr;
void *WINAPI FakeSetFilter(void *filter)
{
    Log("SetUnhandledExceptionFilter(%p)", filter);
    gFilterNow = filter;
    return reinterpret_cast<void *>(0x7000 + (reinterpret_cast<uintptr_t>(filter) & 0xff));
}

const wchar_t *gWideName = L"";
DWORD WINAPI FakeWide(void *module, wchar_t *buffer, DWORD size)
{
    Log("GetModuleFileNameW module=%p size=%u", module, size);
    std::wcscpy(buffer, gWideName);
    return static_cast<DWORD>(std::wcslen(gWideName));
}
DWORD WINAPI FakeNarrow(void *module, char *buffer, DWORD size)
{
    Log("GetModuleFileNameA module=%p size=%u", module, size);
    buffer[0] = 0;
    return 0;
}

} // namespace

DIFF_TEST(ExceptionReporter_CtorDtor, kReporterCtor)
{
    Imports imports(ctx);
    imports.Set(kIatSetFilter, reinterpret_cast<void *>(&FakeSetFilter));
    imports.Set(kIatGetModuleFileNameA, reinterpret_cast<void *>(&FakeNarrow));
    uint8_t *resolved = ctx.image.At<uint8_t>(kModuleResolved);
    void **cached = ctx.image.At<void *>(kModulePointer);
    void **instance = ctx.image.At<void *>(appbase::ExceptionReporterObject::kInstanceGlobal);
    const uint8_t savedResolved = *resolved;
    void *const savedCached = *cached;
    void *const savedInstance = *instance;
    *resolved = 1;
    *cached = reinterpret_cast<void *>(&FakeWide);

    const std::wstring kLong = L"C:\\" + std::wstring(250, L'd') + L"\\g.exe"; // a directory part right at the limit (the buffer is 260)
    const wchar_t *const kModules[] = {L"C:\\Games\\The Sims 2\\EP9\\TSBin\\Sims2EP9.exe", L"Sims2EP9.exe", L"D:\\x.exe", L"", L"\\\\server\\share\\a.b\\c.exe",
                                              kLong.c_str()};
    for (int run = 0; run < 60; ++run) {
        gWideName = kModules[run % 6];
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            gFilterNow = nullptr;
            *ctx.image.At<int32_t>(kSystemCodePageCache) = -3;
            alignas(8) uint8_t raw[appbase::ExceptionReporterObject::kSize + 8];
            std::memset(raw, 0x37 + run, sizeof(raw));
            void *r = side == 0 ? ctx.Original<void *(__thiscall *)(void *)>(kReporterCtor)(raw) : reinterpret_cast<appbase::ExceptionReporterObject *>(raw)->Construct();
            auto *obj = reinterpret_cast<appbase::ExceptionReporterObject *>(raw);
            // The list's head node is a heap pointer: compare the list through its contents, the
            // rest of the object bytes as they are (the filter is an absolute address; make it comparable).
            uint8_t copy[sizeof(raw)];
            std::memcpy(copy, raw, sizeof(raw));
            std::memset(copy + 0x1a0, 0, 4);
            for (uint32_t offset = 0; offset <= 0x80; offset += 0x20) { // a heap string's pointer differs per side
                auto *string = reinterpret_cast<const cRZString *>(raw + offset);
                if (string->mString.capacity() > 15) std::memset(copy + offset + 4, 0, 4);
            }
            std::string texts = obj->mDirectory.mString + "|" + obj->mReportName.mString + "|" + obj->mDumpName.mString + "|" + obj->mReportFile.mString + "|" + obj->mDumpFile.mString + "|";
            std::string log = Joined();
            char filter[16];
            std::snprintf(filter, sizeof(filter), "%p", ctx.image.At<void>(0x1bd687));
            for (size_t at; (at = log.find(filter)) != std::string::npos;) log.replace(at, std::strlen(filter), "FILTER");
            std::string state = std::string(r == raw ? "this " : "other ") + Hex(copy, sizeof(copy)) + " " + texts + " list " + std::to_string(obj->mListeners.size()) + " empty " + std::to_string(obj->mListeners.empty()) + " global " +
                                std::to_string(*instance == raw) + " filterNow " + std::to_string(gFilterNow == ctx.image.At<void>(0x1bd687)) + "\n" + log;
            // Destruct.
            gLog.clear();
            gFilterNow = nullptr;
            if (side == 0) ctx.Original<void(__thiscall *)(void *)>(kReporterDtor)(raw);
            else obj->Destruct();
            state += "after dtor: global " + std::to_string(*instance == nullptr) + " filterNow " + std::to_string(gFilterNow == nullptr) + "\n" + Joined();
            results[side] = state;
        }
        ExpectSame(ctx, "reporter ctor/dtor", results[0], results[1]);
    }
    *resolved = savedResolved;
    *cached = savedCached;
    *instance = savedInstance;
}

// ---- small members ----

namespace {

constexpr uint32_t kWriteLine = 0x1c4347, kRemoveFilter = 0x1bd001, kSetDirectory = 0x1bd42a, kAddListener = 0x1bac86, kAddExtra = 0x1bac12;
constexpr uint32_t kExtraEnd = 0x1bb00a, kBroadcast = 0x1bd339, kNotifyListener = 0x1bd39f;
constexpr uint32_t kIatGetCurrentProcess = 0xd9a1b8, kIatTerminateProcess = 0xd9a1c0;

// A text file whose Write slot (16) records and answers from a script.
void *gFileVt[40];
std::vector<int> gWriteResults;
size_t gWriteIndex = 0;
uint8_t __fastcall FakeWrite(void *, void *, const void *data, uint32_t length)
{
    Log("Write len=%u [%s]", length, Hex(data, length).c_str());
    uint8_t r = gWriteIndex < gWriteResults.size() ? static_cast<uint8_t>(gWriteResults[gWriteIndex]) : 1;
    ++gWriteIndex;
    return r;
}

void MakeFileVt(difftest::Context &ctx)
{
    std::memcpy(gFileVt, ctx.image.At<void>(cRZTextFile::kVTable), sizeof(gFileVt));
    gFileVt[16] = reinterpret_cast<void *>(&FakeWrite);
}

} // namespace

DIFF_TEST(ExceptionReporter_WriteLine, kWriteLine)
{
    MakeFileVt(ctx);
    std::mt19937 rng(0x1c4347);
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, const char *, uint32_t)>(kWriteLine);
    static const char *const kTexts[] = {"", "a line", "Extra Exception Data"};
    static const int32_t kSizes[] = {0, 1, 2, 3, 0x7fffffff, -1};
    for (int run = 0; run < 600; ++run) {
        const char *text = kTexts[rng() % 3];
        int32_t charSize = kSizes[rng() % 6];
        gWriteResults = {static_cast<int>(rng() % 3), static_cast<int>(rng() % 3)};
        if (run % 3 == 0) gWriteResults[0] = 0;
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            alignas(8) uint8_t raw[cRZTextFile::kSize];
            std::memset(raw, 0x66, sizeof(raw));
            *reinterpret_cast<void **>(raw) = gFileVt;
            *reinterpret_cast<int32_t *>(raw + 0x9c) = charSize;
            gLog.clear();
            gWriteIndex = 0;
            uint8_t r = side == 0 ? orig(raw, text, static_cast<uint32_t>(std::strlen(text))) : appbase::WriteLine(reinterpret_cast<cRZTextFile *>(raw), nullptr, text, static_cast<uint32_t>(std::strlen(text)));
            results[side] = "al=" + std::to_string(r) + "\n" + Joined();
        }
        ExpectSame(ctx, "write line", results[0], results[1]);
    }
}

namespace {
std::vector<std::string> gCalls;
HANDLE gProcess = reinterpret_cast<HANDLE>(0x1234);
void *WINAPI FakeSetFilterNull(void *filter)
{
    Log("SetUnhandledExceptionFilter(%p)", filter);
    return nullptr;
}
HANDLE WINAPI FakeGetCurrentProcess()
{
    Log("GetCurrentProcess");
    return gProcess;
}
BOOL WINAPI FakeTerminateProcess(HANDLE process, UINT code)
{
    Log("TerminateProcess(%p, %u)", process, code);
    return 1;
}
} // namespace

DIFF_TEST(ExceptionReporter_RemoveFilterAndTerminate, kRemoveFilter)
{
    Imports imports(ctx);
    imports.Set(kIatSetFilter, reinterpret_cast<void *>(&FakeSetFilterNull));
    imports.Set(kIatGetCurrentProcess, reinterpret_cast<void *>(&FakeGetCurrentProcess));
    imports.Set(kIatTerminateProcess, reinterpret_cast<void *>(&FakeTerminateProcess));
    std::string results[2];
    for (int side = 0; side < 2; ++side) {
        gLog.clear();
        if (side == 0) ctx.Original<void(__cdecl *)()>(kRemoveFilter)();
        else appbase::RemoveFilterAndTerminate();
        results[side] = Joined();
    }
    ExpectSame(ctx, "remove filter and terminate", results[0], results[1]);
}

DIFF_TEST(ExceptionReporter_SetDirectory, kSetDirectory)
{
    std::mt19937 rng(0x1bd42a);
    auto orig = ctx.Original<void(__thiscall *)(void *, const void *)>(kSetDirectory);
    static const char *const kDirs[] = {"", "C:\\Games\\", "C:\\Games", "/usr/local/", "/usr/local", "exactly15chars\\", "exactly 15 char", "a considerably longer directory name that needs the heap\\", "a considerably longer directory name that needs the heap",
                                        "x"};
    // The empty directory reads the byte before the text, the top byte of the vtable pointer for a
    // small string: give the objects vtable values that make it a separator or not.
    static const uint32_t kFakeVt[] = {0x5c000000, 0x2f000000, 0x41000000};
    for (int run = 0; run < 900; ++run) {
        const char *dir = kDirs[rng() % 10];
        const char *old = kDirs[rng() % 10];
        uint32_t fakeVt = kFakeVt[rng() % 3];
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            alignas(8) uint8_t raw[0x20];
            std::memset(raw, 0x42, sizeof(raw));
            auto *self = reinterpret_cast<cRZString *>(raw);
            self->Construct(old);
            self->mVTable = reinterpret_cast<const void *>(static_cast<uintptr_t>(fakeVt));
            cRZString argument;
            argument.Construct(dir);
            if (side == 0) orig(self, &argument);
            else reinterpret_cast<appbase::ExceptionReporterObject *>(raw)->SetDirectory(&argument);
            results[side] = "[" + self->mString + "] " + std::to_string(self->mString.size()) + "/" + std::to_string(self->mString.capacity()) + " arg [" + argument.mString + "] " + Hex(raw, 4);
            self->mString.~basic_string();
            argument.Destruct();
        }
        ExpectSame(ctx, "set directory", results[0], results[1]);
    }
}

namespace {
// A listener with recorded AddRef/Release/notify slots.
void *gListenerVt[8];
uint32_t gThrowOn = 0;
int gSide = 0;
uint32_t __fastcall FakeAddRef(void *self, void *)
{
    Log("AddRef %u", static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self) & 3) );
    return 1;
}
uint32_t __fastcall FakeRelease(void *self, void *)
{
    Log("Release %u", static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self) & 3));
    return 1;
}
uint32_t __fastcall FakeNotify(void *self, void *, void *message)
{
    uint32_t tag = *(static_cast<uint32_t *>(self) + 1);
    Log("Notify listener %u message-id %x", tag, message ? *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(message) + 0xc) : 0xffffffffu);
    if (tag == gThrowOn && gSide == 1) RaiseException(0xE0001234, 0, 0, nullptr);
    if (tag == gThrowOn + 100) *static_cast<volatile int *>(nullptr) = 1;
    return 0x40 + tag;
}
} // namespace

DIFF_TEST(ExceptionReporter_Listeners, kAddListener)
{
    gListenerVt[0] = nullptr;
    gListenerVt[1] = reinterpret_cast<void *>(&FakeAddRef);
    gListenerVt[2] = reinterpret_cast<void *>(&FakeRelease);
    gListenerVt[3] = reinterpret_cast<void *>(&FakeNotify);
    struct Listener {
        void *vtable;
        uint32_t tag;
    };
    Listener listeners[4];
    for (uint32_t i = 0; i < 4; ++i) listeners[i] = {gListenerVt, i};
    auto addOrig = ctx.Original<void(__thiscall *)(void *, void *)>(kAddListener);
    auto notifyOrig = ctx.Original<uint32_t(__thiscall *)(void *, void *, void *)>(kNotifyListener);
    std::mt19937 rng(0x1bac86);
    for (int run = 0; run < 200; ++run) {
        gThrowOn = 99; // the image's SEH handler does not catch in the harness (see the ours-only check below)
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gSide = side;
            gLog.clear();
            alignas(8) uint8_t raw[appbase::ExceptionReporterObject::kSize];
            std::memset(raw, 0x11, sizeof(raw));
            auto *obj = reinterpret_cast<appbase::ExceptionReporterObject *>(raw);
            new (&obj->mListeners) std::list<void *>();
            std::mt19937 draw(run);
            int count = static_cast<int>(draw() % 6);
            for (int i = 0; i < count; ++i) {
                void *l = draw() % 7 == 0 ? nullptr : &listeners[draw() % 4];
                if (side == 0) addOrig(raw, l);
                else obj->AddListener(static_cast<cIGZUnknown *>(l));
            }
            std::string state = "listeners:";
            for (void *p : obj->mListeners) state += " " + std::to_string(p == nullptr ? -1 : static_cast<int>(static_cast<Listener *>(p) - listeners));
            state += " size " + std::to_string(obj->mListeners.size());
            // NotifyListener on a listener, optionally raising inside it.
            uint32_t tag = draw() % 4;
            uint32_t message[4] = {0, 0, 0, 0xabc00 + static_cast<uint32_t>(run)};
            uint32_t r = side == 0 ? notifyOrig(raw, &listeners[tag], message) : obj->NotifyListener(reinterpret_cast<cIGZUnknown *>(&listeners[tag]), message);
            // After an exception the result register is unspecified in the original: compare the value only without one.
            state += gThrowOn == tag ? " (raised)" : " r=" + std::to_string(r);
            results[side] = state + "\n" + Joined();
            obj->mListeners.~list();
        }
        ExpectSame(ctx, "listeners", results[0], results[1]);
    }
    // Ours only: a listener that raises is swallowed (the original's SEH handler does not run in the
    // harness: the exception ends the process). The run reaching the next line is the check.
    gSide = 1;
    gThrowOn = 2;
    gLog.clear();
    alignas(8) uint8_t raw[appbase::ExceptionReporterObject::kSize];
    std::memset(raw, 0, sizeof(raw));
    reinterpret_cast<appbase::ExceptionReporterObject *>(raw)->NotifyListener(reinterpret_cast<cIGZUnknown *>(&listeners[2]), raw);
    ExpectSame(ctx, "listener raising (ours)", "Notify listener 2 message-id 0\n", Joined());
    gThrowOn = 99;
}

DIFF_TEST(ExceptionReporter_ExtraData, kAddExtra)
{
    MakeFileVt(ctx);
    std::mt19937 rng(0x1bac12);
    auto addOrig = ctx.Original<void(__thiscall *)(void *, const char *, const char *, uint32_t)>(kAddExtra);
    auto endOrig = ctx.Original<void(__thiscall *)(void *)>(kExtraEnd);
    static const char *const kTexts[] = {"", "Version", "1.18.0.187", "a value long enough to need the heap, certainly more than fifteen characters"};
    for (int run = 0; run < 600; ++run) {
        int32_t charSize = run % 4 == 0 ? 2 : 0;
        uint8_t started = static_cast<uint8_t>(rng() % 3 == 0 ? 1 : (rng() % 5 == 0 ? 7 : 0));
        int calls = static_cast<int>(rng() % 4);
        const char *labels[3], *values[3];
        for (int i = 0; i < 3; ++i) {
            labels[i] = kTexts[rng() % 4];
            values[i] = kTexts[rng() % 4];
        }
        gWriteResults = {1, 1, 1, 1, 1, 1, 1, 1};
        if (run % 7 == 0) gWriteResults[1] = 0;
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            gWriteIndex = 0;
            alignas(8) uint8_t raw[appbase::ExceptionReporterObject::kSize];
            std::memset(raw, 0x99, sizeof(raw));
            *reinterpret_cast<void **>(raw + 0xa0) = gFileVt;
            *reinterpret_cast<int32_t *>(raw + 0xa0 + 0x9c) = charSize;
            raw[0x19c] = started;
            auto *obj = reinterpret_cast<appbase::ExceptionReporterObject *>(raw);
            for (int i = 0; i < calls; ++i) {
                if (side == 0) addOrig(raw, labels[i], values[i], 7 + i);
                else obj->AddExtraData(labels[i], values[i], 7 + i);
                if (i == 1) {
                    if (side == 0) endOrig(raw);
                    else obj->WriteExtraDataEnd();
                }
            }
            if (side == 0) endOrig(raw);
            else obj->WriteExtraDataEnd();
            results[side] = "flag " + std::to_string(raw[0x19c]) + " " + Hex(raw + 0x190, 0x10) + "\n" + Joined();
        }
        ExpectSame(ctx, "extra data", results[0], results[1]);
    }
}

namespace {
void *gMessageVt[40];
uint32_t __fastcall MsgAddRef(void *, void *)
{
    Log("message AddRef");
    return 1;
}
uint32_t __fastcall MsgRelease(void *, void *)
{
    Log("message Release");
    return 0;
}
void __fastcall MsgSetId(void *self, void *, uint32_t id)
{
    // The constructor's bytes (not the padding at +9..+0xb and not the vtable) are hashed.
    uint32_t h = 2166136261u;
    for (uint32_t i = 4; i < 0x2c; ++i) {
        if (i >= 9 && i <= 0xb) continue;
        h = (h ^ static_cast<uint8_t *>(self)[i]) * 16777619u;
    }
    Log("message SetId %x state %08x", id, h);
    *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(self) + 0xc) = id;
}
void *__cdecl TestMalloc(uint32_t size)
{
    void *p = HeapAlloc(GetProcessHeap(), 0, size);
    Log("alloc %u", size);
    std::memset(p, 0xcd, size);
    return p;
}
struct CodeRedirect {
    uint8_t *site;
    uint8_t saved[5];
    CodeRedirect(difftest::Context &ctx, uint32_t rva, void *target) : site(ctx.image.At<uint8_t>(rva))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    ~CodeRedirect() { std::memcpy(site, saved, 5); }
};
} // namespace

DIFF_TEST(ExceptionReporter_Broadcast, kBroadcast)
{
    gListenerVt[0] = nullptr;
    gListenerVt[1] = reinterpret_cast<void *>(&FakeAddRef);
    gListenerVt[2] = reinterpret_cast<void *>(&FakeRelease);
    gListenerVt[3] = reinterpret_cast<void *>(&FakeNotify);
    CodeRedirect malloc(ctx, 0x1be3b0, reinterpret_cast<void *>(&TestMalloc));
    // Ours allocates through the global allocator pointer: aim it at one on our own arena.
    auto *allocator = static_cast<GeneralAllocator *>(::operator new(sizeof(GeneralAllocator)));
    allocator->Construct(nullptr, 0, 0, 0, nullptr, nullptr);
    RZMemory::testGlobalAllocator = allocator;
    void **vtable = ctx.image.At<void *>(cRZMessage2Standard::kVTable);
    void *savedSlots[3] = {vtable[1], vtable[2], vtable[5]};
    DWORD old;
    VirtualProtect(vtable, 64, PAGE_READWRITE, &old);
    vtable[1] = reinterpret_cast<void *>(&MsgAddRef);
    vtable[2] = reinterpret_cast<void *>(&MsgRelease);
    vtable[5] = reinterpret_cast<void *>(&MsgSetId);
    struct Listener {
        void *vtable;
        uint32_t tag;
    };
    Listener listeners[4];
    for (uint32_t i = 0; i < 4; ++i) listeners[i] = {gListenerVt, i};
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(kBroadcast);
    std::mt19937 rng(0x1bd339);
    for (int run = 0; run < 200; ++run) {
        gThrowOn = 99;
        uint32_t id = run % 4 == 0 ? 0xb627ad2 : static_cast<uint32_t>(rng());
        int count = static_cast<int>(rng() % 5);
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gSide = side;
            gLog.clear();
            alignas(8) uint8_t raw[appbase::ExceptionReporterObject::kSize];
            std::memset(raw, 0x11, sizeof(raw));
            auto *obj = reinterpret_cast<appbase::ExceptionReporterObject *>(raw);
            new (&obj->mListeners) std::list<void *>();
            for (int i = 0; i < count; ++i) obj->mListeners.push_back(&listeners[(run + i) % 4]);
            if (side == 0) orig(raw, id);
            else obj->Broadcast(id);
            std::string log = Joined();
            // Ours allocates from its own allocator (the log line "alloc" is the original's only).
            for (size_t at; (at = log.find("alloc 44\n")) != std::string::npos;) log.erase(at, 9);
            results[side] = log;
            obj->mListeners.~list();
        }
        ExpectSame(ctx, "broadcast", results[0], results[1]);
    }
    {
        // Ours only (see the listeners test): the first of three listeners raises, the others still run.
        gSide = 1;
        gThrowOn = 1;
        gLog.clear();
        alignas(8) uint8_t raw[appbase::ExceptionReporterObject::kSize];
        auto *obj = reinterpret_cast<appbase::ExceptionReporterObject *>(raw);
        new (&obj->mListeners) std::list<void *>();
        for (int i = 0; i < 3; ++i) obj->mListeners.push_back(&listeners[i]);
        obj->Broadcast(0x77);
        ExpectSame(ctx, "broadcast with a raising listener (ours)", "message AddRef\nmessage SetId 77 state 2dfe038f\nNotify listener 0 message-id 77\nNotify listener 1 message-id 77\nNotify listener 2 message-id 77\nmessage Release\n", Joined());
        obj->mListeners.~list();
        gThrowOn = 99;
    }
    vtable[1] = savedSlots[0];
    vtable[2] = savedSlots[1];
    vtable[5] = savedSlots[2];
    RZMemory::testGlobalAllocator = nullptr;
    difftest_alloc::ShutdownAndRelease(allocator);
    ::operator delete(allocator);
}
