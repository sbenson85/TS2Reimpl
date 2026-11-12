// Differential test of the nTSSG render surfaces (lane 261108c, scenegraph TS1; src/sg/cTSRenderSurface.h).
//
//   SG_RenderSurfaceLife  one surface's life on each side: the back-buffer (constructor 0x36eb10), render-target
//       (0x36ebf0) or render-texture surface (0x36ec70, the base 0x36eb60 in each) built on a block filled alike, its
//       Init (0x3730e0 / 0x373100 / 0x373250, or the texture's SetExternal 0x3734d0), then random calls: the slots
//       (0-9, 19, 20 noted; SetScale; Create 0x372700 / 0x372790 / 0x372870; Destroy 0x375740 / 0x3757b0 / 0x3757d0;
//       IsValid 0x3736e0 / 0x373720; Apply 0x3751f0 on a pass whose device state is a fake; Clear 0x370f50; ClearRect
//       0x370fe0; CopyFrom 0x371210, CopyDepth 0x371120, CopyRect 0x371010 and CopyColor 0x3710a0 with another
//       surface's fake or the surface itself), the members (Configure 0x373390 / 0x3733f0 / 0x373450, Bind 0x375a90,
//       Reset 0x3759a0, ReleaseSurfaces 0x375950, ResetTexture 0x375990, Init again, SetExternal) and the fakes'
//       creation states, sizes and formats changing between them. The texture's Create / Destroy run ReparseShaders
//       0x3713b0 (the settings service or the material manager). The whole object is noted after every call (the strings
//       by text, size and capacity); at the end slot 21 (the deleting destructors 0x36fbd0 / 0x36fbf0 / 0x36fc10 with the
//       destructors 0x36f7f0 / 0x36f870 / 0x36f930), the fakes' counts and the heap.
//
// Stood in on both sides: the COM director getter 0x1ae18d (handing out the render target, the depth-stencil surface and
// the render-target texture fakes), the formats manager getter 0x18acd0 (slots 6 and 11 answer format fakes), and the
// device's multisample switch 0x18afc0 (not ours: notes its arguments, answers true or false). Cells: the texture
// manager 0x10f8f94 (Add answers new ids, GetTexture the render-target texture fake), the material manager 0x10f8fcc and
// the framework's service object 0x10c9378 (none, or one whose settings service notes the property set). The game's
// operator new / delete go to ours, so the original's string buffers and blocks are counted like ours.
#include "sgfx.h"

#include "code_patch.h"

#include "gz/GraphicServices.h"
#include "gz/cIGZPropertySet.h"
#include "rz/StaticGetters.h"
#include "sg/cDisplayList.h"
#include "sg/cTSRenderSurface.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>

