// Differential tests of nTSSG::cDisplayListDynamicRenderStrategy's draw path (lane 261108k, scenegraph TS1c;
// src/sg/cDisplayListDynamicRenderStrategy.h).
//
//   SG_DynamicStrategyDraw  a strategy block filled alike (random bytes; the containers, the helper, the surface
//       pointers and the scissor state's header set by the test) and three frame states (each with its table; their
//       cIGZUnknown answers through an embedded fake), then random calls on each side: Render (+8 slot 4 0x374930, on
//       the face; Prepare 0x371b10, Draw 0x373e20 and Finish 0x376460 inside it, MatricesDiffer 0x36d3c0 on a projection
//       that is the frame state's own, a few representable floats off it, or another), Clear (+8 slot 5 0x373920, with
//       MarkOpaqueUI 0x375240), and each step on its own: SetUpTarget (primary slot 6 0x374dd0), Prepare, Draw, Finish,
//       DisableScissor 0x3714b0, MarkOpaqueUI, the helper's FrameStep / Reset (0x3755e0 / 0x370fc0) and the table's
//       (0x375590 / 0x370f90). The settings, the surfaces' answers, the pass (its type, handle texture, rectangles,
//       matrices) and the viewer's clear info change between calls; any fake may throw (the count of calls to the next
//       throw is set now and then), which Render's handler sees on its way out. After each call the object is noted by
//       declared word kinds, with its lists, vectors, helper (and the helper's maps), the frame states and their tables.
//       At the end the fakes' counts and the heap after the test frees what it made.
//   SG_DynamicMatricesDiffer  MatricesDiffer 0x36d3c0 on pairs of matrices whose elements are equal, a few
//       representable floats apart across the threshold (31, 32, 33 either way), of either sign around zero, denormal,
//       NaN or infinite.
//
// Row TS1d (lane 261108m) runs inside these: the outlines 0x371670 (Finish; their drawer 0x3717e0 and its parts, on the
// Draw2D fake with the outline brush at +0x14) and the debug buffer overlay 0x376840 (Finish; the surfaces' sizes,
// colour and depth parts, the state commands it applies, the pass's textures). Their own tests, below:
//   SG_DynamicOutlines  DrawOutlines 0x371670 called alone: every dirtyRectsDebug and frame-state mode, the flag cells
//       (enabled or not, any kinds), records and helper maps of any frames, the turbo-rect flag either way.
//   SG_DynamicShowBuffer  DrawDebugOverlay 0x376840 called alone: the surfaces' sizes and multisampling, the reduced
//       resolution, debugStencil, the parts null or not.
//   SG_DynamicOpaqueUICallback  the window callback 0x3752d0 on trees of fake windows (their flags, the property
//       0x300d422's variant present, absent or failed with a variant left, the walk's answers), and the window property
//       get / set 0x372670 / 0x375660 on their own (the set's new cRZVariant handed to the window).
//   SG_DynamicStateLife  the frame state's factory 0x3721f0 (constructor 0x36e790, the table's 0x36e8b0 and its
//       records' 0x36e960) and destructor 0x36f5c0 (the table's 0x36f640) on filled states.
//
// Row TS1i (lane 261108s) runs inside these too: the drawing paths 0x3743e0 / 0x374670 (Draw), with DrawSide 0x374bb0,
// ListItems 0x373a70 (the table's Record 0x372150), PresentRegistered 0x371150, CopyToSecond 0x371240 and Present
// 0x373f00, on both sides for real. Their own test, below:
//   SG_DynamicDrawPaths  each of them called alone on the same world: the flag bytes 0, 1 or another byte, the frame
//       counts about the paths' thresholds (0, 1, 2 and above, unsigned), the targets one surface or two, the surfaces'
//       scales and aspects exactly 1 or a float off it, mFinal the back buffer or not, the registered items' flags and
//       the copies' answers (any upper bytes), the restart cells set or not, the bounds' edges tied with the pixels'.
//
// Stood in on both sides: the paths' callees from rows TS1e-TS1g (each notes its arguments; the byte ones by the bytes
// the callee reads, the bools by whether that byte is set): UpdateDirtyRects 0x3706a0 (adds rectangles to the frame's
// lists), MarkRecords 0x3724a0, ComputeBounds 0x370eb0 (sets the side's bounds from a few edges), SetUpScissor
// 0x3719a0 (answers with any upper bytes), DrawCensor 0x374ad0, DrawItemRun 0x374350, DrawReflectionItems 0x374270,
// DrawItemsBy 0x374100, ClearRects 0x373b70 and the censor's CollectDirtyRects 0x377f50 (rectangles and an answer); the
// current device getter 0x18ae40 (a fake whose enumeration's properties answer presentWorkaround), the Draw2D getter
// 0x18c000 (a fake), the texture surface's SetExternal 0x3734d0 (ours too), the COM director getter
// 0x1ae18d (the frame state's class), RZMemory's Malloc / Free (the helper's maps) and the game's operator new / delete
// / free (to ours, so the original's blocks are counted like ours). Cells: the framework's service object 0x10c9378 (the
// settings service and the window manager, whose main window's slot 37 notes the callback and the flag it is handed),
// the outlines' alpha bytes and flag cells, the paths' restart bytes 0x10d9d18 / 0x10d9d19 (set from the seed).
#include "sgfx.h"

#include "code_patch.h"
#include "fx_slots.h"
#include "heap_poison.h"
#include "ts_services_fixture.h"

#include "gz/GraphicServices.h"
#include "gz/cGZWinMgrW32.h"
#include "gz/cIGZPropertySet.h"
#include "rz/RZMemory.h"
#include "rz/StaticGetters.h"
#include "rz/cIGZVariant.h"
#include "sg/cDisplayList.h"
#include "sg/cDisplayListCensorRenderStrategy.h"
#include "sg/cDisplayListDynamicRenderStrategy.h"
#include "sg/cTSRenderSurface.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <list>
#include <memory>
#include <new>
#include <random>
#include <vector>

namespace {

using namespace nTSSG;
using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;
using Self = cDisplayListDynamicRenderStrategy;
using Rects = std::list<DynamicWords16>;
using Words = std::vector<uint32_t>;
using Pointers = std::vector<void *>;

struct At {
    unsigned slot;
    Slot s;
};
// A COM interface's table (QueryInterface, AddRef, Release first).
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
// A table without cIGZUnknown's slots (cITSRenderSurface).
template <size_t N, size_t K> constexpr std::array<Slot, N> Plain(const At (&at)[K])
{
    std::array<Slot, N> t{};
    for (Slot &s : t) s = {0, Ret::Int, false, "", ""};
    for (const At &a : at) t[a.slot] = a.s;
    return t;
}
constexpr const char *kDeclared = drawfx::kTestTable;

// The censor strategy's +8 face (cITSDisplayListRenderStrategy): 5 Clear.
constexpr At kCensorAt[] = {{5, {0, Ret::Int, true, "Clear", ""}}};
constexpr auto kCensorSlots = Table<6>(kCensorAt);
constexpr drawfx::Iface kCensor{"CensorFace", 6, kCensorSlots.data(), false, kDeclared};
// cIColorBrush: 5 Resize (the count), 6 SetColor (index, the colour word).
constexpr At kBrushAt[] = {{5, {1, Ret::Int, true, "Resize", "u"}}, {6, {2, Ret::Int, true, "SetColor", "uW1"}}};
constexpr auto kBrushSlots = Table<7>(kBrushAt);
constexpr drawfx::Iface kBrush{"ColorBrush", 7, kBrushSlots.data(), false, kDeclared};
// cIViewerRefNodeBase: 99 SetRefA8, 104 GetClearInfo, 116 the +0xa8 reference.
constexpr At kViewerAt[] = {{99, {1, Ret::Int, true, "Slot99", "a"}},
                            {104, {1, Ret::Int, true, "GetClearInfo", "a"}},
                            {116, {0, Ret::Int, true, "Slot116", ""}}};
constexpr auto kViewerSlots = Table<117>(kViewerAt);
constexpr drawfx::Iface kViewer{"Viewer", 117, kViewerSlots.data(), false, kDeclared};
constexpr At kComAt[] = {{3, {3, Ret::Int, true, "GetClassObject", "uua"}}};
constexpr auto kComSlots = Table<4>(kComAt);
constexpr drawfx::Iface kCom{"COM", 4, kComSlots.data(), false, kDeclared};
constexpr At kFrameworkAt[] = {{5, {3, Ret::Int, true, "GetSystemService", "uua"}}};
constexpr auto kFrameworkSlots = Table<6>(kFrameworkAt);
constexpr drawfx::Iface kFramework{"Framework", 6, kFrameworkSlots.data(), false, kDeclared};
constexpr At kSettingsAt[] = {{9, {2, Ret::Int, true, "GetPropertyValue", "aa"}}};
constexpr auto kSettingsSlots = Table<10>(kSettingsAt);
constexpr drawfx::Iface kSettings{"Settings", 10, kSettingsSlots.data(), false, kDeclared};
constexpr At kWinMgrAt[] = {{3, {0, Ret::Int, true, "GetMainWindow", ""}}};
constexpr auto kWinMgrSlots = Table<4>(kWinMgrAt);
constexpr drawfx::Iface kWinMgr{"WinMgr", 4, kWinMgrSlots.data(), false, kDeclared};
// cIOutlineColorBrush: 5 SetOutlineColor, 6 SetFillColor (the colour word each).
constexpr At kOutlineAt[] = {{5, {1, Ret::Int, true, "SetOutlineColor", "W1"}}, {6, {1, Ret::Int, true, "SetFillColor", "W1"}}};
constexpr auto kOutlineSlots = Table<7>(kOutlineAt);
constexpr drawfx::Iface kOutline{"OutlineBrush", 7, kOutlineSlots.data(), false, kDeclared};
// cGZWin slot 37 (id filter, callback, the flag's address: the callback and the flag noted by the handler).
constexpr At kWindowAt[] = {{37, {3, Ret::Int, true, "Slot37", "uW0W0"}}};
constexpr auto kWindowSlots = Table<38>(kWindowAt);
constexpr drawfx::Iface kWindow{"Window", 38, kWindowSlots.data(), false, kDeclared};
// cIDeviceDX: 27 Clear.
constexpr At kDeviceAt[] = {{27, {4, Ret::Int, true, "Clear", "uF4fu"}}};
constexpr auto kDeviceSlots = Table<28>(kDeviceAt);
constexpr drawfx::Iface kDevice{"Device", 28, kDeviceSlots.data(), false, kDeclared};
// cIDeviceState: 12 ApplyStateCommand (the command noted by the handler), 25 SetProjection.
constexpr At kStateAt[] = {{12, {1, Ret::Int, true, "ApplyStateCommand", "W0"}},
                           {25, {1, Ret::Int, true, "SetProjection", "F16"}}};
constexpr auto kStateSlots = Table<26>(kStateAt);
constexpr drawfx::Iface kDeviceState{"DeviceState", 26, kStateSlots.data(), false, kDeclared};
// cITSRenderSurface: 0 GetWidth, 1 GetHeight, 3 GetScale, 4 GetAspect (ST0), 6 IsMultisampled, 8 IsValid,
// 9 IsCreated, 12 Apply, 13 Clear (pass, flags, depth, stencil), 15 CopyFrom, 16 CopyDepth, 17 CopyRect (the other, the
// rectangle), 18 CopyColor, 19 GetColorSurface, 20 GetDepthSurface.
constexpr At kSurfaceAt[] = {{0, {0, Ret::Int, true, "GetWidth", ""}},
                             {1, {0, Ret::Int, true, "GetHeight", ""}},
                             {3, {0, Ret::Float, true, "GetScale", ""}},
                             {4, {0, Ret::Float, true, "GetAspect", ""}},
                             {6, {0, Ret::Int, true, "IsMultisampled", ""}},
                             {8, {0, Ret::Int, true, "IsValid", ""}},
                             {9, {0, Ret::Int, true, "IsCreated", ""}},
                             {12, {2, Ret::Int, true, "Apply", "ab"}},
                             {13, {4, Ret::Int, true, "Clear", "aufu"}},
                             {15, {1, Ret::Int, true, "CopyFrom", "a"}},
                             {16, {1, Ret::Int, true, "CopyDepth", "a"}},
                             {17, {2, Ret::Int, true, "CopyRect", "aW4"}},
                             {18, {2, Ret::Int, true, "CopyColor", "ab"}},
                             {19, {0, Ret::Int, true, "GetColorSurface", ""}},
                             {20, {0, Ret::Int, true, "GetDepthSurface", ""}}};
constexpr auto kSurfaceSlots = Plain<21>(kSurfaceAt);
constexpr drawfx::Iface kSurface{"RenderSurface", 21, kSurfaceSlots.data(), false, kDeclared};
// The frame state's cIGZUnknown (embedded), and the pass's handle texture (only handed on).
constexpr At kNoneAt[] = {{0, {2, Ret::Int, true, "QueryInterface", "ua"}}};
constexpr auto kUnknownSlots = Table<3>(kNoneAt);
constexpr drawfx::Iface kUnknown{"Unknown", 3, kUnknownSlots.data(), false, kDeclared};
// Present's device path: the current device's 16 GetDeviceEnumConst, the enumeration's 22 GetProperties (the property
// set answers through kSettings' slot 9).
constexpr At kCurrentDeviceAt[] = {{16, {0, Ret::Int, true, "GetDeviceEnumConst", ""}}};
constexpr auto kCurrentDeviceSlots = Table<17>(kCurrentDeviceAt);
constexpr drawfx::Iface kCurrentDevice{"CurrentDevice", 17, kCurrentDeviceSlots.data(), false, kDeclared};
constexpr At kDeviceEnumAt[] = {{22, {0, Ret::Int, true, "GetProperties", ""}}};
constexpr auto kDeviceEnumSlots = Table<23>(kDeviceEnumAt);
constexpr drawfx::Iface kDeviceEnum{"DeviceEnum", 23, kDeviceEnumSlots.data(), false, kDeclared};
// A registered item's cISurface: 6 IsCreated, 17 StretchRect (the target rectangle, the source surface, its rectangle).
constexpr At kRegSurfaceAt[] = {{6, {0, Ret::Int, true, "IsCreated", ""}},
                                {17, {3, Ret::Int, true, "StretchRect", "W4aW4"}}};
constexpr auto kRegSurfaceSlots = Table<18>(kRegSurfaceAt);
constexpr drawfx::Iface kRegSurface{"RegisteredSurface", 18, kRegSurfaceSlots.data(), false, kDeclared};

constexpr uint32_t kSettingsId = 0x0ac1887e, kWinMgrId = 0xa417445e, kFrameworkCell = 0x10c9378;
constexpr uint32_t kStateClass = 0x6d548978;
constexpr uint32_t kPassTag = 0x7c000000, kItemsTag = 0x7b000000, kPartsTag = 0x7a000000, kRegisteredTag = 0x79000000;
constexpr uint32_t kSize = Self::kSize;
constexpr unsigned kSurfaces = 8, kStates = 3, kItems = 16, kRegistered = 4;
constexpr uint32_t kScissorVTable = 0xd9ff60;

enum SettingKind : uint8_t { kU32, kBool };
struct Setting {
    const char *name;
    SettingKind kind;
};
constexpr Setting kSettingList[] = {{"debugRenderBuffer", kU32},     {"renderOpaqueUI", kBool},
                                    {"dirtyRectsDebug", kU32},       {"dirtyRectsDisableIntervalTrees", kBool},
                                    {"dirtyRectsMergeThreshold", kU32}, {"useScissoring", kBool},
                                    {"useTurboRect", kBool},         {"useSplitRect", kBool},
                                    {"enablePostProcessing", kBool}, {"mirrorDirtyRects", kBool},
                                    {"dynamicRenderStrategy", kU32}, {"debugStencil", kBool},
                                    {"zeroFrameRectUpdate", kBool},  {"presentWorkaround", kBool}};
constexpr unsigned kSettingCount = sizeof(kSettingList) / sizeof(kSettingList[0]);

using SetUpFn = void(__thiscall *)(void *, nGZSceneGraph::Pass *);

struct World {
    Fake censor, brush, viewer, com, framework, settings, winmgr, window, device, deviceState, handle, outline, draw2d;
    Fake currentDevice, deviceEnum, properties, pixelBrush;
    Fake surfaces[kSurfaces], stateFakes[kStates], regSurfaces[kRegistered];
    uint32_t parts[kSurfaces][2]; // what each surface's GetColorSurface / GetDepthSurface hands out (registered)
    DynamicStrategyState *states[kStates] = {};
    DynamicStrategyState *viewerState = nullptr;
    uint32_t settingValue[kSettingCount];
    bool settingSet[kSettingCount];
    nGZSceneGraph::Pass pass;
    nGZSceneGraph::DrawItem items[kItems];
    DynamicRegisteredItem registered[kRegistered];
    bool original = false;
    unsigned throwIn = 0; // calls to the throw points until one throws (0: none)
    uint32_t lcg = 1;
    uint32_t Next()
    {
        lcg = lcg * 1664525u + 1013904223u;
        return lcg >> 8;
    }
};
World gw;

uint32_t W(const void *o, uint32_t at) { return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(o) + at); }
template <class T> T *Member(void *o, uint32_t at) { return reinterpret_cast<T *>(static_cast<uint8_t *>(o) + at); }

