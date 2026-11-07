#pragma once
// What the exception reporter's text report still needed: the module of an address, directory
// creation for the report folder, the report and dump file names, and the text file's read-line
// path. Group ReportBlockers.
#include "app/ExceptionReporter.h"
#include "gz/cIGZString.h"
#include "rz/cRZString.h"
#include "rz/cRZTextFile.h"

#include <cstdint>

namespace appbase {

// 0x1bbe01 (__cdecl): the module that contains `address` and the section within it. *section and
// *offset are zeroed first. VirtualQuery (import) gives the allocation base; GetModuleFileNameA
// (import, 0x1ff characters into a zeroed 0x200-byte buffer) names the module, and the name is
// converted from the system code page into `name` (ConvertStringFrom, result ignored). The base is
// read as a PE image: the first section (in header order) whose range
// [VirtualAddress, VirtualAddress + max(VirtualSize, SizeOfRawData)] (end inclusive) holds the
// address gives *section = its 1-based number and *offset = address - VirtualAddress. Returns
// whether it was found (al only; the name is already assigned when the section search fails).
// A null allocation base, or a failing VirtualQuery / GetModuleFileNameA, return false with the
// name untouched.
uint32_t __cdecl FindModuleOfAddress(const void *address, cIGZString *name, uint32_t *section, uint32_t *offset);

// 0x1c4caf (__cdecl): creates every directory of `path` that does not exist yet, one component at a
// time (the text is given a final backslash first). Returns whether it succeeded (al only); an
// empty path fails. A leading "\\server\" is skipped as a unit. When a directory cannot be
// created, the ones this call created are removed again (in creation order, so only the last
// can go: ORIGINAL-BUG, see the source) and false is returned.
uint32_t __cdecl CreateDirectories(cRZString *path);

// 0x1bd49b (__thiscall): builds the reporter's report and dump file names from the directory
// (made to exist, else the executable's own directory), the names and the local time.
void __fastcall NameFiles(ExceptionReporterObject *self, void *edx);

// ---- the text file's line reading ----

// 0x1acbfb (__cdecl): dst's text becomes a copy of `text` (std::string assignment, self-assignment
// skipped).
void __cdecl AssignText(const std::string *text, cRZString *dst);

// 0x1ad84c (__cdecl): dst's text becomes the UTF-16 text narrowed one unit at a time: units up to
// 0x7f as they are, anything else '?'.
void __cdecl AssignNarrowed(const std::u16string *wide, cRZString *dst);

// 0x1c408f (__thiscall, ret 4): reads the next line of the text file into `out` (its Resize(0) is
// called first, then FromChar(text, length) at the end). Opens the file read-only (access 1, creation
// 2, sharing 1) when it is not open yet (slot 4 says so); fails (al 0) when it cannot, or when the
// position is at the end. The file is read 0x80 bytes at a time until a CR or LF; when a CR LF or LF
// CR pair follows, both are consumed, otherwise the file position is moved back to just after
// the first terminator. The line (without terminator) is collected in the file's narrow or wide
// buffer (the wide one for UTF-16 files, mCharSize 2), converted into the file's mText and handed
// to `out`. Returns 1 once a line was appended (an empty line before a terminator counts); a read
// failure after some data still gives that data and 1. Only al is defined.
// ORIGINAL-BUG: for a UTF-16 file the appended count is the line's length in bytes but is used as a
// count of 16-bit units, so the wide line also holds the terminator and the text after it (up to
// twice the line's length in bytes counted from the line start; past the 0x80 bytes of the read
// buffer it reads whatever lies after it on the stack).
uint32_t __fastcall ReadLineToString(cRZTextFile *file, void *edx, cIGZString *out);

// 0x1c42bf (__thiscall, ret 8): ReadLineToString into a scratch string, then up to *length bytes of
// the line are copied to `buffer` (no terminator) and *length is the number copied. Returns whether
// a line was read (al only).
uint32_t __fastcall ReadLineToBuffer(cRZTextFile *file, void *edx, char *buffer, uint32_t *length);

// 0x1bbd40 (__cdecl): line `lineIndex` (counted from 0, negative counts as 0; at most 0x800 bytes)
// of the text file at `path`, assigned to out (whose text is emptied first). The file is opened
// read-only (access 1, creation 2, sharing 1); false (al only) when it cannot be opened or has too
// few lines.
uint32_t __cdecl ReadFileLine(const char *path, int32_t lineIndex, cRZString *out);

} // namespace appbase