namespace {

using namespace nTSSG;
using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;
using A = cTSRenderSurfaceAdapters;

struct At {
    unsigned slot;
    Slot s;
};
template <size_t N, size_t K> constexpr std::array<Slot, N> Table(const At (&at)[K])
{
    std::array<Slot, N> t{};
    for (Slot &s : t) s = {0, Ret::Int, false, "", ""};
    t[0] = {2, Ret::Int, true, "QueryInterface", "ua"};
    t[1] = {0, Ret::Int, true, "AddRef", ""};
    t[2] = {0, Ret::Int, true, "Release", ""};
    for (const At &a : at) t[a.slot] = a.s;
    return t;
}

// cIDeviceDX: 27 Clear, 28 ClearTargets, 35 IsDeviceReady, 39 GetBackBuffer, 40 GetTargetProviderSurface.
constexpr At kDeviceAt[] = {{27, {4, Ret::Int, true, "Clear", "uF4fu"}},
                            {28, {6, Ret::Int, true, "ClearTargets", "aruF4fu"}},
                            {35, {0, Ret::Int, true, "IsDeviceReady", ""}},
                            {39, {1, Ret::Int, true, "GetBackBuffer", "u"}},
                            {40, {0, Ret::Int, true, "GetTargetProviderSurface", ""}}};
constexpr auto kDeviceSlots = Table<41>(kDeviceAt);
constexpr drawfx::Iface kDevice{"Device", 41, kDeviceSlots.data()};

// cISurface (and its render-target / depth-stencil leaves): 3 ReleaseResource, 6 IsCreated, 15 CopyRect, 16 Copy,
// 17 StretchRect, 18 Stretch, 20 GetWidth, 21 GetHeight, 22 GetPixelFormat, 25 Create.
constexpr At kSurfaceAt[] = {{3, {0, Ret::Int, true, "ReleaseResource", ""}},
                             {6, {0, Ret::Int, true, "IsCreated", ""}},
                             {15, {3, Ret::Int, true, "CopyRect", "par"}},
                             {16, {1, Ret::Int, true, "Copy", "a"}},
                             {17, {3, Ret::Int, true, "StretchRect", "rar"}},
                             {18, {1, Ret::Int, true, "Stretch", "a"}},
                             {20, {0, Ret::Int, true, "GetWidth", ""}},
                             {21, {0, Ret::Int, true, "GetHeight", ""}},
                             {22, {0, Ret::Int, true, "GetPixelFormat", ""}},
                             {25, {6, Ret::Int, true, "Create", "auuauu"}}};
constexpr auto kSurfaceSlots = Table<27>(kSurfaceAt);
constexpr drawfx::Iface kSurface{"Surface", 27, kSurfaceSlots.data()};

// cIRenderTargetTexture: 6 IsCreated, 9 Create, 17 GetTargetSurface, 18 GetDepthSurface.
constexpr At kTextureAt[] = {{6, {0, Ret::Int, true, "IsCreated", ""}},
                             {9, {8, Ret::Int, true, "Create", "uuuuuuF4u"}},
                             {17, {1, Ret::Int, true, "GetTargetSurface", "u"}},
                             {18, {0, Ret::Int, true, "GetDepthSurface", ""}}};
constexpr auto kTextureSlots = Table<20>(kTextureAt);
constexpr drawfx::Iface kTexture{"RenderTargetTexture", 20, kTextureSlots.data()};

// Another render surface (cITSRenderSurface, no QueryInterface: its slots 0-2 are not COM's): 19 GetColorSurface,
// 20 GetDepthSurface.
constexpr std::array<Slot, 22> OtherSlots()
{
    std::array<Slot, 22> t{};
    for (Slot &s : t) s = {0, Ret::Int, false, "", ""};
    t[19] = {0, Ret::Int, true, "GetColorSurface", ""};
    t[20] = {0, Ret::Int, true, "GetDepthSurface", ""};
    return t;
}
constexpr auto kOtherSlots = OtherSlots();
constexpr drawfx::Iface kOther{"RenderSurface", 22, kOtherSlots.data()};

constexpr At kComAt[] = {{3, {3, Ret::Int, true, "GetClassObject", "uua"}}};
constexpr auto kComSlots = Table<4>(kComAt);
constexpr drawfx::Iface kCom{"COM", 4, kComSlots.data()};

constexpr At kStateAt[] = {{12, {1, Ret::Int, true, "ApplyStateCommand", "a"}}};
constexpr auto kStateSlots = Table<13>(kStateAt);
constexpr drawfx::Iface kState{"DeviceState", 13, kStateSlots.data()};

// The formats manager: 6 GetARGB, 11 GetDepthStencil.
constexpr At kManagerAt[] = {{6, {5, Ret::Int, true, "GetARGB", "uuuuu"}},
                             {11, {4, Ret::Int, true, "GetDepthStencil", "uuuu"}}};
constexpr auto kManagerSlots = Table<12>(kManagerAt);
constexpr drawfx::Iface kManager{"FormatsManager", 12, kManagerSlots.data()};

// cITextureManager: 18 AddRefId, 19 ReleaseId, 20 Add, 23 GetTexture.
constexpr At kTexturesAt[] = {{18, {1, Ret::Int, true, "AddRefId", "u"}},
                              {19, {1, Ret::Int, true, "ReleaseId", "u"}},
                              {20, {3, Ret::Int, true, "Add", "asu"}},
                              {23, {1, Ret::Int, true, "GetTexture", "u"}}};
constexpr auto kTexturesSlots = Table<24>(kTexturesAt);
constexpr drawfx::Iface kTextures{"TextureManager", 24, kTexturesSlots.data()};

// cIMaterialManager: 24 ReloadAll.
constexpr At kMaterialsAt[] = {{24, {2, Ret::Int, true, "ReloadAll", "su"}}};
constexpr auto kMaterialsSlots = Table<25>(kMaterialsAt);
constexpr drawfx::Iface kMaterials{"MaterialManager", 25, kMaterialsSlots.data()};

// The framework (5 GetSystemService) and the settings service (3 SetProperty: the key and the value noted by the
// handler; the pointers are stack addresses).
constexpr At kFrameworkAt[] = {{5, {3, Ret::Int, true, "GetSystemService", "uua"}}};
constexpr auto kFrameworkSlots = Table<6>(kFrameworkAt);
constexpr drawfx::Iface kFramework{"Framework", 6, kFrameworkSlots.data()};
constexpr At kSettingsAt[] = {{3, {2, Ret::Int, true, "SetProperty", "aa"}}};
constexpr auto kSettingsSlots = Table<4>(kSettingsAt);
constexpr drawfx::Iface kSettings{"Settings", 4, kSettingsSlots.data()};

constexpr uint32_t kRenderTargetClass = 0x5b75b757, kDepthStencilClass = 0x7b75b73a, kTextureClass = 0x2b96cc2b;
constexpr uint32_t kTexturesCell = 0x10f8f94, kMaterialsCell = 0x10f8fcc, kFrameworkCell = 0x10c9378;
constexpr unsigned kSurfaces = 8, kFormats = 3;
enum Kind : unsigned { kBackbuffer, kTarget, kTextureSurface };
constexpr uint32_t kCtor[] = {0x36eb10, 0x36ebf0, 0x36ec70};
constexpr uint32_t kSize[] = {0x70, 0x74, 0xc4};

struct World {
    Fake device, com, state, manager, textures, materials, framework, settings;
    Fake target, stencil, texture, external, other;
    Fake surfaces[kSurfaces], formats[kFormats];
    uint32_t lcg = 1;
    uint32_t Next()
    {
        lcg = lcg * 1664525u + 1013904223u;
        return lcg >> 8;
    }
};
World gw;

uint32_t Word0(Fake &self, const uint32_t *) { return self.value[0]; }
uint32_t Word1(Fake &self, const uint32_t *) { return self.value[1]; }
uint32_t Word2(Fake &self, const uint32_t *) { return self.value[2]; }
uint32_t Word3(Fake &self, const uint32_t *) { return self.value[3]; }
uint32_t Word4(Fake &self, const uint32_t *) { return self.value[4]; }

// A creation: whether it took is what IsCreated answers afterwards.
uint32_t Made(Fake &self, const uint32_t *)
{
    self.value[0] = gw.Next() % 4 != 0;
    return self.value[0];
}
uint32_t GetClassObject(Fake &, const uint32_t *a)
{
    Fake *made = a[0] == kRenderTargetClass ? &gw.target : a[0] == kDepthStencilClass ? &gw.stencil : &gw.texture;
    ++made->refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = made;
    return 1;
}
uint32_t TextureSurface(Fake &self, const uint32_t *a) { return drawfx::P(&gw.surfaces[(self.value[1] + a[0]) % kSurfaces]); }
uint32_t Added(Fake &self, const uint32_t *) { return 0x100 + ++self.value[0]; }
uint32_t GetTexture(Fake &, const uint32_t *) { return drawfx::P(&gw.texture); }
uint32_t GetService(Fake &self, const uint32_t *a)
{
    if (!self.value[0]) return 0;
    ++gw.settings.refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &gw.settings;
    return 1;
}
uint32_t SetProperty(Fake &, const uint32_t *a)
{
    difftest::NotCounted bookkeeping;
    sgfx::NoteText("property", reinterpret_cast<const GZServiceString *>(uintptr_t(a[0]))->c_str());
    const uint32_t *value = reinterpret_cast<const uint32_t *>(uintptr_t(a[1]));
    drawfx::Note("property value", {drawfx::Ptr(value[0]), value[1] & 0xff});
    return 1;
}

void NoteState(uint32_t address)
{
    difftest::NotCounted bookkeeping;
    const uint32_t *w = reinterpret_cast<const uint32_t *>(uintptr_t(address));
    drawfx::Note("command", {drawfx::Ptr(w[0]), w[1] & 0xff, w[1] >> 16});
    drawfx::Note("command targets", {drawfx::Ptr(w[2]), drawfx::Ptr(w[3]), drawfx::Ptr(w[4]), drawfx::Ptr(w[5]),
                                     drawfx::Ptr(w[6])});
}
uint32_t ApplyState(Fake &, const uint32_t *a)
{
    NoteState(a[0]);
    return 0;
}
uint32_t ClearTargets(Fake &, const uint32_t *a)
{
    NoteState(a[0]);
    return 0;
}
uint32_t BackBuffer(Fake &, const uint32_t *) { return drawfx::P(&gw.surfaces[0]); }
uint32_t ProviderSurface(Fake &, const uint32_t *) { return drawfx::P(&gw.surfaces[1]); }

void *StandCom() { return &gw.com; }
void *__cdecl StandManager() { return &gw.manager; }
bool __cdecl StandMultisample(uint32_t enable, uint32_t quality)
{
    drawfx::Note("multisample", {enable, quality});
    return gw.Next() % 3 != 0;
}
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
void __cdecl GameFree(void *p) { ::operator delete(p); }

struct Stands {
    shape::StandIn com{0x1ae18d, &GetDirectorFrameworkObject, &StandCom};
    shape::StandIn manager{0x18acd0, &nGZGraphic4::GetPixelFormatsManagerCached, &StandManager};
    shape::CodePatch multisample{0x18afc0u, shape::CodeOf(&StandMultisample)};
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
    shape::CodePatch gameFree{0x82540fu, shape::CodeOf(&GameFree)};
};

Fake *AnySurface(uint32_t r) { return r % 5 == 0 ? nullptr : &gw.surfaces[(r / 5) % kSurfaces]; }

void Build(uint32_t seed, drawfx::Cells &cells)
{
    gw.lcg = seed * 2654435761u + 7;
    gw.device.Reset<kDevice>(660);
    gw.device.value[0] = seed % 6 != 0;
    gw.device.On(28, &ClearTargets);
    gw.device.On(35, &Word0);
    gw.device.On(39, &BackBuffer);
    gw.device.On(40, &ProviderSurface);
    gw.com.Reset<kCom>(661);
    gw.com.On(3, &GetClassObject);
    gw.state.Reset<kState>(662);
    gw.state.On(12, &ApplyState);
    gw.manager.Reset<kManager>(663);
    gw.manager.Returns(6, &gw.formats[0]);
    gw.manager.Returns(11, &gw.formats[1]);
    gw.textures.Reset<kTextures>(664);
    gw.textures.On(20, &Added);
    gw.textures.On(23, &GetTexture);
    gw.materials.Reset<kMaterials>(665);
    gw.framework.Reset<kFramework>(666);
    gw.framework.value[0] = seed % 4 != 0;
    gw.framework.On(5, &GetService);
    gw.settings.Reset<kSettings>(667);
    gw.settings.On(3, &SetProperty);
    for (Fake *f : {&gw.target, &gw.stencil}) {
        f->Reset<kSurface>(f == &gw.target ? 670 : 671);
        f->On(6, &Word0);
        f->On(25, &Made);
        f->On(20, &Word1);
        f->On(21, &Word2);
        f->On(22, &Word3);
        f->value[1] = 64 + seed % 3;
        f->value[2] = 32;
        f->value[3] = drawfx::P(&gw.formats[seed % kFormats]);
    }
    for (Fake *f : {&gw.texture, &gw.external}) {
        f->Reset<kTexture>(f == &gw.texture ? 672 : 673);
        f->On(6, &Word0);
        f->On(9, &Made);
        f->On(17, &TextureSurface);
        f->On(18, &Word4);
        f->AddFace(0x0b96cc22u, *f);
        f->value[0] = seed % 3 != 0;
        f->value[1] = f == &gw.texture ? 2 : 5;
        f->value[4] = seed % 5 ? drawfx::P(&gw.surfaces[f == &gw.texture ? 3 : 6]) : 0;
    }
    gw.other.Reset<kOther>(674);
    gw.other.On(19, &Word0);
    gw.other.On(20, &Word1);
    gw.other.value[0] = drawfx::P(&gw.surfaces[4]);
    gw.other.value[1] = drawfx::P(&gw.surfaces[5]);
    static constexpr uint32_t kSides[] = {0, 1, 640, 1024, 0x80000001u};
    for (unsigned i = 0; i < kSurfaces; ++i) {
        Fake &s = gw.surfaces[i];
        s.Reset<kSurface>(680 + i);
        s.On(6, &Word0);
        s.On(20, &Word1);
        s.On(21, &Word2);
        s.On(22, &Word3);
        s.value[0] = (seed + i) % 3 != 0;
        s.value[1] = kSides[(seed + i) % 5];
        s.value[2] = kSides[(seed / 5 + i) % 5];
        s.value[3] = drawfx::P(&gw.formats[(seed + i * 7) % kFormats]);
    }
    for (unsigned i = 0; i < kFormats; ++i) gw.formats[i].Reset<sgfx::kUnknown>(700 + i);
    cells.Set(kTexturesCell, &gw.textures);
    cells.Set(kMaterialsCell, &gw.materials);
    cells.Set(kFrameworkCell, seed % 9 ? &gw.framework : nullptr);
}

void NoteFakes()
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = "fake refs";
    for (Fake *f : {&gw.device, &gw.com, &gw.state, &gw.manager, &gw.textures, &gw.materials, &gw.framework,
                    &gw.settings, &gw.target, &gw.stencil, &gw.texture, &gw.external, &gw.other})
        c.words.push_back(uint32_t(f->refs));
    for (const Fake &f : gw.surfaces) c.words.push_back(uint32_t(f.refs));
    drawfx::gLog.push_back(std::move(c));
}

