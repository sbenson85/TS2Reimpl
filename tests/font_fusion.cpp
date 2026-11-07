// The font engine (src/font/FontFusion.h) against the original Font Fusion, on the game's own fonts, read from the
// game copy (local/game) at test time: nothing of the fonts is in the repository.
//
// Both engines are driven the way cGZFont drives them (src/gz/cGZFont.size.cpp): the font through a reading stream,
// a Type 1 file to its pure form, the Windows Unicode cmap preferred, the 72 dpi transform at each size the game's
// FontStyle.ini uses, glyph indices from character codes, names, and the glyph renders in both pixel modes.
//
// What has to match exactly: the font type, the extraction, the font's fixed-pitch flag and character range, its style
// bits, every name cGZFont asks for, the transform's results (the matrix, pixels per em, scales and every font-wide
// metric, also at transforms the game doesn't use), every character code's glyph index and existence (also under
// other cmap preferences), every glyph name's code, each render's error, its y advance and its linear advances.
// Rendering is FreeType's, so within stated tolerances: the advance (+0xd0) exact (a hinted TrueType glyph's may be a
// pixel off, in at most one render per thousand), the bitmap's box (top +0xe0, left +0xe4, width, height) within one
// pixel on each edge (two in at most one render per thousand) and exact in three renders of four, and the bitmaps
// compared pixel by pixel and reported (differing pixels and the summed difference against the original's ink), with
// bounds per font and mode at the end of the test.
#include "difftest.h"

