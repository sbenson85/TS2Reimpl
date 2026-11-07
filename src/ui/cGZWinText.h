#pragma once
#include <cstddef>
#include <cstdint>

#include "rz/RZFrameworkHelpers.h"

// cGZWinText: the UI text label (a window drawing its caption, optionally wrapped, filled and framed; RVAs
// 1.18.0.187). Hook-shaped: the game creates labels from its own code (cGZWinCtrlMgr 0x94b7ae, the message box
// 0x952b74, cGZWinFileBrowser), and its cGZWin base is not a C++ class yet, so the functions below run on game-built
// objects through RVA hooks.
//
// Faces: cGZWin at +0 (cIGZWin vtable 0xe55cf4, 173 slots; the caption is cGZWin's cRZString at +0x60, its
// std::string at +0x64), cIGZWinText at +0xf8 (vtable 0xe55fac, 29 slots). Size 0x134 (the deleting destructor frees
// 0x134 bytes).
//
// Style bits (+0x118, tested through cIGZWinText slot 6, the shared 0x9377ae): 2 wraps, 4 fills the background
// (cIGZWin slot 142's colour), 8 draws a frame (+0x11c), 0x10000000 the colours are the font's own (set by
// SetColorRGB when both match the font's). Colours (+0x128, +0x12c) are RGB words, 0xdeadbeef unset; cIGZWin slot 143
// turns one into a draw colour, slot 144 back. Alignment (+0x124, 0..8): the horizontal text format is
// alignment / 3 (+0x130), the vertical placement alignment % 3 (top, centre, bottom).
class cGZWinText {
public:
    static constexpr uint32_t kVTable = 0xe55cf4;     // cGZWin face
    static constexpr uint32_t kTextVTable = 0xe55fac; // cIGZWinText face at +0xf8
    static constexpr uint32_t kSize = 0x134;
    static constexpr uint32_t kIid = 0x212cdc1f;      // cIGZWinText
    static constexpr uint32_t kUnset = 0xdeadbeef;

    // Constructor 0x949ed1 (151 bytes): the cGZWin constructor, members (margins 2, 2; text rect 0, 0, 8, 8; style 6;
    // colours unset), window flag 0x8000 cleared. Returns this.
    cGZWinText *Construct();
    // Destructor 0x949f68 (47 bytes): Teardown, Release of the font, then the cGZWin destructor (tail jump).
    void Destruct();
    // Deleting destructor 0x949f97 (cIGZWin slot 170, ret 4).
    cGZWinText *DeletingDestructor(uint32_t flags);

    // cIGZWin slots (this = the object).
    bool QueryInterface(uint32_t riid, void **ppv);          // 0, 0x94a382: 0x212cdc1f answers +0xf8 (AddRef'd)
    bool DoMessage(uint32_t *message);                      // 3, 0x949ffd: mouse messages go to the parent (+0x58)
    bool Activate();                                        // 4, 0x94a315: the default font when there is none
    bool Teardown();                                        // 5, 0x94a784
    void SetArea(int32_t left, int32_t top, int32_t right, int32_t bottom); // 64, 0x94a428: Layout on a size change
    uint32_t SetCaption(void *text);                        // 90, 0x94a4a0: cGZWin's, Layout, redraw (slot 105's eax)
    bool Draw();                                            // 101, 0x94a0bb
    uint32_t SetBackgroundColor(uint32_t value);                   // 119, 0x94a4cc: cGZWin's slot 119, redraw

    // Non-virtual members.
    void Layout();                                          // 0x94a7a9 (344 bytes): the text rect (+0x108)
    void Measure(int32_t *out, uint32_t wrapToContent, uint32_t splitLines); // 0x94a916 (630 bytes, ret 0xc)

    void *Text() { return reinterpret_cast<uint8_t *>(this) + 0xf8; }

