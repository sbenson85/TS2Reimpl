#pragma once
// cGZFont (lane OC, docs/subsystems/rendering.md R6): a font (COM class 0x1aa404ac): a face file opened through the
// Bitstream Font Fusion (T2K) engine linked into the image (0x9114ba..0x92bc90; db/third-party.jsonl), at a size,
// style, colours and anti-aliasing mode, with two caches of rasterised glyphs and the text layout over them. 0x190
// bytes, /O1 (the module is built for size).
//
// The font engine is library code: never reimplemented; ours calls its entry points through OriginalFunction
// (cGZFont.size.cpp, `t2k`), and tests stand a fake face in for it.
//
// Standalone shape (docs/standalone-shape.md): the COM director's creator 0x909fb9 (new 0x190, then the constructor
// 0x90a7db by a jump) is hooked; the font system's hand-made fallback (src/gz/cGZFontSys.size.cpp) is ours too, and
// nothing else references the vtable 0xe507ec, so every font carries ours and the slots are in effect through their
// callers (lane OH made the last of the forwarders ours: every slot of the class is ours).
//
// The font file reader (cGZFontFile, 0x30 bytes) and its file (cGZFontFileStream, a cRZFile with a header) are their
// own classes; the font system (cGZFontSys) uses the reader too.
#include "gz/cGZFontSys.h" // FontSysString
#include "gz/cIGZFont.h"
#include "gz/cIGZFontRenderer.h"
#include "rz/cRZFile.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <unordered_map>
#include <vector>

// ---- the font file ----

// The file under the font engine (0xa8 bytes): a cRZFile, opened read-only with a 4 KB read buffer, and the header of
// an obfuscated font ('MXFN', big-endian: the count of leading bytes xor-ed with a key byte).
struct cGZFontFileStream {
    static constexpr uint32_t kSize = 0xa8;
    static constexpr uint32_t kMagic = 0x4d58464e; // 'MXFN'
    static constexpr uint32_t kHeaderLength = 9;   // the magic, the count, the key

    // 0x90a6f7 (inline in the original's reader): the file at `path`, opened, its header read.
    explicit cGZFontFileStream(const cIGZString &path);
    // The reader's Close 0x90ea52: the cRZFile's destructor (the three fields are plain).
    ~cGZFontFileStream() { file.Destruct(); }
    cGZFontFileStream(const cGZFontFileStream &) = delete;
    cGZFontFileStream &operator=(const cGZFontFileStream &) = delete;

    // 0x90e280: `count` bytes at `offset` past the header into `dst`, the obfuscated ones decoded; false when the
    // file's Read fails.
    bool Read(uint8_t *dst, uint32_t offset, uint32_t count);
    // 0x90dd48: the length after the header.
    uint32_t DataLength();
    // 0x90e2dd: the whole data (at most `max` bytes) into `out` (cleared first); false when the file is not open or its
    // Read fails.
    bool ReadAll(std::vector<char> &out, uint32_t max);
    // 0x90e340 (cdecl): the font engine's read callback (the stream given to New_NonRamInputStream).
    static uint8_t __cdecl ReadCallback(cGZFontFileStream *stream, uint8_t *dst, uint32_t offset, uint32_t count);

    union {
        cRZFile file;            // +0 (vtable 0xda42a0; built and destroyed by hand, as the original does)
    };
    uint32_t headerLength;       // +0x9c 0, or 9 for an obfuscated file
    uint32_t obfuscatedLength;   // +0xa0 the leading data bytes xor-ed with the key
    uint8_t key;                 // +0xa4 (the three bytes after it are never written)
};
static_assert(sizeof(cGZFontFileStream) == cGZFontFileStream::kSize, "cGZFontFileStream: 0xa8 bytes");
static_assert(offsetof(cGZFontFileStream, headerLength) == 0x9c && offsetof(cGZFontFileStream, obfuscatedLength) == 0xa0 &&
                  offsetof(cGZFontFileStream, key) == 0xa4,
              "cGZFontFileStream layout");

