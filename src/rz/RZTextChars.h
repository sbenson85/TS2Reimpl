#pragma once
#include <cstdint>

// Characters of text in an encoding (lane OF, for cGZFont's text slots; docs/subsystems/rendering.md R6). Encodings as in
// rz/RZTextConvert.h: 0 ASCII, 8 UTF-8, 16 UTF-16, -2 the default, -1 the system code page, else a Windows code page.
namespace RZText {

// 0x1b0de2 (__cdecl, a byte and a code page): whether the byte starts a two-byte character in the double-byte code pages
// 932 (Shift-JIS), 936 (GBK), 949 (Korean) and 950 (Big5); false for any other code page.
bool IsLeadByte(uint32_t byte, int32_t codePage);

// 0x1b0545 (__cdecl, text, length, encoding): the units the first character of the text takes (at most `length`; 0 for no
// text). UTF-8 follows the old 2- to 6-byte forms when the lead and the next byte are valid, else 1. A two-byte lead
// of a double-byte code page with a second unit available takes 2. Anything else (and UTF-16) takes 1.
uint32_t CharLength(const void *text, uint32_t length, int32_t encoding);

// 0x1b0d66 (__cdecl, text, count, encoding): the units the first `count` characters take (each measured against an
// unlimited length).
uint32_t CharsLength(const void *text, uint32_t count, int32_t encoding);

// 0x1b2518 (__cdecl, a character): CJK ideographs and compatibility forms, and the full-width forms.
bool IsIdeograph(uint32_t ch);

// 0x1b2558 (__cdecl): Hangul compatibility jamo and syllables.
bool IsHangul(uint32_t ch);

// 0x1b257c (__cdecl): a character of the Thai block (U+0E00 to U+0E7F).
bool IsThaiBlock(uint32_t ch);

} // namespace RZText
