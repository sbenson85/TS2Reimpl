// Differential tests for src/app/ReportBlockers.*: the module/section lookup of an address
// (0x1bbe01), directory creation (0x1c4caf) and the report file names (0x1bd49b).
//
// Windows functions go through the import table; the tests point those slots (and the game's two
// wide directory function pointers) at scripted fakes for the life of a guard. Both sides see the
// same script and their logs, results and outputs are compared.
#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#include "app/ReportBlockers.h"
#include "app/ReportText.h"
#include "difftest.h"
#include "game/game.h"
#include "rz/cRZString.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kFindModule = 0x1bbe01, kCreateDirectories = 0x1c4caf, kNameFiles = 0x1bd49b;
constexpr uint32_t kIatGetModuleFileNameA = 0xd9a16c, kIatVirtualQuery = 0xd9a1cc, kIatFindFirstFileA = 0xd9a1f8, kIatFindClose = 0xd9a1f4;
constexpr uint32_t kCreateDirectoryWPointer = 0x10cb620, kRemoveDirectoryWPointer = 0x10cb624, kWildcardPointer = 0xff2478;
constexpr uint32_t kModuleResolved = 0x10caeb5, kModulePointer = 0x10caeb8, kSystemCodePageCache = 0xff23a4;
constexpr uint32_t kEpochGlobal = 0x10cafe0, kTimeZoneGlobal = 0x10dfc98, kDaylightGlobal = 0x10dfc9c;

std::vector<std::string> gLog;

void Log(const char *fmt, ...)
{
    char buf[1024];
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

std::string Narrow(const wchar_t *wide)
{
    std::string out;
    for (; *wide; ++wide) out += *wide < 0x80 ? static_cast<char>(*wide) : '?';
    return out;
}

void ExpectSame(difftest::Context &ctx, const char *what, const std::string &a, const std::string &b)
{
    ++ctx.cases;
    if (a != b) {
        std::string x = a.size() > 600 ? a.substr(0, 600) : a;
        std::string y = b.size() > 600 ? b.substr(0, 600) : b;
        ctx.Fail("%s:\n  original: %s\n  ours:     %s", what, x.c_str(), y.c_str());
    }
}

class Slots {
public:
    explicit Slots(difftest::Context &c) : ctx(c) {}
    ~Slots()
    {
        for (auto it = saved.rbegin(); it != saved.rend(); ++it) *ctx.image.At<void *>(it->first) = it->second;
    }
    void Set(uint32_t rva, void *value)
    {
        void **slot = ctx.image.At<void *>(rva);
        saved.emplace_back(rva, *slot);
        *slot = value;
    }

private:
    difftest::Context &ctx;
    std::vector<std::pair<uint32_t, void *>> saved;
};

// A cRZString on the stack (original vtable), destroyed at the end of scope.
struct Str {
    alignas(cRZString) unsigned char raw[sizeof(cRZString)];
    explicit Str(const char *text = "")
    {
        std::memset(raw, 0xcd, sizeof(raw));
        S().Construct(text);
    }
    ~Str() { S().Destruct(); }
    Str(const Str &) = delete;
    cRZString &S() { return *reinterpret_cast<cRZString *>(raw); }
    cIGZString *I() { return S().AsInterface(); }
};

// ---- 0x1bbe01 ----

struct ModuleScript {
    bool queryOk = true;
    void *allocationBase = nullptr;
    DWORD nameLength = 0; // what GetModuleFileNameA reports; 0 fails
    std::string name;     // bytes it writes
} gModule;

SIZE_T WINAPI FakeVirtualQuery(const void *address, MEMORY_BASIC_INFORMATION *info, SIZE_T length)
{
    Log("VirtualQuery(%p, %u)", address, static_cast<unsigned>(length));
    if (!gModule.queryOk) return 0;
    std::memset(info, 0x6b, sizeof(*info));
    info->AllocationBase = gModule.allocationBase;
    return sizeof(*info);
}

DWORD WINAPI FakeGetModuleFileName(void *module, char *buffer, DWORD size)
{
    bool zero = true;
    for (int i = 0; i < 0x200; ++i) zero = zero && buffer[i] == 0;
    Log("GetModuleFileNameA(%p, %u) zeroed=%d", module, size, zero);
    std::memcpy(buffer, gModule.name.data(), gModule.name.size());
    return gModule.nameLength;
}

struct SectionSpec {
    uint32_t virtualSize, virtualAddress, rawSize;
};

} // namespace

