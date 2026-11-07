// Differential tests for cGZDrawContextAsBuffer as a whole class (src/gz/cGZDrawContextAsBuffer.h; lane NM, rendering R5).
//
// Each case builds one object per side from the same pool block: the original by the COM director's creator 0x8c22a0
// (odd seeds) or the pool allocator 0x8c1460 and the constructor 0x8c13a0 (the game's vtables), ours by
// cGZDrawContextAsBuffer::Create or `new cGZDrawContextAsBuffer` (our vtables); every slot is called
// through the object's own face vtables (+0 cIGZBuffer, +4 cIGZBufferShader, +8 cRZRefCount, +0x10
// cIGZDrawContextAsBuffer), so the original side runs the game's slot functions and ours our members. The pool
// (cRZFastFixedAllocator2<0x58> at 0x105f820) is a test page of seeded bytes, the same on both sides, so the fields the
// constructor leaves (bounds, format, the surface's other words, colour, rebatch flag) start alike. The world is drawfx
// fakes:
// - the draw context (cIGZDrawContext, 49 slots) logs every call (rectangles and points by content); HasTarget and
//   InBatch answer from what SetTarget, BeginBatch and EndBatch did; GetOrigin writes the case's point; LockSurface writes
//   the case's surface (over the case's pixel memory) and answers the case's bool;
// - two targets (cIGZBuffer) answer the case's bounds, colour type, bytes per pixel, transparent colour, change counter
//   and hardware cache, and (by case) a cIGZBufferShader face whose GetColor answers the case's colour or fails;
// - a Blt source (cIGZBuffer) with (by case) a shader face, and a hardware cache (cIGZUnknown).
// A case is Init, then 60 random slot calls with random arguments (Init again on either target among them). After every
// call the log gets the result, the out-parameters and every word of the object but its four vtable words (pointers
// normalised: fakes by id, the object and the pixel memory by offset). At the end the count at +0xc is released one by
// one (the last Release runs the deleting destructor 0x8c2350 back to the pool) and the log gets the pool's page header,
// the fakes' reference counts and the side's heap balance (difftest::AllocationCounter).
#include "difftest.h"
#include "drawfx.h"

#include "gz/cGZDrawContextAsBuffer.h"
#include "rz/RZFixedAllocator.h"

#include <Windows.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <random>