void ThrowPoint(const char *where)
{
    if (!gw.throwIn || --gw.throwIn != 0) return;
    {
        difftest::NotCounted bookkeeping;
        sgfx::NoteText("throw", where);
    }
    throw 7;
}

Fake *FakeOf(const void *state)
{
    for (unsigned i = 0; i < kStates; ++i)
        if (gw.states[i] == state) return &gw.stateFakes[i];
    return nullptr;
}

// ---- the fakes' handlers ----

uint32_t Word0(Fake &self, const uint32_t *) { return self.value[0]; }
uint32_t Word1(Fake &self, const uint32_t *) { return self.value[1]; }
uint32_t Word2(Fake &self, const uint32_t *) { return self.value[2]; }
uint32_t Word3(Fake &self, const uint32_t *) { return self.value[3]; }
uint32_t Word4(Fake &self, const uint32_t *) { return self.value[4]; }

// A surface's colour (bit 0 of value[5] clear) and depth (bit 1 clear) parts: test words, or null.
unsigned SurfaceIndex(const Fake &self) { return unsigned(&self - gw.surfaces); }
uint32_t ColorPart(Fake &self, const uint32_t *)
{
    return self.value[5] & 1 ? 0 : drawfx::P(&gw.parts[SurfaceIndex(self)][0]);
}
uint32_t DepthPart(Fake &self, const uint32_t *)
{
    return self.value[5] & 2 ? 0 : drawfx::P(&gw.parts[SurfaceIndex(self)][1]);
}

uint32_t GetService(Fake &, const uint32_t *a)
{
    Fake *const service = a[0] == kSettingsId ? &gw.settings : a[0] == kWinMgrId ? &gw.winmgr : nullptr;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = service;
    if (!service) return 0;
    ++service->refs;
    return 1;
}
// The key's text and the default in the value (its kind's width), then the seed's value written there (or nothing).
uint32_t GetPropertyValue(Fake &, const uint32_t *a)
{
    ThrowPoint("setting");
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
uint32_t MainWindow(Fake &, const uint32_t *) { return drawfx::P(&gw.window); }
// The window callback a walk is handed: 1 for the callback (the original's 0x3752d0 or ours), else the word.
uint32_t CallbackWord(uint32_t callback)
{
    const bool known = callback == drawfx::P(GameAddress<void>(0x3752d0)) ||
                       callback == drawfx::P(reinterpret_cast<const void *>(&Self::OpaqueUICallback));
    return known ? 1 : callback;
}
uint32_t WindowWalk(Fake &, const uint32_t *a)
{
    drawfx::Note("opaque UI walk", {CallbackWord(a[1]), *reinterpret_cast<const uint8_t *>(uintptr_t(a[2]))});
    ThrowPoint("window");
    return gw.Next() % 2;
}
constexpr uint32_t kPixelBrushClass = 0xdc012582; // Present's colour brush (the frame state's class otherwise)
uint32_t ClassObject(Fake &, const uint32_t *a)
{
    if (a[0] == kPixelBrushClass) {
        ++gw.pixelBrush.refs;
        *reinterpret_cast<void **>(uintptr_t(a[2])) = &gw.pixelBrush;
        return 1;
    }
    DynamicStrategyState *const made = gw.states[gw.Next() % kStates];
    ++FakeOf(made)->refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = made;
    return 1;
}
uint32_t ViewerState(Fake &, const uint32_t *) { return drawfx::P(gw.viewerState); }
uint32_t DeviceEnum(Fake &, const uint32_t *) { return drawfx::P(&gw.deviceEnum); }
uint32_t Properties(Fake &, const uint32_t *) { return drawfx::P(&gw.properties); }
// A registered surface's answers (value[0] IsCreated, value[1] StretchRect; their upper bytes any, the callers read al).
uint32_t RegCreated(Fake &self, const uint32_t *) { return self.value[0]; }
uint32_t RegStretch(Fake &self, const uint32_t *)
{
    ThrowPoint("stretch");
    return self.value[1];
}
uint32_t SetViewerState(Fake &, const uint32_t *a)
{
    auto *const made = reinterpret_cast<DynamicStrategyState *>(uintptr_t(a[0]));
    if (Fake *f = FakeOf(made)) ++f->refs;
    if (Fake *f = FakeOf(gw.viewerState)) --f->refs;
    gw.viewerState = made;
    return 0;
}

const float kNan = std::bit_cast<float>(0x7fc00000u);
const float kInf = std::bit_cast<float>(0x7f800000u);
const float kColors[] = {0.0f, 1.0f, 0.5f, -0.5f, 2.0f, 255.0f / 256.0f, 0.5f / 256.0f, 1.5f / 256.0f, 0.0019531249f,
                         0.0019531251f, 0.99805f, 0.25f, 1.0000001f, 0.99999994f, -0.0f, kNan, kInf, -kInf, 1e10f, -1e10f,
                         127.5f / 256.0f, 128.5f / 256.0f, 0.75f, 1e-30f};
float PickColor() { return kColors[gw.Next() % std::size(kColors)]; }

// The colour the caller put in (the original leaves the other words unset), then the seed's answer: the flags, the
// colour (or the caller's left), depth and stencil.
uint32_t ClearInfo(Fake &, const uint32_t *a)
{
    auto *const info = reinterpret_cast<nGZSceneGraph::cViewerClearInfo *>(uintptr_t(a[0]));
    {
        difftest::NotCounted bookkeeping;
        drawfx::Call c;
        c.note = "clear info colour in";
        drawfx::Words(c.words, info->mColor, 4, "u");
        drawfx::gLog.push_back(std::move(c));
    }
    const uint32_t r = gw.Next();
    info->mFlags = r % 5 == 0 ? gw.Next() : r % 4;
    if (gw.Next() % 3 != 0)
        for (float &c : info->mColor) c = PickColor();
    info->mDepth = gw.Next() % 2 ? 1.0f : PickColor();
    info->mStencil = gw.Next() % 3 ? 0 : gw.Next();
    return 0;
}
uint32_t DeviceClear(Fake &, const uint32_t *)
{
    ThrowPoint("device clear");
    return 0;
}

// A state command by its id: the header (vtable, id, size) and the fields each id's code sets (the viewport's depth
// words, the stencil state's bytes after its enable byte, are left unset by the original).
uint32_t ApplyCommand(Fake &, const uint32_t *a)
{
    difftest::NotCounted bookkeeping;
    const auto *w = reinterpret_cast<const uint32_t *>(uintptr_t(a[0]));
    const auto *b = reinterpret_cast<const uint8_t *>(w);
    const uint32_t id = w[1] & 0xff;
    drawfx::Call c;
    c.note = "command";
    c.words = {w[0], id, w[1] >> 16};
    const unsigned payload = id == 1 ? 4 : id == 2 ? 2 : id == 0x20 ? 5 : id == 7 ? 1 : 0;
    for (unsigned i = 0; i < payload; ++i) c.words.push_back(w[2 + i]);
    switch (id) {
    case 0: // the render targets (four colour, the depth): test words or null
        for (unsigned i = 2; i < 7; ++i) c.words.push_back(drawfx::Ptr(w[i]));
        break;
    case 0x14: c.words.insert(c.words.end(), {b[8], b[9]}); break;
    case 0x15: c.words.push_back(b[8]); break;
    case 0x16: c.words.insert(c.words.end(), {b[8], w[3]}); break;
    case 0x17: c.words.insert(c.words.end(), {b[8], w[3], w[4]}); break;
    case 0x18: c.words.insert(c.words.end(), {b[8], b[9], b[10]}); break;
    default: break;
    }
    drawfx::gLog.push_back(std::move(c));
    return 0;
}

// ---- the stand-ins ----

DynamicWords16 MakeRect()
{
    static const uint32_t kValues[] = {0, 1, 10, 100, 640, 480, 0xffffffffu, 0x80000000u, 0x7fffffffu, 33};
    DynamicWords16 r;
    for (uint32_t &w : r.w) w = gw.Next() % 3 ? gw.Next() % 1024 : kValues[gw.Next() % std::size(kValues)];
    return r;
}

// The paths' callees (rows TS1e-TS1g). A list argument is noted as which of the object's lists it is (0 mRects, 1
// mTurboRects, 2 another); a byte argument by the byte its callee reads (uint8_t(x)); a bool by whether that byte is set.
uint32_t ListId(const Self *self, const void *list)
{
    return list == Member<void>(const_cast<Self *>(self), 0x48) ? 0 : list == Member<void>(const_cast<Self *>(self), 0x50) ? 1 : 2;
}
uint32_t IsSet(uint32_t word) { return uint8_t(word) != 0; }
uint32_t ItemIndex(const nGZSceneGraph::DrawItem *item)
{
    const uintptr_t at = reinterpret_cast<uintptr_t>(item), base = reinterpret_cast<uintptr_t>(gw.items);
    return at >= base && at < base + sizeof gw.items ? uint32_t((at - base) / sizeof(nGZSceneGraph::DrawItem)) : 0xeeeeeeeeu;
}
// The edges ComputeBounds' stand picks from, the pixels' too (so the bounds tie with them now and then).
int32_t PickEdge()
{
    static const int32_t kEdges[] = {0, 0, 10, 100, 100, 480, 640, 640, 1023, -5, INT32_MAX, INT32_MIN};
    return kEdges[gw.Next() % std::size(kEdges)];
}
// 0x3706a0: rectangles for the frame's lists, a throw point.
void __fastcall StandUpdateDirtyRects(Self *self, void *, nGZSceneGraph::Pass *pass, const nGZSceneGraph::DrawItem *items,
                                      uint32_t count, uint32_t useRecords)
{
    drawfx::Note("update dirty rects", {drawfx::Ptr(pass), ItemIndex(items), count, IsSet(useRecords)});
    Rects &rects = *Member<Rects>(self, 0x48), &turbo = *Member<Rects>(self, 0x50);
    for (unsigned i = gw.Next() % 4; i; --i) rects.push_back(MakeRect());
    for (unsigned i = gw.Next() % 3; i; --i) turbo.push_back(MakeRect());
    ThrowPoint("update dirty rects");
}
// 0x3724a0 (ret 0x14).
void __fastcall StandMarkRecords(Self *self, void *, const Rects *rects, nGZSceneGraph::Pass *pass, uint32_t turbo,
                                 uint32_t test, uint32_t which)
{
    drawfx::Note("mark records", {ListId(self, rects), drawfx::Ptr(pass), IsSet(turbo), IsSet(test), uint8_t(which)});
}
// 0x370eb0 (ret 8): the side's bounds (by the byte read) from the edges, now and then empty or inverted.
void __fastcall StandComputeBounds(Self *self, void *, const Rects *rects, uint32_t which)
{
    drawfx::Note("compute bounds", {ListId(self, rects), uint8_t(which)});
    int32_t *const bounds = Member<int32_t>(self, uint8_t(which) ? 0x68 : 0x58);
    for (unsigned k = 0; k < 4; ++k) bounds[k] = PickEdge();
}
// 0x3719a0 (ret 8): an answer whose low byte is 0, 1 or another, its upper bytes now and then set.
uint32_t PickAnswer()
{
    uint32_t v = gw.Next() % 2;
    if (gw.Next() % 6 == 0) v = gw.Next() & 0xff;
    if (gw.Next() % 3 == 0) v |= gw.Next() << 8;
    return v;
}
uint32_t __fastcall StandSetUpScissor(Self *, void *, nGZSceneGraph::Pass *pass, uint32_t which)
{
    const uint32_t answer = PickAnswer();
    drawfx::Note("set up scissor", {drawfx::Ptr(pass), uint8_t(which), answer});
    return answer;
}
// 0x374ad0 (ret 4).
void __fastcall StandDrawCensor(Self *, void *, nGZSceneGraph::Pass *pass)
{
    drawfx::Note("draw censor", drawfx::Ptr(pass));
    ThrowPoint("draw censor");
}
// 0x374350 (ret 0x10).
void __fastcall StandDrawItemRun(Self *, void *, nGZSceneGraph::Pass *pass, nGZSceneGraph::DrawItem *items, uint32_t count,
                                 uint32_t late)
{
    drawfx::Note("draw item run", {drawfx::Ptr(pass), ItemIndex(items), count, uint8_t(late)});
}
// 0x374270 (ret 8).
void __fastcall StandDrawReflectionItems(Self *, void *, nGZSceneGraph::Pass *pass, uint32_t reflection)
{
    drawfx::Note("draw reflection items", {drawfx::Ptr(pass), uint8_t(reflection)});
}
// 0x374100 (ret 8): the mode is a whole word.
void __fastcall StandDrawItemsBy(Self *, void *, nGZSceneGraph::Pass *pass, uint32_t mode)
{
    drawfx::Note("draw items by", {drawfx::Ptr(pass), mode});
    ThrowPoint("draw items by");
}
// 0x373b70 (ret 0xc).
void __fastcall StandClearRects(Self *self, void *, nGZSceneGraph::Pass *pass, const Rects *rects, uint32_t second)
{
    drawfx::Note("clear rects", {drawfx::Ptr(pass), ListId(self, rects), uint8_t(second)});
}
// 0x377f50 (the censor's, ret 8): a few rectangles into `out`, an answer of 0 (gathered), 1, -1 or 2.
int32_t __fastcall StandCollectDirtyRects(void *, void *, nGZSceneGraph::Pass *pass, std::vector<DynamicWords16> *out)
{
    drawfx::Note("collect dirty rects", {drawfx::Ptr(pass), uint32_t(out->size())});
    for (unsigned i = gw.Next() % 3; i; --i) out->push_back(MakeRect());
    static const int32_t kAnswers[] = {0, 0, 0, 1, -1, 2};
    return kAnswers[gw.Next() % std::size(kAnswers)];
}
void *__cdecl StandCurrentDevice() { return &gw.currentDevice; }
void NoteList(const char *label, const Rects *rects)
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = label;
    c.words.push_back(uint32_t(rects->size()));
    for (const DynamicWords16 &r : *rects) c.words.insert(c.words.end(), std::begin(r.w), std::end(r.w));
    drawfx::gLog.push_back(std::move(c));
}
void __fastcall StandSetExternal(void *self, void *, void *texture)
{
    drawfx::Note("set external", {drawfx::Ptr(self), drawfx::Ptr(texture)});
}
void *StandCom() { return &gw.com; }
void *__cdecl StandDraw2D() { return &gw.draw2d; }
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
void __cdecl GameFree(void *p) { ::operator delete(p); }
void *__cdecl StandMalloc(uint32_t n) { return ::operator new(n); }
void __cdecl StandFree(void *p) { ::operator delete(p); }

