#pragma once
// The window classes' handlers of cGZLegacyControl (lane MF, foundations F6 part 2) and the helpers they share.
// The registrations (src/gz/cGZLegacyControlRegistrations.h) store a handler per window class in the handlers table;
// Create (src/gz/cGZLegacyControl.size.cpp) calls it with `apply` true after making the window and with false after
// placing it (a LegacyHandler: cdecl; the result is not read, ours answers true). A handler asks the window for the
// class's face (QueryInterface, slot 0) and, with one, reads the class's attributes through the readers of
// src/gz/cGZLegacyControl.h and sets them on the face; the face is released at the end.
#include "gz/cGZLegacyControl.h"
#include "gz/cGZPersistResourceManager.h" // cGZPersistResourceKey

// ---- attribute ids shared by the classes (the vocabulary names of RegisterCommonNames 0x85e431) -------------------------
namespace legacyattr {
constexpr uint32_t kFont = 0xf000;      // font: a uint32 font id, else a text (a font name)
constexpr uint32_t kBackColor = 0xf001; // bkgcolor: "(r,g,b)"
constexpr uint32_t kForeColor = 0xf002; // forecolor: "(r,g,b)"
constexpr uint32_t kNotify = 0xf003;    // notify
constexpr uint32_t kGutters = 0xf004;   // gutters: "(x,y)"
constexpr uint32_t kAlign = 0xf015;     // align: a uint32 (0xf008 is `center`)
constexpr uint32_t kAlignCenterWord = 0xf008;
constexpr uint32_t kStyle = 0xf005;     // style: a uint32 naming one style word per attribute (repeated)
constexpr uint32_t kImage = 0xf016;     // image: text "{group,instance}" (hex)
constexpr uint32_t kImageRect = 0xf018; // imagerect: text "(l,t,r,b)"

// The style words (values of a `style` attribute; the vocabulary names of RegisterCommonNames).
constexpr uint32_t kStyleLeft = 0xf006;
constexpr uint32_t kStyleRight = 0xf007;
constexpr uint32_t kStyleFlat = 0xf009;
constexpr uint32_t kStyleSunken = 0xf00b;
constexpr uint32_t kStyleRaised = 0xf00c;
constexpr uint32_t kStyleNoFill = 0xf00e;
constexpr uint32_t kStyleFill = 0xf00d;
} // namespace legacyattr

// An image resource the legacy controls load (resource type 0x856ddbac): counted; slots 9 and 10 give its size.
class cIGZLegacyImage : public cIGZUnknown {
public:
    virtual void vf3_cIGZLegacyImage() = 0;
    virtual void vf4_cIGZLegacyImage() = 0;
    virtual void vf5_cIGZLegacyImage() = 0;
    virtual void vf6_cIGZLegacyImage() = 0;
    virtual void vf7_cIGZLegacyImage() = 0;
    virtual void vf8_cIGZLegacyImage() = 0;
    virtual int32_t GetWidth() = 0;  // 9
    virtual int32_t GetHeight() = 0; // 10
    virtual void vf11_cIGZLegacyImage() = 0;
    virtual const int32_t *GetRect() = 0; // 12: the image's own rectangle (four words)
};

// The resource manager's image resource (interface 0x06f1568e): slot 3 gives the counted image.
class cIGZLegacyImageResource : public cIGZUnknown {
public:
    virtual cIGZLegacyImage *GetImage() = 0; // 3
};

// The bitmap window class's face (GZWinBMP, cGZWinBMP's +0xf8 vtable 0xe54460, 12 slots).
class cIGZWinBitmap : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0xc12cea13;
    static constexpr uint32_t kFlagNotify = 0x1;         // flag bit of the notify attribute
    static constexpr uint32_t kFlagTransparentBack = 0x2; // transparentbkg
    static constexpr uint32_t kFlagEdgeImage = 0x8;      // edgeimage
    static constexpr uint32_t kFlagImageRect = 0x10;     // set when an image rectangle was given
    virtual void *GetWindow() = 0;                       // 3 (folded 0x5b8509: the object less 0xf8)
    virtual void SetImage(cIGZLegacyImage *image) = 0;   // 4 (0x937898: counts the new image, releases the old)
    virtual void vf5_cIGZWinBitmap() = 0;
    virtual void SetAlpha(bool enabled, float alpha) = 0; // 6 (0x937870: byte at +8, float at +0xc, then redraws)
    virtual void GetAlpha(bool *enabled, float *alpha) = 0; // 7 (the byte at +8, the float at +0xc)
    virtual void SetImageRect(const int32_t *rect) = 0; // 8 (0x9378e6: four words at +0x10, clipped to the image)
    virtual void GetImageRect(int32_t *rect) = 0;        // 9 (the four words at +0x10)
    virtual bool IsFlagSet(uint32_t mask) = 0;           // 10 (folded 0x9377ae)
    virtual void SetFlag(uint32_t mask, bool on) = 0;   // 11 (0x9379c0: the flag word at +0x20)
};

// The flat rectangle window class's face (GZWinFlatRect, iid 0xc2afa76f, cGZWinFlatRect's +0xf8 vtable 0xe54dd0).
class cIGZWinFlatRect : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0xc2afa76f;
    static constexpr uint32_t kBorderLeft = 0x1;        // the style bits slot 12 takes: the borders drawn
    static constexpr uint32_t kBorderTop = 0x2;
    static constexpr uint32_t kBorderRight = 0x4;
    static constexpr uint32_t kBorderBottom = 0x8;
    static constexpr uint32_t kRaised = 0x10;
    static constexpr uint32_t kSunken = 0x20;
    static constexpr uint32_t kNoFill = 0x40;
    virtual void *GetWindow() = 0; // 3 (folded 0x5b8509: the object less 0xf8)
    virtual void vf4_cIGZWinFlatRect(uint32_t) = 0;
    virtual void vf5_cIGZWinFlatRect(uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf6_cIGZWinFlatRect() = 0;
    virtual void vf7_cIGZWinFlatRect(uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf8_cIGZWinFlatRect(uint32_t) = 0;
    virtual void vf9_cIGZWinFlatRect(uint32_t, uint32_t, uint32_t) = 0;
    // 10 (0x93a857): colour `index` (0 left, 1 top, 2 right, 3 bottom; any other ignored) = opaque (r, g, b); the
    // bytes are read from the low byte of each word.
    virtual void SetColor(uint32_t index, uint8_t red, uint8_t green, uint8_t blue) = 0;
    // 11 (0x93a6d2): colour `index` (0..3 as SetColor's) into the three bytes; any other index leaves them.
    virtual void GetColor(uint32_t index, uint8_t *red, uint8_t *green, uint8_t *blue) = 0;
    virtual void SetStyle(uint32_t bits) = 0; // 12 (0x93aa2d)
    virtual uint8_t GetStyle() = 0;           // 13 (0x93a7e8): the style bits
};

// The text window class's face (GZWinText, iid 0x212cdc1f, cGZWinText's +0xf8 vtable 0xe55fac, 29 slots).
class cIGZWinText : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x212cdc1f;
    static constexpr uint32_t kFlagNotify = 0x1;  // slot 7's masks: notify, wrapped, opaque
    static constexpr uint32_t kFlagWrapped = 0x2;
    static constexpr uint32_t kFlagOpaque = 0x4;
    static constexpr uint32_t kAlignCenter = 0x4;  // an align attribute of `center` (0xf008) is stored as 4
    virtual void *GetWindow() = 0; // 3 (folded 0x5b8509: the object less 0xf8)
    virtual void vf4_cIGZWinText(uint32_t) = 0;
    virtual void FitToText(int32_t width, int32_t height) = 0; // 5: called (0, 0) when the autofit attribute is there
    virtual bool IsFlagSet(uint32_t mask) = 0;                 // 6
    virtual void SetFlag(uint32_t mask, bool on) = 0;          // 7
    virtual void vf8_cIGZWinText(uint32_t, uint32_t) = 0;
    virtual void SetColor(uint32_t which, uint32_t colour) = 0; // 9: 0 the text colour, 1 the background (native colours)
    virtual void vf10_cIGZWinText(uint32_t) = 0;
    virtual void vf11_cIGZWinText(uint32_t, uint32_t) = 0;
    virtual bool GetColor(uint32_t which, uint32_t *colour) = 0; // 12: the native colour, false without one
    virtual void vf13_cIGZWinText(uint32_t) = 0;
    virtual void vf14_cIGZWinText(uint32_t) = 0;
    virtual void vf15_cIGZWinText() = 0;
    virtual void vf16_cIGZWinText(uint32_t) = 0;
    virtual void vf17_cIGZWinText() = 0;
    virtual void vf18_cIGZWinText(uint32_t) = 0;
    virtual void SetFont(uint32_t font) = 0;                   // 19
    virtual bool GetFont(uint32_t *font) = 0;                  // 20
    virtual void SetAlign(uint32_t align) = 0;                 // 21
    virtual bool GetAlign(uint32_t *align) = 0;                // 22
    virtual void SetGutters(int32_t x, int32_t y) = 0;         // 23
    virtual bool GetGutters(int8_t *x, int8_t *y) = 0;         // 24: two signed bytes
    virtual void SetTextOffsets(int32_t x, int32_t y) = 0;     // 25
    virtual bool GetTextOffsets(int8_t *x, int8_t *y) = 0;     // 26: two signed bytes
};

// The slider window class's face (GZWinSlider, iid 0x21325207).
class cIGZWinSlider : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x21325207;
    virtual void *GetWindow() = 0;            // 3
    virtual int32_t GetValue() = 0;           // 4
    virtual void SetValue(int32_t value) = 0; // 5
    virtual void SetMin(int32_t value) = 0;   // 6
    virtual int32_t GetMin() = 0;             // 7
    virtual void SetMax(int32_t value) = 0;   // 8
    virtual int32_t GetMax() = 0;             // 9
    virtual void SetDirection(uint32_t direction) = 0; // 10
    virtual void GetDirection(uint32_t *direction) = 0; // 11: 1 horizontal, 2 vertical (the writer ignores the answer)
    virtual void SetImage(cIGZLegacyImage *image, const int32_t *rect) = 0; // 12: rect null for the whole image
};

// The scroll bar window class's face (GZWinScrollbar, iid 0x61325a2d).
class cIGZWinScrollbar : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x61325a2d;
    static constexpr uint32_t kHorizontal = 1; // the directions the script names
    static constexpr uint32_t kVertical = 2;
    virtual void *GetWindow() = 0; // 3
    virtual void vf4_cIGZWinScrollbar() = 0;
    virtual void SetValue(int32_t value) = 0;                 // 5
    virtual int32_t GetValue() = 0;                           // 6
    virtual void SetRange(int32_t low, int32_t high) = 0;     // 7
    virtual bool GetRange(int32_t *low, int32_t *high) = 0;   // 8
    virtual void SetDirection(uint32_t direction) = 0;        // 9
    virtual uint32_t GetDirection() = 0;                      // 10
    virtual void SetPageSize(uint32_t size) = 0;              // 11
    virtual uint32_t GetPageSize() = 0;                       // 12
    virtual void SetLineSize(uint32_t size) = 0;              // 13
    virtual uint32_t GetLineSize() = 0;                       // 14
    virtual void SetLinePageCount(int32_t lines, int32_t pages) = 0; // 15
    virtual void GetLinePageCount(int32_t *lines, int32_t *pages) = 0; // 16
    virtual void SetImage(const int32_t *rect, cIGZLegacyImage *image, int32_t flag) = 0; // 17
};

// The outline window class's face (GZWinOutline, iid 0xe303e5ae, cGZWinOutline's vtable 0xe58760, 19 slots).
class cIGZWinOutline : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0xe303e5ae;
    static constexpr uint32_t kStyleFlat = 0x1;   // the style bits slot 3 takes (the word is kept at +0xfc)
    static constexpr uint32_t kStyleSunken = 0x2;
    static constexpr uint32_t kStyleRaised = 0x4;
    static constexpr uint32_t kStyleFill = 0x20;
    virtual void SetStyle(uint32_t bits) = 0; // 3
    virtual uint32_t GetStyle() = 0; // 4: SetStyle's bits
    // 5 (0x95fbdf): colour `index` (1 dark, 2 light; 0 goes to the window's own) = opaque (r, g, b), from the low bytes.
    virtual void SetColor(uint32_t index, uint8_t red, uint8_t green, uint8_t blue) = 0;
    // 6: colour `index` (as SetColor's) into the three bytes.
    virtual void GetColor(uint32_t index, uint8_t *red, uint8_t *green, uint8_t *blue) = 0;
    virtual void vf7_cIGZWinOutline() = 0;
    virtual void vf8_cIGZWinOutline(uint32_t) = 0;
    virtual void vf9_cIGZWinOutline(uint32_t) = 0;
    virtual void vf10_cIGZWinOutline() = 0;
    virtual void vf11_cIGZWinOutline() = 0;
    virtual void vf12_cIGZWinOutline(uint32_t) = 0;
    // 13 (0x95fcdf): the outline's nodes, `count` (x, y) pairs; the two flags are 1 in the only caller.
    virtual void SetNodes(uint32_t count, const int32_t *pairs, uint32_t flagA, uint32_t flagB) = 0;
    virtual void vf14_cIGZWinOutline(uint32_t) = 0;
    virtual void vf15_cIGZWinOutline(uint32_t) = 0;
    virtual uint32_t GetNodeCount() = 0;        // 16: the number of (x, y) pairs
    virtual bool GetNodes(int32_t *pairs) = 0; // 17: copies GetNodeCount's pairs; false when it cannot
};

