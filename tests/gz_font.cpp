// Differential tests for cGZFont as a whole class, its font file reader and its file (src/gz/cGZFont.h; lane OC,
// rendering R6).
//
// The world, the same on both sides (every stand-in sits over the image's code and, where ours replaces it, over ours):
// - the Bitstream Font Fusion (T2K) engine (db/third-party.jsonl; ours is src/font/FontFusion.h, compared with the
//   original by tests/font_fusion.cpp): the entry points the class calls are stood in with fakes, over the image's and
//   over ours (the original calls the addresses, ours the shim's functions). The fakes log their arguments (the engine's
//   objects as tokens), fail on the call a case says (an error code, or a null object), hand out one T2K object whose
//   metrics, charmap, style bits and names come from the case, and read the file through the read callback they are given
//   (the image's 0x90e340 or ours: both log as one), so the stream's header decoding is compared;
// - the file under the stream: cRZFile's constructor from a path (0x1c0c07) and destructor (0x1c0e6d) stood in to build an
//   in-memory fake file (a cIGZFile over the case's bytes: some with the 'MXFN' header, some short, some unopenable);
//   cRZFile::PathExists 0x1c1242 answers from the case;
// - ConvertBytesToString 0x1afc9d (copies the text, UTF-16 by its low bytes), the buffer font renderer's constructor
//   0x90a7b5 (a fake renderer in the 8-byte block), and the
//   image's operator new / delete (0x824632 / 0x824624) and free (0x6c4b50) on this exe's CRT, so blocks the test puts into
//   either side's object (cache bitmaps, kept blocks, cached glyphs) are freed alike;
// - the system code page global 0xff23a4 preset (the original would call the GetACP import, a trap here).
// GZFont_Slots: each case builds a font per side (the original by the COM director's creator 0x909fb9 or operator new and
// the constructor 0x90a7db; ours by cGZFont::Create or new), runs 40 random steps over every slot that is ours (through the
// object's vtable: the game's on the original side), logs each result and the whole object (scalars, strings, gamma table,
// reader, engine object fields, caches by content), then releases it to zero and logs the renderers' counts and the heap
// balance. GZFontFile_Reader: the reader alone (constructor, Open, Name with every `which`, Style, Close, destructor).
// GZFont_GameSlots: the slot order against the game's vtable, and the forwarders: our slot calls the game code at its RVA
// with `this` and every argument, and returns its result. GZFont_RendererMode: the hooked helper 0x90ae06.
// Functions no slot names, run inside the steps on both sides (the original's at their RVAs, ours as members):
// - GZFont_Slots: the destructor 0x90a94e, the deleting destructor 0x90a9f1 (slot 58) and Release's last reference
//   0x90c7a4 (each side's final Release); the glyph caches' constructor 0x90a692 (the font's constructor), destructor
//   0x90a91b (the font's destructor) and Clear 0x90b6ba (SetScale, SetAdvanceScale, Unload, BuildGammaTable); the gamma
//   table 0x90ae25 (Load, SetGamma, SetContrast);
// - GZFont_Slots and GZFontFile_Reader: the file stream's constructor 0x90a6f7, DataLength 0x90dd48, ReadAll 0x90e2dd
//   (a Type 1 file read whole) and Read 0x90e280 (through the read callback 0x90e340 the engine fakes call), all in Open;
// - GZFont_Slots, lane OF (the glyph slots): MakeGlyph 0x90aa13 (slot 63; its glyph-name lookup 0x90a4a5 is inline in ours), PlaceBitmap
//   0x90b826 (64), GrowCache 0x90e359 (65), the Type 1 glyph index lookup 0x90cf86 (cGZFontFile::GlyphIndex), the box bitmap 0x90bb4f
//   and the line thicknesses 0x90d654 (underline) / 0x90d10a (strike-through). The engine fakes now include the glyph renderer
//   0x91c9a8 (a deterministic bitmap and metrics from the glyph index, mode, flags and the case's salt), the memory purge
//   0x91c8ba and the glyph-by-name lookup 0x91be55; the image's glyph-name map (0x10ecd04, built by 0x90afb0) is set up and torn
//   down around the test (NameTable). The glyph dumps leave out each glyph's pad word (+0x16), which the original never writes.
#include "code_patch.h"
#include "difftest.h"
#include "shape.h"

#include "font/FontFusion.h"
#include "game/game.h"
#include "gz/GZCOM.h"
#include "gz/cGZBufferFontBlit.h"
#include "gz/cGZFont.h"
#include "gz/cIGZString.h"
#include "ifaces/cIGZFile.h"
#include "rz/RZTextBreak.h"
#include "rz/RZTextChars.h"
#include "rz/RZTextConvert.h"
#include "rz/cRZFile.h"
#include "rz/cRZString.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace gzfont {

// ---- the log ----

std::vector<std::string> gLog;
difftest::AllocationCounter *gHeap; // the side's (diagnostics: the balance after each step)
std::vector<std::string> gErrors; // misuse seen by a fake (an unexpected file slot)

void Log(const char *fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    gLog.emplace_back(buf);
}

uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

uint32_t Hash(const void *data, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) h = (h ^ static_cast<const uint8_t *>(data)[i]) * 16777619u;
    return h;
}

std::string Hex(const void *data, size_t n)
{
    std::string s;
    char b[4];
    for (size_t i = 0; i < n; ++i) {
        std::snprintf(b, sizeof(b), "%02x", static_cast<const uint8_t *>(data)[i]);
        s += b;
    }
    return s;
}

