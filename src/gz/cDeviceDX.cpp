// nGZGraphic4::cDeviceDX (lane NC; the scene bracket was lane LK's): src/gz/cDeviceDX.h.
#include "gz/cDeviceDX.h"

#define NOMINMAX
#include <windows.h>
#include <d3d9.h>

#include <cstring>
#include <new>
#include <vector>

#include "app/WinMain.h" // nGZGraphic4::cError, nRZGraphic4::cD3DException
#include "game/game.h"
#include "game/import_slots.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cAttributeIDIterator.h"
#include "gz/cConnectionManager.h"
#include "gz/GraphicModeSearch.h"
#include "gz/GraphicServices.h"
#include "gz/cDeviceEnumDX.h"
#include "gz/cDeviceResourceManagerDX.h"
#include "gz/cDeviceStateDX.h"
#include "ifaces/cIDeviceResourceManager.h"
#include "ifaces/cIDisplayMonitor.h"
#include "ifaces/cIGZCOM.h"
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZOStream.h"
#include "ifaces/cIPixelFormatDepthStencil.h"
#include "ifaces/cIPixelFormatsManager.h"
#include "ifaces/cISurface.h"
#include "rz/MiscHelpersA.h" // LogD3DError
#include "rz/RZAutoRef.h"
#include "rz/RZFileStreams.h" // cRZTextODSStream
#include "rz/Singleton_20ddfb.h" // GetServiceObject
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject
#include "rz/cRZRefCount.h"      // RZAtomicAdd
#include "rz/cRZString.h"        // the shared empty text

