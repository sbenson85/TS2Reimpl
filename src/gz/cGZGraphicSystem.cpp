// nGZGraphic4::cGZGraphicSystem and cBackbufferAsGZBuffer (lane NQ): src/gz/cGZGraphicSystem.h.
#include "gz/cGZGraphicSystem.h"
#include "gz/cBehaviorRegistry.h"
#include <iterator>
#include <optional>

#define NOMINMAX
#include <windows.h>

#include "game/game.h"
#include "game/import_slots.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/GraphicModeSearch.h"
#include "gz/GraphicServices.h"
#include "gz/MultiMon.h" // GetSystemMetricsStub
#include "gz/cDeviceDX.h"   // cIDeviceDX
#include "gz/cDeviceEnum.h" // cIDeviceEnum, the ranges
#include "gz/cGZDrawContextAsBuffer.h"
#include "gz/cIGZCOM.h"
#include "gz/cIGZString.h"
#include "gz/cSurfaceDX.h"
#include "ifaces/cIGZFrameWork.h"
#include "rz/RZAutoRef.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject

namespace nGZGraphic4 {

namespace {

// Framework services.
constexpr uint32_t kFontServiceId = 0x20673960, kFontServiceIid = 0x673960;          // takes the font renderer
constexpr uint32_t kGraphicSystemDXId = 0x85522803, kGraphicSystemDXIid = 0xa55227bb;
constexpr uint32_t kWindowServiceId = 0xfb6372ee;
constexpr uint32_t kWindowBehaviorIid = 0x5b637209, kWindowCanvasIid = 0xbb6377fa;
constexpr uint32_t kCanvasServiceId = 0xa3460deb, kCanvasServiceIid = 0x66b217a1;

// COM classes.
constexpr uint32_t kBufferClass = 0xc470d325;                                   // cGZBuffer
constexpr uint32_t kFontRendererClass = 0x8b3255f9, kFontRendererIid = 0xa8bc2a75;
constexpr uint32_t kDrawBufferClass = 0xab300b20;                               // cGZDrawContextAsBuffer
constexpr uint32_t kDrawContextClass = 0xae6320e, kDrawContextIid = 0x2ae63219; // cGZDrawContext

constexpr const char *kWindowTitle = "GZGraphic4 App"; // the image's 0xd9bfc8
constexpr uint32_t kWindowShowFlags = 0xb;  // the window's slot 15 (Init)

// The device state codes of kDeviceStateEvent (data + 4).
constexpr uint32_t kStateLost = 0, kStateReady = 1, kStateGone = 3;
constexpr uint32_t kBackbufferFormat = 9, kBackbufferBits = 0x20; // what the device's back buffer is taken as

// Init's display-mode search bounds (0x18b490): bits 16-32, any refresh up to 1000 Hz.
constexpr uint32_t kMinBits = 0x10, kMaxBits = 0x20, kMinRefresh = 0, kMaxRefresh = 0x3e8;

constexpr uint32_t kArgbFormatIid = 0x9ac38991; // cIPixelFormatARGB
constexpr uint32_t kRate60 = 0x42700000; // 60.0f (slots 50-52)

// Win32, through the slots the original calls.
constexpr IatSlot kIatGetDC = &ts2_rva_d9a518, kIatReleaseDC = &ts2_rva_d9a514, kIatGetDeviceCaps = &ts2_rva_d9a0c0;
constexpr IatSlot kIatGetCursorPos = &ts2_rva_d9a4d0, kIatScreenToClient = &ts2_rva_d9a49c, kIatMessageBoxA = &ts2_rva_d9a568;
constexpr UINT kMessageBoxFlags = 0x40000; // MB_TOPMOST

cIGZCOM *Com() { return static_cast<cIGZCOM *>(GetDirectorFrameworkObject()); }

// A framework service held for a scope (the original's RZAutoRef: asked for when there is a framework, released
// through 0x1466e0's guarded release at the end).
class HeldService {
public:
    HeldService(uint32_t id, uint32_t iid)
    {
        mRef.mObject = nullptr;
        if (void *const framework = GetServiceObject())
            As<ifaces::cIGZFrameWork>(framework)->GetSystemService(id, iid, &mRef.mObject);
    }
    ~HeldService() { mRef.ReleaseObject(); }
    HeldService(const HeldService &) = delete;
    HeldService &operator=(const HeldService &) = delete;
    void *Get() const { return mRef.mObject; }

private:
    RZAutoRef mRef;
};

// Game code still called through its RVA.
// 0x18ae40 (cdecl): the current device (cGraphicSystemDX's), asked for once.
cIDeviceDX *CurrentDevice() { return GetCurrentDeviceCached(); }
// The current device's enumeration (GraphicServices.h, 0x18b470).
cIDeviceEnum *CurrentEnum() { return static_cast<cIDeviceEnum *>(GetCurrentDeviceEnum()); }
// 0x18acd0 (cdecl): the pixel formats manager.
void *FormatsManager() { return GetPixelFormatsManagerCached(); }
// The mode and format searches and the device switch are ours (gz/GraphicModeSearch.h, lane RJ): 0x18a9b0,
// 0x18b440, 0x18b490 and 0x18b250.
// The desktop's size (0 width, 1 height): the multi-monitor GetSystemMetrics (MultiMon.h, 0x154a80).
uint32_t DesktopSize(uint32_t which) { return uint32_t(nMultiMon::GetSystemMetricsStub(int(which))); }

// The window's HWND (its canvas interface's slot 40).
HWND WindowHandle(void *window) { return reinterpret_cast<HWND>(RawSlot<40, uintptr_t>(window)); }

// The ARGB pixel format of the back buffer's surface (counted by QueryInterface and released at once, as the
// original's; the format is static).
void *ArgbFormatOf(cISurfaceDX *surface)
{
    void *const format = RawSlot<22, void *>(surface->GetSurfaceConst());
    void *argb = nullptr;
    static_cast<cIGZUnknown *>(format)->QueryInterface(kArgbFormatIid, &argb);
    if (argb) static_cast<cIGZUnknown *>(argb)->Release();
    return argb;
}

} // namespace

// ==== cBackbufferAsGZBuffer ====

cBackbufferAsGZBuffer::cBackbufferAsGZBuffer()
    : mSurface(nullptr), mBounds{0, 0, 0, 0}, mTransparentColor(0), mFlags(0), mColorType(3, 0x10),
      mHardwareCache(nullptr), mChangeCounter(0)
{
}

// 0x1468d0: the surface let go (and cleared), the hardware cache released.
cBackbufferAsGZBuffer::~cBackbufferAsGZBuffer()
{
    Shutdown();
    if (mHardwareCache) mHardwareCache->Release();
}

bool cBackbufferAsGZBuffer::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid == cBackbufferAsGZBufferRaw::kIid) {
        *out = static_cast<cBackbufferAsGZBufferRaw *>(this);
        AddRef();
        return true;
    }
    // The stub's (0x18a8c0).
    if (riid == 1 || riid == cIGZBuffer::kIid) {
        *out = static_cast<cIGZBuffer *>(this);
        AddRef();
        return true;
    }
    return false;
}

