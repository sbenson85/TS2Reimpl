// cGZFont (lane OC): the font, its glyph caches and its font file reader (src/gz/cGZFont.h).
#include "gz/cGZFont.h"

#include "font/FontFusion.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/GZCOM.h" // ConvertBytesToString
#include "gz/cGZBufferFontBlit.h"
#include "gz/cIGZFrameWork.h"
#include "gz/cRZCursor.h" // GetGraphicSystem
#include "ifaces/cIGZFile.h"
#include "ifaces/cIGZGraphicSystem.h"
#include "misc/ClassesRound4.h" // cGZBufferFontRenderer
#include "rz/RZTextBreak.h"
#include "rz/RZTextChars.h"
#include "rz/RZTextConvert.h"
#include "rz/StringKeyParser.h" // cRZStringHelpers::LowerCase
#include "rz/cRZCmdLine.h"      // cIGZCmdLine
#include "rz/cRZIniFile.h"      // cRZIniStringHelpers::TrimBoth
#include "rz/cRZString.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace {

// ---- Bitstream Font Fusion (T2K): our font engine (src/font/FontFusion.h, FreeType behind T2K's interface) ----
// Every entry is cdecl and reports through its last argument, an error code (0: none; the engine's errors are
// 10000-based). Names from the engine's API, matched by shape (db/third-party.jsonl).
namespace ff {

void *NewMemhandler(int32_t *err)
{
    return fontfusion::tsi_NewMemhandler(err);
}
void DeleteMemhandler(void *memory)
{
    fontfusion::tsi_DeleteMemhandler(static_cast<fontfusion::Memhandler *>(memory));
}
// An input stream that reads through `read` (stream, dst, offset, count) as the engine needs the bytes.
void *NewNonRamInputStream(void *memory, void *stream, void *read, uint32_t length, int32_t *err)
{
    return fontfusion::New_NonRamInputStream(static_cast<fontfusion::Memhandler *>(memory), stream,
                                             reinterpret_cast<fontfusion::ReadFunction>(read), length, err);
}
// An input stream over `length` bytes in memory.
void *NewInputStream3(void *memory, void *data, uint32_t length, int32_t *err)
{
    return fontfusion::New_InputStream3(static_cast<fontfusion::Memhandler *>(memory), static_cast<uint8_t *>(data), length,
                                        err);
}
void DeleteInputStream(void *input, int32_t *err)
{
    fontfusion::Delete_InputStream(static_cast<fontfusion::InputStream *>(input), err);
}
// The font type in the stream's header (16 bits; 1 a PC Type 1 file, 2 TrueType, 7 and 0x16 two other types).
uint16_t FindFontTypeFromStream(void *input, int32_t *err)
{
    return fontfusion::FF_FindFontTypeFromStream(static_cast<fontfusion::InputStream *>(input), err);
}
// Decodes a PC Type 1 (PFB) file in place to its pure Type 1 form; `length` in and out.
void ExtractPureT1FromPCType1(void *data, uint32_t *length, int32_t *err)
{
    fontfusion::ExtractPureT1FromPCType1(static_cast<uint8_t *>(data), length, err);
}
void *NewSfntClass(void *memory, uint32_t type, void *input, int32_t *err)
{
    return fontfusion::New_sfntClass(static_cast<fontfusion::Memhandler *>(memory), static_cast<int16_t>(type), 0,
                                     static_cast<fontfusion::InputStream *>(input), nullptr, nullptr, err);
}
void DeleteSfntClass(void *sfnt, int32_t *err)
{
    fontfusion::Delete_sfntClass(static_cast<fontfusion::Sfnt *>(sfnt), err);
}
uint8_t *NewT2K(void *memory, void *sfnt, int32_t *err)
{
    return reinterpret_cast<uint8_t *>(
        fontfusion::NewT2K(static_cast<fontfusion::Memhandler *>(memory), static_cast<fontfusion::Sfnt *>(sfnt), err));
}
void DeleteT2K(uint8_t *t2k, int32_t *err)
{
    fontfusion::DeleteT2K(reinterpret_cast<fontfusion::T2K *>(t2k), err);
}
// The scaler's transform: 72 dpi both ways, the 16.16 matrix (xx, xy, yx, yy).
void NewTransformationAndGrid(uint8_t *t2k, int32_t *matrix, int32_t *err)
{
    fontfusion::T2K_NewTransformation(reinterpret_cast<fontfusion::T2K *>(t2k), 1, 72, 72, matrix, 1, err);
}
// The glyph index of a character (16 bits). The argument goes through as given (MakeGlyph passes its whole code).
uint32_t GetGlyphIndex(uint8_t *t2k, uint32_t code, int32_t *err)
{
    return fontfusion::T2K_GetGlyphIndex(reinterpret_cast<fontfusion::T2K *>(t2k), code, err) & 0xffff;
}
// Whether the face has a glyph for the code (0 when it has none); the flags argument is always 0 here.
uint32_t HasGlyph(uint8_t *t2k, uint32_t code, int32_t *err)
{
    return fontfusion::T2K_GlyphExists(reinterpret_cast<fontfusion::T2K *>(t2k), code, 0, err);
}
// The character code the face's Type 1 glyph `name` has (16 bits; 0 when it has none).
uint32_t GetCharCodeByName(uint8_t *t2k, const char *name, int32_t *err)
{
    return fontfusion::T2K_GetCharCodeByName(reinterpret_cast<fontfusion::T2K *>(t2k), name, err);
}
// Renders a glyph at the current transform into the T2K object: the bitmap (+0x118; null when the glyph is empty) with
// its width (+0x10c), height (+0x110), row bytes (+0x114), bearings (+0xe0, +0xe4) and advances (+0x80, +0xd0).
// `mode`: 0 a 1-bit bitmap, 3 an 8-bit one; `flags` the reader's flags (cGZFontFile::flags).
void RenderGlyph(uint8_t *t2k, uint32_t glyphIndex, uint32_t xFraction, uint32_t yFraction, uint32_t mode, uint32_t flags,
                 int32_t *err)
{
    fontfusion::T2K_RenderGlyph(reinterpret_cast<fontfusion::T2K *>(t2k), glyphIndex, static_cast<int32_t>(xFraction),
                                static_cast<int32_t>(yFraction), mode, flags, err);
}
// Releases what the last render allocated (level 1: the glyph bitmap).
void PurgeMemory(uint8_t *t2k, uint32_t level, int32_t *err)
{
    fontfusion::T2K_PurgeMemory(reinterpret_cast<fontfusion::T2K *>(t2k), static_cast<int32_t>(level), err);
}
// Looks a name up: the result in the T2K object's name strings (+0x50 8-bit, +0x54 16-bit; both null when missing).
void GetNameString(uint8_t *t2k, uint32_t language, uint32_t nameId, int32_t *err)
{
    fontfusion::T2K_GetNameString(reinterpret_cast<fontfusion::T2K *>(t2k), language, nameId, err);
}

} // namespace ff

// The T2K object's fields the class reads or sets.
constexpr uint32_t kT2KFlag48 = 0x48;       // set to 1 once open
constexpr uint32_t kT2KName8 = 0x50;        // const char *
constexpr uint32_t kT2KName16 = 0x54;       // const char16_t *
constexpr uint32_t kT2KCharmap = 0x5c;
constexpr uint32_t kT2KLineGap = 0x6c;      // 16.16 (the three vertical metrics after the transform)
constexpr uint32_t kT2KDescent = 0x74;
constexpr uint32_t kT2KAscender = 0x7c;
constexpr uint32_t kT2KAdvanceCharmap = 0x80; // the rendered glyph's advance (16.16) when the face has a charmap
constexpr uint32_t kT2KAdvance = 0xd0;      // ... and when it has none
constexpr uint32_t kT2KBearingY = 0xe0;     // the rendered glyph's vertical offset (26.6)
constexpr uint32_t kT2KBearingX = 0xe4;     // its horizontal bearing (26.6)
constexpr uint32_t kT2KBitmapWidth = 0x10c; // the rendered bitmap
constexpr uint32_t kT2KBitmapHeight = 0x110;
constexpr uint32_t kT2KBitmapPitch = 0x114;
constexpr uint32_t kT2KBitmap = 0x118;      // null for an empty glyph
constexpr uint32_t kT2KFont = 0x180;        // the font object: +0x78 / +0x7a the preferred cmap platform / encoding,
                                            // +0x40 its OS/2 table (+0x30 the selection bits)
constexpr uint32_t kLanguageEnglishUS = 0x409;

template <class T> T &Field(uint8_t *object, uint32_t offset)
{
    return *reinterpret_cast<T *>(object + offset);
}
template <class T> T Field(const uint8_t *object, uint32_t offset)
{
    return *reinterpret_cast<const T *>(object + offset);
}

bool NoName(const uint8_t *t2k)
{
    return Field<uint32_t>(t2k, kT2KName16) == 0 && Field<uint32_t>(t2k, kT2KName8) == 0;
}

// 16.16 to whole pixels, toward zero.
int32_t Pixels(int32_t fixed)
{
    return fixed / 65536;
}

} // namespace

