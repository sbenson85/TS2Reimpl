#pragma once
// cIGZFont (iid 0x10673960; 66 slots, the deleting destructor at 58): a font, implemented by cGZFont (src/gz/cGZFont.h;
// vtable 0xe507ec, 0x190 bytes; docs/subsystems/rendering.md R6). Lane OA declared the slots the font system calls; lane
// OC (cGZFont) named the rest from the class's bodies; lane OH typed the last of them (every slot is ours now). Names are
// tentative. The cGZWinText calls (src/ui/cGZWinText.size.cpp:
// slots 12, 13, 35, 44, 48, 49, 53) still go through numbers.
//
// Text arguments (`text`, `length`) are in the font's encoding (slot 24): `length` counts units of it (bytes, or
// 16-bit units for encoding 0x10).
#include "gz/cIGZBuffer.h"
#include "gz/cIGZFontRenderer.h"
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"

#include <cstdint>

struct cGZFontGlyphCache;
struct cGZFontCachedGlyph;

class cIGZFont : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x10673960;
    static constexpr uint32_t kClassId = 0x1aa404ac; // cGZFont's COM class

    virtual bool Load() = 0;                                // 3: opens the file at the path; false: unusable
    virtual bool Unload() = 0;                              // 4: true
    virtual void GetFamily(cIGZString *out) = 0;            // 5
    virtual bool HasStyle(uint32_t flags) = 0;              // 6: every bit of `flags` set
    virtual uint32_t GetStyle() = 0;                        // 7: bit 0 bold, bit 1 italic, bits 2-5 settable
    virtual void SetStyle(uint32_t flags, bool set) = 0;    // 8: bits 2-5 of `flags` only
    virtual uint32_t GetGroup() = 0;                        // 9: 1 or 2: whether the face has a charmap, plus one
    virtual uint32_t GetSize() = 0;                         // 10
    virtual bool SetSize(uint32_t size) = 0;                // 11
    virtual uint32_t GetColour() = 0;                       // 12
    virtual uint32_t GetShadowColour() = 0;                 // 13
    virtual void SetColours(uint32_t colour, uint32_t shadow) = 0; // 14
    virtual uint32_t GetQuality() = 0;                      // 15: the anti-aliasing mode
    virtual void SetQuality(uint32_t mode) = 0;             // 16: 0 is ignored
    virtual float GetGamma() = 0;                           // 17
    virtual void SetGamma(float gamma) = 0;                 // 18: clamped to [0, 10]
    virtual float GetContrast() = 0;                        // 19
    virtual void SetContrast(float contrast) = 0;           // 20: clamped to [-1, 1]
    virtual void GetPath(cIGZString *out) = 0;              // 21
    virtual void SetPath(const cIGZString *path) = 0;       // 22
    virtual bool GetEncoding(uint32_t *out) = 0;            // 23: true
    virtual bool SetEncoding(int32_t encoding) = 0;         // 24: -1 the system code page, -2 the default; true
    virtual bool SupportsEncoding(uint32_t encoding) = 0;   // 25: the face has the characters that identify a code page (lane OF)
    virtual bool IsUnicodeGroup(uint32_t group) = 0;        // 26: group == 2
    virtual bool HasGlyphs(const void *text, uint32_t length) = 0;   // 27: every character of the text (in the font encoding) has a glyph in the face; true for no text
    virtual bool CacheGlyphs(const void *text, uint32_t length) = 0;  // 28: makes the glyphs of the text (slot 62 for each); returns whether the font is loaded
    virtual void GetScale(float *x, float *y) = 0;          // 29
    virtual void SetScale(float x, float y) = 0;            // 30: each clamped to [0.01, 1000]
    virtual void GetAdvanceScale(float *x, float *y) = 0;   // 31: y is always 1
    virtual void SetAdvanceScale(float x, float y) = 0;     // 32: x clamped to [0.1, 10]; y unused
    virtual void GetSpacing(int32_t *x, int32_t *y) = 0;    // 33
    virtual void SetSpacing(int32_t x, int32_t y) = 0;      // 34
    virtual int32_t GetLineSpacing() = 0;                   // 35: line height plus the vertical spacing
    virtual int32_t GetCharWidth(const void *text, uint32_t length) = 0;   // 36: the first character's box width (lane OF)
    virtual uint32_t GetLineHeight(uint32_t, uint32_t) = 0; // 37: the arguments are unused
    virtual int32_t GetAscent() = 0;                        // 38: ascent plus the vertical spacing
    virtual int32_t GetDescent() = 0;                       // 39: slot 35 less slot 38
    virtual int32_t GetCharAdvance(const void *text, uint32_t length) = 0; // 40: the first character's advance and the spacing (lane OF)
    // 41 (game code): the units of the next line of the text for `maxWidth`, broken at a space or by `mode` (1: the last that
    // fits, 2: a cluster boundary); *width the line's width
    virtual uint32_t BreakLine(const void *text, uint32_t length, uint32_t maxWidth, uint32_t *width, uint32_t mode) = 0;
    // 42: how many units of the text fit in `width` (the longest prefix not wider); the prefix's width in *used (lane OF)
    virtual uint32_t FitChars(const void *text, uint32_t length, uint32_t width, uint32_t *used) = 0;
    // 43: the number of lines the text wraps to in `width` (a width of 0 counts as 1), each line made by slot 41 in
    // mode `mode` + 1; *overflow is set when a line's trimmed width is more than the width (lane OF)
    virtual uint32_t WrapLines(const void *text, uint32_t length, uint32_t width, uint8_t mode, uint8_t *overflow) = 0;
    virtual uint32_t CountLines(const void *text, uint32_t length, uint32_t width) = 0;     // 44: slot 43 in mode 1 (lane OF)
    // 45: the least width (20 to 4000) in which the text wraps to height / line spacing lines without overflow, 0 for none (lane OF)
    virtual uint32_t WidthForLines(const void *text, uint32_t length, uint32_t height) = 0;
    // 46: sizes `rect` (keeping its left and top) to hold the text: on one line if it is narrower than `lineWidth`, else on
    // two or more up to `maxWidth` wide and `maxHeight` high; -1 for either width means 12 (and 32) widths of an 'a'. False
    // (the rect emptied) when it does not fit (lane OF)
    virtual bool FitRect(const void *text, uint32_t length, cGZRect *rect, int32_t lineWidth, int32_t maxWidth, int32_t maxHeight) = 0;
    // 47: sets the rect's right (and bottom) to hold the text: no text; one line; wrapped to the rect's width (`fixedWidth`: its
    // bottom is set) or to its height (its right is set) (lane OF)
    virtual void MeasureRect(const void *text, uint32_t length, cGZRect *rect, bool wrap, bool fixedWidth) = 0;
    virtual void GetExtent(const void *text, uint32_t length, cGZRect *rect) = 0;           // 48: {0, 0, width, line height} (lane OF)
    virtual uint32_t GetWidth(const void *text, uint32_t length) = 0;                       // 49 (game code): the text's width
    // 50 (game code): the width without trailing spaces; *used the units of the text kept
    virtual uint32_t GetTrimmedWidth(const void *text, uint32_t length, uint32_t *used) = 0;
    // 51: cuts `text` (a string in the font encoding) to fit the width of `rect`; with `wrap` it is slot 60 instead; else, when it
    // is too wide, it is cut to leave room for `suffix` (emptied when the suffix alone is too wide), trailing spaces dropped, and
    // the suffix appended. Returns the text's length before the cut.
    virtual uint32_t ClipText(cIGZString *text, const cIGZString *suffix, const cGZRect *rect, bool wrap) = 0;
    // 52: draws the text at (x, y) through the renderer (the outermost draw begins and ends the renderer's draw); colour 0x8000000
    // is the font's own, `colour2` the second colour of the anti-aliased quality (a ramp from it to `colour`), `clip` drops
    // the glyphs outside it; `pos` (x, y), when given, is where the pen starts and where it ends. Answers whether the font is
    // loaded (false when the renderer refuses).
    virtual bool DrawText(cIGZBuffer *buffer, int32_t x, int32_t y, const void *text, uint32_t length, uint32_t colour,
                          uint32_t colour2, const cGZRect *clip, int32_t *pos) = 0;
    // 53: draws the text in `rect`, a line at a time (the line breaker's mode 2), each aligned (0 left, 1 centred, 2 right, else
    // where the last line began); trailing blanks of a line are not drawn. `pos` as in slot 52 (the pen after the last line).
    virtual bool DrawBox(cIGZBuffer *buffer, const cGZRect *rect, const void *text, uint32_t length, uint32_t align,
                         uint32_t colour, uint32_t colour2, const cGZRect *clip, int32_t *pos) = 0;
    virtual cIGZFontRenderer *GetRenderer() = 0;            // 54 (not counted)
    virtual void SetRenderer(cIGZFontRenderer *renderer) = 0; // 55: counted; told of the font (slots 3 and 4)
    virtual bool BeginDraw(cIGZBuffer *target) = 0;         // 56: nested; the outermost calls the renderer's slot 5
    virtual bool EndDraw(cIGZBuffer *target) = 0;           // 57: the outermost calls the renderer's slot 6
    virtual ~cIGZFont() = default;                          // 58
    virtual int32_t GetCharBearing(const void *text, uint32_t length) = 0; // 59: the first character's left bearing (lane OF)
    // 60: wraps `text` to the lines that fit `rect` (the lines of mode 2 breaks, `width - 8` wide, as many as the height holds at the
    // line spacing; the last one clipped by slot 51 to `width - 8 - 4 * the suffix's width`), and writes them back joined, then
    // appends `suffix`. Always true.
    virtual bool WrapText(cIGZString *text, const cIGZString *suffix, const cGZRect *rect) = 0;
    virtual cGZFontGlyphCache *GetGlyphCache() = 0;         // 61: the cache of the current quality mode
    virtual cGZFontCachedGlyph *FindGlyph(uint32_t code) = 0; // 62: cached, else slot 63
    // 63: rasterises a character into the cache (a box when the face has no glyph); the cached glyph (lane OF)
    virtual cGZFontCachedGlyph *MakeGlyph(uint32_t code, cGZFontGlyphCache *cache) = 0;
    // 64: copies a glyph's bitmap (8-bit, or 1-bit when mode is 2) into the cache at (x, y) and records the glyph's box
    // (lane OF); always true
    virtual bool PlaceBitmap(cGZFontGlyphCache *cache, cGZFontCachedGlyph *glyph, uint32_t x, uint32_t y, const uint8_t *source,
                             uint32_t width, uint32_t height, uint32_t sourcePitch, uint32_t mode) = 0;
    // 65: makes the cache's bitmap at least width x height, keeping what it holds (lane OF); always true
    virtual bool GrowCache(cGZFontGlyphCache *cache, uint32_t width, uint32_t height) = 0;

    // The names the font system (src/gz/cGZFontSys.size.cpp, lane OA) calls the slots by.
    bool vf3() { return Load(); }
    void vf5(cIGZString *name) { GetFamily(name); }
    uint32_t vf7() { return GetStyle(); }
    void vf8(uint32_t style, uint32_t apply) { SetStyle(style, apply != 0); }
    uint32_t vf9() { return GetGroup(); }
    uint32_t vf10() { return GetSize(); }
    bool vf11(uint32_t size) { return SetSize(size); }
    void vf14(uint32_t colour, uint32_t shadow) { SetColours(colour, shadow); }
    void vf16(uint32_t mode) { SetQuality(mode); }
    void vf21(cIGZString *path) { GetPath(path); }
    void vf22(const cIGZString *path) { SetPath(path); }
    bool vf24(uint32_t encoding) { return SetEncoding(static_cast<int32_t>(encoding)); }
    void vf55(cIGZFontRenderer *renderer) { SetRenderer(renderer); }
};