// The font file reader (0x30 bytes): the font engine's objects for one opened file.
struct cGZFontFile {
    static constexpr uint32_t kSize = 0x30;
    // The face's kind (+0x24), from the engine's font type: 1 TrueType, 2 Type 1 (PFB), 3 and 4 two other types.
    enum Kind : uint32_t { kUnknown = 0, kTrueType = 1, kType1 = 2, kKind3 = 3, kKind4 = 4 };

    cGZFontFile() = default;          // 0x90a6c5
    ~cGZFontFile() { Close(); }       // 0x90a934 (then the map and the vector)
    cGZFontFile(const cGZFontFile &) = delete;
    cGZFontFile &operator=(const cGZFontFile &) = delete;

    // 0x90d6f7: closes, then opens `path` through the engine (Type 1 files decoded to a pure Type 1 copy in memory).
    // True when the engine's T2K object was made.
    bool Open(const cIGZString *path);
    // 0x90ea52: deletes the engine's objects and the file, newest first; true.
    bool Close();
    // 0x90ccd5: the face's name `which` (a name id: 1 the family, 2 the subfamily; falls back to the full name, 4)
    // into `out` (a cRZString); 1 drops a trailing " bold" / " italic", 2 keeps from the first of them. True when the
    // result is not empty. Nothing is written when the file is not open.
    bool Name(cIGZString *out, uint32_t which);
    // 0x90d27e: bit 0 bold, bit 1 italic, from the face's style bits and its subfamily name.
    uint32_t Style();
    // The face's charmap (the T2K object's +0x5c).
    bool HasCharmap() const;
    uint8_t *T2K() const { return t2k; }
    // 0x90cf86 (the stack argument is a 16-bit character): the face's glyph index for a character of a Type 1 face,
    // through the glyph's PostScript name (the Adobe name table, or "uniXXXX"); 0 when it has none. A hit is kept in
    // `glyphIndices`. ORIGINAL-BUG: a hit through the "uniXXXX" name is kept under the engine's character code for that
    // name, not under the character asked for, so the same character is looked up again next time.
    uint32_t GlyphIndex(uint32_t code);
    // The Adobe glyph name of a character, or null. The original fills a map (global 0x10ecd04) from the same 479 entries
    // at its first lookup (0x90afb0, GlyphIndex its only caller and that map's only reader); ours keeps them in a constant
    // table, so that map stays empty.
    static const char *GlyphName(uint16_t code);

    void *memory = nullptr;              // +0 the engine's memory handler (tsi_NewMemhandler)
    void *sfnt = nullptr;                // +4 the engine's font object (New_sfntClass)
    uint8_t *t2k = nullptr;              // +8 the engine's T2K scaler (NewT2K): metrics and the rendered glyph
    cGZFontFileStream *stream = nullptr; // +0xc (new 0xa8)
    void *input = nullptr;               // +0x10 the engine's input stream over the file, or over `type1` for PFB files
    std::vector<char> type1;             // +0x14 a PFB file's pure Type 1 copy
    uint16_t flags = 10;                 // +0x20 | 0x80 for engine types 2 and 7, | 1 for the others, once open
    uint32_t kind = kUnknown;            // +0x24
    std::map<uint16_t, uint32_t> glyphIndices; // +0x28 GlyphIndex's hits: character -> glyph index
};
static_assert(sizeof(cGZFontFile) == cGZFontFile::kSize, "cGZFontFile: 0x30 bytes");
static_assert(offsetof(cGZFontFile, type1) == 0x14 && offsetof(cGZFontFile, flags) == 0x20 && offsetof(cGZFontFile, kind) == 0x24 &&
                  offsetof(cGZFontFile, glyphIndices) == 0x28,
              "cGZFontFile layout");

// The reader for the font system's code (src/gz/cGZFontSys.size.cpp keeps one on its stack): the constructor and
// destructor as functions (tests stand in for them, and for Open / Name / Style, on both sides).
struct cGZFontFileAdapters {
    static cGZFontFile *__fastcall Construct(void *memory, void *); // 0x90a6c5
    static void __fastcall Destruct(cGZFontFile *self, void *);     // 0x90a934
};