namespace nGZGraphic4 {

namespace {

constexpr uint32_t kCountMask = 0x7fffffff;

constexpr IatSlot kSleepIat = &ts2_rva_d9a144;  // Sleep

// The framework's services the device asks for.
constexpr uint32_t kWindowServiceId = 0xfb6372ee, kWindowServiceIid = 0xbb6377fa;   // the window owner (WinMain.h)
constexpr uint32_t kGraphicServiceId = 0x85522803, kGraphicServiceIid = 0xc55289ba; // its slot 25(1): the IDirect3D9
constexpr uint32_t kDisplayMonitorClass = 0x7b64ce01, kDisplayMonitorIid = 0xfb64c519;

// Interface ids the device asks engine objects for.
constexpr uint32_t kVertexBufferIid = 0x998d7e9;        // the IDirect3DVertexBuffer9 of a vertex buffer
constexpr uint32_t kVertexDeclarationIid = 0x8b448b4c;  // the IDirect3DVertexDeclaration9 of a declaration
constexpr uint32_t kD3DSurfaceIid = 0x4a5a7223;         // the IDirect3DSurface9 of a surface
constexpr uint32_t kDepthStencilFormatIid = 0x5ac389b6; // cIPixelFormatDepthStencil

// Canvas message kinds HandleCanvasEvent acts on (message word 1).
constexpr uint32_t kCanvasPaint = 3;
constexpr uint32_t kCanvasKind7 = 7;

// LogD3DError lines (the original's source lines, one per report).
constexpr uint32_t kLineEndSceneOnRelease = 0x150;
constexpr uint32_t kLineProcessVertices = 0x19a;
constexpr uint32_t kLineDepthDesc = 0x1b5;
constexpr uint32_t kLineClear = 0x1c0;
constexpr uint32_t kLineClearTargets = 0x1f6;
constexpr uint32_t kLineBeginScene = 0x20e;
constexpr uint32_t kLineEndScene = 0x219;
constexpr uint32_t kLineDrawPrimitive = 0x233;
constexpr uint32_t kLineDrawIndexedPrimitive = 0x24d;
constexpr uint32_t kLineQueryIssue = 0x2ca;
constexpr uint32_t kLineSyncClear = 0x2e3;
constexpr uint32_t kLineSyncLock = 0x2e8;
constexpr uint32_t kLineSyncUnlock = 0x2ed;

// CreateDevice's.
constexpr uint32_t kLineEndSceneOnRecreate = 0x397;
constexpr uint32_t kLineEvict = 0x39e;
constexpr uint32_t kLineGetBackBuffer = 0x509;
constexpr uint32_t kLineGetDepthStencil = 0x514;
constexpr uint32_t kLineCreateQuery = 0x527;
constexpr uint32_t kLineCreateSyncSurface = 0x53b;
constexpr uint32_t kLineCreateProbeTexture = 0x54b;
constexpr uint32_t kLineSetFvf = 0x552;
constexpr uint32_t kLineClearDeclaration = 0x558;
constexpr uint32_t kLineClearDeclarationInvalid = 0x567;
constexpr uint32_t kLineFirstClear = 0x576;
constexpr uint32_t kLineFirstPresent = 0x58b;
constexpr uint32_t kLineBeginSceneOnCreate = 0x5b2;

// The COM classes of the objects CreateDevice makes for the new device (both through interface 0x5b71b5ce; slot 5
// hands them the device and their surface).
constexpr uint32_t kSwapChainClass = 0x5b75b757;      // one per back buffer: mSwapChains
constexpr uint32_t kTargetProviderClass = 0x7b75b73a; // the depth surface's: mTargetProvider
constexpr uint32_t kDeviceSurfaceIid = 0x5b71b5ce;

constexpr uint32_t kArgbFormatIid = 0x9ac38991;   // cIPixelFormatARGB
constexpr uint32_t kDepthUsage = 0xb;             // the usage CreateDevice asks depth formats for
constexpr uint32_t kMaxBackBuffers = 3;
constexpr D3DCOLOR kFirstClearColor = 0xff404080;
constexpr DWORD kNoDriverManagement = 0x100;      // D3DCREATE_DISABLE_DRIVER_MANAGEMENT
constexpr uint32_t kSoftwareDevice = 3;           // D3DDEVTYPE_SW: no query, sync surface or probe texture

constexpr DWORD kProcessVerticesFlags = 0x1800; // as the original passes them
constexpr DWORD kSyncLockFlags = D3DLOCK_NOSYSLOCK;
constexpr uint32_t kSyncSurfaceSize = 16;

// A framework service held for a scope: asked for when there is a framework, released through 0x1466e0's
// guarded release at the end (or earlier, by Release()).
class HeldService {
public:
    HeldService(uint32_t id, uint32_t iid)
    {
        mRef.mObject = nullptr;
        if (void *const framework = GetServiceObject())
            As<ifaces::cIGZFrameWork>(framework)->GetSystemService(id, iid, &mRef.mObject);
    }
    ~HeldService() { Release(); }
    HeldService(const HeldService &) = delete;
    HeldService &operator=(const HeldService &) = delete;
    void *Get() const { return mRef.mObject; }
    void Release()
    {
        mRef.ReleaseObject();
        mRef.mObject = nullptr;
    }

private:
    RZAutoRef mRef;
};

// Stores a counted reference: the new one counted, then the old one released (nothing when they are the same).
template <class T> void AssignCounted(T *&field, T *value)
{
    T *const old = field;
    if (value == old) return;
    if (value) reinterpret_cast<cIGZUnknown *>(value)->AddRef();
    field = value;
    if (old) reinterpret_cast<cIGZUnknown *>(old)->Release();
}

// `new T(...)` through a constructor called as a function (one still game code, or ours through its hook adapter): the
// memory is freed when the constructor throws, as the original's new-expressions do.
template <class Build> auto NewObject(size_t size, Build build)
{
    void *const memory = ::operator new(size);
    try {
        return build(memory);
    } catch (...) {
        ::operator delete(memory);
        throw;
    }
}

// A Direct3D reference released and cleared.
template <class T> void ReleaseCom(T *&object)
{
    if (object) object->Release();
    object = nullptr;
}

// An engine reference released and cleared (the clear first, as the original's holder reset does).
void ResetCounted(cIGZUnknown *&object)
{
    if (cIGZUnknown *const old = object) {
        object = nullptr;
        old->Release();
    }
}

// One colour component in a D3DCOLOR byte: the float plus 1.5 * 2^15 has 1/256 in its last place, so its low
// mantissa bits are the component times 256 rounded to nearest; then clamped to 0..255.
uint32_t PackComponent(float component)
{
    const float biased = component + 49152.0f;
    int32_t value;
    std::memcpy(&value, &biased, sizeof value);
    value -= 0x47400000;
    return static_cast<uint8_t>((~(value >> 31) & value) | ((255 - value) >> 31));
}

// The engine's colour (four floats: alpha, red, green, blue) as a D3DCOLOR.
D3DCOLOR PackColor(const float *color)
{
    return PackComponent(color[0]) << 24 | PackComponent(color[1]) << 16 | PackComponent(color[2]) << 8 |
           PackComponent(color[3]);
}

// A pixel format's stencil bits, through its cIPixelFormatDepthStencil face.
uint32_t StencilBits(void *format)
{
    void *face = nullptr;
    static_cast<cIGZUnknown *>(format)->QueryInterface(kDepthStencilFormatIid, &face);
    // ORIGINAL-BUG: the face is released before it is asked, and asked even when the format has none (a null call).
    if (face) static_cast<cIGZUnknown *>(face)->Release();
    return As<ifaces::cIPixelFormatDepthStencil>(face)->GetStencilBits();
}

bool HasStencilBits(void *format) { return StencilBits(format) != 0; }

// What ClearTargets is given (an nGZGraphic4::cRenderTargetsState): a state header, four colour targets, the depth
// surface.
struct RenderTargets {
    uint32_t mHeader[2];
    cIGZUnknown *mColors[4]; // +0x08
    cIGZUnknown *mDepth;     // +0x18
};
static_assert(offsetof(RenderTargets, mDepth) == 0x18, "layout");

// The id list GetEventIds hands out (cRZRefCountedT<std::vector<unsigned long>>, game vtable 0xd9bcfc): counted at
// +0x10, deleted at the last Release. The iterators (nGZEvent::cAttributeIDIterator, constructor 0x1a9942) hold a
// reference each.
class EventIdList : public cIGZUnknown {
public:
    std::vector<uint32_t> mIds; // +4
    uint32_t mRefCount = 0;     // +0x10
    bool QueryInterface(uint32_t riid, void **out) override
    {
        if (riid != 1) return false;
        *out = this;
        AddRef();
        return true;
    }
    uint32_t AddRef() override { return ++mRefCount; }
    uint32_t Release() override
    {
        if (mRefCount > 1) return --mRefCount;
        delete this;
        return 0;
    }
    virtual ~EventIdList() = default; // slot 3
};
static_assert(sizeof(EventIdList) == 0x14, "EventIdList size");

// One iterator over the list at `cursor` (ours of 0x1a9942).
nGZEvent::cIAttributeIDIterator *NewIdIterator(EventIdList *ids, const uint32_t *cursor)
{
    return nGZEvent::NewAttributeIDIterator(ids, cursor);
}

// This template instance's event attribute registry (cDeviceDX::EventRegistryFor, 0x16a170, in cDeviceDXEvents.cpp:
// its own file, so the class's tests can stand it in on both sides).
nGZEvent::cEventAttributeMap *Registry()
{
    const nGZEvent::cEventAttribute defaults{0, ts2_rva_d9b9d0, 0, 0, 0, 0};
    return cDeviceDX::EventRegistryFor(0, &defaults);
}

// The listener range of (behaviour, event) from the connection manager, released at scope end as 0x146670 does.
struct ListenerRange {
    nGZEvent::cConnectionRange r;
    ListenerRange(nGZEvent::cIConnectionManager *manager, cIGZUnknown *source, uint32_t event)
        : r(manager->ConnectionsFrom(source, event))
    {
    }
    ListenerRange(const ListenerRange &) = delete;
    ListenerRange &operator=(const ListenerRange &) = delete;
    ~ListenerRange() { nGZEvent::ReleaseIteratorRange(reinterpret_cast<cIGZUnknown *const *>(&r)); }
    bool AtEnd() const { return r.mCurrent->IsEqual(r.mEnd); }
    // The listener and the event it listens with (the original reads the cursor twice: once for each).
    nGZEvent::cIAbstractBehavior *Listener() const
    {
        return static_cast<nGZEvent::cIAbstractBehavior *>(r.mCurrent->Get().mObject);
    }
    uint32_t ListenerEvent() const { return r.mCurrent->Get().mEvent; }
    void Next() const { r.mCurrent->Next(); }
};

void SleepBriefly() { ImportSlot<void(WINAPI *)(DWORD), kSleepIat>()(100); }

[[noreturn]] void ThrowGraphicsError()
{
    cError error;
    error.mVTable = &ts2_rva_d9cd70; // cError's vtable (owned static, app/WinMain.h)
    error.mCode = 0;
    throw error;
}

// ProcessVertices' driver call under the original's __try (handler: everything): a structured exception in the call
// or in its report ends it quietly.
void GuardedProcessVertices(IDirect3DDevice9 *device, uint32_t srcStartIndex, uint32_t destIndex, uint32_t vertexCount,
                            IDirect3DVertexBuffer9 *buffer, IDirect3DVertexDeclaration9 *declaration, uint32_t file)
{
    __try {
        const HRESULT hr =
            device->ProcessVertices(srcStartIndex, destIndex, vertexCount, buffer, declaration, kProcessVerticesFlags);
        if (FAILED(hr)) LogD3DError(static_cast<uint32_t>(hr), file, kLineProcessVertices);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

uint32_t SourceFile() { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ts2_rva_d9e540)); }

// A counted copy of a range (0x167180, inline in CreateDevice): the cursor cloned, then the ends counted.
template <class Iterator> cIteratorRange<Iterator> CopyRange(const cIteratorRange<Iterator> &range)
{
    cIteratorRange<Iterator> copy;
    copy.mCurrent = range.mCurrent->Clone();
    if (copy.mCurrent) copy.mCurrent->AddRef();
    copy.mBegin = range.mBegin;
    if (copy.mBegin) copy.mBegin->AddRef();
    copy.mEnd = range.mEnd;
    if (copy.mEnd) copy.mEnd->AddRef();
    return copy;
}

template <class Iterator> void ReleaseRange(cIteratorRange<Iterator> &range)
{
    nGZEvent::ReleaseIteratorRange(reinterpret_cast<cIGZUnknown *const *>(&range));
}

bool SameMode(const cDisplayMode &a, const cDisplayMode &b)
{
    return a.mWidth == b.mWidth && a.mHeight == b.mHeight && a.mAspect == b.mAspect && a.mRefresh == b.mRefresh &&
           a.mFormat == b.mFormat;
}

// Whether `modes` lists `mode` (a counted copy walks to it; a copy of that copy is asked whether it is at the end).
bool ListsMode(const cDisplayModeRange &modes, const cDisplayMode &mode)
{
    cDisplayModeRange walk = CopyRange(modes);
    while (!walk.mCurrent->IsEqual(walk.mEnd)) {
        if (SameMode(walk.mCurrent->Get(), mode)) break;
        walk.mCurrent->Next();
    }
    cDisplayModeRange found = CopyRange(walk);
    ReleaseRange(walk);
    const bool atEnd = found.mCurrent->IsEqual(found.mEnd);
    ReleaseRange(found);
    return !atEnd;
}

// A pixel format's cIPixelFormatARGB face (released at once: the formats are static).
cIPixelFormat *ArgbFace(cIPixelFormat *format)
{
    void *face = nullptr;
    format->QueryInterface(kArgbFormatIid, &face);
    if (face) static_cast<cIGZUnknown *>(face)->Release();
    return static_cast<cIPixelFormat *>(face);
}

// The mode and depth format searches 0x18b490 / 0x18ab60 are ours (gz/GraphicModeSearch.h, lane RJ).

// 0x178b80 (ours: gz/cDeviceEnumDX.h): the enumeration refreshed for `device` (null: none yet) and the flags; gives
// the device type to create.
uint32_t RefreshDeviceEnum(cIGZUnknown *devices, IDirect3DDevice9 *device, uint32_t flags)
{
    return static_cast<cDeviceEnumDX *>(As<cIDeviceEnum>(devices))->Refresh(device, flags);
}

// The budget check before every draw: the resource manager's ManageTextureMemory once either delta passes its limit.
void CheckTextureBudget(cDeviceResourceManagerDX *manager)
{
    const auto magnitude = [](int32_t value) { return value < 0 ? 0u - static_cast<uint32_t>(value) : static_cast<uint32_t>(value); };
    if (magnitude(manager->mMemoryDelta) > manager->mLimit || magnitude(manager->mPending) > manager->mLimit)
        As<ifaces::cIDeviceResourceManager>(manager)->ManageTextureMemory();
}

} // namespace

// ---- events ----

// shared 0x155080 (slots 1 and 2).
uint32_t cDeviceEventBody::Assign(const cDeviceEventBody *other)
{
    mCode = other->mCode;
    return mCode;
}

uint32_t cDeviceEventBody::AssignAlias(const cDeviceEventBody *other)
{
    mCode = other->mCode;
    return mCode;
}

uint32_t cDeviceEventBody::GetSize() const { return sizeof(cDeviceEventBody); }
uint32_t cDeviceEventBody::GetBodySize() const { return sizeof(mCode); }

uint32_t cDeviceEvent::GetEventType() const { return kType; }
void cDeviceEvent::CloneInto(void *memory) const { ::new (memory) cDeviceEvent(mCode); }

uint32_t cDeviceRenderSceneEvent::GetEventType() const { return kType; }
void cDeviceRenderSceneEvent::CloneInto(void *memory) const { ::new (memory) cDeviceRenderSceneEvent(mCode); }

// ---- construction ----

// 0x167200: everything empty, the display mode the process default (0x10ca208), one back buffer. The two bytes at
// +0x76 are not written.
cDeviceDX::cDeviceDX()
    : mRefCount(0), mAdapter(0), mDevice(nullptr), mDeviceEnum(nullptr), mResourceManager(nullptr), mState(nullptr),
      mDisplayMonitor(nullptr), mSwapChainCount(0), mSwapChains{}, mTargetProvider(nullptr), mGpuQuery(nullptr),
      mSyncSurface(nullptr), mProbeTexture(nullptr), mSceneDepth(0), mFullscreen(false), mDeviceLost(false),
      mDeviceFailed(false), mReleased(false), mByte74(false), mCreationBlocked(false)
{
    std::memcpy(&mSettings, &ts2_rva_10ca208, offsetof(cDeviceSettings, mColorFormat));
    mSettings.mColorFormat = nullptr;
    mSettings.mDepthFormat = nullptr;
    mSettings.mBackBufferCount = 1;
    mSettings.mMultisampleType = 0;
    mSettings.mMultisampleQuality = 0;
    mSettings.mFlags = 0;
}

// 0x167340: the device released when there is one, then every reference in reverse order of the fields.
cDeviceDX::~cDeviceDX()
{
    if (mDevice) ReleaseDevice();
    ReleaseCom(mProbeTexture);
    ReleaseCom(mSyncSurface);
    ReleaseCom(mGpuQuery);
    if (mTargetProvider) mTargetProvider->Release();
    for (int i = 2; i >= 0; --i)
        if (mSwapChains[i]) mSwapChains[i]->Release();
    if (mDisplayMonitor) mDisplayMonitor->Release();
    if (mState) mState->Release();
    if (mResourceManager) reinterpret_cast<cIGZUnknown *>(mResourceManager)->Release();
    if (mDeviceEnum) mDeviceEnum->Release();
    ReleaseCom(mDevice);
}

// 0x169cc0.
bool cDeviceDX::Init(uint32_t adapter)
{
    try {
        IDirect3D9 *direct3d;
        {
            HeldService graphics(kGraphicServiceId, kGraphicServiceIid);
            direct3d = RawSlot<25, IDirect3D9 *>(graphics.Get(), 1u);
        }
        HMONITOR const monitor = direct3d->GetAdapterMonitor(adapter);
        void *const com = GetDirectorFrameworkObject();
        ResetCounted(mDisplayMonitor);
        As<ifaces::cIGZCOM>(com)->GetClassObject(kDisplayMonitorClass, kDisplayMonitorIid, &mDisplayMonitor);
        As<ifaces::cIDisplayMonitor>(mDisplayMonitor)->SetMonitor(monitor);
        mAdapter = adapter;

        AssignCounted(mDeviceEnum, NewObject(sizeof(cDeviceEnumDX), [&](void *memory) {
                          return static_cast<cIGZUnknown *>(cDeviceEnumDXAdapters::Construct(memory, nullptr, adapter, this));
                      }));
        AssignCounted(mResourceManager, NewObject(cDeviceResourceManagerDX::kSize, [&](void *memory) {
                          return cDeviceResourceManagerAdapters::Construct(memory, nullptr, this);
                      }));
        AssignCounted(mState, NewObject(sizeof(cDeviceStateDX), [&](void *memory) {
                          return cDeviceStateDXAdapters::Construct(memory, nullptr, this, mDeviceEnum);
                      }));
    } catch (const nRZGraphic4::cD3DException &) {
        return false;
    }
    RefreshDeviceEnum(mDeviceEnum, nullptr, mSettings.mFlags);
    As<cIDeviceEnum>(mDeviceEnum)->GetAdapterIdentifier(); // copied out and dropped
    return true;
}

cDeviceDX *__fastcall cDeviceDXAdapters::Construct(void *memory, void *) { return ::new (memory) cDeviceDX; }

bool __fastcall cDeviceDXAdapters::Init(cDeviceDX *device, void *, uint32_t adapter) { return device->Init(adapter); }

void cDeviceDX::ReportFailure(long hr, uint32_t line) { LogD3DError(static_cast<uint32_t>(hr), SourceFile(), line); }

// ---- CreateDevice ----

const cDisplayMode &cDeviceDX::DefaultMode() { return ts2_rva_10ca208; }

bool cDeviceDX::IsDefaultMode(const cDisplayMode &mode) { return SameMode(mode, DefaultMode()); }

// What CreateDevice chooses on the way (its locals in the original).
struct cDeviceRequest {
    void *window;                   // the window owner service (held by CreateDevice)
    cDisplayMode mode;              // the display mode (the settings', or the closest listed one)
    cIPixelFormat *displayFormat;   // the format colour and depth are checked against
    cIPixelFormat *color;           // the back buffer format
    cIPixelFormat *depth;           // the depth format
    uint32_t deviceType;            // D3DDEVTYPE (0x178b80)
    DWORD behavior;                 // D3DCREATE_* flags
    D3DPRESENT_PARAMETERS present;  // each Reset / CreateDevice gets a copy
};

// The settings' mode when the enumeration lists it, else the closest one to it, to it at 16-32 bits, or to 1280x720
// (0x18b490, written into the request). False (the device released) when there is none.
bool cDeviceDX::ChooseDisplayMode(cDeviceRequest &request)
{
    cDisplayMode &mode = request.mode;
    cDisplayModeRange modes = As<cIDeviceEnum>(GetDeviceEnumConst())->GetDisplayModes(0);
    if (!ListsMode(modes, mode)) {
        cIPixelFormat *const argb = ArgbFace(mode.mFormat);
        const uint32_t bits = argb ? static_cast<uint32_t>(argb->GetBitsPerPixel()) : 32;
        const uint32_t width = mode.mWidth, height = mode.mHeight;
        if (!FindClosestDisplayMode(&mode, CopyRange(modes), width, height, bits, bits, mode.mRefresh, 1000) &&
            !FindClosestDisplayMode(&mode, CopyRange(modes), width, height, 16, 32, 0, 1000) &&
            !FindClosestDisplayMode(&mode, CopyRange(modes), 1280, 720, 16, 32, 0, 1000)) {
            ReleaseDevice();
            ReleaseRange(modes);
            return false;
        }
    }
    ReleaseRange(modes);
    return true;
}

// The settings' colour format when the enumeration supports it on the display format, else the listed ARGB format with
// the most bits (the first of equals); the settings' depth format when supported, else the deepest listed one with the
// settings' stencil bits, or with any. False (the device released) when either is missing.
bool cDeviceDX::ChooseFormats(cDeviceRequest &request)
{
    cIDeviceEnum *const devices = As<cIDeviceEnum>(mDeviceEnum);
    cIPixelFormat *color = mSettings.mColorFormat;
    cIPixelFormat *depth = mSettings.mDepthFormat;
    request.deviceType = RefreshDeviceEnum(mDeviceEnum, nullptr, mSettings.mFlags);
    if (!color || !devices->IsColorFormatSupported(color, 0, request.displayFormat)) {
        color = nullptr;
        cPixelFormatRange formats = devices->GetColorFormats(0, request.displayFormat, kArgbFormatIid);
        while (!formats.mCurrent->IsEqual(formats.mEnd)) {
            if (!color) {
                color = formats.mCurrent->Get();
            } else {
                // Both faces asked without a null check: the range lists ARGB formats only.
                cIPixelFormat *const best = ArgbFace(color);
                const int32_t bits = ArgbFace(formats.mCurrent->Get())->GetBitsPerPixel();
                if (best->GetBitsPerPixel() < bits) color = formats.mCurrent->Get();
            }
            formats.mCurrent->Next();
        }
        ReleaseRange(formats);
        if (!color) {
            ReleaseDevice();
            return false;
        }
    }
    if (!depth || !devices->IsDepthFormatSupported(depth, kDepthUsage, request.displayFormat, color)) {
        cPixelFormatRange formats = devices->GetDepthFormats(kDepthUsage, request.displayFormat, color);
        const uint32_t stencil = depth ? StencilBits(depth) : 0;
        depth = FindClosestDepthFormat(CopyRange(formats), 0, stencil, false);
        if (!depth && stencil) depth = FindClosestDepthFormat(CopyRange(formats), 0, 0, false);
        ReleaseRange(formats);
    }
    if (!depth) {
        ReleaseDevice();
        return false;
    }
    request.color = color;
    request.depth = depth;
    return true;
}

// Before the device is reset or made again: listeners told (code 0), an open scene ended and the state cache sent and
// reset, managed resources evicted (all only with a usable device), the GPU waited for, and the device's objects
// released while the device counts as released.
void cDeviceDX::ReleaseForRecreate(const cDeviceRequest &request)
{
    {
        cDeviceEvent event(cDeviceEvent::kRecreating);
        NotifyListeners(cDeviceEvent::kEventId, 0, &event);
    }
    if (IsDeviceReady()) {
        if (mSceneDepth) {
            const HRESULT hr = mDevice->EndScene();
            if (FAILED(hr)) ReportFailure(hr, kLineEndSceneOnRecreate);
        }
        mState->Reset(cDeviceStateDX::kFlushAll);
        mState->Flush(cDeviceStateDX::kFlushAll);
        const HRESULT hr = mDevice->EvictManagedResources();
        if (FAILED(hr)) ReportFailure(hr, kLineEvict);
    }
    WaitForGpu();
    mDeviceFailed = false;
    mReleased = true;
    mDeviceLost = false;
    RawSlot<38>(request.window, 1u);
    RawSlot<22>(request.window, 1u);
    mState->ReleaseDeviceObjects();
    ReleaseFrameObjects();
    mResourceManager->ReleaseNonSurvivors();
    if (!IsDefaultMode(request.mode)) RawSlot<37>(request.window, 1u);
}

// Swapping, multisampling and back buffers from the flags and the enumeration (flip when discarding and the device can,
// the settings' multisampling clamped by 0x18b7d0 when discarding, one back buffer when copying); the default mode is
// windowed at the window's size. Then the behaviour flags: software processing without either hardware capability,
// mixed when the flags ask for it or one is missing, else hardware (pure with capability 7); driver management off
// without feature 2.
void __cdecl ClampMultisample(uint32_t *type, uint32_t *quality, cIDeviceEnum *devices, cIPixelFormat *displayFormat,
                              cIPixelFormat *color)
{
    // Signed throughout, as the settings' values are compared in the game. The quality is reread after each query.
    if (int32_t(*type) == 1) {
        const int32_t levels = devices->GetMultisampleQualities(1, displayFormat, color);
        const int32_t kept = levels < int32_t(*quality) ? levels : int32_t(*quality);
        *quality = uint32_t(kept);
        if (kept == -1) {
            *type = 0;
            *quality = 0;
        }
        return;
    }
    int32_t candidate = int32_t(*type) > 16 ? 16 : int32_t(*type);
    for (;;) {
        *type = uint32_t(candidate);
        if (candidate != 1) {
            const int32_t levels = devices->GetMultisampleQualities(uint32_t(candidate), displayFormat, color);
            if (levels != -1) {
                if (levels < int32_t(*quality)) *quality = uint32_t(levels);
                return;
            }
        }
        if (*type == 0) return;
        candidate = int32_t(*type) - 1;
    }
}

void cDeviceDX::BuildPresentParameters(cDeviceRequest &request)
{
    cIDeviceEnum *const devices = As<cIDeviceEnum>(mDeviceEnum);
    const uint32_t flags = mSettings.mFlags;
    D3DSWAPEFFECT swap = (flags & kFlagCopySwap) ? D3DSWAPEFFECT_COPY : D3DSWAPEFFECT_DISCARD;
    uint32_t multisampleType = 0, multisampleQuality = 0;
    uint32_t backBuffers;
    if (swap == D3DSWAPEFFECT_DISCARD && devices->HasFeature(4)) {
        swap = D3DSWAPEFFECT_FLIP;
        backBuffers = mSettings.mBackBufferCount < kMaxBackBuffers ? mSettings.mBackBufferCount : kMaxBackBuffers;
    } else if (swap == D3DSWAPEFFECT_DISCARD) {
        multisampleType = mSettings.mMultisampleType;
        multisampleQuality = mSettings.mMultisampleQuality;
        ClampMultisample(&multisampleType, &multisampleQuality, devices, request.displayFormat, request.color);
        backBuffers = mSettings.mBackBufferCount < kMaxBackBuffers ? mSettings.mBackBufferCount : kMaxBackBuffers;
    } else {
        backBuffers = 1;
    }
    const DWORD presentFlags = (flags & kFlagNoDeviceClip) ? 0 : D3DPRESENTFLAG_DEVICECLIP;
    UINT interval = D3DPRESENT_INTERVAL_IMMEDIATE;
    if (flags & kFlagAskImmediate) interval = devices->HasFeature(3) ? D3DPRESENT_INTERVAL_IMMEDIATE : D3DPRESENT_INTERVAL_ONE;

    D3DPRESENT_PARAMETERS &pp = request.present;
    const bool windowed = IsDefaultMode(request.mode);
    pp.BackBufferWidth = windowed ? 0 : request.mode.mWidth;
    pp.BackBufferHeight = IsDefaultMode(request.mode) ? 0 : request.mode.mHeight;
    pp.BackBufferFormat = static_cast<D3DFORMAT>(request.color->GetFormatId());
    pp.BackBufferCount = backBuffers;
    pp.MultiSampleType = static_cast<D3DMULTISAMPLE_TYPE>(multisampleType);
    pp.MultiSampleQuality = multisampleQuality;
    pp.SwapEffect = swap;
    pp.hDeviceWindow = RawSlot<40, HWND>(request.window);
    pp.Windowed = IsDefaultMode(request.mode);
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = static_cast<D3DFORMAT>(request.depth->GetFormatId());
    pp.Flags = presentFlags;
    pp.FullScreen_RefreshRateInHz = request.mode.mRefresh;
    pp.PresentationInterval = interval;

    DWORD behavior;
    if (!devices->HasProcessingCap(0) && !devices->HasProcessingCap(2))
        behavior = D3DCREATE_SOFTWARE_VERTEXPROCESSING;
    else if ((flags & kFlagMixedProcessing) || !devices->HasProcessingCap(0) || !devices->HasProcessingCap(2))
        behavior = D3DCREATE_MIXED_VERTEXPROCESSING;
    else
        behavior = D3DCREATE_HARDWARE_VERTEXPROCESSING;
    if (devices->HasProcessingCap(7) && (behavior & D3DCREATE_HARDWARE_VERTEXPROCESSING)) behavior |= D3DCREATE_PUREDEVICE;
    if (!devices->HasFeature(2)) behavior |= kNoDriverManagement;
    request.behavior = behavior;
}

// The existing device reset with the parameters, dropping multisampling, then extra back buffers, on each failure; a
// windowed reset of a fullscreen device is done twice. A lost device (or one that cannot reset with one plain back
// buffer) is reported lost; a failed device is then given up (released, with its objects and the manager's), so
// CreateDevice makes a new one.
long cDeviceDX::ResetD3DDevice(cDeviceRequest &request)
{
    D3DPRESENT_PARAMETERS &pp = request.present;
    HRESULT hr = D3D_OK;
    while (mDevice) {
        D3DPRESENT_PARAMETERS copy = pp;
        hr = mDevice->Reset(&copy);
        if (hr == D3D_OK) {
            if (!IsDefaultMode(request.mode) || !mFullscreen) break;
            mFullscreen = false;
            RawSlot<37>(request.window, 0u);
            continue;
        }
        bool giveUp = false;
        if (hr == D3DERR_DEVICELOST || hr == D3DERR_DRIVERINTERNALERROR) {
            OnDeviceLost();
            giveUp = true;
        } else if (pp.MultiSampleType == 0 && pp.MultiSampleQuality == 0) {
            if (pp.BackBufferCount == 1) {
                OnDeviceLost();
                if (FAILED(hr)) giveUp = true;
                else break;
            } else {
                pp.BackBufferCount = 1;
                continue;
            }
        } else {
            pp.MultiSampleType = D3DMULTISAMPLE_NONE;
            pp.MultiSampleQuality = 0;
            continue;
        }
        if (giveUp) {
            mDeviceFailed = true;
            mState->ReleaseDeviceObjects();
            mResourceManager->ReleaseAll();
            ReleaseCom(mDevice);
        }
        break;
    }
    return hr;
}

// A new device from the graphic service's IDirect3D9 for the device type. A failure asks the window whether to fall
// back to the desktop mode (then the settings take the default mode and the window its size, and creation is tried
// again windowed); then drops multisampling, then extra back buffers, and tries again, until one plain back buffer
// fails too or the device is lost (reported). The manager reads the new device's memory.
long cDeviceDX::CreateD3DDevice(cDeviceRequest &request)
{
    D3DPRESENT_PARAMETERS &pp = request.present;
    HeldService graphics(kGraphicServiceId, kGraphicServiceIid);
    IDirect3D9 *const direct3d = RawSlot<25, IDirect3D9 *>(graphics.Get(), request.deviceType);
    IDirect3DDevice9 *created = nullptr;
    const auto create = [&] {
        D3DPRESENT_PARAMETERS copy = pp;
        HWND const window = RawSlot<40, HWND>(request.window);
        return direct3d->CreateDevice(mAdapter, static_cast<D3DDEVTYPE>(request.deviceType), window, request.behavior,
                                      &copy, &created);
    };
    HRESULT hr;
    for (;;) {
        hr = create();
        if (hr != D3D_OK && RawSlot<39, bool>(request.window)) {
            RawSlot<37>(request.window, 0u);
            mSettings.mMode = DefaultMode();
            request.mode = DefaultMode();
            RawSlot<18>(request.window, request.mode.mWidth, request.mode.mHeight);
            pp.BackBufferWidth = 0;
            pp.BackBufferHeight = 0;
            pp.Windowed = IsDefaultMode(request.mode);
            pp.FullScreen_RefreshRateInHz = request.mode.mRefresh;
            hr = create();
        }
        if (hr == D3D_OK) {
            if (mDevice) mDevice->Release();
            mDevice = created;
            break;
        }
        if (hr == D3DERR_DEVICELOST) {
            OnDeviceLost();
            return hr;
        }
        if (pp.MultiSampleType == 0 && pp.MultiSampleQuality == 0) {
            if (pp.BackBufferCount == 1) break;
            pp.BackBufferCount = 1;
        } else {
            pp.MultiSampleType = D3DMULTISAMPLE_NONE;
            pp.MultiSampleQuality = 0;
        }
    }
    // ORIGINAL-BUG: a positive (success) code other than D3D_OK from the last try reaches here without a device.
    if (SUCCEEDED(hr)) mResourceManager->TakeDevice(mDevice);
    return hr;
}

// The device came up: the flags settle, the enumeration and the manager learn of it, each back buffer gets a swap chain
// object and the depth surface a target provider (both COM objects of the director), and a hardware device the event
// query (or the sync surface without one) and the DXT1 probe texture when it has those features. The state cache sets
// up the device; the FVF is set and the device validated. A new or fullscreen device is then cleared and presented
// once per swap chain, plus once.
void cDeviceDX::TakeNewDevice(const cDeviceRequest &request, IDirect3DDevice9 *before)
{
    cIDeviceEnum *const devices = As<cIDeviceEnum>(mDeviceEnum);
    mDeviceLost = false;
    mDeviceFailed = false;
    mFullscreen = !IsDefaultMode(request.mode);
    RefreshDeviceEnum(mDeviceEnum, mDevice, mSettings.mFlags);
    mResourceManager->ReleaseOnModeChange(); // the original is passed the device and the mode, and reads neither
    mSwapChainCount = request.present.BackBufferCount;
    for (uint32_t i = 0; i < mSwapChainCount; ++i) {
        cIGZUnknown *chain = nullptr;
        As<ifaces::cIGZCOM>(GetDirectorFrameworkObject())->GetClassObject(kSwapChainClass, kDeviceSurfaceIid,
                                                                          reinterpret_cast<void **>(&chain));
        IDirect3DSurface9 *backBuffer = nullptr;
        const HRESULT hr = mDevice->GetBackBuffer(0, i, D3DBACKBUFFER_TYPE_MONO, &backBuffer);
        if (FAILED(hr)) ReportFailure(hr, kLineGetBackBuffer);
        RawSlot<5>(chain, this, backBuffer);
        backBuffer->Release();
        AssignCounted(mSwapChains[i], chain);
        if (chain) chain->Release();
    }
    IDirect3DSurface9 *depth = nullptr;
    HRESULT hr = mDevice->GetDepthStencilSurface(&depth);
    if (FAILED(hr)) ReportFailure(hr, kLineGetDepthStencil);
    void *const com = GetDirectorFrameworkObject();
    ResetCounted(mTargetProvider);
    As<ifaces::cIGZCOM>(com)->GetClassObject(kTargetProviderClass, kDeviceSurfaceIid,
                                             reinterpret_cast<void **>(&mTargetProvider));
    RawSlot<5>(mTargetProvider, this, depth);
    depth->Release();

    if (request.deviceType != kSoftwareDevice) {
        if (devices->HasFeature(0)) {
            ReleaseCom(mGpuQuery);
            hr = mDevice->CreateQuery(D3DQUERYTYPE_EVENT, &mGpuQuery);
            if (hr != D3D_OK) {
                if (hr != D3DERR_NOTAVAILABLE && FAILED(hr)) ReportFailure(hr, kLineCreateQuery);
                ReleaseCom(mGpuQuery);
            }
        }
        if (!mGpuQuery) {
            ReleaseCom(mSyncSurface);
            hr = mDevice->CreateRenderTarget(kSyncSurfaceSize, kSyncSurfaceSize,
                                             static_cast<D3DFORMAT>(request.color->GetFormatId()), D3DMULTISAMPLE_NONE, 0,
                                             TRUE, &mSyncSurface, nullptr);
            if (FAILED(hr)) ReportFailure(hr, kLineCreateSyncSurface);
        }
        if (devices->HasFeature(6)) {
            ReleaseCom(mProbeTexture);
            hr = mDevice->CreateTexture(8, 8, 1, 0, D3DFMT_DXT1, D3DPOOL_DEFAULT, &mProbeTexture, nullptr);
            if (FAILED(hr)) ReportFailure(hr, kLineCreateProbeTexture);
        }
    }
    mState->SetUpDevice(mDevice);
    hr = mDevice->SetFVF(D3DFVF_XYZ);
    if (FAILED(hr)) ReportFailure(hr, kLineSetFvf);

    DWORD passes;
    const HRESULT valid = mDevice->ValidateDevice(&passes);
    if (valid == D3DERR_DEVICELOST) {
        OnDeviceLost();
        return;
    }
    if (valid == D3DERR_DRIVERINTERNALERROR) ThrowGraphicsError();
    hr = mDevice->SetVertexDeclaration(nullptr);
    if (FAILED(hr)) ReportFailure(hr, valid == D3D_OK ? kLineClearDeclaration : kLineClearDeclarationInvalid);
    if (FAILED(valid)) return;

    if (!mFullscreen && before) return;
    HRESULT presented = valid;
    for (uint32_t i = 0; i < mSwapChainCount + 1; ++i) {
        hr = mDevice->Clear(0, nullptr, D3DCLEAR_TARGET, kFirstClearColor, 1.0f, 0);
        if (FAILED(hr)) ReportFailure(hr, kLineFirstClear);
        presented = mDevice->Present(nullptr, nullptr, nullptr, nullptr);
        if (FAILED(presented)) break;
    }
    if (presented == D3DERR_DEVICELOST)
        OnDeviceLost();
    else if (presented == D3DERR_DRIVERINTERNALERROR)
        ThrowGraphicsError();
    else if (FAILED(presented))
        ReportFailure(presented, kLineFirstPresent);
}

bool cDeviceDX::CreateDevice()
{
    if (mCreationBlocked) return true;
    cIDeviceEnum *const devices = As<cIDeviceEnum>(mDeviceEnum);
    devices->GetAdapterIdentifier(); // copied out and dropped
    if (IsDefaultMode(mSettings.mMode)) devices->GetAdapterDisplayMode(); // dropped too

    HeldService window(kWindowServiceId, kWindowServiceIid);
    cDeviceRequest request{};
    request.window = window.Get();
    request.mode = mSettings.mMode;
    request.displayFormat = request.mode.mFormat;
    if (!IsDefaultMode(request.mode)) {
        if (!ChooseDisplayMode(request)) return false;
        request.displayFormat = request.mode.mFormat;
    }
    // The default mode (also when the search came back to it) is shown in the desktop's format.
    if (IsDefaultMode(request.mode))
        request.displayFormat = RawSlot<5, cIPixelFormat *>(GetPixelFormatsManagerCached());
    if (!ChooseFormats(request)) return false;

    ReleaseForRecreate(request);
    BuildPresentParameters(request);
    IDirect3DDevice9 *const before = mDevice;
    HRESULT hr = ResetD3DDevice(request);
    if (!mDevice) hr = CreateD3DDevice(request);
    mReleased = false;
    if (SUCCEEDED(hr)) TakeNewDevice(request, before);

    RawSlot<22>(request.window, 1u);
    RawSlot<38>(request.window, 0u);
    if (!IsDeviceReady() || mDeviceLost) {
        RawSlot<26>(request.window);
        return false;
    }
    cDeviceEvent created(cDeviceEvent::kCreated);
    NotifyListeners(cDeviceEvent::kEventId, 0, &created);
    cDeviceEvent reset(cDeviceEvent::kReset);
    NotifyListeners(cDeviceEvent::kEventId, 0, &reset);
    if (mSceneDepth) {
        hr = mDevice->BeginScene();
        if (FAILED(hr)) ReportFailure(hr, kLineBeginSceneOnCreate);
    }
    return true;
}

// ---- cIGZUnknown ----

// 0x16a110: the device under its own ids and the behaviour's (counted), the behaviour face (not counted), and the
// IDirect3DDevice9 itself (not counted).
bool cDeviceDX::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid == kIidD3DDevice) {
        *out = mDevice;
        return true;
    }
    if (riid == kIidDeviceDX || riid == kIidDevice || riid == 1 || riid == kIidBehavior2) {
        *out = this;
        AddRef();
        return true;
    }
    if (riid == nGZEvent::cIAbstractBehaviorType::kIid) {
        *out = this;
        return true;
    }
    return false;
}

