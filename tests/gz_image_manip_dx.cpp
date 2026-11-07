// Differential tests for nGZGraphic4::cImageManipDX as a whole class (src/gz/cImageManipDX.h, lane NG, rendering R4):
// every slot, the getter and the palette caches.
//
// One world per case, built alike for each side from the case's seed (drawfx::Side):
// - Engine surfaces, textures and a cube texture are drawfx fakes with hand-written slot tables (below) answering from
//   seeded records: sizes, level counts, pixel formats (two format fakes, so "same format" is pointer identity as in
//   the game), the surfaces of each level, access flags. A surface's Lock (cILockableSurface 26) writes all 0x24 bytes
//   of the lock record (the surface, counted; the whole rectangle; its 256-byte pixel buffer; pitch, row bytes, rows),
//   so the locked row copies move real bytes, and every pixel buffer is compared after each side.
// - Each surface answers QueryInterface for its IDirect3DSurface9 with a d3dfx COM fake whose GetDesc writes the
//   seeded description and answers the seeded HRESULT (it writes the description even when it fails: the original
//   reads the description whatever GetDesc answered, so a fake that wrote nothing on failure would leave each side
//   reading its own stack). Failures reach LogD3DError (d3dfx::ErrorLog notes). Textures answer for their
//   IDirect3DBaseTexture9 / IDirect3DTexture9 always (the original asks into uninitialised locals for some of them:
//   0x16af30 [ebp-0x14], 0x16ac20 [ebp-0x14], 0x16bf80 [ebp-0x14], 0x16a980 [ebp-0x30]), and for the memory-texture
//   interface (0x5b891e1e) when the case says so.
// - D3DX (third-party, statically linked: 0x1e310, 0x1d57c, 0x1e256, 0x1d73e, 0x1e5ac, 0x1d884) and the engine's
//   memory-texture helpers (0x184710, 0x183500, cMemBlockAsTexture's 0x18c300 / 0x18c350 / 0x18c5b0 / 0x18c3e0) are
//   stood in on both sides (shape::CodePatch for D3DX, shape::StandIn for the engine's, ours too): recorders noting their
//   arguments (rectangles by content) and answering the case's results. The resampled image of ComputeNormalMap is an
//   embedded fake (its constructor's stand-in embeds it). The clipping helper 0x18ce70 runs for real.
// - The framework's graphic service (cIGraphicSystemDX: slot 28 the palette) is a fake answered through ErrorLog. The
//   five palette caches (the original's thread-safe statics at 0x10ca274..0x10ca298, ours cImageManipDX::sPalettes)
//   start empty or filled (with another palette) per case and are noted after the call.
// - The game's operator new / delete are ours on both sides (the original's std::string and std::vector use them).
// Compared: the log (every call on every fake with its arguments, the stand-ins' notes, LogD3DError), the result, the
// palette caches, every pixel buffer and every fake's reference count.
#include "code_patch.h"
#include "d3dfx.h"
#include "difftest.h"

#include "game/vtable.h"
#include "gz/cImageManipDX.h"
#include "gz/cImageManipDXMemory.h"
#include "gz/cMemBlock.h"

#include <bit>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

using nGZGraphic4::cImageManipDX;

