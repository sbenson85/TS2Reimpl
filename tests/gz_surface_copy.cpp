// Differential tests for the surface copies (lane OS, rendering R3b step 4): src/gz/cSurfaceCopy.h.
//   ClipCopyRects 0x18ce70, CanStretchRectDX 0x162d60, CopySurfaceRectLocked 0x160ef0,
//   MakeSystemMemorySurfaceLike 0x161b20, CopySurfaceRectDX 0x1614b0.
//
// Each case builds the same seeded world for each side (the original at its RVA, ours), runs one call and compares the
// call log (every call on every fake with its arguments, the LogD3DError lines), the result and the out-arguments, the
// fakes' reference counts, the pixel buffers and their lock counts.
//
// The world (drawfx / d3dfx fakes):
// - Eight Direct3D surfaces (d3dfx::Level: description, pitch, buffer, results of GetDesc / LockRect / UnlockRect, a
//   container for GetContainer: none, or a texture-like fake of type texture, cube texture or something else, with an
//   answer of success, S_FALSE or E_NOINTERFACE). 0 is the copy's source, 1 its target, 2 and 3 the offscreen surfaces
//   the device makes, 4 and 5 the levels of the texture and cube texture it makes.
// - The device (IDirect3DDevice9 fake): StretchRect / UpdateSurface / GetRenderTargetData / BeginScene / EndScene answer
//   seeded results (the last two copy the source buffer to the target's), the Create* slots by d3dfx::Script.
// - The engine side: two surface faces (cISurfaceDX: QueryInterface for the Direct3D surface, GetSurface with its access
//   flags, GetDevice) over a cIDeviceDX fake (scene state, WaitForGpu, the device enumeration and the Direct3D device),
//   the device enumeration (CanStretchRect answers a seeded bit per call, GetCaps with seeded filter caps) and three
//   pixel formats with the formats manager the getter 0x18acd0 hands out (stood in on both sides: shape::CodePatch).
// Not covered by comparison: nothing is left out; every fake's calls are in the log.
#include "code_patch.h"
#include "d3dfx.h"
#include "difftest.h"
#include "gz/GraphicServices.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cSurfaceCopy.h"
#include "gz/cSurfaceDX.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <random>
#include <string>
#include <vector>

using namespace nGZGraphic4;