// shared 0x146a20 (cRefMultiThreaded): the low 31 bits count, atomically.
uint32_t cDeviceDX::AddRef()
{
    RZAtomicAdd(reinterpret_cast<volatile int32_t *>(&mRefCount), 1);
    return mRefCount & kCountMask;
}

// 0x16a3b0: the last reference disconnects the device from everything (when there is a manager), then deletes it.
uint32_t cDeviceDX::Release()
{
    if ((mRefCount & kCountMask) == 1) {
        if (GetEventManager()) GetEventManager()->DisconnectAll(this);
        delete this;
        return 0;
    }
    RZAtomicAdd(reinterpret_cast<volatile int32_t *>(&mRefCount), -1);
    return mRefCount & kCountMask;
}

// ---- the behaviour ----

// 0x167e00.
nGZEvent::cIConnectionManager *cDeviceDX::GetEventManager()
{
    nGZEvent::cIConnectionManager *&cell = ts2_rva_10ca22c;
    if (!cell) {
        nGZEvent::cEventServiceRef service;
        if (service.mRef.mObject) cell = service.GetConnectionManager();
    }
    return cell;
}

// 0x169bf0: a connection change keeps the active bit in step; a canvas message is a frame; a window resize recreates
// the device when the back buffer no longer has the client size (and reports the device lost when that fails).
void cDeviceDX::OnEvent(uint32_t type, uint32_t, void *data, uint32_t)
{
    if (type == nGZEvent::cConnectionChangedEvent::kType) {
        const bool listened = GetEventManager()->HasConnectionsFrom(this);
        if (static_cast<uint8_t>(mRefCount >> 31) != static_cast<uint8_t>(listened))
            RZAtomicAdd(reinterpret_cast<volatile int32_t *>(&mRefCount), static_cast<int32_t>(0x80000000u));
        return;
    }
    if (type == kCanvasEvent) {
        HandleCanvasEvent(static_cast<const uint32_t *>(data));
        return;
    }
    if (type != kResizeEvent || !IsDeviceReady() || mDeviceLost) return;
    const uint32_t *const resize = static_cast<const uint32_t *>(data); // +4 the client rect, +0x14 flags
    if (resize[5] & 0x18) return;
    const uint32_t width = As<ifaces::cISurface>(GetBackBuffer(0))->GetWidth();
    const uint32_t height = As<ifaces::cISurface>(GetBackBuffer(0))->GetHeight();
    if ((width != resize[3] - resize[1] || height != resize[4] - resize[2]) && !CreateDevice()) OnDeviceLost();
}