namespace {

// ---- slot tables of the engine objects ----
constexpr drawfx::Slot U{0, drawfx::Ret::Int, false, "", ""};
constexpr drawfx::Slot kQI{2, drawfx::Ret::Int, true, "QueryInterface", "ui"};
constexpr drawfx::Slot kAddRef{0, drawfx::Ret::Int, true, "AddRef", ""};
constexpr drawfx::Slot kRelease{0, drawfx::Ret::Int, true, "Release", ""};

// cILockableSurface (and cISurface): 19 access flags, 20 / 21 width / height, 26 Lock(record, readOnly), 27 Unlock.
inline constexpr drawfx::Slot kSurfaceSlots[] = {
    kQI, kAddRef, kRelease, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    {0, drawfx::Ret::Int, true, "GetAccessFlags", ""},
    {0, drawfx::Ret::Int, true, "GetWidth", ""},
    {0, drawfx::Ret::Int, true, "GetHeight", ""},
    U, U, U, U,
    {2, drawfx::Ret::Int, true, "Lock", "iu"},
    {1, drawfx::Ret::Int, true, "Unlock", "i"},
};
inline constexpr drawfx::Iface kSurface{"cILockableSurface", 28, kSurfaceSlots};

// cITexture: 16 levels, 17 / 18 width / height of a level, 19 pixel format, 25-28 a level's surfaces.
inline constexpr drawfx::Slot kTextureSlots[] = {
    kQI, kAddRef, kRelease, U, U, U, U, U, U, U, U, U, U, U, U, U,
    {0, drawfx::Ret::Int, true, "GetLevelCount", ""},
    {1, drawfx::Ret::Int, true, "GetWidth", "u"},
    {1, drawfx::Ret::Int, true, "GetHeight", "u"},
    {0, drawfx::Ret::Int, true, "GetPixelFormat", ""},
    U, U, U, U, U,
    {1, drawfx::Ret::Int, true, "GetSurface", "u"},
    {1, drawfx::Ret::Int, true, "GetTargetSurface", "u"},
    {1, drawfx::Ret::Int, true, "GetLockableSurface", "u"},
    {1, drawfx::Ret::Int, true, "GetTargetLockableSurface", "u"},
};
inline constexpr drawfx::Iface kTexture{"cITexture", 29, kTextureSlots};

// cICubeTexture: as cITexture, 26 / 28 take (face, level).
inline constexpr drawfx::Slot kCubeSlots[] = {
    kQI, kAddRef, kRelease, U, U, U, U, U, U, U, U, U, U, U, U, U,
    {0, drawfx::Ret::Int, true, "GetLevelCount", ""},
    {1, drawfx::Ret::Int, true, "GetWidth", "u"},
    {1, drawfx::Ret::Int, true, "GetHeight", "u"},
    {0, drawfx::Ret::Int, true, "GetPixelFormat", ""},
    U, U, U, U, U, U,
    {2, drawfx::Ret::Int, true, "GetTargetSurface", "uu"},
    U,
    {2, drawfx::Ret::Int, true, "GetTargetLockableSurface", "uu"},
};
inline constexpr drawfx::Iface kCube{"cICubeTexture", 29, kCubeSlots};

inline constexpr drawfx::Slot kFormatSlots[] = {
    kQI, kAddRef, kRelease,
    {0, drawfx::Ret::Int, true, "GetFormatId", ""},
    U, U,
    {0, drawfx::Ret::Int, true, "GetBitsPerPixel", ""},
};
inline constexpr drawfx::Iface kFormat{"cIPixelFormat", 7, kFormatSlots};

// The graphic service: 28 the palette; LogD3DError's QueryServiceValue asks it for slot 30 (error text; 0 the default).
inline constexpr drawfx::Slot kGraphicSlots[] = {
    kQI, kAddRef, kRelease, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    {0, drawfx::Ret::Int, true, "GetPalette", ""},
    U,
    {1, drawfx::Ret::Int, true, "Slot30", "u"},
};
inline constexpr drawfx::Iface kGraphic{"cIGraphicSystemDX", 31, kGraphicSlots};

inline constexpr drawfx::Slot kUnknownSlots[] = {kQI, kAddRef, kRelease};
inline constexpr drawfx::Iface kUnknown{"cIGZUnknown", 3, kUnknownSlots};

constexpr uint32_t kD3DSurfaceIid = 0x4a5a7223, kD3DBaseTextureIid = 0x998dbd2, kD3DTextureIid = 0x8b54da90,
                   kMemoryTextureIid = 0x5b891e1e;
constexpr uint32_t kGraphicServiceId = 0x85522803, kGraphicServiceIid = 0xc55289ba;
constexpr uint32_t kGameVTable = 0xd9e844;
constexpr uint32_t kSystemCodePageCell = 0xff23a4;

// ---- the world ----
constexpr unsigned kSurfaces = 64, kTextures = 8, kLevels = 4, kPixelBytes = 256;
constexpr unsigned kImage = kTextures;    // the resampled image's record (ComputeNormalMap)
constexpr unsigned kCubeRec = kTextures + 1;
constexpr unsigned kRecords = kTextures + 2;

struct SurfaceRec {
    uint32_t width, height, access;
    int32_t pitch;
    uint32_t rowBytes, rows;
    D3DSURFACE_DESC desc;
    uint32_t descHr;
    uint8_t pixels[kPixelBytes];
};
struct TextureRec {
    uint32_t levels, format;
    uint32_t width[kLevels], height[kLevels];
    uint32_t surface[6][kLevels];
    bool memoryFace;
};
// The D3DX and helper stand-ins' answers.
struct Script {
    uint32_t hr[6];          // LoadFromSurface, LoadFromMemory, LoadFromFile, Save, Filter, NormalMap
    uint32_t memoryCopies;   // bit n: the n-th call of 0x184710 answers true
    unsigned memoryCopyCalls;
    bool memoryNormal;       // 0x183500's answer
    uint8_t *imageBase;      // the resampled image's top level (SetTopLevel), for AddLevel's offsets
};
struct World {
    SurfaceRec surf[kSurfaces];
    TextureRec tex[kRecords];
    Script script;
    uint32_t formatId[2], formatBits[2];
    uint32_t paletteCached; // bit n: palette cache n starts filled
    // Arguments passed by pointer (a registered range).
    struct Args {
        RECT rect[2];
        uint32_t key;
        uint8_t bits[kPixelBytes];
        uint8_t file[64];
        char path[64];
        uint8_t palette[2][16];
    } args;
} gWorld;

drawfx::Fake gSurf[kSurfaces], gD3DSurf[kSurfaces], gTex[kTextures + 1], gCube, gD3DTex, gMemTex, gFormat[2], gGraphic;

uint32_t W(const void *p) { return drawfx::P(p); }
uint32_t N(const void *p) { return drawfx::Norm(W(p)); }
uint32_t N(uint32_t w) { return drawfx::Norm(w); }

SurfaceRec &SRec(drawfx::Fake &f) { return gWorld.surf[f.value[0]]; }
TextureRec &TRec(drawfx::Fake &f) { return gWorld.tex[f.value[0]]; }

// ---- handlers ----
uint32_t SurfaceAccess(drawfx::Fake &f, const uint32_t *) { return SRec(f).access; }
uint32_t SurfaceWidth(drawfx::Fake &f, const uint32_t *) { return SRec(f).width; }
uint32_t SurfaceHeight(drawfx::Fake &f, const uint32_t *) { return SRec(f).height; }
// The lock record (0x24 bytes), all of it: the surface (counted), the rectangle, bits, pitch, row bytes, rows.
uint32_t SurfaceLock(drawfx::Fake &f, const uint32_t *a)
{
    SurfaceRec &s = SRec(f);
    uint32_t *record = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    ++f.refs;
    record[0] = W(&f);
    record[1] = 0;
    record[2] = 0;
    record[3] = s.width;
    record[4] = s.height;
    record[5] = W(s.pixels);
    record[6] = uint32_t(s.pitch);
    record[7] = s.rowBytes;
    record[8] = s.rows;
    return 1;
}
uint32_t D3DSurfaceDesc(drawfx::Fake &f, const uint32_t *a)
{
    SurfaceRec &s = SRec(f);
    if (a[0]) std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), &s.desc, sizeof(D3DSURFACE_DESC));
    return s.descHr;
}
uint32_t TexLevels(drawfx::Fake &f, const uint32_t *) { return TRec(f).levels; }
uint32_t TexWidth(drawfx::Fake &f, const uint32_t *a) { return TRec(f).width[a[0] % kLevels]; }
uint32_t TexHeight(drawfx::Fake &f, const uint32_t *a) { return TRec(f).height[a[0] % kLevels]; }
uint32_t TexFormat(drawfx::Fake &f, const uint32_t *) { return W(&gFormat[TRec(f).format]); }
uint32_t TexSurface(drawfx::Fake &f, const uint32_t *a) { return W(&gSurf[TRec(f).surface[0][a[0] % kLevels]]); }
uint32_t CubeSurface(drawfx::Fake &f, const uint32_t *a)
{
    return W(&gSurf[TRec(f).surface[a[0] % 6][a[1] % kLevels]]);
}

