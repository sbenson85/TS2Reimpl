// Differential tests for cGZDrawFontRenderer as a whole class (src/gz/cGZDrawFontRenderer.h; lane NM, rendering R5).
//
// Each case builds one renderer per side: the original by the COM director's creator 0x8c32a0 (odd seeds) or operator
// new and the constructor 0x8c2440 (even seeds; the game's vtable), ours by cGZDrawFontRenderer::Create or the
// constructor's adapter (our vtable). Every slot is called through the object's vtable, so the original side runs the
// game's slot functions and ours our members. The world is drawfx fakes:
// - the COM director (GetDirectorFrameworkObject 0x1ae18d stood in on both sides, as tests/gz_gimex_factory.cpp's)
//   answers the buffer font renderer (a cIGZFontRenderer fake) and the atlas buffer (a cIGZBuffer fake; it fails when the
//   step says so, with a nonzero word whose low byte is 0);
// - the graphics system (cell 0x10f51a4) answers ConvertNativeToRGB (slot 36) with bytes of the value;
// - three targets: two answer the drawable-buffer query (cIGZDrawContextAsBuffer fakes, each locking its own draw
//   context fake: LockSurface's answer by session, GetOrigin the session's origin), one does not;
// - the atlas answers its size, Lock, IsReady, its native colour type (types 4 and 5 left out: the original divides by
//   zero for them), bits and bytes per pixel and a stride, and its pixels are a block both sides start from zeroed.
// A case is an AddRef, then 40 steps: draw sessions (BeginDraw, up to four DrawGlyphs of 0 to 159 glyphs from a pool of
// shapes with repeated characters, so that a pass meets its 64-glyph limit, and mostly EndDraw), ReleaseFont, SetAtlasSize, slots 3 and 9, QueryInterface,
// AddRef / Release and changes to the atlas's answers. After every call the log gets the result and, after each step,
// the object: the count, the atlas size, the atlas and the packing cursor, the places (font, character, x, y, sorted),
// the current font, the colour table (once there is an atlas), the buffer renderer, the context, the route byte and
// the origin (once a session began), plus the glyphs (DrawGlyphs moves them in place) and the atlas pixels. The hash
// maps' bucket arrays and load factor are left out: only the std helpers the class calls read them (find 0x15ac20,
// try_emplace 0x8c33c0, emplace 0x8c35a0, clear 0x8c3870, erase in ReleaseFont 0x8c25e0), so they decide nothing but
// iteration order, which no code of the class depends on. At the end the count is released to zero (the deleting
// destructor) and the log gets the fakes' reference counts and the side's heap balance.
#include "code_patch.h"
#include "difftest.h"
#include "drawfx.h"

#include "gz/cGZDrawContextAsBuffer.h"
#include "gz/cGZDrawFontRenderer.h"
#include "rz/StaticGetters.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace {

using drawfx::Ret;
constexpr drawfx::Slot U{0, Ret::Int, false, "", ""};
constexpr drawfx::Slot kQI{2, Ret::Int, true, "QueryInterface", "ui"};
constexpr drawfx::Slot kAddRef{0, Ret::Int, true, "AddRef", ""};
constexpr drawfx::Slot kRelease{0, Ret::Int, true, "Release", ""};

inline constexpr drawfx::Slot kComSlots[] = {kQI, kAddRef, kRelease, {3, Ret::Int, true, "GetClassObject", "uuW0"}};
inline constexpr drawfx::Iface kCom{"cIGZCOM", 4, kComSlots};

inline constexpr drawfx::Slot kGraphicsSlots[] = {
    kQI, kAddRef, kRelease, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    U, U, {4, Ret::Int, true, "ConvertNativeToRGB", "uiii"},
};
inline constexpr drawfx::Iface kGraphics{"cIGZGraphicSystem", 37, kGraphicsSlots};

// The glyphs as words (9 a glyph; the bitmap pointer is into the same block on both sides).
inline constexpr drawfx::Slot kRendererSlots[] = {
    kQI, kAddRef, kRelease, U, U,
    {3, Ret::Int, true, "BeginDraw", "uiu"},
    {3, Ret::Int, true, "EndDraw", "uiu"},
    {7, Ret::Int, true, "DrawGlyphs", "ui@3x9uuW1r"},
};
inline constexpr drawfx::Iface kRenderer{"cIGZFontRenderer", 8, kRendererSlots};