DIFF_TEST(ReportBlockers_FindModuleOfAddress, kFindModule)
{
    Slots slots(ctx);
    slots.Set(kIatVirtualQuery, reinterpret_cast<void *>(&FakeVirtualQuery));
    slots.Set(kIatGetModuleFileNameA, reinterpret_cast<void *>(&FakeGetModuleFileName));
    int32_t *codePage = ctx.image.At<int32_t>(kSystemCodePageCache);
    const int32_t savedCodePage = *codePage;
    auto original = ctx.Original<uint32_t(__cdecl *)(const void *, cIGZString *, uint32_t *, uint32_t *)>(kFindModule);
    std::mt19937 rng(1234);
    static const char *const kNames[] = {"C:\\Games\\The Sims 2\\EP9\\TSBin\\Sims2EP9.exe", "x", "", "C:\\Sims\\caf\xe9\\\x93quoted\x94.dll",
                                         "a name that is much longer than fifteen characters to force the heap"};
    for (int run = 0; run < 1500; ++run) {
        // A fake PE image: DOS header with e_lfanew, NT headers, a variable optional header, sections.
        alignas(16) uint8_t image[0x800];
        std::memset(image, 0, sizeof(image));
        uint32_t ntAt = 0x40 + (rng() % 8) * 8;
        *reinterpret_cast<uint32_t *>(image + 0x3c) = ntAt;
        uint32_t optionalSize = (rng() % 4 == 0) ? (rng() % 3) * 8 : 0xe0;
        uint32_t count = rng() % 8;
        if (rng() % 10 == 0) count = 0;
        *reinterpret_cast<uint16_t *>(image + ntAt + 6) = static_cast<uint16_t>(count);
        *reinterpret_cast<uint16_t *>(image + ntAt + 0x14) = static_cast<uint16_t>(optionalSize);
        std::vector<SectionSpec> specs;
        uint32_t cursor = 0x1000;
        for (uint32_t i = 0; i < count; ++i) {
            SectionSpec s;
            s.virtualAddress = (rng() % 5 == 0) ? static_cast<uint32_t>(rng() % 0x6000) : cursor;
            s.virtualSize = (rng() % 4 == 0) ? 0 : static_cast<uint32_t>(rng() % 0x1800);
            s.rawSize = (rng() % 4 == 0) ? 0 : static_cast<uint32_t>(rng() % 0x1800);
            cursor = (s.virtualAddress + std::max(s.virtualSize, s.rawSize) + 0xfff) & ~0xfffu;
            specs.push_back(s);
            uint8_t *header = image + ntAt + 0x18 + optionalSize + i * 0x28;
            if (header + 0x28 > image + sizeof(image)) break;
            std::memcpy(header, "section", 8);
            *reinterpret_cast<uint32_t *>(header + 8) = s.virtualSize;
            *reinterpret_cast<uint32_t *>(header + 0xc) = s.virtualAddress;
            *reinterpret_cast<uint32_t *>(header + 0x10) = s.rawSize;
        }
        // The address: inside, on the boundaries of, or outside a section (offsets from the base).
        uint32_t rva;
        if (!specs.empty() && rng() % 8 != 0) {
            const SectionSpec &s = specs[rng() % specs.size()];
            uint32_t size = std::max(s.virtualSize, s.rawSize);
            static const int32_t deltas[] = {-1, 0, 1, 2};
            switch (rng() % 5) {
            case 0: rva = s.virtualAddress + deltas[rng() % 4]; break;
            case 1: rva = s.virtualAddress + size + deltas[rng() % 4]; break;
            case 2: rva = s.virtualAddress + (size ? rng() % size : 0); break;
            default: rva = rng() % 0x8000; break;
            }
        } else {
            rva = static_cast<uint32_t>(rng() % 0x9000);
        }
        gModule = ModuleScript{};
        gModule.queryOk = rng() % 12 != 0;
        gModule.allocationBase = rng() % 12 == 0 ? nullptr : image;
        const char *text = kNames[rng() % 5];
        gModule.name = text;
        gModule.nameLength = rng() % 12 == 0 ? 0 : static_cast<DWORD>(gModule.name.size());
        const void *address = image + rva; // never read: only the offset from the base matters
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            *codePage = -3;
            gLog.clear();
            Str name("previous text");
            uint32_t section = 0x5a5a5a5a, offset = 0xa5a5a5a5;
            uint32_t r = side == 0 ? original(address, name.I(), &section, &offset) : appbase::FindModuleOfAddress(address, name.I(), &section, &offset);
            // Rebase the pointers in the log so both sides print the same text.
            std::string log = Joined();
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%p", static_cast<void *>(image));
            for (size_t at; (at = log.find(buf)) != std::string::npos;) log.replace(at, std::strlen(buf), "BASE");
            std::snprintf(buf, sizeof(buf), "%p", address);
            for (size_t at; (at = log.find(buf)) != std::string::npos;) log.replace(at, std::strlen(buf), "ADDRESS");
            results[side] = "ret " + std::to_string(r & 0xff) + " section " + std::to_string(section) + " offset " + std::to_string(offset) + " name \"" + name.S().mString + "\" " + log;
        }
        ExpectSame(ctx, "FindModuleOfAddress", results[0], results[1]);
    }
    *codePage = savedCodePage;
}

