// Differential test of nTSSG::cDisplayListDynamicRenderStrategy (lane 261108g, scenegraph TS1b;
// src/sg/cDisplayListDynamicRenderStrategy.h).
//
//   SG_DynamicStrategyLife  one strategy's life on each side: built by the constructor 0x36e400 on a block filled alike
//       (the member constructors 0x36e9c0 and 0x36ed40 inside it; the censor strategy's 0x3775c0 and the tree objects'
//       0x401910 are each side's own / the original's), or by the factory 0x372190 (its words the constructor leaves
//       are the heap pattern; the current-surface words, which neither writes, are then set alike), then random calls:
//       slot 8 (0x375b00: the surfaces made, sized and updated through 0x372a50 and 0x3737f0) as the device, the back
//       buffer's size, the formats, the device enumerator's answers and the settings change between calls; the
//       forwarding slots 3 (BeginPass), 6 (Invalidate), 7 (AddBox) to the censor strategy; Register / Unregister (slots 9,
//       10) on a small pool of items (the draw slots, +8 slot 4 0x374930, slot 5 0x373920 and primary slot 6 0x374dd0,
//       have their own test: tests/sg_display_list_dynamic_draw.cpp, row TS1c); QueryInterface through
//       either vtable; AddRef / Release / RefCount; UpdateSurfaces 0x372a50 and TurboRectSupported 0x3737f0 called
//       directly (ReleaseSurfaceResources 0x373d70 only through the destructor, its one caller: it leaves the targets
//       without their surfaces). After each call the whole object is noted by declared word
//       kinds, with the helper, its two maps, the tree objects, the censor strategy and every surface (as
//       tests/sg_render_surface.cpp notes one). At the end (slot 8 run first if the surfaces were never made: the
//       destructor releases them unchecked) the last Release (0x1f9aa0 to the deleting destructor 0x36fb80 and the
//       destructor 0x36f220, 0x36fa10 for the helper), then the fakes' counts and the heap.
//
// Stood in on both sides: the COM director getter 0x1ae18d (the two director objects the constructor asks for, and
// the surfaces' render target, depth-stencil and render-target texture fakes), the formats manager getter 0x18acd0
// (GetARGB / GetDepthStencil answer format fakes whose slot 13 is GetStencilBits), the current device 0x18ae40 (the
// device fake, or none), the device's multisample switch 0x18afc0 (half the time it runs slot 8 again, as the game's
// device reset does, while the back buffer's multisample type is changing), RZMemory's Malloc / Free 0x1be3b0 / 0x1be3a1 (the
// helper's maps) and the game's operator new / delete / free, all to ours so the original's blocks are counted like
// ours. Cells: the texture manager 0x10f8f94, the material manager 0x10f8fcc (FindOrLoad answers -1: the censor's
// BeginPass stops after asking), the framework's service object 0x10c9378 (the settings service, whose
// GetPropertyValue answers each key from the seed's table or leaves the default, and the scene-graph system, whose slot
// 36 object's slot 9 says whether the water reflects) and the scene-graph system's cache 0x10cc458 (empty, or the fake).
#include "sgfx.h"

#include "code_patch.h"
#include "heap_poison.h"
#include "ts_services_fixture.h"

#include "gz/GraphicServices.h"
#include "gz/cIGZPropertySet.h"
#include "rz/RZMemory.h"
#include "rz/StaticGetters.h"
#include "sg/cDisplayList.h"
#include "sg/cDisplayListDynamicRenderStrategy.h"
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
using A = cDisplayListDynamicRenderStrategyAdapters;

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

// cIDeviceDX: 16 GetDeviceEnumConst, 27 Clear, 28 ClearTargets, 35 IsDeviceReady, 39 GetBackBuffer,
// 40 GetTargetProviderSurface.
constexpr At kDeviceAt[] = {{16, {0, Ret::Int, true, "GetDeviceEnumConst", ""}},
                            {27, {4, Ret::Int, true, "Clear", "uF4fu"}},
                            {28, {6, Ret::Int, true, "ClearTargets", "aruF4fu"}},
                            {35, {0, Ret::Int, true, "IsDeviceReady", ""}},
                            {39, {1, Ret::Int, true, "GetBackBuffer", "u"}},
                            {40, {0, Ret::Int, true, "GetTargetProviderSurface", ""}}};
constexpr auto kDeviceSlots = Table<41>(kDeviceAt);
constexpr drawfx::Iface kDevice{"Device", 41, kDeviceSlots.data()};

