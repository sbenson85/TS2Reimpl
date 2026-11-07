// Differential tests for src/app/ReporterDump.*: the module version query (0x1bc0c2), the DbgHelp
// loader (0x1bc8a7) and the minidump writer (0x1bd87a).
//
// Every Windows function they use goes through the import table; the tests point those slots at
// scripted fakes (restored afterwards), so the original never loads the real DbgHelp or writes a real
// dump. The fakes log all arguments (and what they were handed); both sides see the same script and
// their logs, return values, output buffers and the DbgHelp table are compared.
//
// Not tested: both DbgHelp.dll and ImageHlp.dll failing to load. The original then calls the null
// SymInitialize slot and crashes (marked ORIGINAL-BUG in the source).
#define NOMINMAX
#include "app/ReporterDump.h"
#include "difftest.h"
#include "game/game.h"
#include "rz/RZTextConvert.h"
#include "rz/cRZString.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kQueryVersion = 0x1bc0c2, kLoader = 0x1bc8a7, kWriteDump = 0x1bd87a;
constexpr uint32_t kIatGetProcAddress = 0xd9a124, kIatLoadLibraryA = 0xd9a128, kIatGetModuleFileNameA = 0xd9a16c;
constexpr uint32_t kIatCloseHandle = 0xd9a17c, kIatGetCurrentThreadId = 0xd9a194, kIatCreateFileA = 0xd9a1ac;
constexpr uint32_t kIatSetFilePointer = 0xd9a1b0, kIatGetCurrentProcess = 0xd9a1b8, kIatGetCurrentProcessId = 0xd9a1bc;
constexpr uint32_t kIatGetFileVersionInfoA = 0xd9a5e8, kIatVerQueryValueA = 0xd9a5ec, kIatGetFileVersionInfoSizeA = 0xd9a5f0;
constexpr uint32_t kSystemCodePageCache = 0xff23a4;

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
        std::string x = a.size() > 700 ? a.substr(0, 700) : a;
        std::string y = b.size() > 700 ? b.substr(0, 700) : b;
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

// ---- the script the fakes follow ----

struct Script {
    std::set<std::string> failLoad;  // LoadLibraryA names that fail
    std::set<std::string> missing;   // "dll|proc" that GetProcAddress cannot find
    uint32_t moduleFileNameLength = 12; // 0: GetModuleFileNameA fails
    DWORD versionSize = 0x240;       // 0: GetFileVersionInfoSizeA fails
    bool getInfoOk = true;
    bool queryOk = true;
    uint16_t version[8] = {5, 1, 2600, 7, 5, 1, 2600, 8}; // file major, minor, build, revision, then product
    BOOL symInitResult = TRUE;
    void *createFileResult = reinterpret_cast<void *>(0x5555);
    DWORD dumpResult = TRUE;
};
Script gScript;

// Module handles are derived from the (case-insensitive) library name.
std::map<std::string, void *> gModules;
std::string gLastName;

