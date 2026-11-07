#pragma once
#include <cstddef>
#include <cstdint>

#include "rz/RZFrameworkHelpers.h"
#include "rz/cRZString.h"

// cGZWinBtn: the UI push button / check box / radio button / image button (RVAs 1.18.0.187). Hook-shaped: the game
// creates buttons from its own code (cGZWinCtrlMgr's factory slots 0x94adbe / 0x94af1f, cGZWinFileBrowser 0x9626fc,
// cGZWinCombo, and the cTSWinBtn / cTSWinTextLabel subclasses' constructors 0x6287d0 / 0x62a640), and its cGZWin base
// is not a C++ class yet, so the functions below run on game-built objects through RVA hooks.
//
// Faces: cIGZWinBtn at +0 (vtable 0xe53e14, 53 slots: cIGZWinBtn's 51 plus two of cGZWinBtn's own), cGZWin at +4
// (cIGZWin vtable 0xe53eec, 173 slots). Size 0x1ec (the deleting destructor frees 0x1ec bytes).
//
// Style bits (+0xfc, tested by IsStyleSet 0x936a5c): 1 on (checked), 4 caption shown, 0x10 no bevel, 0x20 size to
// fit, 0x40 caption wraps, 0x80 caption shifts when pressed, 0x100 has a tooltip, 0x200 / 0x400 tooltip flags 1 / 2,
// 0x800 toggles on click, 0x1000 pressed, 0x2000 mouse over, 0x4000 key held, 0x10000000 mouse tracking,
// 0x1000000 redraw on change. Button types (+0x160): 1 plain, 2 / 3 image with four states, 4 image with eight
// states (check box), 0 none. Notifications go to the window at +0x60 through cIGZWin slot 168.
class cGZWinBtn {
public:
    static constexpr uint32_t kVTable = 0xe53e14;        // cIGZWinBtn face
    static constexpr uint32_t kWinVTable = 0xe53eec;     // cGZWin face at +4
    static constexpr uint32_t kInterfaceVTable = 0xe53d44; // cIGZWinBtn's own vtable (the constructor's first store)
    static constexpr uint32_t kSize = 0x1ec;
    static constexpr uint32_t kIid = 0x8810;              // cIGZWinBtn
    static constexpr uint32_t kTimerMessage = 0xc7ec278e;  // flash timer tick (DoMessage)
    static constexpr int32_t kUnsetColor = 0x8000000;

    // Constructor 0x93536f (294 bytes): the cGZWin constructor, members, style 0x6a0, flag 0x800 cleared through
    // the cGZWin face's SetFlag override, background 0xffc0c0c0, colours unset, sounds 0. Returns this.
    cGZWinBtn *Construct();
    // Destructor 0x935495 (117 bytes): Teardown, the timer's destructor, the strings, Release of the image and the
    // font, then the cGZWin destructor (tail jump).
    void Destruct();
    // Deleting destructor 0x62a667 (34 bytes, ret 4; cIGZWin slot 170 reaches it through the thunk 0x62a65f).
    cGZWinBtn *DeletingDestructor(uint32_t flags);

