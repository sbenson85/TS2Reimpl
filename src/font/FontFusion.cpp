// The font engine behind Font Fusion's entry points (src/font/FontFusion.h).
#include "font/FontFusion.h"

#include "font/FontFusionType1.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_ADVANCES_H
#include FT_SIZES_H
#include FT_DRIVER_H
#include FT_MODULE_H
#include FT_OUTLINE_H
#include FT_SFNT_NAMES_H
#include FT_TRUETYPE_TABLES_H
#include FT_TRUETYPE_TAGS_H
#include FT_TYPE1_TABLES_H

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace fontfusion {

// ---- objects ----

constexpr uint32_t kMemhandlerStamp = 0xaa005501;

struct Memhandler {
    uint32_t stamp = kMemhandlerStamp;
    FT_Library library = nullptr;
    std::vector<InputStream *> streams; // the objects made on this handler and not deleted yet
    std::vector<Sfnt *> fonts;
    std::vector<T2K *> scalers;
};

struct InputStream {
    Memhandler *mem = nullptr;
    const uint8_t *data = nullptr; // a stream over memory (New_InputStream3)
    uint32_t length = 0;
    void *id = nullptr;            // ... or one read through `read` (New_NonRamInputStream)
    ReadFunction read = nullptr;
    bool readFailed = false;
};

struct SfntState {
    uint16_t type = 0;
    FT_StreamRec stream = {};  // FreeType's view of the input stream
    FT_Face face = nullptr;
    HeadTable head = {};
    bool cffOutlines = false;   // CFF outlines (a bare CFF or an OpenType font with 'CFF ')
    bool hasPrep = false;       // a TrueType control value program ('prep')
    uint16_t unitsPerEm = 0;
    // Type 1: the program as Font Fusion read it, its glyphs' FreeType indices and its data (when read from a stream).
    Type1Program type1;
    std::vector<FT_UInt> type1Glyphs;
    std::vector<uint8_t> ownedData;
    // The font-wide metrics in font units (Font Fusion's GetFontWideMetrics).
    bool horizontalValid = false;
    int16_t ascender = 0, descender = 0, lineGap = 0, maxAdvance = 0;
    int32_t caretX = 0, caretY = 0;
    bool verticalValid = false;
    int16_t vAscender = 0, vDescender = 0, vLineGap = 0, vMaxAdvance = 0;
    int32_t vCaretX = 0, vCaretY = 0;
    // The cmap Font Fusion would use, chosen at the first lookup (sfnt +0x60).
    bool cmapChosen = false;
    FT_CharMap cmap = nullptr;
};

struct T2KState {
    FT_Size size = nullptr;
    std::vector<uint8_t> bitmap; // the last rendered glyph's (baseAddr points into it)
};

namespace {

// 26.6 values to whole pixels.
FT_Pos PixFloor(FT_Pos v)
{
    return v & ~63;
}
FT_Pos PixCeil(FT_Pos v)
{
    return (v + 63) & ~63;
}
FT_Pos PixRound(FT_Pos v)
{
    return (v + 32) & ~63;
}

void SetError(int32_t *err, int32_t code)
{
    if (err) *err = code;
}

template <class T> void Forget(std::vector<T *> &list, T *item)
{
    const auto at = std::find(list.begin(), list.end(), item);
    if (at != list.end()) list.erase(at);
}

uint16_t ReadU16(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0] << 8 | p[1]);
}

uint32_t ReadU32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3];
}

// Reads `count` bytes at `offset` (those past the end read as 0). False when the reader fails.
bool ReadAt(InputStream *in, uint32_t offset, uint8_t *dst, uint32_t count)
{
    std::memset(dst, 0, count);
    if (offset >= in->length) return true;
    const uint32_t n = std::min(count, in->length - offset);
    if (in->data) {
        std::memcpy(dst, in->data + offset, n);
        return true;
    }
    if (in->read && in->read(in->id, dst, offset, n)) return true;
    in->readFailed = true;
    return false;
}

unsigned long StreamRead(FT_Stream stream, unsigned long offset, unsigned char *buffer, unsigned long count)
{
    InputStream *const in = static_cast<InputStream *>(stream->descriptor.pointer);
    if (count == 0) return offset <= in->length ? 0 : 1; // a seek: 0 when it succeeds
    if (offset >= in->length) return 0;
    const uint32_t n = static_cast<uint32_t>(std::min<unsigned long>(count, in->length - offset));
    if (!in->read(in->id, buffer, static_cast<uint32_t>(offset), n)) {
        in->readFailed = true;
        return 0;
    }
    return n;
}

// Whether the sfnt directory at the start of the stream lists any of `tags`.
bool SfntHasTable(InputStream *in, std::initializer_list<uint32_t> tags)
{
    uint8_t header[12];
    if (!ReadAt(in, 0, header, sizeof header)) return false;
    const uint16_t count = ReadU16(header + 4);
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t entry[16];
        if (12 + 16 * i >= in->length || !ReadAt(in, 12 + 16 * i, entry, sizeof entry)) return false;
        for (const uint32_t tag : tags)
            if (ReadU32(entry) == tag) return true;
    }
    return false;
}

bool IsSfntHeader(const uint8_t *b)
{
    return (b[0] == 0 && (b[1] == 1 || b[1] == 2) && b[2] == 0 && b[3] == 0) ||
           (b[0] == 'O' && b[1] == 'T' && b[2] == 'T' && b[3] == 'O');
}

constexpr uint32_t Tag(char a, char b, char c, char d)
{
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) << 24 | static_cast<uint32_t>(static_cast<uint8_t>(b)) << 16 |
           static_cast<uint32_t>(static_cast<uint8_t>(c)) << 8 | static_cast<uint8_t>(d);
}

// Font Fusion scales font units to 26.6 pixels with whole pixels per em: round(units * ppem * 64 / unitsPerEm).
// The fraction (ppem * 64) / unitsPerEm is first reduced while both are even; a numerator of 16 bits or more goes
// through a 16.16 factor, a power-of-two denominator through a rounding shift (rounding half up), any other through a
// division rounding half away from zero.
int32_t ScaleUnits(int32_t units, int32_t ppem, int32_t unitsPerEm)
{
    int32_t num = ppem << 6, den = unitsPerEm;
    if (den <= 0) return 0;
    while (((num | den) & 1) == 0) {
        num >>= 1;
        den >>= 1;
    }
    if (num <= -0x8000 || num >= 0x8000) return FixMul(FixDiv(num, den), units);
    const int32_t product = units * num;
    if ((den & (den - 1)) == 0) {
        int shift = 0;
        while ((den >> shift) != 1) ++shift;
        return (product + (den >> 1)) >> shift;
    }
    if (product < 0) return -(((den >> 1) - product) / den);
    return (product + (den >> 1)) / den;
}

int32_t SineFixed(int32_t degrees16)
{
    const double degrees = std::clamp(degrees16 / 65536.0, 0.0, 90.0);
    return std::min<int32_t>(0x10000, static_cast<int32_t>(std::lround(std::sin(degrees * 3.14159265358979323846 / 180.0) * 65536.0)));
}

