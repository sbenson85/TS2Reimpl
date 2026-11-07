// Differential tests for cDeviceDX::CreateDevice (0x167ec0, src/gz/cDeviceDX.cpp, lane NF, rendering campaign R1b):
// called directly, and through its three callers (ApplySettings, OnEvent's resize and canvas paths), against the
// shared fake Direct3D (tests/d3dfx.h) and fakes of the enumeration, its iterators, the pixel formats, the window owner
// service and the COM director.
//
// One world per case, built alike for each side from the case's seed (the shape of tests/gz_device_dx_class.cpp):
// - The device is a static block: the original's side gets the game's vtable word, ours ours; every other byte is the
//   case's. The original is called at its RVAs, ours directly (CreateDevice) or through our vtable.
// - The enumeration (cDeviceEnumDX, still game code) is a fake answering its slots from the case: the adapter
//   identifier and desktop mode (hidden-pointer results), the mode list and the colour / depth format lists (ranges of
//   iterator fakes that clone, compare, give and step by a position word), the format questions and the capability and
//   feature tables. The pixel formats are fakes with an id, bits, stencil bits and their ARGB / depth-stencil faces.
// - Game code CreateDevice still calls is stood in on both sides by recorders that answer from the case: the mode
//   search 0x18b490, the depth search 0x18ab60, the multisample clamp 0x18b7d0 (each releasing the range it is given,
//   as the originals do), the enumeration refresh 0x178b80 (the device type), the manager's 0x17b710 / 0x17bb30 /
//   0x17bb00 / 0x17bb80, the state's set-up 0x1807b0 and its members (Flush, SetBackBufferTarget, MarkTargetsDirty,
//   ReleaseDeviceObjects), the pixel formats getter 0x18acd0 and the director's framework getter.
// - IDirect3D9::CreateDevice and the device's Reset log their present parameters and answer from per-call sequences;
//   the device's Create* slots hand out a new query, sync surface and probe texture; GetBackBuffer and
//   GetDepthStencilSurface always give a surface (both sides release what they get unchecked).
// - Events: our real cConnectionManager in the device's cell, a listener fake connected to the device's events.
// Compared: the log (every call on every fake with its arguments, the stand-ins' notes, the present parameters), the
// result, every byte of the device object but its vtable word, the state and manager blocks, the old sync surface's
// memory, the sync clear colour, an escaped cError, allocations, and every fake's reference count.
#include "code_patch.h"
#include "d3dfx.h"
#include "difftest.h"
#include "gz_registry_fx.h"
#include "gz/GraphicModeSearch.h"
#include "gz/GraphicServices.h"
#include "impfx.h"

#include "app/WinMain.h"
#include "game/vtable.h"
#include "gz/cConnectionManager.h"
#include "gz/cDeviceDX.h"
#include "gz/cDeviceEnum.h"
#include "gz/cDeviceEnumDX.h"
#include "gz/cDeviceResourceManagerDX.h"
#include "gz/cDeviceStateDX.h"
#include "rz/StaticGetters.h"

#include <bit>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace nGZGraphic4;
using nGZEvent::cConnectionManager;
using nGZEvent::cConnectionManagerAdapters;
using nGZEvent::cEventAttributeMap;

namespace {

constexpr uint32_t kVTableRva = 0xd9e478;
constexpr uint32_t kEventManagerCell = 0x10ca22c;
constexpr uint32_t kSyncClearColor = 0x10ca21c;
constexpr uint32_t kDefaultModeRva = 0x10ca208;
constexpr uint32_t kRegistryMap = 0x10ca220, kRegistryMapGuard = 0x10ca228, kRegistryOnce = 0x10ca230,
                   kRegistryHolderGuard = 0x10ca238;
constexpr uint32_t kSwapChainClass = 0x5b75b757, kTargetProviderClass = 0x7b75b73a;
constexpr uint32_t kArgbIid = 0x9ac38991, kDepthStencilIid = 0x5ac389b6;

constexpr uint32_t kObjectTag = 0x7e000000, kStateTag = 0x57000000, kManagerBlockTag = 0x5e000000,
                   kDataTag = 0x7c000000, kConnTag = 0x7b000000, kSyncTag = 0x7a000000;

// ---- interfaces ----
constexpr drawfx::Slot U{0, drawfx::Ret::Int, false, "", ""};
// The engine objects (one slot number means one thing across them): 3 the COM director's GetClassObject; 4 a swap
// chain's (or the target provider's) surface; 5 their set-up with the device and a Direct3D surface; 9 the state's
// Reset; 18 / 22 / 26 / 37 / 38 / 39 / 40 the window owner service's; 20 / 21 a surface's size.
inline constexpr drawfx::Slot kEngineSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    {3, drawfx::Ret::Int, true, "GetClassObject", "uui"},
    {0, drawfx::Ret::Int, true, "GetSurface", ""},
    {2, drawfx::Ret::Int, true, "SetSurface", "ii"},
    U, U, U,
    {1, drawfx::Ret::Int, true, "Reset", "u"},
    U, U, U, U, U, U, U, U,
    {2, drawfx::Ret::Int, true, "Resize", "uu"},
    U,
    {0, drawfx::Ret::Int, true, "GetWidth", ""},
    {0, drawfx::Ret::Int, true, "GetHeight", ""},
    {1, drawfx::Ret::Int, true, "Slot22", "u"},
    U, U, U,
    {0, drawfx::Ret::Int, true, "Slot26", ""},
    U, U, U, U, U, U, U, U, U, U,
    {1, drawfx::Ret::Int, true, "Slot37", "u"},
    {1, drawfx::Ret::Int, true, "Slot38", "u"},
    {0, drawfx::Ret::Int, true, "AskFallback", ""},
    {0, drawfx::Ret::Int, true, "GetWindow", ""},
};
inline constexpr drawfx::Iface kEngine{"engine", 41, kEngineSlots};

// The graphic service: slot 25(device type) the IDirect3D9; 30 LogD3DError's text query.
inline constexpr drawfx::Slot kGraphicSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    {1, drawfx::Ret::Int, true, "GetDirect3D", "u"},
    U, U, U, U,
    {1, drawfx::Ret::Int, true, "Slot30", "u"},
};
inline constexpr drawfx::Iface kGraphicService{"graphic service", 31, kGraphicSlots};

inline constexpr drawfx::Slot kFormatsSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    U,
    {1, drawfx::Ret::Int, true, "GetFormat", "u"},
    {0, drawfx::Ret::Int, true, "GetDesktopFormat", ""},
};
inline constexpr drawfx::Iface kFormats{"cIPixelFormatsManager", 6, kFormatsSlots};

// cIPixelFormat (slots 3-6, src/gz/cDeviceEnum.h) with cIPixelFormatDepthStencil's slot 13.
inline constexpr drawfx::Slot kPixelFormatSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    {0, drawfx::Ret::Int, true, "GetFormatId", ""},
    U, U,
    {0, drawfx::Ret::Int, true, "GetBitsPerPixel", ""},
    U, U, U, U, U, U,
    {0, drawfx::Ret::Int, true, "GetStencilBits", ""},
};
inline constexpr drawfx::Iface kPixelFormat{"cIPixelFormat", 14, kPixelFormatSlots};

// cIDeviceEnum (src/gz/cDeviceEnum.h): the slots CreateDevice calls; the others unsettled (a call fails the test).
// Struct results come back through a hidden pointer, the first argument.
inline constexpr drawfx::Slot kEnumSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    U, U, U,
    {1, drawfx::Ret::Int, true, "GetAdapterDisplayMode", "i"},
    {2, drawfx::Ret::Int, true, "GetDisplayModes", "iu"},
    U, U,
    {4, drawfx::Ret::Int, true, "GetColorFormats", "iiiu"},
    U,
    {3, drawfx::Ret::Int, true, "IsColorFormatSupported", "iui"},
    U,
    {4, drawfx::Ret::Int, true, "GetDepthFormats", "iuii"},
    U,
    {4, drawfx::Ret::Int, true, "IsDepthFormatSupported", "iuii"},
    U, U, U,
    {1, drawfx::Ret::Int, true, "HasProcessingCap", "u"},
    U, U, U, U,
    {1, drawfx::Ret::Int, true, "HasFeature", "u"},
    U,
    {1, drawfx::Ret::Int, true, "GetAdapterIdentifier", "i"},
};
inline constexpr drawfx::Iface kEnum{"cIDeviceEnum", 28, kEnumSlots};