namespace {

// ---- engine-side slot tables ----
constexpr drawfx::Slot U{0, drawfx::Ret::Int, false, "", ""};
constexpr drawfx::Slot kQI{2, drawfx::Ret::Int, true, "QueryInterface", "ui"};
constexpr drawfx::Slot kAddRef{0, drawfx::Ret::Int, true, "AddRef", ""};
constexpr drawfx::Slot kRelease{0, drawfx::Ret::Int, true, "Release", ""};

// cISurfaceDX (the +4 face of a surface): 3 GetSurface, 4 GetSurfaceConst, 6 GetDevice, 7 GetD3DSurface.
inline constexpr drawfx::Slot kCopyFaceSlots[] = {
    kQI, kAddRef, kRelease,
    {0, drawfx::Ret::Int, true, "GetSurface", ""},
    {0, drawfx::Ret::Int, true, "GetSurfaceConst", ""},
    U,
    {0, drawfx::Ret::Int, true, "GetDevice", ""},
    {0, drawfx::Ret::Int, true, "GetD3DSurface", ""},
};
inline constexpr drawfx::Iface kCopyFace{"cISurfaceDX.copy", 8, kCopyFaceSlots};

// cISurface: slot 19 GetAccessFlags.
inline constexpr drawfx::Slot kCopySurfaceSlots[] = {
    kQI, kAddRef, kRelease, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    {0, drawfx::Ret::Int, true, "GetAccessFlags", ""},
};
inline constexpr drawfx::Iface kCopySurface{"cISurface.copy", 20, kCopySurfaceSlots};

inline constexpr drawfx::Slot kCopyDeviceSlots[] = {
    kQI, kAddRef, kRelease, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    U, U,
    {0, drawfx::Ret::Int, true, "InScene", ""}, // 36
    U, U, U, U, U, U, U, U,
    {0, drawfx::Ret::Int, true, "GetDeviceEnum45", ""}, // 45
    {0, drawfx::Ret::Int, true, "WaitForGpu", ""},      // 46
    U,
    {0, drawfx::Ret::Int, true, "GetD3DDevice", ""}, // 48
};
inline constexpr drawfx::Iface kCopyDevice{"cIDeviceDX.copy", 49, kCopyDeviceSlots};
static_assert(sizeof(kCopyDeviceSlots) / sizeof(kCopyDeviceSlots[0]) == 49, "cIDeviceDX slots");
static_assert(sizeof(kCopySurfaceSlots) / sizeof(kCopySurfaceSlots[0]) == 20, "cISurface slots");

inline constexpr drawfx::Slot kCopyEnumSlots[] = {
    kQI, kAddRef, kRelease, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    {8, drawfx::Ret::Int, true, "CanStretchRect", "upriupri"}, // 19
    U, U, U, U,
    {0, drawfx::Ret::Int, true, "GetCaps", ""}, // 24
};
inline constexpr drawfx::Iface kCopyEnum{"cIDeviceEnum.copy", 25, kCopyEnumSlots};
static_assert(sizeof(kCopyEnumSlots) / sizeof(kCopyEnumSlots[0]) == 25, "cIDeviceEnum slots");

inline constexpr drawfx::Slot kCopyFormatSlots[] = {
    kQI, kAddRef, kRelease,
    {0, drawfx::Ret::Int, true, "GetFormatId", ""},
    U, U,
    {0, drawfx::Ret::Int, true, "GetBitsPerPixel", ""},
    {0, drawfx::Ret::Int, true, "GetBlockHeight", ""},
};
inline constexpr drawfx::Iface kCopyFormat{"cIPixelFormat.copy", 8, kCopyFormatSlots};

inline constexpr drawfx::Slot kCopyFormatsSlots[] = {
    kQI, kAddRef, kRelease, U,
    {1, drawfx::Ret::Int, true, "GetFormat", "u"},
};
inline constexpr drawfx::Iface kCopyFormats{"cIPixelFormatsManager.copy", 5, kCopyFormatsSlots};

// ---- the world ----
constexpr unsigned kCopyLevels = 8, kCopyConts = 3, kCopyTex = 2, kCopyFormatCount = 3;
constexpr uint32_t kCopyDeviceId = 0x7d001, kCopyIidD3DSurface = 0x4a5a7223;

struct FormatRec {
    uint32_t id, bits, blockHeight;
};
struct World {
    d3dfx::Level level[kCopyLevels];
    d3dfx::Resource tex[kCopyTex];
    uint32_t flags[2];
    uint32_t inScene, canStretchBits, canStretchCalls;
    uint8_t caps[0x130];
    uint32_t hrStretch, hrUpdate, hrTarget, hrBegin, hrEnd;
    uint32_t contIndex[kCopyLevels], contHr[kCopyLevels], contType[kCopyConts];
    FormatRec format[kCopyFormatCount];
    RECT rect[4];
    POINT point;
    D3DSURFACE_DESC desc[2];
} gWorld;

drawfx::Fake gCopyFace[2], gCopySurf[2], gCopyDevice, gCopyEnum, gCopyFormat[kCopyFormatCount], gCopyFormats, gCopyD3D,
    gCopyLevel[kCopyLevels], gCopyCont[kCopyConts], gCopyTex[kCopyTex];
d3dfx::Script *gCopyScript = nullptr;

uint32_t Pick(std::mt19937 &g, std::initializer_list<uint32_t> v) { return v.begin()[g() % v.size()]; }
uint32_t CopyBitsFor(drawfx::Fake &, const uint32_t *);

uint32_t FaceQuery(drawfx::Fake &self, const uint32_t *a)
{
    void **out = reinterpret_cast<void **>(a[1]);
    if (a[0] == kCopyIidD3DSurface) {
        if (out) *out = self.owner; // not counted
        return 1;
    }
    if (out) *out = nullptr;
    return 0;
}
uint32_t FormatId(drawfx::Fake &f, const uint32_t *) { return gWorld.format[f.value[0]].id; }
uint32_t FormatBits(drawfx::Fake &f, const uint32_t *) { return gWorld.format[f.value[0]].bits; }
uint32_t FormatBlock(drawfx::Fake &f, const uint32_t *) { return gWorld.format[f.value[0]].blockHeight; }
uint32_t FormatsGet(drawfx::Fake &, const uint32_t *a) { return drawfx::P(&gCopyFormat[a[0] % kCopyFormatCount]); }
void *__cdecl StandFormats() { return &gCopyFormats; }

// CanStretchRect answers a seeded bit per call.
uint32_t EnumCanStretch(drawfx::Fake &, const uint32_t *)
{
    return (gWorld.canStretchBits >> (gWorld.canStretchCalls++ % 32)) & 1;
}
// GetContainer(iid, out): the level's container (counted) or none, with the level's answer.
uint32_t LevelContainer(drawfx::Fake &self, const uint32_t *a)
{
    d3dfx::Level *level = static_cast<d3dfx::Level *>(self.owner);
    const unsigned i = unsigned(level - gWorld.level);
    void **out = reinterpret_cast<void **>(a[1]);
    if (gWorld.contIndex[i]) {
        drawfx::Fake &c = gCopyCont[gWorld.contIndex[i] - 1];
        ++c.refs;
        if (out) *out = &c;
    } else if (out) {
        *out = nullptr;
    }
    return gWorld.contHr[i];
}
uint32_t ContainerType(drawfx::Fake &self, const uint32_t *) { return gWorld.contType[self.value[0]]; }
// GetRenderTargetData(source, target) and UpdateSurface(source, rect, target, point) copy the source's buffer.
uint32_t CopyBitsFor(drawfx::Fake &, const uint32_t *a)
{
    auto *s = reinterpret_cast<drawfx::Fake *>(a[0]);
    auto *t = reinterpret_cast<drawfx::Fake *>(a[1]);
    if (s && t && s != t)
        std::memcpy(static_cast<d3dfx::Level *>(t->owner)->bits, static_cast<d3dfx::Level *>(s->owner)->bits,
                    d3dfx::kLevelBytes);
    return gWorld.hrTarget;
}
uint32_t UpdateSurfaceCopy(drawfx::Fake &, const uint32_t *a)
{
    auto *s = reinterpret_cast<drawfx::Fake *>(a[0]);
    auto *t = reinterpret_cast<drawfx::Fake *>(a[2]);
    if (s && t && s != t)
        std::memcpy(static_cast<d3dfx::Level *>(t->owner)->bits, static_cast<d3dfx::Level *>(s->owner)->bits,
                    d3dfx::kLevelBytes);
    return gWorld.hrUpdate;
}

void SeedLevel(std::mt19937 &g, d3dfx::Level &l, bool twin)
{
    l = d3dfx::Level{};
    l.desc.Format = D3DFORMAT(Pick(g, {21, 22, 28, 41, 50}));
    l.desc.Type = D3DRTYPE_SURFACE;
    l.desc.Usage = twin ? 0 : Pick(g, {0, 0, 1, 1, 1, 2, 0x200, 3});
    l.desc.Pool = D3DPOOL(twin ? Pick(g, {2, 2, 2, 0, 1}) : Pick(g, {0, 0, 1, 2, 3}));
    l.desc.MultiSampleType = D3DMULTISAMPLE_TYPE(g() % 5 == 0 ? Pick(g, {1, 2, 4}) : 0);
    l.desc.MultiSampleQuality = g() % 6 == 0 ? 1 : 0;
    l.desc.Width = Pick(g, {1, 2, 4, 8, 16, 33});
    l.desc.Height = Pick(g, {1, 2, 4, 8, 16, 33});
    l.descHr = g() % 10 == 0 ? d3dfx::kResults[1 + g() % 3] : 0;
    l.lockHr = g() % 12 == 0 ? d3dfx::kResults[1 + g() % 3] : 0;
    l.unlockHr = g() % 12 == 0 ? d3dfx::kResults[1 + g() % 3] : 0;
    l.pitch = int32_t(Pick(g, {16, 32, 64}));
    l.priority = 0;
    l.device = &gCopyD3D;
    for (uint8_t &b : l.bits) b = uint8_t(g());
}

void Seed(uint32_t seed)
{
    std::mt19937 g(seed);
    World &w = gWorld;
    for (unsigned i = 0; i < kCopyLevels; ++i) SeedLevel(g, w.level[i], i >= 2);
    // The target often has the source's size (the whole-surface paths), the source rectangle often is the whole.
    if (g() % 2) {
        w.level[1].desc.Width = w.level[0].desc.Width;
        w.level[1].desc.Height = w.level[0].desc.Height;
    }
    if (g() % 3 == 0) w.level[1].desc.Pool = D3DPOOL_SYSTEMMEM;
    if (g() % 3 == 0) w.level[0].desc.Pool = D3DPOOL(Pick(g, {1, 2, 3}));
    for (unsigned i = 0; i < kCopyLevels; ++i) {
        w.contIndex[i] = g() % 3 == 0 ? 1 + g() % kCopyConts : 0;
        w.contHr[i] = w.contIndex[i] ? (g() % 8 == 0 ? Pick(g, {1, 0x80004005}) : 0) : uint32_t(E_NOINTERFACE);
        if (!w.contIndex[i] && g() % 6 == 0) w.contHr[i] = 1;
    }
    for (uint32_t &t : w.contType) t = Pick(g, {3, 5, 3, 5, 1, 4});
    for (unsigned r = 0; r < kCopyTex; ++r) {
        d3dfx::Resource &res = w.tex[r];
        res = d3dfx::Resource{};
        res.cube = r == 1;
        res.levels = 1;
        res.surface[0][0] = &gCopyLevel[4 + r];
        res.level[0][0] = &w.level[4 + r];
        res.surfaceHr = g() % 6 == 0 ? Pick(g, {0x8876086c, 1}) : 0;
    }
    for (unsigned s = 0; s < 2; ++s) w.flags[s] = g() % 5 == 0 ? Pick(g, {6, 7, 14, 15}) : g() % 16;
    w.inScene = g() % 2;
    w.canStretchBits = uint32_t(g());
    w.canStretchCalls = 0;
    std::memset(w.caps, 0, sizeof(w.caps));
    const uint32_t filterCaps = Pick(g, {0, 0x1000100, 0x2000200, 0x3000300, 0x100, 0x1000000});
    std::memcpy(w.caps + 0xf4, &filterCaps, 4);
    auto answer = [&](uint32_t oneIn) { return g() % oneIn == 0 ? d3dfx::kResults[g() % 4] : 0u; };
    w.hrStretch = answer(4);
    w.hrUpdate = answer(4);
    w.hrTarget = answer(4);
    w.hrBegin = answer(5);
    w.hrEnd = answer(5);
    for (FormatRec &f : w.format) {
        f.id = Pick(g, {21, 22, 28, 41, 50});
        f.bits = Pick(g, {4, 8, 16, 32});
        f.blockHeight = Pick(g, {1, 1, 4});
    }
    // Rectangles: whole, partly outside, empty or inverted in height (never in width: the row loop copies a width's bytes).
    const D3DSURFACE_DESC &sd = w.level[0].desc;
    for (RECT &r : w.rect) {
        if (g() % 3 == 0) {
            r = RECT{0, 0, LONG(sd.Width), LONG(sd.Height)};
        } else {
            r.left = LONG(int32_t(g() % 20) - 4);
            r.top = LONG(int32_t(g() % 20) - 4);
            r.right = r.left + LONG(g() % 8 == 0 ? 0 : int32_t(g() % 40));
            r.bottom = r.top + LONG(g() % 10 == 0 ? -1 : int32_t(g() % 40));
        }
    }
    // A point on the target's near edge but above the top (the target is cut only at its far edges): x 0 and y negative.
    const unsigned pointKind = g() % 4;
    w.point = pointKind < 2 ? POINT{0, 0}
              : pointKind == 2 ? POINT{0, LONG(-1 - int32_t(g() % 3))}
                               : POINT{LONG(int32_t(g() % 20) - 4), LONG(int32_t(g() % 20) - 4)};
    for (D3DSURFACE_DESC &d : w.desc) {
        d = D3DSURFACE_DESC{};
        d.Format = D3DFORMAT(Pick(g, {21, 22, 28, 41, 50}));
        d.Pool = D3DPOOL(g() % 4 == 0 ? Pick(g, {1, 2, 3}) : 0);
        d.Width = Pick(g, {1, 4, 33, 64});
        d.Height = Pick(g, {1, 4, 33, 64});
    }
}

void Build(uint32_t seed)
{
    std::mt19937 g(seed ^ 0x5eed);
    gCopyDevice.Reset<kCopyDevice>(0x7d010);
    gCopyDevice.Returns(36, gWorld.inScene);
    gCopyDevice.Returns(45, &gCopyEnum);
    gCopyDevice.Returns(48, &gCopyD3D);
    gCopyEnum.Reset<kCopyEnum>(0x7d011);
    gCopyEnum.On(19, &EnumCanStretch);
    gCopyEnum.Returns(24, gWorld.caps);
    for (unsigned i = 0; i < kCopyFormatCount; ++i) {
        gCopyFormat[i].Reset<kCopyFormat>(0x7d020 + i);
        gCopyFormat[i].value[0] = i;
        gCopyFormat[i].On(3, &FormatId);
        gCopyFormat[i].On(6, &FormatBits);
        gCopyFormat[i].On(7, &FormatBlock);
    }
    gCopyFormats.Reset<kCopyFormats>(0x7d030);
    gCopyFormats.On(4, &FormatsGet);
    gCopyD3D.Reset<fx_d3d9::IDirect3DDevice9>(kCopyDeviceId);
    gCopyD3D.Returns(fx_d3d9::dev::StretchRect, gWorld.hrStretch);
    gCopyD3D.On(fx_d3d9::dev::UpdateSurface, &UpdateSurfaceCopy);
    gCopyD3D.On(fx_d3d9::dev::GetRenderTargetData, &CopyBitsFor);
    gCopyD3D.Returns(fx_d3d9::dev::BeginScene, gWorld.hrBegin);
    gCopyD3D.Returns(fx_d3d9::dev::EndScene, gWorld.hrEnd);
    for (unsigned i = 0; i < kCopyLevels; ++i) {
        d3dfx::MakeLevel(gCopyLevel[i], 0x7d100 + i, gWorld.level[i]);
        gCopyLevel[i].On(d3dfx::res::GetContainer, &LevelContainer);
    }
    for (unsigned i = 0; i < kCopyConts; ++i) {
        gCopyCont[i].Reset<fx_d3d9::IDirect3DTexture9>(0x7d200 + i);
        gCopyCont[i].value[0] = i;
        gCopyCont[i].On(d3dfx::res::GetType, &ContainerType);
    }
    for (unsigned r = 0; r < kCopyTex; ++r) d3dfx::MakeResource(gCopyTex[r], 0x7d300 + r, gWorld.tex[r]);
    for (unsigned s = 0; s < 2; ++s) {
        gCopySurf[s].Reset<kCopySurface>(0x7d400 + s);
        gCopySurf[s].Returns(19, gWorld.flags[s]);
        gCopyFace[s].Reset<kCopyFace>(0x7d410 + s);
        gCopyFace[s].owner = &gCopyLevel[s];
        gCopyFace[s].On(0, &FaceQuery);
        gCopyFace[s].Returns(3, &gCopySurf[s]);
        gCopyFace[s].Returns(4, &gCopySurf[s]);
        gCopyFace[s].Returns(6, &gCopyDevice);
        gCopyFace[s].Returns(7, &gCopyLevel[s]);
    }
    static d3dfx::Script *script = nullptr;
    delete script;
    script = new d3dfx::Script(gCopyD3D);
    gCopyScript = script;
    // The device's creations: two offscreen surfaces, a texture and a cube texture.
    for (unsigned i = 0; i < 2; ++i) {
        const uint32_t hr = g() % 5 == 0 ? Pick(g, {1, 0x8876086c, 0x80004005}) : 0;
        script->Add(fx_d3d9::dev::CreateOffscreenPlainSurface, hr, int32_t(hr) >= 0 ? &gCopyLevel[2 + i] : nullptr);
    }
    for (unsigned slot : {fx_d3d9::dev::CreateTexture, fx_d3d9::dev::CreateCubeTexture}) {
        const unsigned r = slot == fx_d3d9::dev::CreateTexture ? 0 : 1;
        const uint32_t hr = g() % 5 == 0 ? Pick(g, {1, 0x8876086c, 0x80004005}) : 0;
        script->Add(slot, hr, int32_t(hr) >= 0 ? &gCopyTex[r] : nullptr);
    }
    drawfx::AddRange(&gWorld.rect, sizeof(gWorld.rect), 0x7a000000);
    drawfx::AddRange(&gWorld.point, sizeof(gWorld.point), 0x7a100000);
    drawfx::AddRange(gWorld.caps, sizeof(gWorld.caps), 0x7b000000);
}

void NoteRefs()
{
    std::vector<uint32_t> refs;
    for (drawfx::Fake *f : drawfx::gFakes) refs.push_back(uint32_t(f->refs));
    drawfx::Call c;
    c.note = "refs";
    c.words = std::move(refs);
    drawfx::gLog.push_back(std::move(c));
}
void NoteResult(uint32_t r) { drawfx::Note("result", r); }
void NoteRect(const char *label, const RECT &r)
{
    drawfx::Note(label, {uint32_t(r.left), uint32_t(r.top), uint32_t(r.right), uint32_t(r.bottom)});
}

struct Seen {
    unsigned trues = 0, falses = 0, reports = 0, locks = 0, creates = 0, device = 0;
};

struct After {
    drawfx::Log log;
    std::vector<std::string> errors;
    std::vector<uint8_t> pixels;
};

template <class Run> void Case(difftest::Context &ctx, const char *what, uint32_t seed, Seen &seen, Run run)
{
    auto side = [&](bool original) {
        After a;
        Seed(seed);
        auto [log, errors] = drawfx::Side([&] { Build(seed); }, [&] {
            drawfx::gLog.reserve(256);
            run(original);
            NoteRefs();
        });
        a.log = std::move(log);
        a.errors = std::move(errors);
        for (const d3dfx::Level &l : gWorld.level) {
            a.pixels.insert(a.pixels.end(), l.bits, l.bits + d3dfx::kLevelBytes);
            a.pixels.push_back(uint8_t(l.locks));
        }
        return a;
    };
    const After a = side(true);
    const After b = side(false);
    for (const drawfx::Call &c : a.log) {
        if (!c.iface && std::strcmp(c.note, "LogD3DError") == 0) ++seen.reports;
        if (c.iface && c.slot == fx_d3d9::dev::UpdateSurface) ++seen.device;
        if (c.iface && (c.slot == fx_d3d9::dev::StretchRect || c.slot == fx_d3d9::dev::GetRenderTargetData)) ++seen.device;
        if (c.iface && c.slot == d3dfx::res::LockRect) ++seen.locks;
        if (c.iface && (c.slot == fx_d3d9::dev::CreateOffscreenPlainSurface || c.slot == fx_d3d9::dev::CreateTexture ||
                        c.slot == fx_d3d9::dev::CreateCubeTexture))
            ++seen.creates;
        if (!c.iface && std::strcmp(c.note, "result") == 0) (c.words[0] ? seen.trues : seen.falses)++;
    }
    std::vector<std::string> errors = a.errors;
    errors.insert(errors.end(), b.errors.begin(), b.errors.end());
    if (!drawfx::Same(ctx, a.log, b.log, what, seed, errors)) return;
    if (a.pixels != b.pixels) ctx.Fail("%s seed %u: pixel buffers or lock counts differ", what, seed);
}

struct Fixture {
    d3dfx::ErrorLog errors;
    shape::StandIn formats{0x18acd0, &nGZGraphic4::GetPixelFormatsManagerCached, &StandFormats};
    explicit Fixture(difftest::Context &ctx) : errors(ctx) {}
};

using ClipFn = bool(__cdecl *)(RECT *, const RECT *, RECT *, const RECT *);
using CanStretchFn = bool(__cdecl *)(cISurfaceDX *, const D3DSURFACE_DESC *, const RECT *, cISurfaceDX *,
                                     const D3DSURFACE_DESC *, const RECT *, uint32_t *, bool);
using LockedFn = bool(__cdecl *)(IDirect3DSurface9 *, const RECT *, IDirect3DSurface9 *, const RECT *);
using TwinFn = bool(__cdecl *)(IDirect3DSurface9 **, IDirect3DTexture9 **, IDirect3DCubeTexture9 **, IDirect3DSurface9 *);
using CopyFn = bool(__cdecl *)(cISurfaceDX *, const RECT *, cISurfaceDX *, const POINT *);

IDirect3DSurface9 *D3DSurface(unsigned i) { return reinterpret_cast<IDirect3DSurface9 *>(&gCopyLevel[i]); }

} // namespace