    uint8_t mWin[0xf8];           // +0x000 cGZWin (vtable at +0); fields used: +0x34 draw rect, +0x58 parent, +0x60
                                  //        caption (cRZString; std::string at +0x64, size +0x74), +0x84 text context,
                                  //        +0x88 draw context, +0xe8 flags
    const void *mTextVTable;      // +0x0f8 cIGZWinText face
    RZUnknownSlot mFont;          // +0x0fc
    uint32_t mFontID;             // +0x100
    int8_t mMarginX;              // +0x104
    int8_t mMarginY;              // +0x105
    int8_t mExtra[2];             // +0x106 (cIGZWinText slots 25 / 26 only)
    int32_t mTextRect[4];         // +0x108 where the text is drawn, relative to the draw rect
    uint32_t mStyle;              // +0x118
    uint32_t mFrameColor;         // +0x11c
    uint32_t mUnused120;          // +0x120 (not touched by the class's code)
    uint32_t mAlignment;          // +0x124
    uint32_t mColor[2];           // +0x128 text colours (RGB; kUnset)
    uint32_t mFormat;             // +0x130 alignment / 3
};

// The cIGZWinText face (this = object + 0xf8).
class cGZWinTextFace {
public:
    cGZWinText *Obj() { return reinterpret_cast<cGZWinText *>(reinterpret_cast<uint8_t *>(this) - 0xf8); }

    bool SizeToText(uint32_t wrapToContent, uint32_t splitLines); // 5, 0x94a064: SetSize to the measured text
    bool SetStyle(uint32_t mask, uint32_t on);                    // 7, 0x94a6ed
    bool SetColorRGB(uint32_t index, uint32_t rgb);               // 8, 0x94a632
    uint32_t SetColor(uint32_t index, uint32_t color);            // 9, 0x94a609: slot 8 with slot 144's RGB
    uint32_t SetTextColor(uint32_t color);                        // 10, 0x94a5fb: slot 9 (0, color)
    bool GetColorRGB(uint32_t index, uint32_t *out);              // 11, 0x94a2d1
    bool GetColor(uint32_t index, uint32_t *out);                 // 12, 0x94a292: through slot 143
    uint32_t GetTextColor(uint32_t *out);                         // 13, 0x94a284: slot 12 (0, out)
    uint32_t ResetColor(uint32_t index);                          // 14, 0x949fb9: the font's colour (slot 9's eax)
    uint32_t ResetTextColor();                                    // 15, 0x949ff5: slot 14 (0)
    uint32_t SetFill(uint32_t on);                                // 16, 0x94a492: slot 7 (4, on)
    uint32_t IsFilled();                                          // 17, 0x94a37a: slot 6 (4)
    bool SetFont(void *font);                                     // 18, 0x94a4e6
    bool SetFontID(uint32_t id);                                  // 19, 0x94a576
    bool GetFontID(uint32_t *out);                                // 20, 0x94a23c
    bool SetAlignment(uint32_t alignment);                        // 21, 0x94a3bb
    bool GetAlignment(uint32_t *out);                             // 22, 0x94a22e
    bool SetMargins(int8_t x, int8_t y);                          // 23, 0x94a5bd
    bool GetMargins(int8_t *x, int8_t *y);                        // 24, 0x94a252
    bool SetExtra(int8_t a, int8_t b);                            // 25, 0x94a6da
    bool GetExtra(int8_t *a, int8_t *b);                          // 26, 0x94a2fe
    uint32_t SetFrame(uint32_t on, uint32_t color);               // 27, 0x94a5db: returns the colour (eax)
    uint32_t *GetFrame(uint8_t *on, uint32_t *color);             // 28, 0x94a269: returns `color` (eax)
};

static_assert(offsetof(cGZWinText, mTextVTable) == 0xf8);
static_assert(offsetof(cGZWinText, mFont) == 0xfc);
static_assert(offsetof(cGZWinText, mMarginX) == 0x104);
static_assert(offsetof(cGZWinText, mTextRect) == 0x108);
static_assert(offsetof(cGZWinText, mStyle) == 0x118);
static_assert(offsetof(cGZWinText, mAlignment) == 0x124);
static_assert(offsetof(cGZWinText, mColor) == 0x128);
static_assert(offsetof(cGZWinText, mFormat) == 0x130);
static_assert(sizeof(cGZWinText) == cGZWinText::kSize);