// The iterators (display modes: Get through a hidden pointer; pixel formats: Get in eax).
inline constexpr drawfx::Slot kModeIteratorSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    {0, drawfx::Ret::Int, true, "Clone", ""},
    {1, drawfx::Ret::Int, true, "IsEqual", "i"},
    {1, drawfx::Ret::Int, true, "GetMode", "i"},
    {0, drawfx::Ret::Int, true, "Next", ""},
};
inline constexpr drawfx::Iface kModeIterator{"cIDisplayModeIterator", 7, kModeIteratorSlots};
inline constexpr drawfx::Slot kFormatIteratorSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    {0, drawfx::Ret::Int, true, "Clone", ""},
    {1, drawfx::Ret::Int, true, "IsEqual", "i"},
    {0, drawfx::Ret::Int, true, "GetFormat", ""},
    {0, drawfx::Ret::Int, true, "Next", ""},
};
inline constexpr drawfx::Iface kFormatIterator{"cIPixelFormatIterator", 7, kFormatIteratorSlots};

inline constexpr drawfx::Slot kBehaviorSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"},
    {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},
    {4, drawfx::Ret::Int, true, "OnEvent", "uuii"},
    {3, drawfx::Ret::Int, true, "OnQuery", "uui"},
};
inline constexpr drawfx::Iface kBehavior{"cIAbstractBehavior", 5, kBehaviorSlots};

// ---- fakes ----
constexpr unsigned kFormatCount = 6, kChains = 3, kPool = 40, kListeners = 2;
enum : uint32_t {
    kEnumId = 0xe0001, kWindowId, kGraphicId, kComId, kStateFakeId, kManagerFakeId, kFormatsId, kProviderId,
    kNewProviderId, kSurfaceObjectId, kListenerId0, kListenerId1,
    kChainId0 = 0xe0100, kNewChainId0 = 0xe0110, kFormatId0 = 0xe0200, kModeItId0 = 0xe1000, kFormatItId0 = 0xe2000,
    kD3DId = 0xd3d00010, kQueryId, kSyncId, kProbeId, kNewQueryId, kNewSyncId, kNewProbeId, kDepthSurfaceId,
    kBackBufferId0 = 0xd3d00100
};

drawfx::Fake gDevice, gD3D, gQuery, gSync, gProbe, gNewQuery, gNewSync, gNewProbe, gDepthSurface, gBackBuffer[kChains];
drawfx::Fake gEnum, gWindow, gGraphic, gCom, gStateFake, gManagerFake, gFormatsFake, gProvider, gNewProvider,
    gSurfaceObject, gChain[kChains], gNewChain[kChains], gFormat[kFormatCount], gModeIt[kPool], gFormatIt[kPool],
    gListener[kListeners];
d3dfx::SurfaceData gSyncData, gNewSyncData;
d3dfx::QueryData gQueryData, gNewQueryData;
std::optional<d3dfx::Creates> gCreates;
unsigned gModeItNext, gFormatItNext;

// ---- the blocks ----
alignas(16) uint8_t gObject[cDeviceDX::kSize];
constexpr size_t kStateBytes = 0xb00;
alignas(16) uint8_t gStateBlock[kStateBytes];
alignas(16) uint8_t gManagerBlock[cDeviceResourceManagerDX::kSize];
struct Data {
    cDeviceSettings settings;
    uint32_t message[8];
} gData;

cDeviceDX *Device() { return reinterpret_cast<cDeviceDX *>(gObject); }
cIDeviceDX *Face() { return Device(); }
uint32_t Word(const void *p) { return uint32_t(uintptr_t(p)); }
uint32_t Norm(const void *p) { return drawfx::Norm(Word(p)); }
template <class T> void Put(void *block, size_t offset, T value) { std::memcpy(static_cast<uint8_t *>(block) + offset, &value, sizeof(T)); }
// The device's flag bytes (+0x70 fullscreen, lost, failed, released, +0x74, blocked) as callees and listeners see
// them mid-call: the end-state compare alone misses a flag that is set and cleared again within one call.
void NoteFlags()
{
    uint32_t lo;
    uint16_t hi;
    std::memcpy(&lo, gObject + 0x70, 4);
    std::memcpy(&hi, gObject + 0x74, 2);
    drawfx::Note("flags", {lo, uint32_t(hi)});
}

// ---- a case ----
enum Op : uint32_t { kCreate, kApply, kResize, kCanvas, kOpCount };
const char *kOpNames[kOpCount] = {"CreateDevice", "ApplySettings", "OnEvent(resize)", "OnEvent(canvas)"};

struct Case {
    uint32_t op;
    uint8_t object[cDeviceDX::kSize];
    uint8_t state[kStateBytes];
    uint8_t manager[cDeviceResourceManagerDX::kSize];
    Data data;
    d3dfx::Answers device;
    uint32_t args[4];
    cDisplayMode defaultMode;   // the process default (0x10ca208)
    cDisplayMode desktopMode;   // the enumeration's slot 6
    unsigned modeCount;
    cDisplayMode modes[4];      // the mode list
    bool closest[3];            // 0x18b490's answers in turn
    cDisplayMode closestMode[3];
    uint32_t formatId[kFormatCount];
    int32_t formatBits[kFormatCount];
    uint32_t formatStencil[kFormatCount];
    bool formatArgb[kFormatCount];
    unsigned desktopFormat;
    bool colorSupported, depthSupported;
    unsigned colorCount, depthCount;
    uint8_t colorList[4], depthList[4];
    int32_t depthFound[4];      // 0x18ab60's answers in turn (a format index, or -1: none)
    uint32_t deviceType;        // 0x178b80's answer
    uint32_t caps, features;    // the enumeration's tables (bit i: index i)
    uint32_t msType, msQuality; // what 0x18b7d0 writes
    uint32_t fallback;          // the window's slot 39 answers (bit i: call i)
    uint32_t window;            // its slot 40 (the HWND)
    uint32_t resetHrs[8], createHrs[8];
    uint32_t validateHr, presentHr, coop, queryHr;
    bool queueQuery, queueSync, queueProbe;
    uint32_t queryBusy;
    uint32_t connections;
};

// ---- stand-ins ----
struct Script {
    const Case *c = nullptr;
    unsigned closest = 0, depth = 0, resets = 0, creates = 0, fallbacks = 0, chains = 0;
} gScript;

void *__cdecl GameNew(size_t size) { return ::operator new(size); }
void __cdecl GameDelete(void *p) { ::operator delete(p); }

// A range a stand-in is given: noted (each end and the cursor's position), then released end, begin, cursor (0x146670).
template <class Range> void TakeRange(const char *label, const Range &r)
{
    const auto *cursor = reinterpret_cast<const drawfx::Fake *>(r.mCurrent);
    drawfx::Note(label, {Norm(r.mCurrent), cursor ? cursor->value[0] : ~0u, Norm(r.mBegin), Norm(r.mEnd)});
    r.mEnd->Release();
    r.mBegin->Release();
    r.mCurrent->Release();
}