// ---- cGZFontFileStream ----

cGZFontFileStream::cGZFontFileStream(const cIGZString &path)
{
    file.Construct(path);
    headerLength = 0;
    obfuscatedLength = 0;
    key = 0;
    ifaces::cIGZFile *const f = As<ifaces::cIGZFile>(&file);
    f->SetBuffering(0x1000, 0);
    if (!f->Open(1, 2, 1) || f->Length() <= kHeaderLength) return;
    f->SeekToBegin();
    // The original reads the magic into a stack slot that holds `this` (a failed Read leaves it there).
    uint32_t magic = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));
    f->Read(&magic, 4);
    if (_byteswap_ulong(magic) != kMagic) return;
    headerLength = kHeaderLength;
    f->Read(&obfuscatedLength, 4);
    obfuscatedLength = _byteswap_ulong(obfuscatedLength);
    f->Read(&key, 1);
}

bool cGZFontFileStream::Read(uint8_t *dst, uint32_t offset, uint32_t count)
{
    ifaces::cIGZFile *const f = As<ifaces::cIGZFile>(&file);
    f->SeekToPosition(static_cast<int32_t>(headerLength + offset));
    if (!f->Read(dst, count)) return false;
    if (offset < obfuscatedLength) {
        uint32_t n = obfuscatedLength - offset;
        if (count < n) n = count;
        for (uint32_t i = 0; i < n; ++i) dst[i] ^= key;
    }
    return true;
}

uint32_t cGZFontFileStream::DataLength()
{
    return As<ifaces::cIGZFile>(&file)->Length() - headerLength;
}

bool cGZFontFileStream::ReadAll(std::vector<char> &out, uint32_t max)
{
    out.clear();
    ifaces::cIGZFile *const f = As<ifaces::cIGZFile>(&file);
    if (!f->IsOpen()) return false;
    uint32_t n = f->Length() - headerLength;
    if (max < n) n = max;
    out.resize(n);
    f->SeekToPosition(static_cast<int32_t>(headerLength));
    return Read(reinterpret_cast<uint8_t *>(out.data()), 0, n);
}

uint8_t __cdecl cGZFontFileStream::ReadCallback(cGZFontFileStream *stream, uint8_t *dst, uint32_t offset, uint32_t count)
{
    return stream->Read(dst, offset, count);
}

// ---- cGZFontFile ----

bool cGZFontFile::Open(const cIGZString *path)
{
    int32_t err = 0;
    Close();
    memory = ff::NewMemhandler(&err);
    if (!memory || err != 0) return t2k != nullptr;
    stream = new cGZFontFileStream(*path);
    input = ff::NewNonRamInputStream(memory, stream, reinterpret_cast<void *>(&cGZFontFileStream::ReadCallback),
                                     stream->DataLength(), &err);
    if (!input || err != 0) return t2k != nullptr;
    const uint16_t type = ff::FindFontTypeFromStream(input, &err);
    switch (type) {
    case 1: kind = kType1; break;
    case 0x16: kind = kKind3; break;
    case 7: kind = kKind4; break;
    case 2: kind = kTrueType; break;
    default: kind = kUnknown; return t2k != nullptr;
    }
    if (err != 0) return t2k != nullptr;
    if (kind == kType1 && stream->ReadAll(type1, 0xffffffffu)) {
        uint32_t length = static_cast<uint32_t>(type1.size());
        ff::ExtractPureT1FromPCType1(type1.data(), &length, &err);
        if (err == 0) {
            type1.resize(length);
            void *const pure = ff::NewInputStream3(memory, type1.data(), static_cast<uint32_t>(type1.size()), &err);
            ff::DeleteInputStream(input, &err);
            input = pure;
        }
    }
    if (!memory || err != 0) return t2k != nullptr;
    sfnt = ff::NewSfntClass(memory, type, input, &err);
    if (!sfnt || err != 0) return t2k != nullptr;
    t2k = ff::NewT2K(memory, sfnt, &err);
    if (!t2k || err != 0) return t2k != nullptr;
    Field<uint16_t>(Field<uint8_t *>(t2k, kT2KFont), 0x78) = 3; // Windows,
    Field<uint16_t>(Field<uint8_t *>(t2k, kT2KFont), 0x7a) = 1; // Unicode BMP
    Field<uint32_t>(t2k, kT2KFlag48) = 1;
    flags |= (type == 2 || type == 7) ? 0x80 : 1;
    return true;
}

bool cGZFontFile::Close()
{
    int32_t err = 0;
    if (t2k) {
        ff::DeleteT2K(t2k, &err);
        t2k = nullptr;
    }
    if (sfnt) {
        ff::DeleteSfntClass(sfnt, &err);
        sfnt = nullptr;
    }
    if (input) {
        ff::DeleteInputStream(input, &err);
        input = nullptr;
    }
    if (stream) {
        delete stream;
        stream = nullptr;
    }
    if (memory) {
        ff::DeleteMemhandler(memory);
        memory = nullptr;
    }
    return true;
}

bool cGZFontFile::Name(cIGZString *out, uint32_t which)
{
    cRZString *const name = reinterpret_cast<cRZString *>(out);
    if (t2k) {
        int32_t err = 0;
        const uint32_t id = which & 0xffff;
        // Eight lookups, each after the first only while nothing was found: the name, then the full name (4); in
        // English (US), then any language; each with the preferred cmap encoding and then with it at 0.
        for (unsigned step = 0; step < 8; ++step) {
            if (step != 0 && !NoName(t2k)) continue;
            const uint32_t nameId = step < 4 ? id : 4;
            const uint32_t language = (step & 2) ? 0 : kLanguageEnglishUS;
            const bool otherEncoding = (step & 1) != 0;
            if (otherEncoding) Field<uint16_t>(Field<uint8_t *>(t2k, kT2KFont), 0x7a) = 0;
            ff::GetNameString(t2k, language, nameId, &err);
            if (otherEncoding) Field<uint16_t>(Field<uint8_t *>(t2k, kT2KFont), 0x7a) = 1;
        }
        if (const char16_t *const wide = Field<const char16_t *>(t2k, kT2KName16)) {
            const std::u16string text(wide);
            ConvertBytesToString(text.data(), static_cast<uint32_t>(text.size()), RZText::kUtf16, out);
        } else if (const char *const narrow = Field<const char *>(t2k, kT2KName8)) {
            const std::string text(narrow);
            ConvertBytesToString(text.data(), static_cast<uint32_t>(text.size()), 0x4e4, out); // Windows-1252
        } else {
            name->mString.clear();
        }
        if (which == 1 || which == 2) {
            const uint32_t bold = static_cast<uint32_t>(out->RFind("bold", 0xffffffffu, false));
            const uint32_t italic = static_cast<uint32_t>(out->RFind("italic", 0xffffffffu, false));
            if (which == 1) {
                // ORIGINAL-BUG: the family name is cut at "bold" and then at "italic"; when "italic" follows "bold"
                // (a family name like "X Bold Italic"), the second cut is past the end and std::string::erase throws
                // std::out_of_range.
                if (bold != 0xffffffffu) name->mString.erase(bold);
                if (italic != 0xffffffffu) name->mString.erase(italic);
            } else {
                const uint32_t first = italic < bold ? italic : bold;
                if (first != 0xffffffffu) name->mString.erase(0, first);
            }
            reinterpret_cast<cRZIniStringHelpers *>(name)->TrimBoth();
        }
    }
    return name->mString.size() != 0;
}

uint32_t cGZFontFile::Style()
{
    uint32_t style = 0;
    if (!t2k) return style;
    if (const uint8_t *const font = Field<const uint8_t *>(t2k, kT2KFont)) {
        if (const uint8_t *const os2 = Field<const uint8_t *>(font, 0x40)) {
            if (os2[0x30] & 1) style = 1;
            if (os2[0x30] & 2) style |= 2;
        }
    }
    FontSysString subfamily;
    Name(subfamily.s.AsInterface(), 2);
    if (subfamily.s.mString.size() != 0) {
        reinterpret_cast<cRZStringHelpers *>(&subfamily.s)->LowerCase();
        if (subfamily.s.mString.find("italic") != std::string::npos) style |= 2;
        if (subfamily.s.mString.find("bold") != std::string::npos) style |= 1;
    }
    return style;
}

bool cGZFontFile::HasCharmap() const
{
    return Field<uint32_t>(t2k, kT2KCharmap) != 0;
}

namespace {
// The Adobe glyph names of the characters a Type 1 face may hold (first entry wins for a repeated character).
struct GlyphName {
    uint16_t code;
    const char *name;
};
const GlyphName kGlyphNames[] = {
#include "gz/cGZFontGlyphNames.inc"
};
} // namespace

const char *cGZFontFile::GlyphName(uint16_t code)
{
    for (const auto &entry : kGlyphNames)
        if (entry.code == code) return entry.name;
    return nullptr;
}