struct Stands {
    shape::StandIn com{0x1ae18d, &GetDirectorFrameworkObject, &StandCom};
    shape::StandIn rzMalloc{0x1be3b0, &RZMemory::Malloc, &StandMalloc};
    shape::StandIn rzFree{0x1be3a1, &RZMemory::Free, &StandFree};
    shape::StandIn setExternal{0x3734d0, shape::MemberCode(&cTSRenderTextureSurface::SetExternal), &StandSetExternal};
    shape::StandIn updateDirtyRects{0x3706a0, shape::MemberCode(&Self::UpdateDirtyRects), &StandUpdateDirtyRects};
    shape::StandIn markRecords{0x3724a0, shape::MemberCode(&Self::MarkRecords), &StandMarkRecords};
    shape::StandIn computeBounds{0x370eb0, shape::MemberCode(&Self::ComputeBounds), &StandComputeBounds};
    shape::StandIn setUpScissor{0x3719a0, shape::MemberCode(&Self::SetUpScissor), &StandSetUpScissor};
    shape::StandIn drawCensor{0x374ad0, shape::MemberCode(&Self::DrawCensor), &StandDrawCensor};
    shape::StandIn drawItemRun{0x374350, shape::MemberCode(&Self::DrawItemRun), &StandDrawItemRun};
    shape::StandIn drawReflectionItems{0x374270, shape::MemberCode(&Self::DrawReflectionItems), &StandDrawReflectionItems};
    shape::StandIn drawItemsBy{0x374100, shape::MemberCode(&Self::DrawItemsBy), &StandDrawItemsBy};
    shape::StandIn clearRects{0x373b70, shape::MemberCode(&Self::ClearRects), &StandClearRects};
    shape::StandIn collectDirtyRects{0x377f50, shape::MemberCode(&cDisplayListCensorRenderStrategy::CollectDirtyRects),
                                     &StandCollectDirtyRects};
    shape::StandIn currentDevice{0x18ae40, &nGZGraphic4::GetCurrentDeviceCached, &StandCurrentDevice};
    shape::StandIn draw2d{0x18c000, &nGZGraphic4::GetDraw2DCached, &StandDraw2D};
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
    shape::CodePatch gameFree{0x82540fu, shape::CodeOf(&GameFree)};
};

// ---- the world ----

uint32_t PickSetting(unsigned i, uint32_t r)
{
    static const uint32_t kBuffers[] = {0, 0, 0, 1, 2, 3, 4, 5, 6, 0xffffffffu};
    static const uint32_t kStrategies[] = {1, 1, 2, 2, 2, 0, 3};
    switch (i) {
    case 0: return kBuffers[r % std::size(kBuffers)];
    case 2: return r % 5;
    case 4: return r % 3 ? r % 400 : r;
    case 10: return kStrategies[r % std::size(kStrategies)];
    default: return r % 5 == 0 ? 2 : r % 3 != 0;
    }
}
void PickSettings(std::mt19937 &rng)
{
    for (unsigned i = 0; i < kSettingCount; ++i) {
        gw.settingSet[i] = rng() % 6 != 0;
        gw.settingValue[i] = PickSetting(i, rng());
    }
}
void PickSurface(Fake &s, std::mt19937 &rng)
{
    s.value[0] = rng() % 4 == 0; // multisampled
    s.value[1] = rng() % 5 != 0; // valid
    s.value[2] = rng() % 4 != 0; // created
    static const uint32_t kSides[] = {640, 640, 640, 480, 0};
    s.value[3] = kSides[rng() % std::size(kSides)]; // width
    s.value[4] = kSides[rng() % std::size(kSides)]; // height
    s.value[5] = rng() % 6 == 0 ? 1 + rng() % 3 : 0; // the parts null
    // GetScale / GetAspect: 1 most times (CopyToSecond's product exactly 1), a float either side of it, others.
    static const float kScales[] = {1.0f, 1.0f, 1.0f, 1.0f, 0.5f, 2.0f, 0.75f, 1.5f, 1.0000001f, 0.99999994f, 0.0f, -1.0f,
                                    1.3333334f, 0.333f, std::bit_cast<float>(0x7fc00000u)};
    s.ret[3] = std::bit_cast<uint32_t>(kScales[rng() % std::size(kScales)]);
    s.ret[4] = std::bit_cast<uint32_t>(kScales[rng() % std::size(kScales)]);
}
uint8_t PickByte(std::mt19937 &rng)
{
    const uint32_t r = rng() % 6;
    return r < 2 ? 0 : r < 5 ? 1 : uint8_t(rng());
}
// The debug mode: 0-5 (3 the last that clears), the signed / unsigned edges, any word.
uint32_t PickDebugMode(std::mt19937 &rng)
{
    static const uint32_t kModes[] = {0, 1, 2, 3, 3, 4, 5, 0xffffffffu, 0x80000000u, 0x7fffffffu};
    return rng() % 6 ? kModes[rng() % std::size(kModes)] : rng();
}
// A registered item: its surface (a fake, its answers' low bytes 0, 1 or another, their upper bytes now and then set),
// the rectangle and the flags.
void PickRegistered(unsigned i, std::mt19937 &rng)
{
    Fake &s = gw.regSurfaces[i];
    for (unsigned k = 0; k < 2; ++k) {
        s.value[k] = rng() % 4 ? 1 : 0;
        if (rng() % 6 == 0) s.value[k] = rng() & 0xff;
        if (rng() % 3 == 0) s.value[k] |= rng() & 0xffffff00u;
    }
    DynamicRegisteredItem &item = gw.registered[i];
    item.mSurface = reinterpret_cast<nGZGraphic4::cISurface *>(&s);
    for (int32_t &v : item.mRect) v = int32_t(rng() % 5 ? rng() % 700 : rng());
    item.mKeep = PickByte(rng);
    item.mPending = rng() % 4 ? 1 : PickByte(rng);
    item.mDone = PickByte(rng);
    item.mFailed = PickByte(rng);
}