#include "font/FontFusion.h"

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace fontfusion_test {

struct GlyphName {
    uint16_t code;
    const char *name;
};
const GlyphName kGlyphNames[] = {
#include "gz/cGZFontGlyphNames.inc"
};

std::wstring FontsDir()
{
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir = exe;
    for (int up = 0; up < 8; ++up) {
        const size_t slash = dir.find_last_of(L"\\/");
        if (slash == std::wstring::npos) break;
        dir.resize(slash);
        const std::wstring probe = dir + L"\\local\\game\\Base\\TSData\\Res\\UI\\Fonts";
        if (GetFileAttributesW(probe.c_str()) != INVALID_FILE_ATTRIBUTES) return probe;
    }
    // A worktree: the game copy is in the main checkout (…\.claude\worktrees\<name>\build\Release\difftest.exe).
    const size_t at = std::wstring(exe).find(L"\\.claude\\worktrees\\");
    if (at != std::wstring::npos) {
        const std::wstring probe = std::wstring(exe).substr(0, at) + L"\\local\\game\\Base\\TSData\\Res\\UI\\Fonts";
        if (GetFileAttributesW(probe.c_str()) != INVALID_FILE_ATTRIBUTES) return probe;
    }
    return L"";
}

// A .mxf file: a 9-byte header, then the font with its first `masked` bytes XORed with the key.
std::vector<uint8_t> LoadFont(const std::wstring &path)
{
    std::vector<uint8_t> out;
    FILE *f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return out;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> raw(static_cast<size_t>(n));
    const size_t got = std::fread(raw.data(), 1, raw.size(), f);
    std::fclose(f);
    if (got != raw.size() || raw.size() < 9) return out;
    const uint32_t masked = (uint32_t(raw[4]) << 24) | (uint32_t(raw[5]) << 16) | (uint32_t(raw[6]) << 8) | raw[7];
    const uint8_t key = raw[8];
    out.assign(raw.begin() + 9, raw.end());
    for (size_t i = 0; i < out.size() && i < masked; ++i) out[i] ^= key;
    return out;
}

struct Buffer {
    const uint8_t *data;
    uint32_t size;
};

// cGZFontFileStream::Read's contract: the whole range or failure.
uint8_t __cdecl ReadBuffer(Buffer *b, uint8_t *dst, uint32_t offset, uint32_t count)
{
    if (offset > b->size || count > b->size - offset) return 0;
    std::memcpy(dst, b->data + offset, count);
    return 1;
}

template <class T> T At(const void *p, uint32_t off)
{
    T v;
    std::memcpy(&v, static_cast<const uint8_t *>(p) + off, sizeof v);
    return v;
}

// One engine's 18 entry points, as cGZFont calls them.
struct Engine {
    const char *name;
    void *(__cdecl *newMem)(int32_t *);
    void(__cdecl *delMem)(void *);
    void *(__cdecl *nonRam)(void *, void *, void *, uint32_t, int32_t *);
    void *(__cdecl *input3)(void *, void *, uint32_t, int32_t *);
    void(__cdecl *delInput)(void *, int32_t *);
    uint32_t(__cdecl *findType)(void *, int32_t *);
    void *(__cdecl *extract)(void *, uint32_t *, int32_t *);
    void *(__cdecl *newSfnt)(void *, uint32_t, uint32_t, void *, uint32_t, uint32_t, int32_t *);
    void(__cdecl *delSfnt)(void *, int32_t *);
    uint8_t *(__cdecl *newT2K)(void *, void *, int32_t *);
    void(__cdecl *delT2K)(uint8_t *, int32_t *);
    void(__cdecl *transform)(uint8_t *, uint32_t, uint32_t, uint32_t, int32_t *, uint32_t, int32_t *);
    uint32_t(__cdecl *glyphIndex)(uint8_t *, uint32_t, int32_t *);
    uint32_t(__cdecl *glyphExists)(uint8_t *, uint32_t, uint32_t, int32_t *);
    uint32_t(__cdecl *byName)(uint8_t *, const char *, int32_t *);
    void(__cdecl *render)(uint8_t *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, int32_t *);
    void(__cdecl *purge)(uint8_t *, uint32_t, int32_t *);
    void(__cdecl *nameString)(uint8_t *, uint32_t, uint32_t, int32_t *);
};

template <class To, class From> To As(From f)
{
    return reinterpret_cast<To>(reinterpret_cast<void *>(f));
}

Engine Original(difftest::Context &ctx)
{
    Engine e;
    e.name = "original";
    e.newMem = ctx.Original<decltype(e.newMem)>(0x9116c7);
    e.delMem = ctx.Original<decltype(e.delMem)>(0x9115c0);
    e.nonRam = ctx.Original<decltype(e.nonRam)>(0x911a54);
    e.input3 = ctx.Original<decltype(e.input3)>(0x9119be);
    e.delInput = ctx.Original<decltype(e.delInput)>(0x91180d);
    e.findType = ctx.Original<decltype(e.findType)>(0x91dc8d);
    e.extract = ctx.Original<decltype(e.extract)>(0x91359f);
    e.newSfnt = ctx.Original<decltype(e.newSfnt)>(0x9199cb);
    e.delSfnt = ctx.Original<decltype(e.delSfnt)>(0x9198e1);
    e.newT2K = ctx.Original<decltype(e.newT2K)>(0x91be9a);
    e.delT2K = ctx.Original<decltype(e.delT2K)>(0x91bcf2);
    e.transform = ctx.Original<decltype(e.transform)>(0x91c300);
    e.glyphIndex = ctx.Original<decltype(e.glyphIndex)>(0x91c04e);
    e.glyphExists = ctx.Original<decltype(e.glyphExists)>(0x91bdc8);
    e.byName = ctx.Original<decltype(e.byName)>(0x91be55);
    e.render = ctx.Original<decltype(e.render)>(0x91c9a8);
    e.purge = ctx.Original<decltype(e.purge)>(0x91c8ba);
    e.nameString = ctx.Original<decltype(e.nameString)>(0x91da2f);
    return e;
}

Engine Ours()
{
    namespace ff = fontfusion;
    Engine e;
    e.name = "ours";
    e.newMem = As<decltype(e.newMem)>(&ff::tsi_NewMemhandler);
    e.delMem = As<decltype(e.delMem)>(&ff::tsi_DeleteMemhandler);
    e.nonRam = As<decltype(e.nonRam)>(&ff::New_NonRamInputStream);
    e.input3 = As<decltype(e.input3)>(&ff::New_InputStream3);
    e.delInput = As<decltype(e.delInput)>(&ff::Delete_InputStream);
    e.findType = As<decltype(e.findType)>(&ff::FF_FindFontTypeFromStream);
    e.extract = As<decltype(e.extract)>(&ff::ExtractPureT1FromPCType1);
    e.newSfnt = As<decltype(e.newSfnt)>(&ff::New_sfntClass);
    e.delSfnt = As<decltype(e.delSfnt)>(&ff::Delete_sfntClass);
    e.newT2K = As<decltype(e.newT2K)>(&ff::NewT2K);
    e.delT2K = As<decltype(e.delT2K)>(&ff::DeleteT2K);
    e.transform = As<decltype(e.transform)>(&ff::T2K_NewTransformation);
    e.glyphIndex = As<decltype(e.glyphIndex)>(&ff::T2K_GetGlyphIndex);
    e.glyphExists = As<decltype(e.glyphExists)>(&ff::T2K_GlyphExists);
    e.byName = As<decltype(e.byName)>(&ff::T2K_GetCharCodeByName);
    e.render = As<decltype(e.render)>(&ff::T2K_RenderGlyph);
    e.purge = As<decltype(e.purge)>(&ff::T2K_PurgeMemory);
    e.nameString = As<decltype(e.nameString)>(&ff::T2K_GetNameString);
    return e;
}

// A font opened as cGZFontFile::Open opens it.
struct Opened {
    std::vector<uint8_t> file; // the engine reads (and may change) its own copy
    Buffer buffer = {};
    void *mem = nullptr, *in = nullptr, *sfnt = nullptr;
    uint8_t *t2k = nullptr;
    uint32_t type = 0;
    int32_t typeErr = 0, extractErr = 0, sfntErr = 0, t2kErr = 0;
    uint32_t pureLength = 0;
    bool extracted = false;
    std::vector<uint8_t> pure;
};

void Open(const Engine &e, const std::vector<uint8_t> &bytes, Opened &o)
{
    o.file = bytes;
    o.buffer = {o.file.data(), static_cast<uint32_t>(o.file.size())};
    int32_t err = 0;
    o.mem = e.newMem(&err);
    o.in = e.nonRam(o.mem, &o.buffer, reinterpret_cast<void *>(&ReadBuffer), o.buffer.size, &err);
    o.typeErr = 0;
    o.type = e.findType(o.in, &o.typeErr) & 0xffff;
    if (o.type == 1) {
        o.pure = o.file;
        uint32_t length = static_cast<uint32_t>(o.pure.size());
        e.extract(o.pure.data(), &length, &o.extractErr);
        o.pureLength = length;
        o.extracted = true;
        if (o.extractErr == 0) {
            o.pure.resize(length);
            void *const pure = e.input3(o.mem, o.pure.data(), length, &err);
            e.delInput(o.in, &err);
            o.in = pure;
        }
    }
    o.sfnt = e.newSfnt(o.mem, o.type, 0, o.in, 0, 0, &o.sfntErr);
    if (!o.sfnt || o.sfntErr) return;
    o.t2k = e.newT2K(o.mem, o.sfnt, &o.t2kErr);
    if (!o.t2k || o.t2kErr) return;
    uint8_t *const font = At<uint8_t *>(o.t2k, 0x180);
    *reinterpret_cast<uint16_t *>(font + 0x78) = 3;
    *reinterpret_cast<uint16_t *>(font + 0x7a) = 1;
    *reinterpret_cast<uint32_t *>(o.t2k + 0x48) = 1;
}

void Close(const Engine &e, Opened &o)
{
    int32_t err = 0;
    if (o.t2k) e.delT2K(o.t2k, &err);
    if (o.sfnt) e.delSfnt(o.sfnt, &err);
    if (o.in) e.delInput(o.in, &err);
    if (o.mem) e.delMem(o.mem);
    o = Opened{};
}

// cGZFontFile::Name's lookups: the name, then the full name; in English (US), then any language; each with the
// preferred encoding and then with encoding 0. The result: the 16-bit string, else the 8-bit one.
std::u16string NameOf(const Engine &e, uint8_t *t2k, uint32_t id, bool *wide)
{
    int32_t err = 0;
    uint8_t *const font = At<uint8_t *>(t2k, 0x180);
    for (unsigned step = 0; step < 8; ++step) {
        if (step != 0 && (At<void *>(t2k, 0x50) || At<void *>(t2k, 0x54))) continue;
        const uint32_t nameId = step < 4 ? id : 4;
        const uint32_t language = (step & 2) ? 0 : 0x409;
        if (step & 1) *reinterpret_cast<uint16_t *>(font + 0x7a) = 0;
        e.nameString(t2k, language, nameId, &err);
        if (step & 1) *reinterpret_cast<uint16_t *>(font + 0x7a) = 1;
    }
    std::u16string out;
    *wide = false;
    if (const char16_t *w = At<const char16_t *>(t2k, 0x54)) {
        *wide = true;
        out = w;
    } else if (const char *n = At<const char *>(t2k, 0x50)) {
        for (; *n; ++n) out.push_back(static_cast<unsigned char>(*n));
    }
    return out;
}

// The rendered glyph in pixel coordinates (x right of the origin, y up), for comparing bitmaps whose boxes differ.
struct Render {
    int32_t err = 0;
    int32_t advance = 0, top = 0, left = 0;
    int32_t yAdvance = 0, xLinear = 0, yLinear = 0; // +0xd4, +0xd8, +0xdc
    uint32_t width = 0, height = 0, pitch = 0;
    bool bitmap = false;
    std::vector<uint8_t> pixels; // width x height, 0..255 (mono: 0 or 255)
    int32_t Pixel(int32_t x, int32_t y) const
    {
        const int32_t col = x - (left >> 6), row = (top >> 6) - 1 - y;
        if (col < 0 || row < 0 || col >= static_cast<int32_t>(width) || row >= static_cast<int32_t>(height)) return 0;
        return pixels[static_cast<size_t>(row) * width + col];
    }
};

Render RenderOne(const Engine &e, uint8_t *t2k, uint32_t index, uint32_t mode, uint32_t flags)
{
    Render r;
    e.render(t2k, index, 0, 0, mode, flags, &r.err);
    if (r.err == 0) {
        r.advance = At<int32_t>(t2k, 0xd0);
        r.yAdvance = At<int32_t>(t2k, 0xd4);
        r.xLinear = At<int32_t>(t2k, 0xd8);
        r.yLinear = At<int32_t>(t2k, 0xdc);
        r.top = At<int32_t>(t2k, 0xe0);
        r.left = At<int32_t>(t2k, 0xe4);
        r.width = At<uint32_t>(t2k, 0x10c);
        r.height = At<uint32_t>(t2k, 0x110);
        r.pitch = At<uint32_t>(t2k, 0x114);
        const uint8_t *const bits = At<const uint8_t *>(t2k, 0x118);
        r.bitmap = bits != nullptr;
        if (bits) {
            r.pixels.resize(static_cast<size_t>(r.width) * r.height);
            for (uint32_t y = 0; y < r.height; ++y)
                for (uint32_t x = 0; x < r.width; ++x)
                    r.pixels[static_cast<size_t>(y) * r.width + x] =
                        mode == 0 ? ((bits[y * r.pitch + (x >> 3)] >> (7 - (x & 7))) & 1 ? 255 : 0) : bits[y * r.pitch + x];
        }
    }
    int32_t err = 0;
    e.purge(t2k, 1, &err);
    return r;
}

// Per font and pixel mode: how the renders compare.
struct Tally {
    uint64_t renders = 0, sameAdvance = 0, sameBox = 0, sameBitmap = 0, boxWithinOne = 0;
    uint64_t ink = 0, difference = 0, pixelsDiffering = 0, pixels = 0;
    int32_t worstAdvance = 0, worstEdge = 0;
    uint64_t advanceOffByOne = 0, boxOffByTwo = 0;
    uint64_t edges[4][3] = {}; // top, left, bottom, right: ours one less, the same, one more
};

// A failure; with TS2_FONT_VERBOSE set, every one is also printed (the runner keeps the first 20).
template <class... A> void Mismatch(difftest::Context &ctx, const char *fmt, A... args)
{
    ctx.Fail(fmt, args...);
    static const bool verbose = GetEnvironmentVariableA("TS2_FONT_VERBOSE", nullptr, 0) != 0;
    static int printed = 0;
    if (verbose && printed < 3000) {
        ++printed;
        std::printf("    ! ");
        std::printf(fmt, args...);
        std::printf("\n");
    }
}

std::string Narrow(const std::u16string &s)
{
    std::string out;
    for (char16_t c : s) out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    return out;
}

} // namespace fontfusion_test

