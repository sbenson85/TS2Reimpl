#pragma once
#include <cstdint>

// The bytes that follow a lead byte in an encoding (lane VU, O-c; the canvas's WM_CHAR handler collects a character's
// bytes with it, src/gz/cCanvasW32Proc.cpp). Encodings as in rz/RZTextConvert.h; character tests in rz/RZTextChars.h.
namespace RZText {

// 0x1b2817 (__cdecl, a byte; only the low byte counts): the trail bytes of a UTF-8 lead in the old 2- to 6-byte forms:
// 0xc2-0xdf 1, 0xe0-0xef 2, 0xf0-0xf7 3, 0xf8-0xfb 4, 0xfc-0xfd 5; anything else (ASCII, 0x80-0xc1, 0xfe, 0xff) 0.
uint32_t Utf8TrailCount(uint32_t byte);

// 0x1b0dc9 (__cdecl, a byte and a code page): 1 for a double-byte lead (IsLeadByte), else 0.
uint32_t CodePageTrailCount(uint32_t byte, int32_t codePage);

// 0x1b06a6 (__cdecl, a byte and an encoding; -2 the default, -1 the system code page): UTF-8's trail count, a code
// page's (100 and up), else 0.
uint32_t TrailCount(uint32_t byte, int32_t encoding);

} // namespace RZText