bool __cdecl StandClosestMode(cDisplayMode *mode, cDisplayModeRange modes, uint32_t width, uint32_t height,
                              uint32_t minBits, uint32_t maxBits, uint32_t minRefresh, uint32_t maxRefresh)
{
    const unsigned n = gScript.closest++;
    drawfx::Note("closest", {n, width, height, minBits, maxBits, minRefresh, maxRefresh, mode->mWidth, mode->mHeight,
                             std::bit_cast<uint32_t>(mode->mAspect), mode->mRefresh, Norm(mode->mFormat)});
    TakeRange("closest.range", modes);
    const bool found = n < 3 && gScript.c->closest[n];
    if (found) *mode = gScript.c->closestMode[n];
    return found;
}
cIPixelFormat *__cdecl StandDepthFormat(cPixelFormatRange formats, uint32_t minDepth, uint32_t minStencil, uint32_t floatDepth)
{
    const unsigned n = gScript.depth++;
    drawfx::Note("depth search", {n, minDepth, minStencil, floatDepth & 0xff});
    TakeRange("depth.range", formats);
    const int32_t found = n < 4 ? gScript.c->depthFound[n] : -1;
    return found < 0 ? nullptr : reinterpret_cast<cIPixelFormat *>(&gFormat[found]);
}
void __cdecl StandMultisample(uint32_t *type, uint32_t *quality, cIDeviceEnum *devices, cIPixelFormat *displayFormat,
                              cIPixelFormat *color)
{
    drawfx::Note("multisample", {*type, *quality, Norm(devices), Norm(displayFormat), Norm(color)});
    *type = gScript.c->msType;
    *quality = gScript.c->msQuality;
}
uint32_t __fastcall StandRefresh(void *self, void *, void *device, uint32_t flags)
{
    drawfx::Note("enum.refresh", {Norm(self), Norm(device), flags});
    return gScript.c->deviceType;
}
void __fastcall StandManager710(void *self, void *)
{
    drawfx::Note("manager.0x17b710", Norm(self));
    NoteFlags();
}
// 0x17bb30 reads neither of its two arguments (it pops them: ret 8), so ours takes none and neither stand-in logs them:
// the image's, called with the arguments, pops them; ours is called with none.
void __fastcall StandManagerTakeImage(void *self, void *, void *, uint32_t)
{
    drawfx::Note("manager.0x17bb30", Norm(self));
    NoteFlags();
}
void __fastcall StandManagerTake(void *self, void *)
{
    drawfx::Note("manager.0x17bb30", Norm(self));
    NoteFlags();
}
void __fastcall StandManagerMemory(void *self, void *, void *device) { drawfx::Note("manager.0x17bb00", {Norm(self), Norm(device)}); }
void __fastcall StandReleaseAll(void *self, void *)
{
    drawfx::Note("manager.ReleaseAll", Norm(self));
    NoteFlags();
}
void __fastcall StandStateSetUp(void *self, void *, void *device)
{
    drawfx::Note("state.0x1807b0", {Norm(self), Norm(device)});
    NoteFlags();
}
void *__cdecl StandFormats() { return &gFormatsFake; }
void *__cdecl StandFramework() { return &gCom; }
void __fastcall StandFlush(void *self, void *, uint32_t groups) { drawfx::Note("state.Flush", {Norm(self), groups}); }
void __fastcall StandMarkTargets(void *self, void *) { drawfx::Note("state.MarkTargetsDirty", Norm(self)); }
void __fastcall StandSetTarget(void *self, void *, void *surface, void *depth, uint32_t width, uint32_t height)
{
    drawfx::Note("state.SetBackBufferTarget", {Norm(self), Norm(surface), Norm(depth), width, height});
}
void __fastcall StandReleaseObjects(void *self, void *)
{
    drawfx::Note("state.ReleaseDeviceObjects", Norm(self));
    NoteFlags();
}

// ---- handlers ----
drawfx::Fake &FakeAt(uint32_t word) { return *reinterpret_cast<drawfx::Fake *>(uintptr_t(word)); }

drawfx::Fake &NextModeIterator(uint32_t position)
{
    drawfx::Fake &it = gModeIt[gModeItNext < kPool ? gModeItNext++ : kPool - 1];
    it.value[0] = position;
    return it;
}
drawfx::Fake &NextFormatIterator(uint32_t list, uint32_t position)
{
    drawfx::Fake &it = gFormatIt[gFormatItNext < kPool ? gFormatItNext++ : kPool - 1];
    it.value[0] = position;
    it.value[1] = list;
    return it;
}
uint32_t ModeClone(drawfx::Fake &self, const uint32_t *) { return Word(&NextModeIterator(self.value[0])); }
uint32_t FormatClone(drawfx::Fake &self, const uint32_t *) { return Word(&NextFormatIterator(self.value[1], self.value[0])); }
uint32_t IteratorIsEqual(drawfx::Fake &self, const uint32_t *a) { return a[0] && self.value[0] == FakeAt(a[0]).value[0]; }
uint32_t IteratorNext(drawfx::Fake &self, const uint32_t *) { return ++self.value[0]; }
uint32_t ModeGet(drawfx::Fake &self, const uint32_t *a)
{
    const Case &c = *gScript.c;
    cDisplayMode mode{};
    if (self.value[0] < c.modeCount) mode = c.modes[self.value[0]];
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), &mode, sizeof(mode));
    return a[0];
}
uint32_t FormatGet(drawfx::Fake &self, const uint32_t *)
{
    const Case &c = *gScript.c;
    const unsigned count = self.value[1] ? c.depthCount : c.colorCount;
    const uint8_t *list = self.value[1] ? c.depthList : c.colorList;
    return self.value[0] < count ? Word(&gFormat[list[self.value[0]]]) : 0u;
}

// The enumeration's ranges and copied-out results (through the hidden pointer, a[0]).
template <class Range> void WriteRange(uint32_t out, drawfx::Fake &cursor, drawfx::Fake &begin, drawfx::Fake &end)
{
    Range r;
    r.mCurrent = reinterpret_cast<decltype(r.mCurrent)>(&cursor);
    r.mBegin = reinterpret_cast<decltype(r.mBegin)>(&begin);
    r.mEnd = reinterpret_cast<decltype(r.mEnd)>(&end);
    std::memcpy(reinterpret_cast<void *>(uintptr_t(out)), &r, sizeof(r));
}
uint32_t EnumModes(drawfx::Fake &, const uint32_t *a)
{
    drawfx::Fake &cursor = NextModeIterator(0);
    drawfx::Fake &begin = NextModeIterator(0);
    drawfx::Fake &end = NextModeIterator(gScript.c->modeCount);
    WriteRange<cDisplayModeRange>(a[0], cursor, begin, end);
    return a[0];
}
uint32_t EnumFormats(uint32_t out, uint32_t list)
{
    const unsigned count = list ? gScript.c->depthCount : gScript.c->colorCount;
    drawfx::Fake &cursor = NextFormatIterator(list, 0);
    drawfx::Fake &begin = NextFormatIterator(list, 0);
    drawfx::Fake &end = NextFormatIterator(list, count);
    WriteRange<cPixelFormatRange>(out, cursor, begin, end);
    return out;
}
uint32_t EnumColorFormats(drawfx::Fake &, const uint32_t *a) { return EnumFormats(a[0], 0); }
uint32_t EnumDepthFormats(drawfx::Fake &, const uint32_t *a) { return EnumFormats(a[0], 1); }
uint32_t EnumDesktopMode(drawfx::Fake &, const uint32_t *a)
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), &gScript.c->desktopMode, sizeof(cDisplayMode));
    return a[0];
}
uint32_t EnumIdentifier(drawfx::Fake &, const uint32_t *a)
{
    std::memset(reinterpret_cast<void *>(uintptr_t(a[0])), 0x3c, sizeof(D3DADAPTER_IDENTIFIER9));
    return a[0];
}
uint32_t EnumCap(drawfx::Fake &, const uint32_t *a) { return a[0] < 32 && (gScript.c->caps >> a[0]) & 1; }
uint32_t EnumFeature(drawfx::Fake &, const uint32_t *a) { return a[0] < 32 && (gScript.c->features >> a[0]) & 1; }

uint32_t FormatsDesktop(drawfx::Fake &, const uint32_t *) { return Word(&gFormat[gScript.c->desktopFormat]); }
uint32_t GraphicDirect3D(drawfx::Fake &, const uint32_t *) { return Word(&gD3D); }
uint32_t WindowFallback(drawfx::Fake &, const uint32_t *)
{
    const unsigned n = gScript.fallbacks++;
    return n < 32 && (gScript.c->fallback >> n) & 1;
}
// The director's GetClassObject: the swap chains in turn, the target provider.
uint32_t ComGetClassObject(drawfx::Fake &, const uint32_t *a)
{
    drawfx::Fake *made = a[0] == kSwapChainClass        ? &gNewChain[gScript.chains++ % kChains]
                         : a[0] == kTargetProviderClass ? &gNewProvider
                                                        : nullptr;
    if (!made) return 0;
    ++made->refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = made;
    return 1;
}

