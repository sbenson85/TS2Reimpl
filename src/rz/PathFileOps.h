#pragma once
#include "game/owned.h"

#include <cstdint>

class cIGZString;
class cRZString;

// Two of cRZFile's path statics that have no cRZFile member (lane QK, N-c2). RVAs are 1.18.0.187.
//
// 0x1c2068 (__cdecl): a time of the file at `path`, found with the CRT's _stat64i32 on the system code page text of the
// path. `which` 1 is the creation time, 2 the modification time and 3 the access time (each the low dword of the 64-bit
// value). 0 when the file cannot be examined, or for any other `which`.
// Callers: the packed file's open check (cGZDBSegmentPackedFile, with 2).
uint32_t __cdecl PathFileTime(const cIGZString *path, int32_t which);

// 0x1c2e25 (__cdecl): deletes the file at `path`: the wide DeleteFile from the game's table of wide file functions (cRZFile's,
// RVA 0x10cb5f0) when it is filled, with the UTF-16 text, else DeleteFileA with the system code page text. Whether it
// succeeded. The original returns in al only (the rest of eax is left over).
// Callers: the write record (when it drops its temporary file), the segment's export and compaction.
bool __cdecl PathDelete(const cIGZString *path);

// ---- Temporary file names (lane QN, N-d2) ----------------------------------------------------------------------------------

// 0x1c1f9c (__cdecl): the system's temporary directory (with its trailing backslash) in `out`. The wide GetTempPathW of the
// game's table (WideFileFunctions::getTempPath, 0x10cb604) when it and GetLongPathNameW (0x10cb600) are filled: the short path
// expanded in place, then the UTF-16 text converted into `out`; else GetTempPathA (import slot 0xd9a21c), GetLongPathNameA
// (0x10cb5fc) when filled, and the text converted from the system code page. False (and `out` untouched) when the
// system call returns 0.
bool __cdecl GetTempDirectory(cIGZString *out);

// 0x1c236b (__cdecl): shortens the text to at most `maxBytes` bytes, never inside a character (RZText::CharLength with
// the default encoding finds the characters); nothing happens to a text that already fits or is empty.
void __cdecl TrimToByteLength(cRZString *text, uint32_t maxBytes);

// 0x1c23bf (__cdecl): makes a new, empty file in `directory` with a name built from `*name` (the prefix, cut to 244 bytes; "RZ_"
// when empty), the time in seconds (counting down by one for each name that is taken) and `suffix` (".tmp" when null):
// "<directory><prefix><time><suffix>". CREATE_NEW opens (access 3, creation 0, sharing 0) decide: the first name that
// opens wins, the file is closed and its full path stored in `*name`. Gives up after 10000 names (false, `*name` as it was).
bool __cdecl MakeTempFileIn(const cIGZString *directory, cIGZString *name, const cIGZString *suffix);

// 0x1c255e (__cdecl): the same in the system's temporary directory (GetTempDirectory; an empty directory text, so a relative
// name, when that fails).
bool __cdecl MakeTempFile(cIGZString *name, const cIGZString *suffix);

// The import slot PathDelete's ANSI branch calls through (0xd9a1f0, DeleteFileA): the game's slot in the hooked game (the tests
// point it at a fake), kernel32's entry in an image-free program (standalone/owned_statics.cpp). The wide function it tries
// first is WideFileFunctions::deleteFile (+0x10 of 0x10cb5e0, rz/WinApiTables.h).
TS2_OWNED_STATIC(void *, d9a1f0);

// The import slot GetTempDirectory's ANSI branch calls through (0xd9a21c, GetTempPathA); same arrangement.
TS2_OWNED_STATIC(void *, d9a21c);