uint32_t cGZFontFile::GlyphIndex(uint32_t requested)
{
    int32_t err = 0;
    const uint16_t code = static_cast<uint16_t>(requested);
    const auto known = glyphIndices.find(code);
    if (known != glyphIndices.end()) return known->second;
    if (const char *const name = GlyphName(code)) {
        if (const uint32_t named = ff::GetCharCodeByName(t2k, name, &err)) {
            if (const uint32_t index = ff::GetGlyphIndex(t2k, named, &err)) {
                glyphIndices.insert({code, index});
                return index;
            }
        }
    }
    char unicodeName[16];
    std::snprintf(unicodeName, sizeof(unicodeName), "uni%04X", static_cast<unsigned>(code));
    if (const uint32_t named = ff::GetCharCodeByName(t2k, unicodeName, &err)) {
        const uint16_t namedCode = static_cast<uint16_t>(named);
        if (const uint32_t index = ff::GetGlyphIndex(t2k, namedCode, &err)) {
            // ORIGINAL-BUG: the hit is kept under the engine's code for the name, not under `code`.
            glyphIndices.insert({namedCode, index});
            return index;
        }
    }
    return 0;
}

cGZFontFile *__fastcall cGZFontFileAdapters::Construct(void *memory, void *)
{
    return new (memory) cGZFontFile;
}

void __fastcall cGZFontFileAdapters::Destruct(cGZFontFile *self, void *)
{
    self->~cGZFontFile();
}

// ---- cGZFontGlyphCache ----

void cGZFontGlyphCache::Clear()
{
    std::free(memory);
    memory = nullptr;
    pixels = nullptr;
    width = 0;
    height = 0;
    pitch = 0;
    cursor = 0;
    glyphs.clear();
}

// ---- cGZFont: construction ----

cGZFont::cGZFont()
{
    mFile = new cGZFontFile;
}

cGZFont::~cGZFont()
{
    Unload();
    delete mFile;
    mFile = nullptr;
}

cGZFont *__cdecl cGZFont::Create()
{
    return new cGZFont;
}

// ---- cIGZUnknown ----

bool cGZFont::QueryInterface(uint32_t riid, void **out)
{
    if (riid != kIid && riid != 1) return false;
    *out = static_cast<cIGZFont *>(this);
    AddRef();
    return true;
}

uint32_t cGZFont::AddRef()
{
    return static_cast<uint32_t>(++mRefCount);
}

uint32_t cGZFont::Release()
{
    if (mRefCount > 1) return static_cast<uint32_t>(--mRefCount);
    Unload();
    delete this;
    return 0;
}

// ---- loading ----

bool cGZFont::Load()
{
    if (mLoaded || mPath.s.mString.size() == 0 || !cRZFile::PathExists(*mPath.s.AsInterface())) return mLoaded;
    cGZFontFile *const file = mFile;
    if (file->Open(mPath.s.AsInterface()) && (mSize == 0 || SetSize(mSize))) {
        int32_t err = 0;
        file->Name(mFamily.s.AsInterface(), 1);
        const uint32_t kept = mStyle & 0x3c;
        mStyle = file->Style() | kept;
        mGroup = file->HasCharmap() ? 2 : 1;
        mDefaultGlyph = ff::GetGlyphIndex(file->T2K(), mDefaultChar & 0xffff, &err);
        if (err != 0) mDefaultGlyph = 0;
        mLoaded = mFamily.s.mString.size() != 0;
    }
    if (mLoaded) {
        if (!mRenderer) {
            void *const block = ::operator new(8);
            mRenderer = reinterpret_cast<cIGZFontRenderer *>(
                cGZBufferFontRendererCode::Construct(static_cast<cGZBufferFontRenderer *>(block), nullptr));
            mRenderer->AddRef();
        }
        BuildGammaTable();
    }
    return mLoaded;
}

bool cGZFont::Unload()
{
    if (mLoaded) {
        SetRenderer(nullptr);
        mCacheMono.Clear();
        mCacheSmooth.Clear();
        mColourTable = 0;
        for (uint8_t *block : mBlocks) ::operator delete[](block);
        mBlocks.clear();
        mFile->Close();
        mLoaded = false;
    }
    return true;
}

// ---- names and style ----

void cGZFont::GetFamily(cIGZString *out)
{
    out->FromChar(mFamily.s.AsInterface()->ToChar());
}

bool cGZFont::HasStyle(uint32_t flags)
{
    return (mStyle & flags) == flags;
}

void cGZFont::SetStyle(uint32_t flags, bool set)
{
    if (set) mStyle |= flags & 0x3c;
    else mStyle &= ~(flags & 0x3c);
}

void cGZFont::GetPath(cIGZString *out)
{
    out->FromChar(mPath.s.AsInterface()->ToChar());
}

void cGZFont::SetPath(const cIGZString *path)
{
    mPath.s.AsInterface()->FromChar(path->ToChar());
}

bool cGZFont::GetEncoding(uint32_t *out)
{
    *out = mEncoding;
    return true;
}

bool cGZFont::SetEncoding(int32_t encoding)
{
    if (encoding == RZText::kSystemCodePage) encoding = RZText::SystemCodePage();
    else if (encoding == RZText::kDefault) encoding = RZText::DefaultEncoding();
    mEncoding = static_cast<uint32_t>(encoding);
    return true;
}

bool cGZFont::IsUnicodeGroup(uint32_t group)
{
    return group == 2;
}

// ---- size, scale, spacing ----

int32_t cGZFont::ApplyTransform()
{
    int32_t err = 0;
    const float size = static_cast<float>(static_cast<double>(mSize));
    int32_t matrix[4] = {static_cast<int32_t>(mScaleX * 65536.0f * size), 0, 0, static_cast<int32_t>(mScaleY * 65536.0f * size)};
    ff::NewTransformationAndGrid(mFile->T2K(), matrix, &err);
    return err;
}

bool cGZFont::SetSize(uint32_t size)
{
    if (size == 0 || mCacheMono.glyphs.size() != 0 || mCacheSmooth.glyphs.size() != 0) return false;
    mSize = size <= kMaxSize ? size : kMaxSize;
    const cGZFontFile *const file = mFile;
    if (!file || !file->T2K()) return true;
    const int32_t err = ApplyTransform();
    const uint8_t *const t = file->T2K();
    mDescentLine = Pixels(Field<int32_t>(t, kT2KAscender));
    mAscent = Pixels(Field<int32_t>(t, kT2KAscender) + Field<int32_t>(t, kT2KLineGap));
    uint32_t line = static_cast<uint32_t>(
        Pixels(Field<int32_t>(t, kT2KAscender) - Field<int32_t>(t, kT2KDescent) + Field<int32_t>(t, kT2KLineGap)));
    if (line == 0) line = 1;
    if (file->kind == cGZFontFile::kType1 || file->kind == cGZFontFile::kKind3) {
        ++line;
        if (line > 0x1c) ++line;
    }
    mLineHeight = line;
    return err == 0;
}

void cGZFont::GetScale(float *x, float *y)
{
    *x = mScaleX;
    *y = mScaleY;
}

void cGZFont::SetScale(float x, float y)
{
    // The clamps as the original's maxss / minss (a NaN argument passes through).
    float cx = 0.01f > x ? 0.01f : x;
    float cy = 0.01f > y ? 0.01f : y;
    cx = 1000.0f < cx ? 1000.0f : cx;
    cy = 1000.0f < cy ? 1000.0f : cy;
    if (cx == mScaleX && cy == mScaleY) return;
    mScaleX = cx;
    mScaleY = cy;
    mCacheMono.Clear();
    mCacheSmooth.Clear();
    ApplyTransform();
}

void cGZFont::GetAdvanceScale(float *x, float *y)
{
    *x = mAdvanceScale;
    *y = 1.0f;
}

void cGZFont::SetAdvanceScale(float x, float)
{
    float v = 0.1f > x ? 0.1f : x;
    v = 10.0f < v ? 10.0f : v;
    if (mAdvanceScale == v) return;
    mAdvanceScale = v;
    mCacheMono.Clear();
    mCacheSmooth.Clear();
}

void cGZFont::GetSpacing(int32_t *x, int32_t *y)
{
    *x = mSpacingX;
    *y = mSpacingY;
}

void cGZFont::SetSpacing(int32_t x, int32_t y)
{
    mSpacingX = x;
    mSpacingY = y;
}

int32_t cGZFont::GetLineSpacing()
{
    return mSpacingY + static_cast<int32_t>(mLineHeight);
}

uint32_t cGZFont::GetLineHeight(uint32_t, uint32_t)
{
    return mLineHeight;
}

int32_t cGZFont::GetAscent()
{
    return mAscent + mSpacingY;
}

int32_t cGZFont::GetDescent()
{
    const int32_t ascent = GetAscent();
    return GetLineSpacing() - ascent;
}

// ---- colours, quality, gamma ----

void cGZFont::SetColours(uint32_t colour, uint32_t shadow)
{
    mColour = colour;
    mShadowColour = shadow;
}

void cGZFont::SetQuality(uint32_t mode)
{
    if (mode != 0) mQuality = mode;
}