template <const drawfx::Iface &I> void TextureFake(drawfx::Fake &f, uint32_t id, unsigned record)
{
    f.Reset<I>(id);
    f.value[0] = record;
    f.On(16, &TexLevels);
    f.On(17, &TexWidth);
    f.On(18, &TexHeight);
    f.On(19, &TexFormat);
    if (&I == &kCube) {
        f.On(26, &CubeSurface);
        f.On(28, &CubeSurface);
    } else {
        for (unsigned s = 25; s <= 28; ++s) f.On(s, &TexSurface);
    }
    f.AddFace(kD3DBaseTextureIid, gD3DTex);
    f.AddFace(kD3DTextureIid, gD3DTex);
    if (gWorld.tex[record].memoryFace) f.AddFace(kMemoryTextureIid, gMemTex);
}

// ---- stand-ins ----
void NoteWords(const char *label, const std::vector<uint32_t> &words)
{
    drawfx::Call c;
    c.note = label;
    c.words = words;
    drawfx::gLog.push_back(std::move(c));
}
void Rect(std::vector<uint32_t> &w, const RECT *r)
{
    w.push_back(r != nullptr);
    if (r) w.insert(w.end(), {uint32_t(r->left), uint32_t(r->top), uint32_t(r->right), uint32_t(r->bottom)});
}

HRESULT __stdcall StandLoadFromSurface(void *dst, void *dstPal, const RECT *dstRect, void *src, void *srcPal,
                                       const RECT *srcRect, DWORD filter, DWORD key)
{
    std::vector<uint32_t> w{N(dst), N(dstPal)};
    Rect(w, dstRect);
    w.insert(w.end(), {N(src), N(srcPal)});
    Rect(w, srcRect);
    w.insert(w.end(), {filter, key});
    NoteWords("D3DXLoadSurfaceFromSurface", w);
    return HRESULT(gWorld.script.hr[0]);
}
HRESULT __stdcall StandLoadFromMemory(void *dst, void *dstPal, const RECT *dstRect, const void *bits, DWORD format,
                                      UINT pitch, void *srcPal, const RECT *srcRect, DWORD filter, DWORD key)
{
    std::vector<uint32_t> w{N(dst), N(dstPal)};
    Rect(w, dstRect);
    w.insert(w.end(), {N(bits), format, pitch, N(srcPal)});
    Rect(w, srcRect);
    w.insert(w.end(), {filter, key});
    NoteWords("D3DXLoadSurfaceFromMemory", w);
    return HRESULT(gWorld.script.hr[1]);
}
HRESULT __stdcall StandLoadFromFile(void *dst, void *dstPal, const RECT *dstRect, const void *data, UINT size,
                                    const RECT *srcRect, DWORD filter, DWORD key, void *info)
{
    std::vector<uint32_t> w{N(dst), N(dstPal)};
    Rect(w, dstRect);
    w.insert(w.end(), {N(data), size});
    Rect(w, srcRect);
    w.insert(w.end(), {filter, key, N(info)});
    NoteWords("D3DXLoadSurfaceFromFileInMemory", w);
    return HRESULT(gWorld.script.hr[2]);
}
HRESULT __stdcall StandSave(const char *file, uint32_t format, void *surface, void *palette, const RECT *rect)
{
    drawfx::Call c;
    c.note = "D3DXSaveSurfaceToFileA";
    c.text = file ? file : "(null)";
    c.words = {file != nullptr, N(format), N(surface), N(palette)};
    Rect(c.words, rect);
    drawfx::gLog.push_back(std::move(c));
    return HRESULT(gWorld.script.hr[3]);
}
HRESULT __stdcall StandFilter(void *texture, void *palette, UINT level, DWORD filter)
{
    drawfx::Note("D3DXFilterTexture", {N(texture), N(palette), level, filter});
    return HRESULT(gWorld.script.hr[4]);
}
HRESULT __stdcall StandNormalMap(void *texture, void *source, void *palette, DWORD flags, DWORD channel, float amplitude)
{
    drawfx::Note("D3DXComputeNormalMap", {N(texture), N(source), N(palette), flags, channel, std::bit_cast<uint32_t>(amplitude)});
    return HRESULT(gWorld.script.hr[5]);
}
bool __cdecl StandMemoryCopy(void *source, void *target, bool mips)
{
    const unsigned n = gWorld.script.memoryCopyCalls++;
    const bool answer = n < 32 && (gWorld.script.memoryCopies >> n & 1);
    drawfx::Note("CopyMemoryTextures", {N(source), N(target), uint32_t(mips), answer});
    return answer;
}
bool __cdecl StandMemoryNormal(void *source, void *target, float amplitude)
{
    drawfx::Note("ComputeMemoryNormalMap", {N(source), N(target), std::bit_cast<uint32_t>(amplitude)});
    return gWorld.script.memoryNormal;
}
// cMemBlockAsTexture: the object becomes an embedded fake of the image's record.
void *__fastcall StandImageConstruct(uint32_t *self, void *)
{
    drawfx::Note("image.construct", N(self));
    gTex[kImage].Embed(self);
    self[1] = 0;
    self[2] = 0;
    return self;
}
void __fastcall StandImageDestruct(uint32_t *self, void *) { drawfx::Note("image.destruct", {N(self), self[1], self[2]}); }
void __fastcall StandImageTop(uint32_t *self, void *, uint8_t *bits, void *format, uint32_t width, uint32_t height,
                              uint32_t pitch)
{
    gWorld.script.imageBase = bits;
    drawfx::Note("image.SetTopLevel", {N(self), bits != nullptr, N(format), width, height, pitch});
}
void __fastcall StandImageLevel(uint32_t *self, void *, uint8_t *bits, uint32_t width, uint32_t height, uint32_t pitch)
{
    // The levels follow each other in one block: noted by offset from the top level (each side's block is its own).
    drawfx::Note("image.AddLevel", {N(self), uint32_t(bits - gWorld.script.imageBase), width, height, pitch});
}
void *__cdecl GameNew(size_t size) { return ::operator new(size); }
void __cdecl GameDelete(void *p) { ::operator delete(p); }