namespace {

using drawfx::Ret;
constexpr drawfx::Slot U{0, Ret::Int, false, "", ""};
constexpr drawfx::Slot S0{0, Ret::Int, true, "", ""};
constexpr drawfx::Slot kQI{2, Ret::Int, true, "QueryInterface", "ui"};
constexpr drawfx::Slot kAddRef{0, Ret::Int, true, "AddRef", ""};
constexpr drawfx::Slot kRelease{0, Ret::Int, true, "Release", ""};

inline constexpr drawfx::Slot kContextSlots[] = {
    kQI, kAddRef, kRelease,
    {0, Ret::Int, true, "PushTransform", ""},
    U,
    {1, Ret::Int, true, "IntersectClip", "r"},
    U, U, U,
    {0, Ret::Int, true, "PopTransform", ""},
    U, U, U, U,
    {1, Ret::Int, true, "GetOrigin", "i"},
    U,
    {0, Ret::Int, true, "HasTarget", ""},
    {1, Ret::Int, true, "SetTarget", "i"},
    U, U,
    {2, Ret::Int, true, "SetColorAt", "uu"},
    {1, Ret::Int, true, "SetColor", "u"},
    U, U, U, U,
    {1, Ret::Int, true, "SetLineWidth", "f"},
    U, U, U, U, U, U,
    {0, Ret::Int, true, "FillClip", ""},
    {2, Ret::Int, true, "DrawPoint", "uu"},
    {4, Ret::Int, true, "DrawLine", "uuuu"},
    {1, Ret::Int, true, "FillRect", "r"},
    {4, Ret::Int, true, "vf37", "uuuu"},
    U,
    {3, Ret::Int, true, "DrawTextureStretched", "irr"},
    U,
    {0, Ret::Int, true, "vf41", ""},
    {0, Ret::Int, true, "InBatch", ""},
    {0, Ret::Int, true, "BeginBatch", ""},
    {0, Ret::Int, true, "EndBatch", ""},
    {3, Ret::Int, true, "DrawPrimitives", "uF4u"},
    U,
    {2, Ret::Int, true, "LockSurface", "iu"},
    {0, Ret::Int, true, "UnlockSurface", ""},
};
inline constexpr drawfx::Iface kContext{"cIGZDrawContext", 49, kContextSlots};

inline constexpr drawfx::Slot kBufferSlots[] = {
    kQI, kAddRef, kRelease, U, U, U, U, U, U, U, U, U,
    {0, Ret::Int, true, "GetBoundsPtr", ""},
    U, U,
    {1, Ret::Int, true, "GetNativeColorType", "i"}, // the hidden result pointer
    U,
    {0, Ret::Int, true, "GetBytesPerPixel", ""},
    U, U, U, U, U,
    {1, Ret::Int, true, "SetTransparency", "u"},
    U, U,
    {1, Ret::Int, true, "GetTransparentColor", "i"},
    {0, Ret::Int, true, "RemoveTransparency", ""},
    U, U, U, U, U, U, U, U,
    {0, Ret::Int, true, "GetChangeCounter", ""},
    {0, Ret::Int, true, "GetHardwareCache", ""},
    {1, Ret::Int, true, "SetHardwareCache", "i"},
    U,
};
inline constexpr drawfx::Iface kBuffer{"cIGZBuffer", 40, kBufferSlots};

inline constexpr drawfx::Slot kShaderSlots[] = {
    kQI, kAddRef, kRelease, {4, Ret::Int, true, "SetColor", "bbbb"}, {4, Ret::Int, true, "GetColor", "iiii"},
};
inline constexpr drawfx::Iface kShader{"cIGZBufferShader", 5, kShaderSlots};

inline constexpr drawfx::Slot kUnknownSlots[] = {kQI, kAddRef, kRelease};
inline constexpr drawfx::Iface kUnknown{"cIGZUnknown", 3, kUnknownSlots};

constexpr uint32_t kAllocate = 0x8c1460, kConstructor = 0x8c13a0, kDirectorCreate = 0x8c22a0, kDrawEllipse = 0x8c1e50;
constexpr uint32_t kGameVTables[4] = {0xe4ef98, 0xe4f03c, 0xe4f054, 0xe4f06c};
constexpr uint32_t kFaceOffsets[4] = {0, 4, 8, 0x10};
constexpr uint32_t kObjectTag = 0x0c000000, kPixelTag = 0x0d000000, kPageTag = 0x0e000000;

// ---- the case ----

constexpr uint32_t kMaxSide = 64, kStride = kMaxSide * 4 + 16, kPixelBytes = 0x8000;
alignas(16) uint8_t gPixels[kPixelBytes];

struct Case {
    cGZRect bounds[2];       // the targets' (GetBoundsPtr)
    uint32_t format[2];      // the targets' native colour type
    uint32_t bits[2];
    uint32_t bytes[2];       // GetBytesPerPixel
    uint32_t transparent[2]; // GetTransparentColor's colour
    uint32_t hasTransparent[2];
    uint32_t counter[2];
    uint32_t setCache[2];
    bool shaderFace[3];      // the targets' and the source's
    uint32_t shaderColor[3]; // r | g << 8 | b << 16 | a << 24
    uint32_t shaderOk[3];
    cGZDrawSurface surface;  // LockSurface's
    uint32_t lockOk;
    cGZPoint origin;
};
Case gCase;

drawfx::Fake gContext, gSource, gCache;
drawfx::Fake gTargets[2], gShaders[3];

// The context's state as the fakes' handlers see it (value[0]: has a target, value[1]: in a batch).
uint32_t HasTarget(drawfx::Fake &self, const uint32_t *) { return self.value[0]; }
uint32_t SetTarget(drawfx::Fake &self, const uint32_t *args)
{
    self.value[0] = args[0] != 0;
    return 0;
}
uint32_t InBatch(drawfx::Fake &self, const uint32_t *) { return self.value[1]; }
uint32_t BeginBatch(drawfx::Fake &self, const uint32_t *)
{
    self.value[1] = 1;
    return 0;
}
uint32_t EndBatch(drawfx::Fake &self, const uint32_t *)
{
    self.value[1] = 0;
    return 0;
}
uint32_t GetOrigin(drawfx::Fake &, const uint32_t *args)
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(args[0])), &gCase.origin, sizeof(cGZPoint));
    return 0;
}
uint32_t LockSurface(drawfx::Fake &, const uint32_t *args)
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(args[0])), &gCase.surface, sizeof(cGZDrawSurface));
    return gCase.lockOk;
}

