#pragma once
// The exception reporter's text report and the pieces it is built from: the module list, the
// register / stack / instruction dumps, the report itself (WriteReport) and the filter body that
// drives it (Report). Group ReportWrite. The earlier pieces (the call-stack walk, the system
// information, the DbgHelp table, module lookup, text-file lines) are in ReportFinish.h,
// ReporterDump.h and ReportBlockers.h.
#include "app/ExceptionReporter.h"
#include "app/ReporterDump.h"
#include "rz/cRZStopwatch.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#define NOMINMAX
#include <windows.h>

namespace appbase {

// ---- the loaded-module list (0x48-byte elements of a std::vector) ----

struct ModuleInfo {
    cRZString path; // 0x00: the module's full path
    cRZString name; // 0x20: its file name
    uint32_t base;  // 0x40: load address
    uint32_t size;  // 0x44: image size

    ModuleInfo()
    {
        path.Construct();
        name.Construct();
        base = 0;
        size = 0;
    }
    ModuleInfo(const ModuleInfo &other) { CopyConstruct(&other); }
    ~ModuleInfo() { Destruct(); }
    ModuleInfo &operator=(const ModuleInfo &) = delete;

    // 0x1ba8df (__thiscall, ret 4): copies both strings (cRZString(src, 0, npos)), base and size.
    // Returns this.
    ModuleInfo *CopyConstruct(const ModuleInfo *other);
    // 0x1baabe (__thiscall): destroys the name, then the path.
    void Destruct();
};

static_assert(sizeof(ModuleInfo) == 0x48, "ModuleInfo size");
static_assert(offsetof(ModuleInfo, name) == 0x20 && offsetof(ModuleInfo, base) == 0x40, "ModuleInfo layout");

// 0x1bb2b6 (__cdecl): fills `modules` (cleared first) with the modules of the process through a
// Toolhelp snapshot (the DbgHelp table's CreateToolhelp32Snapshot / Module32First / Module32Next,
// which have to be present or the list stays empty). Returns whether the list is not empty (al
// only). When the table cannot be loaded, 0. When the snapshot cannot be made the result is
// undefined (the original returns its caller's ebx): 0 here.
uint8_t __cdecl EnumerateModulesToolhelp(std::vector<ModuleInfo> *modules, uint32_t processId);

// 0x1bb108 (__cdecl, the second half starts at 0x1bb126): the module list of a process. Toolhelp
// first; when that gives nothing, through PSAPI (EnumProcessModules, GetModuleInformation,
// GetModuleFileNameExA, GetModuleBaseNameA of the table) with `process`. Returns whether the list is
// not empty (al only).
// ORIGINAL-BUG: a failing GetModuleInformation leaves the module's base and size as they were
// (the previous module's, or the stack's garbage for the first).
uint8_t __cdecl EnumerateModules(std::vector<ModuleInfo> *modules, uint32_t processId, void *process);

// 0x1bb026 (__cdecl): fills `modules` through EnumerateModules and, when the table has SymLoadModule,
// loads every one into the symbol handler (SymLoadModule(process, 0, path, name, base, size); the
// path and name are copies with an extra NUL appended). The list is left filled.
void __cdecl LoadSymbolModules(void *process, uint32_t processId, std::vector<ModuleInfo> *modules);

// 0x1bcf4a (__cdecl): once per process (the byte at 0x10caffd), loads the DbgHelp table, sets the
// symbol options to 0x14 and loads the symbols of every module of the process. Returns 1 (al only);
// 0 when the table cannot be loaded.
uint8_t __cdecl LoadSymbolModulesOnce();

// 0x1bcdd4 (__thiscall): the "Loaded Modules" section. Loads the table (0 when it fails), sets the
// symbol options to 0x14, loads the module list (LoadSymbolModules) and, when report level 3 is on,
// writes one line per module. Returns 1.
uint8_t __fastcall WriteModules(ExceptionReporterObject *self, void *edx);

// ---- the dumps ----

// 0x1bd744 (__thiscall, ret 0x18): writes `count` bytes as hex lines to the report. The first bytes
// are skipped until `address` is a multiple of `align` (signed division; the bytes are skipped from
// `data` too). A line is "0x%08x   " and `groupsPerLine` groups of `bytesPerGroup` bytes, each byte
// "%02x" and each group followed by a space; `address` advances by a full line each time.
// ORIGINAL-BUG: a last, incomplete line is never written.
void __fastcall WriteHexDump(ExceptionReporterObject *self, void *edx, uint32_t address, const uint8_t *data, uint32_t count, int32_t groupsPerLine,
                             int32_t bytesPerGroup, int32_t align);

// 0x1bcc6c (__thiscall, ret 4): the manual stack walk used when DbgHelp is unavailable: a "Call
// stack:" heading and column header, then one line "0x%08x  0x%08x  0x%04x:0x%08x module." (the
// instruction pointer, the frame pointer, the section and offset of the instruction pointer in its
// module) per frame: the next frame is the saved frame pointer at [ebp], the next instruction
// pointer the return address at [ebp + 4]. Stops when the next frame pointer is not 4-aligned, does
// not grow, or 8 bytes at it cannot be written (IsBadWritePtr).
// ORIGINAL-BUG: the context's frame pointer itself is read without a check, so a bad one crashes.
void __fastcall WriteManualCallStack(ExceptionReporterObject *self, void *edx, const CONTEXT *context);

// 0x1bd064 (__thiscall, ret 4): "Instruction data" for the context's instruction pointer: when 1
// byte there is readable (IsBadReadPtr), the 0x100 bytes around it (the readable ones, in a
// guarded loop) through WriteHexDump (32 groups of 1 byte, no alignment); else a note that it is
// not readable.
void __fastcall WriteInstructionData(ExceptionReporterObject *self, void *edx, const CONTEXT *context);

// 0x1bd1de (__thiscall, ret 4): "Stack data" around the context's stack pointer: the bytes from 0x180
// below to 0x180 above it, clipped to the thread's stack (the stack base and limit of the thread
// information block), through WriteHexDump (8 groups of 4 bytes, 4-aligned). A stack pointer
// outside the stack, or an exception while reading, give a note instead.
void __fastcall WriteStackData(ExceptionReporterObject *self, void *edx, const CONTEXT *context);

// ---- the report ----

// 0x1c9211 (__thiscall, ret 4): waits for `milliseconds` stopwatch units: a new stopwatch of the same
// mode as the one given is started and Sleep(0) is called until its Elapsed is not below the count.
void __fastcall WaitStopwatch(const cRZStopwatch *stopwatch, void *edx, uint32_t units);

// 0x1bb410 (__thiscall, ret 4): the text report. Opens the report file (+0x60) for writing
// (access 3, creation 3, sharing 1; nothing is written when that fails). A file longer than 200000
// bytes is closed and the process terminated through RemoveFilterAndTerminate. Then, by level: 0:
// the time, exception code, thread id and version information; 5: the extra-data broadcast around
// the module lines; 0: the exception module and address; 2: registers; 1: the loaded modules and the
// call stack (or the manual one), stack data and instruction data. The file is flushed and closed.
void __fastcall WriteReport(ExceptionReporterObject *self, void *edx, EXCEPTION_POINTERS *info);

// 0x1bd69a (__thiscall, ret 4): the unhandled-exception filter's body. Removes the filter the
// application had before, names the files, broadcasts 0xb627ad2, writes the minidump and the text
// report, broadcasts 0xb627ad3, installs the filter stub (0x1bd687) again and waits 1000 units of a
// mode 4 stopwatch. Returns 1 when the flag byte +0x19a is set; else, with a debugger present, what the
// application's previous filter says (0 when it had none); else the process is terminated.
uint32_t __fastcall Report(ExceptionReporterObject *self, void *edx, EXCEPTION_POINTERS *info);

} // namespace appbase