DIFF_TEST(FontFusion_MatchesOriginalOnGameFonts, 0x91c9a8)
{
    using namespace fontfusion_test;
    const std::wstring dir = FontsDir();
    if (dir.empty()) {
        Mismatch(ctx, "no game fonts folder (local/game/Base/TSData/Res/UI/Fonts)");
        return;
    }
    const Engine original = Original(ctx), ours = Ours();
    const wchar_t *const files[] = {L"HelveticaNeueLT Std Medium.mxf", L"ITC Benguiat Gothic (bold).mxf",
                                    L"LetterGothicStd.mxf", L"Maxis Password.mxf", L"Phantok.mxf",
                                    L"DFPGothicP.mxf", L"DFPGothicP (bold).mxf", L"AsiaTK.mxf",
                                    L"AsiaSDNR-M (bold).mxf", L"AR PYenti GB.mxf", L"AR PYenti GB (bold).mxf",
                                    L"AR Heiti B5.mxf", L"AR Heiti2 B5 (bold).mxf"};
    // The sizes FontStyle.ini asks for (and the small ones around them); 26 also at the 0.8 x scale it uses.
    struct Size {
        uint32_t size;
        float scaleX;
    };
    std::vector<Size> sizes;
    for (uint32_t s = 8; s <= 24; ++s) sizes.push_back({s, 1.0f});
    for (uint32_t s : {26u, 30u, 32u, 60u, 70u}) sizes.push_back({s, 1.0f});
    sizes.push_back({26, 0.8f});

    for (const wchar_t *file : files) {
        const std::vector<uint8_t> bytes = LoadFont(dir + L"\\" + file);
        char label[96];
        std::snprintf(label, sizeof label, "%ls", file);
        if (bytes.empty()) {
            Mismatch(ctx, "%s: can't read the font", label);
            continue;
        }
        ctx.Mark(label);
        Opened a, b;
        Open(original, bytes, a);
        Open(ours, bytes, b);
        ++ctx.cases;
        if (a.type != b.type || a.typeErr != b.typeErr)
            Mismatch(ctx, "%s: type %u/%d, ours %u/%d", label, a.type, a.typeErr, b.type, b.typeErr);
        if (a.extracted != b.extracted || a.extractErr != b.extractErr || a.pureLength != b.pureLength)
            Mismatch(ctx, "%s: extraction %d len %u, ours %d len %u", label, a.extractErr, a.pureLength, b.extractErr, b.pureLength);
        if ((a.sfnt != nullptr) != (b.sfnt != nullptr) || a.sfntErr != b.sfntErr || (a.t2k != nullptr) != (b.t2k != nullptr) ||
            a.t2kErr != b.t2kErr) {
            Mismatch(ctx, "%s: open sfnt %d t2k %d, ours sfnt %d t2k %d", label, a.sfntErr, a.t2kErr, b.sfntErr, b.t2kErr);
            Close(original, a);
            Close(ours, b);
            continue;
        }
        if (!a.t2k) {
            Close(original, a);
            Close(ours, b);
            continue;
        }
        const uint32_t type = a.type;
        const uint32_t flags = (type == 2 || type == 7) ? 0x8a : 0x0b;

        // The font: fixed pitch, character range, style bits.
        for (uint32_t off : {0x58u, 0x5cu}) {
            if (At<uint32_t>(a.t2k, off) != At<uint32_t>(b.t2k, off))
                Mismatch(ctx, "%s: t2k+%x %u, ours %u", label, off, At<uint32_t>(a.t2k, off), At<uint32_t>(b.t2k, off));
        }
        if (At<uint32_t>(a.t2k, 0x60) != At<uint32_t>(b.t2k, 0x60))
            Mismatch(ctx, "%s: char range %08x, ours %08x", label, At<uint32_t>(a.t2k, 0x60), At<uint32_t>(b.t2k, 0x60));
        {
            const uint8_t *const fa = At<uint8_t *>(a.t2k, 0x180), *const fb = At<uint8_t *>(b.t2k, 0x180);
            const uint8_t *const ha = At<uint8_t *>(fa, 0x40), *const hb = At<uint8_t *>(fb, 0x40);
            if ((ha != nullptr) != (hb != nullptr))
                Mismatch(ctx, "%s: head %d, ours %d", label, ha != nullptr, hb != nullptr);
            else if (ha && (At<uint16_t>(ha, 0x30) != At<uint16_t>(hb, 0x30) || At<uint16_t>(ha, 0x16) != At<uint16_t>(hb, 0x16) ||
                            At<uint16_t>(ha, 0x32) != At<uint16_t>(hb, 0x32)))
                Mismatch(ctx, "%s: head macStyle %x upem %u rec %u, ours %x %u %u", label, At<uint16_t>(ha, 0x30), At<uint16_t>(ha, 0x16),
                         At<uint16_t>(ha, 0x32), At<uint16_t>(hb, 0x30), At<uint16_t>(hb, 0x16), At<uint16_t>(hb, 0x32));
            // The underline values (+0x7c) only for sfnt fonts: the original's Type 1 reader never writes them (its
            // font-wide metrics take the underline from the Type 1 class, 0x919cb5), so they hold whatever the heap had.
            for (uint32_t off : {0x7cu, 0x80u, 0x84u})
                if ((off != 0x7c || type == 2 || type == 7) && At<uint32_t>(fa, off) != At<uint32_t>(fb, off))
                    Mismatch(ctx, "%s: sfnt+%x %08x, ours %08x", label, off, At<uint32_t>(fa, off), At<uint32_t>(fb, off));
        }
        // The names cGZFont asks for (family, subfamily, full name) and two more.
        for (uint32_t id : {0u, 1u, 2u, 3u, 4u, 6u}) {
            bool wa = false, wb = false;
            const std::u16string na = NameOf(original, a.t2k, id, &wa), nb = NameOf(ours, b.t2k, id, &wb);
            ++ctx.cases;
            if (na != nb || wa != wb)
                Mismatch(ctx, "%s: name %u \"%s\" (%s), ours \"%s\" (%s)", label, id, Narrow(na).c_str(), wa ? "16" : "8",
                         Narrow(nb).c_str(), wb ? "16" : "8");
        }

        // The character codes to look up, and those to render.
        std::vector<uint32_t> codes, renderCodes;
        for (uint32_t c = 0; c < 0x250; ++c) codes.push_back(c);
        for (uint32_t c = 0x2000; c < 0x2070; ++c) codes.push_back(c);
        for (uint32_t c : {0x20acu, 0x2122u, 0xfeffu, 0xffffu, 0x10041u}) codes.push_back(c);
        for (uint32_t c = 0x3000; c < 0x3100; ++c) codes.push_back(c);
        for (uint32_t c = 0x4e00; c < 0xa000; c += 37) codes.push_back(c);
        for (uint32_t c = 0xac00; c < 0xd7a4; c += 41) codes.push_back(c);
        for (uint32_t c = 0xff00; c < 0xfff0; ++c) codes.push_back(c);
        const uint32_t last = At<uint16_t>(a.t2k, 0x62);
        for (uint32_t c = 0x20; c < 0x7f; ++c) renderCodes.push_back(c);
        for (uint32_t c = 0xa0; c < 0x100; ++c) renderCodes.push_back(c);
        if (type == 1)
            for (uint32_t c = 0x100; c <= last && c < 0x400; ++c) renderCodes.push_back(c);
        for (uint32_t c : {0x2026u, 0x20acu, 0x2122u, 0x3001u, 0x3002u, 0x30a2u, 0x3042u, 0xff01u, 0xff21u}) renderCodes.push_back(c);
        for (uint32_t c = 0x4e00; c < 0xa000; c += 331) renderCodes.push_back(c);
        for (uint32_t c = 0xac00; c < 0xd7a4; c += 347) renderCodes.push_back(c);

        // Glyph indices, existence and names don't depend on the size: once, after the first transform.
        bool lookedUp = false;
        Tally tallies[2];
        // The smallest size the font's instructions hint at (head's lowestRecPPEM; none for Type 1).
        const uint8_t *const headA = At<uint8_t *>(At<uint8_t *>(a.t2k, 0x180), 0x40);
        const int32_t lowestHinted = (flags & 0x80) && headA ? At<uint16_t>(headA, 0x32) : 0x7fffffff;
        for (const Size &size : sizes) {
            const float px = static_cast<float>(static_cast<double>(size.size));
            int32_t ma[4] = {static_cast<int32_t>(size.scaleX * 65536.0f * px), 0, 0, static_cast<int32_t>(65536.0f * px)};
            int32_t mb[4] = {ma[0], ma[1], ma[2], ma[3]};
            int32_t ea = 0, eb = 0;
            original.transform(a.t2k, 1, 72, 72, ma, 1, &ea);
            ours.transform(b.t2k, 1, 72, 72, mb, 1, &eb);
            ++ctx.cases;
            if (ea != eb || std::memcmp(ma, mb, sizeof ma) != 0)
                Mismatch(ctx, "%s size %u: transform err %d m %x %x %x %x, ours %d m %x %x %x %x", label, size.size, ea, ma[0], ma[1],
                         ma[2], ma[3], eb, mb[0], mb[1], mb[2], mb[3]);
            const bool hintedSize = At<int32_t>(a.t2k, 0x124) >= lowestHinted && At<int32_t>(a.t2k, 0x128) >= lowestHinted;
            // The transform's results: the matrix, the identity flag, the metrics, pixels per em, the scales.
            struct Range {
                uint32_t from, to;
            };
            // The vertical metrics (+0xa4..+0xcb) only when the flag at +0xa0 says they were set: the original's
            // T2K_NewTransformation (0x91c300) writes them only for a font with vertical metrics ('vhea'), and none of
            // the game's fonts has them, so its values are the heap's.
            const bool vertical = At<uint32_t>(a.t2k, 0xa0) != 0 || At<uint32_t>(b.t2k, 0xa0) != 0;
            for (const Range &r : {Range{0x08, 0x18}, Range{0x1c, 0x20}, Range{0x64, 0xa4}, Range{0xa4, vertical ? 0xccu : 0xa4u},
                                   Range{0x124, 0x13c}, Range{0x164, 0x168}, Range{0x17c, 0x17e}})
                for (uint32_t off = r.from; off < r.to; off += 4)
                    if (At<uint32_t>(a.t2k, off) != At<uint32_t>(b.t2k, off) && !(off == 0x17c && At<uint16_t>(a.t2k, off) == At<uint16_t>(b.t2k, off)))
                        Mismatch(ctx, "%s size %u x%.1f: t2k+%x %08x, ours %08x", label, size.size, size.scaleX, off,
                                 At<uint32_t>(a.t2k, off), At<uint32_t>(b.t2k, off));
            for (uint32_t off : {0x88u, 0x8cu}) {
                const uint8_t *const fa = At<uint8_t *>(a.t2k, 0x180), *const fb = At<uint8_t *>(b.t2k, 0x180);
                if (At<uint32_t>(fa, off) != At<uint32_t>(fb, off))
                    Mismatch(ctx, "%s size %u: sfnt+%x %u, ours %u", label, size.size, off, At<uint32_t>(fa, off), At<uint32_t>(fb, off));
            }

            if (!lookedUp) {
                lookedUp = true;
                for (uint32_t code : codes) {
                    int32_t ga = 0, gb = 0, xa = 0, xb = 0;
                    const uint32_t ia = original.glyphIndex(a.t2k, code, &ga) & 0xffff;
                    const uint32_t ib = ours.glyphIndex(b.t2k, code, &gb) & 0xffff;
                    const uint32_t sa = original.glyphExists(a.t2k, code, 0, &xa);
                    const uint32_t sb = ours.glyphExists(b.t2k, code, 0, &xb);
                    ++ctx.cases;
                    if (ia != ib || ga != gb)
                        Mismatch(ctx, "%s: code %x glyph %u err %d, ours %u err %d", label, code, ia, ga, ib, gb);
                    if (sa != sb || xa != xb)
                        Mismatch(ctx, "%s: code %x exists %u err %d, ours %u err %d", label, code, sa, xa, sb, xb);
                }
                std::vector<std::string> names;
                for (const GlyphName &n : kGlyphNames) names.push_back(n.name);
                for (uint32_t c = 0; c < 0x300; ++c) {
                    char uni[16];
                    std::snprintf(uni, sizeof uni, "uni%04X", c);
                    names.push_back(uni);
                }
                for (const char *extra : {".notdef", "", "space", "fi", "fl", "Euro", "nothing"}) names.push_back(extra);
                for (const std::string &n : names) {
                    int32_t na = 0, nb = 0;
                    const uint32_t ca = original.byName(a.t2k, n.c_str(), &na) & 0xffff;
                    const uint32_t cb = ours.byName(b.t2k, n.c_str(), &nb) & 0xffff;
                    ++ctx.cases;
                    if (ca != cb || na != nb) Mismatch(ctx, "%s: name \"%s\" code %u err %d, ours %u err %d", label, n.c_str(), ca, na, cb, nb);
                }
            }

            // Renders, as cGZFont::MakeGlyph makes them: the glyph index of the code, in each pixel mode.
            for (uint32_t mode : {0u, 3u}) {
                Tally &t = tallies[mode == 0 ? 0 : 1];
                for (uint32_t code : renderCodes) {
                    int32_t ga = 0, gb = 0;
                    const uint32_t index = original.glyphIndex(a.t2k, code, &ga) & 0xffff;
                    const uint32_t indexOurs = ours.glyphIndex(b.t2k, code, &gb) & 0xffff;
                    if (index == 0 || index != indexOurs) continue;
                    const Render ra = RenderOne(original, a.t2k, index, mode, flags);
                    const Render rb = RenderOne(ours, b.t2k, index, mode, flags);
                    ++ctx.cases;
                    ++t.renders;
                    if (ra.err != rb.err) {
                        Mismatch(ctx, "%s size %u mode %u: code %x err %d, ours %d", label, size.size, mode, code, ra.err, rb.err);
                        continue;
                    }
                    if (ra.err) continue;
                    // The advance: exact, but a TrueType glyph hinted by its own instructions may end a pixel apart (the
                    // two bytecode interpreters can move the advance's phantom point differently: LetterGothic's
                    // section sign at 60 and 70), on at most one render in a thousand.
                    const int32_t advanceOff = std::abs(ra.advance - rb.advance);
                    t.worstAdvance = std::max(t.worstAdvance, advanceOff);
                    if (advanceOff == 0) ++t.sameAdvance;
                    else if (hintedSize && advanceOff <= 0x10000) ++t.advanceOffByOne;
                    else
                        Mismatch(ctx, "%s size %u x%.1f mode %u: code %x advance %d, ours %d", label, size.size, size.scaleX, mode, code,
                                 ra.advance >> 16, rb.advance >> 16);
                    // The other advances: the y one (0 here) and the linear (unrounded) pair, exact.
                    if (ra.yAdvance != rb.yAdvance || ra.xLinear != rb.xLinear || ra.yLinear != rb.yLinear)
                        Mismatch(ctx, "%s size %u x%.1f mode %u: code %x advance y %x linear %x %x, ours %x %x %x", label, size.size,
                                 size.scaleX, mode, code, ra.yAdvance, ra.xLinear, ra.yLinear, rb.yAdvance, rb.xLinear, rb.yLinear);
                    if (ra.bitmap != rb.bitmap) {
                        Mismatch(ctx, "%s size %u mode %u: code %x bitmap %d, ours %d", label, size.size, mode, code, ra.bitmap, rb.bitmap);
                        continue;
                    }
                    if (!ra.bitmap) {
                        ++t.sameBox;
                        ++t.boxWithinOne;
                        ++t.sameBitmap;
                        continue;
                    }
                    const int32_t aBottom = (ra.top >> 6) - static_cast<int32_t>(ra.height);
                    const int32_t bBottom = (rb.top >> 6) - static_cast<int32_t>(rb.height);
                    const int32_t aRight = (ra.left >> 6) + static_cast<int32_t>(ra.width);
                    const int32_t bRight = (rb.left >> 6) + static_cast<int32_t>(rb.width);
                    const int32_t edge = std::max({std::abs((ra.top >> 6) - (rb.top >> 6)), std::abs((ra.left >> 6) - (rb.left >> 6)),
                                                   std::abs(aBottom - bBottom), std::abs(aRight - bRight)});
                    t.worstEdge = std::max(t.worstEdge, edge);
                    {
                        const int32_t d[4] = {(rb.top >> 6) - (ra.top >> 6), (rb.left >> 6) - (ra.left >> 6), bBottom - aBottom,
                                              bRight - aRight};
                        for (int k = 0; k < 4; ++k)
                            if (d[k] >= -1 && d[k] <= 1) ++t.edges[k][d[k] + 1];
                    }
                    if (edge == 0) ++t.sameBox;
                    if (edge > 1 && GetEnvironmentVariableA("TS2_FONT_DUMP", nullptr, 0) != 0) {
                        for (const Render *r : {&ra, &rb}) {
                            std::printf("      %s t%d l%d %ux%u\n", r == &ra ? "original" : "ours", r->top >> 6, r->left >> 6, r->width, r->height);
                            for (uint32_t y = 0; y < r->height; ++y) {
                                std::printf("      ");
                                for (uint32_t x = 0; x < r->width; ++x) std::printf("%c", " .:-=+*#%@"[r->pixels[y * r->width + x] * 9 / 255]);
                                std::printf("|\n");
                            }
                        }
                    }
                    // Each edge within a pixel; two on at most one render in a thousand (a hinted dot the original's
                    // interpreter leaves between pixel centres: DFPGothicP's '!' at 8); never more.
                    if (edge <= 1) ++t.boxWithinOne;
                    else if (edge == 2) ++t.boxOffByTwo;
                    else
                        Mismatch(ctx, "%s size %u x%.1f mode %u: code %x box t%d l%d %ux%u, ours t%d l%d %ux%u", label, size.size,
                                 size.scaleX, mode, code, ra.top >> 6, ra.left >> 6, ra.width, ra.height, rb.top >> 6, rb.left >> 6,
                                 rb.width, rb.height);
                    if (ra.pitch != (mode == 0 ? (ra.width + 7) / 8 : ra.width) || rb.pitch != (mode == 0 ? (rb.width + 7) / 8 : rb.width))
                        Mismatch(ctx, "%s size %u mode %u: code %x pitch %u (w %u), ours %u (w %u)", label, size.size, mode, code, ra.pitch,
                                 ra.width, rb.pitch, rb.width);
                    bool same = true;
                    const int32_t x0 = std::min(ra.left, rb.left) >> 6, x1 = std::max(aRight, bRight);
                    const int32_t y0 = std::min(aBottom, bBottom), y1 = std::max(ra.top, rb.top) >> 6;
                    for (int32_t y = y0; y < y1; ++y)
                        for (int32_t x = x0; x < x1; ++x) {
                            const int32_t pa = ra.Pixel(x, y), pb = rb.Pixel(x, y);
                            t.ink += static_cast<uint64_t>(pa);
                            ++t.pixels;
                            if (pa != pb) {
                                same = false;
                                ++t.pixelsDiffering;
                                t.difference += static_cast<uint64_t>(std::abs(pa - pb));
                            }
                        }
                    if (same) ++t.sameBitmap;
                }
            }
        }
        // Transforms the game doesn't use, for the scaler's arithmetic: x and y scaled apart, skewed and rotated (both
        // off-diagonal entries set: the length of each column, Font Fusion's distance and divide).
        {
            const int32_t obliques[][4] = {{30 << 16, 0, 0, 60 << 16},      {60 << 15, 0, 0, 70 << 16},
                                           {12 << 16, 3 << 16, 2 << 16, 12 << 16}, {0x0e0000, -0x07a000, 0x07a000, 0x0e0000},
                                           {-20 << 16, 0x1234, -0x4321, 25 << 16}};
            for (const auto &m : obliques) {
                int32_t ma[4] = {m[0], m[1], m[2], m[3]}, mb[4] = {m[0], m[1], m[2], m[3]};
                int32_t ea = 0, eb = 0;
                original.transform(a.t2k, 1, 72, 72, ma, 1, &ea);
                ours.transform(b.t2k, 1, 72, 72, mb, 1, &eb);
                ++ctx.cases;
                if (ea != eb || std::memcmp(ma, mb, sizeof ma) != 0)
                    Mismatch(ctx, "%s oblique %x %x %x %x: err %d m %x %x %x %x, ours %d m %x %x %x %x", label, m[0], m[1], m[2], m[3],
                             ea, ma[0], ma[1], ma[2], ma[3], eb, mb[0], mb[1], mb[2], mb[3]);
                for (uint32_t off = 0x08; off < 0x168; off += 4) {
                    // As above: the vertical metrics only when written (0x91c300), the rest of the object up to
                    // +0x124 holds the last render's results, which a transform doesn't touch.
                    if ((off >= 0x18 && off < 0x1c) || (off >= 0x20 && off < 0x64) || (off >= 0xa4 && off < 0xcc) ||
                        (off >= 0xcc && off < 0x124) || (off >= 0x13c && off < 0x164))
                        continue;
                    if (At<uint32_t>(a.t2k, off) != At<uint32_t>(b.t2k, off))
                        Mismatch(ctx, "%s oblique %x %x %x %x: t2k+%x %08x, ours %08x", label, m[0], m[1], m[2], m[3], off,
                                 At<uint32_t>(a.t2k, off), At<uint32_t>(b.t2k, off));
                }
            }
        }
        Close(original, a);
        Close(ours, b);

        // The cmap choice with other preferred platforms and encodings than cGZFont's 3 / 1 (a fresh font each: the
        // choice is made at the first lookup): any encoding, any platform, none that the font has, Macintosh Roman.
        if (type == 2 || type == 7) {
            const uint16_t preferences[][2] = {{3, 0xffff}, {0xffff, 0}, {7, 7}, {1, 0}, {0, 3}};
            for (const auto &p : preferences) {
                Opened a2, b2;
                Open(original, bytes, a2);
                Open(ours, bytes, b2);
                if (a2.t2k && b2.t2k) {
                    for (Opened *o : {&a2, &b2}) {
                        uint8_t *const font = At<uint8_t *>(o->t2k, 0x180);
                        *reinterpret_cast<uint16_t *>(font + 0x78) = p[0];
                        *reinterpret_cast<uint16_t *>(font + 0x7a) = p[1];
                    }
                    for (uint32_t code : {0x20u, 0x41u, 0x61u, 0x7au, 0xa7u, 0xe9u, 0xffu, 0x3001u, 0x4e00u, 0xff01u, 0xac00u}) {
                        int32_t ga = 0, gb = 0;
                        const uint32_t ia = original.glyphIndex(a2.t2k, code, &ga) & 0xffff;
                        const uint32_t ib = ours.glyphIndex(b2.t2k, code, &gb) & 0xffff;
                        ++ctx.cases;
                        if (ia != ib || ga != gb)
                            Mismatch(ctx, "%s cmap preference %x/%x: code %x glyph %u err %d, ours %u err %d", label, p[0], p[1], code, ia,
                                     ga, ib, gb);
                    }
                }
                Close(original, a2);
                Close(ours, b2);
            }
        }

        for (int m = 0; m < 2; ++m) {
            const Tally &t = tallies[m];
            std::printf("  %-34s mode %d: %6llu renders, advance same %6llu (worst %d/65536), box same %6llu within 1px %6llu "
                        "(worst %d), bitmap same %6llu, pixels differing %.2f%%, difference %.2f%% of ink\n",
                        label, m == 0 ? 0 : 3, t.renders, t.sameAdvance, t.worstAdvance, t.sameBox, t.boxWithinOne, t.worstEdge,
                        t.sameBitmap, t.pixels ? 100.0 * t.pixelsDiffering / t.pixels : 0.0,
                        t.ink ? 100.0 * t.difference / t.ink : 0.0);
            std::printf("  %-34s         edges (ours - original: -1 / 0 / +1): top %llu/%llu/%llu left %llu/%llu/%llu bottom "
                        "%llu/%llu/%llu right %llu/%llu/%llu\n",
                        "", t.edges[0][0], t.edges[0][1], t.edges[0][2], t.edges[1][0], t.edges[1][1], t.edges[1][2],
                        t.edges[2][0], t.edges[2][1], t.edges[2][2], t.edges[3][0], t.edges[3][1], t.edges[3][2]);
            // The bounds per font and mode (every game font clears each with a margin): at most one render in a
            // thousand with an advance a pixel off or an edge two off, three in four boxes exact, and the bitmaps'
            // summed difference under 7% of the original's ink (1-bit: under 2% of the pixels differing).
            const char *const modeName = m == 0 ? "1-bit" : "grey";
            if (t.advanceOffByOne * 1000 > t.renders)
                Mismatch(ctx, "%s %s: %llu advances a pixel off in %llu renders", label, modeName, t.advanceOffByOne, t.renders);
            if (t.boxOffByTwo * 1000 > t.renders)
                Mismatch(ctx, "%s %s: %llu boxes two pixels off in %llu renders", label, modeName, t.boxOffByTwo, t.renders);
            if (t.sameBox * 4 < t.renders * 3)
                Mismatch(ctx, "%s %s: %llu boxes the same in %llu renders", label, modeName, t.sameBox, t.renders);
            if (t.difference * 100 > t.ink * 7)
                Mismatch(ctx, "%s %s: bitmap difference %llu, 7%% of the ink is %llu", label, modeName, t.difference, t.ink * 7 / 100);
            if (m == 0 && t.pixelsDiffering * 50 > t.pixels)
                Mismatch(ctx, "%s %s: %llu pixels differ in %llu", label, modeName, t.pixelsDiffering, t.pixels);
        }
    }
}
