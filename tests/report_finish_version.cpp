// Differential tests for src/app/ReportFinish.*: the version/system information class cRZVersion
// (vtable 0xda4580: constructors, destructors, the version-resource load and string queries, the
// Windows version test, the memory query) and the report's system section (0x1bb94c).
//
// Every OS query goes through the import table; the slots point at scripted fakes for the life of a
// guard (the version resource functions, wsprintfA, VerQueryValueA, VerSetConditionMask,
// VerifyVersionInfoW, GlobalMemoryStatusEx), and the executable's path comes from a fake
// GetModuleFileNameW installed where GetModulePath looks it up. Both sides run on the same script;
// results, outputs and the log of every call with its arguments are compared.
//
// Not tested: a failing GetFileVersionInfoA followed by a query (the original, and ours, then read
// through the null translation pointer: ORIGINAL-BUG in cRZVersion::Load).
#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#include "app/ReportFinish.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "rz/cRZString.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kConstructPath = 0x1c98ab, kConstruct = 0x1c98d6, kDestruct = 0x1c9901, kDeleting = 0x1c992d, kLoad = 0x1c9da3, kGetPath = 0x1c9b58;
constexpr uint32_t kGetDescription = 0x1c9ad1, kGetFileVersion = 0x1c9b6d, kGetDebugVersion = 0x1c9a4a, kGetSystemVersion = 0x1c9c6b, kGetMemory = 0x1c9c01;
constexpr uint32_t kVerify = 0x1c9d0b, kWriteSystemInfo = 0x1bb94c;
constexpr uint32_t kIatGetModuleHandleA = 0xd9a114, kIatVerSetConditionMask = 0xd9a268, kIatGlobalMemoryStatusEx = 0xd9a26c, kIatVerifyVersionInfoW = 0xd9a274;
constexpr uint32_t kIatWsprintfA = 0xd9a5bc, kIatGetFileVersionInfoA = 0xd9a5e8, kIatVerQueryValueA = 0xd9a5ec, kIatGetFileVersionInfoSizeA = 0xd9a5f0;
constexpr uint32_t kModuleResolved = 0x10caeb5, kModulePointer = 0x10caeb8, kSystemCodePageCache = 0xff23a4;
constexpr uint32_t kMemoryTotal = 0x10cb6a8, kMemoryAvailable = 0x10cb6ac;

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

