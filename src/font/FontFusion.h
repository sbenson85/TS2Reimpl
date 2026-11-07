// The font engine: our own code with Bitstream Font Fusion's (T2K's) call shapes, behind which FreeType reads, scales
// and renders the glyphs (docs/subsystems/rendering.md, "Standalone font engine"). The game linked Font Fusion into its
// image (db/third-party.jsonl); cGZFont (src/gz/cGZFont.size.cpp) is its only user and calls the 18 entry points below,
// which are hooked at their RVAs (src/hook/tables/FontFusion.cpp).
//
// The objects keep Font Fusion's layout where callers read or write them: the T2K scaler (fonts' metrics, the last
// rendered glyph, the name strings) and the sfnt class (the preferred cmap, the parsed 'head' table). Our own state
// follows the original's size. Errors go through the last argument, as Font Fusion's do (0: none; the codes are its
// 10000-based ones), and never unwind into the caller: Font Fusion longjmp'd to its entry point and tore the whole
// memory handler down; the shim leaves every object valid.
//
// FreeType: portions of this software are copyright (C) 2026 The FreeType Project (www.freetype.org). All rights
// reserved. FreeType is used under the FreeType License (FTL); our build compiles the modules in
// src/font/freetype/ts2_ftmodule.h with the options in ts2_ftoption.h (CMakeLists.txt, "FreeType").
#pragma once

#include <cstddef>
#include <cstdint>