// The outlines' game cells: the alpha bytes (their neighbours kept), the flag cells' switch and the flags themselves.
constexpr uint32_t kAlphaCells[] = {0x10091dc, 0x10091e0, 0x10091e4, 0x10091e8};
constexpr uint32_t kFlagsEnabledCell = 0x10091c8, kFlags0Cell = 0x10d9d1c, kFlags1Cell = 0x10d9d20;
constexpr uint32_t kRestartCells = 0x10d9d18;
uint32_t PickOutlineFlags(std::mt19937 &rng)
{
    const uint32_t r = rng();
    return r % 5 == 0 ? 0 : r % 7 == 0 ? rng() : rng() & 0x4000071fu;
}
void PickCells(std::mt19937 &rng, drawfx::Cells &cells)
{
    auto byteCell = [&](uint32_t rva, uint8_t value) {
        cells.Set(rva, (*GameAddress<uint32_t>(rva) & 0xffffff00u) | value);
    };
    for (uint32_t rva : kAlphaCells) byteCell(rva, uint8_t(rng()));
    byteCell(kFlagsEnabledCell, rng() % 3 == 0 ? 0 : rng() % 4 == 0 ? uint8_t(rng()) : 1);
    cells.Set(kFlags0Cell, PickOutlineFlags(rng));
    cells.Set(kFlags1Cell, PickOutlineFlags(rng));
    // The paths' restart bytes (0x374670's at 0x10d9d18, 0x3743e0's at +1; one dword, its upper half kept): clear most
    // times (as the image leaves them), 1 or another byte.
    auto restart = [&] { return uint32_t(rng() % 3 ? 0 : rng() % 2 ? 1 : uint8_t(rng())); };
    const uint32_t restartBytes = restart() | restart() << 8;
    cells.Set(kRestartCells, (*GameAddress<uint32_t>(kRestartCells) & 0xffff0000u) | restartBytes);
}
float PickFloat(std::mt19937 &rng)
{
    static const float kFloats[] = {0.0f, 1.0f, 0.5f, 0.25f, 0.75f, -0.0f, 1e-3f, 0.999f};
    return kFloats[rng() % std::size(kFloats)];
}
void PickPass(std::mt19937 &rng)
{
    static const uint32_t kTypes[] = {0, 0, 0, 0, 1, 2, 3, 3, 7, 9, 5};
    nGZSceneGraph::Pass &p = gw.pass;
    p.mRenderType = kTypes[rng() % std::size(kTypes)];
    p.mHandleTexture = rng() % 5 == 0 ? reinterpret_cast<cIGZUnknown *>(&gw.handle) : nullptr;
    for (float &f : p.mViewportMin) f = PickFloat(rng);
    for (float &f : p.mViewportMax) f = PickFloat(rng);
    for (float &f : p.mDepthRange) f = PickFloat(rng);
    for (int32_t &v : p.mPixelRect) v = int32_t(rng() % 3 ? rng() % 1024 : rng());
    for (float &f : p.mMatrices[0]) f = float(int32_t(rng() % 200) - 100) / 8.0f;
}

void Build(uint32_t seed, drawfx::Cells &cells)
{
    gw.lcg = seed * 2654435761u + 7;
    std::mt19937 rng(seed ^ 0xd5a3);
    gw.censor.Reset<kCensor>(900);
    gw.brush.Reset<kBrush>(901);
    gw.viewer.Reset<kViewer>(902);
    gw.viewer.On(99, &SetViewerState);
    gw.viewer.On(104, &ClearInfo);
    gw.viewer.On(116, &ViewerState);
    gw.com.Reset<kCom>(903);
    gw.com.On(3, &ClassObject);
    gw.framework.Reset<kFramework>(904);
    gw.framework.On(5, &GetService);
    gw.settings.Reset<kSettings>(905);
    gw.settings.On(9, &GetPropertyValue);
    gw.winmgr.Reset<kWinMgr>(906);
    gw.winmgr.On(3, &MainWindow);
    gw.window.Reset<kWindow>(907);
    gw.window.On(37, &WindowWalk);
    gw.device.Reset<kDevice>(908);
    gw.device.On(27, &DeviceClear);
    gw.deviceState.Reset<kDeviceState>(909);
    gw.deviceState.On(12, &ApplyCommand);
    gw.handle.Reset<kUnknown>(910);
    gw.outline.Reset<kOutline>(911);
    gw.draw2d.Reset<fx_slots::cIDraw2D>(912);
    gw.currentDevice.Reset<kCurrentDevice>(913);
    gw.currentDevice.On(16, &DeviceEnum);
    gw.deviceEnum.Reset<kDeviceEnum>(914);
    gw.deviceEnum.On(22, &Properties);
    gw.properties.Reset<kSettings>(915);
    gw.properties.On(9, &GetPropertyValue);
    gw.pixelBrush.Reset<kBrush>(916);
    for (unsigned i = 0; i < kRegistered; ++i) {
        gw.regSurfaces[i].Reset<kRegSurface>(940 + i);
        gw.regSurfaces[i].On(6, &RegCreated);
        gw.regSurfaces[i].On(17, &RegStretch);
        PickRegistered(i, rng);
    }
    drawfx::AddRange(gw.registered, sizeof gw.registered, kRegisteredTag);
    for (unsigned i = 0; i < kSurfaces; ++i) {
        gw.surfaces[i].Reset<kSurface>(920 + i);
        gw.surfaces[i].On(0, &Word3);
        gw.surfaces[i].On(1, &Word4);
        gw.surfaces[i].On(6, &Word0);
        gw.surfaces[i].On(8, &Word1);
        gw.surfaces[i].On(9, &Word2);
        gw.surfaces[i].On(19, &ColorPart);
        gw.surfaces[i].On(20, &DepthPart);
        PickSurface(gw.surfaces[i], rng);
    }
    drawfx::AddRange(gw.parts, sizeof gw.parts, kPartsTag);
    PickCells(rng, cells);
    for (unsigned i = 0; i < kStates; ++i) gw.stateFakes[i].Reset<kUnknown>(930 + i);
    PickSettings(rng);
    std::memset(&gw.pass, 0, sizeof gw.pass);
    drawfx::AddRange(&gw.pass, sizeof gw.pass, kPassTag);
    drawfx::AddRange(gw.items, sizeof gw.items, kItemsTag);
    // The items: any bytes (the paths' callees are stood in), their record slots (mUserData) the tables' first few,
    // past them, or any.
    static const uint32_t kSlots[] = {0, 1, 2, 3, 4, 5, 6, 0xffffffffu};
    for (nGZSceneGraph::DrawItem &item : gw.items) {
        for (uint32_t b = 0; b < sizeof item; ++b) reinterpret_cast<uint8_t *>(&item)[b] = uint8_t(rng());
        item.mUserData = rng() % 5 ? kSlots[rng() % std::size(kSlots)] : rng();
    }
    gw.pass.mViewer = reinterpret_cast<nGZSceneGraph::cIViewerRefNodeBase *>(&gw.viewer);
    gw.pass.mDevice = reinterpret_cast<nGZGraphic4::cIDeviceDX *>(&gw.device);
    gw.pass.mDeviceState = reinterpret_cast<nGZGraphic4::cDeviceStateDX *>(&gw.deviceState);
    PickPass(rng);
    gw.throwIn = 0;
    cells.Set(kFrameworkCell, &gw.framework);
}

void NoteFakes()
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = "fake refs";
    for (Fake *f : {&gw.censor, &gw.brush, &gw.viewer, &gw.com, &gw.framework, &gw.settings, &gw.winmgr, &gw.window,
                    &gw.device, &gw.deviceState, &gw.handle, &gw.outline, &gw.draw2d, &gw.currentDevice, &gw.deviceEnum,
                    &gw.properties, &gw.pixelBrush})
        c.words.push_back(uint32_t(f->refs));
    for (const Fake &f : gw.surfaces) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.regSurfaces) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.stateFakes) c.words.push_back(uint32_t(f.refs));
    drawfx::gLog.push_back(std::move(c));
}

// ---- the object ----

// The strategy's words by kind ('-' the two class vtables; 'a' what the test set to pointers).
std::array<char, kSize / 4 + 1> ObjectKinds()
{
    std::array<char, kSize / 4 + 1> k{};
    for (char &c : k) c = 'u';
    k[kSize / 4] = 0;
    k[0] = k[2] = '-';
    for (unsigned at : {0xcu, 0x10u, 0x14u, 0x18u, 0x48u, 0x50u, 0xd4u, 0xf0u, 0xf4u, 0xf8u, 0x100u, 0x148u, 0x14cu}) k[at / 4] = 'a';
    for (unsigned at = 0x30; at < 0x48; at += 4) k[at / 4] = 'a';   // the two pointer vectors
    for (unsigned at = 0x104; at < 0x144; at += 4) k[at / 4] = 'a'; // the surfaces, the registered items' vector
    // ORIGINAL-BUG: 0x3743e0 copies its whole viewport's depth words, which it never sets, into mViewport (+0x18c /
    // +0x190): the original's are its stack's leftovers, ours 0.
    k[0x18c / 4] = k[0x190 / 4] = '-';
    return k;
}
const auto kObjectKinds = ObjectKinds();

void NoteVector(const char *label, const Words *v)
{
    drawfx::Call c;
    c.note = label;
    c.words.push_back(uint32_t(v->size()));
    c.words.insert(c.words.end(), v->begin(), v->end());
    drawfx::gLog.push_back(std::move(c));
}
// A vector of pointers (the items, their records in the tables, the registered items): each as its record.
void NotePointers(const char *label, const Pointers *v)
{
    drawfx::Call c;
    c.note = label;
    c.words.push_back(uint32_t(v->size()));
    for (const void *p : *v) c.words.push_back(drawfx::Ptr(p));
    drawfx::gLog.push_back(std::move(c));
}
// The items' records: each as its frame state's index and its index in the table's records (the records' array may be
// aligned inside a larger block, NoteTable), null as 0, anything else by its record. A table's Reset (0x370f90)
// clears its records in place, so a record listed before it is still inside the array: up to its capacity counts.
void NoteItemRecords(const Pointers *v)
{
    drawfx::Call c;
    c.note = "item records 3c";
    c.words.push_back(uint32_t(v->size()));
    for (const void *p : *v) {
        uint32_t word = p ? 0xffffffffu : 0;
        for (unsigned i = 0; i < kStates && word == 0xffffffffu; ++i) {
            const auto &records = gw.states[i]->mTable->mRecords;
            const auto *rec = static_cast<const DynamicRectRecord *>(p);
            if (rec >= records.data() && rec < records.data() + records.capacity())
                word = 0x51000000u | i << 16 | uint32_t(rec - records.data());
        }
        c.words.push_back(word == 0xffffffffu ? drawfx::Ptr(p) : word);
    }
    drawfx::gLog.push_back(std::move(c));
}
// The registered items' rectangles and flags (PresentRegistered sets the flags).
void NoteRegistered()
{
    drawfx::Call c;
    c.note = "registered items";
    for (const DynamicRegisteredItem &item : gw.registered) {
        c.words.push_back(drawfx::Ptr(item.mSurface));
        c.words.insert(c.words.end(), std::begin(item.mRect), std::end(item.mRect));
        c.words.insert(c.words.end(), {item.mKeep, item.mPending, item.mDone, item.mFailed});
    }
    drawfx::gLog.push_back(std::move(c));
}
void NoteBits(const char *label, const std::vector<bool> *v)
{
    drawfx::Call c;
    c.note = label;
    c.words.push_back(uint32_t(v->size()));
    for (bool b : *v) c.words.push_back(b);
    drawfx::gLog.push_back(std::move(c));
}
void NoteMap(const char *label, const DynamicStrategyHelper::Map *map)
{
    drawfx::Call c;
    c.note = label;
    drawfx::Words(c.words, map, 8, "uauaaauu");
    for (const auto &[key, value] : *map) {
        c.words.push_back(key);
        c.words.insert(c.words.end(), std::begin(value.w), std::end(value.w));
    }
    drawfx::gLog.push_back(std::move(c));
}
void NoteTable(const DynamicStrategyTable *t)
{
    drawfx::Call c;
    c.note = "table";
    // The records' vector as its size and capacity: its array (1000 records, 36000 bytes) is over 4 KB, which both
    // sides' allocators align to 32 bytes inside a larger block, so its words point at offsets of their blocks that
    // depend on where the block fell.
    drawfx::Words(c.words, t, sizeof(DynamicStrategyTable) / 4, "aaauaaa---aaaaaau");
    c.words.push_back(t->mRecords.data() != nullptr);
    c.words.push_back(uint32_t(t->mRecords.size()));
    c.words.push_back(uint32_t(t->mRecords.capacity()));
    drawfx::gLog.push_back(std::move(c));
    NoteBits("table 0", &t->mDirtyTurbo);
    drawfx::Call r;
    r.note = "table records";
    r.words.push_back(uint32_t(t->mRecords.size()));
    for (const DynamicRectRecord &rec : t->mRecords) drawfx::Words(r.words, &rec, sizeof rec / 4, "u");
    drawfx::gLog.push_back(std::move(r));
    NoteVector("table 28", &t->mLastVisible);
    NoteVector("table 34", &t->mVisible);
}