void ExpectSame(difftest::Context &ctx, const char *what, const std::string &a, const std::string &b)
{
    ++ctx.cases;
    if (a != b) {
        size_t at = 0;
        while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
        size_t from = at > 80 ? at - 80 : 0;
        ctx.Fail("%s: differ at %zu\n  original: ...%.400s\n  ours:     ...%.400s", what, at, a.c_str() + from, b.c_str() + from);
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

// ---- the script ----

struct Script {
    DWORD handle = 0x77;
    DWORD size = 0x120;        // GetFileVersionInfoSizeA's answer
    bool infoOk = true;
    bool translationOk = true;
    uint16_t translation[2] = {0x0409, 0x04b0};
    std::map<std::string, std::pair<bool, std::string>> values; // value name -> (found, text)
    bool versionOk = true;     // VerifyVersionInfoW's answer
    uint64_t totalBytes = 0x80000000ull, availableBytes = 0x30000000ull;
    std::wstring modulePath = L"C:\\Games\\The Sims 2\\EP9\\TSBin\\Sims2EP9.exe";
} gS;

uint16_t gTranslation[2]; // what the translation query hands out
std::string gValueText;
int gSizeCalls;

DWORD WINAPI FakeVersionSize(const char *path, DWORD *handle)
{
    Log("GetFileVersionInfoSizeA(%s)", path);
    *handle = gS.handle;
    return gS.size;
}

BOOL WINAPI FakeVersionInfo(const char *path, DWORD handle, DWORD size, void *data)
{
    Log("GetFileVersionInfoA(%s, %x, %u)", path, handle, size);
    if (!gS.infoOk) return FALSE;
    for (DWORD i = 0; i < size; ++i) static_cast<uint8_t *>(data)[i] = static_cast<uint8_t>(i * 5 + 1);
    return TRUE;
}

const void *gLastBlock; // the buffer the last query was handed (to see whether a failed load freed it)

BOOL WINAPI FakeQuery(const void *block, const char *subBlock, void **value, UINT *length)
{
    gLastBlock = block;
    Log("VerQueryValueA(first bytes %s, %s)", Hex(block, gS.size < 4 ? gS.size : 4).c_str(), subBlock); // the fake wrote gS.size bytes only
    if (!std::strcmp(subBlock, "\\VarFileInfo\\Translation")) {
        if (!gS.translationOk) return FALSE;
        gTranslation[0] = gS.translation[0];
        gTranslation[1] = gS.translation[1];
        *value = gTranslation;
        *length = 4;
        return TRUE;
    }
    const char *name = std::strrchr(subBlock, '\\');
    auto it = gS.values.find(name ? name + 1 : "");
    if (it == gS.values.end() || !it->second.first) return FALSE;
    gValueText = it->second.second;
    *value = const_cast<char *>(gValueText.c_str());
    *length = static_cast<UINT>(gValueText.size() + 1);
    return TRUE;
}

int __cdecl FakeWsprintf(char *buffer, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int n = std::vsprintf(buffer, format, args);
    va_end(args);
    return n;
}

BOOL WINAPI FakeMemoryStatus(MEMORYSTATUSEX *status)
{
    Log("GlobalMemoryStatusEx(length %u)", status->dwLength);
    std::memset(status, 0x3c, sizeof(*status));
    status->dwLength = 0x20;
    status->ullTotalPhys = gS.totalBytes;
    status->ullAvailPhys = gS.availableBytes;
    return TRUE;
}

ULONGLONG WINAPI FakeSetCondition(ULONGLONG mask, DWORD typeMask, BYTE condition)
{
    Log("VerSetConditionMask(%llx, %x, %u)", mask, typeMask, condition);
    return mask * 31 + typeMask * 7 + condition + 1;
}

BOOL WINAPI FakeVerify(OSVERSIONINFOEXW *info, DWORD typeMask, ULONGLONG mask)
{
    // Field by field (the log line is cut when it is too long); the CSD string through a checksum.
    uint32_t h = 0x811c9dc5;
    for (size_t i = 0; i < sizeof(info->szCSDVersion); ++i) h = (h ^ reinterpret_cast<const uint8_t *>(info->szCSDVersion)[i]) * 16777619u;
    Log("VerifyVersionInfoW(size %x major %x minor %x build %x platform %x csd %08x spmajor %x spminor %x suite %x product %x reserved %x, %x, %llx)",
        info->dwOSVersionInfoSize, info->dwMajorVersion, info->dwMinorVersion, info->dwBuildNumber, info->dwPlatformId, h, info->wServicePackMajor,
        info->wServicePackMinor, info->wSuiteMask, info->wProductType, info->wReserved, typeMask, mask);
    return gS.versionOk;
}

// GetModulePath finds this in the game's cache and calls it.
DWORD WINAPI FakeModuleFileNameW(void *module, wchar_t *buffer, DWORD size)
{
    Log("GetModuleFileNameW(%p, %u)", module, size);
    std::wcscpy(buffer, gS.modulePath.c_str());
    return static_cast<DWORD>(gS.modulePath.size());
}

HMODULE WINAPI FakeGetModuleHandle(const char *name)
{
    Log("GetModuleHandleA(%s)", name);
    return reinterpret_cast<HMODULE>(0x400000);
}

struct Env {
    explicit Env(difftest::Context &c) : ctx(c), slots(c)
    {
        slots.Set(kIatVerSetConditionMask, reinterpret_cast<void *>(&FakeSetCondition));
        slots.Set(kIatGlobalMemoryStatusEx, reinterpret_cast<void *>(&FakeMemoryStatus));
        slots.Set(kIatVerifyVersionInfoW, reinterpret_cast<void *>(&FakeVerify));
        slots.Set(kIatWsprintfA, reinterpret_cast<void *>(&FakeWsprintf));
        slots.Set(kIatGetFileVersionInfoA, reinterpret_cast<void *>(&FakeVersionInfo));
        slots.Set(kIatVerQueryValueA, reinterpret_cast<void *>(&FakeQuery));
        slots.Set(kIatGetFileVersionInfoSizeA, reinterpret_cast<void *>(&FakeVersionSize));
        slots.Set(kIatGetModuleHandleA, reinterpret_cast<void *>(&FakeGetModuleHandle));
        resolved = ctx.image.At<uint8_t>(kModuleResolved);
        cached = ctx.image.At<void *>(kModulePointer);
        savedResolved = *resolved;
        savedCached = *cached;
        total = ctx.image.At<uint32_t>(kMemoryTotal);
        available = ctx.image.At<uint32_t>(kMemoryAvailable);
        savedTotal = *total;
        savedAvailable = *available;
        codePage = ctx.image.At<int32_t>(kSystemCodePageCache);
        savedCodePage = *codePage;
        *resolved = 1;
        *cached = reinterpret_cast<void *>(&FakeModuleFileNameW);
    }
    ~Env()
    {
        *resolved = savedResolved;
        *cached = savedCached;
        *total = savedTotal;
        *available = savedAvailable;
        *codePage = savedCodePage;
    }
    void Reset(bool overrideMemory = false, uint32_t t = 0, uint32_t a = 0)
    {
        *codePage = -3;
        gLastBlock = nullptr;
        *total = overrideMemory ? t : 0;
        *available = overrideMemory ? a : 0;
        gLog.clear();
    }

    difftest::Context &ctx;
    Slots slots;
    uint8_t *resolved;
    void **cached;
    uint32_t *total, *available;
    int32_t *codePage;
    uint8_t savedResolved;
    void *savedCached;
    uint32_t savedTotal, savedAvailable;
    int32_t savedCodePage;
};

// A cRZString in raw memory (original vtable).
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
};

// The object as raw memory; strings are compared by content (heap pointers differ).
struct Version {
    alignas(8) uint8_t raw[appbase::cRZVersion::kSize + 8];
    Version() { std::memset(raw, 0x5d, sizeof(raw)); }
    appbase::cRZVersion &V() { return *reinterpret_cast<appbase::cRZVersion *>(raw); }
};

std::string Describe(difftest::Context &ctx, appbase::cRZVersion &v, bool stringLive = true)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), "vt %d pathvt %d u24 %08x data %d translation %s", v.mVTable == ctx.image.At(appbase::cRZVersion::kVTable),
                  v.mPath.mVTable == ctx.image.At(cRZString::kVTable), v.mUnused24, v.mData != nullptr,
                  v.mTranslation == reinterpret_cast<uint16_t *>(gTranslation) ? "block" : Hex(&v.mTranslation, 4).c_str());
    std::string out = buf;
    if (stringLive) out += " path [" + v.mPath.mString + "]";
    if (v.mData && gS.infoOk) out += " bytes " + Hex(v.mData, gS.size < 16 ? gS.size : 16); // only what the fake wrote (a failed GetFileVersionInfoA leaves the new[] block uninitialised)
    return out;
}