struct StandIns {
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNew)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDelete)};
    shape::CodePatch loadSurface{0x1e310, reinterpret_cast<const void *>(&StandLoadFromSurface)};
    shape::CodePatch loadMemory{0x1d57c, reinterpret_cast<const void *>(&StandLoadFromMemory)};
    shape::CodePatch loadFile{0x1e256, reinterpret_cast<const void *>(&StandLoadFromFile)};
    shape::CodePatch save{0x1d73e, reinterpret_cast<const void *>(&StandSave)};
    shape::CodePatch filter{0x1e5ac, reinterpret_cast<const void *>(&StandFilter)};
    shape::CodePatch normalMap{0x1d884, reinterpret_cast<const void *>(&StandNormalMap)};
    shape::StandIn memoryCopy{0x184710, &nGZGraphic4::memchain::CopyMemoryTextures, &StandMemoryCopy}; // ours is lane OW's
    shape::StandIn memoryNormal{0x183500, &nGZGraphic4::memchain::ComputeMemoryNormalMap, &StandMemoryNormal};
    // cMemBlockAsTexture is ours (lane RN, src/gz/cMemBlock.h; tests/gz_memblock.cpp): stood in on both sides.
    shape::StandIn imageConstruct{0x18c300, &nRZGraphic4::cMemBlockAsTexture::Construct, &StandImageConstruct};
    shape::StandIn imageDestruct{0x18c350, &nRZGraphic4::cMemBlockAsTexture::Destruct, &StandImageDestruct};
    shape::StandIn imageTop{0x18c5b0, shape::MemberCode(&nRZGraphic4::cMemBlockAsTexture::SetTopLevel), &StandImageTop};
    shape::StandIn imageLevel{0x18c3e0, shape::MemberCode(&nRZGraphic4::cMemBlockAsTexture::AddLevel), &StandImageLevel};
};

// ---- palette caches ----
// The original's: a thread-safe static each (guard, value). A guard of INT_MIN is never newer than the thread's epoch
// (the header's fast path: filled); 0 makes the header claim it (empty).
struct PaletteCaches {
    difftest::Context &ctx;
    int32_t savedGuard[cImageManipDX::kPaletteUsers];
    uint32_t savedValue[cImageManipDX::kPaletteUsers];
    cImageManipDX::PaletteCache savedOurs[cImageManipDX::kPaletteUsers];
    int32_t savedCodePage;
    explicit PaletteCaches(difftest::Context &c) : ctx(c)
    {
        for (unsigned i = 0; i < cImageManipDX::kPaletteUsers; ++i) {
            savedGuard[i] = *ctx.image.At<int32_t>(cImageManipDX::kPaletteGuardRva[i]);
            savedValue[i] = *ctx.image.At<uint32_t>(cImageManipDX::kPaletteValueRva[i]);
            savedOurs[i] = cImageManipDX::sPalettes[i];
        }
        savedCodePage = *ctx.image.At<int32_t>(kSystemCodePageCell);
        // GetACP's cache: UTF-8, the strings' own encoding, so the path's conversion copies nothing (no import call).
        *ctx.image.At<int32_t>(kSystemCodePageCell) = 8;
    }
    ~PaletteCaches()
    {
        for (unsigned i = 0; i < cImageManipDX::kPaletteUsers; ++i) {
            *ctx.image.At<int32_t>(cImageManipDX::kPaletteGuardRva[i]) = savedGuard[i];
            *ctx.image.At<uint32_t>(cImageManipDX::kPaletteValueRva[i]) = savedValue[i];
            cImageManipDX::sPalettes[i] = savedOurs[i];
        }
        *ctx.image.At<int32_t>(kSystemCodePageCell) = savedCodePage;
    }
    void Prepare(bool original)
    {
        for (unsigned i = 0; i < cImageManipDX::kPaletteUsers; ++i) {
            const bool filled = gWorld.paletteCached >> i & 1;
            const uint32_t value = filled ? W(gWorld.args.palette[1]) : 0;
            if (original) {
                *ctx.image.At<int32_t>(cImageManipDX::kPaletteGuardRva[i]) = filled ? INT_MIN : 0;
                *ctx.image.At<uint32_t>(cImageManipDX::kPaletteValueRva[i]) = value;
            } else {
                cImageManipDX::sPalettes[i] = {filled, reinterpret_cast<const PALETTEENTRY *>(uintptr_t(value))};
            }
        }
    }
    void NoteAfter(bool original)
    {
        for (unsigned i = 0; i < cImageManipDX::kPaletteUsers; ++i) {
            const bool ready = original ? *ctx.image.At<int32_t>(cImageManipDX::kPaletteGuardRva[i]) != 0
                                        : cImageManipDX::sPalettes[i].ready;
            const uint32_t value = original ? *ctx.image.At<uint32_t>(cImageManipDX::kPaletteValueRva[i])
                                            : W(cImageManipDX::sPalettes[i].palette);
            drawfx::Note("palette cache", {i, ready, N(value)});
        }
    }
};

// ---- building a case ----
uint32_t Pick(std::mt19937 &gen, std::initializer_list<uint32_t> values) { return values.begin()[gen() % values.size()]; }

