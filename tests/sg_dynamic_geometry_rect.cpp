// Differential tests of nGZSceneGraph::cDynamicGeometryRect (lane QR, scenegraph D3 part 2; src/sg/cDynamicGeometryRect.h).
//
// Each side builds its own rect: the game's constructor 0xa43ec0 on a block of 0xb0 bytes, and ours (the hook's adapter).
// The graphic services both sides ask are the cached getters' cells (the vertex formats manager, the vertex declarations
// manager, the graphic system the current device comes from) pre-seeded with fakes (tests/drawfx_core.h): a format
// (stride and offsets by usage), the two managers, declarations, the device state, the device and four dynamic arrays whose
// Lock answers a 64 KB buffer each. Compared: every fake call with its arguments in order, the object's words after
// each step (but its vtable, +0x3c and +0x6a, which the constructor does not write) and the buffers (a hash per 256
// bytes of the first 16 KB, so the bytes of the quads and the index words are compared exactly, unwritten ones included).
//
//   Rect_Construct            the constructor: the format built (BeginFormat, two AddElements, EndFormat), the stride and
//                             offsets asked, the object; then the destructor (the declaration, then the format, released)
//   Rect_Destroy              0xa440f0 on objects with and without a format / declaration
//   Rect_DeletingDestructor   0xa44150 (flags 0: the original's delete is the CRT's, not run)
//   Rect_Begin                slot 7 (0xa44180): the device's DrawPrimitive(5, 2, first)
//   Rect_Render               slot 8 (0xa441b0): random cell counts (fractional, zero, negative, NaN), boxes, UV flag, z,
//                             stride and offsets (overlapping too), context sizes, flag, declaration states; the vertex
//                             and index bytes, Reserve / Lock / Unlock / SetStreamSource / SetVertexDeclaration /
//                             SetIndices / DrawIndexedPrimitive in order
//   Rect_Slots                the folded slots through the vtable: QueryInterface, AddRef, Release (not to zero), IsSupported,
//                             GetFlags, GetDrawInfo (0xa441a0), Prepare
//   Rect_ReleaseLast          ours only: the last Release deletes the object and releases its format and declaration
//                             (the original's delete is the CRT's, not run; expectations read from 0x153f50 / 0xa44150)
//
// RVAs these tests reach (status entries cite them): the constructor 0xa43ec0, destructor 0xa440f0, deleting destructor
// 0xa44150, slots 7 / 8 0xa44180 / 0xa441b0, the folded slots 0x3af660 / 0x148fb0 / 0x153f50 / 0x148260 / 0x142840 /
// 0xa441a0 / 0x18a5b0, and the cached getters 0x18dcf0 / 0x18dd70 / 0x18ae40.
#include "difftest.h"
#include "sgfx.h"
#include "test_helpers.h"

#include "sg/cDynamicGeometryRect.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;
using drawfx::Ret;
using drawfx::Slot;

constexpr uint32_t kObjectTag = 0x7d000000;
constexpr uint32_t kUsagePosition = 0x5b830781;
constexpr uint32_t kUsageTexture = 0xbb8307ab;
constexpr uint32_t kBufferSize = 0x10000;

#define RECT_UNKNOWN_SLOTS {2, Ret::Int, true, "QueryInterface", "ui"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""}
#define U {0, Ret::Int, false, "", ""}
#define U2 U, U
#define U4 U2, U2
#define U8 U4, U4

inline constexpr Slot kFormatSlots[] = {RECT_UNKNOWN_SLOTS, U8, // 3-10
                                        {1, Ret::Int, true, "GetStreamSize", "u"}, // 11
                                        U, U, U,                                   // 12-14
                                        {2, Ret::Int, true, "GetOffsetAny", "ub"}}; // 15
inline constexpr drawfx::Iface kFormat{"VertexFormat", 16, kFormatSlots};

inline constexpr Slot kFormatsSlots[] = {RECT_UNKNOWN_SLOTS, {0, Ret::Int, true, "BeginFormat", ""},
                                         {4, Ret::Int, true, "AddElement", "uuub"}, {1, Ret::Int, true, "EndFormat", "i"}};
inline constexpr drawfx::Iface kFormats{"VertexFormatsManager", 6, kFormatsSlots};

inline constexpr Slot kDeclsSlots[] = {RECT_UNKNOWN_SLOTS, {3, Ret::Int, true, "GetDeclaration", "iii"}};
inline constexpr drawfx::Iface kDecls{"VertexDeclarationsManager", 4, kDeclsSlots};