uint32_t cBackbufferAsGZBuffer::AddRef() { return ++mRefCount; }

// The stub's (0x18a8f0): the last reference deletes through slot 40.
uint32_t cBackbufferAsGZBuffer::Release()
{
    if (--mRefCount == 0) {
        delete this;
        return 0;
    }
    return mRefCount;
}

bool cBackbufferAsGZBuffer::Init(uint32_t, uint32_t, uint32_t, uint32_t) { return false; }

bool cBackbufferAsGZBuffer::Shutdown()
{
    if (cISurfaceDX *const surface = mSurface) {
        mSurface = nullptr;
        surface->Release();
    }
    return true;
}

bool cBackbufferAsGZBuffer::Uninitialize() { return false; }

bool cBackbufferAsGZBuffer::Lock(uint32_t flags)
{
    if (flags & kLockCounted) ++mChangeCounter;
    return true;
}

bool cBackbufferAsGZBuffer::Unlock(uint32_t) { return true; }
bool cBackbufferAsGZBuffer::IsLocked() { return false; }
uint32_t cBackbufferAsGZBuffer::GetWidth() { return static_cast<uint32_t>(mBounds.right); }
uint32_t cBackbufferAsGZBuffer::GetHeight() { return static_cast<uint32_t>(mBounds.bottom); }
cGZRect cBackbufferAsGZBuffer::GetBounds() { return mBounds; }
const cGZRect *cBackbufferAsGZBuffer::GetBoundsPtr() { return &mBounds; }
uint32_t cBackbufferAsGZBuffer::GetMemoryUsage() { return 0; }
cGZColorType cBackbufferAsGZBuffer::GetColorType() { return mColorType; }
cGZColorType cBackbufferAsGZBuffer::GetNativeColorType() { return mColorType; }
uint32_t cBackbufferAsGZBuffer::GetBitsPerPixel() { return mColorType.bits; }
uint32_t cBackbufferAsGZBuffer::GetBytesPerPixel() { return mColorType.bits >> 3; }
bool cBackbufferAsGZBuffer::Fill(uint32_t, const cGZRect *) { return false; }
bool cBackbufferAsGZBuffer::FillCoords(uint32_t, int32_t, int32_t, int32_t, int32_t, const cGZRect *) { return false; }
bool cBackbufferAsGZBuffer::FillRect(uint32_t, const cGZRect *, const cGZRect *) { return false; }
uint32_t cBackbufferAsGZBuffer::GetPixel(uint32_t, uint32_t) { return 0; }
void cBackbufferAsGZBuffer::SetPixel(uint32_t, uint32_t, uint32_t) {}