// The length of (a, b) in 16.16 as Font Fusion took it: max + min / 2 as the first guess, then three Newton steps
// r = (r + a * (a / r) + b * (b / r) + 1) / 2 in its fixed-point arithmetic (so the result matches it to the last bit).
int32_t Length(int32_t a, int32_t b)
{
    a = std::abs(a);
    b = std::abs(b);
    if (a == 0) return b;
    if (b == 0) return a;
    int32_t r = b < a ? a + (b >> 1) : b + (a >> 1);
    for (int i = 0; i < 3; ++i) r = (r + FixMul(b, FixDiv(b, r)) + FixMul(a, FixDiv(a, r)) + 1) >> 1;
    return r;
}

void FreeNames(T2K *t)
{
    std::free(t->nameString8);
    t->nameString8 = nullptr;
    std::free(t->nameString16);
    t->nameString16 = nullptr;
}

void FreeBitmap(T2K *t)
{
    t->baseAddr = nullptr;
    t->state->bitmap.clear();
    t->state->bitmap.shrink_to_fit();
}

void DestroyT2K(T2K *t)
{
    FreeNames(t);
    FreeBitmap(t);
    if (t->state->size) FT_Done_Size(t->state->size);
    delete t->state;
    t->stamp1 = t->stamp2 = 0;
    delete t;
}

void DestroySfnt(Sfnt *font)
{
    if (font->state->face) FT_Done_Face(font->state->face);
    delete font->state;
    delete font;
}

// ---- the cmap Font Fusion would choose ----

bool IsReadFormat(uint16_t format)
{
    return format == 0 || format == 2 || format == 4 || format == 6;
}

// Chooses among the font's cmap subtables as Font Fusion did: the first of format 0, 2, 4 or 6 for the preferred
// platform and encoding (either may be 0xffff, any), else the first of format 0, then 2, then 4, then 6, else the first.
// Returns false (Font Fusion's error 0x2725) when that one has another format.
bool ChooseCmap(Sfnt *font)
{
    SfntState *const s = font->state;
    FT_ULong length = 0;
    if (FT_Load_Sfnt_Table(s->face, TTAG_cmap, 0, nullptr, &length) != 0 || length < 4) {
        s->cmapChosen = true; // no cmap: every lookup finds nothing
        return true;
    }
    std::vector<uint8_t> table(length);
    FT_Load_Sfnt_Table(s->face, TTAG_cmap, 0, table.data(), &length);
    const uint32_t count = std::min<uint32_t>(ReadU16(&table[2]), static_cast<uint32_t>((length - 4) / 8));
    if (count == 0) {
        s->cmapChosen = true;
        return true;
    }
    struct Subtable {
        uint16_t platform, encoding, format;
    };
    std::vector<Subtable> subtables(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t *const r = &table[4 + 8 * i];
        const uint32_t offset = ReadU32(r + 4);
        subtables[i] = {ReadU16(r), ReadU16(r + 2),
                        static_cast<uint16_t>(offset + 2 <= length ? ReadU16(&table[offset]) : 0xffff)};
    }
    const int16_t platform = static_cast<int16_t>(font->preferredPlatform);
    const int16_t encoding = static_cast<int16_t>(font->preferredEncoding);
    uint32_t chosen = 0;
    bool found = false;
    for (uint32_t i = 0; i < count && !found; ++i) {
        const Subtable &st = subtables[i];
        const int16_t p = static_cast<int16_t>(st.platform), e = static_cast<int16_t>(st.encoding);
        if (IsReadFormat(st.format) &&
            ((p == platform && (e == encoding || encoding == -1)) || (e == encoding && (p == platform || platform == -1)))) {
            chosen = i;
            found = true;
        }
    }
    for (const uint16_t format : {0, 2, 4, 6}) {
        for (uint32_t i = 0; i < count && !found; ++i) {
            if (subtables[i].format == format) {
                chosen = i;
                found = true;
            }
        }
    }
    const Subtable &st = subtables[chosen];
    if (!IsReadFormat(st.format)) return false;
    // FreeType's charmap for that subtable: the same platform, encoding and format, as many times in.
    uint32_t nth = 0;
    for (uint32_t i = 0; i < chosen; ++i)
        if (subtables[i].platform == st.platform && subtables[i].encoding == st.encoding && subtables[i].format == st.format) ++nth;
    for (FT_Int i = 0; i < s->face->num_charmaps; ++i) {
        FT_CharMap cm = s->face->charmaps[i];
        if (cm->platform_id == st.platform && cm->encoding_id == st.encoding &&
            FT_Get_CMap_Format(cm) == static_cast<FT_Long>(st.format) && nth-- == 0) {
            s->cmap = cm;
            break;
        }
    }
    s->cmapChosen = true;
    return true;
}

// ---- lookups ----

// The glyph index of a character code (Font Fusion's GetSfntClassGlyphIndex); `found` for T2K_GlyphExists.
uint32_t GlyphIndexOf(Sfnt *font, uint32_t code, bool *found, int32_t *err)
{
    SfntState *const s = font->state;
    *found = false;
    code &= 0xffff; // character codes are 16 bits
    if (s->type == kFontType1) return s->type1.GlyphIndex(static_cast<uint16_t>(code), found);
    if (s->type == kFontCff) {
        // A bare CFF font: its encoding (FreeType's charmap for it).
        if (!s->face->charmap) return 0;
        const FT_UInt index = FT_Get_Char_Index(s->face, code & 0xffff);
        *found = index != 0;
        return index;
    }
    if (!s->cmapChosen && !ChooseCmap(font)) {
        SetError(err, kErrUnsupportedCmap);
        return 0;
    }
    if (!s->cmap) return 0;
    if (s->face->charmap != s->cmap) FT_Set_Charmap(s->face, s->cmap);
    return FT_Get_Char_Index(s->face, code);
}

// The code of a glyph name (Type 1: Font Fusion's own; CFF: the encoding's code for the named glyph).
uint32_t CodeOfName(Sfnt *font, const char *name)
{
    SfntState *const s = font->state;
    if (s->type == kFontType1) return s->type1.CodeByName(name);
    if (!s->cffOutlines || !s->face) return 0;
    const FT_UInt glyph = FT_Get_Name_Index(s->face, name);
    if (glyph == 0 || !s->face->charmap) return 0;
    FT_UInt index = 0;
    for (FT_ULong code = FT_Get_First_Char(s->face, &index); index != 0; code = FT_Get_Next_Char(s->face, code, &index))
        if (index == glyph) return static_cast<uint32_t>(code & 0xffff);
    return 0;
}

// The FreeType glyph for Font Fusion's glyph index, or -1.
int64_t FreeTypeGlyph(const SfntState *s, uint32_t index)
{
    if (s->type == kFontType1) return index < s->type1Glyphs.size() ? static_cast<int64_t>(s->type1Glyphs[index]) : -1;
    return index < static_cast<uint32_t>(s->face->num_glyphs) ? static_cast<int64_t>(index) : -1;
}