void Seed(uint32_t seed)
{
    std::mt19937 gen(seed);
    World &w = gWorld;
    std::memset(&w, 0, sizeof(w));
    for (SurfaceRec &s : w.surf) {
        s.width = Pick(gen, {1, 2, 4, 8, 16, 33, 64});
        s.height = Pick(gen, {1, 2, 4, 8, 16, 40, 64});
        s.access = gen() % 8 == 0 ? Pick(gen, {0, 2, 6}) : Pick(gen, {1, 3, 0xffffffff});
        s.pitch = int32_t(gen() % 33);
        s.rowBytes = gen() % 33;
        s.rows = gen() % 9;
        s.desc.Format = gen() % 3 == 0 ? D3DFMT_P8 : D3DFORMAT(Pick(gen, {21, 22, 23, 50, 0x31545844}));
        s.desc.Type = D3DRTYPE_SURFACE;
        s.desc.Width = s.width;
        s.desc.Height = s.height;
        s.descHr = gen() % 6 == 0 ? d3dfx::kResults[gen() % 4] : 0;
        for (uint8_t &b : s.pixels) b = uint8_t(gen());
    }
    for (TextureRec &t : w.tex) {
        // Level counts stay small: the level loops run to the count (a count of 2^32 - 1 would loop until memory ran out
        // on both sides).
        t.levels = gen() % 10 == 0 ? Pick(gen, {0, 5}) : 1 + gen() % kLevels;
        t.format = gen() % 2;
        uint32_t width = Pick(gen, {2, 4, 8, 16}), height = Pick(gen, {2, 4, 8, 16});
        if (gen() % 3 == 0) height = width;
        for (unsigned l = 0; l < kLevels; ++l) {
            t.width[l] = gen() % 12 == 0 ? Pick(gen, {0, 3, 0x80000000}) : width;
            t.height[l] = gen() % 12 == 0 ? Pick(gen, {0, 5, 0x80000001}) : height;
            width = width > 1 ? width / 2 : 1;
            height = height > 1 ? height / 2 : 1;
        }
        for (auto &face : t.surface)
            for (uint32_t &s : face) s = gen() % kSurfaces;
        t.memoryFace = gen() % 3 == 0;
    }
    // The cube is square, sized like most faces.
    TextureRec &cube = w.tex[kCubeRec];
    cube.width[0] = cube.height[0] = Pick(gen, {4, 8});
    for (unsigned l = 1; l < kLevels; ++l) cube.width[l] = cube.height[l] = cube.width[l - 1] > 1 ? cube.width[l - 1] / 2 : 1;
    for (unsigned f = 0; f < 6 && gen() % 2; ++f) {
        TextureRec &t = w.tex[f];
        std::memcpy(t.width, cube.width, sizeof(t.width));
        std::memcpy(t.height, cube.height, sizeof(t.height));
    }
    for (uint32_t &hr : w.script.hr) hr = gen() % 4 == 0 ? d3dfx::kResults[gen() % 4] : 0;
    w.script.memoryCopies = gen() % 4 == 0 ? uint32_t(gen()) : 0;
    w.script.memoryNormal = gen() % 3 == 0;
    w.formatId[0] = Pick(gen, {21, 41, 50});
    w.formatId[1] = Pick(gen, {22, 23, 0x31545844});
    w.formatBits[0] = Pick(gen, {8, 16, 32});
    w.formatBits[1] = Pick(gen, {4, 8, 24, 32});
    w.paletteCached = gen();
    // Rectangles mostly inside the surfaces (so most copies survive clipping), sometimes partly outside, empty or
    // inverted; the target often the source's size.
    for (RECT &r : w.args.rect) {
        r.left = LONG(gen() % 4 == 0 ? int32_t(gen() % 80) - 8 : int32_t(gen() % 8));
        r.top = LONG(gen() % 4 == 0 ? int32_t(gen() % 80) - 8 : int32_t(gen() % 8));
        r.right = gen() % 8 == 0 ? r.left - LONG(gen() % 4) : r.left + LONG(gen() % 4 == 0 ? gen() % 72 : 1 + gen() % 8);
        r.bottom = gen() % 8 == 0 ? r.top : r.top + LONG(gen() % 4 == 0 ? gen() % 72 : 1 + gen() % 8);
    }
    if (gen() % 2) {
        RECT &t = w.args.rect[1];
        const RECT &s = w.args.rect[0];
        t.right = t.left + (s.right - s.left);
        t.bottom = t.top + (s.bottom - s.top);
    }
    w.args.key = gen() % 2 ? 0 : uint32_t(gen());
    for (uint8_t &b : w.args.bits) b = uint8_t(gen());
    for (uint8_t &b : w.args.file) b = uint8_t(gen());
    const unsigned pathLength = Pick(gen, {0, 5, 15, 16, 40});
    for (unsigned i = 0; i < pathLength; ++i) w.args.path[i] = char('a' + gen() % 26);
    std::memset(w.args.palette, 0x5a, sizeof(w.args.palette));
}

// The fakes for the seeded world (each side's build).
void Build()
{
    for (unsigned i = 0; i < kSurfaces; ++i) {
        drawfx::Fake &s = gSurf[i];
        s.Reset<kSurface>(0x5f000 + i);
        s.value[0] = i;
        s.On(19, &SurfaceAccess);
        s.On(20, &SurfaceWidth);
        s.On(21, &SurfaceHeight);
        s.On(26, &SurfaceLock);
        drawfx::Fake &d = gD3DSurf[i];
        d.Reset<fx_d3d9::IDirect3DSurface9>(0xd3d50000 + i);
        d.value[0] = i;
        d.On(d3dfx::surf::GetDesc, &D3DSurfaceDesc);
        s.AddFace(kD3DSurfaceIid, d);
    }
    gD3DTex.Reset<fx_d3d9::IDirect3DTexture9>(0xd3d60001);
    gMemTex.Reset<kUnknown>(0x6e001);
    for (unsigned i = 0; i < 2; ++i) {
        gFormat[i].Reset<kFormat>(0x6f000 + i);
        gFormat[i].Returns(3, gWorld.formatId[i]);
        gFormat[i].Returns(6, gWorld.formatBits[i]);
    }
    for (unsigned i = 0; i <= kImage; ++i) TextureFake<kTexture>(gTex[i], 0x7e000 + i, i);
    gTex[kImage].AddFace(kMemoryTextureIid, gMemTex); // cMemBlockAsTexture answers for itself
    TextureFake<kCube>(gCube, 0x7c000, kCubeRec);
    gGraphic.Reset<kGraphic>(0x6a001);
    gGraphic.Returns(28, gWorld.args.palette[0]);
    drawfx::AddRange(&gWorld.args, sizeof(gWorld.args), 0x7a000000);
}

// The fakes' reference counts and every pixel buffer, after a side.
struct After {
    drawfx::Log log;
    std::vector<std::string> errors;
    std::vector<uint8_t> pixels;
};
void NoteRefs()
{
    std::vector<uint32_t> refs;
    for (drawfx::Fake *f : drawfx::gFakes) refs.push_back(uint32_t(f->refs));
    NoteWords("refs", refs);
}

