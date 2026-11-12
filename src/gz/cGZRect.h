#pragma once
// cGZRect's intersection (lane 261108s, scenegraph TS1i): a free helper the game keeps among the dynamic render
// strategy's code (0x373520) and calls from the scene graph's dirty rectangles, the gz draw contexts and the window
// code (0x92c6b0, 0x97d406). cGZRect itself is defined by gz/cIGZBuffer.h (and, for the window scripts,
// gz/cGZScriptableControlSet.h): four int32 words, left, top, right, bottom, right and bottom exclusive.
#include <cstdint>

struct cGZRect;

// 0x373520 (__thiscall on `out`, ret 8; hooked): when a and b overlap (each edge strictly inside the other's
// opposite one, signed), `*out` = their intersection and true; otherwise `*out` all zero and false. `out` may be a or b:
// each word is read just before it is written.
bool GZRectIntersect(cGZRect *out, const cGZRect *a, const cGZRect *b);

// The hook's entry point (src/hook/tables/cGZRect.cpp): `out` in ecx.
struct cGZRectAdapters {
    static bool __fastcall Intersect(cGZRect *out, void *, const cGZRect *a, const cGZRect *b);
};