// ---- the object ----

uint32_t W(const void *o, uint32_t at) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(o) + at); }
uint8_t B(const void *o, uint32_t at) { return static_cast<const uint8_t *>(o)[at]; }

// A cRZString member: its vtable, text, size, capacity and count (the buffer's address is each side's own).
void NoteString(const char *label, const void *o, uint32_t at)
{
    difftest::NotCounted bookkeeping;
    const auto *text = reinterpret_cast<const std::string *>(static_cast<const uint8_t *>(o) + at + 4);
    drawfx::Note(label, {drawfx::Ptr(W(o, at)), W(o, at + 0x14), W(o, at + 0x18), W(o, at + 0x1c)});
    sgfx::NoteText(label, text->c_str());
}

void NoteObject(Kind kind, const void *o)
{
    difftest::NotCounted bookkeeping;
    drawfx::Note("requested", {W(o, 4), W(o, 8)});
    drawfx::Note("device, bound", {drawfx::Ptr(W(o, 0xc)), drawfx::Ptr(W(o, 0x10)), drawfx::Ptr(W(o, 0x14))});
    drawfx::Note("created, size, scale, depth", {W(o, 0x18), W(o, 0x1c), W(o, 0x20), W(o, 0x24), W(o, 0x28)});
    NoteState(drawfx::P(static_cast<const uint8_t *>(o) + 0x2c));
    drawfx::Note("samples", W(o, 0x48));
    NoteString("name", o, 0x4c);
    if (kind == kBackbuffer) {
        drawfx::Note("changing multisample", W(o, 0x6c));
        return;
    }
    drawfx::Note("surfaces", {drawfx::Ptr(W(o, 0x6c)), drawfx::Ptr(W(o, 0x70))});
    if (kind != kTextureSurface) return;
    drawfx::Note("alpha, reparse", W(o, 0x74));
    NoteString("material", o, 0x78);
    drawfx::Note("external", drawfx::Ptr(W(o, 0x98)));
    NoteString("texture", o, 0x9c);
    drawfx::Note("handle, aspect", {W(o, 0xbc), W(o, 0xc0)});
}