// The scroll bar window class 2's face (GZWinScrollbar2, iid 0x4b42da53).
class cIGZWinScrollbar2 : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x4b42da53;
    virtual void *GetWindow() = 0; // 3
    virtual void SetVertical(bool vertical) = 0;         // 4
    virtual void GetVertical(uint32_t *vertical) = 0;    // 5: the writer reads a whole word, 0 horizontal
    virtual void SetValue(int32_t value, int32_t flag) = 0;       // 6 (flag 0)
    virtual bool GetValue(int32_t *value) = 0; // 7: written when true
    virtual void vf8_cIGZWinScrollbar2() = 0;
    virtual void SetMinValue(int32_t value, int32_t flag) = 0;    // 9 (flag 1)
    virtual bool GetMinValue(int32_t *value) = 0; // 10: written when true
    virtual void SetMaxValue(int32_t value, int32_t flag) = 0;    // 11 (flag 1)
    virtual bool GetMaxValue(int32_t *value) = 0; // 12: written when true
    virtual void SetVisibleRange(int32_t value, int32_t flag) = 0; // 13 (flag 1)
    virtual bool GetVisibleRange(int32_t *value) = 0; // 14: written when true
    virtual void SetPageOverlap(int32_t value) = 0;       // 15
    virtual bool GetPageOverlap(int32_t *value) = 0; // 16: written when true
    virtual void SetIncrement(int32_t value) = 0;         // 17
    virtual bool GetIncrement(int32_t *value) = 0; // 18: written when true
    virtual void SetArrowsImage(cIGZLegacyImage *image) = 0;    // 19 (null clears)
    virtual void vf20_cIGZWinScrollbar2() = 0;
    virtual void SetContainerImage(cIGZLegacyImage *image) = 0; // 21
    virtual void vf22_cIGZWinScrollbar2() = 0;
    virtual void SetThumbImage(cIGZLegacyImage *image) = 0;     // 23
};

// The folders window class's face (GZWinFolders, iid 0xa13351b8).
class cIGZWinFolders : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0xa13351b8;
    virtual void *GetWindow() = 0; // 3
    virtual void AddElement(const cRZString *text, int32_t flag) = 0; // 4 (flag 0)
    virtual void vf5_cIGZWinFolders(uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf6_cIGZWinFolders(uint32_t) = 0;
    virtual void vf7_cIGZWinFolders(uint32_t) = 0;
    virtual void vf8_cIGZWinFolders(uint32_t, uint32_t) = 0;
    virtual void ClearElements() = 0;                                 // 9
    virtual void vf10_cIGZWinFolders(uint32_t) = 0;
    virtual void vf11_cIGZWinFolders(uint32_t) = 0;
    virtual void vf12_cIGZWinFolders(uint32_t) = 0;
    virtual bool GetFont(uint32_t *font) = 0;                         // 13: the font id
    virtual void SetFont(uint32_t font) = 0;                          // 14
    virtual void SetImage(cIGZLegacyImage *image) = 0;                // 15
    virtual void vf16_cIGZWinFolders() = 0;
    virtual void vf17_cIGZWinFolders() = 0;
    virtual void vf18_cIGZWinFolders(uint32_t) = 0;
    virtual void vf19_cIGZWinFolders(uint32_t, uint32_t, uint32_t) = 0;
    virtual bool GetElement(int32_t index, cIGZString *text) = 0;    // 20: element `index`'s text; false past the last
};

// The combo box window class's face (GZWinCombo, iid 0x412ce496).
class cIGZWinCombo : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x412ce496;
    virtual void *GetWindow() = 0;                                          // 3
    virtual void vf4_cIGZWinCombo() = 0;
    virtual void vf5_cIGZWinCombo() = 0;
    virtual void vf6_cIGZWinCombo(uint32_t) = 0;
    virtual void vf7_cIGZWinCombo(uint32_t) = 0;
    virtual void vf8_cIGZWinCombo(uint32_t) = 0;
    virtual void vf9_cIGZWinCombo(uint32_t) = 0;
    virtual void vf10_cIGZWinCombo() = 0;
    virtual void vf11_cIGZWinCombo(uint32_t) = 0;
    virtual bool GetFont(uint32_t *font) = 0;                               // 12
    virtual void SetFont(uint32_t font) = 0;                                // 13
    virtual void vf14_cIGZWinCombo(uint32_t, uint32_t) = 0;
    virtual void vf15_cIGZWinCombo(uint32_t, uint32_t) = 0;
    virtual void vf16_cIGZWinCombo(uint32_t) = 0;
    virtual bool IsEditable() = 0;                                          // 17
    virtual void SetEditable(bool editable) = 0;                            // 18
    virtual void vf19_cIGZWinCombo(uint32_t) = 0;
    virtual void vf20_cIGZWinCombo(uint32_t) = 0;
    virtual void vf21_cIGZWinCombo(uint32_t, uint32_t) = 0;
    virtual void vf22_cIGZWinCombo() = 0;
    virtual void AddElement(const cRZString *text, int32_t position, int32_t flag) = 0; // 23 (-2, 0)
    virtual void SetInitialSelection(uint32_t index, int32_t flag) = 0;     // 24 (flag 1)
    virtual void vf25_cIGZWinCombo(uint32_t, uint32_t) = 0;
    virtual int32_t GetInitialSelection() = 0;                              // 26
    virtual void vf27_cIGZWinCombo(uint32_t, uint32_t, uint32_t) = 0;
    virtual bool GetElement(int32_t index, cIGZString *text) = 0;           // 28
    virtual void vf29_cIGZWinCombo(uint32_t, uint32_t) = 0;
    virtual void vf30_cIGZWinCombo(uint32_t, uint32_t) = 0;
    virtual void vf31_cIGZWinCombo(uint32_t) = 0;
    virtual void SetArrowImage(cIGZLegacyImage *image, const int32_t *rect) = 0; // 32 (rect or null)
    virtual bool GetArrowImage(cIGZLegacyImage **image, int32_t *rect) = 0; // 33 (rect: left, top, right, bottom)
    virtual void SetArrowColor(uint32_t color) = 0;                         // 34
    virtual bool GetArrowColor(uint32_t *color) = 0;                        // 35
    virtual void SetButtonGutter(uint32_t gutter) = 0;                      // 36
    virtual bool GetButtonGutter(int8_t *gutter) = 0;                       // 37
    virtual void SetGutters(int32_t x, int32_t y) = 0;                      // 38
    virtual bool GetGutters(int8_t *x, int8_t *y) = 0;                      // 39
    virtual void vf40_cIGZWinCombo(uint32_t) = 0;
    virtual void SetColor(uint32_t index, uint32_t color) = 0;              // 41
    virtual bool GetColor(uint32_t index, uint32_t *color) = 0;             // 42
    virtual void vf43_cIGZWinCombo(uint32_t) = 0;
    virtual void vf44_cIGZWinCombo(uint32_t) = 0;
    virtual void SetHighlightColor(uint32_t color) = 0;                     // 45
    virtual bool GetHighlightColor(uint32_t *color) = 0;                    // 46
    virtual void vf47_cIGZWinCombo(uint32_t) = 0;
    virtual void vf48_cIGZWinCombo(uint32_t) = 0;
    virtual void SetOutlineColor(uint32_t color) = 0;                       // 49
    virtual bool GetOutlineColor(uint32_t *color) = 0;                      // 50
};

// ---- helpers ------------------------------------------------------------------------------------------------------------

// 0x863f55 (cdecl): as FindUint32Attribute (the value must be a uint32 variant, type 6), the value read as a signed
// number (slot 80, AsSint32Ref).
bool __cdecl FindSint32Attribute(const cGZScriptAttributes *attributes, uint32_t id, int32_t *out);
// 0x863ee9 (cdecl): the attribute's text read as "(%d,%d,%d,%d)"; true when all four numbers were read (each read one
// is written, in order).
bool __cdecl FindRectAttribute(const cGZScriptAttributes *attributes, uint32_t id, int32_t *rect);
// 0x86dc44 (cdecl): the image resource {0x856ddbac, group, instance, 0} as a counted image (the caller releases it):
// through the UI script service's context (slot 3) when there is one or, without, the resource manager's resource
// 0x06f1568e (its slot 3 gives the image); null when not found.
cIGZLegacyImage *__cdecl LoadImageResource(uint32_t group, uint32_t instance);
// 0x86dbb5 (cdecl): the same for an image resource key as given (any type; LoadImageResource builds {0x856ddbac, group,
// instance, 0} and does this).
cIGZLegacyImage *__cdecl LoadImageByKey(const cGZPersistResourceKey *key);
// 0x86df76 (cdecl): sets the window's property 0xdaa3b860 + index to a new untyped 8-byte variant of (group, instance)
// (not released); true.
bool __cdecl SetImagePairProperty(ifaces::cIGZWin *window, uint32_t group, uint32_t instance, int32_t index);

// 0x86df41 (cdecl): sets the window's property 0xfaa3be85 + index to a new uint32 variant of `value`; true.
bool __cdecl SetUint32WindowProperty(ifaces::cIGZWin *window, uint32_t value, int32_t index);
// 0x86df0c (cdecl): sets the window's property 0xfaa3be85 + index to a new text variant of `text`; true.
bool __cdecl SetTextWindowProperty(ifaces::cIGZWin *window, const cRZString *text, int32_t index);

// ---- the handlers (LegacyHandler) ---------------------------------------------------------------------------------------

// 0x864376, GZWinBMP (type 0xc12cea13): apply sets the alpha (0xb00, clamped to 255), the notify / transparentbkg /
// edgeimage flags (0xf003, 0xb01, 0xb02), loads the image of the image attribute (0xf016), hands it to the face, sizes
// the window to it and keeps its key as properties; the second pass sets the image rectangle (0xf018) and its flag.
bool __cdecl ApplyBitmapProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply);

// 0x8674f6, GZWinFlatRect (type 0xc2afa76f): apply ORs the border / look bits of every uint32 `style` attribute (0xf005:
// left, top, right, bottom, all, lrb, trb, sunken, raised, nofill; others add nothing) into the face's style (always
// set, even 0), then sets the colour of each side whose attribute (0x200..0x203) reads as "(r,g,b)". The second pass
// does nothing.
bool __cdecl ApplyFlatRectProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply);

// 0x86b2be, GZWinText (type 0x212cdc1f): apply sets the notify / wrapped (0x300) / opaque (0x301) flags (opaque off also
// sets window flag 0x20000), the font (0xf000: a uint32 id, else a name text; also kept as the window's font property),
// the text and background colours (0xf002, 0xf001: through the graphics system's native colour), the gutters (0xf004,
// (0, 0) without) and the text offsets (0x305); the second pass sets the alignment (0xf015). Either pass then sets the
// window's property 0xdacf8f9c to whether `autofit` (0x304) is there and, if it is, calls the face's FitToText(0, 0).
bool __cdecl ApplyTextProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply);

// 0x86aa7f, GZWinSlider (type 0x21325208): apply sets the range (0xc00 "minmaxvalue", a pair, ordered low to high: the
// maximum then the minimum), the direction (0xc01: 0xc03 vertical is 2, any other value 1), and the image of the image
// attribute (0xf016; its rectangle 0xf018 or none), keeping its key as properties; the second pass sets the value from
// the initvalue attribute (0x40b, read through FindSint32Attribute).
bool __cdecl ApplySliderProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply);

// 0x86a820, GZWinScrollbar (type 0x61325a2e): apply sets the range (0xc00, ordered), the direction (0xc01 as the
// slider's), the page size (0xd00), the line size (0xd01), the line / page counts (0xd02, a pair) and the image (0xf016,
// with the rectangle of 0xf018 else the image's own); the second pass sets the value (0x40b).
bool __cdecl ApplyScrollbarProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply);