// cIDeviceEnum: 15 IsDepthFormatSupportedForDisplay, 19 CanStretchRect (the sizes and rectangles by value).
constexpr At kEnumAt[] = {{15, {3, Ret::Int, true, "IsDepthFormatSupportedForDisplay", "aua"}},
                          {19, {8, Ret::Int, true, "CanStretchRect", "upraupra"}}};
constexpr auto kEnumSlots = Table<20>(kEnumAt);
constexpr drawfx::Iface kEnum{"DeviceEnum", 20, kEnumSlots.data()};

// cISurface (and its render-target / depth-stencil leaves): 3 ReleaseResource, 6 IsCreated, 15 CopyRect, 16 Copy,
// 17 StretchRect, 18 Stretch, 20 GetWidth, 21 GetHeight, 22 GetPixelFormat, 25 Create.
constexpr At kSurfaceAt[] = {{3, {0, Ret::Int, true, "ReleaseResource", ""}},
                             {6, {0, Ret::Int, true, "IsCreated", ""}},
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

constexpr At kComAt[] = {{3, {3, Ret::Int, true, "GetClassObject", "uua"}}};
constexpr auto kComSlots = Table<4>(kComAt);
constexpr drawfx::Iface kCom{"COM", 4, kComSlots.data()};

// The first director object's slot 5 (called with 1 by the constructor).
constexpr At kDirectorAt[] = {{5, {1, Ret::Int, true, "Slot5", "u"}}};
constexpr auto kDirectorSlots = Table<6>(kDirectorAt);
constexpr drawfx::Iface kDirector{"Director", 6, kDirectorSlots.data()};

// The formats manager (6 GetARGB, 11 GetDepthStencil) and a format (13 GetStencilBits, as cIPixelFormatDepthStencil).
constexpr At kManagerAt[] = {{6, {5, Ret::Int, true, "GetARGB", "uuuuu"}},
                             {11, {4, Ret::Int, true, "GetDepthStencil", "uuuu"}}};
constexpr auto kManagerSlots = Table<12>(kManagerAt);
constexpr drawfx::Iface kManager{"FormatsManager", 12, kManagerSlots.data()};
constexpr At kFormatAt[] = {{13, {0, Ret::Int, true, "GetStencilBits", ""}}};
constexpr auto kFormatSlots = Table<14>(kFormatAt);
constexpr drawfx::Iface kFormat{"PixelFormat", 14, kFormatSlots.data()};

// cITextureManager: 18 AddRefId, 19 ReleaseId, 20 Add, 23 GetTexture.
constexpr At kTexturesAt[] = {{18, {1, Ret::Int, true, "AddRefId", "u"}},
                              {19, {1, Ret::Int, true, "ReleaseId", "u"}},
                              {20, {3, Ret::Int, true, "Add", "asu"}},
                              {23, {1, Ret::Int, true, "GetTexture", "u"}}};
constexpr auto kTexturesSlots = Table<24>(kTexturesAt);
constexpr drawfx::Iface kTextures{"TextureManager", 24, kTexturesSlots.data()};

// cIMaterialManager: 19 FindOrLoad (the censor's BeginPass), 20 AddRefIndex, 21 ReleaseIndex, 24 ReloadAll.
constexpr At kMaterialsAt[] = {{19, {1, Ret::Int, true, "FindOrLoad", "s"}},
                               {20, {1, Ret::Int, true, "AddRefIndex", "u"}},
                               {21, {1, Ret::Int, true, "ReleaseIndex", "u"}},
                               {24, {2, Ret::Int, true, "ReloadAll", "su"}}};
constexpr auto kMaterialsSlots = Table<25>(kMaterialsAt);
constexpr drawfx::Iface kMaterials{"MaterialManager", 25, kMaterialsSlots.data()};

// The framework (5 GetSystemService), the settings service (3 SetProperty, 9 GetPropertyValue: noted by the handlers;
// the pointers are stack addresses) and the scene-graph system (36: an object whose slot 9 is the water question).
constexpr At kFrameworkAt[] = {{5, {3, Ret::Int, true, "GetSystemService", "uua"}}};
constexpr auto kFrameworkSlots = Table<6>(kFrameworkAt);
constexpr drawfx::Iface kFramework{"Framework", 6, kFrameworkSlots.data()};
constexpr At kSettingsAt[] = {{3, {2, Ret::Int, true, "SetProperty", "aa"}},
                              {9, {2, Ret::Int, true, "GetPropertyValue", "aa"}}};
constexpr auto kSettingsSlots = Table<10>(kSettingsAt);
constexpr drawfx::Iface kSettings{"Settings", 10, kSettingsSlots.data()};
constexpr At kSgAt[] = {{36, {0, Ret::Int, true, "Slot36", ""}}};
constexpr auto kSgSlots = Table<37>(kSgAt);
constexpr drawfx::Iface kSg{"SgSystem", 37, kSgSlots.data()};
constexpr At kSgInnerAt[] = {{9, {0, Ret::Int, true, "Slot9", ""}}};
constexpr auto kSgInnerSlots = Table<10>(kSgInnerAt);
constexpr drawfx::Iface kSgInner{"SgSystemObject", 10, kSgInnerSlots.data()};

constexpr uint32_t kRenderTargetClass = 0x5b75b757, kDepthStencilClass = 0x7b75b73a;
constexpr uint32_t kDirector10Class = 0xdc012582, kDirector14Class = 0x9c1d3c77;
constexpr uint32_t kSettingsId = 0x0ac1887e, kSgSystemId = 0x9a353ce8;
constexpr uint32_t kTexturesCell = 0x10f8f94, kMaterialsCell = 0x10f8fcc, kFrameworkCell = 0x10c9378;
constexpr uint32_t kSgCell = 0x10cc458;
constexpr uint32_t kPassTag = 0x7d000000;
constexpr uint32_t kSize = cDisplayListDynamicRenderStrategy::kSize;
constexpr unsigned kSurfaces = 8, kFormats = 3, kMaxBoxes = 20;

// The settings the strategy reads, by kind, with the values a seed picks from.
enum SettingKind : uint8_t { kU32, kBool, kFloat };
struct Setting {
    const char *name;
    SettingKind kind;
};
constexpr Setting kSettingList[] = {{"antialiasingSupport", kU32},    {"antialiasingQuality", kU32},
                                    {"debugRenderRes", kFloat},       {"dynamicRenderStrategy", kU32},
                                    {"useTurboRect", kBool},          {"reflectionWithExtraViewer", kBool},
                                    {"nhoodWaterReflection", kBool},  {"enablePostProcessing", kBool},
                                    {"useRenderTextures", kBool}};
constexpr unsigned kSettingCount = sizeof(kSettingList) / sizeof(kSettingList[0]);

struct World {
    Fake device, enumerator, com, director10, director14, manager, textures, materials, framework, settings, sg, sgInner;
    Fake target, stencil, texture;
    Fake surfaces[kSurfaces], formats[kFormats];
    uint32_t settingValue[kSettingCount];
    bool settingSet[kSettingCount];
    bool noDevice = false;
    // The strategy slot 8 re-enters from the multisample switch (the game resets the device there), and its caller.
    void *reenter = nullptr;
    void (*reenterSlot8)(void *) = nullptr;
    nGZSceneGraph::Pass pass;
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

uint32_t Made(Fake &self, const uint32_t *)
{
    self.value[0] = gw.Next() % 4 != 0;
    return self.value[0];
}
uint32_t GetClassObject(Fake &, const uint32_t *a)
{
    Fake *made = a[0] == kRenderTargetClass ? &gw.target
                 : a[0] == kDepthStencilClass ? &gw.stencil
                 : a[0] == kDirector10Class   ? &gw.director10
                 : a[0] == kDirector14Class   ? (gw.director14.value[7] ? &gw.director14 : nullptr)
                                              : &gw.texture;
    if (made) ++made->refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = made;
    return made != nullptr;
}
uint32_t TextureSurface(Fake &self, const uint32_t *a) { return drawfx::P(&gw.surfaces[(self.value[1] + a[0]) % kSurfaces]); }
uint32_t Added(Fake &self, const uint32_t *) { return 0x100 + ++self.value[0]; }
uint32_t GetTexture(Fake &, const uint32_t *) { return drawfx::P(&gw.texture); }
uint32_t FindOrLoad(Fake &, const uint32_t *) { return 0xffffffffu; }
uint32_t GetService(Fake &, const uint32_t *a)
{
    Fake *const service = a[0] == kSgSystemId ? &gw.sg : &gw.settings;
    ++service->refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = service;
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
// The key's text and the default the caller put in the value (its kind's width), then the seed's value written there
// (or nothing: the default stays).
uint32_t GetPropertyValue(Fake &, const uint32_t *a)
{
    difftest::NotCounted bookkeeping;
    const char *const name = reinterpret_cast<const GZServiceString *>(uintptr_t(a[0]))->c_str();
    sgfx::NoteText("setting", name);
    auto *const value = reinterpret_cast<uint8_t *>(uintptr_t(a[1])) + 4;
    for (unsigned i = 0; i < kSettingCount; ++i) {
        if (std::strcmp(name, kSettingList[i].name) != 0) continue;
        const bool isBool = kSettingList[i].kind == kBool;
        uint32_t before = 0;
        std::memcpy(&before, value, isBool ? 1 : 4);
        drawfx::Note("setting default", before);
        if (!gw.settingSet[i]) return 0;
        std::memcpy(value, &gw.settingValue[i], isBool ? 1 : 4);
        return 1;
    }
    drawfx::Note("setting unknown", 0);
    return 0;
}
uint32_t BackBuffer(Fake &, const uint32_t *) { return drawfx::P(&gw.surfaces[0]); }
uint32_t ProviderSurface(Fake &, const uint32_t *) { return drawfx::P(&gw.surfaces[1]); }

void *StandCom() { return &gw.com; }
void *__cdecl StandManager() { return &gw.manager; }
nGZGraphic4::cIDeviceDX *__cdecl StandDevice()
{
    return gw.noDevice ? nullptr : reinterpret_cast<nGZGraphic4::cIDeviceDX *>(&gw.device);
}
bool __cdecl StandMultisample(uint32_t enable, uint32_t quality)
{
    drawfx::Note("multisample", {enable, quality});
    if (void *const o = gw.reenter; o && gw.Next() % 2 == 0) {
        // Slot 8 again while the back buffer's multisample type changes: it must return at once.
        gw.reenter = nullptr;
        drawfx::Note("reenter", 0);
        gw.reenterSlot8(o);
        gw.reenter = o;
    }
    return gw.Next() % 3 != 0;
}
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
void __cdecl GameFree(void *p) { ::operator delete(p); }
void *__cdecl StandMalloc(uint32_t n) { return ::operator new(n); }
void __cdecl StandFree(void *p) { ::operator delete(p); }

struct Stands {
    shape::StandIn com{0x1ae18d, &GetDirectorFrameworkObject, &StandCom};
    shape::StandIn manager{0x18acd0, &nGZGraphic4::GetPixelFormatsManagerCached, &StandManager};
    shape::StandIn device{0x18ae40, &nGZGraphic4::GetCurrentDeviceCached, &StandDevice};
    shape::StandIn rzMalloc{0x1be3b0, &RZMemory::Malloc, &StandMalloc};
    shape::StandIn rzFree{0x1be3a1, &RZMemory::Free, &StandFree};
    shape::CodePatch multisample{0x18afc0u, shape::CodeOf(&StandMultisample)};
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
    shape::CodePatch gameFree{0x82540fu, shape::CodeOf(&GameFree)};
};

// A setting's value as a seed picks it: support 0-5, quality 0-4, a scale (1 most often), a strategy (2 most often),
// a flag.
uint32_t PickSetting(unsigned i, uint32_t r)
{
    static const float kScales[] = {1.0f, 1.0f, 0.5f, 0.75f, 2.0f, 0.1f, 1.0000001f, 0.0f};
    static const uint32_t kStrategies[] = {2, 2, 2, 0, 1, 3};
    switch (i) {
    case 0: return r % 6;
    case 1: return (r % 3 == 0) ? 0 : r % 5;
    case 2: return std::bit_cast<uint32_t>(kScales[r % std::size(kScales)]);
    case 3: return kStrategies[r % std::size(kStrategies)];
    default: return r % 3 != 0;
    }
}
void PickSettings(std::mt19937 &rng)
{
    for (unsigned i = 0; i < kSettingCount; ++i) {
        gw.settingSet[i] = rng() % 6 != 0;
        gw.settingValue[i] = PickSetting(i, rng());
    }
}

void PickSurface(Fake &s, uint32_t r)
{
    static constexpr uint32_t kSides[] = {0, 1, 640, 1024, 0x80000001u, 800, 600};
    s.value[0] = r % 3 != 0;
    s.value[1] = kSides[(r / 3) % 7];
    s.value[2] = kSides[(r / 21) % 7];
    s.value[3] = drawfx::P(&gw.formats[(r / 147) % kFormats]);
}

void Build(uint32_t seed, drawfx::Cells &cells)
{
    gw.lcg = seed * 2654435761u + 5;
    std::mt19937 rng(seed ^ 0x5eed);
    gw.noDevice = seed % 11 == 0;
    gw.device.Reset<kDevice>(800);
    gw.device.value[0] = seed % 6 != 0;
    gw.device.Returns(16, &gw.enumerator);
    gw.device.On(35, &Word0);
    gw.device.On(39, &BackBuffer);
    gw.device.On(40, &ProviderSurface);
    gw.enumerator.Reset<kEnum>(801);
    gw.enumerator.On(15, &Word0);
    gw.enumerator.On(19, &Word1);
    gw.enumerator.value[0] = rng() % 4 != 0;
    gw.enumerator.value[1] = rng() % 4 != 0;
    gw.com.Reset<kCom>(802);
    gw.com.On(3, &GetClassObject);
    gw.director10.Reset<kDirector>(803);
    gw.director14.Reset<kDirector>(804);
    gw.director14.value[7] = seed % 7 != 0;
    gw.manager.Reset<kManager>(805);
    gw.manager.Returns(6, &gw.formats[seed % kFormats]);
    gw.manager.Returns(11, &gw.formats[(seed / 3) % kFormats]);
    gw.textures.Reset<kTextures>(806);
    gw.textures.On(20, &Added);
    gw.textures.On(23, &GetTexture);
    gw.materials.Reset<kMaterials>(807);
    gw.materials.On(19, &FindOrLoad);
    gw.framework.Reset<kFramework>(808);
    gw.framework.On(5, &GetService);
    gw.settings.Reset<kSettings>(809);
    gw.settings.On(3, &SetProperty);
    gw.settings.On(9, &GetPropertyValue);
    gw.sg.Reset<kSg>(810);
    gw.sg.Returns(36, &gw.sgInner);
    gw.sgInner.Reset<kSgInner>(811);
    gw.sgInner.On(9, &Word0);
    gw.sgInner.value[0] = rng() % 2;
    for (Fake *f : {&gw.target, &gw.stencil}) {
        f->Reset<kSurface>(f == &gw.target ? 812 : 813);
        f->On(6, &Word0);
        f->On(25, &Made);
        f->On(20, &Word1);
        f->On(21, &Word2);
        f->On(22, &Word3);
        f->value[1] = 64 + seed % 3;
        f->value[2] = 32;
        f->value[3] = drawfx::P(&gw.formats[seed % kFormats]);
    }
    gw.texture.Reset<kTexture>(814);
    gw.texture.On(6, &Word0);
    gw.texture.On(9, &Made);
    gw.texture.On(17, &TextureSurface);
    gw.texture.On(18, &Word4);
    gw.texture.AddFace(0x0b96cc22u, gw.texture);
    gw.texture.value[0] = seed % 3 != 0;
    gw.texture.value[1] = 2;
    gw.texture.value[4] = seed % 5 ? drawfx::P(&gw.surfaces[3]) : 0;
    for (unsigned i = 0; i < kSurfaces; ++i) {
        Fake &s = gw.surfaces[i];
        s.Reset<kSurface>(820 + i);
        s.On(6, &Word0);
        s.On(20, &Word1);
        s.On(21, &Word2);
        s.On(22, &Word3);
        PickSurface(s, rng());
    }
    for (unsigned i = 0; i < kFormats; ++i) {
        gw.formats[i].Reset<kFormat>(830 + i);
        gw.formats[i].On(13, &Word0);
        gw.formats[i].value[0] = rng() % 3 == 0 ? 0 : 8;
    }
    PickSettings(rng);
    std::memset(&gw.pass, 0, sizeof gw.pass);
    drawfx::AddRange(&gw.pass, sizeof gw.pass, kPassTag);
    cells.Set(kTexturesCell, &gw.textures);
    cells.Set(kMaterialsCell, &gw.materials);
    cells.Set(kFrameworkCell, &gw.framework);
    cells.Set(kSgCell, seed % 5 == 0 ? &gw.sg : nullptr);
}

void NoteFakes()
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = "fake refs";
    for (Fake *f : {&gw.device, &gw.enumerator, &gw.com, &gw.director10, &gw.director14, &gw.manager, &gw.textures,
                    &gw.materials, &gw.framework, &gw.settings, &gw.sg, &gw.sgInner, &gw.target, &gw.stencil,
                    &gw.texture})
        c.words.push_back(uint32_t(f->refs));
    for (const Fake &f : gw.surfaces) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.formats) c.words.push_back(uint32_t(f.refs));
    drawfx::gLog.push_back(std::move(c));
}

// ---- the object ----

uint32_t W(const void *o, uint32_t at) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(o) + at); }
uint8_t B(const void *o, uint32_t at) { return static_cast<const uint8_t *>(o)[at]; }
const uint8_t *At8(const void *o, uint32_t at) { return static_cast<const uint8_t *>(o) + at; }

