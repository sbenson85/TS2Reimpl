// Differential tests for nGZGraphic4::cGZGraphicSystem and cBackbufferAsGZBuffer as whole classes
// (src/gz/cGZGraphicSystem.h, lane NQ): the creator 0x1475e0, every slot of the system's three faces and of the back
// buffer's two, and the destructors.
//
// One world per case, built alike for each side from the case's seed:
// - The system is a 0x40-byte block of the case's bytes (static; a heap block for the ops that free it) with the side's
//   three vtable words; its back buffer (+0x18) a heap block of the case's bytes with the side's two vtable words (the
//   original's class on the original's side, ours on ours). Its surface (+0xc) and hardware cache (+0x30) are fakes
//   or null; the system's draw buffer (+0x1c) a fake or null.
// - Game code the classes hand on to is stood in on both sides by recorders: the COM director's framework (0x1ae18d: a
//   fake whose GetClassObject hands out the draw buffer, draw context, font renderer and buffer fakes), the current
//   device (0x18ae40), its enumeration (0x18b470), the pixel formats manager (0x18acd0), the format and mode searches
//   and setters (0x18a9b0, 0x18b440, 0x18b490, 0x18b250) and the desktop size (0x154a80), each answering the case.
// - Framework services (d3dfx::ErrorLog) answer fakes: the font service, cGraphicSystemDX, the window's two interfaces,
//   the canvas; a case may run without a framework. Win32 (GetDC, GetDeviceCaps, ReleaseDC, GetCursorPos,
//   ScreenToClient, MessageBoxA) are impfx fakes.
// - Events: our real cConnectionManager in the cell (0x10c9500), listener fakes connected to the system; the attribute
//   registry 0x148440 runs for real over a map placed at its static (RegistryScope).
// Compared: the log (every call on every fake with its arguments, the stand-ins' notes), each slot's result, every byte
// of the system and the back buffer but their vtable words (and the system's back buffer pointer, compared as "the
// side's back buffer"), every fake's count, the connections, the allocations.
#include "code_patch.h"
#include "d3dfx.h"
#include "difftest.h"
#include "gz_registry_fx.h"
#include "gz/GraphicServices.h"
#include "gz/GraphicModeSearch.h"
#include "gz/MultiMon.h"
#include "impfx.h"

#include "game/vtable.h"
#include "gz/cConnectionManager.h"
#include "gz/cGZGraphicSystem.h"
#include "rz/StaticGetters.h"

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace nGZGraphic4;
using nGZEvent::cConnectionManager;
using nGZEvent::cConnectionManagerAdapters;
using nGZEvent::cEventAttributeMap;