uint32_t P(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// ---- the case ----

enum Entry { eMem, eNonRam, eType, eExtract, eInput3, eSfnt, eT2K, eTransform, eGlyph, eName, eRender, ePurge, eByName, eEntries };
const char *const kEntryNames[eEntries] = {"mem",   "nonram", "type", "extract", "input3", "sfnt", "t2k",
                                           "transform", "glyph", "name", "render", "purge", "byname"};

struct Fault {
    uint8_t mode = 0; // 0 none, 1 an error code, 2 a null object (creators) and an error code
    uint8_t call = 0; // which call of the side fails (1-based)
};

struct FileSpec {
    std::string path;
    bool exists = true;   // PathExists
    bool openable = true; // the fake file's Open
    std::vector<uint8_t> bytes;
};

// A name the face has (ids 1, 2, 4): found from the lookup `from` on (0: English (US) with the preferred encoding,
// 1: English (US) with encoding 0, 2: any language, 3: any language with encoding 0); -1 never.
struct NameSpec {
    int from = -1;
    bool wide = false;
    std::string text;
    std::u16string wtext;
};

struct Case {
    std::vector<FileSpec> files;
    Fault fault[eEntries];
    int32_t ascender = 0, descent = 0, lineGap = 0; // 16.16
    uint32_t charmap = 0;
    bool os2 = true;
    uint8_t os2Bits = 0;      // two bits per face made (the side's first, second, ... NewT2K), so a reload can change style
    bool fixedMetrics = false; // the transform leaves the metrics as they are
    NameSpec names[3]; // ids 1, 2, 4
    bool viaCreator = false;
    uint32_t salt = 0;       // the fake engine's glyph indices, bitmaps and metrics come from the glyph, the mode and this
    unsigned missing = 0;    // of 8 characters, how many the face has no glyph for
    bool narrow = false;     // the engine's advances stay under 11 pixels (text cases: lines the wrapping ops can use)
};
Case gCase;

// ---- per side ----

struct FakeInput {
    uint8_t head[8];
    bool used;
};

struct Side {
    unsigned calls[eEntries];
    unsigned inputs;
    FakeInput input[64]; // one or two per open; 40 steps open at most 80 times in theory, a few dozen in practice
    void *font; // the side's font as cIGZFont *
} gSide;

alignas(8) uint8_t gMemory[16];
alignas(8) uint8_t gSfnt[16];
alignas(8) uint8_t gT2K[0x200];
alignas(8) uint8_t gFontObject[0x80];
alignas(8) uint8_t gOs2[0x40];
alignas(8) uint8_t gBuffer[16]; // the draw target (a token)

void NewSide()
{
    std::memset(&gSide, 0, sizeof(gSide));
    std::memset(gT2K, 0, sizeof(gT2K));
    // A fresh log per side, its room taken before the side's heap count starts (both sides then grow it alike).
    std::vector<std::string>().swap(gLog);
    gLog.reserve(4096);
    std::vector<std::string>().swap(gErrors); // likewise: an error's first push must not cost one side a block only
    gErrors.reserve(16);
}

// The fault of this call of `e` (0 none).
uint8_t FaultOf(Entry e)
{
    const unsigned n = ++gSide.calls[e];
    return gCase.fault[e].mode && n == gCase.fault[e].call ? gCase.fault[e].mode : 0;
}

std::string Token(const void *p)
{
    if (!p) return "null";
    if (p == gMemory) return "mem";
    if (p == gSfnt) return "sfnt";
    if (p == gT2K) return "t2k";
    if (p == gBuffer) return "buffer";
    for (unsigned i = 0; i < gSide.inputs; ++i)
        if (p == &gSide.input[i]) return "input" + std::to_string(i);
    if (gSide.font && p == gSide.font) return "self";
    char b[16];
    std::snprintf(b, sizeof(b), "?%08x", P(p));
    return b;
}

std::string FontName(uint32_t font) { return Token(reinterpret_cast<const void *>(static_cast<uintptr_t>(font))); }

// ---- the font engine ----

using ReadFn = uint8_t(__cdecl *)(void *stream, uint8_t *dst, uint32_t offset, uint32_t count);

int ReadKind(void *read)
{
    if (read == GameAddress<void>(0x90e340)) return 1;
    if (read == reinterpret_cast<void *>(&cGZFontFileStream::ReadCallback)) return 1;
    return 99;
}

void *__cdecl FfNewMemhandler(int32_t *err)
{
    const uint8_t f = FaultOf(eMem);
    Log("ff.NewMem err=%d fault=%u", *err, f);
    if (f) *err = 10001;
    return f == 2 ? nullptr : gMemory;
}
void __cdecl FfDeleteMemhandler(void *memory) { Log("ff.DeleteMem %s", Token(memory).c_str()); }

FakeInput *NewInput()
{
    constexpr unsigned kInputs = sizeof(gSide.input) / sizeof(gSide.input[0]);
    if (gSide.inputs >= kInputs) {
        gErrors.emplace_back("too many inputs");
        gSide.inputs = kInputs - 1;
    }
    FakeInput *in = &gSide.input[gSide.inputs++];
    std::memset(in, 0, sizeof(*in));
    in->used = true;
    return in;
}

void *__cdecl FfNewNonRam(void *memory, void *stream, void *read, uint32_t length, int32_t *err)
{
    const uint8_t f = FaultOf(eNonRam);
    Log("ff.NonRam %s stream=%d read=%d length=%u err=%d fault=%u", Token(memory).c_str(), stream != nullptr, ReadKind(read),
        length, *err, f);
    FakeInput *in = NewInput();
    if (ReadKind(read) == 1 && stream) {
        const ReadFn fn = reinterpret_cast<ReadFn>(read);
        const uint32_t reads[3][2] = {{0, 8}, {2, 3}, {length - 2, 4}};
        for (const auto &r : reads) {
            uint8_t buf[8];
            std::memset(buf, 0xcc, sizeof(buf));
            const uint8_t ok = fn(stream, buf, r[0], r[1]);
            Log("  read %u %u -> %u %s", r[0], r[1], ok, Hex(buf, sizeof(buf)).c_str());
            if (r[0] == 0) std::memcpy(in->head, buf, 8);
        }
    }
    if (f) *err = 10002;
    return f == 2 ? nullptr : in;
}

void *__cdecl FfNewInput3(void *memory, void *data, uint32_t length, int32_t *err)
{
    const uint8_t f = FaultOf(eInput3);
    Log("ff.Input3 %s length=%u data=%08x err=%d fault=%u", Token(memory).c_str(), length, data ? Hash(data, length) : 0u, *err, f);
    FakeInput *in = NewInput();
    if (data) std::memcpy(in->head, data, length < 8 ? length : 8);
    if (f) *err = 10003;
    return f == 2 ? nullptr : in;
}

void __cdecl FfDeleteInput(void *input, int32_t *err) { Log("ff.DeleteInput %s err=%d", Token(input).c_str(), *err); }

// The engine's font type from the first byte the stream read (bits above 16 to check the class keeps only 16).
uint32_t __cdecl FfFindType(void *input, int32_t *err)
{
    static const uint32_t kTypes[8] = {1, 2, 7, 0x16, 3, 0x50002, 0x10001, 0};
    const uint8_t f = FaultOf(eType);
    const FakeInput *in = static_cast<const FakeInput *>(input);
    const uint32_t type = kTypes[in->head[0] % 8];
    Log("ff.FindType %s err=%d fault=%u -> %x", Token(input).c_str(), *err, f, type);
    if (f) *err = 10004;
    return type;
}

void __cdecl FfExtract(void *data, uint32_t *length, int32_t *err)
{
    const uint8_t f = FaultOf(eExtract);
    Log("ff.Extract length=%u data=%08x err=%d fault=%u", *length, Hash(data, *length), *err, f);
    if (f) {
        *err = 10005;
        return;
    }
    if (*length) static_cast<uint8_t *>(data)[0] ^= 0x55;
    *length -= *length / 4;
}

void *__cdecl FfNewSfnt(void *memory, uint32_t type, uint32_t a, void *input, uint32_t b, uint32_t c, int32_t *err)
{
    const uint8_t f = FaultOf(eSfnt);
    Log("ff.NewSfnt %s type=%x %x %s %x %x err=%d fault=%u", Token(memory).c_str(), type, a, Token(input).c_str(), b, c, *err, f);
    if (f) *err = 10006;
    return f == 2 ? nullptr : gSfnt;
}
void __cdecl FfDeleteSfnt(void *sfnt, int32_t *err) { Log("ff.DeleteSfnt %s err=%d", Token(sfnt).c_str(), *err); }

template <class T> T &At(uint8_t *object, uint32_t offset) { return *reinterpret_cast<T *>(object + offset); }

uint8_t *__cdecl FfNewT2K(void *memory, void *sfnt, int32_t *err)
{
    const uint8_t f = FaultOf(eT2K);
    Log("ff.NewT2K %s %s err=%d fault=%u", Token(memory).c_str(), Token(sfnt).c_str(), *err, f);
    std::memset(gT2K, 0, sizeof(gT2K));
    std::memset(gFontObject, 0, sizeof(gFontObject));
    std::memset(gOs2, 0, sizeof(gOs2));
    At<uint32_t>(gT2K, 0x5c) = gCase.charmap;
    At<int32_t>(gT2K, 0x6c) = gCase.lineGap;
    At<int32_t>(gT2K, 0x74) = gCase.descent;
    At<int32_t>(gT2K, 0x7c) = gCase.ascender;
    At<uint8_t *>(gT2K, 0x180) = gFontObject;
    At<uint8_t *>(gFontObject, 0x40) = gCase.os2 ? gOs2 : nullptr;
    gOs2[0x30] = static_cast<uint8_t>(gCase.os2Bits >> (2 * ((gSide.calls[eT2K] - 1) % 4)));
    At<uint16_t>(gFontObject, 0x78) = 0x7777;
    At<uint16_t>(gFontObject, 0x7a) = 0x7777;
    if (f) *err = 10007;
    return f == 2 ? nullptr : gT2K;
}
void __cdecl FfDeleteT2K(uint8_t *t2k, int32_t *err) { Log("ff.DeleteT2K %s err=%d", Token(t2k).c_str(), *err); }

// The metrics follow the matrix, so the class's reading of them after the transform is compared.
void __cdecl FfTransform(uint8_t *t2k, uint32_t a, uint32_t b, uint32_t c, int32_t *m, uint32_t d, int32_t *err)
{
    const uint8_t f = FaultOf(eTransform);
    Log("ff.Transform %s %u %u %u [%08x %08x %08x %08x] %u err=%d fault=%u", Token(t2k).c_str(), a, b, c, m[0], m[1], m[2], m[3],
        d, *err, f);
    if (t2k == gT2K && !gCase.fixedMetrics) {
        At<int32_t>(gT2K, 0x7c) = gCase.ascender + m[3] / 16;
        At<int32_t>(gT2K, 0x74) = gCase.descent - m[3] / 32;
        At<int32_t>(gT2K, 0x6c) = gCase.lineGap + m[0] / 64;
    }
    if (f) *err = 10008;
}

uint32_t Mix(uint32_t a, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0)
{
    uint32_t h = a * 2654435761u + 0x9e3779b9u;
    h ^= h >> 15;
    h = (h + b) * 2246822519u;
    h ^= h >> 13;
    h = (h + c) * 3266489917u;
    h ^= h >> 16;
    h = (h + d) * 668265263u;
    return h ^ (h >> 15);
}

// Whether the case's face has no glyph for a 16-bit character.
bool Missing(uint32_t code) { return (Mix(code & 0xffff, gCase.salt) >> 29) < gCase.missing; }

// (t2k, a character, err): the original's Load and MakeGlyph push three arguments (MakeGlyph the whole code: the engine
// takes 16 bits of it). Like every engine call it clears the error code first.
uint32_t __cdecl FfGlyphIndex(uint8_t *t2k, uint32_t code, int32_t *err)
{
    const uint8_t f = FaultOf(eGlyph);
    Log("ff.GlyphIndex %s %x err=%d fault=%u", Token(t2k).c_str(), code, *err, f);
    *err = f ? 10009 : 0;
    code &= 0xffff;
    const uint32_t low = (Missing(code) ? 0u : ((code * 7 + 3 + gCase.salt) & 0xffffu) | 1u);
    return 0xabc00000u | low; // the high half is the engine's to ignore
}

// (t2k, a character, flags byte, err): whether the face has a glyph for the character, which cGZFont's encoding probe
// (slot 25) asks; the original pushes the flags byte as a word. The engine clears the error code first.
uint32_t __cdecl FfHasGlyph(uint8_t *t2k, uint32_t code, uint32_t flags, int32_t *err)
{
    // The font's encoding at the call (+0x94) too: HasGlyphs (slot 27) puts 0x10 there while it asks, and the sides must agree.
    Log("ff.HasGlyph %s %x flags=%x enc=%x", Token(t2k).c_str(), code, flags & 0xff, gSide.font ? At<uint32_t>(static_cast<uint8_t *>(gSide.font), 0x94) : 0xffffffffu);
    *err = 0;
    return Missing(code) ? 0u : 1u;
}

// (t2k, a glyph name, err): the 16-bit character code the face's Type 1 glyph of that name has, 0 for none.
uint32_t __cdecl FfCharCodeByName(uint8_t *t2k, const char *name, int32_t *err)
{
    const uint8_t f = FaultOf(eByName);
    Log("ff.ByName %s '%s' err=%d fault=%u", Token(t2k).c_str(), name, *err, f);
    *err = f ? 10013 : 0;
    const uint32_t h = Mix(Hash(name, std::strlen(name)), gCase.salt);
    return h % 3 == 0 ? 0u : (h >> 8) & 0xffffu;
}

alignas(8) uint8_t gGlyphBits[40 * 40];
bool gGlyphLive; // the T2K object holds a rendered bitmap not yet purged

// (t2k, glyph index, x and y fraction, mode (0: 1-bit, 3: 8-bit), the reader's flags, err): the bitmap, its size and the
// metrics come from the glyph, the mode, the flags and the case; some glyphs are empty (no bitmap).
void __cdecl FfRender(uint8_t *t2k, uint32_t index, uint32_t xf, uint32_t yf, uint32_t mode, uint32_t flags, int32_t *err)
{
    const uint8_t f = FaultOf(eRender);
    Log("ff.Render %s index=%x %u %u mode=%u flags=%x err=%d fault=%u", Token(t2k).c_str(), index, xf, yf, mode, flags, *err, f);
    *err = f ? 10011 : 0;
    if (f) return;
    const uint32_t h = Mix(index, mode, flags, gCase.salt);
    const bool empty = (h & 7) == 0;
    const uint32_t width = empty ? 0 : (h >> 3) % 21, height = empty ? 0 : (h >> 9) % 19;
    const uint32_t pitch = (mode == 0 ? (width + 7) / 8 : width) + ((h >> 20) % 3);
    std::memset(gGlyphBits, 0, sizeof(gGlyphBits));
    for (uint32_t i = 0; i < pitch * height; ++i) gGlyphBits[i] = static_cast<uint8_t>(Mix(h, i) >> 11);
    At<uint8_t *>(t2k, 0x118) = empty ? nullptr : gGlyphBits;
    gGlyphLive = !empty;
    At<uint32_t>(t2k, 0x10c) = width;
    At<uint32_t>(t2k, 0x110) = height;
    At<uint32_t>(t2k, 0x114) = pitch;
    At<int32_t>(t2k, 0xe4) = static_cast<int32_t>((h >> 5) % 1500) - 700;  // 26.6, either sign
    At<int32_t>(t2k, 0xe0) = static_cast<int32_t>((h >> 11) % 2000) - 900;
    // 16.16 advances, with a charmap and without; a third have a small fraction (a scale just under 1 then truncates a whole
    // pixel off, which a space's advance rule compares against).
    auto advance = [&](uint32_t k) {
        uint32_t v = Mix(h, k) % (gCase.narrow ? 700000u : 3000000u);
        if (Mix(h, k + 8) % 3 == 0) v = (v & 0xffff0000u) | (Mix(h, k + 16) & 0x1ff);
        return static_cast<int32_t>(v);
    };
    At<int32_t>(t2k, 0x80) = advance(1);
    At<int32_t>(t2k, 0xd0) = advance(2);
}

// (t2k, level, err): releases the bitmap the render made.
void __cdecl FfPurge(uint8_t *t2k, uint32_t level, int32_t *err)
{
    const uint8_t f = FaultOf(ePurge);
    Log("ff.Purge %s %u err=%d fault=%u live=%d", Token(t2k).c_str(), level, *err, f, gGlyphLive);
    *err = f ? 10012 : 0;
    if (f) return;
    At<uint8_t *>(t2k, 0x118) = nullptr;
    gGlyphLive = false;
}

void __cdecl FfNameString(uint8_t *t2k, uint32_t language, uint32_t nameId, int32_t *err)
{
    const uint8_t f = FaultOf(eName);
    const uint16_t platform = At<uint16_t>(gFontObject, 0x78), encoding = At<uint16_t>(gFontObject, 0x7a);
    Log("ff.Name %s lang=%x id=%x err=%d platform=%x encoding=%x fault=%u", Token(t2k).c_str(), language, nameId, *err, platform,
        encoding, f);
    const int lookup = (language == 0x409 ? 0 : 2) + (encoding == 1 ? 0 : 1);
    const int index = nameId == 1 ? 0 : nameId == 2 ? 1 : nameId == 4 ? 2 : -1;
    At<const void *>(gT2K, 0x50) = nullptr;
    At<const void *>(gT2K, 0x54) = nullptr;
    if (index >= 0 && gCase.names[index].from >= 0 && lookup >= gCase.names[index].from) {
        const NameSpec &n = gCase.names[index];
        if (n.wide) At<const void *>(gT2K, 0x54) = n.wtext.c_str();
        else At<const void *>(gT2K, 0x50) = n.text.c_str();
    }
    if (f) *err = 10010;
}

// ---- the file ----

class FakeFile final : public ifaces::cIGZFile {
public:
    explicit FakeFile(const FileSpec *s) : spec(s) {}
    ~FakeFile() = default;

    const FileSpec *spec;
    uint32_t pos = 0;
    bool open = false;

    uint32_t Size() const { return spec ? static_cast<uint32_t>(spec->bytes.size()) : 0u; }
    void Unexpected(const char *what) { gErrors.emplace_back(std::string("unexpected file slot ") + what); }

    uint8_t QueryInterface(uint32_t, void *) override { return Unexpected("QueryInterface"), 0; }
    uint32_t AddRef() override { return Unexpected("AddRef"), 0; }
    uint32_t Release() override { return Unexpected("Release"), 0; }
    uint8_t Open(uint32_t access, uint32_t creation, uint32_t sharing) override
    {
        open = spec && spec->openable;
        Log("file.Open %u %u %u -> %u", access, creation, sharing, open);
        return open;
    }
    uint8_t IsOpen() override
    {
        Log("file.IsOpen %u", open);
        return open;
    }
    uint8_t Close() override { return Unexpected("Close"), 0; }
    int32_t Position() override { return Unexpected("Position"), 0; }
    uint32_t Length() override
    {
        Log("file.Length %u", Size());
        return Size();
    }
    uint8_t SetLength(uint32_t) override { return Unexpected("SetLength"), 0; }
    int32_t SeekToBegin() override
    {
        Log("file.SeekToBegin");
        pos = 0;
        return 0;
    }
    int32_t SeekToEnd() override { return Unexpected("SeekToEnd"), 0; }
    int32_t SeekToRelativePosition(int32_t) override { return Unexpected("SeekToRelativePosition"), 0; }
    int32_t SeekToPosition(int32_t position) override
    {
        Log("file.Seek %d", position);
        pos = static_cast<uint32_t>(position);
        return position;
    }
    int32_t Seek(int32_t, uint32_t) override { return Unexpected("Seek"), 0; }
    uint8_t Read(void *dst, uint32_t length) override
    {
        const bool ok = open && pos <= Size() && length <= Size() - pos;
        Log("file.Read %u at %u -> %u", length, pos, ok);
        if (!ok) return 0;
        if (length) std::memcpy(dst, spec->bytes.data() + pos, length);
        pos += length;
        return 1;
    }
    uint8_t ReadWithCount(void *, uint32_t *) override { return Unexpected("ReadWithCount"), 0; }
    uint8_t Write(void *, uint32_t) override { return Unexpected("Write"), 0; }
    uint8_t WriteWithCount(void *, uint32_t *) override { return Unexpected("WriteWithCount"), 0; }
    uint8_t Flush() override { return Unexpected("Flush"), 0; }
    uint8_t SysFlush() override { return Unexpected("SysFlush"), 0; }
    uint8_t Remove() override { return Unexpected("Remove"), 0; }
    uint8_t Rename(void *) override { return Unexpected("Rename"), 0; }
    uint8_t Exists() override { return Unexpected("Exists"), 0; }
    uint8_t CopyFrom(cIGZFile *, uint8_t) override { return Unexpected("CopyFrom"), 0; }
    uint8_t CopyFromPath(void *, uint8_t) override { return Unexpected("CopyFromPath"), 0; }
    void *GetPath() override { return Unexpected("GetPath"), nullptr; }
    void vf26_cIGZFile() override { Unexpected("26"); }
    void vf27_cIGZFile() override { Unexpected("27"); }
    void vf28_cIGZFile() override { Unexpected("28"); }
    void SetPath(void *) override { Unexpected("SetPath"); }
    uint8_t GetOpenModes(uint32_t *, uint32_t *, uint32_t *) override { return Unexpected("GetOpenModes"), 0; }
    void vf31_cIGZFile() override { Unexpected("31"); }
    uint8_t SetBuffering(int32_t readSize, int32_t writeSize) override
    {
        Log("file.SetBuffering %d %d", readSize, writeSize);
        return 1;
    }
    void *DeletingDestructor(uint32_t) override { return Unexpected("DeletingDestructor"), nullptr; }
};
static_assert(sizeof(FakeFile) <= sizeof(cRZFile), "the fake file fits in a cRZFile");

std::string TextOf(const cIGZString &s) { return std::string(s.Data(), s.Strlen()); }

const FileSpec *FindFile(const std::string &path)
{
    for (const FileSpec &f : gCase.files)
        if (f.path == path) return &f;
    return nullptr;
}

cRZFile *__fastcall StandFileConstruct(cRZFile *self, void *, const cIGZString &path)
{
    const std::string text = TextOf(path);
    const FileSpec *spec = FindFile(text);
    Log("file.ctor %08x known=%d", Hash(text.data(), text.size()), spec != nullptr);
    ::new (static_cast<void *>(self)) FakeFile(spec);
    return self;
}
void __fastcall StandFileDestruct(cRZFile *self, void *)
{
    Log("file.dtor");
    reinterpret_cast<FakeFile *>(self)->~FakeFile();
}

bool __cdecl StandPathExists(const cIGZString &path)
{
    const std::string text = TextOf(path);
    const FileSpec *spec = FindFile(text);
    const bool exists = spec && spec->exists;
    Log("exists %08x -> %u", Hash(text.data(), text.size()), exists);
    return exists;
}

uint32_t __cdecl StandConvertBytes(const void *src, uint32_t length, int32_t encoding, cIGZString *dst)
{
    std::string t;
    if (encoding == 16) {
        const auto *w = static_cast<const char16_t *>(src);
        for (uint32_t i = 0; i < length; ++i) t += static_cast<char>(w[i] & 0xff);
    } else {
        t.assign(static_cast<const char *>(src), length);
    }
    Log("convertBytes %d %u %08x", encoding, length, Hash(t.data(), t.size()));
    dst->FromChar(t.c_str(), static_cast<uint32_t>(t.size()));
    return 1;
}

// ---- the renderer ----

unsigned gRuns, gRampRuns, gRampBuilds, gBlackAnswers, gClippedRuns, gBoxes, gBoxesNoWidth; // lane OH: what the draw ops reached (checked at the end of GZFont_Text)

class FakeRenderer final : public cIGZFontRenderer {
public:
    uint32_t count = 0;

    std::string Id() const;
    bool QueryInterface(uint32_t, void **) override
    {
        gErrors.emplace_back("renderer QueryInterface");
        return false;
    }
    uint32_t AddRef() override
    {
        Log("rend.AddRef %s -> %u", Id().c_str(), count + 1);
        return ++count;
    }
    uint32_t Release() override;
    bool vf3(uint32_t font) override
    {
        Log("rend.vf3 %s %s", Id().c_str(), FontName(font).c_str());
        return true;
    }
    bool ReleaseFont(uint32_t font) override
    {
        Log("rend.ReleaseFont %s %s", Id().c_str(), FontName(font).c_str());
        return true;
    }
    bool BeginDraw(uint32_t font, cIGZBuffer *target, uint32_t mode) override
    {
        Log("rend.BeginDraw %s %s %s %u", Id().c_str(), FontName(font).c_str(), Token(target).c_str(), mode);
        return true;
    }
    bool EndDraw(uint32_t font, cIGZBuffer *target, uint32_t mode) override
    {
        Log("rend.EndDraw %s %s %s %u", Id().c_str(), FontName(font).c_str(), Token(target).c_str(), mode);
        return true;
    }
    bool DrawGlyphs(uint32_t font, cIGZBuffer *target, cGZFontGlyph *glyphs, uint32_t glyphCount, uint32_t mode, const uint32_t *colours,
                    const cGZRect *clip) override
    {
        // Lane OH: the glyph run as the draw built it (the rectangles, the bitmap rows each glyph points at, the codes), the colours
        // (one word, or the 128-word ramp of quality 2) and the clip.
        Log("rend.DrawGlyphs %s %s %s count %u mode %u colours %s clip %s", Id().c_str(), FontName(font).c_str(), Token(target).c_str(),
            glyphCount, mode, mode == 2 ? ("ramp " + std::to_string(Hash(colours, 0x200))).c_str() : ("one " + std::to_string(colours[0])).c_str(),
            clip ? (std::to_string(clip->left) + "," + std::to_string(clip->top) + "," + std::to_string(clip->right) + "," +
                    std::to_string(clip->bottom)).c_str()
                 : "none");
        ++gRuns;
        if (mode == 2) ++gRampRuns;
        if (clip) ++gClippedRuns;
        for (uint32_t i = 0; i < glyphCount && i < 64; ++i) {
            const cGZFontGlyph &g = glyphs[i];
            uint32_t pixels = 2166136261u;
            for (int32_t row = 0; row < g.height; ++row) pixels = pixels * 31u + Hash(g.pixels + static_cast<ptrdiff_t>(g.pitch) * row, static_cast<size_t>(g.width));
            Log("  glyph %u rect %d %d %d %d size %d x %d pitch %d code %x pixels %08x", i, g.rect.left, g.rect.top, g.rect.right, g.rect.bottom,
                g.width, g.height, g.pitch, g.code, pixels);
        }
        return true;
    }
};
static_assert(sizeof(FakeRenderer) == 8, "the fake renderer fits in the buffer renderer's block");

FakeRenderer gHeld[2]; // given to SetRenderer by the test

std::string FakeRenderer::Id() const
{
    if (this == &gHeld[0]) return "held0";
    if (this == &gHeld[1]) return "held1";
    return "built";
}

uint32_t FakeRenderer::Release()
{
    Log("rend.Release %s -> %u", Id().c_str(), count - 1);
    if (--count == 0 && this != &gHeld[0] && this != &gHeld[1]) {
        this->~FakeRenderer();
        ::operator delete(static_cast<void *>(this));
        return 0;
    }
    return count;
}

std::string RendererName(const cIGZFontRenderer *r)
{
    if (!r) return "null";
    if (r == &gHeld[0]) return "held0";
    if (r == &gHeld[1]) return "held1";
    return "built"; // a renderer the font made (only one at a time)
}

cGZBufferFontRenderer *__fastcall StandRendererConstruct(cGZBufferFontRenderer *self, void *)
{
    Log("rend.ctor");
    ::new (static_cast<void *>(self)) FakeRenderer;
    return self;
}

// ---- lane OH: the draw's world: the graphics system (cell 0x10f51a4), the framework (cell 0x10c9378) and its command line ----

bool gBlackFont; // whether the fake command line has the "blackFont" switch
uint32_t Native565(uint32_t r, uint32_t g, uint32_t b) { return (((r & 0xff) >> 3) << 11) | (((g & 0xff) >> 2) << 5) | ((b & 0xff) >> 3); }
uint32_t __fastcall GfxToNative(void *, void *, uint32_t r, uint32_t g, uint32_t b)
{
    const uint32_t n = Native565(r, g, b);
    Log("gfx.ToNative %u %u %u -> %x", r & 0xff, g & 0xff, b & 0xff, n);
    return n;
}
uint32_t __fastcall GfxToNativeAt(void *, void *, uint32_t *out, uint32_t r, uint32_t g, uint32_t b)
{
    *out = Native565(r, g, b);
    ++gRampBuilds;
    Log("gfx.ToNativeAt %u %u %u -> %x", r & 0xff, g & 0xff, b & 0xff, *out);
    return 1;
}
uint32_t __fastcall GfxToRGB(void *, void *, uint32_t v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = static_cast<uint8_t>(((v >> 11) & 31) * 255 / 31);
    *g = static_cast<uint8_t>(((v >> 5) & 63) * 255 / 63);
    *b = static_cast<uint8_t>((v & 31) * 255 / 31);
    Log("gfx.ToRGB %x -> %u %u %u", v, *r, *g, *b);
    return 1;
}
uint32_t __fastcall CmdIsSwitch(void *, void *, const cIGZString *key)
{
    Log("cmd.IsSwitchPresent '%s' -> %u", TextOf(*key).c_str(), gBlackFont);
    gBlackAnswers += gBlackFont;
    return gBlackFont;
}
struct FakeVtObject {
    const void *const *vt;
};
const void *gGfxVt[48];
const void *gCmdVt[16];
const void *gFwVt[48];
FakeVtObject gGfxObject{gGfxVt}, gCmdObject{gCmdVt}, gFwObject{gFwVt};
void *__fastcall FwCommandLine(void *, void *) { return &gCmdObject; }
void InitDrawWorld()
{
    gGfxVt[34] = reinterpret_cast<const void *>(&GfxToNative);
    gGfxVt[35] = reinterpret_cast<const void *>(&GfxToNativeAt);
    gGfxVt[36] = reinterpret_cast<const void *>(&GfxToRGB);
    gCmdVt[9] = reinterpret_cast<const void *>(&CmdIsSwitch);
    gFwVt[21] = reinterpret_cast<const void *>(&FwCommandLine);
}

// ---- game code the slots reach ----

void *__cdecl GameNew(size_t size) { return ::operator new(size); }
void __cdecl GameDelete(void *p) { ::operator delete(p); }
void __cdecl GameFree(void *p) { std::free(p); }
int __cdecl NoAtExit(void *) { return 0; }

struct World {
    shape::StandIn memNew{0x9116c7, &fontfusion::tsi_NewMemhandler, &FfNewMemhandler};
    shape::StandIn memDelete{0x9115c0, &fontfusion::tsi_DeleteMemhandler, &FfDeleteMemhandler};
    shape::StandIn nonRam{0x911a54, &fontfusion::New_NonRamInputStream, &FfNewNonRam};
    shape::StandIn input3{0x9119be, &fontfusion::New_InputStream3, &FfNewInput3};
    shape::StandIn inputDelete{0x91180d, &fontfusion::Delete_InputStream, &FfDeleteInput};
    shape::StandIn findType{0x91dc8d, &fontfusion::FF_FindFontTypeFromStream, &FfFindType};
    shape::StandIn extract{0x91359f, &fontfusion::ExtractPureT1FromPCType1, &FfExtract};
    shape::StandIn sfntNew{0x9199cb, &fontfusion::New_sfntClass, &FfNewSfnt};
    shape::StandIn sfntDelete{0x9198e1, &fontfusion::Delete_sfntClass, &FfDeleteSfnt};
    shape::StandIn t2kNew{0x91be9a, &fontfusion::NewT2K, &FfNewT2K};
    shape::StandIn t2kDelete{0x91bcf2, &fontfusion::DeleteT2K, &FfDeleteT2K};
    shape::StandIn transform{0x91c300, &fontfusion::T2K_NewTransformation, &FfTransform};
    shape::StandIn glyphIndex{0x91c04e, &fontfusion::T2K_GetGlyphIndex, &FfGlyphIndex};
    shape::StandIn hasGlyph{0x91bdc8, &fontfusion::T2K_GlyphExists, &FfHasGlyph};
    // RZText::Convert (verified by tests/rz_text.cpp) on both sides: the original's imports are traps.
    shape::CodePatch convert{0x1afd58, reinterpret_cast<const void *>(&RZText::Convert)};
    shape::StandIn nameString{0x91da2f, &fontfusion::T2K_GetNameString, &FfNameString};
    shape::StandIn fileConstruct{0x1c0c07, static_cast<cRZFile *(cRZFile::*)(const cIGZString &)>(&cRZFile::Construct),
                                 &StandFileConstruct};
    shape::StandIn fileDestruct{0x1c0e6d, &cRZFile::Destruct, &StandFileDestruct};
    shape::StandIn exists{0x1c1242, &cRZFile::PathExists, &StandPathExists};
    shape::StandIn convertBytes{0x1afc9d, &ConvertBytesToString, &StandConvertBytes};
    shape::StandIn renderer{0x90a7b5, &cGZBufferFontRendererCode::Construct, &StandRendererConstruct};
    shape::StandIn render{0x91c9a8, &fontfusion::T2K_RenderGlyph, &FfRender};
    shape::StandIn purge{0x91c8ba, &fontfusion::T2K_PurgeMemory, &FfPurge};
    shape::StandIn byName{0x91be55, &fontfusion::T2K_GetCharCodeByName, &FfCharCodeByName};
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNew)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDelete)};
    shape::CodePatch gameFree{0x6c4b50, reinterpret_cast<const void *>(&GameFree)};
    // The line breaker's character classes (0x1b2293, 0x1b23d5, 0x1b0959, 0x1b0a60, 0x1b258f, 0x1b1d25) are the image's own
    // function-local statics, built on first use under the thread's epoch (-1 makes a zero guard take the slow path); their
    // registration for the exit (atexit) would allocate on the CRT heap, and nothing exits.
    shape::CodePatch atExit{0x824cbe, reinterpret_cast<const void *>(&NoAtExit)};
    int32_t *codePage = GameAddress<int32_t>(0xff23a4);
    int32_t savedCodePage = *codePage;
    int32_t *epoch = nullptr;
    int32_t savedEpoch = 0;
    // The graphics system and the framework the draw asks for (cells 0x10f51a4 and 0x10c9378).
    void **graphicsCell = GameAddress<void *>(0x10f51a4);
    void **frameworkCell = GameAddress<void *>(0x10c9378);
    void *savedGraphics = *graphicsCell, *savedFramework = *frameworkCell;
    World()
    {
        InitDrawWorld();
        *graphicsCell = &gGfxObject;
        *frameworkCell = &gFwObject;
        *codePage = 1252;
        const uint32_t index = *GameAddress<uint32_t>(0x10df814);
        epoch = reinterpret_cast<int32_t **>(__readfsdword(0x2c))[index];
        savedEpoch = epoch[1];
        epoch[1] = -1;
    }
    ~World()
    {
        epoch[1] = savedEpoch;
        *graphicsCell = savedGraphics;
        *frameworkCell = savedFramework;
        *codePage = savedCodePage;
    }
};