void *HandleFor(const char *name)
{
    std::string key = name;
    for (char &c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto it = gModules.find(key);
    if (it == gModules.end()) it = gModules.emplace(key, reinterpret_cast<void *>(0x10000 + 0x1000 * gModules.size())).first;
    return it->second;
}

std::string NameOf(void *handle)
{
    if (!handle) return "NULL";
    for (auto &kv : gModules) if (kv.second == handle) return kv.first;
    return "?";
}

uint32_t gFixedInfo[13];

BOOL WINAPI FakeSymInit(void *process, const char *path, BOOL invade)
{
    Log("SymInitialize(%p, %p, %d)", process, static_cast<const void *>(path), invade);
    return gScript.symInitResult;
}

BOOL WINAPI FakeDump(void *process, DWORD pid, void *file, DWORD type, void *exception, void *user, void *callback)
{
    Log("MiniDumpWriteDump(%p, %u, %p, %u, user=%p, callback=%p)", process, pid, file, type, user, callback);
    const uint32_t *e = static_cast<const uint32_t *>(exception);
    Log("  exception info: thread %u pointers %08x client %u", e[0], e[1], e[2]);
    return gScript.dumpResult;
}

void *WINAPI FakeLoadLibrary(const char *name)
{
    Log("LoadLibraryA(%s)", name);
    if (gScript.failLoad.count(name)) return nullptr;
    return HandleFor(name);
}

void *WINAPI FakeGetProcAddress(void *module, const char *name)
{
    std::string dll = NameOf(module);
    Log("GetProcAddress(%s, %s)", dll.c_str(), name);
    if (!module || gScript.missing.count(dll + "|" + name)) return nullptr;
    if (!std::strcmp(name, "SymInitialize")) return reinterpret_cast<void *>(&FakeSymInit);
    if (!std::strcmp(name, "MiniDumpWriteDump")) return reinterpret_cast<void *>(&FakeDump);
    uint32_t h = 0x811c9dc5;
    for (char c : dll + "|" + name) h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
    return reinterpret_cast<void *>(0x40000000 + (h & 0xfffff0));
}

DWORD WINAPI FakeGetModuleFileName(void *module, char *buffer, DWORD size)
{
    Log("GetModuleFileNameA(%s, size %u)", NameOf(module).c_str(), size);
    if (!gScript.moduleFileNameLength) return 0;
    int n = std::snprintf(buffer, size, "C:\\fake\\%s.dll", NameOf(module).c_str());
    return static_cast<DWORD>(n);
}

DWORD WINAPI FakeVersionSize(const char *path, DWORD *handle)
{
    Log("GetFileVersionInfoSizeA(%s)", path);
    *handle = 0x77;
    return gScript.versionSize;
}

BOOL WINAPI FakeVersionInfo(const char *path, DWORD handle, DWORD size, void *data)
{
    bool zero = true;
    for (DWORD i = 0; i < size; ++i) if (static_cast<const char *>(data)[i]) zero = false;
    Log("GetFileVersionInfoA(%s, %u, %u) buffer zero %d", path, handle, size, zero);
    if (!gScript.getInfoOk) return FALSE;
    uint8_t *b = static_cast<uint8_t *>(data);
    for (DWORD i = 0; i < size; ++i) b[i] = static_cast<uint8_t>(i * 7 + 3);
    // The "resource" the query hands out lives in a static (the buffer may be smaller than a
    // VS_FIXEDFILEINFO; reading past a heap block would differ per side).
    uint32_t *f = gFixedInfo;
    f[0] = 0xfeef04bd;
    f[1] = 0x10000;
    f[2] = (gScript.version[0] << 16) | gScript.version[1];
    f[3] = (gScript.version[2] << 16) | gScript.version[3];
    f[4] = (gScript.version[4] << 16) | gScript.version[5];
    f[5] = (gScript.version[6] << 16) | gScript.version[7];
    return TRUE;
}

BOOL WINAPI FakeQuery(const void *data, const char *block, void **value, UINT *length)
{
    Log("VerQueryValueA(%s, initial value %p length %u)", block, *value, *length);
    if (!gScript.queryOk) return FALSE;
    *value = gFixedInfo;
    *length = 52;
    return TRUE;
}

DWORD WINAPI FakeProcessId()
{
    Log("GetCurrentProcessId");
    return 0x4242;
}
DWORD_PTR WINAPI FakeProcess()
{
    Log("GetCurrentProcess");
    return 0xfffffff0;
}
DWORD WINAPI FakeThreadId()
{
    Log("GetCurrentThreadId");
    return 0x7777;
}

void *WINAPI FakeCreateFile(const char *name, DWORD access, DWORD share, void *security, DWORD disposition, DWORD flags, void *templateFile)
{
    Log("CreateFileA(%s, %08x, %u, %p, %u, %08x, %p)", name, access, share, security, disposition, flags, templateFile);
    return gScript.createFileResult;
}
DWORD WINAPI FakeSetFilePointer(void *file, LONG distance, LONG *high, DWORD method)
{
    Log("SetFilePointer(%p, %d, %p, %u)", file, distance, static_cast<void *>(high), method);
    return 0;
}
BOOL WINAPI FakeCloseHandle(void *file)
{
    Log("CloseHandle(%p)", file);
    return TRUE;
}

void InstallVersionFakes(Imports &imports)
{
    imports.Set(kIatGetModuleFileNameA, reinterpret_cast<void *>(&FakeGetModuleFileName));
    imports.Set(kIatGetFileVersionInfoSizeA, reinterpret_cast<void *>(&FakeVersionSize));
    imports.Set(kIatGetFileVersionInfoA, reinterpret_cast<void *>(&FakeVersionInfo));
    imports.Set(kIatVerQueryValueA, reinterpret_cast<void *>(&FakeQuery));
}

void InstallLoaderFakes(Imports &imports)
{
    InstallVersionFakes(imports);
    imports.Set(kIatLoadLibraryA, reinterpret_cast<void *>(&FakeLoadLibrary));
    imports.Set(kIatGetProcAddress, reinterpret_cast<void *>(&FakeGetProcAddress));
    imports.Set(kIatGetCurrentProcessId, reinterpret_cast<void *>(&FakeProcessId));
    imports.Set(kIatGetCurrentProcess, reinterpret_cast<void *>(&FakeProcess));
}

const char *const kDlls[] = {"DbgHelp.dll", "ImageHlp.dll", "NTDLL.dll", "kernel32.dll", "tlhelp32.dll", "psapi.dll"};
const char *const kProcs[] = {"SymInitialize", "SymCleanup", "SymSetOptions", "StackWalk", "SymFunctionTableAccess", "SymGetModuleBase", "SymGetSymFromAddr",
                              "SymGetLineFromAddr", "SymLoadModule", "UnDecorateSymbolName", "MiniDumpWriteDump", "NtOpenThread", "CreateToolhelp32Snapshot",
                              "Module32First", "Module32Next", "Thread32First", "Thread32Next", "EnumProcessModules", "GetModuleFileNameExA",
                              "GetModuleBaseNameA", "GetModuleInformation"};

} // namespace