// ---- 0x1c4caf and 0x1bd49b: a fake file system ----

namespace {

// The directories that "exist": FindFirstFileA of "<dir>\*" succeeds for them. The wide fakes add
// to it; the failing list refuses creation.
struct FakeFs {
    std::set<std::string> directories;
    std::set<std::string> refuse; // directory names (lower case, with the trailing backslash) that cannot be created
    std::set<std::string> madeBy; // unused
} gFs;

std::string Lower(std::string s)
{
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Directory keys are lower case with every slash a backslash and a trailing backslash.
std::string Key(std::string path)
{
    for (char &c : path) if (c == '/') c = '\\';
    if (path.empty() || path.back() != '\\') path += '\\';
    return Lower(path);
}

HANDLE WINAPI FakeFindFirst(const char *pattern, WIN32_FIND_DATAA *data)
{
    Log("FindFirstFileA(%s)", pattern);
    std::string p = pattern;
    if (p.size() < 1 || p.back() != '*') return INVALID_HANDLE_VALUE;
    p.pop_back();
    if (!gFs.directories.count(Key(p))) return INVALID_HANDLE_VALUE;
    std::memset(data, 0, sizeof(*data));
    data->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
    std::strcpy(data->cFileName, ".");
    return reinterpret_cast<HANDLE>(0x4242);
}

BOOL WINAPI FakeFindClose(HANDLE)
{
    Log("FindClose");
    return TRUE;
}

BOOL WINAPI FakeCreateDirectoryW(const wchar_t *path, void *attributes)
{
    std::string p = Narrow(path);
    bool refused = gFs.refuse.count(Key(p)) != 0;
    Log("CreateDirectoryW(%s, %p) -> %d", p.c_str(), attributes, !refused);
    if (refused) return FALSE;
    gFs.directories.insert(Key(p));
    return TRUE;
}

BOOL WINAPI FakeRemoveDirectoryW(const wchar_t *path)
{
    std::string p = Narrow(path);
    Log("RemoveDirectoryW(%s)", p.c_str());
    gFs.directories.erase(Key(p));
    return TRUE;
}

std::string FsState()
{
    std::string out;
    for (const std::string &d : gFs.directories) out += d + ";";
    return out;
}

} // namespace

DIFF_TEST(ReportBlockers_CreateDirectories_Wide, kCreateDirectories)
{
    Slots slots(ctx);
    slots.Set(kIatFindFirstFileA, reinterpret_cast<void *>(&FakeFindFirst));
    slots.Set(kIatFindClose, reinterpret_cast<void *>(&FakeFindClose));
    slots.Set(kCreateDirectoryWPointer, reinterpret_cast<void *>(&FakeCreateDirectoryW));
    slots.Set(kRemoveDirectoryWPointer, reinterpret_cast<void *>(&FakeRemoveDirectoryW));
    auto original = ctx.Original<uint32_t(__cdecl *)(cRZString *)>(kCreateDirectories);
    const char *const paths[] = {"C:\\Games\\The Sims 2\\Logs\\", "C:\\Games\\The Sims 2\\Logs", "C:/Games/The Sims 2/Logs/", "C:\\a", "C:\\a\\", "", "\\", "/", "x",
                                 "\\\\server\\share\\dir\\sub", "\\\\server\\share\\", "\\\\server", "\\\\server\\", "\\\\\\triple\\a", "\\\\s\\a\\b\\c\\d",
                                 "relative\\path with spaces\\and caf\xc3\xa9", "C:\\Games\\a very long directory name that needs the heap\\and another one\\",
                                 "C:\\mix/of\\both/kinds"};
    std::mt19937 rng(77);
    for (int run = 0; run < 900; ++run) {
        const char *path = paths[run % (sizeof(paths) / sizeof(paths[0]))];
        // Existing directories: a random subset of the prefixes of the path (and sometimes the drive).
        std::vector<std::string> prefixes;
        {
            std::string p = path;
            if (!p.empty() && p.back() != '\\' && p.back() != '/') p += '\\';
            for (size_t i = 0; i < p.size(); ++i) if (p[i] == '\\' || p[i] == '/') prefixes.push_back(p.substr(0, i + 1));
        }
        std::set<std::string> existing, refuse;
        for (const std::string &prefix : prefixes) {
            if (rng() % 3 == 0) existing.insert(Key(prefix));
            else if (rng() % 7 == 0) refuse.insert(Key(prefix));
        }
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gFs = FakeFs{};
            gFs.directories = existing;
            gFs.refuse = refuse;
            gLog.clear();
            Str input(path);
            uint32_t r = side == 0 ? original(&input.S()) : appbase::CreateDirectories(&input.S());
            results[side] = "ret " + std::to_string(r & 0xff) + " arg \"" + input.S().mString + "\" fs " + FsState() + " log " + Joined();
        }
        ExpectSame(ctx, path, results[0], results[1]);
    }
}