// The image's global glyph-name map (0x10ecd04: {head node, size}), which the original fills at its first lookup of a
// character by name (0x90afb0). The image's static initialiser (0x13e1bc) does not run in the harness: make the head node
// as it does (0x213c20), fill the map, and take it down afterwards, so the lookups of the original find the names ours
// holds in a table (the node blocks come and go through the stood-in operator new / delete).
struct NameTable {
    difftest::Context &ctx;
    uint32_t saved[2];
    explicit NameTable(difftest::Context &c) : ctx(c)
    {
        uint32_t *const map = GameAddress<uint32_t>(0x10ecd04);
        saved[0] = map[0];
        saved[1] = map[1];
        map[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx.Original<void *(__cdecl *)(void *)>(0x213c20)(map)));
        map[1] = 0;
        ctx.Original<void(__cdecl *)()>(0x90afb0)();
    }
    ~NameTable()
    {
        ctx.Original<void(__cdecl *)()>(0xd951a5)();
        uint32_t *const map = GameAddress<uint32_t>(0x10ecd04);
        map[0] = saved[0];
        map[1] = saved[1];
    }
};

// ---- seeding ----

const char *const kNames[] = {"Arial",           "Arial Bold",   "Arial Italic", "Times Italic Bold", "  Spaced Out  ",
                              "bold",            "ITALIC",       "XboldY",       "Regular",           "",
                              "Comic italicBOLD", "Verdana Bold ", "Bold Face",   "Italic"};

// A family name the original cuts without throwing (see the ORIGINAL-BUG note in cGZFontFile::Name): "italic" after
// "bold" (either case) never appears.
bool Cuttable(const std::string &s)
{
    std::string l = s;
    for (char &c : l) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const size_t bold = l.rfind("bold"), italic = l.rfind("italic");
    return bold == std::string::npos || italic == std::string::npos || italic < bold;
}

void NewCase(uint32_t seed)
{
    std::mt19937 rng(seed);
    auto pick = [&](unsigned n) { return static_cast<unsigned>(rng() % n); };
    gCase = Case{};
    const unsigned files = 1 + pick(4);
    for (unsigned i = 0; i < files; ++i) {
        FileSpec f;
        f.path = "C:\\Fonts\\face" + std::to_string(i) + (pick(2) ? ".ttf" : ".pfb");
        f.exists = pick(8) != 0;
        f.openable = pick(10) != 0;
        static const unsigned kLengths[] = {0, 5, 9, 10, 12, 13, 20, 30, 64};
        const unsigned length = kLengths[pick(9)];
        for (unsigned b = 0; b < length; ++b) f.bytes.push_back(static_cast<uint8_t>(rng()));
        if (length >= 9 && pick(2)) {
            const uint32_t count = pick(3) ? pick(length + 4) : 0xfffffff0u + pick(16);
            const uint8_t header[9] = {'M', 'X', 'F', 'N', uint8_t(count >> 24), uint8_t(count >> 16), uint8_t(count >> 8), uint8_t(count),
                                       uint8_t(rng())};
            std::memcpy(f.bytes.data(), header, 9);
            if (pick(6) == 0) f.bytes[pick(4)] ^= 0x20; // a near miss: not the magic
        }
        gCase.files.push_back(f);
    }
    // Faults in half of the cases only, so that the other half loads (and reloads) often.
    if (pick(2))
        for (Fault &f : gCase.fault) {
            const unsigned r = pick(12);
            if (r == 0) f = {1, uint8_t(1 + pick(3))};
            else if (r == 1) f = {2, uint8_t(1 + pick(3))};
        }
    switch (pick(6)) {
    case 0: break; // all zero: the line height floors at 1
    case 1:
        gCase.ascender = static_cast<int32_t>(pick(0x200000));
        gCase.descent = -static_cast<int32_t>(pick(0x100000));
        gCase.lineGap = static_cast<int32_t>(pick(0x40000));
        break;
    case 2: // around 0x1c pixels
        gCase.ascender = static_cast<int32_t>(0x140000 + pick(0x40000));
        gCase.descent = -static_cast<int32_t>(0x60000 + pick(0x20000));
        gCase.lineGap = static_cast<int32_t>(pick(0x20000));
        break;
    case 3: // negative pieces (rounding toward zero)
        gCase.ascender = -static_cast<int32_t>(pick(0x100000));
        gCase.descent = static_cast<int32_t>(pick(0x100000));
        gCase.lineGap = -static_cast<int32_t>(pick(0x10000));
        break;
    case 4: // a line of 0x1a..0x1c pixels whatever the size (the Type 1 extra pixel at 0x1c)
        gCase.fixedMetrics = true;
        gCase.ascender = static_cast<int32_t>(((0x1a + pick(3)) << 16) | pick(0x10000));
        break;
    default:
        gCase.ascender = static_cast<int32_t>(rng());
        gCase.descent = static_cast<int32_t>(rng()) >> 4;
        gCase.lineGap = static_cast<int32_t>(rng()) >> 8;
        break;
    }
    gCase.charmap = pick(3) ? 0x1234u : 0u;
    gCase.os2 = pick(5) != 0;
    gCase.os2Bits = static_cast<uint8_t>(rng());
    for (NameSpec &n : gCase.names) {
        n.from = pick(5) == 0 ? -1 : static_cast<int>(pick(4));
        n.wide = pick(2) != 0;
        do n.text = kNames[pick(sizeof(kNames) / sizeof(kNames[0]))];
        while (!Cuttable(n.text));
        n.wtext.clear();
        for (char c : n.text) n.wtext += static_cast<char16_t>(static_cast<unsigned char>(c) | (pick(4) == 0 ? 0x100 : 0));
    }
    gCase.viaCreator = pick(2) != 0;
    gCase.salt = static_cast<uint32_t>(rng());
    gCase.missing = pick(3) == 0 ? 0 : pick(5);
}

// ---- the object ----

const char *Str(const FontSysString &s) { return s.s.mString.c_str(); }

// A glyph's bytes without its pad word (+0x16): MakeGlyph (0x90aa13) builds the glyph in a stack record and copies all 0x18
// bytes into the cache, and never writes the pad, so the original's holds stack garbage.
std::string GlyphHex(const cGZFontCachedGlyph &g) { return Hex(&g, 0x16); }

void DumpCache(const char *name, const cGZFontGlyphCache &c)
{
    std::vector<std::pair<uint32_t, cGZFontCachedGlyph>> glyphs(c.glyphs.begin(), c.glyphs.end());
    std::sort(glyphs.begin(), glyphs.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    std::string list;
    for (const auto &g : glyphs) list += " " + std::to_string(g.first) + ":" + GlyphHex(g.second);
    // The pixels: the rows from the bitmap's start, and the two blank rows above it when it is a grown block.
    uint32_t pixels = 2166136261u;
    if (c.pixels && c.pitch >= c.width)
        for (uint32_t r = 0; r < c.height; ++r) pixels = pixels * 31u + Hash(c.pixels + c.pitch * r, c.width);
    uint32_t above = 0;
    if (c.memory && c.pixels > c.memory) above = Hash(c.memory, static_cast<size_t>(c.pixels - c.memory));
    Log("  %s memory=%d pixels=%d w=%u h=%u pitch=%u cursor=%u pixelhash=%08x above=%08x glyphs=%zu%s", name, c.memory != nullptr,
        c.pixels == nullptr ? 0 : c.pixels == c.memory ? 1 : 2, c.width, c.height, c.pitch, c.cursor, pixels, above, c.glyphs.size(),
        list.c_str());
}

void DumpFile(const cGZFontFile &f)
{
    if (gHeap) Log("  heap %lld", gHeap->Blocks());
    std::string indices; // the glyph index cache, in key order (the map is ordered)
    for (const auto &entry : f.glyphIndices) indices += " " + std::to_string(entry.first) + ">" + std::to_string(entry.second);
    Log("  file memory=%s sfnt=%s t2k=%s stream=%d input=%s type1=%zu:%08x flags=%x kind=%u indices=%zu%s", Token(f.memory).c_str(),
        Token(f.sfnt).c_str(), Token(f.t2k).c_str(), f.stream != nullptr, Token(f.input).c_str(), f.type1.size(),
        Hash(f.type1.data(), f.type1.size()), f.flags, f.kind, f.glyphIndices.size(), indices.c_str());
    if (f.stream) {
        const auto *file = reinterpret_cast<const FakeFile *>(&f.stream->file);
        Log("  stream header=%u obfuscated=%u key=%02x file pos=%u open=%u", f.stream->headerLength, f.stream->obfuscatedLength,
            f.stream->key, file->pos, file->open);
    }
    if (f.t2k == gT2K)
        Log("  t2k flag=%u platform=%x encoding=%x names=%d %d", At<uint32_t>(gT2K, 0x48), At<uint16_t>(gFontObject, 0x78),
            At<uint16_t>(gFontObject, 0x7a), At<const void *>(gT2K, 0x50) != nullptr, At<const void *>(gT2K, 0x54) != nullptr);
}

void Dump(const void *object)
{
    const cGZFont &o = *static_cast<const cGZFont *>(object);
    Log("  font loaded=%u ref=%d path='%s' family='%s' group=%u size=%u line=%u style=%x colours=%x,%x quality=%u u68=%u", o.mLoaded,
        o.mRefCount, Str(o.mPath), Str(o.mFamily), o.mGroup, o.mSize, o.mLineHeight, o.mStyle, o.mColour, o.mShadowColour,
        o.mQuality, o.mUnknown68);
    Log("  gamma=%08x contrast=%08x table=%zu %s", Bits(o.mGamma), Bits(o.mContrast), o.mGammaTable.size(),
        Hex(o.mGammaTable.data(), o.mGammaTable.size()).c_str());
    Log("  scale=%08x,%08x advance=%08x spacing=%d,%d encoding=%u depth=%d ascent=%d descent=%d renderer=%s", Bits(o.mScaleX),
        Bits(o.mScaleY), Bits(o.mAdvanceScale), o.mSpacingX, o.mSpacingY, o.mEncoding, o.mDrawDepth, o.mAscent, o.mDescentLine,
        RendererName(o.mRenderer).c_str());
    std::string layout;
    for (const auto &v : o.mLayout) layout += std::to_string(v.size()) + ",";
    // The colour ramp the draw keeps (lane OH): the blocks in their order (the most recently used first) by a hash of the 128 words each,
    // and which one the font's table is.
    std::string blocks, table = "none";
    unsigned index = 0;
    for (const uint8_t *block : o.mBlocks) {
        blocks += " " + std::to_string(Hash(block, 0x200));
        if (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(block)) == o.mColourTable) table = "block" + std::to_string(index);
        ++index;
    }
    if (o.mColourTable != 0 && table == "none") table = "elsewhere";
    Log("  layout=%s words=%zu char=%x glyph=%x run=%zu table=%s blocks=%zu:%s file=%d", layout.c_str(), o.mLayoutWords.size(), o.mDefaultChar,
        o.mDefaultGlyph, o.mGlyphRun.size(), table.c_str(), o.mBlocks.size(), blocks.c_str(), o.mFile != nullptr);
    if (o.mFile) DumpFile(*o.mFile);
    DumpCache("mono", o.mCacheMono);
    DumpCache("smooth", o.mCacheSmooth);
}

// ---- the steps ----

const float kNaN = std::numeric_limits<float>::quiet_NaN();

float PickFloat(std::mt19937 &rng, const float *values, unsigned n, float current)
{
    const unsigned i = rng() % (n + 2);
    if (i == n) return current;
    if (i == n + 1) return static_cast<float>(static_cast<int>(rng() % 4001) - 2000) / 100.0f;
    return values[i];
}

std::string PathFor(std::mt19937 &rng)
{
    const unsigned n = static_cast<unsigned>(gCase.files.size());
    const unsigned r = rng() % (3 * n + 2); // a file of the case three times as often as an empty or a missing path
    if (r < 3 * n) return gCase.files[r % n].path;
    return r == 3 * n ? std::string() : std::string("C:\\Fonts\\missing.ttf");
}

cGZFont &View(cIGZFont *f) { return *reinterpret_cast<cGZFont *>(f); }

// Whether making glyphs stays small: the cases' random metrics can make a line of tens of thousands of pixels, and the
// cache bitmap is (line height + 4) rows of 32 glyphs' width (both sides skip alike).
bool GlyphsAreCheap(const cGZFont &o)
{
    return o.mLineHeight <= 80 && o.mCacheMono.width <= 30000 && o.mCacheSmooth.width <= 30000;
}

// A piece of text in the font's encoding, with room after it (the originals read a unit past the end in places).
struct Text {
    std::vector<uint8_t> bytes;
    uint32_t length = 0; // in units
};