inline constexpr Slot kDeclSlots[] = {RECT_UNKNOWN_SLOTS, U, U, U, {0, Ret::Int, true, "IsCreated", ""}};
inline constexpr drawfx::Iface kDecl{"VertexDeclaration", 7, kDeclSlots};

inline constexpr Slot kStateSlots[] = {RECT_UNKNOWN_SLOTS, U8, U4, U, // 3-15
                                       {1, Ret::Int, true, "SetVertexDeclaration", "i"},
                                       {4, Ret::Int, true, "SetStreamSource", "uiuu"},
                                       {1, Ret::Int, true, "SetIndices", "i"}};
inline constexpr drawfx::Iface kState{"DeviceState", 19, kStateSlots};

inline constexpr Slot kDeviceSlots[] = {RECT_UNKNOWN_SLOTS, U8, U8, U8, U4, // 3-30
                                        {3, Ret::Int, true, "DrawPrimitive", "uuu"},
                                        {6, Ret::Int, true, "DrawIndexedPrimitive", "uuuuuu"}};
inline constexpr drawfx::Iface kDevice{"Device", 33, kDeviceSlots};

inline constexpr Slot kArraySlots[] = {RECT_UNKNOWN_SLOTS, U8, U4, // 3-14
                                       {3, Ret::Int, true, "Lock", "uuu"},
                                       {0, Ret::Int, true, "Unlock", ""},
                                       U, U, U,
                                       {2, Ret::Int, true, "Reserve", "uu"}};
inline constexpr drawfx::Iface kArray{"DynamicArray", 21, kArraySlots};

inline constexpr Slot kSystemSlots[] = {RECT_UNKNOWN_SLOTS, U8, U8, U2, // 3-20
                                        {0, Ret::Int, true, "GetCurrentDeviceIndex", ""},
                                        U, U,
                                        {1, Ret::Int, true, "GetDevice", "u"}};
inline constexpr drawfx::Iface kSystem{"GraphicSystem", 25, kSystemSlots};

#undef RECT_UNKNOWN_SLOTS
#undef U8
#undef U4
#undef U2
#undef U

enum : unsigned { kStride, kPosition, kTexture, kOther };

Fake gFormat, gFormats, gDecls, gDeclPre, gDeclNew[4], gState, gDevice, gSystem, gArrays[4];
uint8_t gBuffers[4][kBufferSize];
cGeometryRenderContext gContext;
unsigned gNextDecl;
difftest::Context *gCtx;

uint32_t FormatStreamSize(Fake &, const uint32_t *) { return gFormat.value[kStride]; }
uint32_t FormatOffset(Fake &, const uint32_t *a)
{
    return a[0] == kUsagePosition ? gFormat.value[kPosition] : a[0] == kUsageTexture ? gFormat.value[kTexture] : gFormat.value[kOther];
}
uint32_t EndFormat(Fake &, const uint32_t *a)
{
    ++gFormat.refs;
    *reinterpret_cast<void **>(uintptr_t(a[0])) = &gFormat;
    return 0;
}
// value[0]: answer a declaration (counted); value[1]: the result.
uint32_t GetDeclaration(Fake &self, const uint32_t *a)
{
    if (self.value[0]) {
        Fake &d = gDeclNew[gNextDecl++ & 3];
        ++d.refs;
        *reinterpret_cast<void **>(uintptr_t(a[2])) = &d;
    }
    return self.value[1];
}
uint32_t IsCreated(Fake &self, const uint32_t *) { return self.value[0]; }
uint32_t CurrentIndex(Fake &self, const uint32_t *) { return self.value[0]; }
uint32_t GetDevice(Fake &, const uint32_t *) { return P(&gDevice); }
// value[0]: Reserve's answer; value[1]: the buffer Lock answers.
uint32_t Lock(Fake &self, const uint32_t *) { return self.value[1]; }
uint32_t Reserve(Fake &self, const uint32_t *) { return self.value[0]; }

template <class T> T Pick(std::mt19937 &rng, const T *list, unsigned n) { return list[rng() % n]; }

