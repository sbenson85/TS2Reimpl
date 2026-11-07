#pragma once
#include <cstddef>
#include <cstdint>

#include "rz/RZFrameworkHelpers.h"

// cGZWinScrollbar2: the UI scroll bar (arrows, a track and a thumb, drawn as bevels and triangles or from up to three
// images; RVAs 1.18.0.187). Hook-shaped: the game creates scroll bars from its own code (the window control manager's
// factory and the list boxes), and its cGZWin base is not a C++ class yet, so the functions below run on game-built
// objects through RVA hooks.
//
// Faces: cGZWin at +0 (cIGZWin vtable 0xe579ec, 173 slots), cIGZWinScrollbar2 at +0xf8 (vtable 0xe57ca4, 26 slots).
// Size 0x174 (the deleting destructor frees 0x174 bytes).
//
// The bar is horizontal or vertical (+0x149). Along it: the back arrow, the track before the thumb, the thumb, the
// track after it and the forward arrow (the parts HitTest returns: 1 back arrow, 2 page back, 5 thumb, 3 page
// forward, 4 forward arrow, 0 outside). The value runs from the minimum to the maximum less the page size. An arrow
// moves it by the line step, the track by the page size less the overlap (at least 1); holding the button repeats
// through the timer at +0x14c (300 ms, then 70 ms: the tick posts kRepeatMessage to the bar itself).
class cGZWinScrollbar2 {
public:
    static constexpr uint32_t kVTable = 0xe579ec;     // cGZWin face
    static constexpr uint32_t kBarVTable = 0xe57ca4;  // cIGZWinScrollbar2 face at +0xf8
    static constexpr uint32_t kSize = 0x174;
    static constexpr uint32_t kIid = 0x4b42da53;      // cIGZWinScrollbar2
    static constexpr uint32_t kRepeatMessage = 0x8b443261;
    static constexpr uint32_t kNotifyValue = 0x4b42dcc7;   // the value changed (SetValue with notify)
    static constexpr uint32_t kNotifyRelease = 0x4b42dcc8; // the mouse button was released on the bar

    enum Part : uint8_t { kNone = 0, kBackArrow = 1, kPageBack = 2, kPageForward = 3, kForwardArrow = 4, kThumb = 5 };

    // Constructor 0x9596cd (204 bytes): the cGZWin constructor, members (range 0..0x100, page 0x10, steps 1,
    // vertical), the timer's constructor, window flag 0x8000 cleared, then SetArea(0, 0, 0x20, 0x60). Returns this.
    cGZWinScrollbar2 *Construct();
    // Destructor 0x959799 (83 bytes): the timer's destructor, Release of the images (last first), the cGZWin
    // destructor.
    void Destruct();
    // Deleting destructor 0x9597ec (cIGZWin slot 170, ret 4).
    cGZWinScrollbar2 *DeletingDestructor(uint32_t flags);

    // cIGZWin slots (this = the object).
    bool QueryInterface(uint32_t riid, void **ppv);          // 0, 0x95ae54: only 0x4b42da53, 1, 0x22ba0121, 0xe98b2f57
    bool DoMessage(const uint32_t *message);                // 3, 0x959b69: kRepeatMessage repeats the pressed part
    bool Activate();                                        // 4, 0x95ad1d: the timer's callback, sizes
    bool Teardown();                                        // 5, 0x95b0c3
    uint32_t SetArea(int32_t left, int32_t top, int32_t right, int32_t bottom); // 64, 0x95ae8d: cGZWin's eax
    bool Draw();                                            // 101, 0x959f6c
    bool OnMouseDown(int32_t x, int32_t y, uint32_t keys);  // 155, 0x959c88 (returns false)
    bool OnMouseUp(int32_t x, int32_t y, uint32_t keys);    // 157, 0x959ee7 (returns false)
    bool OnMouseMove(int32_t x, int32_t y, uint32_t keys);  // 159, 0x959de2 (returns false)
    bool OnMouseExit(uint32_t keys);                        // 163, 0x959d96 (returns false)

    // Non-virtual members.
    void MeasureParts();                                    // 0x95980e (617 bytes): arrow, track and minimum sizes
    void PlaceThumb();                                      // 0x959a77 (217 bytes): thumb position and length
    uint8_t HitTest(int32_t x, int32_t y);                  // 0x95ad49 (267 bytes, ret 8): a Part
    // 0x95b10e (41 bytes, cdecl): the repeat timer's callback: interval 70 ms, then kRepeatMessage to the bar
    // (cIGZWin slot 168).
    static void __cdecl RepeatTick(cGZWinScrollbar2 *self);