// 0x86a2a5, GZWinOutline (type 0x4303e5b6): apply ORs the look bits of every uint32 `style` attribute (0xf005: flat 1,
// sunken 2, raised 4, fill 0x20; others add nothing) into the face's style (only when not 0), sets the dark and light
// colours (0x1100, 0x1101: "(r,g,b)") and hands the face every `node` text (0x1102, up to 32, each "(x,y)"; a text that
// does not hold two numbers is not tested: the original leaves the numbers of the previous read) as one array. The second
// pass does nothing.
bool __cdecl ApplyOutlineProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply);

// 0x86a4a8, GZWinScrollbar2 (type 0x4b42da5d): apply sets from the attributes 0x1700 orientation (vertical 0xc03 is
// true), 0x1701 value, 0x1702 minvalue, 0x1703 maxvalue, 0x1704 visiblerange, 0x1705 pageoverlap, 0x1706 increment (the
// numbers through FindSint32Attribute) and the three images 0x1707 arrowsimage, 0x1708 containerimage, 0x1709 thumbimage
// ("{type,group,instance}", all three hex numbers needed: the image is loaded by that key and the key kept as a property
// 0xbfa11d7 / 0x6bfa11e1 / 0xbfa11e2; with no such attribute the face's image is cleared). The second pass does nothing.
bool __cdecl ApplyScrollbar2Properties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply);

// 0x8676ea, GZWinFolders (type 0x78e8055): apply sets the font (0xf000 as a number: the face's and the window's font
// property; else as a text: the window's property only) and the image (0xf016 "{group,instance}": loaded, set on the face,
// its key kept as the window's property 0x2b4e9920 and the pair as 0xdaa3b860; the parse is not checked, so a text with
// fewer than two numbers is not tested: the original leaves uninitialised stack). The second pass clears the face's
// elements and adds every `listelement` (0x502) text.
bool __cdecl ApplyFoldersProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply);

// 0x865043, GZWinCombo (type 0x59b): apply sets the font (0xf000, as GZWinFolders' ApplyFoldersProperties), the arrow image
// 0x800 ("{group,instance}", both numbers needed; the rectangle 0x801 or none goes with it; the pair kept as the property
// 0xdaa3b860 even when no image is found), the button gutter 0x803 and the gutters 0xf004. The second pass sets the colours
// 0xf00f / 0xf010 / 0xf011 (indexes 0 / 1 / 2), 0x408 highlight, 0x40c outline, 0x802 arrow (each "(r,g,b)" converted to the
// graphics system's colour), the editable flag 0x400 (== 1), adds every 0x502 text (the value is used without a null check)
// and sets the initial selection 0x501.
bool __cdecl ApplyComboProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply);
// The list box window class's face (GZWinListBox, iid 0x4132242b).
class cIGZWinListBox : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x4132242b;
    virtual void *GetWindow() = 0; // 3
    virtual void vf4_cIGZWinListBox(uint32_t, uint32_t) = 0;
    virtual void vf5_cIGZWinListBox(uint32_t) = 0;
    virtual void vf6_cIGZWinListBox() = 0;
    virtual void AddElement(const cRZString *text, int32_t position, int32_t flag) = 0; // 7 (-2, 0)
    virtual void vf8_cIGZWinListBox(uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf9_cIGZWinListBox(uint32_t, uint32_t) = 0;
    virtual void FitStep1() = 0; // 10 autofit: slots 10, 48, 49 in a row
    virtual void SetInitialSelection(uint32_t index, int32_t flag) = 0; // 11 (flag 1)
    virtual void vf12_cIGZWinListBox(uint32_t, uint32_t) = 0;
    virtual int32_t GetInitialSelection() = 0; // 13
    virtual void vf14_cIGZWinListBox() = 0;
    virtual void SortStep1() = 0; // 15 sort == 1: 15 then 17
    virtual bool IsSorted() = 0; // 16
    virtual void SortStep2() = 0; // 17
    virtual void SetDrop(bool drop) = 0; // 18
    virtual bool IsDrop() = 0; // 19
    virtual void vf20_cIGZWinListBox(uint32_t) = 0;
    virtual void vf21_cIGZWinListBox() = 0;
    virtual void vf22_cIGZWinListBox() = 0;
    virtual void vf23_cIGZWinListBox() = 0;
    virtual void vf24_cIGZWinListBox() = 0;
    virtual void vf25_cIGZWinListBox() = 0;
    virtual void vf26_cIGZWinListBox() = 0;
    virtual void vf27_cIGZWinListBox(uint32_t) = 0;
    virtual void SetLineVisible(uint32_t value) = 0; // 28
    virtual void vf29_cIGZWinListBox(uint32_t) = 0;
    virtual void SetLineOnTop(uint32_t value) = 0; // 30
    virtual int32_t GetElementCount() = 0; // 31
    virtual void vf32_cIGZWinListBox() = 0;
    virtual void vf33_cIGZWinListBox() = 0;
    virtual void vf34_cIGZWinListBox() = 0;
    virtual void vf35_cIGZWinListBox(uint32_t, uint32_t, uint32_t) = 0;
    virtual void SetScrollbars(bool vertical, bool horizontal) = 0; // 36
    virtual bool GetScrollbars(int8_t *vertical, int8_t *horizontal) = 0; // 37
    virtual void vf38_cIGZWinListBox(uint32_t) = 0;
    virtual void SetFont(uint32_t font) = 0; // 39
    virtual bool GetFont(uint32_t *font) = 0; // 40
    virtual void vf41_cIGZWinListBox(uint32_t) = 0;
    virtual void SetColor(uint32_t index, uint32_t color) = 0; // 42
    virtual bool GetColor(uint32_t index, uint32_t *color) = 0; // 43
    virtual void vf44_cIGZWinListBox(uint32_t) = 0;
    virtual void vf45_cIGZWinListBox(uint32_t) = 0;
    virtual void vf46_cIGZWinListBox() = 0;
    virtual void vf47_cIGZWinListBox(uint32_t) = 0;
    virtual void FitStep2() = 0; // 48
    virtual void FitStep3() = 0; // 49
    virtual bool GetElement(int32_t index, cIGZString *text, int32_t flag) = 0; // 50 (flag 0)
    virtual void vf51_cIGZWinListBox(uint32_t, uint32_t) = 0;
    virtual void SetOutline(bool outline) = 0; // 52
    virtual bool IsOutline() = 0; // 53
    virtual void SetSunken(bool sunken) = 0; // 54
    virtual bool IsSunken() = 0; // 55
    virtual void SetScrollbarGutters(int32_t x, int32_t y) = 0; // 56
    virtual bool GetScrollbarGutters(int8_t *x, int8_t *y) = 0; // 57
    virtual void SetGutters(int32_t x, int32_t y) = 0; // 58
    virtual bool GetGutters(int8_t *x, int8_t *y) = 0; // 59
    virtual void vf60_cIGZWinListBox(uint32_t) = 0;
    virtual void vf61_cIGZWinListBox(uint32_t, uint32_t) = 0;
    virtual void vf62_cIGZWinListBox(uint32_t, uint32_t) = 0;
    virtual void vf63_cIGZWinListBox(uint32_t) = 0;
    virtual void vf64_cIGZWinListBox() = 0;
    virtual void vf65_cIGZWinListBox(uint32_t, uint32_t) = 0;
    virtual void vf66_cIGZWinListBox(uint32_t, uint32_t) = 0;
    virtual void vf67_cIGZWinListBox(uint32_t) = 0;
    virtual void SetHighlightColor(uint32_t color) = 0; // 68
    virtual bool GetHighlightColor(uint32_t *color) = 0; // 69
    virtual void SetOutlineColors(uint32_t first, uint32_t second) = 0; // 70
    virtual bool GetOutlineColors(uint32_t *first, uint32_t *second) = 0; // 71
};

// 0x868e93, GZWinListBox (type 0x598): apply sets the scroll bars (0x402 vscrollbar, default on; 0x401 hscrollbar, default off;
// each == 1), the font (0xf000, as GZWinFolders), the colours 0xf00f / 0xf010 / 0xf011 (indexes 0 / 1 / 2), 0x408 highlight,
// 0x40c outline colours ("(r,g,b)(r,g,b)": all six numbers needed), 0xf019 outline and 0xf00b sunken (each == 1), the pairs
// 0x500 scrollbar gutters and 0xf004 gutters, and 0x504 drop (== 1). The second pass adds every 0x502 text, calls the sort
// slots when 0x503 sort is 1, sets the initial selection 0x501, the line flags 0x505 and 0x506 (as given) and, by 0x304
// autofit, the window property 0xdacf8f9c (true with the three fit slots, else false).
bool __cdecl ApplyListBoxProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply);

// The line input window class's face (GZWinLineInput, iid 0x21335c59).
class cIGZWinLineInput : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x21335c59;
    virtual void *GetWindow() = 0; // 3
    virtual void SetFont(uint32_t font) = 0; // 4
    virtual void SetColor(uint32_t index, uint32_t color) = 0; // 5
    virtual void vf6_cIGZWinLineInput(uint32_t) = 0;
    virtual bool GetColor(uint32_t index, uint32_t *color) = 0; // 7 (0..2)
    virtual void vf8_cIGZWinLineInput(uint32_t) = 0;
    virtual void SetFilled(bool filled) = 0; // 9
    virtual bool IsFilled() = 0; // 10
    virtual void SetOutlineStyle(uint32_t style) = 0; // 11 (1 flat, 2 outline3d, 3 outline3dex)
    virtual void GetOutlineStyle(uint32_t *style) = 0; // 12
    virtual void vf13_cIGZWinLineInput(uint32_t) = 0;
    virtual void SetBorderColor(uint32_t index, uint32_t color) = 0; // 14 (0..3)
    virtual bool GetBorderColor(uint32_t index, uint32_t *color) = 0; // 15 (0..3)
    virtual void vf16_cIGZWinLineInput(uint32_t) = 0;
    virtual void SetHighlightFillColor(uint32_t color) = 0; // 17
    virtual bool GetHighlightFillColor(uint32_t *color) = 0; // 18
    virtual void SetHighlightFillAlpha(float alpha) = 0; // 19
    virtual void GetHighlightFillAlpha(float *alpha) = 0; // 20
    virtual void vf21_cIGZWinLineInput(uint32_t) = 0;
    virtual void vf22_cIGZWinLineInput(uint32_t) = 0;
    virtual void vf23_cIGZWinLineInput(uint32_t) = 0;
    virtual void vf24_cIGZWinLineInput() = 0;
    virtual void SetCaretPosition(uint32_t position) = 0; // 25
    virtual bool GetCaretPosition(int32_t *position) = 0; // 26
    virtual bool IsEditable() = 0; // 27
    virtual void SetEditable(bool editable) = 0; // 28
    virtual void SetCharLimit(uint32_t limit) = 0; // 29
    virtual bool GetCharLimit(int32_t *limit) = 0; // 30
    virtual void SetSelection(int32_t start, int32_t length) = 0; // 31
    virtual bool GetSelection(int32_t *start, int32_t *length) = 0; // 32 (the length is written as a count)
    virtual void SetGutterRect(int32_t a, int32_t b, int32_t c, int32_t d) = 0; // 33
    virtual void GetGutterRect(int8_t *a, int8_t *b, int8_t *c, int8_t *d) = 0; // 34 (the answer is not used)
};

// 0x8688a3, GZWinLineInput (type 0x21335c5a): apply sets the font (0xf000 uint32: the UI defaults service's font for it (its slot 5)
// on the face and SetUint32WindowProperty(window, v, 0); a text: SetTextWindowProperty; none: the default font "main9" looked up
// by name (service slot 38, then slot 5)), the colours 0xf00f / 0xf010 / 0xf011 (0xf00f..), 0xe03..0xe06 (border colours 0..3),
// 0xe07 (highlight fill), 0xe08 (highlight alpha, at most 255, as a float over 255), 0xe00 filled, 0xf005 style (0xe01 -> 2,
// 0xe02 -> 3, 0xf00a -> 1) and the 0xf004 text of one or three commas ("(a,b)" is read as (a,b,a,b), "(a,b,c,d)" as is; a
// text that does not give the numbers is not tested: the original leaves stack values). The second pass sets 0xe09 caret
// position, 0x400 editable, 0xe0a character limit and the 0xe0b selection pair as (first, second - first).
bool __cdecl ApplyLineInputProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply);