// ---- the glyph caches ----

// A glyph in a cache (0x18 bytes): its box in the cache's bitmap and its metrics in pixels.
struct cGZFontCachedGlyph {
    int32_t left, top, right, bottom; // +0 the box (right / bottom exclusive)
    int16_t bearingX;                 // +0x10
    int16_t bearingY;                 // +0x12
    int16_t advance;                  // +0x14
    uint16_t pad;                     // +0x16
};
static_assert(sizeof(cGZFontCachedGlyph) == 0x18, "cGZFontCachedGlyph: 0x18 bytes");

// A glyph cache (0x38 bytes): an 8-bit bitmap the glyphs are packed into (grown by slot 65, filled by slot 64) and the
// glyphs by character code.
struct cGZFontGlyphCache {
    cGZFontGlyphCache() = default;    // 0x90a692 (the map: 0x90a612)
    ~cGZFontGlyphCache() { Clear(); } // 0x90a91b (then the map)
    cGZFontGlyphCache(const cGZFontGlyphCache &) = delete;
    cGZFontGlyphCache &operator=(const cGZFontGlyphCache &) = delete;

    // 0x90b6ba: frees the bitmap, zeroes the fields, forgets every glyph.
    void Clear();

    uint8_t *memory = nullptr;   // +0 (malloc'd by slot 65; freed with free)
    uint8_t *pixels = nullptr;   // +4
    uint32_t width = 0;          // +8
    uint32_t height = 0;         // +0xc
    uint32_t pitch = 0;          // +0x10
    uint32_t cursor = 0;         // +0x14
    std::unordered_map<uint32_t, cGZFontCachedGlyph> glyphs; // +0x18
};
static_assert(sizeof(cGZFontGlyphCache) == 0x38 && offsetof(cGZFontGlyphCache, glyphs) == 0x18, "cGZFontGlyphCache layout");

// ---- the font ----

class cGZFont : public cIGZFont {
public:
    static constexpr uint32_t kSize = 0x190;
    static constexpr uint32_t kMaxSize = 0x800;
    static constexpr uint32_t kDefaultChar = 0x20;

    cGZFont();                   // 0x90a7db
    ~cGZFont() override;         // 0x90a94e; deleting: slot 58 (0x90a9f1)
    cGZFont(const cGZFont &) = delete;
    cGZFont &operator=(const cGZFont &) = delete;

    static cGZFont *__cdecl Create(); // 0x909fb9: new 0x190, the constructor

    // ---- cIGZUnknown ----
    bool QueryInterface(uint32_t riid, void **out) override; // ids 0x10673960 and 1
    uint32_t AddRef() override;
    uint32_t Release() override; // at the last reference: Unload (slot 4), then the deleting destructor (slot 58)

