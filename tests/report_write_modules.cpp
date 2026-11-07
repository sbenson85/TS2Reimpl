// Differential tests for src/app/ReportWrite.*: the module list (ModuleInfo 0x1baabe / 0x1ba8df,
// the Toolhelp and PSAPI enumerations 0x1bb2b6 / 0x1bb108, the symbol loading 0x1bb026 / 0x1bcf4a)
// and the "Loaded Modules" section (0x1bcdd4).
//
// The DbgHelp table in the image (0x10cb000) points at scripted fakes (CreateToolhelp32Snapshot,
// Module32First/Next, EnumProcessModules, GetModuleInformation, GetModuleFileNameExA,
// GetModuleBaseNameA, SymLoadModule, SymSetOptions); its done flag is set so that Load answers at
// once (the failing Load is made with fake LoadLibraryA / GetProcAddress). The report file is an
// object whose Write slot records. Both sides run on the same script; results, the module vector's
// contents and the log of every call are compared.
//
// Not tested: a failing CreateToolhelp32Snapshot through 0x1bb108 and the functions above it (the
// original answers with its caller's ebx there, so the flow is undefined), and a failing
// GetModuleInformation for the first module (the original keeps stack garbage; ours zero).
#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#include "app/ReportWrite.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "rz/cRZTextFile.h"

#include <windows.h>
#include <tlhelp32.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kDestruct = 0x1baabe, kCopy = 0x1ba8df, kToolhelp = 0x1bb2b6, kEnumerate = 0x1bb108, kLoadSymbols = 0x1bb026, kLoadOnce = 0x1bcf4a;
constexpr uint32_t kWriteModules = 0x1bcdd4;
constexpr uint32_t kIatCloseHandle = 0xd9a17c, kIatLoadLibraryA = 0xd9a128, kIatGetProcAddress = 0xd9a124, kIatGetModuleFileNameA = 0xd9a16c;
constexpr uint32_t kSymbolsLoadedFlag = 0x10caffd;

std::vector<std::string> gLog;

