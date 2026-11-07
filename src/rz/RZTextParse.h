#pragma once
// The game's shared text parsers (1.18.0.187, a size-built module next to RZText's conversions): 64-bit hexadecimal
// and integer numbers, the double reader and the separator splitter the variants, property sets and ini readers use.
// Free __cdecl functions; lane LS (foundations F2 part 3). The 32-bit hexadecimal parser is hexparse::ParseHex32
// (misc/HexParse.h).
#include <cstdint>

class cRZString;

namespace rztext {

// 0x1b0ec2: white space skipped (the CRT's isspace on the sign-extended character), an optional "0x" / "0X", then up
// to 16 hexadecimal digits of either case up to the first other character. No digits: 0.
uint64_t __cdecl ParseHex64(const char *text);

// 0x1b101c: white space skipped, an optional sign, then the base from the prefix ("0x" / "0X" 16, another leading
// "0" 8, else 10) and digits (isdigit, else isalpha letters through toupper as 10...) while they are below the base;
// accumulated in 64 bits with wrap-around, negated for '-'.
uint64_t __cdecl ParseInt64(const char *text);

// 0x1afaa3: base raised to the power exponent (both converted to double; the CRT's pow).
double __cdecl PowerOf(int base, int exponent);

// 0x1b1ab5: the game's own double reader: white space, a sign, decimal digits, an optional fraction and an optional
// exponent ('e' / 'E', a sign, decimal digits: the mantissa times PowerOf(10, n), or divided for '-'). `end` (may be
// null) receives the first character not taken.
double __cdecl ReadDouble(const char *text, const char **end);

// 0x1b1008: ReadDouble without the end pointer.
double __cdecl ParseDouble(const char *text);

// 0x1b1557: splits text[0, size) at `separator` (' ' means a space or a tab) into parts[0 .. *count) (on entry *count
// is how many parts there is room for). Leading white space of the whole text is skipped; a separator inside an odd
// number of double quotes is not one; white space after each separator is skipped; with a separator other than ' '
// each part but the last has its trailing white space removed; a part in double quotes loses them. Returns false when
// the parts ran out before the text did. Only each part's std::string is written (cRZString at +4).
bool __cdecl SplitText(const char *text, uint32_t size, char separator, cRZString *parts, uint32_t *count);

} // namespace rztext