// The targets' answers by their index (value[0]).
uint32_t GetNativeColorType(drawfx::Fake &self, const uint32_t *args)
{
    const cGZColorType type{gCase.format[self.value[0]], gCase.bits[self.value[0]]};
    std::memcpy(reinterpret_cast<void *>(uintptr_t(args[0])), &type, sizeof(type));
    return args[0];
}
uint32_t GetTransparentColor(drawfx::Fake &self, const uint32_t *args)
{
    if (args[0]) *reinterpret_cast<uint32_t *>(uintptr_t(args[0])) = gCase.transparent[self.value[0]];
    return gCase.hasTransparent[self.value[0]];
}
// The shaders' GetColor (value[0]: the shader's index).
uint32_t ShaderGetColor(drawfx::Fake &self, const uint32_t *args)
{
    const uint32_t i = self.value[0];
    for (unsigned k = 0; k < 4; ++k)
        *reinterpret_cast<uint8_t *>(uintptr_t(args[k])) = uint8_t(gCase.shaderColor[i] >> (8 * k));
    return gCase.shaderOk[i];
}

void BuildWorld()
{
    gContext.Reset<kContext>(401);
    gContext.On(14, &GetOrigin);
    gContext.On(16, &HasTarget);
    gContext.On(17, &SetTarget);
    gContext.On(42, &InBatch);
    gContext.On(43, &BeginBatch);
    gContext.On(44, &EndBatch);
    gContext.On(47, &LockSurface);
    gCache.Reset<kUnknown>(402);
    for (unsigned i = 0; i < 3; ++i) {
        gShaders[i].Reset<kShader>(420 + i);
        gShaders[i].value[0] = i;
        gShaders[i].On(4, &ShaderGetColor);
    }
    for (unsigned i = 0; i < 2; ++i) {
        drawfx::Fake &t = gTargets[i];
        t.Reset<kBuffer>(410 + i);
        t.value[0] = i;
        t.Returns(12, &gCase.bounds[i]);
        t.On(15, &GetNativeColorType);
        t.Returns(17, gCase.bytes[i]);
        t.On(26, &GetTransparentColor);
        t.Returns(36, gCase.counter[i]);
        t.Returns(37, &gCache);
        t.Returns(38, gCase.setCache[i]);
        if (gCase.shaderFace[i]) t.AddFace(cIGZBufferShader::kIid, gShaders[i]);
    }
    gSource.Reset<kBuffer>(403);
    if (gCase.shaderFace[2]) gSource.AddFace(cIGZBufferShader::kIid, gShaders[2]);
}

// ---- the pool ----

constexpr uint32_t kPageSize = 0x2000;
constexpr uint32_t kBlocks = (kPageSize - 16) / cGZDrawContextAsBuffer::kSize;
uint8_t *gPage;

// A page of seeded bytes holding `freeBlocks` free blocks (in a seeded order) out of kBlocks: the pool's current page.
// The used count stays far from 0 and from the full mark, so neither the allocation nor the free reaches the pool's
// page bookkeeping (TakeFreePage, ReleaseEmptyPage, ReturnFullPage: game code not under test).
void BuildPool(uint32_t seed)
{
    if (!gPage) gPage = static_cast<uint8_t *>(VirtualAlloc(nullptr, kPageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    std::mt19937 r(seed * 0x9e3779b1u + 11);
    for (uint32_t i = 0; i < kPageSize; ++i) gPage[i] = uint8_t(r());
    auto *page = reinterpret_cast<RZFixedPage *>(gPage);
    const uint32_t freeBlocks = 3 + r() % 5;
    uint8_t *first = nullptr;
    for (uint32_t k = 0; k < freeBlocks; ++k) {
        uint8_t *block = gPage + 16 + ((r() % kBlocks + k * 7) % kBlocks) * cGZDrawContextAsBuffer::kSize;
        bool seen = false;
        for (uint8_t *b = first; b; b = *reinterpret_cast<uint8_t **>(b)) seen |= b == block;
        if (seen) continue;
        *reinterpret_cast<uint8_t **>(block) = first;
        first = block;
    }
    page->freeList = first;
    page->used = kBlocks - freeBlocks;
    page->next = page->prev = page;
}

void NotePool()
{
    const auto *page = reinterpret_cast<const RZFixedPage *>(gPage);
    const uint32_t *pool = GameAddress<uint32_t>(cGZDrawContextAsBuffer::kPool);
    drawfx::Note("pool", {drawfx::Norm(pool[1]), drawfx::Norm(pool[2]), drawfx::Norm(drawfx::P(page->next)),
                          drawfx::Norm(drawfx::P(page->prev)), drawfx::Norm(drawfx::P(page->freeList)), page->used});
}

// ---- calls through the object's own faces ----

template <class R, class... A> R CallFace(void *object, uint32_t face, unsigned slot, A... args)
{
    void *const self = static_cast<uint8_t *>(object) + face;
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*static_cast<void ***>(self))[slot])(self, args...);
}

uint32_t Word(const void *object, uint32_t offset)
{
    uint32_t v;
    std::memcpy(&v, static_cast<const uint8_t *>(object) + offset, 4);
    return v;
}