void Log(const char *fmt, ...)
{
    char buf[600];
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

uint32_t Fnv(const void *data, size_t n)
{
    uint32_t h = 0x811c9dc5;
    for (size_t i = 0; i < n; ++i) h = (h ^ static_cast<const uint8_t *>(data)[i]) * 16777619u;
    return h;
}

void ExpectSame(difftest::Context &ctx, const char *what, const std::string &a, const std::string &b)
{
    ++ctx.cases;
    if (a != b) {
        size_t at = 0;
        while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
        size_t from = at > 60 ? at - 60 : 0;
        ctx.Fail("%s: differ at %zu\n o: ..%.150s\n u: ..%.150s", what, at, a.c_str() + from, b.c_str() + from);
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

struct Mod {
    std::string path, name;
    uint32_t base, size;
};

struct Script {
    std::vector<Mod> toolhelp;   // what the Toolhelp snapshot lists
    std::vector<Mod> psapi;      // what EnumProcessModules lists
    bool hasSnapshot = true, hasFirst = true, hasNext = true;
    bool snapshotFails = false;
    bool hasEnum = true, hasFileName = true, hasBaseName = true, hasInfo = true, hasSymLoad = true;
    bool enumFails = false;
    int64_t needed = -1;         // EnumProcessModules' byte count when not the list's
    uint32_t infoFailMask = 0;   // bit i: GetModuleInformation fails for the module i (never the first)
    uint32_t nameFailMask = 0;   // bit i: GetModuleFileNameExA / GetModuleBaseNameA write nothing for the module i
    bool loadFails = false;      // the table cannot be loaded
} gS;

int gToolhelpIndex, gInfoCalls;
const uint32_t kProcess = 0x4321, kProcessId = 0xbeef;

void *WINAPI FakeSnapshot(DWORD flags, DWORD pid)
{
    Log("Snapshot(%x,%x)", flags, pid);
    return gS.snapshotFails ? INVALID_HANDLE_VALUE : reinterpret_cast<void *>(0x5150);
}

BOOL FillEntry(MODULEENTRY32 *e)
{
    if (gToolhelpIndex >= static_cast<int>(gS.toolhelp.size())) return FALSE;
    const Mod &m = gS.toolhelp[gToolhelpIndex++];
    std::strcpy(e->szModule, m.name.c_str());
    std::strcpy(e->szExePath, m.path.c_str());
    e->modBaseAddr = reinterpret_cast<BYTE *>(static_cast<uintptr_t>(m.base));
    e->modBaseSize = m.size;
    e->th32ModuleID = 0x11111111; // fields the code does not read
    e->hModule = reinterpret_cast<HMODULE>(0x22222222);
    return TRUE;
}

BOOL WINAPI FakeFirst(void *snapshot, MODULEENTRY32 *e)
{
    Log("First(%p,%u,%08x)", snapshot, e->dwSize, Fnv(e, sizeof(*e)));
    gToolhelpIndex = 0;
    return FillEntry(e);
}

BOOL WINAPI FakeNext(void *snapshot, MODULEENTRY32 *e)
{
    Log("Next(%p,%u,%08x)", snapshot, e->dwSize, Fnv(e, sizeof(*e)));
    return FillEntry(e);
}

BOOL WINAPI FakeCloseHandle(void *handle)
{
    Log("Close(%p)", handle);
    return TRUE;
}

const Mod &ModAt(size_t index)
{
    static const Mod empty{"", "", 0, 0};
    return index < gS.psapi.size() ? gS.psapi[index] : empty;
}

BOOL WINAPI FakeEnum(void *process, void **handles, DWORD size, DWORD *needed)
{
    Log("Enum(%p,%u)", process, size);
    if (gS.enumFails) return FALSE;
    for (size_t i = 0; i < 0x4000; ++i) handles[i] = reinterpret_cast<void *>(0x7000 + i); // (the list, then more for a count that is too large)
    *needed = gS.needed >= 0 ? static_cast<DWORD>(gS.needed) : static_cast<DWORD>(gS.psapi.size() * 4);
    return TRUE;
}

struct ModuleInformation {
    void *base;
    DWORD size;
    void *entry;
};

BOOL WINAPI FakeInfo(void *process, void *module, ModuleInformation *info, DWORD size)
{
    const size_t index = reinterpret_cast<uintptr_t>(module) - 0x7000;
    int call = gInfoCalls++;
    Log("Info(%p,%p,%u)", process, module, size);
    if (call > 0 && index < 32 && (gS.infoFailMask >> index & 1)) return FALSE;
    info->base = reinterpret_cast<void *>(static_cast<uintptr_t>(ModAt(index).base));
    info->size = ModAt(index).size;
    info->entry = reinterpret_cast<void *>(0x99);
    return TRUE;
}

DWORD WINAPI FakeFileName(void *process, void *module, char *buffer, DWORD size)
{
    const size_t index = reinterpret_cast<uintptr_t>(module) - 0x7000;
    Log("FileName(%p,%p,b0=%d,%u)", process, module, buffer[0], size);
    if (index < 32 && (gS.nameFailMask >> index & 1)) return 0;
    std::strcpy(buffer, ModAt(index).path.c_str());
    return static_cast<DWORD>(ModAt(index).path.size());
}

DWORD WINAPI FakeBaseName(void *process, void *module, char *buffer, DWORD size)
{
    const size_t index = reinterpret_cast<uintptr_t>(module) - 0x7000;
    Log("BaseName(%p,%p,b0=%d,%u)", process, module, buffer[0], size);
    if (index < 32 && (gS.nameFailMask >> index & 1)) return 0;
    std::strcpy(buffer, ModAt(index).name.c_str());
    return static_cast<DWORD>(ModAt(index).name.size());
}

DWORD WINAPI FakeSymLoad(void *process, void *file, const char *image, const char *name, DWORD base, DWORD size)
{
    // The byte after the terminator too: the code appends one more NUL to its copies.
    Log("SymLoad(%p,%p,[%s]%02x,[%s]%02x,%x,%u)", process, file, image, static_cast<uint8_t>(image[std::strlen(image) + 1]), name, static_cast<uint8_t>(name[std::strlen(name) + 1]), base, size);
    return base;
}

DWORD WINAPI FakeSymOptions(DWORD options)
{
    Log("SymOptions(%x)", options);
    return options;
}

// For the failing Load: every library loads and no entry point is found.
void *WINAPI FakeLoadLibrary(const char *name)
{
    Log("LoadLibraryA(%s)", name);
    return reinterpret_cast<void *>(0x1000);
}
void *WINAPI FakeGetProcAddress(void *, const char *name)
{
    Log("GetProcAddress(%s)", name);
    return nullptr;
}
DWORD WINAPI FakeModuleFileName(void *, char *, DWORD)
{
    return 0;
}

void *gFileVt[40];
uint8_t __fastcall FakeWrite(void *, void *, const void *data, uint32_t length)
{
    Log("Write(%s)", std::string(static_cast<const char *>(data), length).c_str());
    return 1;
}

// The table: fakes where the script has the entry point, null otherwise.
void SetTable(difftest::Context &ctx)
{
    uint8_t *table = ctx.image.At<uint8_t>(appbase::DbgHelpTable::kGlobal);
    std::memset(table, 0, 0x74);
    auto set = [&](uint32_t offset, void *value) { std::memcpy(table + offset, &value, 4); };
    *reinterpret_cast<uint32_t *>(table + 0) = kProcessId;
    *reinterpret_cast<uint32_t *>(table + 4) = kProcess;
    set(0x28, reinterpret_cast<void *>(&FakeSymOptions));
    if (gS.hasSymLoad) set(0x40, reinterpret_cast<void *>(&FakeSymLoad));
    if (gS.hasSnapshot) set(0x50, reinterpret_cast<void *>(&FakeSnapshot));
    if (gS.hasFirst) set(0x54, reinterpret_cast<void *>(&FakeFirst));
    if (gS.hasNext) set(0x58, reinterpret_cast<void *>(&FakeNext));
    if (gS.hasEnum) set(0x64, reinterpret_cast<void *>(&FakeEnum));
    if (gS.hasFileName) set(0x68, reinterpret_cast<void *>(&FakeFileName));
    if (gS.hasBaseName) set(0x6c, reinterpret_cast<void *>(&FakeBaseName));
    if (gS.hasInfo) set(0x70, reinterpret_cast<void *>(&FakeInfo));
    *ctx.image.At<uint8_t>(appbase::DbgHelpTable::kDoneFlag) = gS.loadFails ? 0 : 1;
}

// Keeps the image's table, flags and import slots for the life of the guard.
struct Fixture {
    difftest::Context &ctx;
    Slots slots;
    uint8_t savedTable[0x74];
    uint8_t savedDone, savedLoaded;
    explicit Fixture(difftest::Context &c) : ctx(c), slots(c)
    {
        slots.Set(kIatCloseHandle, reinterpret_cast<void *>(&FakeCloseHandle));
        slots.Set(kIatLoadLibraryA, reinterpret_cast<void *>(&FakeLoadLibrary));
        slots.Set(kIatGetProcAddress, reinterpret_cast<void *>(&FakeGetProcAddress));
        slots.Set(kIatGetModuleFileNameA, reinterpret_cast<void *>(&FakeModuleFileName));
        std::memcpy(savedTable, ctx.image.At<void>(appbase::DbgHelpTable::kGlobal), sizeof(savedTable));
        savedDone = *ctx.image.At<uint8_t>(appbase::DbgHelpTable::kDoneFlag);
        savedLoaded = *ctx.image.At<uint8_t>(kSymbolsLoadedFlag);
        std::memcpy(gFileVt, ctx.image.At<void>(cRZTextFile::kVTable), sizeof(gFileVt));
        gFileVt[16] = reinterpret_cast<void *>(&FakeWrite);
    }
    ~Fixture()
    {
        std::memcpy(ctx.image.At<void>(appbase::DbgHelpTable::kGlobal), savedTable, sizeof(savedTable));
        *ctx.image.At<uint8_t>(appbase::DbgHelpTable::kDoneFlag) = savedDone;
        *ctx.image.At<uint8_t>(kSymbolsLoadedFlag) = savedLoaded;
    }
};

// ---- generators ----

std::string RandomText(std::mt19937 &rng, size_t maxLength)
{
    static const char *const kAlphabet = "abcdefghijklmnopqrstuvwxyz0123456789_.\\:- ";
    static const size_t kLengths[] = {0, 1, 5, 14, 15, 16, 17, 31, 40, 120, 259};
    size_t length = kLengths[rng() % 11];
    if (length > maxLength) length = maxLength;
    std::string s;
    for (size_t i = 0; i < length; ++i) s += kAlphabet[rng() % 42];
    return s;
}

std::vector<Mod> RandomMods(std::mt19937 &rng, size_t maxCount)
{
    std::vector<Mod> mods;
    size_t n = rng() % (maxCount + 1);
    for (size_t i = 0; i < n; ++i) mods.push_back({RandomText(rng, 259), RandomText(rng, 255), static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng())});
    return mods;
}

// A string as the code sees it: vtable (the game's or not), reference count, size, capacity, text; for a
// small string also the whole buffer (the bytes after the text are what the copy left there).
std::string Describe(const cRZString &s)
{
    char head[80];
    std::snprintf(head, sizeof(head), "vt%d rc%u %zu/%zu", s.mVTable == GameAddress<const void>(cRZString::kVTable), s.mRefCount, s.mString.size(), s.mString.capacity());
    std::string out = std::string(head) + " [" + s.mString + "]";
    if (s.mString.capacity() <= 15) out += " buf " + Hex(reinterpret_cast<const uint8_t *>(&s) + 4, 16);
    return out;
}

std::string Describe(const appbase::ModuleInfo &m)
{
    char tail[40];
    std::snprintf(tail, sizeof(tail), " %08x %u", m.base, m.size);
    return "{" + Describe(m.path) + " / " + Describe(m.name) + tail + "}";
}

std::string Describe(const std::vector<appbase::ModuleInfo> &v)
{
    std::string out = std::to_string(v.size()) + "/" + std::to_string(v.capacity()) + ":";
    for (const appbase::ModuleInfo &m : v) out += Describe(m);
    return out;
}

appbase::ModuleInfo RandomInfo(std::mt19937 &rng)
{
    appbase::ModuleInfo m;
    m.path.mString.assign(RandomText(rng, 259));
    m.name.mString.assign(RandomText(rng, 255));
    if (rng() % 4 == 0) m.name.mString.reserve(rng() % 80);
    m.base = static_cast<uint32_t>(rng());
    m.size = static_cast<uint32_t>(rng());
    return m;
}

std::vector<appbase::ModuleInfo> RandomVector(std::mt19937 &rng, size_t maxCount)
{
    std::vector<appbase::ModuleInfo> v;
    const size_t n = rng() % (maxCount + 1);
    for (size_t i = 0; i < n; ++i) v.push_back(RandomInfo(rng));
    return v;
}

void RandomScript(std::mt19937 &rng, bool allowSnapshotFailure)
{
    gS = Script();
    gS.toolhelp = RandomMods(rng, 6);
    gS.psapi = RandomMods(rng, 5);
    // Mostly everything present; each entry point missing now and then.
    gS.hasSnapshot = rng() % 12 != 0;
    gS.hasFirst = rng() % 12 != 0;
    gS.hasNext = rng() % 12 != 0;
    gS.hasEnum = rng() % 8 != 0;
    gS.hasFileName = rng() % 8 != 0;
    gS.hasBaseName = rng() % 8 != 0;
    gS.hasInfo = rng() % 8 != 0;
    gS.hasSymLoad = rng() % 6 != 0;
    gS.snapshotFails = allowSnapshotFailure && rng() % 10 == 0;
    gS.enumFails = rng() % 8 == 0;
    switch (rng() % 8) {
    case 0: gS.needed = 0; break;
    case 1: gS.needed = 3; break;
    case 2: gS.needed = static_cast<int64_t>(gS.psapi.size() * 4 + 3); break;
    case 3: gS.needed = 0x10001; break;
    case 4: gS.needed = 0xffffffffll; break;
    default: break;
    }
    gS.infoFailMask = static_cast<uint32_t>(rng());
    if (rng() % 2) gS.infoFailMask = 0;
    gS.nameFailMask = rng() % 3 == 0 ? static_cast<uint32_t>(rng()) : 0;
    gS.loadFails = rng() % 15 == 0;
}

// Runs `call(side)` on both sides with the same script and compares what it returns (a string).
template <typename F> void BothSides(difftest::Context &ctx, const char *what, int run, F call)
{
    std::string results[2];
    for (int side = 0; side < 2; ++side) {
        SetTable(ctx);
        gLog.clear();
        gToolhelpIndex = 0;
        gInfoCalls = 0;
        results[side] = call(side);
        results[side] += "\n" + Joined();
    }
    char label[80];
    std::snprintf(label, sizeof(label), "%s run %d", what, run);
    ExpectSame(ctx, label, results[0], results[1]);
}

} // namespace

// ---- ModuleInfo ----

DIFF_TEST(ReportWrite_ModuleInfoCopy, kCopy)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *, const void *)>(kCopy);
    std::mt19937 rng(0x1ba8df);
    for (int run = 0; run < 300; ++run) {
        appbase::ModuleInfo source = RandomInfo(rng);
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            alignas(8) uint8_t raw[sizeof(appbase::ModuleInfo)];
            std::memset(raw, 0x5a, sizeof(raw));
            void *r = side == 0 ? orig(raw, &source) : reinterpret_cast<appbase::ModuleInfo *>(raw)->CopyConstruct(&source);
            auto *copy = reinterpret_cast<appbase::ModuleInfo *>(raw);
            results[side] = std::string(r == raw ? "this " : "other ") + Describe(*copy);
            copy->Destruct();
        }
        ExpectSame(ctx, "module info copy", results[0], results[1]);
    }
}