// The font-wide metrics in font units, as Font Fusion took them for each kind of font.
void ReadFontWideMetrics(Sfnt *font)
{
    SfntState *const s = font->state;
    FT_Face face = s->face;
    if (s->type == kFontType1) {
        const Type1Program &p = s->type1;
        s->horizontalValid = true;
        // Without "/ascent" and "/descent": the top of 'f' and the bottom of 'g' (their outlines' points), else 750
        // and -250. The line gap is what is left of the em.
        int32_t ascent = p.ascent, descent = p.descent;
        auto extreme = [&](uint16_t code, bool top, int32_t *out) {
            const int32_t glyph = p.GlyphForMetrics(code);
            if (glyph < 0 || static_cast<size_t>(glyph) >= s->type1Glyphs.size()) return;
            if (FT_Load_Glyph(face, s->type1Glyphs[glyph], FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING) != 0) return;
            const FT_Outline &o = face->glyph->outline;
            if (o.n_points <= 0) return;
            FT_Pos best = o.points[0].y;
            for (int i = 1; i < o.n_points; ++i) best = top ? std::max(best, o.points[i].y) : std::min(best, o.points[i].y);
            *out = static_cast<int16_t>(best);
        };
        if (ascent == 0x7fff) extreme('f', true, &ascent);
        if (descent == 0x7fff) extreme('g', false, &descent);
        if (ascent == 0x7fff) ascent = 750;
        if (descent == 0x7fff) descent = -250;
        const int32_t gap = std::max(0, descent + (p.unitsPerEm - ascent));
        s->ascender = static_cast<int16_t>(ascent);
        s->descender = static_cast<int16_t>(descent);
        s->lineGap = static_cast<int16_t>(gap);
        s->maxAdvance = static_cast<int16_t>(p.unitsPerEm);
        s->caretX = 0;
        s->caretY = 0x10000;
        if (p.italicAngle != 0) {
            const int32_t angle = std::abs(p.italicAngle);
            s->caretX = SineFixed(angle);
            s->caretY = SineFixed(0x5a0000 - angle);
        }
        return;
    }
    if (s->cffOutlines) {
        // Font Fusion took these from its CFF reader; FreeType's are from the same Top DICT.
        s->horizontalValid = true;
        s->ascender = static_cast<int16_t>(face->ascender);
        s->descender = static_cast<int16_t>(face->descender);
        s->lineGap = static_cast<int16_t>(std::max<int32_t>(0, face->height - (face->ascender - face->descender)));
        s->maxAdvance = static_cast<int16_t>(face->max_advance_width);
        s->caretX = 0;
        s->caretY = 0x10000;
        PS_FontInfoRec info;
        if (FT_Get_PS_Font_Info(face, &info) == 0 && info.italic_angle != 0) {
            const int32_t angle = std::abs(static_cast<int32_t>(info.italic_angle));
            s->caretX = SineFixed(angle);
            s->caretY = SineFixed(0x5a0000 - angle);
        }
        return;
    }
    // TrueType: 'hhea' and 'vhea'; the caret slope doubled while both parts stay within 16 bits.
    auto caret = [](int32_t run, int32_t rise, int32_t *x, int32_t *y) {
        for (int i = 0; i < 16 && run >= -0xffff && run <= 0xffff && rise >= -0xffff && rise <= 0xffff; ++i) {
            run *= 2;
            rise *= 2;
        }
        *x = run;
        *y = rise;
    };
    if (const TT_HoriHeader *h = static_cast<TT_HoriHeader *>(FT_Get_Sfnt_Table(face, FT_SFNT_HHEA))) {
        s->horizontalValid = true;
        s->ascender = h->Ascender;
        s->descender = h->Descender;
        s->lineGap = h->Line_Gap;
        s->maxAdvance = static_cast<int16_t>(h->advance_Width_Max);
        caret(h->caret_Slope_Run, h->caret_Slope_Rise, &s->caretX, &s->caretY);
    }
    if (const TT_VertHeader *v = static_cast<TT_VertHeader *>(FT_Get_Sfnt_Table(face, FT_SFNT_VHEA))) {
        s->verticalValid = true;
        s->vAscender = v->Ascender;
        s->vDescender = v->Descender;
        s->vLineGap = v->Line_Gap;
        s->vMaxAdvance = static_cast<int16_t>(v->advance_Height_Max);
        caret(v->caret_Slope_Run, v->caret_Slope_Rise, &s->vCaretX, &s->vCaretY);
    }
}

// Font Fusion's two metric transforms: a horizontal (x) or vertical (y) distance in font units, scaled, then through
// the transform unless it is the identity.
void TransformX(const T2K *t, int16_t units, int32_t *x, int32_t *y)
{
    const int32_t v = FixMul(static_cast<int32_t>(units) << 16, t->xScale);
    *x = v;
    *y = 0;
    if (!(t->matrix[0] == 0x10000 && t->matrix[1] == 0 && t->matrix[2] == 0 && t->matrix[3] == 0x10000)) {
        *x = FixMul(t->matrix[0], v);
        *y = FixMul(t->matrix[2], v);
    }
}

void TransformY(const T2K *t, int16_t units, int32_t *x, int32_t *y)
{
    const int32_t v = FixMul(static_cast<int32_t>(units) << 16, t->yScale);
    *x = 0;
    *y = v;
    if (!(t->matrix[0] == 0x10000 && t->matrix[1] == 0 && t->matrix[2] == 0 && t->matrix[3] == 0x10000)) {
        *x = FixMul(t->matrix[1], v);
        *y = FixMul(t->matrix[3], v);
    }
}

bool IsIdentity(const T2K *t)
{
    return t->matrix[0] == 0x10000 && t->matrix[1] == 0 && t->matrix[2] == 0 && t->matrix[3] == 0x10000;
}

// Where an outline (26.6) crosses the lines through pixel centres: Font Fusion's scan converter sizes the bitmap from
// the crossings on the horizontal lines (their x, the lines' y) and on the vertical ones (the lines' x, their y), so a
// feature that crosses no centre line (a thin dot at a small size) adds nothing to the box.
struct ScanBounds {
    bool any = false;
    double xMin = 0, xMax = 0, yMin = 0, yMax = 0;
    void Add(double x, double y)
    {
        if (!any) {
            any = true;
            xMin = xMax = x;
            yMin = yMax = y;
            return;
        }
        xMin = std::min(xMin, x);
        xMax = std::max(xMax, x);
        yMin = std::min(yMin, y);
        yMax = std::max(yMax, y);
    }
};

struct ScanState {
    ScanBounds bounds;
    bool columns = true;
    double x = 0, y = 0;
};

// The centre lines k * 64 + 32 in [low, high).
template <class F> void ForCentres(double low, double high, F f)
{
    int32_t k = static_cast<int32_t>(std::floor((low - 32.0) / 64.0));
    while (k * 64.0 + 32.0 < low) ++k;
    for (; k * 64.0 + 32.0 < high; ++k) f(k * 64.0 + 32.0);
}