// ---- 0x1bc0c2 ----

DIFF_TEST(ReporterDump_QueryModuleVersion, kQueryVersion)
{
    Imports imports(ctx);
    InstallVersionFakes(imports);
    std::mt19937 rng(0x1bc0c2);
    auto orig = ctx.Original<uint8_t(__cdecl *)(void *, uint32_t *, uint32_t *)>(kQueryVersion);
    for (int run = 0; run < 1500; ++run) {
        gScript = Script();
        gModules.clear();
        void *module = rng() % 5 ? HandleFor(kDlls[rng() % 6]) : reinterpret_cast<void *>(rng());
        if (rng() % 9 == 0) gScript.moduleFileNameLength = 0;
        if (rng() % 9 == 0) gScript.versionSize = 0;
        else if (rng() % 3 == 0) gScript.versionSize = 1 + rng() % 300; // includes buffers too small for the fixed info; the fake then writes none
        else if (rng() % 3 == 0) gScript.versionSize = 68;
        if (rng() % 9 == 0) gScript.getInfoOk = false;
        if (rng() % 9 == 0) gScript.queryOk = false;
        for (auto &v : gScript.version) v = static_cast<uint16_t>(rng() % 4 ? rng() % 12 : rng());
        // A buffer too small for the fixed info leaves it as the fake's pattern bytes; both sides read the same.
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            uint32_t fileVersion[4], productVersion[4];
            std::memset(fileVersion, 0xA5, sizeof(fileVersion));
            std::memset(productVersion, 0x5A, sizeof(productVersion));
            uint8_t r = side == 0 ? orig(module, fileVersion, productVersion) : appbase::QueryModuleVersion(module, fileVersion, productVersion);
            // Only al is the result; the upper bits of eax are leftovers.
            results[side] = "al=" + std::to_string(r) + " " + Hex(fileVersion, 16) + " " + Hex(productVersion, 16) + "\n" + Joined();
        }
        ExpectSame(ctx, "module version", results[0], results[1]);
    }
}

// ---- 0x1bc8a7 ----

namespace {

struct LoaderResult {
    std::string text;
};

std::string RunLoader(difftest::Context &ctx, int side, const uint8_t *garbage)
{
    gLog.clear();
    auto *table = ctx.image.At<uint8_t>(appbase::DbgHelpTable::kGlobal);
    uint8_t *done = ctx.image.At<uint8_t>(appbase::DbgHelpTable::kDoneFlag);
    std::memcpy(table, garbage, 0x74);
    *done = 0;
    uint8_t r = side == 0 ? ctx.Original<uint8_t(__thiscall *)(void *)>(kLoader)(table) : appbase::LoadDbgHelp(reinterpret_cast<appbase::DbgHelpTable *>(table), nullptr);
    return "al=" + std::to_string(r) + " done=" + std::to_string(*done) + " table " + Hex(table, 0x74) + "\n" + Joined();
}

} // namespace

