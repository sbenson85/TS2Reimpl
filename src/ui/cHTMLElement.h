#pragma once
#include <cstddef>
#include <cstdint>

#include "game/vtable.h"

// HTML layout elements of the in-game browser/help view (RVAs 1.18.0.187). Only what slot 7 (hit testing) touches.

// An integer rectangle, edges inclusive at left/top and exclusive at right/bottom. Also used by the window code
// (cTSWinSceneGraph, cTSWinTextLabel).
struct HTMLRect {
    int32_t left;   // +0
    int32_t top;    // +4
    int32_t right;  // +8
    int32_t bottom; // +0xc

    // 0x5bbc56 (__thiscall, ret 8): left <= x < right and top <= y < bottom (signed), in the original's order of tests.
    __declspec(noinline) bool Contains(int32_t x, int32_t y) const;
};
static_assert(sizeof(HTMLRect) == 0x10, "layout");

// The element base (vtable 0xe5ce38 is cHTMLLink's; cHTMLMETA shares the body). Hook-shaped: the game builds the
// elements, so the member is hooked at its RVA and works on objects carrying the game's vtables.
class cHTMLElement {
public:
    static constexpr uint32_t kVTable = 0xe5bf9c; // cHTMLElement's own vtable

    // Slot 0, 0x97e660 (__thiscall, ret 4): deleting destructor. Stores the cHTMLElement vtable (the destructor body;
    // the element owns nothing), frees the 0x1c bytes when bit 0 of `flags` is set, returns this.
    cHTMLElement *DeletingDestructor(uint32_t flags);

    // Slot 7, 0x9892c0 (__thiscall, ret 0xc): hit test. 0 when (x, y) is outside the bounds; else asks slot 6 (a bool
    // "accepts hits" query; cHTMLLink and cHTMLMETA answer false): false gives 1, true stores this in *hit and gives 2.
    int32_t HitTest(cHTMLElement **hit, int32_t x, int32_t y);

    const void *mVTable; // +0
    uint8_t _gap4[8];    // +4
    HTMLRect mBounds;    // +0xc
};
static_assert(offsetof(cHTMLElement, mBounds) == 0xc, "layout");
static_assert(sizeof(cHTMLElement) == 0x1c, "layout: the size the deleting destructor frees");
