#pragma once
#include <cstdint>

// Line-breaking character classes and clusters of UTF-16 text (lane OF, for cGZFont's line breaker; docs/subsystems/rendering.md
// R6). In the game the sets are lazily built statics; here they are constant tables.
namespace RZText {

// 0x1b2293 (__cdecl, the low 16 bits of a character): false for the characters that must not start a line (closing
// brackets and quotes, stops, small kana, prolonged-sound marks...).
bool CanStartLine(uint32_t ch);

// 0x1b23d5 (__cdecl, the low 16 bits): false for the characters that must not end a line (opening brackets and quotes,
// currency signs...).
bool CanEndLine(uint32_t ch);

// 0x1b0959 (__cdecl, the low 16 bits): a line or paragraph break: 10, 13, 0x85, 0x2028, 0x2029.
bool IsLineBreakChar(uint32_t ch);

// 0x1b0a60 (__cdecl, a 16-bit unit, zero-extended by its callers): a place a line can break after: the spaces (0x82, 0x20,
// U+2002 to U+200B, U+3000), the hyphens and dashes (0x2d, U+2010, U+2012 to U+2015, U+203E, U+301C, U+FE31, U+FE32,
// U+FE58), and the control characters up to 0x20 except 10 and 13.
bool IsBreakSpace(uint32_t ch);

// 0x1b258f (__cdecl): a Thai character that sits above or below its consonant: U+0E31, U+0E34 to U+0E3A, U+0E47 to U+0E4E.
bool IsThaiMark(uint32_t ch);

// 0x1b27ae (__cdecl, UTF-16 text, count): the units of a Thai syllable cluster at the start: the character and up to two
// marks, each followed by an optional sara am (U+0E33): 0 for no text, else 1 to 4.
// ORIGINAL-BUG: with a count of 3 and two marks it asks whether the unit after the count is U+0E33, and counts it if so.
uint32_t ThaiClusterLength(const uint16_t *text, uint32_t count);

// 0x1b215d (__cdecl, UTF-16 text, count): the units of the first cluster: a run of characters that a line may not be
// broken inside (a break character ends it, a character that cannot start a line after one that can end it, ideographs
// and spaces by the line breaking rules, and U+0E46 ends a cluster). 0 for no text.
uint32_t ClusterLength(const uint16_t *text, uint32_t count);

// 0x1b05ed (__cdecl, text, count, encoding): the units the first character of the text takes in its encoding, counting a Thai
// syllable cluster as one in UTF-8 and UTF-16 (otherwise as CharLength). 0 for no text.
// ORIGINAL-BUG: a UTF-8 cluster of three converted units asks for the fourth, of which the original's buffer holds stack
// garbage; here it is 0.
uint32_t CharUnits(const void *text, uint32_t count, int32_t encoding);

} // namespace RZText