Text MakeText(std::mt19937 &rng, uint32_t encoding, bool wellFormed, bool thaiHeavy = false)
{
    static const uint8_t kPieces[][4] = {{'a'}, {'b'}, {'W'}, {'i'}, {' '}, {' '}, {'\n'}, {'\r'}, {'-'}, {'.'}, {','}, {'0'},
                                         {0x80}, {0xa1}, {0xe9}, {0xfc}, {0xc3, 0xa9}, {0xe2, 0x82, 0xac}, {0xe3, 0x81, 0x82},
                                         {0x83, 0x41}, {0xb0, 0xa1}, {0xf0, 0x90, 0x80, 0x80}, {0xc3}, {0xff}, {0x7f},
                                         // Thai (UTF-8; pieces 25 and up): letters, vowels above and below, tone marks, sara am, the repetition mark.
                                         {0xe0, 0xb8, 0x81}, {0xe0, 0xb8, 0xad}, {0xe0, 0xb8, 0xb1}, {0xe0, 0xb8, 0xb3}, {0xe0, 0xb8, 0xb4},
                                         {0xe0, 0xb8, 0xb8}, {0xe0, 0xb9, 0x80}, {0xe0, 0xb9, 0x88}, {0xe0, 0xb9, 0x8c}, {0xe0, 0xb9, 0x87},
                                         {0xe0, 0xb9, 0x86}, {0xe0, 0xb8, 0x8d}, {0xe0, 0xb8, 0xaf},
                                         // An ideograph (U+4E00; piece 38): the line breaker cuts between ideographs and takes a space after one.
                                         {0xe4, 0xb8, 0x80}};
    static const uint32_t kUnits[] = {'a', 'b', 'W', ' ', ' ', '\n', '\r', '-', '.', 0xe9, 0x20ac, 0x3042, 0xac00, 0xe9, 0x41,
                                      0xff21, 0x4e00, 0xd800, 0xdc00, 0x200b, 0x5000,
                                      0xe01, 0xe2d, 0xe31, 0xe33, 0xe34, 0xe38, 0xe40, 0xe48, 0xe4c, 0xe47, 0xe46, 0xe0d, 0xe2f, 0xe4d};
    Text t;
    const unsigned pieces = rng() % 4 == 0 ? rng() % 3 : 1 + rng() % 12;
    // The line breaker (0x90b40e) returns 0 when the text left starts with a space, a character that cannot start a line follows
    // (so the space does not end a cluster of its own) and the cluster is wider than the limit; its callers then never advance.
    // A stop or a comma is therefore not put after a space.
    bool afterSpace = false;
    // A Thai text now and then starts with a consonant and a sara am: the shaper turns the latter into a mark (U+0E4D) and a letter,
    // so the first syllable of the shaped text is two units long (the line breaker, mode 2, takes whole syllables).
    if (thaiHeavy && rng() % 2 == 0) {
        if (encoding == 16) {
            for (uint8_t b : {0x01, 0x0e, 0x33, 0x0e}) t.bytes.push_back(b);
            t.length += 2;
        } else if (encoding == 8) {
            for (uint8_t b : {0xe0, 0xb8, 0x81, 0xe0, 0xb8, 0xb3}) t.bytes.push_back(b);
            t.length += 6;
        }
    }
    for (unsigned i = 0; i < pieces; ++i) {
        if (encoding == 16) {
            uint32_t u = kUnits[rng() % (sizeof(kUnits) / sizeof(kUnits[0]))];
            if (thaiHeavy && rng() % 5 != 0) u = kUnits[21 + rng() % (sizeof(kUnits) / sizeof(kUnits[0]) - 21)];
            while (afterSpace && (u == '.' || u == ',')) u = kUnits[rng() % (sizeof(kUnits) / sizeof(kUnits[0]))];
            afterSpace = u == ' ' || u == 0x200b;
            t.bytes.push_back(static_cast<uint8_t>(u));
            t.bytes.push_back(static_cast<uint8_t>(u >> 8));
            ++t.length;
        } else {
            // Pieces 12 to 15 and 19 to 23 are left out: not UTF-8 (a stray high byte, a lone lead, a double-byte pair), or (21) a
            // four-byte form the decoder keeps only 16 bits of, which come out as U+0000, and the breaker returns 0 at a NUL.
            unsigned pick = rng() % (sizeof(kPieces) / sizeof(kPieces[0]));
            if (thaiHeavy && encoding == 8 && rng() % 5 != 0) pick = 25 + rng() % 13;
            while ((wellFormed && encoding == 8 && ((pick >= 12 && pick <= 15) || (pick >= 19 && pick != 24 && pick < 25))) ||
                   (afterSpace && (pick == 9 || pick == 10)))
                pick = rng() % 12;
            afterSpace = pick == 4 || pick == 5;
            const uint8_t *piece = kPieces[pick];
            for (unsigned b = 0; b < 4 && (b == 0 || piece[b]); ++b) {
                t.bytes.push_back(piece[b]);
                ++t.length;
            }
        }
    }
    t.bytes.resize(t.bytes.size() + 16, 0);
    return t;
}

// The widest single character of the text: the line breaker (slot 41, 0x90b40e) needs a limit at least as wide as the first
// cluster (a break character is a cluster of its own, see the fake classes) or it returns 0, and its callers
// (0x90b08f, 0x90b39e) then never advance.
uint32_t WidestChar(cIGZFont *f, const cGZFont &o, const Text &t, uint32_t length)
{
    const int32_t encoding = static_cast<int32_t>(o.mEncoding);
    const uint8_t *at = t.bytes.data();
    uint32_t widest = 0;
    for (uint32_t done = 0; done < length;) {
        const uint32_t n = RZText::CharLength(at, length - done, encoding);
        if (n == 0) break;
        widest = (std::max)(widest, f->GetWidth(at, n));
        done += n;
        at += n * (encoding == 16 ? 2 : 1);
    }
    return widest;
}

// A long run of 'a': exactly 4000 pixels wide when an 'a' divides 4000 (the widest WidthForLines tries, 0x90b39e), else about as
// wide; or, `tooWide`, more than twice that (the width for two lines is then beyond the limit and WidthForLines gives 0).
Text MakeRun(cIGZFont *f, const cGZFont &o, std::mt19937 &rng, bool tooWide)
{
    Text t;
    const uint8_t one[2] = {'a', 0};
    const uint32_t width = f->GetWidth(one, 1);
    Log("run: an 'a' is %u wide, too wide %u", width, tooWide);
    if (width == 0 || width > 10) return t;
    const uint32_t count = tooWide ? 8200 / width + rng() % 50 : 4000 / width;
    for (uint32_t i = 0; i < count; ++i) {
        t.bytes.push_back('a');
        if (o.mEncoding == 16) t.bytes.push_back(0);
    }
    t.length = count;
    t.bytes.resize(t.bytes.size() + 16, 0);
    return t;
}

// The text slots (25, 36, 40, 42 to 48 and 59) on a random piece of text. The guards are the original's own limits: it
// divides by the line spacing (WidthForLines at 0x90b39e and FitRect at 0x90b1e9, unsigned and signed) and by the
// widest allowed width (0x90b1e9), and reads the face file without a null check (0x90da9e).
// The line breaker (0x90b40e) returns 0 when the text left starts with a space and the cluster (the text up to the next place the
// line may break, 0x1b215d) is wider than the limit: a space is a cluster of its own only when what follows can start a line
// (0x1b2293), and then its width counts as 0. The wrapping ops' callers (0x90b08f, 0x90b39e) never advance on a 0, so texts
// that hold a space before a character that cannot start a line are left out of them.
bool SpaceThenNoStart(const cGZFont &o, const Text &t, uint32_t length)
{
    std::vector<uint16_t> units(length * 2 + 8, 0);
    uint32_t count = length;
    if (o.mEncoding == 16) {
        std::memcpy(units.data(), t.bytes.data(), length * 2);
    } else {
        count = static_cast<uint32_t>(units.size());
        RZText::Convert(t.bytes.data(), length, static_cast<int32_t>(o.mEncoding), units.data(), &count, RZText::kUtf16);
    }
    for (uint32_t i = 0; i + 1 < count; ++i)
        if ((units[i] == 0x20 || (units[i] >= 0x2002 && units[i] <= 0x200b)) && !RZText::CanStartLine(units[i + 1])) return true;
    return false;
}

// Whether the text holds a Thai character (U+0E00 to U+0E7F). The line breaker (0x90b40e) breaks Thai text in clusters (a letter
// with its marks), and a cluster can be wider than any one character of it: with a limit of the widest single character it
// returns 0, and the callers of the wrapping ops (0x90b08f, 0x90b39e) never advance (found by lane OH: a new draw of a seed
// reached WrapLines with Thai text, the original did not return).
bool HasThaiText(const cGZFont &o, const Text &t, uint32_t length)
{
    if (o.mEncoding == 16) {
        for (uint32_t i = 0; i < length; ++i) {
            const uint32_t u = t.bytes[2 * i] | (t.bytes[2 * i + 1] << 8);
            if (u >= 0xe00 && u <= 0xe7f) return true;
        }
        return false;
    }
    for (uint32_t i = 0; i + 2 < length; ++i)
        if (t.bytes[i] == 0xe0 && (t.bytes[i + 1] == 0xb8 || t.bytes[i + 1] == 0xb9)) return true; // UTF-8 for U+0E00 to U+0E7F
    return false;
}

// The text with a few spaces on its end (what GetTrimmedWidth, 0x90d128, cuts: U+0020 and U+2002 to U+200B, and two characters that
// look like spaces but are not), or only the spaces. `length` is in the encoding's units.
Text WithSpaces(const Text &t, uint32_t length, uint32_t encoding, std::mt19937 &rng, bool onlySpaces)
{
    static const uint32_t kSpaces[] = {0x20, 0x20, 0x2002, 0x2003, 0x200b, 0x3000, 0xa0};
    Text r;
    if (!onlySpaces) {
        r.bytes.assign(t.bytes.begin(), t.bytes.begin() + length * (encoding == 16 ? 2 : 1));
        r.length = length;
    }
    for (unsigned n = 1 + rng() % 4; n > 0; --n) {
        const uint32_t c = kSpaces[rng() % (sizeof(kSpaces) / sizeof(kSpaces[0]))];
        if (encoding == 16) {
            r.bytes.push_back(static_cast<uint8_t>(c));
            r.bytes.push_back(static_cast<uint8_t>(c >> 8));
            r.length += 1;
        } else if (encoding == 8 && c > 0x7f) {
            if (c < 0x800) {
                r.bytes.push_back(static_cast<uint8_t>(0xc0 | (c >> 6)));
                r.bytes.push_back(static_cast<uint8_t>(0x80 | (c & 0x3f)));
                r.length += 2;
            } else {
                r.bytes.push_back(static_cast<uint8_t>(0xe0 | (c >> 12)));
                r.bytes.push_back(static_cast<uint8_t>(0x80 | ((c >> 6) & 0x3f)));
                r.bytes.push_back(static_cast<uint8_t>(0x80 | (c & 0x3f)));
                r.length += 3;
            }
        } else {
            r.bytes.push_back(' ');
            r.length += 1;
        }
    }
    r.bytes.resize(r.bytes.size() + 16, 0);
    return r;
}

// Whether the text breaks into lines in `mode` for a limit, advancing every time (this side's slot 41): the loops of the wrapping slots
// and of the box draw never end for a 0 or for more units than remain.
bool BreaksInto(cIGZFont *f, const cGZFont &o, const Text &t, uint32_t length, uint32_t limit, uint32_t mode = 2)
{
    const uint32_t unitSize = o.mEncoding == 16 ? 2 : 1;
    uint32_t at = 0, guard = 0;
    while (at < length && ++guard < 1000) {
        const uint32_t n = f->BreakLine(t.bytes.data() + at * unitSize, length - at, limit, nullptr, mode);
        if (n == 0 || n > length - at) return false;
        at += n;
    }
    return true;
}