// The UI defaults service (the cached object 0x10f51b0 of GetCachedServiceObjectB; cGZWinText and cGZWinBtn read fonts from it).
class cIGZUIDefaults : public cIGZUnknown {
public:
    virtual void vf3_cIGZUIDefaults() = 0;
    virtual void vf4_cIGZUIDefaults() = 0;
    virtual uint32_t GetFont(uint32_t fontId) = 0; // 5 the font object (a number) for a font id
    virtual void vf6_cIGZUIDefaults() = 0;
    virtual void vf7_cIGZUIDefaults() = 0;
    virtual void vf8_cIGZUIDefaults() = 0;
    virtual void vf9_cIGZUIDefaults() = 0;
    virtual void vf10_cIGZUIDefaults() = 0;
    virtual void vf11_cIGZUIDefaults() = 0;
    virtual void vf12_cIGZUIDefaults() = 0;
    virtual void vf13_cIGZUIDefaults() = 0;
    virtual void vf14_cIGZUIDefaults() = 0;
    virtual void vf15_cIGZUIDefaults() = 0;
    virtual void vf16_cIGZUIDefaults() = 0;
    virtual void vf17_cIGZUIDefaults() = 0;
    virtual void vf18_cIGZUIDefaults() = 0;
    virtual void vf19_cIGZUIDefaults() = 0;
    virtual void vf20_cIGZUIDefaults() = 0;
    virtual void vf21_cIGZUIDefaults() = 0;
    virtual void vf22_cIGZUIDefaults() = 0;
    virtual void vf23_cIGZUIDefaults() = 0;
    virtual void vf24_cIGZUIDefaults() = 0;
    virtual void vf25_cIGZUIDefaults() = 0;
    virtual void vf26_cIGZUIDefaults() = 0;
    virtual void vf27_cIGZUIDefaults() = 0;
    virtual void vf28_cIGZUIDefaults() = 0;
    virtual void vf29_cIGZUIDefaults() = 0;
    virtual void vf30_cIGZUIDefaults() = 0;
    virtual void vf31_cIGZUIDefaults() = 0;
    virtual void vf32_cIGZUIDefaults() = 0;
    virtual void vf33_cIGZUIDefaults() = 0;
    virtual void vf34_cIGZUIDefaults() = 0;
    virtual void vf35_cIGZUIDefaults() = 0;
    virtual void vf36_cIGZUIDefaults() = 0;
    virtual void *FindFontEntry(uint32_t fontId) = 0; // 37 the font entry of an id (slot 5 its name), or null
    virtual uint32_t FindFontId(const cRZString *name) = 0; // 38 the font id of a name
};

// The spinner window class's face (GZWinSpinner, iid 0x612ce0c3).
class cIGZWinSpinner : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x612ce0c3;
    static constexpr uint32_t kFlagOutline = 0x1;       // slot 6's masks: outline (0xf019), fill (0xf00d), the three
    static constexpr uint32_t kFlagFill = 0x2;          // autonumber flags
    static constexpr uint32_t kFlagAutoNumber = 0x1000;
    static constexpr uint32_t kFlagAutoNumberComma = 0x2000;
    static constexpr uint32_t kFlagAutoNumberCurrency = 0x4000;
    virtual void *GetWindow() = 0;                      // 3
    virtual void SetDigits(uint32_t side) = 0;          // 4 (the digits attribute: 0 none, 0xf006 -> 1, 0xf007 -> 2)
    virtual bool GetDigits(uint32_t *side) = 0;         // 5
    virtual void SetFlag(uint32_t mask, bool on) = 0;   // 6
    virtual bool IsFlagSet(uint32_t mask) = 0;          // 7
    virtual int32_t GetValue() = 0;                     // 8
    virtual void SetValue(int32_t value) = 0;           // 9
    virtual void SetRange(int32_t low, int32_t high) = 0; // 10 (minmax)
    virtual void GetRange(int32_t *low, int32_t *high) = 0; // 11 (the writer ignores the answer)
    virtual void SetStepSize(int32_t step) = 0;         // 12
    virtual void GetStepSize(int32_t *step) = 0;        // 13 (the answer is ignored)
    virtual void SetImage(cIGZLegacyImage *image, const int32_t *rect) = 0; // 14 (rect null)
    virtual void vf15_cIGZWinSpinner(uint32_t) = 0;
    virtual void vf16_cIGZWinSpinner(uint32_t) = 0;
    virtual void vf17_cIGZWinSpinner(uint32_t) = 0;
    virtual void vf18_cIGZWinSpinner(uint32_t) = 0;
    virtual void SetFont(uint32_t font) = 0;            // 19
    virtual void vf20_cIGZWinSpinner(uint32_t) = 0;
    virtual void vf21_cIGZWinSpinner(uint32_t) = 0;
    virtual void vf22_cIGZWinSpinner(uint32_t, uint32_t) = 0;
    virtual void SetColor(uint32_t index, uint32_t color) = 0; // 23
    virtual void vf24_cIGZWinSpinner(uint32_t, uint32_t) = 0;
    virtual bool GetColor(uint32_t index, uint32_t *color) = 0; // 25
    virtual void vf26_cIGZWinSpinner(uint32_t) = 0;
    virtual void SetAlign(uint32_t align) = 0;          // 27 (0 left, 1 centre, 2 right)
    virtual bool GetAlign(uint32_t *align) = 0;         // 28
    virtual void SetGutters(int32_t x, int32_t y) = 0;  // 29
    virtual void GetGutters(uint8_t *x, uint8_t *y) = 0; // 30 (the answer is ignored)
    // 31: the four outline colours (left, top, right, bottom, in the order of the outline attributes' ids 0x1007,
    // 0x1008, 0x1009, 0x100a).
    virtual void SetOutlineColors(uint32_t left, uint32_t top, uint32_t right, uint32_t bottom) = 0;
    // 32: the four outline colours into four words (the answer is ignored).
    virtual void GetOutlineColors(uint32_t *left, uint32_t *top, uint32_t *right, uint32_t *bottom) = 0;
};

// 0x86ac57, GZWinSpinner (type 0x612ce0c4): apply sets the digits side (0x1000), the flags outline (0xf019), fill (0xf00d),
// autonumber, autonumbercomma, autonumbercurrency (0x1001..0x1003), the font (0xf000, as GZWinLineInput's, the default
// "main9"), the colours 0xf00f / 0xf011 / 0xf010 (indexes 0 / 1 / 2), the image (0xf016, no rectangle; its key kept as the
// window's property 0x8bd1333a and the pair as 0xdaa3b860), the alignment (0xf015: left 0, right 2, centre 1) and the gutters
// (0xf004); then, when any of the outline colours 0x1006..0x100a is there, the four outline colours (0x1006 is the default
// of the other four). The second pass sets the range (0x1004), the step size (0x1005) and the value (0x40b). The image text
// is parsed unchecked: a text without both numbers leaves stack values in the original, which the tests never feed.
bool __cdecl ApplySpinnerProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply);

// The button window class's face (GZWinBtn, iid 0x8810).
class cIGZWinButton : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x8810;
    static constexpr uint32_t kFlagToggle = 0x1;          // slot 5's masks (the attribute names are the lower case ones)
    static constexpr uint32_t kFlagTriggerOnDown = 0x2;
    static constexpr uint32_t kFlagShowCaption = 0x4;
    static constexpr uint32_t kFlagNoFill = 0x10;         // set when the fill attribute (0xf00d) is 0
    static constexpr uint32_t kFlagAutoSize = 0x20;
    static constexpr uint32_t kFlagWrapCaption = 0x40;
    static constexpr uint32_t kFlagShiftCaption = 0x80;
    static constexpr uint32_t kFlagTips = 0x100;
    static constexpr uint32_t kFlagNoTipsDelay = 0x200;   // set when tipsdelay is 0
    static constexpr uint32_t kFlagNoTipsTimeout = 0x400; // set when tipstimeout is 0
    virtual void *GetWindow() = 0; // 3
    virtual bool IsFlagSet(uint32_t mask) = 0; // 4
    virtual void SetFlag(uint32_t mask, bool on) = 0; // 5
    virtual void vf6_cIGZWinButton() = 0;
    virtual void vf7_cIGZWinButton() = 0;
    virtual void vf8_cIGZWinButton() = 0;
    virtual void vf9_cIGZWinButton() = 0;
    virtual void vf10_cIGZWinButton() = 0;
    virtual uint32_t GetLook() = 0; // 11 (SetImage's look)
    // 12: the look (1 label, 2 standard, 3 toggle, 4 check box / radio button, 0 none) with its image and the image's
    // own rectangle (both null without an image).
    virtual void SetImage(uint32_t look, cIGZLegacyImage *image, const int32_t *rect) = 0;
    virtual void vf13_cIGZWinButton() = 0;
    virtual void vf14_cIGZWinButton() = 0;
    virtual void vf15_cIGZWinButton() = 0;
    virtual void vf16_cIGZWinButton() = 0;
    virtual void vf17_cIGZWinButton() = 0;
    virtual void vf18_cIGZWinButton() = 0;
    virtual void SetFontObject(uint32_t font) = 0; // 19: the default font's object
    virtual void SetFontId(uint32_t font) = 0;     // 20: the font attribute's number as given
    virtual bool GetFontId(uint32_t *font) = 0; // 21
    virtual void vf22_cIGZWinButton() = 0;
    virtual void SetColor(uint32_t index, uint32_t color) = 0; // 23
    virtual void vf24_cIGZWinButton() = 0;
    virtual bool GetColor(uint32_t index, uint32_t *native) = 0; // 25
    virtual void vf26_cIGZWinButton() = 0;
    virtual void SetAlign(uint32_t align) = 0; // 27 (0 centre, 1 left, 2 right)
    virtual bool GetAlign(uint32_t *align) = 0; // 28
    virtual void SetGutterRect(int32_t a, int32_t b, int32_t c, int32_t d) = 0; // 29
    virtual void SetGutters(int32_t x, int32_t y) = 0;                          // 30
    virtual bool GetGutterRect(int32_t *a, int32_t *b, int32_t *c, int32_t *d) = 0; // 31
    virtual void vf32_cIGZWinButton() = 0;
    virtual void vf33_cIGZWinButton() = 0;
    virtual void vf34_cIGZWinButton() = 0;
    virtual void SetTipText(const cRZString *text) = 0; // 35
    virtual bool GetTipText(cRZString *text) = 0; // 36
    virtual void vf37_cIGZWinButton() = 0;
    virtual void SetTipFlags(uint32_t flags) = 0; // 38
    virtual bool GetTipFlags(uint32_t *flags) = 0; // 39
    virtual void SetTipOffsets(int32_t x, int32_t y) = 0; // 40
    virtual bool GetTipOffsets(int32_t *x, int32_t *y) = 0; // 41
    virtual void vf42_cIGZWinButton() = 0;
    virtual void vf43_cIGZWinButton() = 0;
    virtual void vf44_cIGZWinButton() = 0;
    virtual void SetSound(uint32_t which, uint32_t instance) = 0; // 45 (0 the click sound, 1 the release sound)
    virtual uint32_t GetSound(uint32_t which) = 0; // 46
};

// 0x8645be, GZWinBtn (type 0x22ecfc47): apply sets the flags (toggle 0xa00, triggerondown 0xa01, showcaption 0xa02, fill
// 0xf00d [0 sets "no fill"], autosize 0xa03, wrapcaption 0xa04, shiftcaption 0xa05, tips 0xa06, tipsdelay 0xa07 and
// tipstimeout 0xa08 [0 sets "none"]), the look from the first style attribute (0xf005: label 0xa09 is 1, standard 0xa0a /
// 0xa0b 2, toggle 0xa00 / 0xa0c / 0xa0d 3, checkbox 0xa0e, radiobtn 0xa0f and radiocheck 0xa1b 4, others 0), the font
// (0xf000: a number goes to the face as given and is kept as the window's font property, a text is kept only, none sets
// the default font "main9"'s object), the colours 0xf00f..0xf014 (face indexes 0, 4, 2, 1, 5, 3), the gutters (0xf004: one
// comma "(a,b)", three "(a,b,c,d)"), the image (0xf016: loaded and set with the look and the image's own rectangle, its key
// kept as the property 0x2b343bec and the pair as 0xdaa3b860; without the attribute the look alone), the alignment (0xf015),
// the tip text (0xa10 text, or 0xa12 "{group,instance}" through LoadLocalizedText, its key kept as 0x2b340ffe), the tip
// offsets (0xa11), the tip flags (0xa18, default 0x1000000) and the sounds (0xa19, 0xa1a: "{group,instance}" kept as the
// properties 0xa5be39d / 0x4a5be2f8, the instance given to the face). The second pass sets the show-caption flag again.
// Texts parsed unchecked in the original (images, sounds, tip resource) are always whole in the tests.
bool __cdecl ApplyButtonProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply);

// The text ticker window class's face (GZWinTextTicker, iid 0x032f0b3c).
class cIGZWinTextTicker : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x032f0b3c;
    // 3: the message text, then the two colours (the foreground, the background) and two words that are 0.
    virtual void SetMessage(const cIGZString *text, uint32_t foreColor, uint32_t backColor, int32_t a, int32_t b) = 0;
    virtual void vf4_cIGZWinTextTicker(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf5_cIGZWinTextTicker(uint32_t) = 0;
    virtual void vf6_cIGZWinTextTicker() = 0;
    virtual void SetFont(uint32_t font) = 0;       // 7
    virtual void SetSpeed(uint32_t speed) = 0;     // 8
    virtual void SetFrequency(uint32_t frequency) = 0; // 9
    virtual uint32_t GetSpeed() = 0;               // 10
    virtual uint32_t GetFrequency() = 0;           // 11
    virtual void *GetWindow() = 0;                 // 12
};