void cGZFont::SetGamma(float gamma)
{
    float v = 0.0f > gamma ? 0.0f : gamma;
    v = 10.0f < v ? 10.0f : v;
    if (mGamma == v) return;
    mGamma = v;
    BuildGammaTable();
}

void cGZFont::SetContrast(float contrast)
{
    float v = -1.0f > contrast ? -1.0f : contrast;
    v = 1.0f < v ? 1.0f : v;
    if (v == mContrast) return;
    mContrast = v;
    BuildGammaTable();
}

void cGZFont::BuildGammaTable()
{
    if (mGamma == 1.0f && mContrast == 0.0f) {
        mGammaTable.clear();
        mCacheSmooth.Clear();
        return;
    }
    mGammaTable.resize(0x100, 0);
    mCacheSmooth.Clear();
    // The contrast curve's exponent: (contrast + 1) ^ (1 / log10 2), as a float.
    const float exponent = static_cast<float>(std::pow(static_cast<double>(mContrast) + 1.0, 1.0 / std::log10(2.0)));
    for (int32_t i = 0; i < 0x100; ++i) {
        float v = static_cast<float>(i) / 255.0f;
        if (mGamma != 1.0f) v = static_cast<float>(std::pow(static_cast<double>(v), static_cast<double>(mGamma)));
        if (mContrast != 0.0f) {
            const float c = 1.0f < v ? 1.0f : v;
            if (i <= 0x7f) {
                v = static_cast<float>(std::pow(static_cast<double>(c + c), static_cast<double>(exponent))) * 0.5f;
            } else {
                const float d = 1.0f - c;
                v = 1.0f - static_cast<float>(std::pow(static_cast<double>(d + d), static_cast<double>(exponent))) * 0.5f;
            }
        }
        mGammaTable[i] = static_cast<uint8_t>(static_cast<int32_t>(v * 255.0f));
    }
}

// ---- the renderer and drawing ----

void cGZFont::SetRenderer(cIGZFontRenderer *renderer)
{
    if (renderer == mRenderer) return;
    const uint32_t self = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<cIGZFont *>(this)));
    if (renderer) {
        renderer->AddRef();
        renderer->vf3(self);
    }
    if (mRenderer) {
        mRenderer->ReleaseFont(self);
        mRenderer->Release();
    }
    mRenderer = renderer;
}

uint32_t cGZFont::RendererMode(uint32_t quality)
{
    return quality == 2 || quality == 3 ? quality : 1;
}

bool cGZFont::BeginDraw(cIGZBuffer *target)
{
    if (mDrawDepth == 0)
        mRenderer->BeginDraw(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<cIGZFont *>(this))), target,
                             RendererMode(mQuality));
    ++mDrawDepth;
    return true;
}

bool cGZFont::EndDraw(cIGZBuffer *target)
{
    if (mDrawDepth == 1)
        mRenderer->EndDraw(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<cIGZFont *>(this))), target,
                           RendererMode(mQuality));
    --mDrawDepth;
    return true;
}

// ---- glyphs ----

cGZFontGlyphCache *cGZFont::GetGlyphCache()
{
    return mQuality == 1 ? &mCacheMono : &mCacheSmooth;
}

cGZFontCachedGlyph *cGZFont::FindGlyph(uint32_t code)
{
    cGZFontGlyphCache *const cache = GetGlyphCache();
    if (!cache) return nullptr;
    const auto found = cache->glyphs.find(code);
    if (found == cache->glyphs.end()) return MakeGlyph(code, cache);
    return &found->second;
}

// ---- making glyphs ----

uint32_t cGZFont::UnderlineThickness() const
{
    if (mSize < 0x10) return 1;
    if (mSize < 0x1a) return 2;
    return mSize < 0x24 ? 3 : 4;
}

uint32_t cGZFont::StrikeoutThickness() const
{
    if (mSize < 0x18) return 1;
    if (mSize < 0x30) return 2;
    return mSize < 0x48 ? 3 : 4;
}

namespace {

// 0x90bb4f: a box outline for a character the face has no glyph for: 0xff along the border of width x height, 0 inside,
// in a new block (the caller frees it with free).
uint8_t *BoxBitmap(int32_t width, int32_t height)
{
    uint8_t *const bits = static_cast<uint8_t *>(::operator new(static_cast<size_t>(width) * static_cast<size_t>(height)));
    for (int32_t column = 0; column < width; ++column) {
        int32_t at = column;
        for (int32_t row = 0; row < height; ++row) {
            bits[at] = (column == 0 || row == 0 || column == width - 1 || row == height - 1) ? 0xff : 0;
            at += width;
        }
    }
    return bits;
}

constexpr float kOneOver65536 = 1.0f / 65536.0f;

} // namespace

cGZFontCachedGlyph *cGZFont::MakeGlyph(uint32_t code, cGZFontGlyphCache *cache)
{
    cGZFontFile *const file = mFile;
    int32_t err = 1; // the engine sets it on each call it is given
    if (file && file->T2K()) {
        uint8_t *const t = file->T2K();
        // Type 1 faces (kinds 2 and 3) are searched by glyph name for the characters their charmap lacks.
        const bool byName = file->kind == cGZFontFile::kType1 || file->kind == cGZFontFile::kKind3;
        uint32_t index = 0;
        bool found = false;
        bool tryName;
        if (code >= 0x80 && byName) {
            tryName = true;
        } else {
            index = ff::GetGlyphIndex(t, code, &err);
            found = err == 0 && index != 0;
            tryName = !found && code >= 0x20 && byName;
        }
        if (tryName) {
            index = file->GlyphIndex(code);
            found = index != 0;
        }
        if (!found) index = mDefaultGlyph; // the font's default glyph (0: none)
        if (index != 0) {
            const uint32_t pixelMode = mQuality == 1 ? 0 : 3;
            ff::RenderGlyph(t, index, 0, 0, pixelMode, file->flags, &err);
            if (err == 0) {
                cGZFontCachedGlyph glyph = {};
                uint8_t *const bitmap = Field<uint8_t *>(t, kT2KBitmap);
                const uint32_t width = bitmap ? Field<uint32_t>(t, kT2KBitmapWidth) : 0;
                const uint32_t height = bitmap ? Field<uint32_t>(t, kT2KBitmapHeight) : 0;
                glyph.left = static_cast<int32_t>(cache->cursor);
                glyph.top = 0;
                glyph.right = static_cast<int32_t>(width + cache->cursor);
                glyph.bottom = static_cast<int32_t>(height);
                glyph.bearingX = static_cast<int16_t>(Field<int32_t>(t, kT2KBearingX) / 64);
                glyph.bearingY = static_cast<int16_t>(
                    Pixels(Field<int32_t>(t, kT2KAscender) + Field<int32_t>(t, kT2KLineGap)) - Field<int32_t>(t, kT2KBearingY) / 64);
                const int32_t fixedAdvance = Field<uint32_t>(t, kT2KCharmap) != 0 ? Field<int32_t>(t, kT2KAdvanceCharmap)
                                                                                  : Field<int32_t>(t, kT2KAdvance);
                // The scaled advance in pixels. A scale just under 1 truncates to the unscaled width, except that a space
                // keeps the scaled value, one less when that would exceed it.
                const float scale = mAdvanceScale;
                const int32_t scaled = static_cast<int32_t>(static_cast<float>(fixedAdvance) * scale * kOneOver65536);
                int16_t advance;
                bool zeroWidth = code == 0x200b || code == 0xfeff;
                if (scale == 1.0f) {
                    advance = static_cast<int16_t>(Pixels(fixedAdvance));
                } else if (scale > 0.98f && scale < 1.0f) {
                    if (code != 0x20) {
                        advance = static_cast<int16_t>(Pixels(fixedAdvance));
                    } else {
                        advance = static_cast<int16_t>(scaled);
                        if (static_cast<int32_t>(advance) + 1 > Pixels(fixedAdvance)) advance = static_cast<int16_t>(advance - 1);
                        zeroWidth = false;
                    }
                } else {
                    advance = static_cast<int16_t>(scaled);
                }
                glyph.advance = zeroWidth ? 0 : advance;
                if (bitmap)
                    PlaceBitmap(cache, &glyph, cache->cursor, 0, bitmap, width, height, Field<uint32_t>(t, kT2KBitmapPitch),
                                mQuality == 1 ? 2 : 0x100);
                else
                    PlaceBitmap(cache, &glyph, cache->cursor, 0, nullptr, 0, 0, 0, 2);
                cache->glyphs.insert({code, glyph});
                cGZFontCachedGlyph *const made = FindGlyph(code);
                ff::PurgeMemory(t, 1, &err);
                if (err == 0) return made;
            }
        }
    }
    // No glyph to make: a box of half the line height (nothing for the zero-width characters).
    cGZFontCachedGlyph glyph = {};
    const uint32_t half = mLineHeight >> 1;
    glyph.left = static_cast<int32_t>(cache->cursor);
    glyph.right = static_cast<int32_t>(cache->cursor + half);
    glyph.bottom = static_cast<int32_t>(half);
    int16_t advance = static_cast<int16_t>(static_cast<int32_t>(static_cast<float>(static_cast<int32_t>(half)) * mAdvanceScale));
    if (code == 0x200b || code == 0xfeff) {
        advance = 0;
        glyph.advance = advance;
        PlaceBitmap(cache, &glyph, static_cast<uint32_t>(glyph.left), 0, nullptr, 0, 0, 0, 2);
    } else {
        glyph.advance = advance;
        uint8_t *const box = BoxBitmap(static_cast<int32_t>(half), static_cast<int32_t>(half));
        PlaceBitmap(cache, &glyph, static_cast<uint32_t>(glyph.left), static_cast<uint32_t>(glyph.top), box,
                    static_cast<uint32_t>(glyph.right - glyph.left), static_cast<uint32_t>(glyph.bottom - glyph.top),
                    static_cast<uint32_t>(glyph.right - glyph.left), 0x100);
        std::free(box);
    }
    cache->glyphs.insert({code, glyph});
    return FindGlyph(code);
}