// ---- the surfaces (as tests/sg_render_surface.cpp notes them) ----

enum Kind : unsigned { kBackbuffer, kTarget, kTextureSurface };

void NoteState(uint32_t address)
{
    const uint32_t *w = reinterpret_cast<const uint32_t *>(uintptr_t(address));
    drawfx::Note("command", {drawfx::Ptr(w[0]), w[1] & 0xff, w[1] >> 16});
    drawfx::Note("command targets", {drawfx::Ptr(w[2]), drawfx::Ptr(w[3]), drawfx::Ptr(w[4]), drawfx::Ptr(w[5]),
                                     drawfx::Ptr(w[6])});
}
void NoteString(const char *label, const void *o, uint32_t at)
{
    const auto *text = reinterpret_cast<const std::string *>(At8(o, at + 4));
    drawfx::Note(label, {drawfx::Ptr(W(o, at)), W(o, at + 0x14), W(o, at + 0x18), W(o, at + 0x1c)});
    sgfx::NoteText(label, text->c_str());
}
void NoteSurface(Kind kind, const void *o)
{
    if (!o) return;
    drawfx::Note("requested", {W(o, 4), W(o, 8)});
    drawfx::Note("device, bound", {drawfx::Ptr(W(o, 0xc)), drawfx::Ptr(W(o, 0x10)), drawfx::Ptr(W(o, 0x14))});
    drawfx::Note("created, size, scale, depth", {W(o, 0x18), W(o, 0x1c), W(o, 0x20), W(o, 0x24), W(o, 0x28)});
    NoteState(drawfx::P(At8(o, 0x2c)));
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

// The strategy's words by kind ('-' the two class vtables), then what its pointers own.
std::array<char, kSize / 4 + 1> ObjectKinds()
{
    std::array<char, kSize / 4 + 1> k{};
    for (char &c : k) c = 'u';
    k[kSize / 4] = 0;
    k[0] = k[2] = '-';
    for (unsigned at : {0xcu, 0x10u, 0x14u, 0x18u, 0x48u, 0x50u, 0x7cu, 0x98u, 0x9cu, 0xa0u, 0xa8u, 0x14cu, 0x174u})
        k[at / 4] = 'a';
    for (unsigned at = 0x30; at < 0x48; at += 4) k[at / 4] = 'a';   // the two word vectors
    for (unsigned at = 0xb0; at < 0xbc; at += 4) k[at / 4] = 'a';   // the trees' vector
    for (unsigned at = 0x104; at < 0x144; at += 4) k[at / 4] = 'a'; // the surfaces, the registered vector
    return k;
}
const auto kObjectKinds = ObjectKinds();

void NoteMap(const char *label, const void *map)
{
    if (!map) return;
    drawfx::Call c;
    c.note = label;
    drawfx::Words(c.words, map, 8, "uauaaauu");
    drawfx::gLog.push_back(std::move(c));
}

void NoteObject(const void *o)
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = "object";
    drawfx::Words(c.words, o, kSize / 4, kObjectKinds.data());
    drawfx::gLog.push_back(std::move(c));
    {
        // The registered items (data words the test made).
        const auto *v = reinterpret_cast<const std::vector<uint32_t> *>(At8(o, 0x138));
        drawfx::Call r;
        r.note = "registered";
        r.words.push_back(uint32_t(v->size()));
        r.words.push_back(uint32_t(v->capacity()));
        for (uint32_t item : *v) r.words.push_back(item);
        drawfx::gLog.push_back(std::move(r));
    }
    if (const void *const helper = reinterpret_cast<const void *>(uintptr_t(W(o, 0x18)))) {
        drawfx::Call h;
        h.note = "helper";
        drawfx::Words(h.words, helper, 7, "aaauaau");
        h.words.push_back(W(helper, 0x24) & 0xffff);
        drawfx::gLog.push_back(std::move(h));
        NoteMap("map 10", reinterpret_cast<const void *>(uintptr_t(W(helper, 0x10))));
        NoteMap("map 14", reinterpret_cast<const void *>(uintptr_t(W(helper, 0x14))));
    }
    for (uint32_t at : {0x98u, 0x9cu}) {
        if (const void *const tree = reinterpret_cast<const void *>(uintptr_t(W(o, at)))) {
            drawfx::Call t;
            t.note = "tree";
            drawfx::Words(t.words, tree, 4, "-uau");
            if (const void *const block = reinterpret_cast<const void *>(uintptr_t(W(tree, 8)))) t.words.push_back(W(block, 0));
            drawfx::gLog.push_back(std::move(t));
        }
    }
    if (const void *const censor = reinterpret_cast<const void *>(uintptr_t(W(o, 0xc)))) {
        drawfx::Call s;
        s.note = "censor";
        drawfx::Words(s.words, censor, 14, "-u-aaaaaauau-u");
        drawfx::gLog.push_back(std::move(s));
    }
    static constexpr std::pair<uint32_t, Kind> kOwned[] = {
        {0x134, kBackbuffer},     {0x104, kTarget},         {0x108, kTarget},         {0x10c, kTextureSurface},
        {0x110, kTextureSurface}, {0x11c, kTextureSurface}, {0x120, kTextureSurface}, {0x124, kTextureSurface},
        {0x128, kTextureSurface}, {0x12c, kTextureSurface}, {0x130, kTextureSurface}};
    for (const auto &[at, kind] : kOwned) NoteSurface(kind, reinterpret_cast<const void *>(uintptr_t(W(o, at))));
}