// 0x86be58, GZWinTextTicker (type 0xe32f0b31): apply (the second pass does nothing) sets the speed (0x1200) and the frequency
// (0x1201), the font (0xf000, as GZWinSpinner's) and then walks the attribute list once in order: a bkgcolor (0xf001) and a
// forecolor (0xf002) text "(r,g,b)" set the colours the next messages are given (the background starts 0, the foreground
// 0xffffffff, so a message before the colours gets the defaults), and each message (0x1202 text) is handed to the face
// with the colours so far. The colour texts are parsed unchecked: one without three numbers leaves stack values in the
// original, which the tests never feed.
bool __cdecl ApplyTextTickerProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply);

// The text edit window class's face (GZWinTextEdit, iid 0x231a1c57; the attribute names are RegisterTextEdit's).
class cIGZWinTextEdit : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x231a1c57;
    virtual void vf3_cIGZWinTextEdit() = 0;
    virtual void vf4_cIGZWinTextEdit() = 0;
    virtual void *GetWindow() = 0; // 5
    virtual void vf6_cIGZWinTextEdit(uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf7_cIGZWinTextEdit(uint32_t) = 0;
    virtual void vf8_cIGZWinTextEdit() = 0;
    virtual void vf9_cIGZWinTextEdit(uint32_t) = 0;
    virtual void SetFont(uint32_t font) = 0; // 10
    virtual bool GetFont(uint32_t *font) = 0; // 11
    virtual void SetColor(uint32_t index, uint32_t color) = 0; // 12
    virtual void vf13_cIGZWinTextEdit(uint32_t) = 0;
    virtual bool GetColor(uint32_t index, uint32_t *native) = 0; // 14
    virtual void vf15_cIGZWinTextEdit(uint32_t, uint32_t) = 0;
    virtual void vf16_cIGZWinTextEdit(uint32_t, uint32_t) = 0;
    virtual void vf17_cIGZWinTextEdit(uint32_t) = 0;
    virtual void SetCaretColor(uint32_t color) = 0; // 18
    virtual bool GetCaretColor(uint32_t *native) = 0; // 19
    virtual void vf20_cIGZWinTextEdit(uint32_t) = 0;
    virtual void vf21_cIGZWinTextEdit(uint32_t) = 0;
    virtual uint32_t GetHighlightColor() = 0; // 22
    virtual void SetHighlightColor(uint32_t color) = 0; // 23
    virtual void vf24_cIGZWinTextEdit(uint32_t, uint32_t) = 0;
    virtual void vf25_cIGZWinTextEdit(uint32_t, uint32_t) = 0;
    virtual void vf26_cIGZWinTextEdit() = 0;
    virtual void vf27_cIGZWinTextEdit() = 0;
    virtual void vf28_cIGZWinTextEdit() = 0;
    virtual void vf29_cIGZWinTextEdit() = 0;
    virtual void vf30_cIGZWinTextEdit() = 0;
    virtual void vf31_cIGZWinTextEdit() = 0;
    virtual int32_t GetMaxText() = 0; // 32
    virtual void SetMaxText(uint32_t count) = 0; // 33
    virtual int32_t GetMaxGlyphs() = 0; // 34
    virtual void SetMaxGlyphs(uint32_t count) = 0; // 35
    virtual void vf36_cIGZWinTextEdit() = 0;
    virtual void vf37_cIGZWinTextEdit() = 0;
    virtual void vf38_cIGZWinTextEdit() = 0;
    virtual void SetOutlineColor(bool outlined, uint32_t color) = 0; // 39
    virtual void GetOutlineColor(uint8_t *outlined, uint32_t *native) = 0; // 40
    virtual int32_t GetGutterX() = 0; // 41
    virtual void SetGutterX(int32_t x) = 0; // 42
    virtual int32_t GetGutterY() = 0; // 43
    virtual void SetGutterY(int32_t y) = 0; // 44
    virtual void Autofit() = 0; // 45
    virtual void vf46_cIGZWinTextEdit(uint32_t, uint32_t) = 0;
    virtual void vf47_cIGZWinTextEdit(uint32_t) = 0;
    virtual const char *GetInitialValue() = 0; // 48
    virtual void vf49_cIGZWinTextEdit() = 0;
    virtual void vf50_cIGZWinTextEdit(uint32_t, uint32_t) = 0;
    virtual void vf51_cIGZWinTextEdit(uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf52_cIGZWinTextEdit(uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf53_cIGZWinTextEdit(uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf54_cIGZWinTextEdit(uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf55_cIGZWinTextEdit(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf56_cIGZWinTextEdit(uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf57_cIGZWinTextEdit(uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf58_cIGZWinTextEdit(uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf59_cIGZWinTextEdit() = 0;
    virtual bool GetPrompt(cRZString *text) = 0; // 60
    virtual void SetPrompt(const cRZString *text, int32_t zero) = 0; // 61
    virtual void vf62_cIGZWinTextEdit() = 0;
    virtual void vf63_cIGZWinTextEdit(uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf64_cIGZWinTextEdit(uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf65_cIGZWinTextEdit(uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf66_cIGZWinTextEdit(uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf67_cIGZWinTextEdit(uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf68_cIGZWinTextEdit(uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf69_cIGZWinTextEdit(uint32_t) = 0;
    virtual void SetInsertMode(bool insert) = 0; // 70
    virtual bool GetInsertMode() = 0; // 71
    virtual void SetInsertIndex(uint32_t index) = 0; // 72
    virtual bool GetInsertIndex(uint32_t *index) = 0; // 73
    virtual void SetInsertPos(int32_t x, int32_t y) = 0; // 74
    virtual bool GetInsertPos(int32_t *x, int32_t *y) = 0; // 75
    virtual void vf76_cIGZWinTextEdit(uint32_t) = 0;
    virtual void vf77_cIGZWinTextEdit(uint32_t) = 0;
    virtual void SetCaretPeriod(uint32_t period) = 0; // 78
    virtual bool GetCaretPeriod(uint32_t *period) = 0; // 79
    virtual void vf80_cIGZWinTextEdit(uint32_t, uint32_t) = 0;
    virtual void vf81_cIGZWinTextEdit(uint32_t, uint32_t) = 0;
    virtual void vf82_cIGZWinTextEdit(uint32_t) = 0;
    virtual void SetFlags(uint32_t flags) = 0; // 83
    virtual void vf84_cIGZWinTextEdit(uint32_t) = 0;
    virtual void vf85_cIGZWinTextEdit(uint32_t) = 0;
    virtual uint32_t GetFlags() = 0; // 86
    virtual void SetMaxUndo(uint32_t count) = 0; // 87
    virtual int32_t GetMaxUndo() = 0; // 88
    virtual void vf89_cIGZWinTextEdit() = 0;
    virtual void vf90_cIGZWinTextEdit() = 0;
    virtual bool GetVScrollImage(cIGZLegacyImage **image, int32_t *rect) = 0; // 91
    virtual bool GetHScrollImage(cIGZLegacyImage **image, int32_t *rect) = 0; // 92
    virtual void SetVScrollImage(cIGZLegacyImage *image, const int32_t *rect) = 0; // 93
    virtual void SetHScrollImage(cIGZLegacyImage *image, const int32_t *rect) = 0; // 94
};

// One row of a flag table: the attribute that gives a flag its value and the flag's bit (a table ends with attribute 0).
struct LegacyFlagTableEntry {
    uint32_t attribute;
    uint32_t mask;
};

// 0x863aa8 (cdecl): `flags` after the table's attributes: for each entry whose attribute is a uint32 value the entry's
// bit is cleared and, when the value is 1, set again.
uint32_t __cdecl ApplyFlagTable(const LegacyFlagTableEntry *table, const cGZScriptAttributes *attributes, uint32_t flags);

// 0x86b5b0, GZWinTextEdit (type 0x231a1c58): apply sets the face's flags from the flag table (0x400, 0x300, 0x401, 0x402,
// 0xf019, 0x301, 0x403..0x406, 0x416..0x41a), the font (0xf000: a number is the face's font and the window's font
// property, a text only the property; none does nothing), the colours 0xf00f..0xf014 (face indexes 0, 2, 4, 1, 3, 5),
// the caret (0x407) and highlight (0x408) colours, the outline colour (0x40c, with the outline flag 0xf019 read
// unchecked), the prompt text (0x41b, or 0x41c "{group,instance}" through LoadLocalizedText with its key kept as
// 0xb1a4e021), the limits (0x409, 0x40a), the gutters (0xf004), the insert mode (0x40d: value 0 is true), 0x40e, 0x40f,
// 0x410, 0x411, and the two images (0x414 with rectangle 0x415, then 0x412 with 0x413: "{group,instance}" read to the
// end, the image given to the face if found, the key kept as 0x8b4bfefc / 0x8b4bfefd and the pair as the image
// properties 0 / 1). The second pass sets the autofit property 0xdacf8f9c to whether 0x304 is there and, if it is,
// calls the face's autofit. Texts parsed unchecked in the original (0x41c) and the unchecked outline flag are always
// whole in the tests.
bool __cdecl ApplyTextEditProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply);

// One option of an option group (what the group's slot 27 hands out, counted): slot 3 gives its window.
class cIGZWinOptionItem : public cIGZUnknown {
public:
    virtual ifaces::cIGZWin *GetWindow() = 0; // 3
};

// The option group window class's face (GZWinOptGrp, iid 0xa1336cc0; the attribute names are RegisterOptGrp's).
class cIGZWinOptGrp : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0xa1336cc0;
    static constexpr uint32_t kFlagMainCaption = 0x1; // slot 5's masks: maincaption, itemcaption, drawfill, outline
    static constexpr uint32_t kFlagItemCaption = 0x2;
    static constexpr uint32_t kFlagDrawFill = 0x4;
    static constexpr uint32_t kFlagOutline = 0x8;
    virtual ifaces::cIGZWin *GetWindow() = 0; // 3
    virtual bool IsFlagSet(uint32_t mask) = 0; // 4
    virtual void SetFlag(uint32_t mask, bool on) = 0; // 5
    virtual void SetGutters(int32_t x, int32_t y) = 0; // 6
    virtual void GetGutters(int32_t *x, int32_t *y) = 0; // 7 (the answer is ignored)
    virtual void SetLabelFont(uint32_t font) = 0;  // 8 (labelfont)
    virtual void SetButtonFont(uint32_t font) = 0; // 9 (buttonfont)
    virtual void vf10_cIGZWinOptGrp(uint32_t) = 0;
    virtual void vf11_cIGZWinOptGrp(uint32_t) = 0;
    virtual void vf12_cIGZWinOptGrp(uint32_t) = 0;
    virtual void vf13_cIGZWinOptGrp(uint32_t) = 0;
    // 14: colour `index` (0 label normal, 1 label disabled, 2 button normal, 3 button hilited, 4 button disabled).
    virtual void SetColor(uint32_t index, uint32_t color) = 0;
    virtual bool GetColor(uint32_t index, uint32_t *native) = 0; // 15
    virtual void vf16_cIGZWinOptGrp(uint32_t, uint32_t) = 0;
    virtual void vf17_cIGZWinOptGrp(uint32_t, uint32_t) = 0;
    virtual void vf18_cIGZWinOptGrp(uint32_t, uint32_t) = 0;
    virtual void vf19_cIGZWinOptGrp(uint32_t, uint32_t) = 0;
    virtual void vf20_cIGZWinOptGrp(uint32_t) = 0;
    virtual int32_t GetInitialSelection() = 0; // 21
    virtual void SetInitialSelection(uint32_t index) = 0; // 22
    // 23: one `option` attribute's option: its two numbers, label, image (or null) and image rectangle.
    virtual void AddOption(int32_t first, const cRZString *label, int32_t second, cIGZLegacyImage *image,
                           const int32_t *rect) = 0;
    virtual void vf24_cIGZWinOptGrp(uint32_t) = 0;
    virtual void MoveOption(int32_t first, int32_t x, int32_t y) = 0;    // 25 (optionmoveto)
    virtual void SetOptionSize(int32_t first, int32_t x, int32_t y) = 0; // 26 (optionsetsize)
    virtual bool GetOption(int32_t first, cIGZWinOptionItem **option) = 0; // 27 (counted)
    virtual void SetBackImage(cIGZLegacyImage *image) = 0;               // 28 (backimage)
    // 29: the four outline colours, in the order the outline attribute (0x40c) gives them.
    virtual void SetOutlineColors(uint32_t first, uint32_t second, uint32_t third, uint32_t fourth) = 0;
    virtual bool GetOutlineColors(uint32_t *first, uint32_t *second, uint32_t *third, uint32_t *fourth) = 0; // 30
    virtual void vf31_cIGZWinOptGrp(uint32_t) = 0;
    virtual void vf32_cIGZWinOptGrp() = 0;
    virtual void vf33_cIGZWinOptGrp(uint32_t) = 0;
    virtual void vf34_cIGZWinOptGrp(uint32_t) = 0;
    virtual void vf35_cIGZWinOptGrp(uint32_t) = 0;
    virtual void SetAutofill(uint32_t type, uint32_t size) = 0; // 36 (autofilltype: 0 automatic with a size, 1 manual)
    virtual bool GetAutofill(uint32_t *type, uint32_t *size) = 0; // 37
};

// 0x86942b, GZWinOptGrp (type 0xa1336cc1): apply sets the flags maincaption / itemcaption / drawfill (0xf00..0xf02) and
// outline (0xf019) (each == 1), the gutters (0xf004), the label and button fonts (0xf03, 0xf04: a number is the UI
// defaults service's font for it, kept as the window's font property 0 / 1; a text only the property; none does nothing),
// the five colours 0xf05..0xf09 ("(r,g,b)", face indexes 0..4), the caption text resource (0xf0a "{group,instance}"
// through LoadLocalizedText to the face's window's caption; its pair kept as the property 0x4a4af4ac, the text parsed
// unchecked), the back image (0xf0b: loaded, given to the face if found, the pair and the key 0x2b343bec kept), the outline
// colours (0x40c, four "(r,g,b)": all twelve numbers needed) and the autofill (0xf0c: automatic 0xf0e with its size 0xf10,
// else manual). The second pass walks the attributes twice: each `option` (0xf0d, a text of "first:label:second:{g,i}:(l,t,r,b)")
// is added to the face (image and rectangle optional, the rectangle {0,0,1,1} without) and its option's window gets the image
// pair and the property 0xdab5167e true; then each optionmoveto (0xf11) and optionsetsize (0xf12) text
// "first:(x,y)" is passed on; the initial selection (0x501) ends it. Numbers before a ':' are parsed unchecked (a text
// without one leaves the previous value; the first value is the window pointer, the original reusing its argument slot),
// which the tests never feed.
bool __cdecl ApplyOptionGroupProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply);

// ---- SC3WinGen (GZWinGen, type 0x4386d516; lane MK, F6 part 5) -------------------------------------------------------------

// The window procedure the generic window gets (vtable 0xe49270, 8 bytes: the vtable and a count). Its AddRef and Release go
// through the COM director's counting (AddRef and RemoveRef) and keep their own count; the last Release deletes it. Slots 0
// (shared 0x67546f: ids 1 and 0x22e85d8e answer true with the object, counted) and 3 (shared 0x1474b0: false) are the
// shared bodies the game's vtable points at; they are written here for the vtable only and not hooked. The object is
// made only by the generic window's handler (the vtable is stored there and in the destructor).
class cGZDummyWinProc : public cIGZUnknown {
public:
    static constexpr uint32_t kSize = 8;
    static constexpr uint32_t kQueryId = 0x22e85d8e; // answered besides 1
    static constexpr uint32_t kMessageA = 0xa2bf8ad0; // the two message types slot 4 answers true for
    static constexpr uint32_t kMessageB = 0xa2bf8ad1;
    cGZDummyWinProc() = default;
    bool QueryInterface(uint32_t riid, void **out) override; // shared 0x67546f
    uint32_t AddRef() override;                              // 0x85da25
    uint32_t Release() override;                             // 0x86de9e
    virtual bool vf3_cGZDummyWinProc(uint32_t, uint32_t);   // shared 0x1474b0: false
    virtual bool HandlesMessage(uint32_t, uint32_t message, uint32_t, uint32_t, uint32_t); // 4: 0x863b3e
    virtual ~cGZDummyWinProc();                              // 5: the deleting destructor 0x85d9e6
    cGZDummyWinProc(const cGZDummyWinProc &) = delete;
    cGZDummyWinProc &operator=(const cGZDummyWinProc &) = delete;

    uint32_t mRefCount = 0; // +4
};

// The generic window's face (iid 0x5386d516, the one cGZWinProcMgr asks windows for).
class cIGZWinGen : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x5386d516;
    // Slot 6's masks.
    static constexpr uint32_t kFlagMoveable = 0x1;
    static constexpr uint32_t kFlagSizeable = 0x2;
    static constexpr uint32_t kFlagDefaultKeys = 0x4;
    static constexpr uint32_t kFlagCloseVisible = 0x10;
    static constexpr uint32_t kFlagGoBackVisible = 0x20;
    static constexpr uint32_t kFlagMinMaxVisible = 0x40;
    static constexpr uint32_t kFlagCloseDisabled = 0x100;
    static constexpr uint32_t kFlagGoBackDisabled = 0x200;
    static constexpr uint32_t kFlagMinMaxDisabled = 0x400;
    static constexpr uint32_t kFlagNoTitleBar = 0x1000;
    static constexpr uint32_t kFlagNoFill = 0x2000;
    static constexpr uint32_t kFlagNoOutline = 0x4000;
    static constexpr uint32_t kFlagNoPaint = 0x8000;
    static constexpr uint32_t kFlagSideBar = 0x10000;
    virtual void *GetWindow() = 0; // 3
    virtual void vf4_cIGZWinGen() = 0;
    virtual bool IsFlagSet(uint32_t mask) = 0; // 5 (the writer's getter; SetFlag is 6)
    virtual void SetFlag(uint32_t mask, bool on) = 0; // 6
    virtual bool SetWinProc(cIGZUnknown *proc) = 0;   // 7 (null clears it)
    virtual void vf8_cIGZWinGen() = 0;
    virtual void vf9_cIGZWinGen(uint32_t, uint32_t) = 0;
    virtual void vf10_cIGZWinGen(uint32_t) = 0;
    virtual void vf11_cIGZWinGen(uint32_t) = 0;
    virtual void vf12_cIGZWinGen(uint32_t) = 0;
    virtual void SetBackImage(int32_t type, cIGZLegacyImage *image) = 0; // 13 (blttype: 0 or 2 kept, else 1)
    virtual void vf14_cIGZWinGen(uint32_t) = 0;
    virtual bool GetBackImageType(int32_t *type, int32_t *other) = 0; // 15 (the writer's blttype)
    virtual void vf16_cIGZWinGen(uint32_t) = 0;
    virtual void vf17_cIGZWinGen(uint32_t) = 0;
    virtual void vf18_cIGZWinGen(uint32_t, uint32_t) = 0;
    virtual void SetUserData(uint32_t value) = 0; // 19 (userdata)
    virtual void vf20_cIGZWinGen(uint32_t, uint32_t) = 0;
    virtual void vf21_cIGZWinGen(uint32_t, uint32_t, uint32_t) = 0;
    virtual uint32_t GetUserData() = 0; // 22
    virtual void vf23_cIGZWinGen(uint32_t, uint32_t) = 0;
    virtual void vf24_cIGZWinGen(uint32_t) = 0;
    virtual void vf25_cIGZWinGen(uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void SetGutters(int32_t x, int32_t y) = 0; // 26 (gutters, 0xf004)
    virtual void vf27_cIGZWinGen(uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual bool GetGutters(int32_t *x, int32_t *y) = 0; // 28 (the writer ignores the answer)
    virtual void SetBlueBar(uint32_t value, uint32_t zero, uint32_t alsoZero, const int32_t *rect) = 0; // 29 (bluebar)
    virtual void SetDivider(uint32_t value, int32_t amount) = 0; // 30 (divider)
};

// 0x86d5ee, SC3WinGen (type 0x5386d516): apply sets the flags of moveable .. sidebar and paint (each from its own uint32
// attribute; titlebar, fill 0xf00d, outline 0xf019 and paint are set when the value is 0), the user data (0x10000), the
// background image (0xf016 "{group,instance}": loaded; given to the face with the blt type 0x10001 reduced to 0, 2 or 1 if
// found; the key 0xab428c5d and the pair kept), the gutters (0xf004) and a new cGZDummyWinProc as the window procedure.
// Apply false walks the attributes: divider (0x10002) texts "(%u,%d)" and bluebar (0x10003) texts "(%u,%d,%d,%d,%d)" are
// passed on. The group and instance of the image text are read unchecked after a failed parse only for the key.
bool __cdecl ApplyGenProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply);

// The file browser window's face (iid 0x3aa52e64). The slot names are by use: the three sections (A, B, C) each have a font,
// six colours and a mode.
class cIGZWinFileBrowser : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x3aa52e64;
    virtual void *GetWindow() = 0; // 3
    virtual void vf4_cIGZWinFileBrowser(uint32_t) = 0;
    virtual void vf5_cIGZWinFileBrowser(uint32_t) = 0;
    virtual void vf6_cIGZWinFileBrowser(uint32_t) = 0;
    virtual void vf7_cIGZWinFileBrowser(uint32_t) = 0;
    virtual void vf8_cIGZWinFileBrowser(uint32_t) = 0;
    virtual void vf9_cIGZWinFileBrowser() = 0;
    virtual void vf10_cIGZWinFileBrowser() = 0;
    virtual void vf11_cIGZWinFileBrowser(uint32_t, uint32_t) = 0;
    virtual void vf12_cIGZWinFileBrowser(uint32_t, uint32_t) = 0;
    virtual void vf13_cIGZWinFileBrowser(uint32_t, uint32_t) = 0;
    virtual void GetDefaultPath(cRZString *path) = 0; // 14
    virtual void SetCaption(const cRZString *text) = 0; // 15: caption (0x1301)
    virtual void vf16_cIGZWinFileBrowser(uint32_t) = 0;
    virtual void vf17_cIGZWinFileBrowser(uint32_t) = 0;
    virtual void vf18_cIGZWinFileBrowser(uint32_t) = 0;
    virtual void vf19_cIGZWinFileBrowser(uint32_t) = 0;
    virtual void vf20_cIGZWinFileBrowser(uint32_t) = 0;
    virtual bool GetEntry(uint32_t index, cRZString *label, uint32_t *image, int32_t *number, uint32_t zero) = 0; // 21: AddEntry's reverse, false past the last
    virtual void AddEntry(const cRZString *label, cIGZLegacyImage *image, int32_t number, uint32_t zero) = 0; // 22: one entry (0x1342)
    virtual void vf23_cIGZWinFileBrowser(uint32_t, uint32_t) = 0;
    virtual void vf24_cIGZWinFileBrowser(uint32_t, uint32_t) = 0;
    virtual void vf25_cIGZWinFileBrowser(uint32_t, uint32_t) = 0;
    virtual void vf26_cIGZWinFileBrowser(uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf27_cIGZWinFileBrowser() = 0;
    virtual void vf28_cIGZWinFileBrowser(uint32_t) = 0;
    virtual void GetDefaultSelection(int32_t *type, cRZString *name, cRZString *extension) = 0; // 29
    virtual void SetModeTexts(uint32_t kind, const cRZString *first, const cRZString *second) = 0; // 30: 0x1305 kind, 0x1306 / 0x1307 texts
    virtual void vf31_cIGZWinFileBrowser(uint32_t) = 0;
    virtual void vf32_cIGZWinFileBrowser() = 0;
    virtual void vf33_cIGZWinFileBrowser(uint32_t) = 0;
    virtual void vf34_cIGZWinFileBrowser() = 0;
    virtual void vf35_cIGZWinFileBrowser(uint32_t) = 0;
    virtual void GetCorners(uint32_t *first, uint32_t *second) = 0; // 36: reads the pair 0x130c / 0x130d change
    virtual void SetCorners(uint32_t first, uint32_t second) = 0; // 37: writes it back
    virtual void GetDirectorySort(int32_t *sort) = 0; // 38
    virtual void SetFilter(uint32_t value) = 0; // 39: 0x1312
    virtual void GetBars(uint32_t *first, uint32_t *second, uint32_t *third) = 0; // 40: reads the triple 0x1319..0x131b change
    virtual void vf41_cIGZWinFileBrowser(uint32_t, uint32_t) = 0;
    virtual void SetBars(uint32_t first, uint32_t second, uint32_t third) = 0; // 42: writes it back
    virtual void vf43_cIGZWinFileBrowser(uint32_t, uint32_t) = 0;
    virtual void vf44_cIGZWinFileBrowser() = 0;
    virtual void vf45_cIGZWinFileBrowser(uint32_t) = 0;
    virtual void vf46_cIGZWinFileBrowser(uint32_t, uint32_t) = 0;
    virtual uint32_t GetFileNameColor(uint32_t index) = 0; // 47: the native colour
    virtual int32_t GetFileNameAlignment() = 0; // 48
    virtual void SetFontA(uint32_t font) = 0; // 49: first section font (0x131c)
    virtual void vf50_cIGZWinFileBrowser(uint32_t) = 0;
    virtual void vf51_cIGZWinFileBrowser(uint32_t, uint32_t) = 0;
    virtual void SetColorA(uint32_t index, uint32_t color) = 0; // 52: first section colours (0x131d..0x1322)
    virtual void SetModeA(uint32_t mode) = 0; // 53: first section flag (0x1323)
    virtual void vf54_cIGZWinFileBrowser() = 0;
    virtual void vf55_cIGZWinFileBrowser(uint32_t) = 0;
    virtual void vf56_cIGZWinFileBrowser(uint32_t, uint32_t) = 0;
    virtual uint32_t GetFileExtensionColor(uint32_t index) = 0; // 57
    virtual int32_t GetFileExtensionAlignment() = 0; // 58
    virtual void SetFontB(uint32_t font) = 0; // 59: second section font (0x1324)
    virtual void vf60_cIGZWinFileBrowser(uint32_t) = 0;
    virtual void vf61_cIGZWinFileBrowser(uint32_t, uint32_t) = 0;
    virtual void SetColorB(uint32_t index, uint32_t color) = 0; // 62: second section colours (0x1325..0x132a)
    virtual void SetModeB(uint32_t mode) = 0; // 63: second section flag (0x132b)
    virtual void vf64_cIGZWinFileBrowser() = 0;
    virtual void vf65_cIGZWinFileBrowser(uint32_t) = 0;
    virtual void vf66_cIGZWinFileBrowser(uint32_t, uint32_t) = 0;
    virtual uint32_t GetDriveNameColor(uint32_t index) = 0; // 67
    virtual int32_t GetDriveNameAlignment() = 0; // 68
    virtual void SetFontC(uint32_t font) = 0; // 69: third section font (0x132c)
    virtual void vf70_cIGZWinFileBrowser(uint32_t) = 0;
    virtual void vf71_cIGZWinFileBrowser(uint32_t, uint32_t) = 0;
    virtual void SetColorC(uint32_t index, uint32_t color) = 0; // 72: third section colours (0x132d..0x1332)
    virtual void SetModeC(uint32_t mode) = 0; // 73: third section flag (0x1333)
    virtual int32_t GetIconColumnWidth() = 0; // 74
    virtual void SetValueA(uint32_t value) = 0; // 75: 0x1334
    virtual int32_t GetExtensionColumnWidth() = 0; // 76
    virtual void SetValueB(uint32_t value) = 0; // 77: 0x1335
    virtual int32_t GetRowHeight() = 0; // 78
    virtual void SetValueC(uint32_t value) = 0; // 79: 0x1336
    virtual void GetSelection(bool *enabled, uint32_t *color) = 0; // 80: reads the pair 0x1337 / 0x1338 change
    virtual void SetSelection(bool enabled, uint32_t color) = 0; // 81: writes it back
    virtual void vf82_cIGZWinFileBrowser(uint32_t) = 0;
    virtual void vf83_cIGZWinFileBrowser(uint32_t) = 0;
    virtual void GetCursor(bool *enabled, uint32_t *color) = 0; // 84: reads the pair 0x1339 / 0x133a change
    virtual void SetCursor(bool enabled, uint32_t color) = 0; // 85: writes it back
    virtual void vf86_cIGZWinFileBrowser(uint32_t) = 0;
    virtual void vf87_cIGZWinFileBrowser(uint32_t) = 0;
    virtual void GetBorder(uint32_t *first, uint32_t *second) = 0; // 88: reads the colours 0x133b / 0x133c change
    virtual void SetBorder(uint32_t first, uint32_t second) = 0; // 89: writes them back
    virtual void vf90_cIGZWinFileBrowser(uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf91_cIGZWinFileBrowser(uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf92_cIGZWinFileBrowser() = 0;
    virtual void vf93_cIGZWinFileBrowser(uint32_t) = 0;
    virtual void GetBackDirText(cRZString *text) = 0; // 94
    virtual void SetPath(const cRZString *text) = 0; // 95: 0x133d
    virtual uint8_t IsOutline() = 0; // 96
    virtual void SetOutline(bool on) = 0; // 97: 0xf019
    virtual int32_t GetDriveListSize() = 0; // 98
    virtual void SetValueD(uint32_t value) = 0; // 99: 0x133e
    virtual void vf100_cIGZWinFileBrowser(uint32_t) = 0;
    virtual uint32_t GetBackgroundColor() = 0; // 101: the native colour
    virtual void vf102_cIGZWinFileBrowser(uint32_t) = 0;
    virtual void SetColorD(uint32_t color) = 0; // 103: 0x133f
    virtual void vf104_cIGZWinFileBrowser() = 0;
    virtual void SetBackImage(cIGZLegacyImage *image) = 0; // 105: 0x1340
    virtual uint8_t IsBackgroundTransparent() = 0; // 106
    virtual void SetFlagE(bool on) = 0; // 107: 0x1341
};

// 0x866410, GZWinFileBrowser (type 0x3aa52e64; first pass only): the caption (0x1301), the mode texts (0x1305 .. 0x1307), the
// pairs and triples that are read from the face, changed by the attributes that name a value (0x130c, 0x130d, 0x1312, 0x1319
// .. 0x131b) and written back, three sections of font (a number: the UI defaults service's font, kept as the window's
// property; a text: the property only), six colours and a mode, the plain numbers and colours after them (0x1334 .. 0x133f,
// 0x1341, 0xf019), the path text (0x133d), the image (0x1340 "{group,instance}") and the entries (0x1342 "label:number:
// {group,instance}", repeated; each with its image pair and key kept as the window's properties).
bool __cdecl ApplyFileBrowserProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply);

// A grid cell's font as the grid's SetCellFont takes it (0x24 bytes; the original builds it with 0x940a6e: the words at the
// bit pattern of 2.0f, the style dword at 0xff).
struct GridCellFont {
    static constexpr uint32_t kDefaultWord = 0x40000000; // 2.0f
    uint32_t font = 0;
    uint32_t words[6] = {kDefaultWord, kDefaultWord, kDefaultWord, kDefaultWord, kDefaultWord, kDefaultWord}; // +4 .. +0x18
    uint8_t style[4] = {0xff, 0, 0, 0};                                                                       // +0x1c
    uint32_t last = kDefaultWord;                                                                              // +0x20
};
static_assert(sizeof(GridCellFont) == 0x24, "GridCellFont layout");

// The grid window's face (GZWinGrid, iid 0xdaa6b9be). It is the window object's primary interface: its slot 5 gives the window's
// own interface (this + 4), whose slot 121 turns a colour attribute into the native colour.
class cIGZWinGrid : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0xdaa6b9be;
    virtual void vf3_cIGZWinGrid() = 0;
    virtual void vf4_cIGZWinGrid() = 0;
    virtual ifaces::cIGZWin * GetWindow() = 0; // 5: shared 0x1c04e0: this + 4, the window's own interface (its slot 121 converts a colour)
    virtual void vf6_cIGZWinGrid() = 0;
    virtual void vf7_cIGZWinGrid(uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf8_cIGZWinGrid(uint32_t) = 0;
    virtual void vf9_cIGZWinGrid(uint32_t) = 0;
    virtual void SetFlags(uint32_t mask) = 0; // 10: sets bits of the window's flag word
    virtual void vf11_cIGZWinGrid(uint32_t) = 0;
    virtual void ClearFlags(uint32_t mask) = 0; // 12: clears them
    virtual bool IsFlagSet(uint32_t mask) = 0; // 13
    virtual void vf14_cIGZWinGrid() = 0;
    virtual void SetFont(uint32_t font) = 0; // 15: 0xf000 and the default
    virtual void vf16_cIGZWinGrid() = 0;
    virtual void vf17_cIGZWinGrid(uint32_t) = 0;
    virtual void vf18_cIGZWinGrid(uint32_t) = 0;
    virtual void vf19_cIGZWinGrid(uint32_t, uint32_t) = 0;
    virtual void SetColor(uint32_t color, uint32_t index) = 0; // 20: colour table of six: 0x1601 at 0
    virtual void vf21_cIGZWinGrid(uint32_t, uint32_t) = 0;
    virtual void GetFontColor(uint32_t *native, uint32_t index) = 0; // 22
    virtual void vf23_cIGZWinGrid(uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf24_cIGZWinGrid(uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void SetFlag800(bool on) = 0; // 25: 0x1603
    virtual bool GetTextWrapping() = 0; // 26
    virtual void GetGutters(int32_t *a, int32_t *b, int32_t *c, int32_t *d) = 0; // 27
    virtual void SetMargins(int32_t left, int32_t top, int32_t right, int32_t bottom) = 0; // 28: 0xf004
    virtual void vf29_cIGZWinGrid(uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf30_cIGZWinGrid(uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void SetHighlight(bool on, uint32_t color) = 0; // 31: flag 0x200 and the colour: 0x1616 / 0x1604
    virtual void vf32_cIGZWinGrid(uint32_t) = 0;
    virtual uint32_t GetOutlineColor() = 0; // 33: the native colour
    virtual void vf34_cIGZWinGrid(uint32_t) = 0;
    virtual void vf35_cIGZWinGrid(uint32_t) = 0;
    virtual void vf36_cIGZWinGrid(uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf37_cIGZWinGrid(uint32_t) = 0;
    virtual void vf38_cIGZWinGrid(uint32_t) = 0;
    virtual void vf39_cIGZWinGrid(uint32_t) = 0;
    virtual void vf40_cIGZWinGrid(uint32_t) = 0;
    virtual void vf41_cIGZWinGrid(uint32_t) = 0;
    virtual void vf42_cIGZWinGrid(uint32_t) = 0;
    virtual void vf43_cIGZWinGrid(uint32_t, uint32_t) = 0;
    virtual void GetSelection(int32_t *rule, int32_t *maximum) = 0; // 44
    virtual void SetSpacing(int32_t first, int32_t second) = 0; // 45: 0x1605 / 0x1606
    virtual void vf46_cIGZWinGrid() = 0;
    virtual void vf47_cIGZWinGrid(uint32_t, uint32_t) = 0;
    virtual void vf48_cIGZWinGrid(uint32_t, uint32_t) = 0;
    virtual void vf49_cIGZWinGrid(uint32_t, uint32_t) = 0;
    virtual void vf50_cIGZWinGrid(uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf51_cIGZWinGrid(uint32_t, uint32_t) = 0;
    virtual void vf52_cIGZWinGrid() = 0;
    virtual void vf53_cIGZWinGrid(uint32_t, uint32_t) = 0;
    virtual void vf54_cIGZWinGrid(uint32_t, uint32_t) = 0;
    virtual void vf55_cIGZWinGrid(uint32_t, uint32_t) = 0;
    virtual void vf56_cIGZWinGrid(uint32_t) = 0;
    virtual void vf57_cIGZWinGrid(uint32_t) = 0;
    virtual void vf58_cIGZWinGrid(uint32_t) = 0;
    virtual void vf59_cIGZWinGrid(uint32_t) = 0;
    virtual void vf60_cIGZWinGrid(uint32_t, uint32_t) = 0;
    virtual void vf61_cIGZWinGrid(uint32_t, uint32_t) = 0;
    virtual void vf62_cIGZWinGrid(uint32_t) = 0;
    virtual void vf63_cIGZWinGrid(uint32_t) = 0;
    virtual void SetBackColors(uint32_t first, uint32_t second) = 0; // 64: 0x1623 / 0x1624 (apply false)
    virtual void GetGridColors(uint32_t *column, uint32_t *row) = 0; // 65: the native colours
    virtual void SetValue66(int32_t value) = 0; // 66: 0x1602
    virtual void vf67_cIGZWinGrid(uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf68_cIGZWinGrid(uint32_t, uint32_t, uint32_t) = 0;
    virtual int32_t GetTextAlign() = 0; // 69
    virtual void SetSizeA(int32_t value) = 0; // 70: 0x1607
    virtual void SetColumn(int32_t first, int32_t count, int32_t value) = 0; // 71: one 0x1626 column description
    virtual void SetSizeB(int32_t value) = 0; // 72: 0x1608
    virtual void SetRows(int32_t first, int32_t count, int32_t value) = 0; // 73: one 0x1627 row description
    virtual int32_t GetDefaultColumnWidth() = 0; // 74
    virtual int32_t GetDefaultRowHeight() = 0; // 75
    virtual void vf76_cIGZWinGrid(uint32_t, uint32_t) = 0;
    virtual void vf77_cIGZWinGrid(uint32_t, uint32_t) = 0;
    virtual void vf78_cIGZWinGrid(uint32_t, uint32_t) = 0;
    virtual void vf79_cIGZWinGrid(uint32_t, uint32_t) = 0;
    virtual void SetScrollbars(bool first, bool second) = 0; // 80: from 0x1609
    virtual void GetHeaderSizes(int32_t *column, int32_t *row) = 0; // 81
    virtual void SetOffset(int32_t first, int32_t second) = 0; // 82: 0x160a / 0x160b
    virtual void vf83_cIGZWinGrid(uint32_t) = 0;
    virtual void vf84_cIGZWinGrid(uint32_t, uint32_t) = 0;
    virtual void vf85_cIGZWinGrid(uint32_t, uint32_t) = 0;
    virtual void vf86_cIGZWinGrid(uint32_t, uint32_t) = 0;
    virtual void vf87_cIGZWinGrid(uint32_t, uint32_t) = 0;
    virtual void vf88_cIGZWinGrid() = 0;
    virtual void vf89_cIGZWinGrid() = 0;
    virtual void vf90_cIGZWinGrid(uint32_t, uint32_t, uint32_t) = 0;
    virtual void SetCellFont(int32_t column, int32_t row, const GridCellFont *font) = 0; // 91: a cell's font
    virtual void vf92_cIGZWinGrid(uint32_t, uint32_t) = 0;
    virtual void vf93_cIGZWinGrid(uint32_t, uint32_t) = 0;
    virtual void vf94_cIGZWinGrid(uint32_t, uint32_t) = 0;
    virtual void vf95_cIGZWinGrid(uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf96_cIGZWinGrid(uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf97_cIGZWinGrid(uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf98_cIGZWinGrid(uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf99_cIGZWinGrid(uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf100_cIGZWinGrid(uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf101_cIGZWinGrid(uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf102_cIGZWinGrid(uint32_t, uint32_t) = 0;
    virtual void vf103_cIGZWinGrid(uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf104_cIGZWinGrid() = 0;
    virtual void vf105_cIGZWinGrid(uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf106_cIGZWinGrid(uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf107_cIGZWinGrid(uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf108_cIGZWinGrid(uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf109_cIGZWinGrid(uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf110_cIGZWinGrid(uint32_t, uint32_t) = 0;
    virtual void vf111_cIGZWinGrid(uint32_t, uint32_t) = 0;
    virtual void vf112_cIGZWinGrid(uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf113_cIGZWinGrid(uint32_t, uint32_t) = 0;
    virtual void vf114_cIGZWinGrid(uint32_t, uint32_t) = 0;
    virtual void vf115_cIGZWinGrid(uint32_t, uint32_t) = 0;
    virtual void vf116_cIGZWinGrid(uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf117_cIGZWinGrid(uint32_t, uint32_t, uint32_t) = 0;
    virtual void SetCellText(int32_t column, int32_t row, const char *text, uint32_t length, bool one, bool flag) = 0; // 118: a cell's text
    virtual void vf119_cIGZWinGrid(uint32_t, uint32_t, uint32_t) = 0;
    virtual void SetCellImage(int32_t column, int32_t row, cIGZLegacyImage *image, bool one) = 0; // 120: a cell's image
};

// The native colour of the window's own slot 121 (cGZWin 0x931c34: the window's draw buffer, else the graphics system).
uint32_t WindowNativeColor(ifaces::cIGZWin *window, uint8_t red, uint8_t green, uint8_t blue);

// 0x867920, GZWinGrid (type 0xdaa6b9be). Apply: the colour 0x1601 (table index 0), the font (0xf000: a number gives the UI
// defaults service's font and is kept as the window's font property; a text is kept only; none sets the default font
// 0x68963c4c), 0x1602, 0x1603, the gutters (0xf004, zeros without), 0x1605 / 0x1606, the highlight (0x1616 == 1, colour
// 0x1604), the sizes 0x1607 / 0x1608, the scrollbars (0x1609: the original reads it unchecked, keeping the 0x1607 number
// without it), the offset (0x160a / 0x160b), then the flag word's bits (0x160c .. 0x1622: set for a value, cleared for 0;
// 0x160f also invalidates the window when cleared), then the cells (0x1625), columns (0x1626) and rows (0x1627). Apply
// false: the two colours 0x1623 / 0x1624 (black without) to SetBackColors.
bool __cdecl ApplyGridProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply);

// 0x863ba3 (lane MM): a grid cell attribute's text "column,row,flags,data..." split at commas (at most 11 parts; fewer
// than 4: false, nothing but the parts read). The column and row are numbers (decimal, or hex after "0x"), the flags hex.
// Without the text flag 0x40000 the data is the next two parts joined by a comma (group,instance) into `resource`; with
// it, the next part alone is assigned to the resource's string. With the font flag 0x1000 the next part is a font name
// (the UI defaults' id for it goes to `fontId`) and the five after it are numbers for the font words.
bool __cdecl ParseGridCell(cRZString *text, uint32_t *column, uint32_t *row, uint32_t *flags, cRZString *resource,
                           uint32_t *fontId, uint32_t *fontWord0, uint32_t *fontWord1, uint32_t *fontWord2,
                           uint32_t *fontWord3, uint32_t *fontWord4);

// The tree view window class's face (GZWinTreeView, iid 0x3ae8bad2; cGZWinTreeView's vtable 0xe54f1c, slots 3 .. 58). A node is
// a number; node 0 is the root. Only the slots the handler calls are named; the others' words are the callers' counts.
class cIGZWinTreeView : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x3ae8bad2;
    static constexpr uint32_t kFlagRoot = 0x2;         // displayroot
    static constexpr uint32_t kFlagNodeIcons = 0x4;    // displaynodeicons
    static constexpr uint32_t kFlagTreeLines = 0x8;    // displaytreelines
    static constexpr uint32_t kFlagMultiSelect = 0x10; // multiselect
    virtual void SetFlag(uint32_t mask, bool on) = 0; // 3
    virtual bool IsFlagSet(uint32_t mask) = 0; // 4: the flags 2, 4, 8, 0x10 (the script writer reads them)
    virtual void vf5_cIGZWinTreeView(uint32_t, uint32_t) = 0;
    virtual void vf6_cIGZWinTreeView(uint32_t, uint32_t) = 0;
    virtual void SetIconGutter(uint32_t value) = 0; // 7: icongutter 0x1405
    virtual int32_t GetIconGutter() = 0; // 8: icongutter
    virtual void SetBaseColor(uint32_t color, uint32_t one) = 0; // 9: stores both; the handler gives black and 1
    virtual void vf10_cIGZWinTreeView(uint32_t, uint32_t) = 0;
    virtual void SetValue1336(uint32_t value) = 0; // 11: attribute 0x1336
    virtual int32_t GetRowHeight() = 0; // 12: the number SetValue1336 stored (rowheight)
    virtual void SetMinMaxBoxSize(uint32_t value) = 0; // 13: minmaxboxsize 0x1406
    virtual int32_t GetMinMaxBoxSize() = 0; // 14
    virtual void SetColor133f(uint32_t color) = 0; // 15: attribute 0x133f
    virtual uint32_t GetBackgroundColor() = 0; // 16: the native colour SetColor133f stored
    virtual void SetHighlightColor(uint32_t color) = 0; // 17: attribute 0x408
    virtual uint32_t GetHighlightColor() = 0; // 18
    virtual void vf19_cIGZWinTreeView(uint32_t, uint32_t, uint32_t) = 0;
    virtual uint32_t AddImage(cIGZLegacyImage *image, const int32_t *rect) = 0; // 20: the image's number for the node slots
    virtual bool GetImage(uint32_t number, cIGZLegacyImage **image, int32_t *rect) = 0; // 21: the image AddImage numbered; its rectangle
    virtual void vf22_cIGZWinTreeView() = 0;
    virtual void vf23_cIGZWinTreeView(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) = 0;
    // 24: a node under `parent` (a node number) at `position` (-2 first, -3 last, -4 sorted) with its text; its number
    virtual uint32_t AddNode(uint32_t parent, int32_t position, const cRZString *text, uint32_t flag) = 0;
    virtual void vf25_cIGZWinTreeView(uint32_t) = 0;
    virtual void vf26_cIGZWinTreeView(uint32_t, uint32_t, uint32_t) = 0;
    virtual void vf27_cIGZWinTreeView(uint32_t) = 0;
    virtual void vf28_cIGZWinTreeView() = 0;
    virtual void vf29_cIGZWinTreeView(uint32_t, uint32_t) = 0;
    virtual void vf30_cIGZWinTreeView(uint32_t, uint32_t) = 0;
    virtual cIGZString *GetNodeText(uint32_t node) = 0; // 31
    virtual void SetNodeText(uint32_t node, const cRZString *text) = 0; // 32: the handler gives the root (0) its text
    virtual void vf33_cIGZWinTreeView(uint32_t) = 0;
    virtual void vf34_cIGZWinTreeView(uint32_t, uint32_t) = 0;
    virtual uint32_t GetNodeImage(uint32_t node, uint32_t index) = 0; // 35: the image number
    virtual void SetNodeImage(uint32_t node, uint32_t index, uint32_t image) = 0; // 36: 0 normal, 1 selected, 2 expanded, 3 both
    virtual void vf37_cIGZWinTreeView(uint32_t) = 0;
    virtual void vf38_cIGZWinTreeView(uint32_t) = 0;
    virtual void SetNodeFont(uint32_t node, uint32_t font) = 0; // 39
    virtual void vf40_cIGZWinTreeView(uint32_t) = 0;
    virtual void vf41_cIGZWinTreeView(uint32_t, uint32_t) = 0;
    virtual uint32_t GetNodeColor(uint32_t node, uint32_t index) = 0; // 42: a native colour
    virtual void SetNodeColor(uint32_t node, uint32_t index, uint32_t color) = 0; // 43: 0 foreground, 1 background, 2 and 3 hilited
    virtual uint8_t IsNodeExpanded(uint32_t node) = 0; // 44: the writer tests the low byte for 1
    virtual void SetNodeExpanded(uint32_t node, bool on) = 0; // 45: the callee reads the low byte only
    virtual void vf46_cIGZWinTreeView(uint32_t) = 0;
    virtual void vf47_cIGZWinTreeView(uint32_t) = 0;
    virtual void vf48_cIGZWinTreeView(uint32_t) = 0;
    virtual void vf49_cIGZWinTreeView(uint32_t, uint32_t) = 0;
    virtual uint8_t IsNodeSelected(uint32_t node) = 0; // 50: the writer tests the low byte for 1
    virtual uint32_t GetNodeParent(uint32_t node) = 0; // 51: a node number
    virtual int32_t GetNodePosition(uint32_t node) = 0; // 52: -2 first, -3 last, -4 sorted, or another
    virtual void vf53_cIGZWinTreeView() = 0;
    virtual void vf54_cIGZWinTreeView() = 0;
    virtual void vf55_cIGZWinTreeView(uint32_t) = 0;
    virtual void SetNodeSelected(uint32_t node, uint32_t zero) = 0; // 56
    virtual void vf57_cIGZWinTreeView(uint32_t) = 0;
    virtual void vf58_cIGZWinTreeView() = 0;
    virtual void *GetWindow() = 0; // 59 (0xec)
};

// 0x86c19e, GZWinTreeView (type 0x3ae8bad2). Apply true: the flags (0x1401 .. 0x1404 set the face's flags 2, 4, 8, 0x10 to the
// number being nonzero), 0x1405, 0x1336, 0x1406, and the two colours 0x133f and 0x408. Apply false builds the tree and keeps
// its tables on the window as properties (each a number that is a pointer to a std::map the game's code reads in place):
//   - the images (0xf016, "name:{group,instance}:(l,t,r,b)", the rectangle optional): for each, the table 0x66c20802
//     (name -> group, instance; kept whether or not the image loads) and, when the image loads, the face's AddImage number in
//     0x46c207a5 (name -> number) and 0xc6c207c8 (number -> name);
//   - the root: 0x1407 text, the images 0x1408 .. 0x140b by name (0, 1, 2, 3 without), the colours 0x140c .. 0x140f, the font
//     0x1410 (a number: the UI defaults service's font and the window's font property; a text: the property only),
//     0x1411 expanded and 0x1412 selected;
//   - the nodes (0x1102, "name:text:parent:first|last|sorted:image0:image1:image2:image3:(color0):(color1):(color2):
//     (color3):font:expanded:selected"): the table name -> font number 0x86c21da0, then the nodes are made under their
//     parents depth first from "Root" (the children in list order); name -> node 0xe6c21d83 and node -> name 0x26c21e35.
// A position that is none of the three words keeps the previous node's (the first: stack leftovers in the original: zero here).
// The tables are never freed (the window's properties hold the pointers).
bool __cdecl ApplyTreeViewProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply);