// Every word of the object but its four vtable words. By kind (lane RK): the context, the target and its shader face
// (+0x14..+0x1c) are pointers (the test's fakes, or null); the rest is data, as it is. The locked surface's bits (+0x38)
// are data too: a pointer into gPixels (one address on both sides) or what the block held.
void NoteObject(void *object)
{
    drawfx::Call c;
    c.note = "object";
    for (uint32_t off = 0xc; off < cGZDrawContextAsBuffer::kSize; off += 4)
        if (off != 0x10)
            c.words.push_back(off >= 0x14 && off < 0x20 ? drawfx::Ptr(Word(object, off)) : Word(object, off));
    drawfx::gLog.push_back(std::move(c));
}

int32_t Coord(std::mt19937 &rng) { return int32_t(rng() % 300) - 50; }
void RandomRect(std::mt19937 &rng, cGZRect &r) { r = {Coord(rng), Coord(rng), Coord(rng), Coord(rng)}; }

uint32_t RandomFlags(std::mt19937 &rng)
{
    const uint32_t flags[] = {0, 0x10, 0x8000, 0x8010, 1};
    return rng() % 6 ? flags[rng() % 5] : rng();
}

void *OriginalCreate(bool director)
{
    if (director) {
        // The COM director's creator: the +0x10 face of a new object.
        void *const face = reinterpret_cast<void *(__cdecl *)()>(GameAddress<void>(kDirectorCreate))();
        return face ? static_cast<uint8_t *>(face) - 0x10 : nullptr;
    }
    void *const block = reinterpret_cast<void *(__cdecl *)()>(GameAddress<void>(kAllocate))();
    using Ctor = void *(__thiscall *)(void *);
    return reinterpret_cast<Ctor>(GameAddress<void>(kConstructor))(block);
}
void *OurCreate(bool director)
{
    if (director) {
        cIGZDrawContextAsBuffer *const face = cGZDrawContextAsBuffer::Create();
        return face ? static_cast<cIGZBuffer *>(static_cast<cGZDrawContextAsBuffer *>(face)) : nullptr;
    }
    return static_cast<cIGZBuffer *>(new cGZDrawContextAsBuffer);
}

bool OriginalDrawEllipse(void *object, int32_t l, int32_t t, int32_t r, int32_t b, uint32_t value, int32_t thickness)
{
    using Fn = bool(__thiscall *)(void *, int32_t, int32_t, int32_t, int32_t, uint32_t, int32_t);
    return reinterpret_cast<Fn>(GameAddress<void>(kDrawEllipse))(object, l, t, r, b, value, thickness);
}
bool OurDrawEllipse(void *object, int32_t l, int32_t t, int32_t r, int32_t b, uint32_t value, int32_t thickness)
{
    return cGZDrawContextAsBufferAdapters::DrawEllipse(
        static_cast<cGZDrawContextAsBuffer *>(static_cast<cIGZBuffer *>(object)), nullptr, l, t, r, b, value, thickness);
}