    // cIGZWinBtn slots (this = the object).
    bool QueryInterface(uint32_t riid, void **ppv);         // 0, 0x93603e: 0x8810 answers this (AddRef'd)
    bool IsStyleSet(uint32_t mask);                         // 4, 0x935b92: jumps to 0x936a5c
    bool SetStyle(uint32_t mask, uint32_t on);              // 5, 0x9360a6: jumps to SetStyleBits
    bool SetOn();                                           // 6, 0x9366a7: SetStyleBits(1, 1)
    bool SetOff();                                          // 7, 0x93669d: SetStyleBits(1, 0)
    bool IsOn();                                            // 8 and 10, 0x936036: IsStyleSet(1)
    bool SetOnState(uint32_t on);                           // 9, 0x93620f: SetStyleBits(1, on)
    bool SetImage(uint32_t type, void *image, const int32_t *rect); // 12, 0x93648e
    bool SetType(uint32_t type);                            // 13, 0x9360d0: SetImage again with the full source rect
    int32_t GetImageWidth();                                // 14, 0x935cae
    int32_t GetImageHeight();                               // 15, 0x935ca1
    bool GetImageRect(int32_t *out);                        // 16, 0x935c8a
    bool SetImageResource(void *image);                     // 17, 0x9360ab: SetImage(type, image, null), redraw
    bool GetImage(void **out);                              // 18, 0x935b97
    bool SetFont(void *font);                               // 19, 0x936278
    bool SetFontID(uint32_t id);                            // 20, 0x93638e
    bool GetFontID(uint32_t *out);                          // 21, 0x935c11
    bool SetColorRGB(uint32_t index, uint32_t rgb);         // 22, 0x936354: through cIGZWin slot 143
    bool SetColor(uint32_t index, int32_t argb);            // 23, 0x93632c
    bool GetColorRGB(uint32_t index, uint32_t *out);        // 24, 0x935be3: through cIGZWin slot 144
    bool GetColor(uint32_t index, int32_t *out);            // 25, 0x935bc1
    bool ResetColor(uint32_t index);                        // 26, 0x93556d: SetColor(index, unset)
    bool SetAlignment(uint32_t alignment);                  // 27, 0x9361b1
    bool GetAlignment(uint32_t *out);                       // 28, 0x935bb0
    bool SetMargins(uint8_t left, uint8_t top, uint8_t right, uint8_t bottom); // 29, 0x9363f7
    uint32_t SetMargins2(uint32_t horizontal, uint32_t vertical);   // 30, 0x9363df: slot 29 (h, v, h, v), its eax
    bool GetMargins(uint32_t *left, uint32_t *top, uint32_t *right, uint32_t *bottom); // 31, 0x935c2a
    bool GetMargins2(uint32_t *left, uint32_t *top);        // 32, 0x935c63
    bool SizeToFit();                                       // 33, 0x93563a
    bool GetSizeLimits(int32_t *out);                       // 34, 0x935cbb: min w, min h, max w, max h
    bool SetTooltip(void *text);                            // 35, 0x9365f2 (a cIGZString, null clears)
    bool GetTooltip(void *out);                             // 36, 0x935f1c (copies into a cIGZString)
    bool ClearTooltip();                                    // 37, 0x935580
    bool SetTooltipFlags(uint32_t flags);                   // 38, 0x9365e3
    bool GetTooltipFlags(uint32_t *out);                    // 39, 0x935f0b
    bool SetTooltipOffset(int16_t x, int16_t y);            // 40, 0x9365c6
    bool GetTooltipOffset(int32_t *x, int32_t *y);          // 41, 0x935eec
    bool SetTooltipColor(uint32_t index, uint8_t r, uint8_t g, uint8_t b);   // 42, 0x93659d
    bool GetTooltipColor(uint32_t index, uint8_t *r, uint8_t *g, uint8_t *b); // 43, 0x935eb5
    uint32_t Flash(int32_t count, uint32_t interval);       // 44, 0x93567b: count ticks (-1 forever), ms; the timer's Start eax
    bool SetSound(uint32_t index, uint32_t sound);          // 45, 0x93647a
    uint32_t GetSound(uint32_t index);                      // 46, 0x935ea7
    uint32_t SetID(uint32_t id);                            // 47, 0x93644a: the cGZWin face's 0x1eeb80 (returns id)
    uint32_t GetID();                                       // 48, 0x935c82: the cGZWin face's id (+0x14)
    bool SetCaption(void *text);                            // 49, 0x93613c (a cIGZString); Layout's result
    void *GetCaption();                                     // 50, 0x935ba8: the cGZWin caption string (+0x64)
    bool NotifyToggled();                                   // 51, 0x93550a
    bool NotifyClicked();                                   // 52, 0x935541

    // Non-virtual members.
    bool SetStyleBits(uint32_t mask, uint32_t on);          // 0x936f94 (270 bytes, ret 8)
    bool HideTooltip();                                     // 0x936a6c (60 bytes)
    bool Layout(int32_t width, int32_t height);             // 0x936aa8 (1160 bytes, ret 8)
    void PlayEffect(uint32_t index);                         // 0x936f30 (100 bytes, ret 4)
    bool ShowTooltip();                                     // 0x9370a2 (274 bytes)
    void UpdateTextExtent();                                // 0x93723e (476 bytes)
    void PlaceText(const int32_t *rect);                    // 0x9371b4 (138 bytes, ret 4)
    bool DrawImage();                                       // 0x9366b1 (264 bytes)
    bool DrawBevel();                                       // 0x9367b9 (219 bytes)
    void DrawCaption();                                     // 0x936894 (417 bytes)
    // 0x936a35 (39 bytes, cdecl): the flash timer's callback; posts kTimerMessage to the button through the
    // window manager (cIGZWinMgr slot 12).
    static void __cdecl FlashTick(cGZWinBtn *self);

    void *Win() { return reinterpret_cast<uint8_t *>(this) + 4; }
    bool Style(uint32_t mask) const { return (mStyle & mask) != 0; } // 0x936a5c, inlined