DIFF_TEST(gz_SurfaceCopy_Clip, 0x18ce70)
{
    std::mt19937 g(0xc11b);
    auto word = [&](int32_t lo, int32_t span) { return LONG(lo + int32_t(g() % uint32_t(span))); };
    unsigned passed = 0, failed = 0;
    for (unsigned n = 0; n < 40000; ++n) {
        RECT source, sourceBounds, target, targetBounds;
        const unsigned shape = g() % 9;
        auto rect = [&](RECT &r, int32_t lo, int32_t span, int32_t size) {
            r.left = word(lo, span);
            r.top = word(lo, span);
            r.right = r.left + word(shape == 0 ? -2 : 0, size);
            r.bottom = r.top + word(shape == 1 ? -2 : 0, size);
        };
        rect(source, -8, 24, 40);
        rect(target, -8, 24, 40);
        rect(sourceBounds, -4, 8, 48);
        rect(targetBounds, -4, 8, 48);
        if (shape == 2) source.right = source.left; // zero width: 0 / 0
        if (shape == 3) target.bottom = target.top;
        if (shape == 4) sourceBounds = RECT{0, 0, 0, 0};
        if (shape == 5) targetBounds = RECT{0, 0, 1, 1};
        if (shape == 6) {
            source = RECT{0, 0, 64, 64};
            target = RECT{0, 0, 32, 32};
            sourceBounds = RECT{0, 0, 64, 64};
            targetBounds = RECT{0, 0, 16, 40};
        }
        if (shape == 8) { // the target starts exactly at the target bounds' far edge (the equality of the fail test)
            sourceBounds = RECT{0, 0, 64, 64};
            source = RECT{0, 0, 32, 32};
            targetBounds = RECT{0, 0, 20, 20};
            target = RECT{20, word(0, 4), 40, 16};
        }
        RECT s0 = source, t0 = target, s1 = source, t1 = target;
        const bool a = ctx.Original<ClipFn>(0x18ce70)(&s0, &sourceBounds, &t0, &targetBounds);
        const bool b = ClipCopyRects(&s1, &sourceBounds, &t1, &targetBounds);
        (a ? passed : failed)++;
        if (a != b || std::memcmp(&s0, &s1, sizeof(RECT)) || std::memcmp(&t0, &t1, sizeof(RECT))) {
            ctx.Fail("Clip %u: source %d %d %d %d, bounds %d %d %d %d, target %d %d %d %d, bounds %d %d %d %d: original %d "
                     "(%d %d %d %d | %d %d %d %d), ours %d (%d %d %d %d | %d %d %d %d)",
                     n, source.left, source.top, source.right, source.bottom, sourceBounds.left, sourceBounds.top,
                     sourceBounds.right, sourceBounds.bottom, target.left, target.top, target.right, target.bottom,
                     targetBounds.left, targetBounds.top, targetBounds.right, targetBounds.bottom, a, s0.left, s0.top,
                     s0.right, s0.bottom, t0.left, t0.top, t0.right, t0.bottom, b, s1.left, s1.top, s1.right, s1.bottom,
                     t1.left, t1.top, t1.right, t1.bottom);
            return;
        }
    }
    if (passed < 2000 || failed < 2000) ctx.Fail("Clip: a path never ran (%u passed, %u failed)", passed, failed);
}