inline constexpr drawfx::Slot kDrawableSlots[] = {
    kQI, kAddRef, kRelease, U, U, {0, Ret::Int, true, "LockContext", ""}, {0, Ret::Int, true, "UnlockContext", ""},
};
inline constexpr drawfx::Iface kDrawable{"cIGZDrawContextAsBuffer", 7, kDrawableSlots};

inline constexpr drawfx::Slot kContextSlots[] = {
    kQI, kAddRef, kRelease,
    {0, Ret::Int, true, "PushTransform", ""},
    U,
    {1, Ret::Int, true, "IntersectClip", "r"},
    U, U, U,
    {0, Ret::Int, true, "PopTransform", ""},
    {0, Ret::Int, true, "PushState", ""},
    {0, Ret::Int, true, "PopState", ""},
    U, U,
    {1, Ret::Int, true, "GetOrigin", "i"},
    U, U, U, U, U, U,
    {1, Ret::Int, true, "SetColor", "u"},
    {1, Ret::Int, true, "vf22", "u"},
    U,
    {1, Ret::Int, true, "vf24", "b"},
    U, U, U, U,
    {0, Ret::Int, true, "ClearTexture", ""},
    {5, Ret::Int, true, "SetTexture", "iffff"},
    U, U, U, U, U,
    {1, Ret::Int, true, "FillRect", "r"},
    U, U, U, U, U, U, U, U, U, U,
    {2, Ret::Int, true, "LockSurface", "iu"},
    {0, Ret::Int, true, "UnlockSurface", ""},
};
inline constexpr drawfx::Iface kContext{"cIGZDrawContext", 49, kContextSlots};

inline constexpr drawfx::Slot kBufferSlots[] = {
    kQI, kAddRef, kRelease,
    {4, Ret::Int, true, "Init", "uuuu"},
    U, U,
    {1, Ret::Int, true, "Lock", "u"},
    {1, Ret::Int, true, "Unlock", "u"},
    U,
    {0, Ret::Int, true, "GetWidth", ""},
    {0, Ret::Int, true, "GetHeight", ""},
    U, U, U, U,
    {1, Ret::Int, true, "GetNativeColorType", "i"}, // the hidden result pointer
    {0, Ret::Int, true, "GetBitsPerPixel", ""},
    {0, Ret::Int, true, "GetBytesPerPixel", ""},
    {2, Ret::Int, true, "Fill", "ui"},
    U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    {0, Ret::Int, true, "GetColorSurfaceBits", ""},
    {0, Ret::Int, true, "GetColorSurfaceStride", ""},
    U, U, U,
    {0, Ret::Int, true, "IsReady", ""},
};
inline constexpr drawfx::Iface kBuffer{"cIGZBuffer", 40, kBufferSlots};

constexpr uint32_t kGraphicsCell = 0x10f51a4;
constexpr uint32_t kCreator = 0x8c32a0, kConstructor = 0x8c2440, kDestructor = 0x8c2530;
constexpr uint32_t kGameVTable = 0xe4f09c;
constexpr uint32_t kObjectTag = 0x0c000000, kAtlasTag = 0x0d000000, kPixelsTag = 0x0e000000;
constexpr uint32_t kAtlasBytes = 0x10000;

drawfx::Fake gCom, gGraphics, gRenderer, gAtlas, gTargets[3], gDrawables[2], gContexts[2];
bool gAtlasFails = false;

// The world's answers that a step may change.
struct Case {
    int32_t originX, originY;
    uint32_t atlasWidth, atlasHeight;
};
Case gCase;

alignas(16) uint8_t gAtlasPixels[kAtlasBytes];
alignas(16) uint8_t gBitmaps[0x4000];
cGZFontGlyph gShapes[24];
cGZFontGlyph gGlyphs[160];