constexpr unsigned kPresentWords = sizeof(D3DPRESENT_PARAMETERS) / 4;
void NotePresent(const char *label, uint32_t pp)
{
    uint32_t w[kPresentWords];
    std::memcpy(w, reinterpret_cast<const void *>(uintptr_t(pp)), sizeof(w));
    drawfx::Note(label, {w[0], w[1], w[2], w[3], w[4], w[5], w[6], Norm(reinterpret_cast<void *>(uintptr_t(w[7]))),
                         w[8], w[9], w[10], w[11], w[12], w[13]});
}
// Coverage labels for what the parameters ask (derived from logged words: the same on both sides when the logs agree).
void NoteKinds(uint32_t behavior, uint32_t pp)
{
    D3DPRESENT_PARAMETERS p;
    std::memcpy(&p, reinterpret_cast<const void *>(uintptr_t(pp)), sizeof(p));
    drawfx::Note(behavior & D3DCREATE_SOFTWARE_VERTEXPROCESSING ? "vp.software"
                 : behavior & D3DCREATE_MIXED_VERTEXPROCESSING  ? "vp.mixed"
                 : behavior & D3DCREATE_PUREDEVICE              ? "vp.pure"
                                                                : "vp.hardware",
                 0u);
    if (behavior & 0x100) drawfx::Note("no driver management", 0u);
    drawfx::Note(p.SwapEffect == D3DSWAPEFFECT_FLIP ? "swap.flip" : p.SwapEffect == D3DSWAPEFFECT_COPY ? "swap.copy" : "swap.discard", 0u);
    drawfx::Note(p.Windowed ? "windowed" : "fullscreen", 0u);
    if (p.MultiSampleType) drawfx::Note("multisampled", 0u);
    if (p.BackBufferCount > 1) drawfx::Note("back buffers", 0u);
}
const char *const kCreateLabels[] = {"create#0", "create#1", "create#2", "create#3", "create#4+"};
const char *const kResetLabels[] = {"reset#0", "reset#1", "reset#2", "reset#3", "reset#4+"};

// IDirect3D9::CreateDevice(adapter, type, window, behaviour, parameters, out).
uint32_t D3DCreateDevice(drawfx::Fake &, const uint32_t *a)
{
    const unsigned n = gScript.creates++;
    drawfx::Note(kCreateLabels[n < 4 ? n : 4], 0u);
    NotePresent("pp.create", a[4]);
    NoteKinds(a[3], a[4]);
    const uint32_t hr = n < 8 ? gScript.c->createHrs[n] : 0u;
    void **out = reinterpret_cast<void **>(uintptr_t(a[5]));
    if (hr == 0) {
        ++gDevice.refs;
        *out = &gDevice;
    } else {
        *out = nullptr;
    }
    return hr;
}
// IDirect3DDevice9::Reset(parameters).
uint32_t DeviceReset(drawfx::Fake &, const uint32_t *a)
{
    const unsigned n = gScript.resets++;
    drawfx::Note(kResetLabels[n < 4 ? n : 4], 0u);
    NotePresent("pp.reset", a[0]);
    return n < 8 ? gScript.c->resetHrs[n] : 0u;
}
// GetBackBuffer(chain, index, type, out) and GetDepthStencilSurface(out) always give a surface: both sides release what
// they get without a check (null would fault on either).
uint32_t DeviceBackBuffer(drawfx::Fake &self, const uint32_t *a)
{
    drawfx::Fake &surface = gBackBuffer[a[1] % kChains];
    ++surface.refs;
    *reinterpret_cast<void **>(uintptr_t(a[3])) = &surface;
    return self.ret[fx_d3d9::dev::GetBackBuffer];
}
uint32_t DeviceDepthSurface(drawfx::Fake &self, const uint32_t *a)
{
    ++gDepthSurface.refs;
    *reinterpret_cast<void **>(uintptr_t(a[0])) = &gDepthSurface;
    return self.ret[fx_d3d9::dev::GetDepthStencilSurface];
}
uint32_t DeviceValidate(drawfx::Fake &self, const uint32_t *a)
{
    if (a[0]) *reinterpret_cast<DWORD *>(uintptr_t(a[0])) = 1;
    return self.ret[fx_d3d9::dev::ValidateDevice];
}

bool OnStack(uint32_t p)
{
    const auto *tib = reinterpret_cast<const NT_TIB *>(NtCurrentTeb());
    return p >= uintptr_t(tib->StackLimit) && p < uintptr_t(tib->StackBase);
}
// The listeners' OnEvent: the device's own events (built on the stack) are asked their type, sizes and code, and
// cloned (the original's bodies have the game's vtables, ours ours: the calls compare).
const char *const kDeviceEventLabels[] = {"event.recreating", "event.created", "event.reset", "event.releasing", "event.lost"};
constexpr uint32_t kListenEvents[kListeners] = {cDeviceEvent::kEventId, cDeviceRenderSceneEvent::kEventId};
constexpr uint32_t kHeardAs[kListeners] = {0x51, 0x52};
uint32_t ListenerOnEvent(drawfx::Fake &, const uint32_t *a)
{
    if ((a[0] != kHeardAs[0] && a[0] != kHeardAs[1]) || !OnStack(a[2])) return 0;
    const auto *body = reinterpret_cast<const cDeviceEventBody *>(uintptr_t(a[2]));
    alignas(4) uint8_t copy[8];
    body->CloneInto(copy);
    const auto *clone = reinterpret_cast<const cDeviceEventBody *>(copy);
    drawfx::Note("event", {body->GetEventType(), body->GetSize(), body->GetBodySize(), body->mCode, clone->GetEventType(),
                           clone->mCode});
    if (a[0] == kHeardAs[0] && body->mCode < 5) drawfx::Note(kDeviceEventLabels[body->mCode], 0u);
    NoteFlags();
    return 0;
}
// The temporary face of the device while the listeners connect: the behaviour is the device block itself.
drawfx::Fake gSource;
uint32_t SourceQuery(drawfx::Fake &self, const uint32_t *a)
{
    if (a[0] != nGZEvent::cIAbstractBehaviorType::kIid || !a[1]) return 0;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = self.owner;
    return 1;
}

// ---- the registry: a map placed over the original's static, its guards marked done ----
class RegistryScope {
public:
    RegistryScope()
    {
        std::memcpy(saved_, GameAddress<void>(kRegistryMap), sizeof(saved_));
        savedMapGuard_ = *GameAddress<int32_t>(kRegistryMapGuard);
        savedOnce_ = *GameAddress<uint8_t>(kRegistryOnce);
        savedHolderGuard_ = *GameAddress<int32_t>(kRegistryHolderGuard);
        map_ = ::new (GameAddress<void>(kRegistryMap)) cEventAttributeMap;
        *GameAddress<int32_t>(kRegistryMapGuard) = INT_MIN;
        *GameAddress<uint8_t>(kRegistryOnce) = 1;
        *GameAddress<int32_t>(kRegistryHolderGuard) = INT_MIN;
    }
    ~RegistryScope()
    {
        std::destroy_at(map_);
        std::memcpy(GameAddress<void>(kRegistryMap), saved_, sizeof(saved_));
        *GameAddress<int32_t>(kRegistryMapGuard) = savedMapGuard_;
        *GameAddress<uint8_t>(kRegistryOnce) = savedOnce_;
        *GameAddress<int32_t>(kRegistryHolderGuard) = savedHolderGuard_;
    }
    RegistryScope(const RegistryScope &) = delete;
    RegistryScope &operator=(const RegistryScope &) = delete;

private:
    uint8_t saved_[sizeof(cEventAttributeMap)];
    int32_t savedMapGuard_;
    uint8_t savedOnce_;
    int32_t savedHolderGuard_;
    cEventAttributeMap *map_;
};

// The cells a test sets, restored at its end.
class Cells {
public:
    Cells()
        : manager_(*GameAddress<uint32_t>(kEventManagerCell)), color_(*GameAddress<uint32_t>(kSyncClearColor)),
          mode_(*GameAddress<cDisplayMode>(kDefaultModeRva))
    {
    }
    ~Cells()
    {
        *GameAddress<uint32_t>(kEventManagerCell) = manager_;
        *GameAddress<uint32_t>(kSyncClearColor) = color_;
        *GameAddress<cDisplayMode>(kDefaultModeRva) = mode_;
    }
    Cells(const Cells &) = delete;
    Cells &operator=(const Cells &) = delete;

private:
    uint32_t manager_, color_;
    cDisplayMode mode_;
};