    // ---- cIGZFont ----
    bool Load() override;
    bool Unload() override;
    void GetFamily(cIGZString *out) override;
    bool HasStyle(uint32_t flags) override;
    uint32_t GetStyle() override { return mStyle; }
    void SetStyle(uint32_t flags, bool set) override;
    uint32_t GetGroup() override { return mGroup; }
    uint32_t GetSize() override { return mSize; }
    bool SetSize(uint32_t size) override;
    uint32_t GetColour() override { return mColour; }
    uint32_t GetShadowColour() override { return mShadowColour; }
    void SetColours(uint32_t colour, uint32_t shadow) override;
    uint32_t GetQuality() override { return mQuality; }
    void SetQuality(uint32_t mode) override;
    float GetGamma() override { return mGamma; }
    void SetGamma(float gamma) override;
    float GetContrast() override { return mContrast; }
    void SetContrast(float contrast) override;
    void GetPath(cIGZString *out) override;
    void SetPath(const cIGZString *path) override;
    bool GetEncoding(uint32_t *out) override;
    bool SetEncoding(int32_t encoding) override;
    bool SupportsEncoding(uint32_t encoding) override;
    bool IsUnicodeGroup(uint32_t group) override;
    bool HasGlyphs(const void *text, uint32_t length) override;
    bool CacheGlyphs(const void *text, uint32_t length) override;
    void GetScale(float *x, float *y) override;
    void SetScale(float x, float y) override;
    void GetAdvanceScale(float *x, float *y) override;
    void SetAdvanceScale(float x, float y) override;
    void GetSpacing(int32_t *x, int32_t *y) override;
    void SetSpacing(int32_t x, int32_t y) override;
    int32_t GetLineSpacing() override;
    int32_t GetCharWidth(const void *text, uint32_t length) override;
    uint32_t GetLineHeight(uint32_t, uint32_t) override;
    int32_t GetAscent() override;
    int32_t GetDescent() override;
    int32_t GetCharAdvance(const void *text, uint32_t length) override;
    uint32_t BreakLine(const void *text, uint32_t length, uint32_t maxWidth, uint32_t *width, uint32_t mode) override;
    uint32_t FitChars(const void *text, uint32_t length, uint32_t width, uint32_t *used) override;
    uint32_t WrapLines(const void *text, uint32_t length, uint32_t width, uint8_t mode, uint8_t *overflow) override;
    uint32_t CountLines(const void *text, uint32_t length, uint32_t width) override;
    uint32_t WidthForLines(const void *text, uint32_t length, uint32_t height) override;
    bool FitRect(const void *text, uint32_t length, cGZRect *rect, int32_t lineWidth, int32_t maxWidth, int32_t maxHeight) override;
    void MeasureRect(const void *text, uint32_t length, cGZRect *rect, bool wrap, bool fixedWidth) override;
    void GetExtent(const void *text, uint32_t length, cGZRect *rect) override;
    uint32_t GetWidth(const void *text, uint32_t length) override;
    uint32_t GetTrimmedWidth(const void *text, uint32_t length, uint32_t *used) override;
    uint32_t ClipText(cIGZString *text, const cIGZString *suffix, const cGZRect *rect, bool wrap) override;
    bool DrawText(cIGZBuffer *buffer, int32_t x, int32_t y, const void *text, uint32_t length, uint32_t colour, uint32_t colour2,
                  const cGZRect *clip, int32_t *pos) override;
    bool DrawBox(cIGZBuffer *buffer, const cGZRect *rect, const void *text, uint32_t length, uint32_t align, uint32_t colour,
                 uint32_t colour2, const cGZRect *clip, int32_t *pos) override;
    cIGZFontRenderer *GetRenderer() override { return mRenderer; }
    void SetRenderer(cIGZFontRenderer *renderer) override;
    bool BeginDraw(cIGZBuffer *target) override;
    bool EndDraw(cIGZBuffer *target) override;
    int32_t GetCharBearing(const void *text, uint32_t length) override;
    bool WrapText(cIGZString *text, const cIGZString *suffix, const cGZRect *rect) override;
    cGZFontGlyphCache *GetGlyphCache() override;
    cGZFontCachedGlyph *FindGlyph(uint32_t code) override;
    cGZFontCachedGlyph *MakeGlyph(uint32_t code, cGZFontGlyphCache *cache) override;
    bool PlaceBitmap(cGZFontGlyphCache *cache, cGZFontCachedGlyph *glyph, uint32_t x, uint32_t y, const uint8_t *source,
                     uint32_t width, uint32_t height, uint32_t sourcePitch, uint32_t mode) override;
    bool GrowCache(cGZFontGlyphCache *cache, uint32_t width, uint32_t height) override;