// What the overlay and the outlines leave outside the object: the pass's textures (test words, or null) and the
// outlines' flag cells.
void NoteOutside()
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = "pass textures, outline flags";
    c.words.push_back(gw.pass.mTextureCount);
    for (cIGZUnknown *t : gw.pass.mTextures) c.words.push_back(drawfx::Ptr(t));
    c.words.push_back(drawfx::Ptr(gw.pass.mTextureRef94));
    c.words.push_back(*GameAddress<uint32_t>(kFlags0Cell));
    c.words.push_back(*GameAddress<uint32_t>(kFlags1Cell));
    drawfx::gLog.push_back(std::move(c));
}

void NoteObject(void *o)
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = "object";
    drawfx::Words(c.words, o, kSize / 4, kObjectKinds.data());
    drawfx::gLog.push_back(std::move(c));
    NotePointers("items 30", Member<Pointers>(o, 0x30));
    NoteItemRecords(Member<Pointers>(o, 0x3c));
    NotePointers("registered 138", Member<Pointers>(o, 0x138));
    NoteRegistered();
    NoteList("rects", Member<Rects>(o, 0x48));
    NoteList("turbo rects", Member<Rects>(o, 0x50));
    NoteList("last rects", Member<Rects>(o, 0x14c));
    const auto *helper = *Member<DynamicStrategyHelper *>(o, 0x18);
    drawfx::Call h;
    h.note = "helper";
    drawfx::Words(h.words, helper, 3, "a"); // the vector
    h.words.push_back(helper->mFlagC);
    drawfx::Words(h.words, &helper->mMap10, 5, "aau"); // the maps, the frame, the unset words
    h.words.push_back(helper->mByte24);
    h.words.push_back(helper->mDebug);
    drawfx::gLog.push_back(std::move(h));
    NoteMap("map 10", helper->mMap10);
    NoteMap("map 14", helper->mMap14);
    for (const DynamicStrategyState *s : gw.states) {
        drawfx::Call st;
        st.note = "frame state";
        // The face's vtable, the modes and projection, the table, the frame count and the rest.
        drawfx::Words(st.words, s, 1, "-");
        drawfx::Words(st.words, &s->mMode, 18, "u");
        drawfx::Words(st.words, &s->mTable, 1, "a");
        drawfx::Words(st.words, &s->mFrames, 5, "u");
        drawfx::gLog.push_back(std::move(st));
        NoteTable(s->mTable);
    }
    NoteOutside();
}

// ---- the run ----

uint8_t *Face(void *o) { return static_cast<uint8_t *>(o) + 8; }

// A projection for Render: the frame state's own, some elements moved a few representable floats, or another.
void PickProjection(std::mt19937 &rng)
{
    float *const m = gw.pass.mMatrices[4];
    const DynamicStrategyState *base = gw.viewerState ? gw.viewerState : gw.states[rng() % kStates];
    std::memcpy(m, base->mProjection, sizeof(base->mProjection));
    const unsigned how = rng() % 4;
    if (how == 0) return;
    if (how == 3) {
        for (unsigned i = 0; i < 16; ++i) m[i] = float(int32_t(rng() % 64) - 32) / 4.0f;
        return;
    }
    static const int32_t kSteps[] = {1, -1, 31, -31, 32, -32, 33, -33, 0x400000, -0x400000};
    for (unsigned n = 1 + rng() % 2; n; --n) {
        uint32_t &bits = reinterpret_cast<uint32_t *>(m)[rng() % 16];
        bits = uint32_t(int32_t(bits) + kSteps[rng() % std::size(kSteps)]);
    }
}

struct Run {
    difftest::Context &ctx;
    bool original;
    void *o;

    Self *Ours() const { return static_cast<Self *>(o); }
    nGZSceneGraph::Pass *Pass() const { return &gw.pass; }
    nGZSceneGraph::DrawItem *Items() const { return gw.items; }
    DynamicStrategyState *&State() const { return *Member<DynamicStrategyState *>(o, 0xd4); }
    // The surfaces a step on its own may use without the draw steps' invariants: the current pair.
    bool Complete() const { return W(o, 0x114) && W(o, 0x118); }
    template <class F> F Original(uint32_t rva) const { return ctx.Original<F>(rva); }

    void ChangeWorld(std::mt19937 &rng)
    {
        const unsigned what = rng() % 5;
        drawfx::Note("change", what);
        if (what == 0) PickSettings(rng);
        else if (what == 1) PickSurface(gw.surfaces[rng() % kSurfaces], rng);
        else if (what == 2) PickPass(rng);
        else if (what == 3) gw.viewerState = nullptr; // the viewer's reference dropped (its count kept by the fake)
        else {
            const unsigned i = rng() % kSettingCount;
            gw.settingSet[i] = true;
            gw.settingValue[i] = PickSetting(i, rng());
        }
    }

    void Throws(std::mt19937 &rng) { gw.throwIn = rng() % 5 == 0 ? 1 + rng() % 14 : 0; }

    void Step(std::mt19937 &rng)
    {
        const unsigned what = rng() % 24;
        drawfx::Note("op", what);
        try {
            Do(what, rng);
        } catch (int e) {
            drawfx::Note("threw", uint32_t(e));
        }
        gw.throwIn = 0;
    }

    void Do(unsigned what, std::mt19937 &rng)
    {
        DynamicStrategyHelper *const helper = *Member<DynamicStrategyHelper *>(o, 0x18);
        uint32_t &frame = *Member<uint32_t>(o, 0xd8);
        if (what == 0) {
            const uint32_t to = frame + rng() % 3 - 1;
            if (original) Original<void(__thiscall *)(void *, uint32_t)>(0x3755e0)(helper, to);
            else helper->FrameStep(to);
        } else if (what == 1) {
            if (original) Original<void(__thiscall *)(void *)>(0x370fc0)(helper);
            else helper->Reset();
        } else if (what == 2) {
            DynamicStrategyTable *const table = gw.states[rng() % kStates]->mTable;
            const uint32_t to = frame + rng() % 3 - 1;
            if (original) Original<void(__thiscall *)(void *, uint32_t)>(0x375590)(table, to);
            else table->FrameStep(to);
        } else if (what == 3) {
            DynamicStrategyTable *const table = gw.states[rng() % kStates]->mTable;
            if (original) Original<void(__thiscall *)(void *)>(0x370f90)(table);
            else table->Reset();
        } else if (what == 4) {
            if (original) Original<void(__thiscall *)(void *, void *)>(0x3714b0)(o, Pass());
            else Ours()->DisableScissor(Pass());
        } else if (what == 5) {
            uint8_t flag = uint8_t(rng());
            const uint8_t value = rng() % 4 == 0 ? uint8_t(rng()) : uint8_t(rng() % 2);
            const uint32_t above = rng() & 0xffffff00u; // the pushed word's other bytes (the original reads the low one)
            Throws(rng);
            if (original) Original<void(__thiscall *)(uint8_t *, uint32_t)>(0x375240)(&flag, value | above);
            else Self::MarkOpaqueUI(&flag, value);
            (void)above;
            drawfx::Note("flag", flag);
        } else if (what < 10) {
            if (!Complete()) return;
            State() = gw.states[rng() % kStates];
            if (rng() % 2) ChangeWorld(rng);
            if (what == 6) {
                if (original) Original<SetUpFn>(0x374dd0)(o, Pass());
                else Ours()->SetUpTarget(Pass());
            } else if (what == 7) {
                const bool changed = rng() % 3 == 0;
                if (original) Original<void(__thiscall *)(void *, void *, uint32_t)>(0x371b10)(o, Pass(), changed);
                else Ours()->Prepare(Pass(), changed);
            } else if (what == 8) {
                const uint32_t count = rng() % kItems;
                if (original) Original<void(__thiscall *)(void *, void *, void *, uint32_t)>(0x373e20)(o, Pass(), Items(), count);
                else Ours()->Draw(Pass(), Items(), count);
            } else {
                if (original) Original<void(__thiscall *)(void *, void *)>(0x376460)(o, Pass());
                else Ours()->Finish(Pass());
            }
            State() = nullptr;
        } else if (what < 17) {
            if (rng() % 3 == 0) ChangeWorld(rng);
            PickProjection(rng);
            const uint32_t count = rng() % kItems;
            Throws(rng);
            if (original)
                Original<void(__thiscall *)(void *, void *, void *, uint32_t)>(0x374930)(Face(o), Pass(), Items(), count);
            else Ours()->Self::Render(Pass(), Items(), count);
        } else if (what < 20) {
            if (rng() % 3 == 0) ChangeWorld(rng);
            Throws(rng);
            if (original) Original<void(__thiscall *)(void *)>(0x373920)(Face(o));
            else Ours()->Self::Clear();
        } else {
            ChangeWorld(rng);
        }
    }
};

// The frame states (each with its table) and the strategy block, filled alike.
DynamicStrategyTable *MakeTable(std::mt19937 &rng)
{
    void *const block = ::operator new(sizeof(DynamicStrategyTable));
    for (uint32_t i = 0; i < sizeof(DynamicStrategyTable); ++i) static_cast<uint8_t *>(block)[i] = uint8_t(rng());
    auto *const t = new (block) DynamicStrategyTable;
    for (unsigned n = rng() % 80; n; --n) t->mDirtyTurbo.push_back((rng() & 1) != 0);
    for (Words *v : {&t->mLastVisible, &t->mVisible})
        for (unsigned n = rng() % 4; n; --n) v->push_back(rng());
    t->mFrame = rng() % 8;
    // A few records (the constructor's are uninitialised but for the frame), any bytes, frames near the table's.
    t->mRecords.resize(rng() % 6);
    for (DynamicRectRecord &rec : t->mRecords) {
        for (uint32_t b = 0; b < sizeof rec; ++b) reinterpret_cast<uint8_t *>(&rec)[b] = uint8_t(rng());
        rec.mFrame = rng() % 4 ? t->mFrame - rng() % 3 : rng();
    }
    t->mRecordArea = rng();
    t->mDirtyArea = rng();
    t->mDebug = uint8_t(rng() % 2);
    t->mFilled = rng() % 4 == 0 ? 0x80 : uint8_t(rng() % 2);
    return t;
}
DynamicStrategyState *MakeState(unsigned i, std::mt19937 &rng)
{
    auto *const s = static_cast<DynamicStrategyState *>(::operator new(sizeof(DynamicStrategyState)));
    for (uint32_t b = 0; b < sizeof(DynamicStrategyState); ++b) reinterpret_cast<uint8_t *>(s)[b] = uint8_t(rng());
    gw.stateFakes[i].Embed(s);
    s->mMode = rng() % 4;
    s->mLastMode = rng() % 4;
    for (uint32_t &w : s->mProjection) w = std::bit_cast<uint32_t>(float(int32_t(rng() % 64) - 32) / 4.0f);
    s->mTable = MakeTable(rng);
    s->mFrames = rng() % 3 ? rng() % 4 : rng();
    return s;
}