struct StandIns {
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNew)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDelete)};
    shape::StandIn closest{0x18b490, &nGZGraphic4::FindClosestDisplayMode, &StandClosestMode};
    shape::StandIn depth{0x18ab60, &nGZGraphic4::FindClosestDepthFormat, &StandDepthFormat};
    shape::StandIn registry{0x16a170, &cDeviceDX::EventRegistryFor, &pdfx::StandRegistry<kRegistryMap>};
    shape::StandIn multisample{0x18b7d0, &nGZGraphic4::ClampMultisample, &StandMultisample};
    shape::StandIn refresh{0x178b80, &cDeviceEnumDX::Refresh, &StandRefresh};
    shape::StandIn manager710{0x17b710, &cDeviceResourceManagerDX::ReleaseNonSurvivors, &StandManager710};
    // 0x17bb30 is called with two arguments it pops (ret 8) and ignores; ours takes none: a stand-in for each.
    shape::CodePatch managerTake{0x17bb30, reinterpret_cast<const void *>(&StandManagerTakeImage)};
    shape::CodePatch managerTakeOurs{shape::MemberCode(&cDeviceResourceManagerDX::ReleaseOnModeChange), reinterpret_cast<const void *>(&StandManagerTake)};
    shape::StandIn managerMemory{0x17bb00, &cDeviceResourceManagerDX::TakeDevice, &StandManagerMemory};
    shape::StandIn releaseAll{0x17bb80, &cDeviceResourceManagerDX::ReleaseAll, &StandReleaseAll};
    shape::StandIn stateSetUp{0x1807b0, &cDeviceStateDX::SetUpDevice, &StandStateSetUp};
    shape::StandIn formats{0x18acd0, &nGZGraphic4::GetPixelFormatsManagerCached, &StandFormats};
    shape::StandIn framework{0x1ae18d, &::GetDirectorFrameworkObject, &StandFramework};
    shape::StandIn flush{0x17cdf0, &cDeviceStateDX::Flush, &StandFlush};
    shape::StandIn markTargets{0x17ec40, &cDeviceStateDX::MarkTargetsDirty, &StandMarkTargets};
    shape::StandIn setTarget{0x180050, &cDeviceStateDX::SetBackBufferTarget, &StandSetTarget};
    shape::StandIn releaseObjects{0x1815f0, &cDeviceStateDX::ReleaseDeviceObjects, &StandReleaseObjects};
};

// ---- seeding ----
uint32_t Pick(std::mt19937 &gen, unsigned oneIn) { return gen() % oneIn == 0; }

// Modes from a small pool, so that the settings' mode, the list's and the default meet.
cDisplayMode SeedMode(std::mt19937 &gen)
{
    const uint32_t widths[] = {640, 1024}, heights[] = {480, 768}, refresh[] = {60, 85};
    const float aspects[] = {1.25f, 4.0f / 3.0f};
    cDisplayMode m;
    m.mWidth = widths[gen() % 2];
    m.mHeight = heights[gen() % 2];
    m.mAspect = Pick(gen, 16) ? std::nanf("") : aspects[gen() % 2];
    m.mRefresh = refresh[gen() % 2];
    m.mFormat = reinterpret_cast<cIPixelFormat *>(&gFormat[gen() % kFormatCount]);
    return m;
}

// A settings block: the default mode, a listed one or another; formats set or not; the counts and flags.
void SeedSettings(std::mt19937 &gen, const Case &c, cDeviceSettings &s)
{
    const uint32_t pick = gen() % 3;
    s.mMode = pick == 0 ? c.defaultMode : pick == 1 && c.modeCount ? c.modes[gen() % c.modeCount] : SeedMode(gen);
    s.mColorFormat = Pick(gen, 3) ? nullptr : reinterpret_cast<cIPixelFormat *>(&gFormat[gen() % kFormatCount]);
    s.mDepthFormat = Pick(gen, 3) ? nullptr : reinterpret_cast<cIPixelFormat *>(&gFormat[gen() % kFormatCount]);
    s.mBackBufferCount = gen() % 6;
    s.mMultisampleType = Pick(gen, 2) ? 0 : gen() % 9;
    s.mMultisampleQuality = Pick(gen, 2) ? 0 : gen() % 4;
    const uint32_t bits[] = {0x01, 0x04, 0x20, 0x40, 0x80, 0x02};
    s.mFlags = 0;
    for (uint32_t b : bits)
        if (Pick(gen, 3)) s.mFlags |= b;
}

uint32_t SeedHr(std::mt19937 &gen, bool positive)
{
    const uint32_t hrs[] = {0, 0, 0, uint32_t(D3DERR_DEVICELOST), uint32_t(D3DERR_DRIVERINTERNALERROR), 0x8876086c,
                            0x80004005, uint32_t(D3DERR_NOTAVAILABLE), uint32_t(D3DERR_OUTOFVIDEOMEMORY), 1};
    return hrs[gen() % (positive ? 10 : 9)];
}