void FreeData(appbase::cRZVersion &v)
{
    if (v.mData) {
        delete[] v.mData;
        v.mData = nullptr;
    }
}

const wchar_t *const kPaths[] = {L"C:\\Games\\The Sims 2\\EP9\\TSBin\\Sims2EP9.exe", L"x.exe", L"", L"C:\\Spiel\\Sims \u00e4\u00f6 2\\Sims2EP9.exe",
                                 L"C:\\a very long directory name that needs the heap, more than fifteen characters\\game.exe"};

} // namespace

// ---- constructors and destructors ----

DIFF_TEST(ReportFinish_VersionLifetime, kConstruct)
{
    Env env(ctx);
    std::mt19937 rng(0x1c98d6);
    auto construct = ctx.Original<void *(__thiscall *)(void *)>(kConstruct);
    auto constructPath = ctx.Original<void *(__thiscall *)(void *, const void *)>(kConstructPath);
    auto destruct = ctx.Original<void(__thiscall *)(void *)>(kDestruct);
    auto deleting = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kDeleting);
    static const char *const kTexts[] = {"", "C:\\x.dll", "C:\\a long path to a module that does not fit in the small string buffer.dll"};
    for (int run = 0; run < 400; ++run) {
        gS = Script();
        gS.modulePath = kPaths[rng() % 5];
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 local(run * 7919u + 1);
            (void)local;
            env.Reset();
            Version v;
            std::string summary;
            switch (run % 4) {
            case 0: { // default constructor, then destructor
                void *r = side == 0 ? construct(v.raw) : v.V().Construct();
                summary = std::string(r == v.raw ? "this " : "other ") + Describe(ctx, v.V()) + "|" + Joined();
                if (side == 0) destruct(v.raw);
                else v.V().Destruct();
                summary += " after: " + Describe(ctx, v.V(), false);
                break;
            }
            case 1: { // constructor from a path
                Str path(kTexts[local() % 3]);
                void *r = side == 0 ? constructPath(v.raw, &path.S()) : v.V().ConstructFromPath(&path.S());
                summary = std::string(r == v.raw ? "this " : "other ") + Describe(ctx, v.V()) + "|" + Joined();
                if (side == 0) destruct(v.raw);
                else v.V().Destruct();
                break;
            }
            case 2: { // destructor frees the data
                void *r = side == 0 ? construct(v.raw) : v.V().Construct();
                (void)r;
                v.V().mData = new char[32];
                void *old = v.V().mData;
                difftest::FreeRecorder rec;
                if (side == 0) destruct(v.raw);
                else v.V().Destruct();
                rec.Stop();
                const bool oldFreed = rec.Freed(old);
                summary = "after: " + Describe(ctx, v.V(), false) + " freed " + std::to_string(oldFreed);
                break;
            }
            default: { // deleting destructor, with and without the delete bit
                uint32_t flags = (run / 4) % 4;
                void *p = operator new(appbase::cRZVersion::kSize);
                std::memset(p, 0x5d, appbase::cRZVersion::kSize);
                if (side == 0) construct(p);
                else static_cast<appbase::cRZVersion *>(p)->Construct();
                static_cast<appbase::cRZVersion *>(p)->mData = local() % 2 ? new char[8] : nullptr;
                void *old = static_cast<appbase::cRZVersion *>(p)->mData;
                difftest::FreeRecorder rec;
                void *r = side == 0 ? deleting(p, flags) : static_cast<appbase::cRZVersion *>(p)->DeletingDestructor(flags);
                rec.Stop();
                const bool selfFreed = rec.Freed(p);
                const int dataFreed = old ? static_cast<int>(rec.Freed(old)) : -1;
                summary = std::string(r == p ? "this" : "other") + " flags " + std::to_string(flags) + " freed " + std::to_string(selfFreed) + " data freed " +
                          std::to_string(dataFreed);
                if (!(flags & 1)) operator delete(p);
                break;
            }
            }
            results[side] = summary;
        }
        ExpectSame(ctx, ("version lifetime run " + std::to_string(run)).c_str(), results[0], results[1]);
    }
}