// ---- 0x1bd49b ----

namespace {

// The image's localtime wrapper (0x82fb8c) normally ends in the game CRT's time-zone initialisation,
// which asks the OS (it traps in the harness): redirected to our CRT's conversion, as in
// tests/report_text_date.cpp, so only the code around it is under test.
int __cdecl StubLocalTime(struct tm *out, const __time64_t *value)
{
    return _localtime64_s(out, value);
}

class ClockFixture {
public:
    explicit ClockFixture(difftest::Context &c) : ctx(c)
    {
        __time64_t zero = 0; // let our C runtime run its one-time time-zone initialisation first
        struct tm scratch;
        _localtime64_s(&scratch, &zero);
        site = ctx.image.At<uint8_t>(0x82fb8c);
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&StubLocalTime) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
        savedEpoch = *ctx.image.At<uint32_t>(kEpochGlobal);
        savedZone = *ctx.image.At<int32_t>(kTimeZoneGlobal);
        savedDaylight = *ctx.image.At<int32_t>(kDaylightGlobal);
        ourZone = *__timezone();
        ourDaylight = *__daylight();
        *ctx.image.At<uint32_t>(kEpochGlobal) = appbase::cRZDate::kEpochDay;
    }
    ~ClockFixture()
    {
        std::memcpy(site, saved, 5);
        *ctx.image.At<uint32_t>(kEpochGlobal) = savedEpoch;
        *ctx.image.At<int32_t>(kTimeZoneGlobal) = savedZone;
        *ctx.image.At<int32_t>(kDaylightGlobal) = savedDaylight;
        *__timezone() = ourZone;
        *__daylight() = ourDaylight;
    }
    void Set(int32_t zone, int32_t daylight)
    {
        *ctx.image.At<int32_t>(kTimeZoneGlobal) = zone;
        *ctx.image.At<int32_t>(kDaylightGlobal) = daylight;
        *__timezone() = zone;
        *__daylight() = daylight;
    }

private:
    difftest::Context &ctx;
    uint8_t *site = nullptr;
    uint8_t saved[5];
    uint32_t savedEpoch;
    int32_t savedZone, savedDaylight, ourZone, ourDaylight;
};

const wchar_t *gModuleName = L"";
DWORD WINAPI FakeModuleFileNameW(void *module, wchar_t *buffer, DWORD size)
{
    Log("GetModuleFileNameW module=%p size=%u", module, size);
    std::wcscpy(buffer, gModuleName);
    return static_cast<DWORD>(std::wcslen(gModuleName));
}

DWORD WINAPI FakeModuleFileNameNarrow(void *module, char *buffer, DWORD size)
{
    Log("GetModuleFileNameA module=%p size=%u", module, size);
    buffer[0] = 0;
    return 0;
}

struct ReporterBlock {
    alignas(8) uint8_t raw[appbase::ExceptionReporterObject::kSize];
    ReporterBlock(const char *directory)
    {
        std::memset(raw, 0x5c, sizeof(raw));
        self().mDirectory.Construct(directory);
        self().mReportName.Construct("Exception Report");
        self().mDumpName.Construct("Minidump");
        self().mReportFile.Construct("old report file name that is long enough for the heap");
        self().mDumpFile.Construct("old");
    }
    ~ReporterBlock()
    {
        self().mDirectory.Destruct();
        self().mReportName.Destruct();
        self().mDumpName.Destruct();
        self().mReportFile.Destruct();
        self().mDumpFile.Destruct();
    }
    appbase::ExceptionReporterObject &self() { return *reinterpret_cast<appbase::ExceptionReporterObject *>(raw); }
};

} // namespace