void SeedCase(std::mt19937 &gen, Case &c, uint32_t op)
{
    std::memset(&c, 0, sizeof(c));
    c.op = op;
    for (uint8_t &b : c.object) b = uint8_t(gen());
    for (uint8_t &b : c.state) b = uint8_t(gen());
    for (uint8_t &b : c.manager) b = uint8_t(gen());
    // The formats: ids, bits (signed; the colour search compares them signed), stencil bits; the first four have
    // their ARGB face (the colour lists draw from them only: CreateDevice asks a listed format's face unchecked).
    const uint32_t ids[] = {D3DFMT_X8R8G8B8, D3DFMT_A8R8G8B8, D3DFMT_R5G6B5, D3DFMT_D24S8, D3DFMT_D16, D3DFMT_D24X8};
    const int32_t bits[] = {16, 24, 32, 32, 16, -8};
    for (unsigned i = 0; i < kFormatCount; ++i) {
        c.formatId[i] = ids[gen() % 6];
        c.formatBits[i] = bits[gen() % 6];
        c.formatStencil[i] = Pick(gen, 2) ? 0 : 8;
        c.formatArgb[i] = i < 4 || Pick(gen, 3);
    }
    c.desktopFormat = gen() % kFormatCount;
    // The modes.
    c.defaultMode = Pick(gen, 2) ? cDisplayMode{0, 0, 0.0f, 0, nullptr} : SeedMode(gen);
    if (std::isnan(c.defaultMode.mAspect)) c.defaultMode.mAspect = 1.25f; // a NaN default never matches itself
    c.desktopMode = SeedMode(gen);
    c.modeCount = gen() % 5;
    for (unsigned i = 0; i < c.modeCount; ++i) c.modes[i] = SeedMode(gen);
    for (unsigned i = 0; i < 3; ++i) {
        c.closest[i] = !Pick(gen, 3);
        c.closestMode[i] = Pick(gen, 5) ? c.defaultMode : SeedMode(gen);
        if (!c.closestMode[i].mFormat) c.closestMode[i].mFormat = reinterpret_cast<cIPixelFormat *>(&gFormat[0]);
    }
    if (Pick(gen, 5)) c.closestMode[gen() % 3] = c.defaultMode; // the search may come back to the default
    // The formats' lists and answers.
    c.colorSupported = !Pick(gen, 3);
    c.depthSupported = !Pick(gen, 3);
    c.colorCount = Pick(gen, 8) ? 0 : 1 + gen() % 4;
    for (unsigned i = 0; i < 4; ++i) c.colorList[i] = uint8_t(gen() % 4);
    c.depthCount = gen() % 5;
    for (unsigned i = 0; i < 4; ++i) c.depthList[i] = uint8_t(gen() % kFormatCount);
    for (int32_t &f : c.depthFound) f = Pick(gen, 4) ? -1 : int32_t(gen() % kFormatCount);
    const uint32_t types[] = {D3DDEVTYPE_HAL, D3DDEVTYPE_HAL, D3DDEVTYPE_REF, D3DDEVTYPE_SW};
    c.deviceType = types[gen() % 4];
    c.caps = gen() & 0xff;
    c.features = gen() & 0xff;
    c.msType = Pick(gen, 2) ? 0 : gen() % 9;
    c.msQuality = Pick(gen, 2) ? 0 : gen() % 4;
    c.fallback = Pick(gen, 2) ? 0 : gen();
    c.window = 0x40000 + gen() % 0x100;
    for (uint32_t &hr : c.resetHrs) hr = SeedHr(gen, true);
    // A positive answer from the last creation try reaches TakeNewDevice without a device on both sides (cDeviceDX.cpp's
    // ORIGINAL-BUG): creation answers are never positive.
    for (uint32_t &hr : c.createHrs) hr = SeedHr(gen, false);
    if (Pick(gen, 3)) std::memset(c.createHrs, 0, sizeof(c.createHrs));
    const uint32_t validates[] = {0, 0, 0, uint32_t(D3DERR_DEVICELOST), uint32_t(D3DERR_DRIVERINTERNALERROR), 0x88760818, 1};
    c.validateHr = validates[gen() % 7];
    const uint32_t presents[] = {0, 0, 0, 0, uint32_t(D3DERR_DEVICELOST), uint32_t(D3DERR_DRIVERINTERNALERROR), 0x8876086c};
    c.presentHr = presents[gen() % 7];
    const uint32_t coops[] = {0, 0, uint32_t(D3DERR_DEVICELOST), uint32_t(D3DERR_DEVICENOTRESET), uint32_t(D3DERR_DRIVERINTERNALERROR)};
    c.coop = coops[gen() % 5];
    const uint32_t queries[] = {0, 0, uint32_t(D3DERR_NOTAVAILABLE), 0x8876086c, 1};
    c.queryHr = queries[gen() % 5];
    c.queueQuery = !Pick(gen, 4);
    c.queueSync = !Pick(gen, 4);
    c.queueProbe = !Pick(gen, 4);
    c.queryBusy = gen() % 3;
    c.connections = gen() % 4;
    c.device.Random(gen, fx_d3d9::IDirect3DDevice9.count, 5);

    // The device.
    auto *d = reinterpret_cast<cDeviceDX *>(c.object);
    // The active bit (NotifyListeners tells the listeners only while it is set), mostly set.
    d->mRefCount = 2 + gen() % 4 | (Pick(gen, 6) ? 0u : 0x80000000u);
    SeedSettings(gen, c, d->mSettings);
    d->mDevice = Pick(gen, 3) ? nullptr : reinterpret_cast<IDirect3DDevice9 *>(&gDevice);
    if (op == kResize) d->mDevice = reinterpret_cast<IDirect3DDevice9 *>(&gDevice);
    d->mDeviceEnum = reinterpret_cast<cIGZUnknown *>(&gEnum);
    d->mResourceManager = reinterpret_cast<cDeviceResourceManagerDX *>(gManagerBlock);
    d->mState = reinterpret_cast<cDeviceStateDX *>(gStateBlock);
    d->mDisplayMonitor = nullptr;
    d->mSwapChainCount = gen() % 4;
    for (unsigned i = 0; i < kChains; ++i)
        d->mSwapChains[i] = i < d->mSwapChainCount || Pick(gen, 3) ? reinterpret_cast<cIGZUnknown *>(&gChain[i]) : nullptr;
    if (op == kResize) d->mSwapChains[0] = reinterpret_cast<cIGZUnknown *>(&gChain[0]);
    d->mTargetProvider = Pick(gen, 3) ? nullptr : reinterpret_cast<cIGZUnknown *>(&gProvider);
    d->mGpuQuery = Pick(gen, 2) ? nullptr : reinterpret_cast<IDirect3DQuery9 *>(&gQuery);
    d->mSyncSurface = Pick(gen, 2) ? nullptr : reinterpret_cast<IDirect3DSurface9 *>(&gSync);
    d->mProbeTexture = Pick(gen, 2) ? nullptr : reinterpret_cast<IDirect3DTexture9 *>(&gProbe);
    const int32_t depths[] = {0, 0, 1, 2};
    d->mSceneDepth = depths[gen() % 4];
    d->mFullscreen = Pick(gen, 2);
    d->mDeviceLost = Pick(gen, 4);
    d->mDeviceFailed = Pick(gen, 6);
    d->mReleased = Pick(gen, 8);
    d->mByte74 = Pick(gen, 3);
    d->mCreationBlocked = Pick(gen, 10);
    if (op == kResize) d->mDeviceLost = d->mDeviceFailed = d->mReleased = false;
    if (op == kCanvas) d->mReleased = Pick(gen, 16);
    // The state's fields WaitForGpu reads: the wanted first target.
    Put<uint32_t>(c.state, 0xadc, Pick(gen, 4) ? 0u : gen() | 1);
    // Arguments.
    for (uint32_t &w : c.args) w = gen();
    SeedSettings(gen, c, c.data.settings);
    if (Pick(gen, 2)) c.data.settings.mFlags = d->mSettings.mFlags;
    for (uint32_t &w : c.data.message) w = gen();
    if (op == kResize) {
        c.data.message[1] = 0;
        c.data.message[2] = 0;
        c.data.message[3] = Pick(gen, 4) ? 640 : 800;
        c.data.message[4] = Pick(gen, 4) ? 480 : 600;
        c.data.message[5] = Pick(gen, 8) ? 0x8 : 0;
    }
    if (op == kCanvas) c.data.message[1] = Pick(gen, 3) ? 7 : 3;
}

// ---- a side's world ----
cConnectionManager *gManager = nullptr;