// ---- Load ----

DIFF_TEST(ReportFinish_VersionLoad, kLoad)
{
    Env env(ctx);
    std::mt19937 rng(0x1c9da3);
    auto original = ctx.Original<void(__thiscall *)(void *)>(kLoad);
    static const char *const kTexts[] = {"C:\\Games\\Sims2EP9.exe", "", "C:\\a long path to a module that does not fit in the small string buffer.dll", "C:\\caf\xe9.exe"};
    for (int run = 0; run < 500; ++run) {
        gS = Script();
        gS.size = rng() % 6 == 0 ? 0 : 1 + rng() % 300;
        gS.infoOk = rng() % 5 != 0;
        gS.translationOk = rng() % 4 != 0;
        gS.translation[0] = static_cast<uint16_t>(rng());
        gS.translation[1] = static_cast<uint16_t>(rng());
        gS.handle = rng();
        const bool preloaded = run % 7 == 0;
        const char *text = kTexts[rng() % 4];
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 local(run * 7919u + 1);
            (void)local;
            env.Reset();
            std::memset(gTranslation, 0, sizeof(gTranslation));
            Version v;
            Str path(text);
            v.V().ConstructFromPath(&path.S());
            char dummy[8] = {1, 2, 3, 4, 5, 6, 7, 8};
            if (preloaded) v.V().mData = dummy;
            if (side == 0) original(v.raw);
            else v.V().Load();
            results[side] = Describe(ctx, v.V()) + "|" + Joined();
            // Not checked: that a buffer whose translation block is missing is freed again. HeapValidate on the freed
            // block answers differently for a block the original's own operator new[] made (it stays "valid"), so the
            // leak mutant (the delete[] removed from Load) survives; the field compare covers mData being cleared.
            if (v.V().mData != dummy) FreeData(v.V());
            v.V().mData = nullptr;
            v.V().Destruct();
        }
        ExpectSame(ctx, ("version load run " + std::to_string(run)).c_str(), results[0], results[1]);
    }
}