// A call through slot kSlot of the vtable at `self` (each side's own vtable).
template <unsigned kSlot, class R, class... Args> R Virtual(void *self, Args... args)
{
    using F = R(__thiscall *)(void *, Args...);
    return (*reinterpret_cast<F *const *>(self))[kSlot](self, args...);
}
uint8_t *Face(void *o) { return static_cast<uint8_t *>(o) + 8; }
template <unsigned kSlot, class R = uint32_t, class... Args> R Slot8(void *o, Args... args)
{
    return Virtual<kSlot, R>(Face(o), args...);
}
template <unsigned kSlot, class R = uint32_t, class... Args> R Slot0(void *o, Args... args)
{
    return Virtual<kSlot, R>(o, args...);
}

void QueryInterface(void *o, std::mt19937 &rng)
{
    static const uint32_t kIids[] = {1u, 0x0bf077fdu, 0x2ca4c07du, 0x49d02f0bu, 0u};
    const uint32_t iid = kIids[rng() % std::size(kIids)];
    void *out = reinterpret_cast<void *>(uintptr_t(0x5a5a5a5a));
    void **const where = rng() % 6 == 0 ? nullptr : &out;
    const bool viaFace = rng() % 2 != 0;
    const bool found = (viaFace ? Slot8<0, uint8_t>(o, iid, where) : Slot0<0, uint8_t>(o, iid, where)) != 0;
    drawfx::Note("query", {iid, uint32_t(viaFace), uint32_t(found), found && where ? drawfx::Ptr(out) : drawfx::P(out), W(o, 4)});
    if (found) Slot0<2>(o);
}