void TextStep(cIGZFont *f, cGZFont &o, std::mt19937 &rng)
{
    // Most cases have no loaded font at a small size by luck: a text step puts one in place first (logged like any step).
    if (!o.mLoaded && rng() % 4 != 0) {
        const FontSysString path(PathFor(rng).c_str());
        f->SetPath(path.s.AsInterface());
        Log("text: SetPath '%s'; Load -> %u", Str(path), f->Load());
    }
    if (o.mLoaded && !GlyphsAreCheap(o) && rng() % 4 != 0) Log("text: SetSize -> %u", f->SetSize(8 + rng() % 24));
    if (!o.mLoaded) {
        // The three single-character slots answer 0 for a font that is not loaded (0x90b672, 0x90cbd2, 0x90d672).
        const uint8_t ascii[2] = {static_cast<uint8_t>('a' + rng() % 20), 0};
        Log("unloaded: width %d advance %d bearing %d", f->GetCharWidth(ascii, 1), f->GetCharAdvance(ascii, 1),
            f->GetCharBearing(ascii, 1));
        Log("unloaded: CacheGlyphs %u", f->CacheGlyphs(ascii, 1));
    }
    // An encoding below zero (SetEncoding takes -3, which names no code page) makes the original's width (0x90d1dd) loop for ever.
    if (!o.mLoaded || !GlyphsAreCheap(o) || f->GetLineSpacing() == 0 || static_cast<int32_t>(o.mEncoding) < 0) {
        Log("text skipped");
        return;
    }
    unsigned textOp = rng() % 22;
    if (rng() % 4 == 0) textOp = 20 + rng() % 2; // the draws a quarter of the time more
    // The line-wrapping ops (5 to 12) get well-formed UTF-8: the original breaker (0x90b40e) converts the remaining text to
    // UTF-16, and a converter that stops at an invalid sequence leaves it no characters, so it returns 0 and its caller
    // (0x90b08f) never advances.
    // The double-byte code pages are left out of them for the same reason: a lead byte with no trail byte (or one cut off
    // by the end) converts to fewer characters than the unlimited look-ahead of 0x1b0d66 counts, and the line breaker returns
    // more units than remain.
    if (((textOp >= 5 && textOp <= 12) || textOp >= 18) && (o.mEncoding == 932 || o.mEncoding == 936 || o.mEncoding == 949 || o.mEncoding == 950)) {
        Log("text %u skipped: double-byte code page", textOp);
        return;
    }
    // The single-character ops (1 to 3) as well: a character the converter cannot decode comes back as the low 16 bits of the
    // text's own address (ORIGINAL-BUG, see cGZFont::CharAt), which differs between the sides by construction.
    // The direct line breaker and width ops (14, 15) get any text half of the time: they return what the original returns, whatever
    // the converter makes of it.
    // Layout's two callees, HasThai (0x90dd14) and ToUtf16 (0x90b7c0), are tested here through the slots that lay text out:
    // ToUtf16 in every encoding, HasThai with this Thai text.
    const bool thaiHeavy = (textOp == 14 || textOp == 16 || textOp == 17) && rng() % 3 == 0;
    // Thai text is only Thai in UTF-8 and UTF-16.
    if (thaiHeavy && o.mEncoding != 8 && o.mEncoding != 16) Log("SetEncoding for Thai text -> %u", f->SetEncoding(rng() % 2 ? 8 : 16));
    const Text t = MakeText(rng, o.mEncoding, (textOp >= 1 && textOp <= 3) || (textOp >= 5 && textOp <= 12) || textOp >= 16 || (textOp >= 14 && rng() % 2 == 0),
                            thaiHeavy);
    const void *const text = t.bytes.data();
    // A shorter length ends on a character boundary: the original's line breaker (0x90b40e) turns its count of characters
    // into units with an unlimited look-ahead (0x1b0d66), so a length that cuts a multi-byte character makes it return more
    // units than remain, and its caller (0x90b08f) then counts the remainder down past zero and reads on through memory.
    uint32_t length = t.length;
    if (rng() % 8 == 0) {
        length = RZText::CharsLength(t.bytes.data(), rng() % (t.length + 1), static_cast<int32_t>(o.mEncoding));
        if (length > t.length) length = t.length;
    }
    Log("text %u: %u units, encoding %d", textOp, length, static_cast<int32_t>(o.mEncoding));
    if (((textOp >= 5 && textOp <= 12) || textOp >= 19) && HasThaiText(o, t, length)) {
        Log("text %u skipped: Thai text (clusters wider than a character)", textOp);
        return;
    }
    if (textOp >= 5 && textOp <= 12 && SpaceThenNoStart(o, t, length)) {
        Log("text %u skipped: a space before a character that cannot start a line", textOp);
        return;
    }
    if (textOp >= 5 && textOp <= 12 && length != 0) {
        // The loop of the wrapping slots (0x90b08f and the like) on this side's line breaker, with the narrowest limit they use (the
        // widest character): a 0 anywhere in the text (a leading space with a cluster wider than the limit that follows it, as in
        // " -i0.", or " -." after an ideograph) never advances, and the original does not return.
        const uint32_t unitSize = o.mEncoding == 16 ? 2 : 1;
        const uint32_t limit = WidestChar(f, o, t, length);
        bool stuck = false;
        for (uint32_t mode = 1; mode <= 2 && !stuck; ++mode) {
            uint32_t at = 0, guard = 0;
            while (at < length && ++guard < 1000) {
                const uint32_t n = f->BreakLine(t.bytes.data() + at * unitSize, length - at, limit, nullptr, mode);
                if (n == 0 || n > length - at) {
                    stuck = true;
                    break;
                }
                at += n;
            }
        }
        if (stuck) {
            Log("text %u skipped: the line breaker returns 0 for a limit of the widest character", textOp);
            return;
        }
    }
    switch (textOp) {
    case 0: {
        static const uint32_t kEncodings[] = {0, 8, 16, 0x36a, 0x3a4, 0x3a8, 0x3b5, 0x3b6, 0x4e2, 0x4e3, 0x4e4, 0x4e5,
                                              0x4e6, 0x4e7, 0x4e8, 0x4e9, 1252, 12345, 0xffffffffu};
        const uint32_t e = kEncodings[rng() % (sizeof(kEncodings) / sizeof(kEncodings[0]))];
        if (!o.mFile) {
            Log("SupportsEncoding skipped");
            break;
        }
        Log("SupportsEncoding %x -> %u", e, f->SupportsEncoding(e));
        break;
    }
    case 1:
        Log("GetCharWidth %u -> %d", length, f->GetCharWidth(text, length));
        break;
    case 2:
        Log("GetCharAdvance %u -> %d", length, f->GetCharAdvance(text, length));
        break;
    case 3:
        Log("GetCharBearing %u -> %d", length, f->GetCharBearing(text, length));
        break;
    case 4: {
        const uint32_t width = rng() % 4 ? rng() % 150 : rng() % 6;
        uint32_t used = 0xcdcdcdcd;
        const bool wantUsed = rng() % 4 != 0;
        const uint32_t n = f->FitChars(text, length, width, wantUsed ? &used : nullptr);
        Log("FitChars %u width %u -> %u used %x", length, width, n, used);
        break;
    }
    case 5:
    case 6: {
        const uint32_t width = WidestChar(f, o, t, length) + (rng() % 4 ? rng() % 100 : 0);
        uint8_t overflow = 0xcd;
        // Modes 0 and 1 only: slot 41 (0x90b40e) returns 0 for a mode other than 1 or 2 when nothing fits, which loops 0x90b08f for ever.
        const uint32_t mode = rng() % 2;
        const uint32_t lines = f->WrapLines(text, length, width, static_cast<uint8_t>(mode), &overflow);
        Log("WrapLines %u width %u mode %u -> %u overflow %u", length, width, mode, lines, overflow);
        break;
    }
    case 7: {
        const uint32_t width = WidestChar(f, o, t, length) + rng() % 200;
        Log("CountLines %u width %u -> %u", length, width, f->CountLines(text, length, width));
        break;
    }
    case 8: {
        if (rng() % 3 == 0) {
            const bool tooWide = rng() % 3 == 0;
            const Text run = MakeRun(f, o, rng, tooWide);
            if (run.length != 0) {
                const uint32_t height = static_cast<uint32_t>(f->GetLineSpacing()) * (1 + rng() % 2);
                Log("WidthForLines run %u height %u -> %u", run.length, height, f->WidthForLines(run.bytes.data(), run.length, height));
                break;
            }
        }
        if (WidestChar(f, o, t, length) > 0x14) {
            Log("WidthForLines skipped"); // its narrowest line is 0x14 wide
            break;
        }
        const uint32_t height = rng() % 5 ? rng() % 300 : rng() % 40;
        Log("WidthForLines %u height %u -> %u", length, height, f->WidthForLines(text, length, height));
        break;
    }
    case 9:
    case 10: {
        const int32_t lineSpacing = f->GetLineSpacing();
        Text run;
        if (rng() % 3 == 0) run = MakeRun(f, o, rng, rng() % 2 == 0);
        const bool useRun = run.length != 0;
        const void *const fitText = useRun ? run.bytes.data() : text;
        const uint32_t fitLength = useRun ? run.length : length;
        if (!useRun && WidestChar(f, o, t, length) > 0x14) {
            Log("FitRect skipped"); // its narrowest line is 0x14 wide
            break;
        }
        int32_t lineWidth = rng() % 3 ? static_cast<int32_t>(rng() % 120) : -1;
        int32_t maxWidth = rng() % 3 ? 1 + static_cast<int32_t>(rng() % 250) : -1;
        int32_t maxHeight = rng() % 4 ? static_cast<int32_t>(rng() % 300) : static_cast<int32_t>(rng() % 20);
        if (useRun) {
            // Narrow first widths and wide limits too: the slot tries lines of 2 and more with a width for them of 0.
            lineWidth = static_cast<int32_t>(rng() % 2 ? rng() % 5000 : rng() % 2000);
            maxWidth = rng() % 2 ? 1 + static_cast<int32_t>(rng() % 5000) : 4000 + static_cast<int32_t>(rng() % 1500);
            maxHeight = lineSpacing * static_cast<int32_t>(2 + rng() % 5);
            if (rng() % 2 == 0) {
                // Two lines: a limit between half the run and the run, a first width under half of it.
                const int32_t whole = static_cast<int32_t>(f->GetWidth(fitText, fitLength));
                lineWidth = static_cast<int32_t>(rng() % static_cast<uint32_t>(whole / 2 + 1));
                maxWidth = whole / 2 + 1 + static_cast<int32_t>(rng() % static_cast<uint32_t>(whole / 2 + 1));
            }
        } else if (rng() % 2 == 0) {
            // Values on the edges of the slot's comparisons: the text's own width, the width for two lines, a derived width.
            const int32_t measured = static_cast<int32_t>(f->GetWidth(text, length));
            const int32_t two = static_cast<int32_t>(f->WidthForLines(text, length, static_cast<uint32_t>(lineSpacing) * 2));
            const int32_t d = static_cast<int32_t>(rng() % 3) - 1;
            const uint32_t probe = 0x61;
            const int32_t a = o.mEncoding == 16 ? 0 : f->GetCharWidth(&probe, 1);
            switch (rng() % 6) {
            case 0: lineWidth = measured + d; break;
            case 1: maxWidth = measured + d; break;
            case 2:
                lineWidth = measured - static_cast<int32_t>(rng() % 2);
                maxWidth = two + d;
                maxHeight = lineSpacing * 2 + static_cast<int32_t>(rng() % 2);
                break;
            case 3: maxWidth = 1 + static_cast<int32_t>(rng() % 2); break;
            case 4:
                if (a != 0) {
                    lineWidth = -1;
                    maxWidth = a * 12;
                }
                break;
            default:
                if (a != 0) {
                    lineWidth = a << 5;
                    maxWidth = -1;
                }
                break;
            }
            if (rng() % 2) maxHeight = lineSpacing * static_cast<int32_t>(1 + rng() % 4) + d;
        }
        {
            // The effective widths, as the slot derives them from an 'a': a zero maximum is a division by zero (0x90b1e9).
            const uint32_t probe = (static_cast<uint32_t>(lineWidth) & ~0xffu) | 0x61;
            const int32_t a = (lineWidth == -1 || maxWidth == -1) ? f->GetCharWidth(&probe, 1) : 0;
            const int32_t lw = lineWidth == -1 ? a * 12 : lineWidth;
            int32_t mw = maxWidth == -1 ? a << 5 : maxWidth;
            if ((lineWidth == -1 || maxWidth == -1) && mw <= lw) mw = lw * 2;
            if (mw == 0) {
                Log("FitRect skipped");
                break;
            }
        }
        cGZRect rect = {static_cast<int32_t>(rng() % 20), static_cast<int32_t>(rng() % 20), 0x1111, 0x2222};
        const bool fitted = f->FitRect(fitText, fitLength, &rect, lineWidth, maxWidth, maxHeight);
        Log("FitRect %u %d %d %d -> %u rect %d %d %d %d", fitLength, lineWidth, maxWidth, maxHeight, fitted, rect.left, rect.top, rect.right,
            rect.bottom);
        break;
    }
    case 11:
    case 12: {
        const bool wrap = rng() % 3 != 0, fixedWidth = rng() % 2 != 0;
        cGZRect rect = {static_cast<int32_t>(rng() % 20), static_cast<int32_t>(rng() % 20), static_cast<int32_t>(rng() % 200),
                        static_cast<int32_t>(rng() % 300)};
        const bool nullText = rng() % 8 == 0;
        const uint32_t widest = WidestChar(f, o, t, length);
        if (wrap && !nullText && length != 0 && (widest > 0x14 || (fixedWidth && widest > static_cast<uint32_t>(rect.right - rect.left)))) {
            Log("MeasureRect skipped");
            break;
        }
        f->MeasureRect(nullText ? nullptr : text, length, &rect, wrap, fixedWidth);
        Log("MeasureRect %u wrap %u fixed %u null %u -> rect %d %d %d %d", length, wrap, fixedWidth, nullText, rect.left, rect.top,
            rect.right, rect.bottom);
        break;
    }
    case 14: {
        // The line breaker itself (slot 41, 0x90b40e), in every mode (0 and 3 answer 0 when nothing fits), with limits on the edges of
        // its comparisons, with and without the width out-parameter. Now and then the text is only stray bytes (it converts to nothing)
        // or has a NUL in it (the mode 1 scan stops there).
        Text altered;
        const void *subject = text;
        uint32_t subjectLength = length;
        const uint32_t unit = o.mEncoding == 16 ? 2 : 1;
        unsigned spaceRun = 0;
        switch (rng() % 8) {
        case 0:
            if (o.mEncoding == 16) altered.bytes = {0x00, 0xdc};
            else altered.bytes = {0xff, 0x80, 0xc3};
            altered.length = o.mEncoding == 16 ? 1 : 3;
            altered.bytes.resize(altered.bytes.size() + 16, 0);
            subject = altered.bytes.data();
            subjectLength = altered.length;
            break;
        case 1:
            if (length > 0) {
                // Often right after the first unit: the mode 1 scan meets the NUL when the first cluster did not fit.
                const uint32_t at = (rng() % 3 == 0 ? 1 : rng() % length) * unit;
                altered.bytes.assign(t.bytes.begin(), t.bytes.begin() + at);
                altered.bytes.insert(altered.bytes.end(), unit, 0);
                altered.bytes.insert(altered.bytes.end(), t.bytes.begin() + at, t.bytes.begin() + length * unit);
                altered.length = length + 1;
                altered.bytes.resize(altered.bytes.size() + 16, 0);
                subject = altered.bytes.data();
                subjectLength = altered.length;
            }
            break;
        case 2: {
            // Three or more leading spaces, a stop, more text (lane OH, for the fix-up that takes one space more after the cut: a cut
            // that ends on a space with a space after it, `!IsSpace(units[consumed - 1])` in the breaker, reaches it only here): the
            // spaces before the stop count nothing, the cluster they leave with the stop does not fit, and the cut falls inside them.
            spaceRun = 3 + rng() % 3;
            const std::string s = std::string(spaceRun, ' ') + ".ab";
            for (char c : s) {
                altered.bytes.push_back(static_cast<uint8_t>(c));
                if (o.mEncoding == 16) altered.bytes.push_back(0);
            }
            altered.length = static_cast<uint32_t>(s.size());
            altered.bytes.resize(altered.bytes.size() + 16, 0);
            subject = altered.bytes.data();
            subjectLength = altered.length;
            break;
        }
        default: break;
        }
        const uint32_t whole = f->GetWidth(subject, subjectLength);
        const uint32_t widest = WidestChar(f, o, subject == text ? t : altered, subjectLength);
        uint32_t maxWidth;
        switch (spaceRun != 0 ? 7 + rng() % 2 : rng() % 7) {
        case 7: maxWidth = 1 + rng() % ((std::min)(f->GetWidth(subject, 3), 1000u) + 3); break; // a negative spacing makes a width wrap
        case 8: maxWidth = f->GetWidth(subject, 2) + rng() % 3; break;
        case 0: maxWidth = 0; break;
        case 1: maxWidth = 1 + rng() % 6; break;
        case 2: maxWidth = widest; break;
        case 3: maxWidth = widest > 1 ? widest - 1 : 1; break;
        case 4: maxWidth = widest + rng() % 60; break;
        case 5: {
            const uint32_t w = whole + rng() % 3;
            maxWidth = w > 1 ? w - 1 : 1;
            break;
        }
        default: maxWidth = 1 + rng() % 150; break;
        }
        const uint32_t mode = rng() % 4;
        uint32_t outWidth = 0xcdcdcdcd;
        const bool wantWidth = rng() % 4 != 0;
        // Mode 2 takes units until the measured width reaches the limit (a do/while on GetWidth(converted units, consumed) < maxWidth in
        // the original); a text that measures nothing never reaches it, so the original does not return. Leave that input out.
        if (mode == 2 && (whole == 0 || widest == 0)) {
            Log("BreakLine skipped (nothing to measure)");
            break;
        }
        const uint32_t r = f->BreakLine(subject, subjectLength, maxWidth, wantWidth ? &outWidth : nullptr, mode);
        Log("BreakLine %u limit %u mode %u -> %u width %x", subjectLength, maxWidth, mode, r, outWidth);
        break;
    }
    case 15: {
        // The widths themselves (slots 49 and 50): the text, then the text with spaces on its end, then only spaces.
        const unsigned variant = rng() % 3;
        const Text spaced = variant == 0 ? Text() : WithSpaces(t, length, o.mEncoding, rng, variant == 2);
        const void *const subject = variant == 0 ? text : spaced.bytes.data();
        const uint32_t subjectLength = variant == 0 ? length : spaced.length;
        uint32_t used = 0xcdcdcdcd;
        const uint32_t trimmed = f->GetTrimmedWidth(subject, subjectLength, &used);
        Log("GetTrimmedWidth %u variant %u -> %u used %x; GetWidth %u", subjectLength, variant, trimmed, used,
            f->GetWidth(subject, subjectLength));
        break;
    }
    // Lane OH: the glyph slots 27 and 28, and the string slots 51 (clip to a width) and 60 (wrap into a box).
    case 16: Log("HasGlyphs %u -> %u", length, f->HasGlyphs(text, length)); break;
    case 17: {
        const bool r = f->CacheGlyphs(text, length);
        Log("CacheGlyphs %u -> %u", length, r);
        break;
    }
    case 18:
    case 19: {
        if (o.mEncoding == 16) {
            Log("string op skipped: UTF-16 text is not a string of bytes");
            break;
        }
        static const char *const kSuffixes[] = {"", ".", "...", "..", " ~", "\xe2\x80\xa6"};
        const char *const suffixText = kSuffixes[rng() % (sizeof(kSuffixes) / sizeof(kSuffixes[0]))];
        const uint32_t suffixLength = static_cast<uint32_t>(std::strlen(suffixText));
        FontSysString suffix;
        suffix.s.FromChar(suffixText, suffixLength);
        // Mode 2 of the line breaker (slot 60's) never returns for text that measures nothing (see op 14).
        const uint32_t whole = f->GetWidth(text, length), widest = WidestChar(f, o, t, length);
        const bool wrap = textOp == 19 || rng() % 5 == 0;
        if (wrap && (whole == 0 || widest == 0 || HasThaiText(o, t, length))) {
            Log("string op skipped (nothing to measure)");
            break;
        }
        FontSysString string;
        string.s.FromChar(reinterpret_cast<const char *>(text), length);
        const uint32_t suffixWidth = f->GetWidth(suffixText, suffixLength);
        int32_t width;
        switch (rng() % 9) {
        case 0: width = static_cast<int32_t>(whole); break;
        case 1: width = static_cast<int32_t>(whole) - 1; break;
        case 2: width = static_cast<int32_t>(whole + rng() % 3); break;
        case 3: width = static_cast<int32_t>(suffixWidth); break;
        case 4: width = static_cast<int32_t>(suffixWidth) + static_cast<int32_t>(rng() % 3) - 1; break;
        case 5: width = static_cast<int32_t>(rng() % 150); break;
        case 6: width = -static_cast<int32_t>(rng() % 30); break;
        case 7: width = static_cast<int32_t>(widest + 8 + rng() % 40); break;
        default: width = static_cast<int32_t>(whole / 2 + 8 + rng() % 8); break;
        }
        if (textOp == 18 && !wrap && rng() % 3 == 0) {
            // A cut right after blanks: ClipText drops the spaces the cut leaves at the end of the text (and only spaces: a tab stays).
            static const char *const kSeps[] = {"\t", " ", "  ", " \t", "\t ", "   "};
            const std::string head(1 + rng() % 3, 'm');
            const std::string cut = head + kSeps[rng() % 6];
            const std::string body = cut + "nnnnn";
            string.s.FromChar(body.c_str(), static_cast<uint32_t>(body.size()));
            width = static_cast<int32_t>(f->GetWidth(cut.c_str(), static_cast<uint32_t>(cut.size())) + suffixWidth + rng() % 3) -
                    static_cast<int32_t>(rng() % 2);
        }
        const int32_t spacing = f->GetLineSpacing();
        const int32_t left = static_cast<int32_t>(rng() % 10) - 3;
        // A rect for the string slot: `width` wide and, for a wrap, a few lines high (and now and then none, or less than one).
        cGZRect rect = {left, 5, left + width, 5 + (rng() % 5 == 0 ? static_cast<int32_t>(rng() % 3) - 1 : spacing * static_cast<int32_t>(1 + rng() % 4) + static_cast<int32_t>(rng() % 3) - 1)};
        if (textOp == 19 && rng() % 2 == 0) rect.right = left + 8 + static_cast<int32_t>(widest + rng() % 80);
        const uint32_t r = textOp == 18 ? f->ClipText(string.s.AsInterface(), suffix.s.AsInterface(), &rect, wrap)
                                        : f->WrapText(string.s.AsInterface(), suffix.s.AsInterface(), &rect);
        Log("%s %u suffix '%s' rect %d %d %d %d -> %u string %08x len %u", textOp == 18 ? "ClipText" : "WrapText", length, suffixText,
            rect.left, rect.top, rect.right, rect.bottom, r, Hash(string.s.Data(), string.s.Strlen()), string.s.Strlen());
        break;
    }
    // Lane OH: the draw slots 52 (DrawText) and 53 (DrawBox) through the fake renderer, graphics system and command line.
    case 20:
    case 21: {
        const bool box = textOp == 21;
        const uint32_t whole = f->GetWidth(text, length), widest = WidestChar(f, o, t, length);
        if (box && (whole == 0 || widest == 0)) {
            Log("draw skipped (nothing to measure)");
            break;
        }
        if (!o.mRenderer) {
            cIGZFontRenderer *const r = rng() % 2 ? &gHeld[0] : &gHeld[1];
            f->SetRenderer(r);
            Log("draw: SetRenderer %s", RendererName(r).c_str());
        }
        o.mUnknown68 = rng() % 4;
        gBlackFont = rng() % 6 == 0;
        if (rng() % 3 == 0) f->SetQuality(2);
        if (rng() % 8 == 0) o.mDrawDepth = 1; // a draw inside another: the renderer is not begun
        // Colours: the font's own (0x8000000), black, white, a native colour, or any word; a second colour likewise.
        auto pickColour = [&](bool second) -> uint32_t {
            switch (rng() % 7) {
            case 0:
            case 1: return 0x8000000;
            case 2: return 0;
            case 3: return second ? 0xffff : 0xffffffffu;
            case 4: return rng() & 0xffff;
            case 5: return rng();
            default: return o.mColour;
            }
        };
        uint32_t colour = pickColour(false), colour2 = pickColour(true);
        // Now and then a colour pair some block the font keeps already holds (a hit moves the block to the front).
        // Colour ramps the font already keeps (as the "kept block" step makes them), so that the hits, the least recent block
        // dropped when four are kept, and the table the font uses already are all reached.
        if (rng() % 3 == 0)
            for (unsigned n = 1 + rng() % 4; n > 0; --n) {
                uint8_t *const block = static_cast<uint8_t *>(std::malloc(0x200));
                for (unsigned i = 0; i < 0x200; ++i) block[i] = static_cast<uint8_t>(rng());
                o.mBlocks.push_back(block);
            }
        if (!o.mBlocks.empty() && rng() % 3 == 0) {
            auto at = o.mBlocks.begin();
            std::advance(at, rng() % o.mBlocks.size());
            const uint32_t *const kept = reinterpret_cast<const uint32_t *>(*at);
            colour = kept[127];
            colour2 = kept[0] ^ (rng() % 3 == 0 ? 1u << (rng() % 16) : 0u); // now and then the first colour only
            if (rng() % 2) o.mColourTable = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*at));
        }
        cIGZBuffer *const buffer = rng() % 6 == 0 ? nullptr : reinterpret_cast<cIGZBuffer *>(gBuffer);
        // Where the text starts (the pen of a text, the rect's corner of a box), and a clip either anywhere or tight around it: its edges
        // then fall on the glyphs' edges often, which the comparisons of the clip test need.
        const int32_t baseX = static_cast<int32_t>(rng() % 10), baseY = static_cast<int32_t>(rng() % 10);
        cGZRect clipRect = {static_cast<int32_t>(rng() % 30), static_cast<int32_t>(rng() % 20), 0, 0};
        if (rng() % 2) {
            clipRect.left = baseX + static_cast<int32_t>(rng() % 16) - 4;
            clipRect.top = baseY + static_cast<int32_t>(rng() % 16) - 4;
            clipRect.right = clipRect.left + static_cast<int32_t>(rng() % 24);
            clipRect.bottom = clipRect.top + static_cast<int32_t>(rng() % 24);
        } else {
            clipRect.right = clipRect.left + static_cast<int32_t>(rng() % 120);
            clipRect.bottom = clipRect.top + static_cast<int32_t>(rng() % 60);
        }
        const cGZRect *const clip = rng() % 5 == 0 ? nullptr : &clipRect;
        int32_t pen[2] = {baseX + static_cast<int32_t>(rng() % 8), baseY + static_cast<int32_t>(rng() % 8)};
        int32_t *pos = rng() % 2 ? pen : nullptr;
        bool r;
        if (!box) {
            const int32_t x = baseX, y = baseY;
            r = f->DrawText(buffer, x, y, text, length, colour, colour2, clip, pos);
            Log("DrawText %u at %d %d colours %x %x buffer %u clip %u -> %u pos %d %d depth %d", length, x, y, colour, colour2, buffer != nullptr,
                clip != nullptr, r, pos ? pen[0] : -1, pos ? pen[1] : -1, o.mDrawDepth);
        } else {
            const int32_t spacing = f->GetLineSpacing();
            const int32_t left = baseX;
            int32_t width;
            switch (rng() % 8) {
            case 0: width = static_cast<int32_t>(whole); break;
            case 1: width = static_cast<int32_t>(whole) / 2 + 1; break;
            case 2: width = static_cast<int32_t>(widest) + static_cast<int32_t>(rng() % 30); break;
            case 3: width = static_cast<int32_t>(rng() % 100); break;
            case 4: width = -static_cast<int32_t>(rng() % 5); break;
            case 5: width = 0; break;
            default: width = static_cast<int32_t>(widest + 8 + rng() % 60); break;
            }
            const int32_t top = baseY;
            const cGZRect rect = {left, top, left + width,
                                  top + (rng() % 6 == 0 ? static_cast<int32_t>(rng() % 3) - 1
                                                        : spacing * static_cast<int32_t>(1 + rng() % 4) + static_cast<int32_t>(rng() % 3) - 1)};
            const uint32_t align = rng() % 4;
            if (width > 0 && !BreaksInto(f, o, t, length, static_cast<uint32_t>(width))) {
                Log("DrawBox skipped: the line breaker returns 0 for this width");
                o.mDrawDepth = 0;
                break;
            }
            if (pos && rng() % 2) pen[0] = left; // the pen at the rect's left, or not
            // With no line drawn (a rect with no width or height) the original writes two stack words it never set to *pos
            // (ORIGINAL-BUG in cGZFont::DrawBox): no pen is passed for such a rect.
            if (pos && (width <= 0 || rect.bottom <= rect.top)) pos = nullptr;
            ++gBoxes;
            gBoxesNoWidth += width == 0;
            r = f->DrawBox(buffer, &rect, text, length, align, colour, colour2, clip, pos);
            Log("DrawBox %u rect %d %d %d %d align %u colours %x %x buffer %u clip %u -> %u pos %d %d depth %d", length, rect.left, rect.top,
                rect.right, rect.bottom, align, colour, colour2, buffer != nullptr, clip != nullptr, r, pos ? pen[0] : -1, pos ? pen[1] : -1,
                o.mDrawDepth);
        }
        o.mDrawDepth = 0;
        break;
    }
    default: {
        cGZRect rect = {5, 6, 7, 8};
        f->GetExtent(text, length, &rect);
        Log("GetExtent %u -> rect %d %d %d %d", length, rect.left, rect.top, rect.right, rect.bottom);
        break;
    }
    }
}