const float kNan = std::bit_cast<float>(0xffc00000u);
const float kCounts[] = {1, 1, 1, 1, 2, 2, 3, 4, 5, 6, 2.5f, 1.5f, 0.5f, 0.25f, 3.9999998f, 4.0000005f, 0, -1, -0.5f, kNan, 1e-30f};
const float kFloats[] = {0, 1, -1, 0.5f, 0.25f, 2, 3.5f, -2.5f, 100, -1000, 1000, 0.1f, 1e-3f, 1e30f, -1e30f, kNan};
const float kGoodFloats[] = {0, 1, -1, 0.5f, 2, -2.5f, 100, 0.1f};
const uint32_t kWidths[] = {0, 1, 2, 3, 100, 640, 1024, 0xffffffffu, 0x80000000u, 0x7fffffffu, 4096};
const uint32_t kReserves[] = {0, 1, 7, 100, 0xffffffffu, 0x80000000u};

void ResetFakes(std::mt19937 &rng)
{
    gFormat.Reset<kFormat>(700);
    gFormats.Reset<kFormats>(701);
    gDecls.Reset<kDecls>(702);
    gDeclPre.Reset<kDecl>(703);
    for (unsigned i = 0; i < 4; ++i) gDeclNew[i].Reset<kDecl>(704 + i);
    gState.Reset<kState>(710);
    gDevice.Reset<kDevice>(711);
    gSystem.Reset<kSystem>(712);
    for (unsigned i = 0; i < 4; ++i) gArrays[i].Reset<kArray>(720 + i);
    gNextDecl = 0;

    gFormat.On(11, &FormatStreamSize);
    gFormat.On(15, &FormatOffset);
    static const uint32_t strides[] = {0, 4, 8, 20, 24, 28, 32, 36, 40, 64, 100};
    gFormat.value[kStride] = Pick(rng, strides, 11);
    gFormat.value[kPosition] = (rng() % 16) * 4;
    const unsigned shape = rng() % 6;
    gFormat.value[kTexture] = shape == 0   ? gFormat.value[kPosition] // overlapping
                              : shape == 1 ? gFormat.value[kPosition] + 4
                              : shape == 2 ? gFormat.value[kPosition] + 8
                                           : gFormat.value[kPosition] + 12 + (rng() % 4) * 4;
    gFormat.value[kOther] = 0x1234;
    gFormats.On(5, &EndFormat);
    gDecls.On(3, &GetDeclaration);
    gDecls.value[0] = rng() % 4 != 0;
    gDecls.value[1] = rng() % 3 != 0;
    gDeclPre.On(6, &IsCreated);
    for (Fake &d : gDeclNew) d.On(6, &IsCreated), d.value[0] = rng() % 2;
    gSystem.On(21, &CurrentIndex);
    gSystem.On(24, &GetDevice);
    gSystem.value[0] = rng() % 4;
    for (unsigned i = 0; i < 4; ++i) {
        gArrays[i].On(15, &Lock);
        gArrays[i].On(20, &Reserve);
        std::memset(gBuffers[i], 0xcd, kBufferSize);
        gArrays[i].value[0] = Pick(rng, kReserves, 6);
        gArrays[i].value[1] = P(gBuffers[i]);
    }
    std::memset(&gContext, 0, sizeof gContext);
    gContext.mWidth = Pick(rng, kWidths, 11);
    gContext.mHeight = Pick(rng, kWidths, 11);
    gContext.mDevice = reinterpret_cast<nGZGraphic4::cIDeviceDX *>(&gDevice);
    gContext.mState = reinterpret_cast<nGZGraphic4::cIDeviceState *>(&gState);
    gContext.mVertices = reinterpret_cast<nGZGraphic4::cIDynamicVertexArray *>(&gArrays[0]);
    gContext.mVerticesB = reinterpret_cast<nGZGraphic4::cIDynamicVertexArray *>(&gArrays[1]);
    gContext.mIndices = reinterpret_cast<nGZGraphic4::cIDynamicVertexArray *>(&gArrays[2]);
    gContext.mIndicesB = reinterpret_cast<nGZGraphic4::cIDynamicVertexArray *>(&gArrays[3]);
}

void Build(uint32_t seed, drawfx::Cells &cells)
{
    std::mt19937 rng(seed * 2654435761u + 17u);
    ResetFakes(rng);
    cells.Set(0x10caa24, &gFormats); // the cached vertex formats manager
    cells.Set(0x10caa28, &gDecls);   // the cached vertex declarations manager
    cells.Set(0x10ca87c, &gSystem);  // the cached graphic system (the current device)
}