namespace fontfusion {

// Font Fusion's error codes (the ones the shim reports).
enum : int32_t {
    kErrNone = 0,
    kErrNoMemhandler = 10000,    // NewT2K without a memory handler
    kErrNullMatrix = 0x2711,     // T2K_NewTransformation
    kErrBadResolution = 0x2712,  // a resolution under 1
    kErrGreyLevel = 0x2713,      // T2K_RenderGlyph: a grey level above 5
    kErrGlyphNotFound = 0x2715,  // no glyph for the code or index
    kErrBadT2KStamp = 0x2717,    // DeleteT2K on something that isn't a T2K object
    kErrBadMemhandler = 0x2723,  // a memory handler whose stamp is wrong
    kErrUnsupportedCmap = 0x2725, // the chosen cmap subtable has a format other than 0, 2, 4 or 6
    kErrRead = 0x2728,           // the input stream's reader failed
    kErrBadFontData = 0x2729,    // not a font Font Fusion reads, or a damaged one
};

// The input stream's reader: `count` bytes at `offset` into `dst`; 0 when it fails.
using ReadFunction = uint8_t(__cdecl *)(void *id, uint8_t *dst, uint32_t offset, uint32_t count);

// Font kinds (FF_FindFontTypeFromStream; New_sfntClass's `type`).
enum : uint16_t {
    kFontType1 = 1,         // a PC Type 1 file (PFB), then its pure form (ExtractPureT1FromPCType1)
    kFontTrueType = 2,      // an sfnt with 'loca' (TrueType outlines), a Mac 'true' font or a collection
    kFontOpenType = 7,      // an sfnt with 'DSIG' or 'CFF ' (OpenType)
    kFontCff = 0x16,        // a bare CFF font
    kFontUnknown = 0xffff,
};

// RenderGlyph's command bits (cGZFontFile::flags: 0x8a for kinds 2 and 7, 0x0b for the others).
enum : uint32_t {
    kGridFit = 0x01,
    kScanConvert = 0x02,     // make a bitmap
    kReturnOutlines = 0x04,
    kCodeIsGlyphIndex = 0x08,
    kUseFracPen = 0x10,      // fractional advances (the shim's are whole pixels, as cGZFont uses them)
    kNativeHints = 0x80,     // TrueType instructions
};

struct Memhandler;
struct InputStream;
struct SfntState;
struct T2KState;

// The 'head' table as Font Fusion parsed it (sfnt +0x40; cGZFontFile::Style reads macStyle).
struct HeadTable {
    uint8_t reserved00[0x16];
    uint16_t unitsPerEm;      // +0x16
    uint8_t reserved18[0x18];
    uint16_t macStyle;        // +0x30: bit 0 bold, bit 1 italic
    uint16_t lowestRecPPEM;   // +0x32
    uint8_t reserved34[2];
    int16_t indexToLocFormat; // +0x36
    int16_t glyphDataFormat;  // +0x38
    uint8_t reserved3a[2];
};
static_assert(sizeof(HeadTable) == 0x3c);

// Font Fusion's sfntClass: one font. Only the fields below are filled; the rest of the original's 0xdc bytes stay 0.
struct Sfnt {
    uint8_t reserved00[0x40];
    HeadTable *head;              // +0x40: null for Type 1 and CFF fonts
    uint8_t reserved44[0x34];
    uint16_t preferredPlatform;   // +0x78: the cmap platform the first lookup looks for (0xffff: any)
    uint16_t preferredEncoding;   // +0x7a: ... and encoding (cGZFont sets 3 / 1, Windows Unicode)
    int16_t underlinePosition;    // +0x7c: font units
    int16_t underlineThickness;   // +0x7e
    uint32_t isFixedPitch;        // +0x80
    uint16_t firstCharIndex;      // +0x84
    uint16_t lastCharIndex;       // +0x86
    int32_t xPixelsPerEm;         // +0x88: written by T2K_NewTransformation
    int32_t yPixelsPerEm;         // +0x8c
    uint8_t reserved90[0x30];
    InputStream *input;           // +0xc0
    uint8_t reservedC4[8];
    Memhandler *mem;              // +0xcc
    uint8_t reservedD0[8];
    uint32_t field_d8;            // +0xd8: 1 (NewT2K copies it to T2K +0x58)
    // ---- ours ----
    SfntState *state;             // +0xdc
};
static_assert(offsetof(Sfnt, head) == 0x40 && offsetof(Sfnt, preferredPlatform) == 0x78 &&
              offsetof(Sfnt, isFixedPitch) == 0x80 && offsetof(Sfnt, input) == 0xc0 && offsetof(Sfnt, mem) == 0xcc &&
              offsetof(Sfnt, state) == 0xdc);

// Font Fusion's T2K scaler: the font at a size and the last glyph rendered.
struct T2K {
    uint32_t stamp1;                    // +0x00: kT2KStamp1
    Memhandler *mem;                    // +0x04
    int32_t matrix[4];                  // +0x08: the normalised transform (16.16): xx, xy, yx, yy
    uint32_t field_18;
    uint32_t isIdentity;                // +0x1c: the matrix is the identity (the metrics skip it)
    uint8_t reserved20[0x28];
    uint32_t field_48;                  // +0x48: cGZFont sets 1
    uint32_t field_4c;
    char *nameString8;                  // +0x50: the last T2K_GetNameString's 8-bit result (null: none)
    uint16_t *nameString16;             // +0x54: ... its 16-bit result
    uint32_t field_58;                  // +0x58: sfnt +0xd8
    uint32_t isFixedPitch;              // +0x5c
    uint16_t firstCharCode;             // +0x60
    uint16_t lastCharCode;              // +0x62
    // The font-wide horizontal metrics at the current transform (16.16 pixels).
    uint32_t horizontalMetricsAreValid; // +0x64
    int32_t xAscender, yAscender;       // +0x68
    int32_t xDescender, yDescender;     // +0x70 (yDescender negative)
    int32_t xLineGap, yLineGap;         // +0x78
    int32_t xMaxLinearAdvanceWidth, yMaxLinearAdvanceWidth; // +0x80
    int32_t caretDx, caretDy;           // +0x88
    int32_t xUnderlinePosition, yUnderlinePosition;   // +0x90
    int32_t xUnderlineThickness, yUnderlineThickness; // +0x98
    uint32_t verticalMetricsAreValid;   // +0xa0
    int32_t vertical[10];               // +0xa4
    uint32_t field_cc;
    // The last rendered glyph.
    int32_t xAdvanceWidth16Dot16;       // +0xd0: whole pixels
    int32_t yAdvanceWidth16Dot16;       // +0xd4
    int32_t xLinearAdvanceWidth16Dot16; // +0xd8: the unrounded advance
    int32_t yLinearAdvanceWidth16Dot16; // +0xdc
    int32_t fTop26Dot6;                 // +0xe0: the bitmap's top edge above the baseline (whole pixels)
    int32_t fLeft26Dot6;                // +0xe4: its left edge from the origin
    uint8_t reservedE8[0x1c];
    void *glyph;                        // +0x104: Font Fusion's glyph outline (the shim keeps none)
    uint16_t glyphIndex;                // +0x108
    uint16_t field_10a;
    uint32_t width;                     // +0x10c: the bitmap, in pixels
    uint32_t height;                    // +0x110
    int32_t rowBytes;                   // +0x114
    uint8_t *baseAddr;                  // +0x118: null when the glyph has no outline
    void *baseARGB;                     // +0x11c
    uint32_t embeddedBitmapWasUsed;     // +0x120
    int32_t xPixelsPerEm, yPixelsPerEm; // +0x124: whole pixels
    int32_t xPixelsPerEm16Dot16, yPixelsPerEm16Dot16; // +0x12c
    int32_t xScale, yScale;             // +0x134: pixels per font unit (16.16)
    uint8_t reserved13c[0x28];
    int32_t field_164;                  // +0x164
    uint8_t reserved168[0x10];
    uint32_t enableSbits;               // +0x178
    uint16_t unitsPerEm;                // +0x17c
    uint16_t field_17e;
    Sfnt *font;                         // +0x180
    uint32_t field_184;
    uint32_t stamp2;                    // +0x188: kT2KStamp2
    // ---- ours ----
    T2KState *state;                    // +0x18c
};
static_assert(offsetof(T2K, nameString8) == 0x50 && offsetof(T2K, isFixedPitch) == 0x5c &&
              offsetof(T2K, yAscender) == 0x6c && offsetof(T2K, yDescender) == 0x74 && offsetof(T2K, yLineGap) == 0x7c &&
              offsetof(T2K, xMaxLinearAdvanceWidth) == 0x80 && offsetof(T2K, xAdvanceWidth16Dot16) == 0xd0 &&
              offsetof(T2K, fTop26Dot6) == 0xe0 && offsetof(T2K, fLeft26Dot6) == 0xe4 && offsetof(T2K, glyph) == 0x104 &&
              offsetof(T2K, width) == 0x10c && offsetof(T2K, baseAddr) == 0x118 && offsetof(T2K, xPixelsPerEm) == 0x124 &&
              offsetof(T2K, field_164) == 0x164 && offsetof(T2K, unitsPerEm) == 0x17c && offsetof(T2K, font) == 0x180 &&
              offsetof(T2K, stamp2) == 0x188 && offsetof(T2K, state) == 0x18c);

constexpr uint32_t kT2KStamp1 = 0x5a1234a5;
constexpr uint32_t kT2KStamp2 = 0xa5fedc5a;

// ---- the 18 entry points (cdecl, as in the image) ----
Memhandler *__cdecl tsi_NewMemhandler(int32_t *err);                                   // 0x9116c7
void __cdecl tsi_DeleteMemhandler(Memhandler *mem);                                    // 0x9115c0
InputStream *__cdecl New_NonRamInputStream(Memhandler *mem, void *id, ReadFunction read, uint32_t length,
                                           int32_t *err);                              // 0x911a54
InputStream *__cdecl New_InputStream3(Memhandler *mem, uint8_t *data, uint32_t length, int32_t *err); // 0x9119be
void __cdecl Delete_InputStream(InputStream *in, int32_t *err);                        // 0x91180d
uint16_t __cdecl FF_FindFontTypeFromStream(InputStream *in, int32_t *err);             // 0x91dc8d
uint8_t *__cdecl ExtractPureT1FromPCType1(uint8_t *data, uint32_t *length, int32_t *err); // 0x91359f
Sfnt *__cdecl New_sfntClass(Memhandler *mem, int16_t type, int32_t fontNum, InputStream *in, void *styling,
                            void *params, int32_t *err);                               // 0x9199cb
void __cdecl Delete_sfntClass(Sfnt *font, int32_t *err);                               // 0x9198e1
T2K *__cdecl NewT2K(Memhandler *mem, Sfnt *font, int32_t *err);                        // 0x91be9a
void __cdecl DeleteT2K(T2K *t, int32_t *err);                                          // 0x91bcf2
void __cdecl T2K_NewTransformation(T2K *t, int32_t doSetUpNow, int32_t xRes, int32_t yRes, int32_t *matrix,
                                   int32_t enableSbits, int32_t *err);                 // 0x91c300
uint32_t __cdecl T2K_GetGlyphIndex(T2K *t, uint32_t charCode, int32_t *err);           // 0x91c04e
uint32_t __cdecl T2K_GlyphExists(T2K *t, uint32_t charCode, uint8_t cmd, int32_t *err); // 0x91bdc8
uint32_t __cdecl T2K_GetCharCodeByName(T2K *t, const char *name, int32_t *err);        // 0x91be55
void __cdecl T2K_RenderGlyph(T2K *t, uint32_t code, int32_t xFracPenDelta, int32_t yFracPenDelta,
                             uint32_t greyScaleLevel, uint32_t cmd, int32_t *err);     // 0x91c9a8
void __cdecl T2K_PurgeMemory(T2K *t, int32_t level, int32_t *err);                    // 0x91c8ba
void __cdecl T2K_GetNameString(T2K *t, uint32_t languageId, uint32_t nameId, int32_t *err); // 0x91da2f

// Font Fusion's fixed-point helpers as the metrics use them (16.16).
int32_t FixMul(int32_t a, int32_t b);
int32_t FixDiv(int32_t a, int32_t b);

} // namespace fontfusion