// ---- the string queries ----

DIFF_TEST(ReportFinish_VersionQueries, kGetDescription)
{
    Env env(ctx);
    std::mt19937 rng(0x1c9ad1);
    struct Query {
        uint32_t rva;
        const char *name;
    };
    static const Query kQueries[] = {{kGetDescription, "FileDescription"}, {kGetFileVersion, "FileVersion"}, {kGetDebugVersion, "Debug Version"}};
    static const char *const kValues[] = {"", "1.18.0.187", "The Sims 2", "a value long enough to need the heap, certainly more than fifteen characters"};
    for (int run = 0; run < 900; ++run) {
        const Query &q = kQueries[run % 3];
        gS = Script();
        gS.size = rng() % 8 == 0 ? 0 : 1 + rng() % 200;
        gS.translationOk = rng() % 5 != 0;
        gS.translation[0] = static_cast<uint16_t>(rng() % 3 ? 0x0409 : rng());
        gS.translation[1] = static_cast<uint16_t>(rng() % 3 ? 0x04b0 : rng());
        for (const Query &other : kQueries) gS.values[other.name] = {rng() % 4 != 0, kValues[rng() % 4]};
        const bool preloaded = (run / 3) % 3 == 0;
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 local(run * 7919u + 1);
            (void)local;
            env.Reset();
            std::memset(gTranslation, 0, sizeof(gTranslation));
            Version v;
            Str path("C:\\Games\\Sims2EP9.exe");
            v.V().ConstructFromPath(&path.S());
            char loaded[16];
            for (int i = 0; i < 16; ++i) loaded[i] = static_cast<char>(0x40 + i);
            if (preloaded) {
                v.V().mData = loaded;
                v.V().mTranslation = gTranslation;
                gTranslation[0] = gS.translation[0];
                gTranslation[1] = gS.translation[1];
            }
            Str out(local() % 2 ? "previous text" : "");
            uint32_t r = side == 0 ? ctx.Original<uint32_t(__thiscall *)(void *, void *)>(q.rva)(v.raw, &out.S())
                                   : (q.rva == kGetDescription ? v.V().GetDescription(&out.S()) : q.rva == kGetFileVersion ? v.V().GetFileVersion(&out.S()) : v.V().GetDebugVersion(&out.S()));
            results[side] = "al " + std::to_string(r & 0xff) + " out [" + out.S().mString + "] " + Describe(ctx, v.V()) + "|" + Joined();
            if (v.V().mData != loaded) FreeData(v.V());
            v.V().mData = nullptr;
            v.V().Destruct();
        }
        ExpectSame(ctx, (std::string("version query ") + q.name + " run " + std::to_string(run)).c_str(), results[0], results[1]);
    }
}

