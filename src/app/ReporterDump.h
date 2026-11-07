#pragma once
// The exception reporter's crash-dump path: the DbgHelp loader and its table, the DLL version query
// the loader uses, and the minidump writer. The reporter object itself is in app/ExceptionReporter.h.
#include "app/ExceptionReporter.h"

#include <cstddef>
#include <cstdint>

struct _EXCEPTION_POINTERS;

namespace appbase {

// 0x1bc0c2 (__cdecl): the file and product version of a loaded module. Asks GetModuleFileNameA for
// the module's path (260 characters), sizes and reads its version resource and queries the root
// block ("\\", VS_FIXEDFILEINFO). `fileVersion` and `productVersion` each receive four values
// (major, minor, build, revision: the high and low words of the MS then LS dwords). Returns whether
// all of that worked (al only: the upper bits of eax are leftovers).
uint8_t __cdecl QueryModuleVersion(void *module, uint32_t *fileVersion, uint32_t *productVersion);

// The table of DbgHelp (and related) entry points the loader fills, a game global (0x10cb000, 0x74
// bytes) that is zeroed and refilled on every load attempt.
struct DbgHelpTable {
    static constexpr uint32_t kGlobal = 0x10cb000;
    static constexpr uint32_t kDoneFlag = 0x10caffc; // byte: the table is loaded and SymInitialize succeeded

    // 0x1bc8a7 (__thiscall): fills the table (it is zeroed first) and initialises the symbol handler.
    // Returns 1 when the table is ready (also at once when the done flag is already set), else 0.
    // ORIGINAL-BUG: when neither DbgHelp.dll nor ImageHlp.dll loads, the required-symbol lookups are
    // skipped (no module) and the final call goes through the null SymInitialize slot: it crashes.
    uint8_t Load();
    // 0x1bd611 (__thiscall; lane HT, src/app/StaticInitHT.size.cpp): the exit thunk of XC slot 99. When the done flag
    // is set: it and the byte after it cleared, SymCleanup(process) when loaded, the five modules (+0x0c..+0x1c)
    // freed when not null (the fields are left), CloseHandle(process), process zeroed. Returns 1 either way.
    uint8_t Unload();

    uint32_t processId;       // 0x00: GetCurrentProcessId()
    void *process;            // 0x04: GetCurrentProcess()
    uint8_t versionAtLeast51; // 0x08: DbgHelp.dll is version 5.1 or later (DbgHelp.dll itself only)
    uint8_t pad9[3];
    void *dbghelp;            // 0x0c: DbgHelp.dll, else ImageHlp.dll
    void *ntdll;              // 0x10
    void *kernel32;           // 0x14
    void *tlhelp32;           // 0x18
    void *psapi;              // 0x1c
    void *symInitialize;      // 0x20 (the required entry points: Load fails without any of these)
    void *symCleanup;         // 0x24
    void *symSetOptions;      // 0x28
    void *stackWalk;          // 0x2c
    void *symFunctionTableAccess; // 0x30
    void *symGetModuleBase;   // 0x34
    void *symGetSymFromAddr;  // 0x38
    void *symGetLineFromAddr; // 0x3c
    void *symLoadModule;      // 0x40 (optional from here)
    void *unDecorateSymbolName; // 0x44
    void *miniDumpWriteDump;  // 0x48
    void *ntOpenThread;       // 0x4c
    void *createToolhelp32Snapshot; // 0x50 (kernel32.dll, else tlhelp32.dll)
    void *module32First;      // 0x54
    void *module32Next;       // 0x58
    void *thread32First;      // 0x5c
    void *thread32Next;       // 0x60
    void *enumProcessModules; // 0x64 (psapi.dll)
    void *getModuleFileNameExA; // 0x68
    void *getModuleBaseNameA; // 0x6c
    void *getModuleInformation; // 0x70
};

static_assert(sizeof(DbgHelpTable) == 0x74, "DbgHelp table size");
static_assert(offsetof(DbgHelpTable, dbghelp) == 0x0c, "DbgHelp table layout");
static_assert(offsetof(DbgHelpTable, symInitialize) == 0x20, "DbgHelp table layout");
static_assert(offsetof(DbgHelpTable, miniDumpWriteDump) == 0x48, "DbgHelp table layout");
static_assert(offsetof(DbgHelpTable, createToolhelp32Snapshot) == 0x50, "DbgHelp table layout");
static_assert(offsetof(DbgHelpTable, getModuleInformation) == 0x70, "DbgHelp table layout");

// 0x1bc8a7 as a hook target.
uint8_t __fastcall LoadDbgHelp(DbgHelpTable *table, void *edx);

// 0x1bd87a (__thiscall on the reporter, ret 4): loads the DbgHelp table; if it is ready and has
// MiniDumpWriteDump, writes a minidump to the reporter's dump file (+0x80, converted to the system
// code page): CreateFileA (write, always open), SetFilePointer to the end, MiniDumpWriteDump with
// this process and thread and the exception pointers (client pointers), CloseHandle. Does nothing
// otherwise. No return value.
// ORIGINAL-BUG: only a null handle is treated as a failed CreateFileA; INVALID_HANDLE_VALUE goes on
// to SetFilePointer, MiniDumpWriteDump and CloseHandle.
void __fastcall WriteMiniDump(ExceptionReporterObject *self, void *edx, _EXCEPTION_POINTERS *info);

} // namespace appbase
