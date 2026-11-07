#pragma once
// The rest of the exception reporter's text report: the call-stack section (0x1bc29e) and the
// system-information class with the report's "system" section. Group ReportFinish. The pieces the
// section needs (DbgHelp table, module lookup, source-line reading, text-file lines) are in
// ReporterDump.h and ReportBlockers.h.
#include "app/ExceptionReporter.h"
#include "app/ReporterDump.h"

#include <cstddef>
#include <cstdint>

#define NOMINMAX
#include <windows.h>

namespace appbase {

// ---- the DbgHelp structures the stack walk hands to the table's entry points (32-bit layouts) ----

struct StackAddress { // ADDRESS
    uint32_t offset;
    uint16_t segment;
    uint16_t pad;
    uint32_t mode; // 3 = flat
};

struct StackFrame32 { // STACKFRAME (0xa4 bytes)
    StackAddress pc;     // 0x00
    StackAddress ret;    // 0x0c
    StackAddress frame;  // 0x18
    StackAddress stack;  // 0x24
    void *funcTableEntry; // 0x30
    uint32_t params[4];  // 0x34
    uint32_t isFar;      // 0x44
    uint32_t isVirtual;  // 0x48
    uint32_t reserved[3]; // 0x4c
    uint8_t rest[0x4c];  // 0x58: KDHELP and AddrBStore
};
static_assert(sizeof(StackFrame32) == 0xa4, "STACKFRAME size");
static_assert(offsetof(StackFrame32, frame) == 0x18 && offsetof(StackFrame32, stack) == 0x24, "STACKFRAME layout");

struct ImageHelpSymbol { // IMAGEHLP_SYMBOL with a 0x200-character name (the stack walk's local, 0x218 bytes)
    uint32_t sizeOfStruct;   // 0x00: 0x218
    uint32_t address;        // 0x04
    uint32_t size;           // 0x08
    uint32_t flags;          // 0x0c
    uint32_t maxNameLength;  // 0x10: 0x200
    char name[0x204];        // 0x14
};
static_assert(sizeof(ImageHelpSymbol) == 0x218, "IMAGEHLP_SYMBOL size");

struct ImageHelpLine { // IMAGEHLP_LINE (0x14 bytes)
    uint32_t sizeOfStruct; // 0x00: 0x14
    uint32_t key;          // 0x04
    uint32_t lineNumber;   // 0x08
    char *fileName;        // 0x0c
    uint32_t address;      // 0x10
};
static_assert(sizeof(ImageHelpLine) == 0x14, "IMAGEHLP_LINE size");

// 0x1bc29e (__thiscall on the reporter, ret 4): the "Call stack:" section of the report, written to
// the report file (+0xa0) from a copy of `context`. Walks at most 500 frames with the DbgHelp
// table's StackWalk, stopping early when the file is 200000 bytes or longer, when StackWalk fails
// or the frame pointer is 0. Without the table's SymGetSymFromAddr, or with a DbgHelp older than
// 5.1, a note says the information is minimal (the walk still runs). Each frame is one line
// "0xSSSS:0xOOOOOOOO module symbol() + displacement" (the symbol undecorated when the table can);
// a frame without symbol is "0xSSSS:0xOOOOOOOO module." and, for the first frame only, a note says
// the .pdb files are missing. With line information (the address minus one for every frame but
// the first) the line " "file", line N" follows, then ": " and the source line, then an empty line;
// when the source file cannot be read, the line number and ": <unknown code>" (then an empty line).
// ORIGINAL-BUG: when the source line is found, the number printed is one higher than the line
// number the lookup returned (the unreadable-file path prints it unchanged).
void __fastcall WriteCallStack(ExceptionReporterObject *self, void *edx, const CONTEXT *context);

// ---- the version/system information class (RTTI cRZVersion, vtable 0xda4580, 0x30 bytes) ----
//
// Reads the version resource of a module (by default the running executable) and answers a few
// string queries on it; the report's system section is built from it.
struct cRZVersion {
    static constexpr uint32_t kVTable = 0xda4580;
    static constexpr uint32_t kSize = 0x30;