// ---- 0x1c9b58, 0x1c9c01, 0x1c9d0b and 0x1c9c6b ----

DIFF_TEST(ReportFinish_VersionSmall, kGetPath)
{
    Env env(ctx);
    std::mt19937 rng(0x1c9b58);
    static const char *const kTexts[] = {"", "C:\\x.dll", "C:\\a long path to a module that does not fit in the small string buffer.dll"};
    auto getPath = ctx.Original<uint32_t(__thiscall *)(void *, void *)>(kGetPath);
    for (int run = 0; run < 200; ++run) {
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 local(run * 7919u + 1);
            (void)local;
            env.Reset();
            Version v;
            Str path(kTexts[local() % 3]);
            v.V().ConstructFromPath(&path.S());
            Str out(kTexts[local() % 3]);
            cRZString *target = run % 10 == 0 ? &v.V().mPath : &out.S(); // assigning the path to itself
            uint32_t r = side == 0 ? getPath(v.raw, target) : v.V().GetPath(target);
            results[side] = "al " + std::to_string(r & 0xff) + " out [" + target->mString + "] path [" + v.V().mPath.mString + "]";
            v.V().Destruct();
        }
        ExpectSame(ctx, "get path", results[0], results[1]);
    }
}

DIFF_TEST(ReportFinish_VersionMemory, kGetMemory)
{
    Env env(ctx);
    std::mt19937 rng(0x1c9c01);
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t *, uint32_t *)>(kGetMemory);
    static const uint64_t kBytes[] = {0, 1, 0xfffff, 0x100000, 0x100001, 0x300000, 0x400000, 0x500000, 0x7ffffffull, 0x80000000ull, 0xffffffffull, 0x100000000ull, 0x2f0000000ull, 0x3ff00000000ull};
    for (int run = 0; run < 600; ++run) {
        gS = Script();
        gS.totalBytes = rng() % 3 ? kBytes[rng() % 14] : (static_cast<uint64_t>(rng()) << 12) ^ rng();
        gS.availableBytes = rng() % 3 ? kBytes[rng() % 14] : (static_cast<uint64_t>(rng()) << 12) ^ rng();
        const bool overrideValues = run % 5 == 0;
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 local(run * 7919u + 1);
            (void)local;
            env.Reset(overrideValues, 1 + local() % 5000, local() % 5000);
            uint32_t a = 0x11111111, b = 0x22222222;
            Version v;
            uint32_t r = side == 0 ? original(v.raw, &a, &b) : v.V().GetMemory(&a, &b);
            results[side] = "al " + std::to_string(r & 0xff) + " total " + std::to_string(a) + " available " + std::to_string(b) + "|" + Joined();

        }
        ExpectSame(ctx, "memory", results[0], results[1]);
    }
}