DIFF_TEST(ReporterDump_Loader, kLoader)
{
    Imports imports(ctx);
    InstallLoaderFakes(imports);
    uint8_t *table = ctx.image.At<uint8_t>(appbase::DbgHelpTable::kGlobal);
    uint8_t *done = ctx.image.At<uint8_t>(appbase::DbgHelpTable::kDoneFlag);
    uint8_t savedTable[0x74];
    std::memcpy(savedTable, table, sizeof(savedTable));
    const uint8_t savedDone = *done;

    std::mt19937 rng(0x1bc8a7);
    for (int run = 0; run < 1500; ++run) {
        gScript = Script();
        gModules.clear();
        int mode = run < 40 ? run : static_cast<int>(rng() % 40);
        // Deterministic edge cases first, then random mixtures.
        switch (mode) {
        case 0: break;                                                       // everything works, DbgHelp 5.1
        case 1: gScript.version[0] = 5; gScript.version[1] = 0; break;     // 5.0: too old
        case 2: gScript.version[0] = 4; gScript.version[1] = 9; break;     // 4.x
        case 3: gScript.version[0] = 6; gScript.version[1] = 0; break;     // 6.0
        case 4: gScript.version[0] = 5; gScript.version[1] = 2; break;
        case 5: gScript.version[0] = 0; gScript.version[1] = 1; break;
        case 6: gScript.version[0] = 6; gScript.version[1] = 0; gScript.moduleFileNameLength = 0; break; // version unknown
        case 7: gScript.versionSize = 0; break;
        case 8: gScript.getInfoOk = false; break;
        case 9: gScript.queryOk = false; break;
        case 10: gScript.failLoad.insert("DbgHelp.dll"); break;               // falls back to ImageHlp.dll
        case 11: gScript.failLoad.insert("DbgHelp.dll"); gScript.missing.insert("imagehlp.dll|SymLoadModule"); break;
        case 12: gScript.failLoad.insert("DbgHelp.dll"); gScript.missing.insert("imagehlp.dll|StackWalk"); break;
        case 13: gScript.symInitResult = FALSE; break;
        case 14: gScript.failLoad.insert("NTDLL.dll"); break;
        case 15: gScript.failLoad.insert("kernel32.dll"); break;
        case 16: gScript.failLoad.insert("tlhelp32.dll"); break;
        case 17: gScript.failLoad.insert("kernel32.dll"); gScript.failLoad.insert("tlhelp32.dll"); break;
        case 18: gScript.failLoad.insert("psapi.dll"); break;
        case 19: gScript.missing.insert("kernel32.dll|CreateToolhelp32Snapshot"); break; // falls back to tlhelp32.dll
        case 20: gScript.missing.insert("kernel32.dll|Thread32Next"); gScript.missing.insert("tlhelp32.dll|Thread32Next"); break;
        case 21: gScript.missing.insert("dbghelp.dll|MiniDumpWriteDump"); break;
        case 22: gScript.missing.insert("dbghelp.dll|SymInitialize"); break;
        case 23: gScript.missing.insert("dbghelp.dll|SymGetLineFromAddr"); break;
        case 24: gScript.failLoad.insert("kernel32.dll"); gScript.missing.insert("tlhelp32.dll|Module32First"); break;
        case 25: gScript.failLoad.insert("tlhelp32.dll"); gScript.missing.insert("kernel32.dll|Module32Next"); break;
        case 26: gScript.missing.insert("psapi.dll|EnumProcessModules"); gScript.missing.insert("psapi.dll|GetModuleInformation"); break;
        case 27: gScript.missing.insert("ntdll.dll|NtOpenThread"); break;
        default: {
            // Random mixture: not DbgHelp+ImageHlp both failing (the original crashes then).
            for (const char *dll : kDlls)
                if (rng() % 6 == 0 && std::strcmp(dll, "ImageHlp.dll")) gScript.failLoad.insert(dll);
            if (gScript.failLoad.count("DbgHelp.dll") && rng() % 2) gScript.failLoad.erase("DbgHelp.dll");
            for (const char *dll : kDlls)
                for (const char *proc : kProcs)
                    if (rng() % 25 == 0) {
                        std::string key = dll;
                        for (char &c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                        gScript.missing.insert(key + "|" + proc);
                    }
            if (rng() % 6 == 0) gScript.symInitResult = FALSE;
            if (rng() % 6 == 0) gScript.moduleFileNameLength = 0;
            if (rng() % 6 == 0) gScript.versionSize = 0;
            for (auto &v : gScript.version) v = static_cast<uint16_t>(rng() % 8);
        }
        }
        uint8_t garbage[0x74];
        for (uint8_t &b : garbage) b = static_cast<uint8_t>(rng());
        std::string a = RunLoader(ctx, 0, garbage);
        std::string b = RunLoader(ctx, 1, garbage);
        ExpectSame(ctx, ("loader mode " + std::to_string(mode)).c_str(), a, b);
    }

    // Already loaded: nothing is touched.
    {
        gScript = Script();
        uint8_t garbage[0x74];
        for (uint8_t &b : garbage) b = static_cast<uint8_t>(rng());
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            std::memcpy(table, garbage, 0x74);
            *done = 1;
            uint8_t r = side == 0 ? ctx.Original<uint8_t(__thiscall *)(void *)>(kLoader)(table) : appbase::LoadDbgHelp(reinterpret_cast<appbase::DbgHelpTable *>(table), nullptr);
            results[side] = "al=" + std::to_string(r) + " " + Hex(table, 0x74) + "\n" + Joined();
        }
        ExpectSame(ctx, "loader already done", results[0], results[1]);
    }

    std::memcpy(table, savedTable, sizeof(savedTable));
    *done = savedDone;
}