void ResetFakes(const Case &c)
{
    gDevice.Reset<fx_d3d9::IDirect3DDevice9>(d3dfx::kDeviceId);
    c.device.Apply(gDevice);
    gDevice.Returns(fx_d3d9::dev::TestCooperativeLevel, c.coop);
    gDevice.Returns(fx_d3d9::dev::ValidateDevice, c.validateHr);
    gDevice.Returns(fx_d3d9::dev::Present, c.presentHr);
    gDevice.Returns(fx_d3d9::dev::CreateQuery, c.queryHr);
    gDevice.On(fx_d3d9::dev::Reset, &DeviceReset);
    gDevice.On(fx_d3d9::dev::GetBackBuffer, &DeviceBackBuffer);
    gDevice.On(fx_d3d9::dev::GetDepthStencilSurface, &DeviceDepthSurface);
    gDevice.On(fx_d3d9::dev::ValidateDevice, &DeviceValidate);
    gCreates.reset();
    gCreates.emplace(gDevice);
    d3dfx::MakeQuery(gNewQuery, kNewQueryId, gNewQueryData);
    d3dfx::MakeSurface(gNewSync, kNewSyncId, gNewSyncData);
    gNewProbe.Reset<fx_d3d9::IDirect3DTexture9>(kNewProbeId);
    if (c.queueQuery) gCreates->Queue(fx_d3d9::dev::CreateQuery, gNewQuery);
    if (c.queueSync) gCreates->Queue(fx_d3d9::dev::CreateRenderTarget, gNewSync);
    if (c.queueProbe) gCreates->Queue(fx_d3d9::dev::CreateTexture, gNewProbe);
    gD3D.Reset<fx_d3d9::IDirect3D9>(kD3DId);
    gD3D.On(16, &D3DCreateDevice);
    d3dfx::MakeQuery(gQuery, kQueryId, gQueryData);
    gQueryData.busy = c.queryBusy;
    d3dfx::MakeSurface(gSync, kSyncId, gSyncData);
    gSync.Returns(d3dfx::surf::LockRect, 0u); // WaitForGpu writes through what LockRect gives
    gProbe.Reset<fx_d3d9::IDirect3DTexture9>(kProbeId);
    gDepthSurface.Reset<fx_d3d9::IDirect3DSurface9>(kDepthSurfaceId);
    for (unsigned i = 0; i < kChains; ++i) gBackBuffer[i].Reset<fx_d3d9::IDirect3DSurface9>(kBackBufferId0 + i);

    gEnum.Reset<kEnum>(kEnumId);
    gEnum.On(6, &EnumDesktopMode);
    gEnum.On(7, &EnumModes);
    gEnum.On(10, &EnumColorFormats);
    gEnum.Returns(12, uint32_t(c.colorSupported));
    gEnum.On(14, &EnumDepthFormats);
    gEnum.Returns(16, uint32_t(c.depthSupported));
    gEnum.On(20, &EnumCap);
    gEnum.On(25, &EnumFeature);
    gEnum.On(27, &EnumIdentifier);
    for (unsigned i = 0; i < kPool; ++i) {
        gModeIt[i].Reset<kModeIterator>(kModeItId0 + i);
        gModeIt[i].On(3, &ModeClone);
        gModeIt[i].On(4, &IteratorIsEqual);
        gModeIt[i].On(5, &ModeGet);
        gModeIt[i].On(6, &IteratorNext);
        gFormatIt[i].Reset<kFormatIterator>(kFormatItId0 + i);
        gFormatIt[i].On(3, &FormatClone);
        gFormatIt[i].On(4, &IteratorIsEqual);
        gFormatIt[i].On(5, &FormatGet);
        gFormatIt[i].On(6, &IteratorNext);
    }
    gModeItNext = gFormatItNext = 0;
    for (unsigned i = 0; i < kFormatCount; ++i) {
        gFormat[i].Reset<kPixelFormat>(kFormatId0 + i);
        gFormat[i].Returns(3, c.formatId[i]);
        gFormat[i].Returns(6, uint32_t(c.formatBits[i]));
        gFormat[i].Returns(13, c.formatStencil[i]);
        if (c.formatArgb[i]) gFormat[i].AddFace(kArgbIid, gFormat[i]);
        gFormat[i].AddFace(kDepthStencilIid, gFormat[i]);
    }
    gWindow.Reset<kEngine>(kWindowId);
    gWindow.On(39, &WindowFallback);
    gWindow.Returns(40, c.window);
    gGraphic.Reset<kGraphicService>(kGraphicId);
    gGraphic.On(25, &GraphicDirect3D);
    gCom.Reset<kEngine>(kComId);
    gCom.On(3, &ComGetClassObject);
    gStateFake.Reset<kEngine>(kStateFakeId);
    gStateFake.Embed(gStateBlock);
    gManagerFake.Reset<kEngine>(kManagerFakeId);
    gManagerFake.Embed(gManagerBlock);
    gFormatsFake.Reset<kFormats>(kFormatsId);
    gFormatsFake.On(5, &FormatsDesktop);
    gSurfaceObject.Reset<kEngine>(kSurfaceObjectId);
    gSurfaceObject.Returns(20, 640u);
    gSurfaceObject.Returns(21, 480u);
    gProvider.Reset<kEngine>(kProviderId);
    gNewProvider.Reset<kEngine>(kNewProviderId);
    for (unsigned i = 0; i < kChains; ++i) {
        gChain[i].Reset<kEngine>(kChainId0 + i);
        gChain[i].Returns(4, &gSurfaceObject);
        gNewChain[i].Reset<kEngine>(kNewChainId0 + i);
        gNewChain[i].Returns(4, &gSurfaceObject);
    }
    for (unsigned i = 0; i < kListeners; ++i) {
        gListener[i].Reset<kBehavior>(kListenerId0 + i);
        gListener[i].AddFace(nGZEvent::cIAbstractBehaviorType::kIid, gListener[i]);
        gListener[i].On(3, &ListenerOnEvent);
    }
    gSource.Reset<kBehavior>(0xe0fff);
    gSource.On(0, &SourceQuery);
}

void BuildSide(const Case &c, d3dfx::ErrorLog &errors, void *vtable)
{
    drawfx::ResetAll();
    gScript = {&c};
    std::memcpy(gObject, c.object, cDeviceDX::kSize);
    std::memcpy(gStateBlock, c.state, sizeof(gStateBlock));
    std::memcpy(gManagerBlock, c.manager, sizeof(gManagerBlock));
    gData = c.data;
    std::memset(&gSyncData, 0, sizeof(gSyncData));
    gSyncData.bits[0] = 0x5a;
    std::memset(&gNewSyncData, 0, sizeof(gNewSyncData));
    gQueryData = {};
    gNewQueryData = {};
    *GameAddress<uint32_t>(kSyncClearColor) = c.args[3];
    *GameAddress<cDisplayMode>(kDefaultModeRva) = c.defaultMode;
    ResetFakes(c);
    errors.Answer(0xfb6372ee, 0xbb6377fa, gWindow);
    errors.Answer(0x85522803, 0xc55289ba, gGraphic);
    drawfx::AddRange(gObject, cDeviceDX::kSize, kObjectTag);
    drawfx::AddRange(gStateBlock, sizeof(gStateBlock), kStateTag);
    drawfx::AddRange(gManagerBlock, sizeof(gManagerBlock), kManagerBlockTag);
    drawfx::AddRange(&gData, sizeof(gData), kDataTag);
    drawfx::AddRange(gSyncData.bits, sizeof(gSyncData.bits), kSyncTag);
    gManager = cConnectionManagerAdapters::Construct(::operator new(cConnectionManager::kSize), nullptr);
    drawfx::AddRange(gManager, cConnectionManager::kSize, kConnTag);
    *GameAddress<void *>(kEventManagerCell) = gManager;
    gSource.Embed(gObject);
    for (unsigned i = 0; i < kListeners; ++i)
        if ((c.connections >> i) & 1)
            static_cast<nGZEvent::cIConnectionManager *>(gManager)->Connect(
                reinterpret_cast<cIGZUnknown *>(gObject), kListenEvents[i], reinterpret_cast<cIGZUnknown *>(&gListener[i]),
                kHeardAs[i]);
    std::memcpy(gObject, &vtable, 4);
    drawfx::gLog.clear();
}

void DestroySide()
{
    cConnectionManagerAdapters::Destruct(gManager, nullptr);
    ::operator delete(gManager);
    gManager = nullptr;
    gCreates.reset();
}

void NoteRefs()
{
    std::vector<const drawfx::Fake *> all = {&gDevice, &gD3D, &gQuery, &gSync, &gProbe, &gNewQuery, &gNewSync, &gNewProbe,
                                             &gDepthSurface, &gEnum, &gWindow, &gGraphic, &gCom, &gStateFake,
                                             &gManagerFake, &gFormatsFake, &gProvider, &gNewProvider, &gSurfaceObject};
    for (unsigned i = 0; i < kChains; ++i) {
        all.push_back(&gBackBuffer[i]);
        all.push_back(&gChain[i]);
        all.push_back(&gNewChain[i]);
    }
    for (const drawfx::Fake &f : gFormat) all.push_back(&f);
    for (const drawfx::Fake &f : gModeIt) all.push_back(&f);
    for (const drawfx::Fake &f : gFormatIt) all.push_back(&f);
    for (const drawfx::Fake &f : gListener) all.push_back(&f);
    for (const drawfx::Fake *f : all) drawfx::Note("refs", {f->id, uint32_t(f->refs)});
    drawfx::Note("iterators", {gModeItNext, gFormatItNext});
    drawfx::Note("pending creates", gCreates ? gCreates->Pending() : 0u);
}

// ---- one operation, on the original (at its RVAs) or ours ----
void RunOriginal(difftest::Context &ctx, const Case &c)
{
    void *const self = gObject;
    switch (c.op) {
    case kCreate: drawfx::Note("ret", ctx.Original<bool(__thiscall *)(void *)>(0x167ec0)(self)); break;
    case kApply: drawfx::Note("ret", ctx.Original<bool(__thiscall *)(void *, const cDeviceSettings *)>(0x16a520)(self, &gData.settings)); break;
    case kResize:
    case kCanvas: {
        const uint32_t type = c.op == kResize ? cDeviceDX::kResizeEvent : cDeviceDX::kCanvasEvent;
        ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t, void *, uint32_t)>(0x169bf0)(self, type, c.args[0], gData.message, c.args[1]);
        break;
    }
    }
}