DIFF_TEST(ReportFinish_VerifyWindowsVersion, kVerify)
{
    Env env(ctx);
    std::mt19937 rng(0x1c9d0b);
    auto original = ctx.Original<uint32_t(__cdecl *)(uint32_t, uint32_t, uint32_t)>(kVerify);
    for (int run = 0; run < 300; ++run) {
        gS = Script();
        gS.versionOk = rng() % 2;
        uint32_t major = rng() % 3 ? rng() % 12 : rng(), minor = rng() % 3 ? rng() % 4 : rng(), sp = rng() % 3 ? rng() % 5 : rng();
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 local(run * 7919u + 1);
            (void)local;
            env.Reset();
            uint32_t r = side == 0 ? original(major, minor, sp) : appbase::VerifyWindowsVersion(major, minor, sp);
            results[side] = "al " + std::to_string(r & 0xff) + "|" + Joined();
        }
        ExpectSame(ctx, "verify version", results[0], results[1]);
    }
}

namespace {
// A string whose Sprintf slot (31) records its arguments.
void *gRecordingVt[40];
void __cdecl RecordSprintf(void *self, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    uint32_t a = va_arg(args, uint32_t);
    uint32_t b = va_arg(args, uint32_t);
    va_end(args);
    Log("Sprintf(%s, %u, %u)", format, a, b);
}
} // namespace

DIFF_TEST(ReportFinish_VersionSystem, kGetSystemVersion)
{
    Env env(ctx);
    std::memcpy(gRecordingVt, ctx.image.At<void>(cRZString::kVTable), sizeof(gRecordingVt));
    gRecordingVt[31] = reinterpret_cast<void *>(&RecordSprintf);
    std::mt19937 rng(0x1c9c6b);
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, void *)>(kGetSystemVersion);
    for (int run = 0; run < 100; ++run) {
        gS = Script();
        gS.versionOk = run % 2;
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 local(run * 7919u + 1);
            (void)local;
            env.Reset();
            Version v;
            Str out("x");
            out.S().mVTable = gRecordingVt;
            uint32_t r = side == 0 ? original(v.raw, &out.S()) : v.V().GetSystemVersion(out.S().AsInterface());
            out.S().mVTable = ctx.image.At(cRZString::kVTable);
            results[side] = "al " + std::to_string(r & 0xff) + "|" + Joined();
        }
        ExpectSame(ctx, "system version", results[0], results[1]);

    }
}

// ---- 0x1bb94c ----

DIFF_TEST(ReportFinish_WriteSystemInfo, kWriteSystemInfo)
{
    Env env(ctx);
    std::mt19937 rng(0x1bb94c);
    auto original = ctx.Original<void(__stdcall *)(void *)>(kWriteSystemInfo);
    static const char *const kValues[] = {"", "1.18.0.187", "The Sims 2", "a value long enough to need the heap, certainly more than fifteen characters"};
    static const char *const kNames[] = {"FileDescription", "FileVersion", "Debug Version"};
    for (int run = 0; run < 600; ++run) {
        gS = Script();
        gS.modulePath = kPaths[rng() % 5];
        gS.size = rng() % 6 == 0 ? 0 : 1 + rng() % 200;
        gS.translationOk = rng() % 6 != 0;
        gS.versionOk = rng() % 2;
        gS.totalBytes = static_cast<uint64_t>(rng()) << (rng() % 12);
        gS.availableBytes = static_cast<uint64_t>(rng()) << (rng() % 12);
        for (const char *name : kNames) gS.values[name] = {rng() % 4 != 0, kValues[rng() % 4]};
        const bool overrideValues = run % 9 == 0;
        const uint32_t overrideTotal = 1 + rng() % 5000, overrideAvailable = rng() % 5000;
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 local(run * 7919u + 1);
            (void)local;
            env.Reset(overrideValues, overrideTotal, overrideAvailable);
            std::memset(gTranslation, 0, sizeof(gTranslation));
            Str out(run % 3 == 0 ? "" : "text already in the output string, long enough to be on the heap\r\n");
            if (side == 0) original(&out.S());
            else appbase::WriteSystemInfo(&out.S());
            results[side] = "[" + out.S().mString + "]|" + Joined();
        }
        ExpectSame(ctx, ("system info run " + std::to_string(run)).c_str(), results[0], results[1]);
    }
}