uint32_t Hash(const uint8_t *p, unsigned n)
{
    uint32_t h = 2166136261u;
    for (unsigned i = 0; i < n; ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}

void NoteBuffers()
{
    for (unsigned a = 0; a < 4; ++a) {
        drawfx::Call c;
        c.note = "buffer";
        c.words.push_back(a);
        for (unsigned off = 0; off < 0x4000; off += 256) c.words.push_back(Hash(gBuffers[a] + off, 256));
        drawfx::gLog.push_back(std::move(c));
    }
}

void NoteObject(const char *label, const uint8_t *mem)
{
    drawfx::Call c;
    c.note = label;
    const uint32_t *w = reinterpret_cast<const uint32_t *>(mem);
    for (unsigned i = 1; i < 0xb0 / 4; ++i) {
        if (i == 0x3c / 4) continue;
        uint32_t v = w[i];
        if (i == 0x68 / 4) v &= 0xffff;
        if (i == 0x40 / 4 || i == 0x6c / 4 || i == 0xac / 4) v = drawfx::Norm(v);
        c.words.push_back(v);
    }
    drawfx::gLog.push_back(std::move(c));
}

void NoteRefs()
{
    drawfx::Note("refs", {uint32_t(gFormat.refs), uint32_t(gDeclPre.refs), uint32_t(gDeclNew[0].refs),
                          uint32_t(gDeclNew[1].refs), uint32_t(gDeclNew[2].refs), uint32_t(gDeclNew[3].refs)});
}

using CtorFn = void *(__fastcall *)(void *, void *);
using DtorFn = void(__fastcall *)(void *, void *);
using DeletingFn = void *(__fastcall *)(void *, void *, uint32_t);
using Adapters = cDynamicGeometryRectAdapters;

uint8_t *NewBlock()
{
    uint8_t *const mem = static_cast<uint8_t *>(::operator new(sizeof(cDynamicGeometryRect)));
    std::memset(mem, 0xa5, sizeof(cDynamicGeometryRect));
    drawfx::AddRange(mem, sizeof(cDynamicGeometryRect), kObjectTag);
    return mem;
}

cDynamicGeometryRect *Construct(difftest::Context &ctx, bool original, uint8_t *mem)
{
    if (original) return static_cast<cDynamicGeometryRect *>(ctx.Original<CtorFn>(0xa43ec0)(mem, nullptr));
    return Adapters::Construct(reinterpret_cast<cDynamicGeometryRect *>(mem), nullptr);
}

void Destruct(difftest::Context &ctx, bool original, cDynamicGeometryRect *rect)
{
    if (original)
        ctx.Original<DtorFn>(0xa440f0)(rect, nullptr);
    else
        Adapters::Destruct(rect, nullptr);
}

// Random state for the quads.
void Configure(cDynamicGeometryRect *r, std::mt19937 &rng)
{
    const bool plain = rng() % 2;
    auto f = [&] { return plain ? Pick(rng, kGoodFloats, 8) : Pick(rng, kFloats, 16); };
    r->mX0 = f();
    r->mX1 = f();
    r->mY0 = f();
    r->mY1 = f();
    r->mU0 = f();
    r->mU1 = f();
    r->mV0 = f();
    r->mV1 = f();
    r->mZ = f();
    r->mCountX = Pick(rng, kCounts, 21);
    r->mCountY = rng() % 3 ? r->mCountX : Pick(rng, kCounts, 21);
    static const int32_t flags[] = {0, 0, 1, 1, -1, 2, 0x100};
    r->mUvFlag = Pick(rng, flags, 7);
}

} // namespace

DIFF_TEST(Rect_Construct, 0xa43ec0)
{
    gCtx = &ctx;
    sgfx::CheckSides(ctx, "construct", 64, Build, [&](bool original, std::mt19937 &rng) {
        uint8_t *const mem = NewBlock();
        cDynamicGeometryRect *const rect = Construct(ctx, original, mem);
        drawfx::Note("returned", drawfx::Norm(P(rect)));
        NoteObject("object", mem);
        NoteRefs();
        if (rng() % 2) rect->mDeclaration = reinterpret_cast<nGZGraphic4::cIVertexDeclarationDX *>(&gDeclPre);
        Destruct(ctx, original, rect);
        NoteObject("destroyed", mem);
        NoteRefs();
        ::operator delete(mem);
    });
}

