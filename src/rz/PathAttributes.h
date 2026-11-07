#pragma once
#include <cstdint>

class cIGZString;

// 0x1c1931 (__cdecl; lane MA): whether a path's attributes (GetFileAttributes: the wide one from the game's table of wide
// file functions when it is filled, else GetFileAttributesA on the system code page text) meet `flags`. In cRZFile's
// range of statics (between PathExists 0x1c1242 and ResolveShortcutA 0x1c3059) but not declared there, so it is free here.
// Flags, each a requirement:
//   0x0002 not read-only            0x0008 a directory
//   0x0010 a shortcut: ResolveShortcutA (system code page text) must not answer 0 (not a .lnk path); skipped with 0x0400
//   0x0020 archive                  0x0040 hidden                   0x0080 system
// Met: true, or with 0x1000 the read-only bit. A missing file reads as 0xffffffff (every bit set), so it passes the
// attribute requirements but fails "not read-only". Flag 1 (the property files' loader) requires nothing: always true.
// The original returns in al (the rest of eax is left over); every caller tests al.
// Callers: the property files (0xcb1cf0 flag 1, 0xcb28b0 flag 2), the persist system (0x1de470, 0x1e36f0), the rollback
// segment (0x1d2d50, 0x1d4220), cTSObjectFolder::DoMessage 0x4d22b0.
bool __cdecl PathAttributesMatch(const cIGZString *path, uint32_t flags);

namespace pathattr {
constexpr uint32_t kNotReadOnly = 0x2;
constexpr uint32_t kDirectory = 0x8;
constexpr uint32_t kShortcut = 0x10;
constexpr uint32_t kArchive = 0x20;
constexpr uint32_t kHidden = 0x40;
constexpr uint32_t kSystem = 0x80;
constexpr uint32_t kSkipShortcut = 0x400;
constexpr uint32_t kReadOnlyAnswer = 0x1000;
} // namespace pathattr