void Step(cIGZFont *f, std::mt19937 &rng)
{
    cGZFont &o = View(f);
    unsigned op = rng() % 54;
    if (op >= 46) op = 45; // the text slots
    else if (op >= 44) op = 44;
    else if (op >= 40) op = 15; // a path (and a load) more often than the other steps
    else if (op >= 34) op = 26; // and glyphs made through FindGlyph
    switch (op) {
    case 0: {
        static const uint32_t kIds[] = {cIGZFont::kIid, 1, 0xa8bc2a75, 0};
        const uint32_t id = kIds[rng() % 4];
        void *out = gBuffer;
        const bool r = f->QueryInterface(id, &out);
        Log("QueryInterface %x -> %u %s", id, r, Token(out).c_str());
        break;
    }
    case 1: Log("AddRef -> %u", f->AddRef()); break;
    case 2:
        if (o.mRefCount > 1) Log("Release -> %u", f->Release());
        break;
    case 3:
    case 4: Log("Load -> %u", f->Load()); break;
    case 5: Log("Unload -> %u", f->Unload()); break;
    case 6: {
        FontSysString out("junk");
        f->GetFamily(out.s.AsInterface());
        Log("GetFamily '%s'", Str(out));
        break;
    }
    case 7: {
        static const uint32_t kFlags[] = {0, 1, 2, 3, 4, 0x3c, 0x40, 0xffffffffu};
        const uint32_t flags = rng() % 3 ? kFlags[rng() % 8] : static_cast<uint32_t>(rng());
        Log("HasStyle %x -> %u, GetStyle %x", flags, f->HasStyle(flags), f->GetStyle());
        break;
    }
    case 8: {
        const uint32_t flags = static_cast<uint32_t>(rng());
        const bool set = rng() % 2 != 0;
        f->SetStyle(flags, set);
        Log("SetStyle %x %u", flags, set);
        break;
    }
    case 9: Log("GetGroup %u GetSize %u", f->GetGroup(), f->GetSize()); break;
    case 10: {
        static const uint32_t kSizes[] = {0, 1, 8, 12, 16, 0x1c, 0x7ff, 0x800, 0x801, 0xffffffffu};
        // The sizes either side of the line-thickness thresholds (0x10, 0x1a, 0x24 and 0x18, 0x30, 0x48: slots 64's lines).
        static const uint32_t kEdges[] = {15, 16, 17, 23, 24, 25, 26, 35, 36, 37, 46, 47, 48, 49, 71, 72, 73};
        const uint32_t size = rng() % 3 == 0 ? kEdges[rng() % 17]
                              : rng() % 4  ? kSizes[rng() % 10]
                                           : static_cast<uint32_t>(rng() % 200);
        Log("SetSize %u -> %u", size, f->SetSize(size));
        break;
    }
    case 11: {
        const uint32_t a = static_cast<uint32_t>(rng()), b = static_cast<uint32_t>(rng());
        f->SetColours(a, b);
        Log("SetColours %x %x; GetColour %x GetShadowColour %x", a, b, f->GetColour(), f->GetShadowColour());
        break;
    }
    case 12: {
        const uint32_t q = rng() % 6;
        f->SetQuality(q);
        Log("SetQuality %u; GetQuality %u", q, f->GetQuality());
        break;
    }
    case 13: {
        static const float kGammas[] = {-1.0f, 0.0f, 0.25f, 0.5f, 1.0f, 1.0f, 1.8f, 2.2f, 10.0f, 11.0f, kNaN};
        const float g = PickFloat(rng, kGammas, 11, o.mGamma);
        f->SetGamma(g);
        Log("SetGamma %08x; GetGamma %08x", Bits(g), Bits(f->GetGamma()));
        break;
    }
    case 14: {
        static const float kContrasts[] = {-2.0f, -1.0f, -0.5f, -0.1f, 0.0f, 0.0f, 0.3f, 0.75f, 1.0f, 2.0f, kNaN};
        const float c = PickFloat(rng, kContrasts, 11, o.mContrast);
        f->SetContrast(c);
        Log("SetContrast %08x; GetContrast %08x", Bits(c), Bits(f->GetContrast()));
        break;
    }
    case 15: {
        const FontSysString path(PathFor(rng).c_str());
        f->SetPath(path.s.AsInterface());
        FontSysString out("junk");
        f->GetPath(out.s.AsInterface());
        Log("SetPath '%s'; GetPath '%s'", Str(path), Str(out));
        if (rng() % 2) Log("then Load -> %u", f->Load()); // a path, then a load: most loads succeed this way
        break;
    }
    case 16: {
        static const int32_t kEncodings[] = {-1, -2, 0, 8, 16, 1252, -3, 932, 950, 936, 949, 1251, 1250};
        const int32_t e = kEncodings[rng() % 13];
        uint32_t out = 0xcdcdcdcd;
        const bool set = f->SetEncoding(e);
        const bool got = f->GetEncoding(&out);
        Log("SetEncoding %d -> %u; GetEncoding -> %u %u", e, set, got, out);
        break;
    }
    case 17: {
        const uint32_t g = rng() % 4;
        Log("IsUnicodeGroup %u -> %u", g, f->IsUnicodeGroup(g));
        break;
    }
    case 18: {
        static const float kScales[] = {0.001f, 0.01f, 0.5f, 1.0f, 1.0f, 2.5f, 999.0f, 1000.0f, 5000.0f, -1.0f, kNaN};
        const float x = PickFloat(rng, kScales, 11, o.mScaleX), y = rng() % 3 ? PickFloat(rng, kScales, 11, o.mScaleY) : x;
        f->SetScale(x, y);
        float gx = -7, gy = -7;
        f->GetScale(&gx, &gy);
        Log("SetScale %08x %08x; GetScale %08x %08x", Bits(x), Bits(y), Bits(gx), Bits(gy));
        break;
    }
    case 19: {
        static const float kAdvances[] = {0.01f, 0.1f, 0.5f, 1.0f, 1.0f, 3.0f, 10.0f, 11.0f, -1.0f, kNaN, 0.99f, 0.985f, 0.99f, 1.0f};
        const float x = PickFloat(rng, kAdvances, 14, o.mAdvanceScale), y = PickFloat(rng, kAdvances, 14, 2.0f);
        f->SetAdvanceScale(x, y);
        float gx = -7, gy = -7;
        f->GetAdvanceScale(&gx, &gy);
        Log("SetAdvanceScale %08x %08x; GetAdvanceScale %08x %08x", Bits(x), Bits(y), Bits(gx), Bits(gy));
        break;
    }
    case 20: {
        const int32_t x = static_cast<int32_t>(rng() % 21) - 10, y = static_cast<int32_t>(rng() % 21) - 10;
        f->SetSpacing(x, y);
        int32_t gx = -77, gy = -77;
        f->GetSpacing(&gx, &gy);
        Log("SetSpacing %d %d; GetSpacing %d %d", x, y, gx, gy);
        break;
    }
    case 21: {
        const uint32_t a = static_cast<uint32_t>(rng()), b = static_cast<uint32_t>(rng());
        Log("GetLineSpacing %d GetLineHeight %u GetAscent %d GetDescent %d", f->GetLineSpacing(), f->GetLineHeight(a, b), f->GetAscent(),
            f->GetDescent());
        break;
    }
    case 22: {
        cIGZFontRenderer *const choices[] = {nullptr, &gHeld[0], &gHeld[1], o.mRenderer};
        cIGZFontRenderer *const r = choices[rng() % 4];
        f->SetRenderer(r);
        Log("SetRenderer %s; GetRenderer %s", RendererName(r).c_str(), RendererName(f->GetRenderer()).c_str());
        break;
    }
    case 23:
        if (o.mDrawDepth != 0 || o.mRenderer) Log("BeginDraw -> %u", f->BeginDraw(reinterpret_cast<cIGZBuffer *>(gBuffer)));
        break;
    case 24:
        if (o.mDrawDepth != 1 || o.mRenderer) Log("EndDraw -> %u", f->EndDraw(reinterpret_cast<cIGZBuffer *>(gBuffer)));
        break;
    case 25: Log("GetGlyphCache +%x", P(f->GetGlyphCache()) - P(f)); break;
    case 26:
    case 27: {
        // Characters of every kind the glyph maker treats differently: the space, ASCII, a control character, the Latin-1
        // and Adobe-name ones, a name the table lacks, the zero-width ones, and a code past 16 bits.
        static const uint32_t kCodes[] = {0x20,   0x41,   0x42,  0x10041, 0,      0x1f,   0x7f,  0x80,   0xe9,   0xc4,  0x2019,
                                          0x20ac, 0x200b, 0xfeff, 0x1234, 0x2022, 0x131, 0xa0,  0x3a9,  0x5000, 0x61,  0x62};
        const uint32_t code = kCodes[rng() % (sizeof(kCodes) / sizeof(kCodes[0]))];
        if (!GlyphsAreCheap(o)) {
            Log("FindGlyph %x skipped", code);
            break;
        }
        const cGZFontCachedGlyph *g = f->FindGlyph(code);
        Log("FindGlyph %x -> %s", code, g ? GlyphHex(*g).c_str() : "null");
        break;
    }
    case 28: { // a glyph into a cache, as slot 63 would (one STL on both sides: the layouts are the same)
        static const uint32_t kCodes[] = {0x20, 0x41, 0x42, 0x10041, 0};
        cGZFontGlyphCache &c = rng() % 2 ? o.mCacheMono : o.mCacheSmooth;
        const uint32_t code = kCodes[rng() % 5];
        cGZFontCachedGlyph g;
        for (unsigned i = 0; i < sizeof(g); ++i) reinterpret_cast<uint8_t *>(&g)[i] = static_cast<uint8_t>(rng());
        c.glyphs[code] = g;
        Log("put glyph %x into %s", code, &c == &o.mCacheMono ? "mono" : "smooth");
        break;
    }
    case 29: { // a bitmap for a cache, as slot 65 would make it: a block with two blank rows above the pixels
        cGZFontGlyphCache &c = rng() % 2 ? o.mCacheMono : o.mCacheSmooth;
        std::free(c.memory);
        c.width = 8 + rng() % 8;
        c.height = (std::min)(o.mLineHeight, 80u) + rng() % 3; // tall enough for the lines placing draws
        c.pitch = c.width + rng() % 3;
        const size_t size = static_cast<size_t>(c.height + 4) * c.pitch;
        c.memory = static_cast<uint8_t *>(std::malloc(size));
        for (size_t i = 0; i < size; ++i) c.memory[i] = static_cast<uint8_t>(rng());
        c.pixels = c.memory + 2 * c.pitch;
        c.cursor = rng() % 16;
        Log("bitmap for %s", &c == &o.mCacheMono ? "mono" : "smooth");
        break;
    }
    case 31: { // slot 63 on either cache, whichever the quality picks
        static const uint32_t kCodes[] = {0x20, 0x41, 0xe9, 0x2019, 0x200b, 0x1234, 0x5000, 0xfeff, 0x61};
        const uint32_t code = kCodes[rng() % 9];
        cGZFontGlyphCache *const c = rng() % 2 ? &o.mCacheMono : &o.mCacheSmooth;
        if (!GlyphsAreCheap(o)) {
            Log("MakeGlyph %x skipped", code);
            break;
        }
        const cGZFontCachedGlyph *g = f->MakeGlyph(code, c);
        Log("MakeGlyph %x %s -> %s", code, c == &o.mCacheMono ? "mono" : "smooth", g ? GlyphHex(*g).c_str() : "null");
        break;
    }
    case 32: { // slot 64 with a seeded bitmap and glyph
        if (!GlyphsAreCheap(o)) {
            Log("PlaceBitmap skipped");
            break;
        }
        cGZFontGlyphCache *const c = rng() % 2 ? &o.mCacheMono : &o.mCacheSmooth;
        if (c->height < o.mLineHeight && c->pixels) {
            Log("PlaceBitmap skipped: short cache");
            break;
        }
        uint8_t source[32 * 24];
        for (uint8_t &b : source) b = static_cast<uint8_t>(rng());
        const uint32_t mode = rng() % 2 ? 2 : 0x100;
        const uint32_t width = rng() % 3 ? 1 + rng() % 24 : 0, height = rng() % 24;
        const uint32_t pitch = (mode == 2 ? (width + 7) / 8 : width) + rng() % 3;
        const uint32_t x = rng() % 3 ? c->cursor : rng() % 40, y = rng() % 3 == 0 ? rng() % 12 : 0;
        cGZFontCachedGlyph g = {};
        g.left = static_cast<int32_t>(x);
        g.top = static_cast<int32_t>(y);
        g.right = static_cast<int32_t>(x + width);
        g.bottom = static_cast<int32_t>(y + height);
        g.bearingX = static_cast<int16_t>(static_cast<int>(rng() % 9) - 4);
        g.bearingY = static_cast<int16_t>(static_cast<int>(rng() % 17) - 8);
        g.advance = static_cast<int16_t>(static_cast<int>(rng() % 41) - 8);
        const bool placed = f->PlaceBitmap(c, &g, x, y, source, width, height, pitch, mode);
        Log("PlaceBitmap %s x=%u y=%u w=%u h=%u pitch=%u mode=%x -> %u glyph %s", c == &o.mCacheMono ? "mono" : "smooth", x, y, width,
            height, pitch, mode, placed, GlyphHex(g).c_str());
        break;
    }
    case 33: { // slot 65
        cGZFontGlyphCache *const c = rng() % 2 ? &o.mCacheMono : &o.mCacheSmooth;
        // Wider than the bitmap or taller (never narrower while it holds rows: the original would read past a row).
        const uint32_t width = c->width + rng() % 3 * (rng() % 20), height = rng() % 4 ? c->height + rng() % 30 : rng() % 40;
        // A first bitmap shorter than the line height is a state the font never makes (its own growth starts at the line height):
        // the original's glyph placer then writes past the block on a later FindGlyph (heap corruption seen in the original at
        // seed 137), so it is left out.
        if ((c->pixels && height < c->height) || (!c->pixels && height < o.mLineHeight)) {
            Log("GrowCache skipped");
            break;
        }
        const bool grown = f->GrowCache(c, width, height);
        Log("GrowCache %s %u %u -> %u", c == &o.mCacheMono ? "mono" : "smooth", width, height, grown);
        break;
    }
    case 44: { // the lines of slot 64 at a size beside a thickness threshold: empty caches, a size, the line styles, a bitmap
        static const uint32_t kEdges[] = {15, 16, 23, 24, 25, 26, 35, 36, 46, 47, 48, 71, 72};
        for (cGZFontGlyphCache *c : {&o.mCacheMono, &o.mCacheSmooth}) {
            c->Clear();
            c->glyphs.clear();
        }
        const uint32_t size = kEdges[rng() % 13];
        const bool sized = f->SetSize(size);
        f->SetStyle(0xc, false);
        f->SetStyle(rng() % 3 == 0 ? 4 : rng() % 2 ? 8 : 0xc, true);
        Log("LineSetup size %u -> %u style %x", size, sized, o.mStyle);
        if (!GlyphsAreCheap(o)) break;
        cGZFontGlyphCache *const c = rng() % 2 ? &o.mCacheMono : &o.mCacheSmooth;
        f->GrowCache(c, 48, o.mLineHeight + 8);
        uint8_t source[32 * 24];
        for (uint8_t &b : source) b = static_cast<uint8_t>(rng());
        const uint32_t width = 1 + rng() % 24, height = 1 + rng() % 12, x = c->cursor % 8;
        cGZFontCachedGlyph g = {};
        g.left = static_cast<int32_t>(x);
        g.right = static_cast<int32_t>(x + width);
        g.bottom = static_cast<int32_t>(height);
        g.bearingX = static_cast<int16_t>(static_cast<int>(rng() % 5) - 2);
        g.bearingY = static_cast<int16_t>(static_cast<int>(rng() % 17) - 4);
        g.advance = static_cast<int16_t>(rng() % 30);
        f->PlaceBitmap(c, &g, x, 0, source, width, height, width, 0x100);
        Log("LinePlace %s x=%u w=%u h=%u glyph %s", c == &o.mCacheMono ? "mono" : "smooth", x, width, height, GlyphHex(g).c_str());
        break;
    }
    case 45: TextStep(f, o, rng); break;
    default: { // a block the drawing code keeps, and a glyph run count
        // A colour ramp of the draw's size (0x200 bytes), with random words; its ends often what the next draw asks for.
        uint8_t *const block = static_cast<uint8_t *>(std::malloc(0x200));
        for (unsigned i = 0; i < 0x200; ++i) block[i] = static_cast<uint8_t>(rng());
        o.mBlocks.push_back(block);
        o.mColourTable = rng() % 3 == 0 ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(block)) : 0;
        Log("kept block");
        break;
    }
    }
}