DIFF_TEST(Rect_Destroy, 0xa440f0)
{
    gCtx = &ctx;
    sgfx::CheckSides(ctx, "destroy", 64, Build, [&](bool original, std::mt19937 &rng) {
        uint8_t *const mem = NewBlock();
        cDynamicGeometryRect *const rect = Construct(ctx, original, mem);
        drawfx::Note("format refs", uint32_t(gFormat.refs));
        const unsigned shape = rng() % 4;
        if (shape & 1) rect->mFormat = nullptr;
        if (shape & 2) rect->mDeclaration = reinterpret_cast<nGZGraphic4::cIVertexDeclarationDX *>(&gDeclPre);
        Destruct(ctx, original, rect);
        NoteObject("destroyed", mem);
        NoteRefs();
        ::operator delete(mem);
    });
}

DIFF_TEST(Rect_DeletingDestructor, 0xa44150)
{
    gCtx = &ctx;
    sgfx::CheckSides(ctx, "deleting destructor", 32, Build, [&](bool original, std::mt19937 &rng) {
        uint8_t *const mem = NewBlock();
        cDynamicGeometryRect *const rect = Construct(ctx, original, mem);
        if (rng() % 2) rect->mDeclaration = reinterpret_cast<nGZGraphic4::cIVertexDeclarationDX *>(&gDeclPre);
        // Flags without bit 0: the original's delete is the CRT's and not run; the bit-0 case is ours only (below).
        const uint32_t flags = (rng() % 4) & 2;
        void *const result = original ? ctx.Original<DeletingFn>(0xa44150)(rect, nullptr, flags)
                                      : Adapters::DeletingDestructor(rect, nullptr, flags);
        drawfx::Note("returned", drawfx::Norm(P(result)));
        NoteObject("destroyed", mem);
        NoteRefs();
        ::operator delete(mem);
    });
}

DIFF_TEST(Rect_Begin, 0xa44180)
{
    gCtx = &ctx;
    sgfx::CheckSides(ctx, "begin", 48, Build, [&](bool original, std::mt19937 &rng) {
        uint8_t *const mem = NewBlock();
        cDynamicGeometryRect *const rect = Construct(ctx, original, mem);
        for (unsigned k = 0; k < 4; ++k) {
            const uint32_t index = rng(), first = rng() % 3 ? rng() % 100 : rng();
            sgfx::MarkStep(ctx, "begin");
            if (original || k % 2)
                static_cast<cIGeometryRender *>(rect)->Begin(index, &gContext, first);
            else
                Adapters::Begin(rect, nullptr, index, &gContext, first);
        }
        NoteObject("object", mem);
        Destruct(ctx, original, rect);
        ::operator delete(mem);
    });
}

DIFF_TEST(Rect_Render, 0xa441b0)
{
    gCtx = &ctx;
    sgfx::CheckSides(ctx, "render", 600, Build, [&](bool original, std::mt19937 &rng) {
        uint8_t *const mem = NewBlock();
        cDynamicGeometryRect *const rect = Construct(ctx, original, mem);
        drawfx::Note("refs after construct", uint32_t(gFormat.refs));
        const unsigned renders = 1 + rng() % 3;
        for (unsigned k = 0; k < renders; ++k) {
            Configure(rect, rng);
            switch (rng() % 5) {
            case 0: rect->mDeclaration = nullptr; break;
            case 1:
                gDeclPre.value[0] = 1;
                rect->mDeclaration = reinterpret_cast<nGZGraphic4::cIVertexDeclarationDX *>(&gDeclPre);
                break;
            case 2:
                gDeclPre.value[0] = 0;
                rect->mDeclaration = reinterpret_cast<nGZGraphic4::cIVertexDeclarationDX *>(&gDeclPre);
                break;
            case 3:
                gDeclPre.value[0] = 0x100; // al clear, the rest set
                rect->mDeclaration = reinterpret_cast<nGZGraphic4::cIVertexDeclarationDX *>(&gDeclPre);
                break;
            default: break; // keep what the last render left
            }
            static const uint8_t flags[] = {0, 1, 1, 0, 2, 0x80, 0xff};
            const uint8_t flag = Pick(rng, flags, 7);
            std::memset(gBuffers, 0xcd, sizeof gBuffers);
            char step[48];
            std::snprintf(step, sizeof step, "render %u", k);
            sgfx::MarkStep(ctx, step);
            uint32_t r;
            const bool viaVirtual = rng() % 2;
            if (original || viaVirtual)
                r = static_cast<cIGeometryRender *>(rect)->Render(0x11111111, &gContext, flag, 0x22222222);
            else
                r = Adapters::Render(rect, nullptr, 0x11111111, &gContext, flag, 0x22222222);
            drawfx::Note("render result", r);
            NoteObject("object", mem);
            NoteBuffers();
            NoteRefs();
        }
        Destruct(ctx, original, rect);
        NoteRefs();
        ::operator delete(mem);
    });
}