// One side (ours or the original): the object made by the side's creator (the director's on odd seeds, else the
// allocator and the constructor as cGZDrawSystem slot 5 does), then the case's calls.
void RunSide(bool ours, uint32_t seed)
{
    drawfx::AddRange(gPixels, sizeof(gPixels), kPixelTag);
    // The log's growth is the test's own: an earlier test may leave its buffer at any capacity (drawfx::Side clears
    // it, keeping the capacity), and the side would count the log's reallocation or not by the suite's order.
    drawfx::gLog.reserve(1 << 14);
    std::mt19937 rng(seed);
    difftest::AllocationCounter heap;

    void *const object = ours ? OurCreate(seed % 2 != 0) : OriginalCreate(seed % 2 != 0);
    {
        difftest::NotCounted bookkeeping; // drawfx's range list keeps its capacity from the side before
        drawfx::AddRange(object, cGZDrawContextAsBuffer::kSize, kObjectTag);
        drawfx::AddRange(gPage, kPageSize, kPageTag);
    }
    drawfx::Note("created", drawfx::Norm(drawfx::P(object)));
    NotePool();
    NoteObject(object);
    drawfx::Note("addref", CallFace<uint32_t>(object, 0, 1));
    CallFace<void>(object, 0x10, 7, static_cast<void *>(&gContext), static_cast<void *>(&gTargets[0])); // Init
    NoteObject(object);

    for (int step = 0; step < 60; ++step) {
        const uint32_t pick = rng() % 53;
        drawfx::Note("pick", pick);
        cGZRect r1, r2, clipRect;
        RandomRect(rng, r1);
        RandomRect(rng, r2);
        RandomRect(rng, clipRect);
        const cGZRect *clip = rng() % 3 ? &clipRect : nullptr;
        const uint32_t word = rng(), word2 = rng();
        // GetPixel's coordinates inside the case's surface (its memory is the case's; outside it the original reads
        // wherever the stride takes it).
        const uint32_t w = gCase.surface.width ? gCase.surface.width : 1, h = gCase.surface.height ? gCase.surface.height : 1;
        const uint32_t px = rng() % w, py = rng() % h;
        uint8_t o[4] = {0xcc, 0xcc, 0xcc, 0xcc};
        uint32_t out = 0xdeadbeef;
        if (pick < 37) {
            const unsigned slot = 3 + pick; // the cIGZBuffer face's slots 3..39
            switch (slot) {
            case 3: drawfx::Note("bool", CallFace<uint32_t>(object, 0, slot, word % 64, word2 % 64, word % 11, 16u) & 0xff); break;
            case 6: case 7: drawfx::Note("bool", CallFace<uint32_t>(object, 0, slot, RandomFlags(rng)) & 0xff); break;
            case 11: {
                cGZRect r{-7, -7, -7, -7};
                CallFace<void>(object, 0, slot, &r);
                drawfx::Note("rect", {uint32_t(r.left), uint32_t(r.top), uint32_t(r.right), uint32_t(r.bottom)});
                break;
            }
            case 12: case 37: drawfx::Note("pointer", drawfx::Norm(CallFace<uint32_t>(object, 0, slot))); break;
            case 14: case 15: {
                cGZColorType t{0xdddd, 0xdddd};
                CallFace<void>(object, 0, slot, &t);
                drawfx::Note("type", {t.format, t.bits});
                break;
            }
            case 18: drawfx::Note("bool", CallFace<uint32_t>(object, 0, slot, word, clip) & 0xff); break;
            case 19:
                drawfx::Note("bool", CallFace<uint32_t>(object, 0, slot, word, r1.left, r1.top, r1.right, r1.bottom,
                                                        clip) & 0xff);
                break;
            case 20: drawfx::Note("bool", CallFace<uint32_t>(object, 0, slot, word, &r1, clip) & 0xff); break;
            case 21: drawfx::Note("pixel", CallFace<uint32_t>(object, 0, slot, px, py)); break;
            case 22: CallFace<void>(object, 0, slot, uint32_t(r1.left), uint32_t(r1.top), word); break;
            case 23: CallFace<void>(object, 0, slot, word); break;
            case 25: {
                uint32_t x = word % (kMaxSide + 8), y = word2 % (kMaxSide + 8);
                if (rng() % 2) {
                    // A pixel inside the surface (after the origin), set to a target's transparent colour with at most
                    // one bit flipped, half the time at a format mask's edge: each format's mask is tested bit by bit.
                    const uint32_t ox = uint32_t(gCase.origin.x), oy = uint32_t(gCase.origin.y);
                    x = w > ox ? rng() % (w - ox) : 0;
                    y = h > oy ? rng() % (h - oy) : 0;
                    const uint32_t edges[] = {11, 12, 14, 15, 23, 24, 31};
                    const uint32_t bit = rng() % 2 ? edges[rng() % 7] : rng() % 32;
                    const uint32_t value = gCase.transparent[rng() % 2] ^ (rng() % 4 ? 1u << bit : 0u);
                    const uint32_t size = gCase.surface.bytesPerPixel;
                    const size_t at = size_t(kStride) * (y + oy) + size_t(x + ox) * size;
                    for (uint32_t k = 0; k < size && k < 4 && at + k < sizeof gPixels; ++k)
                        gPixels[at + k] = uint8_t(value >> (8 * k));
                }
                drawfx::Note("bool", CallFace<uint32_t>(object, 0, slot, x, y) & 0xff);
                break;
            }
            case 26:
                drawfx::Note("bool", CallFace<uint32_t>(object, 0, slot, &out) & 0xff);
                drawfx::Note("colour", out);
                break;
            case 28: {
                // Thickness: around 1 mostly (the rectangle, line and offset-line paths), sometimes any word.
                const int32_t thick[] = {-1, 0, 1, 2, 3, 7};
                const int32_t t = rng() % 8 ? thick[rng() % 6] : int32_t(rng());
                int32_t x0 = r1.left, y0 = r1.top, x1 = r1.right, y1 = r1.bottom;
                if (rng() % 4 == 0) x1 = x0;
                else if (rng() % 3 == 0) y1 = y0;
                drawfx::Note("bool", CallFace<uint32_t>(object, 0, slot, x0, y0, x1, y1, word, t) & 0xff);
                break;
            }
            case 29:
                drawfx::Note("bool", CallFace<uint32_t>(object, 0, slot, static_cast<void *>(&gSource), &r1, &r2, clip) & 0xff);
                break;
            case 30: drawfx::Note("native", CallFace<uint32_t>(object, 0, slot, word, word2, rng())); break;
            case 31: case 33: {
                const uint32_t answer = slot == 31 ? CallFace<uint32_t>(object, 0, slot, word, &o[0], &o[1], &o[2])
                                                   : CallFace<uint32_t>(object, 0, slot, word, &o[0], &o[1], &o[2], &o[3]);
                drawfx::Note("rgb", {answer & 0xff, uint32_t(o[0]), uint32_t(o[1]), uint32_t(o[2]), uint32_t(o[3])});
                break;
            }
            case 32: drawfx::Note("native", CallFace<uint32_t>(object, 0, slot, word, word2, rng(), rng())); break;
            case 34: drawfx::Note("bits", drawfx::Norm(CallFace<uint32_t>(object, 0, slot))); break;
            case 38:
                drawfx::Note("bool", CallFace<uint32_t>(object, 0, slot, rng() % 2 ? static_cast<void *>(&gCache) : nullptr) & 0xff);
                break;
            case 4: case 5: case 8: case 24: case 39: drawfx::Note("bool", CallFace<uint32_t>(object, 0, slot) & 0xff); break;
            case 27: CallFace<void>(object, 0, slot); break;
            default: drawfx::Note("result", CallFace<uint32_t>(object, 0, slot)); break; // 9 10 13 16 17 35 36
            }
        } else if (pick < 39) {
            // The cIGZBufferShader face.
            if (pick == 37) {
                drawfx::Note("bool", CallFace<uint32_t>(object, 4, 3, word, word2, rng(), rng()) & 0xff);
            } else {
                const uint32_t answer = CallFace<uint32_t>(object, 4, 4, &o[0], &o[1], &o[2], &o[3]) & 0xff;
                drawfx::Note("color", {answer, uint32_t(o[0]), uint32_t(o[1]), uint32_t(o[2]), uint32_t(o[3])});
            }
        } else if (pick < 46) {
            // The cIGZDrawContextAsBuffer face's slots 3..9.
            const unsigned slot = pick - 36;
            switch (slot) {
            case 7: CallFace<void>(object, 0x10, slot, static_cast<void *>(&gContext), static_cast<void *>(&gTargets[rng() % 2])); break;
            case 9: {
                void *const sources[] = {&gSource, &gTargets[0], &gTargets[1]};
                CallFace<void>(object, 0x10, slot, sources[rng() % 3]);
                break;
            }
            case 6: case 8: CallFace<void>(object, 0x10, slot); break;
            default: drawfx::Note("pointer", drawfx::Norm(CallFace<uint32_t>(object, 0x10, slot))); break; // 3 4 5
            }
        } else if (pick < 50) {
            // QueryInterface on a face: its own faces, or the target's.
            const uint32_t iids[] = {1, cIGZBuffer::kIid, cIGZBufferShader::kIid, cIGZDrawContextAsBuffer::kIid, 0x12345678};
            // (The +8 face has no QueryInterface: its slot 0 is the deleting destructor.)
            const uint32_t queryFaces[] = {0, 4, 0x10, 0};
            const uint32_t face = queryFaces[pick - 46];
            const uint32_t answer = CallFace<uint32_t>(object, face, 0, iids[rng() % 5], static_cast<void *>(&out)) & 0xff;
            drawfx::Note("query", {answer, drawfx::Norm(out)});
        } else if (pick == 52) {
            // DrawEllipse 0x8c1e50 (no slot: called directly, the game's or ours).
            const auto draw = ours ? &OurDrawEllipse : &OriginalDrawEllipse;
            drawfx::Note("bool", draw(object, r1.left, r1.top, r1.right, r1.bottom, word, int32_t(word2)) ? 1u : 0u);
        } else {
            // AddRef on a face (the +8 face's is its slot 2).
            const uint32_t faces[] = {0, 4, 0x10};
            drawfx::Note("addref", pick == 50 ? CallFace<uint32_t>(object, 8, 2)
                                              : CallFace<uint32_t>(object, faces[rng() % 3], 1));
        }
        NoteObject(object);
    }

    // The last references, one Release each (through each face in turn); the last runs the deleting destructor.
    const uint32_t count = Word(object, 0xc);
    drawfx::Note("count", count);
    for (uint32_t i = 0; i < count && i < 64; ++i) {
        const uint32_t face = kFaceOffsets[i % 4];
        drawfx::Note("release", CallFace<uint32_t>(object, face, face == 8 ? 1 : 2));
    }
    heap.Stop();
    NotePool();
    drawfx::Note("refs", {uint32_t(gContext.refs), uint32_t(gTargets[0].refs), uint32_t(gTargets[1].refs),
                          uint32_t(gShaders[0].refs), uint32_t(gShaders[1].refs), uint32_t(gShaders[2].refs),
                          uint32_t(gSource.refs), uint32_t(gCache.refs)});
    drawfx::Note("heap", {uint32_t(heap.Blocks()), uint32_t(heap.Bytes())});
}

