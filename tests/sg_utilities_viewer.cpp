// Differential tests of nGZSceneGraph's viewer texture state helpers (lane 261111l; src/sg/SGUtilitiesViewer.cpp):
//
//   SG_Utilities_CaptureViewerTextures      0xaf2450
//   SG_Utilities_ApplyViewerTextures        0xaf23f0
//
// A viewer fake answers slot 113 (the whole answer word scripted, so a set high byte with a clear low byte is seen),
// Slot108's id (-1 or a texture id), the texture count (0-5: five writes the fifth texture over mRef94, as the
// original does), each texture (one of five counted texture fakes, or null), the ref94 texture and the target size
// (written into the caller's buffer or answered from the viewer's own, the scale's bits random, NaNs among them).
// Slots 92-94 log what they are given. The texture manager is a fake planted in the cache 0x10f8f94 (slots 18 / 19 log
// the ids counted and released). The state starts as a random leftover (counted cells holding fakes or null, a random
// handle word and flag byte) and is noted whole after each call with every fake's count; each seed makes 1-6 calls,
// capture and apply mixed, on one state.
#include "sgfx.h"

#include "sg/SGUtilities.h"

#include <array>
#include <cstring>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;

constexpr unsigned kViewerSlotCount = 114;
constexpr std::array<Slot, kViewerSlotCount> ViewerSlots()
{
    std::array<Slot, kViewerSlotCount> r{};
    for (unsigned i = 0; i < kViewerSlotCount; ++i) r[i] = {0, Ret::Int, false, "", ""};
    r[92] = {3, Ret::Int, true, "SetExplicit", "iif"};
    r[93] = {4, Ret::Int, true, "SetTextures", "uW4if"};
    r[94] = {1, Ret::Int, true, "SetHandle", "u"};
    r[105] = {0, Ret::Int, true, "GetTextureCount", ""};
    r[106] = {1, Ret::Int, true, "GetTexture", "u"};
    r[107] = {0, Ret::Int, true, "GetTextureRef94", ""};
    r[108] = {0, Ret::Int, true, "Slot108", ""};
    r[110] = {1, Ret::Int, true, "GetTargetSize", "i"};
    r[113] = {0, Ret::Int, true, "IsDeviceView", ""};
    return r;
}
constexpr auto kViewerSlots = ViewerSlots();
constexpr drawfx::Iface kViewer{"Viewer", kViewerSlotCount, kViewerSlots.data()};

constexpr Slot kTexSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ui"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""}};
constexpr drawfx::Iface kTex{"Texture", 3, kTexSlots};

constexpr unsigned kManagerSlotCount = 24;
constexpr std::array<Slot, kManagerSlotCount> ManagerSlots()
{
    std::array<Slot, kManagerSlotCount> r{};
    for (unsigned i = 0; i < kManagerSlotCount; ++i) r[i] = {0, Ret::Int, false, "", ""};
    r[18] = {1, Ret::Int, true, "AddRefId", "u"};
    r[19] = {1, Ret::Int, true, "ReleaseId", "u"};
    return r;
}
constexpr auto kManagerSlots = ManagerSlots();
constexpr drawfx::Iface kManager{"TextureManager", kManagerSlotCount, kManagerSlots.data()};

constexpr unsigned kTextures = 5;
constexpr uint32_t kTargetCell = 0x10f8f94; // the texture manager's cache

// The viewer's script for one call, in the viewer fake's value words.
enum : unsigned { kDevice, kId, kCount, kTexBase, kTexNull, kRef94, kOwnSize, kScaleBits };

struct Viewer {
    Fake viewer;
    Fake manager;
    Fake textures[kTextures];
    uint32_t ownSize[3]; // the size the viewer answers from its own storage
    ViewerTextureState state;
} gv;

template <unsigned W> uint32_t Word(Fake &f, const uint32_t *) { return f.value[W]; }

Fake *TextureOrNull(uint32_t pick) { return pick < kTextures ? &gv.textures[pick] : nullptr; }