void *MakeObject(std::mt19937 &rng, void *&censor)
{
    void *const o = ::operator new(kSize);
    for (uint32_t i = 0; i < kSize; ++i) static_cast<uint8_t *>(o)[i] = uint8_t(rng());
    *Member<const void *>(o, 0) = GameAddress<void>(0xdbbadc);
    *Member<const void *>(o, 8) = GameAddress<void>(0xdbbafc);
    censor = ::operator new(0x10);
    for (uint32_t i = 0; i < 0x10; ++i) static_cast<uint8_t *>(censor)[i] = uint8_t(rng());
    gw.censor.Embed(static_cast<uint8_t *>(censor) + 8);
    *Member<void *>(o, 0xc) = censor;
    *Member<void *>(o, 0x10) = &gw.brush;
    *Member<void *>(o, 0x14) = &gw.outline;
    auto *const helper = new DynamicStrategyHelper;
    helper->mFlagC = uint8_t(rng());
    helper->mRecordedArea = rng();
    helper->mAddedArea = rng();
    helper->mByte24 = uint8_t(rng());
    helper->mDebug = uint8_t(rng());
    for (DynamicStrategyHelper::Map *map : {helper->mMap10, helper->mMap14})
        for (unsigned n = rng() % 4; n; --n) {
            DynamicWords24 &v = (*map)[rng() % 1000];
            for (uint32_t &w : v.w) w = rng();
        }
    *Member<DynamicStrategyHelper *>(o, 0x18) = helper;
    // The item lists left by an earlier frame: items, or null; the records null (no table record is known yet).
    for (uint32_t at : {0x30u, 0x3cu}) {
        Pointers *const v = new (Member<void>(o, at)) Pointers;
        for (unsigned n = rng() % 4; n; --n)
            v->push_back(at == 0x30u && rng() % 3 ? static_cast<void *>(&gw.items[rng() % kItems]) : nullptr);
    }
    // The registered items (0-4 of them, one now and then twice).
    Pointers *const registered = new (Member<void>(o, 0x138)) Pointers;
    for (unsigned n = rng() % 5; n; --n) registered->push_back(&gw.registered[rng() % kRegistered]);
    for (uint32_t at : {0x48u, 0x50u, 0x14cu}) {
        Rects *const l = new (Member<void>(o, at)) Rects;
        for (unsigned n = rng() % 3; n; --n) {
            DynamicWords16 r;
            for (uint32_t &w : r.w) w = rng() % 1000;
            l->push_back(r);
        }
    }
    // The scissor state's header (its words random).
    *Member<const void *>(o, 0x7c) = GameAddress<void>(kScissorVTable);
    *Member<uint8_t>(o, 0x80) = 0x20;
    *Member<uint16_t>(o, 0x82) = 0x1c;
    auto surface = [&] { return static_cast<void *>(&gw.surfaces[rng() % kSurfaces]); };
    for (uint32_t at = 0x104; at < 0x138; at += 4) *Member<void *>(o, at) = surface();
    for (uint32_t at : {0xf0u, 0xf4u, 0xf8u, 0x100u}) *Member<void *>(o, at) = surface();
    if (rng() % 4 == 0) *Member<void *>(o, 0xf8) = nullptr;
    if (rng() % 5 == 0) *Member<void *>(o, 0x114) = nullptr;
    if (rng() % 5 == 0) *Member<void *>(o, 0x118) = nullptr;
    *Member<void *>(o, 0xd4) = nullptr;
    *Member<void *>(o, 0x148) = rng() % 3 == 0 ? gw.states[rng() % kStates] : nullptr;
    *Member<uint32_t>(o, 0xd8) = rng() % 8;
    *Member<uint32_t>(o, 0x2c) = rng() % 4;  // mInvalidated
    *Member<uint32_t>(o, 0xec) = rng() % 7;  // mDebugBuffer
    *Member<uint8_t>(o, 0xd2) = uint8_t(rng() % 2); // mReducedResolution: 0 / 1 only (slot 8 is its one writer)
    // The flags the paths read as bytes (mTurbo, mDirtyRects, mRedraw, mPostProcessing): 0, 1 or another byte.
    for (uint32_t at : {0xccu, 0xceu, 0xd0u, 0xd1u}) *Member<uint8_t>(o, at) = PickByte(rng);
    *Member<uint32_t>(o, 0xc0) = PickDebugMode(rng); // mDebugMode (DrawSide tests <= 3)
    // mPixelRect now and then from the bounds' edges (0x3743e0's clip ties with them).
    if (rng() % 2)
        for (uint32_t k = 0; k < 4; ++k) *Member<int32_t>(o, 0xdc + 4 * k) = PickEdge();
    if (rng() % 3 == 0) *Member<void *>(o, 0x100) = W(o, 0x134) ? *Member<void *>(o, 0x134) : nullptr; // mFinal the back buffer
    if (rng() % 4 == 0) *Member<void *>(o, 0xf4) = *Member<void *>(o, 0xf0); // the two targets one
    if (rng() % 4 == 0) *Member<void *>(o, 0xf8) = *Member<void *>(o, 0xf4); // the post-processing surface the second
    helper->mFrame = W(o, 0xd8) + rng() % 3 - 1;
    return o;
}

void Free(void *o, void *censor)
{
    for (uint32_t at : {0x48u, 0x50u, 0x14cu}) std::destroy_at(Member<Rects>(o, at));
    for (uint32_t at : {0x30u, 0x3cu, 0x138u}) std::destroy_at(Member<Pointers>(o, at));
    delete *Member<DynamicStrategyHelper *>(o, 0x18);
    ::operator delete(censor);
    ::operator delete(o);
    for (DynamicStrategyState *&s : gw.states) {
        std::destroy_at(s->mTable);
        ::operator delete(s->mTable);
        ::operator delete(s);
        s = nullptr;
    }
    gw.viewerState = nullptr;
}

void RunDraw(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    difftest::AllocationCounter counter;
    gw.original = original;
    for (unsigned i = 0; i < kStates; ++i) gw.states[i] = MakeState(i, rng);
    gw.viewerState = rng() % 4 ? gw.states[0] : nullptr;
    void *censor = nullptr;
    void *const o = MakeObject(rng, censor);
    Run run{ctx, original, o};
    NoteObject(o);
    const unsigned steps = 4 + rng() % 30;
    for (unsigned i = 0; i < steps; ++i) {
        sgfx::MarkStep(ctx, "step");
        run.Step(rng);
        NoteObject(o);
    }
    sgfx::MarkStep(ctx, "free");
    Free(o, censor);
    NoteFakes();
    counter.Stop();
    drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
}

// ---- the drawing paths alone (row TS1i) ----

// A record's index in its table's records, null as 0xffffffff (NoteItemRecords' words otherwise).
uint32_t RecordWord(const DynamicStrategyTable *t, const void *p)
{
    if (!p) return 0xffffffffu;
    const auto *rec = static_cast<const DynamicRectRecord *>(p);
    const auto &records = t->mRecords;
    return rec >= records.data() && rec < records.data() + records.capacity() ? uint32_t(rec - records.data())
                                                                                : drawfx::Ptr(p);
}

// Each step one of the paths or their helpers called directly, with the frame state set and its frame count picked
// (the first, second and third frames, the count's unsigned edge, any), the world now and then changed.
void PathStep(Run &run, std::mt19937 &rng)
{
    const bool original = run.original;
    void *const o = run.o;
    const unsigned what = rng() % 12;
    drawfx::Note("path op", what);
    DynamicStrategyState *const state = gw.states[rng() % kStates];
    static const uint32_t kFrames[] = {0, 0, 1, 1, 2, 2, 3, 0xffffffffu, 0x80000000u};
    state->mFrames = rng() % 6 ? kFrames[rng() % std::size(kFrames)] : rng();
    run.State() = state;
    if (rng() % 4 == 0) run.ChangeWorld(rng);
    if (rng() % 4 == 0) PickRegistered(rng() % kRegistered, rng);
    if (rng() % 3 == 0) *Member<uint32_t>(o, 0xc0) = PickDebugMode(rng);
    if (rng() % 3 == 0) *Member<uint8_t>(o, 0xcc + 2 * (rng() % 2)) = PickByte(rng); // mTurbo / mDirtyRects
    if (rng() % 4 == 0) *Member<uint8_t>(o, 0xd0 + rng() % 2) = PickByte(rng);       // mRedraw / mPostProcessing
    if (rng() % 5 == 0) *Member<void *>(o, 0xf4) = *Member<void *>(o, 0xf0);
    else if (rng() % 5 == 0) *Member<void *>(o, 0xf4) = &gw.surfaces[rng() % kSurfaces];
    if (rng() % 5 == 0) *Member<void *>(o, 0x100) = *Member<void *>(o, rng() % 2 ? 0x134 : 0xf4);
    // The surfaces the helpers use unchecked (as Draw has them after SetUpTarget), and the pair SetUpTarget picks from.
    for (uint32_t at : {0xf0u, 0xf4u, 0xf8u, 0x100u, 0x114u, 0x118u})
        if (!W(o, at)) *Member<void *>(o, at) = &gw.surfaces[rng() % kSurfaces];
    const uint32_t count = rng() % 8 == 0 ? 0 : rng() % (kItems + 1);
    nGZSceneGraph::Pass *const pass = run.Pass();
    try {
        if (what == 0) {
            // Record: an item's slot against the table's count (one under, equal, one over, the edges).
            DynamicStrategyTable *const t = state->mTable;
            nGZSceneGraph::DrawItem &item = gw.items[rng() % kItems];
            const uint32_t n = uint32_t(t->mRecords.size());
            const uint32_t slots[] = {n - 1, n, n + 1, 0, 0xffffffffu, 0x80000000u, item.mUserData};
            item.mUserData = slots[rng() % std::size(slots)];
            const void *rec;
            if (original) rec = run.Original<void *(__thiscall *)(void *, void *)>(0x372150)(t, &item);
            else rec = t->Record(&item);
            drawfx::Note("record", {item.mUserData, n, RecordWord(t, rec)});
        } else if (what == 1) {
            if (original) run.Original<void(__thiscall *)(void *, void *, void *, uint32_t)>(0x373a70)(o, pass, gw.items, count);
            else run.Ours()->ListItems(pass, gw.items, count);
        } else if (what == 2) {
            run.Throws(rng);
            if (original) run.Original<void(__thiscall *)(void *)>(0x371150)(o);
            else run.Ours()->PresentRegistered();
        } else if (what == 3) {
            if (original) run.Original<void(__thiscall *)(void *)>(0x371240)(o);
            else run.Ours()->CopyToSecond();
        } else if (what == 4) {
            run.Throws(rng);
            if (original) run.Original<void(__thiscall *)(void *, void *)>(0x373f00)(o, pass);
            else run.Ours()->Present(pass);
        } else if (what < 7) {
            // DrawSide: the side 0 or 1 as a byte (the original reads the low one; its pushed word's others any). Its
            // callers pass only 0 and 1: the original takes its list at +0x48 + 8 * side, past mTurboRects for others.
            const uint8_t which = uint8_t(rng() % 2);
            const uint32_t above = rng() % 2 ? rng() & 0xffffff00u : 0;
            run.Throws(rng);
            if (original) run.Original<void(__thiscall *)(void *, void *, uint32_t)>(0x374bb0)(o, pass, which | above);
            else run.Ours()->DrawSide(pass, which);
        } else if (what < 10) {
            run.Throws(rng);
            if (original) run.Original<void(__thiscall *)(void *, void *, void *, uint32_t)>(0x374670)(o, pass, gw.items, count);
            else run.Ours()->DrawPath(pass, gw.items, count);
        } else {
            run.Throws(rng);
            if (original) run.Original<void(__thiscall *)(void *, void *, void *, uint32_t)>(0x3743e0)(o, pass, gw.items, count);
            else run.Ours()->DrawOwnPath(pass, gw.items, count);
        }
    } catch (int e) {
        drawfx::Note("threw", uint32_t(e));
    }
    gw.throwIn = 0;
    drawfx::Note("frames", state->mFrames);
    run.State() = nullptr;
}

void RunPaths(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    difftest::AllocationCounter counter;
    gw.original = original;
    for (unsigned i = 0; i < kStates; ++i) gw.states[i] = MakeState(i, rng);
    gw.viewerState = rng() % 4 ? gw.states[0] : nullptr;
    void *censor = nullptr;
    void *const o = MakeObject(rng, censor);
    Run run{ctx, original, o};
    NoteObject(o);
    const unsigned steps = 4 + rng() % 24;
    for (unsigned i = 0; i < steps; ++i) {
        sgfx::MarkStep(ctx, "path step");
        PathStep(run, rng);
        NoteObject(o);
    }
    sgfx::MarkStep(ctx, "free");
    Free(o, censor);
    NoteFakes();
    counter.Stop();
    drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
}

// ---- the outlines and the overlay alone ----

// A block standing for the strategy, its words random but what the step reads.
void *MakeBlock(std::mt19937 &rng)
{
    void *const o = ::operator new(kSize);
    for (uint32_t i = 0; i < kSize; ++i) static_cast<uint8_t *>(o)[i] = uint8_t(rng());
    *Member<void *>(o, 0x10) = &gw.brush;
    *Member<void *>(o, 0x14) = &gw.outline;
    return o;
}

// Records for the outlines: their flags 0 or 1 (now and then any byte), frames around the strategy's.
void FillRecords(DynamicStrategyTable *t, uint32_t frame, std::mt19937 &rng)
{
    auto flag = [&] { return uint8_t(rng() % 9 == 0 ? rng() : rng() % 2); };
    t->mRecords.resize(rng() % 12);
    for (DynamicRectRecord &rec : t->mRecords) {
        for (uint32_t b = 0; b < sizeof rec; ++b) reinterpret_cast<uint8_t *>(&rec)[b] = uint8_t(rng());
        for (int32_t &v : rec.mRect) v = int32_t(rng() % 5 == 0 ? rng() : rng() % 900);
        rec.mFrame = rng() % 6 ? frame - rng() % 4 + 1 : rng();
        rec.mFlag1c = flag();
        rec.mFlag1d = flag();
        rec.mDrawn = rng() % 5 ? 1 : flag();
        rec.mFlag1f = flag();
        rec.mFlag20 = flag();
        rec.mSecond = flag();
    }
}
DynamicStrategyHelper *MakeHelper(uint32_t frame, std::mt19937 &rng)
{
    auto *const helper = new DynamicStrategyHelper;
    for (DynamicStrategyHelper::Map *map : {helper->mMap10, helper->mMap14})
        for (unsigned n = rng() % 5; n; --n) {
            DynamicWords24 &v = (*map)[rng() % 1000];
            for (uint32_t &w : v.w) w = rng() % 5 == 0 ? rng() : rng() % 900;
            v.w[4] = rng() % 3 ? frame - rng() % 2 : rng();
            v.w[5] = rng() % 5 == 0 ? rng() : rng() % 2 | (rng() & 0xffffff00u);
        }
    return helper;
}