uint32_t ComGetClassObject(drawfx::Fake &, const uint32_t *args)
{
    void **const out = reinterpret_cast<void **>(uintptr_t(args[2]));
    if (args[0] == cGZDrawFontRenderer::kBufferRenderer && args[1] == cIGZFontRenderer::kIid) {
        *out = &gRenderer;
        ++gRenderer.refs;
        return 0xab01;
    }
    if (args[0] == cGZDrawFontRenderer::kBufferClass && args[1] == cIGZBuffer::kIid) {
        if (gAtlasFails) return 0xab00; // nonzero, low byte 0: a failure to the callers' byte test
        *out = &gAtlas;
        ++gAtlas.refs;
        return 0xab01;
    }
    return 0;
}
uint32_t GraphicsToRGB(drawfx::Fake &, const uint32_t *args)
{
    *reinterpret_cast<uint8_t *>(uintptr_t(args[1])) = uint8_t(args[0] >> 16);
    *reinterpret_cast<uint8_t *>(uintptr_t(args[2])) = uint8_t(args[0] >> 8);
    *reinterpret_cast<uint8_t *>(uintptr_t(args[3])) = uint8_t(args[0] ^ 0x5a);
    return 1;
}
uint32_t ContextGetOrigin(drawfx::Fake &, const uint32_t *args)
{
    const cGZPoint origin{gCase.originX, gCase.originY};
    std::memcpy(reinterpret_cast<void *>(uintptr_t(args[0])), &origin, sizeof(origin));
    return args[0];
}
uint32_t AtlasNativeColorType(drawfx::Fake &self, const uint32_t *args)
{
    const cGZColorType type{self.value[0], 16};
    std::memcpy(reinterpret_cast<void *>(uintptr_t(args[0])), &type, sizeof(type));
    return args[0];
}

void BuildWorld()
{
    gCom.Reset<kCom>(601);
    gCom.On(3, &ComGetClassObject);
    gGraphics.Reset<kGraphics>(602);
    gGraphics.On(36, &GraphicsToRGB);
    gRenderer.Reset<kRenderer>(603);
    gRenderer.Returns(5, 1u);
    gRenderer.Returns(6, 1u);
    gRenderer.Returns(7, 1u);
    gAtlas.Reset<kBuffer>(604);
    gAtlas.Returns(3, 1u);
    gAtlas.Returns(6, 1u);
    gAtlas.Returns(7, 1u);
    gAtlas.Returns(9, gCase.atlasWidth);
    gAtlas.Returns(10, gCase.atlasHeight);
    gAtlas.value[0] = 7;
    gAtlas.On(15, &AtlasNativeColorType);
    gAtlas.Returns(16, 16u);
    gAtlas.Returns(17, 2u);
    gAtlas.Returns(34, gAtlasPixels);
    gAtlas.Returns(35, 0x100u);
    gAtlas.Returns(39, 1u);
    for (unsigned i = 0; i < 2; ++i) {
        gContexts[i].Reset<kContext>(620 + i);
        gContexts[i].On(14, &ContextGetOrigin);
        gDrawables[i].Reset<kDrawable>(610 + i);
        gDrawables[i].Returns(5, &gContexts[i]);
    }
    for (unsigned i = 0; i < 3; ++i) gTargets[i].Reset<kBuffer>(630 + i);
    gTargets[0].AddFace(cIGZDrawContextAsBuffer::kIid, gDrawables[0]);
    gTargets[1].AddFace(cIGZDrawContextAsBuffer::kIid, gDrawables[1]);
}

void *__cdecl FakeDirectorObject() { return &gCom; }

void NewCase(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 29);
    const uint32_t sides[] = {16, 24, 40, 64, 80};
    gCase.atlasWidth = sides[rng() % 5];
    gCase.atlasHeight = sides[rng() % 5];
    gCase.originX = gCase.originY = 0;
    for (auto &b : gBitmaps) b = uint8_t(rng());
    for (unsigned i = 0; i < 24; ++i) {
        cGZFontGlyph &g = gShapes[i];
        g.width = int32_t(rng() % 19);
        g.height = int32_t(rng() % 19);
        g.pitch = g.width + int32_t(rng() % 4);
        g.pixels = gBitmaps + rng() % (sizeof gBitmaps - 0x200);
        g.code = rng() % 20 + (rng() % 8 ? 0x41 : 0);
    }
    std::memset(gAtlasPixels, 0, sizeof gAtlasPixels);
    gAtlasFails = false;
}

// ---- calls through the object's vtable ----

template <class R, class... A> R Call(void *object, unsigned slot, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*static_cast<void ***>(object))[slot])(object, args...);
}

