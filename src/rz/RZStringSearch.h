#pragma once
// A case-insensitive substring search (lane NO). RVAs are 1.18.0.187.

namespace rztext {

// 0x1b1a52 (__cdecl, /O1): the first place in `text` where `part` occurs, comparing tolower of each character (the
// CRT's, as signed chars); `text` itself when `part` is empty, null when not found (and when `text` is empty and `part`
// is not). Callers: the expression evaluator's strstr (0xb08510) and 0x32b970.
const char *__cdecl FindNoCase(const char *text, const char *part);

} // namespace rztext