void cBackbufferAsGZBuffer::SetTransparency(uint32_t colour)
{
    mFlags |= 2;
    mTransparentColor = colour;
}

bool cBackbufferAsGZBuffer::GetTransparency() { return false; }
bool cBackbufferAsGZBuffer::IsPixelTransparent(uint32_t, uint32_t) { return false; }

bool cBackbufferAsGZBuffer::GetTransparentColor(uint32_t *colour)
{
    if (!(mFlags & 2)) return false;
    *colour = mTransparentColor;
    return true;
}

void cBackbufferAsGZBuffer::RemoveTransparency() { mFlags &= ~2u; }
bool cBackbufferAsGZBuffer::DrawLine(int32_t, int32_t, int32_t, int32_t, uint32_t, int32_t) { return false; }
bool cBackbufferAsGZBuffer::Blt(cIGZBuffer *, const cGZRect *, const cGZRect *, const cGZRect *) { return false; }

// Opaque r, g, b as the surface's format stores them (its ARGB slot 16).
uint32_t cBackbufferAsGZBuffer::ConvertRGBValueToNative(uint8_t r, uint8_t g, uint8_t b)
{
    void *const format = ArgbFormatOf(mSurface);
    const uint32_t argb = 0xff000000u | uint32_t(r) << 16 | uint32_t(g) << 8 | b;
    return RawSlot<16, uint32_t>(format, &argb);
}

// The surface format's ARGB of `value` (its slot 17), split.
bool cBackbufferAsGZBuffer::ConvertNativeValueToRGB(uint32_t value, uint8_t *r, uint8_t *g, uint8_t *b)
{
    void *const format = ArgbFormatOf(mSurface);
    uint32_t argb;
    RawSlot<17, void *>(format, &argb, value);
    *r = uint8_t(argb >> 16);
    *g = uint8_t(argb >> 8);
    *b = uint8_t(argb);
    return true;
}