nGZCompGeom::BoundBox MakeBox(std::mt19937 &rng)
{
    nGZCompGeom::BoundBox b;
    b.min.x = float(rng() % 600);
    b.min.y = float(rng() % 400);
    b.min.z = 0.25f * float(rng() % 4);
    b.max.x = b.min.x + float(rng() % 100);
    b.max.y = b.min.y + float(rng() % 100);
    b.max.z = 1.0f;
    return b;
}

uint32_t CensorBoxes(const void *o)
{
    const void *const censor = reinterpret_cast<const void *>(uintptr_t(W(o, 0xc)));
    return censor ? (W(censor, 0x10) - W(censor, 0xc)) / 0x30 : 0;
}

struct Run {
    difftest::Context &ctx;
    bool original;
    void *o;

    cDisplayListDynamicRenderStrategy *Ours() const { return static_cast<cDisplayListDynamicRenderStrategy *>(o); }
    nGZGraphic4::cIDeviceDX *Device() const { return reinterpret_cast<nGZGraphic4::cIDeviceDX *>(&gw.device); }
    bool Built() const { return W(o, 0x10c) != 0; }

    void ChangeWorld(std::mt19937 &rng)
    {
        const unsigned what = rng() % 6;
        drawfx::Note("change", what);
        if (what == 0) gw.noDevice = rng() % 5 == 0, gw.device.value[0] = rng() % 4 != 0;
        else if (what == 1) PickSettings(rng);
        else if (what == 2) PickSurface(gw.surfaces[rng() % 2], rng());
        else if (what == 3) gw.enumerator.value[0] = rng() % 3 != 0, gw.enumerator.value[1] = rng() % 3 != 0;
        else if (what == 4) gw.formats[rng() % kFormats].value[0] = rng() % 2 ? 8 : 0, gw.sgInner.value[0] = rng() % 2;
        else {
            for (Fake *f : {&gw.target, &gw.stencil, &gw.texture}) f->value[0] = rng() % 2;
            const unsigned i = rng() % kSettingCount;
            gw.settingSet[i] = true;
            gw.settingValue[i] = PickSetting(i, rng());
        }
    }