bool cGZFont::PlaceBitmap(cGZFontGlyphCache *cache, cGZFontCachedGlyph *glyph, uint32_t x, uint32_t y, const uint8_t *source,
                          uint32_t width, uint32_t height, uint32_t sourcePitch, uint32_t mode)
{
    const bool lines = (mStyle & (kStyleUnderline | kStyleStrikeout)) != 0;
    uint32_t cell = width; // the glyph's cell: its width, widened to its advance and its positive left bearing
    if (lines) {
        if (glyph->advance > 0) {
            cell = static_cast<uint32_t>(glyph->advance);
            if (cell < width) cell = width;
        }
        if (glyph->bearingX > 0) {
            const int32_t bearing = glyph->bearingX;
            glyph->left += bearing;
            cell += static_cast<uint32_t>(bearing);
            glyph->right += bearing;
            glyph->bearingX = 0;
        }
    }
    const uint32_t end = x + cell;
    cache->cursor = end;
    if (end >= cache->width) GrowCache(cache, cell * 0x20 + x, cache->height < mLineHeight ? mLineHeight : cache->height);
    if (y + height > cache->height) {
        GrowCache(cache, cache->width, y + height);
        height = cache->height >= y ? cache->height - y : 0;
    }
    if (width != 0) {
        const uint8_t *row = source;
        uint32_t rowPitch = sourcePitch;
        uint8_t *expanded = nullptr;
        if (mode == 2) {
            // A 1-bit source, most significant bit first, widened to 0xff / 0.
            expanded = static_cast<uint8_t *>(::operator new(static_cast<size_t>(width) * height));
            uint8_t *out = expanded;
            for (uint32_t r = 0; r < height; ++r, out += width)
                for (uint32_t c = 0; c < width; ++c) out[c] = (source[r * sourcePitch + (c >> 3)] & (0x80 >> (c & 7))) ? 0xff : 0;
            row = expanded;
            rowPitch = width;
        }
        uint8_t *dst = cache->pixels + cache->pitch * y + glyph->left;
        for (uint32_t left = height; left != 0; --left) {
            std::memcpy(dst, row, width);
            if (mQuality != 1 && !mGammaTable.empty())
                for (uint8_t *p = dst; p < dst + width; ++p) *p = mGammaTable[*p];
            row += rowPitch;
            dst += cache->pitch;
        }
        std::free(expanded);
    }
    if (lines) {
        // The lines: rows of 0xff across the cell, clipped to the line height; the glyph's box grows to hold them.
        const auto drawLine = [&](int32_t first, uint32_t thickness) {
            if (first < 0 || static_cast<uint32_t>(first) >= mLineHeight) return;
            int32_t drawn = 0;
            uint32_t at = static_cast<uint32_t>(first);
            while (drawn < static_cast<int32_t>(thickness) && at < mLineHeight) {
                std::memset(cache->pixels + cache->pitch * at + x, 0xff, cell);
                ++drawn;
                ++at;
            }
            if (glyph->bottom < first + drawn) glyph->bottom = first + drawn;
        };
        if (mStyle & kStyleUnderline) {
            const uint32_t thickness = UnderlineThickness();
            drawLine(mAscent - glyph->bearingY + static_cast<int32_t>(thickness), thickness);
        }
        if (mStyle & kStyleStrikeout) {
            const uint32_t thickness = StrikeoutThickness();
            drawLine(mAscent - static_cast<int32_t>(mLineHeight >> 2) - glyph->bearingY, thickness);
        }
        glyph->left = static_cast<int32_t>(x);
        if (x != 0) glyph->right = static_cast<int32_t>(end);
    }
    return true;
}

bool cGZFont::GrowCache(cGZFontGlyphCache *cache, uint32_t width, uint32_t height)
{
    if (width > cache->width || height > cache->height) {
        const size_t size = static_cast<size_t>(height + 4) * width;
        uint8_t *const memory = static_cast<uint8_t *>(::operator new(size));
        std::memset(memory, 0, size);
        uint8_t *const pixels = memory + width * 2; // two blank rows above the bitmap
        if (cache->pixels) {
            uint8_t *dst = pixels;
            for (uint32_t r = 0; r < cache->height; ++r, dst += width) std::memcpy(dst, cache->pixels + cache->pitch * r, cache->width);
            std::free(cache->memory);
        }
        cache->pixels = pixels;
        cache->memory = memory;
        cache->width = width;
        cache->height = height;
        cache->pitch = width;
    }
    return true;
}

// ---- text: characters and measuring ----

bool cGZFont::SupportsEncoding(uint32_t encoding)
{
    // The characters that tell a code page's script: the face must have a glyph for each.
    struct Probe {
        uint32_t count, codes[2];
    };
    Probe probe = {0, {0, 0}};
    switch (encoding) {
    case 8:
    case 0x10: return true;
    case 0: // ASCII, and 1252
    case 0x4e4: probe = {1, {0x61, 0}}; break;
    case 0x36a: probe = {2, {0xe01, 0xe2c}}; break;     // 874 Thai
    case 0x3a4: probe = {2, {0x3061, 0x30a4}}; break;   // 932 Japanese
    case 0x3a8: probe = {2, {0x70a7, 0x7535}}; break;   // 936 Simplified Chinese
    case 0x3b5: probe = {2, {0xc2b9, 0xc9d1}}; break;   // 949 Korean
    case 0x3b6: probe = {2, {0x6f54, 0x70b5}}; break;   // 950 Traditional Chinese
    case 0x4e2: probe = {1, {0x142, 0}}; break;         // 1250
    case 0x4e3: probe = {1, {0x401, 0}}; break;         // 1251
    case 0x4e5: probe = {2, {0x370, 0x393}}; break;     // 1253
    case 0x4e6: probe = {1, {0x11e, 0}}; break;         // 1254
    case 0x4e7: probe = {2, {0x5d0, 0x5e2}}; break;     // 1255
    case 0x4e8: probe = {2, {0x621, 0x641}}; break;     // 1256
    case 0x4e9: probe = {1, {0x160, 0}}; break;         // 1257
    default: break;
    }
    for (uint32_t i = 0; i < probe.count; ++i) {
        int32_t err = 0;
        if (ff::HasGlyph(mFile->T2K(), probe.codes[i], &err) == 0) return false;
    }
    return true;
}

uint32_t cGZFont::CharAt(const void *text, uint32_t length, uint32_t *advance)
{
    if (length == 0) return 0;
    const uint8_t *const bytes = static_cast<const uint8_t *>(text);
    const int32_t encoding = static_cast<int32_t>(mEncoding);
    if (encoding == RZText::kUtf16) {
        if (advance) *advance = 2;
        return *static_cast<const uint16_t *>(text);
    }
    if (bytes[0] <= 0x7f) {
        if (advance) *advance = 1;
        return bytes[0];
    }
    // ORIGINAL-BUG: the converter writes its one UTF-16 unit over the low half of a stack word that starts as the text's
    // address, so when it writes nothing the result is the low 16 bits of the address.
    uint32_t slot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(text));
    uint32_t units = 1;
    if (encoding == RZText::kAscii) {
        RZText::Convert(text, 1, RZText::kAscii, &slot, &units, RZText::kUtf16);
        if (advance) *advance = 1; // not the count the converter used
    } else {
        const uint32_t take = RZText::CharLength(text, length, encoding);
        const uint32_t used = RZText::Convert(text, take, encoding, &slot, &units, RZText::kUtf16);
        if (advance) *advance = used;
    }
    return slot & 0xffff;
}

bool cGZFont::HasLineBreak(const void *text, uint32_t length)
{
    const uint8_t *at = static_cast<const uint8_t *>(text);
    const uint8_t *const end = at + length;
    while (at < end) {
        uint32_t units = 0;
        const uint32_t ch = CharAt(at, length, &units);
        if (ch == 10 || ch == 13) return true;
        at += units;
    }
    return false;
}

namespace {

// The glyph of the text's first character (null for no glyph).
const cGZFontCachedGlyph *FirstGlyph(cGZFont &font, const void *text, uint32_t length)
{
    const uint32_t ch = font.mEncoding == 0x10 ? *static_cast<const uint16_t *>(text) : font.CharAt(text, length, nullptr);
    return font.FindGlyph(ch);
}

} // namespace