uint32_t cBackbufferAsGZBuffer::ConvertRGBAValueToNative(uint8_t, uint8_t, uint8_t, uint8_t) { return 0; }
bool cBackbufferAsGZBuffer::ConvertNativeValueToRGBA(uint32_t, uint8_t *, uint8_t *, uint8_t *, uint8_t *) { return false; }
void *cBackbufferAsGZBuffer::GetColorSurfaceBits() { return nullptr; }
uint32_t cBackbufferAsGZBuffer::GetColorSurfaceStride() { return 0; }
uint32_t cBackbufferAsGZBuffer::GetChangeCounter() { return mChangeCounter; }
cIGZUnknown *cBackbufferAsGZBuffer::GetHardwareCache() { return mHardwareCache; }

// The old cache let go first; the new one kept (counted) only when its slot 3 accepts.
bool cBackbufferAsGZBuffer::SetHardwareCache(cIGZUnknown *cache)
{
    if (cIGZUnknown *const old = mHardwareCache) {
        mHardwareCache = nullptr;
        old->Release();
    }
    if (!RawSlot<3, bool>(cache, static_cast<cIGZBuffer *>(this))) return false;
    if (cache != mHardwareCache) {
        cIGZUnknown *const old = mHardwareCache;
        cache->AddRef();
        mHardwareCache = cache;
        if (old) old->Release();
    }
    return true;
}

// The surface exists and is created.
bool cBackbufferAsGZBuffer::IsReady() { return mSurface && mSurface->GetSurfaceConst()->IsCreated(); }

bool cBackbufferAsGZBuffer::QuerySurface(uint32_t iid, void **out) { return mSurface->QueryInterface(iid, out); }
bool cBackbufferAsGZBuffer::Slot4() { return true; }

void cBackbufferAsGZBuffer::TakeSurface(cIGZUnknown *backBuffer)
{
    Shutdown();
    mColorType = cGZColorType(kBackbufferFormat, kBackbufferBits);
    const uint32_t height = RawSlot<21, uint32_t>(backBuffer);
    const uint32_t width = RawSlot<20, uint32_t>(backBuffer);
    mBounds = {0, 0, int32_t(width), int32_t(height)};
    if (cISurfaceDX *const old = mSurface) {
        mSurface = nullptr;
        old->Release();
    }
    backBuffer->QueryInterface(kIidSurfaceDX, reinterpret_cast<void **>(&mSurface));
}

// ==== cGZGraphicSystem ====

namespace {
std::optional<nGZEvent::cEventAttributeMap> gRegistry;
const nGZEvent::ClassEvent kClassEvents[] = {{0xca7ccbbc, "CurrentDeviceEvent", 0x3b6c3e0a, 2}};
} // namespace

nGZEvent::cEventAttributeMap *cGZGraphicSystemBehaviorTraits::Registry()
{
    const nGZEvent::cEventAttribute defaults = nGZEvent::behavior_mt::DefaultAttribute();
    return RegistryFor(0, &defaults);
}

// 0x148440.
nGZEvent::cEventAttributeMap *__cdecl cGZGraphicSystemBehaviorTraits::RegistryFor(uint32_t id,
                                                                                  const nGZEvent::cEventAttribute *attribute)
{
    return nGZEvent::ClassRegistryFor(gRegistry, kClassEvents, std::size(kClassEvents), id, attribute);
}

void cGZGraphicSystemBehaviorTraits::ResetRegistryForTest() { gRegistry.reset(); }

// 0x1475e0's object: the settings' first three words and the fullscreen byte cleared (the rest left as the allocator
// gave them), a new back buffer (counted).
cGZGraphicSystem::cGZGraphicSystem()
    : mServiceId(kServiceId), mRunning(false), mBackbuffer(nullptr), mDrawBuffer(nullptr)
{
    mSettings.mWidth = 0;
    mSettings.mHeight = 0;
    mSettings.mBits = 0;
    mFullscreen = false;
    cBackbufferAsGZBuffer *const backbuffer = new cBackbufferAsGZBuffer;
    backbuffer->AddRef();
    mBackbuffer = backbuffer;
}

// 0x146970: the draw buffer, then the back buffer.
cGZGraphicSystem::~cGZGraphicSystem()
{
    if (mDrawBuffer) mDrawBuffer->Release();
    if (mBackbuffer) mBackbuffer->Release();
}