namespace {

constexpr uint32_t kVTableRva = 0xd9bd78, kFaceVTableRva = 0xd9bdbc, kServiceVTableRva = 0xd9bed4;
constexpr uint32_t kBufVTableRva = 0xd9bf0c, kBufRawVTableRva = 0xd9bfb4;
constexpr uint32_t kEventManagerCell = 0x10c9500;
constexpr uint32_t kRegistryMap = 0x10c9504, kRegistryMapGuard = 0x10c950c, kRegistryOnce = 0x10c951c,
                   kRegistryHolderGuard = 0x10c9524;
constexpr size_t kSize = cGZGraphicSystem::kSize, kBufSize = cBackbufferAsGZBuffer::kSize;
constexpr uint32_t kSysTag = 0x7e000000, kBufTag = 0x7d000000, kDataTag = 0x7c000000, kConnTag = 0x7b000000,
                   kMarkTag = 0x7a000000;

// ---- fakes ----
constexpr drawfx::Slot U{0, drawfx::Ret::Int, false, "", ""};
constexpr drawfx::Slot kQI{2, drawfx::Ret::Int, true, "QueryInterface", "ui"};
constexpr drawfx::Slot kAddRefSlot{0, drawfx::Ret::Int, true, "AddRef", ""};
constexpr drawfx::Slot kReleaseSlot{0, drawfx::Ret::Int, true, "Release", ""};

inline constexpr drawfx::Slot kComSlots[] = {kQI, kAddRefSlot, kReleaseSlot, {3, drawfx::Ret::Int, true, "GetClassObject", "uui"}};
inline constexpr drawfx::Iface kComIface{"cIGZCOM", 4, kComSlots};

// The current device (cIDeviceDX): 37 IsFullscreen, 39 GetBackBuffer, 42 GetDisplayMonitor.
inline constexpr drawfx::Slot kDeviceSlots[] = {
    kQI, kAddRefSlot, kReleaseSlot, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    U, U, U, U, U, U,
    {0, drawfx::Ret::Int, true, "IsFullscreen", ""}, U,
    {1, drawfx::Ret::Int, true, "GetBackBuffer", "u"}, U, U,
    {0, drawfx::Ret::Int, true, "GetDisplayMonitor", ""},
};
inline constexpr drawfx::Iface kDeviceIface{"cIDeviceDX", 43, kDeviceSlots};

// The device's back buffer surface (cISurface): 6 IsCreated, 20 / 21 its size, 22 its pixel format.
inline constexpr drawfx::Slot kSurfaceSlots[] = {
    kQI, kAddRefSlot, kReleaseSlot, U, U, U,
    {0, drawfx::Ret::Int, true, "IsCreated", ""}, U, U, U, U, U, U, U, U, U, U, U, U, U,
    {0, drawfx::Ret::Int, true, "GetWidth", ""},
    {0, drawfx::Ret::Int, true, "GetHeight", ""},
    {0, drawfx::Ret::Int, true, "GetPixelFormat", ""},
};
inline constexpr drawfx::Iface kSurfaceIface{"cISurface", 23, kSurfaceSlots};

// Its cISurfaceDX face: 4 GetSurfaceConst.
inline constexpr drawfx::Slot kSurfaceDXSlots[] = {kQI, kAddRefSlot, kReleaseSlot, U, {0, drawfx::Ret::Int, true, "GetSurfaceConst", ""}};
inline constexpr drawfx::Iface kSurfaceDXIface{"cISurfaceDX", 5, kSurfaceDXSlots};

// Pixel formats: 6 GetBitsPerPixel; the ARGB face 16 / 17.
inline constexpr drawfx::Slot kFormatSlots[] = {kQI, kAddRefSlot, kReleaseSlot, U, U, U, {0, drawfx::Ret::Int, true, "GetBitsPerPixel", ""}};
inline constexpr drawfx::Iface kFormatIface{"cIPixelFormat", 7, kFormatSlots};
inline constexpr drawfx::Slot kArgbSlots[] = {
    kQI, kAddRefSlot, kReleaseSlot, U, U, U, U, U, U, U, U, U, U, U, U, U,
    {1, drawfx::Ret::Int, true, "ToNative", "W1"},
    {2, drawfx::Ret::Int, true, "FromNative", "iu"},
};
inline constexpr drawfx::Iface kArgbIface{"cIPixelFormatARGB", 18, kArgbSlots};

// The enumeration: 6 GetAdapterDisplayMode, 7 GetDisplayModes, 8 GetDisplayFormats, 10 GetColorFormats.
inline constexpr drawfx::Slot kEnumSlots[] = {
    kQI, kAddRefSlot, kReleaseSlot, U, U, U,
    {1, drawfx::Ret::Int, true, "GetAdapterDisplayMode", "i"},
    {2, drawfx::Ret::Int, true, "GetDisplayModes", "ii"},
    {1, drawfx::Ret::Int, true, "GetDisplayFormats", "i"}, U,
    {4, drawfx::Ret::Int, true, "GetColorFormats", "iuiu"},
};
inline constexpr drawfx::Iface kEnumIface{"cIDeviceEnum", 11, kEnumSlots};

inline constexpr drawfx::Slot kFormatsSlots[] = {kQI, kAddRefSlot, kReleaseSlot, U, U, {0, drawfx::Ret::Int, true, "GetDisplayFormat", ""}};
inline constexpr drawfx::Iface kFormatsIface{"cIPixelFormatsManager", 6, kFormatsSlots};

inline constexpr drawfx::Slot kFontsSlots[] = {kQI, kAddRefSlot, kReleaseSlot, U, U, U, {1, drawfx::Ret::Int, true, "SetRenderer", "i"}};
inline constexpr drawfx::Iface kFontsIface{"cIGZFontService", 7, kFontsSlots};

inline constexpr drawfx::Slot kSystemDXSlots[] = {
    kQI, kAddRefSlot, kReleaseSlot, U, U, U, U, U,
    {3, drawfx::Ret::Int, true, "dx.Connect", "uiu"},
    U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    {0, drawfx::Ret::Int, true, "dx.GetDeviceCount", ""},
};
inline constexpr drawfx::Iface kSystemDXIface{"cIGraphicSystemDX", 24, kSystemDXSlots};

// The window's behaviour interface (0x5b637209) and its canvas interface (0xbb6377fa).
inline constexpr drawfx::Slot kWinSlots[] = {
    kQI, kAddRefSlot, kReleaseSlot, U, U, U, U, U, U, U, U, U, U, U, U,
    {2, drawfx::Ret::Int, true, "win.Show", "uu"}, U,
    {1, drawfx::Ret::Int, true, "win.SetTitle", "s"},
    {2, drawfx::Ret::Int, true, "win.SetSize", "uu"}, U,
    {1, drawfx::Ret::Int, true, "win.SetMonitor", "i"}, U, U,
    {0, drawfx::Ret::Int, true, "win.Slot23", ""},
    {0, drawfx::Ret::Int, true, "win.Slot24", ""},
    U, U, U, U, U, U, U, U, U, U, U,
    {0, drawfx::Ret::Int, true, "win.Slot36", ""},
};
inline constexpr drawfx::Iface kWindowIface{"cIGZWin(0x5b637209)", 37, kWinSlots};
inline constexpr drawfx::Slot kWindowCanvasSlots[] = {
    kQI, kAddRefSlot, kReleaseSlot, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    U, U, U, U, U, U, U, U, U,
    {0, drawfx::Ret::Int, true, "win.GetHwnd", ""},
};
inline constexpr drawfx::Iface kWindowCanvasIface{"cICanvasW32", 41, kWindowCanvasSlots};

// The draw-context buffer (cIGZDrawContextAsBuffer) and the draw context the system gives it.
inline constexpr drawfx::Slot kDrawBufferSlots[] = {
    kQI, kAddRefSlot, kReleaseSlot,
    {0, drawfx::Ret::Int, true, "db.GetBuffer", ""}, U, U, U,
    {2, drawfx::Ret::Int, true, "db.Init", "ii"},
    {0, drawfx::Ret::Int, true, "db.Refresh", ""},
};
inline constexpr drawfx::Iface kDrawBufferIface{"cIGZDrawContextAsBuffer", 9, kDrawBufferSlots};
inline constexpr drawfx::Slot kContextSlots[] = {
    kQI, kAddRefSlot, kReleaseSlot, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    {1, drawfx::Ret::Int, true, "ctx.SetTarget", "i"}, U, U, U, U,
    {1, drawfx::Ret::Int, true, "ctx.Slot22", "u"}, U,
    {1, drawfx::Ret::Int, true, "ctx.Slot24", "u"},
};
inline constexpr drawfx::Iface kContextIface{"cIGZDrawContext", 25, kContextSlots};

// The draw buffer's buffer (cIGZBuffer): 15 GetNativeColorType, 30 / 31 the conversions.
inline constexpr drawfx::Slot kBufferSlots[] = {
    kQI, kAddRefSlot, kReleaseSlot, U, U, U, U, U, U, U, U, U, U, U, U,
    {1, drawfx::Ret::Int, true, "buf.GetNativeColorType", "i"}, U, U, U, U, U, U, U, U, U, U, U, U, U, U,
    {3, drawfx::Ret::Int, true, "buf.ToNative", "bbb"},
    {4, drawfx::Ret::Int, true, "buf.ToRGB", "uiii"},
};
inline constexpr drawfx::Iface kBufferIface{"cIGZBuffer", 32, kBufferSlots};

inline constexpr drawfx::Slot kStringSlots[] = {kQI, kAddRefSlot, kReleaseSlot, U, U, {0, drawfx::Ret::Int, true, "str.ToChar", ""}};
inline constexpr drawfx::Iface kStringIface{"cIGZString", 6, kStringSlots};

inline constexpr drawfx::Slot kCacheSlots[] = {kQI, kAddRefSlot, kReleaseSlot, {1, drawfx::Ret::Int, true, "cache.Accept", "i"}};
inline constexpr drawfx::Iface kCacheIface{"cIGZHardwareCache", 4, kCacheSlots};

inline constexpr drawfx::Slot kListenerSlots[] = {
    kQI, kAddRefSlot, kReleaseSlot,
    {4, drawfx::Ret::Int, true, "OnEvent", "uuii"},
    {3, drawfx::Ret::Int, true, "OnQuery", "uui"},
};
inline constexpr drawfx::Iface kListenerIface{"cIAbstractBehavior", 5, kListenerSlots};

inline constexpr drawfx::Slot kPlainSlots[] = {kQI, kAddRefSlot, kReleaseSlot};
inline constexpr drawfx::Iface kPlainIface{"cIGZUnknown", 3, kPlainSlots};

enum : uint32_t {
    kComId = 0xe0100, kDeviceId, kSurfaceId, kSurfaceDXId, kFormatId, kArgbId, kEnumId, kFormatsId, kFontsId,
    kSystemDXId, kWindowId, kWindowCanvasId, kDrawBufferId, kContextId, kBufferId, kTextId, kCaptionId, kCacheId,
    kCache2Id, kCanvasId, kRendererId, kNewDrawBufferId, kModeFormatId, kListenerId0 = 0xe0200
};
constexpr unsigned kListeners = 3;
constexpr uint32_t kListenEvents[kListeners] = {0xca7ccbbc, 0x2b5745f8, 0x11};
constexpr uint32_t kHeardAs[kListeners] = {0x61, 0x62, 0x63};

drawfx::Fake gCom, gDevice, gSurface, gSurfaceDX, gFormat, gArgb, gEnum, gFormats, gFonts, gSystemDX, gWindow,
    gWindowCanvas, gDrawBuffer, gContext, gBuffer, gText, gCaption, gCache, gCache2, gCanvas, gRenderer,
    gNewDrawBuffer, gModeFormat, gListener[kListeners], gSource;

uint8_t gMarks[16]; // tokens the stand-ins hand out (ranges, formats)
struct Data {
    uint32_t message[4]; // event data (+4 the device state)
    uint32_t words[8];   // out-parameters
    uint32_t settings[7];
} gData;

// ---- the blocks ----
alignas(16) uint8_t gSysBlock[kSize];
uint8_t *gSys = gSysBlock;
uint8_t *gBuf = nullptr; // the side's back buffer (heap)

uint32_t Word(const void *p) { return uint32_t(uintptr_t(p)); }
uint32_t Norm(const void *p) { return drawfx::Norm(Word(p)); }
template <class T> void Put(void *block, size_t offset, T value) { std::memcpy(static_cast<uint8_t *>(block) + offset, &value, sizeof(T)); }
template <class T> T Get(const void *block, size_t offset)
{
    T value;
    std::memcpy(&value, static_cast<const uint8_t *>(block) + offset, sizeof(T));
    return value;
}

// ---- a case ----
enum Op : uint32_t {
    kQueryInterface, kAddRef, kRelease, kOnConnection, kOnState, kOnOther, kOnQuery, kNotify, kAsk, kConnect,
    kDisconnect, kIsActive, kEventCount, kEventIds, kEventAttribute, kEmptySlots, kStubs, kDrawBuffer, kColour,
    kMetrics, kWindowSlots, kServiceSlots, kInit, kShutdown, kDestruct, kReleaseLast, kDelete,
    // the back buffer
    kBufQueryInterface, kBufRefs, kBufSlots, kBufConvert, kBufCache, kBufReady, kBufRaw, kBufTake, kBufDelete, kOpCount
};
bool SystemGone(uint32_t op) { return op == kReleaseLast || op == kDelete; }
bool Destroys(uint32_t op) { return op == kDestruct || op == kReleaseLast || op == kDelete; }

struct Case {
    uint32_t op;
    uint8_t sys[kSize];
    uint8_t buf[kBufSize];
    bool hasDrawBuffer, hasSurface, hasCache, noFramework, makeDrawBuffer, makeContext, makeRenderer;
    uint32_t connections, listenerAnswers;
    uint32_t best[3];            // 0x18a9b0's answers in turn: 0 null, 1 / 2 a format
    uint32_t desktopOk, closestOk, setOk, fullscreen, deviceCount, isCreated, accept, slot36, bits, planes;
    uint32_t sizes[2];           // 0x154a80's answers
    uint32_t mode[5];            // the closest mode 0x18b490 writes
    Data data;
    uint32_t args[8];
};

// ---- stand-ins ----
struct Script {
    const Case *c = nullptr;
    unsigned best = 0;
} gScript;

void *__cdecl GameNew(size_t size) { return ::operator new(size); }
void __cdecl GameDelete(void *p) { ::operator delete(p); }

void *StandFramework() { return &gCom; }
void *__cdecl StandDevice()
{
    drawfx::Note("current device", 0u);
    return &gDevice;
}
void *__cdecl StandEnum()
{
    drawfx::Note("current enum", 0u);
    return &gEnum;
}
void *__cdecl StandFormats()
{
    drawfx::Note("formats manager", 0u);
    return &gFormats;
}
struct Range {
    uint32_t w[3];
};
void *__cdecl StandBest(Range r, uint32_t bits, uint32_t flags)
{
    drawfx::Note("best format", {drawfx::Norm(r.w[0]), drawfx::Norm(r.w[1]), drawfx::Norm(r.w[2]), bits, flags});
    const unsigned at = gScript.best++;
    const uint32_t which = at < 3 ? gScript.c->best[at] : 0;
    return which ? &gMarks[8 + which] : nullptr;
}
bool __cdecl StandDesktopMode(int32_t device, void *format, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    drawfx::Note("desktop mode", {uint32_t(device), Norm(format), a, b, c, d, e});
    return gScript.c->desktopOk != 0;
}
bool __cdecl StandClosest(uint32_t *mode, Range r, uint32_t w, uint32_t h, uint32_t minB, uint32_t maxB, uint32_t minR, uint32_t maxR)
{
    drawfx::Note("closest mode", {mode[0], mode[1], mode[2], mode[3], mode[4], drawfx::Norm(r.w[0]), drawfx::Norm(r.w[1]),
                                  drawfx::Norm(r.w[2]), w, h, minB, maxB, minR, maxR});
    if (!gScript.c->closestOk) return false;
    std::memcpy(mode, gScript.c->mode, sizeof(gScript.c->mode));
    return true;
}
bool __cdecl StandSetMode(int32_t device, const uint32_t *mode, void *display, void *back, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    drawfx::Note("set mode", {uint32_t(device), mode[0], mode[1], mode[2], mode[3], mode[4], Norm(display), Norm(back), a, b, c, d});
    return gScript.c->setOk != 0;
}
uint32_t __stdcall StandDesktopSize(uint32_t which)
{
    drawfx::Note("desktop size", which);
    return gScript.c->sizes[which & 1];
}

// ---- handlers ----
// The framework's GetClassObject: the draw buffer, the draw context, the font renderer, a buffer (or nothing).
uint32_t ComClassObject(drawfx::Fake &, const uint32_t *a)
{
    void **const out = reinterpret_cast<void **>(uintptr_t(a[2]));
    drawfx::Fake *made = nullptr;
    switch (a[0]) {
    case 0xab300b20: made = gScript.c->makeDrawBuffer ? &gNewDrawBuffer : nullptr; break;
    case 0xae6320e: made = gScript.c->makeContext ? &gContext : nullptr; break;
    case 0x8b3255f9: made = gScript.c->makeRenderer ? &gRenderer : nullptr; break;
    case 0xc470d325: made = &gBuffer; break;
    }
    if (made) {
        ++made->refs;
        *out = made;
    }
    return made ? 1u : 0x77u;
}
uint32_t EnumMode(drawfx::Fake &, const uint32_t *a)
{
    uint32_t *const out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    out[0] = gScript.c->args[5];
    out[1] = gScript.c->args[6];
    out[2] = 0x3f800000;
    out[3] = 60;
    out[4] = Word(&gModeFormat);
    return a[0];
}
// The range slots: three tokens per slot.
uint32_t EnumRange(drawfx::Fake &, const uint32_t *a, unsigned slot)
{
    uint32_t *const out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    for (unsigned i = 0; i < 3; ++i) out[i] = Word(&gMarks[slot % 3 * 3 + i]);
    return a[0];
}
uint32_t EnumModes(drawfx::Fake &f, const uint32_t *a) { return EnumRange(f, a, 7); }
uint32_t EnumFormats(drawfx::Fake &f, const uint32_t *a) { return EnumRange(f, a, 8); }
uint32_t EnumColors(drawfx::Fake &f, const uint32_t *a) { return EnumRange(f, a, 10); }
uint32_t BufferColorType(drawfx::Fake &, const uint32_t *a)
{
    uint32_t *const out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    out[0] = gScript.c->args[3];
    out[1] = gScript.c->args[4];
    return a[0];
}
uint32_t ArgbFromNative(drawfx::Fake &, const uint32_t *a)
{
    *reinterpret_cast<uint32_t *>(uintptr_t(a[0])) = a[1] * 0x9e3779b1u;
    return a[0];
}
uint32_t BufferToRGB(drawfx::Fake &, const uint32_t *a)
{
    for (unsigned i = 1; i < 4; ++i) *reinterpret_cast<uint8_t *>(uintptr_t(a[i])) = uint8_t(a[0] >> (8 * i));
    return 1;
}
uint32_t CursorPos(drawfx::Fake &, const uint32_t *a)
{
    auto *p = reinterpret_cast<POINT *>(uintptr_t(a[0]));
    p->x = LONG(gScript.c->args[5] % 4000);
    p->y = LONG(gScript.c->args[6] % 3000);
    return 1;
}
uint32_t ToClient(drawfx::Fake &, const uint32_t *a)
{
    auto *p = reinterpret_cast<POINT *>(uintptr_t(a[1]));
    drawfx::Note("point", {uint32_t(p->x), uint32_t(p->y)});
    p->x -= 17;
    p->y -= 31;
    return 1;
}
uint32_t DeviceCaps(drawfx::Fake &, const uint32_t *a) { return a[1] == PLANES ? gScript.c->planes : gScript.c->bits; }
uint32_t ListenerOnQuery(drawfx::Fake &self, const uint32_t *) { return self.value[0]; }
uint32_t SourceQuery(drawfx::Fake &self, const uint32_t *a)
{
    if (a[0] != nGZEvent::cIAbstractBehaviorType::kIid || !a[1]) return 0;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = self.owner;
    return 1;
}

// ---- the registry: a map placed over the original's static, its guards marked done ----
char gNames[3][8];
const uint32_t kRegistryIds[] = {0x10, 0xca7ccbbc, 0xa0000000};

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
        for (unsigned i = 0; i < 3; ++i) {
            std::snprintf(gNames[i], sizeof(gNames[i]), "g%u", i);
            (*map_)[kRegistryIds[i]] = {kRegistryIds[i], gNames[i], i, 0x100u + i, 0x200u + i, 0x300u + i};
        }
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

class Cells {
public:
    Cells() : manager_(*GameAddress<uint32_t>(kEventManagerCell)) {}
    ~Cells() { *GameAddress<uint32_t>(kEventManagerCell) = manager_; }
    Cells(const Cells &) = delete;
    Cells &operator=(const Cells &) = delete;

private:
    uint32_t manager_;
};

struct StandIns {
    shape::CodePatch gameNew{0x824632, reinterpret_cast<const void *>(&GameNew)};
    shape::CodePatch gameDelete{0x824624, reinterpret_cast<const void *>(&GameDelete)};
    shape::StandIn framework{0x1ae18d, &::GetDirectorFrameworkObject, &StandFramework};
    shape::StandIn device{0x18ae40, &nGZGraphic4::GetCurrentDeviceCached, &StandDevice};
    shape::StandIn devices{0x18b470, &nGZGraphic4::GetCurrentDeviceEnum, &StandEnum};
    shape::StandIn formats{0x18acd0, &nGZGraphic4::GetPixelFormatsManagerCached, &StandFormats};
    shape::StandIn best{0x18a9b0, &nGZGraphic4::FindClosestColorFormat, &StandBest};
    shape::StandIn desktopMode{0x18b440, &nGZGraphic4::SetDeviceDesktopMode, &StandDesktopMode};
    shape::StandIn closest{0x18b490, &nGZGraphic4::FindClosestDisplayMode, &StandClosest};
    shape::StandIn setMode{0x18b250, &nGZGraphic4::SetDeviceMode, &StandSetMode};
    shape::StandIn desktopSize{0x154a80, &nMultiMon::GetSystemMetricsStub, &StandDesktopSize};
};

// ---- seeding ----
uint32_t Pick(std::mt19937 &gen, unsigned oneIn) { return gen() % oneIn == 0; }

void SeedCase(std::mt19937 &gen, Case &c, uint32_t op)
{
    std::memset(&c, 0, sizeof(c));
    c.op = op;
    for (uint8_t &b : c.sys) b = uint8_t(gen());
    for (uint8_t &b : c.buf) b = uint8_t(gen());
    // The system's count: 2-5 (1 for the last Release), the active bit either way.
    Put<uint32_t>(c.sys, 4, (op == kReleaseLast ? 1 : 2 + gen() % 4) | (Pick(gen, 2) ? 0x80000000u : 0));
    if (op == kRelease && Pick(gen, 3)) Put<uint32_t>(c.sys, 4, (Get<uint32_t>(c.sys, 4) & 0x80000000u) | 2);
    c.sys[0x14] = uint8_t(Pick(gen, 2));
    c.sys[0x3c] = uint8_t(Pick(gen, 2));
    if (Pick(gen, 4)) Put<uint32_t>(c.sys, 0x20, 0); // no window size
    else Put<uint32_t>(c.sys, 0x20, 640 + gen() % 1000);
    // The back buffer's count: 1 (freed with the system) now and then for the destroying ops, else 2-4.
    const bool lastBuf = (Destroys(op) || op == kBufDelete || op == kBufRefs) && Pick(gen, 2);
    Put<uint32_t>(c.buf, 4, lastBuf ? 1 : 2 + gen() % 3);
    c.hasDrawBuffer = !Pick(gen, 2);
    // The back buffer's conversions use its surface without a check (0x1472f3 / 0x147273), and so does the raw
    // face's QuerySurface (0x1478f3): those ops always have one.
    c.hasSurface = op == kColour || op == kBufConvert || op == kBufRaw || !Pick(gen, 4);
    c.hasCache = !Pick(gen, 3);
    c.noFramework = (op == kShutdown) && Pick(gen, 4);
    // The original uses both without a check (0x147c6c, 0x147c8d dereference them): the framework always makes them.
    c.makeDrawBuffer = true;
    c.makeContext = true;
    c.makeRenderer = !Pick(gen, 3);
    c.connections = gen() % 64;
    c.listenerAnswers = gen() % 8;
    for (uint32_t &b : c.best) b = Pick(gen, 5) ? 0 : 1 + gen() % 2;
    c.desktopOk = !Pick(gen, 3);
    c.closestOk = !Pick(gen, 4);
    c.setOk = !Pick(gen, 4);
    c.fullscreen = gen() % 2;
    c.deviceCount = Pick(gen, 2) ? 0 : 1 + gen() % 3;
    c.isCreated = gen() % 2;
    c.accept = gen() % 2;
    c.slot36 = gen() % 2;
    c.bits = 8 + gen() % 25;
    c.planes = 1 + gen() % 2;
    c.sizes[0] = gen() % 4000;
    c.sizes[1] = gen() % 3000;
    for (uint32_t &w : c.mode) w = gen();
    for (uint32_t &w : c.data.message) w = gen();
    for (uint32_t &w : c.data.words) w = gen();
    for (uint32_t &w : c.data.settings) w = gen();
    if (op == kOnState) c.data.message[1] = gen() % 5;
    for (uint32_t &w : c.args) w = gen();
}

// ---- a side's world ----
cConnectionManager *gManager = nullptr;

struct VTables {
    void *primary, *face, *service, *buf, *bufRaw;
};

void ResetFakes(const Case &c)
{
    gCom.Reset<kComIface>(kComId);
    gCom.On(3, &ComClassObject);
    gDevice.Reset<kDeviceIface>(kDeviceId);
    gDevice.Returns(37, c.fullscreen);
    gDevice.Returns(39, &gSurface);
    gDevice.Returns(42, &gMarks[15]);
    gSurface.Reset<kSurfaceIface>(kSurfaceId);
    gSurface.AddFace(0x5b71b5ce, gSurfaceDX);
    gSurface.Returns(6, c.isCreated);
    gSurface.Returns(20, c.args[5] % 4096);
    gSurface.Returns(21, c.args[6] % 4096);
    gSurface.Returns(22, &gFormat);
    gSurfaceDX.Reset<kSurfaceDXIface>(kSurfaceDXId);
    gSurfaceDX.Returns(0, c.args[7] % 2);
    gSurfaceDX.Returns(4, &gSurface);
    gFormat.Reset<kFormatIface>(kFormatId);
    gFormat.AddFace(0x9ac38991, gArgb);
    gArgb.Reset<kArgbIface>(kArgbId);
    gArgb.Returns(16, c.args[2]);
    gArgb.On(17, &ArgbFromNative);
    gEnum.Reset<kEnumIface>(kEnumId);
    gEnum.On(6, &EnumMode);
    gEnum.On(7, &EnumModes);
    gEnum.On(8, &EnumFormats);
    gEnum.On(10, &EnumColors);
    gModeFormat.Reset<kFormatIface>(kModeFormatId);
    gModeFormat.Returns(6, c.bits);
    gFormats.Reset<kFormatsIface>(kFormatsId);
    gFormats.Returns(5, &gMarks[12]);
    gFonts.Reset<kFontsIface>(kFontsId);
    gSystemDX.Reset<kSystemDXIface>(kSystemDXId);
    gSystemDX.Returns(23, c.deviceCount);
    gWindow.Reset<kWindowIface>(kWindowId);
    gWindow.Returns(36, c.slot36);
    gWindowCanvas.Reset<kWindowCanvasIface>(kWindowCanvasId);
    gWindowCanvas.Returns(40, 0x4b000010u + c.args[0] % 4);
    gDrawBuffer.Reset<kDrawBufferIface>(kDrawBufferId);
    gDrawBuffer.Returns(3, &gBuffer);
    gNewDrawBuffer.Reset<kDrawBufferIface>(kNewDrawBufferId);
    gNewDrawBuffer.Returns(3, &gBuffer);
    gContext.Reset<kContextIface>(kContextId);
    gBuffer.Reset<kBufferIface>(kBufferId);
    gBuffer.On(15, &BufferColorType);
    gBuffer.Returns(30, c.args[1]);
    gBuffer.On(31, &BufferToRGB);
    gText.Reset<kStringIface>(kTextId);
    gText.Returns(5, "the text");
    gCaption.Reset<kStringIface>(kCaptionId);
    gCaption.Returns(5, "the caption");
    gCache.Reset<kCacheIface>(kCacheId);
    gCache2.Reset<kCacheIface>(kCache2Id);
    gCache2.Returns(3, c.accept);
    gCanvas.Reset<kPlainIface>(kCanvasId);
    gRenderer.Reset<kPlainIface>(kRendererId);
    for (unsigned i = 0; i < kListeners; ++i) {
        gListener[i].Reset<kListenerIface>(kListenerId0 + i);
        gListener[i].AddFace(nGZEvent::cIAbstractBehaviorType::kIid, gListener[i]);
        gListener[i].On(4, &ListenerOnQuery);
        gListener[i].value[0] = (c.listenerAnswers >> i) & 1;
    }
    gSource.Reset<kListenerIface>(kListenerId0 + 9);
    gSource.On(0, &SourceQuery);
}

void Answers(d3dfx::ErrorLog &errors)
{
    errors.Answer(0x20673960, 0x673960, gFonts);
    errors.Answer(0x85522803, 0xa55227bb, gSystemDX);
    errors.Answer(0xfb6372ee, 0x5b637209, gWindow);
    errors.Answer(0xfb6372ee, 0xbb6377fa, gWindowCanvas);
    errors.Answer(0xa3460deb, 0x66b217a1, gCanvas);
}

void BuildSide(const Case &c, d3dfx::ErrorLog &errors, impfx::Imports &imp, VTables vt)
{
    drawfx::ResetAll();
    imp.BeginSide();
    gScript = {&c, 0};
    std::memcpy(gSys, c.sys, kSize);
    gBuf = static_cast<uint8_t *>(::operator new(kBufSize));
    std::memcpy(gBuf, c.buf, kBufSize);
    gData = c.data;
    ResetFakes(c);
    Answers(errors);
    Put<void *>(gSys, 0x18, gBuf);
    Put<void *>(gSys, 0x1c, c.hasDrawBuffer ? &gDrawBuffer : nullptr);
    Put<void *>(gBuf, 0xc, c.hasSurface ? &gSurfaceDX : nullptr);
    Put<void *>(gBuf, 0x30, c.hasCache ? &gCache : nullptr);
    drawfx::AddRange(gSys, kSize, kSysTag);
    drawfx::AddRange(gBuf, kBufSize, kBufTag);
    drawfx::AddRange(&gData, sizeof(gData), kDataTag);
    drawfx::AddRange(gMarks, sizeof(gMarks), kMarkTag);
    gManager = cConnectionManagerAdapters::Construct(::operator new(cConnectionManager::kSize), nullptr);
    drawfx::AddRange(gManager, cConnectionManager::kSize, kConnTag);
    *GameAddress<void *>(kEventManagerCell) = gManager;
    gSource.Embed(gSys);
    for (unsigned i = 0; i < kListeners; ++i)
        if ((c.connections >> i) & 1)
            static_cast<nGZEvent::cIConnectionManager *>(gManager)->Connect(
                reinterpret_cast<cIGZUnknown *>(gSys), kListenEvents[i], reinterpret_cast<cIGZUnknown *>(&gListener[i]),
                kHeardAs[i]);
    for (unsigned i = 0; i < kListeners; ++i)
        if ((c.connections >> (3 + i)) & 1)
            static_cast<nGZEvent::cIConnectionManager *>(gManager)->Connect(
                reinterpret_cast<cIGZUnknown *>(gSys), kListenEvents[i],
                reinterpret_cast<cIGZUnknown *>(&gListener[(i + 1) % kListeners]), kHeardAs[(i + 1) % kListeners]);
    Put<void *>(gSys, 0, vt.primary);
    Put<void *>(gSys, 8, vt.face);
    Put<void *>(gSys, 0xc, vt.service);
    Put<void *>(gBuf, 0, vt.buf);
    Put<void *>(gBuf, 8, vt.bufRaw);
    drawfx::gLog.clear();
}

void DestroySide()
{
    cConnectionManagerAdapters::Destruct(gManager, nullptr);
    ::operator delete(gManager);
    gManager = nullptr;
}

void NoteRefs()
{
    const drawfx::Fake *all[] = {&gCom, &gDevice, &gSurface, &gSurfaceDX, &gFormat, &gArgb, &gEnum, &gFormats, &gFonts,
                                 &gSystemDX, &gWindow, &gWindowCanvas, &gDrawBuffer, &gContext, &gBuffer, &gText,
                                 &gCaption, &gCache, &gCache2, &gCanvas, &gRenderer, &gNewDrawBuffer, &gModeFormat,
                                 &gListener[0], &gListener[1], &gListener[2]};
    for (const drawfx::Fake *f : all) drawfx::Note("refs", {f->id, uint32_t(f->refs)});
}

void NoteConnections()
{
    auto *m = static_cast<nGZEvent::cIConnectionManager *>(gManager);
    drawfx::Note("connected", m->HasConnectionsFrom(reinterpret_cast<cIGZUnknown *>(gSys)));
    for (unsigned i = 0; i < kListeners; ++i)
        drawfx::Note("from", {i, m->CountFrom(reinterpret_cast<cIGZUnknown *>(gSys), kListenEvents[i])});
    drawfx::Note("from dx", m->CountFrom(reinterpret_cast<cIGZUnknown *>(&gSystemDX), cGZGraphicSystem::kDeviceStateEvent));
}

void NoteIdRange(nGZEvent::cAttributeIDRange range)
{
    unsigned steps = 0;
    while (steps < 8 && !range.mCurrent->IsEqual(range.mEnd)) {
        drawfx::Note("id", range.mCurrent->Get());
        range.mCurrent->Next();
        ++steps;
    }
    nGZEvent::ReleaseIteratorRange(reinterpret_cast<cIGZUnknown *const *>(&range));
}

uint32_t EventType(const Case &c)
{
    switch (c.op) {
    case kOnConnection: return cGZGraphicSystem::kConnectionChangedEvent;
    case kOnState: return cGZGraphicSystem::kDeviceStateEvent;
    default: return c.args[0];
    }
}

const uint32_t kIids[] = {0x73283c, 0x287fb697, 1, 0x3b253dc7, 0xfb859324, 0x20732180, 0};
const uint32_t kBufIids[] = {0xe8c95947, 1, 0x20732180, 0x73283c, 0};

// ---- one operation, on the original (at its RVAs) or ours (through our vtables) ----
template <class F> F At(difftest::Context &ctx, uint32_t rva) { return ctx.Original<F>(rva); }
using W = uint32_t(__thiscall *)(void *);
using B = bool(__thiscall *)(void *);
using WW = uint32_t(__thiscall *)(void *, uint32_t);

void Note(const char *label, uint32_t value) { drawfx::Note(label, value); }

// The +8 face's simple slots (all but the window's, the colour ones and the metrics), with the call's results.
void RunStubsOriginal(difftest::Context &ctx, void *f, const uint32_t *a)
{
    void *out = nullptr;
    Note("3", At<uint32_t(__thiscall *)(void *, void **)>(ctx, 0x1473c0)(f, &out));
    Note("3 out", Norm(out));
    Note("4", At<uint32_t(__thiscall *)(void *, uint32_t, void **)>(ctx, 0x1473a0)(f, a[0] % 2 ? 0x20732180 : a[1], &out));
    Note("5", At<B>(ctx, 0x147480)(f));
    At<void(__thiscall *)(void *, uint32_t)>(ctx, 0x300a0)(f, a[2]);
    Note("7", At<W>(ctx, 0x6c6b0)(f));
    Note("11", Norm(At<void *(__thiscall *)(void *, void *)>(ctx, 0x1478d0)(f, gData.words)));
    Note("12", At<B>(ctx, 0x148060)(f));
    Note("14", Norm(At<void *(__thiscall *)(void *)>(ctx, 0x1477c0)(f)));
    Note("15", At<bool(__thiscall *)(void *, uint32_t, uint32_t)>(ctx, 0x1477b0)(f, a[1], a[2]));
    Note("16", At<WW>(ctx, 0x147140)(f, a[3]));
    Note("22", At<WW>(ctx, 0x12e40)(f, a[3]));
    Note("24", At<uint32_t(__thiscall *)(void *, uint32_t, uint32_t)>(ctx, 0x6c6a0)(f, a[3], a[4]));
    At<void(__thiscall *)(void *)>(ctx, 0x140fe0)(f);
    Note("26", At<bool(__thiscall *)(void *, uint32_t *, uint32_t *)>(ctx, 0x147a80)(f, &gData.words[0], &gData.words[1]));
    Note("28", At<bool(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(ctx, 0x147a70)(f, a[1], a[2], a[3]));
    Note("32", At<B>(ctx, 0x144910)(f));
    At<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(ctx, 0x377e0)(f, a[1], a[2], a[3]);
    At<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(ctx, 0x147130)(f, 1, 2, 3, 4, 5, 6);
    At<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(ctx, 0x147120)(f, 1, 2, 3, 4, 5, 6, 7);
    Note("50", At<float *(__thiscall *)(void *, float *)>(ctx, 0x147900)(f, reinterpret_cast<float *>(&gData.words[2])) == reinterpret_cast<float *>(&gData.words[2]));
    At<void(__thiscall *)(void *, const uint32_t *)>(ctx, 0x1482e0)(f, gData.settings);
    At<void(__thiscall *)(void *, bool)>(ctx, 0x148300)(f, a[4] % 2 != 0);
}

void RunStubsOurs(cIGZGraphicSystem *s, const uint32_t *a)
{
    void *out = nullptr;
    Note("3", s->CreateBuffer(&out));
    Note("3 out", Norm(out));
    Note("4", s->CreateBufferAs(a[0] % 2 ? 0x20732180 : a[1], &out));
    Note("5", s->Slot5());
    s->Slot6(a[2]);
    Note("7", s->Slot7());
    Note("11", Norm(s->GetZeroMetrics(reinterpret_cast<cGZGraphicSystemMetrics *>(gData.words))));
    Note("12", s->IsFullscreen());
    Note("14", Norm(s->GetCanvas()));
    Note("15", s->Slot15(a[1], a[2]));
    Note("16", s->Slot16(a[3]));
    Note("22", s->Slot22(a[3]));
    Note("24", s->Slot24(a[3], a[4]));
    s->Slot25();
    Note("26", s->GetScale(&gData.words[0], &gData.words[1]));
    Note("28", s->Slot28(a[1], a[2], a[3]));
    Note("32", s->UseSystemCursor());
    s->SetCursor(reinterpret_cast<void *>(uintptr_t(a[1])), int32_t(a[2]), int32_t(a[3]));
    s->Slot37(1, 2, 3, 4, 5, 6);
    s->Slot47(1, 2, 3, 4, 5, 6, 7);
    Note("50", s->GetRate50(reinterpret_cast<float *>(&gData.words[2])) == reinterpret_cast<float *>(&gData.words[2])); // returns out
    s->SetSettings(reinterpret_cast<const cGZGraphicSystemSettings *>(gData.settings));
    s->SetFullscreen(a[4] % 2 != 0);
}

// The other stubs of the +8 face (each slot of ours, against the shared body the original's vtable has there).
void RunMoreStubsOriginal(difftest::Context &ctx, void *f, const uint32_t *a)
{
    Note("13", At<B>(ctx, 0x147480)(f));
    Note("17", At<B>(ctx, 0x147480)(f));
    Note("23", At<WW>(ctx, 0x12e40)(f, a[3]));
    Note("27", At<W>(ctx, 0x6c6b0)(f));
    Note("29", At<bool(__thiscall *)(void *, uint32_t, uint32_t)>(ctx, 0x1477b0)(f, a[1], a[2]));
    Note("30", At<WW>(ctx, 0x12e40)(f, a[3]));
    for (int i = 38; i <= 46; ++i)
        At<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(ctx, 0x147130)(f, 1, 2, 3, 4, 5, 6);
    At<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(ctx, 0x147120)(f, 1, 2, 3, 4, 5, 6, 7);
    for (int i = 0; i < 9; ++i) At<void(__thiscall *)(void *, uint32_t)>(ctx, 0x300a0)(f, a[i % 8]);
    Note("51", At<float *(__thiscall *)(void *, float *)>(ctx, 0x147900)(f, reinterpret_cast<float *>(&gData.words[3])) == reinterpret_cast<float *>(&gData.words[3]));
    Note("52", At<float *(__thiscall *)(void *, float *)>(ctx, 0x147900)(f, reinterpret_cast<float *>(&gData.words[4])) == reinterpret_cast<float *>(&gData.words[4]));
    for (int i = 0; i < 3; ++i) Note("58", At<W>(ctx, 0x6c6b0)(f));
    for (int i = 0; i < 4; ++i) At<void(__thiscall *)(void *)>(ctx, 0x140fe0)(f);
}

void RunMoreStubsOurs(cIGZGraphicSystem *s, const uint32_t *a)
{
    Note("13", s->Slot13());
    Note("17", s->Slot17());
    Note("23", s->Slot23(a[3]));
    Note("27", s->Slot27());
    Note("29", s->Slot29(a[1], a[2]));
    Note("30", s->ShowCursor(a[3]));
    s->Slot38(1, 2, 3, 4, 5, 6);
    s->Slot39(1, 2, 3, 4, 5, 6);
    s->Slot40(1, 2, 3, 4, 5, 6);
    s->Slot41(1, 2, 3, 4, 5, 6);
    s->Slot42(1, 2, 3, 4, 5, 6);
    s->Slot43(1, 2, 3, 4, 5, 6);
    s->Slot44(1, 2, 3, 4, 5, 6);
    s->Slot45(1, 2, 3, 4, 5, 6);
    s->Slot46(1, 2, 3, 4, 5, 6);
    s->Slot48(1, 2, 3, 4, 5, 6, 7);
    s->Slot49(a[0]);
    s->Slot54(a[1]);
    s->Slot56(a[2]);
    s->Slot57(a[3]);
    s->Slot61(a[4]);
    s->Slot62(a[5]);
    s->Slot63(a[6]);
    s->Slot64(a[7]);
    s->Slot6(a[0]);
    Note("51", s->GetRate51(reinterpret_cast<float *>(&gData.words[3])) == reinterpret_cast<float *>(&gData.words[3])); // returns out
    Note("52", s->GetRate52(reinterpret_cast<float *>(&gData.words[4])) == reinterpret_cast<float *>(&gData.words[4])); // returns out
    Note("58", s->Slot58());
    Note("58", s->Slot59());
    Note("58", s->Slot60());
    s->Slot65();
    s->Slot66();
    s->Slot67();
    s->Slot68();
}

void RunOriginal(difftest::Context &ctx, const Case &c)
{
    void *const self = gSys;
    void *const face = gSys + 8;
    void *const service = gSys + 0xc;
    void *const buf = gBuf;
    const uint32_t *a = c.args;
    switch (c.op) {
    case kQueryInterface: {
        const uint32_t iid = kIids[a[0] % 7] ? kIids[a[0] % 7] : a[1];
        void *out = reinterpret_cast<void *>(uintptr_t(0x1234));
        const bool ok = At<bool(__thiscall *)(void *, uint32_t, void **)>(ctx, 0x1483c0)(self, iid, a[2] % 5 || iid == 0x73283c || iid == 0x287fb697 ? &out : nullptr);
        drawfx::Note("ret", {ok, Norm(out)});
        break;
    }
    case kAddRef: Note("ret", At<W>(ctx, 0x146a20)(self)); break;
    case kRelease: Note("ret", At<W>(ctx, 0x1487e0)(self)); break;
    case kOnConnection:
    case kOnState:
    case kOnOther:
        At<void(__thiscall *)(void *, uint32_t, uint32_t, void *, uint32_t)>(ctx, 0x147ae0)(self, EventType(c), a[1], gData.message, a[2]);
        break;
    case kOnQuery: Note("ret", At<bool(__thiscall *)(void *, uint32_t, uint32_t, void *)>(ctx, 0x147ce0)(self, kListenEvents[a[0] % 3], a[1], gData.words)); break;
    case kNotify: At<void(__thiscall *)(void *, uint32_t, uint32_t, void *)>(ctx, 0x148890)(self, kListenEvents[a[0] % 3], a[1], gData.words); break;
    case kAsk: Note("ret", At<bool(__thiscall *)(void *, uint32_t, uint32_t, void *)>(ctx, 0x1474c0)(self, kListenEvents[a[0] % 3], a[1], gData.words)); break;
    case kConnect: At<void(__thiscall *)(void *, uint32_t, void *, uint32_t)>(ctx, 0x147150)(self, kListenEvents[a[0] % 3], &gListener[a[1] % 3], a[2] % 4); break;
    case kDisconnect: At<void(__thiscall *)(void *, uint32_t, void *, uint32_t)>(ctx, 0x1473e0)(self, kListenEvents[a[0] % 3], &gListener[a[0] % 3], kHeardAs[a[0] % 3]); break;
    case kIsActive: Note("ret", At<W>(ctx, 0x147cf0)(self)); break;
    case kEventCount: Note("ret", At<W>(ctx, 0x148210)(self)); break;
    case kEventIds: {
        nGZEvent::cAttributeIDRange range;
        At<void *(__thiscall *)(void *, nGZEvent::cAttributeIDRange *)>(ctx, 0x146ce0)(self, &range);
        NoteIdRange(range);
        break;
    }
    case kEventAttribute: {
        nGZEvent::cEventAttribute r;
        const uint32_t ids[] = {kRegistryIds[0], kRegistryIds[1], kRegistryIds[2], a[1]};
        At<void *(__thiscall *)(void *, nGZEvent::cEventAttribute *, uint32_t)>(ctx, 0x147030)(self, &r, ids[a[0] % 4]);
        drawfx::Note("attribute", {r.mId, Word(r.mName), r.mWord08, r.mWord0C, r.mWord10, r.mWord14});
        break;
    }
    case kEmptySlots: At<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(ctx, 0x377e0)(self, a[0], a[1], a[2]); break;
    case kStubs:
        if (a[7] % 2) RunStubsOriginal(ctx, face, a);
        else RunMoreStubsOriginal(ctx, face, a);
        break;
    case kDrawBuffer: Note("8", Norm(At<void *(__thiscall *)(void *)>(ctx, 0x146a00)(face))); break;
    case kColour: {
        Note("9", Norm(At<void *(__thiscall *)(void *, void *)>(ctx, 0x147910)(face, &gData.words[0])));
        Note("34", At<uint32_t(__thiscall *)(void *, uint8_t, uint8_t, uint8_t)>(ctx, 0x147360)(face, uint8_t(a[1]), uint8_t(a[2]), uint8_t(a[3])));
        Note("35", At<bool(__thiscall *)(void *, uint32_t *, uint8_t, uint8_t, uint8_t)>(ctx, 0x147370)(face, &gData.words[2], uint8_t(a[2]), uint8_t(a[3]), uint8_t(a[4])));
        Note("36", At<bool(__thiscall *)(void *, uint32_t, uint8_t *, uint8_t *, uint8_t *)>(ctx, 0x1472e0)(
                       face, a[5], reinterpret_cast<uint8_t *>(&gData.words[3]), reinterpret_cast<uint8_t *>(&gData.words[3]) + 1,
                       reinterpret_cast<uint8_t *>(&gData.words[4])));
        break;
    }
    case kMetrics: Note("10", Norm(At<void *(__thiscall *)(void *, void *)>(ctx, 0x147930)(face, &gData.words[1]))); break;
    case kWindowSlots:
        switch (a[0] % 5) {
        case 0: Note("18", At<B>(ctx, 0x148270)(face)); break;
        case 1: Note("19", At<B>(ctx, 0x148b20)(face)); break;
        case 2: Note("20", At<B>(ctx, 0x147fe0)(face)); break;
        case 3: Note("21", At<int(__thiscall *)(void *, void *, void *)>(ctx, 0x148120)(face, &gText, &gCaption)); break;
        case 4: Note("33", At<bool(__thiscall *)(void *, int32_t *, int32_t *)>(ctx, 0x147830)(face, reinterpret_cast<int32_t *>(&gData.words[5]), reinterpret_cast<int32_t *>(&gData.words[6]))); break;
        }
        break;
    case kServiceSlots: {
        void *out = nullptr;
        const uint32_t iid = kIids[a[0] % 7] ? kIids[a[0] % 7] : a[1];
        drawfx::Note("qi", {At<bool(__thiscall *)(void *, uint32_t, void **)>(ctx, 0x148431)(service, iid, &out), Norm(out)});
        drawfx::Note("qi8", {At<bool(__thiscall *)(void *, uint32_t, void **)>(ctx, 0x148429)(face, iid, &out), Norm(out)});
        Note("addref", At<W>(ctx, 0x146a65)(service));
        Note("addref8", At<W>(ctx, 0x146a5d)(face));
        Note("release", At<W>(ctx, 0x148875)(service));
        Note("release8", At<W>(ctx, 0x14886d)(face));
        Note("id", At<W>(ctx, 0x147a30)(service));
        At<void(__thiscall *)(void *, uint32_t)>(ctx, 0x1489e0)(service, a[2]);
        Note("id", At<W>(ctx, 0x147a30)(service));
        Note("priority", At<W>(ctx, 0x147a40)(service));
        Note("running", At<B>(ctx, 0x1480a0)(service));
        At<void(__thiscall *)(void *, bool)>(ctx, 0x1489f0)(service, a[3] % 2 != 0);
        Note("running", At<B>(ctx, 0x1480a0)(service));
        Note("tick", At<bool(__thiscall *)(void *, uint32_t)>(ctx, 0x148260)(service, a[4]));
        Note("tick priority", At<W>(ctx, 0x147a40)(service));
        break;
    }
    case kInit: Note("ret", At<B>(ctx, 0x147d10)(service)); break;
    case kShutdown: Note("ret", At<B>(ctx, 0x148a30)(service)); break;
    case kDestruct: At<void *(__thiscall *)(void *, uint32_t)>(ctx, 0x146970)(self, 0); break;
    case kReleaseLast: Note("ret", At<W>(ctx, 0x1487e0)(self)); break;
    case kDelete: At<void *(__thiscall *)(void *, uint32_t)>(ctx, 0x146970)(self, 1); break;
    // ---- the back buffer ----
    case kBufQueryInterface: {
        const uint32_t iid = kBufIids[a[0] % 5] ? kBufIids[a[0] % 5] : a[1];
        void *out = reinterpret_cast<void *>(uintptr_t(0x1234));
        drawfx::Note("qi", {At<bool(__thiscall *)(void *, uint32_t, void **)>(ctx, 0x148370)(buf, iid, a[2] % 5 ? &out : nullptr), Norm(out)});
        drawfx::Note("qi8", {At<bool(__thiscall *)(void *, uint32_t, void **)>(ctx, 0x1483af)(gBuf + 8, iid, &out), Norm(out)});
        break;
    }
    case kBufRefs:
        Note("addref", At<W>(ctx, 0x146a50)(buf));
        Note("addref8", At<W>(ctx, 0x146a55)(gBuf + 8));
        Note("release8", At<W>(ctx, 0x148865)(gBuf + 8));
        Note("release", At<W>(ctx, 0x148860)(buf));
        Note("release", At<W>(ctx, 0x148860)(buf));
        break;
    case kBufSlots: {
        Note("init", At<bool(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t)>(ctx, 0x147110)(buf, 1, 2, 3, 4));
        Note("uninit", At<B>(ctx, 0x147480)(buf));
        Note("lock", At<bool(__thiscall *)(void *, uint32_t)>(ctx, 0x1480b0)(buf, a[1]));
        Note("unlock", At<bool(__thiscall *)(void *, uint32_t)>(ctx, 0x148260)(buf, a[2]));
        Note("locked", At<B>(ctx, 0x147480)(buf));
        Note("w", At<W>(ctx, 0x148ba0)(buf));
        Note("h", At<W>(ctx, 0x147d00)(buf));
        cGZRect r{};
        At<void *(__thiscall *)(void *, cGZRect *)>(ctx, 0x147790)(buf, &r);
        drawfx::Note("rect", {uint32_t(r.left), uint32_t(r.top), uint32_t(r.right), uint32_t(r.bottom)});
        Note("rectp", Norm(At<void *(__thiscall *)(void *)>(ctx, 0x147780)(buf)));
        Note("mem", At<W>(ctx, 0x6c6b0)(buf));
        cGZColorType t{};
        At<void *(__thiscall *)(void *, cGZColorType *)>(ctx, 0x147810)(buf, &t);
        drawfx::Note("type", {t.format, t.bits});
        At<void *(__thiscall *)(void *, cGZColorType *)>(ctx, 0x147810)(buf, &t);
        drawfx::Note("native", {t.format, t.bits});
        Note("bits", At<W>(ctx, 0x147770)(buf));
        Note("bytes", At<W>(ctx, 0x1477a0)(buf));
        Note("fill", At<bool(__thiscall *)(void *, uint32_t, void *)>(ctx, 0x1474b0)(buf, 1, nullptr));
        Note("fillc", At<bool(__thiscall *)(void *, uint32_t, int, int, int, int, void *)>(ctx, 0x147490)(buf, 1, 2, 3, 4, 5, nullptr));
        Note("fillr", At<bool(__thiscall *)(void *, uint32_t, void *, void *)>(ctx, 0x1474a0)(buf, 1, nullptr, nullptr));
        Note("pixel", At<uint32_t(__thiscall *)(void *, uint32_t, uint32_t)>(ctx, 0x6c6a0)(buf, 1, 2));
        At<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(ctx, 0x377e0)(buf, 1, 2, 3);
        if (a[3] % 3 == 0) At<void(__thiscall *)(void *, uint32_t)>(ctx, 0x148a00)(buf, a[4]);
        if (a[3] % 3 == 1) At<void(__thiscall *)(void *)>(ctx, 0x148880)(buf);
        Note("transp", At<B>(ctx, 0x147480)(buf));
        Note("ptransp", At<bool(__thiscall *)(void *, uint32_t, uint32_t)>(ctx, 0x1474b0)(buf, 1, 2));
        Note("tcolour", At<bool(__thiscall *)(void *, uint32_t *)>(ctx, 0x147a50)(buf, &gData.words[0]));
        Note("line", At<bool(__thiscall *)(void *, int, int, int, int, uint32_t, int)>(ctx, 0x147490)(buf, 1, 2, 3, 4, 5, 6));
        Note("blt", At<bool(__thiscall *)(void *, void *, void *, void *, void *)>(ctx, 0x147110)(buf, nullptr, nullptr, nullptr, nullptr));
        Note("rgba", At<uint32_t(__thiscall *)(void *, uint8_t, uint8_t, uint8_t, uint8_t)>(ctx, 0x18a5b0)(buf, 1, 2, 3, 4));
        Note("torgba", At<bool(__thiscall *)(void *, uint32_t, void *, void *, void *, void *)>(ctx, 0x18a5a0)(buf, 1, nullptr, nullptr, nullptr, nullptr));
        Note("bitsp", Norm(At<void *(__thiscall *)(void *)>(ctx, 0x6c6b0)(buf)));
        Note("stride", At<W>(ctx, 0x6c6b0)(buf));
        Note("changes", At<W>(ctx, 0x147800)(buf));
        Note("cache", Norm(At<void *(__thiscall *)(void *)>(ctx, 0x1457d0)(buf)));
        Note("shutdown", At<B>(ctx, 0x148a10)(buf));
        break;
    }
    case kBufConvert:
        Note("tonative", At<uint32_t(__thiscall *)(void *, uint8_t, uint8_t, uint8_t)>(ctx, 0x1472f0)(buf, uint8_t(a[1]), uint8_t(a[2]), uint8_t(a[3])));
        Note("torgb", At<bool(__thiscall *)(void *, uint32_t, uint8_t *, uint8_t *, uint8_t *)>(ctx, 0x147270)(
                          buf, a[4], reinterpret_cast<uint8_t *>(&gData.words[1]), reinterpret_cast<uint8_t *>(&gData.words[1]) + 1,
                          reinterpret_cast<uint8_t *>(&gData.words[2])));
        break;
    case kBufCache: Note("set", At<bool(__thiscall *)(void *, void *)>(ctx, 0x148980)(buf, a[1] % 3 ? &gCache2 : &gCache)); break;
    case kBufReady: Note("ready", At<B>(ctx, 0x148080)(buf)); break;
    case kBufRaw: {
        void *out = nullptr;
        Note("surface", At<bool(__thiscall *)(void *, uint32_t, void **)>(ctx, 0x1478f0)(gBuf + 8, a[1], &out));
        Note("surface out", Norm(out));
        Note("raw4", At<B>(ctx, 0x144910)(gBuf + 8));
        break;
    }
    case kBufTake: break;
    case kBufDelete: At<void *(__thiscall *)(void *, uint32_t)>(ctx, 0x1468d0)(buf, a[0] % 2); break;
    }
}

void RunOurs(const Case &c)
{
    auto *const self = reinterpret_cast<cGZGraphicSystemBehavior *>(gSys);
    auto *const face = reinterpret_cast<cIGZGraphicSystem *>(gSys + 8);
    auto *const service = reinterpret_cast<cIGZSystemService *>(gSys + 0xc);
    auto *const buf = reinterpret_cast<cIGZBuffer *>(gBuf);
    auto *const raw = reinterpret_cast<cBackbufferAsGZBufferRaw *>(gBuf + 8);
    const uint32_t *a = c.args;
    switch (c.op) {
    case kQueryInterface: {
        const uint32_t iid = kIids[a[0] % 7] ? kIids[a[0] % 7] : a[1];
        void *out = reinterpret_cast<void *>(uintptr_t(0x1234));
        const bool ok = self->QueryInterface(iid, a[2] % 5 || iid == 0x73283c || iid == 0x287fb697 ? &out : nullptr);
        drawfx::Note("ret", {ok, Norm(out)});
        break;
    }
    case kAddRef: Note("ret", self->AddRef()); break;
    case kRelease: Note("ret", self->Release()); break;
    case kOnConnection:
    case kOnState:
    case kOnOther: self->OnEvent(EventType(c), a[1], gData.message, a[2]); break;
    case kOnQuery: Note("ret", self->OnQuery(kListenEvents[a[0] % 3], a[1], gData.words)); break;
    case kNotify: self->NotifyListeners(kListenEvents[a[0] % 3], a[1], gData.words); break;
    case kAsk: Note("ret", self->AskListeners(kListenEvents[a[0] % 3], a[1], gData.words)); break;
    case kConnect: self->Connect(kListenEvents[a[0] % 3], reinterpret_cast<cIGZUnknown *>(&gListener[a[1] % 3]), a[2] % 4); break;
    case kDisconnect: self->Disconnect(kListenEvents[a[0] % 3], reinterpret_cast<cIGZUnknown *>(&gListener[a[0] % 3]), kHeardAs[a[0] % 3]); break;
    case kIsActive: Note("ret", self->IsActive()); break;
    case kEventCount: Note("ret", self->GetEventCount()); break;
    case kEventIds: NoteIdRange(self->GetEventIds()); break;
    case kEventAttribute: {
        const uint32_t ids[] = {kRegistryIds[0], kRegistryIds[1], kRegistryIds[2], a[1]};
        const nGZEvent::cEventAttribute r = self->GetEventAttribute(ids[a[0] % 4]);
        drawfx::Note("attribute", {r.mId, Word(r.mName), r.mWord08, r.mWord0C, r.mWord10, r.mWord14});
        break;
    }
    case kEmptySlots:
        self->SetAttribute(a[0], a[1], a[2]);
        self->GetAttribute(a[0], a[1], a[2]);
        break;
    case kStubs:
        if (a[7] % 2) RunStubsOurs(face, a);
        else RunMoreStubsOurs(face, a);
        break;
    case kDrawBuffer: Note("8", Norm(face->ActiveDrawBuffer())); break;
    case kColour:
        Note("9", Norm(face->GetScreenColorType(reinterpret_cast<cGZColorType *>(&gData.words[0]))));
        Note("34", face->ConvertRGBToNative(uint8_t(a[1]), uint8_t(a[2]), uint8_t(a[3])));
        Note("35", face->ConvertRGBToNativeAt(&gData.words[2], uint8_t(a[2]), uint8_t(a[3]), uint8_t(a[4])));
        Note("36", face->ConvertNativeToRGB(a[5], reinterpret_cast<uint8_t *>(&gData.words[3]),
                                            reinterpret_cast<uint8_t *>(&gData.words[3]) + 1, reinterpret_cast<uint8_t *>(&gData.words[4])));
        break;
    case kMetrics: Note("10", Norm(face->GetScreenMetrics(reinterpret_cast<cGZGraphicSystemMetrics *>(&gData.words[1])))); break;
    case kWindowSlots:
        switch (a[0] % 5) {
        case 0: Note("18", face->BeginModal()); break;
        case 1: Note("19", face->EndModal()); break;
        case 2: Note("20", face->IsWindowActive()); break;
        case 3: Note("21", uint32_t(face->ShowMessageBox(reinterpret_cast<cIGZString *>(&gText), reinterpret_cast<cIGZString *>(&gCaption)))); break;
        case 4: Note("33", face->GetCursorPosition(reinterpret_cast<int32_t *>(&gData.words[5]), reinterpret_cast<int32_t *>(&gData.words[6]))); break;
        }
        break;
    case kServiceSlots: {
        void *out = nullptr;
        const uint32_t iid = kIids[a[0] % 7] ? kIids[a[0] % 7] : a[1];
        drawfx::Note("qi", {service->QueryInterface(iid, &out), Norm(out)});
        drawfx::Note("qi8", {face->QueryInterface(iid, &out), Norm(out)});
        Note("addref", service->AddRef());
        Note("addref8", face->AddRef());
        Note("release", service->Release());
        Note("release8", face->Release());
        Note("id", service->GetServiceID());
        service->SetServiceID(a[2]);
        Note("id", service->GetServiceID());
        Note("priority", uint32_t(service->GetServicePriority()));
        Note("running", service->IsServiceRunning());
        service->SetServiceRunning(a[3] % 2 != 0);
        Note("running", service->IsServiceRunning());
        Note("tick", c.args[4] % 2 ? service->OnTick(a[4]) : service->OnIdle(a[4]));
        Note("tick priority", uint32_t(service->GetServiceTickPriority()));
        break;
    }
    case kInit: Note("ret", service->Init()); break;
    case kShutdown: Note("ret", service->Shutdown()); break;
    case kDestruct: RawSlot<15, void *>(self, 0u); break;
    case kReleaseLast: Note("ret", self->Release()); break;
    case kDelete: RawSlot<15, void *>(self, 1u); break;
    case kBufQueryInterface: {
        const uint32_t iid = kBufIids[a[0] % 5] ? kBufIids[a[0] % 5] : a[1];
        void *out = reinterpret_cast<void *>(uintptr_t(0x1234));
        drawfx::Note("qi", {buf->QueryInterface(iid, a[2] % 5 ? &out : nullptr), Norm(out)});
        drawfx::Note("qi8", {raw->QueryInterface(iid, &out), Norm(out)});
        break;
    }
    case kBufRefs:
        Note("addref", buf->AddRef());
        Note("addref8", raw->AddRef());
        Note("release8", raw->Release());
        Note("release", buf->Release());
        Note("release", buf->Release());
        break;
    case kBufSlots: {
        Note("init", buf->Init(1, 2, 3, 4));
        Note("uninit", buf->Uninitialize());
        Note("lock", buf->Lock(a[1]));
        Note("unlock", buf->Unlock(a[2]));
        Note("locked", buf->IsLocked());
        Note("w", buf->GetWidth());
        Note("h", buf->GetHeight());
        const cGZRect r = buf->GetBounds();
        drawfx::Note("rect", {uint32_t(r.left), uint32_t(r.top), uint32_t(r.right), uint32_t(r.bottom)});
        Note("rectp", Norm(buf->GetBoundsPtr()));
        Note("mem", buf->GetMemoryUsage());
        const cGZColorType t = buf->GetColorType();
        drawfx::Note("type", {t.format, t.bits});
        const cGZColorType n = buf->GetNativeColorType();
        drawfx::Note("native", {n.format, n.bits});
        Note("bits", buf->GetBitsPerPixel());
        Note("bytes", buf->GetBytesPerPixel());
        Note("fill", buf->Fill(1, nullptr));
        Note("fillc", buf->FillCoords(1, 2, 3, 4, 5, nullptr));
        Note("fillr", buf->FillRect(1, nullptr, nullptr));
        Note("pixel", buf->GetPixel(1, 2));
        buf->SetPixel(1, 2, 3);
        if (a[3] % 3 == 0) buf->SetTransparency(a[4]);
        if (a[3] % 3 == 1) buf->RemoveTransparency();
        Note("transp", buf->GetTransparency());
        Note("ptransp", buf->IsPixelTransparent(1, 2));
        Note("tcolour", buf->GetTransparentColor(&gData.words[0]));
        Note("line", buf->DrawLine(1, 2, 3, 4, 5, 6));
        Note("blt", buf->Blt(nullptr, nullptr, nullptr, nullptr));
        Note("rgba", buf->ConvertRGBAValueToNative(1, 2, 3, 4));
        Note("torgba", buf->ConvertNativeValueToRGBA(1, nullptr, nullptr, nullptr, nullptr));
        Note("bitsp", Norm(buf->GetColorSurfaceBits()));
        Note("stride", buf->GetColorSurfaceStride());
        Note("changes", buf->GetChangeCounter());
        Note("cache", Norm(buf->GetHardwareCache()));
        Note("shutdown", buf->Shutdown());
        break;
    }
    case kBufConvert:
        Note("tonative", buf->ConvertRGBValueToNative(uint8_t(a[1]), uint8_t(a[2]), uint8_t(a[3])));
        Note("torgb", buf->ConvertNativeValueToRGB(a[4], reinterpret_cast<uint8_t *>(&gData.words[1]),
                                                   reinterpret_cast<uint8_t *>(&gData.words[1]) + 1,
                                                   reinterpret_cast<uint8_t *>(&gData.words[2])));
        break;
    case kBufCache: Note("set", buf->SetHardwareCache(reinterpret_cast<cIGZUnknown *>(a[1] % 3 ? &gCache2 : &gCache))); break;
    case kBufReady: Note("ready", buf->IsReady()); break;
    case kBufRaw: {
        void *out = nullptr;
        Note("surface", raw->QuerySurface(a[1], &out));
        Note("surface out", Norm(out));
        Note("raw4", raw->Slot4());
        break;
    }
    case kBufTake: break;
    case kBufDelete: RawSlot<40, void *>(buf, a[0] % 2); break;
    }
}

// What a side leaves.
struct Result {
    drawfx::Log log;
    std::vector<uint8_t> sys, buf;
    Data data; // the out-parameters and event data after the op
    bool bufLinked = false;
    long long blocks, bytes;
};

std::vector<uint8_t> Without(const uint8_t *block, size_t size, std::initializer_list<size_t> skip)
{
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < size; i += 4) {
        bool skipped = false;
        for (size_t s : skip) skipped |= s == i;
        if (!skipped) bytes.insert(bytes.end(), block + i, block + i + 4);
    }
    return bytes;
}

// Whether the side's back buffer outlives the op (the destroying ops free it with its last reference).
bool BufferFreed(const Case &c)
{
    const bool last = Get<uint32_t>(c.buf, 4) == 1;
    if (c.op == kBufDelete) return c.args[0] % 2 != 0;
    if (c.op == kBufRefs) return last;
    return Destroys(c.op) && last;
}

template <class Run> Result Side(const Case &c, d3dfx::ErrorLog &errors, impfx::Imports &imp, VTables vt, Run run)
{
    gSys = SystemGone(c.op) ? static_cast<uint8_t *>(::operator new(kSize)) : gSysBlock;
    BuildSide(c, errors, imp, vt);
    long long blocks = 0, bytes = 0;
    drawfx::gLog = drawfx::Log();
    drawfx::gLog.reserve(1024);
    void *const framework = *GameAddress<void *>(d3dfx::ErrorLog::kServiceRva);
    if (c.noFramework) *GameAddress<void *>(d3dfx::ErrorLog::kServiceRva) = nullptr;
    {
        difftest::AllocationCounter counter;
        run();
        counter.Stop();
        blocks = counter.Blocks();
        bytes = counter.Bytes();
    }
    *GameAddress<void *>(d3dfx::ErrorLog::kServiceRva) = framework;
    NoteRefs();
    if (!Destroys(c.op)) NoteConnections();
    Result r;
    r.log = drawfx::gLog;
    r.data = gData;
    if (!SystemGone(c.op)) {
        r.sys = Without(gSys, kSize, {0, 8, 0xc, 0x18});
        r.bufLinked = Get<void *>(gSys, 0x18) == gBuf;
    }
    if (!BufferFreed(c)) {
        r.buf = Without(gBuf, kBufSize, {0, 8});
        ::operator delete(gBuf);
    }
    gBuf = nullptr;
    r.blocks = blocks;
    r.bytes = bytes;
    DestroySide();
    gSys = gSysBlock;
    return r;
}

const char *kOpNames[kOpCount] = {
    "QueryInterface", "AddRef", "Release", "OnEvent(connection)", "OnEvent(device state)", "OnEvent(other)", "OnQuery",
    "NotifyListeners", "AskListeners", "Connect", "Disconnect", "IsActive", "GetEventCount", "GetEventIds",
    "GetEventAttribute", "Slot7/Slot10", "stubs", "ActiveDrawBuffer", "colour slots", "GetScreenMetrics",
    "window slots", "service slots", "Init", "Shutdown", "destructor", "Release (last)", "delete",
    "buffer QueryInterface", "buffer AddRef/Release", "buffer slots", "buffer conversions", "SetHardwareCache",
    "IsReady", "buffer raw face", "(unused)", "buffer delete"};

// Our vtable words (a constructed system's and its back buffer's; both released after).
VTables OurVTables()
{
    cGZGraphicSystem *const s = new cGZGraphicSystem;
    VTables vt{*reinterpret_cast<void **>(s), *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(s) + 8),
               *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(s) + 0xc), *reinterpret_cast<void **>(s->mBackbuffer),
               *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(s->mBackbuffer) + 8)};
    delete s;
    return vt;
}