    void Step(std::mt19937 &rng)
    {
        const unsigned what = rng() % 24;
        drawfx::Note("op", what);
        if (what < 6) {
            if (rng() % 2) ChangeWorld(rng);
            Slot8<8, void>(o);
        } else if (what < 8) {
            Slot8<9, void>(o, reinterpret_cast<void *>(uintptr_t(0x1000 + rng() % 6)));
        } else if (what < 10) {
            Slot8<10, void>(o, reinterpret_cast<void *>(uintptr_t(0x1000 + rng() % 6)));
        } else if (what == 10) {
            if (CensorBoxes(o) < kMaxBoxes) {
                const nGZCompGeom::BoundBox box = MakeBox(rng);
                Slot8<7, void>(o, &box);
            }
        } else if (what == 11) {
            Slot8<3, void>(o, &gw.pass);
        } else if (what == 12) {
            Slot8<6, void>(o);
        } else if (what < 16) {
            QueryInterface(o, rng);
        } else if (what == 16) {
            drawfx::Note("addref, release, count", {Slot0<1>(o), Slot0<2>(o), Slot0<5>(o)});
        } else if (what == 17) {
            if (!Built()) return;
            if (rng() % 2) ChangeWorld(rng);
            if (original) ctx.Original<void(__thiscall *)(void *, void *)>(0x372a50)(o, Device());
            else Ours()->UpdateSurfaces(Device());
        } else if (what == 18) {
            const uint8_t r = original ? ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x3737f0)(o, Device())
                                       : uint8_t(Ours()->TurboRectSupported(Device()));
            drawfx::Note("turbo rect", r);
        } else {
            ChangeWorld(rng);
        }
    }
};