    // 0x1c98ab (__thiscall, ret 4): the path is a copy of `path`'s text; nothing is loaded yet. Returns this.
    cRZVersion *ConstructFromPath(const cRZString *path);
    // 0x1c98d6 (__thiscall): the path is the running executable's (GetModulePath). Returns this.
    cRZVersion *Construct();
    // 0x1c9901 (__thiscall): frees the version data (delete[]) and destroys the path string.
    void Destruct();
    // 0x1c992d (vtable slot 0, __thiscall, ret 4): Destruct, then a sized operator delete(this, 0x30)
    // when bit 0 of `flags` is set. Returns this.
    cRZVersion *DeletingDestructor(uint32_t flags);

    // 0x1c9da3 (__thiscall): when nothing is loaded yet: the path converted to the system code page,
    // GetFileVersionInfoSizeA, a new[] buffer of that size (kept even when the next call fails),
    // GetFileVersionInfoA, and the "\\VarFileInfo\\Translation" block (mTranslation); when that
    // block is missing the buffer is freed again. Nothing is done for a size of 0.
    // ORIGINAL-BUG: when GetFileVersionInfoA fails the buffer stays, with a null mTranslation; the
    // queries below then take the object as loaded and read through the null pointer.
    void Load();
    // 0x1c9b58 (__thiscall, ret 4): the path text assigned to `out`. Returns 1.
    uint8_t GetPath(cRZString *out) const;
    // 0x1c9ad1 / 0x1c9b6d / 0x1c9a4a (__thiscall, ret 4): the "FileDescription", "FileVersion" and
    // "Debug Version" strings of the version resource ("\\StringFileInfo\\<language><code page>\\<name>")
    // assigned to `out` (cleared when the value is missing). Loads first; 0 when nothing could be
    // loaded, else whether the value was found (al only).
    uint8_t GetDescription(cRZString *out);
    uint8_t GetFileVersion(cRZString *out);
    uint8_t GetDebugVersion(cRZString *out);
    // 0x1c9c6b (__thiscall, ret 4; this is unused): `out`->Sprintf("Windows NT %u.%u", whether
    // Windows 10.0 or later, 0) through its slot 31. Returns 1.
    // ORIGINAL-BUG: the first number is the test's result (0 or 1), not a version number.
    uint8_t GetSystemVersion(cIGZString *out) const;
    // 0x1c9c01 (__thiscall, ret 8; this is unused): the physical memory in megabytes, total and
    // available, each rounded up to a multiple of 4, from GlobalMemoryStatusEx; when the game global
    // at 0x10cb6a8 is set, it and the one after it are returned instead. Returns 1.
    // ORIGINAL-BUG: the length handed to GlobalMemoryStatusEx is 0x20 (a MEMORYSTATUS), not the 0x40
    // it needs: the call fails and the numbers are whatever the stack held.
    uint8_t GetMemory(uint32_t *totalMegabytes, uint32_t *availableMegabytes) const;

    const void *mVTable;       // 0x00
    cRZString mPath;           // 0x04: the module's path
    uint32_t mUnused24;        // 0x24: zeroed, never used
    char *mData;               // 0x28: the version resource (new[]), or null
    uint16_t *mTranslation;    // 0x2c: language and code page of the first translation, inside mData
};

static_assert(sizeof(cRZVersion) == cRZVersion::kSize, "cRZVersion size");
static_assert(offsetof(cRZVersion, mData) == 0x28 && offsetof(cRZVersion, mTranslation) == 0x2c, "cRZVersion layout");

// 0x1c9d0b (__cdecl): VerifyVersionInfoW for "at least this major.minor and service pack major"
// (all three compared with greater-or-equal) on an otherwise zeroed OSVERSIONINFOEXW. al only.
uint8_t __cdecl VerifyWindowsVersion(uint32_t major, uint32_t minor, uint32_t servicePackMajor);

// 0x1bb94c (__stdcall, ret 4): appends the "system information" text of the report to `out`: the
// application path, version name, version, debug version, system version, memory and
// configuration, each as "label: value" and CR LF, taken from a cRZVersion of the running
// executable. The debug and system version lines only when their value is not empty.
void __stdcall WriteSystemInfo(cRZString *out);

} // namespace appbase