    // ---- non-virtual ----
    // Style bits PlaceBitmap draws into the glyph (mStyle & 0xc): a line under the glyph and a line through it.
    static constexpr uint32_t kStyleUnderline = 4;
    static constexpr uint32_t kStyleStrikeout = 8;
    // 0x90d654 / 0x90d10a: the thickness in pixels of the underline and of the strike-through line, by the size.
    uint32_t UnderlineThickness() const;
    uint32_t StrikeoutThickness() const;
    // 0x90b6e1 (thiscall, 3 stack arguments): the first character of the text in the font's encoding as a UTF-16 unit
    // (0 for no text); *advance gets the units it takes.
    uint32_t CharAt(const void *text, uint32_t length, uint32_t *advance);
    // 0x90d6b7: whether the text holds a line feed or a carriage return.
    bool HasLineBreak(const void *text, uint32_t length);
    // 0x90da7d (cdecl): a space of the layout: 0x20 or U+2002 to U+200B.
    static bool IsSpace(uint32_t ch);
    // 0x90d4f5 (a thiscall that ignores the object, one stack argument): whether the UTF-16 unit is a Thai character the
    // shaper treats specially: 1 for the base letters and marks it counts, 3 for the tone marks, else 0.
    static uint32_t ThaiSign(uint32_t ch);
    // 0x1b1d25 (cdecl, one stack argument; hooked: a window class outside this one calls it too): the Thai class of a
    // character (0 outside U+0E00 to U+0E7F), see src/gz/cGZFontThai.size.cpp.
    static int32_t ThaiClass(uint32_t ch);
    // 0x90d324 (stdcall, 2 stack arguments, the character 16 bits): the presentation form of a Thai vowel or mark for a form
    // (1 raised, 2 shifted, 3 low) in the private-use block, or the character itself. The original's upper half of the
    // result is a left-over; its one caller stores 16 bits.
    static uint32_t ThaiForm(uint32_t ch, uint32_t form);
    // 0x90dd90 (thiscall, 5 stack arguments): shapes UTF-16 text for Thai (sara am taken apart into buffer 5, marks in their
    // raised, shifted or low forms in place) and gives the text and its units back; `recordWords` (its low byte) records the
    // map from shaped to source positions in mLayoutWords.
    void ShapeThai(uint16_t *text, uint32_t count, const uint16_t **outText, uint32_t *outCount, uint32_t recordWords);
    // 0x90dd14 (thiscall, 2 stack arguments): whether any unit of UTF-16 text needs the Thai shaper.
    bool HasThai(const uint16_t *text, uint32_t count);
    // 0x90b7c0 (thiscall, 3 stack arguments): the text in the font's encoding as UTF-16 in the buffer (resized to take
    // it); returns the units.
    uint32_t ToUtf16(const void *text, uint32_t count, std::vector<int16_t> &buffer);
    // 0x90d563 (thiscall, 6 stack arguments): the text as UTF-16 laid out in scratch buffer `index`: Thai text is shaped
    // (by ShapeThai) into buffer 5, with the map from shaped to source positions in mLayoutWords (cleared first when
    // `clearWords`); other text is converted, or used as it is when it is UTF-16 already.
    void Layout(const void *text, uint32_t count, const uint16_t **outText, uint32_t *outCount, uint32_t index, uint32_t clearWords);
    // 0x90ae25: the 256-entry coverage table from the gamma and the contrast (emptied when both are neutral); the
    // anti-aliased cache (B) is cleared.
    void BuildGammaTable();
    // 0x90ae06: the renderer's mode for a quality mode (2 and 3 as they are, anything else 1).
    static uint32_t RendererMode(uint32_t quality);
    // 0x90e536 (thiscall, 2 stack arguments): makes the colour ramp for a draw in the anti-aliased quality current
    // (mColourTable): 128 colours from `second` to `first`, in 16 rows of 8 (the first row `second`, the last `first`).
    // The four tables made last are kept in mBlocks, most recently used first, and found by their two colours.
    void SetColourRamp(uint32_t first, uint32_t second);
    // The engine's scale matrix for the size and the scale (SetSize and SetScale); returns the engine's error.
    int32_t ApplyTransform();