void NewCase(uint32_t seed)
{
    std::mt19937 rng(seed * 7919u + 3);
    const uint32_t formats[] = {0, 1, 4, 5, 6, 7, 8, 9, 10, 3};
    // A colour the pixels and the transparent colours are built near (single-bit differences test each format's mask).
    const uint32_t nearColour = rng() % 2 ? rng() : rng() % 4;
    // Half the flips at the formats' mask edges (12-bit 444, 15-bit 1555, 16-bit 565, 24-bit 888).
    const uint32_t edges[] = {11, 12, 14, 15, 23, 24, 31};
    const auto flipped = [&] {
        const uint32_t bit = rng() % 2 ? edges[rng() % 7] : rng() % 32;
        return rng() % 2 ? nearColour ^ (1u << bit) : nearColour;
    };
    for (unsigned i = 0; i < 2; ++i) {
        // Widths and heights 0..64, so IsPixelTransparent's bounds check keeps GetPixel inside the case's pixels.
        const int32_t left = Coord(rng), top = Coord(rng);
        gCase.bounds[i] = {left, top, left + int32_t(rng() % (kMaxSide + 1)), top + int32_t(rng() % (kMaxSide + 1))};
        gCase.format[i] = formats[rng() % 10];
        gCase.bits[i] = rng() % 40;
        gCase.bytes[i] = rng() % 6;
        gCase.transparent[i] = rng() % 4 ? flipped() : rng();
        gCase.hasTransparent[i] = rng() % 4 ? 1 : 0;
        gCase.counter[i] = rng();
        gCase.setCache[i] = rng() % 2;
    }
    for (unsigned i = 0; i < 3; ++i) {
        gCase.shaderFace[i] = rng() % 4 != 0;
        gCase.shaderColor[i] = rng();
        gCase.shaderOk[i] = rng() % 4 ? 1 : 0;
    }
    gCase.surface.bits = gPixels;
    gCase.surface.stride = int32_t(kStride);
    gCase.surface.width = rng() % (kMaxSide + 1);
    gCase.surface.height = rng() % (kMaxSide + 1);
    gCase.surface.format = formats[rng() % 10];
    gCase.surface.bytesPerPixel = rng() % 6;
    gCase.lockOk = rng() % 5 ? 1 : 0;
    // The origin is small and not negative: with a width of 64 or less, x + origin stays inside the pixels' rows.
    gCase.origin = {int32_t(rng() % 8), int32_t(rng() % 8)};
    // Pixels: mostly nearColour's low bytes at the surface's pixel size (some with one bit flipped), else any byte.
    const uint32_t size = gCase.surface.bytesPerPixel ? gCase.surface.bytesPerPixel : 1;
    for (uint8_t &b : gPixels) b = uint8_t(rng());
    for (size_t row = 0; row + kStride <= sizeof gPixels; row += kStride) {
        for (size_t at = row; at + size <= row + kStride; at += size) {
            const uint32_t value = rng() % 4 ? flipped() : rng();
            for (uint32_t k = 0; k < size; ++k) gPixels[at + k] = uint8_t(k < 4 ? value >> (8 * k) : rng());
        }
    }
}

