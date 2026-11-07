#pragma once
#include <cstdint>

// The game's wildcard match (RVAs 1.18.0.187), used by the log service, the effects manager and the cheat commands.
namespace RZWildcard {

// 0x9ccc0d (__cdecl): true when `text` matches `pattern`, where '?' matches any one character and '*' any run
// (a final '*' matches the rest at once). Case-sensitive, byte by byte; recursive at each '*'.
bool __cdecl Match(const char *text, const char *pattern);

// 0x9ccea3 (__cdecl): Match, case-insensitively unless caseSensitive (any nonzero byte): both texts are copied
// into strings and lowercased (_strlwr, cRZString's LowerCase helper 0x1ad167) first.
bool __cdecl MatchCase(const char *text, const char *pattern, uint8_t caseSensitive);

} // namespace RZWildcard