// The side's font: the original (game vtable, game code) or ours.
cIGZFont *Build(difftest::Context &ctx, bool ours)
{
    void *object;
    if (ours) {
        object = gCase.viaCreator ? static_cast<cIGZFont *>(cGZFont::Create()) : static_cast<cIGZFont *>(new cGZFont);
    } else if (gCase.viaCreator) {
        object = ctx.Original<void *(__cdecl *)()>(0x909fb9)();
    } else {
        object = ctx.Original<void *(__thiscall *)(void *)>(0x90a7db)(::operator new(cGZFont::kSize));
    }
    return static_cast<cIGZFont *>(object);
}

// A case whose first file loads (no faults, the file there and openable, sane metrics) for the text slots to work on.
void NewTextCase(uint32_t seed)
{
    NewCase(seed);
    std::mt19937 rng(seed * 31u + 5u);
    for (Fault &f : gCase.fault) f = Fault{};
    gCase.files[0].exists = true;
    gCase.files[0].openable = true;
    gCase.files[0].bytes.clear();
    for (unsigned b = 0; b < 30; ++b) gCase.files[0].bytes.push_back(static_cast<uint8_t>(0x20 + rng() % 0x60)); // no header
    gCase.files[0].bytes[0] = static_cast<uint8_t>((rng() % 2 ? 1 : 2) + 8 * (rng() % 20)); // the fake engine's type 1 or 2
    for (NameSpec &n : gCase.names) n.from = static_cast<int>(rng() % 2); // the family is found
    gCase.fixedMetrics = false;
    gCase.ascender = static_cast<int32_t>(0x100000 + rng() % 0x80000);
    gCase.descent = -static_cast<int32_t>(0x30000 + rng() % 0x40000);
    gCase.lineGap = static_cast<int32_t>(rng() % 0x20000);
    gCase.missing = rng() % 5 == 0 ? 3 + rng() % 3 : rng() % 2;
    gCase.narrow = rng() % 5 != 0;
}

// The line breaker's character classes build their sets on first use: that is done before any side's heap count starts, so
// the sets (which live on) are not part of what the original allocates.
void WarmStatics(difftest::Context &ctx)
{
    for (uint32_t rva : {0x1b2293u, 0x1b23d5u, 0x1b0959u, 0x1b0a60u, 0x1b258fu, 0x1b1d25u})
        ctx.Original<uint32_t(__cdecl *)(uint32_t)>(rva)(0xe31);
}

// text: the text slots' run: the case's first file is loaded first, and three steps in four are text steps.
std::vector<std::string> RunSide(difftest::Context &ctx, bool ours, uint32_t seed, bool text = false)
{
    WarmStatics(ctx);
    NewSide();
    gHeld[0].count = 5;
    gHeld[1].count = 5;
    difftest::AllocationCounter heap;
    gHeap = &heap;
    cIGZFont *f = Build(ctx, ours);
    gSide.font = f;
    Log("built");
    Dump(f);
    std::mt19937 rng(seed * 7919u + 13u);
    if (text) {
        const FontSysString path(gCase.files[0].path.c_str());
        f->SetPath(path.s.AsInterface());
        Log("text setup: Load -> %u, SetSize -> %u", f->Load(), f->SetSize(8 + rng() % 24));
        Dump(f);
    }
    for (int step = 0; step < 40; ++step) {
        if (text && rng() % 4 != 0) TextStep(f, View(f), rng);
        else Step(f, rng);
        Dump(f);
    }
    uint32_t r;
    int guard = 0;
    do {
        r = f->Release();
        Log("final Release -> %u", r);
    } while (r != 0 && ++guard < 10000);
    gSide.font = nullptr;
    Log("held %u %u heap %lld blocks", gHeld[0].count, gHeld[1].count, heap.Blocks());
    for (const std::string &e : gErrors) Log("error: %s", e.c_str());
    gHeap = nullptr;
    return gLog;
}

bool Compare(difftest::Context &ctx, const char *what, uint32_t seed, const std::vector<std::string> &a,
             const std::vector<std::string> &b)
{
    const size_t n = (std::min)(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) {
            ctx.Fail("%s seed %u line %zu:\n  orig %s\n  ours %s\n  (before: %s)", what, seed, i, a[i].c_str(), b[i].c_str(),
                     i ? a[i - 1].c_str() : "");
            return false;
        }
    }
    if (a.size() != b.size()) {
        ctx.Fail("%s seed %u: %zu / %zu lines; next: %s", what, seed, a.size(), b.size(),
                 a.size() > n ? a[n].c_str() : b[n].c_str());
        return false;
    }
    return true;
}

// ---- the reader alone ----

std::vector<std::string> RunReader(difftest::Context &ctx, bool ours, uint32_t seed)
{
    NewSide();
    difftest::AllocationCounter heap;
    gHeap = &heap;
    alignas(8) uint8_t storage[cGZFontFile::kSize];
    std::memset(storage, 0xcd, sizeof(storage));
    cGZFontFile *reader = ours ? cGZFontFileAdapters::Construct(storage, nullptr)
                               : static_cast<cGZFontFile *>(ctx.Original<void *(__thiscall *)(void *)>(0x90a6c5)(storage));
    Log("built %d", P(reader) == P(storage));
    DumpFile(*reader);
    std::mt19937 rng(seed * 31u + 5u);
    for (int step = 0; step < 12; ++step) {
        switch (rng() % 5) {
        case 0:
        case 1: {
            const FontSysString path(PathFor(rng).c_str());
            const bool r = ours ? reader->Open(path.s.AsInterface())
                                : (ctx.Original<uint32_t(__thiscall *)(void *, const cIGZString *)>(0x90d6f7)(reader, path.s.AsInterface()) &
                                   0xff) != 0;
            Log("Open '%s' -> %u", Str(path), r);
            break;
        }
        case 2: {
            static const uint32_t kWhich[] = {0, 1, 2, 3, 4, 5, 0x10001, 0x20002};
            const uint32_t which = kWhich[rng() % 8];
            FontSysString out(rng() % 3 ? "" : "Previous Bold");
            const bool r = ours ? reader->Name(out.s.AsInterface(), which)
                                : (ctx.Original<uint32_t(__thiscall *)(void *, cIGZString *, uint32_t)>(0x90ccd5)(reader, out.s.AsInterface(),
                                                                                                                 which) &
                                   0xff) != 0;
            Log("Name %x -> %u '%s'", which, r, Str(out));
            break;
        }
        case 3: {
            const uint32_t r = ours ? reader->Style() : ctx.Original<uint32_t(__thiscall *)(void *)>(0x90d27e)(reader);
            Log("Style -> %x", r);
            break;
        }
        default: {
            const bool r = ours ? reader->Close() : (ctx.Original<uint32_t(__thiscall *)(void *)>(0x90ea52)(reader) & 0xff) != 0;
            Log("Close -> %u", r);
            break;
        }
        }
        DumpFile(*reader);
    }
    if (ours) cGZFontFileAdapters::Destruct(reader, nullptr);
    else ctx.Original<void(__thiscall *)(void *)>(0x90a934)(reader);
    Log("heap %lld blocks", heap.Blocks());
    for (const std::string &e : gErrors) Log("error: %s", e.c_str());
    gHeap = nullptr;
    return gLog;
}

// ---- the game's slots ----

template <class M> unsigned SlotOfMember(M member)
{
    uint32_t offset = ~0u;
    if (!shape::VcallThunkOffset(shape::MemberCodeWord(member), &offset)) return ~0u;
    return offset / 4;
}

struct Mapping {
    unsigned slot;
    uint32_t rva;
};

void *gSelf;
uint32_t gWords[9];
unsigned gCount, gHits;
template <unsigned... I> uint32_t __fastcall Record(void *self, void *, shape::StackWord<I>... w)
{
    gSelf = self;
    const uint32_t words[] = {0u, w...};
    for (unsigned i = 0; i < sizeof...(I); ++i) gWords[i] = words[i + 1];
    gCount = sizeof...(I);
    ++gHits;
    return 0xc0de005au + (sizeof...(I) << 8);
}
template <unsigned... I> void *RecordAddress(std::integer_sequence<unsigned, I...>) { return reinterpret_cast<void *>(&Record<I...>); }
template <unsigned N> void *RecordFor() { return RecordAddress(std::make_integer_sequence<unsigned, N>()); }

const void *P(uint32_t w) { return reinterpret_cast<const void *>(static_cast<uintptr_t>(w)); }
using Call = uint32_t (*)(cIGZFont *f, const uint32_t *a);
struct Forwarder {
    unsigned slot;
    uint32_t rva;
    unsigned words;
    void *record;
    Call call;
};

} // namespace gzfont

using namespace gzfont;

DIFF_TEST(GZFont_Slots, 0x90d8eb)
{
    World world;
    NameTable names(ctx);
    for (uint32_t seed = 1; seed <= 400; ++seed) {
        ++ctx.cases;
        NewCase(seed);
        char mark[64];
        std::snprintf(mark, sizeof(mark), "orig: seed %u", seed);
        ctx.Mark(mark);
        const std::vector<std::string> orig = RunSide(ctx, false, seed);
        std::snprintf(mark, sizeof(mark), "ours: seed %u", seed);
        ctx.Mark(mark);
        const std::vector<std::string> ours = RunSide(ctx, true, seed);
        if (!Compare(ctx, "slots", seed, orig, ours)) break;
        for (const std::string &line : ours)
            if (line.rfind("error:", 0) == 0) {
                ctx.Fail("seed %u: %s", seed, line.c_str());
                break;
            }
    }
}

// The text slots (lane OF): 25, 36, 40, 42 to 48 and 59 on a loaded font, over random text in every encoding the font takes.
DIFF_TEST(GZFont_Text, 0x90d1dd)
{
    World world;
    NameTable names(ctx);
    for (uint32_t seed = 1; seed <= 800; ++seed) {
        ++ctx.cases;
        NewTextCase(seed);
        char mark[64];
        std::snprintf(mark, sizeof(mark), "orig: seed %u", seed);
        ctx.Mark(mark);
        const std::vector<std::string> orig = RunSide(ctx, false, seed, true);
        std::snprintf(mark, sizeof(mark), "ours: seed %u", seed);
        ctx.Mark(mark);
        const std::vector<std::string> ours = RunSide(ctx, true, seed, true);
        if (!Compare(ctx, "text", seed, orig, ours)) break;
        for (const std::string &line : ours)
            if (line.rfind("error:", 0) == 0) {
                ctx.Fail("seed %u: %s", seed, line.c_str());
                break;
            }
    }
    // The draw ops must have drawn: glyph runs, in the ramp mode, with clips, a ramp made, the black-font switch answered yes.
    // The colour-ramp builder 0x90e536 is compared through them: each ramp run dumps the 4-entry block list and the 0x200-byte
    // table it selected, against the original's, and gRampBuilds counts the 15 conversion calls of every build.
    if (gRuns < 200 || gRampRuns < 20 || gClippedRuns < 50 || gRampBuilds < 15 * 10 || gBlackAnswers < 5 || gBoxes < 100 || gBoxesNoWidth < 5)
        ctx.Fail("the draw ops reached too little: %u runs, %u ramp runs, %u clipped, %u ramp colours, %u black answers, %u boxes, %u without width", gRuns, gRampRuns,
                 gClippedRuns, gRampBuilds, gBlackAnswers, gBoxes, gBoxesNoWidth);
}

DIFF_TEST(GZFontFile_Reader, 0x90d6f7)
{
    World world;
    for (uint32_t seed = 1; seed <= 400; ++seed) {
        ++ctx.cases;
        NewCase(seed);
        const std::vector<std::string> orig = RunReader(ctx, false, seed);
        const std::vector<std::string> ours = RunReader(ctx, true, seed);
        if (!Compare(ctx, "reader", seed, orig, ours)) break;
        for (const std::string &line : ours)
            if (line.rfind("error:", 0) == 0) {
                ctx.Fail("seed %u: %s", seed, line.c_str());
                break;
            }
    }
}

// 0x90afb0, the fill of the glyph-name map: every node the original makes against cGZFontFile::GlyphName, and every
// character ours names against the map's size. The first entry of a repeated character is the one kept on both sides.
DIFF_TEST(GZFont_GlyphNames, 0x90afb0)
{
    World world;
    NameTable names(ctx);
    struct NodeView {
        const uint8_t *bytes;
        bool Nil() const { return bytes[0xd] != 0; }
        NodeView Link(uint32_t offset) const
        {
            const uint8_t *next;
            std::memcpy(&next, bytes + offset, 4);
            return {next};
        }
        uint16_t Code() const { return *reinterpret_cast<const uint16_t *>(bytes + 0x10); }
        const char *Name() const { return *reinterpret_cast<const char *const *>(bytes + 0x14); }
    };
    const uint32_t *const map = GameAddress<uint32_t>(0x10ecd04);
    const NodeView head{reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(map[0]))};
    std::vector<NodeView> pending{head.Link(4)};
    uint32_t nodes = 0;
    while (!pending.empty()) {
        const NodeView node = pending.back();
        pending.pop_back();
        if (node.Nil()) continue;
        pending.push_back(node.Link(0));
        pending.push_back(node.Link(8));
        ++nodes;
        ++ctx.cases;
        const char *const ours = cGZFontFile::GlyphName(node.Code());
        if (!ours || std::strcmp(ours, node.Name()) != 0)
            ctx.Fail("character %04x: original \"%s\", ours \"%s\"", node.Code(), node.Name(), ours ? ours : "(none)");
    }
    uint32_t named = 0;
    for (uint32_t code = 0; code <= 0xffff; ++code)
        if (cGZFontFile::GlyphName(static_cast<uint16_t>(code))) ++named;
    ++ctx.cases;
    if (nodes != map[1] || named != map[1])
        ctx.Fail("the map holds %u (%u walked), ours names %u", map[1], nodes, named);
}

DIFF_TEST(GZFont_RendererMode, 0x90ae06)
{
    const auto original = ctx.Original<uint32_t(__cdecl *)(uint32_t)>(0x90ae06);
    std::mt19937 rng(7);
    for (uint32_t i = 0; i < 200; ++i) {
        ++ctx.cases;
        const uint32_t q = i < 16 ? i - 4 : static_cast<uint32_t>(rng());
        const uint32_t a = original(q), b = cGZFont::RendererMode(q);
        if (a != b) ctx.Fail("mode %x: %x / %x", q, a, b);
    }
}

// The two small text predicates the layout code asks: the space test (0x90da7d, a full-width compare of the argument) and the Thai
// sign class (0x90d4f5, a 16-bit argument), over every 16-bit value and the values just above it.
DIFF_TEST(GZFont_Predicates, 0x90da7d)
{
    const auto isSpace = ctx.Original<uint32_t(__cdecl *)(uint32_t)>(0x90da7d);
    const auto thaiSign = ctx.Original<uint32_t(__stdcall *)(uint32_t)>(0x90d4f5);
    for (uint32_t ch = 0; ch < 0x30000; ++ch) {
        ++ctx.cases;
        const bool a = (isSpace(ch) & 0xff) != 0, b = cGZFont::IsSpace(ch);
        if (a != b) ctx.Fail("IsSpace %x: %u / %u", ch, a, b);
        const uint32_t unit = ch & 0xffff;
        const uint32_t c = thaiSign(unit) & 0xff, d = cGZFont::ThaiSign(ch);
        if (c != d) ctx.Fail("ThaiSign %x: %u / %u", ch, c, d);
    }
    // Wide values (the high half set, as a sign-extended 16-bit character would be).
    for (uint32_t high = 1; high < 0x10; ++high) {
        for (uint32_t low : {0x20u, 0x2002u, 0x200bu, 0xe0du, 0xe31u, 0xe48u, 0xe4du}) {
            ++ctx.cases;
            const uint32_t ch = (high << 16) | low;
            const bool a = (isSpace(ch) & 0xff) != 0, b = cGZFont::IsSpace(ch);
            if (a != b) ctx.Fail("IsSpace %x: %u / %u", ch, a, b);
            const uint32_t c = thaiSign(ch & 0xffff) & 0xff, d = cGZFont::ThaiSign(ch);
            if (c != d) ctx.Fail("ThaiSign %x: %u / %u", ch, c, d);
        }
    }
}