struct Seen {
    unsigned trueResults = 0, falseResults = 0, d3dx = 0, reports = 0, locks = 0, filters = 0, fresh = 0;
};

// Runs one case on both sides and compares.
template <class Run> void Case(difftest::Context &ctx, PaletteCaches &caches, Seen &seen, const char *what, uint32_t seed,
                               Run run)
{
    static void *gameObject[1];
    gameObject[0] = GameAddress<void>(kGameVTable);
    cImageManipDX &ours = *cImageManipDX::Instance();
    auto side = [&](bool original) {
        After a;
        Seed(seed);
        caches.Prepare(original);
        auto [log, errors] = drawfx::Side([&] { Build(); }, [&] {
            drawfx::gLog.reserve(64);
            const bool result = run(original ? static_cast<void *>(gameObject) : static_cast<void *>(&ours), original);
            drawfx::Note("result", result);
            caches.NoteAfter(original);
            NoteRefs();
        });
        a.log = std::move(log);
        a.errors = std::move(errors);
        for (const SurfaceRec &s : gWorld.surf) a.pixels.insert(a.pixels.end(), s.pixels, s.pixels + kPixelBytes);
        return a;
    };
    const After a = side(true);
    const After b = side(false);
    for (const drawfx::Call &c : a.log) {
        if (!c.iface && std::strcmp(c.note, "result") == 0) (c.words[0] ? seen.trueResults : seen.falseResults)++;
        if (!c.iface && std::strncmp(c.note, "D3DX", 4) == 0) ++seen.d3dx;
        if (!c.iface && std::strcmp(c.note, "D3DXFilterTexture") == 0) ++seen.filters;
        if (!c.iface && std::strcmp(c.note, "LogD3DError") == 0) ++seen.reports;
        if (c.iface == &kSurface && c.slot == 26) ++seen.locks;
        if (c.iface == &kGraphic && c.slot == 28) ++seen.fresh;
    }
    std::vector<std::string> errors = a.errors;
    errors.insert(errors.end(), b.errors.begin(), b.errors.end());
    if (!drawfx::Same(ctx, a.log, b.log, what, seed, errors)) return;
    if (a.pixels != b.pixels) {
        size_t i = 0;
        while (a.pixels[i] == b.pixels[i]) ++i;
        ctx.Fail("%s seed %u: pixels differ at surface %u byte %u", what, seed, unsigned(i / kPixelBytes), unsigned(i % kPixelBytes));
    }
}

void CheckSeen(difftest::Context &ctx, const char *what, const Seen &s, bool locks, bool filters, bool reports = true)
{
    if (!s.trueResults || !s.falseResults || !s.d3dx || (reports && !s.reports) || !s.fresh || (locks && !s.locks) ||
        (filters && !s.filters))
        ctx.Fail("%s: a path never ran (true %u, false %u, D3DX %u, reports %u, palette fetches %u, locks %u, filters %u)",
                 what, s.trueResults, s.falseResults, s.d3dx, s.reports, s.fresh, s.locks, s.filters);
}

// The harness for one slot's test: stand-ins, the error log answering the graphic service, the caches.
struct Fixture {
    StandIns stands;
    d3dfx::ErrorLog errors;
    PaletteCaches caches;
    explicit Fixture(difftest::Context &ctx) : errors(ctx), caches(ctx) { errors.Answer(kGraphicServiceId, kGraphicServiceIid, gGraphic); }
};

void *Surface(std::mt19937 &gen) { return &gSurf[gen() % kSurfaces]; }
void *Texture(std::mt19937 &gen) { return &gTex[gen() % kTextures]; }

template <class Fn> Fn Orig(difftest::Context &ctx, uint32_t rva) { return ctx.Original<Fn>(rva); }

} // namespace

DIFF_TEST(cImageManipDX_CopySurfaceRect, 0x16b2a0)
{
    Fixture fx(ctx);
    using Fn = bool(__thiscall *)(void *, void *, const RECT *, void *, const RECT *, const uint32_t *);
    const Fn original = Orig<Fn>(ctx, 0x16b2a0);
    Seen seen;
    for (uint32_t seed = 1; seed <= 1500; ++seed) {
        Case(ctx, fx.caches, seen, "CopySurfaceRect", seed, [&](void *object, bool orig) {
            std::mt19937 gen(seed * 7 + 1);
            void *src = Surface(gen), *dst = Surface(gen);
            const RECT *sr = &gWorld.args.rect[0], *dr = &gWorld.args.rect[1];
            const uint32_t *key = &gWorld.args.key;
            return orig ? original(object, src, sr, dst, dr, key) : RawSlot<3, bool>(object, src, sr, dst, dr, key);
        });
    }
    CheckSeen(ctx, "CopySurfaceRect", seen, false, false);
}

DIFF_TEST(cImageManipDX_CopySurface, 0x16b4c0)
{
    Fixture fx(ctx);
    using Fn = bool(__thiscall *)(void *, void *, void *);
    const Fn original = Orig<Fn>(ctx, 0x16b4c0);
    Seen seen;
    for (uint32_t seed = 1; seed <= 800; ++seed) {
        Case(ctx, fx.caches, seen, "CopySurface", seed, [&](void *object, bool orig) {
            std::mt19937 gen(seed * 7 + 2);
            void *src = Surface(gen), *dst = Surface(gen);
            return orig ? original(object, src, dst) : RawSlot<4, bool>(object, src, dst);
        });
    }
    CheckSeen(ctx, "CopySurface", seen, false, false);
}