cIGZGraphicSystem *__cdecl cGZGraphicSystem::Create() { return static_cast<cIGZGraphicSystem *>(new cGZGraphicSystem); }

// ---- cIGZUnknown ----

bool cGZGraphicSystem::QueryInterface(uint32_t riid, void **out)
{
    if (riid == cIGZGraphicSystem::kIid) {
        *out = static_cast<cIGZGraphicSystem *>(this);
        AddRef();
        return true;
    }
    if (riid == cIGZSystemService::kIID) {
        *out = static_cast<cIGZSystemService *>(this);
        AddRef();
        return true;
    }
    if (!out) return false;
    if (riid == 1 || riid == kIidBehavior2) {
        *out = static_cast<cGZGraphicSystemBehavior *>(this);
        AddRef();
        return true;
    }
    if (riid == kIidBehaviorType) {
        *out = static_cast<cGZGraphicSystemBehavior *>(this); // not counted (the original answers 0xfb859301)
        return true;
    }
    return false;
}

uint32_t cGZGraphicSystem::AddRef() { return Behavior::AddRef(); }
uint32_t cGZGraphicSystem::Release() { return Behavior::Release(); }

// ---- the behaviour ----

// The connection change keeps the active bit; the graphic system's current device state: lost or gone lets the back
// buffer's surface go; ready takes the device's back buffer and makes (once) the draw buffer over it, else refreshes it.
void cGZGraphicSystem::OnEvent(uint32_t type, uint32_t, void *data, uint32_t)
{
    if (type == kConnectionChangedEvent) {
        SyncActive();
        return;
    }
    if (type != kDeviceStateEvent) return;
    const uint32_t state = static_cast<const uint32_t *>(data)[1];
    if (state == kStateLost || state == kStateGone) {
        mBackbuffer->Shutdown();
        return;
    }
    if (state != kStateReady) return;
    cIGZUnknown *const backBuffer = CurrentDevice()->GetBackBuffer(0);
    mBackbuffer->TakeSurface(backBuffer);
    if (mDrawBuffer) {
        mDrawBuffer->Refresh();
        return;
    }
    Com()->GetClassObject(kDrawBufferClass, cIGZDrawContextAsBuffer::kIid, reinterpret_cast<void **>(&mDrawBuffer));
    void *context = nullptr;
    Com()->GetClassObject(kDrawContextClass, kDrawContextIid, &context);
    RawSlot<17, void>(context, static_cast<cIGZBuffer *>(mBackbuffer));
    RawSlot<24, void>(context, 1u);
    RawSlot<22, void>(context, 1u);
    mDrawBuffer->Init(static_cast<cIGZDrawContext *>(context), static_cast<cIGZBuffer *>(mBackbuffer));
    if (context) static_cast<cIGZUnknown *>(context)->Release();
}

// ---- cIGZGraphicSystem ----

uint32_t cGZGraphicSystem::CreateBuffer(void **out) { return Com()->GetClassObject(kBufferClass, cIGZBuffer::kIid, out); }
uint32_t cGZGraphicSystem::CreateBufferAs(uint32_t iid, void **out) { return Com()->GetClassObject(kBufferClass, iid, out); }
bool cGZGraphicSystem::Slot5() { return false; }
void cGZGraphicSystem::Slot6(uint32_t) {}
uint32_t cGZGraphicSystem::Slot7() { return 0; }

cIGZBuffer *cGZGraphicSystem::ActiveDrawBuffer()
{
    if (mDrawBuffer) return mDrawBuffer->GetBuffer();
    return mBackbuffer;
}

cGZColorType *cGZGraphicSystem::GetScreenColorType(cGZColorType *out)
{
    RawSlot<15, cGZColorType *>(ActiveDrawBuffer(), out);
    return out;
}