void ScanLine(ScanState *s, double x1, double y1)
{
    const double x0 = s->x, y0 = s->y;
    s->x = x1;
    s->y = y1;
    if (y0 != y1)
        ForCentres(std::min(y0, y1), std::max(y0, y1), [&](double yc) { s->bounds.Add(x0 + (yc - y0) * (x1 - x0) / (y1 - y0), yc); });
    if (s->columns && x0 != x1)
        ForCentres(std::min(x0, x1), std::max(x0, x1), [&](double xc) { s->bounds.Add(xc, y0 + (xc - x0) * (y1 - y0) / (x1 - x0)); });
}

int ScanMoveTo(const FT_Vector *to, void *user)
{
    ScanState *const s = static_cast<ScanState *>(user);
    s->x = static_cast<double>(to->x);
    s->y = static_cast<double>(to->y);
    return 0;
}

int ScanLineTo(const FT_Vector *to, void *user)
{
    ScanLine(static_cast<ScanState *>(user), static_cast<double>(to->x), static_cast<double>(to->y));
    return 0;
}

// Curves as short lines (an eighth of a pixel or less along the control polygon).
int CurveSteps(double length)
{
    return std::clamp(static_cast<int>(length / 8.0) + 1, 1, 256);
}

int ScanConicTo(const FT_Vector *control, const FT_Vector *to, void *user)
{
    ScanState *const s = static_cast<ScanState *>(user);
    const double x0 = s->x, y0 = s->y, cx = static_cast<double>(control->x), cy = static_cast<double>(control->y);
    const double x2 = static_cast<double>(to->x), y2 = static_cast<double>(to->y);
    const int steps = CurveSteps(std::hypot(cx - x0, cy - y0) + std::hypot(x2 - cx, y2 - cy));
    for (int i = 1; i <= steps; ++i) {
        const double u = static_cast<double>(i) / steps, v = 1.0 - u;
        ScanLine(s, v * v * x0 + 2 * u * v * cx + u * u * x2, v * v * y0 + 2 * u * v * cy + u * u * y2);
    }
    return 0;
}

int ScanCubicTo(const FT_Vector *c1, const FT_Vector *c2, const FT_Vector *to, void *user)
{
    ScanState *const s = static_cast<ScanState *>(user);
    const double x0 = s->x, y0 = s->y;
    const double ax = static_cast<double>(c1->x), ay = static_cast<double>(c1->y);
    const double bx = static_cast<double>(c2->x), by = static_cast<double>(c2->y);
    const double x3 = static_cast<double>(to->x), y3 = static_cast<double>(to->y);
    const int steps = CurveSteps(std::hypot(ax - x0, ay - y0) + std::hypot(bx - ax, by - ay) + std::hypot(x3 - bx, y3 - by));
    for (int i = 1; i <= steps; ++i) {
        const double u = static_cast<double>(i) / steps, v = 1.0 - u;
        ScanLine(s, v * v * v * x0 + 3 * u * v * v * ax + 3 * u * u * v * bx + u * u * u * x3,
                 v * v * v * y0 + 3 * u * v * v * ay + 3 * u * u * v * by + u * u * u * y3);
    }
    return 0;
}

ScanBounds ScanOutline(FT_Outline &outline, bool columns)
{
    static const FT_Outline_Funcs funcs = {&ScanMoveTo, &ScanLineTo, &ScanConicTo, &ScanCubicTo, 0, 0};
    ScanState state;
    state.columns = columns;
    FT_Outline_Decompose(&outline, &funcs, &state);
    // FT_Outline_Decompose closes each contour with a line back to its start.
    return state.bounds;
}

} // namespace

int32_t FixMul(int32_t a, int32_t b)
{
    const bool negative = (a < 0) != (b < 0);
    const uint64_t magnitude = (static_cast<uint64_t>(std::abs(static_cast<int64_t>(a))) *
                                static_cast<uint64_t>(std::abs(static_cast<int64_t>(b)))) >> 16;
    const int32_t value = static_cast<int32_t>(static_cast<uint32_t>(magnitude));
    return negative ? -value : value;
}

int32_t FixDiv(int32_t a, int32_t b)
{
    if (b == 0) return a < 0 ? INT32_MIN : INT32_MAX;
    const bool negative = (a < 0) != (b < 0);
    uint32_t num = static_cast<uint32_t>(std::abs(static_cast<int64_t>(a)));
    uint32_t den = static_cast<uint32_t>(std::abs(static_cast<int64_t>(b)));
    const uint32_t whole = num / den;
    uint32_t rest = num % den;
    // The remainder is brought under 16 bits before the fraction is taken, the divisor halved with it.
    while (rest > 0xffff) {
        rest >>= 1;
        den = static_cast<uint32_t>(static_cast<int32_t>(den) >> 1);
    }
    const int32_t value = static_cast<int32_t>((rest << 16) / den + whole * 0x10000u);
    return negative ? -value : value;
}

// ---- memory handler and input streams ----

Memhandler *__cdecl tsi_NewMemhandler(int32_t *err)
{
    SetError(err, kErrNone);
    Memhandler *const mem = new (std::nothrow) Memhandler;
    if (!mem) {
        SetError(err, 0x2718);
        return nullptr;
    }
    if (FT_Init_FreeType(&mem->library) != 0) {
        delete mem;
        SetError(err, 0x2718);
        return nullptr;
    }
    // TrueType instructions as the classic (Windows 98-era) rasterisers ran them, which Font Fusion followed.
    FT_UInt interpreter = TT_INTERPRETER_VERSION_35;
    FT_Property_Set(mem->library, "truetype", "interpreter-version", &interpreter);
    return mem;
}

void __cdecl tsi_DeleteMemhandler(Memhandler *mem)
{
    if (!mem) return;
    // Whatever was made on the handler goes with it, as Font Fusion freed every block it handed out.
    for (T2K *t : mem->scalers) DestroyT2K(t);
    for (Sfnt *font : mem->fonts) DestroySfnt(font);
    for (InputStream *in : mem->streams) delete in;
    FT_Done_FreeType(mem->library);
    mem->stamp = 0;
    delete mem;
}

InputStream *__cdecl New_NonRamInputStream(Memhandler *mem, void *id, ReadFunction read, uint32_t length, int32_t *err)
{
    SetError(err, kErrNone);
    InputStream *const in = new InputStream;
    in->mem = mem;
    in->id = id;
    in->read = read;
    in->length = length;
    if (mem) mem->streams.push_back(in);
    return in;
}

InputStream *__cdecl New_InputStream3(Memhandler *mem, uint8_t *data, uint32_t length, int32_t *err)
{
    SetError(err, kErrNone);
    InputStream *const in = new InputStream;
    in->mem = mem;
    in->data = data;
    in->length = length;
    if (mem) mem->streams.push_back(in);
    return in;
}

void __cdecl Delete_InputStream(InputStream *in, int32_t *err)
{
    SetError(err, kErrNone);
    if (!in) return;
    if (in->mem) Forget(in->mem->streams, in);
    delete in;
}