DIFF_TEST(cImageManipDX_LoadSurfaceFromMemory, 0x16b0d0)
{
    Fixture fx(ctx);
    using RectFn = bool(__thiscall *)(void *, const void *, void *, uint32_t, uint32_t, uint32_t, void *, const RECT *,
                                      const uint32_t *);
    using WholeFn = bool(__thiscall *)(void *, const void *, void *, uint32_t, uint32_t, uint32_t, void *);
    const RectFn rectOriginal = Orig<RectFn>(ctx, 0x16b0d0);
    const WholeFn wholeOriginal = Orig<WholeFn>(ctx, 0x16b060);
    Seen seen;
    for (uint32_t seed = 1; seed <= 1500; ++seed) {
        Case(ctx, fx.caches, seen, "LoadSurfaceFromMemory", seed, [&](void *object, bool orig) {
            std::mt19937 gen(seed * 7 + 3);
            void *dst = Surface(gen);
            void *format = &gFormat[gen() % 2];
            const uint32_t width = Pick(gen, {0, 1, 4, 16, 70}), height = Pick(gen, {0, 2, 8, 33}), pitch = gen() % 64;
            const void *bits = gWorld.args.bits;
            if (gen() % 3 == 0)
                return orig ? wholeOriginal(object, bits, format, width, height, pitch, dst)
                            : RawSlot<6, bool>(object, bits, format, width, height, pitch, dst);
            const RECT *dr = &gWorld.args.rect[1];
            const uint32_t *key = &gWorld.args.key;
            return orig ? rectOriginal(object, bits, format, width, height, pitch, dst, dr, key)
                        : RawSlot<5, bool>(object, bits, format, width, height, pitch, dst, dr, key);
        });
    }
    CheckSeen(ctx, "LoadSurfaceFromMemory", seen, false, false);
}

DIFF_TEST(cImageManipDX_CopyCubeFaces, 0x16b550)
{
    Fixture fx(ctx);
    using Fn = bool(__thiscall *)(void *, void *, void *, void *, void *, void *, void *, void *);
    const Fn original = Orig<Fn>(ctx, 0x16b550);
    Seen seen;
    for (uint32_t seed = 1; seed <= 2500; ++seed) {
        Case(ctx, fx.caches, seen, "CopyCubeFaces", seed, [&](void *object, bool orig) {
            std::mt19937 gen(seed * 7 + 4);
            void *f[6];
            for (unsigned i = 0; i < 6; ++i) f[i] = gen() % 4 ? &gTex[i] : Texture(gen);
            return orig ? original(object, f[0], f[1], f[2], f[3], f[4], f[5], &gCube)
                        : RawSlot<7, bool>(object, f[0], f[1], f[2], f[3], f[4], f[5], static_cast<void *>(&gCube));
        });
    }
    CheckSeen(ctx, "CopyCubeFaces", seen, true, true);
}

DIFF_TEST(cImageManipDX_CopyTexture, 0x16ba90)
{
    Fixture fx(ctx);
    using Fn = bool(__thiscall *)(void *, void *, void *);
    const Fn original = Orig<Fn>(ctx, 0x16ba90);
    Seen seen;
    for (uint32_t seed = 1; seed <= 2500; ++seed) {
        Case(ctx, fx.caches, seen, "CopyTexture", seed, [&](void *object, bool orig) {
            std::mt19937 gen(seed * 7 + 5);
            void *src = Texture(gen), *dst = Texture(gen);
            return orig ? original(object, src, dst) : RawSlot<8, bool>(object, src, dst);
        });
    }
    CheckSeen(ctx, "CopyTexture", seen, true, true);
}

DIFF_TEST(cImageManipDX_LoadSurfaceFromFileInMemory, 0x16af30)
{
    Fixture fx(ctx);
    using Fn = bool(__thiscall *)(void *, const void *, uint32_t, void *, const uint32_t *);
    const Fn original = Orig<Fn>(ctx, 0x16af30);
    Seen seen;
    for (uint32_t seed = 1; seed <= 600; ++seed) {
        Case(ctx, fx.caches, seen, "LoadSurfaceFromFileInMemory", seed, [&](void *object, bool orig) {
            std::mt19937 gen(seed * 7 + 6);
            void *dst = Surface(gen);
            const uint32_t size = gen() % 65;
            const uint32_t *key = &gWorld.args.key;
            return orig ? original(object, gWorld.args.file, size, dst, key)
                        : RawSlot<9, bool>(object, static_cast<const void *>(gWorld.args.file), size, dst, key);
        });
    }
    CheckSeen(ctx, "LoadSurfaceFromFileInMemory", seen, false, false);
}

DIFF_TEST(cImageManipDX_SaveSurfaceToFile, 0x16ac20)
{
    Fixture fx(ctx);
    using RectFn = bool(__thiscall *)(void *, const char *, void *, const RECT *, uint32_t);
    using WholeFn = bool(__thiscall *)(void *, const char *, void *, uint32_t);
    const RectFn rectOriginal = Orig<RectFn>(ctx, 0x16ac20);
    const WholeFn wholeOriginal = Orig<WholeFn>(ctx, 0x16aed0);
    Seen seen;
    for (uint32_t seed = 1; seed <= 1000; ++seed) {
        Case(ctx, fx.caches, seen, "SaveSurfaceToFile", seed, [&](void *object, bool orig) {
            std::mt19937 gen(seed * 7 + 7);
            void *surface = Surface(gen);
            const uint32_t format = Pick(gen, {0, 1, 2, 3, 0xffffffff});
            const char *path = gWorld.args.path;
            if (gen() % 3 == 0)
                return orig ? wholeOriginal(object, path, surface, format) : RawSlot<11, bool>(object, path, surface, format);
            const RECT *rect = gen() % 4 ? &gWorld.args.rect[0] : nullptr;
            return orig ? rectOriginal(object, path, surface, rect, format)
                        : RawSlot<10, bool>(object, path, surface, rect, format);
        });
    }
    CheckSeen(ctx, "SaveSurfaceToFile", seen, false, false);
}

DIFF_TEST(cImageManipDX_GenerateMipmaps, 0x16bf80)
{
    Fixture fx(ctx);
    using Fn = bool(__thiscall *)(void *, void *);
    const Fn original = Orig<Fn>(ctx, 0x16bf80);
    Seen seen;
    for (uint32_t seed = 1; seed <= 600; ++seed) {
        Case(ctx, fx.caches, seen, "GenerateMipmaps", seed, [&](void *object, bool orig) {
            std::mt19937 gen(seed * 7 + 8);
            void *texture = gen() % 5 == 0 ? static_cast<void *>(&gCube) : Texture(gen);
            return orig ? original(object, texture) : RawSlot<12, bool>(object, texture);
        });
    }
    CheckSeen(ctx, "GenerateMipmaps", seen, false, true, false); // it reports nothing (no LogD3DError in 0x16bf80)
}