int32_t cGZFont::GetCharWidth(const void *text, uint32_t length)
{
    if (!mLoaded) return 0;
    const cGZFontCachedGlyph *const glyph = FirstGlyph(*this, text, length);
    return glyph ? glyph->right - glyph->left : 0;
}

int32_t cGZFont::GetCharAdvance(const void *text, uint32_t length)
{
    if (!mLoaded) return 0;
    const cGZFontCachedGlyph *const glyph = FirstGlyph(*this, text, length);
    return glyph ? glyph->advance + mSpacingX : 0;
}

int32_t cGZFont::GetCharBearing(const void *text, uint32_t length)
{
    if (!mLoaded) return 0;
    const cGZFontCachedGlyph *const glyph = FirstGlyph(*this, text, length);
    return glyph ? glyph->bearingX : 0;
}

uint32_t cGZFont::FitChars(const void *text, uint32_t length, uint32_t width, uint32_t *used)
{
    uint32_t consumed = 0, measured = 0, keptConsumed = 0, keptMeasured = 0;
    if (length != 0) {
        for (;;) {
            if (measured >= width) {
                if (measured > width) {
                    consumed = keptConsumed;
                    measured = keptMeasured;
                }
                break;
            }
            keptMeasured = measured;
            keptConsumed = consumed;
            const uint32_t offset = mEncoding == 0x10 ? consumed * 2 : consumed;
            consumed += RZText::CharLength(static_cast<const uint8_t *>(text) + offset, length - consumed, static_cast<int32_t>(mEncoding));
            measured = GetWidth(text, consumed);
            if (consumed >= length) {
                if (measured > width) {
                    consumed = keptConsumed;
                    measured = keptMeasured;
                }
                break;
            }
        }
    }
    if (used) *used = measured;
    return consumed;
}

uint32_t cGZFont::WrapLines(const void *text, uint32_t length, uint32_t width, uint8_t mode, uint8_t *overflow)
{
    uint32_t lines = 0;
    const uint32_t limit = width != 0 ? width : 1;
    const uint8_t *at = static_cast<const uint8_t *>(text);
    const uint8_t *const end = at + (mEncoding == 0x10 ? length * 2 : length);
    uint32_t remaining = length;
    *overflow = 0;
    while (at < end) {
        const uint32_t used = BreakLine(at, remaining, limit, nullptr, mode + 1u);
        uint32_t kept;
        if (GetTrimmedWidth(at, used, &kept) > limit) *overflow = 1;
        at += mEncoding == 0x10 ? used * 2 : used;
        remaining -= used;
        ++lines;
    }
    return lines;
}

uint32_t cGZFont::CountLines(const void *text, uint32_t length, uint32_t width)
{
    uint8_t overflow;
    return WrapLines(text, length, width, 1, &overflow);
}

uint32_t cGZFont::WidthForLines(const void *text, uint32_t length, uint32_t height)
{
    const uint32_t target = height / static_cast<uint32_t>(GetLineSpacing());
    uint32_t low = 0x14, high = 4000;
    uint8_t overflow = 0;
    do {
        const uint32_t middle = (high + low) >> 1;
        const uint32_t lines = WrapLines(text, length, middle, 0, &overflow);
        if (lines > target || overflow != 0) {
            low = middle + 1;
        } else {
            if (low == high) return low;
            high = middle;
        }
    } while (low <= high);
    return 0;
}

bool cGZFont::FitRect(const void *text, uint32_t length, cGZRect *rect, int32_t lineWidth, int32_t maxWidth, int32_t maxHeight)
{
    const int32_t lineHeight = GetLineSpacing();
    if (maxHeight < lineHeight) maxHeight = lineHeight;
    if (lineWidth == -1 || maxWidth == -1) {
        // The width of an 'a': the character is written into the low byte of the argument's own word.
        const uint32_t probe = (static_cast<uint32_t>(lineWidth) & ~0xffu) | 0x61;
        const int32_t aWidth = GetCharWidth(&probe, 1);
        if (lineWidth == -1) lineWidth = aWidth * 12;
        if (maxWidth == -1) maxWidth = aWidth << 5;
        if (maxWidth <= lineWidth) maxWidth = lineWidth * 2;
    }
    int32_t measured;
    if (!HasLineBreak(text, length)) {
        measured = static_cast<int32_t>(GetWidth(text, length));
        if (measured < lineWidth && lineHeight <= maxHeight) {
            rect->right = rect->left + measured;
            rect->bottom = rect->top + lineHeight;
            return true;
        }
        if (measured < lineWidth * 2) {
            const int32_t twoLines = lineHeight * 2;
            const uint32_t width = WidthForLines(text, length, static_cast<uint32_t>(twoLines));
            if (width < static_cast<uint32_t>(maxWidth) && twoLines <= maxHeight) {
                rect->right = rect->left + static_cast<int32_t>(width);
                rect->bottom = rect->top + twoLines;
                return true;
            }
        }
        if (measured < maxWidth && lineHeight <= maxHeight) {
            rect->right = rect->left + measured;
            rect->bottom = rect->top + lineHeight;
            return true;
        }
    } else {
        measured = 1;
    }
    const int32_t maxLines = maxHeight / lineHeight;
    int32_t lines = measured / maxWidth + 1;
    int32_t height = lines * lineHeight;
    while (lines <= maxLines) {
        const uint32_t width = WidthForLines(text, length, static_cast<uint32_t>(height));
        if (width != 0 && width < static_cast<uint32_t>(maxWidth) && height <= maxHeight) {
            rect->right = rect->left + static_cast<int32_t>(width);
            rect->bottom = rect->top + height;
            return true;
        }
        height += lineHeight;
        ++lines;
    }
    rect->right = rect->left;
    rect->bottom = rect->top;
    return false;
}

void cGZFont::MeasureRect(const void *text, uint32_t length, cGZRect *rect, bool wrap, bool fixedWidth)
{
    if (length == 0 || text == nullptr) {
        rect->right = rect->left;
        rect->bottom = GetLineSpacing() + rect->top;
        return;
    }
    int32_t height;
    if (!wrap) {
        rect->right = static_cast<int32_t>(GetWidth(text, length)) + rect->left;
        height = GetLineSpacing();
    } else if (!fixedWidth) {
        rect->right = rect->left + static_cast<int32_t>(WidthForLines(text, length, static_cast<uint32_t>(rect->bottom - rect->top)));
        return;
    } else {
        const int32_t spacing = GetLineSpacing();
        height = static_cast<int32_t>(CountLines(text, length, static_cast<uint32_t>(rect->right - rect->left))) * spacing;
    }
    rect->bottom = height + rect->top;
}

void cGZFont::GetExtent(const void *text, uint32_t length, cGZRect *rect)
{
    const int32_t height = static_cast<int32_t>(GetLineHeight(0, 0));
    rect->left = 0;
    rect->top = 0;
    rect->right = 0;
    rect->bottom = height;
    rect->right = static_cast<int32_t>(GetWidth(text, length));
}

// ---- text: layout, widths and the line breaker ----

bool cGZFont::IsSpace(uint32_t ch)
{
    return ch == 0x20 || (ch >= 0x2002 && ch <= 0x200b);
}

uint32_t cGZFont::ThaiSign(uint32_t ch)
{
    const uint32_t unit = ch & 0xffff;
    if (unit < 0xe0d || unit > 0xe4d) return 0;
    switch (unit) {
    case 0xe0d: case 0xe31: case 0xe33: case 0xe34: case 0xe35: case 0xe36: case 0xe37: case 0xe47: case 0xe4d: return 1;
    case 0xe48: case 0xe49: case 0xe4a: case 0xe4b: case 0xe4c: return 3;
    default: return 0;
    }
}

bool cGZFont::HasThai(const uint16_t *text, uint32_t count)
{
    for (const uint16_t *at = text, *const end = text + count; at < end; ++at)
        if (ThaiSign(*at) != 0) return true;
    return false;
}

uint32_t cGZFont::ToUtf16(const void *text, uint32_t count, std::vector<int16_t> &buffer)
{
    if (mEncoding == RZText::kUtf16) {
        buffer.resize(count);
        std::memcpy(buffer.data(), text, count * 2);
        return count;
    }
    uint32_t room = static_cast<uint32_t>(buffer.size());
    RZText::Convert(text, count, static_cast<int32_t>(mEncoding), buffer.data(), &room, RZText::kUtf16);
    return room;
}