    // +0 the vtable
    bool mLoaded = false;              // +4
    int32_t mRefCount = 0;             // +8
    FontSysString mPath;               // +0xc
    FontSysString mFamily;             // +0x2c
    uint32_t mGroup = 0;               // +0x4c
    uint32_t mSize = 16;               // +0x50
    uint32_t mLineHeight = 16;         // +0x54
    uint32_t mStyle = 0;               // +0x58
    uint32_t mColour = 0;              // +0x5c
    uint32_t mShadowColour = 0;        // +0x60
    uint32_t mQuality = 1;             // +0x64 1: the 1-bit cache (A), else the anti-aliased one (B)
    uint32_t mUnknown68 = 2;           // +0x68 the draw mode of the anti-aliased draw when its second colour is 0x8000000 (DrawText)
    float mGamma = 1.0f;               // +0x6c
    float mContrast = 0.0f;            // +0x70
    std::vector<uint8_t> mGammaTable;  // +0x74 256 entries, or empty
    float mScaleX = 1.0f;              // +0x80
    float mScaleY = 1.0f;              // +0x84
    float mAdvanceScale = 1.0f;        // +0x88
    int32_t mSpacingX = 0;             // +0x8c
    int32_t mSpacingY = 0;             // +0x90
    uint32_t mEncoding = 0;            // +0x94
    int32_t mDrawDepth = 0;            // +0x98 BeginDraw / EndDraw nesting
    int32_t mAscent = 0;               // +0x9c
    int32_t mDescentLine = 0;          // +0xa0 (the engine's +0x7c, in pixels)
    cIGZFontRenderer *mRenderer = nullptr; // +0xa4 (counted)
    std::vector<int16_t> mLayout[6];   // +0xa8 the layout scratch buffers
    std::vector<uint32_t> mLayoutWords; // +0xf0
    uint32_t mDefaultChar = kDefaultChar; // +0xfc drawn for characters without a glyph
    uint32_t mDefaultGlyph = 0;        // +0x100 its glyph index
    cGZFontFile *mFile = nullptr;      // +0x104 (new 0x30)
    std::vector<cGZFontGlyph> mGlyphRun; // +0x108 the glyphs of a draw (game code)
    uint32_t mColourTable = 0;       // +0x114 the colour ramp the draw uses (a 0x200-byte table, one of mBlocks), or none
    std::list<uint8_t *> mBlocks;      // +0x118 blocks the drawing code keeps (freed with delete[] on Unload)
    cGZFontGlyphCache mCacheMono;      // +0x120 (A)
    cGZFontGlyphCache mCacheSmooth;    // +0x158 (B)
};

static_assert(sizeof(cGZFont) == cGZFont::kSize, "cGZFont size");
static_assert(offsetof(cGZFont, mLoaded) == 4 && offsetof(cGZFont, mRefCount) == 8 && offsetof(cGZFont, mPath) == 0xc &&
                  offsetof(cGZFont, mFamily) == 0x2c && offsetof(cGZFont, mGroup) == 0x4c && offsetof(cGZFont, mStyle) == 0x58 &&
                  offsetof(cGZFont, mQuality) == 0x64 && offsetof(cGZFont, mGamma) == 0x6c && offsetof(cGZFont, mGammaTable) == 0x74,
              "cGZFont layout");
static_assert(offsetof(cGZFont, mScaleX) == 0x80 && offsetof(cGZFont, mEncoding) == 0x94 && offsetof(cGZFont, mRenderer) == 0xa4 &&
                  offsetof(cGZFont, mLayout) == 0xa8 && offsetof(cGZFont, mLayoutWords) == 0xf0 &&
                  offsetof(cGZFont, mDefaultChar) == 0xfc && offsetof(cGZFont, mFile) == 0x104 &&
                  offsetof(cGZFont, mGlyphRun) == 0x108 && offsetof(cGZFont, mBlocks) == 0x118 &&
                  offsetof(cGZFont, mCacheMono) == 0x120 && offsetof(cGZFont, mCacheSmooth) == 0x158,
              "cGZFont layout");
