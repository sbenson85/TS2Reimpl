#pragma once
#include "gz/Framework.h"
#include "gz/cIGZString.h"
#include "rz/cRZCmdLine.h"

#include <cstdint>

// Start-up of the framework (RVAs 1.18.0.187): the command-line steps it needs and
// GZFrameWork_Startup itself. docs/startup-map.md section 4.3 has the order of the steps.
namespace fw_startup {

// 0x1420d2 (__thiscall, ret 4): cRZCmdLine's assignment. Copies the reference count, then the
// argument vector (0x1420af: vector assignment, reusing the elements and the capacity it can), then
// the text and the empty string, in that order. Returns this. 0x1420af itself is a byte-identical
// match of a library vector assignment (probe element type of the same size), so it is not
// hooked or counted: CopyArguments below is its equivalent for cRZString elements.
cRZCmdLine *__fastcall AssignCmdLine(cRZCmdLine *self, void *edx, const cRZCmdLine *other);

// The vector assignment 0x14155c/0x1420af performs (a no-op for itself).
void CopyArguments(cRZStringVector &dst, const cRZStringVector &src);

// 0x1aeb57 (__cdecl, two arguments): makes argument 0 of `cmd` the full path of the program.
// `text` is a path to use, or null to ask GetModulePath. The path is copied through a zeroed
// 512-byte buffer (strncpy), so only its first 511 or so characters matter. When the command line
// has arguments and the path and argument 0 name the same file (their last path component with
// the extension cut off, lowercased) the argument is replaced; when the first argument is some other
// word, or there is none, the path is inserted in front. An empty path inserts nothing.
void __cdecl ParseCommandLine(cIGZCmdLine *cmd, cIGZString *text);

// 0x144514 (__cdecl, two arguments): GZFrameWork_Startup(cmdline, installFlag). Runs on g_Framework
// and g_App; returns whether the whole start-up passed (g_ExitCode == 0 at the end), false after
// setting g_ExitCode to -1 on the paths that abort.
bool __cdecl GZFrameWork_Startup(cRZCmdLine *commandLine, uint32_t installFlag);

} // namespace fw_startup