void cGZFont::Layout(const void *text, uint32_t count, const uint16_t **outText, uint32_t *outCount, uint32_t index,
                     uint32_t clearWords)
{
    std::vector<int16_t> &buffer = mLayout[index];
    if ((clearWords & 0xff) != 0 && !mLayoutWords.empty()) mLayoutWords.clear();
    if (mEncoding == RZText::kUtf16) {
        const uint16_t *const units = static_cast<const uint16_t *>(text);
        if (!HasThai(units, count)) {
            *outText = units;
            *outCount = count;
            return;
        }
        if (buffer.size() < count * 2) buffer.resize(count * 2);
        const uint32_t n = ToUtf16(text, count, buffer);
        *outCount = n;
        ShapeThai(reinterpret_cast<uint16_t *>(buffer.data()), n, outText, outCount, clearWords);
        return;
    }
    if (buffer.size() < count * 12) buffer.resize(count * 12);
    const uint32_t n = ToUtf16(text, count, buffer);
    *outCount = n;
    const uint16_t *const units = reinterpret_cast<const uint16_t *>(buffer.data());
    if (HasThai(units, n)) ShapeThai(reinterpret_cast<uint16_t *>(buffer.data()), n, outText, outCount, clearWords);
    else *outText = units;
}

uint32_t cGZFont::GetWidth(const void *text, uint32_t length)
{
    if (length == 0) return 0;
    const uint16_t *at = nullptr;
    uint32_t units = 0;
    Layout(text, length, &at, &units, 4, 0);
    const uint32_t encoding = mEncoding;
    mEncoding = RZText::kUtf16;
    uint32_t width = 0;
    for (const uint16_t *const end = at + units; at < end;) {
        width += static_cast<uint32_t>(GetCharAdvance(at, 1));
        at += RZText::CharUnits(at, static_cast<uint32_t>(end - at), static_cast<int32_t>(mEncoding));
    }
    mEncoding = encoding;
    return width;
}

uint32_t cGZFont::GetTrimmedWidth(const void *text, uint32_t length, uint32_t *used)
{
    *used = 0;
    if (length == 0) return 0;
    const uint16_t *at = nullptr;
    uint32_t units = 0;
    Layout(text, length, &at, &units, 3, 0);
    while (units > 0 && IsSpace(at[units - 1])) --units;
    if (units == 0) return 0;
    const uint32_t encoding = mEncoding;
    mEncoding = RZText::kUtf16;
    const uint32_t width = GetWidth(at, units);
    mEncoding = encoding;
    // ORIGINAL-BUG: for an encoding other than ASCII and UTF-16 the count of source units is that of the first *used (0)
    // characters of the text, so *used stays 0 (the trimmed count was meant).
    *used = encoding != RZText::kUtf16 && encoding != 0 ? RZText::CharsLength(text, *used, static_cast<int32_t>(encoding)) : units;
    return width;
}

uint32_t cGZFont::BreakLine(const void *text, uint32_t length, uint32_t maxWidth, uint32_t *width, uint32_t mode)
{
    if (maxWidth == 0 || length == 0) {
        if (width) *width = 0;
        return 0;
    }
    const uint16_t *units = nullptr;
    uint32_t count = 0;
    Layout(text, length, &units, &count, 2, 1);
    const uint32_t encoding = mEncoding;
    mEncoding = RZText::kUtf16;
    uint32_t consumed = 0;
    uint32_t total = 0;
    const uint16_t *at = units;
    while (consumed < count) {
        if (*at == 10 || *at == 13) {
            ++consumed;
            break;
        }
        const uint32_t cluster = RZText::ClusterLength(at, count - consumed);
        uint32_t trimmedUnits;
        if (GetTrimmedWidth(at, cluster, &trimmedUnits) + total > maxWidth) break;
        total += GetWidth(at, cluster);
        consumed += cluster;
        at += cluster;
        if (total > maxWidth) break;
    }
    if (consumed == 0) {
        if (mode == 2) {
            if (maxWidth != 0) {
                uint32_t previous = 0;
                do {
                    previous = consumed;
                    consumed += RZText::IsThaiBlock(units[consumed]) ? RZText::ThaiClusterLength(units + consumed, count - consumed) : 1;
                } while (GetWidth(units, consumed) < maxWidth);
                if (previous == 0) previous = consumed;
                consumed = previous;
            }
            if (consumed < count && !mLayoutWords.empty() && mLayoutWords[consumed - 1] == mLayoutWords[consumed]) ++consumed;
        } else if (mode == 1 && count != 0) {
            do {
                const uint16_t ch = units[consumed];
                if (ch == 0 || ch == 10 || ch == 13 || IsSpace(ch)) break;
                consumed += RZText::ClusterLength(units + consumed, count - consumed);
            } while (consumed < count);
        } else {
            goto done;
        }
    }
    if (static_cast<int32_t>(consumed) > 1 && !IsSpace(units[consumed - 1]) && consumed < count && IsSpace(units[consumed])) ++consumed;
done:
    mEncoding = encoding;
    if (!mLayoutWords.empty()) consumed = mLayoutWords[consumed];
    const uint32_t result = RZText::CharsLength(text, consumed, static_cast<int32_t>(encoding));
    if (width) *width = GetWidth(text, result);
    return result;
}

// ---- the string and glyph slots, and the draw ----

namespace {
// Whether the "blackFont" switch is on the command line (the framework's, slot 21; its slot 9 asks).
bool BlackFontSwitch(cIGZCmdLine *commandLine)
{
    cRZString key;
    key.Construct("blackFont");
    const bool present = commandLine->IsSwitchPresent(*key.AsInterface());
    key.Destruct();
    return present;
}
} // namespace

// 0x90d9e5 (thiscall, ret 8; the answer is a byte in al)
bool cGZFont::HasGlyphs(const void *text, uint32_t length)
{
    if (length == 0) return true;
    const uint16_t *units;
    uint32_t count;
    Layout(text, length, &units, &count, 0, 0);
    // The face is asked about UTF-16 units: the encoding is the layout's output while it is, then put back.
    const uint32_t kept = mEncoding;
    mEncoding = 0x10;
    bool all = true;
    for (const uint16_t *at = units; at < units + count; ++at) {
        int32_t err = 0;
        if (ff::HasGlyph(mFile->T2K(), *at, &err) == 0) {
            all = false;
            break;
        }
    }
    mEncoding = kept;
    return all;
}

// 0x90e156 (thiscall, ret 8; the answer is a byte in al)
bool cGZFont::CacheGlyphs(const void *text, uint32_t length)
{
    const bool loaded = mLoaded;
    if (!loaded || length == 0) return loaded;
    const uint16_t *units;
    uint32_t count;
    Layout(text, length, &units, &count, 0, 0);
    for (uint32_t i = 0; i < count; ++i) FindGlyph(units[i]);
    return loaded;
}

// 0x90c8a5 (thiscall, ret 0x10)
uint32_t cGZFont::ClipText(cIGZString *text, const cIGZString *suffix, const cGZRect *rect, bool wrap)
{
    const uint32_t length = text->Strlen();
    if (wrap) return WrapText(text, suffix, rect);
    const uint32_t room = static_cast<uint32_t>(rect->right - rect->left);
    if (FitChars(text->Data(), length, room, nullptr) >= length) return length;
    const uint32_t suffixLength = suffix->Strlen();
    const uint32_t suffixWidth = GetWidth(suffix->Data(), suffixLength);
    if (static_cast<int32_t>(suffixWidth) >= static_cast<int32_t>(room)) {
        text->Resize(0);
        return length;
    }
    text->Resize(FitChars(text->Data(), length, room - suffixWidth, nullptr));
    while (text->Strlen() != 0) {
        const char *const data = text->Data();
        const uint32_t kept = text->Strlen();
        if (data[kept - 1] != ' ') break;
        text->Resize(text->Strlen() - 1);
    }
    const uint32_t more = suffix->Strlen();
    text->Append(suffix->Data(), more);
    return length;
}
// 0x90c9b2 (thiscall, ret 0xc; always 1). Dividing by a zero line spacing faults, as the original's idiv does.
bool cGZFont::WrapText(cIGZString *text, const cIGZString *suffix, const cGZRect *rect)
{
    const FontSysString source(*text);
    const char *at = source.s.Data();
    const char *const end = at + source.s.Strlen();
    const int32_t spacing = GetLineSpacing();
    int32_t linesLeft = (rect->bottom - rect->top) / spacing;
    const uint32_t room = static_cast<uint32_t>(rect->right - rect->left - 8);
    std::vector<FontSysString> lines;
    while (at != end && linesLeft > 0) {
        const uint32_t remaining = static_cast<uint32_t>(end - at);
        const uint32_t consumed = BreakLine(at, remaining, room, nullptr, 2);
        if (consumed == 0) break;
        FontSysString line;
        line.s.FromChar(at, consumed);
        if (linesLeft == 1) {
            // The last line is cut to leave room for four suffixes.
            const uint32_t suffixLength = suffix->Strlen();
            const uint32_t suffixWidth = GetWidth(suffix->ToChar(), suffixLength);
            const cGZRect last = {0, 0, static_cast<int32_t>(rect->right - 4 * suffixWidth - rect->left), spacing};
            ClipText(line.s.AsInterface(), suffix, &last, false);
        }
        lines.push_back(line);
        if (consumed < remaining && linesLeft == 1) break;
        at += consumed;
        --linesLeft;
    }
    text->Erase(0, 0xffffffffu);
    for (FontSysString &line : lines) text->Append(*line.s.AsInterface());
    text->Append(*suffix);
    return true;
}