uint16_t __cdecl FF_FindFontTypeFromStream(InputStream *in, int32_t *err)
{
    SetError(err, kErrNone);
    uint8_t head[0x96];
    if (!ReadAt(in, 0, head, sizeof head)) {
        SetError(err, kErrRead);
        return static_cast<uint16_t>(kErrRead);
    }
    // OpenType: an sfnt with a digital signature or CFF outlines.
    if (IsSfntHeader(head) && SfntHasTable(in, {Tag('D', 'S', 'I', 'G'), Tag('C', 'F', 'F', ' ')})) return kFontOpenType;
    // TrueType: an sfnt with glyph locations, a Mac 'true' font or a collection.
    if (IsSfntHeader(head) && SfntHasTable(in, {Tag('l', 'o', 'c', 'a'), Tag('s', 'l', 'o', 'c')})) return kFontTrueType;
    if (head[0] == 't' && ((head[1] == 't' && head[2] == 'c' && head[3] == 'f') || (head[1] == 'r' && head[2] == 'u' && head[3] == 'e')))
        return kFontTrueType;
    // A bare CFF: major version 1, then a name INDEX whose first name fits the first 150 bytes and ends there.
    if (head[0] == 1) {
        const uint32_t headerSize = head[2];
        if (headerSize + 4 <= 0x95) {
            const uint8_t first = head[4 + headerSize];
            if (first != 0 && static_cast<int32_t>(first) < static_cast<int32_t>(0x92 - headerSize) && head[4 + first + headerSize] == 0)
                return kFontCff;
        }
    }
    // Type 1: "%!" within the first 15 bytes (a PFB's segment header comes first).
    for (int i = 0; i < 14; ++i)
        if (head[i] == '%') return head[i + 1] == '!' ? kFontType1 : kFontUnknown;
    return kFontUnknown;
}

uint8_t *__cdecl ExtractPureT1FromPCType1(uint8_t *data, uint32_t *length, int32_t *err)
{
    SetError(err, kErrBadFontData);
    const uint32_t total = *length;
    uint32_t in = 0, out = 0;
    // PFB segments: 0x80, a type (1 text, 2 binary, 3 the end), a 32-bit length, the bytes. The bytes move down over
    // the segment headers.
    while (in < total) {
        if (data[in] != 0x80) return nullptr;
        if (in + 1 < total && data[in + 1] == 3) break;
        if (in + 6 > total) return nullptr;
        uint32_t n;
        std::memcpy(&n, data + in + 2, 4);
        in += 6;
        if (n > total || n > total - in) return nullptr;
        std::memmove(data + out, data + in, n);
        in += n;
        out += n;
    }
    SetError(err, kErrNone);
    *length = out;
    return data;
}

// ---- fonts ----

Sfnt *__cdecl New_sfntClass(Memhandler *mem, int16_t type, int32_t fontNum, InputStream *in, void *styling, void *params,
                            int32_t *err)
{
    (void)styling;
    (void)params;
    if (!err) return nullptr;
    SetError(err, kErrNone);
    if (!mem || !in) {
        SetError(err, kErrBadFontData);
        return nullptr;
    }
    Sfnt *const font = new Sfnt();
    font->state = new SfntState();
    SfntState *const s = font->state;
    s->type = static_cast<uint16_t>(type);
    font->input = in;
    font->mem = mem;
    font->field_d8 = 1;
    font->xPixelsPerEm = font->yPixelsPerEm = -1;

    FT_Open_Args args = {};
    const uint8_t *programData = in->data;
    uint32_t programLength = in->length;
    if (s->type == kFontType1 && !in->data) {
        // A Type 1 program read through a reader: kept whole, as FreeType and the code scan both need it.
        s->ownedData.resize(in->length);
        if (in->length && !ReadAt(in, 0, s->ownedData.data(), in->length)) {
            DestroySfnt(font);
            SetError(err, kErrRead);
            return nullptr;
        }
        programData = s->ownedData.data();
    }
    if (programData) {
        args.flags = FT_OPEN_MEMORY;
        args.memory_base = programData;
        args.memory_size = static_cast<FT_Long>(programLength);
    } else {
        s->stream.size = in->length;
        s->stream.descriptor.pointer = in;
        s->stream.read = &StreamRead;
        args.flags = FT_OPEN_STREAM;
        args.stream = &s->stream;
    }
    bool known = s->type == kFontType1 || s->type == kFontTrueType || s->type == kFontOpenType || s->type == kFontCff;
    if (!known || FT_Open_Face(mem->library, &args, fontNum, &s->face) != 0 || !FT_IS_SCALABLE(s->face)) {
        DestroySfnt(font);
        SetError(err, in->readFailed ? kErrRead : kErrBadFontData);
        return nullptr;
    }
    FT_Face face = s->face;

    if (s->type == kFontType1) {
        // Codes for StandardEncoding / ISOLatin1Encoding fonts: what FreeType's charmap for that encoding gives.
        auto standardCode = [face](const std::string &name, bool iso) -> int32_t {
            const FT_UInt glyph = FT_Get_Name_Index(face, name.c_str());
            if (glyph == 0 && name != ".notdef") return -1;
            const FT_Encoding wanted = iso ? FT_ENCODING_ADOBE_LATIN_1 : FT_ENCODING_ADOBE_STANDARD;
            if (FT_Select_Charmap(face, wanted) != 0) return -1;
            for (FT_ULong code = 0; code < 256; ++code)
                if (FT_Get_Char_Index(face, code) == glyph) return static_cast<int32_t>(code);
            return -1;
        };
        s->type1.Parse(programData, programLength, standardCode);
        if (!s->type1.valid) {
            DestroySfnt(font);
            SetError(err, kErrBadFontData);
            return nullptr;
        }
        for (const std::string &name : s->type1.glyphNames) s->type1Glyphs.push_back(FT_Get_Name_Index(face, name.c_str()));
        s->unitsPerEm = static_cast<uint16_t>(s->type1.unitsPerEm);
        font->isFixedPitch = s->type1.isFixedPitch;
        font->firstCharIndex = 0;
        font->lastCharIndex = s->type1.lastCharCode;
        font->underlinePosition = static_cast<int16_t>(s->type1.underlinePosition);
        font->underlineThickness = static_cast<int16_t>(s->type1.underlineThickness);
    } else {
        FT_ULong cffLength = 0;
        s->cffOutlines = s->type == kFontCff || (s->type == kFontOpenType && FT_Load_Sfnt_Table(face, TTAG_CFF, 0, nullptr, &cffLength) == 0);
        FT_ULong prepLength = 0;
        s->hasPrep = FT_Load_Sfnt_Table(face, TTAG_prep, 0, nullptr, &prepLength) == 0;
        if (const TT_Header *h = static_cast<TT_Header *>(FT_Get_Sfnt_Table(face, FT_SFNT_HEAD))) {
            s->head.unitsPerEm = h->Units_Per_EM;
            s->head.macStyle = h->Mac_Style;
            s->head.lowestRecPPEM = h->Lowest_Rec_PPEM;
            s->head.indexToLocFormat = h->Index_To_Loc_Format;
            s->head.glyphDataFormat = h->Glyph_Data_Format;
            font->head = &s->head;
        }
        s->unitsPerEm = s->head.unitsPerEm ? s->head.unitsPerEm : static_cast<uint16_t>(face->units_per_EM);
        if (const TT_Postscript *post = static_cast<TT_Postscript *>(FT_Get_Sfnt_Table(face, FT_SFNT_POST))) {
            font->underlinePosition = post->underlinePosition;
            font->underlineThickness = post->underlineThickness;
            font->isFixedPitch = static_cast<uint32_t>(post->isFixedPitch);
        }
        if (const TT_OS2 *os2 = static_cast<TT_OS2 *>(FT_Get_Sfnt_Table(face, FT_SFNT_OS2))) {
            font->firstCharIndex = os2->usFirstCharIndex;
            font->lastCharIndex = os2->usLastCharIndex;
        }
        if (s->cffOutlines) {
            PS_FontInfoRec info;
            if (FT_Get_PS_Font_Info(face, &info) == 0) {
                font->isFixedPitch = info.is_fixed_pitch;
                font->underlinePosition = info.underline_position;
                font->underlineThickness = static_cast<int16_t>(info.underline_thickness);
            }
        }
    }
    ReadFontWideMetrics(font);
    mem->fonts.push_back(font);
    return font;
}