uint32_t Word(const void *object, uint32_t offset)
{
    uint32_t v;
    std::memcpy(&v, static_cast<const uint8_t *>(object) + offset, 4);
    return v;
}
const uint32_t *Node(uint32_t p) { return reinterpret_cast<const uint32_t *>(uintptr_t(p)); }

uint32_t Hash(const void *p, size_t n, uint32_t h = 2166136261u)
{
    for (size_t i = 0; i < n; ++i) h = (h ^ static_cast<const uint8_t *>(p)[i]) * 16777619u;
    return h;
}

// The object but its vtable and heap pointers (see the file comment).
void NoteObject(const void *object, bool begun, bool currentLive)
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = "object";
    // By kind (lane RK): the atlas (+0x10) is a pointer; the count, sizes and packing are data, as they are.
    for (uint32_t off = 4; off < 0x20; off += 4)
        c.words.push_back(off == 0x10 ? drawfx::Ptr(Word(object, off)) : Word(object, off));
    const uint32_t head = Word(object, 0x24);
    c.words.push_back(Word(object, 0x28)); // the font count
    std::vector<std::array<uint32_t, 4>> places;
    unsigned fonts = 0;
    const uint32_t current = Word(object, 0x40);
    uint32_t currentFont = current == 0 ? 0 : current == head ? 0xe0d0e0d0u : 0x57a1e000u; // stale until found
    for (uint32_t node = Node(head)[0]; node != head && fonts < 4096; node = Node(node)[0], ++fonts) {
        const uint32_t font = Node(node)[2];
        if (node == current) currentFont = font;
        const uint32_t innerHead = Node(node)[4]; // the inner map at +0xc: its list head at +4
        places.push_back({font, 0xffffffffu, Node(node)[5], 0}); // the font's own entry: its place count
        unsigned n = 0;
        for (uint32_t p = Node(innerHead)[0]; p != innerHead && n < 4096; p = Node(p)[0], ++n)
            places.push_back({font, Node(p)[2], Node(p)[3], Node(p)[4]});
    }
    std::sort(places.begin(), places.end());
    for (const auto &p : places) c.words.insert(c.words.end(), p.begin(), p.end());
    // The current font, unless ReleaseFont or SetAtlasSize may have freed its node since the last atlas-route session
    // (a stale iterator neither side reads again before BeginDraw sets it; its address may be reused).
    if (currentLive || current == head || current == 0) c.words.push_back(currentFont);
    if (Word(object, 0x10)) c.words.push_back(Hash(static_cast<const uint8_t *>(object) + 0x44, 0x400));
    c.words.push_back(drawfx::Ptr(Word(object, 0x444)));
    c.words.push_back(drawfx::Ptr(Word(object, 0x448)));
    c.words.push_back(Word(object, 0x44c) & 0xff);
    if (begun) {
        c.words.push_back(Word(object, 0x450));
        c.words.push_back(Word(object, 0x454));
    }
    c.words.push_back(Hash(gAtlasPixels, sizeof gAtlasPixels));
    drawfx::gLog.push_back(std::move(c));
}

void *OriginalCreate(bool director)
{
    if (director) return reinterpret_cast<void *(__cdecl *)()>(GameAddress<void>(kCreator))();
    void *const block = ::operator new(cGZDrawFontRenderer::kSize);
    return reinterpret_cast<void *(__thiscall *)(void *)>(GameAddress<void>(kConstructor))(block);
}
void *OurCreate(bool director)
{
    if (director) return static_cast<cIGZFontRenderer *>(cGZDrawFontRenderer::Create());
    return static_cast<cIGZFontRenderer *>(
        cGZDrawFontRendererAdapters::Construct(::operator new(cGZDrawFontRenderer::kSize), nullptr));
}

const uint32_t kFonts[] = {1, 2, 3, 0x80000000u};

// DrawGlyphs' glyphs: `count` from the shapes, at random places.
void FillGlyphs(std::mt19937 &rng, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        gGlyphs[i] = gShapes[rng() % 24];
        cGZRect &r = gGlyphs[i].rect;
        r.left = int32_t(rng() % 400) - 50;
        r.top = int32_t(rng() % 400) - 50;
        r.right = r.left + gGlyphs[i].width;
        r.bottom = r.top + gGlyphs[i].height;
    }
}