DIFF_TEST(gz_SurfaceCopy_CanStretch, 0x162d60)
{
    Fixture fx(ctx);
    Seen seen;
    for (uint32_t seed = 1; seed <= 2000; ++seed) {
        Case(ctx, "CanStretch", seed, seen, [&](bool original) {
            std::mt19937 g(seed * 31 + 7);
            for (unsigned call = 0; call < 3; ++call) {
                const unsigned t = g() % 5 == 0 ? 0 : 1; // sometimes the same face
                uint32_t filter = 0xcdcdcdcd;
                const bool wantFilter = g() % 2;
                const unsigned sd = g() % 2, td = g() % 2;
                RECT sourceRect = gWorld.rect[g() % 4], targetRect = gWorld.rect[g() % 4];
                bool r;
                if (original)
                    r = ctx.Original<CanStretchFn>(0x162d60)(reinterpret_cast<cISurfaceDX *>(&gCopyFace[0]), &gWorld.desc[sd],
                                                             &sourceRect, reinterpret_cast<cISurfaceDX *>(&gCopyFace[t]),
                                                             &gWorld.desc[td], &targetRect, &filter, wantFilter);
                else
                    r = CanStretchRectDX(reinterpret_cast<cISurfaceDX *>(&gCopyFace[0]), &gWorld.desc[sd], &sourceRect,
                                         reinterpret_cast<cISurfaceDX *>(&gCopyFace[t]), &gWorld.desc[td], &targetRect,
                                         &filter, wantFilter);
                NoteResult(r);
                drawfx::Note("filter", filter);
            }
        });
    }
}