void RunOutlines(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    using Fn = void(__thiscall *)(void *, void *, void *, void *, void *, const int32_t *, const int32_t *, uint32_t);
    difftest::AllocationCounter counter;
    for (unsigned i = 0; i < kStates; ++i) gw.states[i] = MakeState(i, rng);
    void *const o = MakeBlock(rng);
    static const uint32_t kModes[] = {0, 1, 1, 1, 2, 2, 2, 3};
    static const uint32_t kFrames[] = {0, 1, 2, 2, 3, 7, 0xffffffffu};
    Rects rects;
    for (unsigned step = 4 + rng() % 4; step; --step) {
        sgfx::MarkStep(ctx, "outlines");
        DynamicStrategyState *const state = gw.states[rng() % kStates];
        state->mMode = rng() % 4;
        state->mFrames = kFrames[rng() % std::size(kFrames)];
        const uint32_t frame = 1 + rng() % 8;
        *Member<uint32_t>(o, 0xd8) = frame;
        *Member<uint32_t>(o, 0xc0) = rng() % 9 == 0 ? rng() : kModes[rng() % std::size(kModes)];
        *Member<uint8_t>(o, 0x79) = rng() % 7 == 0 ? uint8_t(rng()) : uint8_t(rng() % 2);
        FillRecords(state->mTable, frame, rng);
        DynamicStrategyTable *const table = rng() % 5 ? state->mTable : nullptr;
        DynamicStrategyHelper *const helper = rng() % 3 ? MakeHelper(frame, rng) : nullptr;
        rects.clear();
        for (unsigned n = rng() % 4; n; --n) rects.push_back(MakeRect());
        int32_t bounds[4], pixels[4];
        for (int32_t &v : bounds) v = int32_t(rng() % 4 == 0 ? rng() : rng() % 900);
        for (int32_t &v : pixels) v = int32_t(rng() % 4 == 0 ? rng() : rng() % 900);
        const uint32_t second = rng() % 2;
        const uint32_t secondWord = second | (rng() & 0xffffff00u); // the pushed word's other bytes
        if (rng() % 3 == 0) *GameAddress<uint8_t>(kFlagsEnabledCell) = uint8_t(rng() % 2);
        if (rng() % 3 == 0) *GameAddress<uint32_t>(kFlags0Cell) = PickOutlineFlags(rng);
        if (rng() % 3 == 0) *GameAddress<uint32_t>(kFlags1Cell) = PickOutlineFlags(rng);
        drawfx::Note("outlines call", {state->mMode, state->mFrames, W(o, 0xc0), *Member<uint8_t>(o, 0x79),
                                       table != nullptr, helper != nullptr, second});
        if (original) ctx.Original<Fn>(0x371670)(o, state, table, helper, &rects, bounds, pixels, secondWord);
        else static_cast<Self *>(o)->DrawOutlines(state, table, helper, &rects, bounds, pixels, second != 0);
        NoteOutside();
        delete helper;
    }
    rects.clear();
    ::operator delete(o);
    for (DynamicStrategyState *&s : gw.states) {
        std::destroy_at(s->mTable);
        ::operator delete(s->mTable);
        ::operator delete(s);
        s = nullptr;
    }
    NoteFakes();
    counter.Stop();
    drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
}

void RunShowBuffer(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    using Fn = void(__thiscall *)(void *, nGZSceneGraph::Pass *, void *);
    difftest::AllocationCounter counter;
    void *const o = MakeBlock(rng);
    for (unsigned step = 4 + rng() % 6; step; --step) {
        sgfx::MarkStep(ctx, "show buffer");
        if (rng() % 2) PickSettings(rng);
        for (unsigned n = rng() % 3; n; --n) PickSurface(gw.surfaces[rng() % kSurfaces], rng);
        Fake *const backbuffer = &gw.surfaces[rng() % kSurfaces];
        Fake *const surface = rng() % 4 == 0 ? backbuffer : &gw.surfaces[rng() % kSurfaces];
        *Member<void *>(o, 0x134) = backbuffer;
        *Member<uint8_t>(o, 0xd2) = rng() % 3 == 0 ? uint8_t(1 + rng() % 255) : 0;
        for (unsigned i = 0; i < 4; ++i) Member<int32_t>(o, 0xdc)[i] = int32_t(rng() % 4 == 0 ? rng() : rng() % 900);
        gw.pass.mTextureCount = rng() % 5;
        for (cIGZUnknown *&t : gw.pass.mTextures) t = nullptr;
        gw.pass.mTextureRef94 = nullptr;
        drawfx::Note("show buffer call", {backbuffer->id, surface->id, uint32_t(*Member<uint8_t>(o, 0xd2))});
        if (original) ctx.Original<Fn>(0x376840)(o, &gw.pass, surface);
        else static_cast<Self *>(o)->DrawDebugOverlay(&gw.pass, reinterpret_cast<cITSRenderSurface *>(surface));
        NoteOutside();
    }
    ::operator delete(o);
    NoteFakes();
    counter.Stop();
    drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
}

// ---- the frame state's life ----

void NoteState(const DynamicStrategyState *s)
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = "state";
    c.words.push_back(s->mVTable == GameAddress<void>(DynamicStrategyState::kVTable));
    drawfx::Words(c.words, &s->mMode, 18, "u");
    drawfx::Words(c.words, &s->mTable, 1, "a");
    drawfx::Words(c.words, &s->mFrames, 1, "u");
    drawfx::Words(c.words, &s->mRects54, 3, "a");
    c.words.push_back(s->mRefCount);
    drawfx::gLog.push_back(std::move(c));
    if (s->mTable) NoteTable(s->mTable);
}

void RunStateLife(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    using CreateFn = DynamicStrategyState *(__cdecl *)();
    using DestroyFn = void(__thiscall *)(void *);
    difftest::AllocationCounter counter;
    DynamicStrategyState *made[3] = {};
    for (DynamicStrategyState *&s : made) {
        sgfx::MarkStep(ctx, "create");
        s = original ? ctx.Original<CreateFn>(0x3721f0)() : DynamicStrategyState::Create();
        NoteState(s);
    }
    for (DynamicStrategyState *s : made) {
        // Filled as the strategy fills them, then destroyed.
        DynamicStrategyTable *const t = s->mTable;
        for (unsigned n = rng() % 120; n; --n) t->mDirtyTurbo.push_back((rng() & 1) != 0);
        for (Words *v : {&t->mLastVisible, &t->mVisible})
            for (unsigned n = rng() % 5; n; --n) v->push_back(rng());
        for (unsigned n = rng() % 4; n; --n) t->mDirtyRects.push_back(MakeRect());
        if (rng() % 3 == 0) t->mRecords.resize(rng() % 1200);
        for (unsigned n = rng() % 4; n; --n) s->mRects54.push_back(MakeRect());
        s->mMode = rng() % 3;
        s->mFrames = rng() % 5;
        sgfx::MarkStep(ctx, "destroy");
        if (original) ctx.Original<DestroyFn>(0x36f5c0)(s);
        else s->~DynamicStrategyState();
        drawfx::Note("destroyed vtable", s->mVTable == GameAddress<void>(DynamicStrategyState::kVTable));
        ::operator delete(s);
    }
    counter.Stop();
    drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
}

// ---- the window callback and the window properties ----

// cGZWin (the callback's windows): 15 the child count, 37 the children walk, 82 a flag test, 83 a flag set, 126 a
// property get, 127 a property set.
constexpr At kUiWindowAt[] = {{15, {0, Ret::Int, true, "ChildCount", ""}},
                              {37, {3, Ret::Int, true, "Walk", "uW0W0"}},
                              {82, {1, Ret::Int, true, "FlagTest", "u"}},
                              {83, {2, Ret::Int, true, "FlagSet", "ub"}},
                              {126, {2, Ret::Int, true, "GetProperty", "uW0"}},
                              {127, {2, Ret::Int, true, "SetProperty", "ua"}}};
constexpr auto kUiWindowSlots = Table<128>(kUiWindowAt);
constexpr drawfx::Iface kUiWindow{"UiWindow", 128, kUiWindowSlots.data(), false, kDeclared};
// cIGZVariant: 26 GetValUint32 (the out word), 27 SetValUint32.
constexpr At kVariantAt[] = {{26, {1, Ret::Int, true, "GetValUint32", "W0"}}, {27, {1, Ret::Int, true, "SetValUint32", "u"}}};
constexpr auto kVariantSlots = Table<28>(kVariantAt);
constexpr drawfx::Iface kVariant{"Variant", 28, kVariantSlots.data(), false, kDeclared};

constexpr unsigned kWindows = 10, kMaxChildren = 3;
// Each window's property: none, present, or a failed get that still hands its variant (counted).
enum PropertyMode : uint32_t { kNoProperty, kProperty, kFailedLeft };
struct UiWorld {
    Fake windows[kWindows], variants[kWindows];
    uint32_t flags[kWindows];
    PropertyMode mode[kWindows];
    unsigned children[kWindows][kMaxChildren];
    unsigned childCount[kWindows];
    std::vector<cIGZVariant *> held[kWindows]; // the variants SetProperty was handed
};
UiWorld gu;

using CallbackFn = uint32_t(__cdecl *)(void *, uint32_t, void *, uint8_t *);
unsigned WindowIndex(const Fake &self) { return unsigned(&self - gu.windows); }
uint32_t Garbage() { return gw.Next() & 0xffffff00u; }

uint32_t UiChildCount(Fake &self, const uint32_t *) { return gu.childCount[WindowIndex(self)]; }
uint32_t UiWalk(Fake &self, const uint32_t *a)
{
    const unsigned i = WindowIndex(self);
    auto *const flag = reinterpret_cast<uint8_t *>(uintptr_t(a[2]));
    drawfx::Note("walk", {a[0], CallbackWord(a[1]), *flag});
    ThrowPoint("walk");
    const auto callback = reinterpret_cast<CallbackFn>(uintptr_t(a[1]));
    for (unsigned k = 0; k < gu.childCount[i]; ++k) {
        const uint32_t r = callback(&self, a[0], &gu.windows[gu.children[i][k]], flag);
        drawfx::Note("walk answer", r & 0xff);
        if (!(r & 0xff)) return Garbage();
    }
    // The callback answers false only when a walk does, which a tree of callbacks alone never makes (a leaf answers
    // true): the walk refuses by itself now and then.
    if (gw.Next() % 4 == 0) {
        drawfx::Note("walk refused", i);
        return Garbage();
    }
    return 1 | Garbage();
}
// A set flag answers any non-zero byte now and then (the callback's tests of it differ).
uint32_t UiFlagTest(Fake &self, const uint32_t *a)
{
    const bool set = (gu.flags[WindowIndex(self)] & a[0]) != 0;
    const uint32_t byte = !set ? 0 : gw.Next() % 4 == 0 ? 1 + gw.Next() % 255 : 1;
    return byte | Garbage();
}
uint32_t UiFlagSet(Fake &self, const uint32_t *a)
{
    uint32_t &flags = gu.flags[WindowIndex(self)];
    flags = a[1] & 0xff ? flags | a[0] : flags & ~a[0];
    return 0;
}
uint32_t UiGetProperty(Fake &self, const uint32_t *a)
{
    const unsigned i = WindowIndex(self);
    ThrowPoint("get property");
    const PropertyMode mode = a[0] == 0x300d422 ? gu.mode[i] : gw.Next() % 2 ? kProperty : kNoProperty;
    if (mode == kNoProperty) return Garbage();
    *reinterpret_cast<void **>(uintptr_t(a[1])) = &gu.variants[i];
    ++gu.variants[i].refs;
    return (mode == kProperty) | Garbage();
}
uint32_t UiSetProperty(Fake &self, const uint32_t *a)
{
    ThrowPoint("set property");
    auto *const variant = reinterpret_cast<cIGZVariant *>(uintptr_t(a[1]));
    uint32_t value = 0x5a5a5a5a;
    const bool got = variant->GetValUint32(&value);
    const uint32_t count = variant->AddRef();
    drawfx::Note("set property", {a[0], uint32_t(got), value, count});
    gu.held[WindowIndex(self)].push_back(variant);
    return 0;
}
uint32_t VariantGet(Fake &self, const uint32_t *a)
{
    ThrowPoint("variant get");
    *reinterpret_cast<uint32_t *>(uintptr_t(a[0])) = self.value[0];
    return (gw.Next() % 2) | Garbage();
}
uint32_t VariantSet(Fake &self, const uint32_t *a)
{
    ThrowPoint("variant set");
    self.value[0] = a[0];
    return 0;
}