// One draw session: BeginDraw, DrawGlyphs calls, mostly EndDraw.
void Session(void *object, std::mt19937 &rng)
{
    const uint32_t t = rng() % 3;
    void *const target = &gTargets[t];
    gContexts[0].Returns(47, rng() % 2);
    gContexts[1].Returns(47, rng() % 2);
    gCase.originX = int32_t(rng() % 200) - 100;
    gCase.originY = int32_t(rng() % 200) - 100;
    gRenderer.Returns(5, rng() % 4 ? 1u : 0x100u);
    const uint32_t font = kFonts[rng() % 4];
    drawfx::Note("begin", Call<uint32_t>(object, 5, font, target, rng() % 4) & 0xff);
    const uint32_t draws = rng() % 5;
    for (uint32_t k = 0; k < draws; ++k) {
        const uint32_t count = rng() % 6 ? rng() % 12 : rng() % 160;
        FillGlyphs(rng, count);
        const uint32_t colour = rng();
        cGZRect clip{int32_t(rng() % 100) - 20, int32_t(rng() % 100) - 20, int32_t(rng() % 300), int32_t(rng() % 300)};
        const cGZRect clipBefore = clip;
        gRenderer.Returns(7, rng() % 4 ? 1u : 0x100u);
        const uint32_t drawFont = rng() % 4 ? font : kFonts[rng() % 4];
        const uint32_t answer = Call<uint32_t>(object, 7, drawFont, target, gGlyphs, count, rng() % 4, &colour,
                                               rng() % 3 ? &clip : nullptr) & 0xff;
        drawfx::Note("draw", {answer, Hash(gGlyphs, sizeof(cGZFontGlyph) * count),
                              uint32_t(std::memcmp(&clip, &clipBefore, sizeof clip) != 0)});
    }
    // The atlas route's EndDraw needs a drawable target (the original unlocks through the query's answer unchecked):
    // the session's own.
    if (rng() % 8) {
        gRenderer.Returns(6, rng() % 4 ? 1u : 0x100u);
        drawfx::Note("end", Call<uint32_t>(object, 6, font, target, rng() % 4) & 0xff);
    }
}