// Layout (0x90d563) alone, on a font object of its own on each side: where the laid-out text is (the input itself, or in one of the six
// buffers), its units, the buffers' sizes, the layout words and the heap balance, over seeded text in every encoding, the six buffers, and
// clear flags with bits above the low byte (the original looks at the byte only).
namespace gzfont {
std::vector<std::string> RunLayout(difftest::Context &ctx, bool ours, uint32_t seed)
{
    using LayoutFn = void(__thiscall *)(void *, const void *, uint32_t, const uint16_t **, uint32_t *, uint32_t, uint32_t);
    static const uint32_t kEncodings[] = {0, 8, 8, 16, 16, 16, 1252, 932, 12345};
    static const uint32_t kFlags[] = {0, 1, 0x100, 0x101, 0xff, 0xcd00, 0x200, 2};
    const auto original = ctx.Original<LayoutFn>(0x90d563);
    std::vector<std::string> out;
    char buf[512];
    alignas(8) uint8_t storage[cGZFont::kSize] = {};
    cGZFont &o = *reinterpret_cast<cGZFont *>(storage);
    difftest::AllocationCounter heap;
    std::mt19937 rng(seed);
    for (int i = 0; i < 60; ++i) {
        o.mEncoding = kEncodings[rng() % (sizeof(kEncodings) / sizeof(kEncodings[0]))];
        const Text t = MakeText(rng, o.mEncoding, rng() % 2 == 0);
        uint32_t count = t.length;
        if (rng() % 6 == 0) count = rng() % (t.length + 1);
        const uint32_t index = rng() % 6;
        const uint32_t flag = kFlags[rng() % (sizeof(kFlags) / sizeof(kFlags[0]))];
        if (rng() % 3 == 0) o.mLayoutWords.assign(1 + rng() % 5, 7 + rng() % 3);
        const uint16_t *placed = reinterpret_cast<const uint16_t *>(0x1111);
        uint32_t placedCount = 0xcdcdcdcd;
        if (ours) o.Layout(t.bytes.data(), count, &placed, &placedCount, index, flag);
        else original(&o, t.bytes.data(), count, &placed, &placedCount, index, flag);
        std::string where = "elsewhere";
        if (static_cast<const void *>(placed) == t.bytes.data()) {
            where = "input";
        } else {
            for (unsigned b = 0; b < 6; ++b) {
                const uint8_t *const first = reinterpret_cast<const uint8_t *>(o.mLayout[b].data());
                const uint8_t *const at = reinterpret_cast<const uint8_t *>(placed);
                if (first && at >= first && at < first + o.mLayout[b].size() * 2) {
                    std::snprintf(buf, sizeof(buf), "buffer %u +%d", b, static_cast<int>(at - first));
                    where = buf;
                }
            }
        }
        std::string units;
        if (where != "elsewhere")
            for (uint32_t k = 0; k < placedCount && k < 64; ++k) {
                std::snprintf(buf, sizeof(buf), " %x", placed[k]);
                units += buf;
            }
        std::snprintf(buf, sizeof(buf), "enc %u count %u buffer %u flag %x -> %s count %u:%s", o.mEncoding, count, index, flag,
                      where.c_str(), placedCount, units.c_str());
        out.emplace_back(buf);
        std::string sizes;
        for (unsigned b = 0; b < 6; ++b) {
            std::snprintf(buf, sizeof(buf), " %zu", o.mLayout[b].size());
            sizes += buf;
        }
        sizes += " words";
        for (uint32_t w : o.mLayoutWords) {
            std::snprintf(buf, sizeof(buf), " %u", w);
            sizes += buf;
        }
        out.emplace_back(sizes);
    }
    for (std::vector<int16_t> &v : o.mLayout) std::vector<int16_t>().swap(v);
    std::vector<uint32_t>().swap(o.mLayoutWords);
    std::snprintf(buf, sizeof(buf), "heap %lld blocks", heap.Blocks());
    out.emplace_back(buf);
    return out;
}
} // namespace gzfont

DIFF_TEST(GZFont_Layout, 0x90d563)
{
    World world;
    WarmStatics(ctx);
    for (uint32_t seed = 1; seed <= 200; ++seed) {
        ++ctx.cases;
        const std::vector<std::string> orig = RunLayout(ctx, false, seed);
        const std::vector<std::string> ours = RunLayout(ctx, true, seed);
        if (!Compare(ctx, "layout", seed, orig, ours)) break;
    }
}

// CharAt (0x90b6e1) and HasLineBreak (0x90d6b7) on one buffer for both sides: the original's result for text the converter
// rejects is the low half of the text's own address (ORIGINAL-BUG in the port), so the sides must read the same bytes.
DIFF_TEST(GZFont_CharAt, 0x90b6e1)
{
    struct Page {
        int32_t *global = GameAddress<int32_t>(0xff23a4);
        int32_t saved = *global;
        Page() { *global = 1252; }
        ~Page() { *global = saved; }
    } page;
    shape::CodePatch convert{0x1afd58, reinterpret_cast<const void *>(&RZText::Convert)};
    using CharAtFn = uint32_t(__thiscall *)(void *, const void *, uint32_t, uint32_t *);
    using BreakFn = uint32_t(__thiscall *)(void *, const void *, uint32_t);
    const auto charAt = ctx.Original<CharAtFn>(0x90b6e1);
    const auto hasBreak = ctx.Original<BreakFn>(0x90d6b7);
    static const int32_t kEncodings[] = {0, 8, 16, 932, 936, 949, 950, 1252, -1, -2, 874, 12345};
    static const uint8_t kPieces[][4] = {{'a'}, {'\n'}, {'\r'}, {' '}, {0x80}, {0xa1}, {0xc3, 0xa9}, {0xe2, 0x82, 0xac},
                                         {0xf0, 0x90, 0x80, 0x80}, {0x83, 0x41}, {0xb0, 0xa1}, {0xff}, {0xc3}, {0x85}, {0x00}};
    std::vector<uint8_t> font(cGZFont::kSize, 0);
    cGZFont &o = *reinterpret_cast<cGZFont *>(font.data());
    std::mt19937 rng(41);
    for (uint32_t i = 0; i < 30000; ++i) {
        ++ctx.cases;
        std::vector<uint8_t> text;
        for (unsigned n = rng() % 6; n > 0; --n) {
            const uint8_t *piece = kPieces[rng() % 15];
            for (unsigned b = 0; b < 4 && (b == 0 || piece[b]); ++b) text.push_back(piece[b]);
        }
        text.resize(text.size() + 8, 0x41);
        const uint32_t length = rng() % 8 == 0 ? static_cast<uint32_t>(rng() % 12) : static_cast<uint32_t>(text.size() - 8);
        o.mEncoding = static_cast<uint32_t>(kEncodings[rng() % (sizeof(kEncodings) / sizeof(kEncodings[0]))]);
        uint32_t adv0 = 0xcdcdcdcd, adv1 = 0xcdcdcdcd;
        const bool wantAdvance = rng() % 4 != 0;
        const uint32_t a = charAt(&o, text.data(), length, wantAdvance ? &adv0 : nullptr);
        const uint32_t b = o.CharAt(text.data(), length, wantAdvance ? &adv1 : nullptr);
        if (a != b || adv0 != adv1) ctx.Fail("CharAt enc %d len %u: %x adv %x / %x adv %x", o.mEncoding, length, a, adv0, b, adv1);
        // HasLineBreak (0x90d6b7) steps by the advance CharAt reports, so a character with advance 0 (text the converter rejects)
        // is a loop that never ends in the original and in the port alike: those texts are left out.
        bool steps = true;
        for (const uint8_t *at = text.data(); at < text.data() + length && steps;) {
            uint32_t units = 0;
            o.CharAt(at, length, &units);
            steps = units != 0;
            at += units;
        }
        if (!steps) continue;
        const bool x = (hasBreak(&o, text.data(), length) & 0xff) != 0, y = o.HasLineBreak(text.data(), length);
        if (x != y) ctx.Fail("HasLineBreak enc %d len %u: %u / %u", o.mEncoding, length, x, y);
    }
}

DIFF_TEST(GZFont_GameSlots, 0x90da9e)
{
    // The slot order: every named slot of cIGZFont where the game's vtable 0xe507ec has that function (58, the deleting
    // destructor, is checked by the final Release of GZFont_Slots).
    const Mapping kMap[] = {
        {SlotOfMember(&cIGZFont::QueryInterface), 0x90e248}, {SlotOfMember(&cIGZFont::AddRef), 0x145260},
        {SlotOfMember(&cIGZFont::Release), 0x90e439},        {SlotOfMember(&cIGZFont::Load), 0x90d8eb},
        {SlotOfMember(&cIGZFont::Unload), 0x90ead5},         {SlotOfMember(&cIGZFont::GetFamily), 0x90ccba},
        {SlotOfMember(&cIGZFont::HasStyle), 0x90d26d},       {SlotOfMember(&cIGZFont::GetStyle), 0x1dff70},
        {SlotOfMember(&cIGZFont::SetStyle), 0x90ea33},       {SlotOfMember(&cIGZFont::GetGroup), 0x169f60},
        {SlotOfMember(&cIGZFont::GetSize), 0x1e0950},        {SlotOfMember(&cIGZFont::SetSize), 0x90e8bc},
        {SlotOfMember(&cIGZFont::GetColour), 0x229b60},      {SlotOfMember(&cIGZFont::GetShadowColour), 0x230120},
        {SlotOfMember(&cIGZFont::SetColours), 0x90e76f},     {SlotOfMember(&cIGZFont::GetQuality), 0x22fca0},
        {SlotOfMember(&cIGZFont::SetQuality), 0x90e49e},     {SlotOfMember(&cIGZFont::GetGamma), 0x90cc1d},
        {SlotOfMember(&cIGZFont::SetGamma), 0x90e4ac},       {SlotOfMember(&cIGZFont::GetContrast), 0x90cc21},
        {SlotOfMember(&cIGZFont::SetContrast), 0x90e4dc},    {SlotOfMember(&cIGZFont::GetPath), 0x90cc95},
        {SlotOfMember(&cIGZFont::SetPath), 0x90e754},        {SlotOfMember(&cIGZFont::GetEncoding), 0x90cc84},
        {SlotOfMember(&cIGZFont::SetEncoding), 0x90e50d},    {SlotOfMember(&cIGZFont::IsUnicodeGroup), 0x90da72},
        {SlotOfMember(&cIGZFont::GetScale), 0x90d0aa},       {SlotOfMember(&cIGZFont::SetScale), 0x90e7c9},
        {SlotOfMember(&cIGZFont::GetAdvanceScale), 0x90cbb9}, {SlotOfMember(&cIGZFont::SetAdvanceScale), 0x90e44e},
        {SlotOfMember(&cIGZFont::GetSpacing), 0x90d0ef},     {SlotOfMember(&cIGZFont::SetSpacing), 0x90ea0c},
        {SlotOfMember(&cIGZFont::GetLineSpacing), 0x90ccb0}, {SlotOfMember(&cIGZFont::GetLineHeight), 0x90b66c},
        {SlotOfMember(&cIGZFont::GetAscent), 0x90d0c5},      {SlotOfMember(&cIGZFont::GetDescent), 0x90d0d2},
        {SlotOfMember(&cIGZFont::GetRenderer), 0x155e40},    {SlotOfMember(&cIGZFont::SetRenderer), 0x90e780},
        {SlotOfMember(&cIGZFont::BeginDraw), 0x90dd58},      {SlotOfMember(&cIGZFont::EndDraw), 0x90eb7b},
        {SlotOfMember(&cIGZFont::GetGlyphCache), 0x90cc25},  {SlotOfMember(&cIGZFont::FindGlyph), 0x90cc39},
        {SlotOfMember(&cIGZFont::MakeGlyph), 0x90aa13},      {SlotOfMember(&cIGZFont::PlaceBitmap), 0x90b826},
        {SlotOfMember(&cIGZFont::GrowCache), 0x90e359},
        // Lane OF, the measuring slots.
        {SlotOfMember(&cIGZFont::SupportsEncoding), 0x90da9e}, {SlotOfMember(&cIGZFont::GetCharWidth), 0x90b672},
        {SlotOfMember(&cIGZFont::GetCharAdvance), 0x90cbd2}, {SlotOfMember(&cIGZFont::GetCharBearing), 0x90d672},
        {SlotOfMember(&cIGZFont::FitChars), 0x90afed},       {SlotOfMember(&cIGZFont::WrapLines), 0x90b08f},
        {SlotOfMember(&cIGZFont::CountLines), 0x90b070},     {SlotOfMember(&cIGZFont::WidthForLines), 0x90b39e},
        {SlotOfMember(&cIGZFont::FitRect), 0x90b1e9},        {SlotOfMember(&cIGZFont::MeasureRect), 0x90b13c},
        {SlotOfMember(&cIGZFont::GetExtent), 0x90eb40},      {SlotOfMember(&cIGZFont::BreakLine), 0x90b40e},
        {SlotOfMember(&cIGZFont::GetWidth), 0x90d1dd},       {SlotOfMember(&cIGZFont::GetTrimmedWidth), 0x90d128},
        // Lane OH.
        {SlotOfMember(&cIGZFont::HasGlyphs), 0x90d9e5},      {SlotOfMember(&cIGZFont::CacheGlyphs), 0x90e156},
        {SlotOfMember(&cIGZFont::ClipText), 0x90c8a5},       {SlotOfMember(&cIGZFont::WrapText), 0x90c9b2},
        {SlotOfMember(&cIGZFont::DrawText), 0x90c129},       {SlotOfMember(&cIGZFont::DrawBox), 0x90c526},
    };
    void *const *game = shape::GameVTable(0xe507ec);
    for (const Mapping &m : kMap) {
        ++ctx.cases;
        if (m.slot >= 66 || game[m.slot] != GameAddress<void>(m.rva)) ctx.Fail("slot %u is not %x", m.slot, m.rva);
    }
}

// ---- lane OH: the Thai shaper (0x90dd90) with its class lookup (0x1b1d25) and form table (0x90d324), and the draw side ----

namespace gzfont {

// The shaper alone on a font object of its own on each side: the text it leaves (in place, or in layout buffer 5), the count, the
// layout words, buffer 5's size and capacity (the shaper reserves twice the count), and the heap balance. Seeded text of Thai-block
// characters of every class, sara am after tone marks and after letters, zero units in the middle, and the record flag with bits above
// its low byte.
std::vector<std::string> RunShaper(difftest::Context &ctx, bool ours, uint32_t seed)
{
    using ShapeFn = void(__thiscall *)(void *, uint16_t *, uint32_t, const uint16_t **, uint32_t *, uint32_t);
    static const uint32_t kFlags[] = {0, 1, 0x100, 0x101, 0xff, 0xcd00, 0x80};
    static const uint16_t kPool[] = {0xe33, 0xe33, 0xe48, 0xe49, 0xe4a, 0xe4b, 0xe4c, 0xe4d, 0xe0d, 0xe38, 0xe39, 0xe3a, 0xe1b, 0xe1d,
                                     0xe1f, 0xe2c, 0xe31, 0xe34, 0xe35, 0xe36, 0xe37, 0xe47, 0xe01, 0xe2d, 0xe40, 0x20, 'a', 0};
    const auto original = ctx.Original<ShapeFn>(0x90dd90);
    std::vector<std::string> out;
    char buf[1024];
    alignas(8) uint8_t storage[cGZFont::kSize] = {};
    cGZFont &o = *reinterpret_cast<cGZFont *>(storage);
    difftest::AllocationCounter heap;
    std::mt19937 rng(seed * 31u + 5u);
    for (int i = 0; i < 80; ++i) {
        const uint32_t count = rng() % 5 == 0 ? rng() % 3 : 1 + rng() % 14;
        std::vector<uint16_t> text(count + 4, 0);
        for (uint32_t k = 0; k < count; ++k) {
            const unsigned pick = rng() % 8;
            text[k] = pick < 5 ? kPool[rng() % (sizeof(kPool) / sizeof(kPool[0]))]
                      : pick < 7 ? static_cast<uint16_t>(0xe00 + rng() % 0x80)
                                 : static_cast<uint16_t>(rng() % 0x10000);
        }
        const uint32_t flag = kFlags[rng() % (sizeof(kFlags) / sizeof(kFlags[0]))];
        if (rng() % 3 == 0) o.mLayoutWords.assign(rng() % 4, 90 + rng() % 5);
        if (rng() % 4 == 0) o.mLayout[5].reserve(rng() % 40);
        const uint16_t *placed = reinterpret_cast<const uint16_t *>(0x1111);
        uint32_t placedCount = 0xcdcdcdcd;
        if (ours) o.ShapeThai(text.data(), count, &placed, &placedCount, flag);
        else original(&o, text.data(), count, &placed, &placedCount, flag);
        std::string where = "elsewhere";
        if (placed == text.data()) {
            where = "input";
        } else if (placed == reinterpret_cast<const uint16_t *>(o.mLayout[5].data())) {
            where = "buffer 5";
        }
        std::string units, input;
        for (uint32_t k = 0; k < placedCount && k < 64 && where != "elsewhere"; ++k) {
            std::snprintf(buf, sizeof(buf), " %x", placed[k]);
            units += buf;
        }
        for (uint32_t k = 0; k < count + 4; ++k) {
            std::snprintf(buf, sizeof(buf), " %x", text[k]);
            input += buf;
        }
        std::snprintf(buf, sizeof(buf), "count %u flag %x -> %s count %u:%s; input now:%s", count, flag, where.c_str(), placedCount,
                      units.c_str(), input.c_str());
        out.emplace_back(buf);
        std::string state;
        std::snprintf(buf, sizeof(buf), "buffer 5 size %zu capacity %zu words", o.mLayout[5].size(), o.mLayout[5].capacity());
        state = buf;
        for (uint32_t w : o.mLayoutWords) {
            std::snprintf(buf, sizeof(buf), " %u", w);
            state += buf;
        }
        out.emplace_back(state);
    }
    for (std::vector<int16_t> &v : o.mLayout) std::vector<int16_t>().swap(v);
    std::vector<uint32_t>().swap(o.mLayoutWords);
    std::snprintf(buf, sizeof(buf), "heap %lld blocks", heap.Blocks());
    out.emplace_back(buf);
    return out;
}

} // namespace gzfont

DIFF_TEST(GZFont_Shaper, 0x90dd90)
{
    World world;
    WarmStatics(ctx);
    for (uint32_t seed = 1; seed <= 400; ++seed) {
        ++ctx.cases;
        const std::vector<std::string> orig = RunShaper(ctx, false, seed);
        const std::vector<std::string> ours = RunShaper(ctx, true, seed);
        if (!Compare(ctx, "shaper", seed, orig, ours)) break;
    }
}

// The class lookup (0x1b1d25, cdecl, a 32-bit argument) and the form table (0x90d324, stdcall, a 16-bit character; the original's upper
// half of the result is a left-over, its caller stores 16 bits) over every value.
DIFF_TEST(GZFont_ThaiTables, 0x1b1d25)
{
    World world;
    WarmStatics(ctx);
    const auto cls = ctx.Original<int32_t(__cdecl *)(uint32_t)>(0x1b1d25);
    const auto form = ctx.Original<uint32_t(__stdcall *)(uint32_t, uint32_t)>(0x90d324);
    for (uint32_t ch = 0; ch < 0x30000; ++ch) {
        ++ctx.cases;
        const int32_t a = cls(ch), b = cGZFont::ThaiClass(ch);
        if (a != b) ctx.Fail("ThaiClass %x: %d / %d", ch, a, b);
    }
    for (uint32_t high : {0x10000u, 0x40000u, 0xffff0000u})
        for (uint32_t low = 0xdf0; low < 0xe90; ++low) {
            ++ctx.cases;
            const int32_t a = cls(high | low), b = cGZFont::ThaiClass(high | low);
            if (a != b) ctx.Fail("ThaiClass %x: %d / %d", high | low, a, b);
        }
    for (uint32_t ch = 0; ch < 0x10000; ++ch)
        for (uint32_t f : {0u, 1u, 2u, 3u, 4u, 0x101u, 0xffffffffu}) {
            ++ctx.cases;
            const uint32_t a = form(ch, f) & 0xffff, b = cGZFont::ThaiForm(ch, f) & 0xffff;
            if (a != b) ctx.Fail("ThaiForm %x form %x: %x / %x", ch, f, a, b);
        }
}