// With no Direct3D devices the desktop's (GDI) size and depth; else the current adapter's desktop mode.
cGZGraphicSystemMetrics *cGZGraphicSystem::GetScreenMetrics(cGZGraphicSystemMetrics *out)
{
    HeldService system(kGraphicSystemDXId, kGraphicSystemDXIid);
    if (RawSlot<23, uint32_t>(system.Get()) == 0) {
        const HDC dc = ImportSlot<HDC(WINAPI *)(HWND), kIatGetDC>()(nullptr);
        const auto caps = ImportSlot<int(WINAPI *)(HDC, int), kIatGetDeviceCaps>();
        const int planes = caps(dc, PLANES);
        const int bits = caps(dc, BITSPIXEL);
        ImportSlot<int(WINAPI *)(HWND, HDC), kIatReleaseDC>()(nullptr, dc);
        const uint32_t height = DesktopSize(1);
        const uint32_t width = DesktopSize(0);
        out->mWidth = width;
        out->mHeight = height;
        out->mBits = uint32_t(bits * planes);
        return out;
    }
    const cDisplayMode mode = CurrentEnum()->GetAdapterDisplayMode();
    const uint32_t bits = uint32_t(mode.mFormat->GetBitsPerPixel());
    out->mWidth = mode.mWidth;
    out->mHeight = mode.mHeight;
    out->mBits = bits;
    return out;
}

cGZGraphicSystemMetrics *cGZGraphicSystem::GetZeroMetrics(cGZGraphicSystemMetrics *out)
{
    out->mWidth = 0;
    out->mHeight = 0;
    out->mBits = 0;
    return out;
}

bool cGZGraphicSystem::IsFullscreen() { return CurrentDevice()->IsFullscreen(); }
bool cGZGraphicSystem::Slot13() { return false; }

// The canvas service, released before it is handed out (the framework keeps it).
cIGZUnknown *cGZGraphicSystem::GetCanvas()
{
    RZAutoRef canvas{nullptr};
    if (void *const framework = GetServiceObject())
        As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kCanvasServiceId, kCanvasServiceIid, &canvas.mObject);
    void *const object = canvas.mObject;
    canvas.ReleaseObject();
    return static_cast<cIGZUnknown *>(object);
}

bool cGZGraphicSystem::Slot15(uint32_t, uint32_t) { return true; }
uint32_t cGZGraphicSystem::Slot16(uint32_t value) { return value; }
bool cGZGraphicSystem::Slot17() { return false; }

bool cGZGraphicSystem::BeginModal()
{
    HeldService window(kWindowServiceId, kWindowBehaviorIid);
    RawSlot<23, void>(window.Get());
    return true;
}

bool cGZGraphicSystem::EndModal()
{
    HeldService window(kWindowServiceId, kWindowBehaviorIid);
    RawSlot<24, void>(window.Get());
    return !RawSlot<36, bool>(window.Get());
}

bool cGZGraphicSystem::IsWindowActive()
{
    HeldService window(kWindowServiceId, kWindowBehaviorIid);
    return !RawSlot<36, bool>(window.Get());
}

// A topmost box owned by the window, between BeginModal and EndModal.
int cGZGraphicSystem::ShowMessageBox(cIGZString *text, cIGZString *caption)
{
    HeldService window(kWindowServiceId, kWindowCanvasIid);
    const HWND owner = WindowHandle(window.Get());
    BeginModal();
    const char *const captionText = caption->ToChar();
    const char *const bodyText = text->ToChar();
    const int answer = ImportSlot<int(WINAPI *)(HWND, LPCSTR, LPCSTR, UINT), kIatMessageBoxA>()(
        owner, bodyText, captionText, kMessageBoxFlags);
    EndModal();
    return answer;
}

uint32_t cGZGraphicSystem::Slot22(uint32_t) { return 0; }
uint32_t cGZGraphicSystem::Slot23(uint32_t) { return 0; }
uint32_t cGZGraphicSystem::Slot24(uint32_t, uint32_t) { return 0; }
void cGZGraphicSystem::Slot25() {}

bool cGZGraphicSystem::GetScale(uint32_t *x, uint32_t *y)
{
    *x = 1;
    *y = 1;
    return true;
}