// shared 0x147ce0.
bool cDeviceDX::OnQuery(uint32_t type, uint32_t arg1, void *data) { return AskListeners(type, arg1, data); }

// 0x16a430: every listener of `type` gets (its event, arg1, data, this) through its slot 3, while active.
void cDeviceDX::NotifyListeners(uint32_t type, uint32_t arg1, void *data)
{
    if (static_cast<int32_t>(mRefCount) >= 0) return;
    ListenerRange range(GetEventManager(), this, type);
    while (!range.AtEnd()) {
        nGZEvent::cIAbstractBehavior *const listener = range.Listener();
        listener->OnEvent(range.ListenerEvent(), arg1, data, reinterpret_cast<uintptr_t>(static_cast<cIGZUnknown *>(this)));
        range.Next();
    }
}

// 0x169890: true when there is a listener of `type` and every one answered its slot 4 with true (all are asked).
bool cDeviceDX::AskListeners(uint32_t type, uint32_t arg1, void *data)
{
    if (static_cast<int32_t>(mRefCount) >= 0) return false;
    bool any = false;
    bool every = true;
    {
        ListenerRange range(GetEventManager(), this, type);
        while (!range.AtEnd()) {
            nGZEvent::cIAbstractBehavior *const listener = range.Listener();
            const bool yes = listener->OnQuery(range.ListenerEvent(), arg1, data);
            every = yes && every;
            any = true;
            range.Next();
        }
    }
    return every && any;
}