const char *AnyName(uint32_t r)
{
    static const char *const kNames[] = {"", "a", "ShadowMap", "ExternalTexture", "ReflectionTargetWithALongerName_01"};
    return kNames[r % 5];
}

alignas(16) uint8_t gPass[sizeof(nGZSceneGraph::Pass)];

nGZSceneGraph::Pass *FreshPass(std::mt19937 &rng)
{
    std::memset(gPass, 0, sizeof gPass);
    auto *const pass = reinterpret_cast<nGZSceneGraph::Pass *>(gPass);
    pass->mDeviceState = reinterpret_cast<nGZGraphic4::cDeviceStateDX *>(&gw.state);
    pass->mTextureCount = rng() % 3;
    pass->mTextures[0] = reinterpret_cast<cIGZUnknown *>(AnySurface(rng()));
    pass->mTextureRef94 = reinterpret_cast<cIGZUnknown *>(AnySurface(rng()));
    return pass;
}
void NotePass(const nGZSceneGraph::Pass *pass)
{
    drawfx::Note("pass", {pass->mTextureCount, drawfx::Ptr(pass->mTextures[0]), drawfx::Ptr(pass->mTextureRef94)});
}

struct Calls {
    difftest::Context &ctx;
    bool original;
    void Init(Kind kind, void *o, std::mt19937 &rng)
    {
        const char *name = AnyName(rng());
        const char *material = AnyName(rng());
        const bool reparse = rng() % 2, hasDepth = rng() % 3 != 0, alpha = rng() % 2;
        drawfx::Note("init", {uint32_t(kind), uint32_t(reparse), uint32_t(hasDepth), uint32_t(alpha)});
        if (kind == kBackbuffer) {
            if (original) ctx.Original<void(__thiscall *)(void *, const char *, bool)>(0x3730e0)(o, name, hasDepth);
            else A::InitBackbuffer(static_cast<A::Backbuffer *>(o), nullptr, name, hasDepth);
        } else if (kind == kTarget) {
            if (original) ctx.Original<void(__thiscall *)(void *, const char *, bool)>(0x373100)(o, name, hasDepth);
            else A::InitTarget(static_cast<A::Target *>(o), nullptr, name, hasDepth);
        } else {
            using F = void(__thiscall *)(void *, const char *, const char *, bool, bool, bool);
            if (original) ctx.Original<F>(0x373250)(o, material, name, reparse, hasDepth, alpha);
            else A::InitTexture(static_cast<A::Texture *>(o), nullptr, material, name, reparse, hasDepth, alpha);
        }
    }
    void SetExternal(void *o)
    {
        if (original) ctx.Original<void(__thiscall *)(void *, void *)>(0x3734d0)(o, &gw.external);
        else A::SetExternal(static_cast<A::Texture *>(o), nullptr,
                            reinterpret_cast<nGZSceneGraph::cIRenderTargetTexture *>(&gw.external));
    }
    void Configure(Kind kind, void *o, std::mt19937 &rng)
    {
        static constexpr uint32_t kSides[] = {0, 16, 64, 640, 1024};
        auto *const device = rng() % 4 ? reinterpret_cast<nGZGraphic4::cIDeviceDX *>(&gw.device) : nullptr;
        const uint32_t width = kSides[rng() % 5], height = kSides[rng() % 5], samples = rng() % 5;
        drawfx::Note("configure", {drawfx::Ptr(device), width, height, samples});
        if (kind == kBackbuffer) {
            auto *const real = reinterpret_cast<nGZGraphic4::cIDeviceDX *>(&gw.device); // asked unchecked
            if (original) ctx.Original<void(__thiscall *)(void *, void *, uint32_t)>(0x373390)(o, real, samples);
            else A::ConfigureBackbuffer(static_cast<A::Backbuffer *>(o), nullptr, real, samples);
        } else if (kind == kTarget) {
            using F = void(__thiscall *)(void *, void *, uint32_t, uint32_t, uint32_t);
            if (original) ctx.Original<F>(0x3733f0)(o, device, width, height, samples);
            else A::ConfigureTarget(static_cast<A::Target *>(o), nullptr, device, width, height, samples);
        } else {
            using F = void(__thiscall *)(void *, void *, uint32_t, uint32_t);
            if (original) ctx.Original<F>(0x373450)(o, device, width, height);
            else A::ConfigureTexture(static_cast<A::Texture *>(o), nullptr, device, width, height);
        }
    }
    void Bind(void *o, std::mt19937 &rng)
    {
        Fake *const color = AnySurface(rng()), *const depth = AnySurface(rng());
        drawfx::Note("bind", {drawfx::Ptr(color), drawfx::Ptr(depth)});
        if (original) ctx.Original<void(__thiscall *)(void *, void *, void *)>(0x375a90)(o, color, depth);
        else A::Bind(static_cast<A::Impl *>(o), nullptr, reinterpret_cast<cISurface *>(color),
                     reinterpret_cast<cISurface *>(depth));
    }
    void Member(uint32_t rva, void *o)
    {
        if (original) {
            ctx.Original<void(__thiscall *)(void *)>(rva)(o);
            return;
        }
        if (rva == 0x3759a0) A::Reset(static_cast<A::Target *>(o), nullptr);
        else if (rva == 0x375950) A::ReleaseSurfaces(static_cast<A::Target *>(o), nullptr);
        else A::ResetTexture(static_cast<A::Texture *>(o), nullptr);
    }
};