struct PoolScope {
    drawfx::Cells cells;
    PoolScope()
    {
        // The pool object: no vtable (nothing reaches the page bookkeeping), the test page as its current page.
        cells.Set(cGZDrawContextAsBuffer::kPool, 0u);
        cells.Set(cGZDrawContextAsBuffer::kPool + 4, 0u);
        cells.Set(cGZDrawContextAsBuffer::kPool + 8, 0u);
    }
};

void RunCase(difftest::Context &ctx, uint32_t seed, bool &ok)
{
    NewCase(seed);
    ctx.Mark("orig");
    auto a = drawfx::Side(&BuildWorld, [&] {
        BuildPool(seed);
        *GameAddress<uint32_t>(cGZDrawContextAsBuffer::kPool + 4) = drawfx::P(gPage);
        RunSide(false, seed);
    });
    NewCase(seed);
    ctx.Mark("ours");
    auto b = drawfx::Side(&BuildWorld, [&] {
        BuildPool(seed);
        *GameAddress<uint32_t>(cGZDrawContextAsBuffer::kPool + 4) = drawfx::P(gPage);
        RunSide(true, seed);
    });
    ok = drawfx::Same(ctx, a.first, b.first, "draw context as buffer", seed, a.second);
}

} // namespace

DIFF_TEST(GZDrawContextAsBuffer_Slots, 0x8c1800)
{
    // Every slot of the four faces (cIGZBuffer 0x8c14f0 0x8c14d0 0x8c14e0 0x8c17d0 0x8c17e0 0x8c17f0 0x8c1800 0x8c1890
    // 0x8c18c0 0x8c18d0 0x8c18e0 0x8c1900 0x8c18f0 0x8c1910 0x8c1940 0x8c1920 0x8c1970 0x8c1980 0x8c1a30 0x8c19f0
    // 0x8c1990 0x8c1a90 0x8c1b60 0x8c1ba0 0x8c1bb0 0x8c1bc0 0x8c1c80 0x8c1c90 0x8c1ca0 0x8c1ea0 0x8c1f60 0x8c2040 0x8c2070
    // 0x8c2080 0x8c2090 0x8c20a0 0x8c20b0 0x8c20c0 0x8c20d0 0x8c2100; cIGZBufferShader 0x8c1730 0x8c1790;
    // cIGZDrawContextAsBuffer 0x8c1570 0x8c15a0 0x8c15c0 0x8c15d0 0x8c16f0 0x8c2200), the faces' thunks, the colour
    // helper 0x8c2110, DrawEllipse 0x8c1e50 (called directly), the director's creator 0x8c22a0 (odd seeds) or the
    // allocator 0x8c1460 and the constructor 0x8c13a0 (even seeds), and the deleting destructor 0x8c2350 (the last
    // Release).
    // The faces' adjustor thunks (the originals', unhooked; ours are our compiler's, in our vtables, and the test calls
    // them through the faces): +4 QueryInterface 0x8c2417, AddRef 0x8c23ff, Release 0x8c2427 (QueryInterface, AddRef
    // and the final Releases on face 4); +8 AddRef 0x8c2407 (slot 2), Release 0x8c242f (slot 1) and the deleting
    // destructor 0x8c2346 (slot 0: rz::cRZRefCount::Release deletes through it, so every last Release runs it);
    // +0x10 QueryInterface 0x8c241f, AddRef 0x8c240f, Release 0x8c2437.
    PoolScope pool;
    char seedText[16] = {};
    const uint32_t only = GetEnvironmentVariableA("TS2_TEST_SEED", seedText, sizeof(seedText)) ? uint32_t(atoi(seedText)) : 0;
    for (uint32_t seed = only ? only : 1; seed <= (only ? only : 4000); ++seed) {
        bool ok = true;
        RunCase(ctx, seed, ok);
        if (!ok) break;
    }
}