// 0x167d60.
void cDeviceDX::Connect(uint32_t event, cIGZUnknown *target, uint32_t targetEvent)
{
    GetEventManager()->Connect(this, event, target, targetEvent);
}

// 0x1694d0.
void cDeviceDX::Disconnect(uint32_t event, cIGZUnknown *target, uint32_t targetEvent)
{
    GetEventManager()->Disconnect(this, event, target, targetEvent);
}

// shared 0x147cf0.
uint32_t cDeviceDX::IsActive() { return mRefCount >> 31; }

// 0x169f10.
uint32_t cDeviceDX::GetEventCount() { return static_cast<uint32_t>(Registry()->size()); }

// 0x1674e0: every registered id, in order (the end iterator is made first).
nGZEvent::cAttributeIDRange cDeviceDX::GetEventIds()
{
    EventIdList *const ids = new EventIdList;
    ids->AddRef();
    for (auto it = Registry()->begin(); it != Registry()->end(); ++it) ids->mIds.push_back(it->first);
    nGZEvent::cIAttributeIDIterator *const last = NewIdIterator(ids, ids->mIds.data() + ids->mIds.size());
    nGZEvent::cIAttributeIDIterator *const first = NewIdIterator(ids, ids->mIds.data());
    const nGZEvent::cAttributeIDRange range = nGZEvent::MakeIteratorRange(first, last);
    ids->Release();
    return range;
}