DIFF_TEST(cImageManipDX_ComputeNormalMap, 0x16a980)
{
    Fixture fx(ctx);
    using Fn = bool(__thiscall *)(void *, void *, void *, float, bool);
    const Fn original = Orig<Fn>(ctx, 0x16a980);
    Seen seen;
    unsigned resampled = 0;
    for (uint32_t seed = 1; seed <= 2500; ++seed) {
        Case(ctx, fx.caches, seen, "ComputeNormalMap", seed, [&](void *object, bool orig) {
            std::mt19937 gen(seed * 7 + 9);
            void *src = Texture(gen), *dst = gen() % 4 == 0 ? src : Texture(gen);
            const float amplitude = Pick(gen, {0, 1, 3}) * 0.75f - 0.5f;
            const bool occlusion = gen() % 3 == 0;
            if (orig) {
                const bool r = original(object, src, dst, amplitude, occlusion);
                for (const drawfx::Call &c : drawfx::gLog) resampled += !c.iface && std::strcmp(c.note, "image.AddLevel") == 0;
                return r;
            }
            return RawSlot<13, bool>(object, src, dst, amplitude, occlusion);
        });
    }
    if (!resampled) ctx.Fail("ComputeNormalMap: no resampled image had more than one level");
    CheckSeen(ctx, "ComputeNormalMap", seen, true, true);
}

namespace {
// Is `p` the start of a block in use on the process heap (operator new's, through the CRT's malloc)?
bool HeapBusy(const void *p)
{
    HANDLE heap = GetProcessHeap();
    PROCESS_HEAP_ENTRY entry{};
    bool busy = false;
    HeapLock(heap);
    while (HeapWalk(heap, &entry))
        if (entry.lpData == p && (entry.wFlags & PROCESS_HEAP_ENTRY_BUSY)) {
            busy = true;
            break;
        }
    HeapUnlock(heap);
    return busy;
}
} // namespace

// The deleting destructor (slot 14): flags 0 and 1, on a block from operator new (the original's game delete is ours
// here, StandIns): the result, and whether the block is still in use after.
DIFF_TEST(cImageManipDX_DeletingDestructor, 0x16a950)
{
    StandIns stands;
    using Fn = void *(__thiscall *)(void *, uint32_t);
    const Fn original = ctx.Original<Fn>(0x16a950);
    for (uint32_t flags = 0; flags < 4; ++flags) {
        void *const a = ::operator new(4);
        *static_cast<void **>(a) = GameAddress<void>(kGameVTable);
        void *const ra = original(a, flags);
        const bool keptA = HeapBusy(a);
        void *const b = ::operator new(4);
        new (b) cImageManipDX;
        void *const rb = RawSlot<14, void *>(b, flags);
        const bool keptB = HeapBusy(b);
        ++ctx.cases;
        if ((ra == a) != (rb == b) || keptA != keptB || keptA != !(flags & 1))
            ctx.Fail("flags %u: result %d/%d, kept %d/%d", flags, ra == a, rb == b, keptA, keptB);
        if (keptA) ::operator delete(a);
        if (keptB) ::operator delete(b);
    }
}

// QueryInterface, AddRef / Release, and the getter's instance.
DIFF_TEST(cImageManipDX_Unknown, 0x16c120)
{
    using QI = bool(__thiscall *)(void *, uint32_t, void **);
    using Count = uint32_t(__thiscall *)(void *);
    const QI original = ctx.Original<QI>(0x16c120);
    const Count originalCount = ctx.Original<Count>(0x15e510);
    static void *gameObject[1];
    gameObject[0] = GameAddress<void>(kGameVTable);
    // The getter 0x16c0c0: the original with its static's guard (0x10ca270) set to "initialised" (INT_MIN: the
    // thread-safe static's fast path, so the constructor and the atexit registration 0xd90360 don't run) hands out
    // the static at 0xfecf98, every time; ours one object, every time, carrying our vtable.
    {
        using Getter = void *(__cdecl *)();
        const Getter getter = ctx.Original<Getter>(0x16c0c0);
        int32_t *const guard = ctx.image.At<int32_t>(0x10ca270);
        const int32_t savedGuard = *guard;
        *guard = INT_MIN;
        void *const a = getter(), *const b = getter();
        *guard = savedGuard;
        ++ctx.cases;
        if (a != b || a != static_cast<void *>(ctx.image.At<uint8_t>(0xfecf98))) ctx.Fail("original getter: %p %p", a, b);
    }
    cImageManipDX *ours = cImageManipDX::Instance();
    ++ctx.cases;
    if (ours != cImageManipDX::Instance() || *reinterpret_cast<void **>(ours) == gameObject[0])
        ctx.Fail("Instance: not one object with our vtable");
    const uint32_t iids[] = {0, 1, 2, 0x1b954bf1, 0x1b954bf0, 0xc55289ba, 0xffffffff};
    for (uint32_t iid : iids)
        for (int withOut = 0; withOut < 2; ++withOut) {
            void *outA = reinterpret_cast<void *>(0x1234), *outB = outA;
            const bool a = original(gameObject, iid, withOut ? &outA : nullptr);
            const bool b = RawSlot<0, bool>(ours, iid, withOut ? &outB : static_cast<void **>(nullptr));
            ++ctx.cases;
            const bool sameOut = (outA == gameObject) == (outB == ours) && (outA == gameObject || outA == outB);
            if (a != b || !sameOut) ctx.Fail("QueryInterface(%#x, %d): %d/%d", iid, withOut, a, b);
        }
    for (int slot = 1; slot <= 2; ++slot) {
        const uint32_t a = originalCount(gameObject);
        const uint32_t b = slot == 1 ? RawSlot<1, uint32_t>(ours) : RawSlot<2, uint32_t>(ours);
        ++ctx.cases;
        if (a != b) ctx.Fail("slot %d: %u vs %u", slot, a, b);
    }
}