struct World {
    RegistryScope registry;
    shape::StandIn registryStand{0x148440, &cGZGraphicSystemBehaviorTraits::RegistryFor, &pdfx::StandRegistry<kRegistryMap>};
    Cells cells;
    StandIns stands;
    d3dfx::ErrorLog errors;
    impfx::Imports imp;
    explicit World(difftest::Context &ctx) : errors(ctx), imp(ctx)
    {
        IMPFX_ADD(imp, GetDC).Returns(0x4dc00001u);
        IMPFX_ADD(imp, GetDeviceCaps).On(&DeviceCaps);
        IMPFX_ADD(imp, ReleaseDC).Returns(1u);
        IMPFX_ADD(imp, GetCursorPos).On(&CursorPos);
        IMPFX_ADD(imp, ScreenToClient).On(&ToClient);
        IMPFX_ADD(imp, MessageBoxA, "issu").Returns(6u);
    }
};

void Seen(const Result &r, std::set<std::string> &seen)
{
    for (const drawfx::Call &call : r.log) {
        if (!call.iface) seen.insert(call.note);
        else if (call.slot < call.iface->count) seen.insert(call.iface->slots[call.slot].name);
    }
}

void Sweep(difftest::Context &ctx, uint32_t seed, std::initializer_list<uint32_t> ops, unsigned rounds,
           std::initializer_list<const char *> needs = {})
{
    World world(ctx);
    const VTables ours = OurVTables();
    const VTables game = {GameAddress<void>(kVTableRva), GameAddress<void>(kFaceVTableRva), GameAddress<void>(kServiceVTableRva),
                          GameAddress<void>(kBufVTableRva), GameAddress<void>(kBufRawVTableRva)};
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
            if (!drawfx::Same(ctx, a.log, b.log, label, round)) return;
            if (std::memcmp(&a.data, &b.data, sizeof(Data)) != 0) {
                size_t at = 0;
                while (at < sizeof(Data) && reinterpret_cast<const uint8_t *>(&a.data)[at] == reinterpret_cast<const uint8_t *>(&b.data)[at]) ++at;
                ctx.Fail("%s round %u: the out-parameters differ (first at byte %zu)", label, round, at);
                return;
            }
            if (a.sys != b.sys || a.buf != b.buf || a.bufLinked != b.bufLinked || a.blocks != b.blocks || a.bytes != b.bytes) {
                size_t at = 0;
                while (at < a.sys.size() && at < b.sys.size() && a.sys[at] == b.sys[at]) ++at;
                size_t bat = 0;
                while (bat < a.buf.size() && bat < b.buf.size() && a.buf[bat] == b.buf[bat]) ++bat;
                ctx.Fail("%s round %u: system %d (first difference at compared byte %zu) buffer %d (byte %zu, sizes %zu/%zu) "
                         "linked %d/%d blocks %lld/%lld bytes %lld/%lld",
                         label, round, a.sys == b.sys, at, a.buf == b.buf, bat, a.buf.size(), b.buf.size(), a.bufLinked,
                         b.bufLinked, a.blocks, b.blocks, a.bytes, b.bytes);
                return;
            }
        }
    }
    for (const char *need : needs)
        if (!seen.count(need)) ctx.Fail("the sweep never reached '%s' (a vacuous path)", need);
}

} // namespace