// 0x167750.
nGZEvent::cEventAttribute cDeviceDX::GetEventAttribute(uint32_t event)
{
    const auto found = Registry()->find(event);
    return found != Registry()->end() ? found->second : ts2_rva_fecaf8;
}

// ---- cIDeviceDX: getters ----

cIGZUnknown *cDeviceDX::GetDeviceEnum() { return mDeviceEnum; }
cIGZUnknown *cDeviceDX::GetDeviceEnumConst() { return mDeviceEnum; }
cIGZUnknown *cDeviceDX::GetDeviceEnum44() { return mDeviceEnum; }
cIGZUnknown *cDeviceDX::GetDeviceEnum45() { return mDeviceEnum; }
cDeviceResourceManagerDX *cDeviceDX::GetResourceManagerConst() { return mResourceManager; }
cDeviceResourceManagerDX *cDeviceDX::GetResourceManager() { return mResourceManager; }
cDeviceStateDX *cDeviceDX::GetState() { return mState; }
cDeviceStateDX *cDeviceDX::GetStateConst() { return mState; }
cIGZUnknown *cDeviceDX::GetDisplayMonitor() { return mDisplayMonitor; }
cIGZUnknown *cDeviceDX::GetDisplayMonitorConst() { return mDisplayMonitor; }
IDirect3DDevice9 *cDeviceDX::GetD3DDevice() { return mDevice; }
uint32_t cDeviceDX::GetSwapChainCount() { return mSwapChainCount; }

