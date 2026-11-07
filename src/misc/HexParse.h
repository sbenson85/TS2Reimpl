#pragma once
// Two small text parsers of the game's (lane HF): a 32-bit hexadecimal number and a GUID text. Both are free __cdecl
// functions with many callers (the property-set serializers, the persist system, the CAS and genetics code, five XC
// entries that make GUID constants).
#include <cstdint>

namespace hexparse {

// 0x1b0e2c (__cdecl, size-built): leading white space skipped (the CRT's isspace on the sign-extended character), an
// optional "0x" / "0X", then up to 8 hexadecimal digits (either case) up to the first other character. No digits: 0.
uint32_t __cdecl ParseHex32(const char *text);

// 0x4f8eb0 (__cdecl): the GUID text's characters other than '-', '{' and '}' taken in groups of 8, each group parsed
// with ParseHex32 into the next of four words (a trailing group of fewer than 8 is dropped; words with no group stay
// 0); the 16 bytes stored at `out16`, which is returned (the ABI of a 16-byte struct returned by value).
void *__cdecl ParseGuidText(void *out16, const char *text);

} // namespace hexparse