DIFF_TEST(ReportWrite_ModuleInfoDestruct, kDestruct)
{
    auto orig = ctx.Original<void(__thiscall *)(void *)>(kDestruct);
    std::mt19937 rng(0x1baabe);
    for (int run = 0; run < 300; ++run) {
        appbase::ModuleInfo source = RandomInfo(rng);
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            alignas(8) uint8_t raw[sizeof(appbase::ModuleInfo)];
            auto *copy = reinterpret_cast<appbase::ModuleInfo *>(raw);
            copy->CopyConstruct(&source);
            // Blocks are judged by a FreeRecorder across this side's call; a fixed array holds them.
            const void *blocks[2] = {copy->path.mString.capacity() > 15 ? copy->path.mString.data() : nullptr,
                                     copy->name.mString.capacity() > 15 ? copy->name.mString.data() : nullptr};
            difftest::FreeRecorder rec;
            if (side == 0) orig(raw);
            else copy->Destruct();
            rec.Stop();
            std::string freed;
            for (const void *p : blocks) freed += p ? (!rec.Freed(p) ? "live " : "freed ") : "none ";
            // What is left: vtable, an empty small string (only its first byte is written).
            char left[120];
            std::snprintf(left, sizeof(left), "%d %zu/%zu b%d / %d %zu/%zu b%d", copy->path.mVTable == GameAddress<const void>(cRZString::kVTable), copy->path.mString.size(),
                          copy->path.mString.capacity(), copy->path.mString.c_str()[0], copy->name.mVTable == GameAddress<const void>(cRZString::kVTable),
                          copy->name.mString.size(), copy->name.mString.capacity(), copy->name.mString.c_str()[0]);
            results[side] = freed + left;
        }
        ExpectSame(ctx, "module info destruct", results[0], results[1]);
    }
}