uint32_t cGZGraphicSystem::Slot27() { return 0; }
bool cGZGraphicSystem::Slot28(uint32_t, uint32_t, uint32_t) { return true; }
bool cGZGraphicSystem::Slot29(uint32_t, uint32_t) { return true; }
uint32_t cGZGraphicSystem::ShowCursor(uint32_t) { return 0; }
void cGZGraphicSystem::SetCursor(void *, int32_t, int32_t) {}
bool cGZGraphicSystem::UseSystemCursor() { return true; }

bool cGZGraphicSystem::GetCursorPosition(int32_t *x, int32_t *y)
{
    HeldService window(kWindowServiceId, kWindowCanvasIid);
    POINT point;
    ImportSlot<BOOL(WINAPI *)(LPPOINT), kIatGetCursorPos>()(&point);
    ImportSlot<BOOL(WINAPI *)(HWND, LPPOINT), kIatScreenToClient>()(WindowHandle(window.Get()), &point);
    *x = point.x;
    *y = point.y;
    return true;
}

uint32_t cGZGraphicSystem::ConvertRGBToNative(uint8_t r, uint8_t g, uint8_t b)
{
    return ActiveDrawBuffer()->ConvertRGBValueToNative(r, g, b);
}

bool cGZGraphicSystem::ConvertRGBToNativeAt(uint32_t *out, uint8_t r, uint8_t g, uint8_t b)
{
    *out = ActiveDrawBuffer()->ConvertRGBValueToNative(r, g, b);
    return true;
}

bool cGZGraphicSystem::ConvertNativeToRGB(uint32_t value, uint8_t *r, uint8_t *g, uint8_t *b)
{
    return ActiveDrawBuffer()->ConvertNativeValueToRGB(value, r, g, b);
}