void Step(Calls &calls, Kind kind, void *o, std::mt19937 &rng)
{
    auto *const s = static_cast<cITSRenderSurface *>(o);
    // The target vtable's hooked slots: our side calls the hooks' adapters (the original side its vtable).
    auto *const target = static_cast<A::Target *>(o);
    const bool ours = kind == kTarget && !calls.original;
    const unsigned op = rng() % 20;
    drawfx::Note("op", op);
    const bool device = W(o, 0xc) != 0, color = W(o, 0x10) != 0, depthOk = !B(o, 0x28) || W(o, 0x14) != 0;
    auto *const other = reinterpret_cast<cITSRenderSurface *>(&gw.other);
    cITSRenderSurface *const source = rng() % 4 ? other : s;
    int32_t rect[4] = {int32_t(rng() % 50), int32_t(rng() % 50), int32_t(rng() % 700), int32_t(rng() % 700)};
    switch (op) {
    case 0:
    case 1:
        // Each kind's own slots (the texture's GetAspect is 0x372660, slot 4).
        drawfx::Note("sizes", {s->GetWidth(), s->GetHeight(), std::bit_cast<uint32_t>(s->GetScale()),
                               std::bit_cast<uint32_t>(s->GetAspect())});
        drawfx::Note("flags", {uint32_t(s->HasDepth()), uint32_t(s->IsMultisampled()), uint32_t(s->IsTexture()),
                               uint32_t(ours ? A::IsValidTarget(target, nullptr) : s->IsValid()), uint32_t(s->IsCreated())});
        drawfx::Note("surfaces", {drawfx::Ptr(s->GetColorSurface()), drawfx::Ptr(s->GetDepthSurface())});
        break;
    case 2:
        s->SetScale(0.25f * float(rng() % 9));
        break;
    case 3:
    case 4:
        // The target surface's Create asks its render target unchecked: not after ReleaseSurfaces, unless created.
        if (kind != kTarget || W(o, 0x6c) || B(o, 0x18)) ours ? A::CreateTarget(target, nullptr) : s->Create();
        break;
    case 5:
        ours ? A::DestroyTarget(target, nullptr) : s->Destroy();
        break;
    case 6:
        calls.Configure(kind, o, rng);
        break;
    case 7: {
        nGZSceneGraph::Pass *const pass = FreshPass(rng);
        s->Apply(pass, rng() % 2);
        NotePass(pass);
        break;
    }
    case 8:
        if (device) {
            nGZSceneGraph::Pass *const pass = FreshPass(rng);
            s->Clear(pass, rng() % 8, 0.5f * float(rng() % 3), rng() % 4);
            NotePass(pass);
        }
        break;
    case 9:
        if (device) s->ClearRect(nullptr, rng() % 3 ? rect : nullptr, rng() % 8);
        break;
    case 10:
        if (source == s || (depthOk && color)) s->CopyFrom(source);
        break;
    case 11:
        if (depthOk) s->CopyDepth(source);
        break;
    case 12:
        if (source == s || (color && gw.other.value[0])) s->CopyRect(source, reinterpret_cast<const RECT *>(rect));
        break;
    case 13: {
        const bool stretch = rng() % 2;
        if (source == s || (color && (stretch || gw.other.value[0]))) s->CopyColor(source, stretch);
        break;
    }
    case 14:
        calls.Bind(o, rng);
        break;
    case 15:
        if (kind == kBackbuffer) calls.Init(kind, o, rng);
        else calls.Member(0x3759a0, o);
        break;
    case 16:
        if (kind != kBackbuffer) calls.Member(0x375950, o);
        break;
    case 17:
        if (kind == kTextureSurface && rng() % 2) calls.SetExternal(o);
        else if (kind == kTextureSurface) calls.Member(0x375990, o);
        else calls.Init(kind, o, rng);
        break;
    case 18:
        if (kind != kBackbuffer) calls.Init(kind, o, rng);
        break;
    default: {
        // The device objects lost or regained behind the surface's back; sizes and formats changing.
        gw.device.value[0] = rng() % 4 != 0;
        for (Fake *f : {&gw.target, &gw.stencil, &gw.texture, &gw.external}) f->value[0] = rng() % 2;
        Fake &changed = gw.surfaces[rng() % kSurfaces];
        changed.value[0] = rng() % 2;
        changed.value[1] = rng() % 2048;
        changed.value[2] = rng() % 2048;
        changed.value[3] = drawfx::P(&gw.formats[rng() % kFormats]);
        gw.other.value[0] = drawfx::P(AnySurface(rng()));
        gw.other.value[1] = drawfx::P(AnySurface(rng()));
        gw.texture.value[4] = drawfx::P(AnySurface(rng()));
        break;
    }
    }
    NoteObject(kind, o);
}