void RunLife(difftest::Context &ctx, uint32_t seed, bool original, std::mt19937 &rng)
{
    difftest::AllocationCounter counter;
    void *o;
    if (seed % 4 == 0) {
        void *const face = original ? ctx.Original<void *(__cdecl *)()>(0x372190)() : static_cast<void *>(A::Create());
        o = static_cast<uint8_t *>(face) - 8;
    } else {
        o = ::operator new(kSize);
        for (uint32_t i = 0; i < kSize; ++i) static_cast<uint8_t *>(o)[i] = uint8_t(rng());
        if (original) ctx.Original<void *(__thiscall *)(void *)>(0x36e400)(o);
        else A::Construct(static_cast<cDisplayListDynamicRenderStrategy *>(o), nullptr);
    }
    drawfx::Note("made", drawfx::Ptr(o));
    // The current-surface words neither constructor writes (UpdateSurfaces sets them), set alike.
    std::memset(static_cast<uint8_t *>(o) + 0x114, 0, 8);
    Run run{ctx, original, o};
    gw.reenter = o;
    gw.reenterSlot8 = [](void *self) { Slot8<8, void>(self); };
    NoteObject(o);
    Slot0<1>(o);
    const unsigned steps = 4 + rng() % 40;
    for (unsigned i = 0; i < steps; ++i) {
        sgfx::MarkStep(ctx, "step");
        run.Step(rng);
        NoteObject(o);
    }
    sgfx::MarkStep(ctx, "delete");
    if (!run.Built()) {
        // The destructor releases the surfaces unchecked: the game runs slot 8 first.
        gw.noDevice = false;
        gw.device.value[0] = 1;
        Slot8<8, void>(o);
    }
    gw.reenter = nullptr;
    drawfx::Note("released", Slot0<2>(o));
    NoteFakes();
    counter.Stop();
    drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
}

} // namespace

// Verifies: the constructor 0x36e400 (with the member constructors 0x36e9c0 and 0x36ed40), the factory 0x372190, the
// destructor 0x36f220 and deleting destructor 0x36fb80 (with the helper's destructor 0x36fa10), slot 8 0x375b00,
// UpdateSurfaces 0x372a50, TurboRectSupported 0x3737f0, ReleaseSurfaceResources 0x373d70, the face's slots 3 0x3703e0,
// 6 0x373690, 7 0x3703d0, 9 0x370380, 10 0x373de0 (the draw slots: tests/sg_display_list_dynamic_draw.cpp).
DIFF_TEST(SG_DynamicStrategyLife, 0x375b00)
{
    svcfx::AllocatorScope allocator;
    heappoison::Scope poison(ctx, 0xc5);
    Stands stands;
    uint32_t current = 0;
    sgfx::CheckSides(
        ctx, "dynamic strategy", 2000,
        [&](uint32_t seed, drawfx::Cells &cells) {
            current = seed;
            Build(seed, cells);
        },
        [&](bool original, std::mt19937 &rng) {
            RunLife(ctx, current, original, rng);
            ++ctx.cases;
        });
}