void cGZGraphicSystem::Slot37(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
void cGZGraphicSystem::Slot38(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
void cGZGraphicSystem::Slot39(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
void cGZGraphicSystem::Slot40(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
void cGZGraphicSystem::Slot41(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
void cGZGraphicSystem::Slot42(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
void cGZGraphicSystem::Slot43(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
void cGZGraphicSystem::Slot44(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
void cGZGraphicSystem::Slot45(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
void cGZGraphicSystem::Slot46(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
void cGZGraphicSystem::Slot47(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
void cGZGraphicSystem::Slot48(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
void cGZGraphicSystem::Slot49(uint32_t) {}
float *cGZGraphicSystem::GetRate50(float *out) { *reinterpret_cast<uint32_t *>(out) = kRate60; return out; }
float *cGZGraphicSystem::GetRate51(float *out) { *reinterpret_cast<uint32_t *>(out) = kRate60; return out; }
float *cGZGraphicSystem::GetRate52(float *out) { *reinterpret_cast<uint32_t *>(out) = kRate60; return out; }
void cGZGraphicSystem::SetSettings(const cGZGraphicSystemSettings *settings) { mSettings = *settings; }
void cGZGraphicSystem::Slot54(uint32_t) {}
void cGZGraphicSystem::SetFullscreen(bool fullscreen) { mFullscreen = fullscreen; }
void cGZGraphicSystem::Slot56(uint32_t) {}
void cGZGraphicSystem::Slot57(uint32_t) {}
uint32_t cGZGraphicSystem::Slot58() { return 0; }
uint32_t cGZGraphicSystem::Slot59() { return 0; }
uint32_t cGZGraphicSystem::Slot60() { return 0; }
void cGZGraphicSystem::Slot61(uint32_t) {}
void cGZGraphicSystem::Slot62(uint32_t) {}
void cGZGraphicSystem::Slot63(uint32_t) {}
void cGZGraphicSystem::Slot64(uint32_t) {}
void cGZGraphicSystem::Slot65() {}
void cGZGraphicSystem::Slot66() {}
void cGZGraphicSystem::Slot67() {}
void cGZGraphicSystem::Slot68() {}

// ---- cIGZSystemService ----

uint32_t cGZGraphicSystem::GetServiceID() { return mServiceId; }
void cGZGraphicSystem::SetServiceID(uint32_t id) { mServiceId = id; }
int32_t cGZGraphicSystem::GetServicePriority() { return kPriority; }
bool cGZGraphicSystem::IsServiceRunning() { return mRunning; }
void cGZGraphicSystem::SetServiceRunning(bool running) { mRunning = running; }
bool cGZGraphicSystem::OnTick(uint32_t) { return true; }
bool cGZGraphicSystem::OnIdle(uint32_t) { return true; }
int32_t cGZGraphicSystem::GetServiceTickPriority() { return kPriority; }

// The font service gets a new font renderer; the system listens to cGraphicSystemDX's device state; with a size, the
// window is titled, sized, put on the current device's monitor and shown, and the display set: fullscreen in the
// desktop's mode when asked for and possible, else (or as the fallback) the mode closest to the settings with the
// formats closest to their depth. False when no format or mode fits or the mode can't be set.
bool cGZGraphicSystem::Init()
{
    HeldService fonts(kFontServiceId, kFontServiceIid);
    cIGZUnknown *renderer = nullptr;
    Com()->GetClassObject(kFontRendererClass, kFontRendererIid, reinterpret_cast<void **>(&renderer));
    RawSlot<6, void>(fonts.Get(), renderer);
    bool ok = true;
    {
        HeldService system(kGraphicSystemDXId, kGraphicSystemDXIid);
        RawSlot<8, void>(system.Get(), kDeviceStateEvent, static_cast<cGZGraphicSystemBehavior *>(this), kDeviceStateEvent);
        if (mSettings.mWidth) {
            const uint32_t width = mSettings.mWidth, height = mSettings.mHeight, bits = mSettings.mBits;
            const bool fullscreen = mFullscreen;
            HeldService window(kWindowServiceId, kWindowBehaviorIid);
            void *const win = window.Get();
            RawSlot<17, void>(win, kWindowTitle);
            RawSlot<18, void>(win, width, height);
            RawSlot<20, void>(win, CurrentDevice()->GetDisplayMonitor());
            RawSlot<15, void>(win, kWindowShowFlags, 0u);
            bool done = false;
            if (fullscreen) {
                void *const formats = FormatsManager();
                cIDeviceEnum *const devices = CurrentEnum();
                cIPixelFormat *const display = RawSlot<5, cIPixelFormat *>(formats);
                cIPixelFormat *const format = FindClosestColorFormat(devices->GetColorFormats(0, display, 0), bits, 0);
                done = format && SetDeviceDesktopMode(-1, format, 0, 1, 0, 0, 0);
            }
            if (!done) {
                ok = false;
                if (cIPixelFormat *const display = FindClosestColorFormat(CurrentEnum()->GetDisplayFormats(), bits, 0)) {
                    if (cIPixelFormat *const backBuffer = FindClosestColorFormat(CurrentEnum()->GetColorFormats(0, display, 0), bits, 0)) {
                        cDisplayMode mode = {};
                        if (FindClosestDisplayMode(&mode, CurrentEnum()->GetDisplayModes(display), width, height, kMinBits,
                                                   kMaxBits, kMinRefresh, kMaxRefresh))
                            ok = SetDeviceMode(-1, &mode, display, backBuffer, 1, 0, 0, 0);
                    }
                }
            }
        }
    }
    if (renderer) renderer->Release();
    return ok;
}

// The back buffer's surface and the draw buffer let go, the font service's renderer cleared, every connection of the
// system dropped.
bool cGZGraphicSystem::Shutdown()
{
    mBackbuffer->Shutdown();
    if (cIGZDrawContextAsBuffer *const drawBuffer = mDrawBuffer) {
        mDrawBuffer = nullptr;
        drawBuffer->Release();
    }
    HeldService fonts(kFontServiceId, kFontServiceIid);
    if (fonts.Get()) RawSlot<6, void>(fonts.Get(), static_cast<void *>(nullptr));
    GetEventManager()->DisconnectAll(static_cast<cGZGraphicSystemBehavior *>(this));
    return true;
}

} // namespace nGZGraphic4