void __cdecl Delete_sfntClass(Sfnt *font, int32_t *err)
{
    SetError(err, kErrNone);
    if (!font) return;
    if (font->mem) Forget(font->mem->fonts, font);
    DestroySfnt(font);
}

// ---- the scaler ----

T2K *__cdecl NewT2K(Memhandler *mem, Sfnt *font, int32_t *err)
{
    if (!mem) {
        SetError(err, kErrNoMemhandler);
        return nullptr;
    }
    SetError(err, kErrNone);
    T2K *const t = new T2K();
    t->state = new T2KState();
    t->stamp1 = kT2KStamp1;
    t->stamp2 = kT2KStamp2;
    t->mem = mem;
    t->font = font;
    t->field_58 = font->field_d8;
    t->isFixedPitch = font->isFixedPitch;
    t->firstCharCode = font->firstCharIndex;
    t->lastCharCode = font->lastCharIndex;
    font->preferredPlatform = 0xffff;
    font->preferredEncoding = 0xffff;
    FT_New_Size(font->state->face, &t->state->size);
    mem->scalers.push_back(t);
    return t;
}

void __cdecl DeleteT2K(T2K *t, int32_t *err)
{
    SetError(err, kErrNone);
    if (!t) return;
    if (t->stamp1 != kT2KStamp1 || t->stamp2 != kT2KStamp2) {
        SetError(err, kErrBadT2KStamp);
        return;
    }
    if (t->mem) Forget(t->mem->scalers, t);
    DestroyT2K(t);
}

void __cdecl T2K_NewTransformation(T2K *t, int32_t doSetUpNow, int32_t xRes, int32_t yRes, int32_t *matrix,
                                   int32_t enableSbits, int32_t *err)
{
    (void)doSetUpNow;
    (void)enableSbits; // embedded bitmaps aren't read (docs/subsystems/rendering.md, "Standalone font engine")
    SetError(err, kErrNone);
    if (!matrix) {
        SetError(err, kErrNullMatrix);
        return;
    }
    if (xRes < 1 || yRes < 1) {
        SetError(err, kErrBadResolution);
        return;
    }
    Sfnt *const font = t->font;
    SfntState *const s = font->state;
    if (s->type == kFontType1) {
        // The font matrix [a b c d] (times the em) first, in place: Font Fusion uses the new first entry of each pair
        // for the second.
        const int32_t *f = s->type1.fontMatrix;
        matrix[0] = FixMul(matrix[0], f[0]) + FixMul(matrix[2], f[1]);
        matrix[2] = FixMul(matrix[2], f[3]) + FixMul(matrix[0], f[2]);
        matrix[1] = FixMul(matrix[1], f[0]) + FixMul(matrix[3], f[1]);
        matrix[3] = FixMul(matrix[1], f[2]) + FixMul(matrix[3], f[3]);
    }
    const int32_t unitsPerEm = s->unitsPerEm;
    t->unitsPerEm = static_cast<uint16_t>(unitsPerEm);
    int32_t sx, sy;
    if (matrix[2] == 0 || matrix[1] == 0) {
        sx = std::abs(matrix[0]);
        sy = std::abs(matrix[3]);
    } else {
        sx = Length(matrix[0], matrix[2]);
        sy = Length(matrix[1], matrix[3]);
    }
    const int32_t ppemX16 = FixMul(sx, (xRes << 16) / 72);
    const int32_t ppemY16 = FixMul(sy, (yRes << 16) / 72);
    t->xPixelsPerEm16Dot16 = ppemX16;
    t->yPixelsPerEm16Dot16 = ppemY16;
    t->xScale = FixDiv(ppemX16, unitsPerEm << 16);
    t->yScale = FixDiv(ppemY16, unitsPerEm << 16);
    const int32_t ppemX = (ppemX16 + 0x8000) >> 16;
    const int32_t ppemY = (ppemY16 + 0x8000) >> 16;
    if (ppemX < 1 || ppemY < 1) {
        matrix[0] = matrix[1] = matrix[2] = matrix[3] = 0;
    } else {
        matrix[0] = FixDiv(matrix[0], sx);
        matrix[2] = FixDiv(matrix[2], sx);
        matrix[3] = FixDiv(matrix[3], sy);
        matrix[1] = FixDiv(matrix[1], sy);
    }
    font->xPixelsPerEm = ppemX;
    font->yPixelsPerEm = ppemY;
    std::memcpy(t->matrix, matrix, sizeof t->matrix);
    t->isIdentity = IsIdentity(t) ? 1 : 0;
    t->xPixelsPerEm = ppemX;
    t->yPixelsPerEm = ppemY;
    t->field_164 = unitsPerEm ? (std::min(ppemX, ppemY) << 6) / (unitsPerEm * 2) : 0;
    t->enableSbits = 0;

    // The font-wide metrics.
    t->horizontalMetricsAreValid = 0;
    t->verticalMetricsAreValid = 0;
    if (s->horizontalValid) {
        t->xAscender = t->xDescender = t->xLineGap = t->yMaxLinearAdvanceWidth = 0;
        t->caretDx = s->caretX;
        t->caretDy = s->caretY;
        t->horizontalMetricsAreValid = 1;
        if (!IsIdentity(t)) {
            t->caretDx = FixMul(t->matrix[1], s->caretY) + FixMul(t->matrix[0], s->caretX);
            t->caretDy = FixMul(t->matrix[3], s->caretY) + FixMul(t->matrix[2], s->caretX);
        }
        TransformY(t, s->ascender, &t->xAscender, &t->yAscender);
        TransformY(t, s->descender, &t->xDescender, &t->yDescender);
        TransformY(t, s->lineGap, &t->xLineGap, &t->yLineGap);
        TransformX(t, s->maxAdvance, &t->xMaxLinearAdvanceWidth, &t->yMaxLinearAdvanceWidth);
        TransformY(t, font->underlinePosition, &t->xUnderlinePosition, &t->yUnderlinePosition);
        TransformY(t, font->underlineThickness, &t->xUnderlineThickness, &t->yUnderlineThickness);
    }
    if (s->verticalValid) {
        int32_t *v = t->vertical;
        v[8] = s->vCaretX;
        v[9] = s->vCaretY;
        t->verticalMetricsAreValid = 1;
        if (!IsIdentity(t)) {
            v[8] = FixMul(t->matrix[1], s->vCaretY) + FixMul(t->matrix[0], s->vCaretX);
            v[9] = FixMul(t->matrix[3], s->vCaretY) + FixMul(t->matrix[2], s->vCaretX);
        }
        TransformX(t, s->vAscender, &v[0], &v[1]);
        TransformX(t, s->vDescender, &v[2], &v[3]);
        TransformX(t, s->vLineGap, &v[4], &v[5]);
        TransformY(t, s->vMaxAdvance, &v[6], &v[7]);
    }

    // FreeType's size: Font Fusion scales outlines with whole pixels per em.
    if (t->state->size && ppemX >= 1 && ppemY >= 1) {
        FT_Activate_Size(t->state->size);
        FT_Set_Pixel_Sizes(s->face, static_cast<FT_UInt>(ppemX), static_cast<FT_UInt>(ppemY));
    }
}