void RunOurs(const Case &c)
{
    cIDeviceDX *const d = Face();
    switch (c.op) {
    case kCreate: drawfx::Note("ret", Device()->CreateDevice()); break;
    case kApply: drawfx::Note("ret", RawSlot<23, bool>(d, &gData.settings)); break;
    case kResize:
    case kCanvas: {
        const uint32_t type = c.op == kResize ? cDeviceDX::kResizeEvent : cDeviceDX::kCanvasEvent;
        RawSlot<3, void>(d, type, c.args[0], static_cast<void *>(gData.message), c.args[1]);
        break;
    }
    }
}

struct Result {
    drawfx::Log log;
    std::vector<uint8_t> object, state, manager, sync;
    uint32_t color;
    long long blocks, bytes;
    std::string thrown;
    std::vector<std::string> errors;
};

template <class Run> Result Side(const Case &c, d3dfx::ErrorLog &errors, impfx::Imports &imp, void *vtable, Run run)
{
    imp.BeginSide();
    BuildSide(c, errors, vtable);
    std::string thrown;
    long long blocks = 0, bytes = 0;
    drawfx::gLog = drawfx::Log();
    drawfx::gLog.reserve(8192);
    {
        difftest::AllocationCounter counter;
        try {
            run();
        } catch (const cError &e) {
            char text[48];
            std::snprintf(text, sizeof(text), "cError vt %d code %d", e.mVTable == GameAddress<void>(0xd9cd70), e.mCode);
            thrown = text;
        }
        counter.Stop();
        blocks = counter.Blocks();
        bytes = counter.Bytes();
    }
    NoteRefs();
    Result r;
    r.log = drawfx::gLog;
    r.object.assign(gObject + 4, gObject + cDeviceDX::kSize);
    r.state.assign(gStateBlock + 4, gStateBlock + sizeof(gStateBlock));
    r.manager.assign(gManagerBlock + 4, gManagerBlock + sizeof(gManagerBlock));
    r.sync.assign(gSyncData.bits, gSyncData.bits + sizeof(gSyncData.bits));
    r.color = *GameAddress<uint32_t>(kSyncClearColor);
    r.blocks = blocks;
    r.bytes = bytes;
    r.thrown = thrown;
    r.errors = drawfx::gErrors;
    DestroySide();
    return r;
}

void *OurVTable()
{
    alignas(16) uint8_t scratch[cDeviceDX::kSize];
    cDeviceDXAdapters::Construct(scratch, nullptr);
    void *vtable;
    std::memcpy(&vtable, scratch, 4);
    return vtable;
}

struct World {
    RegistryScope registry;
    Cells cells;
    StandIns stands;
    d3dfx::ErrorLog errors;
    impfx::Imports imp;
    explicit World(difftest::Context &ctx) : errors(ctx), imp(ctx)
    {
        IMPFX_ADD(imp, Sleep);
    }
};

void Seen(const Result &r, std::set<std::string> &seen)
{
    for (const drawfx::Call &call : r.log) {
        if (!call.iface) seen.insert(call.note);
        else if (call.slot < call.iface->count) seen.insert(call.iface->slots[call.slot].name);
    }
    if (!r.thrown.empty()) seen.insert("thrown");
}

// `rounds` cases of each op; `needs`: what the original's logs must show over the sweep (note labels, slot names,
// "thrown"), so that a sweep that never reaches a path fails.
void Sweep(difftest::Context &ctx, uint32_t seed, std::initializer_list<uint32_t> ops, unsigned rounds,
           std::initializer_list<const char *> needs)
{
    World world(ctx);
    void *const ours = OurVTable();
    void *const game = GameAddress<void>(kVTableRva);
    std::mt19937 gen(seed);
    std::set<std::string> seen;
    for (uint32_t op : ops) {
        for (unsigned round = 0; round < rounds; ++round) {
            auto c = std::make_unique<Case>();
            SeedCase(gen, *c, op);
            const Result a = Side(*c, world.errors, world.imp, game, [&] { RunOriginal(ctx, *c); });
            const Result b = Side(*c, world.errors, world.imp, ours, [&] { RunOurs(*c); });
            Seen(a, seen);
            const char *label = kOpNames[op];
            for (const std::string &e : a.errors) ctx.Fail("%s round %u (original): %s", label, round, e.c_str());
            for (const std::string &e : b.errors) ctx.Fail("%s round %u (ours): %s", label, round, e.c_str());
            if (!a.errors.empty() || !b.errors.empty()) return;
            if (!drawfx::Same(ctx, a.log, b.log, label, round)) return;
            if (a.object != b.object || a.state != b.state || a.manager != b.manager || a.sync != b.sync ||
                a.color != b.color || a.blocks != b.blocks || a.bytes != b.bytes || a.thrown != b.thrown) {
                size_t at = 0;
                while (at < a.object.size() && a.object[at] == b.object[at]) ++at;
                ctx.Fail("%s round %u: object %d (first difference +0x%zx) state %d manager %d sync %d color %08x/%08x "
                         "blocks %lld/%lld bytes %lld/%lld thrown '%s'/'%s'",
                         label, round, a.object == b.object, at + 4, a.state == b.state, a.manager == b.manager,
                         a.sync == b.sync, a.color, b.color, a.blocks, b.blocks, a.bytes, b.bytes, a.thrown.c_str(),
                         b.thrown.c_str());
                return;
            }
        }
    }
    for (const char *need : needs)
        if (!seen.count(need)) ctx.Fail("the sweep never reached '%s' (a vacuous path)", need);
}

} // namespace

// The choices: the display mode (listed, the three searches, none), the display format (the desktop's for the default
// mode), the colour format (the settings', the best listed ARGB, none) and the depth format (the settings', the
// searches with and without stencil, none).
DIFF_TEST(cDeviceDX_CreateDevice_Choices, 0x167ec0)
{
    Sweep(ctx, 0x167ec0, {kCreate}, 600,
          {"GetAdapterIdentifier", "GetAdapterDisplayMode", "GetDisplayModes", "GetMode", "Clone", "closest",
           "closest.range", "GetDesktopFormat", "IsColorFormatSupported", "GetColorFormats", "GetFormat",
           "GetBitsPerPixel", "IsDepthFormatSupported", "GetDepthFormats", "GetStencilBits", "depth search",
           "depth.range", "enum.refresh", "manager.ReleaseAll", "Slot26", "event.recreating", "ret"});
}

// The present parameters and behaviour (swap effects, multisampling, back buffers, interval, vertex processing, driver
// management) and the device's way up: Reset with its fallbacks and give-up, creation with the window's fallback to the
// desktop and its retries, the new device's objects, validation and the first frames.
DIFF_TEST(cDeviceDX_CreateDevice_Device, 0x167ec0)
{
    Sweep(ctx, 0x167ec1, {kCreate}, 900,
          {"multisample", "vp.software", "vp.mixed", "vp.hardware", "vp.pure", "no driver management", "swap.flip",
           "swap.copy", "swap.discard", "windowed", "fullscreen", "multisampled", "back buffers", "reset#0", "reset#1",
           "reset#2", "create#0", "create#1", "create#2", "AskFallback", "Resize", "Slot37", "Slot38", "Slot22",
           "manager.0x17b710", "manager.0x17bb30", "manager.0x17bb00", "state.0x1807b0", "state.ReleaseDeviceObjects",
           "GetClassObject", "SetSurface", "GetBackBuffer", "GetDepthStencilSurface", "CreateQuery", "CreateRenderTarget",
           "CreateTexture", "SetFVF", "ValidateDevice", "SetVertexDeclaration", "Clear", "Present", "EndScene",
           "EvictManagedResources", "BeginScene", "LogD3DError", "event.created", "event.reset", "event.lost", "thrown",
           "Issue", "GetData", "LockRect", "state.Flush", "Reset"});
}

// The callers that reach CreateDevice: ApplySettings (with its retry from a released device), a window resize, a
// canvas frame's recovery paths.
DIFF_TEST(cDeviceDX_CreateDevice_Callers, 0x16a520)
{
    Sweep(ctx, 0x16a520, {kApply, kResize, kCanvas}, 300,
          {"create#0", "reset#0", "event.created", "event.releasing", "event.lost", "TestCooperativeLevel", "Present",
           "GetWidth", "GetHeight", "Sleep", "thrown", "enum.refresh"});
}
