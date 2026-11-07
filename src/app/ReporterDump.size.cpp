#include "app/ReporterDump.h"

#include "game/game.h"
#include "rz/RZTextConvert.h"
#include "rz/cRZString.h"

#define NOMINMAX
#include <windows.h>

#include <cstring>
#include <vector>

namespace appbase {
namespace {

// Import address table slots in the image.
constexpr uint32_t kIatGetProcAddress = 0xd9a124, kIatLoadLibraryA = 0xd9a128, kIatGetModuleFileNameA = 0xd9a16c;
constexpr uint32_t kIatCloseHandle = 0xd9a17c, kIatGetCurrentThreadId = 0xd9a194, kIatCreateFileA = 0xd9a1ac;
constexpr uint32_t kIatSetFilePointer = 0xd9a1b0, kIatGetCurrentProcess = 0xd9a1b8, kIatGetCurrentProcessId = 0xd9a1bc;
constexpr uint32_t kIatGetFileVersionInfoA = 0xd9a5e8, kIatVerQueryValueA = 0xd9a5ec, kIatGetFileVersionInfoSizeA = 0xd9a5f0;

template <typename Fn> Fn Import(uint32_t iatRva)
{
    return *GameAddress<Fn>(iatRva);
}

void *LoadLib(const char *name)
{
    return Import<void *(WINAPI *)(const char *)>(kIatLoadLibraryA)(name);
}

void *GetProc(void *module, const char *name)
{
    return Import<void *(WINAPI *)(void *, const char *)>(kIatGetProcAddress)(module, name);
}

struct Entry {
    void **slot;
    const char *name;
};

} // namespace

// ---- 0x1bc0c2 ----

uint8_t __cdecl QueryModuleVersion(void *module, uint32_t *fileVersion, uint32_t *productVersion)
{
    char path[0x104];
    if (!Import<DWORD(WINAPI *)(void *, char *, DWORD)>(kIatGetModuleFileNameA)(module, path, 0x104)) return 0;
    DWORD handle = 0;
    DWORD size = Import<DWORD(WINAPI *)(const char *, DWORD *)>(kIatGetFileVersionInfoSizeA)(path, &handle);
    if (!size) return 0;
    std::vector<char> data(size);
    if (!Import<BOOL(WINAPI *)(const char *, DWORD, DWORD, void *)>(kIatGetFileVersionInfoA)(path, 0, size, data.data())) return 0;
    uint16_t *info = nullptr; // VS_FIXEDFILEINFO viewed as words
    UINT length = 0;
    if (!Import<BOOL(WINAPI *)(const void *, const char *, void **, UINT *)>(kIatVerQueryValueA)(data.data(), "\\", reinterpret_cast<void **>(&info), &length)) return 0;
    // fixed info: file version MS at +8, LS at +0xc, product version MS at +0x10, LS at +0x14
    fileVersion[0] = info[5];
    fileVersion[1] = info[4];
    fileVersion[2] = info[7];
    fileVersion[3] = info[6];
    productVersion[0] = info[9];
    productVersion[1] = info[8];
    productVersion[2] = info[11];
    productVersion[3] = info[10];
    return 1;
}

// ---- 0x1bc8a7 ----

uint8_t DbgHelpTable::Load()
{
    uint8_t *const done = GameAddress<uint8_t>(kDoneFlag);
    if (*done) return 1;

    std::memset(this, 0, sizeof(*this));
    processId = Import<DWORD(WINAPI *)()>(kIatGetCurrentProcessId)();
    process = reinterpret_cast<void *>(static_cast<uintptr_t>(Import<DWORD_PTR(WINAPI *)()>(kIatGetCurrentProcess)()));

    // Every slot is zero from here (the memset above), so the original's "only if not yet set"
    // tests are all true and are not repeated.
    dbghelp = LoadLib("DbgHelp.dll");
    if (dbghelp) {
        uint32_t fileVersion[4], productVersion[4];
        if (QueryModuleVersion(dbghelp, fileVersion, productVersion) &&
            (fileVersion[0] > 5 || (fileVersion[0] == 5 && fileVersion[1] >= 1)))
            versionAtLeast51 = 1;
    } else {
        dbghelp = LoadLib("ImageHlp.dll");
    }

    const Entry required[] = {
        {&symInitialize, "SymInitialize"}, {&symCleanup, "SymCleanup"}, {&symSetOptions, "SymSetOptions"},
        {&stackWalk, "StackWalk"}, {&symFunctionTableAccess, "SymFunctionTableAccess"}, {&symGetModuleBase, "SymGetModuleBase"},
        {&symGetSymFromAddr, "SymGetSymFromAddr"}, {&symGetLineFromAddr, "SymGetLineFromAddr"},
    };
    const Entry optional[] = {{&symLoadModule, "SymLoadModule"}, {&unDecorateSymbolName, "UnDecorateSymbolName"}, {&miniDumpWriteDump, "MiniDumpWriteDump"}};
    // Without a module nothing is looked up (and SymInitialize stays null).
    if (dbghelp) {
        for (const Entry &e : required) {
            *e.slot = GetProc(dbghelp, e.name);
            if (!*e.slot) return 0;
        }
        for (const Entry &e : optional) *e.slot = GetProc(dbghelp, e.name);
    }

    ntdll = LoadLib("NTDLL.dll");
    if (ntdll) ntOpenThread = GetProc(ntdll, "NtOpenThread");
    kernel32 = LoadLib("kernel32.dll");
    tlhelp32 = LoadLib("tlhelp32.dll");
    if (kernel32 || tlhelp32) {
        // Each is looked up in kernel32.dll first (a null module handle is passed as it is), then tlhelp32.dll.
        const Entry toolhelp[] = {{&createToolhelp32Snapshot, "CreateToolhelp32Snapshot"}, {&module32First, "Module32First"},
                                  {&module32Next, "Module32Next"}, {&thread32First, "Thread32First"}, {&thread32Next, "Thread32Next"}};
        for (const Entry &e : toolhelp) {
            *e.slot = GetProc(kernel32, e.name);
            if (!*e.slot) *e.slot = GetProc(tlhelp32, e.name);
        }
    }
    psapi = LoadLib("psapi.dll");
    if (psapi) {
        enumProcessModules = GetProc(psapi, "EnumProcessModules");
        getModuleFileNameExA = GetProc(psapi, "GetModuleFileNameExA");
        getModuleBaseNameA = GetProc(psapi, "GetModuleBaseNameA");
        getModuleInformation = GetProc(psapi, "GetModuleInformation");
    }

    // SymInitialize(process, NULL, FALSE) through the table (null when no module loaded: crash).
    if (!reinterpret_cast<BOOL(WINAPI *)(void *, const char *, BOOL)>(symInitialize)(process, nullptr, FALSE)) return 0;
    *done = 1;
    return 1;
}

uint8_t __fastcall LoadDbgHelp(DbgHelpTable *table, void *)
{
    return table->Load();
}

// ---- 0x1bd87a ----

void __fastcall WriteMiniDump(ExceptionReporterObject *self, void *, _EXCEPTION_POINTERS *info)
{
    DbgHelpTable *table = GameAddress<DbgHelpTable>(DbgHelpTable::kGlobal);
    if (!table->Load()) return;
    if (!table->miniDumpWriteDump) return;

    void *process = reinterpret_cast<void *>(static_cast<uintptr_t>(Import<DWORD_PTR(WINAPI *)()>(kIatGetCurrentProcess)()));
    DWORD processId = Import<DWORD(WINAPI *)()>(kIatGetCurrentProcessId)();
    struct ExceptionInformation { // MINIDUMP_EXCEPTION_INFORMATION
        DWORD threadId;
        _EXCEPTION_POINTERS *pointers;
        BOOL clientPointers;
    } exception;
    exception.threadId = Import<DWORD(WINAPI *)()>(kIatGetCurrentThreadId)();
    exception.pointers = info;
    exception.clientPointers = TRUE;

    cRZString name;
    name.Construct();
    RZText::ConvertStringToString(*self->mDumpFile.AsInterface(), *name.AsInterface(), -1);
    void *file = Import<void *(WINAPI *)(const char *, DWORD, DWORD, void *, DWORD, DWORD, void *)>(kIatCreateFileA)(
        name.mString.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file) {
        Import<DWORD(WINAPI *)(void *, LONG, LONG *, DWORD)>(kIatSetFilePointer)(file, 0, nullptr, FILE_END);
        reinterpret_cast<BOOL(WINAPI *)(void *, DWORD, void *, DWORD, void *, void *, void *)>(table->miniDumpWriteDump)(
            process, processId, file, 0 /* MiniDumpNormal */, &exception, nullptr, nullptr);
        Import<BOOL(WINAPI *)(void *)>(kIatCloseHandle)(file);
    }
    name.Destruct();
}

} // namespace appbase