// What the state holds in the middle of a capture: at the device question (the count and scale are reset before
// it) and at each texture's AddRef (the cell is stored after it).
void NoteCellsNow(const char *label)
{
    const ViewerTextureState &s = gv.state;
    uint32_t scale;
    std::memcpy(&scale, &s.mScale, 4);
    drawfx::Note(label, {s.mCount, scale, drawfx::Norm(drawfx::P(s.mTextures[0])), drawfx::Norm(drawfx::P(s.mTextures[1])),
                         drawfx::Norm(drawfx::P(s.mTextures[2])), drawfx::Norm(drawfx::P(s.mTextures[3])),
                         drawfx::Norm(drawfx::P(s.mRef94))});
}
uint32_t DeviceAnswer(Fake &f, const uint32_t *)
{
    NoteCellsNow("state at device question");
    return f.value[kDevice];
}
uint32_t TextureAddRef(Fake &f, const uint32_t *)
{
    NoteCellsNow("state at AddRef");
    return uint32_t(++f.refs);
}

uint32_t GetTexture(Fake &f, const uint32_t *a)
{
    // Texture i: the base one rotated by i, null where the script's mask says so.
    if ((f.value[kTexNull] >> (a[0] & 7)) & 1) return 0;
    return drawfx::P(&gv.textures[(f.value[kTexBase] + a[0]) % kTextures]);
}
uint32_t TargetSize(Fake &f, const uint32_t *a)
{
    uint32_t *const into = f.value[kOwnSize] ? gv.ownSize : reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    into[0] = 640 + f.value[kScaleBits] % 7;
    into[1] = 480;
    into[2] = f.value[kScaleBits];
    return drawfx::P(into);
}

const uint32_t kDeviceAnswers[] = {0, 1, 0x100, 0xffffff00, 2, 0};
const uint32_t kIds[] = {0xffffffffu, 0xffffffffu, 0xffffffffu, 5, 0x77, 0};

uint32_t ScaleBits(std::mt19937 &rng)
{
    static const uint32_t kSpecial[] = {0x3f800000, 0x40000000, 0x7fc00000, 0xffc00001, 0x7f800001, 0x80000000, 0};
    return rng() % 3 ? kSpecial[rng() % 7] : uint32_t(rng());
}

// A fresh viewer script for the next call.
void Script(std::mt19937 &rng)
{
    Fake &v = gv.viewer;
    v.value[kDevice] = kDeviceAnswers[rng() % 6];
    v.value[kId] = kIds[rng() % 6];
    v.value[kCount] = rng() % 8 == 0 ? 5 : rng() % 5;
    v.value[kTexBase] = rng() % kTextures;
    v.value[kTexNull] = rng() % 3 ? 0 : rng() & 0x1f;
    v.value[kRef94] = drawfx::P(TextureOrNull(rng() % (kTextures + 2)));
    v.value[kOwnSize] = rng() % 2;
    v.value[kScaleBits] = ScaleBits(rng);
    drawfx::Note("script", {v.value[kDevice], v.value[kId], v.value[kCount], v.value[kTexBase], v.value[kTexNull],
                            drawfx::Norm(v.value[kRef94]), v.value[kOwnSize], v.value[kScaleBits]});
}