// One side: the object made by the side's creator, then the case's steps.
void RunSide(bool ours, uint32_t seed)
{
    drawfx::Cells cells;
    cells.Set(kGraphicsCell, &gGraphics);
    // The log's growth is the test's own: an earlier test may leave its buffer at any capacity (drawfx::Side clears
    // it, keeping the capacity), and the side would count the log's reallocation or not by the suite's order.
    drawfx::gLog.reserve(1 << 14);
    std::mt19937 rng(seed);
    difftest::AllocationCounter heap;

    void *const object = ours ? OurCreate(seed % 2 != 0) : OriginalCreate(seed % 2 != 0);
    {
        difftest::NotCounted bookkeeping;
        drawfx::AddRange(object, cGZDrawFontRenderer::kSize, kObjectTag);
        drawfx::AddRange(gAtlasPixels, sizeof gAtlasPixels, kAtlasTag);
        drawfx::AddRange(gBitmaps, sizeof gBitmaps, kPixelsTag);
    }
    bool begun = false, currentLive = true;
    NoteObject(object, begun, currentLive);
    drawfx::Note("addref", Call<uint32_t>(object, 1));

    for (int step = 0; step < 40; ++step) {
        const uint32_t pick = rng() % 16;
        drawfx::Note("pick", pick);
        if (pick < 7) {
            Session(object, rng);
            begun = true;
            if ((Word(object, 0x44c) & 0xff) == 0) currentLive = true;
        } else if (pick < 9) {
            const uint32_t font = rng() % 8 ? kFonts[rng() % 4] : rng();
            drawfx::Note("releaseFont", Call<uint32_t>(object, 4, font) & 0xff);
            currentLive = false;
        } else if (pick < 10) {
            gAtlasFails = rng() % 4 == 0;
            const uint32_t sizes[] = {0, 16, 64, 256, 1000};
            Call<void>(object, 10, sizes[rng() % 5], sizes[rng() % 5]); // no value (see the class)
            gAtlasFails = false;
            currentLive = false;
        } else if (pick < 11) {
            const uint32_t w = rng();
            drawfx::Note("vf3", Call<uint32_t>(object, 3, w) & 0xff);
            drawfx::Note("vf9", Call<uint32_t>(object, 9, w, w ^ 1, w ^ 2, w ^ 3) & 0xff);
        } else if (pick < 12) {
            const uint32_t iids[] = {1, cIGZFontRenderer::kIid, cIGZBuffer::kIid, 0x12345678};
            void *out = reinterpret_cast<void *>(uintptr_t(0xdddd0006));
            const uint32_t answer = Call<uint32_t>(object, 0, iids[rng() % 4], &out) & 0xff;
            drawfx::Note("query", {answer, drawfx::Norm(drawfx::P(out))});
            if (answer) drawfx::Note("queryRelease", Call<uint32_t>(out, 2));
        } else if (pick < 13) {
            if (rng() % 2 || Word(object, 4) < 2) drawfx::Note("addref", Call<uint32_t>(object, 1));
            else drawfx::Note("release", Call<uint32_t>(object, 2));
        } else {
            // The atlas's answers change (its size stays the case's: the atlas block is sized for it).
            switch (rng() % 6) {
            case 0: gAtlas.Returns(6, rng() % 4 ? 1u : 0x100u); break;
            case 1: gAtlas.Returns(39, rng() % 4 ? 1u : 0u); break;
            case 2: {
                const uint32_t bits[] = {8, 16, 24, 32, 15, 12};
                gAtlas.Returns(16, bits[rng() % 6]);
                break;
            }
            case 3: gAtlas.Returns(17, 1 + rng() % 4); break;
            case 4: {
                const uint32_t types[] = {6, 7, 8, 9, 3, 0};
                gAtlas.value[0] = types[rng() % 6];
                break;
            }
            default: gAtlas.Returns(35, 0xc0 + rng() % 0x80); break;
            }
        }
        NoteObject(object, begun, currentLive);
    }

    const uint32_t count = Word(object, 4);
    drawfx::Note("count", count);
    for (uint32_t i = 0; i < count && i < 64; ++i) drawfx::Note("release", Call<uint32_t>(object, 2));
    heap.Stop();
    {
        drawfx::Call c;
        c.note = "refs";
        for (const drawfx::Fake *f : {&gCom, &gGraphics, &gRenderer, &gAtlas, &gTargets[0], &gTargets[1], &gTargets[2],
                                      &gDrawables[0], &gDrawables[1], &gContexts[0], &gContexts[1]})
            c.words.push_back(uint32_t(f->refs));
        drawfx::gLog.push_back(std::move(c));
    }
    drawfx::Note("heap", {uint32_t(heap.Blocks()), uint32_t(heap.Bytes())});
}

void RunCase(difftest::Context &ctx, uint32_t seed, bool &ok)
{
    NewCase(seed);
    ctx.Mark("orig");
    auto a = drawfx::Side(&BuildWorld, [&] { RunSide(false, seed); });
    NewCase(seed);
    ctx.Mark("ours");
    auto b = drawfx::Side(&BuildWorld, [&] { RunSide(true, seed); });
    ok = drawfx::Same(ctx, a.first, b.first, "draw font renderer", seed, a.second);
}

} // namespace

DIFF_TEST(GZDrawFontRenderer_Slots, 0x8c2840)
{
    // Every slot (QueryInterface 0x8c2570, AddRef 0x8c25a0, Release 0x8c25b0, vf3 0x8c25d0, ReleaseFont 0x8c25e0,
    // BeginDraw 0x8c2670, EndDraw 0x8c27e0, DrawGlyphs 0x8c2840, the deleting destructor 0x8c3810, vf9 0x8c2ce0,
    // SetAtlasSize 0x8c2cf0), the director's creator 0x8c32a0 (odd seeds) or the constructor 0x8c2440 (even seeds), and
    // what they call: BuildAtlas 0x8c2d10, Reset 0x8c2f10, AddGlyph 0x8c2f80 and the maps' helpers.
    shape::StandIn director{0x1ae18d, &GetDirectorFrameworkObject, &FakeDirectorObject};
    char seedText[16] = {};
    const uint32_t only = GetEnvironmentVariableA("TS2_TEST_SEED", seedText, sizeof(seedText)) ? uint32_t(atoi(seedText)) : 0;
    for (uint32_t seed = only ? only : 1; seed <= (only ? only : 1500); ++seed) {
        bool ok = true;
        RunCase(ctx, seed, ok);
        if (!ok) break;
    }
}