    const void *mVTable;          // +0x000 cIGZWinBtn face
    uint8_t mWin[0xf8];           // +0x004 cGZWin (its vtable at +4); fields used: +0x14 id, +0x28 area, +0x38 draw
                                  //        rect, +0x60 notification target, +0x64 caption (cRZString), +0x88 text
                                  //        context, +0x8c draw context
    uint32_t mStyle;              // +0x0fc
    cRZString mName;              // +0x100 (not read by the class's own code)
    int16_t mTextX;               // +0x120 caption position in the draw rect
    int16_t mTextY;               // +0x122
    int16_t mTextWidth;           // +0x124 caption extent (UpdateTextExtent)
    int16_t mTextHeight;          // +0x126
    uint32_t mAlignment;          // +0x128 0 centre, 1 right, 2 left
    uint16_t mTextFormat;         // +0x12c from the alignment: 1, 0, 2
    uint8_t mMargin[4];           // +0x12e left, top, right, bottom
    RZUnknownSlot mFont;          // +0x134
    uint32_t mFontID;             // +0x138
    int32_t mColor[6];            // +0x13c text colour pairs: normal, mouse over, disabled; kUnsetColor = unset
    uint32_t mSound[3];           // +0x154
    uint32_t mType;               // +0x160
    uint32_t mImageMode;          // +0x164 0 none, 1 four states, 2 eight states (set by SetImage)
    int32_t mImageRect[4];        // +0x168 one state's source rect
    int32_t mImageX;              // +0x178 the current state's source left
    int32_t mImageOffsetX;        // +0x17c image position in the draw rect
    int32_t mImageOffsetY;        // +0x180
    RZUnknownSlot mImage;         // +0x184
    cRZString mTooltip;           // +0x188
    uint32_t mTooltipID;          // +0x1a8 the window manager's tooltip
    uint32_t mTooltipFlags;       // +0x1ac
    int16_t mTooltipX;            // +0x1b0
    int16_t mTooltipY;            // +0x1b2
    uint32_t mTooltipColor[3];    // +0x1b4
    int32_t mFlashCount;          // +0x1c0 flash ticks left (-1 forever); Layout shows the image's lit state on odd counts
    uint8_t mTimer[0x28];         // +0x1c4 cRZCallbackTimer (vtable 0xe61bb8)
};

// The cGZWin face of cGZWinBtn (this = object + 4): the cIGZWin slots cGZWinBtn overrides.
class cGZWinBtnWin {
public:
    cGZWinBtn *Btn() { return reinterpret_cast<cGZWinBtn *>(reinterpret_cast<uint8_t *>(this) - 4); }

    bool DoMessage(const uint32_t *message);                // 3, 0x9355bd
    bool Activate();                                        // 4, 0x935f35
    bool Teardown();                                        // 5, 0x936640
    bool SetArea(int32_t left, int32_t top, int32_t right, int32_t bottom); // 64, 0x936060: Layout's result
    uint32_t SetFlag(uint32_t mask, uint32_t on);           // 83, 0x93621d
    uint32_t SetFontStyle(uint32_t value);                  // 92, 0x936452: slot 49's eax
    bool Draw();                                            // 101, 0x935b5f
    bool OnKeyDown(uint32_t key, uint32_t modifiers);       // 151, 0x93571b (other keys: slot 166's al)
    uint32_t OnKeyUp(uint32_t key, uint32_t modifiers);     // 152, 0x935797: slot 166's eax
    bool OnMouseDown(uint32_t x, uint32_t y, uint32_t keys);   // 155, 0x9357ef
    bool OnMouseDownR(uint32_t x, uint32_t y, uint32_t keys);  // 156, 0x935883
    bool OnMouseUp(uint32_t x, uint32_t y, uint32_t keys);     // 157, 0x9359f1
    bool OnMouseUpR(uint32_t x, uint32_t y, uint32_t keys);    // 158, 0x935a9b
    bool OnMouseMove(uint32_t x, uint32_t y, uint32_t keys);   // 159, 0x93596f
    bool OnFocusChange(uint32_t lost, void *gained);           // 161, 0x9356e2
    bool OnMouseEnter(uint32_t keys);                          // 162, 0x935921
    bool OnMouseExit(uint32_t keys);                           // 163, 0x935950
};

static_assert(offsetof(cGZWinBtn, mStyle) == 0xfc);
static_assert(offsetof(cGZWinBtn, mName) == 0x100);
static_assert(offsetof(cGZWinBtn, mAlignment) == 0x128);
static_assert(offsetof(cGZWinBtn, mMargin) == 0x12e);
static_assert(offsetof(cGZWinBtn, mFont) == 0x134);
static_assert(offsetof(cGZWinBtn, mColor) == 0x13c);
static_assert(offsetof(cGZWinBtn, mSound) == 0x154);
static_assert(offsetof(cGZWinBtn, mType) == 0x160);
static_assert(offsetof(cGZWinBtn, mImage) == 0x184);
static_assert(offsetof(cGZWinBtn, mTooltip) == 0x188);
static_assert(offsetof(cGZWinBtn, mTooltipX) == 0x1b0);
static_assert(offsetof(cGZWinBtn, mFlashCount) == 0x1c0);
static_assert(offsetof(cGZWinBtn, mTimer) == 0x1c4);
static_assert(sizeof(cGZWinBtn) == cGZWinBtn::kSize);