// Construction (0x1475e0 with GameNew handing out blocks of seeded bytes for the system and its back buffer; ours
// constructed over copies of the same bytes): every byte the creator writes.
DIFF_TEST(cGZGraphicSystem_Create, 0x1475e0)
{
    std::mt19937 gen(0x1475e0);
    static uint8_t seededSys[kSize], seededBuf[kBufSize], oursSys[kSize], oursBuf[kBufSize];
    static uint8_t *handSys = nullptr, *handBuf = nullptr;
    struct New {
        static void *__cdecl Give(size_t size)
        {
            if (size == kSize) return handSys;
            if (size == kBufSize) return handBuf;
            return ::operator new(size);
        }
    };
    shape::CodePatch give{0x824632, reinterpret_cast<const void *>(&New::Give)};
    for (unsigned round = 0; round < 64; ++round) {
        for (uint8_t &x : seededSys) x = uint8_t(gen());
        for (uint8_t &x : seededBuf) x = uint8_t(gen());
        std::memcpy(oursSys, seededSys, kSize);
        std::memcpy(oursBuf, seededBuf, kBufSize);
        handSys = seededSys;
        handBuf = round % 8 == 7 ? nullptr : seededBuf;
        ++ctx.cases;
        void *const made = ctx.Original<void *(__cdecl *)()>(0x1475e0)();
        if (made != seededSys + 8) ctx.Fail("round %u: the creator's object is not the +8 face of the block it was given", round);
        ::new (oursSys) cGZGraphicSystem;
        auto *const ours = reinterpret_cast<cGZGraphicSystem *>(oursSys);
        if (Without(seededSys, kSize, {0, 8, 0xc, 0x18}) != Without(oursSys, kSize, {0, 8, 0xc, 0x18})) {
            ctx.Fail("round %u: the system differs", round);
            return;
        }
        if (handBuf) {
            ::new (oursBuf) cBackbufferAsGZBuffer;
            reinterpret_cast<cBackbufferAsGZBuffer *>(oursBuf)->AddRef();
            if (Get<void *>(seededSys, 0x18) != seededBuf || Without(seededBuf, kBufSize, {0, 8}) != Without(oursBuf, kBufSize, {0, 8}))
                ctx.Fail("round %u: the back buffer differs", round);
            if (Get<void *>(seededBuf, 0) != GameAddress<void>(kBufVTableRva) || Get<void *>(seededBuf, 8) != GameAddress<void>(kBufRawVTableRva))
                ctx.Fail("round %u: the original's back buffer vtables are not 0xd9bf0c / 0xd9bfb4", round);
        } else if (Get<void *>(seededSys, 0x18) != nullptr) {
            ctx.Fail("round %u: a failed back buffer allocation left a pointer", round);
        }
        if (Get<void *>(seededSys, 0) != GameAddress<void>(kVTableRva) || Get<void *>(seededSys, 8) != GameAddress<void>(kFaceVTableRva) ||
            Get<void *>(seededSys, 0xc) != GameAddress<void>(kServiceVTableRva))
            ctx.Fail("round %u: the original's vtable words are not 0xd9bd78 / 0xd9bdbc / 0xd9bed4", round);
        // Ours: its own back buffer (heap), counted once.
        if (!ours->mBackbuffer || ours->mBackbuffer->mRefCount != 1) ctx.Fail("round %u: our back buffer is not counted once", round);
        ours->mBackbuffer->Release();
    }
    // A failed allocation gives null.
    handSys = nullptr;
    if (ctx.Original<void *(__cdecl *)()>(0x1475e0)() != nullptr) ctx.Fail("the creator did not give null without memory");
    // Ours: the +8 face of a fresh system.
    cIGZGraphicSystem *const made = cGZGraphicSystem::Create();
    auto *const system = static_cast<cGZGraphicSystem *>(made);
    if (!made || system->mRefCount != 0 || system->mServiceId != cGZGraphicSystem::kServiceId ||
        reinterpret_cast<uint8_t *>(made) != reinterpret_cast<uint8_t *>(static_cast<cGZGraphicSystemBehavior *>(system)) + 8)
        ctx.Fail("Create: not a fresh system's +8 face");
    delete system;
}