DIFF_TEST(GZDrawFontRenderer_Construct, 0x8c2440)
{
    // The constructor on a block of seeded bytes: every byte it writes and every byte it leaves (the colour table and
    // the origin), but the vtable word (the game's on the original side, ours on ours) and the map's heap pointers (its
    // list head +0x24 and bucket array +0x2c..+0x37: each side's own allocations). Then the destructors (the original's
    // 0x8c2530) release the buffer renderer and free the map: the fake's count and the heap balance are compared.
    shape::StandIn director{0x1ae18d, &GetDirectorFrameworkObject, &FakeDirectorObject};
    const auto original = ctx.Original<void *(__thiscall *)(void *)>(kConstructor);
    static uint8_t a[cGZDrawFontRenderer::kSize], b[cGZDrawFontRenderer::kSize];
    for (uint32_t seed = 0; seed < 100; ++seed) {
        drawfx::ResetAll();
        BuildWorld();
        std::mt19937 rng(seed);
        for (auto &x : a) x = uint8_t(rng());
        std::memcpy(b, a, sizeof(a));
        drawfx::gLog.reserve(16); // the fakes' log of GetClassObject (its entries' words freed before the count)
        const int32_t refs0 = gRenderer.refs;
        difftest::AllocationCounter heap;
        void *const ra = original(a);
        const int32_t refsA = gRenderer.refs - refs0;
        void *const rb = cGZDrawFontRendererAdapters::Construct(b, nullptr);
        ++ctx.cases;
        if (ra != a || rb != static_cast<void *>(b)) ctx.Fail("seed %u: the constructor returned another address", seed);
        if (Word(a, 0) != drawfx::P(GameAddress<void>(kGameVTable))) ctx.Fail("seed %u: the original's vtable", seed);
        if (Word(b, 0) == drawfx::P(GameAddress<void>(kGameVTable))) ctx.Fail("seed %u: ours has the game's vtable", seed);
        if (refsA != 1 || gRenderer.refs - refs0 != 2)
            ctx.Fail("seed %u: the buffer renderer's count %d %d", seed, refsA, gRenderer.refs - refs0);
        const uint32_t headA = Word(a, 0x24), headB = Word(b, 0x24);
        if (Node(headA)[0] != headA || Node(headA)[1] != headA || Node(headB)[0] != headB || Node(headB)[1] != headB)
            ctx.Fail("seed %u: the map's list head does not point at itself", seed);
        for (uint32_t i = 0; i < 16; ++i)
            if (Node(Word(a, 0x2c))[i] != headA || Node(Word(b, 0x2c))[i] != headB)
                ctx.Fail("seed %u: bucket word %u is not the list head", seed, i);
        if (Word(a, 0x30) - Word(a, 0x2c) != Word(b, 0x30) - Word(b, 0x2c) ||
            Word(a, 0x34) - Word(a, 0x2c) != Word(b, 0x34) - Word(b, 0x2c))
            ctx.Fail("seed %u: the bucket arrays' sizes differ", seed);
        for (uint32_t off = 4; off < sizeof(a); off += 4) {
            if (off == 0x24 || (off >= 0x2c && off < 0x38)) continue;
            if (Word(a, off) != Word(b, off)) {
                ctx.Fail("seed %u: word +%#x orig %#x ours %#x", seed, off, Word(a, off), Word(b, off));
                break;
            }
        }
        ctx.Original<void(__thiscall *)(void *)>(kDestructor)(a);
        const int32_t refsAfterA = gRenderer.refs - refs0;
        cGZDrawFontRendererAdapters::Destruct(reinterpret_cast<cGZDrawFontRenderer *>(b), nullptr);
        {
            difftest::NotCounted bookkeeping; // drawfx's log is never counted, its words' frees neither
            for (drawfx::Call &c : drawfx::gLog) std::vector<uint32_t>().swap(c.words);
        }
        heap.Stop();
        if (refsAfterA != 1 || gRenderer.refs != refs0)
            ctx.Fail("seed %u: the destructors' releases %d %d", seed, refsAfterA, gRenderer.refs - refs0);
        if (heap.Blocks() != 0) ctx.Fail("seed %u: the constructors and destructors leave %d blocks", seed, int(heap.Blocks()));
        drawfx::ResetAll();
    }
}