// ---- the enumerations ----

DIFF_TEST(ReportWrite_EnumerateModulesToolhelp, kToolhelp)
{
    Fixture fixture(ctx);
    auto orig = ctx.Original<uint32_t(__cdecl *)(void *, uint32_t)>(kToolhelp);
    std::mt19937 rng(0x1bb2b6);
    for (int run = 0; run < 500; ++run) {
        RandomScript(rng, true);
        const uint32_t seed = static_cast<uint32_t>(rng());
        BothSides(ctx, "toolhelp", run, [&](int side) {
            std::mt19937 fill(seed);
            std::vector<appbase::ModuleInfo> v = RandomVector(fill, 3);
            const uint32_t pid = seed ^ 0x1234;
            uint32_t r = side == 0 ? orig(&v, pid) : appbase::EnumerateModulesToolhelp(&v, pid);
            // A failing snapshot gives the caller's ebx in the original: only the effects count there.
            return std::string("al=") + (gS.snapshotFails && !gS.loadFails && gS.hasSnapshot && gS.hasFirst && gS.hasNext ? "?" : std::to_string(r & 0xff)) + " " + Describe(v);
        });
    }
}

DIFF_TEST(ReportWrite_EnumerateModules, kEnumerate)
{
    Fixture fixture(ctx);
    auto orig = ctx.Original<uint32_t(__cdecl *)(void *, uint32_t, void *)>(kEnumerate);
    std::mt19937 rng(0x1bb108);
    for (int run = 0; run < 900; ++run) {
        RandomScript(rng, false);
        // Half of the runs have an empty Toolhelp list, to reach the PSAPI route.
        if (run % 2) gS.toolhelp.clear();
        if (run % 100 == 37) gS.needed = 0x10000; // exactly the buffer: every handle in it is a module
        const uint32_t seed = static_cast<uint32_t>(rng());
        BothSides(ctx, "enumerate", run, [&](int side) {
            std::mt19937 fill(seed);
            std::vector<appbase::ModuleInfo> v = RandomVector(fill, 3);
            void *process = reinterpret_cast<void *>(static_cast<uintptr_t>(seed | 1));
            uint32_t r = side == 0 ? orig(&v, seed ^ 0x77, process) : appbase::EnumerateModules(&v, seed ^ 0x77, process);
            return "al=" + std::to_string(r & 0xff) + " " + Describe(v);
        });
    }
}