// ---- 0x1bd87a ----

DIFF_TEST(ReporterDump_WriteMiniDump, kWriteDump)
{
    Imports imports(ctx);
    InstallLoaderFakes(imports);
    imports.Set(kIatGetCurrentThreadId, reinterpret_cast<void *>(&FakeThreadId));
    imports.Set(kIatCreateFileA, reinterpret_cast<void *>(&FakeCreateFile));
    imports.Set(kIatSetFilePointer, reinterpret_cast<void *>(&FakeSetFilePointer));
    imports.Set(kIatCloseHandle, reinterpret_cast<void *>(&FakeCloseHandle));
    uint8_t *table = ctx.image.At<uint8_t>(appbase::DbgHelpTable::kGlobal);
    uint8_t *done = ctx.image.At<uint8_t>(appbase::DbgHelpTable::kDoneFlag);
    uint8_t savedTable[0x74];
    std::memcpy(savedTable, table, sizeof(savedTable));
    const uint8_t savedDone = *done;
    int32_t *codePage = ctx.image.At<int32_t>(kSystemCodePageCache);
    const int32_t savedCodePage = *codePage;

    static const char *const kNames[] = {"Minidump.mdmp", "C:\\Games\\The Sims 2\\EP9\\TSBin\\Minidump-2026-10-11 12.30.01.mdmp", "", "x", "fifteen chars.md",
                                         "sixteen chars.mdm", "C:\\very long directory name that needs the heap, certainly more than fifteen characters\\Minidump.mdmp"};
    std::mt19937 rng(0x1bd87a);
    for (int run = 0; run < 600; ++run) {
        gScript = Script();
        gModules.clear();
        int mode = run < 12 ? run : static_cast<int>(rng() % 12);
        bool preloaded = false;
        switch (mode) {
        case 0: break;                                                      // loads, dumps
        case 1: gScript.symInitResult = FALSE; break;                       // loader fails: nothing else happens
        case 2: gScript.missing.insert("dbghelp.dll|MiniDumpWriteDump"); break; // no MiniDumpWriteDump: nothing
        case 3: gScript.createFileResult = nullptr; break;                  // CreateFileA fails (null)
        case 4: gScript.createFileResult = INVALID_HANDLE_VALUE; break;     // ORIGINAL-BUG: carries on
        case 5: preloaded = true; break;                                    // already loaded: the loader does nothing
        case 6: preloaded = true; gScript.createFileResult = nullptr; break;
        case 7: gScript.dumpResult = FALSE; break;
        case 8: gScript.failLoad.insert("DbgHelp.dll"); break;
        case 9: preloaded = true; break;
        default: gScript.createFileResult = reinterpret_cast<void *>(static_cast<uintptr_t>(rng() | 1)); break;
        }
        std::string name = kNames[rng() % 7];
        void *info = reinterpret_cast<void *>(static_cast<uintptr_t>(rng()));
        uint8_t garbage[0x74];
        for (uint8_t &b : garbage) b = static_cast<uint8_t>(rng());
        if (preloaded) { // a loaded table: MiniDumpWriteDump present, or (mode 9) absent
            void *p = mode == 9 ? nullptr : reinterpret_cast<void *>(&FakeDump);
            std::memcpy(garbage + 0x48, &p, 4);
        }
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            *codePage = -3;
            std::memcpy(table, garbage, 0x74);
            *done = preloaded ? 1 : 0;
            alignas(8) uint8_t raw[appbase::ExceptionReporterObject::kSize + 8];
            std::memset(raw, 0x6B, sizeof(raw));
            auto *obj = reinterpret_cast<appbase::ExceptionReporterObject *>(raw);
            obj->mDumpFile.Construct(name.c_str());
            if (side == 0) ctx.Original<void(__thiscall *)(void *, void *)>(kWriteDump)(obj, info);
            else appbase::WriteMiniDump(obj, nullptr, static_cast<_EXCEPTION_POINTERS *>(info));
            results[side] = "done=" + std::to_string(*done) + " table " + Hex(table, 0x74) + " dumpFile [" + obj->mDumpFile.mString + "]\n" + Joined();
            obj->mDumpFile.Destruct();
        }
        ExpectSame(ctx, ("write minidump mode " + std::to_string(mode)).c_str(), results[0], results[1]);
    }

    std::memcpy(table, savedTable, sizeof(savedTable));
    *done = savedDone;
    *codePage = savedCodePage;
}