// The behaviour slots (0-14).
DIFF_TEST(cGZGraphicSystem_Behaviour, 0x1474c0)
{
    Sweep(ctx, 0x1474c0,
          {kQueryInterface, kAddRef, kRelease, kOnConnection, kOnOther, kOnQuery, kNotify, kAsk, kConnect, kDisconnect,
           kIsActive, kEventCount, kEventIds, kEventAttribute, kEmptySlots},
          96, {"OnEvent", "OnQuery", "id", "attribute"});
}

// The device state: the back buffer taken, the draw buffer made or refreshed, or the surface let go.
DIFF_TEST(cGZGraphicSystem_DeviceState, 0x147ae0)
{
    Sweep(ctx, 0x147ae0, {kOnState}, 400,
          {"current device", "GetBackBuffer", "GetWidth", "GetHeight", "GetClassObject", "ctx.SetTarget", "ctx.Slot22",
           "ctx.Slot24", "db.Init", "db.Refresh", "Release"});
}

// The +8 face's slots.
DIFF_TEST(cGZGraphicSystem_Slots, 0x147930)
{
    Sweep(ctx, 0x147930, {kStubs, kDrawBuffer, kColour, kMetrics, kWindowSlots, kServiceSlots}, 160,
          {"GetClassObject", "IsFullscreen", "db.GetBuffer", "buf.GetNativeColorType", "buf.ToNative", "buf.ToRGB",
           "dx.GetDeviceCount", "GetDC", "GetDeviceCaps", "ReleaseDC", "desktop size", "GetAdapterDisplayMode",
           "GetBitsPerPixel", "win.Slot23", "win.Slot24", "win.Slot36", "win.GetHwnd", "str.ToChar", "MessageBoxA",
           "GetCursorPos", "ScreenToClient"});
}

// Init (the window and the display mode), Shutdown, the destructors.
DIFF_TEST(cGZGraphicSystem_Lifetime, 0x147d10)
{
    Sweep(ctx, 0x147d10, {kInit, kShutdown, kDestruct, kReleaseLast, kDelete}, 200,
          {"SetRenderer", "dx.Connect", "win.SetTitle", "win.SetSize", "win.SetMonitor", "win.Show", "formats manager",
           "GetDisplayFormat", "GetColorFormats", "GetDisplayFormats", "GetDisplayModes", "best format", "desktop mode",
           "closest mode", "set mode", "Release"});
}

// The back buffer's slots (both faces) and its deleting destructor.
DIFF_TEST(cBackbufferAsGZBuffer_Slots, 0x148370)
{
    Sweep(ctx, 0x148370, {kBufQueryInterface, kBufRefs, kBufSlots, kBufConvert, kBufCache, kBufReady, kBufRaw, kBufDelete}, 120,
          {"GetSurfaceConst", "GetPixelFormat", "QueryInterface", "ToNative", "FromNative", "cache.Accept", "IsCreated",
           "Release"});
}