// 0x16a590.
cDeviceSettings cDeviceDX::GetSettings() { return mSettings; }

// 0x16a800.
bool cDeviceDX::HasDeviceOrLost() { return mDevice || mDeviceLost; }

// 0x169ef0.
bool cDeviceDX::IsDeviceReady() { return mDevice && !mDeviceFailed && !mReleased; }

// 0x169ed0.
bool cDeviceDX::InScene() { return IsDeviceReady() ? mSceneDepth != 0 : false; }

// 0x169eb0.
bool cDeviceDX::IsFullscreen() { return IsDeviceReady() ? mFullscreen : false; }

// 0x167830: the swap chain's back buffer (its slot 4).
cIGZUnknown *cDeviceDX::GetBackBuffer(uint32_t swapChain) { return RawSlot<4, cIGZUnknown *>(mSwapChains[swapChain]); }

// 0x167840.
cIGZUnknown *cDeviceDX::GetTargetProviderSurface() { return RawSlot<4, cIGZUnknown *>(mTargetProvider); }

// 0x16a410: the size of the render target the state cache holds.
cDeviceTargetSize cDeviceDX::GetTargetSize() { return {mState->mWanted.targetWidth, mState->mWanted.targetHeight}; }

// ---- cIDeviceDX: settings and the device's life ----

// 0x167ce0.
bool cDeviceDX::SetFlags(uint32_t flags)
{
    if (mSettings.mFlags == flags) return true;
    if (IsDeviceReady() && !mDeviceLost) {
        cDeviceSettings settings = mSettings;
        settings.mFlags = flags;
        return ApplySettings(&settings);
    }
    // ORIGINAL-BUG: without a usable device the new flags are dropped; the enumeration is told the old ones again.
    RefreshDeviceEnum(mDeviceEnum, nullptr, mSettings.mFlags);
    return true;
}

// 0x16a7d0.
void cDeviceDX::SetCreationBlocked(bool blocked) { mCreationBlocked = blocked; }

// 0x16a520: new flags release the device first; the settings are taken, then the device made. A failure with a
// device made before tries once more from a released device; a final failure leaves it released.
bool cDeviceDX::ApplySettings(const cDeviceSettings *settings)
{
    if (mSettings.mFlags != settings->mFlags) ReleaseDevice();
    mSettings = *settings;
    IDirect3DDevice9 *const before = mDevice;
    if (CreateDevice()) return true;
    if (before) {
        ReleaseDevice();
        if (CreateDevice()) return true;
    }
    ReleaseDevice();
    return false;
}

// 0x169690: once per loss: listeners told (event code 4), then the window owner service's slot 26.
void cDeviceDX::OnDeviceLost()
{
    if (mDeviceLost) return;
    mDeviceLost = true;
    {
        cDeviceEvent event(cDeviceEvent::kLost);
        NotifyListeners(cDeviceEvent::kEventId, 0, &event);
    }
    HeldService window(kWindowServiceId, kWindowServiceIid);
    RawSlot<26>(window.Get()); // the service is assumed
}

// 0x16a5c0.
void cDeviceDX::ReleaseDevice()
{
    HeldService window(kWindowServiceId, kWindowServiceIid);
    if (HasDeviceOrLost()) RawSlot<38>(window.Get(), 1u);
    if (IsDeviceReady()) {
        if (mSceneDepth) {
            const HRESULT hr = mDevice->EndScene();
            if (FAILED(hr)) ReportFailure(hr, kLineEndSceneOnRelease);
        }
        mState->Reset(cDeviceStateDX::kFlushAll);
        mState->Flush(cDeviceStateDX::kFlushAll);
        cDeviceEvent event(cDeviceEvent::kReleasing);
        NotifyListeners(cDeviceEvent::kEventId, 0, &event);
    }
    WaitForGpu();
    mReleased = true;
    mResourceManager->ReleaseAll();
    mState->ReleaseDeviceObjects();
    RefreshDeviceEnum(mDeviceEnum, nullptr, mSettings.mFlags);
    ReleaseFrameObjects();
    ReleaseCom(mDevice);
    mFullscreen = false; // the four flags at +0x70 as one dword
    mDeviceLost = false;
    mDeviceFailed = false;
    mReleased = false;
    RawSlot<37>(window.Get(), 0u);
    RawSlot<26>(window.Get());
}

void cDeviceDX::ReleaseFrameObjects()
{
    ReleaseCom(mGpuQuery);
    ReleaseCom(mSyncSurface);
    ReleaseCom(mProbeTexture);
    for (uint32_t i = 0; i < mSwapChainCount; ++i) {
        if (cIGZUnknown *const chain = mSwapChains[i]) { // a counted copy, dropped at once
            chain->AddRef();
            chain->Release();
        }
        ResetCounted(mSwapChains[i]);
    }
    mSwapChainCount = 0;
    if (cIGZUnknown *const provider = mTargetProvider) {
        provider->AddRef();
        provider->Release();
    }
    ResetCounted(mTargetProvider);
}

// ---- cIDeviceDX: drawing ----

// 0x16a040.
void cDeviceDX::ProcessVertices(uint32_t srcStartIndex, uint32_t destIndex, uint32_t vertexCount, cIGZUnknown *destBuffer,
                                cIGZUnknown *declaration)
{
    mState->ResetTexCoordStates();
    // The answers are not released, as in the original. The original leaves them uninitialised, so a failed query
    // passes a stale stack word to the driver; ours passes null (not reproduced: the value is indeterminate).
    IDirect3DVertexBuffer9 *buffer = nullptr;
    IDirect3DVertexDeclaration9 *d3dDeclaration = nullptr;
    destBuffer->QueryInterface(kVertexBufferIid, reinterpret_cast<void **>(&buffer));
    declaration->QueryInterface(kVertexDeclarationIid, reinterpret_cast<void **>(&d3dDeclaration));
    GuardedProcessVertices(mDevice, srcStartIndex, destIndex, vertexCount, buffer, d3dDeclaration, SourceFile());
    mState->RestoreTexCoordStates();
}

// 0x167890: pending targets and viewport sent first; the depth and stencil cleared only when there is a depth surface
// (the stencil only when its format has stencil bits).
void cDeviceDX::Clear(uint32_t flags, const float *color, float z, uint32_t stencil)
{
    if (mState->mPending.Any()) mState->Flush(cDeviceStateDX::kFlushTargets | cDeviceStateDX::kFlushViewport);
    uint32_t clear = flags & D3DCLEAR_TARGET;
    if ((flags & D3DCLEAR_ZBUFFER) && mState->mWanted.depth) clear |= D3DCLEAR_ZBUFFER;
    if ((flags & D3DCLEAR_STENCIL) && mState->mWanted.depth) {
        D3DSURFACE_DESC desc{};
        const HRESULT hr = mState->mWanted.depth->GetDesc(&desc);
        if (FAILED(hr)) ReportFailure(hr, kLineDepthDesc);
        void *const formats = GetPixelFormatsManagerCached();
        if (HasStencilBits(As<ifaces::cIPixelFormatsManager>(formats)->GetFormat(desc.Format))) clear |= D3DCLEAR_STENCIL;
    }
    if (!clear) return;
    const HRESULT hr = mDevice->Clear(0, nullptr, clear, PackColor(color), z, stencil);
    if (FAILED(hr)) ReportFailure(hr, kLineClear);
}