uint32_t __cdecl T2K_GetGlyphIndex(T2K *t, uint32_t charCode, int32_t *err)
{
    SetError(err, kErrNone);
    bool found = false;
    return GlyphIndexOf(t->font, charCode, &found, err);
}

uint32_t __cdecl T2K_GlyphExists(T2K *t, uint32_t charCode, uint8_t cmd, int32_t *err)
{
    SetError(err, kErrNone);
    if (cmd & kCodeIsGlyphIndex) return 1;
    bool found = false;
    int32_t lookupErr = kErrNone;
    if (GlyphIndexOf(t->font, charCode, &found, &lookupErr) != 0) return 1;
    if (lookupErr != kErrNone) {
        SetError(err, lookupErr);
        return 1;
    }
    // Font Fusion's Type 1 and CFF readers say whether the code was found at all (glyph 0 counts).
    const SfntState *const s = t->font->state;
    return (s->type == kFontType1 || s->type == kFontCff) && found ? 1 : 0;
}

uint32_t __cdecl T2K_GetCharCodeByName(T2K *t, const char *name, int32_t *err)
{
    SetError(err, kErrNone);
    return CodeOfName(t->font, name) & 0xffff;
}

void __cdecl T2K_PurgeMemory(T2K *t, int32_t level, int32_t *err)
{
    SetError(err, kErrNone);
    t->glyph = nullptr;
    if (level > 0) FreeBitmap(t);
}