DIFF_TEST(Rect_Slots, 0xa441a0)
{
    gCtx = &ctx;
    static const uint32_t iids[] = {1, 0x89d39f12, 0, 2, 0x12345678, 0xffffffffu, 0x89d39f13, 0x89d39f11};
    sgfx::CheckSides(ctx, "slots", 64, Build, [&](bool original, std::mt19937 &rng) {
        uint8_t *const mem = NewBlock();
        cDynamicGeometryRect *const rect = Construct(ctx, original, mem);
        auto *const g = static_cast<cIGeometryRender *>(rect);
        rect->mRefs = 1 + rng() % 5;
        const unsigned ops = 20 + rng() % 30;
        for (unsigned k = 0; k < ops; ++k) {
            const unsigned op = rng() % 8;
            drawfx::Note("op", op);
            char step[32];
            std::snprintf(step, sizeof step, "step %u, op %u", k, op);
            sgfx::MarkStep(ctx, step);
            switch (op) {
            case 0: {
                const uint32_t iid = Pick(rng, iids, 8);
                void *out = reinterpret_cast<void *>(uintptr_t(0x77777777));
                const bool r = g->QueryInterface(iid, rng() % 5 ? &out : nullptr);
                drawfx::Note("query", {iid, uint8_t(r), drawfx::Norm(P(out))});
                break;
            }
            case 1: drawfx::Note("add ref", g->AddRef()); break;
            case 2:
                if (rect->mRefs >= 2) drawfx::Note("release", g->Release());
                break;
            case 3: drawfx::Note("supported", uint8_t(g->IsSupported(rng()))); break;
            case 4: {
                uint32_t *const flags = g->GetFlags();
                drawfx::Note("flags", {drawfx::Norm(P(flags)), *flags});
                if (rng() % 2) *flags = rng();
                break;
            }
            case 5: {
                uint32_t *const info = g->GetDrawInfo(rng());
                drawfx::Note("draw info", {drawfx::Norm(P(info)), info[0], info[1]});
                break;
            }
            case 6: {
                const uint32_t a = rng(), b = rng(), c = rng(), d = rng();
                drawfx::Note("prepare", g->Prepare(a, b, c, d));
            } break;
            default: NoteObject("object", mem); break;
            }
        }
        NoteObject("object", mem);
        NoteRefs();
        Destruct(ctx, original, rect);
        ::operator delete(mem);
    });
}

// Ours only (the original's last release calls the CRT's delete): the last Release answers 0, deletes the object and
// releases its format and declaration (0x153f50 calls the deleting destructor 0xa44150 with 1 when the count reaches 0).
DIFF_TEST(Rect_ReleaseLast, 0x153f50)
{
    drawfx::Cells cells;
    for (uint32_t seed = 0; seed < 8; ++seed) {
        drawfx::ResetAll();
        Build(seed, cells);
        uint8_t *const mem = static_cast<uint8_t *>(::operator new(sizeof(cDynamicGeometryRect)));
        cDynamicGeometryRect *const rect = Adapters::Construct(reinterpret_cast<cDynamicGeometryRect *>(mem), nullptr);
        const bool withDecl = seed % 2;
        if (withDecl) rect->mDeclaration = reinterpret_cast<nGZGraphic4::cIVertexDeclarationDX *>(&gDeclPre);
        const int32_t formatRefs = gFormat.refs, declRefs = gDeclPre.refs;
        auto *const g = static_cast<cIGeometryRender *>(rect);
        rect->mRefs = 1;
        ++ctx.cases;
        const uint32_t r = g->Release();
        if (r != 0) ctx.Fail("release to zero answered %u, expected 0 (seed %u)", r, seed);
        if (gFormat.refs != formatRefs - 1) ctx.Fail("release to zero: format refs %d, expected %d (seed %u)", gFormat.refs, formatRefs - 1, seed);
        if (gDeclPre.refs != (withDecl ? declRefs - 1 : declRefs))
            ctx.Fail("release to zero: declaration refs %d, expected %d (seed %u)", gDeclPre.refs, withDecl ? declRefs - 1 : declRefs, seed);
        drawfx::ResetAll();
    }
}