DIFF_TEST(GZDrawContextAsBuffer_Construct, 0x8c13a0)
{
    // The constructor on a block of seeded bytes: every byte it writes and every byte it leaves, but the four vtable
    // words (the game's on the original side, ours on ours).
    const auto original = ctx.Original<void *(__thiscall *)(void *)>(kConstructor);
    alignas(8) uint8_t a[cGZDrawContextAsBuffer::kSize], b[cGZDrawContextAsBuffer::kSize];
    for (uint32_t seed = 0; seed < 200; ++seed) {
        std::mt19937 rng(seed);
        for (auto &x : a) x = uint8_t(rng());
        std::memcpy(b, a, sizeof(a));
        void *const ra = original(a);
        void *const rb = cGZDrawContextAsBufferAdapters::Construct(b, nullptr);
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("seed %u: the constructor returned another address", seed);
        for (unsigned f = 0; f < 4; ++f) {
            const uint32_t game = drawfx::P(GameAddress<void>(kGameVTables[f]));
            if (Word(a, kFaceOffsets[f]) != game) ctx.Fail("seed %u: the original's face +%#x is not the game's", seed, kFaceOffsets[f]);
            if (Word(b, kFaceOffsets[f]) == game) ctx.Fail("seed %u: our face +%#x has the game's vtable", seed, kFaceOffsets[f]);
            std::memset(a + kFaceOffsets[f], 0, 4);
            std::memset(b + kFaceOffsets[f], 0, 4);
        }
        if (std::memcmp(a, b, sizeof(a)) != 0) {
            for (uint32_t off = 0; off < sizeof(a); ++off)
                if (a[off] != b[off]) {
                    ctx.Fail("seed %u: byte +%#x orig %#x ours %#x", seed, off, a[off], b[off]);
                    break;
                }
            break;
        }
    }
}