void BuildUi(uint32_t seed, drawfx::Cells &cells)
{
    Build(seed, cells);
    std::mt19937 rng(seed ^ 0x3752d0);
    for (unsigned i = 0; i < kWindows; ++i) {
        gu.windows[i].Reset<kUiWindow>(950 + i);
        gu.windows[i].On(15, &UiChildCount);
        gu.windows[i].On(37, &UiWalk);
        gu.windows[i].On(82, &UiFlagTest);
        gu.windows[i].On(83, &UiFlagSet);
        gu.windows[i].On(126, &UiGetProperty);
        gu.windows[i].On(127, &UiSetProperty);
        gu.variants[i].Reset<kVariant>(970 + i);
        gu.variants[i].On(26, &VariantGet);
        gu.variants[i].On(27, &VariantSet);
        gu.variants[i].value[0] = rng() % 5 == 0 ? rng() : rng() % 32;
        gu.flags[i] = rng() % 6 == 0 ? rng() : rng() & 0x100025u;
        if (rng() % 3) gu.flags[i] |= 1; // visible
        const uint32_t m = rng() % 6;
        gu.mode[i] = m < 3 ? kProperty : m < 5 ? kNoProperty : kFailedLeft;
        gu.childCount[i] = 0;
        for (unsigned k = 0; k < kMaxChildren && i + 1 < kWindows; ++k)
            if (rng() % 2) gu.children[i][gu.childCount[i]++] = i + 1 + rng() % (kWindows - i - 1);
        gu.held[i].clear();
    }
}

void NoteUi()
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = "windows";
    for (unsigned i = 0; i < kWindows; ++i) {
        c.words.insert(c.words.end(), {gu.flags[i], uint32_t(gu.windows[i].refs), gu.variants[i].value[0],
                                       uint32_t(gu.variants[i].refs), uint32_t(gu.held[i].size())});
        for (cIGZVariant *v : gu.held[i]) {
            uint32_t value = 0x5a5a5a5a;
            c.words.push_back(v->GetValUint32(&value));
            c.words.push_back(value);
        }
    }
    drawfx::gLog.push_back(std::move(c));
}

void RunUi(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    using GetFn = uint32_t(__cdecl *)(void *, uint32_t);
    using SetFn = void(__cdecl *)(void *, uint32_t, uint32_t);
    difftest::AllocationCounter counter;
    auto id = [&] { return rng() % 4 ? 0x300d422u : rng(); };
    for (unsigned step = 3 + rng() % 8; step; --step) {
        sgfx::MarkStep(ctx, "ui step");
        const unsigned what = rng() % 5;
        Fake *const window = &gu.windows[rng() % 3 == 0 ? 0 : rng() % kWindows];
        void *const w = window;
        gw.throwIn = rng() % 6 == 0 ? 1 + rng() % 12 : 0;
        drawfx::Note("ui op", {what, window->id});
        try {
            if (what < 3) {
                uint8_t flag = rng() % 5 == 0 ? uint8_t(rng()) : uint8_t(rng() % 2);
                const uint32_t unused = rng(); // the original's second argument (unused: ours gets 0)
                const uint32_t r = original ? ctx.Original<CallbackFn>(0x3752d0)(&gu.windows[0], unused, w, &flag)
                                            : Self::OpaqueUICallback(nullptr, 0, static_cast<cIGZUnknown *>(w), &flag);
                drawfx::Note("callback", {r & 0xff, flag});
            } else if (what == 3) {
                const uint32_t key = id();
                const uint32_t r = original ? ctx.Original<GetFn>(0x372670)(w, key)
                                            : WindowPropertyUint32(static_cast<cIGZUnknown *>(w), key);
                drawfx::Note("property", r);
            } else {
                const uint32_t key = id(), value = rng() % 3 ? rng() % 64 : rng();
                if (original) ctx.Original<SetFn>(0x375660)(w, key, value);
                else SetWindowPropertyUint32(static_cast<cIGZUnknown *>(w), key, value);
            }
        } catch (int e) {
            drawfx::Note("threw", uint32_t(e));
        }
        gw.throwIn = 0;
        NoteUi();
    }
    for (std::vector<cIGZVariant *> &held : gu.held) {
        for (cIGZVariant *v : held) drawfx::Note("released", v->Release());
        std::vector<cIGZVariant *>().swap(held); // its block freed too
    }
    counter.Stop();
    drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
}

} // namespace

// Verifies: Render 0x374930 (+8 slot 4), Clear 0x373920 (+8 slot 5), SetUpTarget 0x374dd0 (primary slot 6), Prepare
// 0x371b10, Draw 0x373e20, Finish 0x376460, DisableScissor 0x3714b0, MarkOpaqueUI 0x375240, MatricesDiffer 0x36d3c0
// (inside Render), the helper's FrameStep 0x3755e0 and Reset 0x370fc0, the table's FrameStep 0x375590 and Reset 0x370f90.
DIFF_TEST(SG_DynamicStrategyDraw, 0x374930)
{
    svcfx::AllocatorScope allocator;
    heappoison::Scope poison(ctx, 0xc5);
    Stands stands;
    {
        // One original run first, its log dropped: the original's first run keeps one 0x20-byte block for good (a
        // one-time initialisation), which no later run repeats.
        drawfx::Cells warm;
        drawfx::Side([&] { Build(0, warm); }, [&] {
            std::mt19937 rng(13);
            RunDraw(ctx, true, rng);
        });
    }
    sgfx::CheckSides(ctx, "dynamic strategy draw", 1500, [&](uint32_t seed, drawfx::Cells &cells) { Build(seed, cells); },
                     [&](bool original, std::mt19937 &rng) {
                         RunDraw(ctx, original, rng);
                         ++ctx.cases;
                     });
}

// Verifies: the drawing paths (row TS1i): DrawPath 0x374670, DrawOwnPath 0x3743e0, DrawSide 0x374bb0, ListItems
// 0x373a70 with the table's Record 0x372150, PresentRegistered 0x371150, CopyToSecond 0x371240, Present 0x373f00 (each
// called directly; SG_DynamicStrategyDraw reaches them through Draw 0x373e20).
DIFF_TEST(SG_DynamicDrawPaths, 0x374670)
{
    svcfx::AllocatorScope allocator;
    heappoison::Scope poison(ctx, 0xc5);
    Stands stands;
    {
        // One original run first, its log dropped (the original's one-time blocks, as SG_DynamicStrategyDraw's).
        drawfx::Cells warm;
        drawfx::Side([&] { Build(0, warm); }, [&] {
            std::mt19937 rng(13);
            RunPaths(ctx, true, rng);
        });
    }
    sgfx::CheckSides(ctx, "dynamic strategy paths", 2000, [&](uint32_t seed, drawfx::Cells &cells) { Build(seed, cells); },
                     [&](bool original, std::mt19937 &rng) {
                         RunPaths(ctx, original, rng);
                         ++ctx.cases;
                     });
}

// Verifies: DrawOutlines 0x371670 and its drawer: Draw 0x3717e0, Records 0x3714e0, Record 0x371520, Rect 0x371600,
// Kind 0x3718f0, Map 0x371930, MapValue 0x3715b0, Colour 0x3753c0, and the drawer set-up SetDraw 0x375620 and SetRects
// 0x375550 (inlined in ours).
DIFF_TEST(SG_DynamicOutlines, 0x371670)
{
    svcfx::AllocatorScope allocator;
    heappoison::Scope poison(ctx, 0xc5);
    Stands stands;
    {
        // One original run first, its log dropped: the original's first run keeps one 0x18-byte block for good.
        drawfx::Cells warm;
        drawfx::Side([&] { Build(0, warm); }, [&] {
            std::mt19937 rng(13);
            RunOutlines(ctx, true, rng);
        });
    }
    sgfx::CheckSides(ctx, "dynamic strategy outlines", 1500, [&](uint32_t seed, drawfx::Cells &cells) { Build(seed, cells); },
                     [&](bool original, std::mt19937 &rng) {
                         RunOutlines(ctx, original, rng);
                         ++ctx.cases;
                     });
}

// Verifies: DrawDebugOverlay 0x376840 and RestoreOverlayStates 0x374ce0.
DIFF_TEST(SG_DynamicShowBuffer, 0x376840)
{
    svcfx::AllocatorScope allocator;
    heappoison::Scope poison(ctx, 0xc5);
    Stands stands;
    sgfx::CheckSides(ctx, "dynamic strategy debug buffer", 1500, [&](uint32_t seed, drawfx::Cells &cells) { Build(seed, cells); },
                     [&](bool original, std::mt19937 &rng) {
                         RunShowBuffer(ctx, original, rng);
                         ++ctx.cases;
                     });
}

// Verifies: OpaqueUICallback 0x3752d0, WindowPropertyUint32 0x372670, SetWindowPropertyUint32 0x375660.
DIFF_TEST(SG_DynamicOpaqueUICallback, 0x3752d0)
{
    svcfx::AllocatorScope allocator;
    heappoison::Scope poison(ctx, 0xc5);
    Stands stands;
    sgfx::CheckSides(ctx, "dynamic strategy opaque UI", 2000, [&](uint32_t seed, drawfx::Cells &cells) { BuildUi(seed, cells); },
                     [&](bool original, std::mt19937 &rng) {
                         RunUi(ctx, original, rng);
                         ++ctx.cases;
                     });
}

// Verifies: the frame state's factory 0x3721f0 and constructor 0x36e790, the table's constructor 0x36e8b0, the state's
// destructor 0x36f5c0 and the table's destructor 0x36f640 (0x36e960 inside).
DIFF_TEST(SG_DynamicStateLife, 0x3721f0)
{
    svcfx::AllocatorScope allocator;
    heappoison::Scope poison(ctx, 0xc5);
    Stands stands;
    sgfx::CheckSides(ctx, "dynamic strategy frame state", 400, [&](uint32_t seed, drawfx::Cells &cells) { Build(seed, cells); },
                     [&](bool original, std::mt19937 &rng) {
                         RunStateLife(ctx, original, rng);
                         ++ctx.cases;
                     });
}

// Verifies: MatricesDiffer 0x36d3c0 (the low byte of its result).
DIFF_TEST(SG_DynamicMatricesDiffer, 0x36d3c0)
{
    using Fn = uint8_t(__cdecl *)(const uint32_t *, const uint32_t *);
    const Fn original = ctx.Original<Fn>(0x36d3c0);
    std::mt19937 rng(0x36d3c0);
    static const uint32_t kSpecial[] = {0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u, 0x0000001fu, 0x80000020u,
                                        0x007fffffu, 0x807fffffu, 0x7f800000u, 0xff800000u, 0x7fc00000u, 0xffc00000u,
                                        0x7f7fffffu, 0xff7fffffu, 0x3f800000u, 0xbf800000u};
    static const int32_t kSteps[] = {0, 1, -1, 30, -30, 31, -31, 32, -32, 33, -33, 63, -64, 0x7fffffff, -0x7fffffff - 1};
    unsigned differ = 0, same = 0;
    for (unsigned i = 0; i < 400000 && ctx.failures.size() < 8; ++i) {
        uint32_t a[16], b[16];
        for (unsigned k = 0; k < 16; ++k) {
            const unsigned how = rng() % 8;
            a[k] = how == 0 ? kSpecial[rng() % std::size(kSpecial)]
                   : how == 1 ? rng()
                              : std::bit_cast<uint32_t>(float(int32_t(rng() % 64) - 32) / 4.0f);
            b[k] = a[k];
        }
        for (unsigned n = rng() % 3; n; --n) {
            const unsigned k = rng() % 16;
            const unsigned how = rng() % 4;
            b[k] = how == 0 ? kSpecial[rng() % std::size(kSpecial)]
                   : how == 1 ? b[k] ^ 0x80000000u
                              : uint32_t(int32_t(b[k]) + kSteps[rng() % std::size(kSteps)]);
        }
        if (rng() % 2) std::swap(a, b);
        const uint8_t want = original(a, b) & 0xff;
        const uint8_t got = uint8_t(MatricesDiffer(a, b));
        ++ctx.cases;
        (want ? differ : same)++;
        if (want != got) {
            unsigned k = 0;
            while (k < 15 && a[k] == b[k]) ++k;
            ctx.Fail("case %u: original %u, ours %u (first differing element %u: %08x / %08x)", i, want, got, k, a[k], b[k]);
        }
    }
    if (differ < 1000 || same < 1000) ctx.Fail("coverage: %u differing, %u close", differ, same);
}