DIFF_TEST(gz_SurfaceCopy_Locked, 0x160ef0)
{
    Fixture fx(ctx);
    Seen seen;
    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        Case(ctx, "Locked", seed, seen, [&](bool original) {
            std::mt19937 g(seed * 13 + 5);
            const unsigned s = g() % 2 ? 0 : 2, t = g() % 2 ? 1 : 3;
            // (never the same buffer: the two sides' copies of overlapping bytes would be different calls)
            const RECT *sourceRect = &gWorld.rect[g() % 4], *targetRect = &gWorld.rect[g() % 4];
            const bool r = original ? ctx.Original<LockedFn>(0x160ef0)(D3DSurface(s), sourceRect, D3DSurface(t), targetRect)
                                    : CopySurfaceRectLocked(D3DSurface(s), sourceRect, D3DSurface(t), targetRect);
            NoteResult(r);
        });
    }
    if (!seen.locks || !seen.reports || !seen.trues || !seen.falses)
        ctx.Fail("Locked: a path never ran (locks %u, reports %u, true %u, false %u)", seen.locks, seen.reports, seen.trues,
                 seen.falses);
}

DIFF_TEST(gz_SurfaceCopy_Twin, 0x161b20)
{
    Fixture fx(ctx);
    Seen seen;
    for (uint32_t seed = 1; seed <= 3000; ++seed) {
        Case(ctx, "Twin", seed, seen, [&](bool original) {
            std::mt19937 g(seed * 19 + 3);
            IDirect3DSurface9 *surface = nullptr;
            IDirect3DTexture9 *texture = nullptr;
            IDirect3DCubeTexture9 *cube = nullptr;
            const unsigned source = g() % 2 ? 0 : 1;
            const bool r = original ? ctx.Original<TwinFn>(0x161b20)(&surface, &texture, &cube, D3DSurface(source))
                                    : MakeSystemMemorySurfaceLike(&surface, &texture, &cube, D3DSurface(source));
            NoteResult(r);
            drawfx::Note("made", {drawfx::Norm(drawfx::P(surface)), drawfx::Norm(drawfx::P(texture)),
                                  drawfx::Norm(drawfx::P(cube))});
            if (cube) cube->Release();
            if (texture) texture->Release();
            if (surface) surface->Release();
        });
    }
    if (!seen.creates || !seen.reports || !seen.trues || !seen.falses)
        ctx.Fail("Twin: a path never ran (creates %u, reports %u, true %u, false %u)", seen.creates, seen.reports,
                 seen.trues, seen.falses);
}