void __cdecl T2K_RenderGlyph(T2K *t, uint32_t code, int32_t xFracPenDelta, int32_t yFracPenDelta, uint32_t greyScaleLevel,
                             uint32_t cmd, int32_t *err)
{
    (void)xFracPenDelta;
    (void)yFracPenDelta;
    SetError(err, kErrNone);
    if (!t->mem || t->mem->stamp != kMemhandlerStamp) {
        SetError(err, kErrBadMemhandler);
        return;
    }
    Sfnt *const font = t->font;
    SfntState *const s = font->state;
    t->embeddedBitmapWasUsed = 0;
    t->field_cc = 1;

    // Hinting: only with the native-hints bit, then TrueType outlines with their instructions (not under the font's
    // lowest recommended size, nor for glyph data formats 2000-2002) and Type 1 outlines with their hints.
    bool hinted = false;
    if (cmd & kNativeHints) {
        if (s->type == kFontType1) {
            hinted = true;
        } else if (font->head && !s->cffOutlines && !(font->head->glyphDataFormat >= 2000 && font->head->glyphDataFormat <= 2002)) {
            hinted = t->xPixelsPerEm >= font->head->lowestRecPPEM && t->yPixelsPerEm >= font->head->lowestRecPPEM;
        }
    }
    if ((greyScaleLevel & 0xff) > 5) {
        SetError(err, kErrGreyLevel);
        return;
    }
    T2K_PurgeMemory(t, 1, nullptr);

    uint32_t index;
    if (cmd & kCodeIsGlyphIndex) {
        index = code;
        t->glyphIndex = static_cast<uint16_t>(code);
    } else {
        bool found = false;
        index = GlyphIndexOf(font, code & 0xffff, &found, err) & 0xffff;
        if (err && *err != kErrNone) return;
        t->glyphIndex = static_cast<uint16_t>(index);
    }
    const int64_t glyph = FreeTypeGlyph(s, index);
    if (glyph < 0 || !t->state->size) {
        SetError(err, kErrGlyphNotFound);
        return;
    }
    FT_Activate_Size(t->state->size);
    FT_Face face = s->face;
    FT_Set_Transform(face, nullptr, nullptr);
    // The advance in font units first (FreeType may load the glyph into the slot for it).
    FT_Fixed unitsAdvance = 0;
    FT_Get_Advance(face, static_cast<FT_UInt>(glyph), FT_LOAD_NO_SCALE, &unitsAdvance);
    // Unhinted glyphs come in font units and are scaled here as Font Fusion scaled them (whole pixels per em, its
    // rounding), then put through the transform; hinted ones are FreeType's at the size, then transformed.
    FT_Int32 flags = FT_LOAD_NO_BITMAP | FT_LOAD_COMPUTE_METRICS | FT_LOAD_NO_AUTOHINT;
    if (!hinted)
        flags |= FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING;
    else
        flags |= (greyScaleLevel & 0xff) == 0 ? FT_LOAD_TARGET_MONO : FT_LOAD_TARGET_NORMAL;
    if (FT_Load_Glyph(face, static_cast<FT_UInt>(glyph), flags) != 0 || face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) {
        SetError(err, kErrGlyphNotFound);
        return;
    }
    FT_GlyphSlot slot = face->glyph;
    FT_Outline &outline = slot->outline;
    if (!hinted) {
        for (int i = 0; i < outline.n_points; ++i) {
            FT_Vector &p = outline.points[i];
            p.x = ScaleUnits(static_cast<int32_t>(p.x), t->xPixelsPerEm, t->unitsPerEm);
            p.y = ScaleUnits(static_cast<int32_t>(p.y), t->yPixelsPerEm, t->unitsPerEm);
        }
    }
    if (!IsIdentity(t)) {
        for (int i = 0; i < outline.n_points; ++i) {
            FT_Vector &p = outline.points[i];
            const int32_t x = static_cast<int32_t>(p.x) << 10, y = static_cast<int32_t>(p.y) << 10;
            p.x = (FixMul(t->matrix[0], x) + FixMul(t->matrix[1], y)) >> 10;
            p.y = (FixMul(t->matrix[2], x) + FixMul(t->matrix[3], y)) >> 10;
        }
    }

    // Advances: the linear one from the font units at the scale, the rounded one from the (hinted) outline's.
    const int32_t linear = FixMul(static_cast<int32_t>(unitsAdvance) << 16, t->xScale);
    int32_t advance26;
    if (hinted)
        advance26 = static_cast<int32_t>(PixRound(slot->metrics.horiAdvance));
    else
        advance26 = static_cast<int32_t>(PixRound(ScaleUnits(static_cast<int32_t>(unitsAdvance), t->xPixelsPerEm, t->unitsPerEm)));
    const int32_t advance = advance26 << 10;
    t->xLinearAdvanceWidth16Dot16 = linear;
    t->yLinearAdvanceWidth16Dot16 = 0;
    t->xAdvanceWidth16Dot16 = advance;
    t->yAdvanceWidth16Dot16 = 0;
    if (!IsIdentity(t)) {
        t->xLinearAdvanceWidth16Dot16 = FixMul(t->matrix[0], linear);
        t->yLinearAdvanceWidth16Dot16 = FixMul(t->matrix[2], linear);
        t->xAdvanceWidth16Dot16 = FixMul(t->matrix[0], advance);
        t->yAdvanceWidth16Dot16 = FixMul(t->matrix[2], advance);
    }

    // The bitmap: the outline's control box out to whole pixels, scan converted into our own buffer.
    t->width = t->height = 0;
    t->rowBytes = 0;
    t->fTop26Dot6 = t->fLeft26Dot6 = 0;
    if (!(cmd & kScanConvert) || outline.n_points < 1) return;
    // Font Fusion's box: the crossings with the pixel-centre rows, plus those with the centre columns when its scan
    // converter also scans columns (0x92860e sets that for grey levels, and for 1-bit with dropout control: always
    // for unhinted outlines, and for hinted TrueType from its scan-control state, on by default, so for a font with
    // no 'prep'; for the others as their SCANCTRL left it, which FreeType reports as IGNORE_DROPOUTS when off: an
    // approximation, measured against the original by tests/font_fusion.cpp). No crossing: the pixel at the origin.
    const bool mono = (greyScaleLevel & 0xff) == 0;
    const bool columns = !mono || !hinted || !s->hasPrep || !(outline.flags & FT_OUTLINE_IGNORE_DROPOUTS);
    const ScanBounds bounds = ScanOutline(outline, columns);
    FT_Pos left = 0, right = 64, bottom = 0, top = 64;
    if (bounds.any) {
        left = static_cast<FT_Pos>(std::floor(bounds.xMin / 64.0)) * 64;
        right = static_cast<FT_Pos>(std::floor(bounds.xMax / 64.0)) * 64 + 64;
        bottom = static_cast<FT_Pos>(std::floor(bounds.yMin / 64.0)) * 64;
        top = static_cast<FT_Pos>(std::floor(bounds.yMax / 64.0)) * 64 + 64;
    }
    const uint32_t width = static_cast<uint32_t>((right - left) >> 6);
    const uint32_t height = static_cast<uint32_t>((top - bottom) >> 6);
    const uint32_t rowBytes = mono ? (width + 7) >> 3 : width;
    t->width = width;
    t->height = height;
    t->rowBytes = static_cast<int32_t>(rowBytes);
    t->fTop26Dot6 = static_cast<int32_t>(top) & ~63;
    t->fLeft26Dot6 = static_cast<int32_t>(left) & ~63;
    std::vector<uint8_t> &bits = t->state->bitmap;
    bits.assign(static_cast<size_t>(rowBytes) * height + 1, 0);
    t->baseAddr = bits.data();
    if (width == 0 || height == 0) return;
    FT_Outline_Translate(&outline, -left, -bottom);
    // The 1-bit bitmap: pixels whose centres are inside. Font Fusion fills dropouts only without the native-hints bit
    // (0x91cad4: bit 0 of the command, which cGZFont sets for Type 1 alone) and with its scaler's dropout byte set;
    // simple dropout control for Type 1 matches the original's bitmaps a little more often than none.
    if (s->type == kFontType1)
        outline.flags = outline.flags & ~(FT_OUTLINE_SMART_DROPOUTS | FT_OUTLINE_IGNORE_DROPOUTS | FT_OUTLINE_INCLUDE_STUBS);
    else
        outline.flags = (outline.flags & ~FT_OUTLINE_SMART_DROPOUTS) | FT_OUTLINE_IGNORE_DROPOUTS;
    FT_Bitmap target = {};
    target.rows = height;
    target.width = width;
    target.pitch = static_cast<int>(rowBytes);
    target.buffer = bits.data();
    target.num_grays = 256;
    target.pixel_mode = static_cast<unsigned char>(mono ? FT_PIXEL_MODE_MONO : FT_PIXEL_MODE_GRAY);
    FT_Outline_Get_Bitmap(t->mem->library, &outline, &target);
}

void __cdecl T2K_GetNameString(T2K *t, uint32_t languageId, uint32_t nameId, int32_t *err)
{
    SetError(err, kErrNone);
    FreeNames(t);
    Sfnt *const font = t->font;
    SfntState *const s = font->state;
    if (s->type == kFontType1 || s->cffOutlines) {
        // Type 1 and CFF fonts have only the full name (4), from the font program.
        if (static_cast<int16_t>(nameId) != 4) return;
        std::string full;
        if (s->type == kFontType1) {
            if (!s->type1.hasFullName) return;
            full = s->type1.fullName;
        } else {
            PS_FontInfoRec info;
            if (FT_Get_PS_Font_Info(s->face, &info) != 0 || !info.full_name) return;
            full.assign(info.full_name, std::min<size_t>(std::strlen(info.full_name), 0x50));
        }
        t->nameString8 = static_cast<char *>(std::calloc(full.size() + 1, 1));
        std::memcpy(t->nameString8, full.data(), full.size());
        return;
    }
    // TrueType: the first record for the preferred platform and encoding, the language and the name. Odd-length
    // strings, and those without a zero byte in their first ten, are 8-bit; the rest big-endian 16-bit.
    const FT_UInt count = FT_Get_Sfnt_Name_Count(s->face);
    for (FT_UInt i = 0; i < count; ++i) {
        FT_SfntName name;
        if (FT_Get_Sfnt_Name(s->face, i, &name) != 0) continue;
        if (name.platform_id != font->preferredPlatform || name.encoding_id != font->preferredEncoding ||
            name.language_id != static_cast<uint16_t>(languageId) || name.name_id != static_cast<uint16_t>(nameId))
            continue;
        const uint32_t length = name.string_len;
        uint8_t *const buffer = static_cast<uint8_t *>(std::calloc(length + 2, 1));
        bool wide = false;
        if ((length & 1) == 0)
            for (uint32_t k = 0; k < std::min<uint32_t>(length, 10); ++k)
                if (name.string[k] == 0) wide = true;
        if (wide) {
            uint16_t *const units = reinterpret_cast<uint16_t *>(buffer);
            for (uint32_t k = 0; k < length / 2; ++k) units[k] = ReadU16(name.string + 2 * k);
            t->nameString16 = units;
        } else {
            std::memcpy(buffer, name.string, length);
            t->nameString8 = reinterpret_cast<char *>(buffer);
        }
        return;
    }
}

} // namespace fontfusion