void Setup(uint32_t seed, drawfx::Cells &cells)
{
    std::mt19937 rng(seed * 2246822519u + 3u);
    gv.manager.Reset<kManager>(500);
    cells.Set(kTargetCell, &gv.manager);
    for (unsigned i = 0; i < kTextures; ++i) {
        gv.textures[i].Reset<kTex>(510 + i);
        gv.textures[i].On(1, &TextureAddRef);
    }
    gv.viewer.Reset<kViewer>(520);
    gv.viewer.On(105, &Word<kCount>);
    gv.viewer.On(106, &GetTexture);
    gv.viewer.On(107, &Word<kRef94>);
    gv.viewer.On(108, &Word<kId>);
    gv.viewer.On(110, &TargetSize);
    gv.viewer.On(113, &DeviceAnswer);
    // A leftover state: what a caller's stack held, the cells holding fakes the state counted.
    ViewerTextureState &s = gv.state;
    s.mCount = rng() % 6;
    s.mScale = 0.5f * float(rng() % 5);
    for (cIGZUnknown *&cell : s.mTextures) {
        Fake *f = TextureOrNull(rng() % (kTextures + 2));
        if (f) ++f->refs;
        cell = reinterpret_cast<cIGZUnknown *>(f);
    }
    Fake *ref = TextureOrNull(rng() % (kTextures + 2));
    if (ref) ++ref->refs;
    s.mRef94 = reinterpret_cast<cIGZUnknown *>(ref);
    s.mHandle = kIds[rng() % 6];
    s.mDeviceView = uint8_t(rng() % 3 == 0 ? 1 + rng() % 255 : 0);
    gv.ownSize[0] = gv.ownSize[1] = gv.ownSize[2] = 0;
}

void NoteState()
{
    const ViewerTextureState &s = gv.state;
    uint32_t scale;
    std::memcpy(&scale, &s.mScale, 4);
    drawfx::Note("state", {s.mCount, scale, drawfx::Norm(drawfx::P(s.mTextures[0])), drawfx::Norm(drawfx::P(s.mTextures[1])),
                           drawfx::Norm(drawfx::P(s.mTextures[2])), drawfx::Norm(drawfx::P(s.mTextures[3])),
                           drawfx::Norm(drawfx::P(s.mRef94)), s.mHandle, s.mDeviceView});
    for (const Fake &t : gv.textures) drawfx::Note("texture", {t.id, uint32_t(t.refs)});
    drawfx::Note("own size", {gv.ownSize[0], gv.ownSize[1], gv.ownSize[2]});
}

cIViewerRefNodeBase *AsViewer() { return reinterpret_cast<cIViewerRefNodeBase *>(&gv.viewer); }

using CaptureFn = void(__cdecl *)(void *, void *);
using ApplyFn = void(__cdecl *)(void *, const void *);

template <class Call> void Check(difftest::Context &ctx, const char *what, Call &&call)
{
    sgfx::CheckSides(ctx, what, 64, &Setup, [&](bool isOriginal, std::mt19937 &rng) {
        const unsigned calls = 1 + rng() % 6;
        for (unsigned k = 0; k < calls; ++k) {
            Script(rng);
            call(isOriginal, rng);
            NoteState();
        }
    });
}

} // namespace

DIFF_TEST(SG_Utilities_CaptureViewerTextures, 0xaf2450)
{
    const CaptureFn original = ctx.Original<CaptureFn>(0xaf2450);
    const ApplyFn apply = ctx.Original<ApplyFn>(0xaf23f0);
    Check(ctx, "CaptureViewerTextures", [&](bool isOriginal, std::mt19937 &rng) {
        // One call in four applies the state (the original's on both sides) between captures.
        if (rng() % 4 == 0) {
            drawfx::Note("apply", 0);
            apply(&gv.viewer, &gv.state);
            return;
        }
        drawfx::Note("capture", 0);
        if (isOriginal)
            original(&gv.state, &gv.viewer);
        else
            CaptureViewerTextures(&gv.state, AsViewer());
    });
}

DIFF_TEST(SG_Utilities_ApplyViewerTextures, 0xaf23f0)
{
    const ApplyFn original = ctx.Original<ApplyFn>(0xaf23f0);
    Check(ctx, "ApplyViewerTextures", [&](bool isOriginal, std::mt19937 &rng) {
        // The state's flag and handle varied between calls so all three branches are reached.
        if (rng() % 3 == 0) gv.state.mDeviceView = uint8_t(rng() % 2 ? 0 : 1u << (rng() % 8));
        if (rng() % 3 == 0) gv.state.mHandle = kIds[rng() % 6];
        drawfx::Note("apply", {gv.state.mDeviceView, gv.state.mHandle});
        if (isOriginal)
            original(&gv.viewer, &gv.state);
        else
            ApplyViewerTextures(AsViewer(), &gv.state);
    });
}