DIFF_TEST(gz_SurfaceCopy_Copy, 0x1614b0)
{
    Fixture fx(ctx);
    Seen seen;
    for (uint32_t seed = 1; seed <= 6000; ++seed) {
        Case(ctx, "Copy", seed, seen, [&](bool original) {
            std::mt19937 g(seed * 7 + 1);
            const RECT sourceRect = gWorld.rect[g() % 4];
            const POINT point = gWorld.point;
            const bool r = original ? ctx.Original<CopyFn>(0x1614b0)(reinterpret_cast<cISurfaceDX *>(&gCopyFace[0]),
                                                                    &sourceRect, reinterpret_cast<cISurfaceDX *>(&gCopyFace[1]),
                                                                    &point)
                                    : CopySurfaceRectDX(reinterpret_cast<cISurfaceDX *>(&gCopyFace[0]), &sourceRect,
                                                        reinterpret_cast<cISurfaceDX *>(&gCopyFace[1]), &point);
            NoteResult(r);
        });
    }
    if (!seen.device || !seen.locks || !seen.reports || !seen.creates || !seen.trues || !seen.falses)
        ctx.Fail("Copy: a path never ran (device %u, locks %u, reports %u, creates %u, true %u, false %u)", seen.device,
                 seen.locks, seen.reports, seen.creates, seen.trues, seen.falses);
}
