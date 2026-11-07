#pragma once
#include <cstdint>
#include <string>

// Searching a text for one "$Token" with its optional numeric parameters ("$Local1", "$Cash2:3"), the first thing
// every stage of the string-token expansion (cTSObject::vf262, 0x535130) does.

// 0x534d90 (__cdecl, six stack arguments, ret 0). Finds `token` in `text` at or after `start`, ignoring case (both
// are lowercased with _strlwr first, so a NUL ends the lowercased text), and returns its position, -1 when there is
// none. *length is the length of the match: the token itself, plus ":digits" and ":digits:digits" when the caller
// asked for parameters (arg1 or arg2 non-null). With parameters, *arg1 and *arg2 are first set to -1, then to the
// numbers after the first and the second colon (only when at least one digit follows it; the parameters are read
// from the original text, not the lowercased copy). The numbers go through atoi, so a huge number saturates. An
// empty token matches at `start` (as long as start is not past the end of the text).
int32_t FindToken(const std::string &text, const std::string &token, uint32_t start, int32_t *length, int32_t *arg1, int32_t *arg2);
