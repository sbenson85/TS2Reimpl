#pragma once
#include <cstdint>

// Free helpers of the GZWin framework (not class members): drawing helpers the controls' Draw slots call (cGZWinText,
// cGZWinListBox, cGZWinGrid, cGZWinTextEdit draw frames; cGZWinGen, cGZWinCombo, cGZWinScrollbar2 draw bevels), and the
// window helpers that change a window's or its children's flags. All cdecl. The draw context is a cIGZDrawContext and
// the windows are cIGZWin (method facts in db/methods/GZWinHelpers.jsonl); tests/gz_win_helpers.cpp checks them against
// the original with the drawfx / winfx recording fakes (docs/ui-testing.md).

// 0x92ef70 (228 bytes): PushState, SetColor(argb), then four FillRects: the top strip {l, t, r, t + w}, the left strip
// {l, t + w, l + w, b - w}, the right strip {r - w, t + w, r, b - w} and the bottom strip {l, b - w, r, b}; PopState
// (a tail call in the original). No clamping: a thickness larger than the rect draws inverted strips.
void __cdecl GZWinDrawFrame(void *context, const int32_t *rect, uint32_t argb, int32_t thickness);

// 0x92ee00 (365 bytes): fills the rect with argb (alpha forced to 0xff), then the top and left one-pixel edges in the
// colour shaded by +56 (raised) or -56, and the bottom and right edges (each one pixel shorter at its start) shaded the
// other way. Only the low byte of `raised` counts.
void __cdecl GZWinDrawBevel(void *context, const int32_t *rect, uint32_t argb, uint32_t raised);

// 0x92f310 (151 bytes): 0xff000000 | each of red, green, blue (low bytes of the arguments) plus delta, clamped to 0..255.
uint32_t __cdecl GZWinShadeColor(uint32_t red, uint32_t green, uint32_t blue, int32_t delta);

// 0x9351b9 (68 bytes): for a non-null window, Hide() with flag 0x20 cleared, then flag 0x20 put back as it was.
void __cdecl GZWinHideQuiet(void *window);
// 0x935329 (70 bytes): the same around SetVisible(1).
void __cdecl GZWinShowQuiet(void *window);

// 0x93509c (54 bytes): FindChildAndSetEnabled(window, ids[i], ids[i] == selected) for each of the `count` ids.
void __cdecl GZWinEnableOnlyFromList(void *window, const uint32_t *ids, uint32_t count, uint32_t selected);
// 0x9350d2 (46 bytes): the same for the ids first .. first + count - 1 (the end is computed once, unsigned: a range that
// wraps past 0xffffffff enables nothing).
void __cdecl GZWinEnableOnlyInRange(void *window, uint32_t first, uint32_t count, uint32_t selected);