DIFF_TEST(ReportBlockers_NameFiles, kNameFiles)
{
    ClockFixture clock(ctx);
    Slots slots(ctx);
    slots.Set(kIatFindFirstFileA, reinterpret_cast<void *>(&FakeFindFirst));
    slots.Set(kIatFindClose, reinterpret_cast<void *>(&FakeFindClose));
    slots.Set(kCreateDirectoryWPointer, reinterpret_cast<void *>(&FakeCreateDirectoryW));
    slots.Set(kRemoveDirectoryWPointer, reinterpret_cast<void *>(&FakeRemoveDirectoryW));
    slots.Set(kIatGetModuleFileNameA, reinterpret_cast<void *>(&FakeModuleFileNameNarrow));
    slots.Set(kModulePointer, reinterpret_cast<void *>(&FakeModuleFileNameW));
    uint8_t *resolved = ctx.image.At<uint8_t>(kModuleResolved);
    const uint8_t savedResolved = *resolved;
    *resolved = 1;
    int32_t *codePage = ctx.image.At<int32_t>(kSystemCodePageCache);
    const int32_t savedCodePage = *codePage;
    auto original = ctx.Original<void(__thiscall *)(void *)>(kNameFiles);

    struct Scenario {
        const char *directory;
        std::vector<const char *> existing; // directories that exist
        std::vector<const char *> refuse;   // directories that cannot be created
    };
    const Scenario scenarios[] = {
        {"C:\\Games\\Sims\\Logs\\", {"C:\\Games\\Sims\\Logs\\"}, {}},                                // exists
        {"C:\\Games\\Sims\\Logs", {"C:\\Games\\Sims\\Logs"}, {}},                                   // exists, no trailing slash
        {"C:\\Games\\Sims\\Logs\\", {"C:\\Games\\"}, {}},                                           // created
        {"C:\\Games\\Sims\\Logs\\", {"C:\\Games\\"}, {"C:\\Games\\Sims\\Logs\\"}},                  // cannot be created: the module's directory
        {"C:\\Games\\Sims\\Logs\\", {}, {"C:\\Games\\Sims\\"}},                                     // same, nothing exists
        {"", {}, {}},                                                                              // empty: creation fails, module directory
        {"\\\\server\\share\\reports\\", {"\\\\server\\share\\"}, {}},                                // network path
        {"a very long directory name that needs a heap block\\and one more\\", {}, {}},             // created from nothing
    };
    const wchar_t *const modules[] = {L"C:\\Games\\The Sims 2\\EP9\\TSBin\\Sims2EP9.exe", L"D:\\x.exe", L"Sims2EP9.exe", L""};
    std::mt19937 rng(515);
    for (int run = 0; run < 160; ++run) {
        const Scenario &sc = scenarios[run % 8];
        gModuleName = modules[(run / 8) % 4];
        static const int32_t zones[] = {28800, 0, -3600, 18000, -19800, 14400};
        clock.Set(zones[rng() % 6], static_cast<int32_t>(rng() % 3) - (rng() % 5 == 0 ? 1 : 0));
        std::string results[2];
        for (int attempt = 0;; ++attempt) {
            __time64_t before = _time64(nullptr);
            for (int side = 0; side < 2; ++side) {
                gFs = FakeFs{};
                for (const char *d : sc.existing) gFs.directories.insert(Key(d));
                for (const char *d : sc.refuse) gFs.refuse.insert(Key(d));
                *codePage = -3;
                gLog.clear();
                ReporterBlock block(sc.directory);
                if (side == 0) original(block.raw);
                else appbase::NameFiles(&block.self(), nullptr);
                results[side] = "dir \"" + block.self().mDirectory.mString + "\" report \"" + block.self().mReportFile.mString + "\" dump \"" +
                                block.self().mDumpFile.mString + "\" names \"" + block.self().mReportName.mString + "\" \"" + block.self().mDumpName.mString +
                                "\" fs " + FsState() + " log " + Joined();
            }
            __time64_t after = _time64(nullptr);
            // Both sides read the clock: retry when the second changed between (or during) the runs.
            if (results[0] == results[1] || before == after || attempt >= 6) break;
        }
        ExpectSame(ctx, sc.directory, results[0], results[1]);
    }
    *resolved = savedResolved;
    *codePage = savedCodePage;
}