// 0x167a60: each colour target in turn becomes the render target and is cleared over `rect`; the depth and stencil
// with the first only.
void cDeviceDX::ClearTargets(const void *targets, const int32_t *rect, uint32_t flags, const float *color, float z,
                             uint32_t stencil)
{
    const RenderTargets *const state = static_cast<const RenderTargets *>(targets);
    uint32_t clear = flags & D3DCLEAR_TARGET;
    if ((flags & D3DCLEAR_ZBUFFER) && state->mDepth) clear |= D3DCLEAR_ZBUFFER;
    if ((flags & D3DCLEAR_STENCIL) && state->mDepth &&
        HasStencilBits(As<ifaces::cISurface>(state->mDepth)->GetPixelFormat()))
        clear |= D3DCLEAR_STENCIL;
    if (!clear) return;
    IDirect3DSurface9 *depth = nullptr;
    if (state->mDepth) state->mDepth->QueryInterface(kD3DSurfaceIid, reinterpret_cast<void **>(&depth));
    D3DRECT area;
    std::memcpy(&area, rect, sizeof area);
    const D3DCOLOR packed = PackColor(color);
    // ORIGINAL-BUG: the targets' answers go into the colour argument's stack slot, so a target that does not answer
    // is cleared through the previous answer (the first time, the colour pointer itself).
    void *surface = const_cast<float *>(color);
    for (cIGZUnknown *const target : state->mColors) {
        if (!target) continue;
        target->QueryInterface(kD3DSurfaceIid, &surface);
        const uint32_t height = As<ifaces::cISurface>(target)->GetHeight();
        const uint32_t width = As<ifaces::cISurface>(target)->GetWidth();
        mState->SetBackBufferTarget(static_cast<IDirect3DSurface9 *>(surface), depth, width, height);
        const HRESULT hr = mDevice->Clear(1, &area, clear, packed, z, stencil);
        if (FAILED(hr)) ReportFailure(hr, kLineClearTargets);
        clear &= ~(D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL);
        if (!clear) break;
    }
    mState->MarkTargetsDirty();
}

// 0x167850: the outermost call sends every pending state first.
void cDeviceDX::BeginScene()
{
    if (++mSceneDepth != 1) return;
    mState->Flush(cDeviceStateDX::kFlushAll);
    const HRESULT hr = mDevice->BeginScene();
    if (FAILED(hr)) ReportFailure(hr, kLineBeginScene);
}

// 0x169660.
void cDeviceDX::EndScene()
{
    if (--mSceneDepth != 0) return;
    const HRESULT hr = mDevice->EndScene();
    if (FAILED(hr)) ReportFailure(hr, kLineEndScene);
}

// 0x169570: the engine's primitive types are D3DPRIMITIVETYPE - 1.
void cDeviceDX::DrawPrimitive(uint32_t type, uint32_t primitiveCount, uint32_t startVertex)
{
    CheckTextureBudget(mResourceManager);
    if (mState->mPending.Any()) mState->Flush(cDeviceStateDX::kFlushAll);
    const HRESULT hr = mDevice->DrawPrimitive(static_cast<D3DPRIMITIVETYPE>(type + 1), startVertex, primitiveCount);
    if (FAILED(hr)) ReportFailure(hr, kLineDrawPrimitive);
}

// 0x1695e0.
void cDeviceDX::DrawIndexedPrimitive(uint32_t type, uint32_t primitiveCount, int32_t baseVertex, uint32_t minIndex,
                                     uint32_t vertexCount, uint32_t startIndex)
{
    CheckTextureBudget(mResourceManager);
    if (mState->mPending.Any()) mState->Flush(cDeviceStateDX::kFlushAll);
    const HRESULT hr = mDevice->DrawIndexedPrimitive(static_cast<D3DPRIMITIVETYPE>(type + 1), baseVertex, minIndex,
                                                     vertexCount, startIndex, primitiveCount);
    if (FAILED(hr)) ReportFailure(hr, kLineDrawIndexedPrimitive);
}

// ---- cIDeviceDX: frames ----

// 0x169740: with the event query, issue it and spin until the GPU is through; without one, clear the 16x16 sync
// surface (a new colour each time), lock it and touch its first byte, which waits for the GPU as well.
void cDeviceDX::WaitForGpu()
{
    if (!IsDeviceReady() || mDeviceLost || (!mGpuQuery && !mSyncSurface)) return;
    mState->Flush(cDeviceStateDX::kFlushAll);
    if (mGpuQuery) {
        const HRESULT hr = mGpuQuery->Issue(D3DISSUE_END);
        if (FAILED(hr)) ReportFailure(hr, kLineQueryIssue);
        DWORD done;
        while (mGpuQuery->GetData(&done, sizeof done, D3DGETDATA_FLUSH) == S_FALSE) {
        }
        return;
    }
    if (!mState->mWanted.targets[0]) return;
    mState->SetBackBufferTarget(mSyncSurface, nullptr, kSyncSurfaceSize, kSyncSurfaceSize);
    uint32_t &clearColor = ts2_rva_10ca21c;
    HRESULT hr = mDevice->Clear(0, nullptr, D3DCLEAR_TARGET, clearColor, 0.0f, 0);
    if (FAILED(hr)) ReportFailure(hr, kLineSyncClear);
    clearColor += 0x08080808;
    D3DLOCKED_RECT locked;
    hr = mSyncSurface->LockRect(&locked, nullptr, kSyncLockFlags);
    if (FAILED(hr)) ReportFailure(hr, kLineSyncLock);
    *static_cast<uint8_t *>(locked.pBits) ^= 0x88;
    hr = mSyncSurface->UnlockRect();
    if (FAILED(hr)) ReportFailure(hr, kLineSyncUnlock);
    mState->MarkTargetsDirty();
}

// 0x169f70: a banner, the enumeration's and the state cache's dumps, to `stream` or a new debugger stream.
void cDeviceDX::DumpInfo(cIGZUnknown *stream)
{
    if (!stream) stream = reinterpret_cast<cIGZUnknown *>(static_cast<cRZTextODSStream *>(::operator new(0x10))->Construct());
    if (stream) stream->AddRef();
    ifaces::cIGZOStream *const out = As<ifaces::cIGZOStream>(stream);
    out->SetRZCharStr(ts2_rva_d9e598); // a rule
    out->SetRZCharStr(ts2_rva_d9e5f0); // "Device info"
    out->SetRZCharStr(ts2_rva_d9e598);
    static_cast<cDeviceEnumDX *>(As<cIDeviceEnum>(mDeviceEnum))->DumpInfo(stream);
    mState->DumpInfo(stream);
    out->SetRZCharStr(ts2_rva_d9e644); // a newline
    stream->Release();
}

// 0x1699b0.
void cDeviceDX::HandleCanvasEvent(const uint32_t *message)
{
    const uint32_t kind = message[1];
    if ((kind != kCanvasPaint && kind != kCanvasKind7) || mReleased) return;
    if (!mDevice && mDeviceLost && !CreateDevice()) {
        OnDeviceLost();
        SleepBriefly();
    }
    if (!mDevice) return;
    HRESULT hr = mDevice->TestCooperativeLevel();
    if (kind == kCanvasPaint && hr == D3D_OK && !mDeviceLost) hr = PresentFrame();
    if (hr == D3DERR_DRIVERINTERNALERROR) {
        if (mByte74 || !CreateDevice()) ThrowGraphicsError();
        return;
    }
    mByte74 = false;
    if (hr == D3DERR_DEVICELOST) {
        OnDeviceLost();
        SleepBriefly();
    } else if (hr == D3DERR_DEVICENOTRESET && !CreateDevice()) {
        OnDeviceLost();
    }
}

int32_t cDeviceDX::PresentFrame()
{
    try {
        cDeviceRenderSceneEvent event(cDeviceRenderSceneEvent::kBeforeScene);
        NotifyListeners(cDeviceRenderSceneEvent::kEventId, 0, &event);
        BeginScene();
        event.mCode = cDeviceRenderSceneEvent::kInScene;
        NotifyListeners(cDeviceRenderSceneEvent::kEventId, 0, &event);
        EndScene();
        event.mCode = cDeviceRenderSceneEvent::kAfterScene;
        NotifyListeners(cDeviceRenderSceneEvent::kEventId, 0, &event);
        const HRESULT hr = mDevice->Present(nullptr, nullptr, nullptr, nullptr);
        if (mSettings.mFlags & kFlagWaitForGpu) WaitForGpu();
        return hr;
    } catch (const nRZGraphic4::cD3DException &error) {
        if (mSceneDepth) {
            mSceneDepth = 0;
            mDevice->EndScene();
        }
        return reinterpret_cast<const int32_t *>(&error)[1]; // the HRESULT at +4
    }
}

} // namespace nGZGraphic4