void RunLife(difftest::Context &ctx, uint32_t seed, bool original, std::mt19937 &rng)
{
    const Kind kind = Kind(seed % 3);
    Calls calls{ctx, original};
    difftest::AllocationCounter counter;
    void *const o = ::operator new(kSize[kind]);
    for (uint32_t i = 0; i < kSize[kind]; ++i) static_cast<uint8_t *>(o)[i] = uint8_t(rng());
    if (original) ctx.Original<void *(__thiscall *)(void *)>(kCtor[kind])(o);
    else if (kind == kBackbuffer) A::ConstructBackbuffer(static_cast<A::Backbuffer *>(o), nullptr);
    else if (kind == kTarget) A::ConstructTarget(static_cast<A::Target *>(o), nullptr);
    else A::ConstructTexture(static_cast<A::Texture *>(o), nullptr);
    drawfx::Note("made", drawfx::Ptr(o));
    NoteObject(kind, o);
    if (kind == kTextureSurface && rng() % 4 == 0) calls.SetExternal(o);
    else if (rng() % 10) calls.Init(kind, o, rng);
    NoteObject(kind, o);
    const unsigned steps = 4 + rng() % 40;
    for (unsigned i = 0; i < steps; ++i) {
        sgfx::MarkStep(ctx, "step");
        Step(calls, kind, o, rng);
    }
    sgfx::MarkStep(ctx, "delete");
    if (kind == kTarget && !original) drawfx::Note("deleted", uint32_t(A::DeleteTarget(static_cast<A::Target *>(o), nullptr, 1) == o));
    else if (kind == kTarget) drawfx::Note("deleted", uint32_t(ctx.Original<void *(__thiscall *)(void *, unsigned)>(0x36fbf0)(o, 1) == o));
    else delete static_cast<cITSRenderSurface *>(o);
    NoteFakes();
    counter.Stop();
    drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
}

} // namespace