DIFF_TEST(ReportWrite_LoadSymbolModules, kLoadSymbols)
{
    Fixture fixture(ctx);
    auto orig = ctx.Original<void(__cdecl *)(void *, uint32_t, void *)>(kLoadSymbols);
    std::mt19937 rng(0x1bb026);
    for (int run = 0; run < 700; ++run) {
        RandomScript(rng, false);
        if (run % 3 == 0) gS.toolhelp.clear();
        const uint32_t seed = static_cast<uint32_t>(rng());
        BothSides(ctx, "load symbols", run, [&](int side) {
            std::mt19937 fill(seed);
            std::vector<appbase::ModuleInfo> v = RandomVector(fill, 3);
            void *process = reinterpret_cast<void *>(static_cast<uintptr_t>(seed | 1));
            if (side == 0) orig(process, seed ^ 0x55, &v);
            else appbase::LoadSymbolModules(process, seed ^ 0x55, &v);
            return Describe(v);
        });
    }
}

DIFF_TEST(ReportWrite_LoadSymbolModulesOnce, kLoadOnce)
{
    Fixture fixture(ctx);
    auto orig = ctx.Original<uint32_t(__cdecl *)()>(kLoadOnce);
    std::mt19937 rng(0x1bcf4a);
    for (int run = 0; run < 400; ++run) {
        RandomScript(rng, false);
        if (run % 3 == 0) gS.toolhelp.clear();
        const bool already = rng() % 3 == 0;
        BothSides(ctx, "load once", run, [&](int side) {
            *ctx.image.At<uint8_t>(kSymbolsLoadedFlag) = already ? 1 : 0;
            uint32_t r = side == 0 ? orig() : appbase::LoadSymbolModulesOnce();
            return "al=" + std::to_string(r & 0xff) + " flag " + std::to_string(*ctx.image.At<uint8_t>(kSymbolsLoadedFlag));
        });
    }
}

DIFF_TEST(ReportWrite_WriteModules, kWriteModules)
{
    Fixture fixture(ctx);
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(kWriteModules);
    std::mt19937 rng(0x1bcdd4);
    for (int run = 0; run < 700; ++run) {
        RandomScript(rng, false);
        if (run % 3 == 0) gS.toolhelp.clear();
        const uint8_t level3 = rng() % 4 == 0 ? 0 : 1;
        const int32_t charSize = rng() % 4 == 0 ? 2 : 0;
        const uint32_t filler = static_cast<uint32_t>(rng());
        BothSides(ctx, "write modules", run, [&](int side) {
            alignas(8) uint8_t raw[appbase::ExceptionReporterObject::kSize];
            std::memset(raw, static_cast<int>(filler & 0xff), sizeof(raw));
            *reinterpret_cast<void **>(raw + 0xa0) = gFileVt;
            *reinterpret_cast<int32_t *>(raw + 0xa0 + 0x9c) = charSize;
            raw[0x197] = level3;
            uint32_t r = side == 0 ? orig(raw) : appbase::WriteModules(reinterpret_cast<appbase::ExceptionReporterObject *>(raw), nullptr);
            return "al=" + std::to_string(r & 0xff);
        });
    }
}
