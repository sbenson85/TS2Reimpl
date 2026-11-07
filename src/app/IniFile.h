#pragma once
#include "app/AppServices.h"
#include "rz/cRZIniFile.h"
#include "rz/cRZString.h"

#include <cstdint>
#include <string>

// The path normalisation chain under 0x1c33df, and the string concatenation 0x2f5500 that
// cGZFileSystem::DataDirectory / PlugInDirectory use. All are __cdecl free functions.
//
// The chain resolves a (possibly relative) path against a base directory, collapsing "." and ".."
// components, in a 0x20c-byte local buffer (so results are limited to 0x208 characters).
namespace inifile {

// 0x1c0f23: true for an ASCII letter. Takes the character as a stack byte.
bool __cdecl IsAsciiLetter(char c);

// 0x1c2215: true if the text starts with a drive ("X:\") or a UNC prefix ("\\") and contains neither
// "\." nor "/." (so it needs no normalising). The upper bits of eax are garbage in the original;
// only al is meaningful. 0x1c21fe is the same test on the std::string inside a cRZString.
bool __cdecl IsCanonicalAbsolutePath(const char *text);
bool __cdecl IsCanonicalAbsolutePathString(const cRZString *path); // 0x1c21fe

// 0x1c148f: dest = normalise(base + "\" + path). dest null: allocate (malloc) the result. Returns dest
// or 0 (path null, a result longer than maxlen / 0x208, or ".." above the root).
char *__cdecl CombinePath(char *dest, const char *path, int32_t maxlen, const char *base);

// 0x1c1655: dest = the absolute form of path against the current drive / directory (drive letters,
// UNC prefixes, rooted paths handled). Returns dest or 0.
char *__cdecl MakeAbsolutePath(char *dest, const char *path, int32_t maxlen);

// 0x1c1760: out.str = MakeAbsolutePath(in). 0x1c17bd: out.str = CombinePath(in, base) (base may be
// null, meaning ""). Both return true on success; only al is meaningful.
bool __cdecl AbsolutePathString(cRZString *out, const cRZString *in);                       // 0x1c1760
bool __cdecl CombinePathString(cRZString *out, const cRZString *in, const cRZString *base); // 0x1c17bd

// 0x1c33df: out.str = in.str if it is canonical absolute, else the combined path against base (or
// the current drive when base is null). The return value is the callee's (bool in al).
bool __cdecl ResolvePathString(cRZString *out, const cRZString *in, const cRZString *base);

// 0x2f5500: out = left + right, built by inserting left at the front of right and moving the result
// out (right is left empty). Returns out.
std::string *__cdecl ConcatMove(std::string *out, const std::string *left, std::string *right);

} // namespace inifile

// cGZFileSystem::DataDirectory (cIGZFileSystem slot 7, 0xc75ff9) and PlugInDirectory (slot 8, 0xc763f0) are
// defined in IniFile.cpp: first use reads "<app directory><app name>.ini" ([Directories] Data= and PlugIn=)
// through a temporary cRZIniFile and resolves the value against the application directory; the answer is
// cached under the flag at +0x20 / +0x21. Both return the text of the cached string.