// Verifies: the constructors 0x36eb10, 0x36ebf0, 0x36ec70 (with the base's 0x36eb60), the destructors 0x36f7f0,
// 0x36f870, 0x36f930 and the deleting destructors 0x36fbd0, 0x36fbf0, 0x36fc10; Init 0x3730e0, 0x373100, 0x373250;
// Configure 0x373390, 0x3733f0, 0x373450; SetExternal 0x3734d0; Bind 0x375a90; Reset 0x3759a0; ReleaseSurfaces
// 0x375950; ResetTexture 0x375990; ReparseShaders 0x3713b0; the slots Apply 0x3751f0, Clear 0x370f50, ClearRect
// 0x370fe0, CopyFrom 0x371210, CopyDepth 0x371120, CopyRect 0x371010, CopyColor 0x3710a0, Create 0x372700 / 0x372790
// / 0x372870, Destroy 0x375740 / 0x3757b0 / 0x3757d0, IsValid 0x3736e0 / 0x373720; and the folded slots as these
// classes' vtables have them.
DIFF_TEST(SG_RenderSurfaceLife, 0x36eb60)
{
    Stands stands;
    uint32_t current = 0;
    sgfx::CheckSides(
        ctx, "render surface", 3000,
        [&](uint32_t seed, drawfx::Cells &cells) {
            current = seed;
            Build(seed, cells);
        },
        [&](bool original, std::mt19937 &rng) {
            RunLife(ctx, current, original, rng);
            ++ctx.cases;
        });
}