    void *Bar() { return reinterpret_cast<uint8_t *>(this) + 0xf8; }
    int32_t Width() const { return mArea[2] - mArea[0]; }
    int32_t Height() const { return mArea[3] - mArea[1]; }

    uint8_t mWin[0xc8];           // +0x000 cGZWin (vtable at +0); fields used: +0x5c notification target, +0x88 draw
                                  //        context, +0xe8 flags (2 enabled)
    int32_t mArea[4];             // +0x0c8 cGZWin's area rect (the bar's size is its width and height)
    uint8_t mWinRest[0x20];       // +0x0d8 rest of cGZWin
    const void *mBarVTable;       // +0x0f8 cIGZWinScrollbar2 face
    int32_t mValue;               // +0x0fc
    int32_t mMin;                 // +0x100
    int32_t mMax;                 // +0x104
    int32_t mPageSize;            // +0x108
    int32_t mPageOverlap;         // +0x10c a page jump is mPageSize - mPageOverlap (at least 1)
    int32_t mLineStep;            // +0x110 an arrow click
    RZUnknownSlot mImage[3];      // +0x114 arrows (four arrows side by side), track, thumb images (cIGZImage-like:
                                  //        slot 9 width, 10 height)
    int32_t mArrowLength;         // +0x120 along the bar
    int32_t mTrackEnd;            // +0x124 track image: end cap length
    int32_t mTrackMiddle;         // +0x128 track image: half its length
    int32_t mThumbEnd;            // +0x12c thumb: end cap length (the minimum thumb is twice it)
    int32_t mThumbMiddle;         // +0x130 thumb image: half its length
    int32_t mMinWidth;            // +0x134 0: any (SetArea keeps the width of a vertical bar at least... at this)
    int32_t mMinHeight;           // +0x138
    int32_t mThumbPos;            // +0x13c from the end of the back arrow
    int32_t mThumbLength;         // +0x140
    int32_t mDragOffset;          // +0x144 pointer position less mThumbPos when the thumb was grabbed
    uint8_t mDirty;               // +0x148 1 MeasureParts, 2 PlaceThumb pending
    uint8_t mVertical;            // +0x149
    uint8_t mHotPart;             // +0x14a under the pointer
    uint8_t mPressedPart;         // +0x14b
    uint8_t mTimer[0x28];         // +0x14c cRZCallbackTimer (vtable 0xe61bb8)
};

// The cIGZWinScrollbar2 face (this = object + 0xf8).
class cGZWinScrollbar2Face {
public:
    cGZWinScrollbar2 *Obj() { return reinterpret_cast<cGZWinScrollbar2 *>(reinterpret_cast<uint8_t *>(this) - 0xf8); }

    bool SetOrientation(uint32_t vertical);                 // 4, 0x95afb6
    bool GetOrientation(uint32_t *out);                     // 5, 0x95ace4
    bool SetValue(int32_t value, uint32_t notify);          // 6, 0x95b015
    bool GetValue(int32_t *out);                            // 7, 0x95ad01
    bool SetMin(int32_t value, uint32_t notify);            // 9, 0x95af83
    bool SetMax(int32_t value, uint32_t notify);            // 11, 0x95af50
    bool GetMax(int32_t *out);                              // 12, 0x95ac9f
    bool SetPageSize(int32_t value, uint32_t notify);       // 13, 0x95b089
    bool SetPageOverlap(int32_t value);                     // 15, 0x95afdb
    bool SetLineStep(int32_t value);                        // 17, 0x95af3d
    bool SetArrowImage(void *image);                        // 19, 0x95aeef
    bool SetTrackImage(void *image);                        // 21, 0x95af16
    bool SetThumbImage(void *image);                        // 23, 0x95afee
    bool GetMinSize(int32_t *out);                          // 25, 0x95acad: width, height
};

static_assert(offsetof(cGZWinScrollbar2, mArea) == 0xc8);
static_assert(offsetof(cGZWinScrollbar2, mBarVTable) == 0xf8);
static_assert(offsetof(cGZWinScrollbar2, mImage) == 0x114);
static_assert(offsetof(cGZWinScrollbar2, mArrowLength) == 0x120);
static_assert(offsetof(cGZWinScrollbar2, mThumbPos) == 0x13c);
static_assert(offsetof(cGZWinScrollbar2, mDirty) == 0x148);
static_assert(offsetof(cGZWinScrollbar2, mTimer) == 0x14c);
static_assert(sizeof(cGZWinScrollbar2) == cGZWinScrollbar2::kSize);