// 0x90e536 (thiscall, ret 8)
void cGZFont::SetColourRamp(uint32_t first, uint32_t second)
{
    const auto table = [](uint32_t address) { return reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(address)); };
    if (mColourTable) {
        const uint32_t *const current = table(mColourTable);
        if (current[127] == first && current[0] == second) return;
    }
    for (auto at = mBlocks.begin(); at != mBlocks.end(); ++at) {
        const uint32_t *const block = reinterpret_cast<const uint32_t *>(*at);
        if (block[127] != first || block[0] != second) continue;
        uint8_t *const found = *at;
        mColourTable = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(found));
        if (at != mBlocks.begin()) {
            mBlocks.erase(at);
            mBlocks.push_front(found);
        }
        return;
    }
    uint32_t *const made = reinterpret_cast<uint32_t *>(::operator new[](0x200));
    std::memset(made, 0, 0x200);
    mColourTable = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(made));
    ifaces::cIGZGraphicSystem *const graphics = GetGraphicSystem();
    uint8_t r1, g1, b1, r2, g2, b2;
    graphics->ConvertNativeToRGB(first & 0xffff, &r1, &g1, &b1);
    graphics->ConvertNativeToRGB(second & 0xffff, &r2, &g2, &b2);
    for (uint32_t i = 0; i < 8; ++i) made[i] = second;
    for (int32_t row = 0; row < 15; ++row) {
        // Each component steps from the second colour towards the first by row/16 of the difference (truncated towards zero).
        const auto mix = [row](uint8_t from, uint8_t to) {
            return static_cast<uint8_t>((static_cast<int32_t>(from) - static_cast<int32_t>(to)) * row / 16 + static_cast<int32_t>(to));
        };
        uint32_t native;
        graphics->ConvertRGBToNativeAt(&native, mix(r1, r2), mix(g1, g2), mix(b1, b2));
        for (uint32_t i = 0; i < 8; ++i) made[8 + row * 8 + i] = native;
    }
    for (uint32_t i = 120; i < 128; ++i) made[i] = first;
    // ORIGINAL-BUG: the table the full list drops is never freed (the list holds the only pointer to it).
    if (mBlocks.size() >= 4) mBlocks.pop_back();
    mBlocks.push_front(reinterpret_cast<uint8_t *>(made));
}

// 0x90c129 (thiscall, ret 0x24; the answer is a byte in al)
bool cGZFont::DrawText(cIGZBuffer *buffer, int32_t x, int32_t y, const void *text, uint32_t length, uint32_t colour,
                       uint32_t colour2, const cGZRect *clip, int32_t *pos)
{
    if (!mLoaded || length == 0) return mLoaded;
    const uint32_t self = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<cIGZFont *>(this)));
    const uint16_t *units;
    uint32_t count;
    Layout(text, length, &units, &count, 1, 0);
    uint32_t mode = mQuality;
    cIGZCmdLine *const commandLine = static_cast<cIGZFrameWork *>(GetServiceObject())->CommandLine();
    if (mQuality == 2) {
        uint32_t second = colour2;
        if (colour2 == 0x8000000) {
            mode = mUnknown68;
            if (mUnknown68 == 2) second = colour != 0 ? 0 : 0xffffffffu;
        }
        if (BlackFontSwitch(commandLine))
            if (ifaces::cIGZGraphicSystem *const graphics = GetGraphicSystem()) colour = graphics->ConvertRGBToNative(0, 0, 0);
        SetColourRamp(colour, second);
    }
    if (colour == 0x8000000) colour = mColour;
    if (BlackFontSwitch(commandLine))
        if (ifaces::cIGZGraphicSystem *const graphics = GetGraphicSystem()) colour = graphics->ConvertRGBToNative(0, 0, 0);
    bool began = false;
    if (buffer && mDrawDepth == 0) {
        if (!mRenderer->BeginDraw(self, buffer, RendererMode(mode))) return false;
        ++mDrawDepth;
        began = true;
    }
    if (pos) {
        x = pos[0];
        y = pos[1];
    }
    if (mGlyphRun.size() < count) mGlyphRun.resize(count);
    cGZFontGlyph *const run = mGlyphRun.data();
    uint32_t kept = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t code = units[i];
        const cGZFontCachedGlyph *glyph = FindGlyph(code);
        if (!glyph) {
            code = mDefaultChar;
            glyph = FindGlyph(code);
            if (!glyph) continue;
        }
        const int32_t width = glyph->right - glyph->left, height = glyph->bottom - glyph->top;
        if (width != 0 && height != 0) {
            const int32_t left = glyph->bearingX + x, top = glyph->bearingY + mSpacingY + y;
            const int32_t right = left + width, bottom = top + height;
            if (!clip || (left < clip->right && top < clip->bottom && clip->left < right && clip->top < bottom)) {
                cGZFontGlyph &entry = run[kept++];
                entry.rect = {left, top, right, bottom};
                entry.pixels = reinterpret_cast<const uint8_t *>(glyph); // the bitmap's place is worked out below
                entry.width = width;
                entry.height = height;
                entry.code = code;
            }
        }
        x += glyph->advance + mSpacingX;
    }
    const cGZFontGlyphCache *const cache = GetGlyphCache();
    for (uint32_t i = 0; i < kept; ++i) {
        const auto *glyph = reinterpret_cast<const cGZFontCachedGlyph *>(run[i].pixels);
        run[i].pixels = reinterpret_cast<const uint8_t *>(reinterpret_cast<uintptr_t>(cache->pixels) +
                                                          static_cast<uint32_t>(glyph->top) * cache->pitch + static_cast<uint32_t>(glyph->left));
        run[i].pitch = static_cast<int32_t>(cache->pitch);
    }
    if (buffer && kept != 0) {
        const uint32_t *const colours = mode == 2 ? reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(mColourTable)) : &colour;
        mRenderer->DrawGlyphs(self, buffer, run, kept, RendererMode(mode), colours, clip);
    }
    if (pos) {
        pos[0] = x;
        pos[1] = y;
    }
    if (began) {
        mRenderer->EndDraw(self, buffer, RendererMode(mode));
        --mDrawDepth;
    }
    return mLoaded;
}

// 0x90c526 (thiscall, ret 0x24; the answer is a byte in al)
bool cGZFont::DrawBox(cIGZBuffer *buffer, const cGZRect *rect, const void *text, uint32_t length, uint32_t align, uint32_t colour,
                      uint32_t colour2, const cGZRect *clip, int32_t *pos)
{
    if (!mLoaded || length == 0) return mLoaded;
    const uint32_t self = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<cIGZFont *>(this)));
    const uint16_t *units;
    uint32_t count;
    Layout(text, length, &units, &count, 0, 0);
    // The laid-out text is UTF-16: the font reads it as such while the lines are measured and drawn.
    const uint32_t kept = mEncoding;
    mEncoding = 0x10;
    bool began = false;
    if (buffer && mDrawDepth == 0) {
        if (!mRenderer->BeginDraw(self, buffer, RendererMode(mQuality))) {
            mEncoding = kept;
            return false;
        }
        ++mDrawDepth;
        began = true;
    }
    int32_t y = rect->top, x = rect->left;
    const int32_t spacing = GetLineSpacing();
    const int32_t width = rect->right - rect->left;
    const uint16_t *at = units;
    const uint16_t *const end = units + count;
    uint32_t remaining = count;
    if (width > 0) {
        if (pos && pos[0] != rect->left) x = pos[0];
        int32_t where[2];
        bool drawn = false; // ORIGINAL-BUG: with no line drawn the original writes uninitialised stack words to *pos; this leaves it
        while (y < rect->bottom && at < end) {
            const int32_t line = static_cast<int32_t>(BreakLine(at, remaining, static_cast<uint32_t>(width), nullptr, 2));
            int32_t shown = line;
            while (shown > 0) {
                const uint32_t unit = at[shown - 1];
                if (unit != 10 && unit != 13 && !IsSpace(unit)) break;
                --shown;
            }
            if (align == 0) {
                x = rect->left;
            } else if (align == 1) {
                x = rect->left + (width / 2 - static_cast<int32_t>(GetWidth(at, static_cast<uint32_t>(shown))) / 2);
            } else if (align == 2) {
                x = rect->right - static_cast<int32_t>(GetWidth(at, static_cast<uint32_t>(shown)));
            }
            if (pos) {
                where[0] = x;
                where[1] = y;
                drawn = true;
            }
            if (buffer && shown != 0)
                DrawText(buffer, x, y, at, static_cast<uint32_t>(shown), colour, colour2, clip, pos ? where : nullptr);
            at += line;
            remaining -= static_cast<uint32_t>(line);
            y += spacing;
        }
        if (pos && drawn) {
            pos[0] = where[0];
            pos[1] = where[1];
        }
    }
    if (began) {
        mRenderer->EndDraw(self, buffer, RendererMode(mQuality));
        --mDrawDepth;
    }
    mEncoding = kept;
    return mLoaded;
}
